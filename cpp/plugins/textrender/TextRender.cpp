//
// textrender.dll — TextRenderBase 原生类（最小可加载骨架）
//
// 复刻自 libkrkr2.so（Android kirikiroid2）textrender.dll 插件。
// 权威反编译归档：analysis/textrender_textrenderbase_registration.md
//
// 模块注册链   TextRenderBase_moduleRegister      @0x42D01C  (L"TextRender.dll"/L"TextRenderBase")
// 成员注册     TextRenderBase_ncb_registerMembers @0x59BCCC  (17 method + 33 property)
// 真对象析构   TextRenderBase_dtor                @0x5A6B88  (揭示字段布局 ≥592B)
//
// 本阶段 = 骨架：注册全部成员的忠实签名（method/property 种类、RW/RO 拆分、
//   numparams、后备字段均来自二进制，非名字推导），方法体先桩、返回非崩溃默认值，
//   让 `class TextRender extends TextRenderBase`（TextRender.tjs）能编译、cgmode
//   初始化能进入鉴赏界面。render 状态机（0x5A228C）+ 字形度量（0x5A3880）后续填充。
//
// 字节布局复刻工作法：下面用语义字段名写普通 C++ 类，由编译器自由算偏移；
//   analysis 文档里的 ARM64 偏移仅供对照确认字段无遗漏，不进代码（wasm32 ABI 必然不同）。
//
#include <spdlog/spdlog.h>
#include <vector>
#include "tjs.h"
#include "tjsArray.h"
#include "ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("TextRender.dll")
#define LOGGER spdlog::get("plugin")
#define TR_STUB(name)                                                          \
    do {                                                                       \
        if(LOGGER)                                                             \
            LOGGER->debug("TextRenderBase::" name "() stub called");           \
    } while(0)

namespace textrender {

// ============================================================
// TextRenderBase — 文本布局引擎（非 Layer 子类；基类仅 refcount，见 0x5A6B88）
//   字段语义来自 TextRenderBase_dtor@0x5A6B88 揭示的真对象布局（§3b）。
// ============================================================
class TextRenderBase {
public:
    TextRenderBase() = default;
    ~TextRenderBase() = default;

    // ---- 逐字符 / 行 / face 表（骨架阶段保持空；render 状态机后续填充）----
    struct CharItem {
        ttstr text;
        float x = 0, y = 0, cw = 0, size = 0, lineY = 0;
        tjs_uint32 color = 0, shadowColor = 0, edgeColor = 0, shadowDiff = 0;
        bool bold = false, italic = false, shadow = false, edge = false,
             vertical = false;
        int lineIdx = 0, delay = 0;
        ttstr face;
    };
    struct LineItem {
        float left = 0, top = 0, right = 0, bottom = 0; // bbox (+22..+25)
        float offset = 0;                               // align 偏移 (+80)
    };
    std::vector<CharItem> _charList; // 真对象 +296
    std::vector<LineItem> _lineList; // 真对象 +432 (stride 112)
    std::vector<ttstr> _faceTable;   // 真对象 +456 (index→face)
    std::vector<int> _keyWaitList;   // 真对象 +480 (pos)

    // ---- 选项 (setOption → byte 字段 +48..+59,+112) ----
    bool _vertical = false;          // +48
    bool _ignoreColor = false;       // +50
    bool _ignoreSize = false;        // +51
    bool _ignoreDelay = false;       // +52
    bool _ignoreRuby = false;        // +56
    bool _ignoreType = false;        // +57
    bool _ignoreFace = false;        // +58
    bool _ignoreStyle = false;       // +59
    int _kinsokuMax = 0;             // +112

    // ---- 当前样式 (setFont/setStyle 改写) ----
    bool _curBold = false;           // +62
    bool _curShadow = false;         // +63
    bool _curEdge = false;           // +64
    bool _curItalic = false;         // +65
    int _curFaceIndex = 0;           // +72
    float _curFontSize = 0;          // +116
    float _curLineSpacing = 0;       // +136
    float _curPitch = 0;             // +140
    float _curLineSize = 0;          // +144
    float _curRubySize = 0;          // +128
    tjs_uint32 _curChColor = 0;      // +200
    tjs_uint32 _curShadowColor = 0;  // +204
    tjs_uint32 _curShadowDiff = 0;   // +208
    tjs_uint32 _curEdgeColor = 0;    // +212

