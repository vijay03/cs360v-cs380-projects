# Emulator build (provided). Requires libunicorn-dev and pkg-config.
#
# The student sources can be overridden to build against files living elsewhere
# (e.g. the test suite grading a submission):
#   make VMM_SRC=/path/to/vmm.c DEVICE_SRC=/path/to/device.c
CC         := gcc
# -pthread: the provided log store (logstore.c) is mutex-guarded so the MMIO and
# network ingest paths can append concurrently.
CFLAGS     := -std=c11 -Wall -Wextra -O2 -pthread -I. $(shell pkg-config --cflags unicorn)
LDLIBS     := $(shell pkg-config --libs unicorn)
VMM_SRC    ?= vmm.c
DEVICE_SRC ?= device.c
OBJS       := main.o vmm.o device.o logstore.o netlog.o

emulator: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

# vmm.o / device.o / netlog.o built from the *_SRC vars (default: in-tree).
vmm.o: $(VMM_SRC) vmm.h device.h logstore.h
	$(CC) $(CFLAGS) -c -o $@ $<
device.o: $(DEVICE_SRC) vmm.h device.h logstore.h
	$(CC) $(CFLAGS) -c -o $@ $<
netlog.o: netlog.c netlog.h logstore.h
	$(CC) $(CFLAGS) -c -o $@ $<
logstore.o: logstore.c logstore.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c vmm.h device.h logstore.h netlog.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) emulator

.PHONY: clean
