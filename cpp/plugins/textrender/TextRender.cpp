//
// textrender.dll — TextRenderBase 原生类
//
// 复刻自 libkrkr2.so（Android kirikiroid2）textrender.dll 插件。
// 权威反编译归档：analysis/textrender_textrenderbase_registration.md（含 §10
//   绑定器机制全图 + 方案 A 落地记录）。
//
// 模块注册链   TextRenderBase_moduleRegister      @0x42D01C  (L"TextRender.dll"/L"TextRenderBase")
// 成员注册     TextRenderBase_ncb_registerMembers @0x59BCCC  (1 ctor + 16 method + 33 property)
// 构造器成员   TextRenderBase_ncb_constructor     @0x59D160  (TJS new 时 new(0x250)+ctor(obj,objthis))
// 真构造函数   TextRenderBase_ctor                @0x5A111C  (首句 +0=objthis；默认值群 + 内置禁则集)
// 真对象析构   TextRenderBase_dtor                @0x5A6B88  (揭示字段布局 ≥592B；+0 objthis 不 Release)
//
// 绑定器机制（§10，2026-06-12 取证）：textrender 不自带独立绑定器——它与 motionplayer
//   共用同一条 ncbind 注册链（1AB8920 三相数组 = 本地 ncbAutoRegister；同一 LoadModule
//   消费者 sub_704A08）。invoker（sub_5A76EC..5A9B78）是 ncbind Functor/Property 模板按
//   签名实例化的多实例（共享 GETINSTANCE/Itanium PMF 解封/错误码序列），守护串
//   "Invalid instance type."/"No method pointer."/"Multiple constructors." 全在本地
//   cpp/core/plugin/ncbind.hpp。故忠实复刻 = 用本地 ncbind 既有设施重接，零改 ncbind.hpp。
//
// objthis 数据流：二进制 ctor @0x5A111C 首句 `*(this+0)=objthis`（构造器成员 @0x59D160
//   `new(0x250); ctor(obj, objthis)` 注入），dtor @0x5A6B88 对 +0 **不 Release**（仅
//   Release +8/+16/+24/+32/+40 等 ttstr/keyWait，不动 +0）→ +0 是裸 dispatch 指针、
//   不增引用、不释放。本地用 ncbind `Factory(&factory)`（工厂签名直收 objthis，
//   见 DrawDeviceD3D.cpp:47/:349）`new TextRenderBase(objthis)` 复刻该数据流；
//   `objthis` 成员是裸指针、不 AddRef/不 Release（生命周期由 TJS 脚本对象持有，
//   native 仅借引用回调脚本 onGetTextWidth/onEval/onFontChange）。所有内部落字/度量/
//   eval 函数读 `objthis` 成员（= 二进制 `*(this+0)`），不再线程化形参。
//
// 字节布局复刻工作法：下面用语义字段名写普通 C++ 类，**声明顺序按二进制偏移序**
//   （objthis(+0)→following/leading/begin/end/renderText(+8..+40)→option bytes(+48..)→
//   …→faceHash(+536)，偏移表见 analysis §3b）让编译器自由算偏移；analysis 里的 ARM64
//   偏移仅供对照确认字段无遗漏，不进代码（wasm32 ABI 必然不同，禁 pragma pack/
//   static_assert(offsetof)）。
//
#include <spdlog/spdlog.h>
#include <vector>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include "tjs.h"
#include "tjsArray.h"
#include "ncbind.hpp"
#include "ScriptMgnIntf.h" // TVPExecuteExpression（onEval @0x5A0294 → sub_8E3FA4）

#define NCB_MODULE_NAME TJS_W("TextRender.dll")
#define LOGGER spdlog::get("plugin")

namespace textrender {

// ============================================================
// TextRenderBase — 文本布局引擎（非 Layer 子类；基类仅 refcount，见 0x5A6B88）
//   字段语义来自 TextRenderBase_dtor@0x5A6B88 揭示的真对象布局（§3b）。
// ============================================================
class TextRenderBase {
public:
    // ---- 元素 POD / 行结构（数据契约：拷贝/落字按字段读写，POD 内部布局是平台无关
    //   的数据契约；对象 ABI 偏移不需对齐）----
    //   CharItem = 二进制 80B POD（charItem）忠实复刻。字段语义/顺序经 sub_5A4838(拷贝)
    //   @0x5A4838 + appendChar@0x5A3880 + sub_5A4A7C@0x5A4A7C + calcShowCount@0x5A0644
    //   四处反编译交叉确认（analysis §3b-1）。
    struct RubyItem {                    // charItem +56 vector 元素（20B/elem）
        ttstr text;                      // +0 (refcounted ttstr*)
        float x = 0;                     // +8  ruby x 偏移 (appendChar v28-3 = v41)
        float y = 0;                     // +12 ruby y 偏移 (appendChar v28-2 = v40)
        float span = 0;                  // +16 ruby span = rubySize*fontScale (v28-1 = v34*v36)
    };
    struct CharItem {
        ttstr text;                      // +0  单字符文本（onGetTextWidth 输入）
        float x = 0;                     // +8  pen 横坐标（落字回填）
        float y = 0;                     // +12 pen 纵坐标（落字回填）
        float cw = 0;                    // +16 字宽（onGetTextWidth 返回）
        float size = 0;                  // +20 有效字号 = fontScale×curFontSize
        float renderPos = 0;             // +24 落字累积渲染位置（calcShowCount 倒扫读）
        tjs_uint32 chColor = 0;          // +28
        tjs_uint32 shadowColor = 0;      // +32
        tjs_uint32 edgeColor = 0;        // +36
        bool graph = false;              // +40 (构造置 0)；脚本面真名 "graph"
                                         //   （getCharacters@0x5a081c dict 首字段
                                         //    L"graph"，串 @0x14CA19A UTF-16LE）
        bool bold = false;               // +41
        bool italic = false;             // +42
        bool shadow = false;             // +43
        bool edge = false;               // +44
        bool vertical = false;           // +45
        tjs_uint32 shadowDiff = 0;       // +48
        int faceIndex = 0;               // +52
        std::vector<RubyItem> ruby;      // +56/+64/+72 ruby 子标注
    };
    // Line = 二进制 112B 行结构（嵌套结构体，源码结构复刻）。pending 行缓冲（真对象
    //   +320）与 lineList 元素（+432，stride 112）**同型**——三重证据见 §3b。
    //   布局：+0..+79 = std::deque<charItem>（80B 控制块）；+80..+103 = 6 float metric；
    //   +104 = int wordBreakRun；+108 = bool prevWasSpace。
    struct Line {
        std::deque<CharItem> chars;   // +0..+79 行内字符（嵌套 deque<charItem>，6/node）
        float lineBottom = 0;         // +80  行内 max(行高 + char.y)；done valign offset 写它
        float lineHeight = 0;         // +84  max(charSize, curLineSize)
        float bboxLeft = 0;           // +88
        float bboxTop = 0;            // +92
        float bboxRight = 0;          // +96
        float bboxBottom = 0;         // +100
        int wordBreakRun = 0;         // +104 行内 word-break run 起点字符数
        bool prevWasSpace = false;    // +108 上个落字是否空格
        // Line::clear @0x5A1E68：deque 清空（std::deque::clear() 语义）+ 零化 +80..+108。
        void clear() { // sub_5A1E68 (TextRenderBase_pendingLine_clear)
            chars.clear();        // sub_5A1C50 + 释放多余 map 槽，游标复位
            lineBottom = 0;       // a1[10] 低 32 位（+80..+87 8B 清零）
            lineHeight = 0;       // a1[10] 高 32 位
            bboxLeft = 0;         // a1[11]（+88..+95）
            bboxTop = 0;
            bboxRight = 0;        // a1[12]（+96..+103）
            bboxBottom = 0;
            wordBreakRun = 0;     // STUR XZR,[X19,#0x65]（+101..+108 覆盖 +104/+108）
            prevWasSpace = false;
        }
    };
    // keyWait 列表元素：二进制 +480 std::vector，8B 元素 = 两个 int（§9.2）。
    struct KeyWaitItem {
        int index = 0; // +0 低 int：\k push renderCount；getKeyWait 读它做 pos/time
        int time = 0;  // +4 高 int：done 写 charList[index].renderPos bits（dead-for-getKeyWait）
    };
    // face hash 表 functor（resolveFaceIndex 内联 hash 数据契约 @0x5A14DC）。
    struct FaceNameHash {
        size_t operator()(const ttstr &s) const {
            const tjs_char *p = s.c_str();
            unsigned int h;
            if(!p || !*p) {
                h = 0;
            } else {
                unsigned int acc = 0;
                tjs_char ch = *p++;
                do {
                    unsigned int t = 1025u * (acc + (unsigned int)ch);
                    acc = t ^ (t >> 6);
                    ch = *p++;
                } while(ch);
                h = 9u * acc;
            }
            unsigned int v = 32769u * (h ^ (h >> 11));
            return v ? v : 0xFFFFFFFFu;
        }
    };
    struct FaceNameEq {
        bool operator()(const ttstr &a, const ttstr &b) const { return a == b; }
    };

    // ============================================================
    // 数据成员（声明顺序 = 二进制偏移序，analysis §3b 偏移表逐项对照）
    // ============================================================
    // +0：objthis（dispatch 回指）。二进制 ctor @0x5A111C 首句 `*(this+0)=objthis`；
    //   dtor @0x5A6B88 **不** Release +0。裸指针、不 AddRef/不 Release——由 ncbind
    //   Factory(&factory) 在 TJS `new` 时注入（= 二进制 @0x59D160→@0x5A111C 数据流）。
    iTJSDispatch2 *objthis = nullptr;    // +0

    // 禁则字符集字符串 (setOption +8/+16/+24/+32)。二进制存 tTJSVariant*（refcounted
    //   string），仅接受 string/void，object/octet/int/real 抛转换错误。语义=ttstr。
    //   ctor @0x5A111C 以内置日文禁则集 4 串初始化（见构造函数体）。
    ttstr _following;                // +8  L"following"
    ttstr _leading;                  // +16 L"leading"
    ttstr _begin;                    // +24 L"begin"
    ttstr _end;                      // +32 L"end"
    ttstr _renderText;               // +40 (tTJSVariant) 落字累积文本（finishLine 追加换行/缩进）

    // 选项 byte（setOption@0x59D2AC 逐 key 反编译确认；ignore_over 与 ignore_overy 同写
    //   +54 后者覆盖前者；kinsoku_max bool-coerce 写 DWORD 存 0/1）。
    //   ctor 默认值 @0x5A111C：WORD+48=0x0100 → vertical=0、word_break=1。
    bool _vertical = false;          // +48  L"vertical"（ctor=0）
    bool _wordBreak = true;          // +49  L"word_break"（ctor=1）
    bool _ignoreColor = false;       // +50  L"ignore_color"
    bool _ignoreSize = false;        // +51  L"ignore_size"
    bool _ignoreDelay = false;       // +52  L"ignore_delay"
    bool _ignoreOverX = false;       // +53  L"ignore_overx"
    bool _ignoreOverY = false;       // +54  L"ignore_over" / L"ignore_overy"（同址）
    bool _widthTimeScale = false;    // +55  L"width_time_scale"
    bool _ignoreRuby = false;        // +56  L"ignore_ruby"
    bool _ignoreType = false;        // +57  L"ignore_type"
    bool _ignoreFace = false;        // +58  L"ignore_face"
    bool _ignoreStyle = false;       // +59  L"ignore_style"
    bool _renderOver = false;        // +60

