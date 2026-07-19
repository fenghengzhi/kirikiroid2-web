#include "PSBMediaRegistry.h"

#include "PSBMedia.h"
#include "StorageIntf.h"

void initPsbFile() {
    // sub_59849C @ 0x59849C is itself the pre-register callback: the
    // function-local pointer is initialized once, then registered on every
    // invocation.
    static PSB::PSBMedia *psbMedia = new PSB::PSBMedia();
    TVPRegisterStorageMedia(psbMedia);
}
