#pragma once

#include <cstdint>

#ifdef EMSCRIPTEN

// Browser-only diagnostics. This flag is set once from ?perfStats=1 before the
// first main-loop tick, keeping the disabled hot-path cost to one predictable
// branch.
extern bool TVPWebPerformanceStatsEnabled;

void TVPWebPerfRecordBlendEnabled(std::uint64_t pixels);
void TVPWebPerfRecordDirtyRectEnabled(std::uint64_t area);
void TVPWebPerfRecordTextureUploadEnabled(std::uint64_t bytes);

inline void TVPWebPerfRecordBlend(std::uint64_t pixels) {
    if(TVPWebPerformanceStatsEnabled)
        TVPWebPerfRecordBlendEnabled(pixels);
}

inline void TVPWebPerfRecordDirtyRect(std::uint64_t area) {
    if(TVPWebPerformanceStatsEnabled)
        TVPWebPerfRecordDirtyRectEnabled(area);
}

inline void TVPWebPerfRecordTextureUpload(std::uint64_t bytes) {
    if(TVPWebPerformanceStatsEnabled)
        TVPWebPerfRecordTextureUploadEnabled(bytes);
}

#else

inline void TVPWebPerfRecordBlend(std::uint64_t) {}
inline void TVPWebPerfRecordDirtyRect(std::uint64_t) {}
inline void TVPWebPerfRecordTextureUpload(std::uint64_t) {}

#endif
