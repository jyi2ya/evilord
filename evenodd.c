#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>

#include <sys/stat.h>
#include <linux/limits.h>
#include <dirent.h>

#include "spsc/spsc.h"
#include "mmio/mmio.h"

#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
#define UNUSED_PARAM __attribute__((unused))
#define UNUSED_PARAM_RESULT __attribute__((warn_unused_result))
#else // Non-GCC or old GCC.
#define UNUSED_PARAM
#define UNUSED_PARAM_RESULT
#endif

#define QUEUESIZE 6124

char *simple_hash(char *str) {
    for (int i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '/') {
            str[i] = i + 'A';
        }
    }
    return str;
}

void panic(void) {
    abort();
}

#define unimplemented() do { \
    fprintf(stderr, "unimplemented at line %d\n", __LINE__); \
    abort(); \
} while (0)

/**
 * Packet - 进行运算的单位数据
 *
 * 本算法对数据的存储皆通过 Packet 完成。
 *
 * Packet 可以为任意类型，只要其正确实现 PXOR PZERO PASGN 三个宏。
 *
 * 目前所采用的 Packet 为 uint64_t 类型。测试发现使用 __uint128_t 类型时，性能
 * 无明显改观（在超快的集群上对性能提升仅有 5.4%），且可能引用潜在的对编译器版
 * 本的依赖的问题。故目前使用 uint64_t。
 */
typedef uint64_t Packet;

/**
 * PXOR() - 异或两个 Packet，并且保存到前者
 */
#define PXOR(x, y) do { \
    (x) ^= (y); \
} while (0)

/**
 * PZERO() - 将某个 Packet 置零
 */
#define PZERO(x) do { \
    (x) = 0; \
} while (0)

/**
 * PASGN() - Packet 赋值操作
 */
#define PASGN(x, y) do { \
    (x) = (y); \
} while (0)

/**
 * PMAX - evenodd 算法所使用的质数的最大值
 */
#define PMAX (201)

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
typedef struct {
    int p;
    int ok;
    Packet data[];
} Chunk;

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

/**
 * M() - 对 m 取模
 */
#define M(x) (((x) + m) % m)

void check_chunk(Chunk *chunk) {
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
 * check_chunk() - 检查 Chunk 的合法性
 *
 * 失败时退出。
 *
 * 函数会检查 Chunk 是否出错，并报告出错的是对角线还是行。
 * 该函数应该仅在 DEBUG 模式下使用，以测试算法的正确性。
 */
#define check_chunk(chunk) do { \
    if (check_chunk(chunk) != Success) { \
        fprintf(stderr, "check_chunk() failed at line %d\n", __LINE__); \
        abort(); \
    } \
} while (0)

/**
 * cook_chunk_r1() - 计算第一列校验值，即原始数据每行的异或值。
 */
void cook_chunk_r1(Chunk *chunk) {
    assert(chunk != NULL);
    int m = chunk->p;
    for (int l = 0; l <= m - 2; ++l)
        PZERO(AT(l, m));
    for (int t = 0; t <= m - 1; ++t) {
        for (int l = 0; l <= m - 2; ++l) {
            PXOR(AT(l, m), AT(l, t));
        }
    }
}

/**
 * cook_chunk_r2() - 计算第二列校验值，即各种对角线神奇魔法算出来的值。
 */
void cook_chunk_r2(Chunk *chunk) {
    assert(chunk != NULL);
    Packet S;
    PZERO(S);
    int m = chunk->p;
    for (int t = 1; t <= m - 1; ++t)
        PXOR(S, AT(m - 1 - t, t));
    for (int l = 0; l <= m - 2; ++l)
        PASGN(AT(l, m + 1), S);
    for (int t = 0; t <= m - 1; ++t) {
        for (int l = 0; l < t; ++l) {
            PXOR(AT(l, m + 1), ATR(m + l - t, t));
        }
        for (int l = t; l <= m - 2; ++l) {
            PXOR(AT(l, m + 1), ATR(l - t, t));
        }
    }
}

