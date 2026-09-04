/* logstore.h: thread-safe log store core (PROVIDED).
 *
 * The single sink that all ingest paths append to: the device's MMIO adapter
 * (device.c, on the vCPU thread) and the network ingest adapter (netlog.c, on a
 * listener thread) both call logstore_append(). The store owns the record
 * format and the on-disk file, guarded by a mutex so concurrent producers
 * cannot corrupt it (no torn/interleaved records). Each producer supplies its
 * own sequence number.
 *
 * This module has NO emulator/Unicorn dependencies on purpose: the SAME store
 * is reused by the virtio backend (Part III) and lifts out unchanged into the
 * standalone collector later. Keep it that way (no uc_*, no struct vmm).
 */
#ifndef LOGSTORE_H
#define LOGSTORE_H

#include <stdint.h>

typedef struct logstore logstore;

/* Open (truncate) the log file at `path`. Returns NULL on failure. */
logstore *logstore_open(const char *path);
void      logstore_close(logstore *ls);

/* Append one record, formatted as "[<seq>] <LEVEL> <bytes>\n". `level` outside
 * 0..3 is written with the placeholder name "LVL?"; a zero `len` logs an empty
 * message. Thread-safe: callable concurrently from multiple ingest paths. */
void      logstore_append(logstore *ls, uint32_t seq, uint32_t level,
                          const void *bytes, uint32_t len);

/* Flush to disk. Thread-safe. */
void      logstore_flush(logstore *ls);

#endif /* LOGSTORE_H */
