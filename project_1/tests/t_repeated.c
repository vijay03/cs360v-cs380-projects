/* t_repeated.c: repeated LOGs without reprogramming unchanged operands. Host
 * checks SEQ advanced once per record, plus expected/t_repeated.log. */
#include "tobs.h"

int main(void)
{
    static const char msg[] = "tick";

    uint64_t gpa = (uint64_t)(uintptr_t)msg;
    mmio_w32(DEV_BASE + VLOG_REG_MSG_LO, (uint32_t)gpa);
    mmio_w32(DEV_BASE + VLOG_REG_MSG_HI, (uint32_t)(gpa >> 32));
    mmio_w32(DEV_BASE + VLOG_REG_LEN, sizeof(msg) - 1);
    mmio_w32(DEV_BASE + VLOG_REG_LEVEL, VLOG_LVL_INFO);

    for (int i = 0; i < 3; i++)
        mmio_w32(DEV_BASE + VLOG_REG_CMD, VLOG_CMD_LOG);

    obs("err", dev_err());
    obs("seq", vlog_seq());
    return 0;
}
