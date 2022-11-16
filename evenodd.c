#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void panic(void) {
    abort();
}

void unimplemented(void) {
    abort();
}

typedef enum {
    Success = 0,
    ETooManyCorruptions,
    EUnimplemented,
} Error;

typedef uint64_t Packet;
#define PXOR(x, y) do { \
    (x) ^= (y); \
} while (0)
#define PZERO(x) do { \
    (x) = 0; \
} while (0)
#define PASGN(x, y) do { \
    (x) = (y); \
} while (0)


typedef struct {
    int p;
    Packet data[];
} Chunk;

inline Chunk *chunk_init(Chunk *chunk, int p) {
    chunk->p = p;
    return chunk;
}

inline size_t chunk_size(int p) {
    size_t num = (p + 2) * (p - 1);
    return sizeof(Chunk) + sizeof(Packet) * num;
}

inline Chunk *chunk_new(int p) {
    Chunk *result = (Chunk *)malloc(chunk_size(p));
    return chunk_init(result, p);
}

#define AT(row, column) (chunk->data[(column) * (chunk->p - 1) + (row)])
Error try_repair_chunk(Chunk *chunk, int bad_disks[2]) {
    if (bad_disks[1] == -1) {
        for (int j = 0; j < chunk->p + 1; ++j) {
            if (j == bad_disks[0]) {
                continue;
            }
            for (int i = 0; i < chunk->p - 1; ++i) {
                PXOR(AT(i, bad_disks[0]), AT(i, j));
            }
        }
    } else {
        unimplemented();
    }

    return Success;
}

Error cook_chunk(Chunk *chunk) {
    int bad_disks[2] = { chunk->p, -1 };
    return try_repair_chunk(chunk, bad_disks);
    // TODO
}

inline Error write_raw_chunk_limited(Chunk *chunk, FILE *file, size_t limit) {
    fwrite(chunk->data, sizeof(Packet), limit, file);
    return Success;
}

inline Error write_raw_chunk(Chunk *chunk, FILE *file) {
    size_t num = chunk->p * (chunk->p - 1);
    fwrite(chunk->data, sizeof(Packet), num, file);
    return Success;
}

inline Error read_raw_chunk(Chunk *chunk, FILE *file) {
    size_t num = chunk->p * (chunk->p - 1);
    size_t ok = fread(chunk->data, sizeof(Packet), num, file);
    memset(chunk->data + ok, 0, ((chunk->p - 1) * 2 + (num - ok)) * sizeof(Packet));
    return Success;
}

inline Error write_cooked_chunk(Chunk *chunk, FILE *files[]) {
    int disk_num = chunk->p + 2;
    int items_per_disk = chunk->p - 1;
    Packet *data = chunk->data;
    for (int i = 0; i < disk_num; ++i) {
        fwrite(data, sizeof(Packet), items_per_disk, files[i]);
        data += items_per_disk;
    }
    return Success;
}

inline Error read_cooked_chunk(Chunk *chunk, FILE *files[]) {
    int disk_num = chunk->p + 2;
    int items_per_disk = chunk->p - 1;
    Packet *data = chunk->data;
    int bad_disks[2] = { -1, -1 };
    int bad_disk_num = 0;

    for (int i = 0; i < disk_num; ++i) {
        if (files[i] == NULL) {
            bad_disk_num += 1;
            if (bad_disk_num > 2) {
                return ETooManyCorruptions;
            }
            bad_disks[bad_disk_num - 1] = i;
            memset(data, 0, items_per_disk * sizeof(Packet));
        } else {
            size_t ok = fread(data, sizeof(Packet), items_per_disk, files[i]);
            if (ok < items_per_disk) {
                memset(data + ok, 0, (items_per_disk - ok) * sizeof(Packet));
            }
        }
        data += items_per_disk;
    }

    if (bad_disk_num != 0) {
        return try_repair_chunk(chunk, bad_disks);
    }

    return Success;
}

void usage() {
    printf("./evenodd write <file_name> <p>\n");
    printf("./evenodd read <file_name> <save_as>\n");
    printf("./evenodd repair <number_erasures> <idx0> ...\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return -1;
    }

    char* op = argv[1];
    if (strcmp(op, "write") == 0) {
        /*
         * Please encode the input file with EVENODD code
         * and store the erasure-coded splits into corresponding disks
         * For example: Suppose "file_name" is "testfile", and "p" is 5. After your
         * encoding logic, there should be 7 splits, "testfile_0", "testfile_1",
         * ..., "testfile_6", stored in 7 diffrent disk folders from "disk_0" to
         * "disk_6".
         */

    } else if (strcmp(op, "read")) {
        /*
         * Please read the file specified by "file_name", and store it as a file
         * named "save_as" in the local file system.
         * For example: Suppose "file_name" is "testfile" (which we have encoded
         * before), and "save_as" is "tmp_file". After the read operation, there
         * should be a file named "tmp_file", which is the same as "testfile".
         */
    } else if (strcmp(op, "repair")) {
        /*
         * Please repair failed disks. The number of failures is specified by
         * "num_erasures", and the index of disks are provided in the command
         * line parameters.
         * For example: Suppose "number_erasures" is 2, and the indices of
         * failed disks are "0" and "1". After the repair operation, the data
         * splits in folder "disk_0" and "disk_1" should be repaired.
         */
    } else {
        printf("Non-supported operations!\n");
    }
    return 0;
}

