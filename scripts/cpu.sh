#!/bin/bash

set -e
cd "$(dirname "$0")/.."
gcc -O2 -DPERFCNT -DNDEBUG -pthread -std=gnu11 -o evenodd mmio/mmio.c spsc/spsc.c evenodd.c -Wall -Wextra -Wshadow
cd test
# dd if=/dev/urandom of=test3.bin bs=1024M count=2 iflag=fullblock
for i in 3 5 7 11 13 17 19 23 29 31 37 41 43 47; do
    rm -rf disk_*
    echo prime $i ' '
    echo -n write' ' 
    ../evenodd write test.bin $i
    rm -rf disk_2 disk_3
    echo -n 'read '
    ../evenodd read test.bin test.bin.rtv
    echo -n 'repair '
    ../evenodd repair 2 2 3
done
