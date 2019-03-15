#!/bin/bash
export LANG=C
#export ARCH_DATA_MODEL=64
#export DISABLE_HOTSPOT_OS_VERSION_CHECK=ok
export ALT_BOOTDIR=/specific/disk1/home/mad/Galois-PQ/inputs/students/guy/project/jdk1.6.0_45 jvmg
export ALLOW_DOWNLOADS=true
#export USE_PRECOMPILED_HEADER=true
#export SKIP_DEBUG_BUILD=false
#export SKIP_FASTDEBUG_BUILD=true
#export DEBUG_NAME=debug
unset CLASSPATH
unset JAVA_HOME
python -m SimpleHTTPServer 22122 &
#make clean && make sanity && make
#make sanity && make
#export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/specific/disk1/home/mad/Galois-PQ/inputs/students/guy/project/jvm-gc-fifo/msqueue-scheduling:/specific/disk1/home/mad/Galois-PQ/inputs/students/guy/project/jvm-gc-fifo/msqueue-scheduling/papi/lib
make
killall python
