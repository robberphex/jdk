/*
 * Copyright 2001-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 */

# include "incls/_precompiled.incl"
# include "incls/_defNewGeneration.cpp.incl"

//
// DefNewGeneration functions.

// Methods of protected closure types.

DefNewGeneration::IsAliveClosure::IsAliveClosure(Generation* g) : _g(g) {
  assert(g->level() == 0, "Optimized for youngest gen.");
}
void DefNewGeneration::IsAliveClosure::do_object(oop p) {
  assert(false, "Do not call.");
}
bool DefNewGeneration::IsAliveClosure::do_object_b(oop p) {
  return (HeapWord*)p >= _g->reserved().end() || p->is_forwarded();
}

DefNewGeneration::KeepAliveClosure::
KeepAliveClosure(ScanWeakRefClosure* cl) : _cl(cl) {
  GenRemSet* rs = GenCollectedHeap::heap()->rem_set();
  assert(rs->rs_kind() == GenRemSet::CardTable, "Wrong rem set kind.");
  _rs = (CardTableRS*)rs;
}

void DefNewGeneration::KeepAliveClosure::do_oop(oop* p) {
  // We never expect to see a null reference being processed
  // as a weak reference.
  assert (*p != NULL, "expected non-null ref");
  assert ((*p)->is_oop(), "expected an oop while scanning weak refs");

  _cl->do_oop_nv(p);

  // Card marking is trickier for weak refs.
  // This oop is a 'next' field which was filled in while we
  // were discovering weak references. While we might not need
  // to take a special action to keep this reference alive, we
  // will need to dirty a card as the field was modified.
  //
  // Alternatively, we could create a method which iterates through
  // each generation, allowing them in turn to examine the modified
  // field.
  //
  // We could check that p is also in an older generation, but
  // dirty cards in the youngest gen are never scanned, so the
  // extra check probably isn't worthwhile.
  if (Universe::heap()->is_in_reserved(p)) {
    _rs->inline_write_ref_field_gc(p, *p);
  }
}

DefNewGeneration::FastKeepAliveClosure::
FastKeepAliveClosure(DefNewGeneration* g, ScanWeakRefClosure* cl) :
  DefNewGeneration::KeepAliveClosure(cl) {
  _boundary = g->reserved().end();
}

void DefNewGeneration::FastKeepAliveClosure::do_oop(oop* p) {
  assert (*p != NULL, "expected non-null ref");
  assert ((*p)->is_oop(), "expected an oop while scanning weak refs");

  _cl->do_oop_nv(p);

  // Optimized for Defnew generation if it's the youngest generation:
  // we set a younger_gen card if we have an older->youngest
  // generation pointer.
  if (((HeapWord*)(*p) < _boundary) && Universe::heap()->is_in_reserved(p)) {
    _rs->inline_write_ref_field_gc(p, *p);
  }
}

DefNewGeneration::EvacuateFollowersClosure::
EvacuateFollowersClosure(GenCollectedHeap* gch, int level,
                         ScanClosure* cur, ScanClosure* older) :
  _gch(gch), _level(level),
  _scan_cur_or_nonheap(cur), _scan_older(older)
{}

void DefNewGeneration::EvacuateFollowersClosure::do_void() {
  do {
    _gch->oop_since_save_marks_iterate(_level, _scan_cur_or_nonheap,
                                       _scan_older);
  } while (!_gch->no_allocs_since_save_marks(_level));
}

DefNewGeneration::FastEvacuateFollowersClosure::
FastEvacuateFollowersClosure(GenCollectedHeap* gch, int level,
                             DefNewGeneration* gen,
                             FastScanClosure* cur, FastScanClosure* older) :
  _gch(gch), _level(level), _gen(gen),
  _scan_cur_or_nonheap(cur), _scan_older(older)
{}

void DefNewGeneration::FastEvacuateFollowersClosure::do_void() {
  do {
    _gch->oop_since_save_marks_iterate(_level, _scan_cur_or_nonheap,
                                       _scan_older);
  } while (!_gch->no_allocs_since_save_marks(_level));
  guarantee(_gen->promo_failure_scan_stack() == NULL
            || _gen->promo_failure_scan_stack()->length() == 0,
            "Failed to finish scan");
}

