#!/bin/sh
gcc test.c hp2.c -Wall -pedantic-errors -std=c89 -o test
./test
