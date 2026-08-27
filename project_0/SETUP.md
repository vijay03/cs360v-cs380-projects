# Project 0

Project 0's parts run in two places. Parts 1 to 3 run in an Ubuntu 24.04 VM under
QEMU; Part 4 runs in Docker directly on your own machine. Set up whichever you
need for the part you are on, or do both up front.

---

# Parts 1-3: an Ubuntu VM under QEMU

Everything for Parts 1 to 3 happens inside this VM, over SSH. Use it even if you
already run Linux: it gives everyone the same environment, with the kernel
features Project 2 needs.

## 1. Install QEMU

- **Linux:** `sudo apt-get install qemu-system-x86 qemu-utils`
- **macOS:** `brew install qemu`
- **Windows:** run the installer from https://qemu.weilnetz.de/w64/, then add
  `C:\Program Files\qemu` to your `PATH`.

Need version 7.2 or newer (`qemu-system-x86_64 --version`).

## 2. Get the Ubuntu image

Download the **Ubuntu Server 24.04 LTS** ISO from
https://ubuntu.com/download/server (Server, not Desktop; the ARM64 ISO on Apple
Silicon).

## 3. Create the VM disk

```bash
qemu-img create -f qcow2 ubuntu.qcow2 20G
```

## 4. Install Ubuntu

Boot the installer; a QEMU window opens. On Windows, put the whole command on one
line (drop the `\`).

```bash
qemu-system-x86_64 -machine accel=hvf:kvm:whpx:tcg -m 4096 -smp 2 \
  -drive file=ubuntu.qcow2,if=virtio \
  -cdrom ubuntu-24.04.4-live-server-amd64.iso -boot d
```

`invalid accelerator ...` on stderr is normal: QEMU is skipping the options your
OS doesn't have.

The installer runs in the terminal. Use the default on every screen (press *Done* / *Continue* as offered), except:

- **"Update to the new installer"** (if it appears) — choose *Continue without
  updating*.
- **Guided storage configuration** — leave *Use an entire disk* selected. Press
  *Done*, *Done* again on the summary, then *Continue* to confirm erasing the disk.
- **Profile setup** — enter your name, a server name, a **username**, and a
  password. Note the username: you log in with it over SSH in step 5.
- **SSH Setup** — highlight *Install OpenSSH server* and press **Space** to check
  it, then *Done*.
- **Featured server snaps** — leave everything unselected, *Done*.
- **Installation complete** — choose *Reboot Now*.

When it reboots you land back on the installer's first screen: the VM booted the
ISO again, not your new system. That is expected — close the QEMU window. Step 5
boots the installed system (its command has no `-cdrom`).

## 5. Run the VM and connect

Boot the installed VM headless (no window opens). Run this in a terminal and leave it there: it holds the running VM and prints nothing. To stop the VM later, use step 7.

```bash
qemu-system-x86_64 -machine accel=hvf:kvm:whpx:tcg -m 4096 -smp 2 \
  -drive file=ubuntu.qcow2,if=virtio \
  -netdev user,id=n,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=n \
  -display none
```

Then open a **new terminal window** and connect to the VM over SSH (wait 30-60
seconds after boot for it to accept connections):

```bash
ssh -p 2222 <username>@localhost
```

`<username>` is the account you created in step 4. Run this again in another
terminal for each extra shell you want.

Troubleshooting:

- **`ssh` is refused.** The VM is probably still booting — wait and retry. If it
  never connects, drop `-display none` from the boot command to watch the
  console.
- **`WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!`** You reinstalled the VM,
  so it has new SSH host keys. Clear the old one and reconnect:
  `ssh-keygen -R '[localhost]:2222'`.

## 6. Install the toolchain

In an SSH session into the QEMU VM:

```bash
sudo apt-get update && sudo apt-get install -y git
git clone <course-repo-url>
cd <repo>/project_0
sudo ./setup/setup-vm.sh
```

`setup-vm.sh` installs the compilers, libraries, QEMU, and kraftkit the projects
need. Run it once. This clone is where you do Parts 1-3; edit the files over SSH
(see "Editing the files" in [README.md](README.md)).

## 7. Shut the VM down

From an SSH session:

```bash
sudo poweroff
```

The VM stops and the terminal running QEMU exits. Boot it again with the step 5
command whenever you come back.

---

# Part 4: Docker (on your own machine)

Part 4 does **not** run in the VM. It runs in a Docker container directly on your
machine, so set Docker up there.

You probably do not want the QEMU VM and Docker running at the same time,
especially on a laptop: on macOS and Windows, Docker Desktop is itself a VM, so
you would be running two. Shut the VM down (step 7 above) before working on
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

Clone the repo on your own machine too (you cloned it in the VM in step 6 for
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