ScanClosure::ScanClosure(DefNewGeneration* g, bool gc_barrier) :
  OopsInGenClosure(g), _g(g), _gc_barrier(gc_barrier)
{
  assert(_g->level() == 0, "Optimized for youngest generation");
  _boundary = _g->reserved().end();
}

FastScanClosure::FastScanClosure(DefNewGeneration* g, bool gc_barrier) :
  OopsInGenClosure(g), _g(g), _gc_barrier(gc_barrier)
{
  assert(_g->level() == 0, "Optimized for youngest generation");
  _boundary = _g->reserved().end();
}

ScanWeakRefClosure::ScanWeakRefClosure(DefNewGeneration* g) :
  OopClosure(g->ref_processor()), _g(g)
{
  assert(_g->level() == 0, "Optimized for youngest generation");
  _boundary = _g->reserved().end();
}


DefNewGeneration::DefNewGeneration(ReservedSpace rs,
                                   size_t initial_size,
                                   int level,
                                   const char* policy)
  : Generation(rs, initial_size, level),
    _objs_with_preserved_marks(NULL),
    _preserved_marks_of_objs(NULL),
    _promo_failure_scan_stack(NULL),
    _promo_failure_drain_in_progress(false),
    _should_allocate_from_space(false)
{
  MemRegion cmr((HeapWord*)_virtual_space.low(),
                (HeapWord*)_virtual_space.high());
  Universe::heap()->barrier_set()->resize_covered_region(cmr);

  if (GenCollectedHeap::heap()->collector_policy()->has_soft_ended_eden()) {
    _eden_space = new ConcEdenSpace(this);
  } else {
    _eden_space = new EdenSpace(this);
  }
  _from_space = new ContiguousSpace();
  _to_space   = new ContiguousSpace();

  if (_eden_space == NULL || _from_space == NULL || _to_space == NULL)
    vm_exit_during_initialization("Could not allocate a new gen space");

  // Compute the maximum eden and survivor space sizes. These sizes
  // are computed assuming the entire reserved space is committed.
  // These values are exported as performance counters.
  uintx alignment = GenCollectedHeap::heap()->collector_policy()->min_alignment();
  uintx size = _virtual_space.reserved_size();
  _max_survivor_size = compute_survivor_size(size, alignment);
  _max_eden_size = size - (2*_max_survivor_size);

  // allocate the performance counters

  // Generation counters -- generation 0, 3 subspaces
  _gen_counters = new GenerationCounters("new", 0, 3, &_virtual_space);
  _gc_counters = new CollectorCounters(policy, 0);

  _eden_counters = new CSpaceCounters("eden", 0, _max_eden_size, _eden_space,
                                      _gen_counters);
  _from_counters = new CSpaceCounters("s0", 1, _max_survivor_size, _from_space,
                                      _gen_counters);
  _to_counters = new CSpaceCounters("s1", 2, _max_survivor_size, _to_space,
                                    _gen_counters);

  compute_space_boundaries(0);
  update_counters();
  _next_gen = NULL;
  _tenuring_threshold = MaxTenuringThreshold;
  _pretenure_size_threshold_words = PretenureSizeThreshold >> LogHeapWordSize;
}

