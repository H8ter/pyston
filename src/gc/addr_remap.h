//
// Created by user on 8/8/15.
//

#ifndef PYSTON_ADDR_REMAP_H
#define PYSTON_ADDR_REMAP_H

#include "gc/heap.h"
#include <unordered_map>


namespace pyston {
    namespace gc {

        class AddrRemap {
        private:

            static bool enabled;

        public:

            static std::unordered_map<void**, void*> remap;

            static void disable() { enabled = false; }
            static void enable() { enabled = true; }

            static void addReference(Heap* heap, void** from, void* to) {
                if(!enabled) return;

                GCAllocation* a = heap->getAllocationFromInteriorPointer(to);
                if (a) {
                    void* p = a->user_data;

                    if ((uintptr_t)p < SMALL_ARENA_START || (uintptr_t)p >= HUGE_ARENA_START + ARENA_SIZE) {
                        return;
                    }

                    ASSERT(heap->getAllocationFromInteriorPointer(p)->user_data == p, "%p", p);

//                GC_TRACE_LOG("%p\n", a);
//                GC_TRACE_LOG("Seeking for %p [%p]\n", to, from);

                    remap[from] = to;
                }
            }
        };

    }
}


#endif //PYSTON_ADDR_REMAP_H
