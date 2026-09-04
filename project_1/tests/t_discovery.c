/* t_discovery.c: report the discovery registers; host checks ID and VERSION. */
#include "tobs.h"

int main(void)
{
    obs("id", vlog_id());
    obs("version", mmio_r32(DEV_BASE + VLOG_REG_VERSION));
    return 0;
}