void DefNewGeneration::compute_space_boundaries(uintx minimum_eden_size) {
  uintx alignment = GenCollectedHeap::heap()->collector_policy()->min_alignment();

  // Compute sizes
  uintx size = _virtual_space.committed_size();
  uintx survivor_size = compute_survivor_size(size, alignment);
  uintx eden_size = size - (2*survivor_size);
  assert(eden_size > 0 && survivor_size <= eden_size, "just checking");

  if (eden_size < minimum_eden_size) {
    // May happen due to 64Kb rounding, if so adjust eden size back up
    minimum_eden_size = align_size_up(minimum_eden_size, alignment);
    uintx maximum_survivor_size = (size - minimum_eden_size) / 2;
    uintx unaligned_survivor_size =
      align_size_down(maximum_survivor_size, alignment);
    survivor_size = MAX2(unaligned_survivor_size, alignment);
    eden_size = size - (2*survivor_size);
    assert(eden_size > 0 && survivor_size <= eden_size, "just checking");
    assert(eden_size >= minimum_eden_size, "just checking");
  }

  char *eden_start = _virtual_space.low();
  char *from_start = eden_start + eden_size;
  char *to_start   = from_start + survivor_size;
  char *to_end     = to_start   + survivor_size;

  assert(to_end == _virtual_space.high(), "just checking");
  assert(Space::is_aligned((HeapWord*)eden_start), "checking alignment");
  assert(Space::is_aligned((HeapWord*)from_start), "checking alignment");
  assert(Space::is_aligned((HeapWord*)to_start),   "checking alignment");

  MemRegion edenMR((HeapWord*)eden_start, (HeapWord*)from_start);
  MemRegion fromMR((HeapWord*)from_start, (HeapWord*)to_start);
  MemRegion toMR  ((HeapWord*)to_start, (HeapWord*)to_end);

  eden()->initialize(edenMR, (minimum_eden_size == 0));
  // If minumum_eden_size != 0, we will not have cleared any
  // portion of eden above its top. This can cause newly
  // expanded space not to be mangled if using ZapUnusedHeapArea.
  // We explicitly do such mangling here.
  if (ZapUnusedHeapArea && (minimum_eden_size != 0)) {
    eden()->mangle_unused_area();
  }
  from()->initialize(fromMR, true);
    to()->initialize(toMR  , true);
  eden()->set_next_compaction_space(from());
  // The to-space is normally empty before a compaction so need
  // not be considered.  The exception is during promotion
  // failure handling when to-space can contain live objects.
  from()->set_next_compaction_space(NULL);
}

void DefNewGeneration::swap_spaces() {
  ContiguousSpace* s = from();
  _from_space        = to();
  _to_space          = s;
  eden()->set_next_compaction_space(from());
  // The to-space is normally empty before a compaction so need
  // not be considered.  The exception is during promotion
  // failure handling when to-space can contain live objects.
  from()->set_next_compaction_space(NULL);

  if (UsePerfData) {
    CSpaceCounters* c = _from_counters;
    _from_counters = _to_counters;
    _to_counters = c;
  }
}

bool DefNewGeneration::expand(size_t bytes) {
  MutexLocker x(ExpandHeap_lock);
  bool success = _virtual_space.expand_by(bytes);

  // Do not attempt an expand-to-the reserve size.  The
  // request should properly observe the maximum size of
  // the generation so an expand-to-reserve should be
  // unnecessary.  Also a second call to expand-to-reserve
  // value potentially can cause an undue expansion.
  // For example if the first expand fail for unknown reasons,
  // but the second succeeds and expands the heap to its maximum
  // value.
  if (GC_locker::is_active()) {
    if (PrintGC && Verbose) {
      gclog_or_tty->print_cr("Garbage collection disabled, expanded heap instead");
    }
  }

  return success;
}


