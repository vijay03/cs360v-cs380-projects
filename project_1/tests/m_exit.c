/* m_exit.c: VMM micro-test: POWEROFF carries the exit code.
 *
 * Produces no serial output and returns 42; the VMM must propagate that as the
 * process exit code (POWEROFF value -> emulator exit status). Guards against a
 * poweroff that always reports 0. Expected via expected/m_exit.exit. */
#include "tobs.h"

int main(void)
{
    return 42;
}
