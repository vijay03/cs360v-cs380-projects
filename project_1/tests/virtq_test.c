/* virtq_test.c: virtqueue tests for Part III (PROVIDED).
 *
 * Drives YOUR virtq_gpa_to_hva() and vlog_virtq_handle() against a synthetic
 * guest: no QEMU, no vhost-user, no timing. Everything is deterministic for a
 * given seed, so this is both your fastest way to develop Part III and how it is
 * graded.
 *
 * The tests are STAGED, so a partial implementation earns partial credit. Each
 * stage needs everything the earlier ones did, plus one more capability:
 *
 *   translate  virtq_gpa_to_hva() alone: valid ranges, straddling, gaps, overflow.
 *   single     the minimal queue: one readable descriptor per chain, emitted,
 *              and completed on the used ring (avail -> desc -> emit -> used).
 *   chained    multi-descriptor chains, skipping device-writable descriptors and
 *              rejecting descriptors whose address does not translate.
 *   indirect   VRING_DESC_F_INDIRECT descriptors (a table of descriptors in
 *              guest memory).
 *
 * The fake guest has TWO memory regions with a GAP between them, so a valid
 * buffer must lie entirely inside one of them.
 *
 *   ./virtq_test <stage> <seed> <logfile>      # exit 0 iff that stage matches
 */
#include "virtq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- a fake guest with two memory regions ----------------------------- */

#define R0_BASE   0x40000000ULL
#define R1_BASE   0x50000000ULL      /* note the gap between the two */
#define RSIZE     0x8000u
#define GMEM_SIZE (2u * RSIZE)

#define OFF_DESC   0x0000
#define OFF_AVAIL  0x1000
#define OFF_USED   0x2000
#define OFF_PAY    0x3000            /* payload / indirect-table arena */

#define QNUM       8                 /* ring size (power of two) */

static uint8_t *gmem;
static struct virtq_mem_region regions[2];
static struct virtq_mem mem;

/* offset within gmem -> the guest-physical address the guest would use */
static uint64_t gpa_of(unsigned off)
{
    return (off < RSIZE) ? (R0_BASE + off) : (R1_BASE + (off - RSIZE));
}

/* ---- expectations ----------------------------------------------------- */

#define MAX_REC 256
static char     exp_log[MAX_REC][512];
static unsigned exp_n;
static uint16_t exp_head[MAX_REC];
static unsigned exp_heads_n;
static int      failures;

static const char *level_name(unsigned l)
{
    switch (l) {
    case 0:  return "DEBUG";
    case 1:  return "INFO";
    case 2:  return "WARN";
    case 3:  return "ERROR";
    default: return "LVL?";
    }
}

static unsigned rnd(unsigned *s, unsigned n)   /* xorshift, portable + seeded */
{
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return n ? (*s % n) : 0;
}

/* ---- stage: translate (virtq_gpa_to_hva alone) ------------------------ */

static void check_ptr(const char *what, void *got, void *want)
{
    if (got != want) {
        printf("  FAIL gpa_to_hva: %s -> %p, expected %p\n", what, got, want);
        failures++;
    }
}

static void stage_translate(void)
{
    check_ptr("valid in region 0",
              virtq_gpa_to_hva(&mem, R0_BASE + 0x100, 16), gmem + 0x100);
    check_ptr("valid in region 1",
              virtq_gpa_to_hva(&mem, R1_BASE + 0x100, 16), gmem + RSIZE + 0x100);
    check_ptr("ends exactly at region end",
              virtq_gpa_to_hva(&mem, R0_BASE + RSIZE - 16, 16), gmem + RSIZE - 16);
    check_ptr("runs one byte past region end",
              virtq_gpa_to_hva(&mem, R0_BASE + RSIZE - 16, 17), NULL);
    check_ptr("straddles two regions",
              virtq_gpa_to_hva(&mem, R0_BASE + RSIZE - 8, 64), NULL);
    check_ptr("in the gap between regions",
              virtq_gpa_to_hva(&mem, 0x48000000ULL, 8), NULL);
    check_ptr("below all regions",
              virtq_gpa_to_hva(&mem, 0x100, 8), NULL);
    check_ptr("above all regions",
              virtq_gpa_to_hva(&mem, R1_BASE + RSIZE, 8), NULL);
    check_ptr("gpa + len overflows",
              virtq_gpa_to_hva(&mem, 0xfffffffffffffff0ULL, 0x20), NULL);
    check_ptr("zero length",
              virtq_gpa_to_hva(&mem, R0_BASE, 0), NULL);
}

/* ---- stages: single / chained / indirect ------------------------------ */

static unsigned pay;                 /* bump allocator over the payload arena */

