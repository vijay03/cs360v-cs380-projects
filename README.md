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

_Projects 2 to 4 will be linked here as they are released._
