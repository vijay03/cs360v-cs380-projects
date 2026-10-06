/* ukstats.c: your own Unikraft library.  *** YOU IMPLEMENT ***
 *
 * You implement three things (SPEC.md Part III):
 *
 *   1. a boot hook, which runs during boot and records when boot finished;
 *   2. runner_on_invoke(), which counts invocations;
 *   3. runner_platform_info(), which answers the runner's INFO command.
 *
 * runner.c has default versions of (2) and (3); yours replace them.
 */
#include <stdio.h>

/* TODO(student): include Unikraft's headers:
 *
 *     #include <uk/init.h>        uk_late_initcall()
 *     #include <uk/alloc.h>       uk_alloc_get_default(), uk_alloc_availmem()
 *     #include <uk/plat/time.h>   ukplat_monotonic_clock()
 *
 * You can read them in the Unikraft source kraft fetched under
 * app/.unikraft/unikraft/.
 */

/* TODO(student): two variables at file scope (outside any function):
 *
 *     static __nsec boot_ns;             the time your boot hook saves
 *     static unsigned long invocations;  the counter
 *
 * __nsec is Unikraft's type for nanoseconds (a 64-bit unsigned integer). */

/* TODO(student) 1: the boot hook.
 *
 * Write a function that takes no arguments and returns int:
 *   - save ukplat_monotonic_clock() in boot_ns;
 *   - print the boot time in milliseconds (boot_ns / 1000000) with printf:
 *
 *         1 unikernel booted in <N> ms
 *
 *   - return 0.
 *
 * Then, OUTSIDE the function (on its own line after it), register it to run
 * during boot:
 *
 *     uk_late_initcall(your_function, 0);
 *
 * To print a 64-bit value with %lu, cast it: (unsigned long)(boot_ns / 1000000). */

/* TODO(student) 2: count invocations.
 *
 *     void runner_on_invoke(const char *fn)
 *
 * The runner calls this after each successful RUN. Add 1 to invocations. You
 * do not need `fn`. */

/* TODO(student) 3: answer INFO.
 *
 *     int runner_platform_info(char *out, size_t n)
 *
 * Write this line into `out` with snprintf(out, n, ...), then return 0:
 *
 *     platform=unikraft freemem=<bytes> uptime_ms=<ms> boot_ms=<ms> invocations=<n>
 *
 *   - freemem:     a = uk_alloc_get_default(); if a is NULL use -1, otherwise
 *                  uk_alloc_availmem(a);
 *   - uptime_ms:   ukplat_monotonic_clock() / 1000000;
 *   - boot_ms:     boot_ns / 1000000;
 *   - invocations: your counter.
 *
 * Cast each number to long or unsigned long so it matches %ld or %lu. */
