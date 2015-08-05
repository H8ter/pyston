//
// Created by user on 8/4/15.
//

#include "gc_visitor.h"
#include "gc/trace_stack.h"
#include "gc/heap.h"

#include "gc/addr_remap.h"

namespace pyston {
namespace gc {

    bool GCVisitor::isValid(void* p) {
        return global_heap->getAllocationFromInteriorPointer(p) != NULL;
    }

    void GCVisitor::visit(void* p) {

        if ((uintptr_t)p < SMALL_ARENA_START || (uintptr_t)p >= HUGE_ARENA_START + ARENA_SIZE) {
            //ASSERT(!p || isNonheapRoot(p), "%p %p %p", p, min_nonheap_root, max_nonheap_root);
            return;
        }

        ASSERT(global_heap->getAllocationFromInteriorPointer(p)->user_data == p, "%p", p);
        stack->push(p);
    }

    void GCVisitor::visitRange(void* const* start, void* const* end) {
        ASSERT((const char*)end - (const char*)start <= 1000000000, "Asked to scan %.1fGB -- a bug?",
               ((const char*)end - (const char*)start) * 1.0 / (1 << 30));

        assert((uintptr_t)start % sizeof(void*) == 0);
        assert((uintptr_t)end % sizeof(void*) == 0);

        while (start < end) {
#if TRACE_GC_MARKING
            GC_TRACE_LOG("Found precise reference to object %p from %p\n", *start, start);
#endif

            AddrRemap::addReference(global_heap, const_cast<void**>(start), *start);

            visit(*start);
            start++;
        }
    }

    void GCVisitor::visitPotential(void* p) {
        GCAllocation* a = global_heap->getAllocationFromInteriorPointer(p);
        if (a) {
            visit(a->user_data);
        }
    }

    void GCVisitor::visitPotentialRange(void* const* start, void* const* end) {
        ASSERT((const char*)end - (const char*)start <= 1000000000, "Asked to scan %.1fGB -- a bug?",
               ((const char*)end - (const char*)start) * 1.0 / (1 << 30));

        assert((uintptr_t)start % sizeof(void*) == 0);
        assert((uintptr_t)end % sizeof(void*) == 0);

        while (start < end) {
#if TRACE_GC_MARKING
        if (global_heap->getAllocationFromInteriorPointer(*start)) {
            if (*start >= (void*)HUGE_ARENA_START)
                GC_TRACE_LOG("Found conservative reference to huge object %p from %p\n", *start, start);
            else if (*start >= (void*)LARGE_ARENA_START && *start < (void*)HUGE_ARENA_START)
                GC_TRACE_LOG("Found conservative reference to large object %p from %p\n", *start, start);
            else
                GC_TRACE_LOG("Found conservative reference to %p from %p\n", *start, start);
        }
#endif

            AddrRemap::addReference(global_heap, const_cast<void**>(start), *start);

            visitPotential(*start);
            start++;
        }
    }

}
}