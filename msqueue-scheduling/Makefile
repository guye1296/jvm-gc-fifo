SHELL=/bin/bash

n_threads=80
use_cpus=80
scheduling_method="work-time-queue"
max_work=4
scheduling_method_num=0

SRC_DIR= $(shell pwd)
CC=gcc
CFLAGS:=-g -O3 -msse3 -ftree-vectorize -rdynamic -ftree-vectorizer-verbose=0 -finline-functions -lpthread -march=native -mtune=native -DN_THREADS=$(n_threads) -DUSE_CPUS=$(use_cpus) -DMAX_WORK=$(max_work) -DSCHEDULING_METHOD=$(scheduling_method_num) -D_GNU_SOURCE 
INCLUDES=-I$(SRC_DIR)/. -I$(SRC_DIR)/src -I$(SRC_DIR)/includes -I$(SRC_DIR)/papi/src   
LIBS=-Wl,-rpath-link,$(SRC_DIR)/papi/src -Wl,-rpath,$(SRC_DIR)/papi/src -L$(SRC_DIR)/papi/src -lpapi


SOURCES=$(SRC_DIR)/src/cluster_scheduler.c $(SRC_DIR)/numa_queue.c


msqueue: $(SRC_DIR)/MSQUEUE/msqueue.c $(SRC_DIR)/src/cluster_scheduler.c $(SRC_DIR)/numa_queue.c
	$(CC) $(SRC_DIR)/MSQUEUE/msqueue.c $(SOURCES) $(CFLAGS) -DMSQUEUE -pipe -fPIC -shared $(INCLUDES) -I$(SRC_DIR)/MSQUEUE $(LIBS) -o libmsqueue.so

hlcrq: $(SRC_DIR)/HLCRQ/hlcrq.c $(SRC_DIR)/src/cluster_scheduler.c $(SRC_DIR)/numa_queue.c
	$(CC) $(SRC_DIR)/HLCRQ/hlcrq.c $(SOURCES) $(CFLAGS) -DHLCRQ -pipe -fPIC -shared $(INCLUDES) -I$(SRC_DIR)/HLCRQ $(LIBS) -o libhlcrq.so

	
