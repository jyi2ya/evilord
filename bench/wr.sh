#!/bin/sh
set -e
if [ -z "$1" ]; then
  exit 1
fi
filesize="$1"
gcc wr.c ../mmio/mmio.c ../mmio/mmio.h -o wr
time ./wr test.bin "$filesize"
rm test.bin wr
