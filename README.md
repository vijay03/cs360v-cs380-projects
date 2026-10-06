# CS360V / CS380: Virtualization

Course projects for CS360V / CS380. Each project is in its own directory with a
`README.md` for what to do. The environments the projects run in (the QEMU VM and
Docker) are set up once in [`project_0/SETUP.md`](project_0/SETUP.md); the later
projects reuse them.

## Projects

- **[Project 0: Environment setup](project_0/)**: four small programs, one for
  each runtime the later projects build on (an emulated CPU, a Linux container, a
  unikernel, and a Docker container). Gets your toolchain working.
- **[Project 1: A virtual machine monitor, a device, and real virtio](project_1/)**:
  build a small VMM around an emulated CPU, give it a paravirtual logging device
  over MMIO, then rebuild that device as virtio and drive it from a real QEMU
  guest.
- **[Project 2: Minimal Container Runtime](project_2/)**: run a command inside a Linux container you build from scratch — namespaces,
a pivot into a read-only rootfs, cgroup limits, dropped capabilities, and a
seccomp filter — using the kernel features Docker also uses.
- **[Project 3: A Unikernel](project_3/)**: build a small server program into a
  Unikraft unikernel, add a kernel library of your own, and write a tool that
  deploys instances under QEMU and measures how fast they start.
