#!/bin/bash
[[ "$#" -lt 2 ]] || exit 1
filesize="$1"
dd status=none if=/dev/urandom of=test.bin bs="$filesize" count=1 iflag=fullblock&
gcc -g -fsanitize=address -O0 rd.c ../mmio/mmio.c ../mmio/mmio.h -o rd
time ./rd test.bin "$filesize"
rm test.bin rd
