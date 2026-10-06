#!/usr/bin/env python3
"""ukdeploy: build and deploy the runner.c unikernel. *** YOU IMPLEMENT ***

    ukdeploy.py build [--arch ARCH]
    ukdeploy.py deploy [--name NAME] [--port PORT] [--arch ARCH]
    ukdeploy.py list [--json]
    ukdeploy.py stop (NAME | --all)
    ukdeploy.py bench [--count N] [--arch ARCH]

An "instance" is one running unikernel: a QEMU process, a port on this machine
that QEMU forwards to the runner inside it, and a log file of its console output.

The list of instances is saved in app/.ukdeploy/state.json, so `list` and `stop`
know about instances an earlier `deploy` started. load_state() returns it as a
dict shaped like this, and save_state(state) writes it back:

    {"instances": {
        "a": {"pid": 12345, "port": 9000, "image": "...", "log": "...",
              "arch": "x86_64", "started": 1700000000.0},
        ...
    }}

PROVIDED for you below: the command-line parsing, the state file helpers,
find_image(), and the QEMU settings for each architecture. You implement the
commands: building, picking a port, booting an instance and waiting until it is
ready, listing what is running, and stopping instances.
"""
import argparse
import errno
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
APP_DIR = os.environ.get("UKDEPLOY_APP", os.path.join(os.path.dirname(HERE), "app"))
STATE_DIR = os.path.join(APP_DIR, ".ukdeploy")
STATE_FILE = os.path.join(STATE_DIR, "state.json")
LOG_DIR = os.path.join(STATE_DIR, "logs")

RUNNER_PORT = 8080          # the port the runner listens on INSIDE the unikernel
PORT_RANGE = (9000, 9100)   # host ports you may hand out
READY_TIMEOUT = 60.0        # seconds to wait for the runner to answer PING

# PROVIDED. QEMU settings for each architecture. Use these values as they are:
# the unikernel does not boot with other -cpu settings.
ARCHES = {
    "x86_64": {"qemu": "qemu-system-x86_64", "cpu": "host", "fallback_cpu": "max"},
    "arm64":  {"qemu": "qemu-system-aarch64", "cpu": "max", "fallback_cpu": "max",
               "extra": ["-machine", "virt"]},
}


def host_arch():                                            # PROVIDED
    m = os.uname().machine
    return "arm64" if m in ("aarch64", "arm64") else "x86_64"


# ---- state helpers (PROVIDED) --------------------------------------------

def load_state():
    try:
        with open(STATE_FILE) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {"instances": {}}


def save_state(state):
    os.makedirs(STATE_DIR, exist_ok=True)
    tmp = STATE_FILE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(state, f, indent=2)
    os.replace(tmp, STATE_FILE)      # atomic: a crash cannot truncate state


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError as e:
        return e.errno == errno.EPERM


def find_image(arch):
    """The bootable image kraft produced, or None. (PROVIDED)"""
    build = os.path.join(APP_DIR, ".unikraft", "build")
    if not os.path.isdir(build):
        return None
    suffix = f"_qemu-{arch}"
    for f in sorted(os.listdir(build)):
        # the bootable image has no extension; skip .dbg/.bootinfo/.cmd/...
        if f.endswith(suffix) and os.path.isfile(os.path.join(build, f)):
            return os.path.join(build, f)
    return None


# ---- what you implement ---------------------------------------------------

def reap_dead(state):
    """TODO(student) Part II: remove instances whose QEMU process has exited.

    For each entry in state["instances"], call alive(entry["pid"]). If it
    returns False, delete that entry. Return the state.

    Do not delete from a dict while looping over it: collect the dead names
    first (e.g. in a list), then delete them."""
    return state


def port_free(port):
    """TODO(student) Part II: return True if nothing is using `port` right now.

    Try to bind a TCP socket to it:

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(("127.0.0.1", port))

    If bind() raises OSError, the port is in use: return False. Otherwise
    return True. SO_REUSEADDR makes a port that was just released by a stopped
    instance count as free."""
    return True


