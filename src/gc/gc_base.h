//
// Created by user on 7/25/15.
//

#ifndef PYSTON_GC_BASE_H
#define PYSTON_GC_BASE_H



#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_set>
#include <vector>
#include <deque>
#include <sys/mman.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "gc/default_heap.h"

namespace pyston {
    namespace gc {
#define TRACE_GC_MARKING 0
#if TRACE_GC_MARKING
        extern FILE* trace_fp;
        #define GC_TRACE_LOG(...) fprintf(pyston::gc::trace_fp, __VA_ARGS__)
        #else
#define GC_TRACE_LOG(...)
#endif


        // If you want to have a static root "location" where multiple values could be stored, use this:
        class GCRootHandle;

        class GCBase;

        enum TraceStackType {
            MarkPhase,
            FinalizationOrderingFindReachable,
            FinalizationOrderingRemoveTemporaries,
        };
        class TraceStack;

        static std::unordered_set<GCRootHandle*>* getRootHandles() {
            static std::unordered_set<GCRootHandle*> root_handles;
            return &root_handles;
        }
    }

    class gc::TraceStack {
    private:
        const int CHUNK_SIZE = 256;
        const int MAX_FREE_CHUNKS = 50;

        std::vector<void**> chunks;
        static std::vector<void**> free_chunks;

        void** cur;
        void** start;
        void** end;

        TraceStackType visit_type;

        void get_chunk();
        void release_chunk(void** chunk);
        void pop_chunk();

    public:
        TraceStack(TraceStackType type) : visit_type(type) { get_chunk(); }

        TraceStack(TraceStackType type, const std::unordered_set<void*>& root_handles);
        ~TraceStack();

        void push(void* p);
        void* pop_chunk_and_item();
        void* pop();
    };
    //std::vector<void**> TraceStack::free_chunks;

    class gc::GCRootHandle  {
    public:
        Box* value;

        GCRootHandle() { gc::getRootHandles()->insert(this); }
        ~GCRootHandle() { gc :: getRootHandles()->erase(this); };

        void operator=(Box* b) { value = b; }

        operator Box*() { return value; }
        Box* operator->() { return value; }
    };

    class gc::GCBase {
    public:
        virtual ~GCBase() {}
        void *gc_alloc(size_t bytes, GCKind kind_id) __attribute__((visibility("default")));
        void* gc_realloc(void* ptr, size_t bytes) __attribute__((visibility("default")));
        void gc_free(void* ptr) __attribute__((visibility("default")));


        virtual void runCollection() = 0;

        bool gcIsEnabled();
        void disableGC();
        void enableGC();

        bool should_not_reenter_gc;
        int ncollections;

        static std::unordered_set<BoxedClass*> class_objects;

        static std::unordered_set<void*> roots;
        static std::vector<std::pair<void*, void*>> potential_root_ranges;
        static std::unordered_set<void*> nonheap_roots;

        Heap* global_heap;

        std::deque<Box*> pending_finalization_list;
        std::deque<PyWeakReference*> weakrefs_needing_callback_list;

        std::list<Box*> objects_with_ordered_finalizers;

        void prepareWeakrefCallbacks(Box* box);

        void orderFinalizers();

        void finalizationOrderingFindReachable(Box* obj);

        void finalizationOrderingRemoveTemporaries(Box* obj);

        bool gc_enabled;

        __attribute__((always_inline)) void visitByGCKind(void* p, GCVisitor& visitor);

        void graphTraversalMarking(TraceStack& stack, GCVisitor& visitor);

        void markRoots(GCVisitor& visitor);
    };
}


#endif //PYSTON_GC_BASE_H
