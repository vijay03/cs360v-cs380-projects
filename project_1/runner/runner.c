/* runner.c: the function runner (provided; not meant to be edited).
 *
 * A small map-reduce style function runner. It listens on a TCP port and speaks
 * a line-oriented protocol. One connection carries one command, then closes:
 *
 *     PING                  ->  PONG                 (is it up and serving?)
 *     FUNCS                 ->  OK wordcount sum     (which functions it has)
 *     INFO                  ->  OK platform=...      (what it is running on)
 *     RUN <fn> <payload>    ->  OK <result>          (invoke one; the payload
 *                               ERR <reason>          is the rest of the line)
 *
 * A serverless platform is the client: it asks a runner to execute a function
 * and reads back the result. The interesting part is that one source file is
 * meant to run, unchanged, in every environment this course cares about:
 *
 *   - as an ordinary Linux process, launched straight from a shell;
 *   - as the workload inside a container;
 *   - inside a full Linux guest in a virtual machine, where the log records it
 *     prints are exactly what a virtual logging device is built to ingest;
 *   - compiled INTO a unikernel image, linked together with the kernel itself.
 *
 * Those four environments are why it is written the way it is. It uses only
 * plain POSIX sockets and stdio, with no threads, no fork and no dynamic
 * loading, and it stays within what a minimal libc offers. That last constraint
 * is also why the listening port comes from argv rather than from the
 * environment: a unikernel has no environment to read. The functions are built
 * in and selected by name, because a unikernel cannot load code at run time.
 *
 * Logging: every interesting event is written to stdout as one record in the
 * standard "<level> <message>" format (0=DEBUG 1=INFO 2=WARN 3=ERROR). Whoever
 * launches the runner captures that stream: from a shell you simply see it, a
 * VM guest can feed it to a virtual device, and in a unikernel stdout *is* the
 * machine's console.
 *
 * Usage:  runner [LISTEN_PORT] [--log-to HOST:PORT]
 *
 * LISTEN_PORT defaults to 8080. With --log-to, every log record is ALSO sent,
 * one per line, to a collector listening at that address, which is how a
 * unikernel (with no shell to pipe stdout through) ships its logs anywhere.
 * HOST must be a dotted IPv4 address; there is no name resolution.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define RUNNER_DEFAULT_PORT 8080
#define LINE_MAX_LEN        4096

/* ---- logging: one "<level> <message>" record per line ---------------------
 *
 * Records always go to stdout. If a log collector was named with --log-to, the
 * same line is also sent to it over TCP, one record per line, which is exactly
 * the wire format a collector expects. That is the only way a unikernel can
 * ship its logs anywhere: it has no shell to pipe stdout through.
 *
 * The sink is strictly best-effort. Logging must never take the runner down, so
 * a collector that is absent, slow to start, or restarted mid-run only costs
 * records, never the service: the connection is made lazily and re-made after a
 * failure.
 */

enum { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };

static const char *g_log_host;      /* dotted IPv4; NULL means stdout only */
static int         g_log_port;
static int         g_log_fd = -1;   /* -1 means not currently connected     */
static int         g_log_skip;      /* records to skip before retrying      */
static int         g_log_backoff = 1;

#define LOG_BACKOFF_MAX  64         /* records skipped between retries       */

/* Parse "a.b.c.d" into a host-order address. Written out by hand rather than
 * calling inet_pton(), which a minimal libc does not necessarily provide.
 * Returns 0 on success. */
static int parse_ipv4(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (i && *s++ != '.') return -1;
        if (*s < '0' || *s > '9') return -1;
        unsigned octet = 0;
        while (*s >= '0' && *s <= '9') {
            octet = octet * 10 + (unsigned)(*s++ - '0');
            if (octet > 255) return -1;
        }
        v = (v << 8) | octet;
    }
    if (*s) return -1;
    *out = v;
    return 0;
}

/* Start connecting to the collector, without ever stalling the runner.
 *
 * The socket is put in NON-BLOCKING mode and we do not wait for the handshake:
 * connect() returns EINPROGRESS and we hand the descriptor back immediately.
 * Two reasons. First, a blocking connect() is unsafe here: on some systems
 * (WSL2 among them) connecting to a port with nothing listening does not fail
 * fast, it hangs, and a logging call that hangs would stall the request being
 * served. Second, waiting properly would need select() or poll(), which a
 * minimal libc may not have. Whether the connection actually came up is
 * discovered later, by whether write() works. */
static int log_sink_connect(void)
{
    if (!g_log_host) return -1;

    uint32_t ip;
    if (parse_ipv4(g_log_host, &ip) != 0) return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);   /* stays non-blocking */

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)g_log_port);
    a.sin_addr.s_addr = htonl(ip);

    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0 &&
        errno != EINPROGRESS && errno != EWOULDBLOCK && errno != EALREADY) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Records produced before the connection finishes coming up would otherwise be
 * lost, and those are the most interesting ones ("function runner started").
 * So hold them in a small buffer and flush it as soon as a write succeeds. The
 * buffer is deliberately bounded: if a collector never appears we drop the
 * overflow rather than grow without limit. */
