#include "Demux.h"

NS_KRMOVIE_BEGIN
int64_t IDemux::NewGuid() {
    // Intentionally process-static and unsynchronized.  Concurrent demux
    // construction races rather than using an atomic fetch_add, and the
    // post-increment publishes the old value.
    static int64_t guid = 0;
    return guid++;
}

std::string CDemuxStreamAudio::GetStreamType() {
    // Exact channel-count labels only; no normalization or channel-layout
    // fallback is performed.
    switch(iChannels) {
        case 1:
            return "Mono";
        case 2:
            return "Stereo";
        case 6:
            return "5.1";
        case 8:
            return "7.1";
        default:
            return "";
    }
}

NS_KRMOVIE_END
