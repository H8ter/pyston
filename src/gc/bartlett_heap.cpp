//
// Created by user on 8/10/15.
//

#include "bartlett_heap.h"
#include "linear_heap.h"
#include "collector.h"

namespace pyston {
namespace gc {

    BartlettHeap::BartlettHeap(uintptr_t arena_start, uintptr_t arena_size) :
        block_size(arena_size / blocks_cnt)
    {
        uintptr_t initial_map_size = INITIAL_MAP_SIZE / blocks_cnt;
        uintptr_t increment = INCREMENT / blocks_cnt;

        initial_map_size += PAGE_SIZE - (initial_map_size % PAGE_SIZE);
        increment += PAGE_SIZE - (increment % PAGE_SIZE);

        blocks.resize(blocks_cnt);
        block_start.resize(blocks_cnt);
        for(int i = 0; i < blocks_cnt; i++, arena_start += block_size) {
            blocks[i].h     = new LinearHeap(arena_start, block_size, initial_map_size, increment, false);
            blocks[i].id    = 0;
            block_start[i]  = arena_start;
        }

        cur_space = nxt_space = 1;

        allocated_blocks = 0;
    }

    BartlettHeap::~BartlettHeap() {
        for(int i = 0; i < blocks_cnt; i++)
            delete blocks[i].h;
    }

    GCAllocation* BartlettHeap::alloc(size_t bytes) {
        return _alloc(bytes, cur_space);
    }

    /*
        memory allocation in specific space
    */
    GCAllocation *BartlettHeap::_alloc(size_t bytes, int space) {
        registerGCManagedBytes(bytes);

        // slow implementation
        for(int i = 0; i < blocks_cnt; i++) {
            Block& b = blocks[i];
            if (b.h->fit(bytes) && b.id == space) {
                heap_id = i;
                return b.h->alloc(bytes);
            }
        }

        for(int i = 0; i < blocks_cnt; i++) {
            Block &b = blocks[i];

            if (b.h->fit(bytes) && is_free(b.id)) {
                b.id = nxt_space;
                allocated_blocks++;
                heap_id = i;
                return b.h->alloc(bytes);
            }
        }


        ASSERT(false, "allocation failed\n");

        return nullptr;
    }

    GCAllocation *BartlettHeap::realloc(GCAllocation *alloc, size_t bytes) {
        // could cause error (location problem)

        if(!valid_pointer(alloc)) return alloc;

        Block &b = block(alloc);
        if(!b.h->fit(bytes)) {
            GCAllocation* dest = BartlettHeap::alloc(bytes);
            LinearHeap::realloc(
                    b.h, blocks[heap_id].h, alloc, dest,
                    std::min(bytes, LinearHeap::Obj::fromAllocation(alloc)->size)
            );
            return dest;
        }
        else return b.h->realloc(alloc, bytes);

    }

    void BartlettHeap::destructContents(GCAllocation *alloc) {
        _doFree(alloc, NULL);
    }

    void BartlettHeap::free(GCAllocation *alloc) {
        block(alloc).h->free(alloc);
    }

    GCAllocation *BartlettHeap::getAllocationFromInteriorPointer(void *ptr) {
        if (valid_pointer(ptr))
            return block(ptr).h->getAllocationFromInteriorPointer(ptr);
        else
            return NULL;
    }

    void BartlettHeap::freeUnmarked(std::vector<Box *> &weakly_referenced) {
        for(int i = 0; i < blocks_cnt; i++) {
            Block &b = blocks[i];
            b.h->freeUnmarked(weakly_referenced);
        }
    }

    void BartlettHeap::prepareForCollection() {
        disableGC();
        nxt_space = cur_space + 1;                      // necessary for Bartlett's GC
//        for testing only (not with make check)
//        fprintf(stderr, "%d\n", allocated_blocks);
    }

    void BartlettHeap::cleanupAfterCollection() {
        for(int i = 0; i < blocks_cnt; i++) {
            Block &b = blocks[i];
            b.h->cleanupAfterCollection();
        }

        cur_space = nxt_space;                          // necessary for Bartlett's GC

        bytesAllocatedSinceCollection = 0;
        enableGC();
    }

    void BartlettHeap::dumpHeapStatistics(int level) {

    }

    int BartlettHeap::block_index(void *p) {
        auto it = std::lower_bound(block_start.begin(), block_start.end(), (uintptr_t)p);
        if (it == block_start.end() || *it > (uintptr_t)p) --it;
        return it - block_start.begin();
    }

    BartlettHeap::Block& BartlettHeap::block(void* p) {
        return blocks[block_index(p)];
    }

    inline bool BartlettHeap::is_free(int block_id) {
        return block_id != cur_space && block_id != nxt_space;
    }

    inline bool BartlettHeap::valid_pointer(void *p) {
        return (uintptr_t)p >= block_start[0] && (uintptr_t)p < block_start[blocks_cnt-1] + block_size;
    }
}
}
