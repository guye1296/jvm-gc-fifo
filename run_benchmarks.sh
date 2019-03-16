#!/bin/bash

QUEUES=( 'msqueue' 'hlcrq')
NUMA_METHODS=( 'work-time-queue' 'no-scheduling' 'primitive' 'ref-count' 'cache-miss')
THREADS=( 1 2 4 8 16 32 48 64 80 96 112 128 144 160 176)

QUEUES_DIR=`pwd`/msqueue-scheduling

# set LD_PRELOAD

for QUEUE in "${QUEUES[@]}"; do
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
            pushd .
            cd $QUEUES_DIR/
            make $QUEUE use_cpus=$CORES scheduling_method_num=$SCHEDULING_METHOD_NUM
            popd



        done
    done
done




