#!/usr/bin/env bash
#
# run_tests.sh: the Project 0 check for Parts 1-3 (the VM parts). It builds and
# runs them, shows what each printed, and reports pass/fail per part. Part 4
# (Docker) runs on your own machine -- see part4_docker/run.sh.
#
#   ./run_tests.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
P0="$(dirname "$HERE")"
YEAR="$(date +%Y)"

# Per-part results, filled in below and written to a JSON file at the end for
# make-submission.py to collect. Initialised so `set -u` is happy on early exit.
P1_OUT=""; P1_OK=false
P2_OUT=""; P2_OK=false
P3_OUT=""; P3_OK=false

# --- result harness ---------------------------------------------------------
# Prints human PASS/FAIL. When CS360V_RESULTS is set it ALSO appends one machine
# line per check: status<TAB>id<TAB>requirement<TAB>reason<TAB>tech.
: "${CS360V_RESULTS:=}"
PASS=0; FAIL=0
ck() {  # ck <pass|fail> <id> <requirement> [reason] [tech]
    local st="$1" id="$2" req="$3" reason="${4:-}" tech="${5:-}"
    if [ "$st" = pass ]; then
        echo "  PASS  $req"; PASS=$((PASS + 1))
    else
        echo "  FAIL  $req${reason:+  ($reason)}"; FAIL=$((FAIL + 1))
    fi
    [ -n "$CS360V_RESULTS" ] && \
        printf '%s\t%s\t%s\t%s\t%s\n' "$st" "$id" "$req" "$reason" "$tech" >> "$CS360V_RESULTS"
}

echo "== Project 0 environment check =="
echo

# --- compile: the C builds with the provided libraries only ----------------
b1=0; b2=0; berr=""
( cd "$P0/part1_unicorn"   && make >/dev/null 2>&1 ) && b1=1 \
    || berr="Part 1 (unicorn) did not build; is libunicorn-dev installed?"
( cd "$P0/part2_container" && cc -O2 -static -o hello hello.c >/dev/null 2>&1 ) && b2=1 \
    || berr="${berr:-Part 2 (container) did not build; is a static libc installed?}"
if [ $b1 -eq 1 ] && [ $b2 -eq 1 ]; then
    ck pass compile "Your C code compiles with only the provided libraries"
else
    ck fail compile "Your C code compiles with only the provided libraries" \
        "a part did not build" "$berr"
fi
echo

# --- Part 1: read the emulated CPU's result -------------------------------
echo "Part 1 - Unicorn CPU"
REQ1="Part 1: the emulated CPU runs and its result is read back"
if [ $b1 -eq 1 ]; then
    out="$(cd "$P0/part1_unicorn" && ./hello 2>&1)"
    val="$(printf '%s\n' "$out" | grep -oE '^[0-9]+$' | tail -1)"
    echo "        your output: ${val:-${out:-<none>}}"
    P1_OUT="$val"
    if [ "$val" = "42" ]; then P1_OK=true; ck pass part1_unicorn "$REQ1"
    else ck fail part1_unicorn "$REQ1" "expected 42, got \"${val:-$out}\"" "did you call uc_reg_read?"; fi
else
    ck fail part1_unicorn "$REQ1" "Part 1 did not compile"
fi
echo

# --- Part 2: PID inside the namespace ------------------------------------
echo "Part 2 - PID in a new namespace"
REQ2="Part 2: this process reports its PID inside the namespace"
if [ $b2 -eq 1 ]; then
    out="$(cd "$P0/part2_container" && unshare --user --map-root-user --pid --fork --mount ./hello 2>&1)"
    val="$(printf '%s\n' "$out" | grep -oE '^[0-9]+$' | tail -1)"
    echo "        your output: ${val:-${out:-<none>}}"
    P2_OUT="$val"
    if [ "$val" = "1" ]; then P2_OK=true; ck pass part2_container "$REQ2"
    elif [ -z "$val" ]; then ck fail part2_container "$REQ2" "it did not run" "unprivileged user namespaces may be disabled; see README.md"
    else ck fail part2_container "$REQ2" "expected 1, got \"$val\"" "print getpid(), and run it through run.sh not directly"; fi
