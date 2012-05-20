#ifndef SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSTASKTERMINATOR_HPP
#define SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSTASKTERMINATOR_HPP

#include "utilities/taskqueue.hpp"
#include "utilities/growableArray.hpp"
#include "memory/allocation.hpp"
#include "runtime/os.hpp"
#include "runtime/thread.hpp"

class NUMANodeLocalTerminator: public CHeapObj {

public:
  // For prefetching the cache line in modified state.
  volatile intptr_t _dummy;
  volatile intptr_t _nb_terminating;
  volatile intptr_t _timestamp;
#ifdef INTER_NODE_STEALING
  volatile intptr_t _local_ts; // To enable setting _nb_terminating to 0 by the local leader.
  volatile intptr_t _claim_q_node_steal;// bitmap to claim a queue for stealing from other nodes.
#endif
  const intptr_t    _nb_threads;
  volatile bool     _done; // This is set by leader to inform others that the termination
                            // is successful.
  NUMANodeLocalTerminator(intptr_t tc) :
    _nb_terminating(0), _nb_threads(tc), _dummy(0),
#ifdef INTER_NODE_STEALING
    _local_ts(0), _claim_q_node_steal(0),
#endif
    _timestamp(0), _done(false) {
    if (UseNUMA)
      guarantee(((uintptr_t)this & 0x3f) == 0, "sanity");
  }
#ifdef INTER_NODE_STEALING
  bool claim_node_for_steal(uint lgrp_id) {
    intptr_t temp = _claim_q_node_steal;
    intptr_t tmp;
    do {
      tmp = temp;
      if (tmp & (1 << lgrp_id))
        return false; // The node has been already claimed by someone else
      temp = Atomic::cmpxchg(tmp | (1 << lgrp_id), &_claim_q_node_steal, tmp);
    } while (temp != tmp);
    return true;
  }
#endif
  void initialize() {
    _nb_terminating = _timestamp = 0;
#ifdef INTER_NODE_STEALING
    _claim_q_node_steal = _local_ts = 0;
#endif
    _done = false;
  }
} __attribute__ ((aligned(CACHE_LINE_SIZE)));

class NUMAGlobalTerminator: public CHeapObj {

  typedef GrowableArray<NUMANodeLocalTerminator*> node_local_terminator_list_t;
  // For prefetching the cache line in modified state.
  volatile intptr_t                         _dummy0;
  volatile intptr_t                         _nb_terminating;
  intptr_t                                  _nb_threads;
  volatile bool                             _done;
  // The new cache line starts here.
  volatile intptr_t                         _dummy1 __attribute__ ((aligned(CACHE_LINE_SIZE)));
  TaskQueueSetSuper*                        _msg_queue_set;
  TaskQueueSetSuper*                        _local_queue_set;
  node_local_terminator_list_t*             _node_terminators;
  volatile intptr_t                         _timestamp;
  volatile intptr_t                         _nb_finishing;
#ifdef INTER_NODE_STEALING
  uint _offer_termination(intptr_t& g_ts, intptr_t& l_ts, intptr_t node_bitmap,
                                     NUMANodeLocalTerminator* local_terminator);
  uint non_leader_termination(intptr_t& g_ts, intptr_t& l_ts, intptr_t node_bitmap,
                                         NUMANodeLocalTerminator* local_terminator);

  inline bool peek_in_local_q_set(intptr_t node_bitmap) {
    for (uint i = 0; i < os::numa_get_groups_num(); i++) {
      if (node_bitmap & (1 << i)) {
        if (_local_queue_set->peek(i)) {
          return true;
        }
      }
    }
    return false;
  }

  inline bool peek_in_all_local_q_set() {
    for (uint i = 0; i < os::numa_get_groups_num(); i++) {
      if (_local_queue_set->peek(i)) {
        return true;
      }
    }
    return false;
  }

  inline bool peek_for_leader() {
    return _msg_queue_set->peek() || peek_in_all_local_q_set();
  }

  inline bool peek_for_non_leaders(intptr_t node_bitmap) {
    return _msg_queue_set->peek() || peek_in_local_q_set(node_bitmap);
  }
#else
  uint _offer_termination(intptr_t& ts, NUMANodeLocalTerminator* local_terminator);
  uint non_leader_termination(intptr_t& ts, NUMANodeLocalTerminator* local_terminator);
#endif
  inline bool peek_in_queue_set() {
    return _msg_queue_set->peek() || _local_queue_set->peek();
  }
  inline bool peek_in_msg_queue_set() {
    return _msg_queue_set->peek();
  }

  inline void sleep(uint millis) { 
    os::sleep(Thread::current(), millis, false);
  }
  inline void yield() {
    os::yield();
  }
public:
  NUMAGlobalTerminator(intptr_t tc, TaskQueueSetSuper* msg_qs,
                       TaskQueueSetSuper* local_qs,
                       node_local_terminator_list_t* nt) :

    _nb_terminating(0), _nb_threads(tc), _nb_finishing(0),
    _msg_queue_set(msg_qs), _local_queue_set(local_qs),
    _node_terminators(nt), _timestamp(0), _done(false),
                               _dummy0(0), _dummy1(0)
  {
    intptr_t thread_count = 0;
    for (uint i = 0; i < tc; i++) {
      // The following if condition is required to be able to run the
      // jvm with less number of nodes than available on the machine.
      if (_node_terminators->at(i)->_nb_threads > 0) thread_count++;
      _node_terminators->at(i)->initialize();
    }
    _nb_threads = thread_count;
    if (UseNUMA)
      guarantee(((uintptr_t)this & 0x3f) == 0, err_msg("sanity: %p", this));
  }

  void initialize(intptr_t tc, TaskQueueSetSuper* msg_qs,
                            TaskQueueSetSuper* local_qs,
                        node_local_terminator_list_t* nt) {
    _nb_terminating = 0;
    _nb_threads = tc;
    _nb_finishing = 0;
    _msg_queue_set = msg_qs;
    _local_queue_set = local_qs;
    _node_terminators = nt;
    _timestamp = 0;
    _done = false;
    intptr_t thread_count = 0;
    for (uint i = 0; i < tc; i++) {
      if (_node_terminators->at(i)->_nb_threads > 0) thread_count++;
      _node_terminators->at(i)->initialize();
    }
    _nb_threads = thread_count;
  }
#ifdef INTER_NODE_STEALING
  NUMANodeLocalTerminator* terminator_on_node(uint i) {
    return _node_terminators->at(i);
  }

  bool offer_termination(intptr_t& g_ts, intptr_t& l_ts, intptr_t node_bitmap);
#else
  bool offer_termination(intptr_t& ts);
#endif
} __attribute__ ((aligned(CACHE_LINE_SIZE)));
#endif // SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSTASKTERMINATOR_HPP
