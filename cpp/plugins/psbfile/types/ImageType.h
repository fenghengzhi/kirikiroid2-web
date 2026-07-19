#pragma once

#include <string>

#include "IPSBType.h"
#include "BaseImageType.h"
#include "../resources/ImageMetadata.h"

namespace PSB {

    class ImageType : public BaseImageType, public IPSBType {
    public:
        static inline const std::string &G_ImageSourceKey = "imageList";

        PSBType getPSBType() override { return PSBType::Tachie; }

        bool isThisType(const DecodedPSBFile &psb) override;

        std::vector<std::unique_ptr<IResourceMetadata>>
        collectResources(const DecodedPSBFile &psb, bool deDuplication) override;
    };
}; // namespace PSB