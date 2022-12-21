#include <limits.h>
#include "metadata.h"
#include "mmio/mmio.h"
#include <assert.h>
#include "packet.h"
#include <stdlib.h>

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

