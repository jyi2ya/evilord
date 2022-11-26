#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "mmio.h"
#include <string.h>

#define MMIO_RDMAP_OPTION (MAP_NORESERVE | MAP_PRIVATE)
#define MMIO_WRMAP_OPTION (MAP_NORESERVE | MAP_SHARED)

#define min(x, y) ((x) < (y) ? (x) : (y))

void mmrd_open(MMIO *x, const char *fname, size_t size) {
    x->fd = open(fname, O_RDONLY);
    if (x->fd == -1)
        return;
    x->size = size;
    x->pos = 0;
    x->buf = mmap(NULL, x->size, PROT_READ, MMIO_RDMAP_OPTION, x->fd, 0);
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
    x->buf = mmap(NULL, x->size, PROT_WRITE, MMIO_WRMAP_OPTION, x->fd, 0);
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
