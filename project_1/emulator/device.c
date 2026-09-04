/* device.c: STUDENT IMPLEMENTATION FILE for Project 1 (Part II).
 *
 * You implement the paravirtual logging device here. The emulator (vmm.c) maps
 * a DEV_SIZE-byte MMIO region at DEV_BASE and routes every read/write within it
 * to vlog_device_mmio_read() / vlog_device_mmio_write() below.
 *
 * The device reads a log message from guest memory and hands it, via the
 * provided log store, to the host log file. The store lives
 * in the VMM (dev->vmm->store); you append validated records with
 * logstore_append(store, seq, level, bytes, len). It owns the record format
 * and is thread-safe (the provided network ingest path shares it).
 *
 * WHAT IS PROVIDED: the register ABI and device state (device.h),
 * vlog_device_init(), the log store (logstore_append / logstore_flush), and the
 * small helpers below (msg_addr / set_error / clear_error). Use them or ignore
 * them as you see fit.
 *
 * YOUR TASK: implement the two MMIO handlers:
 *   - vlog_device_mmio_read():  return the value of the addressed register.
 *   - vlog_device_mmio_write(): update operand registers, and on a CMD write
 *                               execute the command (NOP / LOG / FLUSH).
 * See SPEC.md for the register map (§2), STATUS/errors (§3), and commands (§4).
 *
 * Rules:
 *   - Access the guest message buffer ONLY through vmm_gpa_to_host(). Never cast
 *     a guest address to a host pointer yourself, and always check the result.
 *   - Report errors through the STATUS register (use set_error()); do not crash
 *     the emulator on malformed input, and do not write a partial record to the
 *     log when an error is detected.
 */
#include "device.h"
#include "vmm.h"

/* ---- Helpers (provided; use if you find them handy) ------------------ */

/* Reassemble the 64-bit guest-physical message address from its two halves. */
static inline uint64_t msg_addr(const struct vlog_device *dev)
{
    return (uint64_t)dev->msg_addr_lo | ((uint64_t)dev->msg_addr_hi << 32);
}

/* Set the ERROR flag with an error code (SPEC.md §3). */
static inline void set_error(struct vlog_device *dev, uint32_t code)
{
    dev->status |= VLOG_STATUS_ERROR | (code << VLOG_STATUS_ERR_SHIFT);
}

/* Clear the ERROR flag and error code. */
static inline void clear_error(struct vlog_device *dev)
{
    dev->status &= ~(VLOG_STATUS_ERROR | (0xffu << VLOG_STATUS_ERR_SHIFT));
}

/* ---- Provided: one-time device state initialization ------------------ */

void vlog_device_init(struct vlog_device *dev, struct vmm *vmm)
{
    dev->vmm         = vmm;
    dev->status      = VLOG_STATUS_READY;
    dev->msg_addr_lo = 0;
    dev->msg_addr_hi = 0;
    dev->len         = 0;
    dev->level       = VLOG_LVL_INFO;
    dev->seq         = 0;
    dev->bytes       = 0;
}

/* ---- Your task: the MMIO handlers ------------------------------------ */

uint64_t vlog_device_mmio_read(uc_engine *uc, uint64_t offset,
                               unsigned size, void *user_data)
{
    (void)uc; (void)size;
    struct vlog_device *dev = user_data;
    (void)dev; (void)offset;

    /* TODO(student): return the 32-bit value of the register at `offset`
     * (relative to DEV_BASE): ID, VERSION, STATUS, MSG_LO, MSG_HI, LEN, LEVEL,
     * SEQ. Return 0 for any other offset. See SPEC.md §2. */
    return 0;
}

void vlog_device_mmio_write(uc_engine *uc, uint64_t offset,
                            unsigned size, uint64_t value, void *user_data)
{
    (void)uc; (void)size;
    struct vlog_device *dev = user_data;
    (void)dev; (void)offset; (void)value;

    /* TODO(student): handle writes by `offset`:
     *   - operand registers (MSG_LO / MSG_HI / LEN / LEVEL): store the value;
     *   - CMD: execute the command:
     *       NOP:   clear any error, do nothing else;
     *       LOG:   validate LEN (<= VLOG_MAX_MSG) and, if LEN > 0, translate
     *               [MSG, MSG+LEN) with vmm_gpa_to_host(); then hand the record
     *               to the store:
     *                 logstore_append(dev->vmm->store, dev->seq, dev->level,
     *                                 msg, dev->len);
     *               and advance dev->seq. On error use set_error() and append
     *               nothing;
     *       FLUSH: logstore_flush(dev->vmm->store);
     *       STAT:  write a `struct vlog_stats` (records + total message bytes
     *               logged) INTO the guest's buffer at [MSG, MSG+LEN). This is
     *               the one command that writes to guest memory, so check that
     *               the guest really offered you enough room (LEN >= the struct,
     *               else ERR_BADLEN) and that the range is really in guest RAM
     *               (vmm_gpa_to_host, else ERR_BADADDR) BEFORE you write. It
     *               does not touch the log or SEQ. Track the byte count in
     *               dev->bytes as you log;
     *       other: set_error(VLOG_ERR_BADCMD);
     *   - read-only registers and unknown offsets: ignore the write.
     * See SPEC.md §3 (errors) and §4 (commands). */
}
