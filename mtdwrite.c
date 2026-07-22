/* Minimal MTD flasher: erase whole partition then write image. Writes ONLY the given
 * /dev/mtdN char device — the kernel MTD layer clamps to that partition, so it cannot
 * touch any other partition (e.g. u-boot). Refuses if image > partition size. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* MTD ABI (avoid depending on mtd/mtd-user.h presence) */
struct mtd_info_user { uint8_t type; uint32_t flags; uint32_t size; uint32_t erasesize; uint32_t writesize; uint32_t oobsize; uint64_t padding; };
struct erase_info_user { uint32_t start; uint32_t length; };
#define MEMGETINFO _IOR('M', 1, struct mtd_info_user)
#define MEMERASE   _IOW('M', 2, struct erase_info_user)

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s <image> <mtd-char-dev e.g. /dev/mtd5>\n", argv[0]); return 2; }
    int fi = open(argv[1], O_RDONLY);
    if (fi < 0) { perror("open image"); return 1; }
    int fd = open(argv[2], O_RDWR);
    if (fd < 0) { perror("open mtd"); return 1; }
    struct mtd_info_user mi;
    if (ioctl(fd, MEMGETINFO, &mi) != 0) { perror("MEMGETINFO"); return 1; }
    off_t isz = lseek(fi, 0, SEEK_END); lseek(fi, 0, SEEK_SET);
    printf("mtd %s: size=%u erasesize=%u writesize=%u ; image=%ld bytes\n",
           argv[2], mi.size, mi.erasesize, mi.writesize, (long)isz);
    if (isz <= 0 || (uint32_t)isz > mi.size) { fprintf(stderr, "ABORT: image size invalid or > partition\n"); return 1; }

    /* erase the whole partition (bounded to this mtd by the kernel) */
    struct erase_info_user ei;
    ei.length = mi.erasesize;
    for (ei.start = 0; ei.start < mi.size; ei.start += mi.erasesize) {
        if (ioctl(fd, MEMERASE, &ei) != 0) { fprintf(stderr, "MEMERASE @0x%x: ", ei.start); perror(""); return 1; }
    }
    printf("erased %u bytes\n", mi.size);

    /* write image from offset 0 */
    lseek(fd, 0, SEEK_SET);
    unsigned char buf[65536];
    unsigned long tot = 0; ssize_t n;
    while ((n = read(fi, buf, sizeof buf)) > 0) {
        unsigned char *p = buf; ssize_t left = n;
        while (left > 0) {
            ssize_t w = write(fd, p, (size_t)left);
            if (w <= 0) { perror("write"); return 1; }
            p += w; left -= w; tot += (unsigned long)w;
        }
    }
    if (n < 0) { perror("read image"); return 1; }
    fsync(fd);
    printf("wrote %lu bytes to %s. OK\n", tot, argv[2]);
    close(fi); close(fd);
    return 0;
}