def pick_port(state):
    """TODO(student) Part II: return a free port for a new instance, or None.

    Try each port in range(*PORT_RANGE) (9000 to 9099) and return the first one
    that is:
      - not the "port" of any entry in state["instances"], and
      - free according to port_free().
    Return None if no port qualifies."""
    return None


def ping(port, timeout=2.0):
    """TODO(student) Part II: return True if the runner on `port` answers PING.

      1. Connect: socket.create_connection(("127.0.0.1", port), timeout=timeout).
      2. Send b"PING\\n" with sendall().
      3. Read the reply with recv(64). Return True if it contains b"PONG".

    If any step raises OSError (refused, timed out, closed), return False."""
    return False


def wait_ready(port, pid, timeout=READY_TIMEOUT):
    """TODO(student) Part II: wait until the instance answers PING.

    Loop until `timeout` seconds have passed (use time.time()):
      - if alive(pid) is False, QEMU has exited: return False;
      - if ping(port) is True, return True;
      - otherwise time.sleep(0.3) and try again.
    Return False if the time runs out."""
    return False


def cmd_build(args):
    """TODO(student) Part I: build the unikernel image. Do this one first.

    Do these steps in order:

      1. Check that kraft is installed. If shutil.which("kraft") returns None,
         print an error and return 1.
      2. Run this command inside the app directory, APP_DIR:

             kraft build --plat qemu --arch <arch> --no-update --no-prompt -j <ncpu>

         Pass the command to subprocess.run() as a list of strings, one per
         word, and set cwd=APP_DIR so it runs inside the app directory:

             argv = ["kraft", "build", "--plat", "qemu", "--arch", args.arch,
                     "--no-update", "--no-prompt", "-j", str(os.cpu_count())]
             result = subprocess.run(argv, cwd=APP_DIR)

         args.arch is the architecture from the command line ("x86_64" or
         "arm64"). Every item must be a string, so convert os.cpu_count() with
         str().
      3. If result.returncode is not 0, the build failed: print an error and
         return 1.
      4. Find the image with find_image(args.arch). If it returns None, the
         build produced no image: print an error and return 1.
      5. Print the image's path and return 0.

    Your return value becomes the exit status of `./deploy/ukdeploy.py build`.
    To check it, run that command: on success, the image is under
    app/.unikraft/build/. SPEC.md Part I explains why the two flags are needed."""
    print("TODO: implement build")
    return 1


def cmd_deploy(args):
    """TODO(student) Part II: start one instance and wait until it is ready.

    On any failure below, print an error (to stderr) and return 1.

      1. state = reap_dead(load_state()).
      2. img = find_image(args.arch). If it is None, tell the user to run
         `ukdeploy.py build` first.
      3. Pick the name: args.name, or if it is None, make one up that is not in
         state["instances"] (e.g. "runner1", "runner2", ...). If the name is
         already in state["instances"], fail.
      4. Pick the port. If args.port is given: fail if port_free(args.port) is
         False, otherwise use it. If not given: use pick_port(state), and fail
         if it returns None.
      5. Build the QEMU command as a list of strings. With spec =
         ARCHES[args.arch]:

             [spec["qemu"], "-kernel", img, "-nographic", "-no-reboot",
              "-m", "128M", <kvm and cpu>, <extra>,
              "-netdev", f"user,id=n0,hostfwd=tcp::{port}-:{RUNNER_PORT}",
              "-device", "virtio-net-pci,netdev=n0"]

         <kvm and cpu>: if args.arch == host_arch() and
         os.access("/dev/kvm", os.R_OK | os.W_OK) is True, use
         "-enable-kvm", "-cpu", spec["cpu"]; otherwise just
         "-cpu", spec["fallback_cpu"].
         <extra>: the items in spec.get("extra", []) (arm64 has some).
      6. Start QEMU with its output going to a log file:

             os.makedirs(LOG_DIR, exist_ok=True)
             log_path = os.path.join(LOG_DIR, f"{name}.log")
             log = open(log_path, "wb")
             proc = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT,
                                     stdin=subprocess.DEVNULL,
                                     start_new_session=True)

      7. If wait_ready(port, proc.pid) is False: stop QEMU with
         os.killpg(os.getpgid(proc.pid), signal.SIGKILL), print the log_path so
         the user can look, and fail. Do not save it.
      8. Save it: state["instances"][name] = {"pid": proc.pid, "port": port,
         "image": img, "log": log_path, "arch": args.arch,
         "started": time.time()}, then save_state(state).
      9. Print a line containing the name and `port=<port>`, for example:
             deployed a  port=9000  pid=12345
         and return 0.

    bench (Part IV) also boots instances, so you may want to put steps 5 to 7
    in a helper function that both can call."""
    print("TODO: implement deploy")
    return 1


