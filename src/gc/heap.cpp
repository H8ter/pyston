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

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

#include "core/common.h"
#include "core/util.h"
#include "gc/gc_alloc.h"
#include "runtime/objmodel.h"
#include "runtime/types.h"
#include "addr_remap.h"

#ifndef NVALGRIND
#include "valgrind.h"
#endif

//#undef VERBOSITY
//#define VERBOSITY(x) 2


namespace pyston {
namespace gc {


unsigned bytesAllocatedSinceCollection;

static StatCounter gc_registered_bytes("gc_registered_bytes");

void _bytesAllocatedTripped() {
    gc_registered_bytes.log(bytesAllocatedSinceCollection);
    bytesAllocatedSinceCollection = 0;

    if (!gcIsEnabled())
        return;

    threading::GLPromoteRegion _lock;

    runCollection();
}

//////
/// Finalizers

bool hasOrderedFinalizer(BoxedClass* cls) {
    if (cls->has_safe_tp_dealloc) {
        ASSERT(!cls->tp_del, "class \"%s\" with safe tp_dealloc also has tp_del?", cls->tp_name);
        return false;
    } else if (cls->hasNonDefaultTpDealloc()) {
        return true;
    } else {
        // The default tp_dealloc calls tp_del if there is one.
        return cls->tp_del != NULL;
    }
}

void finalize(Box* b) {
    GCAllocation* al = GCAllocation::fromUserData(b);
    assert(!hasFinalized(al));
    setFinalized(al);
    b->cls->tp_dealloc(b);
}

__attribute__((always_inline)) bool isWeaklyReferenced(Box* b) {
    if (PyType_SUPPORTS_WEAKREFS(b->cls)) {
        PyWeakReference** list = (PyWeakReference**)PyObject_GET_WEAKREFS_LISTPTR(b);
        if (list && *list) {
            return true;
        }
    }

    return false;
}

__attribute__((always_inline)) bool _doFree(GCAllocation* al, std::vector<Box*>* weakly_referenced) {
    static StatCounter gc_safe_destructors("gc_safe_destructor_calls");

#ifndef NVALGRIND
    VALGRIND_DISABLE_ERROR_REPORTING;
#endif
    GCKind alloc_kind = al->kind_id;
#ifndef NVALGRIND
    VALGRIND_ENABLE_ERROR_REPORTING;
#endif

    if (alloc_kind == GCKind::PYTHON || alloc_kind == GCKind::CONSERVATIVE_PYTHON) {
#ifndef NVALGRIND
        VALGRIND_DISABLE_ERROR_REPORTING;
#endif
        Box* b = (Box*)al->user_data;
#ifndef NVALGRIND
        VALGRIND_ENABLE_ERROR_REPORTING;
#endif

        assert(b->cls);
        if (isWeaklyReferenced(b)) {
            assert(weakly_referenced && "attempting to free a weakly referenced object manually");
            weakly_referenced->push_back(b);
            return false;
        }

        ASSERT(!hasOrderedFinalizer(b->cls) || hasFinalized(al) || alloc_kind == GCKind::CONSERVATIVE_PYTHON, "%s",
               getTypeName(b));

        if (b->cls->tp_dealloc != dealloc_null && b->cls->has_safe_tp_dealloc) {
            gc_safe_destructors.log();

            GCAllocation* al = GCAllocation::fromUserData(b);
            assert(!hasFinalized(al));
            assert(!hasOrderedFinalizer(b->cls));

            // Don't bother setting the finalized flag since the object is getting freed right now.
            b->cls->tp_dealloc(b);
        }
    }
    return true;
}




} // namespace gc
} // namespace pyston
