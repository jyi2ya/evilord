#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../mmio/mmio.h"
int main(int argc, char *argv[]) {
    assert(argc == 3);
    size_t filesize = atoll(argv[2]);
    void *buf = malloc(filesize);
    MMIO mmio;
    mmwr_open(&mmio, argv[1], filesize);
    char res = 0;
    mmwrite(buf, filesize, &mmio);
    char *s = buf;
    printf("mmio.size :>> %ld\n", mmio.size);
    for (size_t i = 0; i < filesize; ++i) {
        *s++ = 0;
    }
    mmwr_close(&mmio);
    free(buf);
    return 0;
}
