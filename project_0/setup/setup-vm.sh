#!/usr/bin/env bash
#
# setup-vm.sh — install the VM toolchain for Parts 1-3.
#
#   cd project_0 && sudo ./setup/setup-vm.sh
#
# Run this once, inside the Ubuntu VM you set up in SETUP.md. It installs the
# compilers, libraries, and tools needed to build and run every part of Projects
# 0 through 4. It does not install any course code; you clone it yourself.
#
# Safe to re-run. Works on amd64 and arm64 hosts; the arch-specific steps are
# guarded on $ARCH.
#
# If your network blocks the kraftkit download, fetch the tarball on another
# machine and re-run with KRAFT_TARBALL=/path/to/kraft_<ver>_linux_<arch>.tar.gz.
set -euo pipefail

log() { printf '\n\033[1;34m== %s\033[0m\n' "$*"; }
die() { printf '\033[1;31merror: %s\033[0m\n' "$*" >&2; exit 1; }

[ $# -eq 0 ] || die "usage: sudo ./setup/setup-vm.sh (takes no arguments)"
[ "$(id -u)" -eq 0 ] || die "run as root: sudo ./setup/setup-vm.sh"

# The script runs as root (for apt and installing into /usr/local/bin), but a few
# steps must run as the user who called sudo. kraft in particular keeps its config
# and package cache under $HOME; if it runs as root the cache lands in /root and
# your later, non-root `kraft build` cannot see it.
REAL_USER="${SUDO_USER:-root}"
as_user() {
    if [ "$REAL_USER" = root ]; then "$@"; else sudo -u "$REAL_USER" -H "$@"; fi
}

export DEBIAN_FRONTEND=noninteractive
ARCH="$(dpkg --print-architecture)"   # amd64 | arm64
log "installing course toolchain (arch=$ARCH, user=$REAL_USER)"

# ---------------------------------------------------------------------------
log "apt update"
apt-get update -y

# ---- Kernel: cloud images can boot a cut-down kernel ---------------------
# The Ubuntu cloud image sometimes boots linux-kvm (minimal), which omits
# modules Project 2 needs (veth, overlayfs, some cgroup controllers). Install
# linux-generic; it takes effect on the next reboot. Nothing in this script or
# Project 0 needs it, so we don't stop for the reboot -- the reminder prints at
# the end.
reboot_for_kernel=0
if ! dpkg-query -W -f='${Status}' linux-generic 2>/dev/null | grep -q "install ok installed"; then
    log "installing the full kernel (linux-generic) for Project 2"
    apt-get install -y linux-generic
    reboot_for_kernel=1
fi

# ---- Common toolchain (all projects) --------------------------------------
log "common build toolchain"
apt-get install -y build-essential pkg-config git make gdb rsync curl

# ---- Project 1: VM / MMIO logging device (Unicorn) ------------------------
log "project 1: Unicorn"
apt-get install -y libunicorn-dev
# The Project 1 guests are always x86-64 flat binaries (Unicorn emulates x86-64
# even on an arm64 host), so on arm64 they need a cross-compiler. On amd64 the
# native gcc already provides x86_64-linux-gnu-gcc.
if [ "$ARCH" = "arm64" ]; then
    apt-get install -y gcc-x86-64-linux-gnu
fi
pkg-config --atleast-version=2.0 unicorn \
    || die "libunicorn-dev >= 2.0 required (uc_mmio_map); got $(pkg-config --modversion unicorn 2>/dev/null || echo none)"
# Project 1 Part III boots a real QEMU VM: it needs qemu-system for this arch and
# cpio to build the guest initramfs. libvhost-user is vendored into the repo,
# so nothing is needed for that.
if [ "$ARCH" = "arm64" ]; then
    apt-get install -y qemu-system-arm cpio     # provides qemu-system-aarch64
else
    apt-get install -y qemu-system-x86 cpio
fi

# ---- Project 2: container runtime (cgroup v2 / namespaces) ----------------
log "project 2: container tooling"
# Namespaces and cgroup v2 are kernel features; Ubuntu 24.04 already boots with
# cgroup v2 unified, so nothing installs them. busybox-static gives the container
# rootfs a shell and basic tools. iproute2 (`ip`) wires the veth/bridge for the
# --net option. libc6-dev provides the static libc that every binary in a
# library-less rootfs must be linked against.
apt-get install -y busybox-static iproute2 libc6-dev
command -v busybox >/dev/null && file "$(command -v busybox)" | grep -q 'statically linked' \
    || die "static busybox missing (Project 2 in-container shell)"
command -v ip >/dev/null || die "iproute2 (ip) missing (Project 2 --net host side)"

# Ubuntu 24.04 ships kernel.apparmor_restrict_unprivileged_userns=1, which stops
# an unconfined process from setting up a user namespace -- the uid_map write
# fails with EPERM. Project 2, and Project 0 Part 2, build their container as an
# ordinary user through a user namespace, so turn the restriction off now and
# across reboots.
log "project 2: enabling unprivileged user namespaces"
sysctl -w kernel.apparmor_restrict_unprivileged_userns=0 >/dev/null
cat > /etc/sysctl.d/99-cs360v-userns.conf <<'EOF'
# CS360V Project 2: the container is built as an unprivileged user via a user
# namespace; Ubuntu 24.04 restricts that by default.
kernel.apparmor_restrict_unprivileged_userns = 0
EOF
if [ "$REAL_USER" != root ]; then
    as_user unshare --user --map-root-user --pid --fork --mount true 2>/dev/null \
        || die "unprivileged user namespaces still blocked after the sysctl (Project 2)"
fi
# Check now that `gcc -static` can build and run a static binary: a pivoted rootfs
# has no shared libraries, so every command and grader payload must be static.
_p2t="$(mktemp -d)"
printf 'int main(void){return 0;}\n' > "$_p2t/s.c"
if gcc -static -O2 -o "$_p2t/s" "$_p2t/s.c" 2>/dev/null && "$_p2t/s"; then
    file "$_p2t/s" | grep -q 'statically linked' \
        || die "gcc -static did not produce a static binary (Project 2 payloads)"
else
    die "gcc -static failed to build/run a static binary — need static libc (libc6-dev) for Project 2"
fi
rm -rf "$_p2t"

# ---- Project 3: unikernel (Unikraft) --------------------------------------
log "project 3: unikraft toolchain (kraftkit)"
# qemu-system is already installed above. Unikraft's build system also needs
# flex/bison/ncurses for kconfig and libelf/uuid for the image tooling.
apt-get install -y flex bison libncurses-dev libelf-dev uuid-dev

# kraft drives the Unikraft build. Install a pinned release binary rather than
# piping a network installer into a shell, so the result is reproducible.
KRAFT_VERSION="${KRAFT_VERSION:-0.12.14}"
case "$ARCH" in
    (amd64) KRAFT_ARCH=amd64 ;;
    (arm64) KRAFT_ARCH=arm64 ;;
    (*)     die "unsupported arch '$ARCH' for kraftkit" ;;
esac
if ! command -v kraft >/dev/null 2>&1 || \
   ! as_user kraft version 2>/dev/null | grep -q "$KRAFT_VERSION"; then
    tmp="$(mktemp -d)"
    if [ -n "${KRAFT_TARBALL:-}" ]; then
        # offline path: a pre-downloaded tarball, for a firewalled network.
        [ -f "$KRAFT_TARBALL" ] || die "KRAFT_TARBALL='$KRAFT_TARBALL' not found"
        cp "$KRAFT_TARBALL" "$tmp/kraft.tar.gz"
    else
        url="https://github.com/unikraft/kraftkit/releases/download/v${KRAFT_VERSION}/kraft_${KRAFT_VERSION}_linux_${KRAFT_ARCH}.tar.gz"
        curl -sSL "$url" -o "$tmp/kraft.tar.gz" \
            || die "could not download kraftkit $KRAFT_VERSION. Behind a firewall? Fetch it elsewhere and re-run with KRAFT_TARBALL=/path/to/kraft_${KRAFT_VERSION}_linux_${KRAFT_ARCH}.tar.gz"
    fi
    tar xzf "$tmp/kraft.tar.gz" -C "$tmp"
    install -m 0755 "$tmp/kraft" /usr/local/bin/kraft
    rm -rf "$tmp"
fi
command -v kraft >/dev/null || die "kraft not installed (Project 3)"
as_user kraft version >/dev/null 2>&1 || die "kraft is installed but does not run (Project 3)"

# Warm the kraft package index now, as the user, so it is cached in that user's
# home. `kraft build` otherwise refreshes the index on every run, which takes
# minutes; with the cache warm you build with --no-update and skip it. This step
# is slow and needs the network. It must run as $REAL_USER, not root: run under
# sudo it would write the cache to /root, where your later builds cannot find it.
log "project 3: warming the kraft package cache (slow, one time)"
as_user kraft pkg update >/dev/null 2>&1 \
    || echo "warning: 'kraft pkg update' failed; your first unikernel build will be slow"

# ---- Project 4: serverless ------------------------------------------------
log "project 4: serverless platform (python3)"
# The Project 4 platform is pure-stdlib Python 3 (http.server + socket), no
# third-party packages. python3 ships with Ubuntu, so this only checks it.
command -v python3 >/dev/null || die "python3 missing (Project 4 serverless platform)"
python3 - <<'PY' || die "python3 stdlib missing http.server/socket (Project 4)"
import http.server, socket, threading, json  # noqa: F401
PY

# ---- Verification ---------------------------------------------------------
log "verification"
gcc --version | head -1
command -v x86_64-linux-gnu-gcc >/dev/null \
    || die "x86_64-linux-gnu-gcc missing (needed to build Project 1 guests)"
echo "x86_64 guest cc: $(x86_64-linux-gnu-gcc -dumpfullversion)"
echo "unicorn: $(pkg-config --modversion unicorn)"
QEMU_BIN="$([ "$ARCH" = arm64 ] && echo qemu-system-aarch64 || echo qemu-system-x86_64)"
command -v "$QEMU_BIN" >/dev/null || die "$QEMU_BIN missing (Project 1 Part III / Project 3)"
"$QEMU_BIN" -device help 2>/dev/null | grep -q vhost-user-device-pci \
    || die "$QEMU_BIN lacks vhost-user-device-pci (need QEMU >= 7.2; Ubuntu 24.04 ships 8.2)"
command -v cpio >/dev/null || die "cpio missing (Project 1 Part III guest initramfs)"
echo "qemu: $("$QEMU_BIN" --version | head -1 | awk '{print $NF}')  ($QEMU_BIN, vhost-user-device-pci ok)"

log "setup complete"
echo "The toolchain is ready. Clone a project and build it from its own"
echo "directory; each project has a README."
if [ "$reboot_for_kernel" = 1 ]; then
    echo
    echo "linux-generic was installed. Project 0 works now, but reboot"
    echo "(sudo reboot) before starting Project 2 so its kernel modules load."
fi
