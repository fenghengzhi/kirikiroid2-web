#pragma once

#include "IPSBType.h"
#include "../resources/ImageMetadata.h"

namespace PSB {

    class ArchiveType : public IPSBType {
    public:
        PSBType getPSBType() override { return PSBType::ArchiveInfo; }

        bool isThisType(const DecodedPSBFile &psb) override;

        std::vector<std::unique_ptr<IResourceMetadata>>
        collectResources(const DecodedPSBFile &psb, bool deDuplication) override;
    };
}; // namespace PSB