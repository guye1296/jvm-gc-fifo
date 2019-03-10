#!/bin/bash

rm -f a.out res.txt;

export LD_LIBRARY_PATH=~/SchedulingMSQUEUE/papi/lib/

src="$1.c";
src="$src $1_test.c cluster_scheduler.c";
 
#echo "Apropriate script use: run.sh FILE N_THREADS LOWER_BACKOFF UPPER_BACKOFF";
#echo "This script runs FILE 10 times using N_THREADS and calulates average exection time";

# Please uncomment one of the following compiling instruction
# that fits to your needs.

# gcc on X86
flags="-g -O3 -msse3 -ftree-vectorize -ftree-vectorizer-verbose=0 -finline-functions -lpthread -march=native -mtune=native -DN_THREADS=$2 -DUSE_CPUS=$3 -DMAX_WORK=$4 -DORIG_OP_ORDER=$5 -D_GNU_SOURCE -pipe -DSCHEDULING_METHOD=$6"
include="-I. -Ipapi/include"
libs="-Lpapi/lib -lpapi"
compiler="gcc"

if [ "${src#*.}" = "cpp" ]; then
  compiler="g++"
  include="${include} -I${TBBROOT}/include"
  libs="${libs} -ltbb"
fi

${compiler} ${src} ${flags} ${include} ${libs}

#gcc $1 -g -O3 -msse3 -ftree-vectorize -ftree-vectorizer-verbose=0 -finline-functions -lpthread -march=native -mtune=native -DN_THREADS=$2 -DUSE_CPUS=$3 -DMAX_WORK=$4 -D_GNU_SOURCE -pipe -I${PAPI_HOME}/include -L${PAPI_HOME}/lib -lpapi

# icc on X86
#icc $1 -xW -gcc -O3 -ipo -pthread -DN_THREADS=$2 -DUSE_CPUS=$3 -D_GNU_SOURCE

# gcc on SPARC niagara2
#gcc $1 -O3 -Wall -mtune=niagara2 -m32 -lrt -lpthread -DN_THREADS=$2 -DUSE_CPUS=$3 -D_GNU_SOURCE

# suncc on SPARC niagara2
#cc $1 -m32 -O5 -fast -lrt -mt -DSPARC -D_REENTRANT -DSPARC -DN_THREADS=$2 -DUSE_CPUS=$3 ./sparc.il 


export LD_PRELOAD=./libjemalloc.so

for a in {1..2};do
    rc=1
    while [[ $rc != 0 ]]
    do
    	out=`./a.out 0 0 0`
    	rc=$?
    done

    echo -n "$1 " >> res.txt
echo "work=$4 op_order=$5 scheduling_method_num=$6 ${out}" >> res.txt;
    tail -1 res.txt;
done;

exit 0

awk 'BEGIN {time = 0;
            failed_cas = 0;
            executed_cas = 0;
            successful_cas = 0;
            executed_swap = 0;
            executed_faa = 0;
            atomics = 0;
            atomics_per_op = 0;
            ops_per_cas = 0;
            i = 0}
            {time += $2;
            failed_cas += $4; 
            executed_cas += $6;
            successful_cas += $8;
            executed_swap += $10;
            executed_faa += $12;
            atomics += $14;
            atomics_per_op += $16;
            ops_per_cas += $18;
            i += 1} 
     END {time = time/i;             print "\naverage time: \t", time, "";
            failed_cas = failed_cas/i; print "failed cas: \t", failed_cas, "";
            executed_cas =executed_cas/i; print "executed cas: \t", executed_cas, "";
            successful_cas = successful_cas/i; print "successful cas: ", successful_cas, "";
            executed_swap = executed_swap/i; print "executed swap: \t", executed_swap, "";
            executed_faa = executed_faa/i; print "executed faa: \t", executed_faa, "";
            atomics = atomics/i; print "atomics: \t", atomics, "";
            atomics_per_op = atomics_per_op/i; print "atomics per op: ", atomics_per_op, "\n";
            ops_per_cas = ops_per_cas/i; print "operations per cas: ", ops_per_cas, "\n";
         }' res.txt
