# Builds the Project 1 autograder test guests.
#
# Each t_*.c is a bare-metal guest sharing the entry stub and vlog library from
# ../guest. Build flags match ../guest/Makefile.
#
# The guests are x86-64 flat binaries no matter the host arch (Unicorn always
# emulates x86-64), so on a non-x86 host build them with the x86-64 cross
# toolchain -- setup-vm.sh installs it (gcc-x86-64-linux-gnu) on arm64. The
# oracle is an ordinary host program, so it keeps the native compiler (HOSTCC).
GUEST_TRIPLE :=
ifeq ($(filter x86_64 amd64,$(shell uname -m)),)
GUEST_TRIPLE := x86_64-linux-gnu-
ifeq ($(shell command -v $(GUEST_TRIPLE)gcc),)
$(error $(GUEST_TRIPLE)gcc not found: the x86-64 test guests need the cross toolchain on this host (sudo apt-get install gcc-x86-64-linux-gnu))
endif
endif

CC      := $(GUEST_TRIPLE)gcc
LD      := $(GUEST_TRIPLE)ld
OBJCOPY := $(GUEST_TRIPLE)objcopy
HOSTCC  := gcc
GUEST   := ../guest

CFLAGS  := -ffreestanding -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only \
           -fno-stack-protector -fno-asynchronous-unwind-tables \
           -fcf-protection=none -Wall -Wextra -O2 -I. -I$(GUEST)

# VMM micro-tests (m_*) + device tests (t_*). m_boot is standalone assembly
# (its own _start, no stack, no vlog) so it links WITHOUT the common objects.
TESTS   := m_boot m_stack m_ram m_exit m_fault \
           t_discovery t_log_basic t_log_levels t_repeated t_badcmd t_stat
BINS    := $(addsuffix .bin, $(TESTS))
COMMON  := start.o vlog.o

# Host build flags for the oracle (the randomized-input + expected-output model).
HFLAGS  := -std=c11 -Wall -Wextra -O2 -I$(GUEST)

all: $(BINS) oracle ag_guest.bin

# The oracle is a HOST program (not a guest): it generates a random boot blob and
# the expected observations/log for a scenario+seed, so a device cannot hardcode.
oracle: oracle.c bootinfo.h $(GUEST)/vlog.h
	$(HOSTCC) $(HFLAGS) -o $@ oracle.c

# ag_guest is a parameterised bare-metal guest that replays the oracle's blob.
ag_guest.o: ag_guest.c bootinfo.h $(GUEST)/vlog.h
	$(CC) $(CFLAGS) -c -o $@ $<

start.o: $(GUEST)/start.S
	$(CC) $(CFLAGS) -c -o $@ $<

vlog.o: $(GUEST)/vlog.c $(GUEST)/vlog.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c tobs.h $(GUEST)/vlog.h
	$(CC) $(CFLAGS) -c -o $@ $<

# m_boot / m_fault: standalone assembly guests with their own entry, no stack,
# linked alone.
m_boot.o: m_boot.S
	$(CC) $(CFLAGS) -c -o $@ $<
m_boot.elf: m_boot.o $(GUEST)/link.ld
	$(LD) -nostdlib -static --no-warn-rwx-segments -T $(GUEST)/link.ld -o $@ m_boot.o
m_fault.o: m_fault.S
	$(CC) $(CFLAGS) -c -o $@ $<
m_fault.elf: m_fault.o $(GUEST)/link.ld
	$(LD) -nostdlib -static --no-warn-rwx-segments -T $(GUEST)/link.ld -o $@ m_fault.o

%.elf: %.o $(COMMON) $(GUEST)/link.ld
	$(LD) -nostdlib -static --no-warn-rwx-segments -T $(GUEST)/link.ld -o $@ $< $(COMMON)

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin oracle

.PHONY: all clean
