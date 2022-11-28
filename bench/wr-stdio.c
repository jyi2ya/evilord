#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
    assert(argc == 3);
    size_t filesize = atoll(argv[2]);
    void *buf = malloc(1024);
    FILE *fp = fopen(argv[1], "wb");
    setvbuf(fp, NULL, _IOFBF, 128 * 1024);
    while (filesize >= 1024) {
        fwrite(buf, 1, 1024, fp);
        char *s = buf;
        for (size_t i = 0; i < 1024; ++i) {
            *s++ = 0;
        }
        filesize -= 1024;
    }
    fclose(fp);
    free(buf);
    return 0;
}
