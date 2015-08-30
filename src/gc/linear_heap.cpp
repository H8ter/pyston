//
// Created by user on 8/8/15.
//

#include "linear_heap.h"

#include "collector.h"


namespace pyston {
    namespace gc {
        LinearHeap::LinearHeap(uintptr_t arena_start, uintptr_t arena_size,
                               uintptr_t initial_map_size, uintptr_t increment,
                               bool alloc_register)
                : arena(new Arena(arena_start, arena_size, initial_map_size, increment)),
                  alloc_register(alloc_register)
        {
//            uint p = (uint)ceil(log(arena_size) / log(2.0));
//            obj_set = x_fast_trie(p);
            last_alloc = 0;
            first_alive_object = 0;
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
            obj->flags = 0;
            Obj::set_alive_flag(obj);

            if (last_alloc) {
                RELEASE_ASSERT(reinterpret_cast<Obj*>(last_alloc)->magic == (size_t)0xCAFEBABE, "%p\n", last_alloc);
            }

            obj->prev = last_alloc;
            if (obj->prev) {
                reinterpret_cast<Obj*>(obj->prev)->next = obj;
            }
            obj->next = 0;
            last_alloc = obj;

            if (!first_alive_object) first_alive_object = obj;

            return obj->data;
        }

        LinearHeap::Obj*LinearHeap::_alloc(size_t size) {
#if OBJSET
            obj_set.insert(arena->cur);
#if _TEST_
            test_set.insert(arena->cur);
            RELEASE_ASSERT(size_test(), "");
#endif
#endif
            void* obj = arena->allocFromArena(size);

            ASSERT(obj < arena->frontier, "Panic! May not alloc beyound the heap!");

            return (Obj*)obj;
        }

        GCAllocation *LinearHeap::realloc(GCAllocation *al, size_t bytes) {
            if (!arena->contains(al)) return al;

            size_t size = Obj::fromAllocation(al)->size;

            GCAllocation* rtn = alloc(bytes);
            memcpy(rtn, al, std::min(bytes, size) + sizeof(GCAllocation));

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
            if (_doFree(alloc, NULL))
                _erase_from_obj_set(alloc);
        }

        void LinearHeap::free(GCAllocation *alloc) {
            destructContents(alloc);
        }

        GCAllocation *LinearHeap::getAllocationFromInteriorPointer(void *ptr) {
            static StatCounter sc_us("us_gc_get_allocation_from_interior_pointer");
            Timer _t("getAllocationFromInteriorPointer", /*min_usec=*/0); // 10000

            GCAllocation* alc = NULL;

#if OBJSET
            #if _TEST_
            RELEASE_ASSERT(size_test(), "before\n");
#endif

            int64_t diff = 0;
            if (!arena->contains(ptr) || obj_set.size() == 0)
                alc = NULL;
            else {
                auto it = obj_set.lower_bound(ptr);
#if _TEST_
            RELEASE_ASSERT(size_test(), "after\n");
#endif

                if (it == obj_set.begin() && *it > ptr) {
                    alc = NULL;
                }
                else {
                    if (obj_set.size() && (it == obj_set.end() || *it > ptr)) --it;

#if _TEST_
                    auto x = test_set.lower_bound(ptr);
                    if (test_set.size() && (x == test_set.end() || *x > ptr)) {
                        --x;
                    }

                    RELEASE_ASSERT(*x == (void*)*it, "%p | %p %p| %p %p| %d\n", ptr, *x, (void*)*it, (void*)*obj_set.begin(), (void*)*test_set.begin(),obj_set.count((uint)*x));
#endif

                    Obj* tmp = reinterpret_cast<Obj*>((void*)*it);                      // !!!
                    if ((void**)((char*)tmp + tmp->size + size_of_header) < ptr) // !!!
                        alc = NULL;
                    else {
                        alc = tmp->data;

                        diff = DIFF(ptr, tmp->data->user_data);

                        if (diff < -8 || diff > 0) {
                            alc = NULL;
                        }
                        else {
//#if TRACE_GC_MARKING
//                            diff_set[diff]++;
//#endif
                            RELEASE_ASSERT(Obj::alive(tmp), "object should be alive\n");
//                            if (diff != 0) fprintf(stderr, "%d\n", (int)diff);
                        }
                    }
                }

            }
#else
            if (!arena->contains(ptr))
                alc = NULL;
            else {
                Obj* tmp = 0;
                for(int diff = 0; diff <= 8; ++diff) {
                    void *p = (char *) ptr - 28 + diff;

                    if (arena->contains(p) &&
                        *((unsigned int *) (p)) == (unsigned int) 0xCAFEBABE) {
                        tmp = reinterpret_cast<Obj *>((void *) ((char *) p - 20));  // -4
                        break;
                    }
                }
                if (tmp && Obj::alive(tmp)) {
                    alc = tmp->data;
                }
                else {
                    alc = NULL;
                }
            }
#endif
            long us = _t.end();
            sc_us.log(us);

            return alc;
        }

