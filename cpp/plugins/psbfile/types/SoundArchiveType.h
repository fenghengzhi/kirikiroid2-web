#pragma once

#include <string>

#include "IPSBType.h"

namespace PSB {

    class SoundArchiveType : public IPSBType {
    public:
        static inline const std::string &G_VoiceResourceKey = "voice";

        PSBType getPSBType() override { return PSBType::SoundArchive; }

        bool isThisType(const DecodedPSBFile &psb) override;

        std::vector<std::unique_ptr<IResourceMetadata>>
        collectResources(const DecodedPSBFile &psb, bool deDuplication) override;
    };
}; // namespace PSB