/* virtq_private.c: ADVERSARIAL virtqueue tests for Part III.
 *
 * The other harness (virtq_test.c) is the happy path, staged for partial credit.
 * THIS one is the hostile guest: a real driver can be buggy or malicious, and
 * SPEC.md Part III demands "a hypervisor must not be crashable by its guest."
 * We build malformed rings and check that vlog_virtq_handle() survives them.
 *
 * The teeth are the build flags, not clever asserts: the autograder compiles this
 * with -fsanitize=address,undefined -fno-sanitize-recover and runs it under a
 * timeout. So the invariants are enforced mechanically:
 *   - out-of-bounds descriptor / ring index    -> ASan (each ring is its own tight
 *                                                  allocation, so overrun = abort);
 *   - dereferencing an untranslated address     -> ASan / SIGSEGV;
 *   - overflowing the record buffer             -> ASan;
 *   - an unbounded (cyclic) chain walk           -> the timeout;
 *   - leaking bytes the guest never granted      -> a poison-sentinel scan of the log.
 *
 * Each memory region is a SEPARATE malloc with an unmapped gap between the two
 * guest-physical bases, so a translator that returns a bad pointer, or a handler
 * that reads past a region, lands in ASan's redzone.
 *
 *   ./virtq_private <scenario> <logfile>     # exit 0 iff it survived correctly
 */
#include "virtq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Two regions, a gap between their guest-physical bases. Each is its own tight
 * allocation so ASan guards its ends. */
#define R0_BASE   0x40000000ULL
#define R1_BASE   0x50000000ULL
#define GAP_BASE  0x48000000ULL      /* maps to nothing */
#define RSIZE     0x4000u
#define QNUM      8

static uint8_t *r0, *r1;
static struct virtq_mem_region regions[2];
static struct virtq_mem mem;

/* rings are their own allocations too (tight, so a bad index overruns them) */
static struct vring_desc  *desc;
static struct vring_avail *avail;
static struct vring_used  *used;

static const char POISON[] = "LEAKSECRET";  /* planted in memory the guest must NOT read out */

static void setup(void)
{
    r0 = malloc(RSIZE); r1 = malloc(RSIZE);
    memset(r0, 0, RSIZE); memset(r1, 0, RSIZE);
    regions[0] = (struct virtq_mem_region){ .gpa = R0_BASE, .size = RSIZE, .hva = r0 };
    regions[1] = (struct virtq_mem_region){ .gpa = R1_BASE, .size = RSIZE, .hva = r1 };
    mem.regions = regions; mem.nregions = 2;

    desc  = calloc(QNUM, sizeof *desc);
    avail = calloc(1, sizeof *avail + (QNUM + 1) * sizeof(uint16_t));
    used  = calloc(1, sizeof *used + (QNUM + 1) * sizeof(struct vring_used_elem));
}

/* put payload bytes into region 0 at offset `off`; return the guest-phys addr */
static uint64_t put0(unsigned off, const void *p, unsigned n)
{
    memcpy(r0 + off, p, n);
    return R0_BASE + off;
}

static int failures;

/* read the produced log back and check for a leaked poison sentinel */
static void check_no_leak(const char *logpath)
{
    FILE *f = fopen(logpath, "r");
    if (!f) return;
    char buf[8192]; size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0;
    fclose(f);
    if (strstr(buf, "LEAK")) {
        printf("  FAIL: the handler leaked bytes the guest never granted:\n    %s\n", buf);
        failures++;
    }
}

static void check_has(const char *logpath, const char *want)
{
    FILE *f = fopen(logpath, "r");
    if (!f) { printf("  FAIL: no log\n"); failures++; return; }
    char buf[8192]; size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0;
    fclose(f);
    if (!strstr(buf, want)) {
        printf("  FAIL: expected record '%s' missing; log was:\n    %s\n", want, buf);
        failures++;
    }
}

