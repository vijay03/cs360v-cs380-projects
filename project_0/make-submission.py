#!/usr/bin/env python3
"""Build submission.json for Project 0 from both environments.

Run on your OWN machine, from project_0/ in your host clone:

    python3 make-submission.py [vm-username]

Parts 1-3 run in the VM; `tests/run_tests.sh` there writes ~/cs360v-p0-vm.json.
Part 4 runs here; `part4_docker/run.sh` writes cs360v-p0-docker.json next to this
script. This re-pulls the VM file over SSH each run (so re-running picks up a
fresh run_tests.sh) and merges the two into submission.json.

This script does NOT re-run any check -- if a part shows a warning, re-run the
check in that environment first, then run this again.

If SSH to the VM does not work, the script tells you how to copy the file across
by hand.

Overrides via env vars: VM_USER, VM_HOST (default localhost), VM_PORT (2222).
"""
import hashlib
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DOCKER_JSON = os.path.join(HERE, "cs360v-p0-docker.json")
VM_JSON = os.path.join(HERE, "cs360v-p0-vm.json")
OUT = os.path.join(HERE, "submission.json")
VM_HOST = os.environ.get("VM_HOST", "localhost")
VM_PORT = os.environ.get("VM_PORT", "2222")
PARTS = ["part1_unicorn", "part2_container", "part3_unikernel", "part4_docker"]


def die(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def load(path, hint):
    try:
        with open(path) as f:
            return json.load(f)
    except FileNotFoundError:
        die(f"Missing {os.path.basename(path)} -- {hint}")
    except json.JSONDecodeError as e:
        die(f"{os.path.basename(path)} is not valid JSON: {e}")


def fetch_vm(user):
    """ssh into the VM, cat the results file, save it locally. True on success."""
    cmd = ["ssh", "-p", VM_PORT,
           "-o", "StrictHostKeyChecking=accept-new",
           "-o", "ConnectTimeout=8",
           f"{user}@{VM_HOST}", "cat ~/cs360v-p0-vm.json"]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        die("ssh not found on this machine.")
    if r.returncode != 0 or not r.stdout.strip():
        return False
    with open(VM_JSON, "w") as f:
        f.write(r.stdout)
    return True


def main():
    # Part 4 results (local, written by part4_docker/run.sh) -- required.
    docker = load(DOCKER_JSON,
                  "run Part 4 first:  cd part4_docker && ./run.sh")

    # VM results -- always try to re-pull, so a re-run picks up a fresh
    # tests/run_tests.sh. Fall back to an existing local file only if the VM is
    # unreachable (that file is the manual copy, or a previous pull).
    user = (sys.argv[1] if len(sys.argv) > 1 else None) \
        or os.environ.get("VM_USER") \
        or input("Your VM username: ").strip()
    if not user:
        die("No VM username given.")
    print(f"Pulling Parts 1-3 results from the VM ({user}@{VM_HOST}:{VM_PORT}) ...",
          flush=True)
    if fetch_vm(user):
        print("  got it.")
    elif os.path.exists(VM_JSON):
        print(f"  could not reach the VM -- using the existing "
              f"{os.path.basename(VM_JSON)} (delete it to force a fresh pull).")
    else:
        die("\nCould not reach the VM over SSH. The VM must be running "
            "(SETUP.md step 5)\nto pull the results file.\n\n"
            "To copy it by hand instead: in your VM SSH session run\n"
            "    cat ~/cs360v-p0-vm.json\n"
            f"save that output to a file named cs360v-p0-vm.json in\n"
            f"    {HERE}\n"
            "then run this again.")

    vm = load(VM_JSON, "see the instructions above")

    parts = {}
    parts.update(vm.get("parts", {}))
    parts.update(docker.get("parts", {}))

    # Integrity hash over the four output strings (each bound to its part id,
    # sorted, newline-joined). The grader rebuilds this from the submitted
    # outputs and compares. It only detects casual edits to submission.json --
    # the hashing logic is right here, so it is not tamper-proof.
    canon = "\n".join(f"{p}={parts[p].get('output', '')}" for p in sorted(parts))
    digest = "sha256:" + hashlib.sha256(canon.encode()).hexdigest()

    with open(OUT, "w") as f:
        json.dump({"project": "project_0", "parts": parts, "output_hash": digest},
                  f, indent=2)

    print(f"\nWrote {OUT}")
    print("  parts: " + ", ".join(p for p in PARTS if p in parts))
    missing = [p for p in PARTS if p not in parts]
    if missing:
        print("  WARNING: missing " + ", ".join(missing))
    notok = [p for p in PARTS if p in parts and not parts[p].get("ok")]
    if notok:
        print("  WARNING: last check did not pass for " + ", ".join(notok))
        print("  Fix those, re-run the check in that environment, then re-run this.")
    print("\nSubmit submission.json.")


if __name__ == "__main__":
    main()
