//
// Created by user on 7/25/15.
//

#ifndef PYSTON_MARKSWEEP_H
#define PYSTON_MARKSWEEP_H


#include "gc/gc_base.h"


#ifndef NVALGRIND
#include "valgrind.h"
#endif

namespace pyston{
namespace gc{
    class MarkSweepGC : public GCBase {
    public:
        MarkSweepGC();
        virtual ~MarkSweepGC() {
            delete global_heap;
        }

        virtual void runCollection() override;

    private:

        void markPhase();

        void sweepPhase(std::vector<Box*>& weakly_referenced);
    };
}
}


#endif //PYSTON_MARKSWEEP_H
