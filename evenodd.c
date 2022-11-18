#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>

#include <sys/stat.h>
#include <linux/limits.h>
#include <dirent.h>

void panic(void) {
    abort();
}

#define unimplemented() do { \
    fprintf(stderr, "unimplemented at line %d\n", __LINE__); \
    abort(); \
} while (0)

typedef enum {
    Success = 0,
    ETooManyCorruptions,
    EUnimplemented,
    ECheckFail,
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
#define PMAX (101)


typedef struct {
    int p;
    Packet data[];
} Chunk;

Chunk *chunk_init(Chunk *chunk, int p) {
    chunk->p = p;
    return chunk;
}

size_t chunk_size(int p) {
    size_t num = (p + 2) * (p - 1);
    return sizeof(Chunk) + sizeof(Packet) * num;
}

Chunk *chunk_new(int p) {
    Chunk *result = (Chunk *)malloc(chunk_size(p));
    return chunk_init(result, p);
}

#define ATR(row, column) (((row) == (chunk->p - 1)) ? 0 \
        : (chunk->data[(column) * (chunk->p - 1) + (row)]))
#define AT(row, column) (chunk->data[(column) * (chunk->p - 1) + (row)])
#define M(x) (((x) % m + m) % m)
Error check_chunk(Chunk *chunk) {
    Packet S;
    PZERO(S);
    int m = chunk->p;
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
            return ECheckFail;
        }
    }

    PZERO(S);
    for (int i = 0; i <= m - 2; ++i) {
        for (int j = 0; j <= m; ++j) {
            PXOR(S, ATR(i, j));
        }
        if (S != 0) {
            fprintf(stderr, "check chunk: row %d/%d broken\n", i, m - 1);
            return ECheckFail;
        }
    }
    return Success;
}

#define check_chunk(chunk) do { \
    if (check_chunk(chunk) != Success) { \
        fprintf(stderr, "check_chunk failed at line %d\n", __LINE__); \
        abort(); \
    } \
} while (0)

Error cook_chunk_r1(Chunk *chunk) {
    int m = chunk->p;
    for (int l = 0; l <= m - 2; ++l) {
        PZERO(AT(l, m));
        for (int t = 0; t <= m - 1; ++t) {
            PXOR(AT(l, m), AT(l, t));
        }
    }
    return Success;
}

Error cook_chunk_r2(Chunk *chunk) {
    Packet S;
    PZERO(S);
    int m = chunk->p;
    for (int t = 1; t <= m - 1; ++t)
        PXOR(S, ATR(m - 1 - t, t));
    for (int l = 0; l <= m - 2; ++l) {
        PASGN(AT(l, m + 1), S);
        for (int t = 0; t <= m - 1; ++t) {
            PXOR(AT(l, m + 1), ATR(M(l - t), t));
        }
    }
    return Success;
}

Error cook_chunk(Chunk *chunk) {
    cook_chunk_r1(chunk);
    cook_chunk_r2(chunk);
#ifndef NDEBUG
    check_chunk(chunk);
#endif
    return Success;
}

