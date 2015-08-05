//
// Created by user on 8/4/15.
//

#ifndef PYSTON_GC_VISITOR_H
#define PYSTON_GC_VISITOR_H

#include <unordered_map>

namespace pyston {
namespace gc {

#define TRACE_GC_MARKING 1
#if TRACE_GC_MARKING
        extern FILE* trace_fp;
#define GC_TRACE_LOG(...) fprintf(pyston::gc::trace_fp, __VA_ARGS__)
#else
#define GC_TRACE_LOG(...)
#endif

    class TraceStack;
    class Heap;
    class GCVisitor {
    private:
        bool isValid(void* p);

    public:

        TraceStack* stack;
        Heap* global_heap;
        GCVisitor(TraceStack* stack, Heap* global_heap) :
                 stack(stack), global_heap(global_heap) {}

        // These all work on *user* pointers, ie pointers to the user_data section of GCAllocations
        void visitIf(void* p) {
            if (p)
                visit(p);
        }
        void visit(void* p);
        void visitRange(void* const* start, void* const* end);
        void visitPotential(void* p);
        void visitPotentialRange(void* const* start, void* const* end);
    };
}
}


#endif //PYSTON_GC_VISITOR_H
