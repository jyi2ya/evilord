#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "mmio.h"
#include <string.h>
#include <assert.h>

#define MMIO_RDMAP_OPTION (MAP_NORESERVE | MAP_SHARED)
#define MMIO_RDMAP_MADVICE (MADV_SEQUENTIAL | MADV_WILLNEED)
#define MMIO_RDMAP_FADVICE (POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED)
#define MMIO_WRMAP_OPTION (MAP_NORESERVE | MAP_SHARED)
#define MMIO_WRMAP_MADVICE (0)
#define MMIO_WRMAP_FADVICE (0)
#define MYBUFSIZE (128 * 1024)

#define min(x, y) ((x) < (y) ? (x) : (y))

void mmrd_open(MMIO *x, const char *fname, size_t size) {
    x->fd = open(fname, O_RDONLY);
    if (x->fd == -1)
        return;
    x->size = size;
    x->pos = 0;
    posix_fadvise64(x->fd, 0, x->size, MMIO_RDMAP_FADVICE);
    x->buf = mmap(NULL, x->size, PROT_READ, MMIO_RDMAP_OPTION, x->fd, 0);
    madvise(x->buf, x->size, MMIO_RDMAP_MADVICE);
}

void mmrd_close(MMIO *x) {
    assert(x->fd != -1);
    munmap(x->buf, x->size);
    close(x->fd);
    x->fd = -1;
}

size_t mmread(void *buf, size_t size, MMIO *x) {
    size_t len = min(size, x->size - x->pos);
    memcpy(buf, x->buf + x->pos, len);
    x->pos += len;
    return len;
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
    assert(x->fp != NULL);
    fclose(x->fp);
    x->fd = -1;
}

size_t mmwrite(void *buf, size_t size, MMIO *x) {
    return fwrite(buf, 1, size, x->fp);
}
