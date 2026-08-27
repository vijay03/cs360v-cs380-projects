/* Project 0, Part 1: the Unicorn CPU emulator (used in Project 1).
 *
 * This opens a Unicorn virtual CPU and runs a short piece of x86-64 machine code
 * inside it:  mov eax, 40 ; add eax, 2 .  When it finishes, the emulated EAX
 * register holds 42.
 *
 * Your task: read that result out of EAX and print it -- one number, nothing
 * else. Then run `make && ./hello`; it should print 42.
 */
#include <unicorn/unicorn.h>
#include <stdio.h>

int main(void)
{
    uc_engine *uc;
    const unsigned char code[] = { 0xB8, 0x28, 0x00, 0x00, 0x00,   /* mov eax, 40 */
                                   0x83, 0xC0, 0x02 };              /* add eax, 2  */
    const uint64_t base = 0x1000;

    uc_err err = uc_open(UC_ARCH_X86, UC_MODE_64, &uc);
    if (err) { fprintf(stderr, "uc_open: %s\n", uc_strerror(err)); return 1; }
    uc_mem_map(uc, base, 0x1000, UC_PROT_ALL);
    uc_mem_write(uc, base, code, sizeof code);
    err = uc_emu_start(uc, base, base + sizeof code, 0, 0);
    if (err) { fprintf(stderr, "uc_emu_start: %s\n", uc_strerror(err)); return 1; }

    int eax = 0;
    /* TODO(student): read the EAX register into `eax`.
     *   uc_reg_read(uc, UC_X86_REG_EAX, &eax);
     */

    uc_close(uc);
    printf("%d\n", eax);
    return 0;
}