    // 当前样式（setFont/setStyle 改写）
    bool _curBold = false;           // +62
    bool _curShadow = false;         // +63
    bool _curEdge = false;           // +64
    bool _curItalic = false;         // +65
    // 默认样式（setDefault 改写；resetFont/Style 复位为这些）。ctor @0x5A111C：
    //   BYTE+66=0 / WORD+67=0x0001 / BYTE+69=0 → bold=0,shadow=1,edge=0,italic=0。
    bool _defaultBold = false;       // +66
    bool _defaultShadow = true;      // +67
    bool _defaultEdge = false;       // +68
    bool _defaultItalic = false;     // +69
    int _curFaceIndex = 0;           // +72  当前 face index
    int _curAlign = 0;               // +76  setStyle L"align"（当前样式 align，≠默认+100）
    int _curValign = 0;              // +80  setStyle L"valign"（当前样式 valign，≠默认+104）
    int _renderCount = 0;            // +84
    int _charBufCountdown = 0;       // +88  组合字符累积倒计数（clear 置 0）
    int _state92 = 0;                // +92  clear 置 0（杂项状态）
    // 二进制 +96 是 default face INDEX（int，setDefault 经 resolveFaceIndex 写入）。
    //   defaultFace 属性 getter/setter 经 _faceTable/resolveFaceIndex 间接读写此 index。
    int _defaultFaceIndex = 0;       // +96  setDefault L"face" → resolveFaceIndex
    int _defaultAlign = -1;          // +100 ctor=-1 @0x5A111C（QWORD+100=-1）
    int _defaultValign = -1;         // +104
    int _kinsokuUsed = 0;            // +108 本行 kinsoku 已用次数（clear 置 0）
    int _kinsokuMax = 1;             // +112 L"kinsoku_max"（bool-coerce→0/1；ctor=1 @0x5A111C）
    float _curFontSize = -1.0f;      // +116 ctor=-1.0f 脏哨兵 @0x5A111C（首次 resetFont 组复位必触发）
    float _curRubySize = -1.0f;      // +128 ctor=-1.0f 脏哨兵 @0x5A111C（resetFont `<0||!=` 门控）
    float _curRubyOffset = 0;        // +132 setFont L"rubyoffset"（当前样式 ruby 偏移）
    float _curLineSpacing = 0;       // +136
    float _curPitch = 0;             // +140
    float _curLineSize = 0;          // +144
    // ctor @0x5A111C：QWORD+148=0x4240000041C00000 → fontSize=24,bigFontSize=48；
    //   OWORD+156=(12,10,-2,6)f；OWORD+172=(0,24,1,1)f（后两 lane = timeScale/fontScale）。
    float _defaultFontSize = 24.0f;      // +148
    float _defaultBigFontSize = 48.0f;   // +152
    float _defaultSmallFontSize = 12.0f; // +156
    float _defaultRubySize = 10.0f;      // +160
    float _defaultRubyOffset = -2.0f;    // +164
    float _defaultLineSpacing = 6.0f;    // +168
    float _defaultPitch = 0;             // +172
    float _defaultLineSize = 24.0f;      // +176
    float _timeScale = 1.0f;             // +180 ctor=1.0f @0x5A111C
    float _fontScale = 1.0f;             // +184 ctor=1.0f @0x5A111C
    float _renderDelayAccum = 0;         // +188
    float _charDelayStep = 1.0f;         // +192 每字 renderPos 步进（ctor=1.0f；clear 8B 连 +188 清零）
    float _lineStartX = 0;               // +196 行首 X（换行后 pen X 复位目标；clear 置 0）
    // 当前/默认 颜色块（OWORD+216=(0xFFFFFFFF,0xFF000000,1,0xFF0080FF)）
    tjs_uint32 _curChColor = 0;          // +200
    tjs_uint32 _curShadowColor = 0;      // +204
    tjs_uint32 _curShadowDiff = 0;       // +208
    tjs_uint32 _curEdgeColor = 0;        // +212
    tjs_uint32 _defaultChColor = 0xFFFFFFFF;     // +216
    tjs_uint32 _defaultShadowColor = 0xFF000000; // +220
    tjs_uint32 _defaultShadowDiff = 1;           // +224
    tjs_uint32 _defaultEdgeColor = 0xFF0080FF;   // +228
    float _penX = 0;                 // +232 横排 pen X（竖排：列 X）
    float _penY = 0;                 // +236 横排 pen Y（竖排：行内 Y）
    float _renderSizeW = 0;          // +240
    float _renderSizeH = 0;          // +244
    float _renderLeft = 0;           // +248
    float _renderTop = 0;            // +252
    float _renderRight = 0;          // +256
    float _renderBottom = 0;         // +260
    // ruby bbox 累加器（appendChar ruby 分支写 +264/+268/+272）
    float _rubyLeft = 0;             // +264
    float _rubyTop = 0;              // +268
    float _rubyRight = 0;            // +272
    float _renderPos = 0;            // +280 当前落字累积渲染位置（renderPos 源）
    float _renderPosSnap = 0;        // +284 renderPos 快照
    int   _state288 = 0;             // +288 clear 置 0（杂项状态）
    // char 列表：二进制 +296 std::vector<charItem*>（8B 元素=堆指针）。
    std::vector<CharItem *> _charList;   // +296
    // pending 行缓冲：真对象 +320，类型 = Line（与 lineList 元素同型）。
    Line _pendingLine;               // +320..+431 (112B)
    std::vector<Line> _lineList;     // +432 (stride 112)
    std::vector<ttstr> _faceTable;   // +456 (index→face)
    std::vector<KeyWaitItem> _keyWaitList; // +480
    // 内部 UTF-16 累积 buffer（+504/+512/+520 begin/end/cap，2B 元素）。源码层 = 字符 vector。
    std::vector<tjs_char> _accumBuf; // +504
    // 当前 ruby 文本（render %... 设置，appendChar 消费后释放；clear 释放）。+528
    ttstr _curRubyText;              // +528
    bool  _hasCurRubyText = false;   // +528 != 0 哨兵
    // face hash 表（resolveFaceIndex intern：face 名→index）。二进制 +536 起 inline-bucket
    //   链式 hashmap；容器实现选型用 unordered_map（intern 表语义等价），hash 算法忠实
    //   复刻二进制内联 hash（FaceNameHash）。
    std::unordered_map<ttstr, int, FaceNameHash, FaceNameEq> _faceHash; // +536
    // render % 子码标签扫描的 cursor 回传槽（非二进制字段——二进制 render 是单函数，
    //   cursor v136 是栈局部；本地把 % 分发拆成成员函数 renderPercentTag，需把推进后的
    //   cursor 传回主循环，故引入此瞬态槽。实现细节，不进数据契约）。
    int _percentCursor = 0;

    // ============================================================
    // 构造 / 析构
    // ============================================================
    // 真构造函数 = TextRenderBase_ctor @0x5A111C。调用路径：ncbind Factory(&factory)
    //   在 TJS `new` 时 `new TextRenderBase(objthis)`（复刻构造器成员 @0x59D160 的
    //   `operator new(0x250); ctor(obj, objthis)` + createNativeInstance @0x5A6A60）。
    //   首句 `*(this+0)=objthis` → objthis 成员（裸指针、不 AddRef，见成员注释）。
    //   标量字段默认值落在各字段初始化器（逐项注 @0x5A111C）；此处补 4 个禁则集字符串
    //   常量、faceHash bucket hint 与末尾 resolveFaceIndex。
    explicit TextRenderBase(iTJSDispatch2 *objthis_)
        : objthis(objthis_), _faceHash(10) { // +0=objthis；bucket hint _M_next_bkt(0xAu) @0x5a125c
        // 内置日文禁则集 4 串（UTF-16 数据契约，逐码点复刻二进制常量；
        //   ttstr_createFromWide @0x5a1158/0x5a1168/0x5a1178/0x5a1184）。
        // following @0x14C9DF8（68 码点，行头禁则：闭括/句读/长音/拗促音等；
        //   含 U+3000 全角空格，故全部用 \u 转义书写，逐码点复刻、编码无歧义）
        _following = ttstr(
            TJS_W("%),:;]}。，、") // %),:;]}。，、
            TJS_W("．：；゛゜ヽヾゝゞ々") // ．：；゛゜ヽヾゝゞ々
            TJS_W("’”）〕］｝〉》」』") // ’”）〕］｝〉》」』
            TJS_W("】°′″℃￠％‰　!") // 】°′″℃￠％‰<U+3000>!
            TJS_W(".?・？！ーぁぃぅぇ") // .?・？！ーぁぃぅぇ
            TJS_W("ぉっゃゅょゎァィゥェ") // ぉっゃゅょゎァィゥェ
            TJS_W("ォッャュョヮヵヶ") // ォッャュョヮヵヶ
            TJS_W("")); // -> +8
        // leading @0x14C9E82（19 码点，行尾禁则：开括/通货记号；首码点 U+005C 反斜杠）
        _leading = ttstr(
            TJS_W("\\$([{‘“（〔［") // 首码点 U+005C 反斜杠须 \\ 转义，次 U+0024 '$'
            TJS_W("｛〈《「『【￥＄￡") // ｛〈《「『【￥＄￡
            TJS_W("")); // -> +16
        // begin @0x14C9EAA（10 码点，开括平衡集）
        _begin = ttstr(
            TJS_W("「『（‘“〔［｛〈《") // 「『（‘“〔［｛〈《
            TJS_W("")); // -> +24
        // end @0x14C9EC0（10 码点，闭括平衡集，与 begin 按索引一一配对）
        _end = ttstr(
            TJS_W("」』）’”〕］｝〉》") // 」』）’”〕］｝〉》
            TJS_W("")); // -> +32
        // ctor 末尾：+96 = resolveFaceIndex(L"normal")（intern 进 faceHash）@0x5a12a4
        _defaultFaceIndex = resolveFaceIndex(ttstr(TJS_W("normal")));
    }
    // dtor @0x5A6B88：Release +8/+16/+24/+32/+40 ttstr、faceHash/列表析构；**不** Release
    //   +0=objthis。本地 objthis 是裸指针、不参与 RAII；其余成员 RAII 自动析构。
    ~TextRenderBase() = default;

    // ncbind Factory @ DrawDeviceD3D.cpp:47 范例。复刻构造器成员 @0x59D160：
    //   `new(0x250); ctor(obj, objthis)`——objthis 由 ncbind invoker 直接注入工厂。
    static tjs_error factory(TextRenderBase **result, tjs_int /*numparams*/,
                             tTJSVariant ** /*params*/, iTJSDispatch2 *objthis) {
        *result = new TextRenderBase(objthis);
        return TJS_S_OK;
    }

    // ============================================================
    // Property accessors（RW/RO 拆分严格按二进制 setter==0 判定，§2）
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
    // defaultFace @0x5A0DA8(get) / 0x5A0E0C(set)：**INDEX-based**（后备字段是
    //   _defaultFaceIndex/+96）。getter 查 _faceTable[+96]（越界→空串 byte_1506A57）；
    //   setter 经 resolveFaceIndex 写 +96。1:1 复刻反编译。
    ttstr get_defaultFace() const { // 0x5A0DA8
        unsigned int idx = (unsigned int)_defaultFaceIndex; // +96
        if((unsigned int)_faceTable.size() <= idx) // OOB
            return ttstr();                         // sub_A13878(&byte_1506A57)
        return _faceTable[idx];                     // _faceTable[+96]
    }
    void set_defaultFace(ttstr v) { // 0x5A0E0C
        _defaultFaceIndex = resolveFaceIndex(v); // +96 = sub_5A14DC(name)
    }
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
    // maxScrollLine @0x5A1080：从视口尺寸(竖排 +240/横排 +244)起，自最后一行向前逐行
    //   减去 lineHeight(lineItem +84)，统计能从底部完整容纳的行号。1:1 复刻反编译。
    double get_maxScrollLine() const { // 0x5A1080
        int count = (int)_lineList.size(); // v2 = (+440-+432)/112
        float result = 0.0f;
        if(count >= 1) {
            int v4 = 0;
            // v6 = 视口尺寸（竖排 renderSizeW，横排 renderSizeH）
            float v6 = _vertical ? _renderSizeW : _renderSizeH; // *v5
            int idx = count - 1; // i 起点：最后一行的 lineHeight(+84)
            for(;;) {
                v6 = v6 - _lineList[idx].lineHeight; // v6 -= *i
                if(v6 < 0.0f)
                    break;
                --v4;
                if(count + v4 <= 0)
                    return 1.0; // 全部容纳 → 1.0
                --idx;          // i -= 28 floats (-112B) → 前一行
            }
            if(v4 != 0)
                return (float)(count + v4); // return count+v4
        }
        return result; // 0.0
    }

#undef TR_RW
#undef TR_RO

