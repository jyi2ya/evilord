#!/bin/bash

gcc -DNDEBUG -O2 mmio/mmio-pipe.c spsc/spsc.c evenodd.c chunk.c metadata.c repair.c -pthread -std=gnu11 -o evenodd
exit 0

gcc mmio/mmio-pipe.c spsc/spsc.c evenodd.c chunk.c metadata.c repair.c \
    -Og -g -fsanitize=address \
    -pthread \
    -std=gnu11 \
    -o evenodd \
    -Wall -Wextra -Wshadow \
    -Wduplicated-cond -Wduplicated-branches -Wlogical-op \
    -Wnull-dereference \
    -Wjump-misses-init \
    -Wdouble-promotion \
    -Wformat=2 \
    -Wuninitialized -Wno-missing-field-initializers \
    -Wpointer-arith \
    -Wstrict-prototypes -Wmissing-prototypes \
    -Wswitch-enum \
    -Wbad-function-cast \
    -Wredundant-decls -Wold-style-definition
