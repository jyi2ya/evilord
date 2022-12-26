#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <sys/stat.h>
#include <linux/limits.h>
#include <dirent.h>

#include "spsc/spsc.h"
#include "mmio/mmio.h"

#include "packet.h"
#include "util.h"
#include "chunk.h"
#include "repair.h"
#include "metadata.h"

#define QUEUEMAXSIZE 6124

static char *simple_hash(char *str) {
    for (int i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '/') {
            str[i] = i + 'A';
        }
    }
    return str;
}
typedef void (*Writer)(Chunk *, MMIO *, int *);

struct ReadCtx;

typedef struct WriteCtx {
    void (*repair)(Chunk *, int, int);
    int i, j;
    Writer writer;
    SpscQueue *dirty_chunks;
    SpscQueue *clean_chunks;
    MMIO *files;
    int *option;
    size_t times;
    struct ReadCtx *peer;
} WriteCtx;

typedef void (*Reader)(Chunk *, MMIO *);

typedef struct ReadCtx {
    void (*repair)(Chunk *, int, int);
    int i, j;
    Reader reader;
    SpscQueue *dirty_chunks;
    SpscQueue *clean_chunks;
    MMIO *files;
    size_t times;
    struct WriteCtx *peer;
} ReadCtx;

static void *read_thread(void *data) {
    ReadCtx *readctx = (ReadCtx *)data;
    size_t threshold = (readctx->dirty_chunks->mask + 1) / 2;
#ifdef PERFCNT
    size_t tot = readctx->times, repaired = 0;
#endif
    while (readctx->times != 0) {
        Chunk *chunk = SpscQueue_pop(readctx->clean_chunks);
        readctx->reader(chunk, readctx->files);
        threshold = readctx->times < threshold * 2
            ? readctx->times / 2 : threshold;
        if (SpscQueue_size(readctx->dirty_chunks) > threshold) {
            readctx->repair(chunk, readctx->i, readctx->j);
            chunk->ok = 1;
#ifdef PERFCNT
            repaired += 1;
#endif
        } else {
            chunk->ok = 0;
        }
        SpscQueue_push(readctx->dirty_chunks, chunk);
        readctx->times -= 1;
    }
#ifdef PERFCNT
    fprintf(stderr, "read thread repaired chunks: %zu%%(%zu/%zu)\n", repaired * 100 / tot, repaired, tot);
#endif
    pthread_exit(NULL);
}

static void *write_thread(void *data) {
    WriteCtx *writectx = (WriteCtx *)data;
#ifdef PERFCNT
    size_t tot = writectx->times, repaired = 0;
#endif
    while (writectx->times != 0) {
        Chunk *chunk = SpscQueue_pop(writectx->dirty_chunks);
        if (!chunk->ok) {
#ifdef PERFCNT
            repaired += 1;
#endif
            writectx->repair(chunk, writectx->i, writectx->j);
        }
        writectx->writer(chunk, writectx->files, writectx->option);
        SpscQueue_push(writectx->clean_chunks, chunk);
        writectx->times -= 1;
    }
#ifdef PERFCNT
    fprintf(stderr, "write thread repaired chunks: %zu%%(%zu/%zu)\n", repaired * 100 / tot, repaired, tot);
#endif
    pthread_exit(NULL);
}


static size_t disk_file_size(Metadata *x) {
    assert(x != NULL);
    size_t size = sizeof(Metadata);
    size += (x->p - 1) * sizeof(Packet) * x->full_chunk_num;
    if (x->last_chunk_data_size != 0)
        size += (x->p - 1) * sizeof(Packet);
    return size;
}

static void push_chunks_into_queue(SpscQueue *queue, Chunk *chunks, int p) {
    size_t size = chunk_size(p);
    void *c = chunks;
    for (size_t k = 0; k < queue->mask + 1; ++k) {
        chunk_init(c, p);
        SpscQueue_push(queue, c);
        c += size;
    }
}

/**
 * read_file() - 题目规定的 read 操作实现
 */
