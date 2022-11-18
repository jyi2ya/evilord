#!/bin/bash

set -xe

cd "$(dirname "$0")/.." || exit 1
sh compile.sh -fsanitize=address -Og -g
mkdir -p test
cd test || exit 1

deploy() {
    dd status=none if=/dev/urandom of=test.bin bs="$filesize" count=1 iflag=fullblock &
    dd status=none if=/dev/urandom of=test2.bin bs="$((filesize * 3))" count=1 iflag=fullblock &

    for p in 2 3 5 7 31 47 97 101; do
        echo p is "$p"
        rm -rf disk*

        wait
        ../evenodd write test.bin "$p" &
        ../evenodd write test2.bin "$p" &

        for _ in 0 1 2; do
            wait
            ../evenodd read test.bin test.bin.rtv &
            ../evenodd read test2.bin test2.bin.rtv &

            wait
            diff test.bin test.bin.rtv || exit 2
            diff test2.bin test2.bin.rtv || exit 2
            rm -rf "disk_$((RANDOM % (p + 2)))"
        done

        wait
        ../evenodd write test.bin "$p" &
        ../evenodd write test2.bin "$p" &

        bad1=$((RANDOM % (p + 2)))
        rm -rf "disk_$bad1" &

        wait
        ../evenodd repair 1 "$bad1"
        ../evenodd read test.bin test.bin.rtv &
        ../evenodd read test2.bin test2.bin.rtv &

        wait
        diff test.bin test.bin.rtv || exit 2
        diff test2.bin test2.bin.rtv || exit 2

        bad2=$((RANDOM % (p + 2)))
        rm -rf "disk_$bad1" "disk_$bad2"

        wait 
        ../evenodd repair 2 "$bad1" "$bad2"
        ../evenodd read test.bin test.bin.rtv &
        ../evenodd read test2.bin test2.bin.rtv &

        wait
        diff test.bin test.bin.rtv || exit 2
        diff test2.bin test2.bin.rtv || exit 2
    done
}

for filesize in 1 2 3 4 5; do
    echo filesize is "$filesize"
    deploy
done

for filesize in 1 2 4 8 16 32 64 128; do
    echo filesize is "$filesize"
    filesize=$((filesize * 9981))
    deploy
done
