# The course function runner (PROVIDED, shared by Projects 1/3/4).
#
#   make            -> ./runner        (dynamically linked, for local use)
#   make static     -> ./runner-static (for a container rootfs or an initramfs)
#
# Project 3 does NOT use this Makefile: there the same runner.c is compiled INTO
# a Unikraft unikernel by Unikraft's own build system.
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2

all: runner

runner: runner.c
	$(CC) $(CFLAGS) -o $@ $<

# An isolated container rootfs has no shared libraries, so link statically.
static: runner-static
runner-static: runner.c
	$(CC) $(CFLAGS) -static -o $@ $<

clean:
	rm -f runner runner-static

.PHONY: all static clean