static void read_file(char *filename, const char *save_as) {
    MMIO out[1];
    MMIO in[PMAX + 2]; // FIXME: dirty hack
    int bad_disks[2] = { -1, -1 };
    int bad_disk_num = 0;

    assert(filename != NULL);
    assert(save_as != NULL);

    simple_hash(filename);

    /* 从 raid 中获取文件的 Metadata */
    Metadata meta = get_cooked_file_metadata(filename);
    size_t queue_size = MIN(meta.full_chunk_num / 2, QUEUEMAXSIZE) + 16;
    int p = meta.p;

    mmwr_open(&out[0], save_as, meta.size);

    /* 打开文件所保存的 p+2 个磁盘 */
    for (int i = 0; i < p + 2; ++i) {
        char path[PATH_MAX];
        sprintf(path, "disk_%d/%s", i, filename);
        mmrd_open(&in[i], path, disk_file_size(&meta));

        /* 跳过磁盘开头的 Metadata */
        if (in[i].fd != -1) {
            skip_metadata(&in[i]);
        } else {
            if (bad_disk_num == 2) {
                puts("File corrupted!");
                exit(0);
            }
            bad_disks[bad_disk_num++] = i;
        }
    }

    if (bad_disk_num == 0) {
        mmrd_close(&in[p]);
        mmrd_close(&in[p + 1]);
        bad_disks[0] = p;
        bad_disks[1] = p + 1;
    } else if (bad_disk_num == 1) {
        if (bad_disks[0] == p + 1) {
            mmrd_close(&in[p]);
            bad_disks[0] = p;
            bad_disks[1] = p + 1;
        } else {
            mmrd_close(&in[p + 1]);
            bad_disks[1] = p + 1;
        }
    }

    void (*repair)(Chunk *, int, int);

    int i = bad_disks[0], j = bad_disks[1];
    if (i == p && j == p + 1) {
        repair = repair_2bad_case1;
        /* 损坏的是两个保存校验值的磁盘 */
    } else if (i < p && j == p) {
        repair = repair_2bad_case2;
    } else if (i < p && j == p + 1) {
        /* 损坏的是一块原始数据磁盘，和保存对角线校验值的磁盘 */
        repair = repair_2bad_case3;
    } else { // i < p and j < p
        repair = repair_2bad_case4;
    }

    SpscQueue dirty_chunks = SpscQueue_new(queue_size);
    SpscQueue clean_chunks = SpscQueue_new(queue_size);
    Chunk *chunks = calloc(clean_chunks.mask + 1, chunk_size(p));
    push_chunks_into_queue(&clean_chunks, chunks, p);

    WriteCtx writectx = {
        .repair = repair,
        .i = i, .j = j,
        .files = out,
        .option = NULL,
        .dirty_chunks = &dirty_chunks,
        .clean_chunks = &clean_chunks,
        .writer = write_raw_chunk,
        .times = meta.full_chunk_num,
    };

    ReadCtx readctx = {
        .repair = repair,
        .i = i, .j = j,
        .files = in,
        .dirty_chunks = &dirty_chunks,
        .clean_chunks = &clean_chunks,
        .reader = read_cooked_chunk,
        .times = meta.full_chunk_num,
    };

    writectx.peer = &readctx;
    readctx.peer = &writectx;

    pthread_t rd, wr;
    pthread_create(&rd, NULL, read_thread, &readctx);
    pthread_create(&wr, NULL, write_thread, &writectx);

    pthread_join(rd, NULL);
    pthread_join(wr, NULL);

#ifdef PERFCNT
    SpscQueue_perf(&dirty_chunks, "dirty_chunks");
    SpscQueue_perf(&clean_chunks, "clean_chunks");
#endif

    if (meta.last_chunk_data_size != 0) {
        Chunk *chunk = chunk_new(p);
        read_cooked_chunk(chunk, in);
        repair(chunk, bad_disks[0], bad_disks[1]);
        write_raw_chunk_limited(chunk, out, meta.last_chunk_data_size);
        free(chunk);
    }

    SpscQueue_drop(&dirty_chunks);
    SpscQueue_drop(&clean_chunks);
    free(chunks);
}

/**
 * write_file() - 题目规定的 write 操作实现
 */
