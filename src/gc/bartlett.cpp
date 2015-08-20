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
#if TRACE_GC_MARKING
        Stats::setEnabled(true);
#endif
        global_heap = new BartlettHeap(SMALL_ARENA_START, ARENA_SIZE);
    }

    BartlettGC::~BartlettGC() {
        delete global_heap;
    }

    void BartlettGC::runCollection() {
        GC_TRACE_LOG("GC begin\n");
#if TRACE_GC_MARKING
        fprintf(stderr, "GC begin\n");
#endif
        static StatCounter sc_us("us_gc_collections");
        static StatCounter sc("gc_collections");
        sc.log();

        UNAVOIDABLE_STAT_TIMER(t0, "us_timer_gc_collection");

        ncollections++;

        startGCUnexpectedRegion();
        Timer _t("collecting", /*min_usec=*/0); // 10000

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

        long us = _t.end();
        sc_us.log(us);

        // dumpHeapStatistics();
#if TRACE_GC_MARKING
        Stats::dump(false);
        Stats::clear();
#endif

#if TRACE_GC_MARKING
        fprintf(stderr, "GC end\n");
#endif
        GC_TRACE_LOG("GC end\n");

#if TRACE_GC_MARKING
//        FILE* diff_fp = fopen("diff_bt_1.txt", "a");
//        for(auto pr : gh->diff_map)
//            fprintf(diff_fp, "%" PRId64 " %" PRId64 " | ", pr.first, pr.second);
//        if (gh->diff_map.size()) fprintf(diff_fp, "\n");
//        gh->diff_map.clear();
//        fclose(diff_fp);
#endif
    }

    void BartlettGC::gc(GCVisitor &visitor, TraceStack &stack) {
        findAndPromoteRoots(visitor, stack);

        scanAndCopy(visitor, stack);

        update();
    }

    void BartlettGC::findAndPromoteRoots(GCVisitor &visitor, TraceStack &stack) {
        markRoots(visitor);

#if TRACE_GC_MARKING
        int active_blocks = 0;
        int free_blocks = 0;
        for(int i = 0; i < gh->blocks.size(); i++) {
            active_blocks += gh->blocks[i].id == gh->cur_space;
            free_blocks += gh->isFree(gh->blocks[i].id);
        }
//        active_blocks = gh->active_blocks.size();
//        free_blocks = gh->free_blocks.size();
        fprintf(stderr, "active %d | free %d\n", active_blocks, free_blocks);
        RELEASE_ASSERT(active_blocks + free_blocks == gh->blocks.size(), "smth went wrong\n");
#endif

        root_blocks = 0;
        move_cnt = 0;
        while (void* root = stack.pop()) {
            RELEASE_ASSERT(gh->block(root).id == gh->cur_space || gh->block(root).id == gh->nxt_space, "%p\n", root);

            promote(&gh->block(root));
            tospace_queue.push(root);
        }
#if TRACE_GC_MARKING
        fprintf(stderr, "root blocks count %d\n", root_blocks);
#endif
    }

    void BartlettGC::scanAndCopy(GCVisitor &visitor, TraceStack &stack) {
        // scan & copy
        static StatCounter sc_us("us_gc_copy_phase");
        static StatCounter sc_marked_objs("gc_marked_object_count");
        Timer _t("copyPhase", /*min_usec=*/0); // 10000

        while(!tospace_queue.empty()) {
            sc_marked_objs.log();

            void* p = tospace_queue.front();
            tospace_queue.pop();

            RELEASE_ASSERT(*((void**)((char*)p - 32)) == (void*)0xCAFEBABE, "%p", p); // !!!
            RELEASE_ASSERT(gh->block(p).id == gh->nxt_space, "%p\n", p);

            copyChildren(LinearHeap::Obj::fromUserData(p), visitor, stack);
        }
        long us = _t.end();
        sc_us.log(us);

#if TRACE_GC_MARKING
        fprintf(stderr, "move count %d\n", move_cnt);
#endif
    }

    void BartlettGC::copyChildren(LinearHeap::Obj *obj, GCVisitor &visitor, TraceStack &stack) {
        GCAllocation* al = obj->data;

        if (!isMarked(al) || al->kind_id == GCKind::UNTRACKED) return; // !!!

        void* data = al->user_data;

        GC_TRACE_LOG("copy children %p\n", data);
        visitByGCKind(data, visitor);

        while(void* child = stack.pop()) {

//            child = gh->getAllocationFromInteriorPointer(child)->user_data; // !!!

            int b_id = gh->block(child).id;

            if(!(b_id == gh->cur_space || b_id == gh->nxt_space)) {
#if TRACE_GC_MARKING
                fprintf(stderr, "b_id %d cur %d child %p\n", b_id, gh->cur_space, child);
                showObjectFromData(stderr, data);
                fprintf(stderr, "forward %p\n", LinearHeap::Obj::fromUserData(child)->forward);
#endif
                RELEASE_ASSERT(false, "bad ref\n");
            }
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
#if TRACE_GC_MARKING
//        fprintf(stderr, "move from %p to %p\n", (void*)obj->data->user_data, forward);
#endif
        move_cnt++;

        return forward;
    }

    void BartlettGC::promote(BartlettHeap::Block* b) {
        if (b->id == gh->cur_space) {
            b->id = gh->nxt_space;

            root_blocks++;
        }
    }

    void BartlettGC::update() {
        static StatCounter sc_us("us_gc_update_phase");
        Timer _t("updatePhase", /*min_usec=*/0); // 10000

        for(auto b : gh->blocks) {
            if (b.id == gh->nxt_space) {
                for (auto obj : b.h->obj_set)
                    updateReferences((LinearHeap::Obj *) obj);
            }
        }

        long us = _t.end();
        sc_us.log(us);
    }

    void BartlettGC::updateReferences(LinearHeap::Obj* obj) {
        if (!isMarked(obj->data)) return;                           // no need to update unreachable object

//        if (obj->data->kind_id == GCKind::UNTRACKED) return;        // !!!

//        GC_TRACE_LOG("update %p\n", obj);
        void* data = obj->data->user_data;
        size_t size = obj->size;

        void** start = (void**)data;
        void** end   = (void**)((char*)data + size);

        RELEASE_ASSERT(*((void**)((char*)data - 32)) == (void*)0xCAFEBABE, "%p", data);

        while(start <= end) {
            void* ptr = *start;
            if (gh->validPointer(ptr)) {
                GCAllocation* al = gh->getAllocationFromInteriorPointer(ptr);

                if (al && LinearHeap::Obj::fromAllocation(al)->forward) {
                    int64_t diff = DIFF(ptr, al->user_data);
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

