# Project 1: A Virtual Machine Monitor, a Device, and Real Virtio

A **virtual machine monitor** (VMM) is the host program that gives a guest a CPU,
memory, and devices and runs it. You build a small one, add a logging device to
it, and then rebuild that device as **virtio**, the device standard that real
guests already have drivers for.

Three parts, one file each:

- **Part I — `emulator/vmm.c`:** the machine. Allocate guest RAM, load the
  guest's code, run the CPU, handle its serial/control writes (console output and
  poweroff), and stay up when the guest touches memory that isn't mapped.
- **Part II — `emulator/device.c`:** the logging device, as MMIO registers you
  design. The guest points the device at a message in its own memory and writes a
  command; the device reads that message and appends it to a log.
- **Part III — `vhost/virtio.c`:** the same device as virtio, running under a
  real QEMU VM whose stock Linux driver talks to it with no guest changes. You
  implement the queue (the *virtqueue*) that carries messages from guest to
  device.

One job recurs: translating a guest address into a host pointer you can safely
read. You write it in Part I (`vmm_gpa_to_host`) and again in Part III
(`virtq_gpa_to_hva`).

- [SPEC.md](SPEC.md): the exact contract. Read it first.
- [SETUP.md](SETUP.md): how to build, run, and test.

You write those three files. Everything else is provided: the guest programs, the
build, the Unicorn CPU setup, the log store, the vhost-user protocol code, and
the script that boots the QEMU VM.

## What you implement

Three files, ~10 functions. Each row is one function; the SPEC column says
what it must do.

### Part I: `emulator/vmm.c` (the machine)

| Function | What it does | SPEC |
|---|---|---|
| `serial_write()` | The serial/control protocol: a byte write goes to stdout; the poweroff register halts the VM and carries the exit code. | Part I, *Serial / control protocol* |
| `mem_invalid()` | The unmapped-memory hook. A guest that touches unmapped memory must be caught, not take the VMM down: record it, report on stderr, stop the CPU. | Part I, *Guest faults* |
| `vmm_create()` | *(partly provided)* Allocate and map guest RAM, register the serial and device MMIO regions, set the initial `RSP`, and register `mem_invalid`. | Part I, *Functions you implement* |
| `vmm_load_binary()` | Load the flat guest binary at `RAM_BASE` and set `RIP`. | Part I |
| `vmm_run()` | Run the CPU until the guest powers off; return the exit code, or `VMM_EXIT_FAULT` if it faulted. | Part I |
| `vmm_gpa_to_host()` | Translate a guest-physical range to a host pointer, or `NULL` if it isn't entirely inside guest RAM. You write it again in Part III. | Part I |

### Part II: `emulator/device.c` (your own MMIO protocol)

| Function | What it does | SPEC |
|---|---|---|
| `vlog_device_mmio_read()` | Return the value of the register at `offset` (`ID`, `VERSION`, `STATUS`, `MSG_*`, `LEN`, `LEVEL`, `SEQ`). | Part II §2 |
| `vlog_device_mmio_write()` | Store the operand registers, and on a `CMD` write execute the command: `NOP`, `LOG`, `FLUSH`, `STAT`. | Part II §3, §4 |

`LOG` reads a message out of guest memory and appends it. `STAT` is the one that
writes back into guest memory, so it must check the guest offered enough room at a
valid address before touching it. `NOP` and `FLUSH` are one-liners.

### Part III: `vhost/virtio.c` (the same device, as real virtio)

| Function | What it does | SPEC |
|---|---|---|
| `virtq_gpa_to_hva()` | `vmm_gpa_to_host()` again, but against a real hypervisor's memory map: several regions, with gaps between them. Reject anything straddling, in a gap, or overflowing. | Part III §3 |
| `vlog_virtq_handle()` | The split virtqueue: read the avail ring, walk each descriptor chain (including indirect tables), emit the record, and complete the chain on the used ring. | Part III §4 |

## Where to look for help

You are not expected to know Unicorn or vhost-user going in.

**Provided helpers:**

| | |
|---|---|
| `emulator/device.c` | `msg_addr()`, `set_error()`, `clear_error()`, already written in your stub. |
| `emulator/logstore.h` | `logstore_append(store, seq, level, bytes, len)` and `logstore_flush()`. This owns the record format; you never format a record yourself. |
| `vhost/virtq.h` | `struct virtq` (the three rings), `struct virtq_mem` (the guest's memory map), `virtq_rmb()` / `virtq_wmb()`, `VIRTQ_MAX_RECORD`, and `vlog_sink_emit()`, where one call is one record. |

**Headers that are the contract:**

| | |
|---|---|
| `emulator/vmm.h` | The memory map (`RAM_BASE`, `SERIAL_BASE`, `DEV_BASE`), `VMM_EXIT_FAULT`, and `struct vmm`. |
| `emulator/device.h` | Register offsets, command codes, `STATUS` bits, error codes, `struct vlog_device`, `struct vlog_stats`. |
| `vhost/standard-headers/linux/virtio_ring.h` | The real virtio structures: `vring_desc` / `vring_avail` / `vring_used`, and the `VRING_DESC_F_*` flags. |
| `guest/vlog.h` | The guest side of your device. Useful for seeing how a guest actually drives the registers. |

In `vmm.c` you call five Unicorn functions: `uc_mem_map_ptr` (host-backed RAM),
`uc_mmio_map` (an MMIO region + callbacks), `uc_reg_write` (RIP/RSP), `uc_hook_add`
(the fault hook), and `uc_emu_start` / `uc_emu_stop`. `device.c` doesn't touch
Unicorn; `vhost/virtio.c` doesn't either, since it runs against real QEMU.

## How to work

```bash
cd tests && ./run_tests.sh        # all three parts; the checks the grader runs
cd tests && ./run_virtq_tests.sh  # just Part III, on its own (fast)
cd vhost  && ./run-qemu.sh        # Part III driving a REAL QEMU VM (optional)
```

Start with the VMM micro-tests (`m_boot`, `m_stack`, `m_ram`, `m_exit`,
`m_fault`). They come in dependency order, so the first one that fails points at
the earliest broken piece. See [SETUP.md](SETUP.md).

## What to turn in

The three files you implement:

- `emulator/vmm.c`
- `emulator/device.c`
- `vhost/virtio.c`

Nothing else — every other file is provided and is not read from your
submission. Run `cd tests && ./run_tests.sh` before you submit: the grader runs
these same checks, though with more random inputs per scenario, so re-run it a
few times rather than trusting one clean pass.
