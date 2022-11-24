#!/bin/bash

gcc -O2 -DBIGBUF -DNDEBUG -pthread -std=gnu11 -o evenodd spsc/spsc.c evenodd.c -Wall -Wextra -Wshadow
