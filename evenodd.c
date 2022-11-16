#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sys/stat.h>
#include <linux/limits.h>
#include <dirent.h>

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

typedef uint8_t Packet;
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

#define AT(row, column) (chunk->data[(column) * (chunk->p - 1) + (row)])
Error try_repair_chunk(Chunk *chunk, int bad_disks[2]) {
    // both the mentioned disks are fine
    if (bad_disks[0] == -1) {
        return Success;
    }
    // just the second disk is broken
    if (bad_disks[1] == -1) {
        for (int j = 0; j < chunk->p + 1; ++j) {
            if (j == bad_disks[0]) {
                continue;
            }
            for (int i = 0; i < chunk->p - 1; ++i) {
                PXOR(AT(i, bad_disks[0]), AT(i, j));
            }
        }
    // both are broken
    } else {
#define min(x, y) (x) < (y) ? (x) : (y)
#define max(x, y) (x) > (y) ? (x) : (y)
#define mod_group(x, m) (x) % (m)
        int i = min(bad_disks[0], bad_disks[1]);
        int j = max(bad_disks[0], bad_disks[1]);
        int p = chunk->p;
        if (i == p && j == p + 1) {
            // reconstruction
            unimplemented();
        } else if (i < p && j == p) {
            // calculate S
            Packet S = AT(mod_group(i - 1, p), p + 1);
            for (int l = 0; l < p; ++l) {
                PXOR(S, AT(mod_group(i - l - 1, p), l));
            }
            // recover column i
            for (int k = 0; k < p; ++k) {
                AT(k, i) = S;
                PXOR(AT(k, i), AT(mod_group(i - 1, p), p + 1));
                for (int l = 0; l < p; ++p) {
                    if (l == i)
                        continue;
                    PXOR(AT(k, i), AT(mod_group(k + i - l, p), l));
                }
            }
            // recover column j
            // just reconstruction
            unimplemented();
        } else if (i < p && j == p + 1) {
            // just reconstruction
            unimplemented();
        } else { // i < p and j < p
            // calculate S
            Packet S = 0;
            for (int l = 0; l < p - 1; ++l) {
                PXOR(S, AT(l, p));
            }
            for (int l = 0; l < p - 1; ++l) {
                PXOR(S, AT(l, p + 1));
            }
            // horizontal syndromes S0
            // diagonal syndromes S1
            Packet *S0 = malloc(p * sizeof(Packet));
            Packet *S1 = malloc(p * sizeof(Packet));
            for (int u = 0; u < p; ++u) {
                Packet n = 0;
                for (int l = 0; l <= p; ++l) {
                    if (l == i || l == j)
                        continue;
                    PXOR(n, AT(u, l));
                }
                S0[u] = n;
            }
            for (int u = 0; u < p; ++u) {
                Packet n = S;
                PXOR(n, AT(u, p + 1));
                for (int l = 0; l < p; ++l) {
                    if (l == i || l == j)
                        continue;
                    PXOR(n, AT(mod_group(u - l, p), l));
                }
            }
            int s = mod_group(-(j - i) - 1, p);
            for (int l = 0; l < p; ++l) {
                AT(p - 1, l) = 0;
            }
            while (s != p - 1) {
                AT(s, j) = S1[mod_group(j + s, p)];
                PXOR(AT(s, j), AT(mod_group(s + (j - i), p), i));
                AT(s, i) = S0[s];
                PXOR(AT(s, i), AT(s, j));
                s = mod_group(s - (j - i), p);
            }
            free(S0);
            free(S1);
        }
    }

    return Success;
}

Error cook_chunk(Chunk *chunk) {
    for (int i = 0; i < chunk->p - 1; ++i) {
        PZERO(AT(i, chunk->p));
    }
    for (int j = 0; j < chunk->p; ++j) {
        for (int i = 0; i < chunk->p - 1; ++i) {
            PXOR(AT(i, chunk->p), AT(i, j));
        }
    }
    Packet syndrome;
    PZERO(syndrome);
    for (int i = 0; i < chunk->p - 1; ++i) {
        PXOR(syndrome, AT(i, chunk->p - 1 - i));
    }
    for (int i = 0; i < chunk->p - 1; ++i) {
        PASGN(AT(i, chunk->p + 1), syndrome);
    }
    for (int j = 0; j < chunk->p; ++j) {
        for (int i = 0; i < chunk->p - 1; ++i) {
            if (i + j != chunk->p - 1) {
                PXOR(AT((i + j) % chunk->p, chunk->p + 1), AT(i, j));
            }
        }
    }
    return Success;
}

Error write_raw_chunk_limited(Chunk *chunk, FILE *file, size_t limit) {
    fwrite(chunk->data, sizeof(Packet), limit, file);
    return Success;
}

Error write_raw_chunk(Chunk *chunk, FILE *file) {
    size_t num = chunk->p * (chunk->p - 1);
    fwrite(chunk->data, sizeof(Packet), num, file);
    return Success;
}

Error read_raw_chunk(Chunk *chunk, FILE *file) {
    size_t num = chunk->p * (chunk->p - 1);
    size_t ok = fread(chunk->data, sizeof(Packet), num, file);
    memset(chunk->data + ok, 0, ((chunk->p - 1) * 2 + (num - ok)) * sizeof(Packet));
    return Success;
}

Error write_cooked_chunk(Chunk *chunk, FILE *files[]) {
    int disk_num = chunk->p + 2;
    int items_per_disk = chunk->p - 1;
    Packet *data = chunk->data;
    for (int i = 0; i < disk_num; ++i) {
        fwrite(data, sizeof(Packet), items_per_disk, files[i]);
        data += items_per_disk;
    }
    return Success;
}

Error write_cooked_chunk_to_bad_disk(Chunk *chunk, int bad_disks[2], FILE *bad_disk_fp[2]) {
    int items_per_disk = chunk->p - 1;
    Packet *data = chunk->data;
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

