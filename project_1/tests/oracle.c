/* oracle.c — host-side reference model + blob generator (part of the test suite).
 *
 * For a (scenario, seed) pair this program:
 *   1. deterministically picks parameters (random message bytes, lengths,
 *      levels, counts, bad values, ...) from the seed,
 *   2. writes the boot-parameter blob the emulator injects with --bootinfo, and
 *   3. writes the EXPECTED observation transcript and (when deterministic) the
 *      expected log file — i.e. what a spec-correct device must produce.
 *
 * Because the expected outputs are computed here from the same seed, a student
 * device cannot hardcode answers: each run uses inputs it has never seen.
 *
 * Usage: oracle <scenario> <seed> <blob_out> <obs_out> <log_out>
 *   <scenario> in {log_one, log_many, malformed, boundary, empty, maxlen}
 * The log file is created only for scenarios with a deterministic log.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "bootinfo.h"
#include "vlog.h"   /* VLOG_MAX_MSG, VLOG_LVL_*, VLOG_ERR_* */

#define RAM_BASE_HOST 0x00100000ULL
#define RAM_SIZE_HOST (16u * 1024u * 1024u)

/* ---- Deterministic PRNG (PCG-ish LCG) -------------------------------- */
static uint64_t rng_state;
static void rng_seed(uint64_t s) { rng_state = s ? s : 0x9E3779B97F4A7C15ULL; }
static uint32_t rng_u32(void)
{
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(rng_state >> 33);
}
/* inclusive range [lo, hi] */
static uint32_t rng_range(uint32_t lo, uint32_t hi)
{
    return lo + (rng_u32() % (hi - lo + 1));
}

static const char *level_name(uint32_t l)
{
    switch (l) {
    case VLOG_LVL_DEBUG: return "DEBUG";
    case VLOG_LVL_INFO:  return "INFO";
    case VLOG_LVL_WARN:  return "WARN";
    case VLOG_LVL_ERROR: return "ERROR";
    default:             return "LVL?";
    }
}

static uint8_t scratch[8192];
static void fill_printable(uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        scratch[i] = (uint8_t)rng_range(0x21, 0x7e); /* printable, no space/newline */
}
/* Any byte except newline (0x0a) — exercises NUL-safety and high bytes. */
static void fill_binsafe(uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b;
        do { b = (uint8_t)rng_range(0x00, 0xff); } while (b == 0x0a);
        scratch[i] = b;
    }
}

/* Write blob = packed bootinfo header, zero-padded to BOOTINFO_HDR_BYTES,
 * followed by `scratch_len` scratch bytes. */
static void write_blob(const char *path, const struct bootinfo *bi, uint32_t scratch_len)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror("blob"); exit(2); }
    fwrite(bi, sizeof *bi, 1, f);
    for (size_t i = sizeof *bi; i < BOOTINFO_HDR_BYTES; i++)
        fputc(0, f);
    if (scratch_len)
        fwrite(scratch, 1, scratch_len, f);
    fclose(f);
}

static FILE *obsf;
static void obs(const char *key, uint32_t val) { fprintf(obsf, "%s=0x%08x\n", key, val); }

