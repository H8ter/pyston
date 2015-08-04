//
// Created by user on 7/25/15.
//

#include "semispace_heap.h"
#include "runtime/types.h"
#include "runtime/objmodel.h"

namespace pyston {
    namespace gc {
        SemiSpaceHeap::SemiSpaceHeap() {
            tospace = new LinearHeap(LARGE_ARENA_START, false);
            fromspace = new LinearHeap(SMALL_ARENA_START, false);

            sp = true;
        }

        SemiSpaceHeap::~SemiSpaceHeap() {
            delete tospace;
            delete fromspace;
        }

        GCAllocation *SemiSpaceHeap::alloc(size_t bytes) {
            GC_TRACE_LOG("alloc begin %d\n", (int)bytes);
            registerGCManagedBytes(bytes);

            LOCK_REGION(lock);

            GCAllocation* p;

            p = tospace->alloc(bytes);

            GC_TRACE_LOG("%p | %p | %p | %p | %d\n", (char *) tospace->arena->arena_start,
                         (char *) tospace->arena->frontier, (char *) tospace->arena->cur, p,
                         (sp ? 1 : 0));
                    ASSERT(p < tospace->arena->frontier, "Panic! May not alloc beyound the heap!");

            GC_TRACE_LOG("alloc end\n");

            return p;
        }

        GCAllocation *SemiSpaceHeap::realloc(GCAllocation *alloc, size_t bytes) {
            GC_TRACE_LOG("realloc %p %d\n", alloc, (int)bytes);

            return tospace->realloc(alloc, bytes);
        }

        void SemiSpaceHeap::destructContents(GCAllocation *alloc) {
            GC_TRACE_LOG("destructContents %p\n", alloc);
            if (tospace->arena->contains(alloc->user_data)) {
                return tospace->destructContents(alloc);
            }
            else if (fromspace->arena->contains(alloc->user_data)) {
                return fromspace->destructContents(alloc);
            }
        }

        void SemiSpaceHeap::free(GCAllocation *alloc) {
            GC_TRACE_LOG("free %p\n", alloc);
            if (tospace->arena->contains(alloc->user_data)) {
                return tospace->free(alloc);
            }
            else if (fromspace->arena->contains(alloc->user_data)) {
                return fromspace->free(alloc);
            }
            else {

            }
        }

        GCAllocation *SemiSpaceHeap::getAllocationFromInteriorPointer(void *ptr) {
            if (tospace->arena->contains(ptr)) {
                return tospace->getAllocationFromInteriorPointer(ptr);
            }
            else if(fromspace->arena->contains(ptr)) {
                return fromspace->getAllocationFromInteriorPointer(ptr);
            }
            else {
                return NULL;
            }
        }

        /*
            WARNING: it's safe to use only after prepareForCollection
        */
        void SemiSpaceHeap::freeUnmarked(std::vector<Box *> &weakly_referenced) {
            fromspace->freeUnmarked(weakly_referenced);
        }

        void SemiSpaceHeap::prepareForCollection() {
            GC_TRACE_LOG("GC begin\n");

            std::swap(tospace, fromspace);
            disableGC();

            sp = !sp;
        }

        void SemiSpaceHeap::cleanupAfterCollection() {
            GC_TRACE_LOG("GC end\n");

            tospace->clearMark();

            fromspace->clearMark();
            fromspace->clear();

            bytesAllocatedSinceCollection = 0;
            enableGC();
        }

        void SemiSpaceHeap::dumpHeapStatistics(int level) {

        }
    }
}