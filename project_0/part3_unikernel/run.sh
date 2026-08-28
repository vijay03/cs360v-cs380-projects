#!/usr/bin/env bash
#
# Part 3: build the unikernel with kraft and boot it under QEMU.
#
# --no-update / --no-prompt: skip kraft's per-build index refresh and any prompt.
# `</dev/null` on kraft and qemu: with a terminal on stdin they switch to an
# interactive display and their output never reaches the console. Keep it.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

# Build for this machine's architecture. kraft and QEMU spell it differently, and
# arm64's QEMU needs an explicit machine type ("virt"); x86_64 has a usable default.
case "$(uname -m)" in
    (aarch64|arm64) KARCH=arm64;  QEMU=qemu-system-aarch64; MACHINE=(-machine virt) ;;
    (*)             KARCH=x86_64; QEMU=qemu-system-x86_64;  MACHINE=() ;;
esac

command -v kraft >/dev/null || { echo "kraft not found (Project 3 toolchain)"; exit 1; }

kraft build --plat qemu --arch "$KARCH" --no-update --no-prompt </dev/null

IMG="$(ls ".unikraft/build/hello_qemu-$KARCH" 2>/dev/null || true)"
[ -n "$IMG" ] || { echo "no image built at .unikraft/build/hello_qemu-$KARCH"; exit 1; }

# -cpu max for the features Unikraft expects; -no-reboot + timeout since the
# guest just prints and exits.
command -v "$QEMU" >/dev/null || { echo "$QEMU not found (Project 3 toolchain)"; exit 1; }
timeout 30 "$QEMU" "${MACHINE[@]}" -kernel "$IMG" -nographic -no-reboot -m 64M -cpu max </dev/null 2>/dev/null || true