    // ============================================================
    // NCB typed 方法（实例方法，经 ncbind invoker 模板分发；§10 方案 A）
    //   numparams/-1004(BADPARAMCOUNT)/-1008(NATIVECLASSCRASH)/result-Clear 由 ncbind
    //   invoker 模板自然产出（本地 doInvoke @ncbind.hpp:1178 `numparams<ArgsCount→-1004`
    //   与二进制 invoker sub_5A71E0/5A76EC 序列一致）。
    // ============================================================
    // setRenderSize @0x59EB70：写 +240/+244 后调 clear。
    void setRenderSize(float w, float h) { // 0x59EB70
        _renderSizeW = w; // +240
        _renderSizeH = h; // +244
        clear(); // sub_59EC6C
    }
    // clear @0x59EC6C — 复位全部渲染状态、重建列表、压缩 face 表为仅 default face。
    //   数据流 1:1 复刻反编译。clear→resetFont 可触发 onStyleChanged（读 objthis 成员）。
    void clear() { // 0x59EC6C
        // sub_5A1E68(+320)：pending Line 清空（deque 清空 + +400..+428 metric 全零化）。
        _pendingLine.clear();
        if(_vertical) { // +48
            // 竖排：pen X / left / right / pending 行 bbox left/right = renderSizeW(+240)
            _penX = _renderSizeW;        // +232
            _renderLeft = _renderSizeW;  // +248
            _renderRight = _renderSizeW; // +256
            _pendingLine.bboxRight = _renderSizeW; // +416
            _pendingLine.bboxLeft = _renderSizeW;  // +408
        } else {
            // 横排：bbox left/right 不写，保持 Line::clear 后的 0
            _penX = 0;        // +232
            _renderLeft = 0;  // +248
            _renderRight = 0; // +256
        }
        _penY = 0;          // +236
        _renderTop = 0;     // +252
        _renderBottom = 0;  // +260
        _kinsokuUsed = 0;   // +108
        releaseCurRubyText(); // +528 release → null
        _charBufCountdown = 0; // +88
        _lineStartX = 0;       // +196
        _accumBuf.clear();     // +512 = +504
        resetFont(); // sub_59EEE0
        // 当前样式从 default 复位（+140/+144/+136/+76/+80 ← +172/+176/+168/+100/+104）
        _curPitch = _defaultPitch;             // +140
        _curLineSize = _defaultLineSize;       // +144
        _curLineSpacing = _defaultLineSpacing; // +136
        _curAlign = _defaultAlign;             // +76
        _curValign = _defaultValign;           // +80
        // 行列表清空（析构每 lineItem 的嵌套 deque）+440=+432
        _lineList.clear();
        // charList 清空（+304=+296）；元素是堆 charItem*，由 lineItem deque 拥有，此处不 delete
        _charList.clear();
        _renderPos = 0;       // +280  STR XZR,[#0x118] 8B：+280/+284 一并清零
        _renderPosSnap = 0;   // +284  （同上一条 8B store）
        _state288 = 0;        // +288
        _renderDelayAccum = 0; // +188 STUR XZR,[#0xBC] 8B：+188/+192 一并清零
        _charDelayStep = 0;   // +192  （同上一条 8B store）
        _state92 = 0;         // +92
        _renderCount = 0;     // +84
        _renderOver = false;  // +60
        // keyWait 列表清空（+488=+480）
        _keyWaitList.clear();
        _renderText = ttstr(); // +40 release → null
        // face 表压缩：取旧 default face name → 清表 → 重 intern → 写回 +96
        ttstr defFaceName;
        if(_defaultFaceIndex >= 0 &&
           _defaultFaceIndex < (int)_faceTable.size())
            defFaceName = _faceTable[_defaultFaceIndex]; // a1+96 索引旧表
        // 二进制越界 → byte_1506A57 = L""（空串）
        _faceHash.clear();   // +536 inline-bucket hashmap 清空
        _faceTable.clear();  // +456/+464 release 所有 ttstr
        _defaultFaceIndex = resolveFaceIndex(defFaceName); // +96 = sub_5A14DC
    }
    // resetFont @0x59EEE0：当前样式从 default* 复位。三路变化检测命中 → 全组复位 +
    //   onStyleChanged（读 objthis 成员）；rubySize 单独门控；其余无条件复位。
    void resetFont() { // 0x59EEE0
        int defFace = _defaultFaceIndex; // v2 = +96
        bool doGroup;
        if(_curFaceIndex != defFace)             // +72 != v2
            doGroup = true;
        else if(_curBold != _defaultBold)        // +62 != +66
            doGroup = true;
        else if(_curItalic != _defaultItalic     // +65 != +69
                || _defaultFontSize != _curFontSize) // +148 != +116
            doGroup = true;
        else
            doGroup = false;                     // → LABEL_9（无 onStyleChanged）
        if(doGroup) { // LABEL_8
            _curFontSize = _defaultFontSize;     // +116 = +148
            _curFaceIndex = defFace;             // +72 = v2
            _curBold = _defaultBold;             // *v3 = v4(=+66)
            _curItalic = _defaultItalic;         // +65 = +69
            onStyleChanged();                    // sub_5A1F28
        }
        // LABEL_9：rubySize 门控复位（curRubySize<0 || defaultRubySize != curRubySize）
        if(_curRubySize < 0.0f || _defaultRubySize != _curRubySize) // +128
            _curRubySize = _defaultRubySize;     // +128 = +160
        // LABEL_14：无条件复位
        _curRubyOffset = _defaultRubyOffset;     // +132 = +164
        _curShadow = _defaultShadow;             // +63 = +67
        _curEdge = _defaultEdge;                 // +64 = +68
        // 4 色 DWORD 块（二进制 16B q-reg 块拷 +200..215 ← +216..231）
        _curChColor = _defaultChColor;           // +200 = +216
        _curShadowColor = _defaultShadowColor;   // +204 = +220
        _curShadowDiff = _defaultShadowDiff;     // +208 = +224
        _curEdgeColor = _defaultEdgeColor;       // +212 = +228
    }
    // resetStyle @0x59EFBC：5 个字段从 default 复位。**不调 resetFont、无 onStyleChanged**。
    void resetStyle() { // 0x59EFBC
        _curLineSpacing = _defaultLineSpacing; // +136 = +168
        _curPitch = _defaultPitch;             // +140 = +172
        _curLineSize = _defaultLineSize;       // +144 = +176
        _curAlign = _defaultAlign;             // +76 = +100
        _curValign = _defaultValign;           // +80 = +104
    }
    // newline @0x59FECC：pending deque 非空（finish.cur(+368) != start.cur(+336)）→ finishLine。
    void newline() { // 0x59FECC
        if(!_pendingLine.chars.empty()) // a1+368 != a1+336
            finishLine();               // sub_5A34B8
    }
    // done @0x59FEE4：终结布局。① pending 非空 → finishLine ② 遍历行列表算全局 bbox
    //   ③ valign 偏移加到每 char.y + 调整全局 top/bottom ④ charList 从各行 deque 铺
    //   charItem 指针 ⑤ keyWait 列表 index→renderPos 回填 ⑥ charList 按 renderPos 排序。
    void done() { // 0x59FEE4
        // ① pending 非空 → finishLine（a1[46]!=a1[42] = +368!=+336）
        if(!_pendingLine.chars.empty())
            finishLine(); // sub_5A34B8
        // ② 全局 bbox（遍历 lineList，stride 112；读 lineItem bbox float[22..25]）
        for(const Line &li : _lineList) {
            if(_renderTop > li.bboxTop)       // *v1+63(+252) > v5[23]
                _renderTop = li.bboxTop;
            if(_renderBottom < li.bboxBottom) // *v1+65(+260) < v5[25]
                _renderBottom = li.bboxBottom;
            if(_renderLeft > li.bboxLeft)     // *v1+62(+248) > v5[22]
                _renderLeft = li.bboxLeft;
            if(_renderRight < li.bboxRight)   // *v1+64(+256) < v5[24]
                _renderRight = li.bboxRight;
        }
        // ③ valign 偏移（!vertical）：v10 = _curValign(+80)；v11 = renderBottom(+260)
        if(!_vertical) {
            int v10 = _curValign; // *(v1+20) = +80
            float v11 = _renderBottom; // *(v1+65) = +260
            int v12;
            if(v10 == 1) // bottom align：renderSizeH - renderBottom
                v12 = (int)(float)(_renderSizeH - v11); // *(v1+61)=+244 - v11
            else if(v10 == 0) // center
                v12 = (int)(float)((float)(_renderSizeH - v11) * 0.5f);
            else // 其它：无偏移
                v12 = 0;
            if(v12 != 0) { // LABEL_18：把 v12 加到每行每 char.y(+12)
                for(Line &li : _lineList)
                    for(CharItem &ci : li.chars)
                        ci.y = ci.y + (float)v12; // *(char+12) += v12
            }
            float v19 = _renderTop;         // *(v1+63) = +252
            _renderBottom = v11 + (float)v12; // *(v1+65) = renderBottom + v12
            _renderTop = v19 + (float)v12;    // *(v1+63) = renderTop + v12
        }
        // ④ charList 重建：从各行 deque 铺 charItem 指针（+304=+296 后逐 push）
        _charList.clear();
        for(Line &li : _lineList)
            for(CharItem &ci : li.chars)
                _charList.push_back(&ci); // 指向行 deque 内元素（deque 元素地址稳定）
        // ⑤ keyWait 列表 time 段回填（done @0x59FEE4 keyWait 循环 0x5a0180..0x5a020c）：
        //   v39[1] = *(int*)(charList[v39[0]] + 24)。读 index(低 int) 索引 charList，
        //   把该 char.renderPos(+24) 的 float bits 写入 time(高 int)；index 不动。
        //   二进制无边界检查（脚本保证 index 有效），本地加守护避免越界。
        for(size_t i = 0; i < _keyWaitList.size(); ++i) {
            int idx = _keyWaitList[i].index; // v39[0]（低 int）
            if(idx >= 0 && idx < (int)_charList.size())
                _keyWaitList[i].time =
                    reinterpretFloatBits(_charList[idx]->renderPos); // v39[1]
        }
        // ⑥ charList 按 charItem.renderPos(+24) 升序排序（sub_5A59E8 introsort +
        //   sub_5A5C34 insertion sort，比较键 = char+24）。二进制是**非稳定** introsort；
        //   本地用 std::sort（libstdc++ std::sort 即 introsort）。
        std::sort(_charList.begin(), _charList.end(),
                         [](const CharItem *a, const CharItem *b) {
                             return a->renderPos < b->renderPos;
                         });
    }
    // calcLineOffset @0x5A05FC：返回行列表第 lineIdx 项的 offset(+80)；越界返回 bottom。
    double calcLineOffset(tjs_int lineIdx) { // 0x5A05FC
        // count = (+440 - +432) / 112；二进制 unsigned 比较 lineCount <= (u64)lineIdx
        if((tjs_uint64)_lineList.size() <= (tjs_uint64)(tjs_uint32)lineIdx)
            return _renderBottom; // a1+260 (§4)
        return _lineList[lineIdx].lineBottom; // +432 + 112*idx + 80 (lineItem float[20])
    }
    // calcShowCount @0x5A0644：char 列表(+296)倒扫，找在给定 width 内可显示的字符数。
    tjs_int calcShowCount(tjs_int width) { // 0x5A0644
        tjs_int count = (tjs_int)_charList.size();
        if(count - 1 < 1) // count <= 1
            return 0;
        float ts = _timeScale; // +180
        tjs_int v6 = count - 1; // 末项索引
        tjs_int result = count;
        // 二进制 while 条件用 charItem +24 (renderPos)；元素是 charItem* 指针。
        while((float)(_charList[v6]->renderPos * ts) > (float)width) {
            bool atHead = (v6 <= 1);
            --v6;
            --result;
            if(atHead)
                return 0;
        }
        return result;
    }

    // ============================================================
    // dict 解析方法（setOption/setDefault/setFont/setStyle）。
    //   §10 方案 A item5：dict 参数经 ncbind 封送为 tTJSVariant，方法体内自己取 dispatch，
    //   按二进制顺序写显式 AddRef/Release（sub_A0F5E0 拷贝 AsObject @0x59d2f8 +
    //   函数尾 Release @0x59ddb4），不用 RAII 守护。type!=Object → sub_A0E48C(,1)=
    //   TJSThrowVariantConvertError(Object)。invoker = ncbind 1-arg(tTJSVariant) =
    //   sub_5A71E0 同构（numparams<1→-1004、objthis/native null→-1008）。
    // ============================================================
    // setOption @0x59D2AC：(dict) → 选项 byte 字段 +48..+59,+112 + 禁则字符串 +8..+32。
    void setOption(tTJSVariant dictVar) { // 0x59D2AC
        if(dictVar.Type() != tvtObject)
            TJSThrowVariantConvertError(dictVar, tvtObject); // sub_A0E48C(,1u) @0x59d2e4
        iTJSDispatch2 *dict = dictVar.AsObject(); // AddRef @0x59d2f8
        tTJSVariant v;
        // --- 禁则字符串：string/void（store ttstr，见 §3b +8/+16/+24/+32）---
        setOptionStr(dict, TJS_W("following"), _following);
        setOptionStr(dict, TJS_W("leading"), _leading);
        setOptionStr(dict, TJS_W("begin"), _begin);
        setOptionStr(dict, TJS_W("end"), _end);
        // --- byte/DWORD 选项（boolCoerce）：顺序同二进制 ---
        if(dictGet(dict, TJS_W("vertical"), &v))
            _vertical = boolCoerce(v);          // +48
        if(dictGet(dict, TJS_W("kinsoku_max"), &v))
            _kinsokuMax = boolCoerce(v) ? 1 : 0; // +112 DWORD (bool-coerce)
        if(dictGet(dict, TJS_W("word_break"), &v))
            _wordBreak = boolCoerce(v);         // +49
        if(dictGet(dict, TJS_W("ignore_color"), &v))
            _ignoreColor = boolCoerce(v);       // +50
        if(dictGet(dict, TJS_W("ignore_size"), &v))
            _ignoreSize = boolCoerce(v);        // +51
        if(dictGet(dict, TJS_W("ignore_delay"), &v))
            _ignoreDelay = boolCoerce(v);       // +52
        if(dictGet(dict, TJS_W("ignore_over"), &v))
            _ignoreOverY = boolCoerce(v);       // +54 (ignore_over)
        if(dictGet(dict, TJS_W("ignore_overy"), &v))
            _ignoreOverY = boolCoerce(v);       // +54 (ignore_overy 覆盖同址)
        if(dictGet(dict, TJS_W("ignore_overx"), &v))
            _ignoreOverX = boolCoerce(v);       // +53
        if(dictGet(dict, TJS_W("width_time_scale"), &v))
            _widthTimeScale = boolCoerce(v);    // +55
        if(dictGet(dict, TJS_W("ignore_ruby"), &v))
            _ignoreRuby = boolCoerce(v);        // +56
        if(dictGet(dict, TJS_W("ignore_type"), &v))
            _ignoreType = boolCoerce(v);        // +57
        if(dictGet(dict, TJS_W("ignore_face"), &v))
            _ignoreFace = boolCoerce(v);        // +58
        if(dictGet(dict, TJS_W("ignore_style"), &v))
            _ignoreStyle = boolCoerce(v);       // +59
        if(dict)
            dict->Release(); // 尾 Release @0x59ddb4
    }
    // following/leading/begin/end 的 string/void 存值（setOption@0x59D2AC：
    //   octet/int/real 与 object → sub_A0E48C(v57, 2u)=TJSThrowVariantConvertError(String)；
    //   string(2)→store ttstr；void(0)→空串）。
    void setOptionStr(iTJSDispatch2 *dict, const tjs_char *key, ttstr &field) {
        tTJSVariant v;
        if(!dictGet(dict, key, &v))
            return; // key 不存在 → 不动该字段（二进制同样跳过）
        switch(v.Type()) {
        case tvtString:
            field = v.AsStringNoAddRef() ? ttstr(v) : ttstr();
            break;
        case tvtVoid:
            field = ttstr(); // 空串（二进制 v5=nullptr）
            break;
        default:
            // object/octet/int/real → ConvertError(String)（sub_A0E48C(,2)）
            TJSThrowVariantConvertError(v, tvtString);
            break;
        }
    }

    // setDefault @0x59DEA8：(dict) → default* 字段（face/字号族/颜色族/对齐/间距）。
    void setDefault(tTJSVariant dictVar) { // 0x59DEA8
        if(dictVar.Type() != tvtObject)
            TJSThrowVariantConvertError(dictVar, tvtObject); // sub_A0E48C(,1u)
        iTJSDispatch2 *dict = dictVar.AsObject(); // AddRef
        tTJSVariant v;
        // face → resolveFaceIndex → +96。类型分发：octet/int/real 与 object →
        //   sub_A0E48C(,2)=ConvertError(String)；string→取值；void→空串。
        if(dictGet(dict, TJS_W("face"), &v)) {
            ttstr faceName;
            if(v.Type() == tvtString)
                faceName = ttstr(v);
            else if(v.Type() == tvtVoid)
                faceName = ttstr(); // v53 = nullptr
            else
                TJSThrowVariantConvertError(v, tvtString); // sub_A0E48C(,2)
            _defaultFaceIndex = resolveFaceIndex(faceName); // +96
        }
        if(dictGet(dict, TJS_W("bold"), &v))
            _defaultBold = boolCoerce(v);                 // +66
        // fontsize 分支：存在→+148，并把缺省 big/small/ruby 字号回填为 fontsize 值
        bool hasFontsize = dictGet(dict, TJS_W("fontsize"), &v);
        if(hasFontsize) {
            _defaultFontSize = realCoerce(v);             // +148
            tTJSVariant tmp;
            if(!dictGet(dict, TJS_W("bigfontsize"), &tmp))
                _defaultBigFontSize = _defaultFontSize;   // +152=+148
            if(!dictGet(dict, TJS_W("smallfontsize"), &tmp))
                _defaultSmallFontSize = _defaultFontSize; // +156=+148
            if(!dictGet(dict, TJS_W("rubysize"), &tmp))
                _defaultRubySize = _defaultFontSize;      // +160=+148
        } else {
            // fontsize 缺失：独立读 big/small/ruby（缺失则保持默认 0.0，二进制 v=0.0）
            if(dictGet(dict, TJS_W("bigfontsize"), &v))
                _defaultBigFontSize = realCoerce(v);      // +152
            if(dictGet(dict, TJS_W("smallfontsize"), &v))
                _defaultSmallFontSize = realCoerce(v);    // +156
            if(dictGet(dict, TJS_W("rubysize"), &v))
                _defaultRubySize = realCoerce(v);         // +160
        }
        if(dictGet(dict, TJS_W("rubyoffset"), &v))
            _defaultRubyOffset = realCoerce(v);           // +164
        if(dictGet(dict, TJS_W("color"), &v))
            _defaultChColor = (tjs_uint32)intCoerce(v);   // +216
        if(dictGet(dict, TJS_W("shadow"), &v))
            _defaultShadow = boolCoerce(v);               // +67
        if(dictGet(dict, TJS_W("shadowcolor"), &v))
            _defaultShadowColor = (tjs_uint32)intCoerce(v); // +220
        if(dictGet(dict, TJS_W("shadowdiff"), &v))
            _defaultShadowDiff = (tjs_uint32)intCoerce(v);  // +224
        if(dictGet(dict, TJS_W("edge"), &v))
            _defaultEdge = boolCoerce(v);                 // +68
        if(dictGet(dict, TJS_W("edgecolor"), &v))
            _defaultEdgeColor = (tjs_uint32)intCoerce(v); // +228
        if(dictGet(dict, TJS_W("linespacing"), &v))
            _defaultLineSpacing = realCoerce(v);          // +168
        if(dictGet(dict, TJS_W("pitch"), &v))
            _defaultPitch = realCoerce(v);                // +172
        // linesize：存在→+176；缺失则回退读 fontsize key（二进制 LABEL_25 共享）
        if(dictGet(dict, TJS_W("linesize"), &v))
            _defaultLineSize = realCoerce(v);             // +176
        else if(dictGet(dict, TJS_W("fontsize"), &v))
            _defaultLineSize = realCoerce(v);             // +176 (fallback)
        if(dictGet(dict, TJS_W("align"), &v))
            _defaultAlign = intCoerce(v);                 // +100
        if(dictGet(dict, TJS_W("valign"), &v))
            _defaultValign = intCoerce(v);                // +104
        if(dict)
            dict->Release(); // 尾 Release
    }

