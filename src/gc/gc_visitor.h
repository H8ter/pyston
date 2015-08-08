//
// Created by user on 8/8/15.
//

#ifndef PYSTON_GC_VISITOR_H
#define PYSTON_GC_VISITOR_H

#include <unordered_map>

namespace pyston {
namespace gc {
    class TraceStack;
    class Heap;
    class GCVisitor {
    private:
        bool isValid(void* p);

    public:
        TraceStack* stack;
        Heap* global_heap;
        GCVisitor(TraceStack* stack, Heap* global_heap) : stack(stack), global_heap(global_heap) {}

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
