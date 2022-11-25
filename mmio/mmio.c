#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "mmio.h"
#include <string.h>

#define min(x, y) ((x) < (y) ? (x) : (y))

void mmrd_open(MMIO *x, const char *fname, size_t size) {
    x->fd = open(fname, O_RDONLY);
    if (x->fd == -1)
        return;
    x->size = size;
    x->mapped = 0;
    x->pos = 0;
    x->bufsize = min(x->size - x->mapped, MMSIZE);
    x->buf = mmap(NULL, x->bufsize, PROT_READ,
            MAP_NORESERVE | MAP_POPULATE | MAP_SHARED,
            x->fd, x->mapped);
    x->mapped += x->bufsize;
}

void mmrd_close(MMIO *x) {
    munmap(x->buf, x->bufsize);
    close(x->fd);
}

void mmrd_nextmap(MMIO *x) {
    munmap(x->buf, x->bufsize);
    x->bufsize = min(x->size - x->mapped, MMSIZE);
    x->buf = mmap(NULL, x->bufsize, PROT_READ,
            MAP_NORESERVE | MAP_POPULATE | MAP_SHARED,
            x->fd, x->mapped);
    x->mapped += x->bufsize;
}

size_t mmread(void *buf, size_t size, MMIO *x) {
    size_t len = min(size, x->size - x->pos);
    size_t copied = 0;
    for (;;) {
        size_t pos = x->pos % MMSIZE;
        if (len - copied <= x->mapped - x->pos) {
            size_t part = len - copied;
            memcpy(buf + copied, x->buf + pos, part);
            x->pos += part;
            break;
        } else {
            size_t part = x->mapped - x->pos;
            memcpy(buf + copied, x->buf + pos, part);
            mmrd_nextmap(x);
            x->pos += part;
            copied += part;
        }
    }
    return len;
}

void mmwr_open(MMIO *x, const char *fname, size_t size) {
    x->fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (x->fd == -1)
        return;
    x->size = size;
    x->pos = 0;
    x->mapped = 0;
    fallocate(x->fd, 0, 0, x->size);
    x->bufsize = min(x->size - x->mapped, MMSIZE);
    x->buf = mmap(NULL, x->bufsize, PROT_WRITE,
            MAP_NORESERVE | MAP_SHARED,
            x->fd, x->mapped);
    x->mapped += x->bufsize;
}

void mmwr_close(MMIO *x) {
    munmap(x->buf, x->bufsize);
    close(x->fd);
}

void mmwr_nextmap(MMIO *x) {
    munmap(x->buf, x->bufsize);
    x->bufsize = min(x->size - x->mapped, MMSIZE);
    x->buf = mmap(NULL, x->bufsize, PROT_WRITE,
            MAP_NORESERVE | MAP_SHARED,
            x->fd, x->mapped);
    x->mapped += x->bufsize;
}

size_t mmwrite(void *buf, size_t size, MMIO *x) {
    size_t len = min(size, x->size - x->pos);
    size_t copied = 0;
    for (;;) {
        size_t pos = x->pos % MMSIZE;
        if (len - copied <= x->mapped - x->pos) {
            size_t part = len - copied;
            memcpy(x->buf + pos, buf + copied, part);
            x->pos += part;
            break;
        } else {
            size_t part = x->mapped - x->pos;
            memcpy(x->buf + pos, buf + copied, part);
            mmwr_nextmap(x);
            x->pos += part;
            copied += part;
        }
    }
    return len;
}
