SHELL=/bin/bash

n_threads=80
use_cpus=80
scheduling_method="work-time-queue"
max_work=4
scheduling_method_num=0

SRC_DIR= $(shell pwd)
CC=gcc
CFLAGS:=-g -O3 -msse3 -ftree-vectorize -rdynamic -ftree-vectorizer-verbose=0 -finline-functions -lpthread -march=native -mtune=native -DN_THREADS=$(n_threads) -DUSE_CPUS=$(use_cpus) -DMAX_WORK=$(max_work) -DSCHEDULING_METHOD=$(scheduling_method_num) -D_GNU_SOURCE -pipe 
INCLUDES=-I$(SRC_DIR)/src -I$(SRC_DIR)/. -I$(SRC_DIR)/papi/include -I$(SRC_DIR)/includes -I$(SRC_DIR)/MSQUEUE -I$(SRC_DIR)/HLCRQ
LIBS=-Wl,-rpath-link,$(SRC_DIR)/papi/lib -Wl,-rpath,$(SRC_DIR)/papi/lib -L$(SRC_DIR)/papi/lib -lpapi


SOURCES=$(SRC_DIR)/MSQUEUE/msqueue.c $(SRC_DIR)/MSQUEUE/msqueue_test.c $(SRC_DIR)/src/cluster_scheduler.c


msqueue: $(SRC_DIR)/MSQUEUE/msqueue.c $(SRC_DIR)/MSQUEUE/msqueue_test.c $(SRC_DIR)/src/cluster_scheduler.c
	$(CC) $(SOURCES) $(CFLAGS) $(INCLUDES) $(LIBS) -o msqueue

hlcrq: $(SRC_DIR)/HLCRQ/hlcrq.c $(SRC_DIR)/HLCRQ/hlcrq_test.c $(SRC_DIR)/src/cluster_scheduler.c
	SOURCES+= $(SRC_DIR)/HLCRQ/hlcrq.c 
	SOURCES+= $(SRC_DIR)/HLCRQ/hlcrq_test.c 
	$(CC) $(SOURCES) $(CFLAGS) $(INCLUDES) $(LIBS) -o hlcrq


shared:
	CFLAGS+="$(CFLAGS) -fPIC -shared"
	echo $(CFLAGS)
	$(CC) $(SOURCES) $(CFLAGS) $(INCLUDES) $(LIBS) -o shared
	

