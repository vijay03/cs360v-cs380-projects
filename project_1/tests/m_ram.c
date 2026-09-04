/* m_ram.c: VMM micro-test: guest RAM is writable and host-backed.
 *
 * Writes a word and a small array in RAM, reads them back, and reports them. If
 * m_stack passes but this fails, guest RAM is mapped wrong (e.g. read-only, or
 * not actually backed by the host buffer). No device involved. */
#include "tobs.h"

static volatile uint32_t cell;
static volatile uint8_t  buf[64];

int main(void)
{
    cell = 0xCAFEF00D;
    for (int i = 0; i < 64; i++)
        buf[i] = (uint8_t)(i + 1);

    uint32_t sum = 0;
    for (int i = 0; i < 64; i++)
        sum += buf[i];

    obs("ram_cell", cell);
    obs("ram_sum", sum);   /* 1 + 2 + ... + 64 = 2080 = 0x820 */
    return 0;
}
