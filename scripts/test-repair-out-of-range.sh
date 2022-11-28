#!/bin/bash

set -e

cd "$(dirname "$0")/.." || exit 1
sh compile.sh
mkdir -p test
cd test || exit 1

dd if=/dev/urandom of=./test1.bin bs=4M count=16
dd if=/dev/urandom of=./test2.bin bs=4M count=32
dd if=/dev/urandom of=./test3.bin bs=4M count=11
../evenodd write ./test1.bin 11
../evenodd write ./test2.bin 19
../evenodd write ./test3.bin 101
rm -rf disk_3 disk_4
../evenodd repair 2 3 4
rm -rf disk_13 disk_14
../evenodd repair 2 13 14
rm -rf disk_99 disk_78
../evenodd repair 2 99 78
rm -rf disk_13 disk_77
../evenodd repair 2 13 77
rm -rf disk_1 disk_99
../evenodd repair 2 1 99
rm -rf disk_99
../evenodd repair 1 99

../evenodd read ./test1.bin test1.bin.rtv
../evenodd read ./test2.bin test2.bin.rtv
../evenodd read ./test3.bin test3.bin.rtv

diff ./test1.bin test1.bin.rtv
diff ./test2.bin test2.bin.rtv
diff ./test3.bin test3.bin.rtv
