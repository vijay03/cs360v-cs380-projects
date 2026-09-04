/* test_nop.c: sample guest program: discovery + NOP.
 *
 *   ../emulator/emulator test_nop.bin
 *
 * The simplest smoke test: confirm the device is present and that NOP works
 * without touching the log.
 */
#include "vlog.h"

int main(void)
{
    puts_("guest: booting\n");

    if (vlog_id() != VLOG_MAGIC) {
        puts_("guest: device MISSING\n");
        return 1;
    }
    puts_("guest: device discovered\n");

    /* NOP via the raw register interface. */
    mmio_w32(DEV_BASE + VLOG_REG_CMD, VLOG_CMD_NOP);
    if (vlog_status() & VLOG_STATUS_ERROR) {
        puts_("guest: nop FAILED\n");
        return 1;
    }
    puts_("guest: nop ok\n");

    puts_("guest: done\n");
    return 0;
}
