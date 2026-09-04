/* virtq.h: the virtqueue interface for Part III (PROVIDED).
 *
 * This is the seam between the provided vhost-user plumbing (backend.c, which
 * speaks the protocol to QEMU, maps guest memory, and sets up the rings) and
 * YOUR device logic (virtio.c, which processes the queue).
 *
 * Everything here is REAL virtio: `struct vring_desc/avail/used` are the actual
 * split-virtqueue structures from the virtio specification (standard-headers/),
 * laid out by the guest driver in GUEST memory. Your job is the same one you did
 * in Part II, but with the real protocol instead of the toy one:
 *
 *   Part II (MMIO)                     Part III (virtio)
 *   -------------------------------    ------------------------------------
 *   guest writes CMD register       -> guest kicks the AVAIL ring
 *   read MSG_LO/MSG_HI + LEN regs   -> walk a DESCRIPTOR CHAIN
 *   vmm_gpa_to_host(gpa, len)       -> gpa(ctx, addr, len)   [same idea]
 *   write STATUS / advance SEQ      -> complete on the USED ring
 *
 * Note the rings live in memory the GUEST owns and can change at any time:
 * validate what you read, and never trust a length or index blindly.
 */
#ifndef VIRTQ_H
#define VIRTQ_H

#include <stdint.h>

#include "standard-headers/linux/virtio_ring.h"

/* ---- The guest's memory map, as the hypervisor sees it -----------------
 *
 * QEMU shares the guest's RAM with us and tells us where each piece lives. A
 * region says: "guest-physical [gpa, gpa+size) is mapped at `hva` in OUR address
 * space." There may be several, they are NOT contiguous, and a guest-physical
 * range that straddles two of them is NOT a valid buffer.
 *
 * This is the real version of the map you built in Part I, where guest RAM was
 * one region at RAM_BASE backed by v->ram. */
struct virtq_mem_region {
    uint64_t gpa;    /* guest-physical base of this region                    */
    uint64_t size;   /* its length                                            */
    uint8_t *hva;    /* where guest-physical `gpa` is mapped in our process    */
};

struct virtq_mem {
    const struct virtq_mem_region *regions;
    unsigned                       nregions;
};

/* YOU IMPLEMENT (virtio.c): translate the guest-physical range [gpa, gpa+len)
 * to a host pointer, or return NULL if it is not ENTIRELY inside a single
 * mapped region.
 *
 * This is vmm_gpa_to_host() again: same contract, real hypervisor. The guest
 * chooses these addresses and lengths, so it decides what you dereference:
 * reject a range that runs off the end of its region, that falls in a gap
 * between regions, or whose gpa+len overflows. */
void *virtq_gpa_to_hva(const struct virtq_mem *mem, uint64_t gpa, uint64_t len);

/* One split virtqueue. desc/avail/used are HOST pointers to the three rings
 * (the backend already translated the ring addresses for you); the addresses
 * *inside* the descriptors are still guest-physical, and those are yours to
 * translate with gpa(). */
struct virtq {
    struct vring_desc  *desc;       /* descriptor table, `num` entries        */
    struct vring_avail *avail;      /* driver -> device: chains ready to run  */
    struct vring_used  *used;       /* device -> driver: chains completed     */
    uint16_t            num;        /* ring size (a power of two)             */
    uint16_t            last_avail; /* next avail entry to consume; YOU advance it */
};

/* The rings are shared with the guest, so ordering matters: acquire before you
 * read what the driver published, release before you publish to the driver. */
#define virtq_rmb() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define virtq_wmb() __atomic_thread_fence(__ATOMIC_RELEASE)

/* The log sink (PROVIDED, opaque). One call = one record appended to the same
 * log the MMIO device writes: it parses the "<level> <message>" wire format,
 * assigns the sequence number, and appends through the shared log store. */
struct vlog_sink;
struct vlog_sink *vlog_sink_open(const char *path);
void              vlog_sink_close(struct vlog_sink *s);
void              vlog_sink_emit(struct vlog_sink *sink, const void *bytes, uint32_t len);

/* Largest record we accept from one descriptor chain. */
#define VIRTQ_MAX_RECORD 4096u

/* ---- YOUR TASK (virtio.c) -------------------------------------------------
 *
 * Process every chain the guest has made available on `vq`: for each one, read
 * the record bytes out of guest memory (translating with virtq_gpa_to_hva),
 * emit it to `sink`, and complete the chain on the used ring. Return the number
 * of chains completed (the backend raises the guest's interrupt if you return
 * > 0).
 *
 * A descriptor with VRING_DESC_F_INDIRECT does not hold data: its address points
 * at a TABLE of further descriptors in guest memory (`len` bytes of them), which
 * form their own chain starting at index 0. Handle those too.
 *
 * See SPEC.md Part III. */
int vlog_virtq_handle(struct virtq *vq, const struct virtq_mem *mem,
                      struct vlog_sink *sink);

#endif /* VIRTQ_H */
