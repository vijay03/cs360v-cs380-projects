#!/usr/bin/env python3
"""Project 3 grader: build the student's unikernel from their files, then drive it.

Staged: a partial submission passes the layers it got working. Everything is host-side. We copy the submission's app into a scratch
tree (so the build cannot depend on artifacts they shipped), build it FROM
SOURCE with their Kraftfile/Config.uk/Makefile.uk, then exercise their deploy
tool against the image and talk to the runner inside it over the forwarded port.

    Run it in place with no arguments (via ./run_tests.sh); paths are resolved
    relative to tests/.
"""
import json, os, re, shutil, signal, socket, subprocess, sys, tempfile, time

# Paths are computed from this script's location (tests/), so the autograder can
# run it in place with no arguments: the submission IS the project directory.
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
APP_SRC = os.path.join(ROOT, "app")                     # Kraftfile / Config.uk / Makefile.uk / libukstats
UKDEPLOY = os.path.join(ROOT, "deploy", "ukdeploy.py")  # the student's deploy tool
RUNNER_SRC = os.path.join(ROOT, "runner")               # runner.c (the app refers to ../runner)
# The deploy tool builds and boots for the host's architecture by default, so
# look for that image (x86_64 on Intel/AMD, arm64 on Apple Silicon).
ARCH = "arm64" if os.uname().machine in ("aarch64", "arm64") else "x86_64"

P, F = 0, 0
WORK = None          # scratch tree: <WORK>/app + <WORK>/runner
BUILD_OK = False


# --- result harness ---------------------------------------------------------
# Human PASS/FAIL (by requirement) to the terminal; machine TSV to CS360V_RESULTS
# if the autograder set it. No points here.
_RESULTS = os.environ.get("CS360V_RESULTS", "")

REQUIREMENTS = {
    "compile": "Your deploy tool is valid and your app builds with only the provided libraries",
    "builds_from_glue": "your Kraftfile and Makefile.uk build a unikernel image from source",
    "image_plausible": "the image is a plausible size, not empty",
    "boots_ready": "a deployed instance boots and answers PING",
    "runs_function": "the runner in the unikernel runs a function correctly",
    "log_captured": "the instance's console log is captured",
    "boot_hook": "your boot-time hook logs the boot record",
    "info_platform": "INFO reports platform=unikraft with live numbers",
    "info_counts": "the invocation counter grows with each call",
    "list_shows": "list shows a deployed instance",
    "list_json": "list --json is machine-readable with name and port",
    "multi_instance": "a second instance gets a distinct, working port",
    "stop_releases": "stop releases the instance's port",
    "bench_reports": "bench reports the image size and time-to-ready",
    "no_image_fails": "deploy fails cleanly when there is no image",
    "port_collision": "deploying onto a taken port is refused, not hung",
    "stale_state": "stale state from a dead instance is reaped",
    "duplicate_name": "a duplicate instance name is rejected",
    "crash_recovery": "an instance that fails to boot is not left recorded",
    "no_leaks": "stopping leaves no leftover QEMU processes",
}


def ck(status, cid, reason="", tech=""):
    global P, F
    ok = status == "pass"
    req = REQUIREMENTS.get(cid, cid.replace("_", " "))
    print(f"  {'PASS' if ok else 'FAIL'}  {req}" + (f"  ({reason})" if reason and not ok else ""))
    P += ok; F += (not ok)
    if _RESULTS:
        with open(_RESULTS, "a") as fh:
            fh.write("\t".join([status, cid, req, reason if not ok else "", tech]) + "\n")


def record(group, name, ok, reason=""):
    # the check bodies call record(group, name, ...); map to the harness by name.
    ck("pass" if ok else "fail", name, reason)


# ---- helpers --------------------------------------------------------------

