#!/bin/sh

cd "$(dirname "$0")"/..
sh compile.sh -Og -g -DNDEBUG 2>/dev/null
mkdir -p test
cd test
perf record --per-thread -g -- ../evenodd "$@"
