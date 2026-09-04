# The function runner (shared, provided)

A tiny map-reduce style function runner. It is **provided** to students, not
implemented by them, and it is the through line that ties the course together:

| Project | How it uses the runner |
|---|---|
| **1** (VM / device) | The guest logs in the runner's record format; the MMIO device is a log sink for it. |
| **2** (container) | The runner can be the workload inside a container, which makes that container a Project 4 backend. |
| **3** (unikernel) | Students compile **this same `runner.c`** into a Unikraft image, at the source level, and write a tool to deploy it. |
| **4** (serverless) | Students' platform is the runner's **client**: it deploys functions to backends and invokes them over the network. |

Because it has to run in all three places, `runner.c` sticks to plain POSIX
sockets and stdio: no threads, no `fork`, no dynamic loading. The "functions" are
built in and selected by name, since a unikernel cannot load code at run time.

## Protocol

Line oriented over TCP. One connection carries one command, then closes.

```text
PING                  ->  PONG
FUNCS                 ->  OK wordcount sum
RUN <fn> <payload>    ->  OK <result>      |   ERR <reason>
```

Examples:

```text
RUN wordcount the quick brown fox   ->  OK 4
RUN sum 10 20 30                    ->  OK 60
RUN sum 1 two                       ->  ERR not a number
RUN nope x                          ->  ERR no such function
```

`PING` exists so a deployer can tell when an instance is actually **ready** to
serve, rather than merely booted. Project 3's deployment tool needs exactly that.

## Logging

Every interesting event is written to stdout as one record in the course's
standard format, `<level> <message>`, with `0=DEBUG 1=INFO 2=WARN 3=ERROR`:

```text
1 function runner started on port 8080
1 invoke wordcount
1 wordcount -> 4
2 no such function: nope
```

This is the same format Project 1's virtual device ingests. In a unikernel stdout
is the console, so whoever boots the image captures the log.

### Shipping logs to a collector

Records always go to stdout. With `--log-to HOST:PORT` the same lines are *also*
sent over TCP, one record per line, which is exactly what a log collector
expects on the wire:

```bash
./runner 8080 --log-to 127.0.0.1:9099
```

For an ordinary process you could just pipe stdout somewhere, but a unikernel has
no shell to pipe through, so this is how it ships its logs anywhere at all.

The sink is strictly best effort, because logging must never take the runner
down. The connection is non-blocking and is never waited on; a collector that is
absent, slow to start, or restarted only costs records. Records produced before
the connection completes are held in a small buffer and flushed once it does, so
the startup record is not lost. `HOST` must be a dotted IPv4 address: there is no
name resolution.

## Building

```bash
make            # ./runner            (local)
make static     # ./runner-static     (container rootfs / initramfs)
./runner 8080   # port from argv (default 8080)
```

## Adding a function

Write `static int fn_<name>(const char *payload, char *out, size_t n)`, returning
0 on success (with the result in `out`) or -1 on failure (with the reason in
`out`), then add it to the `FUNCS` table. The protocol does not change.
