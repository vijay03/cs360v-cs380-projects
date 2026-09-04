/* link.ld — flat-binary layout for bare-metal guest programs.
 *
 * The load/entry address must match RAM_BASE in ../emulator/vmm.h. The
 * _start stub (section .text._start) is placed first so that, after
 * `objcopy -O binary`, the entry point sits at offset 0 of the image.
 */
ENTRY(_start)

SECTIONS
{
    . = 0x100000;          /* == RAM_BASE */

    .text : {
        *(.text._start)
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }

    /DISCARD/ : {
        *(.note*)
        *(.comment)
        *(.eh_frame*)
        *(.eh_frame_hdr)
    }
}
