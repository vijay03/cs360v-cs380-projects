/* t_log_basic.c: one successful LOG. Host checks the returned code, error
 * flag, and SEQ, plus the log file against expected/t_log_basic.log. */
#include "tobs.h"

int main(void)
{
    uint32_t rc = vlog(VLOG_LVL_INFO, "hello world");
    obs("rc", rc);
    obs("err", dev_err());
    obs("seq", vlog_seq());
    return 0;
}
