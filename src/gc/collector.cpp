// Copyright (c) 2014-2015 Dropbox, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gc/collector.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

//#include "codegen/ast_interpreter.h"
#include "codegen/codegen.h"
#include "core/common.h"
#include "core/threading.h"
#include "core/types.h"
#include "core/util.h"
#include "gc/marksweep.h"
#include "runtime/objmodel.h"
#include "runtime/types.h"

#include "gc/semispace.h"

#ifndef NVALGRIND
#include "valgrind.h"
#endif

namespace pyston {
namespace gc {
    MarkSweepGC GC;
//    SemiSpaceGC GC;

#if TRACE_GC_MARKING
FILE* trace_fp;
#endif

        std::deque<Box*>& pending_finalization_list() {
            return GC.pending_finalization_list;
        }

        std::deque<PyWeakReference*>& weakrefs_needing_callback_list() {
            return GC.weakrefs_needing_callback_list;
        }

// Track the highest-addressed nonheap root; the assumption is that the nonheap roots will
// typically all have lower addresses than the heap roots, so this can serve as a cheap
// way to verify it's not a nonheap root (the full check requires a hashtable lookup).
//static void* max_nonheap_root = 0;
//static void* min_nonheap_root = (void*)~0;

void registerReferenceToPermanentRoot(void** ref_to_root) {
//    GC_TRACE_LOG("register reference from %p to root %p\n", ref_to_root, *ref_to_root);
//    fprintf(stderr, "register reference from %p to root %p\n", ref_to_root, *ref_to_root);
//    assert(GC.global_heap->getAllocationFromInteriorPointer(*ref_to_root));
//
//    GC.ref_to_roots.insert(ref_to_root);
}

void registerPermanentRoot(void* obj, bool allow_duplicates) {
    assert(GC.global_heap->getAllocationFromInteriorPointer(obj));

    // Check for double-registers.  Wouldn't cause any problems, but we probably shouldn't be doing them.
    if (!allow_duplicates)
        ASSERT(GC.roots.count(obj) == 0, "Please only register roots once");

//    GC_TRACE_LOG("register permanent root %p\n", obj); // 0x337
    // for now root must be in HugeArena's address space
//    RELEASE_ASSERT((uintptr_t)obj >= HUGE_ARENA_START && (uintptr_t)obj < HUGE_ARENA_START + ARENA_SIZE, "%p\n", obj);

    GC.roots.insert(obj);
}

void deregisterPermanentRoot(void* obj) {
    assert(GC.global_heap->getAllocationFromInteriorPointer(obj));
    ASSERT(GC.roots.count(obj), "");
//    GC_TRACE_LOG("del permanent root");
    GC.roots.erase(obj);
}

void registerPotentialRootRange(void* start, void* end) {
    GC.potential_root_ranges.push_back(std::make_pair(start, end));
}

// 75 occurrences
extern "C" PyObject* PyGC_AddRoot(PyObject* obj) noexcept {
    if (obj) {
        // Allow duplicates from CAPI code since they shouldn't have to know
        // which objects we already registered as roots:
        registerPermanentRoot(obj, /* allow_duplicates */ true);
//        GC_TRACE_LOG("PyGC_AddRoot %p\n", obj);
    }
    return obj;
}

void registerNonheapRootObject(void* obj, int size) {
    // I suppose that things could work fine even if this were true, but why would it happen?
    assert(GC.global_heap->getAllocationFromInteriorPointer(obj) == NULL);
    assert(GC.nonheap_roots.count(obj) == 0);

    GC.nonheap_roots.insert(obj);
    registerPotentialRootRange(obj, ((uint8_t*)obj) + size);

    max_nonheap_root = std::max(obj, max_nonheap_root);
    min_nonheap_root = std::min(obj, min_nonheap_root);
}

bool isNonheapRoot(void* p) {
    if (p > max_nonheap_root || p < min_nonheap_root)
        return false;
    return GC.nonheap_roots.count(p) != 0;
}

bool isValidGCMemory(void* p) {
    return isNonheapRoot(p) || (GC.global_heap->getAllocationFromInteriorPointer(p)->user_data == p);
}

bool isValidGCObject(void* p) {
    if (isNonheapRoot(p))
        return true;
    GCAllocation* al = GC.global_heap->getAllocationFromInteriorPointer(p);
    if (!al)
        return false;
    return al->user_data == p && (al->kind_id == GCKind::CONSERVATIVE_PYTHON || al->kind_id == GCKind::PYTHON);
}

void registerPythonObject(Box* b) {
    assert(isValidGCMemory(b));
    auto al = GCAllocation::fromUserData(b);

    if (al->kind_id == GCKind::CONSERVATIVE) {
        al->kind_id = GCKind::CONSERVATIVE_PYTHON;
    } else {
        assert(al->kind_id == GCKind::PYTHON);
    }

    assert(b->cls);
    if (hasOrderedFinalizer(b->cls)) {
        GC.objects_with_ordered_finalizers.push_back(b);
    }
    if (PyType_Check(b)) {
        GC.class_objects.insert((BoxedClass*)b);
    }
}

