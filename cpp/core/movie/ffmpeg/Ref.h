#pragma once

#include "KRMovieDef.h"
#include <atomic>
#include <assert.h>

NS_KRMOVIE_BEGIN

template <typename T>
struct IRef {
    IRef() : m_refs(1) {}

    virtual ~IRef() = default;

    IRef(const IRef &) = delete;

    IRef &operator=(const IRef &) = delete;

    virtual T *AddRef() {
        // The ordinary message hierarchy uses this default sequentially
        // consistent atomic increment.  It deliberately has no liveness or
        // overflow guard; GeneralSynchronize overrides only Release.
        m_refs++;
        return (T *)this;
    }

    virtual long Release() {
        // count is the post-decrement value.  The assertion is build-mode
        // dependent (retained by the iOS references, compiled out by the
        // Android references); it is not a saturating refcount check.
        intptr_t count = --m_refs;
        assert(count >= 0);
        // delete dispatches through T's virtual deleting destructor.  The
        // returned count remains valid as a scalar after self-destruction.
        if(count == 0)
            delete(T *)this;
        return count;
    }

    std::atomic_intptr_t m_refs;
};

NS_KRMOVIE_END
