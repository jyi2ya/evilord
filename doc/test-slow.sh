#!/bin/bash

set -e

cd "$(dirname "$0")/.." || exit 1
sh compile.sh -fsanitize=address -Og -g
mkdir -p test
cd test || exit 1

deploy() {
    dd status=none if=/dev/urandom of=test.bin bs="$filesize" count=1 iflag=fullblock &
    dd status=none if=/dev/urandom of=test2.bin bs="$((filesize * 3))" count=1 iflag=fullblock &

    for p in 101 31 47 97 2 3 5 7; do
	wait
	{
		rm -rf test1
		mkdir -p test1
		cd test1
		cp ../test.bin . &
		cp ../test2.bin . &
		wait
		../../evenodd write test.bin "$p" &
		../../evenodd write test2.bin "$p" &
		wait
	} &

	{
		rm -rf test2
		mkdir -p test2
		cd test2
		cp ../test.bin . &
		cp ../test2.bin . &
		wait
		../../evenodd write test.bin "$p" &
		../../evenodd write test2.bin "$p" &
		wait
	} &

	wait
	eval "$failtype"
	cd test2
        rm -rf "disk_$bad1" "disk_$bad2"
        ../../evenodd repair 2 "$bad1" "$bad2"
        cd ..
        diff -r test1 test2 || exit 2
    done
}

for failtype in  \
       	'bad1=$((RANDOM % p));bad2=$((RANDOM % p));' \
       	'bad1=$((RANDOM % p));bad2=$p;' \
       	'bad1=$((RANDOM % p)); bad2=$bad1;' \
        'bad1=$p; bad2=$bad1;' \
        'bad1=$((p + 1)); bad2=$bad1' \
       	'bad1=$p;bad2=$((p + 1));' \
       	'bad1=$((RANDOM % p));bad2=$((p + 1));' \
	; do
	echo "testing: $failtype"

	for filesize in 1 2 4 8 16 32 64 128; do
		filesize=$((filesize * 9981))
		echo filesize is "$filesize"
		deploy
	done

	for filesize in 1 2 3 4 5; do
		echo filesize is "$filesize"
		deploy
	done

	echo "$failtype passed"
done