else
    ck fail part2_container "$REQ2" "Part 2 did not compile"
fi
echo

# --- Part 3: unikernel build timestamp ----------------------------------
echo "Part 3 - Unikernel build time (this may take several seconds)"
REQ3="Part 3: the unikernel boots and prints its build date and time"
b3=0
# `</dev/null` on kraft and qemu: with a terminal on stdin each switches to an
# interactive display and its output never reaches this capture.
if command -v kraft >/dev/null 2>&1; then
    ( cd "$P0/part3_unikernel" && kraft build --plat qemu --arch x86_64 --no-update --no-prompt </dev/null >/dev/null 2>&1 ) && b3=1
fi
if [ $b3 -eq 1 ]; then
    img="$P0/part3_unikernel/.unikraft/build/hello_qemu-x86_64"
    out="$(timeout 30 qemu-system-x86_64 -kernel "$img" -nographic -no-reboot -m 64M -cpu max </dev/null 2>&1 | tr -d '\r')"
    banner="$(printf '%s' "$out" | grep -aoE '[A-Z][a-z]+ [0-9]+\.[0-9]+\.[0-9]+' | head -1)"
    [ -n "$banner" ] && echo "        (a Unikraft unikernel booted under QEMU: $banner)"
    # __DATE__ __TIME__ looks like  "Aug 27 2026 21:12:55"  (day may have a leading space)
    ts="$(printf '%s\n' "$out" | grep -aoE '[A-Z][a-z]{2} [ 0-9][0-9] [0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}' | tail -1)"
    echo "        your output: ${ts:-<none>}"
    P3_OUT="$ts"
    if [ -n "$ts" ] && printf '%s' "$ts" | grep -q " $YEAR "; then P3_OK=true; ck pass part3_unikernel "$REQ3"
    elif [ -n "$ts" ]; then ck fail part3_unikernel "$REQ3" "build year is not $YEAR" "got: $ts -- rebuild"
    else ck fail part3_unikernel "$REQ3" "no build timestamp in the output" "print __DATE__ and __TIME__"; fi
else
    ck fail part3_unikernel "$REQ3" "Part 3 did not build (kraft + QEMU?)"
fi
echo

# Part 4 (Docker) is not run here -- it runs in Docker on your own machine, which
# this VM does not have. Run it separately on your host:
#     cd part4_docker && ./run.sh        (with the container up -- see SETUP.md)
echo "Part 4 - Docker: not checked here (runs on your own machine)."
echo "        On your host:  cd part4_docker && ./run.sh"
echo

echo "-----------------------------------"
echo "$PASS passed, $FAIL failed  (Parts 1-3; check Part 4 with part4_docker/run.sh)"
if [ "$FAIL" -eq 0 ]; then
    echo "Parts 1-3 are ready. Do Part 4 on your own machine (SETUP.md 'Part 4')."
else
    echo "Fix the parts marked FAIL above (each shows what it printed and why; see README.md)."
fi

# --- write this environment's results for make-submission.py ---------------
# Always written (submit what you have). make-submission.py on your machine pulls
# this file over SSH and merges it with the Part 4 results.
VMRESULTS="${VMRESULTS:-$HOME/cs360v-p0-vm.json}"
if command -v python3 >/dev/null 2>&1; then
    export P1_OUT P1_OK P2_OUT P2_OK P3_OUT P3_OK
    python3 - "$VMRESULTS" <<'PY'
import json, os, sys
E = os.environ
parts = {}
for pid, tag in (("part1_unicorn", "P1"), ("part2_container", "P2"), ("part3_unikernel", "P3")):
    parts[pid] = {"output": E.get(tag + "_OUT", ""), "ok": E.get(tag + "_OK", "false") == "true"}
json.dump({"project": "project_0", "env": "vm", "parts": parts}, open(sys.argv[1], "w"), indent=2)
PY
    echo
    echo "Wrote $VMRESULTS  (for your submission -- see README.md, 'What to turn in')."
else
    echo "python3 not found; could not write $VMRESULTS (needed for your submission)." >&2
fi

exit $([ "$FAIL" -eq 0 ] && echo 0 || echo 1)