/**
 * cook_chunk() - 计算校验值。
 */
void cook_chunk(Chunk *chunk) {
    assert(chunk != NULL);
    cook_chunk_r1(chunk);
    cook_chunk_r2(chunk);
#ifdef CHECKCHUNK
    check_chunk(chunk);
#endif
}

void repair_2bad_case1(Chunk *chunk, UNUSED_PARAM int i, UNUSED_PARAM int j) {
    /* i == m && j == m + 1 */
    assert(chunk != NULL);
    cook_chunk_r1(chunk);
    cook_chunk_r2(chunk);
}

void repair_2bad_case2(Chunk *chunk, int i, UNUSED_PARAM int j) {
    /* i < m && j == m */
    assert(chunk != NULL);
    assert(i < chunk->p);
    int m = chunk->p;
    Packet S;
    int ref_diagonal = M(i - 1);
    PASGN(S, ATR(ref_diagonal, m + 1));
    for (int l = 0; l <= ref_diagonal; ++l) {
        PXOR(S, ATR(ref_diagonal - l, l));
    }
    for (int l = ref_diagonal + 1; l < m - 1; ++l) {
        PXOR(S, ATR(m + ref_diagonal - l, l));
    }
    if (ref_diagonal < m - 2)
        PXOR(S, ATR(ref_diagonal + 1, m - 1));

    // recover column i
    for (int k = 0; k < m - i && k <= m - 2; ++k) {
        PASGN(AT(k, i), S);
        PXOR(AT(k, i), ATR(i + k, m + 1));

        /* l < i <= k + i < m */
        for (int l = 0; l < i; ++l)
            PXOR(AT(k, i), ATR(k + i - l, l));
        /* i <= k + i < m */
        for (int l = i + 1; l <= m - 1; ++l)
            PXOR(AT(k, i), ATR(M(k + i - l), l));
    }
    for (int k = m - i; k <= m - 2; ++k) {
        PASGN(AT(k, i), S);
        PXOR(AT(k, i), ATR(i + k - m, m + 1));
        for (int l = 0; l < i; ++l)
            PXOR(AT(k, i), ATR(M(k + i - l), l));
        for (int l = i + 1; l <= m - 1; ++l)
            PXOR(AT(k, i), ATR(M(k + i - l), l));
    }
    cook_chunk_r1(chunk);
}

void repair_2bad_case3(Chunk *chunk, int i, UNUSED_PARAM int j) {
    /* i < m && j == m + 1 */
    assert(chunk != NULL);
    assert(i < chunk->p);
    int m = chunk->p;
    for (int k = 0; k < m - 1; ++k)
        PZERO(AT(k, i));
    for (int l = 0; l < i; ++l) {
        for (int k = 0; k < m - 1; ++k) {
            PXOR(AT(k, i), AT(k, l));
        }
    }
    for (int l = i + 1; l <= m; ++l) {
        for (int k = 0; k < m - 1; ++k) {
            PXOR(AT(k, i), AT(k, l));
        }
    }
    cook_chunk_r2(chunk);
}

