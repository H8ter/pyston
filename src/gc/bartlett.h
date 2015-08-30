//
// Created by user on 8/10/15.
//

#ifndef PYSTON_BARTLETT_H
#define PYSTON_BARTLETT_H

#include "gc/gc_base.h"
#include "bartlett_heap.h"
#include <queue>
#include <map>

namespace pyston {
namespace gc {

//    class BartlettHeap;
    /*
        Mostly Copying Bartlett's Garbage Collector
    */
    class BartlettGC : public GCBase {
    public:
        BartlettGC();

        virtual ~BartlettGC();

        virtual void runCollection() override;

        BartlettHeap* gh;

    private:

        void gc(GCVisitor &visitor, TraceStack &stack);

        void findAndPromoteRoots(GCVisitor &visitor, TraceStack &stack);

        void scanAndCopy(GCVisitor &visitor, TraceStack &stack);

        void promote(BartlettHeap::Block* b);

        void copyChildren(LinearHeap::Obj* obj, GCVisitor &visitor, TraceStack &stack);

        void copy(LinearHeap::Obj* obj);

        void* move(LinearHeap::Obj* obj);

        void update();

        void updateReferences(LinearHeap::Obj* obj);

        std::queue<void*> tospace_queue;

        // for testting
        int root_blocks;
        int move_cnt;

    };

}
}


#endif //PYSTON_BARTLETT_H
