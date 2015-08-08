//
// Created by user on 7/25/15.
//

#ifndef PYSTON_SEMISPACE_H
#define PYSTON_SEMISPACE_H



#include "gc/gc_base.h"
#include "gc/linear_heap.h"

namespace pyston {
namespace gc {

    class SemiSpaceGC : public GCBase {
    public:

        SemiSpaceGC();

        virtual ~SemiSpaceGC();

        virtual void runCollection() override;

    protected:

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

    class HybridSemiSpaceGC : public SemiSpaceGC {
    public:
        HybridSemiSpaceGC();


        virtual void runCollection() override;

    private:

    };
}
}

#endif //PYSTON_SEMISPACE_H