static void log_record(FILE *lf, uint32_t seq, uint32_t level,
                       const uint8_t *msg, uint32_t len)
{
    fprintf(lf, "[%u] %s ", seq, level_name(level));
    if (len) fwrite(msg, 1, len, lf);
    fputc('\n', lf);
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr,
                "usage: %s <scenario> <seed> <blob_out> <obs_out> <log_out>\n",
                argv[0]);
        return 2;
    }
    const char *scen = argv[1];
    rng_seed(strtoull(argv[2], NULL, 0));
    const char *blob_out = argv[3];
    const char *obs_out  = argv[4];
    const char *log_out  = argv[5];

    obsf = fopen(obs_out, "w");
    if (!obsf) { perror("obs"); return 2; }

    struct bootinfo bi;
    memset(&bi, 0, sizeof bi);
    bi.magic = BOOTINFO_MAGIC;
    bi.scratch_addr = BOOTINFO_BASE + BOOTINFO_HDR_BYTES;

    if (!strcmp(scen, "log_one")) {
        uint32_t len = rng_range(1, 64), level = rng_range(0, 3);
        fill_printable(len);
        bi.scenario = SC_LOG_ONE; bi.scratch_len = len; bi.level = level; bi.count = 1;
        write_blob(blob_out, &bi, len);
        obs("rc", 0); obs("err", 0); obs("seq", 1);
        FILE *lf = fopen(log_out, "w");
        log_record(lf, 0, level, scratch, len); fclose(lf);

    } else if (!strcmp(scen, "log_many")) {
        uint32_t count = rng_range(2, 16), len = rng_range(1, 32), level = rng_range(0, 3);
        fill_printable(len);
        bi.scenario = SC_LOG_MANY; bi.scratch_len = len; bi.level = level; bi.count = count;
        write_blob(blob_out, &bi, len);
        obs("err", 0); obs("seq", count);
        FILE *lf = fopen(log_out, "w");
        for (uint32_t i = 0; i < count; i++) log_record(lf, i, level, scratch, len);
        fclose(lf);

    } else if (!strcmp(scen, "malformed")) {
        uint32_t level = rng_range(0, 3);
        uint32_t badlen = rng_range(VLOG_MAX_MSG + 1, 100000);
        uint32_t badaddr = rng_range(0x1000, (uint32_t)RAM_BASE_HOST - 16); /* below RAM */
        fill_printable(8);
        bi.scenario = SC_MALFORMED; bi.scratch_len = 8; bi.level = level;
        bi.arg0 = badlen; bi.arg1 = badaddr;
        write_blob(blob_out, &bi, 8);
        obs("err_badcmd", 1); obs("errcode_badcmd", VLOG_ERR_BADCMD);
        obs("rc_badlen", VLOG_ERR_BADLEN); obs("rc_badaddr", VLOG_ERR_BADADDR);
        obs("seq", 0);
        FILE *lf = fopen(log_out, "w"); fclose(lf); /* expected: empty log */

    } else if (!strcmp(scen, "boundary")) {
        uint32_t level = rng_range(0, 3), len = rng_range(1, 16);
        bi.scenario = SC_BOUNDARY; bi.scratch_len = 0; bi.level = level; bi.arg0 = len;
        write_blob(blob_out, &bi, 0);
        obs("rc_at_top", 0); obs("seq_after_top", 1);
        obs("rc_past_top", VLOG_ERR_BADADDR); obs("seq_final", 1);
        /* no deterministic log (top-of-RAM bytes are unspecified) */

    } else if (!strcmp(scen, "empty")) {
        uint32_t level = rng_range(0, 3);
        bi.scenario = SC_EMPTY; bi.scratch_len = 0; bi.level = level; bi.count = 1;
        write_blob(blob_out, &bi, 0);
        obs("rc", 0); obs("err", 0); obs("seq", 1);
        FILE *lf = fopen(log_out, "w");
        log_record(lf, 0, level, scratch, 0); fclose(lf);

    } else if (!strcmp(scen, "maxlen")) {
        uint32_t level = rng_range(0, 3);
        fill_printable(VLOG_MAX_MSG);
        bi.scenario = SC_MAXLEN; bi.scratch_len = VLOG_MAX_MSG; bi.level = level;
        write_blob(blob_out, &bi, VLOG_MAX_MSG);
        obs("rc_max", 0); obs("seq_after_max", 1);
        obs("rc_over", VLOG_ERR_BADLEN); obs("seq_final", 1);
        FILE *lf = fopen(log_out, "w");
        log_record(lf, 0, level, scratch, VLOG_MAX_MSG); fclose(lf);

    } else if (!strcmp(scen, "level_oob")) {
        /* SC_LOG_ONE behavior, but with a severity level outside 0..3, which
         * must be logged with the placeholder name "LVL?" (not an error). */
        uint32_t len = rng_range(1, 32), level = rng_range(4, 1000);
        fill_printable(len);
        bi.scenario = SC_LOG_ONE; bi.scratch_len = len; bi.level = level; bi.count = 1;
        write_blob(blob_out, &bi, len);
        obs("rc", 0); obs("err", 0); obs("seq", 1);
        FILE *lf = fopen(log_out, "w");
        log_record(lf, 0, level, scratch, len); fclose(lf);

    } else if (!strcmp(scen, "binsafe")) {
        /* SC_LOG_ONE behavior with an arbitrary-byte message (incl. NUL and
         * high bytes): the device must write the bytes verbatim, not treat the
         * buffer as a C string. */
        uint32_t len = rng_range(4, 64), level = rng_range(0, 3);
        fill_binsafe(len);
        /* Guarantee the discriminating bytes so this always catches a device
         * that treats the buffer as a C string: an embedded NUL (not at the
         * end) followed by a non-NUL byte that must still be written. */
        scratch[len / 2] = 0x00;
        scratch[len - 1] = 0xff;
        bi.scenario = SC_LOG_ONE; bi.scratch_len = len; bi.level = level; bi.count = 1;
        write_blob(blob_out, &bi, len);
        obs("rc", 0); obs("err", 0); obs("seq", 1);
        FILE *lf = fopen(log_out, "w");
        log_record(lf, 0, level, scratch, len); fclose(lf);

    } else if (!strcmp(scen, "regs")) {
        uint32_t v_lo = rng_u32(), v_hi = rng_u32(), v_len = rng_u32(), v_lvl = rng_u32();
        bi.scenario = SC_REGS; bi.scratch_len = 0;
        bi.arg0 = v_lo; bi.arg1 = v_hi; bi.count = v_len; bi.level = v_lvl;
        write_blob(blob_out, &bi, 0);
        obs("msg_lo", v_lo); obs("msg_hi", v_hi);
        obs("len", v_len); obs("level", v_lvl);
        obs("id_after_ro", VLOG_MAGIC); obs("seq_after_ro", 0);
        obs("status_ro_unchanged", 1);
        /* no log */

    } else if (!strcmp(scen, "nop")) {
        uint32_t badcmd = rng_range(3, 0xffff);
        bi.scenario = SC_NOP; bi.scratch_len = 0; bi.arg0 = badcmd;
        write_blob(blob_out, &bi, 0);
        obs("err_after_badcmd", 1); obs("errcode_after_badcmd", VLOG_ERR_BADCMD);
        obs("err_after_nop", 0); obs("seq", 0);
        /* no log */

    } else if (!strcmp(scen, "recover")) {
        uint32_t len = rng_range(1, 32), level = rng_range(0, 3);
        uint32_t badcmd = rng_range(3, 0xffff);
        fill_printable(len);
        bi.scenario = SC_RECOVER; bi.scratch_len = len; bi.level = level; bi.arg0 = badcmd;
        write_blob(blob_out, &bi, len);
        obs("err_after_bad", 1);
        obs("rc", 0); obs("err", 0); obs("seq", 1);
        FILE *lf = fopen(log_out, "w");
        log_record(lf, 0, level, scratch, len); fclose(lf);

    } else if (!strcmp(scen, "interleave")) {
        uint32_t len = rng_range(1, 32), level = rng_range(0, 3);
        uint32_t badaddr = rng_range(0x1000, (uint32_t)RAM_BASE_HOST - 16);
        fill_printable(len);
        bi.scenario = SC_INTERLEAVE; bi.scratch_len = len; bi.level = level; bi.arg1 = badaddr;
        write_blob(blob_out, &bi, len);
        obs("rc1", 0); obs("rc2", VLOG_ERR_BADADDR); obs("rc3", 0); obs("seq", 2);
        FILE *lf = fopen(log_out, "w");
        log_record(lf, 0, level, scratch, len);
        log_record(lf, 1, level, scratch, len);
        fclose(lf);

    } else if (!strcmp(scen, "flush")) {
        uint32_t len = rng_range(1, 32), level = rng_range(0, 3);
        fill_printable(len);
        bi.scenario = SC_FLUSH; bi.scratch_len = len; bi.level = level; bi.count = 1;
        write_blob(blob_out, &bi, len);
        obs("rc_log", 0); obs("err_after_flush", 0); obs("seq", 1);
        FILE *lf = fopen(log_out, "w");
        log_record(lf, 0, level, scratch, len); fclose(lf);

    } else if (!strcmp(scen, "stat")) {
        uint32_t level = rng_range(0, 3);
        uint32_t len   = rng_range(1, 64);
        uint32_t count = rng_range(1, 8);
        uint32_t small = rng_range(0, sizeof(struct vlog_stats) - 1);  /* too small */
        uint32_t badaddr = rng_range(0x1000, (uint32_t)RAM_BASE_HOST - 16); /* below RAM */
        fill_printable(len);
        bi.scenario = SC_STAT; bi.scratch_len = len; bi.level = level;
        bi.count = count; bi.arg0 = small; bi.arg1 = badaddr;
        write_blob(blob_out, &bi, len);

        obs("stat_rc", 0);
        obs("records", count);
        obs("bytes",   count * len);
        obs("seq",     count);
        obs("small_rc",        VLOG_ERR_BADLEN);
        obs("small_untouched", 0x11111111);
        obs("badaddr_rc",      VLOG_ERR_BADADDR);

        FILE *lf = fopen(log_out, "w");
        for (uint32_t i = 0; i < count; i++)
            log_record(lf, i, level, scratch, len);
        fclose(lf);

    } else {
        fprintf(stderr, "unknown scenario: %s\n", scen);
        fclose(obsf);
        return 2;
    }

    fclose(obsf);
    return 0;
}
