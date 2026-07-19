#pragma once

#include <string>

#include "IPSBType.h"
#include "BaseImageType.h"
#include "../resources/ImageMetadata.h"

namespace PSB {

    class MotionType : public BaseImageType, public IPSBType {
    public:
        static inline const std::string &G_MotionSourceKey = "source";

        PSBType getPSBType() override { return PSBType::Motion; }

        bool isThisType(const DecodedPSBFile &psb) override;

        std::vector<std::unique_ptr<IResourceMetadata>>
        collectResources(const DecodedPSBFile &psb, bool deDuplication) override;
    };
}; // namespace PSB