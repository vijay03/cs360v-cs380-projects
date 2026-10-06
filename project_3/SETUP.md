# Project 3: Setup

You implement the five files listed in [README.md](README.md); everything else is
provided. Read [SPEC.md](SPEC.md) first. Nothing here needs root.

## Setting up your VM

You do all of Project 3 inside the Ubuntu VM from Project 0, over SSH. kraft,
QEMU, and the unikernels you deploy all run there, not on your own machine.

If you haven't set that VM up, do it now: follow the QEMU-VM part of
[Project 0's SETUP.md](../project_0/SETUP.md), then run
`sudo project_0/setup/setup-vm.sh` once inside the VM to install kraft, QEMU,
and Unikraft's build tools. Clone this repo in the VM if it isn't there already,
and work from `<repo>/project_3`.

> **Note:** If you already set up your VM for an earlier project, pull the latest
> version of this repo and run `sudo project_0/setup/setup-vm.sh` again. It now
> installs two more tools Project 3's build needs (`unzip` and `patch`); without
> them, the build fails with `unzip: command not found`. Re-running it is safe.

## Layout

```text
.
├── SPEC.md                  # what to implement, in parts (READ FIRST)
├── README.md                # overview + what you implement
├── SETUP.md                 # this file
├── runner/
│   └── runner.c             # the server program you build into the image (provided, do not edit)
├── app/
│   ├── Kraftfile            # *** YOU IMPLEMENT: what the image is made of ***
│   ├── Config.uk            # *** YOU IMPLEMENT: boolean settings for your library ***
│   ├── Makefile.uk          # *** YOU IMPLEMENT: which sources are compiled in ***
│   └── libukstats/
│       └── ukstats.c        # *** YOU IMPLEMENT: your kernel code ***
├── deploy/
│   └── ukdeploy.py          # *** YOU IMPLEMENT: build, deploy, list, stop, bench ***
└── tests/
    └── run_tests.sh         # the test suite: every check the grader runs
```

Your first build creates `app/.unikraft/` (the Unikraft core and libraries kraft
fetches, plus the build output) and `app/.ukdeploy/` (instance state and console
logs). Both are disposable: delete them and rebuild.

## Troubleshooting

- **Check the console log first.** If an instance does not start or does not
  answer, look in `app/.ukdeploy/logs/<name>.log`. Everything the unikernel
  prints goes there, including the runner's messages and your boot hook's line.
- **Slow or failing kraft builds.** If kraft spends minutes on
  `updating index`, or fails with `bubbletea: could not create cancelable
  reader`, your build command is missing `--no-update` or `--no-prompt`. See
  SPEC Part I.
- **Don't change the `-cpu` values in `ARCHES`** in `ukdeploy.py`. The unikernel
  does not boot without them.
- **Instances keep running after `ukdeploy.py` exits.** Run
  `./deploy/ukdeploy.py stop --all` when you are done, or they keep using ports
  and memory.