    // setFont @0x59EFD8：(dict) → 当前样式字段；变化则调 onStyleChanged@0x5A1F28。
    void setFont(tTJSVariant dictVar) { // 0x59EFD8
        if(dictVar.Type() != tvtObject)
            TJSThrowVariantConvertError(dictVar, tvtObject); // sub_A0E48C(,1u)
        iTJSDispatch2 *dict = dictVar.AsObject(); // AddRef
        tTJSVariant v;
        bool changed = false; // v5
        // face：present→resolveFaceIndex；idx 变则更新 +72 并 changed=true。
        if(dictGet(dict, TJS_W("face"), &v)) {
            ttstr faceName;
            if(v.Type() == tvtString)
                faceName = ttstr(v);
            else if(v.Type() == tvtVoid)
                faceName = ttstr(); // v27 = nullptr
            else
                TJSThrowVariantConvertError(v, tvtString); // sub_A0E48C(,2)
            int idx = resolveFaceIndex(faceName);
            if(_curFaceIndex != idx) {
                _curFaceIndex = idx; // +72
                changed = true;
            }
        }
        if(dictGet(dict, TJS_W("bold"), &v)) {
            bool b = boolCoerce(v);
            if(_curBold != b) { // +62
                _curBold = b;
                changed = true;
            }
        }
        if(dictGet(dict, TJS_W("fontsize"), &v)) {
            float f = realCoerce(v);
            if(_curFontSize < 0.0f || _curFontSize != f) { // +116 脏哨兵
                _curFontSize = f;
                changed = true;
            }
        }
        if(dictGet(dict, TJS_W("rubysize"), &v)) {
            float f = realCoerce(v);
            if(_curRubySize < 0.0f || _curRubySize != f) // +128 (无 changed)
                _curRubySize = f;
        }
        if(dictGet(dict, TJS_W("rubyoffset"), &v))
            _curRubyOffset = realCoerce(v);                 // +132 无条件
        if(dictGet(dict, TJS_W("color"), &v))
            _curChColor = (tjs_uint32)intCoerce(v);         // +200
        if(dictGet(dict, TJS_W("shadow"), &v))
            _curShadow = boolCoerce(v);                     // +63
        if(dictGet(dict, TJS_W("shadowcolor"), &v))
            _curShadowColor = (tjs_uint32)intCoerce(v);     // +204
        if(dictGet(dict, TJS_W("shadowdiff"), &v))
            _curShadowDiff = (tjs_uint32)intCoerce(v);      // +208
        if(dictGet(dict, TJS_W("edge"), &v))
            _curEdge = boolCoerce(v);                       // +64
        if(dictGet(dict, TJS_W("edgecolor"), &v))
            _curEdgeColor = (tjs_uint32)intCoerce(v);       // +212
        if(changed)
            onStyleChanged(); // sub_5A1F28
        if(dict)
            dict->Release(); // 尾 Release
    }

    // setStyle @0x59F7AC：(dict) → 当前样式间距/对齐（不读 font 系键、不调 onStyleChanged）。
    void setStyle(tTJSVariant dictVar) { // 0x59F7AC
        if(dictVar.Type() != tvtObject)
            TJSThrowVariantConvertError(dictVar, tvtObject); // sub_A0E48C(,1u)
        iTJSDispatch2 *dict = dictVar.AsObject(); // AddRef
        tTJSVariant v;
        if(dictGet(dict, TJS_W("linespacing"), &v))
            _curLineSpacing = realCoerce(v);  // +136
        if(dictGet(dict, TJS_W("pitch"), &v))
            _curPitch = realCoerce(v);        // +140
        if(dictGet(dict, TJS_W("linesize"), &v))
            _curLineSize = realCoerce(v);     // +144
        else if(dictGet(dict, TJS_W("fontsize"), &v))
            _curLineSize = realCoerce(v);     // +144 (fallback fontsize key)
        if(dictGet(dict, TJS_W("align"), &v))
            _curAlign = intCoerce(v);         // +76
        if(dictGet(dict, TJS_W("valign"), &v))
            _curValign = intCoerce(v);        // +80
        if(dict)
            dict->Release(); // 尾 Release
    }

    // ============================================================
    // onEval / getKeyWait / getCharacters（NCB typed 查询/eval 方法）
    // ============================================================
    // onEval @0x5A0294：(expr) → TJS eval（在 objthis 上下文求值表达式，结果写 result）。
    //   二进制：*(result+16)=0（Clear）→ sub_8E3FA4(expr, *a1=objthis, result)。
    //   *a1 = native[0] = objthis（= 本地 objthis 成员）。返回 result variant。
    //   参数封送在 invoker 层（0x5A7904 经 0x5A7B28=sub_A0BAF4 variant→ttstr 后才调
    //   方法）——本地签名收 ttstr，由 ncbind 同款 convertor 在 invoker 层转换，同拓扑。
    tTJSVariant onEval(ttstr expr) { // 0x5A0294
        tTJSVariant result; // result.type=0（构造即 void）
        // TVPExecuteExpression(expr, context=objthis, result) = sub_8E3FA4
        TVPExecuteExpression(expr, objthis, &result);
        return result;
    }

    // getKeyWait @0x5A02DC：() → TJS Array of dict{pos, time}，源自 _keyWaitList(+480)。
    //   pos/time 均为低 int(index)（renderPos bits 高 int 不被 getKeyWait 读取）。
    tTJSVariant getKeyWait() { // 0x5A02DC
        iTJSDispatch2 *arr = TJSCreateArrayObject(); // sub_9876D4(0)
        // 0x5a0338 vtbl+200 NativeInstanceSupport(GETINSTANCE, ArrayClassID, &ni)
        //   ——取出的 ni 后续不消费（append 全走 dispatch），dead-but-faithful
        //   源码 token（同款 idiom 见 tjsArray.cpp:1338-1341）。
        tTJSArrayNI *ni = nullptr;
        arr->NativeInstanceSupport(TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
                                   (iTJSNativeInstance **)&ni);
        for(size_t i = 0; i < _keyWaitList.size(); ++i) {
            int v13 = _keyWaitList[i].index; // LDRSW 低 int（pos 与 time 同值）
            iTJSDispatch2 *dict = TJSCreateDictionaryObject(); // sub_9C8440(0)
            tTJSVariant vPos((tjs_int)v13);  // v16=4 Integer
            dict->PropSet(TJS_MEMBERENSURE, TJS_W("pos"), nullptr, &vPos, dict);
            tTJSVariant vTime((tjs_int)v13); // v16=4 Integer（同 v13）
            dict->PropSet(TJS_MEMBERENSURE, TJS_W("time"), nullptr, &vTime, dict);
            // add dict 到数组（二进制 FuncCall L"add"）。
            //   arg variant {Object=dict, ObjThis=null}（0x5a042c v16=1 /
            //   0x5a0430 v15[0]=v12, v15[1]=0）——objthis 槽为 null。
            tTJSVariant vDict(dict, (iTJSDispatch2 *)nullptr);
            dict->Release();
            tTJSVariant *args[1] = { &vDict };
            arr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, args, arr);
        }
        // result variant {Object=arr, ObjThis=null}（0x5a04c8 *(a2+16)=1 /
        //   0x5a04cc *a2=v4, *(a2+8)=0）——objthis 槽为 null。
        tTJSVariant result(arr, (iTJSDispatch2 *)nullptr);
        arr->Release();
        return result;
    }

