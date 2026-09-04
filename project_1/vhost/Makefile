# vhost-user virtio log backend (Part III).
#
# Your code is virtio.c. Override it to build against a file elsewhere:
#   make VIRTIO_SRC=/path/to/virtio.c
CC         := gcc
CFLAGS     := -std=c11 -Wall -Wextra -O2 -g -pthread -I. -I../emulator
VIRTIO_SRC ?= virtio.c
OBJS       := backend.o virtio.o sink.o libvhost-user.o logstore.o

vlog-backend: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

virtio.o: $(VIRTIO_SRC) virtq.h
	$(CC) $(CFLAGS) -c -o $@ $<
sink.o: sink.c virtq.h ../emulator/logstore.h
	$(CC) $(CFLAGS) -c -o $@ $<
backend.o: backend.c virtq.h libvhost-user.h
	$(CC) $(CFLAGS) -c -o $@ $<
libvhost-user.o: libvhost-user.c libvhost-user.h
	$(CC) $(CFLAGS) -w -c -o $@ $<
# the log store is shared with the emulator: one record format for every transport
logstore.o: ../emulator/logstore.c ../emulator/logstore.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) vlog-backend guest/init guest/initramfs.gz

.PHONY: clean
