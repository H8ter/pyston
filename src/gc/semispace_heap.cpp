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
            rootspace = new LinearHeap(HUGE_ARENA_START, false); // or true???

            spaces.push_back(tospace);
            spaces.push_back(fromspace);
            spaces.push_back(rootspace);

            sp = true;

            foo = [this] (GCAllocation* al, heapAction action) {
                for(auto s : this->spaces)
                    if (s->arena->contains(al->user_data))
                        (s->*action)(al);
            };

            alloc_root = false;
        }

        SemiSpaceHeap::~SemiSpaceHeap() {
            for(int i = 0; i < spaces.size(); i++)
                delete spaces[i];
        }

        GCAllocation *SemiSpaceHeap::alloc(size_t bytes) {
            GC_TRACE_LOG("%s alloc begin %d\n", alloc_root ? "root" : "non-r",(int)bytes);
            registerGCManagedBytes(bytes);

            LOCK_REGION(lock);

            GCAllocation* p;

            LinearHeap* space = alloc_root ? rootspace : tospace;

            p = space->alloc(bytes);

            GC_TRACE_LOG("%p | %p | %p | %p | %d\n", (char *) space->arena->arena_start,
                         (char *) space->arena->frontier, (char *) space->arena->cur, p,
                         (sp ? 1 : 0));
            ASSERT(p < space->arena->frontier, "Panic! May not alloc beyound the heap!");

            GC_TRACE_LOG("alloc end\n");

            return p;
        }

        GCAllocation *SemiSpaceHeap::realloc(GCAllocation *alloc, size_t bytes) {
            GC_TRACE_LOG("realloc %p %d\n", alloc, (int)bytes);

            if (tospace->arena->contains(alloc->user_data))
                return tospace->realloc(alloc, bytes);
            else if(rootspace->arena->contains(alloc->user_data))
                return rootspace->realloc(alloc, bytes);
            else if(fromspace->arena->contains(alloc->user_data))
                return fromspace->realloc(alloc, bytes);
            else return NULL;
        }

        void SemiSpaceHeap::destructContents(GCAllocation *alloc) {
            GC_TRACE_LOG("destructContents %p\n", alloc);

            foo(alloc, &LinearHeap::destructContents);
        }

        void SemiSpaceHeap::free(GCAllocation *alloc) {
            GC_TRACE_LOG("free %p\n", alloc);

            foo(alloc, &LinearHeap::free);
        }

        GCAllocation *SemiSpaceHeap::getAllocationFromInteriorPointer(void *ptr) {
            for(auto s : spaces)
                if (s->arena->contains(ptr))
                    return s->getAllocationFromInteriorPointer(ptr);

            return NULL;
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
            std::swap(spaces[0], spaces[1]);
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