/* publish chain head `h` as the next avail entry */
static void avail_push(uint16_t *ai, uint16_t h) { avail->ring[*ai % QNUM] = h; (*ai)++; }

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <scenario> <logfile>\n", argv[0]); return 2; }
    const char *scen = argv[1];
    const char *logpath = argv[2];

    setup();
    struct vlog_sink *sink = vlog_sink_open(logpath);
    if (!sink) { fprintf(stderr, "cannot open %s\n", logpath); return 2; }

    struct virtq vq = { .desc = desc, .avail = avail, .used = used, .num = QNUM, .last_avail = 0 };
    uint16_t ai = 0;

    if (!strcmp(scen, "cyclic")) {
        /* a valid chain, then one that points back to itself with NEXT always
         * set: a bounded walk terminates; an unbounded one hangs (timeout). */
        const char *v = "1 alive";
        desc[0] = (struct vring_desc){ .addr = put0(0x080, v, (unsigned)strlen(v)),
                                       .len = (uint32_t)strlen(v), .flags = 0, .next = 0 };
        avail_push(&ai, 0);
        const char *m = "1 loop";
        desc[1] = (struct vring_desc){ .addr = put0(0x100, m, (unsigned)strlen(m)),
                                       .len = (uint32_t)strlen(m),
                                       .flags = VRING_DESC_F_NEXT, .next = 1 /* -> itself */ };
        avail_push(&ai, 1);

    } else if (!strcmp(scen, "head_oob")) {
        /* a valid chain, then an avail entry whose head index is out of range:
         * indexing desc[head] must not read past the descriptor table. */
        const char *m = "1 alpha";
        desc[0] = (struct vring_desc){ .addr = put0(0x100, m, (unsigned)strlen(m)),
                                       .len = (uint32_t)strlen(m), .flags = 0, .next = 0 };
        avail_push(&ai, 0);
        avail_push(&ai, QNUM + 5);          /* out of range */

    } else if (!strcmp(scen, "next_oob")) {
        /* a chain whose `next` index is out of range: following it must stop. */
        const char *m = "2 bravo";
        desc[0] = (struct vring_desc){ .addr = put0(0x100, m, (unsigned)strlen(m)),
                                       .len = (uint32_t)strlen(m),
                                       .flags = VRING_DESC_F_NEXT, .next = QNUM + 3 };
        avail_push(&ai, 0);

    } else if (!strcmp(scen, "overlong")) {
        /* several big descriptors whose concatenation exceeds VIRTQ_MAX_RECORD:
         * the record buffer must not overflow. */
        static uint8_t big[2000];
        memset(big, 'x', sizeof big);
        big[0] = '1'; big[1] = ' ';
        for (int k = 0; k < 3; k++) {
            desc[k] = (struct vring_desc){ .addr = put0(0x200 + (unsigned)k * 2100, big, sizeof big),
                                           .len = sizeof big,
                                           .flags = (k < 2) ? VRING_DESC_F_NEXT : 0,
                                           .next = (uint16_t)(k + 1) };
        }
        avail_push(&ai, 0);

    } else if (!strcmp(scen, "straddle")) {
        /* a valid chain, then a descriptor whose range crosses region 0's end
         * into the gap: the translator must reject it (no read past the region). */
        const char *m = "1 gamma";
        desc[0] = (struct vring_desc){ .addr = put0(0x100, m, (unsigned)strlen(m)),
                                       .len = (uint32_t)strlen(m), .flags = 0, .next = 0 };
        avail_push(&ai, 0);
        desc[1] = (struct vring_desc){ .addr = R0_BASE + RSIZE - 8, .len = 64,  /* runs off the end */
                                       .flags = 0, .next = 0 };
        avail_push(&ai, 1);

    } else if (!strcmp(scen, "gap_indirect")) {
        /* a valid chain, then an INDIRECT descriptor whose table lives in the
         * unmapped gap: translating the table must fail, not be dereferenced. */
        const char *m = "3 delta";
        desc[0] = (struct vring_desc){ .addr = put0(0x100, m, (unsigned)strlen(m)),
                                       .len = (uint32_t)strlen(m), .flags = 0, .next = 0 };
        avail_push(&ai, 0);
        desc[1] = (struct vring_desc){ .addr = GAP_BASE, .len = 2 * sizeof(struct vring_desc),
                                       .flags = VRING_DESC_F_INDIRECT, .next = 0 };
        avail_push(&ai, 1);

    } else if (!strcmp(scen, "writable_leak")) {
        /* a device-writable descriptor points at bytes the guest did not send as
         * data (poison). Reading them into the log is an information leak. */
        const char *m = "1 echo";
        desc[0] = (struct vring_desc){ .addr = put0(0x100, m, (unsigned)strlen(m)),
                                       .len = (uint32_t)strlen(m), .flags = 0, .next = 0 };
        avail_push(&ai, 0);
        desc[1] = (struct vring_desc){ .addr = put0(0x300, POISON, (unsigned)sizeof POISON),
                                       .len = (uint32_t)sizeof POISON,
                                       .flags = VRING_DESC_F_WRITE, .next = 0 };
        avail_push(&ai, 1);

    } else {
        fprintf(stderr, "unknown scenario '%s'\n", scen);
        return 2;
    }

    avail->idx = ai;
    __atomic_thread_fence(__ATOMIC_RELEASE);

    /* If the handler survives (no ASan abort, no SIGSEGV, no hang), we get here. */
    int n = vlog_virtq_handle(&vq, &mem, sink);
    if (n < 0) { printf("  FAIL: handler returned %d\n", n); failures++; }

    vlog_sink_close(sink);
    check_no_leak(logpath);

    /* scenarios with a well-defined valid record must still have produced it */
    if      (!strcmp(scen, "cyclic"))       check_has(logpath, "[0] INFO alive");
    else if (!strcmp(scen, "head_oob"))     check_has(logpath, "[0] INFO alpha");
    else if (!strcmp(scen, "next_oob"))     check_has(logpath, "[0] WARN bravo");
    else if (!strcmp(scen, "overlong"))     check_has(logpath, "[0] INFO xxxxxxxxxx");
    else if (!strcmp(scen, "straddle"))     check_has(logpath, "[0] INFO gamma");
    else if (!strcmp(scen, "gap_indirect")) check_has(logpath, "[0] ERROR delta");
    else if (!strcmp(scen, "writable_leak"))check_has(logpath, "[0] INFO echo");

    if (!failures) printf("  PASS  adv_%s\n", scen);
    free(r0); free(r1); free(desc); free(avail); free(used);
    return failures ? 1 : 0;
}