    // ---- 默认样式 (setDefault 改写；resetFont/Style 复位为这些) ----
    ttstr _defaultFace;                  // 二进制为 +96 face index；骨架直接存 ttstr
    bool _defaultBold = false;           // +66
    bool _defaultShadow = false;         // +67
    bool _defaultEdge = false;           // +68
    bool _defaultItalic = false;         // +69
    int _defaultAlign = 0;               // +100
    int _defaultValign = 0;              // +104
    float _defaultFontSize = 24.0f;      // +148
    float _defaultBigFontSize = 0;       // +152
    float _defaultSmallFontSize = 0;     // +156
    float _defaultRubySize = 0;          // +160
    float _defaultRubyOffset = 0;        // +164
    float _defaultLineSpacing = 0;       // +168
    float _defaultPitch = 0;             // +172
    float _defaultLineSize = 0;          // +176
    tjs_uint32 _defaultChColor = 0xffffff;   // +216
    tjs_uint32 _defaultShadowColor = 0;      // +220
    tjs_uint32 _defaultShadowDiff = 0;       // +224
    tjs_uint32 _defaultEdgeColor = 0;        // +228

    // ---- 全局缩放 ----
    float _timeScale = 1.0f;         // +180
    float _fontScale = 1.0f;         // +184
    float _renderDelayAccum = 0;     // +188

    // ---- 渲染尺寸 / 结果 bbox / 状态 ----
    float _renderSizeW = 0;          // +240
    float _renderSizeH = 0;          // +244
    float _renderLeft = 0;           // +248
    float _renderTop = 0;            // +252
    float _renderRight = 0;          // +256
    float _renderBottom = 0;         // +260
    bool _renderOver = false;        // +60
    int _renderCount = 0;            // +84
    ttstr _renderText;               // +40 (tTJSVariant)

    // ============================================================
    // Property accessors（RW/RO 拆分严格按二进制 setter==0 判定，§2）
    //   宏只生成 getter/setter，字段语义见上。
    // ============================================================
#define TR_RW(type, prop, field)                                               \
    type get_##prop() const { return field; }                                  \
    void set_##prop(type v) { field = v; }
#define TR_RO(type, prop, field)                                               \
    type get_##prop() const { return field; }

    // RW (22) — getter/setter 见 §2 表，setter != 0
    TR_RW(bool, vertical, _vertical)                  // 0x5A0D74 / 0x5A0D7C
    TR_RW(double, timeScale, _timeScale)              // 0x5A0D88 / 0x5A0D90
    TR_RW(double, fontScale, _fontScale)              // 0x5A0D98 / 0x5A0DA0
    TR_RW(ttstr, defaultFace, _defaultFace)           // 0x5A0DA8 / 0x5A0E0C
    TR_RW(double, defaultFontSize, _defaultFontSize)  // 0x5A0EAC / 0x5A0EB4
    TR_RW(double, defaultBigFontSize, _defaultBigFontSize)     // 0x5A0EBC
    TR_RW(double, defaultSmallFontSize, _defaultSmallFontSize) // 0x5A0ECC
    TR_RW(double, defaultLineSize, _defaultLineSize)  // 0x5A0EDC
    TR_RW(double, defaultLineSpacing, _defaultLineSpacing) // 0x5A0EEC
    TR_RW(double, defaultPitch, _defaultPitch)        // 0x5A0EFC
    TR_RW(tjs_int, defaultAlign, _defaultAlign)       // 0x5A0F0C
    TR_RW(tjs_int, defaultValign, _defaultValign)     // 0x5A0F1C
    TR_RW(double, defaultRubySize, _defaultRubySize)  // 0x5A0F2C
    TR_RW(double, defaultRubyOffset, _defaultRubyOffset) // 0x5A0F3C
    TR_RW(tjs_uint32, defaultChColor, _defaultChColor)     // 0x5A0F4C
    TR_RW(bool, defaultShadow, _defaultShadow)        // 0x5A0F5C
    TR_RW(tjs_uint32, defaultShadowColor, _defaultShadowColor) // 0x5A0F70
    TR_RW(tjs_uint32, defaultShadowDiff, _defaultShadowDiff)   // 0x5A0F80
    TR_RW(bool, defaultEdge, _defaultEdge)            // 0x5A0F90
    TR_RW(tjs_uint32, defaultEdgeColor, _defaultEdgeColor)     // 0x5A0FA4
    TR_RW(bool, defaultBold, _defaultBold)            // 0x5A0FB4
    TR_RW(bool, defaultItalic, _defaultItalic)        // 0x5A0FC8

