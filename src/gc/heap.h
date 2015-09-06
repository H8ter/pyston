// Copyright (c) 2014-2015 Dropbox, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PYSTON_GC_HEAP_H
#define PYSTON_GC_HEAP_H

#include <cstddef>

#include "core/common.h"
#include "core/threading.h"
#include "core/types.h"

#include <sys/mman.h>

#define DS_DEFINE_SPINLOCK(name) pyston::threading::NopLock name

namespace pyston {


namespace gc {

extern unsigned bytesAllocatedSinceCollection;
#define ALLOCBYTES_PER_COLLECTION 10000000
void _bytesAllocatedTripped();

// Notify the gc of n bytes as being under GC management.
// This is called internally for anything allocated through gc_alloc,
// but it can also be called by clients to say that they have memory that
// is ultimately GC managed but did not get allocated through gc_alloc,
// such as memory that will get freed by a gc destructor.
inline void registerGCManagedBytes(size_t bytes) {
//    bytesAllocatedSinceCollection += bytes;
//    if (unlikely(bytesAllocatedSinceCollection >= ALLOCBYTES_PER_COLLECTION)) {
//        _bytesAllocatedTripped();
//    }
}


typedef uint8_t kindid_t;
struct GCAllocation {
    unsigned int gc_flags : 8;
    GCKind kind_id : 8;
    unsigned int _reserved1 : 16;
    unsigned int kind_data : 32;

    char user_data[0];

    static GCAllocation* fromUserData(void* user_data) {
        char* d = reinterpret_cast<char*>(user_data);
        return reinterpret_cast<GCAllocation*>(d - offsetof(GCAllocation, user_data));
    }
};
static_assert(sizeof(GCAllocation) <= sizeof(void*),
              "we should try to make sure the gc header is word-sized or smaller");

/*
    Heap Interface
*/
class Heap {
public:

    DS_DEFINE_SPINLOCK(lock);

    virtual ~Heap() {}

    virtual GCAllocation* __attribute__((__malloc__)) alloc(size_t bytes) = 0;

    virtual GCAllocation* realloc(GCAllocation* alloc, size_t bytes) = 0;

    virtual void destructContents(GCAllocation* alloc) = 0;

    virtual void free(GCAllocation* alloc) = 0;

    virtual GCAllocation* getAllocationFromInteriorPointer(void* ptr) = 0;

    virtual void freeUnmarked(std::vector<Box*>& weakly_referenced) = 0;

    virtual void prepareForCollection() = 0;

    virtual void cleanupAfterCollection() = 0;

    virtual void dumpHeapStatistics(int level) = 0;
};


#define MARK_BIT 0x1
// reserved bit - along with MARK_BIT, encodes the states of finalization order
#define ORDERING_EXTRA_BIT 0x2
#define FINALIZER_HAS_RUN_BIT 0x4

#define ORDERING_BITS (MARK_BIT | ORDERING_EXTRA_BIT)

enum FinalizationState {
    UNREACHABLE = 0x0,
    TEMPORARY = ORDERING_EXTRA_BIT,

    // Note that these two states have MARK_BIT set.
    ALIVE = MARK_BIT,
    REACHABLE_FROM_FINALIZER = ORDERING_BITS,
};

inline bool isMarked(GCAllocation* header) {
    return (header->gc_flags & MARK_BIT) != 0;
}

inline void setMark(GCAllocation* header) {
    assert(!isMarked(header));
    header->gc_flags |= MARK_BIT;
}

inline void clearMark(GCAllocation* header) {
    assert(isMarked(header));
    header->gc_flags &= ~MARK_BIT;
}

inline bool hasFinalized(GCAllocation* header) {
    return (header->gc_flags & FINALIZER_HAS_RUN_BIT) != 0;
}

inline void setFinalized(GCAllocation* header) {
    assert(!hasFinalized(header));
    header->gc_flags |= FINALIZER_HAS_RUN_BIT;
}

inline FinalizationState orderingState(GCAllocation* header) {
    int state = header->gc_flags & ORDERING_BITS;
    assert(state <= static_cast<int>(FinalizationState::REACHABLE_FROM_FINALIZER));
    return static_cast<FinalizationState>(state);
}

inline void setOrderingState(GCAllocation* header, FinalizationState state) {
    header->gc_flags = (header->gc_flags & ~ORDERING_BITS) | static_cast<int>(state);
}

inline void clearOrderingState(GCAllocation* header) {
    header->gc_flags &= ~ORDERING_EXTRA_BIT;
}

#undef MARK_BIT
#undef ORDERING_EXTRA_BIT
#undef FINALIZER_HAS_RUN_BIT
#undef ORDERING_BITS

bool hasOrderedFinalizer(BoxedClass* cls);
void finalize(Box* b);
bool isWeaklyReferenced(Box* b);

#define PAGE_SIZE 4096

class Arena {
public:
    uintptr_t arena_start;
    uintptr_t arena_size;
    uintptr_t initial_mapsize;
    uintptr_t increment;

    void* cur;
    void* frontier;
    void* arena_end;

    Arena(uintptr_t arena_start, uintptr_t arena_size, uintptr_t initial_mapsize, uintptr_t increment) :
            arena_start(arena_start), arena_size(arena_size),
            initial_mapsize(initial_mapsize), increment(increment),
            cur((void*)arena_start), frontier((void*)arena_start), arena_end((void*)(arena_start + arena_size))
    {
        if (initial_mapsize)
            extendMapping(initial_mapsize);
    }

    // extends the mapping for this arena
    void extendMapping(size_t size) {
        assert(size % PAGE_SIZE == 0);

        RELEASE_ASSERT(((uint8_t*)frontier + size) < arena_end, "arena full %p %p %p", (void*)arena_start, arena_end, frontier);

        void* mrtn = mmap(frontier, size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        RELEASE_ASSERT((uintptr_t)mrtn != -1, "failed to allocate memory from OS");
        ASSERT(mrtn == frontier, "%p %p\n", mrtn, cur);

        frontier = (uint8_t*)frontier + size;
    }

    void* allocFromArena(size_t size) {
        if (((char*)cur + size) >= (char*)frontier) {
            // grow the arena by a multiple of increment such that we can service the allocation request
            size_t grow_size = (size + increment - 1) & ~(increment - 1);
            extendMapping(grow_size);
        }

        void* rtn = cur;
        cur = (uint8_t*)cur + size;
        if ((intptr_t)cur % 8 != 0)
            cur = (uint8_t*)cur + 8 - ((intptr_t)cur % 8);

        memset(rtn, 0, size);

        return rtn;
    }

    bool fit(size_t size) {
        if (((char*)cur + size) < (char*)frontier) return true;
        else {
            size_t grow_size = (size + increment - 1) & ~(increment - 1);
            return (((uint8_t*)frontier + grow_size) < arena_end);
        }

    }

    bool contains(void* addr) { return (void*)arena_start <= addr && addr < cur; }
};

constexpr uintptr_t ARENA_SIZE = 0x1000000000L;
constexpr uintptr_t SMALL_ARENA_START = 0x1270000000L;
constexpr uintptr_t LARGE_ARENA_START = 0x2270000000L;
constexpr uintptr_t HUGE_ARENA_START = 0x3270000000L;

constexpr uintptr_t INCREMENT = 16 * 1024 * 1024;
constexpr uintptr_t INITIAL_MAP_SIZE = 64 * 1024 * 1024;

bool _doFree(GCAllocation* al, std::vector<Box*>* weakly_referenced);

} // namespace gc
} // namespace pyston

#endif
