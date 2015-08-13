//
// Created by user on 8/8/15.
//

#include "trace_stack.h"

#include "codegen/ast_interpreter.h"
#include "gc/heap.h"
#include "runtime/types.h"

namespace pyston {
namespace gc {

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

}
}