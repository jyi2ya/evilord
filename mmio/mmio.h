#ifndef MMIO_H_
#define MMIO_H_

#include <stddef.h>

#define MMSIZE 8388608U

typedef struct {
    int fd;
    size_t size;
    size_t mapped;
    size_t pos;
    size_t bufsize;
    void *buf;
} MMIO;

void mmrd_open(MMIO *x, const char *fname, size_t size);
void mmrd_close(MMIO *x);
size_t mmread(void *buf, size_t size, MMIO *x);
void mmwr_open(MMIO *x, const char *fname, size_t size);
void mmwr_close(MMIO *x);
size_t mmwrite(void *buf, size_t size, MMIO *x);

#endif
