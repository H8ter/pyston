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
            tospace = new SemiSpaceHeap(LARGE_ARENA_START);
            fromspace = new SemiSpaceHeap(SMALL_ARENA_START);

            gc_enabled = true;
            should_not_reenter_gc = false;
            ncollections = 0;
        }

        void SemiSpaceGC::runCollection() {
            RELEASE_ASSERT(!should_not_reenter_gc, "");
            should_not_reenter_gc = true; // begin non-reentrant section

            tospace->prepareForCollection();

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

            // forgetting about garbage
            fromspace->obj.clear();
            fromspace->obj_set.clear();
        }

        void SemiSpaceGC::flip() {
            std::swap(tospace, fromspace);

            TraceStack stack(TraceStackType::MarkPhase, roots);
            GCVisitor visitor(&stack, fromspace);

            markRoots(visitor);

            // 1st step: copying roots from f.space to t.space
            while(void* p = stack.pop()) {
                GCAllocation* al = GCAllocation::fromUserData(p);

                assert(isMarked(al));

                SemiSpaceHeap::Obj *obj = SemiSpaceHeap::Obj::fromAllocation(al);
                obj = copy(obj);
            }

            // 2nd step: scanning objects
            auto scan = tospace->obj_set.begin();
            while (scan != tospace->obj_set.end()) {
                auto obj = reinterpret_cast<SemiSpaceHeap::Obj*>(*scan);
                // for P in children(*scan)
                //     P = copy(P);
                ++scan;
            }
        }

        SemiSpaceHeap::Obj *SemiSpaceGC::copy(SemiSpaceHeap::Obj *obj) {
            if (obj->forward)
                return obj->forward;
            else {
                auto addr = SemiSpaceHeap::Obj::fromAllocation(tospace->alloc(obj->size));
                // move
                obj->forward = addr;
                return addr;
            }
        }
    } // namespace gc
} // namespace pyston