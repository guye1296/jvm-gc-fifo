#include "utilities/macros.hpp"
#ifdef INTER_NODE_MSG_Q
#include "gc_implementation/parallelScavenge/psTaskTerminator.hpp"

#define LOCAL_VAR_FOR_LOOPING                                               \
  uint yield_count = 0;                                                     \
  /* Number of hard spin loops done since last yield*/                      \
  uint hard_spin_count = 0;                                                 \
  /* Number of iterations in the hard spin loop.*/                          \
  uint hard_spin_limit = WorkStealingHardSpins;                             \
  /* If WorkStealingSpinToYieldRatio is 0, no hard spinning is done.*/      \
  /* If it is greater than 0, then start with a small number*/              \
  /* of spins and increase number with each turn at spinning until*/        \
  /* the count of hard spins exceeds WorkStealingSpinToYieldRatio.*/        \
  /* Then do a yield() call and start spinning afresh.*/                    \
  if (WorkStealingSpinToYieldRatio > 0) {                                   \
    hard_spin_limit = WorkStealingHardSpins >> WorkStealingSpinToYieldRatio;\
    hard_spin_limit = MAX2(hard_spin_limit, 1U);                            \
  }                                                                         \
  /* Remember the initial spin limit.*/                                     \
  uint hard_spin_start = hard_spin_limit;                                   \

#define LOOPING_FOR_WAITING                                                    \
  /* Look for more work.*/                                                     \
  /* Periodically sleep() instead of yield() to give threads*/                 \
  /* waiting on the cores the chance to grab this code*/                       \
  if (yield_count <= WorkStealingYieldsBeforeSleep) {                          \
    /* Do a yield or hardspin.  For purposes of deciding whether*/             \
    /* to sleep, count this as a yield.*/                                      \
    yield_count++;                                                             \
    /* Periodically call yield() instead spinning*/                            \
    /* After WorkStealingSpinToYieldRatio spins, do a yield() call*/           \
    /* and reset the counts and starting limit.*/                              \
    if (hard_spin_count > WorkStealingSpinToYieldRatio) {                      \
      yield();                                                                 \
      hard_spin_count = 0;                                                     \
      hard_spin_limit = hard_spin_start;                                       \
    } else {                                                                   \
      /* Hard spin this time*/                                                 \
      /* Increase the hard spinning period but only up to a limit.*/           \
      hard_spin_limit = MIN2(2*hard_spin_limit,                                \
                             (uint) WorkStealingHardSpins);                    \
      for (uint j = 0; j < hard_spin_limit; j++) {                             \
        SpinPause();                                                           \
      }                                                                        \
      hard_spin_count++;                                                       \
    }                                                                          \
  } else {                                                                     \
    yield_count = 0;                                                           \
    /* A sleep will cause this processor to seek work on another processor's*/ \
    /* runqueue, if it has nothing else to run (as opposed to the yield*/      \
    /* which may only move the thread to the end of the this processor's*/     \
    /* runqueue).*/                                                            \
    sleep(WorkStealingSleepMillis);                                            \
  }                                                                            \

