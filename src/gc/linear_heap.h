//
// Created by user on 8/8/15.
//

#ifndef PYSTON_LINEAR_HEAP_H
#define PYSTON_LINEAR_HEAP_H



#include "heap.h"
#include <algorithm>
#include <vector>
#include <set>
#include <map>

#include "splay_tree.h"

namespace pyston {
    namespace gc {

        #define _TEST_ 0

        class LinearHeap : public Heap {
        private:

            using SemiSpace = Arena;

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

            static const size_t size_of_header = sizeof(GCAllocation) + sizeof(Obj);

            std::set<void*> obj_set;
//            splay_tree<void*> obj_set;
#if _TEST_
            std::set<void*> test_set;
            bool size_test() {
                if (obj_set.size() != (int)test_set.size()) {
                    fprintf(stderr, "%d %d\n", obj_set.size(), (int)test_set.size());
                    return false;
                }
                return true;
            }
#endif

            bool alloc_register;

        public:
            friend class SemiSpaceGC;
            friend class HybridSemiSpaceGC;
            friend class SemiSpaceHeap;

            friend class BartlettHeap;
            friend class BartlettGC;

            LinearHeap(uintptr_t arena_start, uintptr_t arena_size,
                       uintptr_t initial_map_size, uintptr_t increment,
                       bool alloc_register = true);

            virtual ~LinearHeap();

            bool fit(size_t bytes);

            virtual GCAllocation *alloc(size_t bytes) override;

            virtual GCAllocation *realloc(GCAllocation *alloc, size_t bytes) override;

            static void realloc(LinearHeap* lhs, LinearHeap* rhs, GCAllocation* from, GCAllocation* to, size_t bytes);

            virtual void destructContents(GCAllocation *alloc) override;

            virtual void free(GCAllocation *alloc) override;

            virtual GCAllocation *getAllocationFromInteriorPointer(void *ptr) override;

            virtual void freeUnmarked(std::vector<Box *> &weakly_referenced) override;

            virtual void prepareForCollection() override;

            virtual void cleanupAfterCollection() override;

            virtual void dumpHeapStatistics(int level) override;

            void clear();

            void clearMark();

//            std::map<int64_t,int64_t> diff_set;

        private:

            Obj* _alloc(size_t size);

            void _erase_from_obj_set(GCAllocation *al);
        };

    }
}



#endif //PYSTON_LINEAR_HEAP_H