        void LinearHeap::freeUnmarked(std::vector<Box *> &weakly_referenced) {
#if OBJSET
            #if _TEST_
            RELEASE_ASSERT(size_test(), "");
            #endif
            for(auto p = obj_set.begin(); p != obj_set.end();) {
                GCAllocation* al = reinterpret_cast<Obj*>(*p)->data;            // !!!
                clearOrderingState(al);

                if(isMarked(al)) {
                    ::pyston::gc::clearMark(al);
                    ++p;
                }
                else {
                    if (_doFree(al, &weakly_referenced)) {
#if _TEST_
                        test_set.erase((void*)*p);
#endif
                        Obj::clear_alive_flag(reinterpret_cast<Obj*>(*p));
                        p = obj_set.erase(p);
                    }
                    else ++p;
                }
            }
#else
            for(void* p = first_alive(); p && p < arena->cur;) {
                GCAllocation* al = reinterpret_cast<Obj*>(p)->data;
                clearOrderingState(al);

                if(isMarked(al)) {
                    ::pyston::gc::clearMark(al);
                    p = next_object(p);
                }
                else {
                    void* nxt = next_object(p);
                    if (_doFree(al, &weakly_referenced)) {
//                        Obj::clear_alive_flag(reinterpret_cast<Obj *>(p));
                        _erase_from_obj_set(reinterpret_cast<Obj *>(p)->data);
                    }
                    p = nxt;
                }
            }
#endif
        }

        void LinearHeap::prepareForCollection() {
            alloc_register = false;
        }

        void LinearHeap::cleanupAfterCollection() {
            alloc_register = true;
        }

        void LinearHeap::dumpHeapStatistics(int level) {

        }

        void LinearHeap::clear() {
#if OBJSET
#if _TEST_
            RELEASE_ASSERT(size_test(), "");
            test_set.clear();
#endif
            obj_set.clear();
#endif
            arena->cur = (void*)arena->arena_start;
            last_alloc = 0;
            first_alive_object = 0;
        }

        void LinearHeap::clearMark() {
#if OBJSET
#if _TEST_
            RELEASE_ASSERT(size_test(), "");
#endif
            for(auto scan = obj_set.begin(); scan != obj_set.end(); ++scan) {
                GCAllocation* al = reinterpret_cast<Obj*>(*scan)->data;         // !!!
                if (isMarked(al))
                    ::pyston::gc::clearMark(al);
            }
#else
            for(void* p = first_alive(); p && p < arena->cur; p = next_object(p)) {
                GCAllocation* al = reinterpret_cast<Obj*>(p)->data;
                if (isMarked(al))
                    ::pyston::gc::clearMark(al);
            }
#endif
        }

        void LinearHeap::_erase_from_obj_set(GCAllocation *al) {
#if OBJSET
#if _TEST_
            RELEASE_ASSERT(size_test(), "");
#endif

            void *p = Obj::fromAllocation(al);

            auto it = obj_set.lower_bound(p);
            if (it == obj_set.end()) return;


            void* val = (void*)*it;
#if _TEST_
            test_set.erase(val);
#endif
            obj_set.erase(val);
#else
            Obj* obj = Obj::fromAllocation(al);
            RELEASE_ASSERT(Obj::alive(obj), "");
            RELEASE_ASSERT(obj->magic == (size_t)0xCAFEBABE, "%p %p %p %p\n", (void*)arena->arena_start, first_alive_object, last_alloc, al);

            if (obj->prev) {
                RELEASE_ASSERT(Obj::alive((Obj*)obj->prev), "prev object %p should be alive\n", obj->prev);
                reinterpret_cast<Obj*>(obj->prev)->next = obj->next;
            }
            if (obj->next) {
                RELEASE_ASSERT(Obj::alive((Obj*)obj->next), "next object %p should be alive\n", obj->next);
                reinterpret_cast<Obj*>(obj->next)->prev = obj->prev;
            }

            if (last_alloc == obj) {
                last_alloc = obj->prev;
                while (last_alloc && !Obj::alive((Obj*)last_alloc))
                    last_alloc = reinterpret_cast<Obj*>(last_alloc)->prev;
            }

            if (first_alive_object == obj) {
                first_alive_object = obj->next;
                while (first_alive_object && !Obj::alive((Obj*)first_alive_object))
                    first_alive_object = reinterpret_cast<Obj*>(first_alive_object)->next;
            }


            obj->prev = obj->next = 0;

            Obj::clear_alive_flag(obj);
#endif
        }
    } // namespace gc
} // namespace pyston