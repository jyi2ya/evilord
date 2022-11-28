#!/bin/bash

set -e

cd "$(dirname "$0")/.." || exit 1
sh compile.sh
mkdir -p test
cd test || exit 1

primes=(2 3 5 7 11 13 17 19 23 29 31 37 41 43 47 53 59 61 67 71 73 79 83 89 97)

IFS="
"

find /usr/share -type f | while read -r line; do
    if [ -r "$line" ]; then
        echo "write: $line"
	p=${primes[ $RANDOM % ${#primes[@]} ]}
	../evenodd write "$line" "$p"
    fi
done

echo disk_* | tr ' ' '\n' | sort -R | head -2 | xargs rm -rf

find /usr/share -type f | while read -r line; do
    if [ -r "$line" ]; then
        echo "read : $line"
	../evenodd read "$line" test.bin.rtv
	diff test.bin.rtv "$line" || exit 2
    fi
done
