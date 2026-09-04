/* device.h: MMIO logging device ABI for Project 1.
 *
 * This header defines the register layout and command/status encoding that
 * both the emulator-side device (device.c) and the guest-side library
 * (../guest/vlog.h) agree on. The two copies of these constants MUST stay in
 * sync.
 *
 * The device is a paravirtual log sink: the guest (the function runner) hands
 * it a message buffer + severity level, and the device appends the message to
 * a log file on the host, via the provided thread-safe log store (logstore.h).
 * Students implement the behavior in device.c; see SPEC.md Part II.
 */
#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>
#include <unicorn/unicorn.h>

struct vmm; /* defined in vmm.h */

/* ---- Register offsets (relative to DEV_BASE), all 32-bit ------------- */
#define VLOG_REG_ID        0x00   /* RO: magic identifier (VLOG_MAGIC)       */
#define VLOG_REG_VERSION   0x04   /* RO: device version                      */
#define VLOG_REG_STATUS    0x08   /* RO: status / flags (see below)          */
#define VLOG_REG_CMD       0x0C   /* WO: write a command code to execute it   */
#define VLOG_REG_MSG_LO    0x10   /* RW: guest-phys message address, low 32   */
#define VLOG_REG_MSG_HI    0x14   /* RW: guest-phys message address, high 32  */
#define VLOG_REG_LEN       0x18   /* RW: message length in bytes              */
#define VLOG_REG_LEVEL     0x1C   /* RW: severity level for the next LOG      */
#define VLOG_REG_SEQ       0x20   /* RO: number of records logged so far      */

#define VLOG_MAGIC    0x31474C56  /* "VLG1" little-endian                     */
#define VLOG_VERSION  1

/* Largest single log message the device accepts, in bytes. */
#define VLOG_MAX_MSG  4096u

/* ---- STATUS bits ----------------------------------------------------- */
#define VLOG_STATUS_READY      (1u << 0)
#define VLOG_STATUS_ERROR      (1u << 2)
#define VLOG_STATUS_ERR_SHIFT  8          /* error code lives in bits [15:8] */

/* ---- Commands -------------------------------------------------------- */
#define VLOG_CMD_NOP    0   /* do nothing, stay READY                        */
#define VLOG_CMD_LOG    1   /* append [MSG, MSG+LEN) to the host log         */
#define VLOG_CMD_FLUSH  2   /* flush the host log file to disk               */
#define VLOG_CMD_STAT   3   /* write device stats INTO the guest buffer      */

/* ---- Severity levels ------------------------------------------------- */
#define VLOG_LVL_DEBUG  0
#define VLOG_LVL_INFO   1
#define VLOG_LVL_WARN   2
#define VLOG_LVL_ERROR  3

/* ---- Error codes ----------------------------------------------------- */
#define VLOG_ERR_NONE     0
#define VLOG_ERR_BADCMD   1   /* unknown command code                        */
#define VLOG_ERR_BADADDR  2   /* message range not entirely within guest RAM */
#define VLOG_ERR_BADLEN   3   /* message length exceeds VLOG_MAX_MSG         */

/* The stats block the device writes back into GUEST memory on VLOG_CMD_STAT.
 * Note the direction: every other command reads guest memory; this one WRITES
 * to it, so the device must check that the guest's buffer is really big enough
 * and really inside guest RAM before touching it. */
struct vlog_stats {
    uint32_t records;   /* records successfully logged (same as SEQ)         */
    uint32_t bytes;     /* total message bytes logged (excludes the prefix)   */
};

/* Device instance state.
 *
 * The provided fields below are a suggested starting point that matches the
 * register ABI. Students may add to this struct as their implementation
 * grows. */
struct vlog_device {
    struct vmm *vmm;    /* back-reference for vmm_gpa_to_host() + log store   */
    uint32_t    status;
    uint32_t    msg_addr_lo;
    uint32_t    msg_addr_hi;
    uint32_t    len;
    uint32_t    level;
    uint32_t    seq;    /* records successfully logged                       */
    uint32_t    bytes;  /* total message bytes logged (for VLOG_CMD_STAT)     */
};

/* Called once at startup by the VMM (the device-registration hook). */
void vlog_device_init(struct vlog_device *dev, struct vmm *vmm);

/* MMIO callbacks registered with Unicorn. `offset` is relative to DEV_BASE. */
uint64_t vlog_device_mmio_read(uc_engine *uc, uint64_t offset,
                               unsigned size, void *user_data);
void     vlog_device_mmio_write(uc_engine *uc, uint64_t offset,
                                unsigned size, uint64_t value, void *user_data);

#endif /* DEVICE_H */
