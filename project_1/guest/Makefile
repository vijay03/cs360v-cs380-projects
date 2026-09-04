# Bare-metal guest build (provided).
#
# Produces flat binaries that the emulator loads at RAM_BASE. No libc; the
# only "syscalls" are MMIO accesses via mrdev.h.
#
# Each test_*.c is a standalone guest program with its own main(); they all
# share the start.S entry stub and the mrdev library.
#
# These guests are x86-64 for the emulator (Unicorn emulates x86-64), so on a
# non-x86 host use the x86-64 cross toolchain (setup-vm.sh installs it on arm64).
# An explicit `make CC=...` still wins; LD and OBJCOPY follow whatever CC is.
GUEST_TRIPLE :=
ifeq ($(filter x86_64 amd64,$(shell uname -m)),)
GUEST_TRIPLE := x86_64-linux-gnu-
endif
ifeq ($(origin CC),default)
CC := $(GUEST_TRIPLE)gcc
ifneq ($(GUEST_TRIPLE),)
ifeq ($(shell command -v $(CC)),)
$(error $(CC) not found: the x86-64 guests need the cross toolchain on this host (sudo apt-get install gcc-x86-64-linux-gnu))
endif
endif
endif
LD      := $(patsubst %gcc,%ld,$(CC))
OBJCOPY := $(patsubst %gcc,%objcopy,$(CC))

CFLAGS  := -ffreestanding -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only \
           -fno-stack-protector -fno-asynchronous-unwind-tables \
           -fcf-protection=none -Wall -Wextra -O2

TESTS   := test_nop test_log
BINS    := $(addsuffix .bin, $(TESTS))
COMMON  := start.o vlog.o

all: $(BINS)

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c vlog.h
	$(CC) $(CFLAGS) -c -o $@ $<

# The linker script orders sections, so object order on the command line does
# not affect where _start lands.
%.elf: %.o $(COMMON) link.ld
	$(LD) -nostdlib -static --no-warn-rwx-segments -T link.ld -o $@ $< $(COMMON)

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
