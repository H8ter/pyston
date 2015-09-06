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
#include "x_fast_trie.h"

namespace pyston {
    namespace gc {

        #define _TEST_ 0
        #define OBJSET 0

        class LinearHeap : public Heap {
        private:

            using SemiSpace = Arena;

            SemiSpace* const arena;


            #define ALIVE_BIT 0x1
            struct Obj {
//                void* guard;
                void* prev;
                void* next;
                size_t flags : 32;
                size_t magic : 32;
                size_t size;
                void* forward;
                GCAllocation data[0];

                Obj() {
                    magic = 0xCAFEBABE;
                    forward = NULL;
                    prev = next = 0;
                }

                static Obj* fromAllocation(GCAllocation* alloc) {
                    char* rtn = reinterpret_cast<char*>(alloc) - offsetof(Obj, data);
                    return reinterpret_cast<Obj*>(rtn);
                }

                static Obj* fromUserData(void* p) {
                    return fromAllocation(GCAllocation::fromUserData(p));
                }

                static bool alive(Obj* obj) {
                    RELEASE_ASSERT(obj->magic == (size_t)0xCAFEBABE, "%p\n", obj);
                    return bool(obj->flags & ALIVE_BIT);
                }

                static void set_alive_flag(Obj* obj) {
                    obj->flags |= ALIVE_BIT;
                }

                static void clear_alive_flag(Obj* obj) {
                    obj->flags &= ~ALIVE_BIT;
                }
            };

            static const size_t size_of_header = sizeof(GCAllocation) + sizeof(Obj);
#if OBJSET
            std::set<void*> obj_set;
#endif
//            splay_tree<void*> obj_set;
//            x_fast_trie obj_set;
#if _TEST_
            std::set<void*> test_set;
//            splay_tree<void*> test_set;
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

//#if 1
//            std::map<int64_t,int64_t> diff_set;
//#endif
        private:

            Obj* _alloc(size_t size);

            void _erase_from_obj_set(GCAllocation *al);

            void* first_alive() {
                return first_alive_object;
            }

            // not fast enought
            void* next_object(void* p) {
                void* nxt = p;
                do {
                    nxt = reinterpret_cast<Obj*>(nxt)->next;
                } while(nxt && !Obj::alive((Obj*)nxt));

                return nxt;
            }

            void* last_alloc;
            void* first_alive_object;

            static void showObject(FILE *f, Obj* obj) {
                fprintf(f, "%p\n", obj);

                uint64_t bytes = obj->size + sizeof(Obj)+ sizeof(GCAllocation);
                void** start = (void**)obj;
                void** end = (void**)((char*)obj + bytes);

                while(start < end) {
                    fprintf(f, "%p ", *start);
                    ++start;
                }
                fprintf(f, "\n");
            }
        };

    }
}



#endif //PYSTON_LINEAR_HEAP_H
