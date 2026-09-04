#!/usr/bin/env bash
#
# run_tests.sh: the Project 1 test suite. It builds the emulator with your vmm.c,
# device.c and virtio.c and runs every check the autograder runs; there are no
# hidden tests. For each requirement it prints PASS or FAIL (no points; the
# autograder assigns those). The oracle-driven checks use FRESH RANDOM inputs each
# run, so re-run for confidence.
#
#   ./run_tests.sh                       # test the in-tree emulator/{vmm,device}.c + vhost/virtio.c
#   ./run_tests.sh device.c vmm.c virtio.c   # or override the sources
#
# Env: P1_SEEDS (random inputs per scenario, default 3), P1_TIMEOUT (sec, default 10).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
EMU="$ROOT/emulator"
TIMEOUT="${P1_TIMEOUT:-10}"
SEEDS="${P1_SEEDS:-3}"

abspath() { case "$1" in (/*) printf '%s' "$1";; (*) printf '%s' "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")";; esac; }
DEVICE="$(abspath "${1:-$EMU/device.c}")"
VMM="$(abspath "${2:-$EMU/vmm.c}")"
VIRTIO="$(abspath "${3:-$ROOT/vhost/virtio.c}")"

# --- result harness ---------------------------------------------------------
# Human PASS/FAIL to the terminal; when the autograder sets CS360V_RESULTS it also
# appends status<TAB>id<TAB>requirement<TAB>reason<TAB>tech per check.
: "${CS360V_RESULTS:=}"
PASS=0; FAIL=0
ck() {  # ck <pass|fail> <id> <requirement> [reason] [tech]
    local st="$1" id="$2" req="$3" reason="${4:-}" tech="${5:-}"
    if [ "$st" = pass ]; then echo "  PASS  $req"; PASS=$((PASS + 1))
    else echo "  FAIL  $req${reason:+  ($reason)}"; FAIL=$((FAIL + 1)); fi
    [ -n "$CS360V_RESULTS" ] && \
        printf '%s\t%s\t%s\t%s\t%s\n' "$st" "$id" "$req" "$reason" "$tech" >> "$CS360V_RESULTS"
    return 0   # never let ck's exit status leak into `... && ck pass || ck fail`
}

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
EMUBIN="$EMU/emulator"; ORACLE="$HERE/oracle"; AGGUEST="$HERE/ag_guest.bin"

echo "== Project 1 test suite =="
echo "   vmm: $VMM"
echo "   device: $DEVICE"
echo "   virtio: $VIRTIO"
echo

# --- build (student code) with the PROVIDED toolchain only ------------------
emu_ok=0; guests_ok=0; vq_ok=0; adv_ok=0
make -C "$EMU" clean >/dev/null 2>&1
make -C "$EMU" VMM_SRC="$VMM" DEVICE_SRC="$DEVICE" >/tmp/p1_emu.log 2>&1 && emu_ok=1
make -C "$HERE" clean >/dev/null 2>&1
make -C "$HERE" >/tmp/p1_guests.log 2>&1 && guests_ok=1     # provided test harness
VQBIN="$WORK/virtq_test"; ADVBIN="$WORK/virtq_private"
gcc -std=c11 -O1 -g -I"$ROOT/vhost" -I"$EMU" \
    "$HERE/virtq_test.c" "$VIRTIO" "$ROOT/vhost/sink.c" "$EMU/logstore.c" \
    -pthread -o "$VQBIN" 2>/tmp/p1_vq.log && vq_ok=1
gcc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover \
    -I"$ROOT/vhost" -I"$EMU" \
    "$HERE/virtq_private.c" "$VIRTIO" "$ROOT/vhost/sink.c" "$EMU/logstore.c" \
    -pthread -o "$ADVBIN" 2>/tmp/p1_adv.log && adv_ok=1

if [ $emu_ok -eq 1 ] && [ $vq_ok -eq 1 ]; then
    ck pass compile "Your vmm.c, device.c and virtio.c compile with only the provided libraries"
else
    r=""; [ $emu_ok -ne 1 ] && r="the emulator (vmm.c/device.c) did not build"
    [ $vq_ok -ne 1 ] && r="${r:+$r; }virtio.c did not build"
    ck fail compile "Your vmm.c, device.c and virtio.c compile with only the provided libraries" "$r"
fi
basic_ready=0; [ $emu_ok -eq 1 ] && [ $guests_ok -eq 1 ] && basic_ready=1
echo

# --- deterministic guests (Part I VMM micro-tests, then Part II device) -----
echo "## deterministic checks"
DET="
m_boot|Part I: the VMM boots a guest to its entry point
m_stack|Part I: the VMM gives the guest a working stack
m_ram|Part I: the VMM maps guest RAM read/write
m_exit|Part I: the VMM handles a clean guest power-off
m_fault|Part I: the VMM reports a guest fault instead of crashing
t_discovery|Part II: the device discovery registers read correctly
t_log_basic|Part II: a basic log message is recorded
t_log_levels|Part II: a log message records its severity level
t_repeated|Part II: repeated log messages are all recorded
t_badcmd|Part II: an invalid command is rejected, not obeyed
t_stat|Part II: the stat register reports the record count
"
while IFS='|' read -r id req; do
    [ -n "$id" ] || continue
    if [ $basic_ready -ne 1 ]; then ck fail "$id" "$req" "the emulator or test guests did not build"; continue; fi
    bin="$HERE/$id.bin"; eobs="$HERE/expected/$id.obs"; elog="$HERE/expected/$id.log"
    gobs="$WORK/g.obs"; glog="$WORK/g.log"
    timeout "$TIMEOUT" "$EMUBIN" --log "$glog" "$bin" </dev/null >"$gobs" 2>/dev/null; rc=$?
    erc="$(cat "$HERE/expected/$id.exit" 2>/dev/null || echo 0)"
    ok=1; reason=""
    if [ $rc -eq 124 ]; then ok=0; reason="timed out"
    elif [ $rc -ne "$erc" ]; then ok=0; reason="the guest exited with $rc (want $erc)"; fi
    if [ ! -f "$eobs" ]; then ok=0; reason="${reason:+$reason; }no expected transcript"
    elif ! diff -q "$eobs" "$gobs" >/dev/null 2>&1; then ok=0; reason="${reason:+$reason; }observations did not match the reference"; fi
    if [ -f "$elog" ] && ! diff -q "$elog" "$glog" >/dev/null 2>&1; then ok=0; reason="${reason:+$reason; }the log did not match the reference"; fi
    [ $ok -eq 1 ] && ck pass "$id" "$req" || ck fail "$id" "$req" "$reason" "guest=$id"
done <<< "$DET"
echo

# --- randomized + edge scenarios via the oracle (fresh random inputs) -------
run_oracle() {  # run_oracle <id> <requirement>   (scenario name == id)
    local id="$1" req="$2" scen="$1" i seed rc ok=1 reason="" last=""
    if [ $basic_ready -ne 1 ]; then ck fail "$id" "$req" "the emulator or oracle did not build"; return; fi
    for i in $(seq 1 "$SEEDS"); do
        seed=$RANDOM; last="$seed"
        local blob="$WORK/o.blob" eo="$WORK/o.eobs" el="$WORK/o.elog" go="$WORK/o.gobs" gl="$WORK/o.glog"
        rm -f "$blob" "$eo" "$el" "$go" "$gl"   # no stale files from a prior scenario
        if ! "$ORACLE" "$scen" "$seed" "$blob" "$eo" "$el" </dev/null 2>/dev/null; then ok=0; reason="internal oracle error"; break; fi
        timeout "$TIMEOUT" "$EMUBIN" --bootinfo "$blob" --log "$gl" "$AGGUEST" </dev/null >"$go" 2>/dev/null; rc=$?
        if [ $rc -eq 124 ]; then ok=0; reason="timed out"; break
        elif [ $rc -ne 0 ]; then ok=0; reason="the emulator exited with $rc"; break
        elif ! diff -q "$eo" "$go" >/dev/null 2>&1; then ok=0; reason="observations did not match the reference"; break
        elif [ -f "$el" ] && ! diff -q "$el" "$gl" >/dev/null 2>&1; then ok=0; reason="the log did not match the reference"; break; fi
    done
    [ $ok -eq 1 ] && ck pass "$id" "$req" || ck fail "$id" "$req" "$reason" "scenario=$scen seed=$last"
}

echo "## randomized checks ($SEEDS random inputs each)"
RAND="
log_one|Part II: a single random message round-trips through the device
log_many|Part II: many random messages round-trip in order
regs|Part II: the device registers behave under random access
nop|Part II: a NOP command is accepted and changes nothing
flush|Part II: the flush command persists the log
stat|Part II: the record count stays correct under random load
malformed|Part II: malformed commands are rejected
boundary|Part II: messages at length boundaries are handled
recover|Part II: the device keeps working after a rejected command
interleave|Part II: interleaved commands are handled in order
"
while IFS='|' read -r id req; do [ -n "$id" ] && run_oracle "$id" "$req"; done <<< "$RAND"
echo

echo "## edge-case checks ($SEEDS random inputs each)"
EDGE="
empty|Part II: an empty (zero-length) message is handled
maxlen|Part II: a maximum-length message is recorded in full
level_oob|Part II: an out-of-range severity level is handled safely
binsafe|Part II: arbitrary binary bytes in a message are preserved
"
while IFS='|' read -r id req; do [ -n "$id" ] && run_oracle "$id" "$req"; done <<< "$EDGE"
echo

# --- Part III: virtio virtqueue (staged happy path, then adversarial) -------
echo "## virtqueue checks"
run_vq() {  # run_vq <id> <stage> <requirement> <seeds>
    local id="$1" stage="$2" req="$3" n="$4" i seed rc ok=1 reason="" last=""
    if [ $vq_ok -ne 1 ]; then ck fail "$id" "$req" "virtio.c did not build"; return; fi
    for i in $(seq 1 "$n"); do
        seed=$RANDOM; last="$seed"
        timeout "$TIMEOUT" "$VQBIN" "$stage" "$seed" "$WORK/vq.log" </dev/null >/dev/null 2>&1; rc=$?
        if [ $rc -eq 124 ]; then ok=0; reason="timed out (an infinite chain walk?)"; break
        elif [ $rc -ne 0 ]; then ok=0; reason="the virtqueue result did not match the reference"; break; fi
    done
    [ $ok -eq 1 ] && ck pass "$id" "$req" || ck fail "$id" "$req" "$reason" "stage=$stage seed=$last"
}
run_vq translate translate "Part III: guest-physical addresses translate to host correctly" 1
run_vq single single "Part III: a single-descriptor request is served" "$SEEDS"
run_vq chained chained "Part III: a chained-descriptor request is served" "$SEEDS"
run_vq indirect indirect "Part III: an indirect descriptor table is served" "$SEEDS"

run_adv() {  # run_adv <id> <scen> <requirement>   (deterministic hostile ring)
    local id="$1" scen="$2" req="$3" out rc
    if [ $adv_ok -ne 1 ]; then ck fail "$id" "$req" "the adversarial harness did not build with virtio.c"; return; fi
    out="$(ASAN_OPTIONS=detect_leaks=0 timeout 15 "$ADVBIN" "$scen" "$WORK/adv.log" </dev/null 2>&1)"; rc=$?
    if [ $rc -eq 0 ]; then ck pass "$id" "$req"
    elif [ $rc -eq 124 ]; then ck fail "$id" "$req" "timed out (an unbounded chain walk)" "scen=$scen"
    elif printf '%s' "$out" | grep -q 'AddressSanitizer\|runtime error'; then
        ck fail "$id" "$req" "a malicious guest could crash the hypervisor" "$(printf '%s' "$out" | grep -m1 -E 'AddressSanitizer|runtime error')"
    else ck fail "$id" "$req" "the malformed ring was not handled" "scen=$scen"; fi
}
ADV="
cyclic|Part III: a cyclic descriptor chain cannot hang the hypervisor
head_oob|Part III: an out-of-range descriptor head is rejected
next_oob|Part III: an out-of-range next pointer is rejected
overlong|Part III: an over-long descriptor chain cannot run away
straddle|Part III: a descriptor straddling guest memory is rejected
gap_indirect|Part III: an indirect table in unmapped memory is rejected
writable_leak|Part III: a writable descriptor cannot leak host memory
"
while IFS='|' read -r id req; do [ -n "$id" ] && run_adv "$id" "$id" "$req"; done <<< "$ADV"
echo

echo "-----------------------------------"
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
