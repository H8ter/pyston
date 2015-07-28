//
// Created by user on 7/25/15.
//

#include "semispace.h"
#include "gc/collector.h"
#include "gc/semispace_heap.h"

namespace pyston {
namespace gc{

// SemiSpace GC algorithm's pseudocode
//    flip():
//          swap(fromspace, tospace)
//          top_of_to_space = tospace + size(tospace)
//          scan = free = tospace
//
//          for R in Roots
//              R = copy(R)
//
//          while scan < free
//              for P in Children(scan)
//              *P = copy(*P)
//              scan = scan + size(scan)
//
//    copy(P):
//          if forwarded(P)
//              return forwarding_address(P)
//          else
//              addr = free
//              move(P, free)
//              free = free + size(P)
//              forwarding_address(P) = addr
//              return addr

        SemiSpaceGC::SemiSpaceGC() {
            global_heap = new SemiSpaceHeap();
            gc_enabled = true;
            should_not_reenter_gc = false;
            ncollections = 0;
        }

        void SemiSpaceGC::runCollection() {
            RELEASE_ASSERT(!should_not_reenter_gc, "");
            should_not_reenter_gc = true; // begin non-reentrant section

            global_heap->prepareForCollection();

//            invalidateOrderedFinalizerList();
//
//            // action goes here
//            std::vector<Box*> weakly_referenced;
            flip();

//            for (auto o : weakly_referenced) {
//                assert(isValidGCObject(o));
//                prepareWeakrefCallbacks(o);
//                global_heap->free(GCAllocation::fromUserData(o));
//            }

            should_not_reenter_gc = false; // end non-reentrant section

            global_heap->cleanupAfterCollection();
        }

        void SemiSpaceGC::flip() {
            auto heap = (SemiSpaceHeap*)global_heap;

            std::swap(heap->tospace, heap->fromspace);



        }

} // namespace gc
} // namespace pyston