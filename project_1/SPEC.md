# Project 1: Specification

This is the contract for the three files you implement. The
register and command constants live in `emulator/vmm.h` and `emulator/device.h`;
do not change those headers.

- **Part I — `emulator/vmm.c`:** the machine. Allocate and map guest RAM, load
  the guest binary, run the CPU, service the guest's serial/control writes, and
  catch unmapped-memory accesses instead of letting them crash the VMM.
- **Part II — `emulator/device.c`:** the logging device, as an MMIO register
  interface you design. The guest gives the device a message address and a
  command; the device validates the request, reads the message from guest
  memory, and appends it to the log.
- **Part III — `vhost/virtio.c`:** the same device as virtio. A real Linux guest
  under QEMU drives it with its stock driver, unmodified. You implement the
  address translation and the split virtqueue.

All three parts feed one log, through the same provided thread-safe log store
(`logstore.h`), which owns the record format. A provided network ingest path
(`emulator/netlog.c`) appends to it too; it is not a deliverable, and it is there
because later projects ship their logs to this same store.

---

## Part I: The machine (VMM)

*You implement this in `emulator/vmm.c`.*

### The machine

The VMM drives a single x86-64 CPU emulated by Unicorn (`UC_ARCH_X86`,
`UC_MODE_64`). **Paging is disabled**, so a guest virtual address equals its
guest-physical address (identity). Guest RAM is **host-backed**: you allocate a
host buffer and map it into the guest, so the device can translate guest
addresses to real host pointers.

### Guest-physical memory map

| Region    | Base (`vmm.h`)                                     | Size                | Purpose                              |
|-----------|----------------------------------------------------|---------------------|--------------------------------------|
| RAM       | `RAM_BASE` `0x0010_0000`                           | `RAM_SIZE` 16 MiB   | code + data + stack; host-backed     |
| Serial    | `SERIAL_BASE` `0x1000_0000`                        | `SERIAL_SIZE` 0x1000| console + poweroff (MMIO)            |
| Device    | `DEV_BASE` `0x2000_0000`                           | `DEV_SIZE` 0x1000   | the logging device (Part II)         |
| Boot-info | `BOOTINFO_BASE` (top `BOOTINFO_SIZE`=64 KiB of RAM)| 64 KiB              | parameter blob (provided)            |

The boot-info region is the top 64 KiB of RAM (`BOOTINFO_BASE = RAM_BASE +
RAM_SIZE - BOOTINFO_SIZE`); the stack lives just below it.

### Guest entry state

When the guest starts executing you must have set:

- **RIP = `RAM_BASE`**: the flat binary is loaded at `RAM_BASE` and its first
  byte is the entry point.
- **RSP = `BOOTINFO_BASE - 16`**: top of usable RAM (just below the reserved
  boot-info region), 16-byte aligned. The guest needs a stack to run.
- **RDI = `BOOTINFO_BASE`**: pointer to the boot-info blob (the guest `main()`'s
  first argument). *(Provided; leave the RDI line as-is.)*

### Serial / control protocol

A control region mapped at `SERIAL_BASE` (size `SERIAL_SIZE`). Its two registers
are named by their **offset from `SERIAL_BASE`**, so the guest-physical address of
each is `SERIAL_BASE + offset` — and `offset` is exactly the argument your
`serial_write` callback receives. On a write:

- offset `SERIAL_TX` (`0x00`, i.e. guest address `SERIAL_BASE + 0x00`): emit the
  low byte of the written value to the host's **stdout** (this is how the guest
  prints).
- offset `SERIAL_POWEROFF` (`0x08`, i.e. guest address `SERIAL_BASE + 0x08`):
  stop the machine, recording the written value as the **exit code**.

Any other offset is ignored, and all reads return 0.

### Guest faults

A guest is untrusted: it can read, write, or jump to an address that is not
mapped (no RAM, no MMIO region there), by accident or on purpose. That must not
take the VMM down. Register a Unicorn hook for unmapped accesses
(`UC_HOOK_MEM_UNMAPPED`); when it fires:

