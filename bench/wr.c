#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../mmio/mmio.h"
int main(int argc, char *argv[]) {
    assert(argc == 3);
    size_t filesize = atoll(argv[2]);
    void *buf = malloc(1024);
    MMIO mmio;
    mmwr_open(&mmio, argv[1], filesize);
    while (mmwrite(buf, 1024, &mmio)) {
        char *s = buf;
        for (size_t i = 0; i < 1024; ++i) {
            *s++ = 0;
        }
    }
    mmwr_close(&mmio);
    free(buf);
    return 0;
}
