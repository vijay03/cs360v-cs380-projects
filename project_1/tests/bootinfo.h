/* bootinfo.h — boot-parameter blob contract.
 *
 * The test harness injects a blob into the reserved region at BOOTINFO_BASE
 * (emulator option --bootinfo); the guest receives a pointer to it in RDI (its
 * main() first argument). This header is the single source of truth for the
 * layout, shared by the parameterized test guest (ag_guest.c) and the host-side
 * oracle (oracle.c), both alongside it in tests/. The emulator never interprets
 * the blob.
 *
 * BOOTINFO_BASE must match emulator/vmm.h.
 */
#ifndef BOOTINFO_H
#define BOOTINFO_H

#include <stdint.h>

#define BOOTINFO_BASE      0x010F0000ULL  /* RAM_BASE + RAM_SIZE - 0x10000 */
#define BOOTINFO_MAGIC     0xB007B007u
#define BOOTINFO_HDR_BYTES 64             /* scratch data starts here in the blob */

/* Scenario codes — which behavior the parameterized guest should exercise.
 * (Several oracle scenarios reuse SC_LOG_ONE with different data, e.g. an
 * out-of-range level or a binary-safe message.) */
enum {
    SC_LOG_ONE    = 1,  /* log one message (scratch, scratch_len, level)      */
    SC_LOG_MANY   = 2,  /* log the scratch message `count` times              */
    SC_MALFORMED  = 3,  /* bad command, bad length (arg0), bad address (arg1) */
    SC_BOUNDARY   = 4,  /* log `arg0` bytes ending at top of RAM, then +1     */
    SC_EMPTY      = 5,  /* log a zero-length message                          */
    SC_MAXLEN     = 6,  /* log exactly VLOG_MAX_MSG bytes, then one more      */
    SC_REGS       = 7,  /* operand-register round-trip; RO writes ignored     */
    SC_NOP        = 8,  /* bad cmd (arg0) sets error; NOP clears it           */
    SC_RECOVER    = 9,  /* bad cmd (arg0) error, then a valid LOG recovers    */
    SC_INTERLEAVE = 10, /* LOG ok, bad-addr (arg1) LOG, LOG ok -> seq advances*/
    SC_FLUSH      = 11, /* LOG then FLUSH: no error, SEQ unchanged            */
    SC_STAT       = 12, /* log `count` msgs, then STAT into guest memory;
                         * arg0 = too-small buffer len, arg1 = bad address     */
};

/* Parameter header. Packed + fixed-width so host and guest agree byte-for-byte.
 * Scratch data (if any) follows at offset BOOTINFO_HDR_BYTES in the blob, and
 * scratch_addr points at it in guest physical memory. */
struct bootinfo {
    uint32_t magic;        /* BOOTINFO_MAGIC                       */
    uint32_t scenario;     /* one of SC_*                          */
    uint64_t scratch_addr; /* guest-phys address of scratch buffer */
    uint32_t scratch_len;  /* scratch length in bytes              */
    uint32_t level;        /* severity level for LOG scenarios     */
    uint32_t count;        /* repeat count for SC_LOG_MANY         */
    uint32_t arg0;         /* scenario-specific                    */
    uint32_t arg1;         /* scenario-specific                    */
} __attribute__((packed));

#endif /* BOOTINFO_H */
