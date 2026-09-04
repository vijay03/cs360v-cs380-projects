#!/usr/bin/env bash
#
# run_virtq_tests.sh: test YOUR vhost/virtio.c against synthetic virtqueues.
#
# No QEMU, no timing: descriptor chains are built in a fake guest-memory buffer
# and handed to your code. The tests are STAGED, so a partial implementation
# passes the early stages and you can build up:
#
#   translate  virtq_gpa_to_hva() alone
#   single     a minimal queue: one descriptor per chain -> emit -> used ring
#   chained    multi-descriptor chains + skipping writable / unmapped descriptors
#   indirect   indirect descriptor tables
#
# Each later stage assumes the earlier ones work. This is the same set the
# autograder runs, so develop against it, then watch it drive a real VM with
# ../vhost/run-qemu.sh.
#
#   ./run_virtq_tests.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
VHOST="$HERE/../vhost"
EMU="$HERE/../emulator"
VIRTIO="${VIRTIO_SRC:-$VHOST/virtio.c}"
BIN="$(mktemp -d)/virtq_test"

if ! gcc -std=c11 -Wall -O1 -g -I"$VHOST" -I"$EMU" \
        "$HERE/virtq_test.c" "$VIRTIO" "$VHOST/sink.c" "$EMU/logstore.c" \
        -pthread -o "$BIN" 2>/tmp/virtq_build.err; then
    echo "BUILD FAILED:"; cat /tmp/virtq_build.err; exit 1
fi

pass=0; fail=0
for stage in translate single chained indirect; do
    for seed in 1 2 3 4; do
        if timeout 10 "$BIN" "$stage" "$seed" "$(dirname "$BIN")/t.log"; then
            pass=$((pass+1))
        else
            rc=$?
            [ "$rc" -eq 124 ] && echo "  FAIL  $stage seed=$seed  (timed out: infinite loop in your chain walk?)"
            fail=$((fail+1))
        fi
    done
done
rm -rf "$(dirname "$BIN")"
echo "-----------------------------------"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
