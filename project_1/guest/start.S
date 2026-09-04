/* start.S: bare-metal entry stub for guest programs.
 *
 * Placed first in the binary by link.ld so the emulator can jump straight to
 * RAM_BASE. The VMM already sets up RSP, so we just call main() and report its
 * return value through vm_exit().
 */
    .section .text._start, "ax"
    .global _start
_start:
    call    main
    movl    %eax, %edi      /* main()'s return value -> exit code */
    call    vm_exit
1:  hlt                     /* unreachable: vm_exit never returns */
    jmp     1b

    .section .note.GNU-stack, "", @progbits
