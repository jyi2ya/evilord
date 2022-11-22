#!/bin/bash

set -e

exec 2>&1

cd "$(dirname "$0")/.." || exit 1
sh compile.sh -O2
mkdir -p test
cd test || exit 1

echo file size 2GB
filesize=$((1024 * 1024 * 1024 * 2))
dd if=/dev/urandom of=test.bin bs="$filesize" count=1 iflag=fullblock

timeit() {
	begin=$(date +%s)
	eval "$@"
	end=$(date +%s)
	speed=$((filesize / (end - begin) / 1024 * 100 / 1024 ))
	echo -n "$speed" | sed 's/\(..\)$/.\1/'
	echo " MB/s"
}

do_test() {
    p="$1"
    rm -rf disk_*
    echo write p=$p filesize=$filesize
    timeit time ../evenodd write test.bin "$p"
    echo read p=$p filesize=$filesize
    timeit time ../evenodd read test.bin test.bin.rtv
    rm -rf disk_2
    echo repair1 p=$p filesize=$filesize
    timeit time ../evenodd repair 1 2
    rm -rf disk_2 disk_3
    echo repair2 p=$p filesize=$filesize
    timeit time ../evenodd repair 2 2 3
}

rm -rf disk_*
echo copy
mkdir -p disk_0
timeit time dd if=test.bin of=disk_0/test.bin iflag=fullblock

do_test 3
do_test 101
