/* t_stat.c: VLOG_CMD_STAT: the device writes its stats INTO guest memory.
 *
 * Every other command reads guest memory; this one writes to it. So this test
 * also checks that the device refuses to write when the guest's buffer is too
 * small or its address is bogus, and that it leaves the buffer untouched when
 * it refuses.
 */
#include "tobs.h"
#include "vlog.h"

int main(void)
{
    vlog(VLOG_LVL_INFO,  "alpha");     /* 5 bytes */
    vlog(VLOG_LVL_WARN,  "bravo!");    /* 6 bytes */
    vlog(VLOG_LVL_ERROR, "charlie");   /* 7 bytes  -> 18 total, 3 records */

    /* happy path: the device fills our struct */
    struct vlog_stats st = { 0xdeadbeef, 0xdeadbeef };
    obs("stat_rc",  (uint32_t)vlog_stat(&st, sizeof st));
    obs("records",  st.records);
    obs("bytes",    st.bytes);
    obs("seq",      vlog_seq());

    /* buffer too small: ERR_BADLEN, and NOTHING may be written to it */
    struct vlog_stats small = { 0x11111111, 0x22222222 };
    obs("small_rc",        (uint32_t)vlog_stat(&small, 4));
    obs("small_untouched", small.records);

    /* address outside guest RAM: ERR_BADADDR */
    obs("badaddr_rc",
        (uint32_t)vlog_stat((struct vlog_stats *)(uintptr_t)0xdeadbeef0000ULL, 8));

    vm_exit(0);
}
