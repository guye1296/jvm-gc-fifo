/*
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSYOUNGGEN_HPP
#define SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSYOUNGGEN_HPP

#include "gc_implementation/parallelScavenge/objectStartArray.hpp"
#include "gc_implementation/parallelScavenge/psGenerationCounters.hpp"
#include "gc_implementation/parallelScavenge/psVirtualspace.hpp"
#include "gc_implementation/shared/mutableSpace.hpp"
#include "gc_implementation/shared/spaceCounters.hpp"

class PSMarkSweepDecorator;

class PSYoungGen : public CHeapObj {
  friend class VMStructs;
  friend class ParallelScavengeHeap;
  friend class AdjoiningGenerations;

 protected:
  MemRegion       _reserved;
  PSVirtualSpace* _virtual_space;

  // Spaces
  MutableSpace* _eden_space;
  MutableSpace* _from_space;
  MutableSpace* _to_space;


  // MarkSweep Decorators
  PSMarkSweepDecorator* _eden_mark_sweep;
  PSMarkSweepDecorator* _from_mark_sweep;
  PSMarkSweepDecorator* _to_mark_sweep;

  // Sizing information, in bytes, set in constructor
  const size_t _init_gen_size;
  const size_t _min_gen_size;
  const size_t _max_gen_size;

  // Performance counters
  PSGenerationCounters*     _gen_counters;
  SpaceCounters*            _eden_counters;
  SpaceCounters*            _from_counters;
  SpaceCounters*            _to_counters;

  // Initialize the space boundaries
  void compute_initial_space_boundaries();

  // Space boundary helper
  void set_space_boundaries(size_t eden_size, size_t survivor_size);

#ifdef YOUNGGEN_8TIMES
  virtual bool resize_generation(size_t eden_size, size_t to_size, size_t from_size);
  virtual void resize_spaces(size_t requested_eden_size, size_t requested_to_size, size_t requested_from_size);
#else
  virtual bool resize_generation(size_t eden_size, size_t survivor_size);
  virtual void resize_spaces(size_t eden_size, size_t survivor_size);
#endif

  // Adjust the spaces to be consistent with the virtual space.
  void post_resize();

  // Return number of bytes that the generation can change.
  // These should not be used by PSYoungGen
  virtual size_t available_for_expansion();
  virtual size_t available_for_contraction();

  // Given a desired shrinkage in the size of the young generation,
  // return the actual size available for shrinkage.
  virtual size_t limit_gen_shrink(size_t desired_change);
  // returns the number of bytes available from the current size
  // down to the minimum generation size.
  size_t available_to_min_gen();
  // Return the number of bytes available for shrinkage considering
  // the location the live data in the generation.
  virtual size_t available_to_live();

 public:
  // Initialize the generation.
  PSYoungGen(size_t        initial_byte_size,
             size_t        minimum_byte_size,
             size_t        maximum_byte_size);
  void initialize_work();
  virtual void initialize(ReservedSpace rs, size_t alignment);
  virtual void initialize_virtual_space(ReservedSpace rs, size_t alignment);

  MemRegion reserved() const            { return _reserved; }

  bool is_in(const void* p) const   {
      return _virtual_space->contains((void *)p);
  }

  bool is_in_reserved(const void* p) const   {
      return reserved().contains((void *)p);
  }

  MutableSpace*   eden_space() const    { return _eden_space; }
  MutableSpace*   from_space() const    { return _from_space; }
  MutableSpace*   to_space() const      { return _to_space; }
  PSVirtualSpace* virtual_space() const { return _virtual_space; }

  // For Adaptive size policy
  size_t min_gen_size() { return _min_gen_size; }

  // MarkSweep support
  PSMarkSweepDecorator* eden_mark_sweep() const    { return _eden_mark_sweep; }
  PSMarkSweepDecorator* from_mark_sweep() const    { return _from_mark_sweep; }
  PSMarkSweepDecorator* to_mark_sweep() const      { return _to_mark_sweep;   }

  void precompact();
  void adjust_pointers();
  void compact();

  // Called during/after gc
  void swap_spaces();

  // Resize generation using suggested free space size and survivor size
  // NOTE:  "eden_size" and "survivor_size" are suggestions only. Current
  //        heap layout (particularly, live objects in from space) might
  //        not allow us to use these values.
#ifdef YOUNGGEN_8TIMES
  void resize(size_t eden_size, size_t to_size);
#else
  void resize(size_t eden_size, size_t survivor_size);
#endif

  // Size info
  size_t capacity_in_bytes() const;
  size_t used_in_bytes() const;
  size_t free_in_bytes() const;

  size_t capacity_in_words() const;
  size_t used_in_words() const;
  size_t free_in_words() const;

  // The max this generation can grow to
#ifdef OPTIMIZE_RESIZE
  size_t _no_resize_actual_max_size;
  size_t max_size() const {
    return _no_resize_actual_max_size;
  }
#else
  size_t max_size() const            { return _reserved.byte_size(); }
#endif
  // The max this generation can grow to if the boundary between
  // the generations are allowed to move.
  size_t gen_size_limit() const { return _max_gen_size; }

  bool is_maximal_no_gc() const {
    return true;  // never expands except at a GC
  }

  // Allocation
  HeapWord* allocate(size_t word_size, bool is_tlab) {
    HeapWord* result = eden_space()->cas_allocate(word_size);
    return result;
  }

  HeapWord** top_addr() const   { return eden_space()->top_addr(); }
  HeapWord** end_addr() const   { return eden_space()->end_addr(); }

  // Iteration.
  void oop_iterate(OopClosure* cl);
  void object_iterate(ObjectClosure* cl);

  virtual void reset_after_change();
  virtual void reset_survivors_after_shrink();

  // Performance Counter support
  void update_counters();

  // Debugging - do not use for time critical operations
  void print() const;
  void print_on(outputStream* st) const;
  void print_used_change(size_t prev_used) const;
  virtual const char* name() const { return "PSYoungGen"; }

  void verify(bool allow_dirty);

  // Space boundary invariant checker
  void space_invariants() PRODUCT_RETURN;

  // Helper for mangling survivor spaces.
  void mangle_survivors(MutableSpace* s1,
                        MemRegion s1MR,
                        MutableSpace* s2,
                        MemRegion s2MR) PRODUCT_RETURN;

  void record_spaces_top() PRODUCT_RETURN;
};

#endif // SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSYOUNGGEN_HPP
