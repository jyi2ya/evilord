#include "packet.h"
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "mmio/mmio.h"
#include "util.h"

Chunk *chunk_init(Chunk *chunk, int p) {
    chunk->p = p;
    return chunk;
}

/**
 * chunk_size() - 计算 chunk 结构体的大小
 */
size_t chunk_size(int p) {
    size_t num = (p + 2) * (p - 1);
    return sizeof(Chunk) + sizeof(Packet) * num;
}

/**
 * chunk_data_size() - 计算 chunk 结构体内数据的大小
 */
size_t chunk_data_size(int p) {
    size_t num = (p + 2) * (p - 1);
    return sizeof(Packet) * num;
}

/**
 * chunk_new() - 新建 chunk
 *
 * 会分配内存，返回指向 Chunk 的指针，由调用者释放。
 *
 * 一旦 p 确定，则 cooked chunk 和 raw chunk 的大小就确定了。所以对 Chunk 初始
 * 化时，需要提供 p 作为参数。
 */
Chunk *chunk_new(int p) {
    Chunk *result = (Chunk *)malloc(chunk_size(p));
    assert(result != NULL);
    return chunk_init(result, p);
}

void check_chunk_(Chunk *chunk) {
    assert(chunk != NULL);

    int m = chunk->p;

    /* 检查对角线是否合法 */
    Packet S;
    PZERO(S);
    for (int t = 1; t <= m - 1; ++t)
        PXOR(S, ATR(m - 1 - t, t));
    for (int i = 0; i <= m - 2; ++i) {
        Packet S1;
        PASGN(S1, ATR(i, m + 1));
        for (int j = 0; j <= m - 1; ++j) {
            PXOR(S1, ATR(M(i - j), j));
        }
        if (S1 != S) {
            fprintf(stderr, "check chunk: diagonal %d/%d broken\n", i, m - 1);
            return;
        }
    }

    /* 检查各行异或值是否正确 */
    PZERO(S);
    for (int i = 0; i <= m - 2; ++i) {
        for (int j = 0; j <= m; ++j) {
            PXOR(S, ATR(i, j));
        }
        if (S != 0) {
            fprintf(stderr, "check chunk: row %d/%d broken\n", i, m - 1);
            return;
        }
    }
}

/**
 * write_raw_chunk_limited() - 将 raw chunk 写入文件，并且限制写入长度
 *
 * 原始文件大小经常不能被 p * (p-1) 整除，导致最后一个 chunk 通常不会全为有效
 * 数据。此函数用于处理原始文件的最后一个 chunk，仅写入有效数据的部分。有效数
 * 据的大小由调用者根据其他信息计算。
 */
void write_raw_chunk_limited(Chunk *chunk, MMIO file[1], int limit) {
    assert(chunk != NULL);
    mmwrite(chunk->data, limit, &file[0]);
}

/**
 * write_raw_chunk() - 将 raw chunk 写入文件
 */
void write_raw_chunk(Chunk *chunk, MMIO file[1], UNUSED_PARAM int _unused[1]) {
    assert(chunk != NULL);
    size_t num = chunk->p * (chunk->p - 1);
    mmwrite(chunk->data, sizeof(Packet) * num, &file[0]);
}

/**
 * read_raw_chunk() - 读取文件的部分字节到 chunk
 *
 * 该函数会尝试读取尽可能多的数据到 chunk 中。
 *
 * 原始文件大小经常不能被 p * (p-1) 整除，导致最后一个 chunk 通常不能读取到足
 * 够的数据。此处我们约定，未完全填满的 chunk 其余字节皆为 0。
 */
void read_raw_chunk(Chunk *chunk, MMIO *file) {
    assert(chunk != NULL);
    size_t num = chunk->p * (chunk->p - 1);
    size_t ok = mmread(chunk->data, sizeof(Packet) * num, &file[0]);
    memset((char *)chunk->data + ok, 0, (chunk->p - 1) * (chunk->p + 2) * sizeof(Packet) - ok);
}

/**
 * write_cooked_chunk() - 将 chunk 写入到 raid 中
 *
 * @chunk - 待写入的 chunk
 * @files - 各个磁盘的文件指针，至少有 p+2 项
 *
 * 调用者应保证 chunk 合法，以及 files[] 数组及其内文件指针有效。
 */
void write_cooked_chunk(Chunk *chunk, MMIO *files, UNUSED_PARAM int _unused[1]) {
    assert(chunk != NULL);
    int disk_num = chunk->p + 2;
    int items_per_disk = chunk->p - 1;
    Packet *data = chunk->data;
#ifdef CHECKCHUNK
    check_chunk(chunk);
#endif
    for (int i = 0; i < disk_num; ++i) {
        mmwrite(data, sizeof(Packet) * items_per_disk, &files[i]);
        data += items_per_disk;
    }
}

/**
 * write_cooked_chunk_to_bad_disk() - 将 chunk 写入到 raid 中，但是仅写入两个磁盘
 *
 * @chunk - 待写入的 chunk
 * @bad_disks - 待写入的磁盘的 id。非负整数表示需要写入，-1 表示无需处理。
 * @bad_disk_fp - 待写入的磁盘文件指针
 *
 * 调用者应保证 bad_disks[] 和 bad_disk_fp[] 合法，以及它们之间的元素一一对应。
 *
 * 用于将修复后的 chunk 写入坏掉的磁盘中。
 */
void write_cooked_chunk_to_bad_disk(Chunk *chunk, MMIO bad_disk_fp[2], int bad_disks[2]) {
    assert(chunk != NULL);
    int items_per_disk = chunk->p - 1;
    Packet *data = chunk->data;
#ifdef CHECKCHUNK
    check_chunk(chunk);
#endif
    for (int i = 0; i < 2; ++i) {
        if (bad_disks[i] != -1) {
            mmwrite(data + items_per_disk * bad_disks[i], sizeof(Packet) * items_per_disk, &bad_disk_fp[i]);
        }
    }
}

/**
 * read_cooked_chunk() - 从 raid 中读取 chunk，并且尝试修复 chunk。
 *
 * @chunk - 存放数据的 chunk
 * @files - 待读取的磁盘的文件指针。应该保证至少有 p+2 项。
 *          用 NULL 指代损坏的磁盘。
 *
 * 该函数并不会将修复的结果写回到磁盘中，且对读取到的 chunk 不合法的情况不做处理。
 */
void read_cooked_chunk(Chunk *chunk, MMIO files[]) {
    assert(chunk != NULL);
    int disk_num = chunk->p + 2;
    int items_per_disk = chunk->p - 1;
    Packet *data = chunk->data;

    for (int i = 0; i < disk_num; ++i) {
        if (files[i].fd == -1) {
            memset(data, 0, items_per_disk * sizeof(Packet));
        } else {
            size_t ok = mmread(data, sizeof(Packet) * items_per_disk, &files[i]);
            assert(ok == items_per_disk * sizeof(Packet));
        }
        data += items_per_disk;
    }
}