def uk(*args, timeout=900):
    """Run the submission's deploy tool against the scratch app."""
    # The deploy tool gets the scratch app's path, but not the results file:
    # only this script records PASS/FAIL.
    env = {k: v for k, v in os.environ.items() if k != "CS360V_RESULTS"}
    env["UKDEPLOY_APP"] = os.path.join(WORK, "app")
    try:
        r = subprocess.run([sys.executable, UKDEPLOY, *args], env=env, timeout=timeout,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return r.returncode, r.stdout.decode(errors="replace")
    except subprocess.TimeoutExpired:
        return 124, "(timed out)"


def talk(port, cmd, timeout=5):
    """One request to the runner inside the unikernel."""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=timeout) as s:
            s.sendall(cmd if cmd.endswith(b"\n") else cmd + b"\n")
            return s.recv(512).decode(errors="replace").strip()
    except OSError as e:
        return f"(no answer: {e})"


def deploy(name):
    """Deploy one instance; return (port, output) with port None on failure."""
    rc, out = uk("deploy", "--name", name)
    m = re.search(r"port=(\d+)", out)
    return (int(m.group(1)) if m else None), out


def stop_all():
    uk("stop", "--all", timeout=120)


def log_path(name):
    return os.path.join(WORK, "app", ".ukdeploy", "logs", f"{name}.log")


def read_log(name):
    try:
        with open(log_path(name), "rb") as f:
            return f.read().decode(errors="replace")
    except OSError:
        return ""


def setup_workspace():
    """Copy the submission's app somewhere clean, dropping anything prebuilt."""
    global WORK
    WORK = tempfile.mkdtemp(prefix="p3grade.")
    shutil.copytree(APP_SRC, os.path.join(WORK, "app"),
                    ignore=shutil.ignore_patterns(".unikraft", ".ukdeploy",
                                                  ".config*", "*.o"))
    # the app's Makefile.uk refers to ../runner/runner.c
    if not os.path.isfile(os.path.join(RUNNER_SRC, "runner.c")):
        print(f"no runner.c in {RUNNER_SRC}", file=sys.stderr)
        sys.exit(2)
    os.symlink(RUNNER_SRC, os.path.join(WORK, "runner"))


# ---- the checks -----------------------------------------------------------

