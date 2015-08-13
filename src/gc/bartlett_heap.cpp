//
// Created by user on 8/10/15.
//

#include "bartlett_heap.h"
#include "linear_heap.h"
#include "collector.h"

namespace pyston {
namespace gc {

    BartlettHeap::BartlettHeap(uintptr_t arena_start, uintptr_t arena_size) :
        block_size(arena_size / max_blocks_cnt), arena_start(arena_start), arena_size(arena_size),
        frontier(arena_start)
    {
        initial_map_size = INITIAL_MAP_SIZE / max_blocks_cnt;
        increment = INCREMENT / max_blocks_cnt;

        initial_map_size += PAGE_SIZE - (initial_map_size % PAGE_SIZE);
        increment += PAGE_SIZE - (increment % PAGE_SIZE);

        size_t initial_blocks_cnt = 1;
        allocated_blocks = 0;
        for(int i = 0; i < initial_blocks_cnt; i++) {
            alloc_block();
        }

        cur_space = nxt_space = 1;
    }

    BartlettHeap::~BartlettHeap() {
        for(int i = 0; i < blocks.size(); i++)
            delete blocks[i].h;
    }

    GCAllocation* BartlettHeap::alloc(size_t bytes) {
        return _alloc(bytes, HEAP_SPACE::CUR_SPACE);
    }

    /*
        memory allocation in specific space
    */
    GCAllocation *BartlettHeap::_alloc(size_t bytes, HEAP_SPACE sp) {
        TRACE("alloc begin\n");
        registerGCManagedBytes(bytes);
        LOCK_REGION(lock);

        GCAllocation* mem = NULL;
        int space = sp == HEAP_SPACE::CUR_SPACE ? cur_space : nxt_space;

        // slow implementation
        for(int i = 0; i < blocks.size(); i++) {
            Block& b = blocks[i];
            if (b.id == space)
                if (b.h->fit(bytes)) {
                    heap_id = i;
                    TRACE("%p | %d %d | %d | %d\n", (void*)b.h->arena->arena_start, b.id, space, (int)b.h->fit(bytes), i);
                    mem = b.h->alloc(bytes);
                    break;
                }
        }

        if (!mem) {
            for (int i = 0; i < blocks.size(); i++) {
                Block &b = blocks[i];

                if (b.h->fit(bytes) && is_free(b.id)) {
                    b.id = nxt_space;
                    heap_id = i;
                    TRACE("new %p | %d %d | %d | %d\n", (void *) b.h->arena->arena_start, b.id, nxt_space,
                          (int) b.h->fit(bytes), i);
                    mem = b.h->alloc(bytes);
                    break;
                }
            }
        }

        if (mem) {
            TRACE("alloc end\n");
            return mem;
        }
        else {
            Block &b = alloc_block();
            if (b.h->fit(bytes)) {
                b.id = nxt_space;
                heap_id = (int)blocks.size()-1;
                TRACE("new %p | %d %d | %d\n", (void *) b.h->arena->arena_start, b.id, nxt_space,
                      (int) b.h->fit(bytes));
                mem = b.h->alloc(bytes);
                return mem;
            }

            
            ASSERT(false, "allocation failed\n");
            return NULL;
        }
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
        if (valid_pointer(ptr) /*&& (b.id == cur_space || b.id == nxt_space)*/)
            return block(ptr).h->getAllocationFromInteriorPointer(ptr);
        else
            return NULL;
    }

    void BartlettHeap::freeUnmarked(std::vector<Box *> &weakly_referenced) {
        for(int i = 0; i < blocks.size(); i++) {
            Block &b = blocks[i];
            b.h->freeUnmarked(weakly_referenced);
        }
    }

    void BartlettHeap::prepareForCollection() {
        disableGC();
        nxt_space = cur_space + 1;                      // necessary for Bartlett's GC
//        for testing only (not with make check)
//        fprintf(stderr, "%d\n", allocated_blocks);
#if TRACE_GC_MARKING
        fprintf(stderr, "%d\n", allocated_blocks);
#endif
    }

    void BartlettHeap::cleanupAfterCollection() {
        for(int i = 0; i < blocks.size(); i++) {
            Block &b = blocks[i];
            b.h->cleanupAfterCollection();

            // DANGER
            if (b.id != nxt_space) {
                b.h->clear();
            }
        }

        cur_space = nxt_space;                          // necessary for Bartlett's GC

        bytesAllocatedSinceCollection = 0;
        enableGC();
    }

    void BartlettHeap::dumpHeapStatistics(int level) {

    }

    int BartlettHeap::block_index(void *p) {
        return (int)(((uintptr_t)p - arena_start) / block_size);
    }

    BartlettHeap::Block& BartlettHeap::block(void* p) {
        return blocks[block_index(p)];
    }

    inline bool BartlettHeap::is_free(int block_id) {
        return block_id != cur_space && block_id != nxt_space;
    }

    inline bool BartlettHeap::valid_pointer(void *p) {
        return (uintptr_t)p >= arena_start && (uintptr_t)p < frontier;
    }

    BartlettHeap::Block& BartlettHeap::alloc_block() {
        uintptr_t b_count = blocks.size();
        RELEASE_ASSERT(b_count + 1 <= max_blocks_cnt, "heap is full\n");

        uintptr_t b_start = b_count ? blocks[b_count -1].h->arena->arena_start + block_size : arena_start;
        blocks.push_back(Block(b_start, block_size, initial_map_size, increment));
        allocated_blocks++;
        frontier += block_size;

#if TRACE_GC_MARKING
      fprintf(stderr, "block alloc %p %d\n", (void*)b_start, allocated_blocks);
#endif

        return blocks[b_count];
    }
    
    BartlettHeap::Block::Block(uintptr_t arena_start, uintptr_t block_size, uintptr_t initial_map_size,
                               uintptr_t increment)
    {
        h  = new LinearHeap(arena_start, block_size, initial_map_size, increment, false);
        id = 0;
    }
}
}
