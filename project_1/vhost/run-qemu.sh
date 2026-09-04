#!/usr/bin/env bash
#
# run-qemu.sh: boot a REAL QEMU virtual machine against YOUR virtio backend.
#
# There is no VM image involved. We boot the host's OWN Linux kernel
# (/boot/vmlinuz-*) directly with -kernel, and hand it a tiny initramfs built in
# guest/ whose only program is a static init that logs through /dev/hvc0. So the
# guest is a real Linux, with the stock virtio_console driver binding to your
# backend, but it needs no disk and boots in seconds.
#
# The machine is built for the HOST architecture, so this works unchanged on
# x86-64 and arm64: vhost-user and the virtqueue are
# architecture-agnostic, and your virtio.c never sees the difference.
#
#   ./run-qemu.sh                     # build + boot + print the log your backend wrote
#   VIRTIO_SRC=/path/virtio.c ./run-qemu.sh
#
# If it works you will see the four records below. If your used-ring handling is
# wrong the guest typically hangs after the first write (the driver never gets
# its buffers back). See SPEC.md Part III.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
LOG="${LOG:-$HERE/virtio.log}"
# vhost-user needs a short socket path (sun_path is limited to 108 bytes).
SOCK="${SOCK:-/tmp/vlog-vhost-$$.sock}"

# ---- pick the machine for this architecture --------------------------------
ARCH="${ARCH:-$(uname -m)}"
case "$ARCH" in
    (x86_64|amd64)
        QEMU=qemu-system-x86_64
        MACHINE=q35
        CONSOLE=ttyS0
        CPU=()
        HOSTGCC=gcc
        CROSSGCC=x86_64-linux-gnu-gcc
        ;;
    (aarch64|arm64)
        QEMU=qemu-system-aarch64
        MACHINE=virt      # the arm64 "virt" board: PCIe, a PL011 serial, no BIOS
        CONSOLE=ttyAMA0
        CPU=(-cpu max)    # `virt` has no sensible default 64-bit CPU
        HOSTGCC=gcc
        CROSSGCC=aarch64-linux-gnu-gcc
        ;;
    (*)
        echo "unsupported architecture '$ARCH'" >&2
        exit 2
        ;;
esac

if ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "$QEMU is not installed (apt install qemu-system)." >&2
    exit 2
fi

# The kernel is the host's own; Project 0's setup-vm.sh installs it (linux-generic).
KERNEL="${KERNEL:-$(ls -1 /boot/vmlinuz-*-generic 2>/dev/null | head -1)}"
[ -n "$KERNEL" ] || KERNEL=/boot/vmlinuz
if [ ! -r "$KERNEL" ]; then
    echo "No readable kernel at '$KERNEL'. Set KERNEL=/path/to/vmlinuz." >&2
    exit 2
fi

# KVM only when the guest matches the host; otherwise QEMU emulates (slower).
KVM=()
if [ "$ARCH" = "$(uname -m)" ] && [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    KVM=(-enable-kvm)
fi

# ---- build the backend and the guest ---------------------------------------
# The backend runs on the HOST, so it builds with the normal compiler. The guest
# must be built for the TARGET architecture: native gcc when they match, else the
# cross toolchain (the arm64 image ships x86_64-linux-gnu-gcc and vice-versa).
if [ "$ARCH" = "$(uname -m)" ]; then GUESTCC="${CC:-$HOSTGCC}"; else GUESTCC="${CC:-$CROSSGCC}"; fi
if ! command -v "$GUESTCC" >/dev/null 2>&1; then
    echo "guest compiler '$GUESTCC' not found (apt install the matching gcc)." >&2
    exit 2
fi
make -C "$HERE" ${VIRTIO_SRC:+VIRTIO_SRC="$VIRTIO_SRC"} >/dev/null || exit 1
# always rebuild the guest: it has to match the architecture we are booting
make -C "$HERE/guest" clean >/dev/null 2>&1
make -C "$HERE/guest" CC="$GUESTCC" >/dev/null || exit 1

rm -f "$LOG" "$SOCK"
"$HERE/vlog-backend" "$SOCK" "$LOG" &
BPID=$!
trap 'kill $BPID 2>/dev/null; rm -f "$SOCK"' EXIT
sleep 0.5

# vhost-user REQUIRES shareable guest memory (memory-backend-memfd,share=on):
# the backend maps the guest's RAM, so it cannot be private anonymous memory.
#
# `</dev/null`: -nographic muxes the serial console and monitor onto stdio. With
# stdout redirected to a file but stdin still a terminal, that stdio setup is
# inconsistent and QEMU can wedge before the guest's console comes up (empty
# qemu.out, then the timeout fires). Feeding stdin from /dev/null fixes it.
timeout "${TIMEOUT:-180}" "$QEMU" \
    -machine "$MACHINE,memory-backend=mem" -m 512M "${CPU[@]}" "${KVM[@]}" \
    -object memory-backend-memfd,id=mem,size=512M,share=on \
    -kernel "$KERNEL" -initrd "$HERE/guest/initramfs.gz" \
    -append "console=$CONSOLE rdinit=/init panic=1" \
    -chardev socket,id=vhost0,path="$SOCK" \
    -device vhost-user-device-pci,virtio-id=3,num_vqs=2,chardev=vhost0 \
    -nographic -no-reboot </dev/null >"$HERE/qemu.out" 2>&1
rc=$?

sleep 0.3
kill $BPID 2>/dev/null

echo "----------------------------------------------------------"
if [ "${#KVM[@]}" -gt 0 ]; then accel="KVM"; else accel="emulated (TCG)"; fi
echo "machine: $QEMU -machine $MACHINE, $accel"
echo "kernel:  $KERNEL"
echo
if [ ! -s "$LOG" ]; then
    echo "Your backend logged NOTHING."
    if [ "$rc" -eq 124 ]; then
        echo "  The VM timed out. The guest is probably stuck waiting for buffers,"
        echo "  which means the USED ring is not being updated. See SPEC.md Part III."
    else
        echo "  Is vlog_virtq_handle() implemented? (the stub returns 0)."
    fi
    echo "  QEMU/guest output is in $HERE/qemu.out"
    exit 1
fi
echo "The log YOUR virtio backend wrote (from a real QEMU guest):"
echo
cat "$LOG"
echo
echo "Expected:"
cat <<'EXP'
[0] INFO function runner started
[1] DEBUG processing request 1
[2] WARN cache miss
[3] ERROR request 1 failed
EXP
