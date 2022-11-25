#!/bin/bash

gcc -O2 -DNDEBUG -pthread -std=gnu11 -o evenodd mmio/mmio.c spsc/spsc.c evenodd.c -Wall -Wextra -Wshadow
