/* netlog.h: network log-ingest adapter (PROVIDED, not a student deliverable).
 *
 * A second ingest path into the shared log store: a TCP listener that receives
 * log records over the network and appends them to the SAME store the MMIO
 * device writes. It runs on its own thread, concurrently with the emulator's
 * vCPU, which is why the store is mutex-guarded.
 *
 * This is the bridge to the later "central log collector" deployment: with
 * --listen and no guest, the emulator runs as a standalone collector, which is
 * the shape a networked workload (Project 4) ships its logs to.
 *
 * Wire protocol (minimal, line-oriented): one record per line,
 *     "<level> <message>\n"
 * where <level> is a decimal 0..3 (see SPEC) and <message> is the rest of the
 * line. Example:  echo '1 hello world' | nc localhost 9099
 */
#ifndef NETLOG_H
#define NETLOG_H

#include "logstore.h"

typedef struct netlog netlog;

netlog *netlog_start(logstore *store, int port);
void    netlog_stop(netlog *nl);

#endif /* NETLOG_H */
