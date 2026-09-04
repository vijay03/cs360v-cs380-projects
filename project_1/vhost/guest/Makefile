# The QEMU guest: a static PID 1 in a tiny initramfs (PROVIDED).
KERNEL ?= $(firstword $(wildcard /boot/vmlinuz-*-generic) /boot/vmlinuz)

initramfs.gz: init
	rm -rf root && mkdir -p root/dev && cp init root/init
	cd root && find . -print0 | cpio --null -o -H newc --quiet | gzip > ../initramfs.gz
	@echo "initramfs.gz built (kernel will be: $(KERNEL))"

init: init.c
	$(CC) -static -O2 -w -o $@ $<

clean:
	rm -rf root init initramfs.gz

.PHONY: clean
