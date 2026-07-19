#pragma once

#include "IPSBType.h"
#include "BaseImageType.h"
#include "../resources/ImageMetadata.h"

namespace PSB {

    class ScnType : public BaseImageType, public IPSBType {
    public:
        PSBType getPSBType() override { return PSBType::Scn; }

        bool isThisType(const DecodedPSBFile &psb) override;

        std::vector<std::unique_ptr<IResourceMetadata>>
        collectResources(const DecodedPSBFile &psb, bool deDuplication) override;
    };
}; // namespace PSB