/* init.c: the guest "function runner" (PROVIDED).
 *
 * A tiny static PID 1 for the QEMU VM. It logs a few records through the virtio
 * log device by writing them to /dev/hvc0. The stock virtio_console driver
 * turns each write into a descriptor chain on the TX virtqueue, which your
 * backend (virtio.c) processes. Then it powers the machine off.
 */
#include <fcntl.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    mkdir("/dev", 0755);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

    int fd = open("/dev/hvc0", O_WRONLY);
    if (fd < 0) { reboot(RB_POWER_OFF); return 1; }

    /* Same "<level> <message>" record format the MMIO device and the network
     * ingest path use: one log, one format, three transports. */
    static const char *recs[] = {
        "1 function runner started\n",
        "0 processing request 1\n",
        "2 cache miss\n",
        "3 request 1 failed\n",
    };
    for (int i = 0; i < 4; i++) {
        ssize_t n = write(fd, recs[i], strlen(recs[i]));
        (void)n;
    }
    fsync(fd);
    sync();
    sleep(1);              /* let the backend drain the queue */
    reboot(RB_POWER_OFF);
    return 0;
}