void DefNewGeneration::compute_new_size() {
  // This is called after a gc that includes the following generation
  // (which is required to exist.)  So from-space will normally be empty.
  // Note that we check both spaces, since if scavenge failed they revert roles.
  // If not we bail out (otherwise we would have to relocate the objects)
  if (!from()->is_empty() || !to()->is_empty()) {
    return;
  }

  int next_level = level() + 1;
  GenCollectedHeap* gch = GenCollectedHeap::heap();
  assert(next_level < gch->_n_gens,
         "DefNewGeneration cannot be an oldest gen");

  Generation* next_gen = gch->_gens[next_level];
  size_t old_size = next_gen->capacity();
  size_t new_size_before = _virtual_space.committed_size();
  size_t min_new_size = spec()->init_size();
  size_t max_new_size = reserved().byte_size();
  assert(min_new_size <= new_size_before &&
         new_size_before <= max_new_size,
         "just checking");
  // All space sizes must be multiples of Generation::GenGrain.
  size_t alignment = Generation::GenGrain;

  // Compute desired new generation size based on NewRatio and
  // NewSizeThreadIncrease
  size_t desired_new_size = old_size/NewRatio;
  int threads_count = Threads::number_of_non_daemon_threads();
  size_t thread_increase_size = threads_count * NewSizeThreadIncrease;
  desired_new_size = align_size_up(desired_new_size + thread_increase_size, alignment);

  // Adjust new generation size
  desired_new_size = MAX2(MIN2(desired_new_size, max_new_size), min_new_size);
  assert(desired_new_size <= max_new_size, "just checking");

  bool changed = false;
  if (desired_new_size > new_size_before) {
    size_t change = desired_new_size - new_size_before;
    assert(change % alignment == 0, "just checking");
    if (expand(change)) {
       changed = true;
    }
    // If the heap failed to expand to the desired size,
    // "changed" will be false.  If the expansion failed
    // (and at this point it was expected to succeed),
    // ignore the failure (leaving "changed" as false).
  }
  if (desired_new_size < new_size_before && eden()->is_empty()) {
    // bail out of shrinking if objects in eden
    size_t change = new_size_before - desired_new_size;
    assert(change % alignment == 0, "just checking");
    _virtual_space.shrink_by(change);
    changed = true;
  }
  if (changed) {
    compute_space_boundaries(eden()->used());
    MemRegion cmr((HeapWord*)_virtual_space.low(), (HeapWord*)_virtual_space.high());
    Universe::heap()->barrier_set()->resize_covered_region(cmr);
    if (Verbose && PrintGC) {
      size_t new_size_after  = _virtual_space.committed_size();
      size_t eden_size_after = eden()->capacity();
      size_t survivor_size_after = from()->capacity();
      gclog_or_tty->print("New generation size " SIZE_FORMAT "K->" SIZE_FORMAT "K [eden="
        SIZE_FORMAT "K,survivor=" SIZE_FORMAT "K]",
        new_size_before/K, new_size_after/K, eden_size_after/K, survivor_size_after/K);
      if (WizardMode) {
        gclog_or_tty->print("[allowed " SIZE_FORMAT "K extra for %d threads]",
          thread_increase_size/K, threads_count);
      }
      gclog_or_tty->cr();
    }
  }
}

void DefNewGeneration::object_iterate_since_last_GC(ObjectClosure* cl) {
  // $$$ This may be wrong in case of "scavenge failure"?
  eden()->object_iterate(cl);
}

void DefNewGeneration::younger_refs_iterate(OopsInGenClosure* cl) {
  assert(false, "NYI -- are you sure you want to call this?");
}


size_t DefNewGeneration::capacity() const {
  return eden()->capacity()
       + from()->capacity();  // to() is only used during scavenge
}


size_t DefNewGeneration::used() const {
  return eden()->used()
       + from()->used();      // to() is only used during scavenge
}


size_t DefNewGeneration::free() const {
  return eden()->free()
       + from()->free();      // to() is only used during scavenge
}

size_t DefNewGeneration::max_capacity() const {
  const size_t alignment = GenCollectedHeap::heap()->collector_policy()->min_alignment();
  const size_t reserved_bytes = reserved().byte_size();
  return reserved_bytes - compute_survivor_size(reserved_bytes, alignment);
}

size_t DefNewGeneration::unsafe_max_alloc_nogc() const {
  return eden()->free();
}

size_t DefNewGeneration::capacity_before_gc() const {
  return eden()->capacity();
}

size_t DefNewGeneration::contiguous_available() const {
  return eden()->free();
}


HeapWord** DefNewGeneration::top_addr() const { return eden()->top_addr(); }
HeapWord** DefNewGeneration::end_addr() const { return eden()->end_addr(); }

void DefNewGeneration::object_iterate(ObjectClosure* blk) {
  eden()->object_iterate(blk);
  from()->object_iterate(blk);
}


