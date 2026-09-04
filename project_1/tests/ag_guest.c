/* ag_guest.c — parameterized test guest (part of the shipped test suite).
 *
 * A single bare-metal guest that reads a boot-parameter blob (injected by the
 * harness via the emulator's --bootinfo) and exercises the device according to
 * the selected scenario, printing raw observations as "key=0x........" lines.
 * It makes no pass/fail decision — the host oracle holds the expected values.
 *
 * The SAME binary is driven with deterministic params (basic suite) or random
 * params (randomized suite); only the injected blob differs.
 *
 * Built against ../guest/{vlog,bootinfo}.h and linked with ../guest/{start.S,
 * vlog.c} using ../guest/link.ld.
 */
#include "vlog.h"
#include "bootinfo.h"

static void obs(const char *key, uint32_t val)
{
    puts_(key);
    putc_('=');
    puthex_(val);
    putc_('\n');
}

static uint32_t dev_err(void)
{
    return (vlog_status() & VLOG_STATUS_ERROR) ? 1u : 0u;
}
static uint32_t dev_errcode(void)
{
    return (vlog_status() >> 8) & 0xffu;
}

int main(uint64_t bootinfo_ptr)
{
    const struct bootinfo *bi = (const struct bootinfo *)(uintptr_t)bootinfo_ptr;
    if (bi->magic != BOOTINFO_MAGIC)
        return 1;   /* harness contract violated; host sees nonzero exit */

    const void *scratch = (const void *)(uintptr_t)bi->scratch_addr;

    switch (bi->scenario) {
    case SC_LOG_ONE: {
        uint32_t rc = vlog_write(bi->level, scratch, bi->scratch_len);
        obs("rc", rc);
        obs("err", dev_err());
        obs("seq", vlog_seq());
        break;
    }

    case SC_LOG_MANY: {
        uint32_t err = 0;
        for (uint32_t i = 0; i < bi->count; i++)
            err |= (uint32_t)vlog_write(bi->level, scratch, bi->scratch_len);
        obs("err", err ? 1u : 0u);
        obs("seq", vlog_seq());
        break;
    }

    case SC_STAT: {
        for (uint32_t i = 0; i < bi->count; i++)
            vlog_write(bi->level, scratch, bi->scratch_len);

        /* happy path: the device writes its stats INTO our buffer */
        struct vlog_stats st = { 0xdeadbeef, 0xdeadbeef };
        obs("stat_rc", (uint32_t)vlog_stat(&st, sizeof st));
        obs("records", st.records);
        obs("bytes",   st.bytes);
        obs("seq",     vlog_seq());

        /* buffer too small: refuse, and write NOTHING into it */
        struct vlog_stats small = { 0x11111111, 0x22222222 };
        obs("small_rc",        (uint32_t)vlog_stat(&small, bi->arg0));
        obs("small_untouched", small.records);

        /* address outside guest RAM: refuse */
        obs("badaddr_rc",
            (uint32_t)vlog_stat((struct vlog_stats *)(uintptr_t)bi->arg1, 8));
        break;
    }

    case SC_MALFORMED: {
        mmio_w32(DEV_BASE + VLOG_REG_CMD, 0x7f);   /* unknown command */
        obs("err_badcmd", dev_err());
        obs("errcode_badcmd", dev_errcode());
        obs("rc_badlen", (uint32_t)vlog_write(bi->level, scratch, bi->arg0));
        obs("rc_badaddr",
            (uint32_t)vlog_write(bi->level, (const void *)(uintptr_t)bi->arg1, 4));
        obs("seq", vlog_seq());
        break;
    }

    case SC_BOUNDARY: {
        uint64_t ram_top = RAM_BASE + RAM_SIZE;
        uint32_t len = bi->arg0;
        obs("rc_at_top",
            (uint32_t)vlog_write(bi->level, (const void *)(uintptr_t)(ram_top - len), len));
        obs("seq_after_top", vlog_seq());
        obs("rc_past_top",
            (uint32_t)vlog_write(bi->level, (const void *)(uintptr_t)(ram_top - len + 1), len));
        obs("seq_final", vlog_seq());
        break;
    }

    case SC_EMPTY: {
        uint32_t rc = vlog_write(bi->level, scratch, 0);
        obs("rc", rc);
        obs("err", dev_err());
        obs("seq", vlog_seq());
        break;
    }

    case SC_MAXLEN: {
        obs("rc_max", (uint32_t)vlog_write(bi->level, scratch, VLOG_MAX_MSG));
        obs("seq_after_max", vlog_seq());
        obs("rc_over", (uint32_t)vlog_write(bi->level, scratch, VLOG_MAX_MSG + 1));
        obs("seq_final", vlog_seq());
        break;
    }

    case SC_REGS: {
        mmio_w32(DEV_BASE + VLOG_REG_MSG_LO, bi->arg0);
        mmio_w32(DEV_BASE + VLOG_REG_MSG_HI, bi->arg1);
        mmio_w32(DEV_BASE + VLOG_REG_LEN, bi->count);
        mmio_w32(DEV_BASE + VLOG_REG_LEVEL, bi->level);
        obs("msg_lo", mmio_r32(DEV_BASE + VLOG_REG_MSG_LO));
        obs("msg_hi", mmio_r32(DEV_BASE + VLOG_REG_MSG_HI));
        obs("len", mmio_r32(DEV_BASE + VLOG_REG_LEN));
        obs("level", mmio_r32(DEV_BASE + VLOG_REG_LEVEL));
        mmio_w32(DEV_BASE + VLOG_REG_ID, 0xdeadbeef);   /* RO */
        mmio_w32(DEV_BASE + VLOG_REG_SEQ, 0x55555555);  /* RO */
        obs("id_after_ro", vlog_id());
        obs("seq_after_ro", vlog_seq());
        uint32_t s_before = vlog_status();
        mmio_w32(DEV_BASE + VLOG_REG_STATUS, 0xdeadbeef);   /* RO */
        obs("status_ro_unchanged", (vlog_status() == s_before) ? 1u : 0u);
        break;
    }

    case SC_NOP: {
        mmio_w32(DEV_BASE + VLOG_REG_CMD, bi->arg0);    /* unknown command */
        obs("err_after_badcmd", dev_err());
        obs("errcode_after_badcmd", dev_errcode());
        mmio_w32(DEV_BASE + VLOG_REG_CMD, VLOG_CMD_NOP);
        obs("err_after_nop", dev_err());
        obs("seq", vlog_seq());
        break;
    }

    case SC_RECOVER: {
        mmio_w32(DEV_BASE + VLOG_REG_CMD, bi->arg0);    /* error first */
        obs("err_after_bad", dev_err());
        uint32_t rc = vlog_write(bi->level, scratch, bi->scratch_len);
        obs("rc", rc);
        obs("err", dev_err());
        obs("seq", vlog_seq());
        break;
    }

    case SC_INTERLEAVE: {
        obs("rc1", (uint32_t)vlog_write(bi->level, scratch, bi->scratch_len));
        obs("rc2",
            (uint32_t)vlog_write(bi->level, (const void *)(uintptr_t)bi->arg1, 4));
        obs("rc3", (uint32_t)vlog_write(bi->level, scratch, bi->scratch_len));
        obs("seq", vlog_seq());
        break;
    }

    case SC_FLUSH: {
        obs("rc_log", (uint32_t)vlog_write(bi->level, scratch, bi->scratch_len));
        mmio_w32(DEV_BASE + VLOG_REG_CMD, VLOG_CMD_FLUSH);
        obs("err_after_flush", dev_err());
        obs("seq", vlog_seq());
        break;
    }

    default:
        return 1;
    }

    return 0;
}
