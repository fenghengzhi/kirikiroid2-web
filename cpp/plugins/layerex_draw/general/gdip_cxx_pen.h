//
// Created by lidong on 2025/1/7.
// more:
// https://learn.microsoft.com/en-us/windows/win32/api/gdipluspen
//
#pragma once
#include "gdip_dt.h"

#include "gdip_cxx_brush.h"
namespace libgdiplus {

    class Pen {
    public:
        Pen(GpPen *gpPen) : _gpPen(gpPen) {}

        Pen(const BrushBase *brush, float width) {
            // brush 是多态 C++ 包装对象（BrushBase 带虚函数 → 首字是 C++ vptr），
            // 不能直接 (Brush*)brush 当原生句柄传给 GdipCreatePen2——那样
            // GdipCloneBrush 会把 C++ vtable 当成 BrushClass*，按 clone_brush 签名
            // 间接调用到无关的虚槽，wasm call_indirect 严格类型检查下触发
            // "function signature mismatch" 陷阱。必须经 explicit operator GpBrush*()
            // 取真正的原生句柄（等价于 Windows Gdiplus::Pen 里的 brush->nativeBrush）。
            GdipCreatePen2((GpBrush *)*brush, width, UnitWorld, &this->_gpPen);
        }

        Pen(const Color &color, float width) {
            GdipCreatePen1(*(ARGB *)&color, width, UnitWorld, &this->_gpPen);
        }

        [[nodiscard]] Pen *Clone() const {
            GpPen *cloned{ nullptr };
            this->_gpStatus = GdipClonePen(this->_gpPen, &cloned);
            return new Pen{ cloned };
        }

        GpStatus SetWidth(float width) {
            this->_gpStatus = GdipSetPenWidth(this->_gpPen, width);
            return this->_gpStatus;
        }

        GpStatus SetAlignment(PenAlignment penAlignment) {
            this->_gpStatus = GdipSetPenMode(this->_gpPen, penAlignment);
            return this->_gpStatus;
        }

        GpStatus SetCompoundArray(const float *compoundArray, int count) {
            this->_gpStatus =
                GdipSetPenCompoundArray(this->_gpPen, compoundArray, count);
            return this->_gpStatus;
        }

        GpStatus SetDashCap(GpDashCap dashCap) {
            this->_gpStatus = GdipSetPenDashCap197819(this->_gpPen, dashCap);
            return this->_gpStatus;
        }

        GpStatus SetDashOffset(float dashOffset) {
            this->_gpStatus = GdipSetPenDashOffset(this->_gpPen, dashOffset);
            return this->_gpStatus;
        }

        GpStatus SetDashStyle(GpDashStyle dashStyle) {
            this->_gpStatus = GdipSetPenDashStyle(this->_gpPen, dashStyle);
            return this->_gpStatus;
        }

        GpStatus SetDashPattern(const float *dashArray, int count) {
            this->_gpStatus =
                GdipSetPenDashArray(this->_gpPen, dashArray, count);
            return this->_gpStatus;
        }

        GpStatus SetCustomStartCap(/* const */ CustomLineCap *customCap) {
            this->_gpStatus = GdipSetPenCustomStartCap(this->_gpPen, customCap);
            return this->_gpStatus;
        }

        GpStatus SetStartCap(GpLineCap startCap) {
            this->_gpStatus = GdipSetPenStartCap(this->_gpPen, startCap);
            return this->_gpStatus;
        }

        GpStatus SetCustomEndCap(/* const */ CustomLineCap *customCap) {
            this->_gpStatus = GdipSetPenCustomEndCap(this->_gpPen, customCap);
            return this->_gpStatus;
        }

        GpStatus SetEndCap(GpLineCap endCap) {
            this->_gpStatus = GdipSetPenEndCap(this->_gpPen, endCap);
            return this->_gpStatus;
        }

        GpStatus SetLineJoin(GpLineJoin lineJoin) {
            this->_gpStatus = GdipSetPenLineJoin(this->_gpPen, lineJoin);
            return this->_gpStatus;
        }

        GpStatus SetMiterLimit(float miterLimit) {
            this->_gpStatus = GdipSetPenMiterLimit(this->_gpPen, miterLimit);
            return this->_gpStatus;
        }

        [[nodiscard]] explicit operator GpPen *() const { return this->_gpPen; }

        ~Pen() { GdipDeletePen(this->_gpPen); }

    private:
        GpPen *_gpPen{ nullptr };
        mutable GpStatus _gpStatus{};
    };
} // namespace libgdiplus
