//
// Created by user on 7/25/15.
//

#include "marksweep.h"

#include "runtime/objmodel.h"
#include "runtime/types.h"
#include "gc/heap.h"
#include "gc/default_heap.h"

#include "gc/linear_heap.h"
#include "gc/bartlett_heap.h"
#include "gc/trace_stack.h"

namespace pyston{
    namespace gc {

        MarkSweepGC::MarkSweepGC() {
//            global_heap = new DefaultHeap();
            global_heap = new LinearHeap(LARGE_ARENA_START, ARENA_SIZE, INITIAL_MAP_SIZE, INCREMENT);
//            global_heap = new BartlettHeap(SMALL_ARENA_START, ARENA_SIZE);
            gc_enabled = true;
            should_not_reenter_gc = false;
            ncollections = 0;
        }


        void MarkSweepGC::runCollection() {
            GC_TRACE_LOG("GC begin\n");
            //FILE *f = fopen("out.txt", "a");
            //fprintf(f, "hello from runCollection (Mark-and-Sweep GC class)\n");
            //fclose(f);

            static StatCounter sc_us("us_gc_collections");
            static StatCounter sc("gc_collections");
            sc.log();

            UNAVOIDABLE_STAT_TIMER(t0, "us_timer_gc_collection");

            ncollections++;

            if (VERBOSITY("gc") >= 2)
                printf("Collection #%d\n", ncollections);

            // The bulk of the GC work is not reentrant-safe.
            // In theory we should never try to reenter that section, but it's happened due to bugs,
            // which show up as very-hard-to-understand gc issues.
            // So keep track if we're in the non-reentrant section and abort if we try to go back in.
            // We could also just skip the collection if we're currently in the gc, but I think if we
            // run into this case it's way more likely that it's a bug than something we should ignore.
            RELEASE_ASSERT(!should_not_reenter_gc, "");
            should_not_reenter_gc = true; // begin non-reentrant section

            Timer _t("collecting", /*min_usec=*/10000);

            global_heap->prepareForCollection();

            // Finalizers might have been called since the last GC.
            // Normally we invalidate the list everytime we call a batch of objects with finalizers.
            // However, there are some edge cases where that isn't sufficient, such as a GC being triggered
            // inside a finalizer call. To be safe, it's better to invalidate the list again.
            invalidateOrderedFinalizerList();

            markPhase();

            // The sweep phase will not free weakly-referenced objects, so that we can inspect their
            // weakrefs_list.  We want to defer looking at those lists until the end of the sweep phase,
            // since the deallocation of other objects (namely, the weakref objects themselves) can affect
            // those lists, and we want to see the final versions.
            std::vector<Box*> weakly_referenced;
            sweepPhase(weakly_referenced);

            // Handle weakrefs in two passes:
            // - first, find all of the weakref objects whose callbacks we need to call.  we need to iterate
            //   over the garbage-and-corrupt-but-still-alive weakly_referenced list in order to find these objects,
            //   so the gc is not reentrant during this section.  after this we discard that list.
            // - the callbacks are called later, along with the finalizers
            for (auto o : weakly_referenced) {
                assert(isValidGCObject(o));
                prepareWeakrefCallbacks(o);
                global_heap->free(GCAllocation::fromUserData(o));
            }

            should_not_reenter_gc = false; // end non-reentrant section

            global_heap->cleanupAfterCollection();

            if (VERBOSITY("gc") >= 2)
                printf("Collection #%d done\n\n", ncollections);

            long us = _t.end();
            sc_us.log(us);

            // dumpHeapStatistics();
            GC_TRACE_LOG("GC end\n");
        }

        void MarkSweepGC::markPhase() {
            static StatCounter sc_us("us_gc_mark_phase");
            Timer _t("markPhase", /*min_usec=*/10000);

#ifndef NVALGRIND
    // Have valgrind close its eyes while we do the conservative stack and data scanning,
    // since we'll be looking at potentially-uninitialized values:
    VALGRIND_DISABLE_ERROR_REPORTING;
#endif

            GC_TRACE_LOG("Starting collection %d\n", ncollections);

            GC_TRACE_LOG("Looking at roots\n");
            TraceStack stack(TraceStackType::MarkPhase, roots);
            GCVisitor visitor(&stack, global_heap);

            markRoots(visitor);

            graphTraversalMarking(stack, visitor);
#if 0
            // Some classes might be unreachable. Unfortunately, we have to keep them around for
            // one more collection, because during the sweep phase, instances of unreachable
            // classes might still end up looking at the class. So we visit those unreachable
            // classes remove them from the list of class objects so that it can be freed
            // in the next collection.
            std::vector<BoxedClass*> classes_to_remove;
            for (BoxedClass* cls : class_objects) {
                GCAllocation* al = GCAllocation::fromUserData(cls);
                if (!isMarked(al)) {
                    visitor.visit(cls);
                    classes_to_remove.push_back(cls);
                }
            }

            // We added new objects to the stack again from visiting classes so we nee to do
            // another (mini) traversal.
            graphTraversalMarking(stack, visitor);

            for (BoxedClass* cls : classes_to_remove) {
                class_objects.erase(cls);
            }

            // The above algorithm could fail if we have a class and a metaclass -- they might
            // both have been added to the classes to remove. In case that happens, make sure
            // that the metaclass is retained for at least another collection.
            for (BoxedClass* cls : classes_to_remove) {
                class_objects.insert(cls->cls);
            }
#endif
            // Objects with finalizers cannot be freed in any order. During the call to a finalizer
            // of an object, the finalizer expects the object's references to still point to valid
            // memory. So we root objects whose finalizers need to be called by placing them in a
            // pending finalization list.
            orderFinalizers();

#ifndef NVALGRIND
    VALGRIND_ENABLE_ERROR_REPORTING;
#endif

            long us = _t.end();
            sc_us.log(us);
        }

        void MarkSweepGC::sweepPhase(std::vector<Box *> &weakly_referenced) {
            static StatCounter sc_us("us_gc_sweep_phase");
            Timer _t("sweepPhase", /*min_usec=*/10000);

            // we need to use the allocator here because these objects are referenced only here, and calling the weakref
            // callbacks could start another gc
            global_heap->freeUnmarked(weakly_referenced);

            long us = _t.end();
            sc_us.log(us);
        }

    }
}

