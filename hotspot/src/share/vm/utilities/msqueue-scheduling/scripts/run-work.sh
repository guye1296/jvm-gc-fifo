#!/bin/bash

export LD_PRELOAD=./libjemalloc.so

# ./benchwork.sh  main.c  N_THREADS  USE_CPUS MAX_WORK  ORIG_OP_ORDER  SCHEDULING_METHOD

# hlcrq.c msqueue.c lfstack.c

for alg in hlcrq
do

 for w in 5 10 20 40 80 160 320 640 1280 2560 5120 10240 20480 40960 81920 163840
  do
./benchwork.sh ${alg} 80 80 ${w} 0 0
  done

done


for alg in hlcrq
do

 for w in 5 10 20 40 80 160 320 640 1280 2560 5120 10240 20480 40960 81920 163840
  do
./benchwork.sh ${alg} 80 80 ${w} 0 1
  done

done


for alg in hlcrq
do

 for w in 5 10 20 40 80 160 320 640 1280 2560 5120 10240 20480 40960 81920 163840
  do
./benchwork.sh ${alg} 80 80 ${w} 0 2
  done

done


for alg in hlcrq
do

 for w in 5 10 20 40 80 160 320 640 1280 2560 5120 10240 20480 40960 81920 163840
  do
./benchwork.sh ${alg} 80 80 ${w} 0 3
  done

done


for alg in hlcrq
do

 for w in 5 10 20 40 80 160 320 640 1280 2560 5120 10240 20480 40960 81920 163840
  do
./benchwork.sh ${alg} 80 80 ${w} 0 4
  done

done
