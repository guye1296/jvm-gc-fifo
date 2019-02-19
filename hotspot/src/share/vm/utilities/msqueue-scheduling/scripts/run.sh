#!/bin/bash

export LD_LIBRARY_PATH=./papi/lib/
export LD_PRELOAD=./libjemalloc.so

./a.out 0 0 0
