#!/bin/bash
[[ "$#" -lt 2 ]] || exit 1
filesize="$1"
gcc -g -fsanitize=address -O0 wr.c ../mmio/mmio.c ../mmio/mmio.h -o wr
time ./wr test.bin "$filesize"
rm test.bin wr
