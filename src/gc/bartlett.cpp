//
// Created by user on 8/10/15.
//

#include "bartlett.h"
#include "gc/bartlett_heap.h"
#include "gc/collector.h"
#include "gc/gc_visitor.h"
#include "gc/trace_stack.h"
#include "runtime/types.h"

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
        TRACE("GC begin\n");
        startGCUnexpectedRegion();

        gh = static_cast<BartlettHeap*>(global_heap);

        gh->prepareForCollection();
        //-------------------------------------------b
        invalidateOrderedFinalizerList();
        //-------------------------------------------e

        TraceStack stack(TraceStackType::MarkPhase, roots);
        GCVisitor visitor(&stack, gh);
        gc(visitor, stack);
        //-------------------------------------------b
#if 0
        std::vector<BoxedClass*> classes_to_remove;
        for (BoxedClass* cls : class_objects) {
            GCAllocation* al = GCAllocation::fromUserData(cls);
            if (!isMarked(al)) {
                visitor.visit(cls);
                classes_to_remove.push_back(cls);
            }
        }

        graphTraversalMarking(stack, visitor);

        for (BoxedClass* cls : classes_to_remove) {
            class_objects.erase(cls);
        }
        for (BoxedClass* cls : classes_to_remove) {
            class_objects.insert(cls->cls);
        }
#endif
        orderFinalizers();

        std::vector<Box*> weakly_referenced;
        gh->freeUnmarked(weakly_referenced); // sweepPhase

        for (auto o : weakly_referenced) {
            assert(isValidGCObject(o));
            prepareWeakrefCallbacks(o);
            gh->free(GCAllocation::fromUserData(o));
        }
        //-------------------------------------------e
        gh->cleanupAfterCollection();

        endGCUnexpectedRegion();
        TRACE("GC end\n");
        GC_TRACE_LOG("GC end\n");
    }

    void BartlettGC::gc(GCVisitor &visitor, TraceStack &stack) {
        markRoots(visitor);

#if TRACE_GC_MARKING
        int active_blocks = 0;
        int free_blocks = 0;
        for(int i = 0; i < gh->blocks_cnt; i++) {
            active_blocks += gh->blocks[i].id == gh->cur_space;
            free_blocks += gh->is_free(gh->blocks[i].id );
        }
        fprintf(stderr, "active %d | free %d\n", active_blocks, free_blocks);
#endif

        root_blocks = 0;
        move_cnt = 0;
        while (void* root = stack.pop()) {
#if TRACE_GC_MARKING
            RELEASE_ASSERT(gh->block(root).id == gh->cur_space || gh->block(root).id == gh->nxt_space, "%p\n", root);
#endif
            promote(&gh->block(root));
            tospace_queue.push(root);
        }

        // scan & copy
#if TRACE_GC_MARKING
        fprintf(stderr, "root blocks count %d\n", root_blocks);
#endif
        while(!tospace_queue.empty()) {
            void* p = tospace_queue.front();
            tospace_queue.pop();

#if TRACE_GC_MARKING
            RELEASE_ASSERT(gh->block(p).id == gh->nxt_space, "%p\n", p);
#endif

            copyChildren(LinearHeap::Obj::fromUserData(p), visitor, stack);
        }
#if TRACE_GC_MARKING
        fprintf(stderr, "move count %d\n", move_cnt);
#endif
        // update references
        for(auto b : gh->blocks) {
            if (b.id == gh->nxt_space) {
                for (auto obj : b.h->obj_set)
                    update((LinearHeap::Obj *) obj);
            }
        }
    }

    void BartlettGC::copyChildren(LinearHeap::Obj *obj, GCVisitor &visitor, TraceStack &stack) {
        GCAllocation* al = obj->data;

        if (!isMarked(al)) return;

        void* data = al->user_data;

        GC_TRACE_LOG("copy children %p\n", data);
        visitByGCKind(data, visitor);

        while(void* child = stack.pop()) {
#if TRACE_GC_MARKING
            int b_id = gh->block(child).id;

            if(!(b_id == gh->cur_space || b_id == gh->nxt_space)) {
                fprintf(stderr, "b_id %d cur %d child %p\n", b_id, gh->cur_space, child);
                showObjectFromData(stderr, data);
                fprintf(stderr, "forward %p\n", LinearHeap::Obj::fromUserData(child)->forward);
                RELEASE_ASSERT(false, "bad ref\n");
            }
#endif
            copy(LinearHeap::Obj::fromUserData(child));
        }
    }

    void BartlettGC::copy(LinearHeap::Obj* obj) {
        if (gh->block(obj).id == gh->nxt_space) {
            tospace_queue.push(obj->data->user_data);
            return;
        }

        if (!obj->forward) {
            obj->forward = move(obj);
        }

        tospace_queue.push(obj->forward);

    }

    void* BartlettGC::move(LinearHeap::Obj *obj) {
        LinearHeap::Obj* addr = LinearHeap::Obj::fromAllocation(
                gh->_alloc(obj->size, BartlettHeap::HEAP_SPACE::NXT_SPACE)
        );

        memcpy(addr, obj, obj->size + sizeof(LinearHeap::Obj) + sizeof(GCAllocation));

        void* forward = addr->data->user_data;

//        fprintf(stderr, "move from %p to %p\n", (void*)obj->data->user_data, forward);
        move_cnt++;

        return forward;
    }

    void BartlettGC::promote(BartlettHeap::Block* b) {
        if (b->id == gh->cur_space) {
            b->id = gh->nxt_space;

            root_blocks++;
        }
    }

    void BartlettGC::update(LinearHeap::Obj* obj) {
//        GC_TRACE_LOG("update %p\n", obj);
        void* data = obj->data->user_data;
        size_t size = obj->size;

        void** start = (void**)data;
        void** end   = (void**)((char*)data + size);

        while(start <= end) {
            void* ptr = *start;
            if (isValidPointer(ptr)) {
                GCAllocation* al =global_heap->getAllocationFromInteriorPointer(ptr);

                if (al && LinearHeap::Obj::fromAllocation(al)->forward) {
                    size_t diff = (size_t)((void**)ptr - (void**)al->user_data);
//                    size_t diff = 0;
                    void** f_addr = (void**)((char*)LinearHeap::Obj::fromAllocation(al)->forward + diff);
                    *start = f_addr;

//#if TRACE_GC_MARKING
//    fprintf(stderr, "update child %p forward %p parent %p\n", (void*)al->user_data, f_addr, (void*)obj->data->user_data);
//#endif
                }
            }
            ++start;
        }
    }

    bool BartlettGC::isValidPointer(void *p) {
        if ((uintptr_t)p < SMALL_ARENA_START || (uintptr_t)p >= HUGE_ARENA_START + ARENA_SIZE)
            return false;

        //ASSERT(global_heap->getAllocationFromInteriorPointer(p)->user_data == p, "%p", p);
        return true;
    }

    void BartlettGC::showObjectFromData(FILE *f, void *data) {
        fprintf(f, "%p\n", data);

        GCAllocation* al = GCAllocation::fromUserData(data);
        LinearHeap::Obj* obj = LinearHeap::Obj::fromAllocation(al);

        uint64_t bytes = obj->size + sizeof(LinearHeap::Obj)+ sizeof(GCAllocation);
        void** start = (void**)obj;
        void** end = (void**)((char*)obj + bytes);

        while(start < end) {
            fprintf(f, "%p ", *start);
            ++start;
        }
        fprintf(f, "\n");
    }
}
}

