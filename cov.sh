#!/bin/sh -e
make clean
make GCOV=1 check
gcovr -r . --html --html-details -o coverage.html