void repair_2bad_case4(Chunk *chunk, int i, int j) {
    assert(chunk != NULL);
    assert(i < chunk->p);
    assert(j < chunk->p);
    int m = chunk->p;
    /* 损坏的是两块原始数据磁盘 */
    Packet S;
    PZERO(S);
    for (int l = 0; l <= m - 2; ++l) {
        PXOR(S, ATR(l, m));
        PXOR(S, ATR(l, m + 1));
    }
    // horizontal syndromes S0
    // diagonal syndromes S1
    Packet S0[PMAX];
    Packet S1[PMAX];

    for (int u = 0; u <= m - 1; ++u) {
        PZERO(S0[u]);
        PASGN(S1[u], S);
        PXOR(S1[u], ATR(u, m + 1));
    }

    for (int l = 0; l <= m; ++l) {
        if (l == i || l == j)
            continue;
        for (int u = 0; u < m - 1; ++u)
            PXOR(S0[u], AT(u, l));
    }

    if (i != 0 && j != 0) {
        for (int u = 0; u < m - 1; ++u)
            PXOR(S1[u], AT(u, 0));
    }
    for (int l = 1; l <= m - 1; ++l) {
        if (l == i || l == j)
            continue;
        for (int u = 0; u < l - 1; ++u)
            PXOR(S1[u], AT(m + u - l, l));
        for (int u = l; u < m; ++u)
            PXOR(S1[u], AT(u - l, l));
    }

    int step = j - i;
    for (int s = m - 1 - step; s != m - 1; s -= step) {
        AT(s, j) = S1[M(s + j)];
        PXOR(AT(s, j), ATR(M(s + step), i));
        AT(s, i) = S0[s];
        PXOR(AT(s, i), AT(s, j));
        s += m * (s < step);
    }
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
    int times;
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
    int times;
    struct WriteCtx *peer;
} ReadCtx;

void *read_thread(void *data) {
    ReadCtx *readctx = (ReadCtx *)data;
    size_t threshold = (readctx->dirty_chunks->mask + 1) / 2;
    while (readctx->times != 0) {
        Chunk *chunk = SpscQueue_pop(readctx->clean_chunks);
        readctx->reader(chunk, readctx->files);
        if (SpscQueue_size(readctx->dirty_chunks) > threshold) {
            readctx->repair(chunk, readctx->i, readctx->j);
            chunk->ok = 1;
        } else {
            chunk->ok = 0;
        }
        SpscQueue_push(readctx->dirty_chunks, chunk);
        readctx->times -= 1;
    }
    pthread_exit(NULL);
}