        ///////////////////////////////////////////////////////////////////////////////////////////////

void invalidateOrderedFinalizerList() {
    static StatCounter sc_us("us_gc_invalidate_ordered_finalizer_list");
    Timer _t("invalidateOrderedFinalizerList", /*min_usec=*/10000);

    for (auto iter = GC.objects_with_ordered_finalizers.begin(); iter != GC.objects_with_ordered_finalizers.end();) {
        Box* box = *iter;
        GCAllocation* al = GCAllocation::fromUserData(box);

        if (!hasOrderedFinalizer(box->cls) || hasFinalized(al)) {
            // Cleanup.
            iter = GC.objects_with_ordered_finalizers.erase(iter);
        } else {
            ++iter;
        }
    }

    long us = _t.end();
    sc_us.log(us);
}



static void callWeakrefCallback(PyWeakReference* head) {
    if (head->wr_callback) {
        runtimeCall(head->wr_callback, ArgPassSpec(1), reinterpret_cast<Box*>(head), NULL, NULL, NULL, NULL);
        head->wr_callback = NULL;
    }
}

static void callPendingFinalizers() {
    static StatCounter sc_us_finalizer("us_gc_finalizercalls");
    Timer _timer_finalizer("calling finalizers", /*min_usec=*/10000);

    bool initially_empty = GC.pending_finalization_list.empty();

    // An object can be resurrected in the finalizer code. So when we call a finalizer, we
    // mark the finalizer as having been called, but the object is only freed in another
    // GC pass (objects whose finalizers have been called are treated the same as objects
    // without finalizers).
    while (!GC.pending_finalization_list.empty()) {
        Box* box = GC.pending_finalization_list.front();
        GC.pending_finalization_list.pop_front();

        ASSERT(isValidGCObject(box), "objects to be finalized should still be alive");

        if (isWeaklyReferenced(box)) {
            // Callbacks for weakly-referenced objects with finalizers (if any), followed by call to finalizers.
            PyWeakReference** list = (PyWeakReference**)PyObject_GET_WEAKREFS_LISTPTR(box);
            while (PyWeakReference* head = *list) {
                assert(isValidGCObject(head));
                if (head->wr_object != Py_None) {
                    assert(head->wr_object == box);
                    _PyWeakref_ClearRef(head);

                    callWeakrefCallback(head);
                }
            }
        }

        finalize(box);
        ASSERT(isValidGCObject(box), "finalizing an object should not free the object");
    }

    if (!initially_empty) {
        invalidateOrderedFinalizerList();
    }

    sc_us_finalizer.log(_timer_finalizer.end());
}

static void callPendingWeakrefCallbacks() {
    static StatCounter sc_us_weakref("us_gc_weakrefcalls");
    Timer _timer_weakref("calling weakref callbacks", /*min_usec=*/10000);

    // Callbacks for weakly-referenced objects without finalizers.
    while (!GC.weakrefs_needing_callback_list.empty()) {
        PyWeakReference* head = GC.weakrefs_needing_callback_list.front();
        GC.weakrefs_needing_callback_list.pop_front();

        callWeakrefCallback(head);
    }

    sc_us_weakref.log(_timer_weakref.end());
}

void callPendingDestructionLogic() {
    static bool callingPending = false;

    // Calling finalizers is likely going to lead to another call to allowGLReadPreemption
    // and reenter callPendingDestructionLogic, so we'd really only be calling
    // one finalizer per function call to callPendingFinalizers/WeakrefCallbacks. The purpose
    // of this boolean is to avoid that.
    if (!callingPending) {
        callingPending = true;

        callPendingFinalizers();
        callPendingWeakrefCallbacks();

        callingPending = false;
    }
}


bool gcIsEnabled() {
    return GC.gcIsEnabled();
}

void enableGC() {
    GC.enableGC();
}

void disableGC() {
    GC.disableGC();
}

void startGCUnexpectedRegion() {
    RELEASE_ASSERT(!GC.should_not_reenter_gc, "");
    GC.should_not_reenter_gc = true;
}
void endGCUnexpectedRegion() {
    RELEASE_ASSERT(GC.should_not_reenter_gc, "");
    GC.should_not_reenter_gc = false;
}

void runCollection() {
    GC.runCollection();
}

} // namespace gc
} // namespace pyston
