//
// Created by user on 7/25/15.
//

#ifndef PYSTON_SEMISPACE_H
#define PYSTON_SEMISPACE_H



#include "gc/gc_base.h"
#include "semispace_heap.h"

namespace pyston {
namespace gc {

#if TRACE_GC_MARKING
//        extern FILE* trace_fp;
#endif

    class SemiSpaceGC : public GCBase {
    public:
        SemiSpaceGC();

        virtual ~SemiSpaceGC();

        virtual void runCollection() override;

//        virtual void *gc_alloc_root(size_t bytes, GCKind kind_id) override;

    private:

        void flip(GCVisitor &visitor, TraceStack &stack);

        void collectRoots(GCVisitor &visitor, TraceStack &stack);

        void scanCopy(LinearHeap* tospace, GCVisitor &visitor, TraceStack &stack);

        void copyChildren(GCVisitor &visitor, TraceStack &stack, void *data);

        void* copy(LinearHeap::Obj* obj);

        void* moveObj(LinearHeap::Obj *obj);

        void doRemap(GCVisitor &visitor);

        // for testing
        void showObjectFromData(FILE *f, void *data);
    };


}
}





#endif //PYSTON_SEMISPACE_H