- record the fault in the VMM (`v->faulted`, `v->fault_addr`);
- report it on stderr (stdout is the guest's console, so keep it clean);
- stop the CPU (`uc_emu_stop`) and return `false`, meaning "do not retry";
- `vmm_run` then returns `VMM_EXIT_FAULT` (77, in `vmm.h`) as the exit code.

The `m_fault` micro-test checks this.

### Functions you implement (`vmm.c`)

Prototypes are in `vmm.h`; each returns 0 on success / non-zero on error unless
noted. Machine state lives in `struct vmm` (`vmm.h`). The fields **you** set are:

| field | you set it in | meaning |
|-------|---------------|---------|
| `v->ram` | `vmm_create` | the host buffer backing guest RAM (`RAM_SIZE` bytes) |
| `v->powered_off`, `v->exit_code` | `serial_write` | set on a POWEROFF write |
| `v->faulted`, `v->fault_addr` | `mem_invalid` | set when the guest touches unmapped memory |

`v->uc` (the Unicorn CPU handle you pass to every `uc_*` call), `v->store` (the
log store), and `v->dev` (the device) are already set up for you.

- **`vmm_create`** — three TODOs, in order:
  1. allocate `RAM_SIZE` zeroed bytes into `v->ram` and map them host-backed with
     `uc_mem_map_ptr(v->uc, RAM_BASE, RAM_SIZE, UC_PROT_ALL, v->ram)`;
  2. register the two MMIO regions with `uc_mmio_map(v->uc, base, size, read_cb,
     write_cb, user_data)` — the **serial** region at `SERIAL_BASE`/`SERIAL_SIZE`
     (callbacks `serial_read`/`serial_write`, user-data `v`) and the **device**
     region at `DEV_BASE`/`DEV_SIZE` (callbacks `vlog_device_mmio_read`/
     `vlog_device_mmio_write`, user-data `v->dev`);
  3. set the stack pointer with `uc_reg_write(v->uc, UC_X86_REG_RSP, &rsp)` where
     `rsp = BOOTINFO_BASE - 16` (16-byte aligned), and register the fault hook
     with `uc_hook_add(v->uc, &h, UC_HOOK_MEM_UNMAPPED, mem_invalid, v, 1, 0)`.

  Opening the log store + CPU, allocating/initing the device, and setting RDI are
  already done in the provided part of the function — leave them as-is.
- **`serial_write(uc, offset, size, value, user_data)`** — cast `user_data` to
  `struct vmm *v`. On `SERIAL_TX`, write the low byte of `value` to stdout
  (`putchar`); on `SERIAL_POWEROFF`, set `v->exit_code = (int)value`,
  `v->powered_off = 1`, and stop the CPU with `uc_emu_stop(uc)`; ignore any
  other offset.
- **`vmm_load_binary`** — read the whole flat binary at `path` into `v->ram`
  (offset 0 == `RAM_BASE`), rejecting a file larger than `RAM_SIZE`, then set the
  entry point with `uc_reg_write(v->uc, UC_X86_REG_RIP, &rip)`, `rip = RAM_BASE`.
- **`mem_invalid(uc, type, address, size, value, user_data)`** — the
  `UC_HOOK_MEM_UNMAPPED` callback (registered in `vmm_create`); cast `user_data`
  to `struct vmm *v`. Set `v->faulted = 1` and `v->fault_addr = address`, print
  the fault to **stderr**, call `uc_emu_stop(uc)`, and return `false` ("do not
  retry the access"). `vmm_run` then sees `v->faulted` and returns
  `VMM_EXIT_FAULT`.
- **`vmm_run`** — start the CPU with `uc_emu_start(v->uc, RAM_BASE, 0, 0, 0)`; it
  returns when your `serial_write` calls `uc_emu_stop`. Then return
  `VMM_EXIT_FAULT` if `v->faulted`, `v->exit_code` on a clean poweroff, or
  non-zero if `uc_emu_start` returned an error while `!v->powered_off`.
- **`vmm_gpa_to_host(v, gpa, len)`** — return `v->ram + (gpa - RAM_BASE)` for the
  guest-physical range `[gpa, gpa+len)`, or **`NULL`** unless the *entire* range
  lies within `[RAM_BASE, RAM_BASE + RAM_SIZE)`. Watch for integer overflow when
  forming `gpa + len` for the upper-bound check. The device uses this to reach
  guest buffers.

  You implement the same translation in Part III, against an untrusted guest's
  addresses. Getting it wrong lets a guest reach host memory it was never given.

Provided (do not modify): the log store and CPU setup, the optional `--trace`
instruction tracer, device allocation/init, the RDI boot pointer, the boot-info
blob loader, and teardown.

### How it runs

`./emulator [--trace] [--log <path>] [--listen <port>] <guest.bin>` creates the
VMM, loads the binary, and runs it until poweroff, returning the guest's exit
code. The test suite exercises every piece above: a guest cannot boot or run
unless RAM, the stack, serial, device registration, the run loop, and
`vmm_gpa_to_host` are all correct.

---

## Part II: The logging device (your own MMIO protocol)

*You implement this in `emulator/device.c`.*

### 1. Overview

The emulator presents a single memory-mapped I/O (MMIO) device at guest-physical
base address `DEV_BASE = 0x2000_0000`, occupying `DEV_SIZE = 0x1000` bytes. The
device is a paravirtual log sink for the function runner: the guest hands it a
message buffer plus a severity level, and the device appends the message as one
record to a log file on the host (the VM's filesystem). Performance is not
graded, only correctness and robustness.

All registers are 32-bit and accessed at 4-byte-aligned offsets. The guest issues
a command by programming the operand registers and then writing the command code
to `CMD`. The command executes synchronously during that MMIO write; when the
write returns, `STATUS` and `SEQ` are valid.

With paging disabled in the guest, a guest virtual address equals its
guest-physical address, so the guest passes ordinary C pointers as addresses.
The device translates those addresses with the `vmm_gpa_to_host()` helper (which
you wrote in Part I) and **must not** dereference guest addresses directly.

The device appends records through the provided log store (`logstore_append` via
`dev->vmm->store`), which owns the record format and the on-disk file. Its path
defaults to `vlog.log` and is overridable with the emulator's `--log <path>`
option.

### Functions you implement (`device.c`)

Two MMIO callbacks; the emulator routes every access in `[DEV_BASE, DEV_BASE +
DEV_SIZE)` to them. Device state is `struct vlog_device` (`device.h`): you update
`dev->msg_addr_lo`/`dev->msg_addr_hi`, `dev->len`, `dev->level` (the operands the
guest programs), `dev->seq` (advance by one on each successful `LOG`), `dev->bytes`
(running sum of logged message bytes, reported by `STAT`), and `dev->status` (via
the helpers below). `vlog_device_init` (which sets `dev->status = READY` etc.) is
provided.

Provided helpers in `device.c` — use them:

- `msg_addr(dev)` — reassembles the 64-bit `MSG` address from
  `dev->msg_addr_lo`/`dev->msg_addr_hi`;
- `set_error(dev, code)` / `clear_error(dev)` — set / clear the `ERROR` bit and
  `ERRCODE` field in `dev->status` (§3); use the `VLOG_ERR_*` codes;
- `vmm_gpa_to_host(dev->vmm, gpa, len)` — the Part I translator; the **only** way
  to reach a guest buffer (never cast a guest address to a pointer yourself, and
  always check the result for `NULL`);
- `logstore_append(dev->vmm->store, dev->seq, dev->level, msg, len)` — append one
  record (the store owns the on-disk format); `logstore_flush(dev->vmm->store)`.

- **`vlog_device_mmio_read(offset)`** — return the 32-bit register at `offset`
  (`ID`, `VERSION`, `STATUS`, `MSG_LO`, `MSG_HI`, `LEN`, `LEVEL`, `SEQ`); return
  `0` for any other offset (§2).
- **`vlog_device_mmio_write(offset, value)`** — store operand writes
  (`MSG_LO`/`MSG_HI`/`LEN`/`LEVEL`); on a `CMD` write, execute the command (§4),
  validating fully **before** any side effect (on failure call `set_error` and
  append nothing); ignore writes to read-only registers and unknown offsets.

### 2. Register map

Offsets are relative to `DEV_BASE`. Access is from the guest's point of view.

| Offset | Name      | Access | Description                                            |
|--------|-----------|--------|--------------------------------------------------------|
| 0x00   | `ID`      | RO     | Magic identifier `0x3147_4C56` ("VLG1")                |
| 0x04   | `VERSION` | RO     | Device version (currently `1`)                         |
| 0x08   | `STATUS`  | RO     | Status / error flags (see §3)                          |
| 0x0C   | `CMD`     | WO     | Write a command code (§4) to execute it                |
| 0x10   | `MSG_LO`  | RW     | Message guest-physical address, low 32 bits            |
| 0x14   | `MSG_HI`  | RW     | Message guest-physical address, high 32 bits           |
| 0x18   | `LEN`     | RW     | Message length in bytes                                |
| 0x1C   | `LEVEL`   | RW     | Severity level for the next `LOG` (see §4.1)           |
| 0x20   | `SEQ`     | RO     | Number of records successfully logged so far           |

Rules:

- **Reads** of any unmapped offset within the region return `0`.
- **Writes** to read-only registers (`ID`, `VERSION`, `STATUS`, `SEQ`) and to
  any unmapped offset are silently ignored.
- Operand registers (`MSG_*`, `LEN`, `LEVEL`) retain their values across
  commands; the guest only needs to reprogram what changes.
- Only 32-bit accesses are required to be supported. Behavior for sub-word or
  misaligned accesses is unspecified and will not be tested.

### 3. STATUS register

```
bit 0       READY    device is ready to accept a command
bit 2       ERROR    the most recent command failed
bits [15:8] ERRCODE  error code for the most recent command (valid iff ERROR)
```

`ERRCODE` values:

| Code | Name             | Meaning                                                  |
|------|------------------|----------------------------------------------------------|
| 0    | `ERR_NONE`       | no error                                                 |
| 1    | `ERR_BADCMD`     | unknown command code                                     |
| 2    | `ERR_BADADDR`    | the message range is not entirely within guest RAM       |
| 3    | `ERR_BADLEN`     | the message length exceeds `VLOG_MAX_MSG`                |

A successful command **clears** `ERROR` and the `ERRCODE` field. A failing
command **sets** `ERROR` with the appropriate code, does **not** append a record
to the log, and does **not** advance `SEQ`. Validate the request fully before
writing anything to the log (no partial records).

### 4. Commands

#### 4.0 `NOP` (code 0)

Does nothing. Clears `ERROR`. Does not touch the log or `SEQ`. Always succeeds.

#### 4.1 `LOG` (code 1)

Operands: `MSG`, `LEN`, `LEVEL`.

Append one record to the host log file for the `LEN` bytes at
`[MSG, MSG+LEN)`. The record format is exactly one line:

```
[<seq>] <LEVEL> <message bytes><newline>
```

where `<seq>` is the current value of `SEQ` (records are numbered from 0) and
`<LEVEL>` is the level name. On success, `SEQ` is incremented by one.

Severity levels:

| `LEVEL` value | Name    |
|---------------|---------|
| 0             | `DEBUG` |
| 1             | `INFO`  |
| 2             | `WARN`  |
| 3             | `ERROR` |
| other         | `LVL?`  |

Level values outside 0–3 are **not** an error; they are written with the
placeholder name `LVL?`.

Constraints:

- `LEN == 0` is valid and logs an empty message (just the `[seq] LEVEL ` prefix
  and a newline).
- `LEN > VLOG_MAX_MSG` (4096) → `ERR_BADLEN`.
- If `LEN > 0` and `[MSG, MSG+LEN)` is not entirely within guest RAM →
  `ERR_BADADDR`. (When `LEN == 0` the address is not examined.)
- The message bytes are written verbatim. If they contain a newline the record
  will span multiple physical lines; the autograder controls test inputs.

#### 4.2 `FLUSH` (code 2)

Flush the host log file to disk (`logstore_flush`). Always succeeds.

#### 4.3 `STAT` (code 3)

Operands: `MSG`, `LEN`.

Every other command *reads* guest memory. This one **writes** to it: the device
fills the guest's buffer at `[MSG, MSG+LEN)` with

```c
struct vlog_stats {
    uint32_t records;   /* records successfully logged (same value as SEQ) */
    uint32_t bytes;     /* total message bytes logged, summed over all LOGs */
};
```

`bytes` is the sum of the `LEN` of every **successful** `LOG`: the message bytes
only, not the `[seq] LEVEL ` prefix. Track it as you log.

Because the device is writing into memory the guest gave it, validate **before**
you write:

- `LEN < sizeof(struct vlog_stats)` (the guest offered too small a buffer) →
  `ERR_BADLEN`, and **write nothing**;
- `[MSG, MSG+sizeof(struct vlog_stats))` not entirely within guest RAM →
  `ERR_BADADDR`, and **write nothing**.

A device that trusts the guest's `LEN` or `MSG` here writes past the buffer the
guest offered, which is the classic hypervisor-escape bug. `STAT` does not touch
the log and does not advance `SEQ`.

#### Unknown commands

Any `CMD` value other than 0–3 → `ERR_BADCMD`; the log and `SEQ` are unchanged.

### 5. Programming sequence (informational)

The guest library [guest/vlog.h](guest/vlog.h) wraps this sequence. For `LOG`:

1. Write `MSG_LO` / `MSG_HI`.
2. Write `LEN`.
3. Write `LEVEL`.
4. Write `CMD = 1`.
5. Read `STATUS`; if `ERROR` is set, read `ERRCODE`; otherwise the record is in
   the log and `SEQ` has advanced.

### 6. Authoritative reference output

Running the provided `test_log.bin` guest produces this log file (the autograder
compares against it byte-for-byte):

```
[0] INFO function runner started
[1] DEBUG processing request 1
[2] WARN cache miss
[3] ERROR request 1 failed
```

and `SEQ` reads back as `4` afterward.

---

## Part III: The same device, as real virtio

*You implement this in `vhost/virtio.c`.*

### 1. Overview

Part II's register map is a protocol we invented. Real paravirtual devices (disks,
NICs, consoles, GPUs) all speak virtio, and virtio's data path is the split
virtqueue. In this part you implement that queue, for the same log device, and a
real QEMU virtual machine drives it: a real Linux guest whose stock
`virtio_console` driver binds to your backend with no guest-side code. Whatever
the guest writes to `/dev/hvc0` lands in your queue and, if you get it right, in
your log.

Your device runs as a vhost-user backend: a separate host process that QEMU
connects to over a UNIX socket. QEMU shares the guest's memory with it and tells
it where the rings are. Your code then does what it did in Part II, against the
real protocol:

| Part II (the protocol you invented) | Part III (the one the world uses) |
|---|---|
| guest writes the `CMD` register to submit | driver publishes on the **avail ring** and kicks |
| read `MSG_LO`/`MSG_HI` + `LEN` operands | walk a **descriptor chain** |
| `vmm_gpa_to_host(gpa, len)` | `virtq_gpa_to_hva(mem, addr, len)`, the same job |
| write `STATUS` / advance `SEQ` | complete on the **used ring** |
| your MMIO callback | your virtqueue handler |

### 2. The split virtqueue

A virtqueue is three arrays living in **guest memory** (see `vhost/virtq.h`,
which gives you host pointers to all three):

- **Descriptor table** (`vq->desc`, `vq->num` entries). Each descriptor is
  `{ addr, len, flags, next }`. `addr` is a **guest-physical address**; `len` is
  its length. `VRING_DESC_F_NEXT` means the buffer continues at index `next`
  (a *chain*). `VRING_DESC_F_WRITE` means the buffer is **device-writable**:
  space for us to write *into*. This queue is guest-to-host only, so writable
  descriptors carry no data for you, and you skip them.
- **Avail ring** (`vq->avail`). The driver publishes work here:
  `avail->ring[i % num]` holds the **head** descriptor index of a chain, and
  `avail->idx` is a free-running counter of how many it has published.
  `vq->last_avail` is *your* cursor into it, and you advance it.
- **Used ring** (`vq->used`). You publish completions here:
  `used->ring[i % num] = { id, len }` where `id` is the chain's **head** index,
  followed by bumping `used->idx`. This is how the driver learns its buffer is
  free again. If you never do this, the guest hangs after its first write,
  waiting for you.

`len` in a used entry is the number of bytes the *device* wrote into
driver-writable buffers. Ours is a guest→host queue, so it is 0.

### 3. What you implement, part (a): the address translation

```c
void *virtq_gpa_to_hva(const struct virtq_mem *mem, uint64_t gpa, uint64_t len);
```

QEMU shares the guest's RAM with your backend as a **table of regions**: each says
"guest-physical `[gpa, gpa+size)` is mapped at `hva` in your address space."
There can be several, and there are **gaps between them**.

Return a host pointer for `[gpa, gpa+len)`, or `NULL` unless the range lies
entirely inside one region. Reject a range that runs off the end of its region
(no straddling), one that lands in a gap or outside every region, and one whose
`gpa + len` overflows.

Same as `vmm_gpa_to_host()` from Part I, against a real memory map. The guest
chooses the addresses and lengths, so validate every range before you
dereference it.

### 4. What you implement, part (b): the virtqueue

```c
int vlog_virtq_handle(struct virtq *vq, const struct virtq_mem *mem,
                      struct vlog_sink *sink);
```

Accumulate each chain's data bytes into a **local** buffer (`char
rec[VIRTQ_MAX_RECORD]`) and emit the finished record with the provided
`vlog_sink_emit(sink, rec, len)`. The provided memory-ordering fences are
`virtq_rmb()` (call it before you trust what the driver published) and
`virtq_wmb()` (before you publish to the used ring). The cursor you own and
advance is `vq->last_avail`; you publish completions at `vq->used->ring[...]` and
bump `vq->used->idx`.

For every chain the guest has made available:

1. Read the head from the avail ring:
   `head = vq->avail->ring[vq->last_avail % vq->num]`. (First read
   `vq->avail->idx` to see how many are ready, and `virtq_rmb()` before you trust
   what it published.)
2. Walk the descriptor chain from `head`. For each descriptor:
   - **`VRING_DESC_F_WRITE` set**: it is space for the *device* to write into.
     This queue is guest-to-host, so it carries no data, and you skip it.
   - **`VRING_DESC_F_INDIRECT` set**: it is not data either. `d->addr` points at
     a **table of further descriptors** in guest memory (`d->len` bytes of them,
     so `d->len / sizeof(struct vring_desc)` entries), which form their own chain
     starting at index 0. Translate the table and walk it. (Indirect tables do
     not nest.)
   - otherwise it is data: translate `d->addr` with
     `virtq_gpa_to_hva(mem, d->addr, d->len)`, **check for NULL**, and append its
     bytes.

   Follow `d->next` while `VRING_DESC_F_NEXT` is set, and cap the record at
   `VIRTQ_MAX_RECORD`.
3. Emit it: `vlog_sink_emit(sink, bytes, len)` (the provided sink parses the
   `"<level> <message>"` format and appends to the shared log store).
4. **Complete the chain on the used ring**:
   `vq->used->ring[vq->used->idx % vq->num] = { .id = head, .len = 0 };` then
   `virtq_wmb();` then `vq->used->idx++`.
5. Advance `vq->last_avail` and count the chain.

Return the number of chains completed; the backend raises the guest's interrupt
when that is > 0.

### 5. Safety

The rings live in memory the guest owns and can change at any time. A descriptor
index, a chain, or a length can be nonsense, by accident or on purpose. So:

- bounds-check every index against `vq->num` (a head, a `next`);
- check every `gpa()` result for NULL before you dereference it;
- never let a cyclic chain loop forever (cap the hops);
- never write past the end of your record buffer.

This is the same discipline `vmm_gpa_to_host()` enforces in Part I.

### 6. What is provided

`vhost/backend.c` (all the vhost-user protocol: feature negotiation, mapping the
guest's memory, ring setup, kick/call eventfds), `vhost/virtq.h` (the ring
structures and the memory-region table), `vhost/sink.c` (the log sink),
`vhost/run-qemu.sh` (boots the VM), and the guest itself. You write the queue.

The helpers you call all come from `vhost/virtq.h`: `virtq_rmb()` / `virtq_wmb()`
(the ordering fences), `vlog_sink_emit(sink, bytes, len)` (one call = one record
into the shared log, which assigns the sequence number), and the `VIRTQ_MAX_RECORD`
cap for your record buffer.

Two optional virtio features (`EVENT_IDX`, `INDIRECT_DESC`) are handled by the
provided backend / not used by this guest, so the queue you implement is the
plain split ring.

### 7. Testing it

```bash
cd tests && ./run_tests.sh           # the whole suite (Parts I, II, III); what the grader runs
cd tests && ./run_virtq_tests.sh     # just Part III's synthetic rings: fast, deterministic
cd vhost && ./run-qemu.sh            # a REAL QEMU VM drives your backend (optional)
```

`run_tests.sh` builds your emulator and runs every check the autograder runs;
there are no hidden tests. It prints PASS or FAIL per requirement (no points; the
grader assigns those). The oracle-driven Part II checks use fresh random inputs
each run, so a device cannot hardcode answers. Below is the Part III piece in
more detail:

`run_virtq_tests.sh` builds descriptor chains in a fake guest with two memory
regions and a gap between them. It is staged, so you can build up and a partial
implementation still passes the early stages:

- `translate`: `virtq_gpa_to_hva()` alone (valid, straddling, in-the-gap,
  overflowing);
- `single`: the minimal queue, one readable descriptor per chain, emitted and
  completed on the used ring;
- `chained`: multi-descriptor chains, skipping device-writable descriptors and
  rejecting ones that don't translate;
- `indirect`: indirect descriptor tables.

Each later stage assumes the earlier ones. This is the same staged set the
autograder scores (each stage is graded separately), so develop against it. Then
run `run-qemu.sh` to watch a real Linux guest log through your device; a hang
there almost always means the used ring.

