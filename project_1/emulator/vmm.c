/* vmm.c: STUDENT IMPLEMENTATION FILE for Project 1 (the VMM core).
 *
 * You build the virtual machine monitor: guest RAM, the serial/control device,
 * device MMIO registration, the initial stack pointer, flat-binary loading, the
 * run loop, and guest->host address translation. The logging device itself is
 * in device.c (also yours).
 *
 * WHAT IS PROVIDED (grader/boot glue, leave alone): opening the log file and
 * the Unicorn CPU, the optional instruction tracer, allocating + init-ing the
 * device instance, the boot-parameter pointer (RDI) and blob loader, and
 * teardown. Everything marked TODO is yours.
 *
 * Every TODO is exercised by the basic test suite (a guest can't boot, print,
 * log, or power off without them), so you can develop against `tests/`.
 *
 * See SPEC.md Part I for the machine ABI (memory map, serial protocol, entry
 * state) and the exact contract of each function.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmm.h"
#include "device.h"

/* ---- Serial / control region ----------------------------------------- */

/* `static inline` so the unused-until-you-wire-them stubs don't warn; you pass
 * their addresses to uc_mmio_map() when you register the serial region. */
static inline uint64_t serial_read(uc_engine *uc, uint64_t offset,
                                   unsigned size, void *user_data)
{
    (void)uc; (void)offset; (void)size; (void)user_data;
    return 0; /* nothing readable */
}

static inline void serial_write(uc_engine *uc, uint64_t offset,
                                unsigned size, uint64_t value, void *user_data)
{
    (void)size;
    struct vmm *v = user_data;
    (void)v; (void)uc; (void)offset; (void)value;

    /* TODO(student): implement the serial/control protocol (SPEC.md Part I, "Serial / control protocol"):
     *   - offset SERIAL_TX:       write the low byte of `value` to host stdout.
     *   - offset SERIAL_POWEROFF: record the exit code (`value`), mark the VM
     *                             powered off, and stop the CPU (uc_emu_stop).
     *   - anything else:          ignore. */
}

/* ---- Guest memory faults ---------------------------------------------- */

/* TODO(student): implement the unmapped-memory callback (SPEC.md Part I,
 * "Guest faults"). Unicorn calls it when the guest reads/writes/executes an
 * address that is not mapped (no RAM, no MMIO region).
 *   - record the fault in the VMM (v->faulted, v->fault_addr);
 *   - report it on stderr (NOT stdout, which is the guest's console);
 *   - stop the CPU with uc_emu_stop();
 *   - return false, meaning "do not retry the access".
 * `static inline` so it does not warn until you wire it up in vmm_create(). */
static inline bool mem_invalid(uc_engine *uc, uc_mem_type type, uint64_t address,
                               int size, int64_t value, void *user_data)
{
    (void)uc; (void)type; (void)address; (void)size; (void)value; (void)user_data;
    return false;
}

/* ---- Optional per-instruction tracing (provided) --------------------- */

static void trace_code(uc_engine *uc, uint64_t address,
                       uint32_t size, void *user_data)
{
    (void)uc; (void)user_data;
    fprintf(stderr, "[trace] rip=0x%08llx (%u bytes)\n",
            (unsigned long long)address, size);
}

/* ---- Lifecycle -------------------------------------------------------- */

