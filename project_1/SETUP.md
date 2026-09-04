# Project 1: Build, run, and test

You implement `emulator/vmm.c` (the machine), `emulator/device.c` (the logging
device, as MMIO registers), and `vhost/virtio.c` (the same device as virtio);
everything else is provided. [README.md](README.md) says what each part is;
[SPEC.md](SPEC.md) is the contract. This file is build, run, and test.

## Setup

You write three files — `emulator/vmm.c`, `emulator/device.c`, and
`vhost/virtio.c` — and submit those three. Everything else here is provided.

You do all of Project 1 inside the Ubuntu VM from Project 0, over SSH. The
emulator, the test suites, and the Part III QEMU boot all run there, not on your
own machine.

If you haven't set that VM up, do it now: follow the QEMU-VM part of
[Project 0's SETUP.md](../project_0/SETUP.md), then run
`project_0/setup/setup-vm.sh` once inside the VM to install the compiler,
Unicorn, and QEMU. Clone this repo in the VM if it isn't there already, and work
from `<repo>/project_1`.

## Layout

```
.
├── SPEC.md            # spec: Part I vmm.c, Part II device.c, Part III virtio.c. READ FIRST
├── README.md          # overview
├── SETUP.md           # this file: build / run / test
├── emulator/          # the emulator (built on the Unicorn CPU emulator)
│   ├── vmm.c          #   YOU IMPLEMENT: the VMM core
│   ├── device.c       #   YOU IMPLEMENT: the MMIO logging device
│   ├── vmm.h          #   machine ABI + VMM interface (provided)
│   ├── device.h       #   device register ABI + device state (provided)
│   ├── logstore.h/.c  #   thread-safe log store (provided); every path logs here
│   ├── netlog.h/.c    #   network log ingest (PROVIDED, not a deliverable)
│   ├── main.c         #   entry: ./emulator [--trace] [--log <p>] [--listen <port>] <guest.bin>
│   └── Makefile
├── vhost/             # Part III: the virtio (vhost-user) backend for a real QEMU VM
│   ├── virtio.c       #   YOU IMPLEMENT: the gpa translator + virtqueue handler
│   ├── virtq.h        #   ring structures + guest memory map + log sink (provided)
│   ├── backend.c      #   all vhost-user protocol plumbing (provided)
│   ├── sink.c         #   record sink (provided)
│   ├── run-qemu.sh    #   boots a real QEMU VM against your backend (provided)
│   ├── guest/         #   the VM's guest: a tiny init that logs (provided)
│   └── libvhost-user.*, standard-headers/   # vendored from QEMU (provided)
├── guest/             # the Unicorn guest programs + the MMIO client library
│   ├── vlog.c / vlog.h #  library the guest uses to talk to the device
│   ├── test_nop.c      #  sample: discovery + NOP
│   ├── test_log.c      #  sample: logging at several severity levels
│   ├── start.S, link.ld, Makefile
└── tests/             # test suites you can run while developing
    ├── run_tests.sh        #   the whole suite (Parts I, II, III)
    ├── run_virtq_tests.sh  #   just Part III: synthetic virtqueues (no QEMU)
    ├── t_*.c, m_*.S        #   one guest per behavior
    ├── virtq_test.c        #   the virtqueue harness
    ├── expected/           #   golden .obs / .log files
    ├── tobs.h, Makefile
└── runner/             # the course function runner (provided): a real client you
    └── runner.c        #   can point at your --listen collector (see "Beyond the assignment")
```

## Parts I & II: the emulator

### Building

```bash
cd emulator && make        # -> ./emulator
cd ../guest  && make        # -> test_nop.bin, test_log.bin
```

`make clean` in either directory removes build artifacts.

### Running

The emulator loads a flat guest binary, runs it, and exits with the code the
guest returned. The device appends records to the file named by `--log`
(default `vlog.log`):

```bash
cd guest
../emulator/emulator --log out.log test_log.bin
echo $?          # guest exit code
cat out.log      # the records the device persisted
```

Pass `--trace` to print every executed instruction's address to stderr. It is
handy when a guest hangs or faults:

```bash
../emulator/emulator --trace test_nop.bin 2>trace.log
```

### How it fits together

1. The emulator maps guest RAM at `0x0010_0000`, a serial/control region at
   `0x1000_0000` (byte writes go to stdout; a poweroff register halts the VM),
   and your logging device at `0x2000_0000`.
2. The guest, compiled freestanding (no libc), boots at `RAM_BASE`, submits log
   messages to the device over MMIO via `vlog.h`, prints diagnostics over the
   serial region, and powers off, returning control (and the exit code) to the
   emulator.
3. Paging is disabled, so a guest pointer is also a guest-physical address; your
   device translates those addresses to host pointers with the provided
   `vmm_gpa_to_host()` helper to read the message buffer.
4. Each message becomes one record in the host log, which persists after the
   guest exits. The `STAT` command runs the other way: the device writes its
   stats back into the guest's buffer, so it must check that the guest really
   offered it enough room, at an address that is really guest RAM.

### Testing

```bash
cd tests && ./run_tests.sh     # builds the emulator + guests, runs the suite
```

This is the whole suite: Parts I and II here, plus the Part III virtqueue checks
below. The grader runs these same checks, so use it as your final check before
submitting.

Verdicts are host-side: each test guest exercises the machine and prints the raw
values it observed (`key=0x........` lines); the runner compares those, and the
produced log file, against golden files in `tests/expected/`. It prints a unified
diff for any failing test and exits 0 iff all pass. Until you implement `vmm.c`
and `device.c` the tests fail (a guest can't even boot); a correct implementation
passes all of them.

The suite starts with five VMM micro-tests in dependency order. Use them to bring
`vmm.c` up one layer at a time:

- `m_boot`: stackless. RAM/RIP/run-loop/serial/poweroff, with no stack and no
  device;
- `m_stack`: a C guest runs (your RSP is correct);
- `m_ram`: guest RAM is writable / host-backed;
- `m_exit`: POWEROFF carries the exit code;
- `m_fault`: a guest that touches unmapped memory is caught rather than crashing
  the VMM. The VMM reports the fault and exits with `VMM_EXIT_FAULT`.

The first micro-test that fails points at the earliest broken piece. Fix that
before looking at the device (`t_*`) tests.

## Part III: real virtio on a real QEMU VM

Here you implement the virtio split virtqueue for the same log device. Your code
runs as a vhost-user backend: a host process QEMU connects to over a UNIX socket.
A real Linux guest's stock `virtio_console` driver binds to it with no guest-side
code, and everything the guest writes to `/dev/hvc0` arrives in your queue.

### Testing

`run_tests.sh` already covers Part III. While you are working on it, this runs
those checks on their own, without rebuilding the emulator:

```bash
cd tests && ./run_virtq_tests.sh
```

It builds a fake guest with two memory regions and a gap between them, and runs
four staged checks, so a partial implementation still passes the early ones:

- `translate`: your `virtq_gpa_to_hva()` alone (valid ranges, straddling, the
  gap, overflow);
- `single`: the minimal queue (one descriptor per chain, emitted, used ring);
- `chained`: multi-descriptor chains + skipping writable / unmapped descriptors;
- `indirect`: indirect descriptor tables.

Each later stage assumes the earlier ones work, so bring them up in order. It uses
no QEMU and no timing, so it is fast and deterministic. The grader scores the same
staged set, plus checks that your handler survives malformed rings.

### Then drive a real VM (optional)

```bash
cd vhost && ./run-qemu.sh
```

Not graded, and not needed to submit: this is where you watch a real Linux guest
drive the device you wrote. It builds your backend, boots an actual QEMU virtual
machine, and prints two things: the log your virtqueue handler produced from the
guest's writes, and the reference it should match. A pass is the two blocks being
identical:

```
[0] INFO function runner started
[1] DEBUG processing request 1
[2] WARN cache miss
[3] ERROR request 1 failed
```

The banner reports `machine: ... emulated (TCG)` when there is no hardware
acceleration (the usual case inside the VM); that is fine, just slower than
native.

If it does not work:

- **`No readable kernel at /boot/vmlinuz-...`** — Ubuntu ships the kernel image
  readable only by root. Fix it with `sudo chmod +r /boot/vmlinuz-*-generic`, or
  re-run `project_0/setup/setup-vm.sh`.
- **The VM hangs and you get only the first record (or none)** — the usual cause
  is the used ring: the driver is waiting for you to hand its buffer back. See
  [SPEC.md](SPEC.md) Part III §2. `vhost/qemu.out` has the guest's console
  output.

> There are no hidden tests: the grader runs these same checks. But the Part II
> checks draw fresh random inputs every run, and the grader draws more of them
> than you do by default, so a single clean run is not a guarantee. Implement to
> the SPEC, and re-run (or raise `P1_SEEDS`) for confidence.

---

## Beyond the assignment

None of the following is needed to do or submit Project 1.

### Network ingest (`--listen`), provided

With `--listen <port>` the emulator also runs a provided TCP listener that
appends records received over the network to the same log store, and with
`--listen` and no guest it runs as a standalone collector. It is there because
later projects ship their logs to this same store.

```bash
../emulator/emulator --listen 9099 --log out.log &      # collector (no guest)
printf '1 hello world\n3 request failed\n' | nc localhost 9099
kill %1; cat out.log      # -> [0] INFO hello world / [1] ERROR request failed
```

Those `nc` lines fake a producer. The real one is the course's function runner,
in `runner/runner.c` (the program later projects package as a VM guest, a
container, and a unikernel). It writes records in this same wire format, so you
can point it at your collector and watch work arrive over the network:

```bash
gcc -O2 -o /tmp/runner runner/runner.c
../emulator/emulator --listen 9099 --log out.log &       # your collector
/tmp/runner 9001 --log-to 127.0.0.1:9099 &               # the runner, shipping logs here
printf 'RUN wordcount a b c\n' | nc 127.0.0.1 9001       # OK 3
cat out.log      # -> ... INFO invoke wordcount / INFO wordcount -> 3
```

This is the same collector your Project 4 platform's backends will feed; the whole
loop is described in Project 4's `THROUGHLINE.md`. `--log-to` is a feature of the
provided runner (you do not implement it).

### Booting your own guest by hand

`run-qemu.sh` boots a fixed guest that writes four records and powers off. If you
want to poke at your device by hand, boot your own guest with an interactive
shell instead. Your backend is a stock virtio console, so anything in the guest
that writes to `/dev/hvc0` goes through your virtqueue handler.

Build a small initramfs around the VM's static busybox:

```bash
cd vhost
make                                   # build your backend (vlog-backend)

G=/tmp/myguest; rm -rf $G; mkdir -p $G/root/bin $G/root/dev
cp "$(command -v busybox)" $G/root/bin/busybox
# symlink every applet: without one for `mount`, /dev stays empty and there is
# no /dev/hvc0 to write to
(cd $G/root/bin && for a in $(./busybox --list); do ln -sf busybox "$a"; done)

cat > $G/root/init <<'EOF'
#!/bin/sh
mount -t devtmpfs devtmpfs /dev
exec /bin/sh                           # an interactive shell as PID 1
EOF
chmod +x $G/root/init
(cd $G/root && find . -print0 | cpio --null -o -H newc --quiet | gzip > ../initramfs.gz)
```

Start your backend, then boot QEMU with the same device `run-qemu.sh` uses:

```bash
./vlog-backend /tmp/vlog.sock /tmp/virtio.log &
qemu-system-x86_64 -machine q35,memory-backend=mem -m 512M -enable-kvm \
  -object memory-backend-memfd,id=mem,size=512M,share=on \
  -kernel "$(ls -1 /boot/vmlinuz-*-generic | head -1)" \
  -initrd /tmp/myguest/initramfs.gz \
  -append "console=ttyS0 rdinit=/init" \
  -chardev socket,id=vhost0,path=/tmp/vlog.sock \
  -device vhost-user-device-pci,virtio-id=3,num_vqs=2,chardev=vhost0 \
  -nographic -no-reboot
```

At the guest shell, write records and watch them arrive on the host:

```sh
# in the guest
echo "1 hello from the guest" > /dev/hvc0
echo "3 something failed"     > /dev/hvc0
```

```bash
# on the host, in another terminal
cat /tmp/virtio.log
```

Leave QEMU with Ctrl-A X. Drop `-enable-kvm` if `/dev/kvm` is not available (QEMU
then emulates, which is slower but works; inside the Project 0 VM there is usually
no nested KVM, so expect to drop it). On an arm64 VM use `qemu-system-aarch64
-machine virt -cpu max` and `console=ttyAMA0`; everything else is the same (the
arch table at the top of `run-qemu.sh` has the details).

Two shortcuts:

- `run-qemu.sh` honors `KERNEL=`, `VIRTIO_SRC=`, `LOG=` and `TIMEOUT=`, so if you
  only want to swap the kernel or point at a different `virtio.c` you do not need
  to write the command line out yourself.
- Nothing here is busybox-specific. Any guest works: boot a full distro image with
  the same `-chardev` / `-device` pair and it will expose your device as
  `/dev/hvc0` just the same.

### Writing your own test guest

Drop a `test_<name>.c` with a `main()` into `guest/`, add the stem `test_<name>`
to the `TESTS` list in `guest/Makefile`, and `make`. It links against `start.S`
and `vlog.c` automatically.

### Adding a test to the suite

1. Create `tests/t_<name>.c`: include `tobs.h`, exercise the device, and report
   raw values with `obs("key", value)` (plus the `dev_err()` / `dev_errcode()`
   helpers). Emit only `obs()` lines so the transcript is comparable.
2. Add `t_<name>` to the `TESTS` list in both `tests/Makefile` and
   `tests/run_tests.sh`.
3. Record the expected `tests/expected/t_<name>.obs` (and `.log` if it logs)
   from the SPEC, or capture it from a device run you have verified correct.
