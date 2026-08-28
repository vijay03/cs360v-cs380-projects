# Project 0: Specification

Project 0 is an individual project; do it on your own. It exists to get your
development environment working before Project 1.

Each of the four programs has one `TODO`: retrieve a single value from that
runtime and print it, one line, nothing else.

| Part | Runtime | The `TODO` | Prints | Where |
|---|---|---|---|---|
| 1 | a Unicorn-emulated CPU | read `EAX` after the emulated code runs | the value the CPU computed | the VM |
| 2 | a process in Linux namespaces | print `getpid()` | its process id | the VM |
| 3 | a Unikraft unikernel under QEMU | print `__DATE__` and `__TIME__` | the build date and time | the VM |
| 4 | a Docker container | fill `host` via `gethostname()` | the container id | your own machine |

A part that does not build or run points to an environment problem; see
[SETUP.md](SETUP.md) and the troubleshooting section of [README.md](README.md).

## Part 1: a Unicorn-emulated CPU

`part1_unicorn/hello.c` opens a virtual CPU with the Unicorn engine and runs a
short piece of x86-64 code on it that adds two numbers.

Your `TODO`: read the result out of `EAX` into `eax` with
`uc_reg_read(uc, UC_X86_REG_EAX, &eax)`. The program then prints `eax`.

```bash
cd part1_unicorn && make && ./hello
```

If `make` cannot find `unicorn`, `libunicorn-dev` is not installed.

## Part 2: a process in a container

`run.sh` builds `part2_container/hello.c` statically and runs it in new user,
pid, and mount namespaces (via `unshare`) — the isolation Project 2's runtime is
built on.

Your `TODO`: set `pid` to `getpid()`. The program prints it.

```bash
cd part2_container && ./run.sh
```

Inside the pid namespace this process is near the start of the numbering — build
it and run `./hello` on its own and you would see a much larger pid. If it fails
with "Operation not permitted", unprivileged user namespaces are disabled; see
[README.md](README.md).

## Part 3: a unikernel

`part3_unikernel/main.c` is the whole application of a Unikraft image. `run.sh`
builds it with `kraft` and boots it under QEMU. Under nolibc there is no OS
clock, so the natural "when" is the build time — and it changes each rebuild.

Your `TODO`: `printf("%s %s\n", __DATE__, __TIME__)`.

```bash
cd part3_unikernel && ./run.sh
# ... kraft build, then a boot on the QEMU console, then your line ...
```

The first build fetches and compiles the Unikraft core; with the cache warmed by
`setup/setup-vm.sh` it takes about 20-30 seconds. If `kraft` is not found, the
Project 3 toolchain is not installed.

## Part 4: a Docker container

`part4_docker/hello.c` runs inside the container (`run.sh` handles that; see
SETUP.md "Part 4"). Docker sets the container's hostname to its short container
id, a fresh one each `docker compose up`.

Your `TODO`: fill `host` with `gethostname(host, sizeof host)`. The program prints
it, or serves it once over `--serve PORT` (which `run.sh` uses for the
published-port check).

```bash
cd part4_docker && ./run.sh
```

The image is built for your architecture (arm64 or amd64) with no configuration
on your part.

## Checking your work

```bash
./tests/run_tests.sh          # Parts 1-3, in the VM
cd part4_docker && ./run.sh   # Part 4, on your own machine (container up)
```

Each runs its parts and reports pass/fail per part. Grading details are announced
separately.

## What to turn in

Run `./tests/run_tests.sh` in the VM and `part4_docker/run.sh` on your machine;
each writes a results file. Then, on your machine, `python3 make-submission.py`
pulls the VM's file over SSH and merges both into `submission.json` (adding an
`output_hash` over the four outputs). Submit `submission.json`. See
[README.md](README.md) for the step-by-step.
