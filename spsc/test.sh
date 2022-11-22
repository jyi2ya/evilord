#!/bin/bash
gcc -Wall -Werror -Wshadow -g -fsanitize=address -O0 test.c spsc.h spsc.c -o test
./test
ret=$?
rm test
exit $ret
