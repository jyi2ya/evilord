#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../mmio/mmio.h"
int main(int argc, char *argv[]) {
    assert(argc == 3);
    size_t filesize = atoll(argv[2]);
    void *buf = malloc(filesize);
    MMIO mmio;
    mmrd_open(&mmio, argv[1], filesize);
    char res = 0;
    mmread(buf, filesize, &mmio);
    char *s = buf;
    printf("mmio.size :>> %ld\n", mmio.size);
    for (size_t i = 0; i < filesize; ++i) {
        res += *s++;
    }
    putchar(*s);
    mmrd_close(&mmio);
    printf("useless sum: %d\n", res);
    free(buf);
    return 0;
}
