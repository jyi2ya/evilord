#ifndef METADATA_H_
#define METADATA_H_

#include <stddef.h>
#include "mmio/mmio.h"

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

void skip_metadata(MMIO *file);
void write_metadata(Metadata data, MMIO *file);
Metadata get_raw_file_metadata(const char *filename, int p);
Metadata get_cooked_file_metadata(const char *filename);

#endif
