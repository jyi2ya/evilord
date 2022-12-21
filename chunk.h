#ifndef CHUNK_H_
#define CHUNK_H_

#include <stddef.h>
#include "packet.h"
#include "util.h"
#include "mmio/mmio.h"

/**
 * Chunk - 实现校验与恢复功能的基本单位。
 *
 * @p - Chunk 所使用的质数
 * @data - Chunk 保存数据所使用的空间，是零长数组，共有 (p+2) * (p-1) 项，
 *         是 p+2 行 p-1 列的 Packet 矩阵。
 *
 * Chunk 分为两种，分别为 raw chunk 和 cooked chunk。raw chunk 为原始数据，两
 * 个冗余列并未包括其中。cooked chunk 同时包含原始数据和冗余列，有效数据的大小
 * 比 raw chunk 大。
 *
 * 本实现中未显示区分 raw chunk 和 cooked chunk，而是通过调整内存布局，使得
 * cooked chunk 的开头一块内存正好是其所对应的 raw chunk。这是基于如下考虑：
 * raw chunk 与 cooked chunk 之间的转换会经常进行，而两者有大量可复用的数据。
 * 为减少内存分配，降低内存管理难度以及优化性能，选择这种实现方法。
 *
 * 对于 raw chunk 来说，将 data 视为一维数组，取其前 sizeof(Packet) * p * (p-1)
 * 个字节即为原始文件的部分内容。对于 cooked chunk 来说，将 data 视为 (p+2) 行
 * 的二维数组，每行长度为 sizeof(Packet) * (p-1) 。第 i 行即为编号为 i 的磁盘中
 * 所保存的内容。
 *
 * 原始论文中，类似 Chunk 中 data 的结构被描述为 p-1 行 p+2 列的矩阵。为了获得
 * “cooked chunk 开头一块内存即为其对应的 raw chunk” 这一性质，我们对矩阵进行
 * 了一次转置。所以 data 应被解释为 p+2 行 p-1 列的矩阵。
 *
 * 在结构体中使用零长数组，是为了方便内存管理和 Chunk 复制。除此之外，此处零长
 * 数组与指针相比并无优势。
 *
 * 零长数组导致 Chunk 的大小无法在编译时知晓。所以对 Chunk 的操作均应使用指向
 * chunk 的指针进行。
 */
typedef struct Chunk {
    int p;
    int ok;
    Packet data[];
} Chunk;


Chunk *chunk_init(Chunk *chunk, int p);
size_t chunk_size(int p);
size_t chunk_data_size(int p);
Chunk *chunk_new(int p);
void check_chunk_(Chunk *chunk);

/**
 * check_chunk() - 检查 Chunk 的合法性
 *
 * 失败时退出。
 *
 * 函数会检查 Chunk 是否出错，并报告出错的是对角线还是行。
 * 该函数应该仅在 DEBUG 模式下使用，以测试算法的正确性。
 */
#define check_chunk(chunk) do { \
    if (check_chunk_(chunk) != Success) { \
        fprintf(stderr, "check_chunk() failed at line %d\n", __LINE__); \
        abort(); \
    } \
} while (0)

void read_cooked_chunk(Chunk *chunk, MMIO files[]);
void read_raw_chunk(Chunk *chunk, MMIO *file);
void write_cooked_chunk(Chunk *chunk, MMIO *files, UNUSED_PARAM int _unused[1]);
void write_cooked_chunk_to_bad_disk(Chunk *chunk, MMIO bad_disk_fp[2], int bad_disks[2]);
void write_raw_chunk(Chunk *chunk, MMIO file[1], UNUSED_PARAM int _unused[1]);
void write_raw_chunk_limited(Chunk *chunk, MMIO file[1], int limit);

/**
 * AT() - 获取 chunk 中 data 矩阵的某行某列
 * @row - 行号
 * @column - 列号
 *
 * 计算结果为左值。
 *
 * 为了方便抄论文，此处的行与列为论文中的原始矩阵的行和列，而不是实际上存储的
 * 转置后的矩阵的行和列。计算过程中，会自动完成由原始矩阵行列到实际矩阵行列的
 * 转换。
 */
#define AT(row, column) (chunk->data[(column) * (chunk->p - 1) + (row)])

/**
 * ATR() - 获取 chunk 中 data 矩阵的某行某列
 *
 * 计算结果为右值。
 *
 * 论文中临时给矩阵多加了全零的一行，以方便描述算法。其规定第 p-1 行的所有元素
 * 皆为零。此时如果使用 AT() 宏会出现非法内存访问的错误，因此应当使用 ATR() 进
 * 行计算。在 row < p-1 时，行为与 AT() 一致。
 */
#define ATR(row, column) (((row) == (chunk->p - 1)) ? 0 \
        : (chunk->data[(column) * (chunk->p - 1) + (row)]))

#endif