static char   g_log_pend[2048];
static size_t g_log_pend_len;

static void log_pend(const char *line, size_t n)
{
    if (g_log_pend_len + n > sizeof g_log_pend) return;   /* full: drop it */
    memcpy(g_log_pend + g_log_pend_len, line, n);
    g_log_pend_len += n;
}

/* True when a failed write just means "the handshake has not finished yet". */
static int log_would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK ||
           errno == ENOTCONN || errno == EINPROGRESS;
}

static void log_sink_drop(void)
{
    close(g_log_fd);
    g_log_fd = -1;
    g_log_skip = g_log_backoff;
    if (g_log_backoff < LOG_BACKOFF_MAX) g_log_backoff *= 2;
}

static void log_sink_emit(const char *line, size_t n)
{
    if (!g_log_host) return;

    if (g_log_fd < 0) {
        /* Retry with a widening gap, so a collector that is down costs one
         * short attempt now and then rather than one on every record. */
        if (g_log_skip > 0) { g_log_skip--; log_pend(line, n); return; }
        if ((g_log_fd = log_sink_connect()) < 0) {
            g_log_skip = g_log_backoff;
            if (g_log_backoff < LOG_BACKOFF_MAX) g_log_backoff *= 2;
            log_pend(line, n);
            return;
        }
        g_log_backoff = 1;
    }

    /* Anything buffered while the connection was coming up goes first, so the
     * collector sees the records in the order they happened. */
    if (g_log_pend_len) {
        ssize_t w = write(g_log_fd, g_log_pend, g_log_pend_len);
        if (w == (ssize_t)g_log_pend_len) {
            g_log_pend_len = 0;
        } else if (w > 0) {
            memmove(g_log_pend, g_log_pend + w, g_log_pend_len - (size_t)w);
            g_log_pend_len -= (size_t)w;
            log_pend(line, n);
            return;
        } else {
            if (!log_would_block()) log_sink_drop();
            log_pend(line, n);
            return;
        }
    }

    if (write(g_log_fd, line, n) != (ssize_t)n) {
        if (!log_would_block()) log_sink_drop();
        log_pend(line, n);
    }
}

static void logrec(int level, const char *fmt, ...)
{
    char line[LINE_MAX_LEN];
    va_list ap;

    int n = snprintf(line, sizeof line, "%d ", level);
    if (n < 0 || (size_t)n >= sizeof line) return;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    if (m < 0) return;
    n += m;
    if ((size_t)n > sizeof line - 2) n = (int)sizeof line - 2;   /* room for \n */
    line[n++] = '\n';
    line[n] = '\0';

    fputs(line, stdout);
    fflush(stdout);

    log_sink_emit(line, (size_t)n);
}

/* ---- platform hook -------------------------------------------------------
 *
 * `INFO` reports something about the platform the runner is running on. This
 * weak default is all an ordinary process can honestly say. A build that can do
 * better provides a STRONG definition of the same symbol, and the linker prefers
 * it. The unikernel build does exactly that: linked into the same address space
 * as the kernel, it can answer with numbers only the kernel knows, by calling
 * those APIs directly instead of asking an operating system for them.
 */
__attribute__((weak))
int runner_platform_info(char *out, size_t n)
{
    snprintf(out, n, "platform=posix");
    return 0;
}

/* Called once per successful invocation. The default does nothing; a build that
 * wants to keep statistics provides a strong definition of it (the unikernel
 * build counts invocations this way). */
__attribute__((weak))
void runner_on_invoke(const char *fn)
{
    (void)fn;
}

/* ---- the built-in functions ---------------------------------------------- */
/* Map-reduce shaped: each one maps over the payload's tokens and reduces them to
 * a single value. More can be added here without the protocol changing. */

/* count whitespace-separated words */
static int fn_wordcount(const char *payload, char *out, size_t n)
{
    long words = 0;
    int in_word = 0;
    for (const char *p = payload; *p; p++) {
        if (isspace((unsigned char)*p)) { in_word = 0; }
        else if (!in_word) { in_word = 1; words++; }
    }
    snprintf(out, n, "%ld", words);
    return 0;
}

/* sum whitespace-separated integers; rejects a non-numeric token */
static int fn_sum(const char *payload, char *out, size_t n)
{
    long long total = 0;
    const char *p = payload;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *end;
        long long v = strtoll(p, &end, 10);
        if (end == p) { snprintf(out, n, "not a number"); return -1; }
        total += v;
        p = end;
    }
    snprintf(out, n, "%lld", total);
    return 0;
}

static const struct {
    const char *name;
    int (*fn)(const char *payload, char *out, size_t n);
} FUNCS[] = {
    { "wordcount", fn_wordcount },
    { "sum",       fn_sum },
};
#define NFUNCS ((int)(sizeof FUNCS / sizeof FUNCS[0]))

