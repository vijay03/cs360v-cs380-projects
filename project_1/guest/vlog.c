/* vlog.c: provided guest-side library implementation. */
#include "vlog.h"

void putc_(char c)
{
    mmio_w32(SERIAL_BASE + SERIAL_TX, (uint8_t)c);
}

void puts_(const char *s)
{
    while (*s)
        putc_(*s++);
}

void puthex_(uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    putc_('0');
    putc_('x');
    for (int i = 28; i >= 0; i -= 4)
        putc_(d[(v >> i) & 0xf]);
}

void vm_exit(int code)
{
    mmio_w32(SERIAL_BASE + SERIAL_POWEROFF, (uint32_t)code);
    for (;;) { /* the emulator halts on the write above */ }
}

uint32_t vlog_id(void)
{
    return mmio_r32(DEV_BASE + VLOG_REG_ID);
}

uint32_t vlog_status(void)
{
    return mmio_r32(DEV_BASE + VLOG_REG_STATUS);
}

uint32_t vlog_seq(void)
{
    return mmio_r32(DEV_BASE + VLOG_REG_SEQ);
}

int vlog_write(uint32_t level, const void *msg, uint32_t len)
{
    /* With paging disabled, a guest pointer is also a guest-physical address,
     * so we can pass it straight to the device. */
    uint64_t gpa = (uint64_t)(uintptr_t)msg;
    mmio_w32(DEV_BASE + VLOG_REG_MSG_LO, (uint32_t)gpa);
    mmio_w32(DEV_BASE + VLOG_REG_MSG_HI, (uint32_t)(gpa >> 32));
    mmio_w32(DEV_BASE + VLOG_REG_LEN, len);
    mmio_w32(DEV_BASE + VLOG_REG_LEVEL, level);
    mmio_w32(DEV_BASE + VLOG_REG_CMD, VLOG_CMD_LOG);

    uint32_t st = mmio_r32(DEV_BASE + VLOG_REG_STATUS);
    if (st & VLOG_STATUS_ERROR)
        return (int)((st >> 8) & 0xff);
    return 0;
}

int vlog(uint32_t level, const char *s)
{
    uint32_t len = 0;
    while (s[len])
        len++;
    return vlog_write(level, s, len);
}

int vlog_stat(struct vlog_stats *out, uint32_t len)
{
    uint64_t addr = (uint64_t)(uintptr_t)out;
    mmio_w32(DEV_BASE + VLOG_REG_MSG_LO, (uint32_t)(addr & 0xffffffffu));
    mmio_w32(DEV_BASE + VLOG_REG_MSG_HI, (uint32_t)(addr >> 32));
    mmio_w32(DEV_BASE + VLOG_REG_LEN, len);
    mmio_w32(DEV_BASE + VLOG_REG_CMD, VLOG_CMD_STAT);

    uint32_t st = mmio_r32(DEV_BASE + VLOG_REG_STATUS);
    if (st & VLOG_STATUS_ERROR)
        return (int)((st >> 8) & 0xff);
    return 0;
}
