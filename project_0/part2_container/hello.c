/* Project 0, Part 2: a process in a container (used in Project 2).
 *
 * `run.sh` runs this inside a new PID namespace (plus user and mount
 * namespaces). Inside that namespace this process is PID 1. Build it and run
 * `./hello` on its own and it would print a large PID instead -- the namespace
 * is what makes it 1.
 *
 * Your task: print this process's PID -- one number, nothing else. Through
 * run.sh it should print 1.
 */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int pid = 0;
    /* TODO(student): set `pid` to this process's id with getpid(). */

    printf("%d\n", pid);
    return 0;
}
