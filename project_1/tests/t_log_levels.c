/* t_log_levels.c: one record at each severity level. Host checks each return
 * code and SEQ, plus the log file against expected/t_log_levels.log. */
#include "tobs.h"

int main(void)
{
    obs("rc_debug", vlog(VLOG_LVL_DEBUG, "debug message"));
    obs("rc_info",  vlog(VLOG_LVL_INFO,  "info message"));
    obs("rc_warn",  vlog(VLOG_LVL_WARN,  "warn message"));
    obs("rc_error", vlog(VLOG_LVL_ERROR, "error message"));
    obs("seq", vlog_seq());
    return 0;
}
