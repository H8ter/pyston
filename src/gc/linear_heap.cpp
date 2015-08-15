//
// Created by user on 8/8/15.
//

#include "linear_heap.h"



namespace pyston {
    namespace gc {
        LinearHeap::LinearHeap(uintptr_t arena_start, uintptr_t arena_size,
                               uintptr_t initial_map_size, uintptr_t increment,
                               bool alloc_register)
                : arena(new Arena(arena_start, arena_size, initial_map_size, increment)),
                  alloc_register(alloc_register)
        {
        }

        LinearHeap::~LinearHeap() {
            delete arena;
        }

        bool LinearHeap::fit(size_t bytes) {
            return arena->fit(bytes + size_of_header);
        }

        GCAllocation *LinearHeap::alloc(size_t bytes) {
            if(alloc_register)
                registerGCManagedBytes(bytes);

            LOCK_REGION(lock);
            Obj* obj = _alloc(bytes + size_of_header);
            obj->size = bytes;
            obj->magic = 0xCAFEBABE;
            obj->forward = NULL;

            return obj->data;
        }

        LinearHeap::Obj*LinearHeap::_alloc(size_t size) {
            obj_set.insert(arena->cur);
#if _TEST_
            test_set.insert(arena->cur);
            RELEASE_ASSERT(size_test(), "");
#endif
            void* obj = arena->allocFromArena(size);

            ASSERT(obj < arena->frontier, "Panic! May not alloc beyound the heap!");

            return (Obj*)obj;
        }

        GCAllocation *LinearHeap::realloc(GCAllocation *al, size_t bytes) {
            Obj* o = Obj::fromAllocation(al);
            size_t size = o->size;
            if (size >= bytes && size < bytes * 2)
                return al;

            GCAllocation* rtn = alloc(bytes);
            memcpy(rtn, al, std::min(bytes, size));

            _erase_from_obj_set(al);

            return rtn;
        }


        void LinearHeap::realloc(LinearHeap* lhs, LinearHeap* rhs,
                                 GCAllocation* from, GCAllocation* to,
                                 size_t bytes)
        {
            memcpy(to, from, bytes);

            lhs->_erase_from_obj_set(from);
        }

        void LinearHeap::destructContents(GCAllocation *alloc) {
            _doFree(alloc, NULL);
        }

        void LinearHeap::free(GCAllocation *alloc) {
            destructContents(alloc);

            _erase_from_obj_set(alloc);
        }

        GCAllocation *LinearHeap::getAllocationFromInteriorPointer(void *ptr) {
            if (!arena->contains(ptr))
                return NULL;
#if _TEST_
            RELEASE_ASSERT(size_test(), "before\n");
#endif

            if (obj_set.size() == 0)
                return NULL;

            auto it = obj_set.lower_bound(ptr);
#if _TEST_
            RELEASE_ASSERT(size_test(), "after\n");
#endif

            if (it == obj_set.begin() && *it > ptr) {
                return NULL;
            }

            if (obj_set.size() && (it == obj_set.end() || *it > ptr)) {
                --it;
            }

#if _TEST_
            auto x = test_set.lower_bound(ptr);
            if (test_set.size() && (x == test_set.end() || *x > ptr)) {
                --x;
            }
            RELEASE_ASSERT(*x == *it, "%p | %p %p| %d\n", ptr, *x, *it, obj_set.count(*x));
#endif

            Obj* tmp = reinterpret_cast<Obj*>(*it);
            if ((void**)((char*)tmp + tmp->size + size_of_header) < ptr)
                return NULL;
            else
                return tmp->data;
        }

        void LinearHeap::freeUnmarked(std::vector<Box *> &weakly_referenced) {
            #if _TEST_
            RELEASE_ASSERT(size_test(), "");
            #endif
//            std::vector<void*> e;
            for(auto p = obj_set.begin(); p != obj_set.end();) {
                GCAllocation* al = reinterpret_cast<Obj*>(*p)->data;
                clearOrderingState(al);

                if(isMarked(al)) {
                    ::pyston::gc::clearMark(al);
                    ++p;
                }
                else {
                    if (_doFree(al, &weakly_referenced)) {
#if _TEST_
                        test_set.erase(*p);
#endif
                        p = obj_set.erase(p);
//                        e.push_back(*p);
//                        ++p;
                    }
                    else ++p;
                }
            }
//            for(auto er : e)
//                obj_set.erase(er);
        }

        void LinearHeap::prepareForCollection() {

        }

        void LinearHeap::cleanupAfterCollection() {

        }

        void LinearHeap::dumpHeapStatistics(int level) {

        }

        void LinearHeap::clear() {
#if _TEST_
            RELEASE_ASSERT(size_test(), "");
#endif

            obj_set.clear();
            arena->cur = (void*)arena->arena_start;

            #if _TEST_
            test_set.clear();
            #endif
        }

        void LinearHeap::clearMark() {
#if _TEST_
            RELEASE_ASSERT(size_test(), "");
#endif

            for(auto scan = obj_set.begin(); scan != obj_set.end(); ++scan) {
                GCAllocation* al = reinterpret_cast<Obj*>(*scan)->data;
                if (isMarked(al))
                    ::pyston::gc::clearMark(al);
            }
        }

        void LinearHeap::_erase_from_obj_set(GCAllocation *al) {
#if _TEST_
            RELEASE_ASSERT(size_test(), "");
#endif

            void *p = Obj::fromAllocation(al);

            auto it = obj_set.lower_bound(p); // obj_set
            if (it == obj_set.end()) return;  // obj_set

            void* val = *it;
#if _TEST_
            test_set.erase(val);
#endif
            obj_set.erase(val);
        }
    } // namespace gc
} // namespace pyston