void *write_thread(void *data) {
    WriteCtx *writectx = (WriteCtx *)data;
    while (writectx->times != 0) {
        Chunk *chunk = SpscQueue_pop(writectx->dirty_chunks);
        if (!chunk->ok) {
            writectx->repair(chunk, writectx->i, writectx->j);
        }
        writectx->writer(chunk, writectx->files, writectx->option);
        SpscQueue_push(writectx->clean_chunks, chunk);
        writectx->times -= 1;
    }
    pthread_exit(NULL);
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
    memset((void *)chunk->data + ok, 0, (chunk->p - 1) * (chunk->p + 2) * sizeof(Packet) - ok);
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

/**
 * Metadata - raid 中存放的关于原始文件的元数据
 *
 * @p - 所使用的质数。不同文件存放时，可能会指定不同的质数。
 * @size - 文件长度。文件在 raid 中以 chunk 为单位存储，文件长度不一定能被其大小整除。
 * @full_chunk_num - 为存放文件，需要填满的 chunk 的数量。实际使用的 chunk 数量可能比该值多 1。
 * @last_chunk_data_size - 文件长度不能被 chunk 大小整除时，未满的 chunk 中所填入的数据长度。
 *
 * 对每个文件，其 Metadata 是唯一确定的。
 * Metadta 会被存放在 raid 中每个存储文件的开头（也即，保存 p+2 次）。虽然严格
 * 来说，仅需在编号为 0 1 和 2 的三个磁盘中存储 Metadata 即可保证在任何情况下
 * 都能获取到 Metadata，但是为了统一磁盘操作，简化编码，我们选择在每个磁盘都
 * 存储一份。
 */
typedef struct {
    int p;
    size_t size;
    size_t full_chunk_num;
    size_t last_chunk_data_size;
} Metadata;

/**
 * skip_metadata() - 跳过文件中的 Metadata
 *
 * 调用者需要保证文件正确打开，且其中确实有 Metadata
 */
void skip_metadata(MMIO *file) {
    Metadata data;
    assert(file != NULL);
    mmread(&data, sizeof(data), file);
}

/**
 * write_metadata() - 将 Metadata 写入文件
 */
void write_metadata(Metadata data, MMIO *file) {
    assert(file != NULL);
    mmwrite(&data, sizeof(data), file);
}

/**
 * get_raw_file_metadata() - 获取原始文件的 Metadata
 *
 * 调用者应保证原始文件存在。
 */
Metadata get_raw_file_metadata(const char *filename, int p) {
    Metadata result;
    FILE *fp = fopen(filename, "rb");
    assert(fp != NULL);
    result.p = p;
    fseek(fp, 0, SEEK_END);
    result.size = ftell(fp);
    size_t chunk_data_size = sizeof(Packet) * p * (p - 1);
    result.full_chunk_num = result.size / chunk_data_size;
    result.last_chunk_data_size = result.size - result.full_chunk_num * chunk_data_size;
    return result;
}

/**
 * get_cooked_file_metadata() - 从 raid 中获取文件的 Metadata
 */
Metadata get_cooked_file_metadata(const char *filename) {
    Metadata result;
    int success = 0;

    assert(filename != NULL);
    /* 尝试磁盘 0 1 和 2 ，从第一个成功打开的磁盘中读取文件的 Metadata */
    for (int i = 0; i < 3; ++i) {
        char path[PATH_MAX];
        sprintf(path, "disk_%d/%s", i, filename);
        FILE *fp = fopen(path, "rb");
        if (fp != NULL) {
            fread(&result, sizeof(result), 1, fp);
            fclose(fp);
            success = 1;
            break;
        }
    }
    if (!success) {
        puts("File does not exist！");
        exit(0);
    }
    return result;
}

size_t disk_file_size(Metadata *x) {
    assert(x != NULL);
    size_t size = sizeof(Metadata);
    size += (x->p - 1) * sizeof(Packet) * x->full_chunk_num;
    if (x->last_chunk_data_size != 0)
        size += (x->p - 1) * sizeof(Packet);
    return size;
}

void push_chunks_into_queue(SpscQueue *queue, Chunk *chunks, int p) {
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
void read_file(char *filename, const char *save_as) {
    MMIO out[1];
    MMIO in[PMAX + 2]; // FIXME: dirty hack
    int bad_disks[2] = { -1, -1 };
    int bad_disk_num = 0;

    assert(filename != NULL);
    assert(save_as != NULL);

    simple_hash(filename);

    /* 从 raid 中获取文件的 Metadata */
    Metadata meta = get_cooked_file_metadata(filename);
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

    SpscQueue dirty_chunks = SpscQueue_new(QUEUESIZE);
    SpscQueue clean_chunks = SpscQueue_new(QUEUESIZE);
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
void write_file(char *file_to_read, int p) {
    MMIO in[1];
    MMIO out[PMAX + 2]; // FIXME: dirty hack

    assert(file_to_read != NULL);
    assert(p >= 3);
    assert(p <= 101);

    /* 获取文件的 Metadata */
    Metadata meta = get_raw_file_metadata(file_to_read, p);

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

    SpscQueue dirty_chunks = SpscQueue_new(QUEUESIZE);
    SpscQueue clean_chunks = SpscQueue_new(QUEUESIZE);
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
void repair_file(const char *fname, int bad_disk_num, int bad_disks_[2]) {
    MMIO in[PMAX + 2]; // FIXME: dirty hack
    MMIO out[2];
    char path[PATH_MAX];
    int bad_disks[2] = { bad_disks_[0], bad_disks_[1] };

    assert(fname != NULL);

    /* 从 raid 中读取文件的 Metadata */
    Metadata meta = get_cooked_file_metadata(fname);

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

    SpscQueue dirty_chunks = SpscQueue_new(QUEUESIZE);
    SpscQueue clean_chunks = SpscQueue_new(QUEUESIZE);
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
void usage() {
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

