# Project 3: Specification

You take `runner.c`, a small server program, and turn it into a **unikernel**: a
single bootable image in which your program and the kernel are compiled together,
with nothing else running. Then you write the tool that builds, deploys, and
manages instances of it.

`runner/runner.c` is provided. It listens on a TCP port and answers one-line
commands: `PING` (answers `PONG`), `FUNCS`, `INFO`, and `RUN <function> <args>`
(for example, `RUN sum 1 2 3` answers `OK 6`).

You implement these, in this order:

| What | Where | Part |
|---|---|---|
| what the image contains | `app/Kraftfile` | I |
| which source files are compiled in | `app/Makefile.uk` | I (and III) |
| building the image | `build` in `deploy/ukdeploy.py` | I |
| deploying and managing instances | five helper functions, then `deploy`, `list`, `stop` in `deploy/ukdeploy.py` | II |
| writing a Unikraft library | `app/Config.uk`, `app/Makefile.uk` (TODO B), `app/libukstats/ukstats.c` | III |
| measuring cold start | `bench`, `list --json` in `deploy/ukdeploy.py` | IV |

Everything runs in the Ubuntu VM you set up in Project 0, where `setup-vm.sh`
installed `kraft` and QEMU and downloaded Unikraft's packages.

---

## Part I: Building `runner.c` into a unikernel image

In this part you write three things, in this order:

1. `app/Kraftfile`: what the image is made of;
2. `app/Makefile.uk`: which source files are compiled into it;
3. `build` in `deploy/ukdeploy.py`: the command that builds it.

**1. `app/Kraftfile`**

The Kraftfile lists what goes into the image: the Unikraft core (the kernel
itself) and any extra libraries. You switch features on with options named
`CONFIG_…`. Fill in the file's two TODOs:

1. the core, `unikraft` at version `stable`, with these options:
   - `CONFIG_LIBNOLIBC`: a small C library, enough for `runner.c`;
   - `CONFIG_LIBUKSCHED` and `CONFIG_LIBUKSCHEDCOOP`: a thread scheduler, which
     the network library needs;
2. the library `lwip` at version `stable`: the network stack, so the runner's
   sockets work. Its options are listed in the TODO.

**2. `app/Makefile.uk`**

`Makefile.uk` tells the build which source files to compile into the image. Do
only its first TODO (A) for now; the second (B) is for Part III. TODO A is two
lines:

```make
$(eval $(call addlib,apprunner))
APPRUNNER_SRCS-y += $(APPRUNNER_BASE)/../runner/runner.c
```

- The first line registers your app with the build under the name `apprunner`.
  That name gives you two variables: `APPRUNNER_BASE`, the `app/` directory, and
  `APPRUNNER_SRCS-y`, the list of files to compile.
- The second line adds `runner.c` to that list. The path goes up from `app/` to
  `runner/runner.c`, so the image is built from `runner/runner.c` itself, not a
  copy.

**3. `build` in `deploy/ukdeploy.py`**

`./deploy/ukdeploy.py build` calls `cmd_build()`, which runs the `kraft` tool to
build the image from your `Kraftfile` and `Makefile.uk`. The steps are in the
function's comment. The kraft command always passes two flags:

- `--no-update`: without it, kraft re-downloads its package list on every build,
  which takes minutes. `setup-vm.sh` already downloaded it for you.
- `--no-prompt`: without it, kraft may try to ask you a question. When there is
  no terminal to answer it, as in the tests, the build fails with an obscure
  error.

*Check it:* `./deploy/ukdeploy.py build` produces an image under
`app/.unikraft/build/`.

---

## Part II: The deployment tool

`deploy/ukdeploy.py` manages instances. An **instance** is one running
unikernel: a QEMU process, a port on your VM that QEMU forwards to the runner
inside it, and a log file of its console output. The tool saves its list of
instances in `app/.ukdeploy/state.json`, so a later `list` or `stop` knows about
instances an earlier `deploy` started.

```text
ukdeploy.py build  [--arch ARCH]
ukdeploy.py deploy [--name NAME] [--port PORT] [--arch ARCH]
ukdeploy.py list   [--json]
ukdeploy.py stop   (NAME | --all)
ukdeploy.py bench  [--count N]
```

The command-line parsing, the helpers for saving state, `find_image()`, and the
QEMU settings are provided. You wrote `build` in Part I.

First write the five helper functions marked `TODO(student) Part II` near the
top of the file. The commands below use them, and each one's comment says what
to do:

- `reap_dead`: remove instances whose QEMU process has exited;
- `port_free`: is a port unused right now?
- `pick_port`: choose a free port for a new instance;
- `ping`: does the runner on a port answer `PING`?
- `wait_ready`: wait until a new instance answers `PING`.

Then write the three commands:

- **deploy**: pick a free port, start QEMU with that port forwarded, and wait
  until the instance is **ready**: QEMU starting does not mean the runner is
  listening yet, so send `PING` until it answers `PONG`. Then save the instance
  and print a line containing `port=<port>`, for example
  `deployed a  port=9000  pid=12345`. If it never becomes ready, stop QEMU, print
  where its log is, and do not save it.
- **list**: print one line per instance: its name, port, and whether it answers
  `PING` right now. First remove instances whose QEMU process has exited, so
  their ports can be used again. If there are none, print `no instances`.
- **stop**: stop one instance or all of them, releasing the port. `deploy`
  starts each instance in its own process group, so stop it by signalling the
  whole group: SIGTERM first, then SIGKILL if it is still running.

Ports come from `PORT_RANGE` (9000 to 9099). Two instances must never get the
same port, and a port must be usable again after its instance stops or dies. If
`deploy --port P` is given and port `P` is already in use, print an error and
exit with a non-zero status; do not pick a different port. Any other failure
(no image, a duplicate name) also prints an error and exits non-zero.

