//
// Created by user on 7/25/15.
//

#include "semispace_heap.h"

namespace pyston {
namespace gc {
        SemiSpaceHeap::SemiSpaceHeap(uintptr_t arena_start) {
//            fromspace = new Arena<ARENA_SIZE, initial_map_size, increment>(SMALL_ARENA_START);
//            tospace   = new Arena<ARENA_SIZE, initial_map_size, increment>(LARGE_ARENA_START);
            tospace   = new SSArena(arena_start);
        }

        SemiSpaceHeap::~SemiSpaceHeap() {
            delete tospace;
        }

        GCAllocation *SemiSpaceHeap::alloc(size_t bytes) {
            registerGCManagedBytes(bytes);

            Obj* obj = _alloc(bytes + sizeof(GCAllocation) + sizeof(Obj));
            obj->size = bytes;

            return obj->data;
        }

        SemiSpaceHeap::Obj* SemiSpaceHeap::_alloc(size_t size) {
            obj.push_back(tospace->cur);
            obj_set.insert(tospace->cur);

            //void* old_frontier = tospace->frontier;

            void* obj = tospace->allocFromArena(size);

// TODO: сделать extend во время GC

            /*if ((uint8_t*)old_frontier < (uint8_t*)tospace->frontier) {
                size_t grow_size = (size + increment - 1) & ~(increment - 1);
                fromspace->extendMapping(grow_size);
            }*/

            return (Obj*)obj;
        }

        GCAllocation *SemiSpaceHeap::realloc(GCAllocation *al, size_t bytes) {
            Obj* o = Obj::fromAllocation(al);
            size_t size = o->size;
            if (size >= bytes && size < bytes * 2)
                return al;

            GCAllocation* rtn = alloc(bytes);
            memcpy(rtn, al, std::min(bytes, size));

            void *p = Obj::fromAllocation(al);
            auto it = std::lower_bound(obj.begin(), obj.end(), p);
            obj_set.erase(*it);

            return rtn;
        }

        void SemiSpaceHeap::destructContents(GCAllocation *alloc) {
            _doFree(alloc, NULL);
        }

        void SemiSpaceHeap::free(GCAllocation *alloc) {
            destructContents(alloc);

            void *p = Obj::fromAllocation(alloc);
            auto it = std::lower_bound(obj.begin(), obj.end(), p);
            obj_set.erase(*it);
        }

        GCAllocation *SemiSpaceHeap::getAllocationFromInteriorPointer(void *ptr) {
            if (!tospace->contains(ptr))
                return NULL;

            //return ((Obj*)ptr)->data;                     // FAILED
            //return GCAllocation::fromUserData(ptr);       // FAILED

            auto it = std::lower_bound(obj.begin(), obj.end(), ptr);

            if (it == obj.end() || *it > ptr) --it;

            return reinterpret_cast<Obj*>(*it)->data;
        }

        void SemiSpaceHeap::freeUnmarked(std::vector<Box *> &weakly_referenced) {
            for (auto p = obj.begin(); p != obj.end(); ++p) {
                if (!obj_set.count(*p)) {
                    continue;
                }

                GCAllocation* al = reinterpret_cast<Obj*>(*p)->data;
                clearOrderingState(al);
                if(isMarked(al)) {
                    clearMark(al);
                }
                else {
                    if (_doFree(al, &weakly_referenced))
                        obj_set.erase(*p);
                }
            }
        }

        void SemiSpaceHeap::prepareForCollection() {

        }

        void SemiSpaceHeap::cleanupAfterCollection() {
            obj.clear();
            std::copy(obj_set.begin(), obj_set.end(), std::back_inserter(obj));
        }

        void SemiSpaceHeap::dumpHeapStatistics(int level) {

        }
} // namespace gc
} // namespace pyston