/* ---- the protocol -------------------------------------------------------- */

/* Handle one command line; write the reply (including its newline) to `reply`. */
static void handle_line(char *line, char *reply, size_t rn)
{
    /* strip trailing CR/LF */
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';

    if (strcmp(line, "PING") == 0) {
        logrec(LOG_DEBUG, "ping");
        snprintf(reply, rn, "PONG\n");
        return;
    }

    if (strcmp(line, "INFO") == 0) {
        char info[256];
        if (runner_platform_info(info, sizeof info) != 0)
            snprintf(info, sizeof info, "unavailable");
        logrec(LOG_DEBUG, "info");
        snprintf(reply, rn, "OK %s\n", info);
        return;
    }

    if (strcmp(line, "FUNCS") == 0) {
        size_t off = snprintf(reply, rn, "OK");
        for (int i = 0; i < NFUNCS && off < rn; i++)
            off += snprintf(reply + off, rn - off, " %s", FUNCS[i].name);
        if (off < rn) snprintf(reply + off, rn - off, "\n");
        logrec(LOG_DEBUG, "funcs");
        return;
    }

    if (strncmp(line, "RUN ", 4) == 0) {
        char *rest = line + 4;
        while (*rest == ' ') rest++;
        char *sp = strchr(rest, ' ');
        char name[64];
        const char *payload = "";
        if (sp) {
            size_t nlen = (size_t)(sp - rest);
            if (nlen >= sizeof name) nlen = sizeof name - 1;
            memcpy(name, rest, nlen); name[nlen] = '\0';
            payload = sp + 1;
        } else {
            size_t nlen = strlen(rest);
            if (nlen >= sizeof name) nlen = sizeof name - 1;
            memcpy(name, rest, nlen); name[nlen] = '\0';
        }

        for (int i = 0; i < NFUNCS; i++) {
            if (strcmp(name, FUNCS[i].name) != 0) continue;
            char out[256];
            logrec(LOG_INFO, "invoke %s", name);
            if (FUNCS[i].fn(payload, out, sizeof out) != 0) {
                logrec(LOG_ERROR, "%s failed: %s", name, out);
                snprintf(reply, rn, "ERR %s\n", out);
            } else {
                logrec(LOG_INFO, "%s -> %s", name, out);
                snprintf(reply, rn, "OK %s\n", out);
                runner_on_invoke(name);
            }
            return;
        }
        logrec(LOG_WARN, "no such function: %s", name);
        snprintf(reply, rn, "ERR no such function\n");
        return;
    }

    logrec(LOG_WARN, "bad command");
    snprintf(reply, rn, "ERR bad command\n");
}

/* ---- the server ---------------------------------------------------------- */

int main(int argc, char **argv)
{
    /* Arguments only, never the environment: a unikernel has no environment to
     * read, and keeping every build identical means this file needs no #ifdefs.
     *
     *     runner [LISTEN_PORT] [--log-to HOST:PORT]
     *
     * Parsed by hand rather than with getopt, which a minimal libc may not have.
     */
    int port = RUNNER_DEFAULT_PORT;
    static char host_buf[64];
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--log-to") && i + 1 < argc) {
            const char *hp = argv[++i];
            const char *colon = strchr(hp, ':');
            if (!colon) { logrec(LOG_WARN, "ignoring --log-to %s (want HOST:PORT)", hp); continue; }
            size_t hl = (size_t)(colon - hp);
            if (hl >= sizeof host_buf) hl = sizeof host_buf - 1;
            memcpy(host_buf, hp, hl); host_buf[hl] = '\0';
            g_log_host = host_buf;
            g_log_port = atoi(colon + 1);
            if (g_log_port <= 0 || g_log_port > 65535) g_log_host = NULL;
        } else if (argv[i][0] != '-') {
            port = atoi(argv[i]);
        }
    }
    if (port <= 0 || port > 65535) port = RUNNER_DEFAULT_PORT;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { logrec(LOG_ERROR, "socket: %s", strerror(errno)); return 1; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0) {
        logrec(LOG_ERROR, "bind %d: %s", port, strerror(errno)); return 1;
    }
    if (listen(s, 8) != 0) {
        logrec(LOG_ERROR, "listen: %s", strerror(errno)); return 1;
    }

    /* This exact record is what tells a deployer the runner is up. */
    logrec(LOG_INFO, "function runner started on port %d", port);

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR) continue;
            logrec(LOG_ERROR, "accept: %s", strerror(errno));
            break;
        }
        char line[LINE_MAX_LEN];
        ssize_t n = read(c, line, sizeof line - 1);
        if (n > 0) {
            line[n] = '\0';
            char reply[512];
            handle_line(line, reply, sizeof reply);
            ssize_t w = write(c, reply, strlen(reply));
            (void)w;
        }
        close(c);
    }
    close(s);
    return 0;
}
