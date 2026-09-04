/* main.c: provided emulator entry point for Project 1.
 *
 * Usage:
 *   ./emulator [--trace] [--log <path>] [--bootinfo <path>] [--listen <port>] <guest.bin>
 *   ./emulator --listen <port> [--log <path>]        (collector mode: no guest)
 *
 * Loads a flat guest binary, runs it on the Unicorn-based VMM, and returns the
 * exit code the guest reported via the POWEROFF register. The logging device
 * appends records to <path> (default: vlog.log).
 *
 * With --listen, the provided network ingest listener (netlog.c) runs
 * concurrently and appends records received over TCP to the same log store.
 * With --listen and NO guest binary, the emulator runs as a standalone
 * collector: just the store + the listener, until interrupted (Ctrl-C/SIGTERM).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "vmm.h"
#include "netlog.h"

static volatile sig_atomic_t g_stop;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

int main(int argc, char **argv)
{
    const char *binpath = NULL;
    const char *log_path = "vlog.log";
    const char *bootinfo_path = NULL;
    int trace = 0;
    int listen_port = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trace")) {
            trace = 1;
        } else if (!strcmp(argv[i], "--log")) {
            if (i + 1 >= argc) { fprintf(stderr, "--log requires a path\n"); return 2; }
            log_path = argv[++i];
        } else if (!strcmp(argv[i], "--bootinfo")) {
            if (i + 1 >= argc) { fprintf(stderr, "--bootinfo requires a path\n"); return 2; }
            bootinfo_path = argv[++i];
        } else if (!strcmp(argv[i], "--listen")) {
            if (i + 1 >= argc) { fprintf(stderr, "--listen requires a port\n"); return 2; }
            listen_port = atoi(argv[++i]);
        } else {
            binpath = argv[i];
        }
    }

    /* Collector mode: --listen with no guest. Store + network listener only,
     * until interrupted (the standalone-collector deployment, the bridge to a
     * central log collector for later projects). */
    if (!binpath && listen_port > 0) {
        logstore *store = logstore_open(log_path);
        if (!store) { fprintf(stderr, "cannot open log '%s'\n", log_path); return 1; }
        netlog *nl = netlog_start(store, listen_port);
        if (!nl) {
            fprintf(stderr, "netlog_start failed on port %d\n", listen_port);
            logstore_close(store);
            return 1;
        }
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        fprintf(stderr, "collector: listening on :%d -> %s (Ctrl-C to stop)\n",
                listen_port, log_path);
        while (!g_stop) pause();
        netlog_stop(nl);
        logstore_close(store);
        return 0;
    }

    if (!binpath) {
        fprintf(stderr, "usage: %s [--trace] [--log <path>] [--bootinfo <path>]"
                        " [--listen <port>] <guest.bin>\n", argv[0]);
        return 2;
    }

    struct vmm v;
    if (vmm_create(&v, trace, log_path)) {
        fprintf(stderr, "vmm_create failed\n");
        return 1;
    }
    if (vmm_load_binary(&v, binpath)) {
        fprintf(stderr, "failed to load %s\n", binpath);
        vmm_destroy(&v);
        return 1;
    }
    if (bootinfo_path && vmm_load_bootinfo(&v, bootinfo_path)) {
        fprintf(stderr, "failed to load bootinfo %s\n", bootinfo_path);
        vmm_destroy(&v);
        return 1;
    }

    /* Optional concurrent network ingest, sharing the VMM's log store. */
    netlog *nl = NULL;
    if (listen_port > 0) {
        nl = netlog_start(v.store, listen_port);
        if (!nl)
            fprintf(stderr, "warning: netlog_start failed on port %d "
                            "(continuing without network ingest)\n", listen_port);
    }

    int rc = vmm_run(&v);

    if (nl) netlog_stop(nl);
    vmm_destroy(&v);
    return rc;
}
