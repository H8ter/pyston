//
// Created by user on 7/25/15.
//

#include "gc_base.h"

#include "gc_base.h"

#include "codegen/ast_interpreter.h"
#include "codegen/codegen.h"
#include "core/common.h"
#include "core/threading.h"
#include "core/types.h"
#include "core/util.h"
#include "runtime/objmodel.h"
#include "runtime/types.h"
#include "gc/trace_stack.h"

namespace pyston {
    namespace gc {

        std::vector<void**> TraceStack::free_chunks;

        bool GCBase::gcIsEnabled() {
            return gc_enabled;
        }

        void GCBase::disableGC() {
            gc_enabled = false;
        }

        void GCBase::enableGC() {
            gc_enabled = true;
        }


        void gc::GCBase::visitByGCKind(void *p, GCVisitor &visitor) {
            assert(((intptr_t)p) % 8 == 0);

            GCAllocation* al = GCAllocation::fromUserData(p);

            GCKind kind_id = al->kind_id;
            if (kind_id == GCKind::UNTRACKED) {
                // Nothing to do here.
            } else if (kind_id == GCKind::CONSERVATIVE || kind_id == GCKind::CONSERVATIVE_PYTHON) {
                uint32_t bytes = al->kind_data;
                visitor.visitPotentialRange((void**)p, (void**)((char*)p + bytes));
            } else if (kind_id == GCKind::PRECISE) {
                uint32_t bytes = al->kind_data;
                visitor.visitRange((void**)p, (void**)((char*)p + bytes));
            } else if (kind_id == GCKind::PYTHON) {
                Box* b = reinterpret_cast<Box*>(p);
                BoxedClass* cls = b->cls;

                if (cls) {
                    // The cls can be NULL since we use 'new' to construct them.
                    // An arbitrary amount of stuff can happen between the 'new' and
                    // the call to the constructor (ie the args get evaluated), which
                    // can trigger a collection.
                            ASSERT(cls->gc_visit, "%s", getTypeName(b));
                    cls->gc_visit(&visitor, b);
                }
            } else if (kind_id == GCKind::HIDDEN_CLASS) {
                HiddenClass* hcls = reinterpret_cast<HiddenClass*>(p);
                hcls->gc_visit(&visitor);
            } else {
                RELEASE_ASSERT(0, "Unhandled kind: %d", (int)kind_id);
            }
        }

        void gc::GCBase::graphTraversalMarking(gc::TraceStack &stack, GCVisitor &visitor) {
            static StatCounter sc_us("us_gc_mark_phase_graph_traversal");
            static StatCounter sc_marked_objs("gc_marked_object_count");
            Timer _t("traversing", /*min_usec=*/10000);

            while (void* p = stack.pop()) {
                sc_marked_objs.log();

                GCAllocation* al = GCAllocation::fromUserData(p);

#if TRACE_GC_MARKING
        if (al->kind_id == GCKind::PYTHON || al->kind_id == GCKind::CONSERVATIVE_PYTHON)
            GC_TRACE_LOG("Looking at %s object %p\n", static_cast<Box*>(p)->cls->tp_name, p);
        else
            GC_TRACE_LOG("Looking at non-python allocation %p\n", p);
#endif

                assert(isMarked(al));
                visitByGCKind(p, visitor);
            }

            long us = _t.end();
            sc_us.log(us);
        }

        void gc::GCBase::markRoots(GCVisitor &visitor) {
            GC_TRACE_LOG("Looking at the stack\n");
            threading::visitAllStacks(&visitor);

            GC_TRACE_LOG("Looking at root handles\n");
            for (auto h : *getRootHandles()) {
                visitor.visit(h->value);
            }

            GC_TRACE_LOG("Looking at potential root ranges\n");
            for (auto& e : potential_root_ranges) {
                visitor.visitPotentialRange((void* const*)e.first, (void* const*)e.second);
            }

            GC_TRACE_LOG("Looking at pending finalization list\n");
            for (auto box : pending_finalization_list) {
                visitor.visit(box);
            }

            GC_TRACE_LOG("Looking at weakrefs needing callbacks list\n");
            for (auto weakref : weakrefs_needing_callback_list) {
                visitor.visit(weakref);
            }
        }


    }