*Check it:* each comment shows what the command should print.

```bash
./deploy/ukdeploy.py stop --all                      # start clean
./deploy/ukdeploy.py deploy --name a --port 9000     # deployed a  port=9000  pid=...
./deploy/ukdeploy.py deploy --name b --port 9001     # deployed b  port=9001  pid=...
printf 'RUN sum 1 2 3\n' | nc 127.0.0.1 9000         # OK 6
./deploy/ukdeploy.py list                            # a and b
./deploy/ukdeploy.py stop --all
./deploy/ukdeploy.py list                            # no instances
```

`run_tests.sh` also checks the failure cases above. The Part III and IV checks
will fail until you finish those parts.

---

## Part III: Writing a Unikraft library

Now write some kernel code. `runner.c` has default versions of two functions,
marked `__attribute__((weak))`. That tells the linker to use them only if no
other file defines a function with the same name. So when your library defines
these two functions, the build uses yours instead:

```c
int  runner_platform_info(char *out, size_t n);   /* answers the INFO command */
void runner_on_invoke(const char *fn);            /* called after each RUN    */
```

You provide both, in a library of your own, plus something a normal program
cannot do at all: a function that runs **during boot**. In this part you write
three things:

1. `app/Config.uk`: a boolean build option that turns your library on;
2. `app/Makefile.uk`, TODO B: compile your library when that option is true;
3. `app/libukstats/ukstats.c`: the library itself.

**1. `app/Config.uk`**

Declare a boolean build option named `APPRUNNER_STATS`, true by default. The
file's TODO shows the exact block to write.

**2. `app/Makefile.uk`, TODO B**

Register your library and compile `ukstats.c` only when `APPRUNNER_STATS` is
true:

```make
$(eval $(call addlib,libukstats))
LIBUKSTATS_BASE := $(APPRUNNER_BASE)/libukstats
LIBUKSTATS_SRCS-$(CONFIG_APPRUNNER_STATS) += $(LIBUKSTATS_BASE)/ukstats.c
```

**3. `app/libukstats/ukstats.c`**

Write these three things. The comments in the file list the headers and types
to use.

1. **A boot hook**: a function that Unikraft runs during boot, before the
   runner's `main()`. In it, save the current time from
   `ukplat_monotonic_clock()` (nanoseconds since boot) and print this line with
   `printf`, which ends up in the console log:

   ```text
   1 unikernel booted in <N> ms
   ```

   where `<N>` is the saved time divided by 1,000,000. Register the function by
   writing `uk_late_initcall(your_function, 0);` **outside any function**, on
   its own line after your function.

2. **`runner_on_invoke()`**: add 1 to a counter. The runner calls it after each
   `RUN`.

3. **`runner_platform_info()`**: write this line into `out` with
   `snprintf(out, n, ...)` and return 0:

   ```text
   platform=unikraft freemem=<bytes> uptime_ms=<ms> boot_ms=<ms> invocations=<n>
   ```

   - `freemem`: `uk_alloc_availmem(uk_alloc_get_default())`, or `-1` if
     `uk_alloc_get_default()` returns `NULL`;
   - `uptime_ms`: `ukplat_monotonic_clock()` now, divided by 1,000,000;
   - `boot_ms`: the time your boot hook saved, divided by 1,000,000;
   - `invocations`: your counter.

Why this matters: your library is compiled into the same program as the kernel.
To learn how much memory is free, you do not ask an operating system; you **call
the kernel's memory allocator directly**. That is what a unikernel is.

*Check it:* rebuild, then deploy a new instance. Each comment shows what the
command should print.

```bash
./deploy/ukdeploy.py stop --all
./deploy/ukdeploy.py build
./deploy/ukdeploy.py deploy --name a --port 9000
printf 'INFO\n' | nc 127.0.0.1 9000           # OK platform=unikraft freemem=... invocations=0
printf 'RUN sum 1 2 3\n' | nc 127.0.0.1 9000  # OK 6
printf 'INFO\n' | nc 127.0.0.1 9000           # ... invocations=1
grep booted app/.ukdeploy/logs/a.log          # 1 unikernel booted in <N> ms
./deploy/ukdeploy.py stop --all
```

If `INFO` still says `platform=posix`, `ukstats.c` is not being compiled in:
check TODO B in `Makefile.uk` and the option in `Config.uk`.

---

## Part IV: Measuring cold start

Unikernels are useful because they start fast. Measure it.

You write two things in `deploy/ukdeploy.py`:

**1. `bench`**

`bench --count N` boots a fresh instance, times how long until it answers
`PING`, stops it, and repeats `N` times. Then it prints these lines
(times in seconds):

```text
image      <path to the image>
image_size <size in bytes>
runs       <N>
ready_p50  <median time to ready>
ready_max  <longest time to ready>
```

`ready_p50` is the median ("p50" means the 50th percentile).

**2. `list --json`**

Add a `--json` case to `cmd_list`: instead of the table, print the instances as
a JSON list, one object per instance, with at least `name` and `port` (a
number):

```json
[{"name": "a", "port": 9000, "pid": 12345}]
```

*Check it:* `./deploy/ukdeploy.py bench --count 3` prints the five lines above.
Then run `cd tests && ./run_tests.sh`: every check should now pass.

Finally, compare two numbers: `boot_ms`, which your boot hook reports from inside
the unikernel, and `ready_p50`, which `bench` measures from outside. They differ
a lot. Think about where the extra time goes.

---

## Testing

Run `cd tests && ./run_tests.sh`. It runs every check the grader runs (there are
no hidden tests) and prints PASS or FAIL for each.
