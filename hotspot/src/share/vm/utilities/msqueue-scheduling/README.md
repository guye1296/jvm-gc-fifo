Scripts
-------
- Fill your details in the configuration file
- Copy file to remote machine:
  * Files are copied from local_project_path to remote_project_path
  * All files are copied to one directory (remote_project_path)
  * libjemalloc.so & papi directory aren't copied, you should copy it by yourself
- Connect to remote machine:
  1. cd to scripts directory
  2. call to nova_remote_connect.sh
- compile & run:
  1. connect to remote machine
  2. cd to remote_project_path
  3. call to compile.sh <max_work> <scheduling_method_num> <n_threads> 
     for example: compile.sh 5 2 80
  4. to run the exec call to  run.sh

Code structure
---------------
- includes: includes files and defines
- scripts: above scripts
- MSQUEUE: main (msqueue_test.c) and msqueue functions (msqueue.h)
- src: the code we change
  - cluster_scheduler_include.h: all data structers (also the msqueue structures [lior TODO: need to be restructure files]) and needed includes
  - file per scheduling method (numbers are representing the scheduling_method_num):
    0. no_scheduling - cluster_no_scheduling.h
    1. primitive - cluster_scheduler_primitive.h
    2. ref_count - cluster_scheduler_ref_count.h
    3. cache_miss - cluster_scheduler_cache_miss.h
    4. work_time_queue - cluster_scheduler_work_time_queue.h
    
