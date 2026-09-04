/* netlog.c: network log-ingest listener (PROVIDED, not a student deliverable).
 *
 * A TCP listener on its own thread. For each "<level> <message>\n" line it
 * appends a record to the shared (thread-safe) log store. Concurrency notes:
 *   - the store's own mutex makes concurrent appends (this thread + the vCPU
 *     thread's MMIO path) safe, so no extra lock is needed here;
 *   - the stop flag is a C11 atomic (not a plain/volatile int) so it is race-
 *     free under ThreadSanitizer;
 *   - the listen fd is closed only AFTER the thread is joined, so accept()
 *     never races a close().
 */
#include "netlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdatomic.h>
#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct netlog {
    logstore   *store;
    int         lfd;
    pthread_t   th;
    atomic_int  stop;
    uint32_t    seq;   /* only touched by the single listener thread */
};

#define NL_LINE_MAX 4200   /* fits a max message (VLOG_MAX_MSG=4096) + header */
#define NL_MAX_CONN 64      /* concurrent producers (a Project 4 fleet's worth) */

/* One in-progress connection: its fd and a buffer holding the bytes of a line
 * not yet terminated by '\n' (records can arrive split across reads). */
struct conn {
    int    fd;
    size_t len;
    char   buf[NL_LINE_MAX];
};

/* Append one complete "<level> <message>" record to the store. Runs only on the
 * listener thread, so nl->seq needs no atomicity; the store's own mutex covers
 * the append against the concurrent MMIO/vCPU producer. */
static void process_line(struct netlog *nl, char *line)
{
    char *sp = strchr(line, ' ');
    uint32_t level;
    const char *msg;
    size_t mlen;
    if (sp) {
        *sp = '\0';
        level = (uint32_t)strtoul(line, NULL, 10);
        msg = sp + 1;
        mlen = strlen(msg);
    } else {
        level = (uint32_t)strtoul(line, NULL, 10);
        msg = "";
        mlen = 0;
    }
    logstore_append(nl->store, nl->seq++, level, msg, (uint32_t)mlen);
}

/* Feed a chunk of bytes from one connection through its line buffer, appending a
 * record for each completed line. A line that would overflow the buffer is
 * flushed as-is (a producer that respects VLOG_MAX_MSG never triggers this). */
static void feed(struct netlog *nl, struct conn *c, const char *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char ch = data[i];
        if (ch == '\n') {
            c->buf[c->len] = '\0';
            process_line(nl, c->buf);
            c->len = 0;
        } else if (c->len < NL_LINE_MAX - 1) {
            c->buf[c->len++] = ch;
        } else {
            c->buf[c->len] = '\0';
            process_line(nl, c->buf);
            c->len = 0;
            c->buf[c->len++] = ch;
        }
    }
}

/* A single thread multiplexing every producer with poll(): the listen fd plus
 * one fd per open connection. Serving connections concurrently (rather than one
 * to completion) is what lets a whole fleet of backends stream logs at once; the
 * previous accept-then-drain loop starved every producer after the first, since
 * a runner holds its connection open for its entire lifetime. */
static void *listener(void *arg)
{
    struct netlog *nl = arg;
    struct conn conns[NL_MAX_CONN];
    struct pollfd pfds[NL_MAX_CONN + 1];
    int nc = 0;

    while (!atomic_load(&nl->stop)) {
        pfds[0].fd = nl->lfd; pfds[0].events = POLLIN; pfds[0].revents = 0;
        for (int i = 0; i < nc; i++) {
            pfds[i + 1].fd = conns[i].fd;
            pfds[i + 1].events = POLLIN;
            pfds[i + 1].revents = 0;
        }
        int r = poll(pfds, (nfds_t)(nc + 1), 200);   /* timeout: re-check stop */
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (atomic_load(&nl->stop)) break;

        /* accept new producers */
        if (pfds[0].revents & POLLIN) {
            int fd = accept(nl->lfd, NULL, NULL);
            if (fd >= 0) {
                if (nc < NL_MAX_CONN) {
                    conns[nc].fd = fd;
                    conns[nc].len = 0;
                    nc++;
                } else {
                    close(fd);   /* at capacity: drop the newcomer */
                }
            }
        }

        /* service ready connections, compacting out any that closed */
        int keep = 0;
        for (int i = 0; i < nc; i++) {
            short re = pfds[i + 1].revents;
            int drop = 0;
            if (re & POLLIN) {
                char tmp[2048];
                ssize_t k = read(conns[i].fd, tmp, sizeof tmp);
                if (k <= 0) drop = 1;              /* EOF or error */
                else feed(nl, &conns[i], tmp, (size_t)k);
            }
            if (re & (POLLHUP | POLLERR | POLLNVAL)) drop = 1;
            if (drop) close(conns[i].fd);
            else conns[keep++] = conns[i];         /* copies fd + line buffer */
        }
        nc = keep;
    }

    for (int i = 0; i < nc; i++) close(conns[i].fd);
    return NULL;
}

netlog *netlog_start(logstore *store, int port)
{
    struct netlog *nl = calloc(1, sizeof *nl);
    if (!nl) return NULL;
    nl->store = store;
    atomic_init(&nl->stop, 0);

    nl->lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (nl->lfd < 0) { free(nl); return NULL; }
    int one = 1;
    setsockopt(nl->lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);   /* bind config / auth: future work */
    a.sin_port = htons((uint16_t)port);
    if (bind(nl->lfd, (struct sockaddr *)&a, sizeof a) < 0 ||
        listen(nl->lfd, 16) < 0) {
        close(nl->lfd); free(nl); return NULL;
    }
    if (pthread_create(&nl->th, NULL, listener, nl) != 0) {
        close(nl->lfd); free(nl); return NULL;
    }
    return nl;
}

void netlog_stop(netlog *nl)
{
    if (!nl) return;
    atomic_store(&nl->stop, 1);
    shutdown(nl->lfd, SHUT_RDWR);   /* wake a blocked accept() */
    pthread_join(nl->th, NULL);     /* join BEFORE closing the fd */
    close(nl->lfd);
    free(nl);
}
