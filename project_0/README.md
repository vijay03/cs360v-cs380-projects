# Project 0: Environment Setup

Project 0 is an individual project. It gets your development environment working
and walks you through the build-and-run cycle used in the later projects. Each of
its four small programs has one `TODO` where you retrieve a single value from
that runtime and print it.

Parts 1 to 3 run inside an Ubuntu 24.04 virtual machine under QEMU. Part 4 runs in
Docker directly on your own machine. [SETUP.md](SETUP.md) sets up both; do that
first. [SPEC.md](SPEC.md) is the full description of each part and how to check
it.

## The four parts

| Part | The `TODO` | Prints | Build and run | Where |
|---|---|---|---|---|
| `part1_unicorn/` (`hello.c`) | read EAX from the emulated CPU (`uc_reg_read`) | the value the CPU computed | `make && ./hello` | VM |
| `part2_container/` (`hello.c`) | print `getpid()` | its process id | `./run.sh` | VM |
| `part3_unikernel/` (`main.c`) | print `__DATE__` and `__TIME__` | the build date and time | `./run.sh` | VM |
| `part4_docker/` (`hello.c`) | fill `host` with `gethostname()` | the container id | `./run.sh` (container up) | your machine |

If a part fails to build or run, you have an environment problem to fix now; see
[Troubleshooting](#troubleshooting).

## Editing the files

**Parts 1-3** live inside the VM. In an SSH session, edit with a terminal editor:
`nano part1_unicorn/hello.c` (`Ctrl-O` saves, `Ctrl-X` exits). For a GUI editor,
connect VS Code to the VM with the Remote-SSH extension (`localhost:2222`).

**Part 4** lives on your own machine: `part4_docker/hello.c` is an ordinary file
in your cloned copy of the handout. Edit it with whatever editor you normally use, the container is mounted onto that directory and sees the change right away.

## Layout

```text
.
├── README.md               this file
├── SETUP.md                how to set up the VM and Docker
├── SPEC.md                 what each part must do and how to check it
├── make-submission.py      builds submission.json from both environments (host)
├── setup/
│   ├── setup-vm.sh         installs the VM toolchain (run once in the VM, with sudo)
│   ├── Dockerfile          Part 4's container image
│   └── compose.yaml        Part 4's dev container (run `docker compose` from here)
├── part1_unicorn/
│   ├── hello.c             TODO: read EAX from the emulated CPU
│   └── Makefile
├── part2_container/
│   ├── hello.c             TODO: print getpid()
│   └── run.sh              builds it static, runs it in new namespaces
├── part3_unikernel/
│   ├── main.c              TODO: print __DATE__ and __TIME__
│   ├── Kraftfile           unikernel configuration
│   ├── Makefile.uk         build glue
│   └── run.sh              kraft build + QEMU boot
├── part4_docker/
│   ├── hello.c             TODO: fill host with gethostname()
│   └── run.sh              tests the running container
└── tests/
    └── run_tests.sh        builds and runs Parts 1-3, checks the output
```

## Building and running

Parts 1 to 3, from the VM, each in its own directory:

```bash
cd part1_unicorn && make && ./hello
cd part2_container && ./run.sh
cd part3_unikernel && ./run.sh
```

Part 4, on your own machine, with the container brought up per SETUP.md:

```bash
cd part4_docker && ./run.sh
```

Each prints one line: Part 1 the value the emulated CPU computed, Part 2 its
process id, Part 3 the build date and time, Part 4 the container id.

## Testing

```bash
./tests/run_tests.sh
```

builds and runs Parts 1-3 in the VM and reports pass/fail for each. Part 4 needs
Docker, so check it separately with `part4_docker/run.sh` on your own machine.

## Troubleshooting

If you set up your environment with `setup/setup-vm.sh` (Parts 1-3) or Docker (Part 4) and
a part still fails:

- **Part 1: `make` cannot find `unicorn` or `unicorn/unicorn.h`.** `libunicorn-dev`
  is not installed, or is older than 2.0. Install it with
  `sudo apt-get install libunicorn-dev`.
- **Part 2: `unshare` fails with "Operation not permitted" or `write failed
  /proc/self/uid_map`.** Unprivileged user namespaces are restricted. `setup/setup-vm.sh`
  turns this off (via `kernel.apparmor_restrict_unprivileged_userns=0`); if you
  still hit it, re-run `setup/setup-vm.sh`, or set it directly with
  `sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0`.
- **Part 2: "hello is not static".** The static libc is missing. Install it with
  `sudo apt-get install libc6-dev`.
- **Part 3: `kraft: command not found`.** kraftkit is not installed; run
  `setup/setup-vm.sh`. If `kraft build` instead spends several minutes "updating index",
  the cache was not warmed; let the first build finish and later builds will be
  fast.
- **Part 3: QEMU errors, or nothing prints on the console.** `qemu-system-x86_64`
  is missing or older than 7.2 (Ubuntu 24.04 ships 8.2). If the build succeeds
  but you see no output, run the provided `run.sh` rather than calling QEMU
  yourself; `run.sh` redirects stdin so the guest's output reaches the console.
- **Part 4: `run.sh` says the `dev` container is not running.** Bring it up first
  (SETUP.md, "Part 4"): `docker compose up -d --build` from `setup/`.
- **Part 4: nothing comes back on the published port.** Something else on your
  machine is using port 8004, or a firewall is blocking it.

## What to turn in

Run the check in both environments, then build the submission on your machine:

1. **In the VM:** `./tests/run_tests.sh` — writes `~/cs360v-p0-vm.json`.
2. **On your machine, container up:** `cd part4_docker && ./run.sh` — writes
   `cs360v-p0-docker.json`.
3. **On your machine, from `project_0/release/`:** `python3 make-submission.py`.
   It pulls the VM's file over SSH (keep the VM running), merges the two, and
   writes `submission.json`. If SSH to the VM isn't working it prints how to copy
   the file across by hand.

Submit `submission.json`.
