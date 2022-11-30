#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "mmio.h"
#include <string.h>

#define MMIO_RDMAP_FADVICE (POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED | POSIX_FADV_NOREUSE)
#define MMIO_WRMAP_FADVICE (0)
#define MYBUFSIZE (128 * 1024)

#define min(x, y) ((x) < (y) ? (x) : (y))

void mmrd_open(MMIO *x, const char *fname, size_t size) {
    x->fp = fopen(fname, "rb");
    if (x->fp == NULL) {
        x->fd = -1;
        return;
    }
    x->fd = fileno(x->fp);
    x->size = size;
    x->pos = 0;
    setvbuf(x->fp, NULL, _IOFBF, MYBUFSIZE);
    posix_fadvise64(x->fd, 0, x->size, MMIO_RDMAP_FADVICE);
}

void mmrd_close(MMIO *x) {
    fclose(x->fp);
    x->fd = -1;
}

size_t mmread(void *buf, size_t size, MMIO *x) {
    return fread(buf, 1, size, x->fp);
}

void mmwr_open(MMIO *x, const char *fname, size_t size) {
    x->fp = fopen(fname, "wb");
    if (x->fp == NULL) {
        x->fd = -1;
        return;
    }
    x->fd = fileno(x->fp);
    x->size = size;
    x->pos = 0;
    fallocate(x->fd, 0, 0, x->size);
    posix_fadvise64(x->fd, 0, x->size, MMIO_WRMAP_FADVICE);
    setvbuf(x->fp, NULL, _IOFBF, MYBUFSIZE);
}

void mmwr_close(MMIO *x) {
    fclose(x->fp);
    x->fd = -1;
}

size_t mmwrite(void *buf, size_t size, MMIO *x) {
    return fwrite(buf, 1, size, x->fp);
}
