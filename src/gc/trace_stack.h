//
// Created by user on 8/4/15.
//

#ifndef PYSTON_TRACE_STACK_H
#define PYSTON_TRACE_STACK_H

#include <vector>
#include <unordered_set>

namespace pyston {
namespace gc {
    enum TraceStackType {
        MarkPhase,
        FinalizationOrderingFindReachable,
        FinalizationOrderingRemoveTemporaries,
    };

    class TraceStack {
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
}
}


#endif //PYSTON_TRACE_STACK_H
