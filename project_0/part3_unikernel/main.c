/* Project 0, Part 3: a unikernel (used in Project 3).
 *
 * This is the whole application of a Unikraft image. `run.sh` builds it with
 * kraft and boots it under QEMU. A unikernel under nolibc has no OS clock, so
 * the closest thing to a timestamp is when the image was compiled -- and that
 * changes every time you rebuild.
 *
 * Your task: print the build date and time, using the compiler's __DATE__ and
 * __TIME__ macros. Then run `./run.sh`.
 */
#include <stdio.h>

int main(void)
{
    /* TODO(student): print  __DATE__  and  __TIME__  on one line, e.g.
     *   printf("%s %s\n", __DATE__, __TIME__);
     */
    return 0;
}
