/* vlog.h: provided guest-side library for the logging device + console.
 *
 * This is the "C library to talk to the MMIO registers" referenced in the
 * project description. It is compiled into each bare-metal guest program.
 *
 * The register constants MUST match the emulator's ../emulator/device.h.
 */
#ifndef VLOG_H
#define VLOG_H

#include <stdint.h>

/* ---- Memory map (must match emulator/vmm.h) -------------------------- */
#define RAM_BASE        0x00100000ULL
#define RAM_SIZE        (16u * 1024u * 1024u)

#define SERIAL_BASE     0x10000000ULL
#define SERIAL_TX       0x00
#define SERIAL_POWEROFF 0x08

#define DEV_BASE        0x20000000ULL

/* ---- Device registers (must match emulator/device.h) ----------------- */
#define VLOG_REG_ID        0x00
#define VLOG_REG_VERSION   0x04
#define VLOG_REG_STATUS    0x08
#define VLOG_REG_CMD       0x0C
#define VLOG_REG_MSG_LO    0x10
#define VLOG_REG_MSG_HI    0x14
#define VLOG_REG_LEN       0x18
#define VLOG_REG_LEVEL     0x1C
#define VLOG_REG_SEQ       0x20

#define VLOG_MAGIC      0x31474C56
#define VLOG_VERSION    1
#define VLOG_MAX_MSG    4096u

#define VLOG_CMD_NOP    0
#define VLOG_CMD_LOG    1
#define VLOG_CMD_FLUSH  2
#define VLOG_CMD_STAT   3

/* Written back into guest memory by VLOG_CMD_STAT (must match device.h). */
struct vlog_stats {
    uint32_t records;
    uint32_t bytes;
};

#define VLOG_LVL_DEBUG  0
#define VLOG_LVL_INFO   1
#define VLOG_LVL_WARN   2
#define VLOG_LVL_ERROR  3

#define VLOG_STATUS_ERROR (1u << 2)

#define VLOG_ERR_NONE     0
#define VLOG_ERR_BADCMD   1
#define VLOG_ERR_BADADDR  2
#define VLOG_ERR_BADLEN   3

/* ---- Raw MMIO accessors ---------------------------------------------- */
static inline void mmio_w32(uint64_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}
static inline uint32_t mmio_r32(uint64_t addr)
{
    return *(volatile uint32_t *)addr;
}

/* ---- Serial console + shutdown --------------------------------------- */
void putc_(char c);
void puts_(const char *s);
void puthex_(uint32_t v);
void vm_exit(int code) __attribute__((noreturn));

/* ---- Logging device API ---------------------------------------------- */
uint32_t vlog_id(void);
uint32_t vlog_status(void);
uint32_t vlog_seq(void);

/* Log `len` bytes at `msg` with the given severity level. Returns 0 on
 * success, or the device error code if the device reported an error. */
int vlog_write(uint32_t level, const void *msg, uint32_t len);

/* Convenience: log a NUL-terminated string. */
int vlog(uint32_t level, const char *s);

/* Ask the device to write its stats into `out`. Returns 0 on success, or the
 * device error code. `len` is the size of the buffer you are offering. */
int vlog_stat(struct vlog_stats *out, uint32_t len);

#endif /* VLOG_H */
