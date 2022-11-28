#!/bin/bash
[[ "$#" -lt 2 ]] || exit 1
filesize="$1"
gcc -O2 wr.c ../mmio/mmio.c ../mmio/mmio.h -o wr
gcc -O2 wr-stdio.c ../mmio/mmio.c ../mmio/mmio.h -o wr-stdio
hyperfine -w 3 "./wr test.bin $filesize" "./wr-stdio test.bin $filesize"
rm test.bin wr wr-stdio
