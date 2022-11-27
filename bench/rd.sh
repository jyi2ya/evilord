#!/bin/bash
[[ "$#" -lt 2 ]] || exit 1
filesize="$1"
dd status=none if=/dev/urandom of=test.bin bs="$filesize" count=1 iflag=fullblock
gcc -O2 rd.c ../mmio/mmio.c ../mmio/mmio.h -o rd
gcc -O2 rd-stdio.c ../mmio/mmio.c ../mmio/mmio.h -o rd-stdio
hyperfine -p sync "./rd test.bin $filesize" "./rd-stdio test.bin $filesize"
rm test.bin rd rd-stdio