void DefNewGeneration::space_iterate(SpaceClosure* blk,
                                     bool usedOnly) {
  blk->do_space(eden());
  blk->do_space(from());
  blk->do_space(to());
}

// The last collection bailed out, we are running out of heap space,
// so we try to allocate the from-space, too.
HeapWord* DefNewGeneration::allocate_from_space(size_t size) {
  HeapWord* result = NULL;
  if (PrintGC && Verbose) {
    gclog_or_tty->print("DefNewGeneration::allocate_from_space(%u):"
                  "  will_fail: %s"
                  "  heap_lock: %s"
                  "  free: " SIZE_FORMAT,
                  size,
               GenCollectedHeap::heap()->incremental_collection_will_fail() ? "true" : "false",
               Heap_lock->is_locked() ? "locked" : "unlocked",
               from()->free());
    }
  if (should_allocate_from_space() || GC_locker::is_active_and_needs_gc()) {
    if (Heap_lock->owned_by_self() ||
        (SafepointSynchronize::is_at_safepoint() &&
         Thread::current()->is_VM_thread())) {
      // If the Heap_lock is not locked by this thread, this will be called
      // again later with the Heap_lock held.
      result = from()->allocate(size);
    } else if (PrintGC && Verbose) {
      gclog_or_tty->print_cr("  Heap_lock is not owned by self");
    }
  } else if (PrintGC && Verbose) {
    gclog_or_tty->print_cr("  should_allocate_from_space: NOT");
  }
  if (PrintGC && Verbose) {
    gclog_or_tty->print_cr("  returns %s", result == NULL ? "NULL" : "object");
  }
  return result;
}

HeapWord* DefNewGeneration::expand_and_allocate(size_t size,
                                                bool   is_tlab,
                                                bool   parallel) {
  // We don't attempt to expand the young generation (but perhaps we should.)
  return allocate(size, is_tlab);
}


