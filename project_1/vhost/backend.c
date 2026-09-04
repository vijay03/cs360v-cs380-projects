/* backend.c: vhost-user log device backend (PROVIDED).
 *
 * The plumbing for Part III: it speaks the vhost-user protocol to QEMU over a
 * UNIX socket (feature negotiation, mapping the guest's memory, setting up the
 * virtqueues, kick/call eventfds), then hands each queue kick to YOUR
 * vlog_virtq_handle() in virtio.c.
 *
 * QEMU attaches this as a generic vhost-user device carrying the virtio-console
 * device ID, so the guest's STOCK virtio_console driver binds to it with no guest
 * driver to write. Whatever the guest writes to /dev/hvc0 arrives on the TX
 * virtqueue as a descriptor chain, which is what you process.
 *
 *   ./vlog-backend <socket> <logfile>
 *
 * Launch it via ./run-qemu.sh, which starts this and then boots the VM.
 */
#include "libvhost-user.h"
#include "standard-headers/linux/virtio_config.h"
#include "virtq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>

/* virtio-console queue indices: 0 = RX (host->guest), 1 = TX (guest->host). */
#define VQ_TX 1

/* ---- vhost-user plumbing (provided) ----------------------------------- */

#define MAXW 32
static struct { int fd; vu_watch_cb cb; void *data; } W[MAXW];
static int nW;

static struct vlog_sink *g_sink;
static uint64_t         g_features;   /* what the guest negotiated */

static void set_watch(VuDev *dev, int fd, int cond, vu_watch_cb cb, void *data)
{
    (void)dev; (void)cond;
    for (int i = 0; i < nW; i++)
        if (W[i].fd == fd) { W[i].cb = cb; W[i].data = data; return; }
    if (nW < MAXW) { W[nW].fd = fd; W[nW].cb = cb; W[nW].data = data; nW++; }
}

static void remove_watch(VuDev *dev, int fd)
{
    (void)dev;
    for (int i = 0; i < nW; i++)
        if (W[i].fd == fd) { W[i] = W[--nW]; return; }
}

static void panic_cb(VuDev *dev, const char *msg)
{
    (void)dev;
    fprintf(stderr, "[vlog-backend] panic: %s\n", msg);
    exit(1);
}

/* Hand the student's code the guest's memory map: vhost-user gives us a table of
 * regions, each mmap'd into our address space. Translating an address inside
 * them is their job (virtq_gpa_to_hva): the same job as Part I. */
static struct virtq_mem_region g_regions[VHOST_USER_MAX_RAM_SLOTS];

static void build_mem(VuDev *dev, struct virtq_mem *mem)
{
    unsigned n = 0;
    for (unsigned i = 0; i < dev->nregions && n < VHOST_USER_MAX_RAM_SLOTS; i++) {
        VuDevRegion *r = &dev->regions[i];
        g_regions[n].gpa  = r->gpa;
        g_regions[n].size = r->size;
        g_regions[n].hva  = (uint8_t *)(uintptr_t)(r->mmap_addr + r->mmap_offset);
        n++;
    }
    mem->regions  = g_regions;
    mem->nregions = n;
}

/* A queue was kicked: build the student's view of the ring and hand it over.
 *
 * Your handler implements the PLAIN split virtqueue: avail ring -> descriptor
 * chain -> used ring. Two optional virtio features that QEMU turns on are dealt
 * with here instead, because they are notification bookkeeping rather than ring
 * processing:
 *
 *  - VIRTIO_RING_F_EVENT_IDX: when negotiated, the driver only kicks us when the
 *    device publishes an `avail_event` index saying "kick me at this point". If
 *    we never publish it, the driver kicks once and then goes silent, and the queue
 *    stalls after the first buffer. We publish it below, after your handler runs.
 *  - VIRTIO_RING_F_INDIRECT_DESC: a descriptor may point at a whole table of
 *    further descriptors. The guest here (virtio_console) writes one contiguous
 *    buffer per record, so it never builds an indirect chain, and you will not
 *    see VRING_DESC_F_INDIRECT.
 */
