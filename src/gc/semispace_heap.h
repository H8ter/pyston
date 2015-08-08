//
// Created by user on 7/25/15.
//

#ifndef PYSTON_SEMISPACE_HEAP_H
#define PYSTON_SEMISPACE_HEAP_H



#include "gc/linear_heap.h"
#include <functional>

namespace pyston {
namespace gc {

    class SemiSpaceGC;

    class SemiSpaceHeap : public Heap {
    public:
        friend class SemiSpaceGC;
        friend class HybridSemiSpaceGC;

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
        LinearHeap * rootspace;

        std::vector<LinearHeap*> spaces;

        typedef void (LinearHeap::* heapAction)(GCAllocation*);
        std::function<void(GCAllocation*, heapAction)> foo;

        volatile char sp;
        volatile char alloc_root;
    };

}
}


#endif //PYSTON_SEMISPACE_HEAP_H
