//
// Reverse-engineered from libkrkr2.so D3DEmoteModule class
// Top-level class registered under emoteplayer.dll
//
#pragma once

#include <spdlog/spdlog.h>

namespace motion {

    class D3DEmoteModule {
    public:
        D3DEmoteModule() = default;

        static void setMaskMode(int v) { _maskMode = v; }
        static int getMaskMode() { return _maskMode; }

        static void setMaskRegionClipping(bool v) { _maskRegionClipping = v; }
        static bool getMaskRegionClipping() { return _maskRegionClipping; }

        static void setMipMapEnabled(bool v) { _mipMapEnabled = v; }
        static bool getMipMapEnabled() { return _mipMapEnabled; }

        static void setAlphaOp(int v) { _alphaOp = v; }
        static int getAlphaOp() { return _alphaOp; }

        static void setProtectTranslucentTextureColor(bool v) {
            _protectTranslucentTextureColor = v;
        }
        static bool getProtectTranslucentTextureColor() {
            return _protectTranslucentTextureColor;
        }

        // R-pixelate phase 2: removed pixelateDivision from D3DEmoteModule.
        // cluster E §1 ctor: binary stores pixelateDivision at Player+912
        // (instance default 100), NOT D3DEmoteModule static. Migration done
        // in commit 15e5ddc (Player field add) + this commit (D3DEmoteModule
        // static remove). C++ static field also removed below.

        static void setMaxTextureSize(int w, int h) {
            spdlog::get("plugin")->warn(
                "D3DEmoteModule::setMaxTextureSize({}, {}) stub called", w, h);
        }

    private:
        inline static int _maskMode = 1; // MaskModeAlpha
        inline static bool _maskRegionClipping = false;
        inline static bool _mipMapEnabled = false;
        inline static int _alphaOp = 0;
        inline static bool _protectTranslucentTextureColor = false;
        // R-pixelate phase 2: removed _pixelateDivision static field (binary
        // has it as Player +912 instance, not D3DEmoteModule static).
    };

} // namespace motion
