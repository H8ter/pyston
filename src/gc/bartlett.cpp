//
// Created by user on 8/10/15.
//

#include "bartlett.h"
#include "gc/bartlett_heap.h"
#include "gc/collector.h"
#include "gc/gc_visitor.h"
#include "gc/trace_stack.h"

namespace pyston {
namespace gc {

    BartlettGC::BartlettGC() {
        global_heap = new BartlettHeap(SMALL_ARENA_START, ARENA_SIZE);
    }

    BartlettGC::~BartlettGC() {
        delete global_heap;
    }

    void BartlettGC::runCollection() {
        GC_TRACE_LOG("GC begin\n");
        startGCUnexpectedRegion();

        gh = static_cast<BartlettHeap*>(global_heap);

        gh->prepareForCollection();
        block_used.assign(gh->blocks_cnt, false);
        gc();
        gh->cleanupAfterCollection();

        endGCUnexpectedRegion();
        GC_TRACE_LOG("GC end\n");
    }

    void BartlettGC::gc() {
        TraceStack stack(TraceStackType::MarkPhase, roots);
        GCVisitor visitor(&stack, gh);

        markRoots(visitor);

        while (void* root = stack.pop()) {
            promote(&gh->block(root));
        }

        // scan & copy
        while(!tospace_queue.empty()) {
            BartlettHeap::Block* b = tospace_queue.front();
            tospace_queue.pop();

            for(auto p : b->h->obj_set)
                copyChildren((LinearHeap::Obj *)p, visitor, stack);
        }

        // update references
        for(auto b : gh->blocks) {
            if (b.id != gh->nxt_space) continue;

            for (auto obj : b.h->obj_set)
                update((LinearHeap::Obj *)obj);
        }
    }

    void BartlettGC::copyChildren(LinearHeap::Obj *obj, GCVisitor &visitor, TraceStack &stack) {
        GCAllocation* al = obj->data;
        void* data = al->user_data;

        visitByGCKind(data, visitor);

        while(void* child = stack.pop()) {
            copy(LinearHeap::Obj::fromUserData(child));
        }
    }

    void BartlettGC::copy(LinearHeap::Obj* obj) {
        if (gh->block(obj).id == gh->nxt_space || obj->forward)
            return;

        obj->forward = move(obj);
    }

    void* BartlettGC::move(LinearHeap::Obj *obj) {
        // выполнить перемещение объекта и обновить tospace_queue
//        int b_index = BartlettHeap::block_index(obj);
//        if (!block_used[b_index]) {
//            tospace_queue.push(&BartlettHeap::blocks[b_index]);
//            block_used[b_index] = true;
//        }
//
        LinearHeap::Obj* addr = LinearHeap::Obj::fromAllocation(
                static_cast<BartlettHeap*>(global_heap)->_alloc(obj->size, gh->nxt_space)
        );

        memcpy(addr, obj, obj->size + sizeof(LinearHeap::Obj) + sizeof(GCAllocation));

        return addr->data->user_data;
    }

    void BartlettGC::promote(BartlettHeap::Block* b) {
        if (b->id == gh->cur_space) {
            b->id = gh->nxt_space;
            tospace_queue.push(b);
        }
    }

    void BartlettGC::update(LinearHeap::Obj* obj) {
        void* data = obj->data->user_data;
        size_t size = obj->size;

        void** start = (void**)data;
        void** end   = (void**)((char*)data + size);

        while(start <= end) {
            void* ptr = *start;
            if (isValidPointer(ptr)) {
                GCAllocation* al =global_heap->getAllocationFromInteriorPointer(ptr);
                size_t diff = (size_t)((void**)ptr - (void**)al->user_data);

                void** f_addr = (void**)((char*)LinearHeap::Obj::fromAllocation(al)->forward + diff);

                if (f_addr) *start = *f_addr;
            }
            ++start;
        }
    }

    bool BartlettGC::isValidPointer(void *p) {
        if ((uintptr_t)p < SMALL_ARENA_START || (uintptr_t)p >= HUGE_ARENA_START + ARENA_SIZE)
            return false;

        ASSERT(global_heap->getAllocationFromInteriorPointer(p)->user_data == p, "%p", p);
        return true;
    }
}
}

