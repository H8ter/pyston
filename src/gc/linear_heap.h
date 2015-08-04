//
// Created by user on 8/4/15.
//

#ifndef PYSTON_LINEAR_HEAP_H
#define PYSTON_LINEAR_HEAP_H

#include "heap.h"
#include <algorithm>
#include <vector>
#include <set>

namespace pyston {
    namespace gc {

        class SemiSpaceGC;

        class LinearHeap : public Heap {
        private:
            static const uintptr_t increment = 16 * 1024 * 1024;
            static const uintptr_t initial_map_size = 64 * 1024 * 1024;

            using SemiSpace = Arena<ARENA_SIZE, initial_map_size, increment>;

            SemiSpace* const arena;

            struct Obj {
                size_t magic;
                size_t size;
                void* forward;
                GCAllocation data[0];

                Obj() {
                    magic = 0xCAFEBABE;
                    forward = NULL;
                }

                static Obj* fromAllocation(GCAllocation* alloc) {
                    char* rtn = reinterpret_cast<char*>(alloc) - offsetof(Obj, data);
                    return reinterpret_cast<Obj*>(rtn);
                }

                static Obj* fromUserData(void* p) {
                    return fromAllocation(GCAllocation::fromUserData(p));
                }
            };

            std::vector<void*> obj;
            std::set<void*> obj_set;

            bool alloc_register;

        public:
            friend class SemiSpaceGC;
            friend class SemiSpaceHeap;

            LinearHeap(uintptr_t arena_start, bool alloc_register = true);

            virtual ~LinearHeap();

            virtual GCAllocation *alloc(size_t bytes) override;

            virtual GCAllocation *realloc(GCAllocation *alloc, size_t bytes) override;

            virtual void destructContents(GCAllocation *alloc) override;

            virtual void free(GCAllocation *alloc) override;

            virtual GCAllocation *getAllocationFromInteriorPointer(void *ptr) override;

            virtual void freeUnmarked(std::vector<Box *> &weakly_referenced) override;

            virtual void prepareForCollection() override;

            virtual void cleanupAfterCollection() override;

            virtual void dumpHeapStatistics(int level) override;

            void clear();

            void clearMark();

        private:

            Obj* _alloc(size_t size);
        };

    }
}


#endif //PYSTON_LINEAR_HEAP_H
