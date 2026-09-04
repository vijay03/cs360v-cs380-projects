/* m_stack.c: VMM micro-test: the guest C runtime / stack works.
 *
 * A minimal C guest: reaching main() and calling obs() both need a valid stack
 * (call/push), so this passes only if RSP was set up correctly. If m_boot
 * passes but this fails, the initial stack pointer (RSP) is the problem. No
 * device involved. */
#include "tobs.h"

int main(void)
{
    obs("stack", 0x1);
    return 0;
}