    // RO (11) — render* / maxScroll*，二进制 setter==0（§2）
    TR_RO(bool, renderOver, _renderOver)              // 0x5A0FDC
    tjs_int get_renderLines() const {                 // 0x5A0FE4 (行列表元素数)
        return (tjs_int)_lineList.size();
    }
    TR_RO(tjs_int, renderCount, _renderCount)         // 0x5A1000
    double get_renderDelay() const {                  // 0x5A1008 (delay*timeScale)
        return _renderDelayAccum * _timeScale;
    }
    TR_RO(double, renderLeft, _renderLeft)            // 0x5A1018
    TR_RO(double, renderTop, _renderTop)              // 0x5A1020
    TR_RO(double, renderRight, _renderRight)          // 0x5A1028
    TR_RO(double, renderBottom, _renderBottom)        // 0x5A1030
    TR_RO(ttstr, renderText, _renderText)             // 0x5A1038
    double get_maxScrollOffset() const {              // 0x5A1058
        // §2: vertical ? (+240 - +248) : (+244 - +260)
        return _vertical ? (_renderSizeW - _renderLeft)
                         : (_renderSizeH - _renderBottom);
    }
    double get_maxScrollLine() const { return 0.0; }  // 0x5A1080 (行扫描；骨架=0)

#undef TR_RW
#undef TR_RO

    // ============================================================
    // 简单签名方法（typed）— 桩
    // ============================================================
    void setRenderSize(double w, double h) { // 0x59EB70 → 写 +240/+244 后调 clear
        _renderSizeW = (float)w;
        _renderSizeH = (float)h;
        clear();
    }
    void clear() { // 0x59EC6C — 复位渲染状态、重建列表
        TR_STUB("clear");
        _charList.clear();
        _lineList.clear();
        _keyWaitList.clear();
        _renderCount = 0;
        _renderOver = false;
        _renderLeft = _renderTop = _renderRight = _renderBottom = 0;
        _renderText = ttstr();
        _renderDelayAccum = 0;
    }
    void resetFont() { // 0x59EEE0 — 当前样式复位为 default*
        TR_STUB("resetFont");
        _curBold = _defaultBold;
        _curItalic = _defaultItalic;
        _curShadow = _defaultShadow;
        _curEdge = _defaultEdge;
        _curFontSize = _defaultFontSize;
        _curChColor = _defaultChColor;
        _curShadowColor = _defaultShadowColor;
        _curShadowDiff = _defaultShadowDiff;
        _curEdgeColor = _defaultEdgeColor;
        _curRubySize = _defaultRubySize;
    }
    void resetStyle() { // 0x59EFBC — resetFont 同族
        TR_STUB("resetStyle");
        resetFont();
        _curLineSpacing = _defaultLineSpacing;
        _curPitch = _defaultPitch;
        _curLineSize = _defaultLineSize;
    }
    void newline() { TR_STUB("newline"); }     // 0x59FECC — 强制换行
    void done() { TR_STUB("done"); }           // 0x59FEE4 — 终结布局（align/bbox/排序）
    double calcLineOffset(tjs_int lineIdx) {   // 0x5A05FC → float
        TR_STUB("calcLineOffset");
        if(lineIdx < 0 || lineIdx >= (tjs_int)_lineList.size())
            return _renderBottom; // 越界返回 bottom（§4）
        return _lineList[lineIdx].offset;
    }
    tjs_int calcShowCount(tjs_int /*width*/) {  // 0x5A0644 → 可显示字符数
        TR_STUB("calcShowCount");
        return (tjs_int)_charList.size();
    }

