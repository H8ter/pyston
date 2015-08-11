//
// Created by user on 8/10/15.
//

#ifndef PYSTON_BARTLETT_H
#define PYSTON_BARTLETT_H

#include "gc/gc_base.h"
#include "bartlett_heap.h"
#include <queue>

namespace pyston {
namespace gc {

    /*
        Mostly Copying Bartlett's Garbage Collector
    */
    class BartlettGC : public GCBase {
    public:
        BartlettGC();

        virtual ~BartlettGC();

        virtual void runCollection() override;

    private:

        void gc(GCVisitor &visitor, TraceStack &stack);

        void promote(BartlettHeap::Block* b);

        void copyChildren(LinearHeap::Obj* obj, GCVisitor &visitor, TraceStack &stack);

        void copy(LinearHeap::Obj* obj);

        void* move(LinearHeap::Obj* obj);

        void update(LinearHeap::Obj* obj);

        bool isValidPointer(void* p);

        std::queue<void*> tospace_queue;

        BartlettHeap* gh;

        // for testting
        int root_blocks;
        int move_cnt;
    };

}
}


#endif //PYSTON_BARTLETT_H
