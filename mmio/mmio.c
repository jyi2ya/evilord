#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "mmio.h"
#include <string.h>

#define MMIO_RDMAP_OPTION (MAP_NORESERVE | MAP_PRIVATE)
#define MMIO_RDMAP_MADVICE (MADV_SEQUENTIAL | MADV_WILLNEED)
#define MMIO_RDMAP_FADVICE (POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED | POSIX_FADV_NOREUSE)
#define MMIO_WRMAP_OPTION (MAP_NORESERVE | MAP_SHARED)
#define MMIO_WRMAP_MADVICE (MADV_SEQUENTIAL | MADV_WILLNEED)
#define MMIO_WRMAP_FADVICE (POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED | POSIX_FADV_NOREUSE)

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
    munmap(x->buf, x->size);
    close(x->fd);
}

size_t mmread(void *buf, size_t size, MMIO *x) {
    size_t len = min(size, x->size - x->pos);
    memcpy(buf, x->buf + x->pos, len);
    x->pos += len;
    return len;
}

void mmwr_open(MMIO *x, const char *fname, size_t size) {
    x->fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (x->fd == -1)
        return;
    x->size = size;
    x->pos = 0;
    fallocate(x->fd, 0, 0, x->size);
    posix_fadvise64(x->fd, 0, x->size, MMIO_WRMAP_FADVICE);
    x->buf = mmap(NULL, x->size, PROT_WRITE, MMIO_WRMAP_OPTION, x->fd, 0);
    madvise(x->buf, x->size, MMIO_WRMAP_MADVICE);
}

void mmwr_close(MMIO *x) {
    munmap(x->buf, x->size);
    close(x->fd);
}

size_t mmwrite(void *buf, size_t size, MMIO *x) {
    size_t len = min(size, x->size - x->pos);
    memcpy(x->buf + x->pos, buf, len);
    x->pos += len;
    return len;
}
