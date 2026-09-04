/* t_badcmd.c: a simple failure-path example.
 *
 * Writing an unknown command code must set the device's ERROR flag with code
 * VLOG_ERR_BADCMD and log nothing (SEQ stays 0). This is the one error-path
 * test in the basic suite; use it as a template for asserting error behavior
 * (read STATUS via dev_err() / dev_errcode()). See SPEC.md §3.
 */
#include "tobs.h"

int main(void)
{
    mmio_w32(DEV_BASE + VLOG_REG_CMD, 0x7f);   /* not a valid command */
    obs("err", dev_err());
    obs("errcode", dev_errcode());
    obs("seq", vlog_seq());
    return 0;
}
