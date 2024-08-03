#!/bin/sh -e

export AFL_USE_ASAN=1

make CC=afl-clang-fast CXX=afl-clang-fast++ LD=afl-clang-fast test
afl-fuzz -i testdata -o fuzz "$@" -- ./test
