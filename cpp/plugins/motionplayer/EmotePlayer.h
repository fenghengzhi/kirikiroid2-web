//
// Created by LiDon on 2025/9/15.
//
#pragma once

#include "ResourceManager.h"

namespace motion {

    enum class MaskMode { MaskModeAlpha };

    class EmotePlayer {
    public:
        explicit EmotePlayer(iTJSDispatch2 *rm) {}

        void initPhysics() {}

        void setUseD3D(bool useD3D) { this->_useD3D = useD3D; }
        [[nodiscard]] bool getUseD3D() const { return this->_useD3D; }

        tTJSVariant getVariable(ttstr name) { return tTJSVariant(); }
        void setVariable(ttstr name, tTJSVariant value) {}

    private:
        bool _useD3D = false;
        MaskMode _maskMode = MaskMode::MaskModeAlpha;
    };

} // namespace motion