// Copyright (c) 2014-2015 Dropbox, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

#include "core/common.h"
#include "core/util.h"
#include "gc/gc_alloc.h"
#include "runtime/objmodel.h"
#include "runtime/types.h"

#ifndef NVALGRIND
#include "valgrind.h"
#endif

#include <unordered_map>

//#undef VERBOSITY
//#define VERBOSITY(x) 2


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

            addReference(const_cast<void**>(start), *start);

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

            addReference(const_cast<void**>(start), *start);

            visitPotential(*start);
            start++;
        }
    }

    void GCVisitor::addReference(void** from, void *to) {
        if(!allow_remap) return;

        GCAllocation* a = global_heap->getAllocationFromInteriorPointer(to);
        if (a) {
            void* p = a->user_data;

            if ((uintptr_t)p < SMALL_ARENA_START || (uintptr_t)p >= HUGE_ARENA_START + ARENA_SIZE) {
                return;
            }

            ASSERT(global_heap->getAllocationFromInteriorPointer(p)->user_data == p, "%p", p);

            remap[from] = to;
        }
    }

unsigned bytesAllocatedSinceCollection;
static StatCounter gc_registered_bytes("gc_registered_bytes");
void _bytesAllocatedTripped() {
    gc_registered_bytes.log(bytesAllocatedSinceCollection);
    bytesAllocatedSinceCollection = 0;

    if (!gcIsEnabled())
        return;

    threading::GLPromoteRegion _lock;

    runCollection();
}

//////
/// Finalizers

bool hasOrderedFinalizer(BoxedClass* cls) {
    if (cls->has_safe_tp_dealloc) {
        ASSERT(!cls->tp_del, "class \"%s\" with safe tp_dealloc also has tp_del?", cls->tp_name);
        return false;
    } else if (cls->hasNonDefaultTpDealloc()) {
        return true;
    } else {
        // The default tp_dealloc calls tp_del if there is one.
        return cls->tp_del != NULL;
    }
}

void finalize(Box* b) {
    GCAllocation* al = GCAllocation::fromUserData(b);
    assert(!hasFinalized(al));
    setFinalized(al);
    b->cls->tp_dealloc(b);
}

__attribute__((always_inline)) bool isWeaklyReferenced(Box* b) {
    if (PyType_SUPPORTS_WEAKREFS(b->cls)) {
        PyWeakReference** list = (PyWeakReference**)PyObject_GET_WEAKREFS_LISTPTR(b);
        if (list && *list) {
            return true;
        }
    }

    return false;
}

__attribute__((always_inline)) bool _doFree(GCAllocation* al, std::vector<Box*>* weakly_referenced) {
    static StatCounter gc_safe_destructors("gc_safe_destructor_calls");

#ifndef NVALGRIND
    VALGRIND_DISABLE_ERROR_REPORTING;
#endif
    GCKind alloc_kind = al->kind_id;
#ifndef NVALGRIND
    VALGRIND_ENABLE_ERROR_REPORTING;
#endif

    if (alloc_kind == GCKind::PYTHON || alloc_kind == GCKind::CONSERVATIVE_PYTHON) {
#ifndef NVALGRIND
        VALGRIND_DISABLE_ERROR_REPORTING;
#endif
        Box* b = (Box*)al->user_data;
#ifndef NVALGRIND
        VALGRIND_ENABLE_ERROR_REPORTING;
#endif

        assert(b->cls);
        if (isWeaklyReferenced(b)) {
            assert(weakly_referenced && "attempting to free a weakly referenced object manually");
            weakly_referenced->push_back(b);
            return false;
        }

        ASSERT(!hasOrderedFinalizer(b->cls) || hasFinalized(al) || alloc_kind == GCKind::CONSERVATIVE_PYTHON, "%s",
               getTypeName(b));

        if (b->cls->tp_dealloc != dealloc_null && b->cls->has_safe_tp_dealloc) {
            gc_safe_destructors.log();

            GCAllocation* al = GCAllocation::fromUserData(b);
            assert(!hasFinalized(al));
            assert(!hasOrderedFinalizer(b->cls));

            // Don't bother setting the finalized flag since the object is getting freed right now.
            b->cls->tp_dealloc(b);
        }
    }
    return true;
}




} // namespace gc
} // namespace pyston
