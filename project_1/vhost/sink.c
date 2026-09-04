/* sink.c: the log sink for the virtio backend (PROVIDED).
 *
 * One emitted descriptor chain = one record. We accept the same
 * "<level> <message>" wire format the network ingest path uses and append
 * through the same thread-safe log store the MMIO device writes to, so records
 * from all three transports land in one log, in one format.
 *
 * This is deliberately separate from backend.c so the virtqueue tests can link
 * your virtio.c against it without dragging in vhost-user or QEMU.
 */
#include "virtq.h"
#include "logstore.h"

#include <stdlib.h>

struct vlog_sink {
    logstore *store;
    uint32_t  seq;
};

struct vlog_sink *vlog_sink_open(const char *path)
{
    struct vlog_sink *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->store = logstore_open(path);
    if (!s->store) { free(s); return NULL; }
    return s;
}

void vlog_sink_close(struct vlog_sink *s)
{
    if (!s) return;
    logstore_close(s->store);
    free(s);
}

void vlog_sink_emit(struct vlog_sink *sink, const void *bytes, uint32_t len)
{
    const char *p = bytes;

    /* the guest writes lines; strip the trailing newline */
    while (len && (p[len - 1] == '\n' || p[len - 1] == '\r'))
        len--;
    if (!len)
        return;

    /* "<level> <message>": a leading decimal 0..9 followed by a space */
    uint32_t level = 1 /* INFO */;
    if (len >= 2 && p[0] >= '0' && p[0] <= '9' && p[1] == ' ') {
        level = (uint32_t)(p[0] - '0');
        p += 2;
        len -= 2;
    }
    logstore_append(sink->store, sink->seq++, level, p, len);
}