// This handles phase 2 of 2 phase termination protocol
#ifdef INTER_NODE_STEALING
bool NUMAGlobalTerminator::offer_termination(intptr_t& g_ts, intptr_t& l_ts,
                                                       intptr_t node_bitmap) {
#else
bool NUMAGlobalTerminator::offer_termination(intptr_t& ts) {
#endif
  assert(_nb_terminating < _nb_threads, err_msg("sanity nbt:%ld", _nb_terminating));
  LOCAL_VAR_FOR_LOOPING
  intptr_t nb_threads = _nb_threads;
  NUMANodeLocalTerminator* local_terminator = 
       _node_terminators->at(Thread::current()->lgrp_id());
  // Check first if in meantime some work has come. This is to support the while(true)
  // loop in the StealTask::do_it function.
#ifdef INTER_NODE_STEALING
  if (peek_for_non_leaders(node_bitmap))
#else
  if (peek_in_queue_set())
#endif
    return false;
#ifdef INTER_NODE_STEALING
  uint ret = _offer_termination(g_ts, l_ts, node_bitmap, local_terminator);
#else
  uint ret = _offer_termination(ts, local_terminator);
#endif
  if (ret == 2) {
    return true;
  } else if (ret == 0) {
    return false;
  } else if (ret == 1) {
    if (peek_in_msg_queue_set()) {
      // found work to do, go out
      Atomic::cmpxchg_ptr(0, &_nb_terminating, nb_threads);
#ifdef INTER_NODE_STEALING
      local_terminator->_timestamp = g_ts;
      local_terminator->_claim_q_node_steal = 0;
      local_terminator->_nb_terminating = 0;
      local_terminator->_local_ts = ++l_ts;
#else
      local_terminator->_timestamp = ts;
      Atomic::dec_ptr(&local_terminator->_nb_terminating);
#endif
      return false;
    }
    if (Atomic::add_ptr(1, &_nb_finishing) == nb_threads) {
      _done = true;
    }
  }
  while(true) {
    if (_done) {
      local_terminator->_done = true;
      return true;
    }
    LOOPING_FOR_WAITING
    if (_nb_terminating < nb_threads) {
#ifdef INTER_NODE_STEALING
      local_terminator->_timestamp = g_ts;
      local_terminator->_claim_q_node_steal = 0;
      local_terminator->_nb_terminating = 0;
      local_terminator->_local_ts = ++l_ts;
#else
      local_terminator->_timestamp = ts;
      Atomic::dec_ptr(&local_terminator->_nb_terminating);
#endif
      return false;
    }
  }
}

// The return values have following meaning:
// 0: We found some work, so let us go out
// 1: I am the leader, let me participate in the phase 2 of termination
// 2: We are non-leader, our leader said we are done, so let us go

// This handles phase 1 of 2 phase termination protocol
// This version will not use local timestamps
#ifdef INTER_NODE_STEALING
uint NUMAGlobalTerminator::_offer_termination(intptr_t& g_ts, intptr_t& l_ts,
                                              intptr_t node_bitmap,
                                              NUMANodeLocalTerminator* local_terminator) {
#else
uint NUMAGlobalTerminator::_offer_termination(intptr_t& ts,
                  NUMANodeLocalTerminator* local_terminator) {
#endif
  // prefetch will come here.
  LOCAL_VAR_FOR_LOOPING
  intptr_t nb_thread = local_terminator->_nb_threads;
  assert(local_terminator->_nb_terminating < nb_thread,
         err_msg("sanity local_nbt:%ld", local_terminator->_nb_terminating));

  if (Atomic::add_ptr(1, &local_terminator->_nb_terminating) < nb_thread) {
#ifdef INTER_NODE_STEALING
    return non_leader_termination(g_ts, l_ts, node_bitmap, local_terminator);
#else
    return non_leader_termination(ts, local_terminator);
#endif
  } else { // I am the leader
    // Before doing the inter-node increment, check once more.
#ifdef INTER_NODE_STEALING
    assert(local_terminator->_local_ts == l_ts, "sanity");
    local_terminator->_local_ts = ++l_ts;
#endif
    assert(local_terminator->_nb_terminating == nb_thread, err_msg("sanity %ld", local_terminator->_nb_terminating));
#ifdef INTER_NODE_STEALING
    if (peek_for_leader()) {
      local_terminator->_claim_q_node_steal = 0;
      local_terminator->_nb_terminating = 0;
      local_terminator->_local_ts = ++l_ts;
#else
    if (peek_in_msg_queue_set()) {
      Atomic::dec_ptr(&local_terminator->_nb_terminating);
#endif
      assert(local_terminator->_nb_terminating < nb_thread,
                err_msg("sanity local_nbt:%ld", local_terminator->_nb_terminating));
      return 0;
    }
    intptr_t global_nb_threads = _nb_threads;
    assert(_nb_terminating < global_nb_threads,
              err_msg("sanity nbt:%ld", _nb_terminating));
    if (Atomic::add_ptr(1, &_nb_terminating) == global_nb_threads) {
      _nb_finishing = 0;
#ifdef INTER_NODE_STEALING
      _timestamp = ++g_ts;
#else
      _timestamp = ++ts;
#endif
      return 1;
    }
    // I am not the global leader. So just loop until someone gets elected as
    // global leader.
    while(true) {
#ifdef INTER_NODE_STEALING
      if (_timestamp > g_ts) {
        g_ts = _timestamp;
#else
      if (_timestamp > ts) {
        ts = _timestamp;
#endif
        return 1;
      }
      LOOPING_FOR_WAITING
#ifdef INTER_NODE_STEALING
      if (peek_for_leader()) {
#else
      if (peek_in_msg_queue_set()) {
#endif
        intptr_t ret = _nb_terminating;
        while (ret < global_nb_threads) {
          intptr_t temp = Atomic::cmpxchg_ptr(ret - 1, &_nb_terminating, ret);
          if (temp == ret) {
            // we succedded
#ifdef INTER_NODE_STEALING
            local_terminator->_claim_q_node_steal = 0;
            local_terminator->_nb_terminating = 0;
            local_terminator->_local_ts = ++l_ts;
#else
            Atomic::dec_ptr(&local_terminator->_nb_terminating);
#endif
            return 0;
          }
          ret = temp;
        }
      }
    }
  }
}

// This handles node local termination. The non leader threads loops in here.
#ifdef INTER_NODE_STEALING
uint NUMAGlobalTerminator::non_leader_termination(intptr_t& g_ts, intptr_t& l_ts,
                                                  intptr_t node_bitmap,
                                                  NUMANodeLocalTerminator* local_terminator) {
#else
uint NUMAGlobalTerminator::non_leader_termination(intptr_t& ts,
                      NUMANodeLocalTerminator* local_terminator) {
#endif
  LOCAL_VAR_FOR_LOOPING
  intptr_t nb_threads = local_terminator->_nb_threads;
  while(true) {
    if (local_terminator->_done) {
      return 2;
    } 
    // We loop and do nothing.
    LOOPING_FOR_WAITING
#ifdef INTER_NODE_STEALING
    if (l_ts == local_terminator->_local_ts &&
        local_terminator->_nb_terminating < nb_threads) {
      // Check for work and get out if u find work.
      if (peek_for_non_leaders(node_bitmap)) {
        intptr_t ret = local_terminator->_nb_terminating;
        while (l_ts == local_terminator->_local_ts && ret < nb_threads) {
           intptr_t temp;
           temp = Atomic::cmpxchg_ptr(ret - 1,
                   &local_terminator->_nb_terminating, ret);
          if (temp == ret) {
            goto out;
#else
    if (local_terminator->_nb_terminating < nb_threads) {
      // Check for work and get out if u find work.
      if (peek_in_queue_set()) {
        intptr_t ret = local_terminator->_nb_terminating;
        while (ret < nb_threads) {
          intptr_t temp;
          temp = Atomic::cmpxchg_ptr(ret - 1, 
                  &local_terminator->_nb_terminating, ret);
          if (temp == ret) {           
            ts = local_terminator->_timestamp;
            return 0;
#endif
          }
          ret = temp;
        }
      }
    }
#ifdef INTER_NODE_STEALING 
    if (l_ts + 2 == local_terminator->_local_ts) {
out:
      l_ts = local_terminator->_local_ts;
      g_ts = local_terminator->_timestamp;
      return 0;
    }
#endif
  }
}
#endif //INTER_NODE_MSG_Q