void DefNewGeneration::collect(bool   full,
                               bool   clear_all_soft_refs,
                               size_t size,
                               bool   is_tlab) {
  assert(full || size > 0, "otherwise we don't want to collect");
  GenCollectedHeap* gch = GenCollectedHeap::heap();
  _next_gen = gch->next_gen(this);
  assert(_next_gen != NULL,
    "This must be the youngest gen, and not the only gen");

  // If the next generation is too full to accomodate promotion
  // from this generation, pass on collection; let the next generation
  // do it.
  if (!collection_attempt_is_safe()) {
    gch->set_incremental_collection_will_fail();
    return;
  }
  assert(to()->is_empty(), "Else not collection_attempt_is_safe");

  init_assuming_no_promotion_failure();

  TraceTime t1("GC", PrintGC && !PrintGCDetails, true, gclog_or_tty);
  // Capture heap used before collection (for printing).
  size_t gch_prev_used = gch->used();

  SpecializationStats::clear();

  // These can be shared for all code paths
  IsAliveClosure is_alive(this);
  ScanWeakRefClosure scan_weak_ref(this);

  age_table()->clear();
  to()->clear();

  gch->rem_set()->prepare_for_younger_refs_iterate(false);

  assert(gch->no_allocs_since_save_marks(0),
         "save marks have not been newly set.");

  // Weak refs.
  // FIXME: Are these storage leaks, or are they resource objects?
#ifdef COMPILER2
  ReferencePolicy *soft_ref_policy = new LRUMaxHeapPolicy();
#else
  ReferencePolicy *soft_ref_policy = new LRUCurrentHeapPolicy();
#endif // COMPILER2

  // Not very pretty.
  CollectorPolicy* cp = gch->collector_policy();

  FastScanClosure fsc_with_no_gc_barrier(this, false);
  FastScanClosure fsc_with_gc_barrier(this, true);

  set_promo_failure_scan_stack_closure(&fsc_with_no_gc_barrier);
  FastEvacuateFollowersClosure evacuate_followers(gch, _level, this,
                                                  &fsc_with_no_gc_barrier,
                                                  &fsc_with_gc_barrier);

  assert(gch->no_allocs_since_save_marks(0),
         "save marks have not been newly set.");

  gch->gen_process_strong_roots(_level,
                                true, // Process younger gens, if any, as
                                      // strong roots.
                                false,// not collecting permanent generation.
                                SharedHeap::SO_AllClasses,
                                &fsc_with_gc_barrier,
                                &fsc_with_no_gc_barrier);

  // "evacuate followers".
  evacuate_followers.do_void();

  FastKeepAliveClosure keep_alive(this, &scan_weak_ref);
  ref_processor()->process_discovered_references(
    soft_ref_policy, &is_alive, &keep_alive, &evacuate_followers, NULL);
  if (!promotion_failed()) {
    // Swap the survivor spaces.
    eden()->clear();
    from()->clear();
    swap_spaces();

    assert(to()->is_empty(), "to space should be empty now");

    // Set the desired survivor size to half the real survivor space
    _tenuring_threshold =
      age_table()->compute_tenuring_threshold(to()->capacity()/HeapWordSize);

    if (PrintGC && !PrintGCDetails) {
      gch->print_heap_change(gch_prev_used);
    }
  } else {
    assert(HandlePromotionFailure,
      "Should not be here unless promotion failure handling is on");
    assert(_promo_failure_scan_stack != NULL &&
      _promo_failure_scan_stack->length() == 0, "post condition");

    // deallocate stack and it's elements
    delete _promo_failure_scan_stack;
    _promo_failure_scan_stack = NULL;

    remove_forwarding_pointers();
    if (PrintGCDetails) {
      gclog_or_tty->print(" (promotion failed)");
    }
    // Add to-space to the list of space to compact
    // when a promotion failure has occurred.  In that
    // case there can be live objects in to-space
    // as a result of a partial evacuation of eden
    // and from-space.
    swap_spaces();   // For the sake of uniformity wrt ParNewGeneration::collect().
    from()->set_next_compaction_space(to());
    gch->set_incremental_collection_will_fail();

    // Reset the PromotionFailureALot counters.
    NOT_PRODUCT(Universe::heap()->reset_promotion_should_fail();)
  }
  // set new iteration safe limit for the survivor spaces
  from()->set_concurrent_iteration_safe_limit(from()->top());
  to()->set_concurrent_iteration_safe_limit(to()->top());
  SpecializationStats::print();
  update_time_of_last_gc(os::javaTimeMillis());
}

class RemoveForwardPointerClosure: public ObjectClosure {
public:
  void do_object(oop obj) {
    obj->init_mark();
  }
};

void DefNewGeneration::init_assuming_no_promotion_failure() {
  _promotion_failed = false;
  from()->set_next_compaction_space(NULL);
}

void DefNewGeneration::remove_forwarding_pointers() {
  RemoveForwardPointerClosure rspc;
  eden()->object_iterate(&rspc);
  from()->object_iterate(&rspc);
  // Now restore saved marks, if any.
  if (_objs_with_preserved_marks != NULL) {
    assert(_preserved_marks_of_objs != NULL, "Both or none.");
    assert(_objs_with_preserved_marks->length() ==
           _preserved_marks_of_objs->length(), "Both or none.");
    for (int i = 0; i < _objs_with_preserved_marks->length(); i++) {
      oop obj   = _objs_with_preserved_marks->at(i);
      markOop m = _preserved_marks_of_objs->at(i);
      obj->set_mark(m);
    }
    delete _objs_with_preserved_marks;
    delete _preserved_marks_of_objs;
    _objs_with_preserved_marks = NULL;
    _preserved_marks_of_objs = NULL;
  }
}

void DefNewGeneration::preserve_mark_if_necessary(oop obj, markOop m) {
  if (m->must_be_preserved_for_promotion_failure(obj)) {
    if (_objs_with_preserved_marks == NULL) {
      assert(_preserved_marks_of_objs == NULL, "Both or none.");
      _objs_with_preserved_marks = new (ResourceObj::C_HEAP)
        GrowableArray<oop>(PreserveMarkStackSize, true);
      _preserved_marks_of_objs = new (ResourceObj::C_HEAP)
        GrowableArray<markOop>(PreserveMarkStackSize, true);
    }
    _objs_with_preserved_marks->push(obj);
    _preserved_marks_of_objs->push(m);
  }
}

