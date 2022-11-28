#!/bin/bash

set -e

cd "$(dirname "$0")/.." || exit 1
sh compile.sh
mkdir -p test
cd test || exit 1

do_test() {
    filename="$1"

    for p in 3 5 7 31 47 97 101; do
        echo p is "$p"
        rm -rf disk*

        ../evenodd write "$filename" "$p"

        for _ in 0 1 2; do
            ../evenodd read "$filename" "$PWD/test.bin.rtv"
            diff "$filename" "$PWD/test.bin.rtv" || exit 2
            rm -rf "disk_$((RANDOM % (p + 2)))"
        done

        ../evenodd write "$filename" "$p"

        bad1=$((RANDOM % (p + 2)))
        rm -rf "disk_$bad1"
        ../evenodd repair 1 "$bad1"
        ../evenodd read "$filename" "$PWD/test.bin.rtv"
        diff "$filename" "$PWD/test.bin.rtv" || exit 2
        bad2=$((RANDOM % (p + 2)))
        rm -rf "disk_$bad1" "disk_$bad2"
        ../evenodd repair 2 "$bad1" "$bad2"
        ../evenodd read "$filename" "$PWD/test.bin.rtv"
        diff "$filename" "$PWD/test.bin.rtv" || exit 2
    done
}

IFS="
"

find /usr -type f | while read -r line; do
    if [ -r "$line" ]; then
        echo "$line"
        do_test "$line"
    fi
done
