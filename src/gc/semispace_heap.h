//
// Created by user on 7/25/15.
//

#ifndef PYSTON_SEMISPACE_HEAP_H
#define PYSTON_SEMISPACE_HEAP_H

#include "linear_heap.h"

namespace pyston {
    namespace gc {

        class SemiSpaceHeap : public Heap {
        public:
            friend class SemiSpaceGC;

            SemiSpaceHeap();

            virtual ~SemiSpaceHeap();

            virtual GCAllocation *alloc(size_t bytes) override;

            virtual GCAllocation *realloc(GCAllocation *alloc, size_t bytes) override;

            virtual void destructContents(GCAllocation *alloc) override;

            virtual void free(GCAllocation *alloc) override;

            virtual GCAllocation *getAllocationFromInteriorPointer(void *ptr) override;

            virtual void freeUnmarked(std::vector<Box *> &weakly_referenced) override;

            virtual void prepareForCollection() override;

            virtual void cleanupAfterCollection() override;

            virtual void dumpHeapStatistics(int level) override;

        private:

            LinearHeap * tospace;
            LinearHeap * fromspace;

            volatile char sp;
        };

    }
}


#endif //PYSTON_SEMISPACE_HEAP_H