Error try_repair_chunk(Chunk *chunk, int bad_disks[2]) {
    // both the mentioned disks are fine
    if (bad_disks[0] == -1) {
        return Success;
    }
    // just the second disk is broken
    if (bad_disks[1] == -1) {
        if (bad_disks[0] <= chunk->p) {
            for (int i = 0; i < chunk->p - 1; ++i)
                PZERO(AT(i, bad_disks[0]));
            for (int j = 0; j <= chunk->p; ++j) {
                if (j == bad_disks[0]) {
                    continue;
                }
                for (int i = 0; i < chunk->p - 1; ++i) {
                    PXOR(AT(i, bad_disks[0]), AT(i, j));
                }
            }
        } else {
            cook_chunk_r2(chunk);
        }
    // both are broken
    } else {
#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))
        int i = min(bad_disks[0], bad_disks[1]);
        int j = max(bad_disks[0], bad_disks[1]);
        int m = chunk->p;
        if (i == m && j == m + 1) {
            // reconstruction
            cook_chunk(chunk);
        } else if (i < m && j == m) {
            // calculate S
            Packet S;
            PASGN(S, ATR(M(i - 1), m + 1));
            for (int l = 0; l <= m - 1; ++l) {
                PXOR(S, ATR(M(i - 1 - l), l));
            }

            // recover column i
            for (int k = 0; k <= m - 2; ++k) {
                PASGN(AT(k, i), S);
                PXOR(AT(k, i), ATR(M(i + k), m + 1));
                for (int l = 0; l <= m - 1; ++l) {
                    if (l == i)
                        continue;
                    PXOR(AT(k, i), ATR(M(k + i - l), l));
                }
            }
            cook_chunk_r1(chunk);
        } else if (i < m && j == m + 1) {
            for (int k = 0; k < m - 1; ++k)
                PZERO(AT(k, i));
            for (int j = 0; j <= m; ++j) {
                if (j == i) {
                    continue;
                }
                for (int k = 0; k < m - 1; ++k) {
                    PXOR(AT(k, i), AT(k, j));
                }
            }
            cook_chunk_r2(chunk);
        } else { // i < m and j < m
            // calculate S
            unimplemented();
            Packet S = 0;
            for (int l = 0; l < m - 1; ++l) {
                PXOR(S, AT(l, m));
            }
            for (int l = 0; l < m - 1; ++l) {
                PXOR(S, AT(l, m + 1));
            }
            // horizontal syndromes S0
            // diagonal syndromes S1
            Packet *S0 = malloc(m * sizeof(Packet));
            Packet *S1 = malloc(m * sizeof(Packet));
            for (int u = 0; u < m; ++u) {
                Packet n = 0;
                for (int l = 0; l <= m; ++l) {
                    if (l == i || l == j)
                        continue;
                    PXOR(n, ATR(u, l));
                }
                S0[u] = n;
            }
            for (int u = 0; u < m; ++u) {
                Packet n = S;
                PXOR(n, ATR(u, m + 1));
                for (int l = 0; l < m; ++l) {
                    if (l == i || l == j)
                        continue;
                    PXOR(n, ATR(M(u - l), l));
                }
                S1[u] = n;
            }
            int s = M(-(j - i) - 1);
            for (int l = 0; l < m; ++l) {
                AT(m - 1, l) = 0;
            }
            while (s != m - 1) {
                AT(s, j) = S1[M(j + s)];
                PXOR(AT(s, j), AT(M(s + (j - i)), i));
                AT(s, i) = S0[s];
                PXOR(AT(s, i), AT(s, j));
                s = M(s - (j - i));
            }
            free(S0);
            free(S1);
        }
    }
#ifndef NDEBUG
    check_chunk(chunk);
#endif

    return Success;
}

Error write_raw_chunk_limited(Chunk *chunk, FILE *file, size_t limit) {
    fwrite(chunk->data, 1, limit, file);
    return Success;
}

Error write_raw_chunk(Chunk *chunk, FILE *file) {
    size_t num = chunk->p * (chunk->p - 1);
    fwrite(chunk->data, sizeof(Packet), num, file);
    return Success;
}

Error read_raw_chunk(Chunk *chunk, FILE *file) {
    size_t num = chunk->p * (chunk->p - 1);
    size_t ok = fread(chunk->data, 1, sizeof(Packet) * num, file);
    memset((void *)chunk->data + ok, 0, (chunk->p - 1) * (chunk->p + 2) * sizeof(Packet) - ok);
    return Success;
}

Error write_cooked_chunk(Chunk *chunk, FILE *files[]) {
    int disk_num = chunk->p + 2;
    int items_per_disk = chunk->p - 1;
    Packet *data = chunk->data;
#ifndef NDEBUG
    check_chunk(chunk);
#endif
    for (int i = 0; i < disk_num; ++i) {
        fwrite(data, sizeof(Packet), items_per_disk, files[i]);
        data += items_per_disk;
    }
    return Success;
}

Error write_cooked_chunk_to_bad_disk(Chunk *chunk, int bad_disks[2], FILE *bad_disk_fp[2]) {
    int items_per_disk = chunk->p - 1;
    Packet *data = chunk->data;
#ifndef NDEBUG
    check_chunk(chunk);
#endif
    for (int i = 0; i < 2; ++i) {
        if (bad_disks[i] != -1) {
            fwrite(data + items_per_disk * bad_disks[i], sizeof(Packet), items_per_disk, bad_disk_fp[i]);
        }
    }
    return Success;
}

Error read_cooked_chunk(Chunk *chunk, FILE *files[]) {
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
            size_t ok = fread(data, 1, sizeof(Packet) * items_per_disk, files[i]);
#ifndef NDEBUG
            if (ok < items_per_disk * sizeof(Packet)) {
                fprintf(stderr, "bad read at line %d, read %zu bytes\n", __LINE__, ok);
                abort();
            }
#endif
        }
        data += items_per_disk;
    }

    if (bad_disk_num != 0) {
        return try_repair_chunk(chunk, bad_disks);
    }

#ifndef NDEBUG
    check_chunk(chunk);
#endif

    return Success;
}

typedef struct {
    int p;
    size_t size;
    size_t full_chunk_num;
    size_t last_chunk_data_size;
} Metadata;

void skip_metadata(FILE *file) {
    Metadata data;
    fread(&data, sizeof(data), 1, file);
}

void write_metadata(Metadata data, FILE *file) {
    fwrite(&data, sizeof(data), 1, file);
}