static void tx_kick(VuDev *dev, int qidx)
{
    VuVirtq *vuq = vu_get_queue(dev, qidx);

    if (!vuq->vring.desc || !vuq->vring.avail || !vuq->vring.used || !vuq->vring.num)
        return;

    struct virtq vq = {
        .desc       = (struct vring_desc *)vuq->vring.desc,
        .avail      = (struct vring_avail *)vuq->vring.avail,
        .used       = (struct vring_used *)vuq->vring.used,
        .num        = vuq->vring.num,
        /* libvhost-user persists this across kicks and resets it when the guest
         * resets the device, so your progress survives but a fresh device starts
         * from zero. */
        .last_avail = vuq->last_avail_idx,
    };
    const bool event_idx = !!(g_features & (1ULL << VIRTIO_RING_F_EVENT_IDX));
    /* avail_event lives just past the used ring's entries */
    uint16_t *avail_event = (uint16_t *)&vq.used->ring[vq.num];

    struct virtq_mem mem;
    build_mem(dev, &mem);

    int total = 0;
    for (;;) {
        int n = vlog_virtq_handle(&vq, &mem, g_sink);
        total += n;
        vuq->last_avail_idx = vq.last_avail;

        if (!event_idx)
            break;

        /* Ask to be kicked again at the next index, then re-check: the driver
         * may have published a buffer after your handler read avail->idx but
         * before we asked. Without this re-check the queue can stall. */
        *avail_event = vq.last_avail;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (vq.avail->idx == vq.last_avail)
            break;
        if (n == 0)
            break;                     /* no progress; don't spin forever */
    }

    if (total > 0 && vuq->call_fd >= 0)
        eventfd_write(vuq->call_fd, 1);   /* raise the guest's interrupt */
}

static void queue_set_started(VuDev *dev, int qidx, bool started)
{
    VuVirtq *vq = vu_get_queue(dev, qidx);
    if (qidx == VQ_TX)
        vu_set_queue_handler(dev, vq, started ? tx_kick : NULL);
}

static uint64_t get_features(VuDev *dev)
{
    (void)dev;
    return 1ULL << VIRTIO_F_VERSION_1;
}

static void set_features(VuDev *dev, uint64_t f) { (void)dev; g_features = f; }

static const VuDevIface iface = {
    .get_features      = get_features,
    .set_features      = set_features,
    .queue_set_started = queue_set_started,
};

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <socket> <logfile>\n", argv[0]);
        return 2;
    }

    g_sink = vlog_sink_open(argv[2]);
    if (!g_sink) { fprintf(stderr, "cannot open log '%s'\n", argv[2]); return 1; }

    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", argv[1]);
    unlink(argv[1]);
    if (bind(lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(lfd, 1) < 0) {
        perror("bind/listen");
        return 1;
    }
    fprintf(stderr, "[vlog-backend] listening on %s -> %s\n", argv[1], argv[2]);

    int cfd = accept(lfd, NULL, NULL);
    if (cfd < 0) { perror("accept"); return 1; }
    fprintf(stderr, "[vlog-backend] QEMU connected\n");

    VuDev dev;
    if (!vu_init(&dev, 2, cfd, panic_cb, NULL, set_watch, remove_watch, &iface)) {
        fprintf(stderr, "[vlog-backend] vu_init failed\n");
        return 1;
    }

    for (;;) {
        struct pollfd p[MAXW + 1];
        int n = 0;
        p[n].fd = cfd; p[n].events = POLLIN; n++;
        for (int i = 0; i < nW; i++) { p[n].fd = W[i].fd; p[n].events = POLLIN; n++; }
        if (poll(p, n, -1) < 0) break;

        if (p[0].revents & (POLLIN | POLLHUP)) {
            if (!vu_dispatch(&dev)) {
                fprintf(stderr, "[vlog-backend] QEMU disconnected\n");
                break;
            }
        }
        for (int i = 1; i < n; i++) {
            if (!(p[i].revents & POLLIN)) continue;
            for (int j = 0; j < nW; j++)
                if (W[j].fd == p[i].fd) { W[j].cb(&dev, VU_WATCH_IN, W[j].data); break; }
        }
    }

    vu_deinit(&dev);
    vlog_sink_close(g_sink);
    return 0;
}
