#include "PSBMediaRegistry.h"

#include "PSBMedia.h"
#include "StorageIntf.h"

void initPsbFile() {
    // Every reference guard-initializes the function-local pointer once, while
    // still registering the same media object on every invocation.
    static PSB::PSBMedia *psbMedia = new PSB::PSBMedia();
    TVPRegisterStorageMedia(psbMedia);
}
