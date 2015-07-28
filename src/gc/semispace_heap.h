//
// Created by user on 7/25/15.
//

#ifndef PYSTON_SEMISPACE_HEAP_H
#define PYSTON_SEMISPACE_HEAP_H



#include "heap.h"
#include <algorithm>
#include <vector>
#include <set>

namespace pyston {
namespace gc {

        class SemiSpaceGC;

        class SemiSpaceHeap : public Heap {
        private:
            static const uintptr_t increment = 16 * 1024 * 1024;
            static const uintptr_t initial_map_size = 64 * 1024 * 1024;

            class SSArena : public Arena<ARENA_SIZE, initial_map_size, increment> {
            public:
                SSArena(uintptr_t arena_start) : Arena(arena_start) { }
            };

            //using SemiSpace = Arena<ARENA_SIZE, initial_map_size, increment>;
            using SemiSpace = SSArena;

            SemiSpace* tospace;

            struct Obj {
                size_t size;
                Obj* forward;
                GCAllocation data[0];

                Obj() {
                    forward = nullptr;
                }

                static Obj* fromAllocation(GCAllocation* alloc) {
                    char* rtn = (char*)alloc - offsetof(Obj, data);
                    return reinterpret_cast<Obj*>(rtn);
                }
            };

            std::vector<void*> obj;
            std::set<void*> obj_set;

        public:
            friend class SemiSpaceGC;

            SemiSpaceHeap(uintptr_t arena_start);

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

            Obj* _alloc(size_t size);
        };

}
}


#endif //PYSTON_SEMISPACE_HEAP_H
