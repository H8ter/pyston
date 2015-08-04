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

namespace pyston {
    namespace gc {

        bool GCBase::gcIsEnabled() {
            return gc_enabled;
        }

        void GCBase::disableGC() {
            gc_enabled = false;
        }

        void GCBase::enableGC() {
            gc_enabled = true;
        }

        std::vector<void**> TraceStack::free_chunks;


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
//                    cls->gc_visit(&visitor, b);
                }
            } else if (kind_id == GCKind::HIDDEN_CLASS) {
                HiddenClass* hcls = reinterpret_cast<HiddenClass*>(p);
//                hcls->gc_visit(&visitor);
            } else {
                RELEASE_ASSERT(0, "Unhandled kind: %d", (int)kind_id);
            }
        }

        void gc::GCBase::graphTraversalMarking(gc::TraceStack &stack, GCVisitor &visitor) {
            static StatCounter sc_us("us_glsec_mark_phase_graph_traversal");
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
            visitor.allow_remap = true;
            GC_TRACE_LOG("Looking at the stack\n");
            threading::visitAllStacks(&visitor);


            GC_TRACE_LOG("Looking at root handles\n");
            for (auto h : *getRootHandles()) {          //always empty?
                visitor.visit(h->value);
            }

            GC_TRACE_LOG("Looking at potential root ranges\n");
            for (auto& e : potential_root_ranges) {
                visitor.visitPotentialRange((void* const*)e.first, (void* const*)e.second);
            }

//            GC_TRACE_LOG("Looking at pending finalization list\n");
//            for (auto box : pending_finalization_list) {
//                visitor.visit(box);
//            }
//
//            GC_TRACE_LOG("Looking at weakrefs needing callbacks list\n");
//            for (auto weakref : weakrefs_needing_callback_list) {
//                visitor.visit(weakref);
//            }


            visitor.allow_remap = false;
        }

        //--------------------TRACESTACK---------------------

        void TraceStack::get_chunk() {
            if (free_chunks.size()) {
                start = free_chunks.back();
                free_chunks.pop_back();
            } else {
                start = (void**)malloc(sizeof(void*) * CHUNK_SIZE);
            }

            cur = start;
            end = start + CHUNK_SIZE;
        }

        void TraceStack::release_chunk(void **chunk) {
            if (free_chunks.size() == MAX_FREE_CHUNKS)
                free(chunk);
            else
                free_chunks.push_back(chunk);
        }

        void TraceStack::pop_chunk() {
            start = chunks.back();
            chunks.pop_back();
            end = start + CHUNK_SIZE;
            cur = end;
        }

        void TraceStack::push(void *p) {
            GC_TRACE_LOG("Pushing %p\n", p);
            GCAllocation* al = GCAllocation::fromUserData(p);

            switch (visit_type) {
                case TraceStackType::MarkPhase:
// Use this to print the directed edges of the GC graph traversal.
// i.e. print every a -> b where a is a pointer and b is something a references
#if 0
                if (previous_pop) {
                    GCAllocation* source_allocation = GCAllocation::fromUserData(previous_pop);
                    if (source_allocation->kind_id == GCKind::PYTHON) {
                        printf("(%s) ", ((Box*)previous_pop)->cls->tp_name);
                    }
                    printf("%p > %p", previous_pop, al->user_data);
                } else {
                    printf("source %p", al->user_data);
                }

                if (al->kind_id == GCKind::PYTHON) {
                    printf(" (%s)", ((Box*)al->user_data)->cls->tp_name);
                }
                printf("\n");

#endif

                    if (isMarked(al)) {
                        return;
                    } else {
                        setMark(al);
                    }
                    break;
                    // See PyPy's finalization ordering algorithm:
                    // http://pypy.readthedocs.org/en/latest/discussion/finalizer-order.html
                case TraceStackType::FinalizationOrderingFindReachable:
                    if (orderingState(al) == FinalizationState::UNREACHABLE) {
                        setOrderingState(al, FinalizationState::TEMPORARY);
                    } else if (orderingState(al) == FinalizationState::REACHABLE_FROM_FINALIZER) {
                        setOrderingState(al, FinalizationState::ALIVE);
                    } else {
                        return;
                    }
                    break;
                case TraceStackType::FinalizationOrderingRemoveTemporaries:
                    if (orderingState(al) == FinalizationState::TEMPORARY) {
                        setOrderingState(al, FinalizationState::REACHABLE_FROM_FINALIZER);
                    } else {
                        return;
                    }
                    break;
                default:
                    assert(false);
            }

            *cur++ = p;
            if (cur == end) {
                chunks.push_back(start);
                get_chunk();
            }
        }

        void *TraceStack::pop_chunk_and_item() {
            release_chunk(start);
            if (chunks.size()) {
                pop_chunk();
                assert(cur == end);
                return *--cur; // no need for any bounds checks here since we're guaranteed we're CHUNK_SIZE from the start
            } else {
                // We emptied the stack, but we should prepare a new chunk in case another item
                // gets added onto the stack.
                get_chunk();
                return NULL;
            }
        }

        void *TraceStack::pop() {
            if (cur > start)
                return *--cur;

            return pop_chunk_and_item();
        }

        TraceStack::TraceStack(TraceStackType type, const std::unordered_set<void*>& root_handles) : visit_type(type) {
            get_chunk();
            for (void* p : root_handles) {
                assert(!isMarked(GCAllocation::fromUserData(p)));
                push(p);
            }
        }

        TraceStack::~TraceStack() {
            RELEASE_ASSERT(end - cur == CHUNK_SIZE, "destroying non-empty TraceStack");

            // We always have a block available in case we want to push items onto the TraceStack,
            // but that chunk needs to be released after use to avoid a memory leak.
            release_chunk(start);
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