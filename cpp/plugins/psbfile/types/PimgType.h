#pragma once

#include <string>

#include "IPSBType.h"
#include "BaseImageType.h"
#include "../resources/ImageMetadata.h"

namespace PSB {

    class PimgType : public BaseImageType, public IPSBType {
    public:
        static inline const std::string &G_PimgSourceKey = "layers";

        PSBType getPSBType() override { return PSBType::Pimg; }

        bool isThisType(const DecodedPSBFile &psb) override;

        std::vector<std::unique_ptr<IResourceMetadata>>
        collectResources(const DecodedPSBFile &psb, bool deDuplication) override;
    };
}; // namespace PSB