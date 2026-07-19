#include "PSBMediaRegistry.h"

#include "PSBMedia.h"
#include "StorageIntf.h"

namespace PSB {
    void initPSBMedia() {
        // sub_59849C @ 0x59849C uses __cxa_guard_acquire/release around this
        // function-local pointer and then registers it on every callback.
        static PSBMedia *psbMedia = new PSBMedia();
        TVPRegisterStorageMedia(psbMedia);
    }
} // namespace PSB
