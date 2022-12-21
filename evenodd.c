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

static char *simple_hash(char *str) {
    for (int i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '/') {
            str[i] = i + 'A';
        }
    }
    return str;
}

static size_t disk_file_size(Metadata *x) {
    assert(x != NULL);
    size_t size = sizeof(Metadata);
    size += (x->p - 1) * sizeof(Packet) * x->full_chunk_num;
    if (x->last_chunk_data_size != 0)
        size += (x->p - 1) * sizeof(Packet);
    return size;
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
    int p = meta.p;
    assert(p != 0);

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
        in[p].fd = -1;
        in[p + 1].fd = -1;
        bad_disks[0] = p;
        bad_disks[1] = p + 1;
    } else if (bad_disk_num == 1) {
        if (bad_disks[0] == p + 1) {
            in[p].fd = -1;
            bad_disks[0] = p;
            bad_disks[1] = p + 1;
        } else {
            in[p + 1].fd = -1;
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

    Chunk *chunk = chunk_new(p);
    for (size_t k = 0; k < meta.full_chunk_num; ++k) {
        read_cooked_chunk(chunk, in);
        repair(chunk, i, j);
        write_raw_chunk(chunk, out, NULL);
    }

    if (meta.last_chunk_data_size != 0) {
        read_cooked_chunk(chunk, in);
        repair(chunk, bad_disks[0], bad_disks[1]);
        write_raw_chunk_limited(chunk, out, meta.last_chunk_data_size);
    }

    free(chunk);

    for (int k = 0; k < p + 2; ++k) {
        if (in[k].fd != -1) {
            mmrd_close(&in[k]);
        }
    }
    mmwr_close(out);
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

    Chunk *chunk = chunk_new(p);
    for (size_t k = 0; k < rwnum; ++k) {
        read_raw_chunk(chunk, in);
        repair_2bad_case1(chunk, p, p + 1);
        write_cooked_chunk(chunk, out, NULL);
    }
    free(chunk);

    mmrd_close(in);
    for (int i = 0; i < p + 2; ++i) {
        mmwr_close(&out[i]);
    }
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

    Chunk *chunk = chunk_new(p);
    for (size_t k = 0; k < rwnum; ++k) {
        read_cooked_chunk(chunk, in);
        repair(chunk, i, j);
        write_cooked_chunk_to_bad_disk(chunk, out, bad_disks);
    }
    free(chunk);

    for (int k = 0; k < p + 2; ++k) {
        if (in[k].fd != -1) {
            mmrd_close(&in[k]);
        }
    }
    for (int k = 0; k < bad_disk_num; ++k) {
        mmwr_close(&out[k]);
    }
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
