/* Project 0, Part 4: a Docker container.
 *
 * Runs on your own machine, not the VM. `run.sh` runs this inside the container
 * (see SETUP.md, "Part 4"). Docker sets the container's hostname to its short
 * container id -- a fresh id every `docker compose up` -- so that is what this
 * prints.
 *
 * Your task: put the hostname into `host` with gethostname(). The program then
 * prints it (one line, nothing else), or serves it once with --serve PORT, which
 * run.sh uses for the published-port check.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char **argv)
{
    char host[256] = "";
    /* TODO(student): fill `host` with the container's hostname.
     *   gethostname(host, sizeof host);
     */
    gethostname(host, sizeof host);
    if (argc == 3 && strcmp(argv[1], "--serve") == 0) {
        struct sockaddr_in addr;
        int s, one = 1;
        int port = atoi(argv[2]);

        if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) { perror("socket"); return 1; }
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

        memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_port = htons((unsigned short)port);
        /* 0.0.0.0, not 127.0.0.1: a published port arrives from outside the
         * container, so binding its loopback would be unreachable from the host. */
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0) { perror("bind"); return 1; }
        if (listen(s, 4) != 0) { perror("listen"); return 1; }

        printf("serving on port %d\n", port);
        fflush(stdout);

        /* One client then exit, so a repeated ./run.sh leaves no listener behind. */
        int c = accept(s, NULL, NULL);
        if (c < 0) { perror("accept"); return 1; }
        dprintf(c, "%s\n", host);
        close(c);
        close(s);
        return 0;
    }

    printf("%s\n", host);
    return 0;
}
