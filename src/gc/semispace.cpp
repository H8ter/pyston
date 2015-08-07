//
// Created by user on 7/25/15.
//

#include <unistd.h>
#include "semispace.h"
#include "gc/collector.h"
#include "runtime/types.h"
#include "runtime/objmodel.h"
#include "gc/addr_remap.h"
#include "gc/gc_visitor.h"

namespace pyston {
namespace gc{

//#if TRACE_GC_MARKING
//        FILE* trace_fp = fopen("gc_trace.txt", "w");
//#endif

// SemiSpace GC algorithm's pseudocode
//    flip():
//          swap(fromspace, tospace)
//          top_of_to_space = tospace + size(tospace)
//          scan = free = tospace
//
//          for R in Roots
//              R = copy(R)
//
//          while scan < free
//              for P in Children(scan)
//              *P = copy(*P)
//              scan = scan + size(scan)
//
//    copy(P):
//          if forwarded(P)
//              return forwarding_address(P)
//          else
//              addr = free
//              move(P, free)
//              free = free + size(P)
//              forwarding_address(P) = addr
//              return addr

        void SemiSpaceGC::showObjectFromData(FILE *f, void *data) {
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

        SemiSpaceGC::SemiSpaceGC() {
            global_heap = new SemiSpaceHeap();

            gc_enabled = true;
            should_not_reenter_gc = false;
            ncollections = 0;
        }

        SemiSpaceGC::~SemiSpaceGC()  {
            delete global_heap;
        }

        void SemiSpaceGC::runCollection() {
            RELEASE_ASSERT(!should_not_reenter_gc, "");

            should_not_reenter_gc = true; // begin non-reentrant section

            ncollections++;

            global_heap->prepareForCollection();

            //invalidateOrderedFinalizerList();

            TraceStack stack(TraceStackType::MarkPhase, roots);
            GCVisitor visitor(&stack, global_heap);
            flip(visitor, stack);

//            std::vector<Box*> weakly_referenced;
//            global_heap->freeUnmarked(weakly_referenced); // sweepPhase
//
//            for (auto o : weakly_referenced) {
//                assert(isValidGCObject(o));
//                prepareWeakrefCallbacks(o);
//                global_heap->free(GCAllocation::fromUserData(o));
//            }

            should_not_reenter_gc = false; // end non-reentrant section

            doRemap(visitor);
            global_heap->cleanupAfterCollection();
        }

        void SemiSpaceGC::flip(GCVisitor &visitor, TraceStack &stack) {
            AddrRemap::enable();
            markRoots(visitor);
            AddrRemap::disable();

            // 1st step: copying roots from f.space to t.space
            collectRoots(visitor, stack);

            // 2nd step: scanning & copying objects
            GC_TRACE_LOG("Scan & Copy RootSpace\n");
            scanCopy(static_cast<SemiSpaceHeap *>(global_heap)->rootspace, visitor, stack);
            GC_TRACE_LOG("Scan & Copy ToSpace\n");
            scanCopy(static_cast<SemiSpaceHeap *>(global_heap)->tospace,   visitor, stack);

            // weird stuff
//            std::vector<BoxedClass*> classes_to_remove;
//            for (BoxedClass* cls : class_objects) {
//                GCAllocation* al = GCAllocation::fromUserData(cls);
//                if (!isMarked(al)) {
//                    visitor.visit(cls);
//                    classes_to_remove.push_back(cls);
//                }
//            }
//
//            collectRoots(visitor, stack);
//            scanCopy(visitor, stack);
//
//            for (BoxedClass* cls : classes_to_remove) {
//                class_objects.erase(cls);
//            }
//
//            for (BoxedClass* cls : classes_to_remove) {
//                class_objects.insert(cls->cls);
//            }
//
//            // 3rd step:
//            orderFinalizers();
        }

        void SemiSpaceGC::collectRoots(GCVisitor &visitor, TraceStack &stack) {
            while (void* p = stack.pop()) {
                GCAllocation* al = GCAllocation::fromUserData(p);

                assert(isMarked(al));

                LinearHeap::Obj *root = LinearHeap::Obj::fromAllocation(al);
                // copy object from fspace to tspace only if it's not in rootspace
                if (!static_cast<SemiSpaceHeap *>(global_heap)->rootspace->arena->contains(p))
                    copy(root);
            }
        }

        void SemiSpaceGC::scanCopy(LinearHeap* tospace, GCVisitor &visitor, TraceStack &stack) {
            auto scan = tospace->obj_set.begin();

            while (scan != tospace->obj_set.end()) {
                GCAllocation* al = reinterpret_cast<LinearHeap::Obj*>(*scan)->data;
                void* data = al->user_data;

                visitByGCKind(data, visitor);

                // now trace stack contain children of current object
                copyChildren(visitor, stack, data);

                ++scan;
            }
        }

        void SemiSpaceGC::copyChildren(GCVisitor &visitor, TraceStack &stack, void *parent) {
            auto rootspace = static_cast<SemiSpaceHeap *>(global_heap)->rootspace;

            while(void* P = stack.pop()) {
                if(rootspace->arena->contains(P)) continue;

                GCAllocation* al = GCAllocation::fromUserData(P);
                LinearHeap::Obj* child = LinearHeap::Obj::fromAllocation(al);

//                fprintf(f, " >>> [#1] %p | %p | %p\n", P, al, obj);

                void** addr = reinterpret_cast<void**>(parent);

//                fprintf(f, " >>> [#1] %p | %p | %p\n", P, al, obj);

                void* f_addr = copy(child);
                // refresh current object

                if (al->kind_id == GCKind::CONSERVATIVE || al->kind_id == GCKind::CONSERVATIVE_PYTHON ||
                        al->kind_id == GCKind::PRECISE)
                {

                    GC_TRACE_LOG("%p %p %p---------------------------------------\n", parent, P, f_addr);

                    while (*addr != P)
                        ++addr;

                    ASSERT(*addr == P, "Panic!");

                    *addr = f_addr;
                }
            }

        }

        void* SemiSpaceGC::copy(LinearHeap::Obj *obj) {
            if (obj == NULL) return NULL;

            if (obj->forward)
                return obj->forward;
            else {
                void* addr = moveObj(obj);
                obj->forward = addr;

                return addr;
            }
        }

        void* SemiSpaceGC::moveObj(LinearHeap::Obj *obj) {
            ASSERT(!static_cast<SemiSpaceHeap *>(global_heap)->rootspace->arena->contains(obj), "trying to move root object\n");

            auto addr = LinearHeap::Obj::fromAllocation(global_heap->alloc(obj->size));

            memcpy(addr, obj, obj->size + sizeof(LinearHeap::Obj) + sizeof(GCAllocation));

            return addr->data->user_data;
        }

        void SemiSpaceGC::doRemap(GCVisitor &visitor) {
            GC_TRACE_LOG("Remap\n");
            int cnt = 0;

            auto fromspace = static_cast<SemiSpaceHeap *>(global_heap)->fromspace;

            std::unordered_map<void**, void*>::iterator it = AddrRemap::remap.begin();
            for(; it != AddrRemap::remap.end(); ++it) {
                if (fromspace->arena->contains(it->second)) {
                    auto old_obj = LinearHeap::Obj::fromUserData(it->second);
                    if (old_obj->magic == 0xCAFEBABE) {
                        void *f_addr = old_obj->forward;
                        *it->first = f_addr;

                        cnt++;
                    }
                }
            }

            GC_TRACE_LOG("doRemap 1 %d\n", cnt);

//            cnt = 0;
//            std::unordered_set<void**>::iterator ref_root_it = ref_to_roots.begin();
//            for(; ref_root_it != ref_to_roots.end(); ++ref_root_it) {
//                void** ref = *ref_root_it;
//                if (fromspace->arena->contains(*ref)) {
//                    void* f_addr = LinearHeap::Obj::fromUserData(*ref)->forward;
//                    *ref = f_addr;
//                    cnt++;
//                }
//            }
//
//            // AddrRemap::remap.clear();
//            GC_TRACE_LOG("doRemap 2 %d\n", cnt);
        }

        bool AddrRemap::enabled;
        std::unordered_map<void**, void*> AddrRemap::remap;

//        void* SemiSpaceGC::gc_alloc_root(size_t bytes, GCKind kind_id) {
//            auto gh = static_cast<SemiSpaceHeap *>(global_heap);
//            gh->alloc_root = true;
//
//            void *p = this->gc_alloc(bytes, kind_id);
//
//            gh->alloc_root = false;
//
//            return p;
//        }
    } // namespace gc
} // namespace pyston