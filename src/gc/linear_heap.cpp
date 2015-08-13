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

//            LOCK_REGION(lock);
            Obj* obj = _alloc(bytes + size_of_header);
            obj->size = bytes;
            obj->magic = 0xCAFEBABE;
            obj->forward = NULL;

            return obj->data;
        }

        LinearHeap::Obj*LinearHeap::_alloc(size_t size) {
            obj.push_back(arena->cur);
            obj_set.insert(arena->cur);

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

            auto it = std::lower_bound(obj.begin(), obj.end(), ptr);

            if (it == obj.begin() && *it > ptr) return NULL;

            if (it == obj.end() || *it > ptr) --it;

            Obj* tmp = reinterpret_cast<Obj*>(*it);
            if ((void**)((char*)tmp + tmp->size + size_of_header) < ptr)
                return NULL;
            else
                return tmp->data;
        }

        void LinearHeap::freeUnmarked(std::vector<Box *> &weakly_referenced) {
            for (auto p = obj.begin(); p != obj.end(); ++p) {
                if (!obj_set.count(*p)) {
                    continue;
                }

                GCAllocation* al = reinterpret_cast<Obj*>(*p)->data;
                clearOrderingState(al);
                if(isMarked(al)) {
                    ::pyston::gc::clearMark(al);
                }
                else {
                    if (_doFree(al, &weakly_referenced))
                        obj_set.erase(*p);
                }
            }
        }

        void LinearHeap::prepareForCollection() {

        }

        void LinearHeap::cleanupAfterCollection() {
            obj.clear();
            std::copy(obj_set.begin(), obj_set.end(), std::back_inserter(obj));
        }

        void LinearHeap::dumpHeapStatistics(int level) {

        }

        void LinearHeap::clear() {
            obj.clear();
            obj_set.clear();
            arena->cur = (void*)arena->arena_start;
        }

        void LinearHeap::clearMark() {
            for(auto scan = obj_set.begin(); scan != obj_set.end(); ++scan) {
                GCAllocation* al = reinterpret_cast<Obj*>(*scan)->data;
                if (isMarked(al))
                    ::pyston::gc::clearMark(al);
            }
        }

        void LinearHeap::_erase_from_obj_set(GCAllocation *al) {
            void *p = Obj::fromAllocation(al);
            auto it = std::lower_bound(obj.begin(), obj.end(), p);
            obj_set.erase(*it);
        }
    } // namespace gc
} // namespace pyston