#!/usr/bin/env bash
#
# Part 2: build hello statically and run it in new namespaces.
#
# Static linking, because a container pivots into a root filesystem with no
# shared libraries. New user, pid, and mount namespaces (via unshare), the
# isolation Project 2's runtime is built on. --map-root-user means no sudo is
# needed; if it fails with "Operation not permitted", unprivileged user
# namespaces are disabled on this machine (see README.md).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

cc -O2 -Wall -static -o hello hello.c
file hello | grep -q 'statically linked' || { echo "hello is not static"; exit 1; }

# Run it in its own user/pid/mount namespaces. In the pid namespace it is pid 1.
unshare --user --map-root-user --pid --fork --mount ./hello
