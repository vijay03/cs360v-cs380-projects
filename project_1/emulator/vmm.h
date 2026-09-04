/* vmm.h: VMM interface + machine ABI for Project 1 (provided).
 *
 * This header defines the machine layout (memory map, serial protocol) and the
 * functions you implement in vmm.c. Do not modify this file: it is the shared
 * contract between the emulator, the guest, and the tests. You implement the
 * VMM core in vmm.c and the device in device.c/.h. See SPEC.md (Part I = the machine ABI).
 *
 * The emulator is built on the Unicorn Engine (software CPU emulation), so it
 * runs without any hardware-virtualization support (no /dev/kvm required).
 */
#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unicorn/unicorn.h>

#include "logstore.h"

struct vlog_device; /* defined in device.h */

/* ---- Guest physical memory map ---------------------------------------- */

/* Main guest RAM. The guest binary is loaded at RAM_BASE and the initial
 * RIP/RSP are set up here. With paging disabled (the emulator never installs
 * page tables) linear addresses equal physical addresses, so a guest pointer
 * is also a guest-physical address. */
#define RAM_BASE        0x00100000ULL          /* 1 MiB */
#define RAM_SIZE        (16u * 1024u * 1024u)  /* 16 MiB */

/* Serial / control region (provided). */
#define SERIAL_BASE     0x10000000ULL
#define SERIAL_SIZE     0x1000
#define SERIAL_TX       0x00   /* W: low byte is written to the host stdout  */
#define SERIAL_POWEROFF 0x08   /* W: halt the machine, value = exit code     */

/* Logging device region (students implement the behavior in device.c). */
#define DEV_BASE        0x20000000ULL
#define DEV_SIZE        0x1000

/* Boot-parameter injection region. The top BOOTINFO_SIZE bytes of RAM are
 * reserved for a parameter blob the test harness injects with --bootinfo; the
 * guest receives a pointer to it in RDI. The emulator treats the blob as opaque
 * bytes; its layout is a contract between the harness and the guest. The
 * sample guests ignore RDI. */
#define BOOTINFO_SIZE   0x10000ULL
#define BOOTINFO_BASE   (RAM_BASE + RAM_SIZE - BOOTINFO_SIZE)

/* Exit code the VMM reports when the guest faults (touches memory that is not
 * mapped). A guest can do this by accident or on purpose; either way the VMM
 * must survive it and report it, not crash. */
#define VMM_EXIT_FAULT  77

struct vmm {
    uc_engine          *uc;
    uint8_t            *ram;        /* host backing store for guest RAM      */
    struct vlog_device *dev;        /* the MMIO device instance              */
    logstore           *store;      /* thread-safe log sink (all ingest paths)*/
    int                 trace;      /* per-instruction tracing enabled?      */
    int                 powered_off;/* set when the guest writes POWEROFF    */
    int                 exit_code;  /* exit code reported by the guest       */
    int                 faulted;    /* set when the guest hit unmapped memory*/
    uint64_t            fault_addr; /* the guest-physical address it touched */
};

/* Lifecycle. Each returns 0 on success, non-zero on failure.
 * log_path names the host file the logging device appends to (truncated on
 * open). */
int  vmm_create(struct vmm *v, int trace, const char *log_path);
int  vmm_load_binary(struct vmm *v, const char *path);
int  vmm_load_bootinfo(struct vmm *v, const char *path); /* inject param blob */
int  vmm_run(struct vmm *v);                 /* returns guest exit code */
void vmm_destroy(struct vmm *v);

/* Provided pointer-translation helper.
 *
 * Translate a guest-physical address range [gpa, gpa+len) into a host pointer.
 * Returns NULL if the range is not entirely contained within guest RAM.
 *
 * The device MUST use this helper to access guest buffers; it must never cast
 * a guest address to a host pointer directly. */
void *vmm_gpa_to_host(struct vmm *v, uint64_t gpa, uint64_t len);

#endif /* VMM_H */