    void *gc::GCBase::gc_alloc(size_t bytes, gc::GCKind kind_id) {
#if EXPENSIVE_STAT_TIMERS
    // This stat timer is quite expensive, not just because this function is extremely hot,
    // but also because it tends to increase the size of this function enough that we can't
    // inline it, which is especially useful for this function.
    ScopedStatTimer gc_alloc_stattimer(gc_alloc_stattimer_counter, 15);
#endif
        size_t alloc_bytes = bytes + sizeof(GCAllocation);

#ifndef NVALGRIND
// Adding a redzone will confuse the allocator, so disable it for now.
#define REDZONE_SIZE 0
// This can also be set to "RUNNING_ON_VALGRIND", which will only turn on redzones when
// valgrind is actively running, but I think it's better to just always turn them on.
// They're broken and have 0 size anyway.
#define ENABLE_REDZONES 1

    if (ENABLE_REDZONES)
        alloc_bytes += REDZONE_SIZE * 2;
#endif

//
        bytesAllocatedSinceCollection += bytes;
        if (unlikely(bytesAllocatedSinceCollection >= ALLOCBYTES_PER_COLLECTION)) {
            _bytesAllocatedTripped();
        }

        GCAllocation* alloc = global_heap->alloc(alloc_bytes);


#ifndef NVALGRIND
    VALGRIND_DISABLE_ERROR_REPORTING;
#endif


        alloc->kind_id = kind_id;
        alloc->gc_flags = 0;

        if (kind_id == GCKind::CONSERVATIVE || kind_id == GCKind::PRECISE) {
            // Round the size up to the nearest multiple of the pointer width, so that
            // we have an integer number of pointers to scan.
            // TODO We can probably this better; we could round down when we scan, or even
            // not scan this at all -- a non-pointerwidth-multiple allocation seems to mean
            // that it won't be storing pointers (or it will be storing them non-aligned,
            // which we don't support).
            bytes = (bytes + sizeof(void*) - 1) & (~(sizeof(void*) - 1));
            assert(bytes < (1 << 31));
            alloc->kind_data = bytes;
        }

        void* r = alloc->user_data;

#ifndef NVALGRIND
    VALGRIND_ENABLE_ERROR_REPORTING;

    if (ENABLE_REDZONES) {
        r = ((char*)r) + REDZONE_SIZE;
    }
    VALGRIND_MALLOCLIKE_BLOCK(r, bytes, REDZONE_SIZE, false);
#endif

        // TODO This doesn't belong here (probably in PythonGCObject?)...
        if (kind_id == GCKind::PYTHON) {
            ((Box*)r)->cls = NULL;
        }

#ifndef NDEBUG
// I think I have a suspicion: the gc will see the constant and treat it as a
// root.  So instead, shift to hide the pointer
// if ((((intptr_t)r) >> 4) == (0x127014f9f)) {
// raise(SIGTRAP);
//}

// if (VERBOSITY()) printf("Allocated %ld bytes at [%p, %p)\n", bytes, r, (char*)r + bytes);
#endif

#if STAT_ALLOCATIONS
    gc_alloc_bytes.log(bytes);
    gc_alloc_bytes_typed[(int)kind_id].log(bytes);
#endif

        return r;
    }

    void *gc::GCBase::gc_realloc(void *ptr, size_t bytes) {
        // Normal realloc() supports receiving a NULL pointer, but we need to know what the GCKind is:
        assert(ptr);

        size_t alloc_bytes = bytes + sizeof(GCAllocation);

        GCAllocation* alloc;
        void* rtn;

#ifndef NVALGRIND
    if (ENABLE_REDZONES) {
        void* base = (char*)ptr - REDZONE_SIZE;
        alloc = global_heap->realloc(GCAllocation::fromUserData(base), alloc_bytes + 2 * REDZONE_SIZE);
        void* rtn_base = alloc->user_data;
        rtn = (char*)rtn_base + REDZONE_SIZE;
    } else {
        alloc = global_heap->realloc(GCAllocation::fromUserData(ptr), alloc_bytes);
        rtn = alloc->user_data;
    }

    VALGRIND_FREELIKE_BLOCK(ptr, REDZONE_SIZE);
    VALGRIND_MALLOCLIKE_BLOCK(rtn, alloc_bytes, REDZONE_SIZE, true);
#else
        alloc = global_heap->realloc(GCAllocation::fromUserData(ptr), alloc_bytes);
        rtn = alloc->user_data;
#endif

        if (alloc->kind_id == GCKind::CONSERVATIVE || alloc->kind_id == GCKind::PRECISE) {
            bytes = (bytes + sizeof(void*) - 1) & (~(sizeof(void*) - 1));
            assert(bytes < (1 << 31));
            alloc->kind_data = bytes;
        }

#if STAT_ALLOCATIONS
    gc_alloc_bytes.log(bytes);
#endif

        return rtn;
    }