    // ============================================================
    // 复杂签名方法（raw callback）— 桩
    //   签名：static tjs_error fn(result, numparams, param, objthis)
    // ============================================================
    static TextRenderBase *self(iTJSDispatch2 *objthis) {
        return ncbInstanceAdaptor<TextRenderBase>::GetNativeInstance(objthis,
                                                                     true);
    }

    // setOption / setDefault / setFont / setStyle: (dict) 1 参，逐 key PropGet
    static tjs_error setOption(tTJSVariant *, tjs_int, tTJSVariant **,
                               iTJSDispatch2 *objthis) { // 0x59D2AC
        if(!self(objthis))
            return TJS_E_INVALIDOBJECT;
        TR_STUB("setOption");
        return TJS_S_OK;
    }
    static tjs_error setDefault(tTJSVariant *, tjs_int, tTJSVariant **,
                                iTJSDispatch2 *objthis) { // 0x59DEA8
        if(!self(objthis))
            return TJS_E_INVALIDOBJECT;
        TR_STUB("setDefault");
        return TJS_S_OK;
    }
    static tjs_error setFont(tTJSVariant *, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *objthis) { // 0x59EFD8
        if(!self(objthis))
            return TJS_E_INVALIDOBJECT;
        TR_STUB("setFont");
        return TJS_S_OK;
    }
    static tjs_error setStyle(tTJSVariant *, tjs_int, tTJSVariant **,
                              iTJSDispatch2 *objthis) { // 0x59F7AC
        if(!self(objthis))
            return TJS_E_INVALIDOBJECT;
        TR_STUB("setStyle");
        return TJS_S_OK;
    }

    // render: (string, int, int[, real, bool]) numparams≥3，返回 bool
    static tjs_error render(tTJSVariant *result, tjs_int numparams,
                            tTJSVariant **, iTJSDispatch2 *objthis) { // 0x59FC28
        if(!self(objthis))
            return TJS_E_INVALIDOBJECT;
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT; // 真函数 0x5A228C 同样门控（§4）
        TR_STUB("render");
        // 骨架：尚未排版，返回 true（=渲染成功）。状态机 0x5A228C 后续填充。
        if(result)
            *result = (tjs_int)1;
        return TJS_S_OK;
    }

    // onEval: (expr) → TJS eval；骨架不求值
    static tjs_error onEval(tTJSVariant *result, tjs_int, tTJSVariant **,
                            iTJSDispatch2 *objthis) { // 0x5A0294
        if(!self(objthis))
            return TJS_E_INVALIDOBJECT;
        TR_STUB("onEval");
        if(result)
            result->Clear();
        return TJS_S_OK;
    }

    // getKeyWait: () → TJS Array of {pos,time}；骨架返回空 Array
    static tjs_error getKeyWait(tTJSVariant *result, tjs_int, tTJSVariant **,
                                iTJSDispatch2 *objthis) { // 0x5A02DC
        if(!self(objthis))
            return TJS_E_INVALIDOBJECT;
        TR_STUB("getKeyWait");
        if(result) {
            iTJSDispatch2 *arr = TJSCreateArrayObject();
            *result = tTJSVariant(arr, arr);
            arr->Release();
        }
        return TJS_S_OK;
    }

    // getCharacters: (start, count) → TJS Array of per-char dict；骨架返回空 Array
    static tjs_error getCharacters(tTJSVariant *result, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) { // 0x5A0694
        if(!self(objthis))
            return TJS_E_INVALIDOBJECT;
        TR_STUB("getCharacters");
        if(result) {
            iTJSDispatch2 *arr = TJSCreateArrayObject();
            *result = tTJSVariant(arr, arr);
            arr->Release();
        }
        return TJS_S_OK;
    }
};

} // namespace textrender

using textrender::TextRenderBase;