static unsigned alloc_pay(unsigned n, unsigned align)
{
    pay = (pay + align - 1) & ~(align - 1);
    unsigned at = pay;
    pay += n;
    if (pay > GMEM_SIZE - 64) { pay = OFF_PAY; at = pay; pay += n; }
    return at;
}

/* copy `plen` bytes of `payload` into `nparts` descriptors in the table `tbl`,
 * starting at descriptor index `base`, chaining them with NEXT; returns the
 * number of descriptors used. */
static unsigned lay_chain(struct vring_desc *tbl, uint16_t base, uint16_t link_base,
                          const char *payload, int plen, unsigned nparts, unsigned *seed)
{
    int off = 0;
    for (unsigned p = 0; p < nparts; p++) {
        int take = (p == nparts - 1)
                     ? (plen - off)
                     : (1 + (int)rnd(seed, (unsigned)(plen - off - (int)(nparts - p))));
        if (take < 0) take = 0;
        unsigned b = alloc_pay((unsigned)take ? (unsigned)take : 1, 1);
        memcpy(gmem + b, payload + off, (size_t)take);
        tbl[base + p].addr  = gpa_of(b);
        tbl[base + p].len   = (uint32_t)take;
        tbl[base + p].flags = (p == nparts - 1) ? 0 : VRING_DESC_F_NEXT;
        tbl[base + p].next  = (uint16_t)(link_base + p + 1);
        off += take;
    }
    return nparts;
}

/* remember the record we expect and publish this chain on the avail ring */
static void publish(struct virtq *vq, uint16_t *avail_idx, unsigned *total,
                    uint16_t head, unsigned level, const char *text)
{
    snprintf(exp_log[exp_n], sizeof exp_log[0], "[%u] %s %s",
             exp_n, level_name(level), text);
    exp_n++;
    exp_head[exp_heads_n++] = head;
    vq->avail->ring[*avail_idx % QNUM] = head;
    (*avail_idx)++;
    (*total)++;
}

enum stage { S_TRANSLATE, S_SINGLE, S_CHAINED, S_INDIRECT };

