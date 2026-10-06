# Project 3: A Unikernel

You take `runner.c`, a small provided server program, and turn it into a
**unikernel**: one bootable image in which your program and the kernel are
compiled together, with nothing else running. Then you write the tool that builds
it, deploys instances of it, and measures how fast they start.

- **[SPEC.md](SPEC.md)**: what to implement, in the order to build it. Read it
  first.
- **[SETUP.md](SETUP.md)**: setting up your VM, the file layout, and troubleshooting.

---

## What you implement

| File | What it is |
|---|---|
| `app/Kraftfile` | What the image is made of: the Unikraft kernel and the libraries it needs. |
| `app/Makefile.uk` | Which source files are compiled into the image: `runner.c` and your own library. |
| `app/Config.uk` | A boolean build option that turns your library on. |
| `app/libukstats/ukstats.c` | **Kernel code**: a function that runs during boot, an invocation counter, and the answer to the runner's `INFO` command. |
| `deploy/ukdeploy.py` | The deployment tool: `build`, `deploy`, `list [--json]`, `stop`, `bench`. |

---

## Where to look for help

**Provided (use them, do not edit them):**

| | |
|---|---|
| `runner/runner.c` | The server program you build into the image. It answers `PING`, `FUNCS`, `INFO` and `RUN`. |
| `deploy/ukdeploy.py` | The command-line parsing, state helpers, `find_image()` and QEMU settings are already written. You write the commands. |
| `tests/run_tests.sh` | The test suite: every check the grader runs. |

**Worth reading before you start:**

| | |
|---|---|
| Unikraft's headers, under `app/.unikraft/unikraft/` after your first build | the functions `ukstats.c` calls: `include/uk/init.h`, `lib/ukalloc/include/uk/alloc.h`, `include/uk/plat/time.h` |
| Python's `subprocess` and `socket` documentation | starting QEMU and talking to the runner from `ukdeploy.py` |

---

## How to work

Work through the Parts in [SPEC.md](SPEC.md) in order. Each Part ends with
commands to check it. If something goes wrong, see the troubleshooting list in
[SETUP.md](SETUP.md).
