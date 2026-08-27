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

command -v kraft >/dev/null || { echo "kraft not found (Project 3 toolchain)"; exit 1; }

kraft build --plat qemu --arch x86_64 --no-update --no-prompt </dev/null

IMG="$(ls .unikraft/build/hello_qemu-x86_64 2>/dev/null || true)"
[ -n "$IMG" ] || { echo "no image built at .unikraft/build/hello_qemu-x86_64"; exit 1; }

# -cpu max for the features Unikraft expects; -no-reboot + timeout since the
# guest just prints and exits.
QEMU=qemu-system-x86_64
command -v "$QEMU" >/dev/null || { echo "$QEMU not found (Project 3 toolchain)"; exit 1; }
timeout 30 "$QEMU" -kernel "$IMG" -nographic -no-reboot -m 64M -cpu max </dev/null 2>/dev/null || true