def cmd_bench(args):
    """TODO(student) Part IV: measure how long a new instance takes to become
    ready.

    On any failure below, print an error (to stderr) and return 1.

      1. img = find_image(args.arch). If it is None, tell the user to run
         `ukdeploy.py build` first.
      2. Repeat args.count times:
           - pick a port with pick_port(load_state());
           - note start = time.time(), start QEMU and wait_ready() exactly as
             cmd_deploy does (steps 5 to 7), then record time.time() - start;
           - stop that QEMU: os.killpg(os.getpgid(proc.pid), signal.SIGKILL),
             then proc.wait().
         Do not save these instances in the state.
      3. Print these lines, using os.path.getsize(img) for the size and the
         sorted times for the median and maximum, then return 0:

             image      <path to the image>
             image_size <size in bytes>
             runs       <args.count>
             ready_p50  <median time, in seconds>
             ready_max  <longest time, in seconds>

    If you put cmd_deploy's steps 5 to 7 in a helper function, call it here
    instead of copying them."""
    print("TODO: implement bench")
    return 1


def cmd_list(args):
    """TODO(student) Part II: show the running instances.

      1. state = reap_dead(load_state()), then save_state(state), so removed
         instances stay removed.
      2. If state["instances"] is empty, print "no instances" and return 0.
      3. Otherwise print one line per instance: its name, port, pid, whether
         ping(port) is True right now, and its log path. Return 0.

    Part IV: if args.json is True, then right after step 1 print a JSON list
    and return 0 (an empty list, [], if there are no instances). Build it from
    state["instances"], one dict per instance with its name added, and print it
    with json.dumps():

        [{"name": name, **entry} for name, entry in state["instances"].items()]
    """
    print("TODO: implement list")
    return 1


def cmd_stop(args):
    """TODO(student) Part II: stop one instance (args.name) or all (args.all).

      1. state = load_state().
      2. Decide which instances to stop: all of state["instances"] if args.all;
         otherwise just args.name. If neither is given, or args.name is not in
         state["instances"], print an error and return 1.
      3. For each one, with pid = its "pid":
           - os.killpg(os.getpgid(pid), signal.SIGTERM);
           - wait up to about 2 seconds, checking alive(pid) every 0.1 s;
           - if it is still alive, os.killpg(os.getpgid(pid), signal.SIGKILL).
         Wrap each killpg/getpgid call in try/except OSError: the process may
         have exited already.
      4. Delete each stopped instance from state["instances"], then
         save_state(state). Return 0."""
    print("TODO: implement stop")
    return 1


# ---- command line (PROVIDED) ---------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="build and deploy the runner unikernel")
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("build"); b.add_argument("--arch", default=host_arch(), choices=ARCHES)
    b.set_defaults(func=cmd_build)

    d = sub.add_parser("deploy")
    d.add_argument("--name"); d.add_argument("--port", type=int)
    d.add_argument("--arch", default=host_arch(), choices=ARCHES)
    d.set_defaults(func=cmd_deploy)

    l = sub.add_parser("list"); l.add_argument("--json", action="store_true")
    l.set_defaults(func=cmd_list)

    bn = sub.add_parser("bench")
    bn.add_argument("--count", type=int, default=3)
    bn.add_argument("--arch", default=host_arch(), choices=ARCHES)
    bn.set_defaults(func=cmd_bench)

    s = sub.add_parser("stop")
    s.add_argument("name", nargs="?"); s.add_argument("--all", action="store_true")
    s.set_defaults(func=cmd_stop)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