    // getCharacters @0x5A0694：(int start, int count) → TJS Array of per-char dict。
    //   count==0(!a3) → count = renderCount(+84) - start。clamp → 越界守护 → 逐 char 建 dict。
    //   ncbind typed 2-arg invoker（numparams<2→-1004，count 缺省=void→0=binary `!a3` 路径）。
    tTJSVariant getCharacters(tjs_int start, tjs_int count) { // 0x5A0694
        iTJSDispatch2 *arr = TJSCreateArrayObject(); // sub_9876D4(0)
        // 0x5a0708 vtbl+200 NativeInstanceSupport(GETINSTANCE, ArrayClassID, &ni)
        //   ——二进制 0x5a0714 还取 v34=&ni->Items，二者均不被后续消费（append 全走
        //   dispatch），dead-but-faithful 源码 token（idiom 同 tjsArray.cpp:1338-1341）。
        tTJSArrayNI *ni = nullptr;
        arr->NativeInstanceSupport(TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
                                   (iTJSNativeInstance **)&ni);
        // count==0 → renderCount - start（+84）
        if(!count)
            count = _renderCount - start; // a3 = *(a1+84) - a2
        int charListCount = (int)_charList.size(); // (+304-+296)>>3 = v12
        int v13;
        if(count + start <= charListCount)
            v13 = count;
        else
            v13 = charListCount - start; // v12 - a2
        if(v13 >= 1) {
            int v15 = -1;     // 上一 faceIndex（-1 = 强制首次查）
            ttstr faceName;   // v30：缓存的 face 名
            for(int v14 = 0; v14 < v13; ++v14) {
                int srcIdx = v14 + start;
                if(srcIdx < 0 || srcIdx >= charListCount)
                    break; // 守护（二进制无界，脚本保证有效）
                CharItem *ci = _charList[srcIdx]; // charList[v14+a2]
                // face 缓存刷新（faceIndex 变化时重查 _faceTable）
                int fi = ci->faceIndex; // v17+52
                if(v15 != fi) {
                    if(fi < 0 || fi >= (int)_faceTable.size())
                        faceName = ttstr(); // sub_A13878(&byte_1506A57) 空串
                    else
                        faceName = _faceTable[fi]; // _faceTable[faceIndex]
                    v15 = fi;
                }
                iTJSDispatch2 *dict = TJSCreateDictionaryObject(); // sub_9C8440(0)
                // 首字段 graph（+40 byte→Integer，sub_5A2160 @0x5a081c，先于 text）
                trDictSetInt(dict, TJS_W("graph"), ci->graph ? 1 : 0); // +40
                trDictSetStr(dict, TJS_W("text"), ci->text);         // +0
                trDictSetReal(dict, TJS_W("x"), ci->x);              // +8
                trDictSetReal(dict, TJS_W("y"), ci->y);              // +12
                trDictSetReal(dict, TJS_W("cw"), ci->cw);            // +16
                trDictSetReal(dict, TJS_W("size"), ci->size);        // +20
                trDictSetStr(dict, TJS_W("face"), faceName);         // 缓存名
                // color/shadowColor/edgeColor：二进制 0x5a092c/0x5a0a04/0x5a0a7c
                //   `LDR W8`（*(unsigned int*)）= u32 **零扩展**进 tvtInteger，
                //   0xFF000000 以正值 4278190080 暴露给脚本——勿加 (int) 符号扩展。
                trDictSetInt(dict, TJS_W("color"), ci->chColor); // +28 零扩展 @0x5a092c
                trDictSetInt(dict, TJS_W("bold"), ci->bold ? 1 : 0);   // +41
                trDictSetInt(dict, TJS_W("italic"), ci->italic ? 1 : 0); // +42
                trDictSetInt(dict, TJS_W("shadow"), ci->shadow ? 1 : 0); // +43
                trDictSetInt(dict, TJS_W("edge"), ci->edge ? 1 : 0);     // +44
                trDictSetInt(dict, TJS_W("shadowColor"),
                             ci->shadowColor); // +32 零扩展 @0x5a0a04
                trDictSetInt(dict, TJS_W("shadowDiff"),
                             (int)ci->shadowDiff); // +48 走 sub_5A6020(int*) @0x5a0a74
                                                   //   = **符号扩展**，(int) 必须保留
                trDictSetInt(dict, TJS_W("edgeColor"),
                             ci->edgeColor); // +36 零扩展 @0x5a0a7c
                // ruby（仅 +56 != +64，即 ruby vector 非空）→ 子 Array
                if(!ci->ruby.empty()) // *(v17+56) != *(v17+64)
                    trDictSetRubyArray(dict, ci->ruby);
                trDictSetInt(dict, TJS_W("vertical"),
                             ci->vertical ? 1 : 0);          // +45
                trDictSetReal(dict, TJS_W("delay"), ci->renderPos); // +24
                // 落入数组：PropSetByNum(index=v14)（sub_5A6550）
                tTJSVariant vDict(dict, dict);
                dict->Release();
                arr->PropSetByNum(TJS_MEMBERENSURE, v14, &vDict, arr);
            }
        }
        tTJSVariant result(arr, arr);
        arr->Release();
        return result;
    }
    // getCharacters/getKeyWait dict 字段写 helper。
    static void trDictSetStr(iTJSDispatch2 *dict, const tjs_char *key,
                             const ttstr &val) { // sub_A0FE2C + vtable+48
        tTJSVariant v(val);
        dict->PropSet(TJS_MEMBERENSURE, key, nullptr, &v, dict);
    }
    static void trDictSetReal(iTJSDispatch2 *dict, const tjs_char *key,
                              float val) { // sub_5A614C (type 5 Real)
        tTJSVariant v((tjs_real)val);
        dict->PropSet(TJS_MEMBERENSURE, key, nullptr, &v, dict);
    }
    static void trDictSetInt(iTJSDispatch2 *dict, const tjs_char *key,
                             tjs_int64 val) { // sub_5A2160/5A6020/A0FB64 (type 4 Integer)
        // 参数取 tjs_int64：variant Integer 槽本就 64 位，符号/零扩展由调用点的
        //   成员读取转换决定，对应二进制在 load 指令处定扩展方式
        //   （LDRB/LDRSW 符号路径 vs LDR W8 零扩展路径，见 color 三键注释）。
        tTJSVariant v(val);
        dict->PropSet(TJS_MEMBERENSURE, key, nullptr, &v, dict);
    }
    // ruby 子 Array（sub_5A6240@0x5A6240）：每 RubyItem → dict{text,x,y,size}。
    static void trDictSetRubyArray(iTJSDispatch2 *dict,
                                   const std::vector<RubyItem> &ruby) { // sub_5A6240
        iTJSDispatch2 *arr = TJSCreateArrayObject(); // sub_9876D4(0)
        // 0x5a62b0 vtbl+200 NativeInstanceSupport(GETINSTANCE, ArrayClassID, &ni)
        //   ——二进制 0x5a62bc 取 v18=&ni->Items，均不被后续消费，dead-but-faithful
        //   源码 token（idiom 同 tjsArray.cpp:1338-1341）。
        tTJSArrayNI *ni = nullptr;
        arr->NativeInstanceSupport(TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
                                   (iTJSNativeInstance **)&ni);
        for(size_t i = 0; i < ruby.size(); ++i) {
            const RubyItem &r = ruby[i];
            iTJSDispatch2 *rd = TJSCreateDictionaryObject(); // sub_9C8440(0)
            trDictSetStr(rd, TJS_W("text"), r.text); // +0
            trDictSetReal(rd, TJS_W("x"), r.x);      // +8
            trDictSetReal(rd, TJS_W("y"), r.y);      // +12
            trDictSetReal(rd, TJS_W("size"), r.span); // +16
            tTJSVariant vd(rd, rd);
            rd->Release();
            arr->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)i, &vd, arr);
        }
        tTJSVariant vArr(arr, arr);
        arr->Release();
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("ruby"), nullptr, &vArr, dict);
    }

    // ============================================================
    // render NCB 包装 @0x59FC28（bespoke 封送 Process → raw callback）。
    //   二进制 render 槽（off_1A0BE48 slot2）= 共享 raw 包装模板 sub_5A77F4
    //   （membername→-1001、!objthis→-1008、result Clear、GETINSTANCE 失败→-1008、
    //   然后调 Process(result, numparams, params, native)）；Process @0x59FC28 才是
    //   手写封送体（numparams<3→-1004、param[3] 仅 AsReal 强制后丢弃、param[4]
    //   boolCoerce）。本地第 4 形参取 TextRenderBase*，NCB_METHOD_RAW_CALLBACK 据此
    //   选中 ncbRawCallbackMethod<T*> 特化（ncbind.hpp:1504-1543，与 0x5A77F4 逐句
    //   同构，含 TJS_STATICMEMBER 分支 = a1+58&1）——实例取得(-1008)先于 numparams
    //   (-1004)，与二进制一致。
    //   解包 (text, x, y[, size, flag]) → 调真 render(text, x, y, flag)。
    //   注意（BLOCKING，禁参数名推导）：x/y 在真 render 里语义**不是坐标**——
    //     x = begin/end 平衡集启用标志（render `if(!a3)` 门控），
    //     y = 每字 renderPos 步进初值（render 入口 `+192=(float)a4`）。
    // ============================================================
    static tjs_error render(tTJSVariant *result, tjs_int numparams,
                            tTJSVariant **param,
                            TextRenderBase *t) { // Process @0x59FC28
        if(numparams < 3) // a2 < 3
            return TJS_E_BADPARAMCOUNT; // 4294966292 = 0xFFFFFC14
        bool flag; // v9
        if(numparams == 3) {
            flag = false; // v9 = 0
        } else {
            // a2>=4：param[3]=size real 强制后丢弃（switch @0x59fcb0）。
            (void)param[3]->AsReal();
            // a2<5 → flag=0；a2>=5 → flag = boolCoerce(param[4])。
            if(numparams < 5)
                flag = false; // LABEL_12
            else
                flag = boolCoerce(*param[4]); // v9 = !v12
        }
        ttstr text(*param[0]); // sub_A0BAF4(v17, *a3)：text 拷贝
        tjs_int x = intCoerce(*param[1]); // v13
        tjs_int y = intCoerce(*param[2]); // v15
        bool ok = t->renderImpl(text, (int)x, (int)y, flag);
        if(result)
            *result = (tjs_int)(ok ? 1 : 0); // sub_A0FEF0(a1, v16 & 1)
        return TJS_S_OK;
    }

    // ============================================================
    // 落字 / 行布局 / render 状态机（内部实现，读 objthis 成员回调脚本）
    // ============================================================
    // 释放当前 ruby 文本（+528 release → null）。
    void releaseCurRubyText() {
        _curRubyText = ttstr();
        _hasCurRubyText = false;
    }

    // 字宽度量回调 sub_5A426C@0x5A426C：FuncCall(L"onGetTextWidth", text[str], size[real])，
    //   返回值按 result.type 强制转 double（忠实移植，脚本层取字宽——非平台边界）。
    //   回调目标 = objthis 成员（= 二进制 native+0 = dispatch）。
    float onGetTextWidth(const ttstr &text, float size) { // sub_5A426C
        if(!objthis)
            return 0.0f;
        tTJSVariant result;
        tTJSVariant vText(text);            // arg0: string
        tTJSVariant vSize((tjs_real)size);  // arg1: real (a3)
        tTJSVariant *args[2] = { &vText, &vSize };
        tjs_error hr = objthis->FuncCall(0, TJS_W("onGetTextWidth"), nullptr,
                                         &result, 2, args, objthis);
        if(TJS_FAILED(hr))
            return 0.0f; // FuncCall 失败 → result 保持 void → 0.0
        if(result.Type() == tvtVoid)
            return 0.0f;
        return (float)result.AsReal();
    }

    // appendChar @0x5A3880：累积 UTF-16 buffer + 度量 + ruby + 落字入口。
    bool appendChar(tjs_char ch) { // 0x5A3880
        // push ch 到内部 UTF-16 buffer（+504/+512），容器选型 = char vector
        _accumBuf.push_back(ch);
        // 倒计数(+88)：先算 v21=(+88)-1，仅当 v21>=0 才回写并返回 true。
        {
            int v21 = _charBufCountdown - 1;
            if(v21 >= 0) {
                _charBufCountdown = v21; // *(a1+88) = v21
                return true;
            }
        }
        // buffer 非恰好 1 字符 → 不落字，返回 false（二进制 `(end-begin)!=2`）
        if(_accumBuf.size() != 1)
            return false;
        // 恰好 1 字符且计数耗尽：度量字宽 + 构造 charItem 蓝图
        ttstr text((tjs_char)ch); // 单字符文本（ttstr_createFromWide(&v66)，v66=ch/v67=0）
        float effSize = _fontScale * _curFontSize; // +184 * +116
        float cw = onGetTextWidth(text, effSize); // sub_5A426C → v23/v49
        CharItem v;
        v.text = text;             // +0
        v.cw = cw;                 // +16 (v49)
        v.size = effSize;          // +20 (v50)
        v.graph = false;           // +40 (v54)
        v.bold = _curBold;         // +41 (v55 = a1+62)
        v.italic = _curItalic;     // +42 (v56 = a1+65)
        v.shadow = _curShadow;     // +43 (v57 = a1+63)
        v.edge = _curEdge;         // +44 (v58 = a1+64)
        v.vertical = _vertical;    // +45 (v59 = a1+48)
        v.chColor = _curChColor;       // +28 (v51 = a1+200)
        v.shadowColor = _curShadowColor; // +32 (v52 = a1+204)
        v.edgeColor = _curEdgeColor;     // +36 (v53 = a1+212)
        v.shadowDiff = _curShadowDiff;   // +48 (v60 = a1+208)
        v.faceIndex = _curFaceIndex;     // +52 (v61 = a1+72)
        // ruby 分支：!vertical(v59==0) 且 有当前 ruby 文本(+528)
        if(!_vertical && _hasCurRubyText) { // 0x5a3a48 / 0x5a3a4c
            float rubyCw =
                onGetTextWidth(_curRubyText,
                               _fontScale * _curRubySize); // sub_5A426C(+128)
            // ruby 子项：x = cw*0.5 - rubyCw*0.5；y = -(rubySize*fontScale) - rubyOffset
            float rubyX = (float)(cw * 0.5f) - (float)(rubyCw * 0.5f); // v41
            float rubyY = (float)-(float)(_curRubySize * _fontScale)
                          - _curRubyOffset; // v40 = -(v31*v29)-v30
            RubyItem r;
            r.text = _curRubyText; // ruby 文本（incref → charItem 持有）
            r.x = rubyX;           // ruby +8 (-3 float)
            r.y = rubyY;           // ruby +12 (-2 float)
            r.span = _curRubySize * _fontScale; // ruby +16 (-1 float = v34*v36)
            v.ruby.push_back(r);
            releaseCurRubyText(); // 消费后释放 +528
            // ruby bbox 累加（+264 left / +268 top / +272 right）
            float penX = _penX;       // +232 (v?)
            float penYpos = _penY;    // +236 (v32)
            float ry = rubyY + penYpos;            // v40 + v32
            if(ry < _rubyTop)                       // a1+268
                _rubyTop = ry;
            float rx = rubyX + penX;                // v41 + a1+232
            if(rx < _rubyLeft)                      // a1+264
                _rubyLeft = rx;
            float rxr = rubyCw + rx;                // v27 + v43
            if(rxr > _rubyRight)                    // a1+272
                _rubyRight = rxr;
            if(penYpos < _rubyTop)                  // v32 >= v42 ? skip : *v33=v32
                _rubyTop = penYpos;
        } else if(!_vertical) {
            // 无 ruby 但 horizontal：a1+268 = min(a1+268, penY)
            if(_penY < _rubyTop)
                _rubyTop = _penY;
        }
        // 清空 buffer（+512 = +504），落字
        _accumBuf.clear();
        return kinsoku(v); // sub_5A4A7C(a1, &v48)
    }

    // 落字 + kinsoku 禁则 @0x5A4A7C：把 char 落到 pending deque（+320），处理行尾/行首禁则。
    bool kinsoku(CharItem &c) { // 0x5A4A7C
        // 1. over 检测（是否超出渲染尺寸需换行）。命中 → 直接落字（placeHoriz=v6）。
        if(_vertical) { // a1+48
            float h = _renderSizeH; // +244
            if(h <= 0.0f || h > (float)(_penY + c.size) || _ignoreOverY)
                return placeChar(c, /*placeHoriz=v6=*/false);
        } else {
            float w = _renderSizeW; // +240
            if(w <= 0.0f || w > (float)(_penX + c.cw) || _ignoreOverX)
                return placeChar(c, /*placeHoriz=v6=*/true);
        }
        // 2. 需要换行：执行 kinsoku 重排（临时 deque 暂存回退字符）
        {
            std::deque<CharItem> tmp; // v81：pendingDeque_init(v81,0)
            if(!_wordBreak) { // !a1+49：非 word_break
                // 把超过 wordBreakRun(+424) 个的尾部字符（trailing run）移到 tmp
                int run = _pendingLine.wordBreakRun; // v33 = a1+424
                if(run >= 1) {
                    while((int)_pendingLine.chars.size() > run) {
                        // 弹 pending 末字符到 tmp 前端（v82 头插）
                        tmp.push_front(_pendingLine.chars.back()); // copy
                        _pendingLine.chars.pop_back();
                        --_renderCount; // a1+84
                    }
                }
                // → LABEL_107
            } else if(_following.IndexOf(c.text) != -1) {
                // 当前字在 following 集（following 字符，可触发 kinsoku 计数下移）
                int used = _kinsokuUsed; // v30 = a1+108
                if(used >= _kinsokuMax) { // a1+112：次数用尽
                    while(_pendingLine.chars.size() >= 2) {
                        if(used < 1) {
                            // max<=0 边界：仅当末字符在 leading 集时下移一个。
                            if(_leading.IndexOf(_pendingLine.chars.back().text) != -1) {
                                tmp.push_front(_pendingLine.chars.back());
                                _pendingLine.chars.pop_back();
                                --_renderCount; // a1+84 @0x5a52d8
                            }
                            break; // → LABEL_107
                        }
                        tmp.push_front(_pendingLine.chars.back());
                        _pendingLine.chars.pop_back();
                        --_kinsokuUsed; // a1+108
                        --_renderCount; // a1+84
                        used = _kinsokuUsed; // refetch (v30 = a1+108 - 1)
                    }
                    // → LABEL_107
                } else {
                    // following 集 && used < max @0x5A4D90：++kinsokuUsed 后查 pending
                    //   **末字符**是否在 **following 集(+8)**：命中或 pending 空 → LABEL_107
                    //   （finishLine 换行）；未命中 → @0x5A5338 直接落字，**不 finishLine、不换行**。
                    _kinsokuUsed = used + 1; // ++[+108]
                    if(_pendingLine.chars.empty()) {
                        // [+368]==[+336]：pending 空 → LABEL_107（finishLine）
                    } else if(_following.IndexOf(_pendingLine.chars.back().text)
                              != -1) {
                        // back 在 following 集(a1+8) → LABEL_107（finishLine）
                    } else {
                        // back 不在 following 集 → @0x5A5338 直接落字，不换行
                        return placeChar(c, /*placeHoriz=*/!_vertical);
                    }
                    // → LABEL_107
                }
            } else {
                // 当前字不在 following 集：行尾禁则（leading）处理
                if(_pendingLine.chars.size() >= 3) { // v45 >= 3
                    // 倒数第2字符 v48
                    const CharItem &second =
                        _pendingLine.chars[_pendingLine.chars.size() - 2];
                    if(_leading.IndexOf(second.text) == -1) {
                        // 倒数第2字符不在 leading 集，且末字符在 leading 集 → 下移末字符
                        if(_leading.IndexOf(_pendingLine.chars.back().text) != -1) {
                            tmp.push_front(_pendingLine.chars.back());
                            _pendingLine.chars.pop_back();
                            --_renderCount; // a1+84
                        }
                    }
                }
                // LABEL_94：pending size>=2 且末字符在 leading 集 → 再下移一个
                if(_pendingLine.chars.size() >= 2) {
                    if(_leading.IndexOf(_pendingLine.chars.back().text) != -1) {
                        tmp.push_front(_pendingLine.chars.back());
                        _pendingLine.chars.pop_back();
                        --_renderCount; // a1+84
                    }
                }
                // → LABEL_107
            }
            // LABEL_107：行结束
            if(!finishLine()) // sub_5A34B8
                return false;        // → LABEL_113
            // drain tmp：把回退字符重排到下一行（自递归 kinsoku）
            for(size_t i = 0; i < tmp.size(); ++i) {
                if(!kinsoku(tmp[i]))
                    return false; // → LABEL_113
            }
            // 换行后落字（v6 = !vertical）
            return placeChar(c, /*placeHoriz=*/!_vertical); // 0x5a5348
        }
    }

    // placeChar — kinsoku LABEL_10：把 char 落到 pending deque + 推进 pen + renderPos。
    bool placeChar(CharItem &c, bool placeHoriz) { // LABEL_10
        // char.x(+8) = penX(+232)
        c.x = _penX;
        // char.y(+12)：horizontal → penY - size（v6=1 / placeHoriz）；vertical → penY
        c.y = placeHoriz ? (_penY - c.size) : _penY; // v10
        // char.renderPos(+24) = renderPos(+280)（v11；始终 = renderPos，不取 max）
        c.renderPos = _renderPos; // v11 = a1+280；*(a2+24) = v11
        // delayAccum(+188) = max(delayAccum, renderPos)
        if(_renderDelayAccum <= _renderPos) // !(a1+188 > v11)
            _renderDelayAccum = c.renderPos; // a1+188 = *v9 = char.renderPos
        // else: a1+188 > v11 → v9 指 a1+188 → a1+188 = a1+188（不变）
        // push char 副本到 pending 行 deque（+368 finish.cur += 80）
        _pendingLine.chars.push_back(c); // TextRenderBase_charItem_copy / pushNode
        // word_break：记录是否空格(+428) + 上字是空格则记 run(+424)
        if(!_wordBreak) {          // a1+49 == 0 → 走 LABEL_16
            updateWordBreakState(c); // 0x5a4b8c
        }
        // 推进 renderPos + pen（LABEL_22）
        _renderPosSnap = _renderPos; // a1+284 = a1+280 (v16/v18)
        if(_widthTimeScale) { // a1+55
            float rate = _charDelayStep; // a1+192 (v19)
            if(_vertical) { // a1+48
                _renderCount += 1; // a1+84
                _renderPos = (float)(rate * (float)(c.size /
                              (float)(_fontScale * _curFontSize)))
                              + _renderPosSnap;
                advanceLineVertical(c); // LABEL_26
                return true;
            }
            _renderCount += 1;
            _renderPos = (float)(rate * (float)(c.cw /
                          (float)(_fontScale * _curFontSize)))
                          + _renderPosSnap;
        } else {
            // 非 width_time_scale：renderPos += charDelayStep(+192)
            _renderPos = _charDelayStep + _renderPosSnap; // v22 = a1+192 + v16
            _renderCount += 1;                            // a1+84
            if(_vertical) {     // v21 = a1+48
                advanceLineVertical(c); // LABEL_26
                return true;
            }
        }
        // horizontal pen advance（penX += cw；penX = pitch + penX）
        {
            float newPenX = c.cw + _penX; // v29 = a2+16 + a1+232
            _penX = newPenX;              // a1+232
            if(_pendingLine.bboxRight < newPenX)  // a1+416
                _pendingLine.bboxRight = newPenX;
            _penX = _curPitch + newPenX;  // a1+232 = a1+140 + v29
        }
        return true;
    }

    // word_break 落字后状态更新（kinsoku LABEL_16 / 0x5a4b8c）。
    void updateWordBreakState(const CharItem &c) { // 0x5a4b8c
        bool isSpace = false; // v15
        if(c.text.c_str()) {
            // sub_9B1ED0(text, L" ") == 0 → 是空格（wcscmp）
            isSpace = (c.text == ttstr(TJS_W(" ")));
        }
        if(isSpace) {
            _pendingLine.prevWasSpace = true; // a1+428 = 1（LABEL_20）
            return;
        }
        if(_pendingLine.prevWasSpace) // a1+428：上字是空格 → 记 run = 当前 pending size
            _pendingLine.wordBreakRun = (int)_pendingLine.chars.size(); // a1+424
        _pendingLine.prevWasSpace = false; // a1+428 = 0（LABEL_20 写 v15=0）
    }

    // 竖排 pen advance（kinsoku LABEL_26 / 0x5a4c8c）：penY += size；penY = pitch + penY。
    void advanceLineVertical(const CharItem &c) { // LABEL_26
        float newPenY = c.size + _penY; // v25 = a2+20 + a1+236
        _penY = newPenY;                // a1+236
        if(_pendingLine.bboxBottom < newPenY)   // a1+420
            _pendingLine.bboxBottom = newPenY;
        _penY = _curPitch + newPenY;    // a1+236 = a1+140 + v25
    }

    // finishLine @0x5A34B8：行结束。横排路径（!vertical）= 行宽/over 检测 → align 偏移
    //   → align 缩进填充 → 落字到行（写坐标 + 拼接 renderText）→ push lineItem → 清 pending
    //   → renderText 追加换行 → pen 复位 + 行间距。竖排路径直接跳 LABEL_56 清理。
    bool finishLine() { // 0x5A34B8
        if(!_vertical) { // !a1+48：横排路径
            // 1. 行内最大字号 v6
            float v6 = 0.0f;
            for(const CharItem &ci : _pendingLine.chars) // 遍历 pending 行 deque
                if(ci.size > v6) // char.size(+20)
                    v6 = ci.size;
            // 行高 v11 = max(v6, _curLineSize(+144))
            float v11 = (v6 <= _curLineSize) ? _curLineSize : v6;
            // 2. over 检测：renderSizeH(+244) > 0 且 < v11 + penY(+236)
            float h = _renderSizeH; // +244 (v10)
            if(h > 0.0f && h < (float)(v11 + _penY)) {
                _renderOver = true; // a1+60 = 1
                if(!_ignoreOverY) { // !a1+54
                    _pendingLine.clear(); // sub_5A1E68(a1+320) @0x5a384c
                    return false;         // return 0
                }
            }
            // 3. align 偏移 v14（v13 = _curAlign(+76)）
            float v14;
            int v13 = _curAlign; // a1+76
            if(v13 == 1) {       // right align
                v14 = _renderSizeW - _penX; // a1+240 - a1+232
            } else if(v13 == 0) { // center
                // 二进制：v14=0 然后 if(!v13) v14 = (a1+240-a1+232)*0.5
                v14 = (float)(_renderSizeW - _penX) * 0.5f;
            } else {              // left（其它）
                v14 = 0.0f;
            }
            // 3b. align 缩进：进入缩进段的唯一条件 = pending 非空（0x5A35A0 证伪 v14!=0 门控）
            if(!_pendingLine.chars.empty()) {
                // v15 = onGetTextWidth(L"　"=0x3000, fontScale*fontsize); ==0 → fontsize
                float v15 = onGetTextWidth(ttstr((tjs_char)0x3000),
                                          _fontScale * _curFontSize); // sub_5A426C
                if(v15 == 0.0f)
                    v15 = _curFontSize; // a1+116
                // v16 = (int)((v14 + 首字符.x(+8)) / v15)
                int v16 = (int)((float)(v14 + _pendingLine.chars.front().x) / v15);
                for(; v16 >= 1; --v16)
                    _renderText = _renderText + ttstr((tjs_char)0x3000); // word_14CA1EE 全角空格
            }
            // LABEL_32：落字到行（写坐标 + 拼接 renderText）
            float v24 = 0.0f; // 行底累加 (a1+400)
            for(CharItem &ci : _pendingLine.chars) {
                float v27 = v11 + ci.y; // 行内字底 = v11 + char.y(+12)
                if(v24 < v27)
                    v24 = v27;
                ci.x += v14;  // char.x(+8) += align 偏移
                ci.y = v27;   // char.y(+12) = v27
                if(ci.text.c_str()) // 拼接非空文本到 renderText(+40)
                    _renderText = _renderText + ci.text; // sub_A13ABC
            }
            // 写行 metric（pending Line 内嵌字段 +400..+420）
            _pendingLine.lineHeight = v11;             // a1+404
            _pendingLine.lineBottom = v24;             // a1+400
            float v32 = v14 + _pendingLine.bboxLeft;   // a1+408
            float v34 = v14 + _pendingLine.bboxRight;  // a1+416
            float v33 = v11 + _penY;                   // a1+236 新行 Y
            _pendingLine.bboxLeft = v32;               // a1+408
            _pendingLine.bboxRight = v34;              // a1+416
            _penY = v33;                               // a1+236
            if(_pendingLine.bboxBottom < v33)          // a1+420
                _pendingLine.bboxBottom = v33;
            // push：整个 pending Line 拷入 lineList（源码层 = push_back(pendingLine)）。
            _lineList.push_back(_pendingLine);
            _pendingLine.clear(); // sub_5A1E68(a1+320) @0x5a378c：metric 全零化
            // renderText += L"\n"（行尾换行）
            _renderText = _renderText + ttstr(TJS_W("\n")); // sub_A13ABC(.., L"\n")
            // pen X 复位到行首 + 行间距推进
            _penX = _lineStartX;        // a1+232 = a1+196
            // 二进制 0x5a37c8 读的是 Line::clear 后的 +408（左操作数恒 0）
            if(_pendingLine.bboxLeft > _penX)   // a1+408 > a1+232
                _pendingLine.bboxLeft = _penX;
            _penY = _curLineSpacing + _penY; // a1+236 = a1+136 + a1+236
        }
        // LABEL_56：清理（横排路径走完也到这；竖排路径直接到这）
        releaseCurRubyText(); // release a1+528 → null
        _kinsokuUsed = 0;     // a1+108 = 0
        _accumBuf.clear();    // a1+512 = a1+504
        return true;          // return 1
    }

    // float bits 重解释为 int（done keyWait 段数据契约）。
    static int reinterpretFloatBits(float f) {
        int i;
        memcpy(&i, &f, sizeof(i));
        return i;
    }

    // resolveFaceIndex @0x5A14DC：face 名 → 稳定 index（intern）。
    int resolveFaceIndex(const ttstr &name) { // 0x5A14DC
        auto it = _faceHash.find(name); // sub_5A172C by FaceNameHash
        if(it != _faceHash.end())
            return it->second; // 命中节点 idx (+16)
        int idx = (int)_faceTable.size(); // (a1[58]-a1[57])>>3
        _faceHash.emplace(name, idx); // sub_5A181C intern：节点 idx 写为 v15
        // **不向 _faceTable push**：经 field-level 穷尽核实，二进制从无 faceTable push——
        //   face 名只进 faceHash 节点(+536)，faceTable(+456) 恒空，故 idx=size() 恒为 0，
        //   所有 face 退化为 idx 0（原版退化行为，1:1 忠实复刻）。
        return idx;
    }

    // onStyleChanged @0x5A1F28：当前样式变化后，构造 dict{face, bold, italic}
    //   并对脚本对象 FuncCall(L"onFontChange", dict)。回调目标 = objthis 成员。
    void onStyleChanged() { // 0x5A1F28
        if(!objthis)
            return;
        // 构造参数 dict
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        // face: _faceTable[_curFaceIndex]；越界 → 空串
        ttstr faceName;
        if(_curFaceIndex >= 0 && _curFaceIndex < (int)_faceTable.size())
            faceName = _faceTable[_curFaceIndex];
        tTJSVariant vFace(faceName);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("face"), nullptr, &vFace, dict);
        tTJSVariant vBold((tjs_int)(_curBold ? 1 : 0));    // a1+62
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("bold"), nullptr, &vBold, dict);
        tTJSVariant vItalic((tjs_int)(_curItalic ? 1 : 0)); // a1+65
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("italic"), nullptr, &vItalic,
                      dict);
        // FuncCall(L"onFontChange", dict)：objthis 上的脚本回调。
        //   arg variant {Object=dict, ObjThis=null}——objthis 槽为 null。
        tTJSVariant vDict(dict, (iTJSDispatch2 *)nullptr);
        dict->Release();
        tTJSVariant *args[1] = { &vDict };
        objthis->FuncCall(0, TJS_W("onFontChange"), nullptr, nullptr, 1, args,
                          objthis);
    }

    // ============================================================
    // dict 解析层的三套值强制转换（1:1 复刻反编译里的 switch(type) 内联体）。
    // ============================================================
    static bool boolCoerce(const tTJSVariant &v) { return (bool)v; }
    static tjs_int intCoerce(const tTJSVariant &v) {
        return (tjs_int)v.AsInteger();
    }
    static float realCoerce(const tTJSVariant &v) { return (float)v.AsReal(); }

    // dict 逐 key PropGet(TJS_MEMBERMUSTEXIST=0x400, L"key")（= 二进制 vtable+32，flag 1024）。
    static bool dictGet(iTJSDispatch2 *dict, const tjs_char *key,
                        tTJSVariant *out) {
        return TJS_SUCCEEDED(
            dict->PropGet(TJS_MEMBERMUSTEXIST, key, nullptr, out, dict));
    }

    // ============================================================
    // render 状态机 helper（scanTagUntil / scanDigits / parseInt10 /
    //   parseHexColor / evalDollarTag / renderPercentTag / renderBalancedChar）
    // ============================================================
    // scanTagUntil @0x5A3CE4：从 *cursor 起读字符直到遇到 delim（或到 len）。
    static ttstr scanTagUntil(const tjs_char *text, int *cursor, int len,
                              tjs_char delim) { // 0x5A3CE4
        std::vector<tjs_char> buf; // v9/v10/v11 动态 UTF-16 缓冲
        int c = *cursor;
        while(c < len) {
            *cursor = c + 1;
            tjs_char ch = text[c];
            if(ch == delim)
                break;
            buf.push_back(ch);
            c = *cursor;
        }
        if(buf.empty())
            return ttstr(); // v22 = 0（空串）
        buf.push_back(0);
        return ttstr(&buf[0]); // ttstr_createFromWide(v9)
    }
    // scanDigits @0x5A3F18：从 *cursor 起读连续数字字符（0-9），停在非数字。
    static ttstr scanDigits(const tjs_char *text, int *cursor, int len) { // 0x5A3F18
        std::vector<tjs_char> buf;
        int c = *cursor;
        while(c < len) {
            *cursor = c + 1;
            tjs_char ch = text[c];
            if((unsigned int)(ch - 48) > 9) // 非 0-9 → break
                break;
            buf.push_back(ch);
            c = *cursor;
        }
        if(buf.empty())
            return ttstr();
        buf.push_back(0);
        return ttstr(&buf[0]);
    }
    // parseInt10 @0x9B111C：UTF-16 串转十进制 int（跳前导 <=0x20 空白，可选 '-'，循环 *10+digit）。
    static int parseInt10(const ttstr &s) { // 0x9B111C
        const tjs_char *p = s.c_str();
        if(!p)
            return 0;
        // 跳前导空白（1..0x20）：do { c=*p++; } while(c-1 < 0x20)
        unsigned int c;
        do {
            c = (unsigned short)*p++;
        } while(c - 1 < 0x20);
        if(!c)
            return 0;
        int neg = 0; // v5
        if(c == 45) { // '-'
            do {
                c = (unsigned short)*p++;
            } while(c <= 0x20 && c);
            if(!c)
                return 0;
            neg = 1;
        }
        unsigned int v7 = 0;
        if((unsigned int)(c - 48) <= 9) { // 是数字
            for(;;) {
                v7 = 10 * v7 + c - 48;
                c = (unsigned short)*p++;
                if((unsigned int)(c - 48) >= 0xA)
                    break;
            }
        }
        return neg ? -(int)v7 : (int)v7;
    }
    // parseHexColor @0x5A228C(case '#')：把标签内容当 hex 颜色解析，写 _curChColor。
    bool parseHexColor(const ttstr &content) { // 0x5a25f0..0x5a256c
        const tjs_char *p = content.c_str();
        if(!p) {
            _curChColor = _defaultChColor;
            return true;
        }
        tjs_char first = p[0]; // v25
        if(first == 48 && (p[1] | 0x20) == 0x78) // '0' 且 ('x'|'X')
            p += 2; // v27 += 2
        unsigned int acc = 0; // v28
        for(;;) {
            tjs_char ch = *p++; // v25
            int hv = hexDigitValue(ch); // 掩码+减表
            if(hv < 0) { // 非 hex digit → 终止
                _curChColor = acc | 0xFF000000u; // +200 = v28 | 0xFF000000
                return true;
            }
            acc = (unsigned int)hv | (16 * acc); // v28 = v26 | (16*v28)
        }
    }
    // hexDigitValue：复刻 0x5A228C hex decode（掩码 0x7E0000007E03FF + 减表 qword_14CA200）。
    static int hexDigitValue(tjs_char ch) {
        unsigned int idx = (unsigned int)(unsigned short)(ch - 48);
        if(idx > 0x36u) // (ch-48) > 0x36 → 无效
            return -1;
        if(((0x7E0000007E03FFuLL >> idx) & 1) == 0) // 掩码门控有效 hex 位
            return -1;
        // 减表：table[ch-48] + ch（'0'-'9'→-48, 'A'-'F'→-55, 'a'-'f'→-87）
        if(ch >= 48 && ch <= 57)
            return (int)ch - 48; // 0-9
        if(ch >= 65 && ch <= 70)
            return (int)ch - 55; // A-F
        if(ch >= 97 && ch <= 102)
            return (int)ch - 87; // a-f
        return -1;
    }
    // evalDollarTag @0x5A4148：对脚本对象 FuncCall(L"onEval", tagContent) → 返回值按 type 分发。
    //   回调目标 = objthis 成员（= 二进制 native+0 = dispatch）。
    ttstr evalDollarTag(const ttstr &content) { // 0x5A4148
        if(!objthis)
            return ttstr();
        tTJSVariant arg(content); // v10：tagContent（string）
        tTJSVariant result;       // v12
        tTJSVariant *args[1] = { &arg };
        objthis->FuncCall(0, TJS_W("onEval"), nullptr, &result, 1, args,
                          objthis); // 返回码不检查（二进制同）
        tjs_int ty = (tjs_int)result.Type(); // v13
        if((tjs_uint)(ty - 3) < 3) // octet(3)/int(4)/real(5) @0x5a41d8
            TJSThrowVariantConvertError(result, tvtString); // sub_A0E48C(,2)
        if(ty == tvtString) // v13 == 2 @0x5a41e0
            return ttstr(result);
        if(ty == tvtObject) // v13 == 1 @0x5a41e8 → LABEL_6 同抛
            TJSThrowVariantConvertError(result, tvtString);
        return ttstr(); // void → 空（*a3 = 0 @0x5a41f8）
    }

    // % 样式控制分发 @0x5A228C（case '%'）。code = % 之后的控制字符。
    void renderPercentTag(const tjs_char *p, int len,
                          tjs_char code, int next, int v36, ttstr &tagAccum,
                          int yParam) { // 0x5A228C case '%'
        switch(code) {
        case TJS_W('0'): case TJS_W('1'): case TJS_W('2'): case TJS_W('3'):
        case TJS_W('4'): case TJS_W('5'): case TJS_W('6'): case TJS_W('7'):
        case TJS_W('8'): case TJS_W('9'): {
            // %数字：size 百分比。从 code 处（next）读数字串。
            int cur = next; // v136 = v29+1（回退到 code 位置重读数字）
            tagAccum = scanDigits(p, &cur, len); // sub_5A3F18
            _percentCursor = cur; // 把内部 cursor 回传（见调用点）
            if(!_ignoreSize) { // !+51
                float v41;
                int val = parseInt10(tagAccum);
                if(!tagAccum.IsEmpty() && val > 0)
                    v41 = (float)((float)val / 100.0f) * _defaultFontSize; // +148
                else
                    v41 = _defaultFontSize; // +148
                if(_curFontSize < 0.0f || _curFontSize != v41) {
                    _curFontSize = v41; // +116
                    onStyleChanged(); // sub_5A1F28
                }
            }
            break;
        }
        case TJS_W(';'): // %;：恢复 curFontSize = defaultFontSize(+148)
            if(!_ignoreSize) {
                if(_curFontSize < 0.0f || _curFontSize != _defaultFontSize)
                    applyFontSize(_defaultFontSize); // LABEL_298
            }
            break;
        case TJS_W('C'): // 居中对齐 → +76（cascade，见下）
            if(_ignoreStyle) { // +59 → 直接走 bigfontsize
                applyBigFontSizeTag();
                break;
            }
            // 二进制 cascade：+76=0 → +76=1 → +76=-1（fall-through，最终 -1）。
            _curAlign = 0; _curAlign = 1; _curAlign = -1; // 最终 -1
            applyBigFontSizeTag(); // 落入 LABEL_228
            break;
        case TJS_W('R'): // 右对齐
            if(_ignoreStyle) { applyBigFontSizeTag(); break; }
            _curAlign = 1; _curAlign = -1; // cascade → -1
            applyBigFontSizeTag();
            break;
        case TJS_W('L'): // 左对齐
            if(_ignoreStyle) { applyBigFontSizeTag(); break; }
            _curAlign = -1;
            applyBigFontSizeTag();
            break;
        case TJS_W('B'): // %B：bigfontsize
            applyBigFontSizeTag();
            break;
        case TJS_W('S'): // %S：smallfontsize(+156)。门控 +51(ignore_size)，在 applySmallFontSizeTag 内判。
            applySmallFontSizeTag();
            break;
        case TJS_W('b'): { // %b：bold（下一字符 0/1/其它）
            if(v36 >= len)
                break;
            int dflt = _defaultBold ? 1 : 0; // +66 (v115)
            bool gate = _ignoreType;          // +57 (v116)
            tjs_char arg = p[next + 1];       // v15[v36]（code 之后字符）
            _percentCursor = next + 2;
            int val;
            if(arg == 48) val = 0;
            else if(arg == 49) val = 1;
            else val = dflt;
            if(gate) break; // +57 → 不写
            if((_curBold ? 1 : 0) != val) { // +62
                _curBold = (val != 0);
                onStyleChanged();
            }
            break;
        }
        case TJS_W('i'): { // %i：italic
            if(v36 >= len)
                break;
            int dflt = _defaultItalic ? 1 : 0; // +69
            bool gate = _ignoreType;            // +57
            tjs_char arg = p[next + 1];
            _percentCursor = next + 2;
            int val;
            if(arg == 48) val = 0;
            else if(arg == 49) val = 1;
            else val = dflt;
            if(gate) break;
            if((_curItalic ? 1 : 0) != val) { // +65
                _curItalic = (val != 0);
                onStyleChanged();
            }
            break;
        }
        case TJS_W('e'): { // %e：edge（无 onStyleChanged）
            if(v36 >= len)
                break;
            int dflt = _defaultEdge ? 1 : 0; // +68
            bool gate = _ignoreType;          // +57
            tjs_char arg = p[next + 1];
            _percentCursor = next + 2;
            int val;
            if(arg == 48) val = 0;
            else if(arg == 49) val = 1;
            else val = dflt;
            if(gate) break;
            if((_curEdge ? 1 : 0) != val) // +64
                _curEdge = (val != 0); // 注意：无 onStyleChanged
            break;
        }
        case TJS_W('s'): { // %s：shadow（无 onStyleChanged）
            if(v36 >= len)
                break;
            int dflt = _defaultShadow ? 1 : 0; // +67
            bool gate = _ignoreType;            // +57
            tjs_char arg = p[next + 1];
            _percentCursor = next + 2;
            int val;
            if(arg == 48) val = 0;
            else if(arg == 49) val = 1;
            else val = dflt;
            if(gate) break;
            if((_curShadow ? 1 : 0) != val) // +63
                _curShadow = (val != 0); // 无 onStyleChanged
            break;
        }
        case TJS_W('f'): { // %f：face（标签内容到 ';' → resolveFaceIndex → +72）
            int cur = v36; // 二进制 v136 = v29+2 = v36
            tagAccum = scanTagUntil(p, &cur, len, TJS_W(';'));
            _percentCursor = cur;
            if(!_ignoreFace) { // !+58
                if(!tagAccum.IsEmpty()) {
                    int idx = resolveFaceIndex(tagAccum); // sub_5A14DC
                    if(_curFaceIndex != idx) { // +72
                        _curFaceIndex = idx;
                        onStyleChanged();
                    }
                } else {
                    int idx = _defaultFaceIndex; // +96
                    if(_curFaceIndex != idx) {
                        _curFaceIndex = idx;
                        onStyleChanged();
                    }
                }
            }
            break;
        }
        case TJS_W('d'): { // %d：delay（标签到 ';' → charDelayStep = (val/100)*y）
            int cur = v36;
            tagAccum = scanTagUntil(p, &cur, len, TJS_W(';'));
            _percentCursor = cur;
            if(!_ignoreDelay) { // !+52
                float v96 = (float)yParam; // a4
                if(!tagAccum.IsEmpty())
                    v96 = (float)((float)(int)parseInt10(tagAccum) / 100.0f)
                          * (float)yParam;
                _charDelayStep = v96; // +192
            }
            break;
        }
        case TJS_W('a'): { // %a：absolute delay（标签到 ';' → charDelayStep = val）
            int cur = v36;
            tagAccum = scanTagUntil(p, &cur, len, TJS_W(';'));
            _percentCursor = cur;
            if(!_ignoreDelay) { // !+52
                int v104 = yParam; // a4
                if(!tagAccum.IsEmpty())
                    v104 = parseInt10(tagAccum);
                _charDelayStep = (float)v104; // +192
            }
            break;
        }
        case TJS_W('p'): { // %p：pitch（标签到 ';' → curPitch(+140)；空→default+172）
            int cur = v36;
            tagAccum = scanTagUntil(p, &cur, len, TJS_W(';'));
            _percentCursor = cur;
            if(!_ignoreStyle) { // !+59
                if(!tagAccum.IsEmpty())
                    _curPitch = (float)parseInt10(tagAccum); // +140
                else
                    _curPitch = _defaultPitch; // +140 = +172
            }
            break;
        }
        case TJS_W('l'): { // %l：标签到 ';' → parseInt10（仅校验，无字段写）
            int cur = v36;
            tagAccum = scanTagUntil(p, &cur, len, TJS_W(';'));
            _percentCursor = cur;
            if(!_ignoreDelay) { // !+52
                if(!tagAccum.IsEmpty())
                    parseInt10(tagAccum); // sub_9B111C（结果丢弃）
            }
            break;
        }
        case TJS_W('t'): { // %t：标签到 ';' → parseInt10（无字段写）
            int cur = v36;
            tagAccum = scanTagUntil(p, &cur, len, TJS_W(';'));
            _percentCursor = cur;
            if(!_ignoreDelay) { // !+52
                if(!tagAccum.IsEmpty())
                    parseInt10(tagAccum);
            }
            break;
        }
        case TJS_W('w'): { // %w：标签到 ';' → parseInt10（无字段写）
            int cur = v36;
            tagAccum = scanTagUntil(p, &cur, len, TJS_W(';'));
            _percentCursor = cur;
            if(!_ignoreDelay && !tagAccum.IsEmpty()) // !+52
                parseInt10(tagAccum);
            break;
        }
        case TJS_W('r'): // %r：resetFont（sub_59EEE0）
            resetFont();
            break;
        case TJS_W('D'): { // %D：若码后是 '$' → 嵌套 eval delay；否则当 delay 标签
            int cur = v36;
            if(p[v36] != 36) { // v15[v36] != '$'
                tagAccum = scanTagUntil(p, &cur, len, TJS_W(';'));
                _percentCursor = cur;
                if(!tagAccum.IsEmpty())
                    parseInt10(tagAccum); // sub_9B111C（无字段写）
            } else {
                cur = next + 2; // v136 = v29+3（跳过 '$'）
                tagAccum = scanTagUntil(p, &cur, len, TJS_W(';'));
                _percentCursor = cur;
                if(!_ignoreDelay) { // !+52
                    if(!tagAccum.IsEmpty())
                        parseInt10(tagAccum);
                }
            }
            break;
        }
        default: { // 其它 %X：标签到 ';' → 消费（refcount no-op，无字段写）
            int cur = v36; // 二进制 v136 = v29+2 = v36
            tagAccum = scanTagUntil(p, &cur, len, TJS_W(';'));
            _percentCursor = cur;
            break;
        }
        }
    }
    // %; / %B / %S / 数字 共享：applyFontSize（LABEL_298：+116=size, onStyleChanged）
    void applyFontSize(float size) { // LABEL_298
        _curFontSize = size; // +116
        onStyleChanged(); // sub_5A1F28
    }
    // %B / %C / %L / %R 共享 LABEL_228：curFontSize ← bigFontSize(+152)（!ignore_size）
    void applyBigFontSizeTag() { // LABEL_228
        if(_ignoreSize) // +51
            return;
        float v98 = _curFontSize; // +116
        if(v98 < 0.0f) {
            applyFontSize(_defaultBigFontSize); // +152
            return;
        }
        if(v98 == _defaultBigFontSize) // 无变化
            return;
        applyFontSize(_defaultBigFontSize); // +152
    }
    // %S LABEL: curFontSize ← smallFontSize(+156)（!ignore_size，门控 +51）
    void applySmallFontSizeTag() {
        if(_ignoreSize) // +51（二进制 %S 用 +0x33=51）
            return;
        float v109 = _curFontSize; // +116
        if(v109 >= 0.0f) {
            if(v109 == _defaultSmallFontSize) // +156 无变化
                return;
        }
        applyFontSize(_defaultSmallFontSize); // +156
    }

    // begin/end 平衡集普通字符落字 @0x5A228C（default 分支 a3!=0 路径）。
    bool renderBalancedChar(tjs_char c, bool &v17,
                            int &v133, tjs_char &v132) { // 0x5a263c..LABEL_319
        // 是否在 begin 集（+24）
        int beginIdx = (int)_begin.IndexOf(ttstr(c)); // v63 = found?
        bool v63 = (beginIdx != -1);
        char ok = appendChar(c); // sub_5A3880
        if((ok & 1) == 0)
            return false; // LABEL_325
        if(v63) {
            // begin 字符
            if(v17 && v133 == 0) { // 首个 begin
                _lineStartX = _vertical ? _penY : _penX; // +196 = pen
                v132 = c; // 记起始 begin 字符
            }
            v17 = false;
            ++v133; // ++depth
            return true; // LABEL_320
        }
        // 非 begin → 检查 end 集（+32）
        int endIdx = (int)_end.IndexOf(ttstr(c)); // v69 found?
        if(endIdx != -1) {
            // v73 = end 集中索引；有效且 --depth==0
            if(endIdx >= 0 && --v133 == 0) {
                // 前置门 @0x5a2a5c/0x5a2a68：begin(+24) 与 end(+32) 串长度相等
                if((int)_begin.GetLen() == (int)_end.GetLen()) {
                    // 校验起始 begin 字符 v132 在 begin 集的索引 == endIdx → 配对
                    int bIdx = (int)_begin.IndexOf(ttstr(v132));
                    if(bIdx == endIdx)
                        _lineStartX = 0; // +196 = 0（LABEL_318）
                }
            }
        }
        v17 = false; // LABEL_319
        return true;
    }

    // 真 render 状态机 @0x5A228C（TextRenderBase_render）。
    //   签名 render(this, &text, x, y, flag)：flag(a5) bit0 清/续；x(a3)=begin/end 平衡集
    //   启用标志；y(a4)=charDelayStep(+192) 初值。度量/eval/onFontChange 读 objthis 成员。
    bool renderImpl(const ttstr &text, int x, int y,
                    bool flag) { // 0x5A228C
        // --- 入口复位（a5&1==0 → 清行列表 + renderDelayAccum + keyWaitList）---
        if((flag & 1) == 0) {
            _lineList.clear();    // +432..+440：sub_5A1B24 逐项析构 lineItem
            _renderDelayAccum = 0; // +188 = 0
            _keyWaitList.clear(); // +480..+488
        }
        _renderPos = 0;             // +280 = 0
        ttstr tagAccum;             // v137[0]：标签内容临时累加 ttstr（跨分支复用）
        float curFontSizeSnap = _curFontSize; // v13 = *(float*)(a1+116)，给 \w 用
        _charDelayStep = (float)y;  // +192 = (float)a4（y 参数语义=每字步进）
        // cursor / len / ptr
        const tjs_char *p = text.c_str(); // v15 = text c_str（空→off_1AA7EF8 空串）
        int len = (int)text.length();     // v14 = text length（IDA 误标 operator delete）
        int i = 0;                        // v136 = 0（cursor）
        if(i >= len)                      // 空文本：直接 finishLine 收尾
            return finishLine() & 1; // LABEL_321/322

        bool v17 = true;  // begin-run 起点标志（首字符或换行后置 1）
        int v133 = 0;     // begin/end 嵌套深度计数
        tjs_char v132 = 0; // begin-run 起始字符（用于 end 匹配校验）

        while(i < len) {
            int ch = (unsigned short)p[i]; // v31
            int next = i + 1; // v30 = v16+1
            i = i + 1;        // v136 = v16+1（默认推进 1；标签分支会再调整）
            tjs_char c = (tjs_char)ch;
            // 注意：begin/end 平衡 + 标签分发都在下面。
            bool advanced = true; // 默认走 LABEL_320 推进

            if(c == TJS_W('#')) {
                // ----- # 颜色 hex 解析 -----
                tagAccum = scanTagUntil(p, &i, len, TJS_W(';')); // sub_5A3CE4 delim=59
                if(_ignoreColor)  // +50
                    goto cont;    // → LABEL_320
                if(!tagAccum.IsEmpty())
                    parseHexColor(tagAccum); // 恒写 _curChColor（无效→|0xFF000000）
                else
                    _curChColor = _defaultChColor; // +200 = +216（空标签恢复默认）
                goto cont;
            } else if(c == TJS_W('$')) {
                // ----- $ eval/face-run：标签内容当表达式 eval → 返回串逐字 append -----
                tagAccum = scanTagUntil(p, &i, len, TJS_W(';')); // sub_5A3CE4 delim=59
                ttstr evalResult = evalDollarTag(tagAccum); // sub_5A4148
                tagAccum = evalResult;
                int n = (int)tagAccum.length(); // v50 = operator delete(...)
                if(n < 1)
                    goto cont;
                const tjs_char *q = tagAccum.c_str(); // v51
                for(int k = 0; k < n; ++k) {
                    if((appendChar(q[k]) & 1) == 0) {
                        // appendChar 失败 → LABEL_325 中断
                        return false;
                    }
                }
                // 全部展开成功（++v64>=v50 @0x5a295c → LABEL_319）：v17=0。
                v17 = false;
                goto cont;
            } else if(c == TJS_W('%')) {
                // ----- % 样式控制 -----
                if(next >= len) // (int)v30 >= len → 标签不完整，结束
                    goto cont;
                int v36 = i + 1;       // v36 = v16+2（%code 之后位置）
                i = next + 1;          // v136 = v29+2（默认推进过 %X）
                tjs_char code = p[next]; // v15[v30]
                _percentCursor = i;    // 缺省 cursor（无标签扫描的子码用它）
                renderPercentTag(p, len, code, next, v36, tagAccum,
                                 y);
                i = _percentCursor;    // 同步标签扫描推进的 cursor
                goto cont;
            } else if(c == TJS_W('&')) {
                // ----- & ：消费标签内容（到 ';'），无字段副作用 -----
                tagAccum = scanTagUntil(p, &i, len, TJS_W(';')); // sub_5A3CE4 delim=59
                // goto LABEL_319：v17=0 后续接
                v17 = false;
                goto cont;
            } else if(c == TJS_W('[')) {
                // ----- [ ：ruby 括号，消费到 ']'，!ignore_ruby 仅 refcount no-op -----
                tagAccum = scanTagUntil(p, &i, len, TJS_W(']')); // sub_5A3CE4 delim=93
                (void)_ignoreRuby; // +56
                goto cont;
            } else if(c == TJS_W('\\')) {
                // ----- \ 布局指令 -----
                if(next >= len) // (int)v30 >= len
                    goto cont;
                i = next + 1; // v136 = v16+2
                tjs_char code = p[next]; // v15[v30]
                if(code == TJS_W('i')) {
                    // \i：lineStartX(+196) = 当前 pen（竖排 penY/+236，横排 penX/+232）
                    _lineStartX = _vertical ? _penY : _penX;
                    goto cont;
                } else if(code == TJS_W('k')) {
                    // \k：keyWait push renderCount(+84) 到低 int（time 高 int 留 0）
                    _keyWaitList.push_back(KeyWaitItem{ _renderCount, 0 });
                    v17 = false;
                    goto cont;
                } else if(code == TJS_W('n')) {
                    // \n（反斜杠 n）：finishLine；失败→中断
                    if((finishLine() & 1) == 0)
                        return false; // LABEL_325
                    goto cont;
                } else if(code == TJS_W('r')) {
                    // \r：lineStartX(+196) = 0
                    _lineStartX = 0;
                    goto cont;
                } else if(code == TJS_W('t')) {
                    // \t：appendChar(9)（制表）
                    char ok = appendChar(9);
                    v17 = false; // LABEL_116
                    if((ok & 1) == 0)
                        return false; // LABEL_116→LABEL_322（v57=0，不调 finishLine）
                    goto cont;
                } else if(code == TJS_W('w')) {
                    // \w：pen += curFontSizeSnap（竖排 penY/+236，横排 penX/+232）
                    if(_vertical)
                        _penY = curFontSizeSnap + _penY;
                    else
                        _penX = curFontSizeSnap + _penX;
                    v17 = false;
                    goto cont;
                } else if(code == TJS_W('x')) {
                    // \x：goto LABEL_319（仅 v17=0 后续接，**不**终止渲染）
                    v17 = false;
                    goto cont;
                } else {
                    // 其它 \ ：忽略（default → LABEL_320）
                    goto cont;
                }
            } else if(ch == 10) {
                // ----- 裸 0x0A 换行 → finishLine -----
                if((finishLine() & 1) == 0)
                    return false; // LABEL_325
                v17 = true;       // begin-run 复位
                goto cont;
            } else {
                // ----- 普通字符 -----
                if(!x) {
                    // x==0：无 begin/end 平衡，直接 appendChar
                    char ok = appendChar(c); // sub_5A3880
                    v17 = false; // LABEL_116
                    if((ok & 1) == 0)
                        return false; // LABEL_116→LABEL_322（v57=0，不调 finishLine）
                    goto cont;
                }
                // x!=0：begin/end 平衡集逻辑
                if(!renderBalancedChar(c, v17, v133, v132))
                    return false; // appendChar 失败 → LABEL_325
                goto cont;
            }
        cont:
            // LABEL_320：i 已由分支推进；若到末尾 → finishLine 收尾
            (void)advanced;
            if(i >= len)
                return finishLine() & 1; // LABEL_321/322
        }
        // 循环正常退出（i>=len 在 cont 已处理；兜底）
        return finishLine() & 1;
    }
};

} // namespace textrender

