#!/usr/bin/env bash
#
# Part 4: test the Docker container from SETUP.md ("Part 4"). It must already be
# running (`docker compose up -d --build` from ../setup); this script only tests
# it -- compiles and runs your program inside, checks a published port reaches
# the host. It does not start or stop the container.
#
# Run on your own machine (not the VM), from this directory:  ./run.sh
#
# `docker compose exec` gets -T so output pipes back cleanly to the script.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

PORT=8004
ID_RE='^[0-9a-f]{12}$'      # a Docker short container id
out=""

# The compose file lives in ../setup; -f keeps it working from here.
DC=(docker compose -f "$HERE/../setup/compose.yaml")

# Write this environment's results next to make-submission.py (the parent dir).
# Always written (submit what you have); make-submission.py merges it with the
# VM's Parts 1-3 file.
DOCKERRESULTS="${DOCKERRESULTS:-$HERE/../cs360v-p0-docker.json}"
write_results() {  # write_results <true|false>
    command -v python3 >/dev/null 2>&1 || return 0
    P4_OUT="$out" P4_OK="$1" python3 - "$DOCKERRESULTS" <<'PY'
import json, os, sys
E = os.environ
json.dump({"project": "project_0", "env": "docker",
           "parts": {"part4_docker": {"output": E.get("P4_OUT", ""),
                                      "ok": E.get("P4_OK", "false") == "true"}}},
          open(sys.argv[1], "w"), indent=2)
PY
}
fail() { echo; echo "FAIL: $*"; write_results false; exit 1; }

command -v docker >/dev/null 2>&1 \
    || fail "docker not found. Install it first (see SETUP.md, 'Part 4')."

docker info >/dev/null 2>&1 \
    || fail "cannot reach the Docker daemon. On macOS/Windows, launch Docker
      Desktop and wait for it to report 'running'; on Linux, start the docker
      service."

"${DC[@]}" exec -T dev true 2>/dev/null \
    || fail "the 'dev' container is not running. Start it first (see SETUP.md):
      cd ../setup && docker compose up -d --build"

echo "== Project 0, Part 4: Docker =="
echo

# Build to /tmp, not the mounted /work, so nothing lands on your machine.
echo "compiling hello.c inside the container..."
"${DC[@]}" exec -T dev gcc -O2 -Wall -o /tmp/hello /work/hello.c \
    || fail "hello.c did not compile inside the container."

# --- 1. run it in the container: it should print the container id ------------
echo
echo "1. running your program inside the container"
out="$("${DC[@]}" exec -T dev /tmp/hello 2>&1 | tr -d '\r[:space:]')"
echo "        your output: ${out:-<none>}"
printf '%s' "$out" | grep -qE "$ID_RE" \
    || fail "expected a 12-char container id, got \"$out\" (call gethostname)."

# --- 2. reach a published port from the host ----------------------------------
# The server sends the same container id; connect from this machine and check it.
echo
echo "2. connecting from this machine to a port published by the container"
"${DC[@]}" exec -T -d dev /tmp/hello --serve "$PORT" \
    || fail "could not start the server inside the container."

line=""
for _ in $(seq 1 20); do
    if exec 3<>"/dev/tcp/127.0.0.1/$PORT" 2>/dev/null; then
        IFS= read -r line <&3 || true
        exec 3<&- 3>&- 2>/dev/null
        [ -n "$line" ] && break
    fi
    sleep 0.5
done
line="$(printf '%s' "$line" | tr -d '\r[:space:]')"
echo "        received on port $PORT: ${line:-<nothing>}"

[ -n "$line" ] \
    || fail "nothing came back on localhost:$PORT. The container is up but its
      published port is not reachable -- is something else using port $PORT?"
[ "$line" = "$out" ] && printf '%s' "$line" | grep -qE "$ID_RE" \
    || fail "the published port answered with \"$line\", not the container id \"$out\"."

write_results true
echo
echo "-----------------------------------"
echo "PASS  Docker works on this machine: your program ran in the container"
echo "      and its published port was reachable from here."
echo "Wrote $DOCKERRESULTS  (for your submission -- see README.md)."
