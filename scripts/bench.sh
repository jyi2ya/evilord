#!/bin/bash

set -e

exec 2>&1

cd "$(dirname "$0")/.." || exit 1
gcc -O2 -DPERFCNT -DNDEBUG -pthread -std=gnu11 -o evenodd mmio/mmio-pipe.c spsc/spsc.c evenodd.c chunk.c metadata.c repair.c -Wall -Wextra -Wshadow
mkdir -p test
cd test || exit 1

echo file size 4GB
filesize=$((1024 * 1024 * 1024 * 4))

if ! [ -r test.bin ] || [ "$(stat -c '%s' test.bin)" -ne "$filesize" ]; then
	dd if=/dev/urandom of=test.bin bs="$((filesize / 1024))" count=1024 iflag=fullblock
fi

timeit() {
	sync
	begin=$(date +%s)
	eval "$*"
	end=$(date +%s)
	speed=$((filesize * 100 / (end - begin + 1) / 1024 / 1024 ))
	echo -n "$((end - begin))s "
	echo -n "$speed" | sed 's/\(..\)$/.\1/'
	echo -n " MB/s "
}

do_test() {
    p="$1"
    rm -rf disk_*
    timeit ../evenodd write test.bin "$p"
    echo -n "write p=$p filesize=$filesize: "
    echo "$((speed * 100 / cp_speed))% cp speed"

    timeit ../evenodd read test.bin test.bin.rtv
    echo -n "read p=$p filesize=$filesize: "
    echo "$((speed * 100 / cp_speed))% cp speed"

    rm -rf disk_2
    timeit ../evenodd repair 1 2
    echo -n "repair1 p=$p filesize=$filesize: "
    echo "$((speed * 100 / cp_speed))% cp speed"

    rm -rf disk_2 disk_3
    timeit ../evenodd repair 2 2 3
    echo -n "repair2 p=$p filesize=$filesize: "
    echo "$((speed * 100 / cp_speed))% cp speed"
}

rm -rf disk_*
mkdir -p disk_0
timeit /bin/dd if=test.bin of=disk_0/test.bin
cp_speed="$speed"
echo -n "/bin/cp: "
echo "$((speed * 100 / cp_speed))% cp speed"

do_test 3
do_test 101
