#!/bin/bash

gcc -O2 -DNDEBUG -pthread -std=c11 -o evenodd spsc/spsc.c evenodd.c -Wall -Wextra -Wshadow "$@"
