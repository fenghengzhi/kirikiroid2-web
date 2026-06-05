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

        // pixelateDivision is registered on BOTH classes in the binary
        // (string "pixelateDivision" @0x14c1e50 has exactly 2 xrefs):
        //   - Motion.Player NCB (Player_ncb_registerMembers @0x6d86d8) ->
        //     Player_get/setPixelateDivision read/write Player instance +912
        //     (default 100). See Player.h.
        //   - D3DEmoteModule NCB (sub_52DFA8 @0x52e318) -> sub_52E44C/sub_52E454
        //     read/write D3DEmoteModule instance +20 (int). DIFFERENT field,
        //     different property body. The earlier "binary puts it at Player+912
        //     NOT D3DEmoteModule" comment was wrong: it exists on both.
        // Binary D3DEmoteModule ctor sub_52DF94 zeroes +8(qword)/+16(dword)/
        // +24(qword) but leaves the +20 gap unwritten, so the binary default is
        // effectively 0 (sibling fields' zero-default; allocation not pre-zeroed
        // makes it technically uninitialized, 0 is the faithful reproduction).
        static void setPixelateDivision(int v) { _pixelateDivision = v; }  // sub_52E454 @0x52e454 -> module+20
        static int getPixelateDivision() { return _pixelateDivision; }     // sub_52E44C @0x52e44c -> module+20

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
        // D3DEmoteModule instance +20 (int). Binary ctor sub_52DF94 leaves +20
        // unwritten; faithful default 0. Independent from Player+912 (default 100).
        inline static int _pixelateDivision = 0;
    };

} // namespace motion
