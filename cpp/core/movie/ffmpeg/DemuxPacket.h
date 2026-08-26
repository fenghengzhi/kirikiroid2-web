#pragma once

#include "KRMovieDef.h"
#include <stdint.h>

NS_KRMOVIE_BEGIN
#define DMX_SPECIALID_STREAMINFO -10
#define DMX_SPECIALID_STREAMCHANGE (-11)

struct DemuxPacket {
    // pData is a 16-byte-aligned interior pointer owned by this packet.  The
    // raw new[] pointer is stored by TJSAlignedAlloc immediately before it.
    unsigned char *pData; // data
    // Allocate reserves for its argument but deliberately leaves iSize zero;
    // the producer publishes the logical size only after filling the packet.
    int iSize; // data size
    int iStreamId; // integer representing the stream index
    int64_t demuxerId; // id of the demuxer that created the packet
    int iGroupId; // the group this data belongs to, used to group
                  // data from different streams together

    double pts; // pts in DVD_TIME_BASE
    double dts; // dts in DVD_TIME_BASE
    double duration; // duration in DVD_TIME_BASE if available

    int dispTime;

    static void Free(DemuxPacket *);

    static DemuxPacket *Allocate(int iDataSize = 0);
};

NS_KRMOVIE_END
