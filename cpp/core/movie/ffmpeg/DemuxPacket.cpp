#include "DemuxPacket.h"
#include <memory>
#include "Clock.h"
#include "tjsUtils.h"

extern "C" {
#include "libavcodec/avcodec.h"
}

NS_KRMOVIE_BEGIN
void DemuxPacket::Free(DemuxPacket *pPacket) {
    if(pPacket) {
        try {
            // This must recover/delete the raw new[] owner recorded before the
            // aligned interior pointer; pData itself is not a delete[] target.
            if(pPacket->pData)
                TJSAlignedDealloc(pPacket->pData);
            delete pPacket;
        } catch(...) {
            // The reference path swallows cleanup exceptions and does not
            // retry either owner or clear the caller's raw packet pointer.
            //			CLog::Log(LOGERROR, "%s - Exception thrown
            // while
            // freeing packet", __FUNCTION__);
        }
    }
}

DemuxPacket *DemuxPacket::Allocate(int iDataSize /*= 0*/) {
    // Scalar new is intentionally outside the catch below, so object-allocation
    // failure propagates.  Exceptions after the object exists are converted to
    // a cleaned-up nullptr result.
    auto *pPacket = new DemuxPacket;

    try {
        memset(pPacket, 0, sizeof(DemuxPacket));

        // iDataSize <= 0 allocates no payload.  For a positive request this
        // reserves storage only; iSize remains zero until the producer writes
        // the logical packet length after copying data.

        if(iDataSize > 0) {
            // need to allocate a few bytes more.
            // From avcodec.h (ffmpeg)
            /**
             * Required number of additionally allocated bytes at the
             * end of the input bitstream for decoding. this is mainly
             * needed because some optimized bitstream readers read 32
             * or 64 bit at once and could read over the end<br> Note,
             * if the first 23 bits of the additional bytes are not 0
             * then damaged MPEG bitstreams could cause overread and
             * segfault
             */
            pPacket->pData = (uint8_t *)TJSAlignedAlloc(
                iDataSize + FF_INPUT_BUFFER_PADDING_SIZE, 4);
            if(!pPacket->pData) {
                Free(pPacket);
                return nullptr;
            }

            // Clear all 32 FFmpeg-required bytes after the logical payload.
            memset(pPacket->pData + iDataSize, 0, FF_INPUT_BUFFER_PADDING_SIZE);
        }

        // setup defaults; zeroing above leaves demuxerId/iGroupId/duration and
        // all ABI padding at zero.
        pPacket->dts = DVD_NOPTS_VALUE;
        pPacket->pts = DVD_NOPTS_VALUE;
        pPacket->iStreamId = -1;
        pPacket->dispTime = 0;
    } catch(...) {
        // Free has its own catch-all.  Regardless of whether cleanup itself
        // throws, this outer allocation path publishes nullptr.
        //		CLog::Log(LOGERROR, "%s - Exception thrown",
        //__FUNCTION__);
        Free(pPacket);
        pPacket = nullptr;
    }
    return pPacket;
}

NS_KRMOVIE_END