int vmm_create(struct vmm *v, int trace, const char *log_path)
{
    memset(v, 0, sizeof *v);
    v->trace = trace;

    /* provided: open the device's host log sink */
    v->store = logstore_open(log_path);
    if (!v->store) {
        fprintf(stderr, "cannot open log file '%s'\n", log_path);
        return -1;
    }

    /* provided: create the emulated x86-64 CPU */
    uc_err err = uc_open(UC_ARCH_X86, UC_MODE_64, &v->uc);
    if (err) {
        fprintf(stderr, "uc_open: %s\n", uc_strerror(err));
        return -1;
    }

    /* TODO(student): allocate RAM_SIZE bytes of zeroed guest RAM into v->ram,
     * and map it into the guest at RAM_BASE with uc_mem_map_ptr (host-backed,
     * UC_PROT_ALL) so the device can translate guest addresses to host
     * pointers. Return -1 on failure. */

    /* TODO(student): register the serial/control MMIO region at SERIAL_BASE
     * (size SERIAL_SIZE) with uc_mmio_map, using serial_read / serial_write and
     * `v` as the user_data for both. */

    /* provided: allocate and initialize the device instance (its logic lives
     * in device.c) */
    v->dev = calloc(1, sizeof *v->dev);
    if (!v->dev) {
        fprintf(stderr, "out of memory allocating device\n");
        return -1;
    }
    vlog_device_init(v->dev, v);

    /* TODO(student): register the logging device's MMIO region at DEV_BASE
     * (size DEV_SIZE) with uc_mmio_map, using vlog_device_mmio_read /
     * vlog_device_mmio_write and v->dev as the user_data for both. */

    /* TODO(student): set the initial stack pointer. RSP goes just below the
     * reserved boot-info region (BOOTINFO_BASE), 16-byte aligned, via
     * uc_reg_write(UC_X86_REG_RSP, ...). The guest needs a stack to run. */

    /* provided: boot-parameter pointer. The guest receives BOOTINFO_BASE in
     * RDI (its main()'s first argument). Leave this as-is. */
    uint64_t rdi = BOOTINFO_BASE;
    uc_reg_write(v->uc, UC_X86_REG_RDI, &rdi);

    /* TODO(student): register mem_invalid() for unmapped accesses with
     * uc_hook_add(..., UC_HOOK_MEM_UNMAPPED, mem_invalid, v, 1, 0) so a guest
     * that touches unmapped memory faults cleanly instead of taking the
     * emulator down with it. */

    /* provided: optional instruction tracing (--trace) */
    if (trace) {
        uc_hook h;
        uc_hook_add(v->uc, &h, UC_HOOK_CODE, trace_code, NULL,
                    RAM_BASE, RAM_BASE + RAM_SIZE - 1);
    }
    return 0;
}

int vmm_load_binary(struct vmm *v, const char *path)
{
    (void)v; (void)path;
    /* TODO(student): read the whole flat binary at `path` into guest RAM
     * starting at v->ram (offset 0 == RAM_BASE), rejecting a file larger than
     * RAM_SIZE, then set the initial RIP to RAM_BASE (the entry point) with
     * uc_reg_write(UC_X86_REG_RIP, ...). Return 0 on success, -1 on error. */
    return -1;
}

/* provided: boot-parameter blob loader (used by the test harness via
 * --bootinfo). Blits the opaque blob into the reserved region at the top of
 * RAM; the guest reads it through the RDI pointer set in vmm_create(). */
int vmm_load_bootinfo(struct vmm *v, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("fopen bootinfo");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || (uint64_t)sz > BOOTINFO_SIZE) {
        fprintf(stderr, "bootinfo blob too large or unreadable\n");
        fclose(f);
        return -1;
    }
    size_t n = fread(v->ram + (BOOTINFO_BASE - RAM_BASE), 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        fprintf(stderr, "short read loading bootinfo\n");
        return -1;
    }
    return 0;
}

int vmm_run(struct vmm *v)
{
    (void)v;
    /* TODO(student): start executing the guest from RIP (RAM_BASE) with
     * uc_emu_start. The guest never returns normally; it stops when the
     * POWEROFF register is written (your serial_write calls uc_emu_stop).
     *   - if the guest FAULTED (v->faulted), return VMM_EXIT_FAULT;
     *   - a Unicorn error while NOT powered off is a failure (return non-zero);
     *   - otherwise return v->exit_code. */
    return 1;
}

void vmm_destroy(struct vmm *v)
{
    if (v->uc) uc_close(v->uc);
    if (v->store) logstore_close(v->store);
    free(v->ram);
    free(v->dev);
    v->uc = NULL;
    v->store = NULL;
    v->ram = NULL;
    v->dev = NULL;
}

void *vmm_gpa_to_host(struct vmm *v, uint64_t gpa, uint64_t len)
{
    (void)v; (void)gpa; (void)len;
    /* TODO(student): translate the guest-physical range [gpa, gpa+len) to a
     * host pointer into v->ram. Return NULL unless the ENTIRE range lies within
     * guest RAM [RAM_BASE, RAM_BASE + RAM_SIZE). Beware integer overflow when
     * checking the upper bound. See SPEC.md Part I, vmm_gpa_to_host. */
    return NULL;
}
