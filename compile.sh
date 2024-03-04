#!/bin/bash
#
gcc -ggdb -O -o test test.c
g++ -ggdb -O -o test_time time_function_example.cc
g++ -ggdb -O2 -o stats time_function_stats.cc
