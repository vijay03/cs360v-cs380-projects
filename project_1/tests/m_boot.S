/* m_boot.S: VMM micro-test: minimal boot + serial + poweroff.
 *
 * Deliberately STACKLESS (no call/push) so it exercises only: the binary loaded
 * at RAM_BASE, RIP set to the entry, the run loop, serial TX, and POWEROFF ,
 * with NO dependency on RSP or the device. It writes "OK\n" to the serial
 * console and powers off with code 0. If this fails, the most basic VMM plumbing
 * (RAM/RIP/run/serial/poweroff) is broken; look there before anything else.
 *
 * Addresses must match SPEC.md Part I / vmm.h: SERIAL_BASE=0x10000000, TX at +0,
 * POWEROFF at +8.
 */
    .section .text._start, "ax"
    .global _start
_start:
    movabs  $0x10000000, %rbx      /* SERIAL_BASE + SERIAL_TX */
    movl    $0x4F, (%rbx)          /* 'O' */
    movl    $0x4B, (%rbx)          /* 'K' */
    movl    $0x0A, (%rbx)          /* '\n' */
    movabs  $0x10000008, %rax      /* SERIAL_BASE + SERIAL_POWEROFF */
    movl    $0, (%rax)             /* exit code 0 -> stop the machine */
1:  hlt
    jmp     1b

    .section .note.GNU-stack, "", @progbits
