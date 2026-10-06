#!/usr/bin/env bash
#
# run_tests.sh: the Project 3 test suite. It builds your unikernel from your files,
# deploys instances with your tool, invokes the runner inside, and runs the
# robustness checks -- every check the autograder runs; there are no hidden tests.
# Prints PASS or FAIL per requirement (no points; the autograder assigns those).
# Needs kraft + QEMU.
#
#   ./run_tests.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$HERE/run_tests.py"
