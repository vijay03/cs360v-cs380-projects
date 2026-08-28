# Project 0

Project 0's parts run in two places. Parts 1 to 3 run in an Ubuntu 24.04 VM under
QEMU; Part 4 runs in Docker directly on your own machine. Set up whichever you
need for the part you are on, or do both up front.

---

# Parts 1-3: an Ubuntu VM under QEMU

Everything for Parts 1 to 3 happens inside this VM, over SSH. Use it even if you
already run Linux: it gives everyone the same environment, with the kernel
features Project 2 needs.

Clone the repo on your machine first (`git clone <course-repo-url>`); the boot
command below uses `setup/login-seed.iso` from it, a cloud-init seed that sets
the `ubuntu` login password to `ubuntu` and enables SSH.

## 1. Install QEMU

- **Linux:** `sudo apt-get install qemu-system-x86 qemu-utils`
- **macOS:** `brew install qemu`
- **Windows:** run the installer from https://qemu.weilnetz.de/w64/, then add
  `C:\Program Files\qemu` to your `PATH`. Run the QEMU commands below from
  PowerShell or Command Prompt, on one line (drop the `\` line breaks).

Need QEMU 7.2 or newer (`qemu-system-x86_64 --version`).

## 2. Get the Ubuntu image

Download the Ubuntu Server 24.04 cloud image from
https://cloud-images.ubuntu.com/releases/24.04/release/
(`ubuntu-24.04-server-cloudimg-amd64.img`, or the `-arm64.img` on Apple Silicon)
and save it as `ubuntu.img` in `<repo>/project_0/`. It is a pre-installed disk,
so there is no installer to run. Give it room for all the projects (cloud-init
grows the filesystem on first boot):

```bash
cd <repo>/project_0
qemu-img resize ubuntu.img 20G
```

## 3. Boot the VM and connect

Run this from `<repo>/project_0` and leave the terminal open; with `-display none`
the VM has no window and prints nothing. To stop it later, use step 6.

```bash
qemu-system-x86_64 -machine accel=hvf:kvm:whpx:tcg -m 4096 -smp 2 \
  -drive file=ubuntu.img,if=virtio \
  -drive file=setup/login-seed.iso,if=virtio,format=raw \
  -netdev user,id=n,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=n \
  -display none
```

`invalid accelerator ...` on stderr is normal.

Open a **new terminal window** and connect (the first boot runs cloud-init to set
the password and grow the disk, so give it 30 to 60 seconds):

```bash
ssh -p 2222 ubuntu@localhost        # password: ubuntu
```

The login seed is only read on the first boot; drop the `-drive file=setup/login-seed.iso,...`
line from later boots.

Troubleshooting:

- **`ssh` is refused.** The VM is probably still booting; wait and retry. If it
  never connects, drop `-display none` to watch the console.
- **`WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!`** You recreated the VM, so
  it has new SSH host keys. Clear the old one:
  `ssh-keygen -R '[localhost]:2222'`.

## 4. Install the toolchain

In an SSH session into the VM, clone the repo again and run setup (the copy on
your host was only to boot the VM; this one, inside the VM, is where you work):

```bash
sudo apt-get update && sudo apt-get install -y git
git clone <course-repo-url>
cd <repo>/project_0
sudo ./setup/setup-vm.sh
```

`setup-vm.sh` installs the compilers, libraries, QEMU, and kraftkit the projects
need, and turns on unprivileged user namespaces for Part 2. If it stops and
prints `Reboot the VM ...`, do step 5; otherwise it finished, skip to step 6.

Edit the files over SSH (see "Editing the files" in [README.md](README.md)).

## 5. Reboot and re-run setup

The cloud image often boots a cut-down kernel, so `setup-vm.sh` installs the full
one (`linux-generic`) and stops. Reboot to pick it up:

```bash
sudo reboot
```

That drops your SSH session. Wait about 30 seconds, reconnect, and run setup
again; this time it runs to completion:

```bash
ssh -p 2222 ubuntu@localhost
cd <repo>/project_0
sudo ./setup/setup-vm.sh
```

## 6. Shut the VM down

From an SSH session:

```bash
sudo poweroff
```

The VM stops and the terminal running QEMU exits. Boot it again with the step 3
command whenever you come back.

---

# Part 4: Docker (on your own machine)

Part 4 does **not** run in the VM. It runs in a Docker container directly on your
machine, so set Docker up there.

You probably do not want the QEMU VM and Docker running at the same time,
especially on a laptop: on macOS and Windows, Docker Desktop is itself a VM, so
you would be running two. Shut the VM down (step 6 above) before working on
Part 4.

## 1. Install Docker

- **macOS / Windows:** install Docker Desktop from
  https://www.docker.com/products/docker-desktop/ (it includes Compose), then
  **launch the Docker Desktop app** and wait for it to report "running". It does
  not start on its own — you launch it each session (or enable auto-start in its
  settings). On macOS, `brew install docker` gives only the CLI, not the daemon;
  use the Docker Desktop installer.
- **Linux:** `sudo apt-get install docker.io docker-compose-v2`, then
  `sudo usermod -aG docker $USER` and log out and back in so `docker` works
  without `sudo`.

Check it: `docker run --rm hello-world`. If you get "Cannot connect to the Docker
daemon", Docker Desktop is not running — launch it and wait.

## 2. Bring up the container

Clone the repo on your own machine too (you cloned it in the VM in step 4 for
Parts 1-3; Part 4's files need to be on the host). The container is defined in
`setup/`, so run `docker compose` from there:

```bash
git clone <course-repo-url>
cd <repo>/project_0/setup
docker compose up -d --build
```

The first build downloads the base image and takes a few minutes. It builds for
your architecture (arm64 or amd64) with no configuration on your part. The
`part4_docker/` directory is mounted at `/work` inside the container, so when you
edit `part4_docker/hello.c` on your machine the container sees it immediately —
no rebuild.

You do not need a shell inside the container to do Part 4, but `docker compose
exec dev bash` (from `setup/`) gives you one.

## 3. Test it

With the container up, from `part4_docker/` on your machine:

```bash
./run.sh
```

This compiles and runs your program in the container and checks that a published
port is reachable from your machine.

## 4. Shut the container down

From `setup/` on your machine:

```bash
docker compose down
```

This stops and removes the container. Nothing you did on your own machine
(including your edits to `part4_docker/hello.c`) is lost; bring it back with the
step 2 command.