def main():
    global BUILD_OK
    setup_workspace()

    # ---- build: their Kraftfile/Makefile.uk must produce an image from source ----
    rc, out = uk("build")
    img_dir = os.path.join(WORK, "app", ".unikraft", "build")
    img = None
    if os.path.isdir(img_dir):
        for f in sorted(os.listdir(img_dir)):
            if f.endswith(f"_qemu-{ARCH}") and os.path.isfile(os.path.join(img_dir, f)):
                img = os.path.join(img_dir, f); break
    BUILD_OK = rc == 0 and img is not None

    # compile gate: the deploy tool is valid Python and the image built (using an
    # external library in the Kraftfile / ukstats.c would fail the build here).
    tool_ok = subprocess.run(
        [sys.executable, "-c", "import ast,sys; ast.parse(open(sys.argv[1]).read())", UKDEPLOY],
        capture_output=True).returncode == 0
    ck("pass" if (tool_ok and BUILD_OK) else "fail", "compile",
       "" if (tool_ok and BUILD_OK) else
       ("your deploy tool did not parse" if not tool_ok else "your app did not build"))

    record("build", "builds_from_glue", BUILD_OK,
           f"`ukdeploy build` returned {rc} and produced no image "
           f"(does Makefile.uk register the runner? does the Kraftfile declare unikraft + lwip?)")
    record("build", "image_plausible",
           bool(img) and os.path.getsize(img) > 100_000,
           "no image, or it is implausibly small")

    if not BUILD_OK:
        # Everything downstream needs an image. Report the rest as failures
        # rather than erroring out, so the submission still gets a scored run.
        for g, n in (("runner", "boots_ready"), ("runner", "runs_function"),
                     ("runner", "log_captured"), ("unikernel", "boot_hook"),
                     ("unikernel", "info_platform"), ("unikernel", "info_counts"),
                     ("deploy", "list_shows"), ("deploy", "list_json"),
                     ("deploy", "stop_releases"), ("deploy", "multi_instance"),
                     ("bench", "bench_reports"), ("robustness", "port_collision"),
                     ("robustness", "no_image_fails"), ("robustness", "duplicate_name"),
                     ("robustness", "crash_recovery"),
                     ("robustness", "stale_state"), ("robustness", "no_leaks")):
            record(g, n, False, "no image was built")
        return finish()

    stop_all()

    # ---- runner: the image really contains and serves the runner ----
    port, out = deploy("g1")
    record("runner", "boots_ready", port is not None,
           f"deploy did not report a ready instance: {out.strip()[:160]}")

    if port:
        record("runner", "runs_function", talk(port, b"RUN wordcount a b c d e") == "OK 5",
               "RUN wordcount did not return 'OK 5'")
        record("runner", "log_captured", "function runner started" in read_log("g1"),
               "the instance's console log does not contain the runner's start record")

        # ---- unikernel: their libukstats answers from inside the kernel ----
        record("unikernel", "boot_hook",
               re.search(r"unikernel booted in \d+ ms", read_log("g1")) is not None,
               "no boot record in the console log (is the uk_late_initcall registered?)")
        info = talk(port, b"INFO")
        record("unikernel", "info_platform",
               bool(re.match(r"OK platform=unikraft .*freemem=\d+.*uptime_ms=\d+.*boot_ms=\d+", info)),
               f"INFO = {info!r}; want platform=unikraft with freemem/uptime_ms/boot_ms")
        before = re.search(r"invocations=(\d+)", info)
        for _ in range(3):
            talk(port, b"RUN sum 1 2 3")
        after = re.search(r"invocations=(\d+)", talk(port, b"INFO"))
        record("unikernel", "info_counts",
               bool(before and after) and int(after.group(1)) - int(before.group(1)) == 3,
               "invocations did not increase by 3 after three RUNs "
               "(is runner_on_invoke() implemented?)")
    else:
        for g, n in (("runner", "runs_function"), ("runner", "log_captured"),
                     ("unikernel", "boot_hook"), ("unikernel", "info_platform"),
                     ("unikernel", "info_counts")):
            record(g, n, False, "no running instance")

    # ---- deploy: the lifecycle ----
    rc, lst = uk("list")
    record("deploy", "list_shows", "g1" in lst, f"`list` does not show the instance: {lst.strip()[:160]}")

    rc, js = uk("list", "--json")
    try:
        parsed = json.loads(js)
        ok_json = isinstance(parsed, list) and any(
            i.get("name") == "g1" and isinstance(i.get("port"), int) for i in parsed)
    except (ValueError, AttributeError):
        ok_json = False
    record("deploy", "list_json", ok_json, "`list --json` is not JSON with name/port entries")

    port2, _ = deploy("g2")
    record("deploy", "multi_instance",
           port2 is not None and port2 != port and talk(port2, b"PING") == "PONG",
           "a second instance did not come up on a distinct, working port")

    uk("stop", "g1", timeout=120)
    time.sleep(1)
    rc, lst = uk("list")
    freed = "g1" not in lst
    if port:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=2).close()
            freed = False           # someone is still listening
        except OSError:
            pass
    record("deploy", "stop_releases", freed,
           "after `stop`, the instance is still listed or its port still answers")

    stop_all()

    # ---- bench: cold-start measurement ----
    rc, out = uk("bench", "--count", "2", timeout=600)
    m_sz = re.search(r"image_size\s+(\d+)", out)
    m_p50 = re.search(r"ready_p50\s+([\d.]+)", out)
    record("bench", "bench_reports",
           rc == 0 and bool(m_sz) and bool(m_p50) and float(m_p50.group(1)) > 0,
           f"`bench` did not report image_size and ready_p50: {out.strip()[:160]}")

    stop_all()

    # ---- robustness (adversarial) ----
    # with no image present, deploy must fail cleanly (not hang, not traceback)
    bdir = os.path.join(WORK, "app", ".unikraft", "build")
    hidden = bdir + ".hidden"
    os.rename(bdir, hidden)
    rc, out = uk("deploy", "--name", "noimg", timeout=120)
    os.rename(hidden, bdir)
    record("robustness", "no_image_fails",
           rc != 0 and rc != 124 and "Traceback" not in out,
           f"deploying with no image should fail cleanly; rc={rc}, output={out.strip()[:120]!r}")
    stop_all()
    # a port that is already taken must be refused, not silently reused
    holder = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    holder.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    holder.bind(("127.0.0.1", 9077)); holder.listen(1)
    t0 = time.time()
    rc, out = uk("deploy", "--name", "clash", "--port", "9077", timeout=180)
    elapsed = time.time() - t0
    # It must be REFUSED, and refused promptly. Launching QEMU anyway and only
    # discovering the clash when the instance never becomes ready also "fails",
    # but it burns the whole readiness timeout; checking the port up front is the
    # behaviour we want.
    record("robustness", "port_collision", rc != 0 and elapsed < 30,
           f"deploying onto an already-bound port was not refused promptly "
           f"(rc={rc}, took {elapsed:.1f}s; check the port before booting)")
    holder.close()
    stop_all()

    # a state file pointing at a dead pid must not wedge the tool
    sdir = os.path.join(WORK, "app", ".ukdeploy")
    os.makedirs(sdir, exist_ok=True)
    with open(os.path.join(sdir, "state.json"), "w") as f:
        json.dump({"instances": {"ghost": {"pid": 999999, "port": 9099,
                                           "image": "x", "log": "x",
                                           "arch": ARCH, "started": 0}}}, f)
    rc, lst = uk("list")
    record("robustness", "stale_state", rc == 0 and "ghost" not in lst,
           "a stale instance (dead pid) is still listed; dead entries must be reaped")

    # deploying a name that already exists must be refused, not silently reused
    stop_all()
    p_dup, _ = deploy("dup")
    rc_dup, out_dup = uk("deploy", "--name", "dup", timeout=180)
    record("robustness", "duplicate_name", p_dup is not None and rc_dup != 0,
           "deploying an instance name that already exists was not refused")
    stop_all()

    # an instance killed behind the tool's back must be reaped, and its port
    # must become reusable
    p_cr, _ = deploy("crash")
    crashed_ok = False
    if p_cr:
        st = json.load(open(os.path.join(WORK, "app", ".ukdeploy", "state.json")))
        pid = st["instances"]["crash"]["pid"]
        try:
            os.killpg(os.getpgid(pid), signal.SIGKILL)
        except OSError:
            try: os.kill(pid, signal.SIGKILL)
            except OSError: pass
        time.sleep(2)
        rc, lst = uk("list")
        reaped = "crash" not in lst
        p_again, _ = deploy("after")      # the freed port must be usable again
        crashed_ok = reaped and p_again is not None
    record("robustness", "crash_recovery", crashed_ok,
           "an externally killed instance was not reaped, or its port stayed claimed")
    stop_all()

    # nothing may be left running after stop --all
    stop_all()
    time.sleep(2)                      # let signalled processes actually go
    rc, lst = uk("list")
    # Scope this to THIS grading run's scratch tree. A bare "any qemu" match
    # would also catch unrelated VMs on the machine (another grader, another
    # user) and fail a submission for someone else's process.
    leftover = subprocess.run(
        ["pgrep", "-f", re.escape(WORK)],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode().split()
    record("robustness", "no_leaks",
           not leftover and "port" not in lst,
           f"{len(leftover)} process(es) from this run survived `stop --all`")

    return finish()


def finish():
    stop_all()
    if WORK and os.path.isdir(WORK):
        shutil.rmtree(WORK, ignore_errors=True)
    print("=" * 35)
    print(f"{P} passed, {F} failed  (of {P + F})")
    return 0 if F == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