static void write_file(char *file_to_read, int p) {
    MMIO in[1];
    MMIO out[PMAX + 2]; // FIXME: dirty hack

    assert(file_to_read != NULL);
    assert(p >= 3);
    assert(p <= 101);

    /* 获取文件的 Metadata */
    Metadata meta = get_raw_file_metadata(file_to_read, p);
    size_t queue_size = MIN(meta.full_chunk_num / 2, QUEUEMAXSIZE) + 16;

    mmrd_open(&in[0], file_to_read, meta.size);

    simple_hash(file_to_read);

    /* 准备保存文件所需要的 p+2 个磁盘 */
    for (int i = 0; i < p + 2; ++i) {
        char path[PATH_MAX];
        sprintf(path, "disk_%d", i);
        mkdir(path, 0755);
        sprintf(path, "disk_%d/%s", i, file_to_read);
        errno = 0;
        mmwr_open(&out[i], path, disk_file_size(&meta));
        write_metadata(meta, &out[i]);
    }

    size_t rwnum = meta.full_chunk_num;
    if (meta.last_chunk_data_size != 0)
        rwnum += 1;

    SpscQueue dirty_chunks = SpscQueue_new(queue_size);
    SpscQueue clean_chunks = SpscQueue_new(queue_size);
    Chunk *chunks = calloc(clean_chunks.mask + 1, chunk_size(p));
    push_chunks_into_queue(&clean_chunks, chunks, p);

    WriteCtx writectx = {
        .repair = repair_2bad_case1,
        .i = p, .j = p + 1,
        .files = out,
        .option = NULL,
        .dirty_chunks = &dirty_chunks,
        .clean_chunks = &clean_chunks,
        .writer = write_cooked_chunk,
        .times = rwnum,
    };
    ReadCtx readctx = {
        .repair = repair_2bad_case1,
        .i = p, .j = p + 1,
        .files = in,
        .dirty_chunks = &dirty_chunks,
        .clean_chunks = &clean_chunks,
        .reader = read_raw_chunk,
        .times = rwnum,
    };

    writectx.peer = &readctx;
    readctx.peer = &writectx;

    pthread_t rd, wr;
    pthread_create(&rd, NULL, read_thread, &readctx);
    pthread_create(&wr, NULL, write_thread, &writectx);
    pthread_join(rd, NULL);
    pthread_join(wr, NULL);

#ifdef PERFCNT
    SpscQueue_perf(&dirty_chunks, "dirty_chunks");
    SpscQueue_perf(&clean_chunks, "clean_chunks");
#endif

    SpscQueue_drop(&dirty_chunks);
    SpscQueue_drop(&clean_chunks);
    free(chunks);
}

/**
 * repair_file() - 题目规定的 repair 操作实现
 */