using textrender::TextRenderBase;

// ============================================================
// NCB 注册（模块 TextRender.dll；全部 objectMember/flags=0，§5）。
//   §10 方案 A：用 ncbind 既有 Factory(objthis 注入) + typed 成员复刻二进制同拓扑
//   （= 同一 ncbind 模板按签名实例化的 invoker），零改 ncbind.hpp。
//   typed/raw 划分：render 因 bespoke 封送（numparams>=3、param[3] AsReal 丢弃、
//   param[4] boolCoerce）保留 RAW——但其二进制槽（off_1A0BE48 slot2）外层仍是共享
//   raw 包装模板 sub_5A77F4，手写的只是 Process @0x59FC28，故本地 raw 回调第 4 参取
//   TextRenderBase* 让 ncbRawCallbackMethod<T*> 同款模板接管实例取得；其余 15 个
//   method 走 ncbind typed invoker 模板（错误码/封送自然对齐）。
// ============================================================
NCB_REGISTER_CLASS(TextRenderBase) {
    // 构造器成员 = TextRenderBase_ncb_constructor @0x59D160：TJS `new` 时
    //   `new(0x250); ctor(obj, objthis)`。本地 Factory(&factory) 复刻 objthis 注入
    //   数据流（工厂 `new TextRenderBase(objthis)` → ctor 首句 +0=objthis）。
    Factory(&TextRenderBase::factory);

    // ---- 16 methods（+ 上面 1 构造器 = 二进制 17 个 method-tag 成员）----
    NCB_METHOD(setOption);                       // 0x59D2AC (typed dict)
    NCB_METHOD(setDefault);                      // 0x59DEA8 (typed dict)
    NCB_METHOD(setRenderSize);                   // 0x59EB70 (typed float,float)
    NCB_METHOD(clear);                           // 0x59EC6C (typed no-arg)
    NCB_METHOD(resetFont);                       // 0x59EEE0 (typed no-arg)
    NCB_METHOD(resetStyle);                      // 0x59EFBC (typed no-arg)
    NCB_METHOD(setFont);                         // 0x59EFD8 (typed dict)
    NCB_METHOD(setStyle);                        // 0x59F7AC (typed dict)
    NCB_METHOD_RAW_CALLBACK(render, &Class::render, 0); // 0x59FC28 (bespoke RAW)
    NCB_METHOD(newline);                         // 0x59FECC (typed no-arg)
    NCB_METHOD(done);                            // 0x59FEE4 (typed no-arg)
    NCB_METHOD(onEval);                          // 0x5A0294 (typed expr→variant)
    NCB_METHOD(getKeyWait);                      // 0x5A02DC (typed ()→Array)
    NCB_METHOD(calcLineOffset);                  // 0x5A05FC (typed int→float)
    NCB_METHOD(calcShowCount);                   // 0x5A0644 (typed int→int)
    NCB_METHOD(getCharacters);                   // 0x5A0694 (typed int,int→Array)

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
