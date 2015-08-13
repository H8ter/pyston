//
// Created by user on 8/10/15.
//

#ifndef PYSTON_BARTLETT_HEAP_H
#define PYSTON_BARTLETT_HEAP_H

#include "gc/heap.h"
#include "linear_heap.h"

namespace pyston {
namespace gc {

    class BartlettHeap : public Heap {
    public:

        friend class BartlettGC;

        BartlettHeap(uintptr_t arena_start = SMALL_ARENA_START, uintptr_t arena_size = ARENA_SIZE);

        virtual ~BartlettHeap();

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

        enum class HEAP_SPACE {CUR_SPACE, NXT_SPACE};
        GCAllocation* _alloc(size_t bytes, HEAP_SPACE sp);

        struct Block {
            LinearHeap *h;
            int id;

            Block() {}
            Block(uintptr_t arena_start, uintptr_t block_size,
                  uintptr_t initial_map_size, uintptr_t increment);
        };

        Block& block(void* p);

        int block_index(void* p);

        bool is_free(int block_id);

        bool valid_pointer(void* p);

        Block& alloc_block();

        const int max_blocks_cnt = 1024;      // should be power of 2
        const uintptr_t block_size;
        uintptr_t initial_map_size;
        uintptr_t increment;

        std::vector<Block> blocks;
        uintptr_t arena_start;
        uintptr_t arena_size;
        uintptr_t frontier;

        int cur_space;
        int nxt_space;

        int allocated_blocks;
        int heap_id;
    };
}
}



#endif //PYSTON_BARTLETT_HEAP_H