static void repair_file(const char *fname, int bad_disk_num, int bad_disks_[2]) {
    MMIO in[PMAX + 2]; // FIXME: dirty hack
    MMIO out[2];
    char path[PATH_MAX];
    int bad_disks[2] = { bad_disks_[0], bad_disks_[1] };

    assert(fname != NULL);

    /* 从 raid 中读取文件的 Metadata */
    Metadata meta = get_cooked_file_metadata(fname);
    size_t queue_size = MIN(meta.full_chunk_num / 2, QUEUEMAXSIZE) + 16;

    int p = meta.p;

    if (bad_disks[0] > p + 1) {
        return;
    }

    if (bad_disks[1] > p + 1) {
        bad_disks[1] = -1;
        bad_disk_num -= 1;
    }


    int skip_disks[2] = { bad_disks[0], bad_disks[1] };
    int i, j;
    if (bad_disk_num == 0) {
        return;
    } else if (bad_disk_num == 1) {
        if (bad_disks[0] == p + 1) {
            skip_disks[1] = p;
            i = p;
            j = p + 1;
        } else {
            skip_disks[1] = p + 1;
            i = bad_disks[0];
            j = p + 1;
        }
    } else {
        i = bad_disks[0];
        j = bad_disks[1];
    }

    void (*repair)(Chunk *, int, int);

    assert(i < j);
    if (i == p && j == p + 1) {
        repair = repair_2bad_case1;
        /* 损坏的是两个保存校验值的磁盘 */
    } else if (i < p && j == p) {
        repair = repair_2bad_case2;
    } else if (i < p && j == p + 1) {
        /* 损坏的是一块原始数据磁盘，和保存对角线校验值的磁盘 */
        repair = repair_2bad_case3;
    } else { // i < p and j < p
        repair = repair_2bad_case4;
    }

    /* 打开文件所保存的 p+2 个磁盘 */
    for (int k = 0; k < meta.p + 2; ++k) {
        if (k != skip_disks[0] && k != skip_disks[1]) {
            sprintf(path, "disk_%d", k);
            mkdir(path, 0755);
            sprintf(path, "disk_%d/%s", k, fname);
            mmrd_open(&in[k], path, disk_file_size(&meta));
            skip_metadata(&in[k]);
        } else {
            in[k].fd = -1;
        }
    }

    /* 重建损坏的两个磁盘，并且打开准备写入 */
    for (int k = 0; k < bad_disk_num; ++k) {
        sprintf(path, "disk_%d", bad_disks[k]);
        mkdir(path, 0755);
        sprintf(path, "disk_%d/%s", bad_disks[k], fname);
        mmwr_open(&out[k], path, disk_file_size(&meta));
        write_metadata(meta, &out[k]);
    }

    size_t rwnum = meta.full_chunk_num;
    if (meta.last_chunk_data_size != 0)
        rwnum += 1;

    SpscQueue dirty_chunks = SpscQueue_new(queue_size);
    SpscQueue clean_chunks = SpscQueue_new(queue_size);
    Chunk *chunks = calloc(clean_chunks.mask + 1, chunk_size(p));
    push_chunks_into_queue(&clean_chunks, chunks, p);

    WriteCtx writectx = {
        .repair = repair,
        .i = i, .j = j,
        .files = out,
        .option = bad_disks,
        .dirty_chunks = &dirty_chunks,
        .clean_chunks = &clean_chunks,
        .writer = write_cooked_chunk_to_bad_disk,
        .times = rwnum,
    };
    ReadCtx readctx = {
        .repair = repair,
        .i = i, .j = j,
        .files = in,
        .dirty_chunks = &dirty_chunks,
        .clean_chunks = &clean_chunks,
        .reader = read_cooked_chunk,
        .times = rwnum,
    };

    writectx.peer = &readctx;
    readctx.peer = &writectx;

    pthread_t rd, wr;
    pthread_create(&rd, NULL, read_thread, &readctx);
    pthread_create(&wr, NULL, write_thread, &writectx);
    pthread_join(rd, NULL);
    pthread_join(wr, NULL);

#ifdef PERFCNT
    SpscQueue_perf(&dirty_chunks, "dirty_chunks");
    SpscQueue_perf(&clean_chunks, "clean_chunks");
#endif

    SpscQueue_drop(&dirty_chunks);
    SpscQueue_drop(&clean_chunks);
    free(chunks);
}

/**
 * usage() - 最无聊的函数
 */
static void usage(void) {
    printf("./evenodd write <file_name> <p>\n");
    printf("./evenodd read <file_name> <save_as>\n");
    printf("./evenodd repair <number_erasures> <idx0> ...\n");
}

/**
 * main() - 次无聊的函数
 */
int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return -1;
    }

    char* op = argv[1];
    if (strcmp(op, "write") == 0) {
        write_file(argv[2], atoi(argv[3]));
    } else if (strcmp(op, "read") == 0) {
        read_file(argv[2], argv[3]);
    } else if (strcmp(op, "repair") == 0) {
        int bad_disk_num = atoi(argv[2]);
        int bad_disks[2] = { -1, -1 };

        if (bad_disk_num > 2) {
            puts("Too many corruptions!");
            exit(0);
        }

        assert(0 <= bad_disk_num && bad_disk_num <= 2);

        for (int i = 0; i < bad_disk_num; ++i) {
            bad_disks[i] = atoi(argv[i + 3]);
        }

        if (bad_disk_num == 2) {
            if (bad_disks[0] == bad_disks[1]) {
                bad_disk_num = 1;
                bad_disks[1] = -1;
            } else if (bad_disks[0] > bad_disks[1]) {
                int tmp = bad_disks[0];
                bad_disks[0] = bad_disks[1];
                bad_disks[1] = tmp;
            }
        }

        DIR *dir = NULL;
        for (int i = 0; i < 3; ++i) {
            char path[PATH_MAX];
            sprintf(path, "disk_%d", i);
            dir = opendir(path);
            if (dir != NULL) {
                break;
            }
        }
        assert(dir != NULL);

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
                repair_file(entry->d_name, bad_disk_num, bad_disks);
        }
        closedir(dir);
    } else {
        printf("Non-supported operations!\n");
    }
    return 0;
}