Metadata get_raw_file_metadata(const char *filename, int p) {
    Metadata result;
    FILE *fp = fopen(filename, "rb");
    result.p = p;
    fseek(fp, 0, SEEK_END);
    result.size = ftell(fp);
    size_t chunk_data_size = sizeof(Packet) * p * (p - 1);
    result.full_chunk_num = result.size / chunk_data_size;
    result.last_chunk_data_size = result.size - result.full_chunk_num * chunk_data_size;
    return result;
}

Metadata get_cooked_file_metadata(const char *filename) {
    Metadata result;
    for (int i = 0; i < 3; ++i) {
        char path[PATH_MAX];
        sprintf(path, "disk_%d/%s", i, filename);
        FILE *fp = fopen(path, "rb");
        if (fp != NULL) {
            fread(&result, sizeof(result), 1, fp);
            fclose(fp);
            break;
        }
    }
    return result;
}

void read_file(const char *filename, const char *save_as) {
    FILE *out = fopen(save_as, "wb");
    FILE *in[PMAX + 2]; // FIXME: dirty hack
    Metadata meta = get_cooked_file_metadata(filename);
    int p = meta.p;

    for (int i = 0; i < p + 2; ++i) {
        char path[PATH_MAX];
        sprintf(path, "disk_%d/%s", i, filename);
        in[i] = fopen(path, "rb");
        if (in[i] != NULL)
            skip_metadata(in[i]);
    }

    Chunk *chunk = chunk_new(p);
    for (int i = 0; i < meta.full_chunk_num; ++i) {
        read_cooked_chunk(chunk, in);
        write_raw_chunk(chunk, out);
    }
    if (meta.last_chunk_data_size != 0) {
        read_cooked_chunk(chunk, in);
        write_raw_chunk_limited(chunk, out, meta.last_chunk_data_size);
    }
    free(chunk);
}

void write_file(const char *file_to_read, int p) {
    FILE *in = fopen(file_to_read, "rb");
    FILE *out[PMAX + 2]; // FIXME: dirty hack
    Metadata meta = get_raw_file_metadata(file_to_read, p);

    for (int i = 0; i < p + 2; ++i) {
        char path[PATH_MAX];
        sprintf(path, "disk_%d", i);
        mkdir(path, 0755);
        sprintf(path, "disk_%d/%s", i, file_to_read);
        out[i] = fopen(path, "wb");
        write_metadata(meta, out[i]);
    }

    Chunk *chunk = chunk_new(p);
    for (int i = 0; i < meta.full_chunk_num; ++i) {
        read_raw_chunk(chunk, in);
        cook_chunk(chunk);
        write_cooked_chunk(chunk, out);
    }
    if (meta.last_chunk_data_size != 0) {
        read_raw_chunk(chunk, in);
        cook_chunk(chunk);
        write_cooked_chunk(chunk, out);
    }
    free(chunk);
}

void repair_file(const char *fname, int bad_disks[2]) {
    FILE *in[PMAX + 2]; // FIXME: dirty hack
    FILE *out[2] = { NULL, NULL };
    char path[PATH_MAX];
    Metadata meta = get_cooked_file_metadata(fname);

    if (bad_disks[0] == bad_disks[1]) {
        bad_disks[1] = -1;
    }

    for (int i = 0; i < meta.p + 2; ++i) {
        sprintf(path, "disk_%d", i);
        mkdir(path, 0755);
        sprintf(path, "disk_%d/%s", i, fname);
        in[i] = fopen(path, "rb");
        if (in[i] != NULL)
            skip_metadata(in[i]);
    }

    for (int i = 0; i < 2; ++i) {
        if (bad_disks[i] != -1) {
            sprintf(path, "disk_%d/%s", bad_disks[i], fname);
            out[i] = fopen(path, "wb");
            write_metadata(meta, out[i]);
        }
    }

    Chunk *chunk = chunk_new(meta.p);
    for (int i = 0; i < meta.full_chunk_num; ++i) {
        read_cooked_chunk(chunk, in);
        write_cooked_chunk_to_bad_disk(chunk, bad_disks, out);
    }
    if (meta.last_chunk_data_size != 0) {
        read_cooked_chunk(chunk, in);
        write_cooked_chunk_to_bad_disk(chunk, bad_disks, out);
    }
    free(chunk);
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
        write_file(argv[2], atoi(argv[3]));
    } else if (strcmp(op, "read") == 0) {
        read_file(argv[2], argv[3]);
    } else if (strcmp(op, "repair") == 0) {
        int bad_disk_num = atoi(argv[2]);
        int bad_disks[2] = { -1, -1 };
        for (int i = 0; i < bad_disk_num; ++i) {
            bad_disks[i] = atoi(argv[i + 3]);
        }

        DIR *dir;
        for (int i = 0; i < 3; ++i) {
            char path[PATH_MAX];
            sprintf(path, "disk_%d", i);
            dir = opendir(path);
            if (dir != NULL) {
                break;
            }
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
                repair_file(entry->d_name, bad_disks);
        }
        closedir(dir);
    } else {
        printf("Non-supported operations!\n");
    }
    return 0;
}

