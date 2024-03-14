#!/bin/bash
#
gcc -ggdb -O -o test test.c profiler.cc
g++ -ggdb -O2 -o test_time time_function_example.cc profiler.cc
g++ -ggdb -O2 -o stats time_function_stats.cc profiler.cc