int main(int argc, char **argv)
{
    const char *stage_s = (argc > 1) ? argv[1] : "chained";
    unsigned seed0 = (argc > 2) ? (unsigned)strtoul(argv[2], NULL, 10) : 1;
    unsigned seed  = seed0 ? seed0 : 1;
    const char *logpath = (argc > 3) ? argv[3] : "virtq_test.log";

    enum stage stage;
    if      (!strcmp(stage_s, "translate")) stage = S_TRANSLATE;
    else if (!strcmp(stage_s, "single"))    stage = S_SINGLE;
    else if (!strcmp(stage_s, "chained"))   stage = S_CHAINED;
    else if (!strcmp(stage_s, "indirect"))  stage = S_INDIRECT;
    else { fprintf(stderr, "unknown stage '%s'\n", stage_s); return 2; }

    gmem = calloc(1, GMEM_SIZE);
    if (!gmem) return 2;
    regions[0] = (struct virtq_mem_region){ .gpa = R0_BASE, .size = RSIZE, .hva = gmem };
    regions[1] = (struct virtq_mem_region){ .gpa = R1_BASE, .size = RSIZE, .hva = gmem + RSIZE };
    mem.regions = regions;
    mem.nregions = 2;

    if (stage == S_TRANSLATE) {
        stage_translate();
        free(gmem);
        if (!failures) printf("  PASS  translate  seed=%u\n", seed0);
        return failures ? 1 : 0;
    }

    struct vlog_sink *sink = vlog_sink_open(logpath);
    if (!sink) { fprintf(stderr, "cannot open %s\n", logpath); return 2; }

    struct virtq vq = {
        .desc       = (struct vring_desc *)(gmem + OFF_DESC),
        .avail      = (struct vring_avail *)(gmem + OFF_AVAIL),
        .used       = (struct vring_used *)(gmem + OFF_USED),
        .num        = QNUM,
        .last_avail = 0,
    };

    pay = OFF_PAY;
    uint16_t avail_idx = 0;
    unsigned total_chains = 0;

    for (int round = 0; round < 6; round++) {
        unsigned nchains = 1 + rnd(&seed, 2);
        uint16_t slot = 0;
        unsigned published = 0;

        for (unsigned c = 0; c < nchains && slot + 4 <= QNUM; c++) {
            unsigned level = rnd(&seed, 6);      /* 4..5 exercise "LVL?" */
            unsigned tlen  = 1 + rnd(&seed, 24);
            char text[64];
            for (unsigned i = 0; i < tlen; i++)
                text[i] = (char)('a' + rnd(&seed, 26));
            text[tlen] = '\0';

            char payload[128];
            int plen = snprintf(payload, sizeof payload, "%u %s\n", level, text);
            uint16_t head = slot;

            if (stage == S_SINGLE) {
                /* exactly one readable descriptor holds the whole payload */
                unsigned b = alloc_pay((unsigned)plen, 1);
                memcpy(gmem + b, payload, (size_t)plen);
                vq.desc[slot] = (struct vring_desc){ .addr = gpa_of(b),
                                                     .len = (uint32_t)plen, .flags = 0, .next = 0 };
                slot++;
            } else if (stage == S_CHAINED) {
                unsigned nparts = 2 + rnd(&seed, 2);   /* 2..3 readable parts */
                slot += (uint16_t)lay_chain(vq.desc, slot, slot, payload, plen, nparts, &seed);
                /* sometimes append a descriptor that must NOT contribute bytes */
                if (rnd(&seed, 2)) {
                    vq.desc[slot - 1].flags |= VRING_DESC_F_NEXT;
                    vq.desc[slot - 1].next = slot;
                    if (rnd(&seed, 2)) {               /* device-writable: skip */
                        unsigned b = alloc_pay(8, 1);
                        memcpy(gmem + b, "XXXXXXXX", 8);
                        vq.desc[slot] = (struct vring_desc){ .addr = gpa_of(b), .len = 8,
                                                             .flags = VRING_DESC_F_WRITE, .next = 0 };
                    } else {                           /* unmapped: reject */
                        vq.desc[slot] = (struct vring_desc){ .addr = 0x48000000ULL, .len = 8,
                                                             .flags = 0, .next = 0 };
                    }
                    slot++;
                }
            } else { /* S_INDIRECT */
                unsigned nparts = 1 + rnd(&seed, 2);
                unsigned tbl = alloc_pay(nparts * (unsigned)sizeof(struct vring_desc), 16);
                struct vring_desc *itbl = (struct vring_desc *)(gmem + tbl);
                lay_chain(itbl, 0, 0, payload, plen, nparts, &seed);
                vq.desc[slot] = (struct vring_desc){ .addr = gpa_of(tbl),
                                                     .len = nparts * (uint32_t)sizeof(struct vring_desc),
                                                     .flags = VRING_DESC_F_INDIRECT, .next = 0 };
                slot++;
            }

            publish(&vq, &avail_idx, &total_chains, head, level, text);
            published++;
        }

        vq.avail->idx = avail_idx;
        __atomic_thread_fence(__ATOMIC_RELEASE);

        int n = vlog_virtq_handle(&vq, &mem, sink);

        if (n != (int)published) {
            printf("  FAIL round %d: handler returned %d, expected %u chains\n",
                   round, n, published);
            failures++;
        }
        if (vq.used->idx != (uint16_t)total_chains) {
            printf("  FAIL round %d: used->idx = %u, expected %u\n",
                   round, vq.used->idx, total_chains);
            failures++;
        }
        if (vq.last_avail != avail_idx) {
            printf("  FAIL round %d: last_avail = %u, expected %u\n",
                   round, vq.last_avail, avail_idx);
            failures++;
        }
    }

    /* the used ring must carry the chain heads, in order (last QNUM survive) */
    for (unsigned k = 0; k < exp_heads_n; k++) {
        if (k + QNUM < exp_heads_n) continue;
        uint32_t got = vq.used->ring[k % QNUM].id;
        if (got != exp_head[k]) {
            printf("  FAIL used ring slot %u: id = %u, expected %u\n",
                   k % QNUM, got, exp_head[k]);
            failures++;
        }
    }

    vlog_sink_close(sink);

    /* the log must contain exactly the records we published, in order */
    FILE *f = fopen(logpath, "r");
    if (!f) { printf("  FAIL: no log file\n"); return 1; }
    char line[1024];
    unsigned i = 0;
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        if (l && line[l - 1] == '\n') line[l - 1] = '\0';
        if (i >= exp_n) { printf("  FAIL: extra log line: %s\n", line); failures++; break; }
        if (strcmp(line, exp_log[i]) != 0) {
            printf("  FAIL log line %u:\n    got:  %s\n    want: %s\n", i, line, exp_log[i]);
            failures++;
        }
        i++;
    }
    fclose(f);
    if (i < exp_n) {
        printf("  FAIL: only %u of %u records reached the log (first missing: %s)\n",
               i, exp_n, exp_log[i]);
        failures++;
    }

    if (!failures)
        printf("  PASS  %-9s seed=%u  (%u chains)\n", stage_s, seed0, total_chains);
    free(gmem);
    return failures ? 1 : 0;
}
