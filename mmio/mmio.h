#ifndef MMIO_H_
#define MMIO_H_

#include <stdio.h>
#include <stddef.h>
#include <pthread.h>

typedef struct {
    int fd;
    size_t size;
    size_t pos;

    void *buf;
    FILE *fp;
    pthread_t tid;
    int pipefd[2];
} MMIO;


void mmrd_open(MMIO *x, const char *fname, size_t size);
void mmrd_close(MMIO *x);
size_t mmread(void *buf, size_t size, MMIO *x);
void mmwr_open(MMIO *x, const char *fname, size_t size);
void mmwr_close(MMIO *x);
size_t mmwrite(void *buf, size_t size, MMIO *x);

#endif
