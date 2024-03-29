#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
    assert(argc == 3);
    size_t filesize = atoll(argv[2]);
    void *buf = malloc(1024);
    FILE *fp = fopen(argv[1], "rb");
    setvbuf(fp, NULL, _IOFBF, 128 * 1024);
    posix_fadvise(fileno(fp), 0, 0,
            POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED | POSIX_FADV_NOREUSE);
    char res = 0;
    size_t nbytes;
    while ((nbytes = fread(buf, 1, 1024, fp))) {
        char *s = buf;
        for (size_t i = 0; i < nbytes; ++i) {
            res += *s++;
        }
    }
    fclose(fp);
    printf("useless sum: %d\n", res);
    free(buf);
    return 0;
}