    void gc::GCBase::gc_free(void *ptr) {
        assert(ptr);
#ifndef NVALGRIND
    if (ENABLE_REDZONES) {
        void* base = (char*)ptr - REDZONE_SIZE;
        global_heap->free(GCAllocation::fromUserData(base));
    } else {
        global_heap->free(GCAllocation::fromUserData(ptr));
    }
    VALGRIND_FREELIKE_BLOCK(ptr, REDZONE_SIZE);
#else
        global_heap->free(GCAllocation::fromUserData(ptr));
#endif
    }

    void gc::GCBase::prepareWeakrefCallbacks(Box *box) {
        PyWeakReference** list = (PyWeakReference**)PyObject_GET_WEAKREFS_LISTPTR(box);
        while (PyWeakReference* head = *list) {
            assert(isValidGCObject(head));
            if (head->wr_object != Py_None) {
                assert(head->wr_object == box);
                _PyWeakref_ClearRef(head);

                if (head->wr_callback) {
                    weakrefs_needing_callback_list.push_back(head);
                }
            }
        }

    }

    void gc::GCBase::orderFinalizers() {
        static StatCounter sc_us("us_gc_finalization_ordering");
        Timer _t("finalizationOrdering", /*min_usec=*/10000);

        std::vector<Box*> finalizer_marked;

        for (Box* obj : objects_with_ordered_finalizers) {
            GCAllocation* al = GCAllocation::fromUserData(obj);

            // We are only interested in object with finalizers that need to be garbage-collected.
            if (orderingState(al) == FinalizationState::UNREACHABLE) {
                assert(hasOrderedFinalizer(obj->cls));

                finalizer_marked.push_back(obj);
                finalizationOrderingFindReachable(obj);
                finalizationOrderingRemoveTemporaries(obj);
            }
        }

        for (Box* marked : finalizer_marked) {
            GCAllocation* al = GCAllocation::fromUserData(marked);

            FinalizationState state = orderingState(al);
            assert(state == FinalizationState::REACHABLE_FROM_FINALIZER || state == FinalizationState::ALIVE);

            if (state == FinalizationState::REACHABLE_FROM_FINALIZER) {
                pending_finalization_list.push_back(marked);
            }
        }

        long us = _t.end();
        sc_us.log(us);
    }

    void gc::GCBase::finalizationOrderingFindReachable(Box *obj) {
        static StatCounter sc_marked_objs("gc_marked_object_count_finalizer_ordering");
        static StatCounter sc_us("us_gc_mark_finalizer_ordering_1");
        Timer _t("finalizationOrderingFindReachable", /*min_usec=*/10000);

        TraceStack stack(TraceStackType::FinalizationOrderingFindReachable);
        GCVisitor visitor(&stack, global_heap);

        stack.push(obj);
        while (void* p = stack.pop()) {
            sc_marked_objs.log();

            visitByGCKind(p, visitor);
        }

        long us = _t.end();
        sc_us.log(us);
    }

    void gc::GCBase::finalizationOrderingRemoveTemporaries(Box *obj) {
        static StatCounter sc_us("us_gc_mark_finalizer_ordering_2");
        Timer _t("finalizationOrderingRemoveTemporaries", /*min_usec=*/10000);

        TraceStack stack(TraceStackType::FinalizationOrderingRemoveTemporaries);
        GCVisitor visitor(&stack, global_heap);

        stack.push(obj);
        while (void* p = stack.pop()) {
            GCAllocation* al = GCAllocation::fromUserData(p);
            assert(orderingState(al) != FinalizationState::UNREACHABLE);
            visitByGCKind(p, visitor);
        }

        long us = _t.end();
        sc_us.log(us);
    }
}

using namespace pyston;

std::unordered_set<BoxedClass*>         gc::GCBase::class_objects;
std::unordered_set<void*>               gc::GCBase::roots;
std::vector<std::pair<void*, void*>>    gc::GCBase::potential_root_ranges;
std::unordered_set<void*>               gc::GCBase::nonheap_roots;