// ============================================================
// NCB 注册（模块 TextRender.dll；全部 objectMember/flags=0，§5）
// ============================================================
NCB_REGISTER_CLASS(TextRenderBase) {
    NCB_CONSTRUCTOR(()); // TextRender.tjs 构造调 super()（默认构造）

    // ---- 17 methods ----
    NCB_METHOD_RAW_CALLBACK(setOption, &Class::setOption, 0);
    NCB_METHOD_RAW_CALLBACK(setDefault, &Class::setDefault, 0);
    NCB_METHOD(setRenderSize);
    NCB_METHOD(clear);
    NCB_METHOD(resetFont);
    NCB_METHOD(resetStyle);
    NCB_METHOD_RAW_CALLBACK(setFont, &Class::setFont, 0);
    NCB_METHOD_RAW_CALLBACK(setStyle, &Class::setStyle, 0);
    NCB_METHOD_RAW_CALLBACK(render, &Class::render, 0);
    NCB_METHOD(newline);
    NCB_METHOD(done);
    NCB_METHOD_RAW_CALLBACK(onEval, &Class::onEval, 0);
    NCB_METHOD_RAW_CALLBACK(getKeyWait, &Class::getKeyWait, 0);
    NCB_METHOD(calcLineOffset);
    NCB_METHOD(calcShowCount);
    NCB_METHOD_RAW_CALLBACK(getCharacters, &Class::getCharacters, 0);

    // ---- 22 RW properties ----
    NCB_PROPERTY(vertical, get_vertical, set_vertical);
    NCB_PROPERTY(timeScale, get_timeScale, set_timeScale);
    NCB_PROPERTY(fontScale, get_fontScale, set_fontScale);
    NCB_PROPERTY(defaultFace, get_defaultFace, set_defaultFace);
    NCB_PROPERTY(defaultFontSize, get_defaultFontSize, set_defaultFontSize);
    NCB_PROPERTY(defaultBigFontSize, get_defaultBigFontSize,
                 set_defaultBigFontSize);
    NCB_PROPERTY(defaultSmallFontSize, get_defaultSmallFontSize,
                 set_defaultSmallFontSize);
    NCB_PROPERTY(defaultLineSize, get_defaultLineSize, set_defaultLineSize);
    NCB_PROPERTY(defaultLineSpacing, get_defaultLineSpacing,
                 set_defaultLineSpacing);
    NCB_PROPERTY(defaultPitch, get_defaultPitch, set_defaultPitch);
    NCB_PROPERTY(defaultAlign, get_defaultAlign, set_defaultAlign);
    NCB_PROPERTY(defaultValign, get_defaultValign, set_defaultValign);
    NCB_PROPERTY(defaultRubySize, get_defaultRubySize, set_defaultRubySize);
    NCB_PROPERTY(defaultRubyOffset, get_defaultRubyOffset,
                 set_defaultRubyOffset);
    NCB_PROPERTY(defaultChColor, get_defaultChColor, set_defaultChColor);
    NCB_PROPERTY(defaultShadow, get_defaultShadow, set_defaultShadow);
    NCB_PROPERTY(defaultShadowColor, get_defaultShadowColor,
                 set_defaultShadowColor);
    NCB_PROPERTY(defaultShadowDiff, get_defaultShadowDiff,
                 set_defaultShadowDiff);
    NCB_PROPERTY(defaultEdge, get_defaultEdge, set_defaultEdge);
    NCB_PROPERTY(defaultEdgeColor, get_defaultEdgeColor, set_defaultEdgeColor);
    NCB_PROPERTY(defaultBold, get_defaultBold, set_defaultBold);
    NCB_PROPERTY(defaultItalic, get_defaultItalic, set_defaultItalic);

    // ---- 11 RO properties (render* / maxScroll*，setter==0) ----
    NCB_PROPERTY_RO(renderOver, get_renderOver);
    NCB_PROPERTY_RO(renderLines, get_renderLines);
    NCB_PROPERTY_RO(renderCount, get_renderCount);
    NCB_PROPERTY_RO(renderDelay, get_renderDelay);
    NCB_PROPERTY_RO(renderLeft, get_renderLeft);
    NCB_PROPERTY_RO(renderTop, get_renderTop);
    NCB_PROPERTY_RO(renderRight, get_renderRight);
    NCB_PROPERTY_RO(renderBottom, get_renderBottom);
    NCB_PROPERTY_RO(renderText, get_renderText);
    NCB_PROPERTY_RO(maxScrollOffset, get_maxScrollOffset);
    NCB_PROPERTY_RO(maxScrollLine, get_maxScrollLine);
}
