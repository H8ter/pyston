//
// Created by user on 7/25/15.
//

#ifndef PYSTON_SEMISPACE_H
#define PYSTON_SEMISPACE_H



#include "gc/gc_base.h"
#include "semispace_heap.h"

namespace pyston {
namespace gc {
    class SemiSpaceGC : public GCBase {
    public:
        SemiSpaceGC();

        virtual ~SemiSpaceGC() {
            delete tospace;
            delete fromspace;
        }

        virtual void runCollection() override;

    private:

        void flip();

        SemiSpaceHeap::Obj* copy(SemiSpaceHeap::Obj* obj);

        SemiSpaceHeap* tospace;
        SemiSpaceHeap* fromspace;
    };
}
}





#endif //PYSTON_SEMISPACE_H
