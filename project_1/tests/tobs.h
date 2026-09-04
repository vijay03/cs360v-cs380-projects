/* tobs.h: observation emitter for the Project 1 tests.
 *
 * Host-side verdicts: a test guest's only job is to EXERCISE the device and
 * print the raw values it observed, as "key=0x........" lines on the serial
 * console. It makes no pass/fail decision. The host runner (run_tests.sh) holds
 * the expected values (tests/expected/<name>.obs) and renders the verdict by
 * diffing the transcript against them, so the device under test cannot assert
 * its own success.
 *
 * Keep the emitted output limited to obs() lines so the transcript is
 * byte-for-byte comparable.
 */
#ifndef TOBS_H
#define TOBS_H

#include "vlog.h"

/* Emit one observation: "key=0xXXXXXXXX\n". */
static inline void obs(const char *key, uint32_t val)
{
    puts_(key);
    putc_('=');
    puthex_(val);   /* prints "0x" + 8 lowercase hex digits */
    putc_('\n');
}

/* Spec-defined STATUS fields, reported raw (no judgment here). */
static inline uint32_t dev_err(void)
{
    return (vlog_status() & VLOG_STATUS_ERROR) ? 1u : 0u;
}
static inline uint32_t dev_errcode(void)
{
    return (vlog_status() >> 8) & 0xffu;
}

#endif /* TOBS_H */
