#!/bin/bash

# USAGE:
# ./compile <data_structure> <max_work> <scheduling_method_num> <n_threads>
# you can just use ./compile and it the same as "./compile msqueue 5 4 80"


rm -f a.out res.txt;

source configuration.sh

export LD_LIBRARY_PATH=$remote_project_path/papi/lib/

src="msqueue"
max_work=5
scheduling_method_num=4
scheduling_method="work-time-queue"
n_threads=80
use_cpus=80

if [[ $1 ]]; then
	src=$1
	if [[ $2 ]]; then
		max_work=$2	
		if [[ $3 ]]; then
			scheduling_method_num=$3
			
			if [[ $scheduling_method_num = 0 ]]; then
				scheduling_method="no-scheduling"
			else
				if [[ $scheduling_method_num = 1 ]]; then
					scheduling_method="primitive"
				else
					if [[ $scheduling_method_num = 2 ]]; then
						scheduling_method="ref-count"
					else
						if [[ $scheduling_method_num = 3 ]]; then
							scheduling_method="cache-miss"
						fi
					fi
				fi
			fi
							
			if [[ $4 ]]; then
				n_threads=$4
				use_cpus=$4
			fi
		fi
	fi
fi

echo "src=$src, max_work=$max_work, scheduling_method=$scheduling_method, n_threads=use_cpus=$n_threads";

sources="$src.c";
sources="$sources $src""_test.c cluster_scheduler.c";

# gcc on X86
flags="-g -O3 -msse3 -ftree-vectorize -rdynamic -ftree-vectorizer-verbose=0 -finline-functions -lpthread -march=native -mtune=native -DN_THREADS=$n_threads -DUSE_CPUS=$use_cpus -DMAX_WORK=$max_work -DSCHEDULING_METHOD=$scheduling_method_num -D_GNU_SOURCE -pipe"
include="-I. -Ipapi/include"
libs="-Lpapi/lib -lpapi"
compiler="gcc"

if [ "${sources#*.}" = "cpp" ]; then
  compiler="g++"
  include="${include} -I${TBBROOT}/include"
  libs="${libs} -ltbb"
fi

${compiler} ${sources} ${flags} ${include} ${libs}

export LD_PRELOAD=./libjemalloc.so

#./a.out 0 0 0
