/* m_fault.S: VMM micro-test: the guest faults; the VMM must survive it.
 *
 * Deliberately STACKLESS. Prints "F\n" over the serial console, then reads an
 * address that is NOT mapped (no RAM, no MMIO region there). A correct VMM
 * catches that with its unmapped-memory hook, stops the CPU, and exits with
 * VMM_EXIT_FAULT (77). It does NOT crash, and it never reaches the POWEROFF
 * below. An emulator without the hook typically dies or hangs here.
 *
 * Addresses must match SPEC.md Part I / vmm.h: SERIAL_BASE=0x10000000.
 */
    .section .text._start, "ax"
    .global _start
_start:
    movabs  $0x10000000, %rbx      /* SERIAL_BASE + SERIAL_TX */
    movl    $0x46, (%rbx)          /* 'F' */
    movl    $0x0A, (%rbx)          /* '\n' */

    movabs  $0x50000000, %rax      /* unmapped: not RAM, not serial, not device */
    movl    (%rax), %ecx           /* -> guest fault; the VMM must stop here */

    movl    $0, 8(%rbx)            /* POWEROFF 0: must NOT be reached */
1:  jmp     1b
