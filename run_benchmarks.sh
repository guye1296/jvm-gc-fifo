#!/bin/bash

#QUEUES=( 'msqueue' 'hlcrq')
QUEUES=( 'msqueue')
NUMA_METHODS=( 'work-time-queue' 'no-scheduling' 'primitive' 'ref-count' 'cache-miss')
THREADS=( 8 16 32 48 64 80 96 112 128 144 160 176)

QUEUES_DIR=`pwd`/msqueue-scheduling

export MY_JAVA=`pwd`/build/linux-amd64/bin/java
SPEC_DIR=/a/home/cc/students/cs/guyezer/shared_folder/guy/SPECjvm2008



for QUEUE in "${QUEUES[@]}"; do
    # TODO: recompile JVM to support the given queue

    for METHOD in "${NUMA_METHODS[@]}"; do
        for CORES in "${THREADS[@]}"; do

            echo Compiling libnuma_queue with $CORES cores, using: $QUEUE and with numa scheduling method: $METHOD
            
            # locate scheduling method number
            
            SCHEDULING_METHOD_NUM=0
            
            case $METHOD in
            	'work-time-queue')
            		SCHEDULING_METHOD_NUM=4;;
            	'primitive')
            		SCHEDULING_METHOD_NUM=1;;
            	'ref-count' )
            		SCHEDULING_METHOD_NUM=2;;
            	'cache-miss' )
            		SCHEDULING_METHOD_NUM=3
	    esac
			
	    # compile libnuma_queue
            pushd . > /dev/null
            cd $QUEUES_DIR/
            make $QUEUE n_threads=$CORES scheduling_method_num=$SCHEDULING_METHOD_NUM || exit
            
            popd > /dev/null

            echo running with $CORES cores

            # set LD_PRELOAD
            export LD_PRELOAD="$QUEUES_DIR/libnuma_queue.so $QUEUES_DIR/papi/lib/libpapi.so $QUEUES_DIR/libjemalloc.so"

            # run benchmark
            pushd . > /dev/null
            cd $SPEC_DIR
            LOG_DIR=$QUEUE/$METHOD
            echo $LOG_DIR
            mkdir -p $LOG_DIR
            LOG_FILE=$LOG_DIR/$CORES.log
            $MY_JAVA -Xbootclasspath/p:lib/javac.jar  -XX:+UseParallelOldGC -XX:ParallelGCThreads=$CORES -Xmx1g -Xms1g -XX:+UseNUMA  -jar SPECjvm2008.jar \
                -ikv --lagom -i 1 -ops 40 -bt 48 xml.transform > $LOG_FILE

            popd > /dev/null
            
            echo finished running with $CORES cores

        done

        # generate csv report for all cores
        cd $SPEC_DIR/$QUEUE/$METHOD
        pushd . > /dev/null
        for BENCHMARK_LOG_FILE in *.log; do
            THROUGHPUT=$(awk '/(Young work .*)|(Young GC Time.*)/ { print $4 }' $LOG_FILE | xargs echo | awk '{ print int($1/$2) }')
            FILENAME=$BENCHMARK_LOG_FILE
            FILENAME="${FILENAME%.*}"
            echo $FILENAME,$THROUGHPUT >> throughput.csv
        done

        popd > /dev/null
    done
done
