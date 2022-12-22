#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "mmio.h"
#include <string.h>
#include <sys/sendfile.h>
#include <limits.h>

#define MMIO_RDMAP_FADVICE (POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED)
#define MMIO_WRMAP_FADVICE (0)
#define PIPE_SIZE (1024 * 1024)
#define BUF_SIZE (128 * 1024)

#define min(x, y) ((x) < (y) ? (x) : (y))

typedef struct {
    int in_fd, out_fd;
    size_t size;
} Context;

static void *copy_file_to_pipe(void *data) {
    Context *ctx = (Context *)data;
    while (ctx->size > 0) {
        size_t bytes = sendfile(ctx->out_fd, ctx->in_fd, NULL, ctx->size);
        ctx->size -= bytes;
        if (bytes == 0) {
            break;
        }
    }
    close(ctx->in_fd);
    close(ctx->out_fd);
    free(ctx);
    return NULL;
}

static void *copy_pipe_to_file(void *data) {
    Context *ctx = (Context *)data;
    while (ctx->size > 0) {
        size_t bytes = splice(ctx->in_fd, NULL, ctx->out_fd, NULL, ctx->size, SPLICE_F_MOVE | SPLICE_F_MORE);
        ctx->size -= bytes;
        if (bytes == 0) {
            break;
        }
    }
    close(ctx->in_fd);
    close(ctx->out_fd);
    free(ctx);
    return NULL;
}

void mmrd_open(MMIO *x, const char *fname, size_t size) {
    int fd = open(fname, O_RDONLY);
    if (fd == -1) {
        x->fd = -1;
        return;
    }

    posix_fadvise64(fd, 0, size, MMIO_RDMAP_FADVICE);

    x->size = size;
    x->pos = 0;

    Context *ctx = (Context *)malloc(sizeof(Context));
    pipe(x->pipefd);
    fcntl(x->pipefd[1], F_SETPIPE_SZ, PIPE_SIZE);
    ctx->in_fd = fd;
    ctx->out_fd = x->pipefd[1];
    ctx->size = size;

    pthread_create(&x->tid, NULL, copy_file_to_pipe, ctx);

    x->fd = x->pipefd[0];
    x->fp = fdopen(x->fd, "rb");
    setvbuf(x->fp, NULL, _IOFBF, BUF_SIZE);
}

void mmrd_close(MMIO *x) {
    pthread_join(x->tid, NULL);
    fclose(x->fp);
    x->fd = -1;
}

size_t mmread(void *buf, size_t size, MMIO *x) {
    size_t result = fread(buf, 1, size, x->fp);
    return result;
}

void mmwr_open(MMIO *x, const char *fname, size_t size) {
    int fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        x->fd = -1;
        return;
    }

    x->size = size;
    x->pos = 0;
    fallocate(fd, 0, 0, x->size);
    posix_fadvise64(fd, 0, x->size, MMIO_WRMAP_FADVICE);

    Context *ctx = (Context *)malloc(sizeof(Context));
    pipe(x->pipefd);
    fcntl(x->pipefd[1], F_SETPIPE_SZ, PIPE_SIZE);
    ctx->in_fd = x->pipefd[0];
    ctx->out_fd = fd;
    ctx->size = size;

    pthread_create(&x->tid, NULL, copy_pipe_to_file, ctx);

    x->fd = x->pipefd[1];
    x->fp = fdopen(x->fd, "wb");
    setvbuf(x->fp, NULL, _IOFBF, BUF_SIZE);
}

void mmwr_close(MMIO *x) {
    fclose(x->fp);
    pthread_join(x->tid, NULL);
    x->fd = -1;
}

size_t mmwrite(void *buf, size_t size, MMIO *x) {
    size_t result = fwrite(buf, 1, size, x->fp);
    return result;
}
