/* test_log.c: sample guest program exercising the logging device.
 *
 *   ../emulator/emulator --log out.log test_log.bin
 *   cat out.log
 *
 * Emits several records at different severity levels, then prints how many
 * records the device accepted (read back from the SEQ register).
 */
#include "vlog.h"

int main(void)
{
    puts_("guest: logging test\n");

    if (vlog_id() != VLOG_MAGIC) {
        puts_("guest: device MISSING\n");
        return 1;
    }

    vlog(VLOG_LVL_INFO,  "function runner started");
    vlog(VLOG_LVL_DEBUG, "processing request 1");
    vlog(VLOG_LVL_WARN,  "cache miss");
    vlog(VLOG_LVL_ERROR, "request 1 failed");

    puts_("guest: logged ");
    puthex_(vlog_seq());
    puts_(" records\n");

    puts_("guest: done\n");
    return 0;
}