void DefNewGeneration::handle_promotion_failure(oop old) {
  preserve_mark_if_necessary(old, old->mark());
  // forward to self
  old->forward_to(old);
  _promotion_failed = true;

  push_on_promo_failure_scan_stack(old);

  if (!_promo_failure_drain_in_progress) {
    // prevent recursion in copy_to_survivor_space()
    _promo_failure_drain_in_progress = true;
    drain_promo_failure_scan_stack();
    _promo_failure_drain_in_progress = false;
  }
}

oop DefNewGeneration::copy_to_survivor_space(oop old, oop* from) {
  assert(is_in_reserved(old) && !old->is_forwarded(),
         "shouldn't be scavenging this oop");
  size_t s = old->size();
  oop obj = NULL;

  // Try allocating obj in to-space (unless too old)
  if (old->age() < tenuring_threshold()) {
    obj = (oop) to()->allocate(s);
  }

  // Otherwise try allocating obj tenured
  if (obj == NULL) {
    obj = _next_gen->promote(old, s, from);
    if (obj == NULL) {
      if (!HandlePromotionFailure) {
        // A failed promotion likely means the MaxLiveObjectEvacuationRatio flag
        // is incorrectly set. In any case, its seriously wrong to be here!
        vm_exit_out_of_memory(s*wordSize, "promotion");
      }

      handle_promotion_failure(old);
      return old;
    }
  } else {
    // Prefetch beyond obj
    const intx interval = PrefetchCopyIntervalInBytes;
    Prefetch::write(obj, interval);

    // Copy obj
    Copy::aligned_disjoint_words((HeapWord*)old, (HeapWord*)obj, s);

    // Increment age if obj still in new generation
    obj->incr_age();
    age_table()->add(obj, s);
  }

  // Done, insert forward pointer to obj in this header
  old->forward_to(obj);

  return obj;
}

void DefNewGeneration::push_on_promo_failure_scan_stack(oop obj) {
  if (_promo_failure_scan_stack == NULL) {
    _promo_failure_scan_stack = new (ResourceObj::C_HEAP)
                                    GrowableArray<oop>(40, true);
  }

  _promo_failure_scan_stack->push(obj);
}

void DefNewGeneration::drain_promo_failure_scan_stack() {
  assert(_promo_failure_scan_stack != NULL, "precondition");

  while (_promo_failure_scan_stack->length() > 0) {
     oop obj = _promo_failure_scan_stack->pop();
     obj->oop_iterate(_promo_failure_scan_stack_closure);
  }
}

void DefNewGeneration::save_marks() {
  eden()->set_saved_mark();
  to()->set_saved_mark();
  from()->set_saved_mark();
}


void DefNewGeneration::reset_saved_marks() {
  eden()->reset_saved_mark();
  to()->reset_saved_mark();
  from()->reset_saved_mark();
}


bool DefNewGeneration::no_allocs_since_save_marks() {
  assert(eden()->saved_mark_at_top(), "Violated spec - alloc in eden");
  assert(from()->saved_mark_at_top(), "Violated spec - alloc in from");
  return to()->saved_mark_at_top();
}

#define DefNew_SINCE_SAVE_MARKS_DEFN(OopClosureType, nv_suffix) \
                                                                \
void DefNewGeneration::                                         \
oop_since_save_marks_iterate##nv_suffix(OopClosureType* cl) {   \
  cl->set_generation(this);                                     \
  eden()->oop_since_save_marks_iterate##nv_suffix(cl);          \
  to()->oop_since_save_marks_iterate##nv_suffix(cl);            \
  from()->oop_since_save_marks_iterate##nv_suffix(cl);          \
  cl->reset_generation();                                       \
  save_marks();                                                 \
}

