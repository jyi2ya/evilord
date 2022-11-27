#!/bin/sh
set -e
if [ -z "$1" ]; then
  exit 1
fi
filesize="$1"
dd status=none if=/dev/urandom of=test.bin bs="$filesize" count=1 iflag=fullblock&
gcc rd.c ../mmio/mmio.c ../mmio/mmio.h -o rd
time ./rd test.bin "$filesize"
rm test.bin rd