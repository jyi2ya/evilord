#!/bin/bash

gcc -pthread -std=c11 -o evenodd spsc/spsc.c evenodd.c -Wall -Wextra -Wshadow "$@"