ALL_SINCE_SAVE_MARKS_CLOSURES(DefNew_SINCE_SAVE_MARKS_DEFN)

#undef DefNew_SINCE_SAVE_MARKS_DEFN

void DefNewGeneration::contribute_scratch(ScratchBlock*& list, Generation* requestor,
                                         size_t max_alloc_words) {
  if (requestor == this || _promotion_failed) return;
  assert(requestor->level() > level(), "DefNewGeneration must be youngest");

  /* $$$ Assert this?  "trace" is a "MarkSweep" function so that's not appropriate.
  if (to_space->top() > to_space->bottom()) {
    trace("to_space not empty when contribute_scratch called");
  }
  */

  ContiguousSpace* to_space = to();
  assert(to_space->end() >= to_space->top(), "pointers out of order");
  size_t free_words = pointer_delta(to_space->end(), to_space->top());
  if (free_words >= MinFreeScratchWords) {
    ScratchBlock* sb = (ScratchBlock*)to_space->top();
    sb->num_words = free_words;
    sb->next = list;
    list = sb;
  }
}

bool DefNewGeneration::collection_attempt_is_safe() {
  if (!to()->is_empty()) {
    return false;
  }
  if (_next_gen == NULL) {
    GenCollectedHeap* gch = GenCollectedHeap::heap();
    _next_gen = gch->next_gen(this);
    assert(_next_gen != NULL,
           "This must be the youngest gen, and not the only gen");
  }

  // Decide if there's enough room for a full promotion
  // When using extremely large edens, we effectively lose a
  // large amount of old space.  Use the "MaxLiveObjectEvacuationRatio"
  // flag to reduce the minimum evacuation space requirements. If
  // there is not enough space to evacuate eden during a scavenge,
  // the VM will immediately exit with an out of memory error.
  // This flag has not been tested
  // with collectors other than simple mark & sweep.
  //
  // Note that with the addition of promotion failure handling, the
  // VM will not immediately exit but will undo the young generation
  // collection.  The parameter is left here for compatibility.
  const double evacuation_ratio = MaxLiveObjectEvacuationRatio / 100.0;

  // worst_case_evacuation is based on "used()".  For the case where this
  // method is called after a collection, this is still appropriate because
  // the case that needs to be detected is one in which a full collection
  // has been done and has overflowed into the young generation.  In that
  // case a minor collection will fail (the overflow of the full collection
  // means there is no space in the old generation for any promotion).
  size_t worst_case_evacuation = (size_t)(used() * evacuation_ratio);

  return _next_gen->promotion_attempt_is_safe(worst_case_evacuation,
                                              HandlePromotionFailure);
}

void DefNewGeneration::gc_epilogue(bool full) {
  // Check if the heap is approaching full after a collection has
  // been done.  Generally the young generation is empty at
  // a minimum at the end of a collection.  If it is not, then
  // the heap is approaching full.
  GenCollectedHeap* gch = GenCollectedHeap::heap();
  clear_should_allocate_from_space();
  if (collection_attempt_is_safe()) {
    gch->clear_incremental_collection_will_fail();
  } else {
    gch->set_incremental_collection_will_fail();
    if (full) { // we seem to be running out of space
      set_should_allocate_from_space();
    }
  }

  // update the generation and space performance counters
  update_counters();
  gch->collector_policy()->counters()->update_counters();
}

void DefNewGeneration::update_counters() {
  if (UsePerfData) {
    _eden_counters->update_all();
    _from_counters->update_all();
    _to_counters->update_all();
    _gen_counters->update_all();
  }
}

void DefNewGeneration::verify(bool allow_dirty) {
  eden()->verify(allow_dirty);
  from()->verify(allow_dirty);
    to()->verify(allow_dirty);
}

void DefNewGeneration::print_on(outputStream* st) const {
  Generation::print_on(st);
  st->print("  eden");
  eden()->print_on(st);
  st->print("  from");
  from()->print_on(st);
  st->print("  to  ");
  to()->print_on(st);
}


const char* DefNewGeneration::name() const {
  return "def new generation";
}
