//
// textrender.dll — TextRenderBase 原生类（最小可加载骨架）
//
// 复刻自 libkrkr2.so（Android kirikiroid2）textrender.dll 插件。
// 权威反编译归档：analysis/textrender_textrenderbase_registration.md
//
// 模块注册链   TextRenderBase_moduleRegister      @0x42D01C  (L"TextRender.dll"/L"TextRenderBase")
// 成员注册     TextRenderBase_ncb_registerMembers @0x59BCCC  (1 ctor + 16 method + 33 property)
// 构造器成员   TextRenderBase_ncb_constructor     @0x59D160  (TJS new 时立即 new(0x250)+ctor，非惰性)
// 真构造函数   TextRenderBase_ctor                @0x5A111C  (默认值群 + 内置禁则集 + resolveFaceIndex(L"normal"))
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
    // 真构造函数 = TextRenderBase_ctor @0x5A111C。调用时机：NCB 构造器成员
    //   TextRenderBase_ncb_constructor @0x59D160 在 TJS `new` 时立即
    //   `*slot = operator new(0x250); ctor(obj, objthis)`——非惰性创建。本地
    //   NCB_CONSTRUCTOR(()) 同样在 TJS 构造时立即建 native 实例，时机一致
    //   （二进制 ctor 首句 +0=objthis 回指属 NativeClassBinder native≡dispatch
    //   平台边界，本地 ncbInstanceAdaptor 双对象由 adaptor 持有关联，见 §7.1）。
    //   标量字段默认值落在下方各字段初始化器（逐项注 @0x5A111C）；此处补 4 个
    //   禁则集字符串常量、faceHash bucket hint 与末尾 resolveFaceIndex。
    //   二进制 ctor 还有 +61=0（未识别 byte，本地无对应字段）；+60/+62..+65/
    //   +72..+92/+108/+400.. 区不初始化（依赖随后 setRenderSize→clear；本地
    //   字段零值初始化是安全加固，保留）。
    TextRenderBase() : _faceHash(10) { // bucket hint：_M_next_bkt(0xAu) @0x5a125c
        // 内置日文禁则集 4 串（UTF-16 数据契约，逐码点复刻二进制常量；
        //   ttstr_createFromWide @0x5a1158/0x5a1168/0x5a1178/0x5a1184）。
        // following @0x14C9DF8（68 码点，行头禁则：闭括/句读/长音/拗促音等；
        //   含 U+3000 全角空格，故全部用 \u 转义书写）
        _following = ttstr(
            TJS_W("\u0025\u0029\u002c\u003a\u003b\u005d\u007d\u3002\uff0c\u3001") // %),:;]}。，、
            TJS_W("\uff0e\uff1a\uff1b\u309b\u309c\u30fd\u30fe\u309d\u309e\u3005") // ．：；゛゜ヽヾゝゞ々
            TJS_W("\u2019\u201d\uff09\u3015\uff3d\uff5d\u3009\u300b\u300d\u300f") // ’”）〕］｝〉》」』
            TJS_W("\u3011\u00b0\u2032\u2033\u2103\uffe0\uff05\u2030\u3000\u0021") // 】°′″℃￠％‰<U+3000>!
            TJS_W("\u002e\u003f\u30fb\uff1f\uff01\u30fc\u3041\u3043\u3045\u3047") // .?・？！ーぁぃぅぇ
            TJS_W("\u3049\u3063\u3083\u3085\u3087\u308e\u30a1\u30a3\u30a5\u30a7") // ぉっゃゅょゎァィゥェ
            TJS_W("\u30a9\u30c3\u30e3\u30e5\u30e7\u30ee\u30f5\u30f6") // ォッャュョヮヵヶ
            TJS_W("")); // -> +8
        // leading @0x14C9E82（19 码点，行尾禁则：开括/通货记号）
        _leading = ttstr(
            TJS_W("\u005c\u0024\u0028\u005b\u007b\u2018\u201c\uff08\u3014\uff3b") // \$([{‘“（〔［
            TJS_W("\uff5b\u3008\u300a\u300c\u300e\u3010\uffe5\uff04\uffe1") // ｛〈《「『【￥＄￡
            TJS_W("")); // -> +16
        // begin @0x14C9EAA（10 码点，开括平衡集）
        _begin = ttstr(
            TJS_W("\u300c\u300e\uff08\u2018\u201c\u3014\uff3b\uff5b\u3008\u300a") // 「『（‘“〔［｛〈《
            TJS_W("")); // -> +24
        // end @0x14C9EC0（10 码点，闭括平衡集，与 begin 按索引一一配对）
        _end = ttstr(
            TJS_W("\u300d\u300f\uff09\u2019\u201d\u3015\uff3d\uff5d\u3009\u300b") // 」』）’”〕］｝〉》
            TJS_W("")); // -> +32
        // ctor 末尾：+96 = resolveFaceIndex(L"normal")（intern 进 faceHash）@0x5a12a4
        _defaultFaceIndex = resolveFaceIndex(ttstr(TJS_W("normal")));
    }
    ~TextRenderBase() = default;

    // ---- 逐字符 / 行 / face 表 ----
    //   CharItem = 二进制 80B POD（charItem）的忠实复刻。字段语义/顺序经
    //   sub_5A4838(拷贝)@0x5A4838 + appendChar@0x5A3880 + sub_5A4A7C@0x5A4A7C
    //   + calcShowCount@0x5A0644 四处反编译交叉确认（analysis §3b-1）。
    //   遵字节布局复刻工作法：写语义字段名，编译器自由算偏移；元素 POD 内部
    //   字段是数据契约（拷贝/落字按字段读写），但对象 ABI 偏移不需对齐。
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
    //   +320）与 lineList 元素（+432，stride 112）**同型**——三重证据：
    //   ① Line::clear @0x5A1E68 接收 +320 指针，deque 清空后零化相对 +80..+108
    //     （STR XZR,[X19,#0x50] + STP XZR,XZR,[X19,#0x58] + STUR XZR,[X19,#0x65]
    //      = 对象绝对 +400..+428，覆盖全部行 metric + wordBreakRun + prevWasSpace）；
    //   ② finishLine push（0x5a3758/0x5a3768 与扩容 sub_5A43E8 @0x5a44a8/0x5a44b0）
    //     都把 a2+80 / a2+93 两 OWORD（= +80..+108 全范围）拷入 lineItem+80/+93；
    //   ③ Line dtor @0x5A1B24 同为 pending 行与 lineList 元素的析构（0x5a5328/0x5a44d8）。
    //   布局：+0..+79 = std::deque<charItem>（80B 控制块）；+80..+103 = 6 float metric；
    //   +104 = int wordBreakRun；+108 = bool prevWasSpace。
    //   done@0x59FEE4 读 lineItem float[22..25] 算全局 bbox，读 +80(float[20]) 是 offset
    //   累加器；calcLineOffset@0x5A05FC 返回 lineItem +80。下表 float 索引经 done/finishLine
    //   交叉确认：+80=lineBottom(a1+400 v24), +84=lineHeight(a1+404 v11),
    //   +88=left(a1+408), +92=top(a1+412), +96=right(a1+416), +100=bottom(a1+420 等)。
    //   done 用的 bbox: v5[22]=+88(left) min, v5[23]=+92(top) min, v5[24]=+96(right) max,
    //   v5[25]=+100(bottom) max；offset(+80) 是 valign 居中量加到每 char.y。
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
        // Line::clear @0x5A1E68：deque 清空（释放元素、保留控制块/首 node——
        //   即 std::deque::clear() 语义）+ 零化 +80..+108 全部 metric/wordBreak 字段。
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
    // char 列表：二进制 +296 是 std::vector<charItem*>（8B 元素=堆指针），
    //   落字路径 new 80B charItem 后 push 指针。容器选型对齐二进制（vector<指针>）。
    std::vector<CharItem *> _charList; // 真对象 +296
    // pending 行缓冲：真对象 +320，类型 = Line（与 lineList 元素同型，证据见 Line 注释）。
    //   行 metric/wordBreak 状态是 Line 内嵌字段，不再摊平为 TextRenderBase 字段。
    Line _pendingLine;                 // 真对象 +320..+431 (112B)
    std::vector<Line> _lineList;       // 真对象 +432 (stride 112)
    std::vector<ttstr> _faceTable;       // 真对象 +456 (index→face)
    // keyWait 列表：二进制 +480 是 std::vector<KeyWaitItem>（8B 元素 = 两个 int）。
    //   元素布局经 keyWaitList_pushBack@0x5A5874（operator new(8*n)、(end-begin)>>3 计数、
    //   *(QWORD*)(slot)=*a2 存 renderCount 到低 int）+ done@0x59FEE4 keyWait 段（v39[1] =
    //   charList[v39[0]].renderPos bits，写高 int，低 int 索引不动）+ getKeyWait@0x5A02DC
    //   （LDRSW 读低 int=index）三处交叉确认。
    //   - \k 落点：index = renderCount(+84)，time = 0。
    //   - done 重写：time = charList[index].renderPos 的 float bits（高 int）。
    //   - getKeyWait 消费：pos=time=index（**低 int**，renderPos bits 高 int 不被 getKeyWait
    //     读取 → dead-but-faithful）。
    //   纠正旧 §3b/memory "4B 元素=renderPos float bits"：实为 8B 双 int，getKeyWait 读 index。
    struct KeyWaitItem {
        int index = 0; // +0 低 int：\k push renderCount；getKeyWait 读它做 pos/time
        int time = 0;  // +4 高 int：done 写 charList[index].renderPos bits（dead-for-getKeyWait）
    };
    std::vector<KeyWaitItem> _keyWaitList; // 真对象 +480
    // face hash 表（resolveFaceIndex intern：face 名→index）。二进制 +536 起是
    //   inline-bucket 链式 hashmap（sub_5A172C 查 / sub_5A181C intern，节点 {next,
    //   ttstr*, idx}）。容器实现选型用 unordered_map（intern 表语义等价，与 _faceTable
    //   互为正反），但 hash 算法忠实复刻二进制内联 hash（数据契约，见 FaceNameHash）。
    //   resolveFaceIndex@0x5A14DC 内联 hash: 逐 UTF-16 码元
    //     v7 = (1025*(v7+ch)) ^ ((1025*(v7+ch))>>6); 末尾 *9，再 *32769 折叠 ^>>11，
    //     0 → 0xFFFFFFFF（空串→0）。
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
    std::unordered_map<ttstr, int, FaceNameHash, FaceNameEq> _faceHash; // 真对象 +536

    // ---- 选项 (setOption → byte 字段 +48..+59,+112) ----
    //   字段偏移/名称经 setOption@0x59D2AC 逐 key 反编译确认（key 字符串常量取自
    //   PropGet L"..." 实参）。注意 ignore_over 与 ignore_overy 二进制同写 +54（后者
    //   覆盖前者，忠实复刻）；kinsoku_max 是 bool-coerce 写入 DWORD（存 0/1，非整数值）。
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
    int _kinsokuMax = 1;             // +112 L"kinsoku_max"（bool-coerce→0/1；ctor=1 @0x5A111C，QWORD+112=0xBF80000000000001 低 32 位）
    // 禁则字符集字符串 (setOption +8/+16/+24/+32)。二进制存 tTJSVariant*（refcounted
    //   string），仅接受 string/void，object/octet/int/real 抛转换错误。语义=ttstr。
    //   ctor @0x5A111C 以内置日文禁则集 4 串初始化（常量 0x14C9DF8/0x14C9E82/
    //   0x14C9EAA/0x14C9EC0，见构造函数体）。
    ttstr _following;                // +8  L"following"
    ttstr _leading;                  // +16 L"leading"
    ttstr _begin;                    // +24 L"begin"
    ttstr _end;                      // +32 L"end"

    // ---- 当前样式 (setFont/setStyle 改写) ----
    bool _curBold = false;           // +62
    bool _curShadow = false;         // +63
    bool _curEdge = false;           // +64
    bool _curItalic = false;         // +65
    int _curFaceIndex = 0;           // +72
    int _curAlign = 0;               // +76  setStyle L"align"（当前样式 align，≠默认+100）
    int _curValign = 0;              // +80  setStyle L"valign"（当前样式 valign，≠默认+104）
    float _curFontSize = -1.0f;      // +116 ctor=-1.0f 脏哨兵 @0x5A111C（QWORD+112 高 32 位=0xBF800000；保证首次 resetFont 组复位必触发）
    float _curRubySize = -1.0f;      // +128 ctor=-1.0f 脏哨兵 @0x5A111C（DWORD+128=0xBF800000；resetFont `<0||!=` 门控）
    float _curRubyOffset = 0;        // +132 setFont L"rubyoffset"（当前样式 ruby 偏移）
    float _curLineSpacing = 0;       // +136
    float _curPitch = 0;             // +140
    float _curLineSize = 0;          // +144
    tjs_uint32 _curChColor = 0;      // +200
    tjs_uint32 _curShadowColor = 0;  // +204
    tjs_uint32 _curShadowDiff = 0;   // +208
    tjs_uint32 _curEdgeColor = 0;    // +212

    // ---- 默认样式 (setDefault 改写；resetFont/Style 复位为这些) ----
    //   二进制 +96 是 default face INDEX（int，setDefault 经 resolveFaceIndex 写入）。
    //   defaultFace 属性 getter/setter 经 _faceTable/resolveFaceIndex 间接读写此 index
    //   （0x5A0DA8/0x5A0E0C 确证），故无独立 _defaultFace ttstr 字段（旧占位已删，避免幻影字段）。
    int _defaultFaceIndex = 0;           // +96  setDefault L"face" → resolveFaceIndex
    //   ctor 默认值群 @0x5A111C（打包常量逐项展开）：
    //   - BYTE+66=0 / WORD+67=0x0001 / BYTE+69=0 → bold=0,shadow=1,edge=0,italic=0
    //   - QWORD+100=-1 → align=-1,valign=-1
    //   - QWORD+148=0x4240000041C00000 → fontSize=24,bigFontSize=48
    //   - OWORD+156=xmmword_14C95D0=(12,10,-2,6)f → small=12,rubySize=10,rubyOffset=-2,lineSpacing=6
    //   - OWORD+172=xmmword_14C95E0=(0,24,1,1)f → pitch=0,lineSize=24（后两 lane 是 timeScale/fontScale）
    //   - OWORD+216=xmmword_14C95F0 → chColor=0xFFFFFFFF,shadowColor=0xFF000000,shadowDiff=1,edgeColor=0xFF0080FF
    bool _defaultBold = false;           // +66
    bool _defaultShadow = true;          // +67
    bool _defaultEdge = false;           // +68
    bool _defaultItalic = false;         // +69
    int _defaultAlign = -1;              // +100
    int _defaultValign = -1;             // +104
    float _defaultFontSize = 24.0f;      // +148
    float _defaultBigFontSize = 48.0f;   // +152
    float _defaultSmallFontSize = 12.0f; // +156
    float _defaultRubySize = 10.0f;      // +160
    float _defaultRubyOffset = -2.0f;    // +164
    float _defaultLineSpacing = 6.0f;    // +168
    float _defaultPitch = 0;             // +172
    float _defaultLineSize = 24.0f;      // +176
    tjs_uint32 _defaultChColor = 0xFFFFFFFF;     // +216
    tjs_uint32 _defaultShadowColor = 0xFF000000; // +220
    tjs_uint32 _defaultShadowDiff = 1;           // +224
    tjs_uint32 _defaultEdgeColor = 0xFF0080FF;   // +228

    // ---- 全局缩放 ----
    float _timeScale = 1.0f;         // +180 ctor=1.0f @0x5A111C（xmmword_14C95E0 lane2）
    float _fontScale = 1.0f;         // +184 ctor=1.0f @0x5A111C（xmmword_14C95E0 lane3）
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
    ttstr _renderText;               // +40 (tTJSVariant) 落字累积文本（finishLine 追加换行/缩进）

    // ---- 落字/行布局运行时状态（appendChar/kinsoku/finishLine/clear 用，§3b 偏移对照）----
    // 字段语义经 clear@0x59EC6C / appendChar@0x5A3880 / kinsoku@0x5A4A7C /
    //   finishLine@0x5A34B8 反编译交叉确认，非字段名推导。
    float _penX = 0;                 // +232 横排 pen X（竖排：列 X）
    float _penY = 0;                 // +236 横排 pen Y（竖排：行内 Y）
    float _renderPos = 0;            // +280 当前落字累积渲染位置（renderPos 源）
    float _renderPosSnap = 0;        // +284 renderPos 快照
    int   _state288 = 0;             // +288 clear 置 0（杂项状态）
    int   _state92 = 0;             // +92  clear 置 0（杂项状态）
    float _charDelayStep = 1.0f;     // +192 每字 renderPos 步进（ctor=1.0f @0x5A111C；render %d 标签设置；clear@0x59EC6C STUR XZR,[#0xBC] 8B 连 +188 一起清零）
    float _lineStartX = 0;           // +196 行首 X（换行后 pen X 复位目标；clear 置 0）
    int   _charBufCountdown = 0;     // +88  组合字符累积倒计数（clear 置 0）
    int   _kinsokuUsed = 0;          // +108 本行 kinsoku 已用次数（clear 置 0）
    // 行 metric 累加器 / word_break trailing-run 状态：是 _pendingLine（+400..+428）的
    //   内嵌字段，不是 TextRenderBase 摊平字段（旧摊平实现被 0x5A1E68/0x5A43E8 证伪，
    //   已并入 struct Line，见上）。
    // ruby bbox 累加器（appendChar ruby 分支写 +264/+268/+272）
    float _rubyTop = 0;              // +268
    float _rubyLeft = 0;            // +264
    float _rubyRight = 0;           // +272
    // 当前 ruby 文本（render %... 设置，appendChar 消费后释放；clear 释放）
    ttstr _curRubyText;             // +528
    bool  _hasCurRubyText = false;  // +528 != 0 哨兵
    // 内部 UTF-16 累积 buffer（+504/+512/+520 begin/end/cap，2B 元素）。源码层 = 字符 vector。
    std::vector<tjs_char> _accumBuf; // +504
    // render % 子码标签扫描的 cursor 回传槽（非二进制字段——二进制 render 是单函数，
    //   cursor v136 是栈局部；本地把 % 分发拆成成员函数 renderPercentTag，需把推进后的
    //   cursor 传回主循环，故引入此瞬态槽。实现细节，不进数据契约）。
    int _percentCursor = 0;

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
    // defaultFace @0x5A0DA8(get) / 0x5A0E0C(set)：**INDEX-based**（后备字段是
    //   _defaultFaceIndex/+96，非独立 ttstr）。getter 查 _faceTable[+96]（越界→空串
    //   byte_1506A57）；setter 经 resolveFaceIndex 写 +96。1:1 复刻反编译。
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
    //   - lineCount = (+440-+432)/112；count<1 → 0.0。
    //   - v5 = vertical ? renderSizeW(+240) : renderSizeH(+244)；v6 = *v5（视口尺寸）。
    //   - i 自 lineList[count-1].lineHeight(+84) 起，每次 i-=112B（向前一行）读 lineHeight：
    //       v6 -= lineHeight; if(v6<0) break; --v4; if(count+v4<=0) return 1.0。
    //   - break 后：v4!=0 → return count+v4；否则 fall-through → 0.0。
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
    // 简单签名方法（typed）— 桩
    // ============================================================
    // setRenderSize @0x59EB70：写 +240/+244 后调 clear。clear→resetFont 可触发
    //   onStyleChanged，故内部链路需 objthis。
    void setRenderSizeImpl(iTJSDispatch2 *objthis, float w, float h) { // 0x59EB70
        _renderSizeW = w; // +240
        _renderSizeH = h; // +244
        clearImpl(objthis); // sub_59EC6C
    }
    static tjs_error setRenderSize(tTJSVariant *, tjs_int numparams,
                                   tTJSVariant **param,
                                   iTJSDispatch2 *objthis) {
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        t->setRenderSizeImpl(objthis, (float)param[0]->AsReal(),
                             (float)param[1]->AsReal());
        return TJS_S_OK;
    }
    // clear @0x59EC6C — 复位全部渲染状态、重建列表、压缩 face 表为仅 default face。
    //   数据流 1:1 复刻反编译（pen/bbox 竖排横排分支、cur 样式从 default 复位、
    //   face 表重 intern）。容器选型：STL clear 复刻源码 deque/vector 清空。
    void clearImpl(iTJSDispatch2 *objthis) { // 0x59EC6C
        // sub_5A1E68(+320)：pending Line 清空（deque 清空 + +400..+428 metric 全零化）。
        //   竖排分支随后写 bbox 初值，顺序与二进制一致（0x59ec98 → 0x59ecb4/0x59ecb8）。
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
        resetFontImpl(objthis); // sub_59EEE0
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
        _charDelayStep = 0;   // +192  （同上一条 8B store；证伪旧注释"clear 不重置 +192"）
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
    // clear NCB raw wrapper（clear→resetFont 可触发 onStyleChanged，需 objthis）。
    static tjs_error clear(tTJSVariant *, tjs_int, tTJSVariant **,
                           iTJSDispatch2 *objthis) {
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        t->clearImpl(objthis);
        return TJS_S_OK;
    }
    // resetFont @0x59EEE0：当前样式从 default* 复位。
    //   face/bold/italic/fontsize 作为一组——三路变化检测：curFaceIndex(+72)!=default(+96)
    //   → 或 curBold(+62)!=default(+66) → 或 (curItalic(+65)!=default(+69) ||
    //   defaultFontSize(+148)!=curFontSize(+116))，任一命中 → 全组复位(+72/+116/+62/+65)
    //   + onStyleChanged；全等 → 跳过（**无回调**）。rubySize 单独门控复位(<0||!=)；
    //   rubyOffset/shadow/edge + 4 色 DWORD 块无条件复位。
    //   纠正旧实现：旧版无条件平铺复位、漏 _curFaceIndex/_curRubyOffset、无变化门控、
    //   无 onStyleChanged（0x59EEE0 反编译证伪）。
    //   平台边界：需 objthis 回调 onStyleChanged（二进制 native≡dispatch，直接用 a1）。
    void resetFontImpl(iTJSDispatch2 *objthis) { // 0x59EEE0
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
            onStyleChanged(objthis);             // sub_5A1F28
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
    // resetFont NCB raw wrapper（需 objthis 触发 onStyleChanged）。
    static tjs_error resetFont(tTJSVariant *, tjs_int, tTJSVariant **,
                               iTJSDispatch2 *objthis) {
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        t->resetFontImpl(objthis);
        return TJS_S_OK;
    }
    // resetStyle @0x59EFBC：5 个字段从 default 复位。**不调 resetFont、无 onStyleChanged**
    //   （纠正旧实现误调 resetFont 且漏 align/valign；0x59EFBC 反编译证伪）。
    void resetStyle() { // 0x59EFBC
        _curLineSpacing = _defaultLineSpacing; // +136 = +168
        _curPitch = _defaultPitch;             // +140 = +172
        _curLineSize = _defaultLineSize;       // +144 = +176
        _curAlign = _defaultAlign;             // +76 = +100
        _curValign = _defaultValign;           // +80 = +104
    }
    // ============================================================
    // 落字 / 行布局层（appendChar/度量/kinsoku/finishLine）helper
    // ============================================================
    // 释放当前 ruby 文本（+528 release → null）。
    void releaseCurRubyText() {
        _curRubyText = ttstr();
        _hasCurRubyText = false;
    }

    // 字宽度量回调 sub_5A426C@0x5A426C：FuncCall(L"onGetTextWidth", text[str], size[real])，
    //   返回值按 result.type 强制转 double（忠实移植，脚本层取字宽——非平台边界）：
    //   String→AsReal、Integer→(double)、Real→raw、Object/Octet→抛错、Void/失败→0.0。
    //   脚本未提供该回调（FuncCall 失败）→ result 保持 void → 0.0（fallback 分支）。
    static float onGetTextWidth(iTJSDispatch2 *objthis, const ttstr &text,
                               float size) { // sub_5A426C
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
        // switch(result.type) 复刻 0x5A4338 case 表：
        //   void(0)→0.0；string(2)→sub_A133A8 / int(4)→(double) / real(5)→raw 均=AsReal；
        //   object(1)/octet(3)→二进制 sub_A0E48C(__noreturn) 抛转换错误——本地经
        //   tTJSVariant::AsReal 对 object/octet 同样抛 TJS 转换异常（忠实复刻 throw，
        //   纠正旧实现以 0.0 吞掉异常）。
        if(result.Type() == tvtVoid)
            return 0.0f;
        return (float)result.AsReal();
    }

    // appendChar @0x5A3880：累积 UTF-16 buffer + 度量 + ruby + 落字入口。
    //   返回 bool（true=继续/落字成功，false=finishLine 失败）。a1+88 是组合字符
    //   累积倒计数：每次 push 字符后 --countdown，>=0 时仅累积不落字；buffer 非单字符
    //   也不落字（return false）。恰好 1 字符且计数耗尽 → 度量构造 charItem → kinsoku 落字。
    bool appendChar(iTJSDispatch2 *objthis, tjs_char ch) { // 0x5A3880
        // push ch 到内部 UTF-16 buffer（+504/+512），容器选型 = char vector
        _accumBuf.push_back(ch);
        // 倒计数(+88)：先算 v21=(+88)-1，仅当 v21>=0 才回写并返回 true
        //   （0x5a3970..0x5a3984：负值不回写，+88 永不为负）。
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
        float cw = onGetTextWidth(objthis, text, effSize); // sub_5A426C → v23/v49
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
                onGetTextWidth(objthis, _curRubyText,
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
        return kinsoku(objthis, v); // sub_5A4A7C(a1, &v48)
    }

    // 落字 + kinsoku 禁则 @0x5A4A7C：把 char 落到 pending deque（+320），处理行尾/行首
    //   禁则（following/leading 字符集）与 word_break，必要时换行（finishLine）并把回退
    //   字符重排到下一行。返回 bool（true=成功，false=finishLine 失败）。
    //   字段门控全部 1:1 复刻反编译，非字段名推导（_vertical/_ignoreOverX(+53)/_ignoreOverY
    //   (+54)/_wordBreak(+49)/_kinsokuUsed(+108)/_kinsokuMax(+112)/_pendingLine.wordBreakRun(+424)）。
    bool kinsoku(iTJSDispatch2 *objthis, CharItem &c) { // 0x5A4A7C
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
                            //   0x5a50a0/0x5a50a8 两路（pushFrontNode/charItem_copy）
                            //   汇入与 LABEL_94 tail-merge 的共享尾
                            //   （charItem_destroy@0x5a52cc + --(+84)@0x5a52d8）：
                            //   下移时同样 --renderCount（仅 +84，不动 +108）。
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
                    //   **末字符**（0x5A4DD0 `[+368]-0x50`，即 deque.back）是否在
                    //   **following 集(+8)**：命中或 pending 空 → LABEL_107（finishLine
                    //   换行）；未命中 → @0x5A5338 直接落字，**不 finishLine、不换行**。
                    //   纠正旧实现：旧版无条件 fall 到 finishLine，对"末字符不在 following
                    //   集"的情形产生多余换行（旧注释自承"含糊"，经 0x5A4D90 反编译证伪）。
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
            if(!finishLine(objthis)) // sub_5A34B8
                return false;        // → LABEL_113
            // drain tmp：把回退字符重排到下一行（自递归 kinsoku）
            for(size_t i = 0; i < tmp.size(); ++i) {
                if(!kinsoku(objthis, tmp[i]))
                    return false; // → LABEL_113
            }
            // 换行后落字（v6 = !vertical）
            return placeChar(c, /*placeHoriz=*/!_vertical); // 0x5a5348
        }
    }

    // placeChar — kinsoku LABEL_10：把 char 落到 pending deque + 推进 pen + renderPos。
    //   placeHoriz=v6（true→horizontal：char.y=penY-size；false→vertical：char.y=penY）。
    bool placeChar(CharItem &c, bool placeHoriz) { // LABEL_10
        // char.x(+8) = penX(+232)
        c.x = _penX;
        // char.y(+12)：horizontal → penY - size（v6=1 / placeHoriz）；vertical → penY
        c.y = placeHoriz ? (_penY - c.size) : _penY; // v10
        // char.renderPos(+24) = renderPos(+280)（v11；始终 = renderPos，不取 max）
        c.renderPos = _renderPos; // v11 = a1+280；*(a2+24) = v11
        // delayAccum(+188) = max(delayAccum, renderPos)（v9 默认指 char.renderPos；
        //   仅当 delayAccum > renderPos 时 v9 指 a1+188 → delayAccum = max(两者)。
        //   注意：char.renderPos 本身保持 = renderPos，不被改成 max）。
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

    // word_break 落字后状态更新（kinsoku LABEL_16 / 0x5a4b8c）：检测当前字是否空格(L" ")，
    //   更新 +428(prevWasSpace)；若上一字是空格，则把 +424(wordBreakRun) 记为当前 pending size。
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
    //   返回 bool（true=成功；over 且 !ignore_over → false）。1:1 复刻反编译。
    bool finishLine(iTJSDispatch2 *objthis) { // 0x5A34B8
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
            // 3b. align 缩进：v14>0 时按全角空格宽填充行首到 renderText
            //   仅当 v13!=center-with-zero 路径... 二进制：v16>=1 时循环追加全角空格(0x3000)。
            // 进入缩进段的唯一条件 = pending 非空（0x5A35A0/0x5A35B8 `CMP X8,X24` →
            //   0x5A35C0），与 v14 是否为 0 无关。纠正旧实现多出的 `v14 != 0.0f` 门控
            //   （left-align v14=0 但首字符 x≥全角空格宽时二进制仍缩进，0x5A35A0 证伪）。
            if(!_pendingLine.chars.empty()) {
                // v15 = onGetTextWidth(L"　"=0x3000, fontScale*fontsize); ==0 → fontsize
                float v15 = onGetTextWidth(objthis, ttstr((tjs_char)0x3000),
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
            // push：整个 pending Line 拷入 lineList（sub_5A4588 拷 deque +
            //   0x5a3758/0x5a3768 两 OWORD 拷 +80..+108 全 metric 含 wordBreakRun/
            //   prevWasSpace；扩容路径 sub_5A43E8 同）。源码层 = push_back(pendingLine)。
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

    // newline @0x59FECC：pending deque 非空（finish.cur(+368) != start.cur(+336)）→ finishLine。
    //   平台边界：二进制 native this 即 objthis（NativeClassBinder 风格，native+0=iTJSDispatch2
    //   base），度量回调 sub_5A426C 用 native+0 调 FuncCall。本地走 ncbInstanceAdaptor，
    //   native(TextRenderBase C++) 与 objthis(TJS dispatch) 是两个对象，故内部落字函数
    //   须显式接收 objthis 以回调脚本 onGetTextWidth/onFontChange。
    void newlineImpl(iTJSDispatch2 *objthis) { // 0x59FECC
        if(!_pendingLine.chars.empty()) // a1+368 != a1+336
            finishLine(objthis);        // sub_5A34B8
    }
    static tjs_error newline(tTJSVariant *, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *objthis) { // ncb wrapper
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        t->newlineImpl(objthis);
        return TJS_S_OK;
    }
    static tjs_error done(tTJSVariant *, tjs_int, tTJSVariant **,
                          iTJSDispatch2 *objthis) { // ncb wrapper (0x59FEE4)
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        t->doneImpl(objthis);
        return TJS_S_OK;
    }

    // done @0x59FEE4：终结布局。① pending 非空 → finishLine ② 遍历行列表算全局 bbox
    //   ③ valign 偏移加到每 char.y + 调整全局 top/bottom ④ charList 从各行 deque 铺
    //   charItem 指针 ⑤ keyWait 列表 index→renderPos 回填 ⑥ charList 按 renderPos 排序。
    //   1:1 复刻反编译；字段语义经交叉确认（lineItem bbox float[22..25]，char.y=+12，
    //   renderSizeH=+244，valign=+80，charItem.renderPos=+24）。
    void doneImpl(iTJSDispatch2 *objthis) { // 0x59FEE4
        // ① pending 非空 → finishLine（a1[46]!=a1[42] = +368!=+336）
        if(!_pendingLine.chars.empty())
            finishLine(objthis); // sub_5A34B8
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
        //   本地用 std::sort（libstdc++ std::sort 即 introsort，对相等 renderPos 同为
        //   不稳定，比旧版 stable_sort 更忠实）。
        std::sort(_charList.begin(), _charList.end(),
                         [](const CharItem *a, const CharItem *b) {
                             return a->renderPos < b->renderPos;
                         });
    }
    // float bits 重解释为 int（done keyWait 段：renderPos float 存入 int 槽是按 bits，
    //   getKeyWait 读回时再 bit-reinterpret 为 float —— 数据契约，1:1 复刻 *(v40+8*idx+24)
    //   原样写入 int 数组）。
    static int reinterpretFloatBits(float f) {
        int i;
        memcpy(&i, &f, sizeof(i));
        return i;
    }

    // calcLineOffset @0x5A05FC：返回行列表第 lineIdx 项的 offset(+80)；越界返回 bottom。
    //   二进制越界判定用无符号比较 (count <= (unsigned)a2)，故负 idx 也判越界。
    double calcLineOffset(tjs_int lineIdx) { // 0x5A05FC
        // count = (+440 - +432) / 112；二进制 unsigned 比较 lineCount <= (u64)lineIdx
        if((tjs_uint64)_lineList.size() <= (tjs_uint64)(tjs_uint32)lineIdx)
            return _renderBottom; // a1+260 (§4)
        return _lineList[lineIdx].lineBottom; // +432 + 112*idx + 80 (lineItem float[20])
    }
    // calcShowCount @0x5A0644：char 列表(+296)倒扫，找在给定 width 内可显示的字符数。
    //   count = (+304-+296)/8；若 count<=1 返回 0。timeScale=+180。
    //   从末项倒扫：while(charItem->renderPos(+24) * timeScale > width) { --count; 若到头返回 0 }。
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
    // 内部 helper（非 NCB 成员）
    // ============================================================
    // resolveFaceIndex @0x5A14DC：face 名 → 稳定 index（intern）。
    //   已 intern → 返回旧 idx；未 intern → 分配 idx = 当前 _faceTable.size()
    //   ((+464-+456)>>3) 并存入 _faceHash。注意：本函数不向 _faceTable push，
    //   仅在 hash 表登记。
    int resolveFaceIndex(const ttstr &name) { // 0x5A14DC
        auto it = _faceHash.find(name); // sub_5A172C by FaceNameHash
        if(it != _faceHash.end())
            return it->second; // 命中节点 idx (+16)
        int idx = (int)_faceTable.size(); // (a1[58]-a1[57])>>3
        _faceHash.emplace(name, idx); // sub_5A181C intern：节点 idx 写为 v15
        // **不向 _faceTable push**：经 field-level 穷尽核实（288 处 #464 store + 全部 6
        //   个 caller + intern 子函数 sub_5A181C 反编译），二进制从无 faceTable push——
        //   face 名只进 faceHash 节点(+536)，faceTable(+456) 恒空，故 idx=size() 恒为 0，
        //   所有 face 退化为 idx 0（原版退化行为，1:1 忠实复刻）。clear/onStyleChanged/
        //   getCharacters 对 _faceTable[idx] 的读取在二进制亦为恒空 vector 的 inert 读
        //   （编译器为通用 vector<ttstr*> 字段生成的标准访问，运行时因恒空而零迭代/空串）。
        //   纠正旧实现：旧 push 使 faceTable 非空、face idx 分叉（注释"push 在调用方"被证伪）。
        return idx;
    }

    // onStyleChanged @0x5A1F28：当前样式变化后，构造 dict{face, bold, italic}
    //   并对脚本对象 FuncCall(L"onFontChange", dict)。纯 TJS dispatch。
    //   face 值 = _faceTable[_curFaceIndex]（越界则空串，对齐二进制 sub_A13878(&byte) 空串路径）。
    void onStyleChanged(iTJSDispatch2 *objthis) { // 0x5A1F28
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
        //   arg variant {Object=dict, ObjThis=null}（0x5a2050 v13=1 /
        //   0x5a2054 v12[0]=v6, v12[1]=0）——objthis 槽为 null。
        tTJSVariant vDict(dict, (iTJSDispatch2 *)nullptr);
        dict->Release();
        tTJSVariant *args[1] = { &vDict };
        objthis->FuncCall(0, TJS_W("onFontChange"), nullptr, nullptr, 1, args,
                          objthis);
    }

    // ============================================================
    // 复杂签名方法（raw callback）— 桩
    //   签名：static tjs_error fn(result, numparams, param, objthis)
    // ============================================================
    static TextRenderBase *self(iTJSDispatch2 *objthis) {
        return ncbInstanceAdaptor<TextRenderBase>::GetNativeInstance(objthis,
                                                                     true);
    }

    // ============================================================
    // dict 解析层的三套值强制转换（1:1 复刻 setOption/setDefault/setFont/setStyle
    //   反编译里的 switch(type) 内联体）。type tag = tTJSVariantType
    //   (Void=0,Object=1,String=2,Octet=3,Integer=4,Real=5)。
    //   - boolCoerce  = 二进制 bool 开关：case1/3/4→qword==0、case2→str→int==0、
    //     case5→real==0.0、default(void)→false。等价 (bool)variant（operator bool）。
    //   - intCoerce   = 二进制 int/color 开关：object/octet 走 sub_A0E48C(throw)、
    //     string→sub_A13294、int→raw、real→(int)、void→0。等价 variant.AsInteger()。
    //   - realCoerce  = 二进制 float 开关：object/octet→throw、string→sub_A133A8、
    //     int→(double)、real→raw、void→0.0。等价 variant.AsReal()。
    //   sub_A0E48C 实为 TJSThrowVariantConvertError(__noreturn)，故 object/octet 在
    //   int/real 强制转换里抛错——与 AsInteger/AsReal 语义一致。
    static bool boolCoerce(const tTJSVariant &v) { return (bool)v; }
    static tjs_int intCoerce(const tTJSVariant &v) {
        return (tjs_int)v.AsInteger();
    }
    static float realCoerce(const tTJSVariant &v) { return (float)v.AsReal(); }

    // dict 逐 key PropGet(TJS_MEMBERMUSTEXIST=0x400, L"key")（= 二进制 vtable+32，
    //   flag 1024）。key 不存在 → 高位置位 → 返回 false（调用方跳过该 key）。
    static bool dictGet(iTJSDispatch2 *dict, const tjs_char *key,
                        tTJSVariant *out) {
        return TJS_SUCCEEDED(
            dict->PropGet(TJS_MEMBERMUSTEXIST, key, nullptr, out, dict));
    }
    // 取参数 0 作为 dict 对象。二进制（setOption@0x59d2e4/setDefault@0x59dee0/
    //   setFont@0x59f010/setStyle 同构）：sub_A0F5E0 拷贝 → 若 type!=1 则
    //   sub_A0E48C(v, 1u) = TJSThrowVariantConvertError(v, tvtObject)。
    //   type==object 但指针为 null 时二进制 v3=0（后续 PropGet 即崩）；本地返回
    //   nullptr 由调用方守护（平台守护，不改变合法路径行为）。
    static iTJSDispatch2 *paramAsDict(tjs_int numparams, tTJSVariant **param) {
        if(numparams < 1 || !param || !param[0])
            return nullptr; // NCB 封送守护（二进制 native 直收 variant）
        if(param[0]->Type() != tvtObject)
            TJSThrowVariantConvertError(*param[0], tvtObject); // sub_A0E48C(,1)
        return param[0]->AsObjectNoAddRef();
    }

    // ============================================================
    // setOption @0x59D2AC：(dict) → 选项 byte 字段 +48..+59,+112 + 禁则字符串 +8..+32
    //   逐 key 顺序、key 字符串常量、读取条件、类型强制均 1:1 复刻反编译。
    //   - following/leading/begin/end：string→其值、void→空串；
    //     object/octet/int/real → TJSThrowVariantConvertError(String)。
    //   - 其余 byte 字段：boolCoerce；kinsoku_max 同 boolCoerce 但写 DWORD(0/1)。
    // ============================================================
    static tjs_error setOption(tTJSVariant *, tjs_int numparams,
                               tTJSVariant **param,
                               iTJSDispatch2 *objthis) { // 0x59D2AC
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        iTJSDispatch2 *dict = paramAsDict(numparams, param);
        if(!dict)
            return TJS_E_INVALIDOBJECT;
        tTJSVariant v;
        // --- 禁则字符串：string/void（store ttstr，见 §3b +8/+16/+24/+32）---
        t->setOptionStr(dict, TJS_W("following"), t->_following);
        t->setOptionStr(dict, TJS_W("leading"), t->_leading);
        t->setOptionStr(dict, TJS_W("begin"), t->_begin);
        t->setOptionStr(dict, TJS_W("end"), t->_end);
        // --- byte/DWORD 选项（boolCoerce）：顺序同二进制 ---
        if(dictGet(dict, TJS_W("vertical"), &v))
            t->_vertical = boolCoerce(v);          // +48
        if(dictGet(dict, TJS_W("kinsoku_max"), &v))
            t->_kinsokuMax = boolCoerce(v) ? 1 : 0; // +112 DWORD (bool-coerce)
        if(dictGet(dict, TJS_W("word_break"), &v))
            t->_wordBreak = boolCoerce(v);         // +49
        if(dictGet(dict, TJS_W("ignore_color"), &v))
            t->_ignoreColor = boolCoerce(v);       // +50
        if(dictGet(dict, TJS_W("ignore_size"), &v))
            t->_ignoreSize = boolCoerce(v);        // +51
        if(dictGet(dict, TJS_W("ignore_delay"), &v))
            t->_ignoreDelay = boolCoerce(v);       // +52
        if(dictGet(dict, TJS_W("ignore_over"), &v))
            t->_ignoreOverY = boolCoerce(v);       // +54 (ignore_over)
        if(dictGet(dict, TJS_W("ignore_overy"), &v))
            t->_ignoreOverY = boolCoerce(v);       // +54 (ignore_overy 覆盖同址)
        if(dictGet(dict, TJS_W("ignore_overx"), &v))
            t->_ignoreOverX = boolCoerce(v);       // +53
        if(dictGet(dict, TJS_W("width_time_scale"), &v))
            t->_widthTimeScale = boolCoerce(v);    // +55
        if(dictGet(dict, TJS_W("ignore_ruby"), &v))
            t->_ignoreRuby = boolCoerce(v);        // +56
        if(dictGet(dict, TJS_W("ignore_type"), &v))
            t->_ignoreType = boolCoerce(v);        // +57
        if(dictGet(dict, TJS_W("ignore_face"), &v))
            t->_ignoreFace = boolCoerce(v);        // +58
        if(dictGet(dict, TJS_W("ignore_style"), &v))
            t->_ignoreStyle = boolCoerce(v);       // +59
        return TJS_S_OK;
    }
    // following/leading/begin/end 的 string/void 存值（setOption@0x59D2AC：
    //   octet/int/real（(unsigned)(v58-3)<3 @0x59d358）与 object（v58==1
    //   @0x59d368 → LABEL_10）均 sub_A0E48C(v57, 2u) =
    //   TJSThrowVariantConvertError(v, tvtString)；string(2)→store ttstr；
    //   void(0)→v5=nullptr 空串）。
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

    // ============================================================
    // setDefault @0x59DEA8：(dict) → default* 字段（face/字号族/颜色族/对齐/间距）
    //   1:1 复刻：face 经 resolveFaceIndex 写 +96；fontsize 存在时其值回填缺省的
    //   big/small/ruby 字号（仅当对应 key 缺失）；linesize 缺失则回退读 fontsize key。
    // ============================================================
    static tjs_error setDefault(tTJSVariant *, tjs_int numparams,
                                tTJSVariant **param,
                                iTJSDispatch2 *objthis) { // 0x59DEA8
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        iTJSDispatch2 *dict = paramAsDict(numparams, param);
        if(!dict)
            return TJS_E_INVALIDOBJECT;
        tTJSVariant v;
        // face → resolveFaceIndex → +96。类型分发 @0x59df54..0x59df74：
        //   octet/int/real（(unsigned)(v55-3)<3）与 object（v55==1）均
        //   sub_A0E48C(v54, 2) = ConvertError(String)；string→取值；void→空串。
        if(dictGet(dict, TJS_W("face"), &v)) {
            ttstr faceName;
            if(v.Type() == tvtString)
                faceName = ttstr(v);
            else if(v.Type() == tvtVoid)
                faceName = ttstr(); // v53 = nullptr
            else
                TJSThrowVariantConvertError(v, tvtString); // sub_A0E48C(,2)
            t->_defaultFaceIndex = t->resolveFaceIndex(faceName); // +96
        }
        if(dictGet(dict, TJS_W("bold"), &v))
            t->_defaultBold = boolCoerce(v);                 // +66
        // fontsize 分支：存在→+148，并把缺省 big/small/ruby 字号回填为 fontsize 值
        bool hasFontsize = dictGet(dict, TJS_W("fontsize"), &v);
        if(hasFontsize) {
            t->_defaultFontSize = realCoerce(v);             // +148
            tTJSVariant tmp;
            if(!dictGet(dict, TJS_W("bigfontsize"), &tmp))
                t->_defaultBigFontSize = t->_defaultFontSize;   // +152=+148
            if(!dictGet(dict, TJS_W("smallfontsize"), &tmp))
                t->_defaultSmallFontSize = t->_defaultFontSize; // +156=+148
            if(!dictGet(dict, TJS_W("rubysize"), &tmp))
                t->_defaultRubySize = t->_defaultFontSize;      // +160=+148
        } else {
            // fontsize 缺失：独立读 big/small/ruby（缺失则保持默认 0.0，二进制 v=0.0）
            if(dictGet(dict, TJS_W("bigfontsize"), &v))
                t->_defaultBigFontSize = realCoerce(v);      // +152
            if(dictGet(dict, TJS_W("smallfontsize"), &v))
                t->_defaultSmallFontSize = realCoerce(v);    // +156
            if(dictGet(dict, TJS_W("rubysize"), &v))
                t->_defaultRubySize = realCoerce(v);         // +160
        }
        if(dictGet(dict, TJS_W("rubyoffset"), &v))
            t->_defaultRubyOffset = realCoerce(v);           // +164
        if(dictGet(dict, TJS_W("color"), &v))
            t->_defaultChColor = (tjs_uint32)intCoerce(v);   // +216
        if(dictGet(dict, TJS_W("shadow"), &v))
            t->_defaultShadow = boolCoerce(v);               // +67
        if(dictGet(dict, TJS_W("shadowcolor"), &v))
            t->_defaultShadowColor = (tjs_uint32)intCoerce(v); // +220
        if(dictGet(dict, TJS_W("shadowdiff"), &v))
            t->_defaultShadowDiff = (tjs_uint32)intCoerce(v);  // +224
        if(dictGet(dict, TJS_W("edge"), &v))
            t->_defaultEdge = boolCoerce(v);                 // +68
        if(dictGet(dict, TJS_W("edgecolor"), &v))
            t->_defaultEdgeColor = (tjs_uint32)intCoerce(v); // +228
        if(dictGet(dict, TJS_W("linespacing"), &v))
            t->_defaultLineSpacing = realCoerce(v);          // +168
        if(dictGet(dict, TJS_W("pitch"), &v))
            t->_defaultPitch = realCoerce(v);                // +172
        // linesize：存在→+176；缺失则回退读 fontsize key（二进制 LABEL_25 共享）
        if(dictGet(dict, TJS_W("linesize"), &v))
            t->_defaultLineSize = realCoerce(v);             // +176
        else if(dictGet(dict, TJS_W("fontsize"), &v))
            t->_defaultLineSize = realCoerce(v);             // +176 (fallback)
        if(dictGet(dict, TJS_W("align"), &v))
            t->_defaultAlign = intCoerce(v);                 // +100
        if(dictGet(dict, TJS_W("valign"), &v))
            t->_defaultValign = intCoerce(v);                // +104
        return TJS_S_OK;
    }

    // ============================================================
    // setFont @0x59EFD8：(dict) → 当前样式字段；变化则调 onStyleChanged@0x5A1F28。
    //   changed 仅由 face(idx 变)/bold(变)/fontsize(变或负哨兵) 触发；rubysize/rubyoffset/
    //   color/shadow* /edge* 不置 changed（忠实复刻）。fontsize/rubysize 用 (<0 || !=)
    //   脏哨兵判定（首次/未初始化 cur<0 视为必变）。
    // ============================================================
    static tjs_error setFont(tTJSVariant *, tjs_int numparams,
                             tTJSVariant **param,
                             iTJSDispatch2 *objthis) { // 0x59EFD8
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        iTJSDispatch2 *dict = paramAsDict(numparams, param);
        if(!dict)
            return TJS_E_INVALIDOBJECT;
        tTJSVariant v;
        bool changed = false; // v5
        // face：present→resolveFaceIndex；idx 变则更新 +72 并 changed=true。
        //   类型分发 @0x59f084..0x59f0a4（与 setDefault face 同构）：
        //   octet/int/real 与 object → sub_A0E48C(v28, 2u)=ConvertError(String)；
        //   string→取值；void→空串。
        if(dictGet(dict, TJS_W("face"), &v)) {
            ttstr faceName;
            if(v.Type() == tvtString)
                faceName = ttstr(v);
            else if(v.Type() == tvtVoid)
                faceName = ttstr(); // v27 = nullptr
            else
                TJSThrowVariantConvertError(v, tvtString); // sub_A0E48C(,2)
            int idx = t->resolveFaceIndex(faceName);
            if(t->_curFaceIndex != idx) {
                t->_curFaceIndex = idx; // +72
                changed = true;
            }
        }
        if(dictGet(dict, TJS_W("bold"), &v)) {
            bool b = boolCoerce(v);
            if(t->_curBold != b) { // +62
                t->_curBold = b;
                changed = true;
            }
        }
        if(dictGet(dict, TJS_W("fontsize"), &v)) {
            float f = realCoerce(v);
            if(t->_curFontSize < 0.0f || t->_curFontSize != f) { // +116 脏哨兵
                t->_curFontSize = f;
                changed = true;
            }
        }
        if(dictGet(dict, TJS_W("rubysize"), &v)) {
            float f = realCoerce(v);
            if(t->_curRubySize < 0.0f || t->_curRubySize != f) // +128 (无 changed)
                t->_curRubySize = f;
        }
        if(dictGet(dict, TJS_W("rubyoffset"), &v))
            t->_curRubyOffset = realCoerce(v);                 // +132 无条件
        if(dictGet(dict, TJS_W("color"), &v))
            t->_curChColor = (tjs_uint32)intCoerce(v);         // +200
        if(dictGet(dict, TJS_W("shadow"), &v))
            t->_curShadow = boolCoerce(v);                     // +63
        if(dictGet(dict, TJS_W("shadowcolor"), &v))
            t->_curShadowColor = (tjs_uint32)intCoerce(v);     // +204
        if(dictGet(dict, TJS_W("shadowdiff"), &v))
            t->_curShadowDiff = (tjs_uint32)intCoerce(v);      // +208
        if(dictGet(dict, TJS_W("edge"), &v))
            t->_curEdge = boolCoerce(v);                       // +64
        if(dictGet(dict, TJS_W("edgecolor"), &v))
            t->_curEdgeColor = (tjs_uint32)intCoerce(v);       // +212
        if(changed)
            t->onStyleChanged(objthis); // sub_5A1F28
        return TJS_S_OK;
    }

    // ============================================================
    // setStyle @0x59F7AC：(dict) → 当前样式间距/对齐（NON setFont-isomorphic！二进制
    //   只读 linespacing/pitch/linesize/align/valign，不读 font 系键、不调 onStyleChanged）。
    //   linesize 缺失则回退读 fontsize key（二进制 LABEL_25 共享 fontsize 读取）。
    //   align/valign 写当前样式 +76/+80（≠ setDefault 的默认 +100/+104）。
    // ============================================================
    static tjs_error setStyle(tTJSVariant *, tjs_int numparams,
                              tTJSVariant **param,
                              iTJSDispatch2 *objthis) { // 0x59F7AC
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        iTJSDispatch2 *dict = paramAsDict(numparams, param);
        if(!dict)
            return TJS_E_INVALIDOBJECT;
        tTJSVariant v;
        if(dictGet(dict, TJS_W("linespacing"), &v))
            t->_curLineSpacing = realCoerce(v);  // +136
        if(dictGet(dict, TJS_W("pitch"), &v))
            t->_curPitch = realCoerce(v);        // +140
        if(dictGet(dict, TJS_W("linesize"), &v))
            t->_curLineSize = realCoerce(v);     // +144
        else if(dictGet(dict, TJS_W("fontsize"), &v))
            t->_curLineSize = realCoerce(v);     // +144 (fallback fontsize key)
        if(dictGet(dict, TJS_W("align"), &v))
            t->_curAlign = intCoerce(v);         // +76
        if(dictGet(dict, TJS_W("valign"), &v))
            t->_curValign = intCoerce(v);        // +80
        return TJS_S_OK;
    }

    // ============================================================
    // render 状态机 helper（scanTagUntil / scanDigits / parseInt10 /
    //   parseHexColor / evalDollarTag / renderPercentTag / renderBalancedChar）
    // ============================================================
    // scanTagUntil @0x5A3CE4：从 *cursor 起读字符直到遇到 delim（或到 len），
    //   返回读到的子串 ttstr；cursor 推进到 delim 之后（或 len）。delim 自身不计入。
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
    // scanDigits @0x5A3F18：从 *cursor 起读连续数字字符（0-9），停在非数字，
    //   返回数字子串 ttstr；cursor 停在第一个非数字（或 len）。
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
    // parseInt10 @0x9B111C：UTF-16 串转十进制 int（跳前导 <=0x20 空白，可选 '-'，
    //   循环 *10+digit）。非数字起始 → 0。忠实复刻（≠ AsInteger 的 0x 前缀处理）。
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
    //   '0x'/'0X' 前缀 → 跳过后解析；否则从首字符解析。每位 hex digit 经掩码+减表
    //   计算值；遇非 hex digit 即终止并写 _curChColor = (累积值) | 0xFF000000。
    //   返回 true（有解析，恒写 _curChColor）。掩码 0x7E0000007E03FF / 减表逻辑
    //   等价标准 hex decode（'0'-'9'→0-9, 'A'-'F'/'a'-'f'→10-15）。
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
    // hexDigitValue：复刻 0x5A228C hex decode（掩码 0x7E0000007E03FF + 减表
    //   qword_14CA200，索引 ch-48）。'0'-'9'→0-9,'A'-'F'→10-15,'a'-'f'→10-15，
    //   其它→-1（无效）。
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
    // evalDollarTag @0x5A4148：对脚本对象 FuncCall(L"onEval", tagContent) → 返回值
    //   按 type 分发：octet/int/real（(unsigned)(v13-3)<3 @0x5a41d8）与
    //   object（v13==1 @0x5a41e8 → LABEL_6）均走 sub_A0E48C(v12, 2u) =
    //   TJSThrowVariantConvertError(result, tvtString)（__noreturn 抛错）；
    //   string(2) 取其值 @0x5a422c；void(0) → *a3=0 空串 @0x5a41f8。
    //   FuncCall 返回码二进制不检查（vtbl+16 调用结果丢弃）。
    //   平台边界：二进制用 native+0 调 FuncCall（NativeClassBinder
    //   native≡dispatch），本地传 objthis。
    static ttstr evalDollarTag(iTJSDispatch2 *objthis,
                               const ttstr &content) { // 0x5A4148
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

    // ============================================================
    // % 样式控制分发 @0x5A228C（case '%'）。code = % 之后的控制字符。
    //   next = code 在文本的索引；v36 = code 之后位置；i 已被调用方推进过 %X。
    //   1:1 复刻每个子码的字段写入与门控（ignore_size/+51、ignore_delay/+52、
    //   ignore_face/+58、ignore_type/+57、ignore_style/+59）。
    // ============================================================
    void renderPercentTag(iTJSDispatch2 *objthis, const tjs_char *p, int len,
                          tjs_char code, int next, int v36, ttstr &tagAccum,
                          int yParam) { // 0x5A228C case '%'
        switch(code) {
        case TJS_W('0'): case TJS_W('1'): case TJS_W('2'): case TJS_W('3'):
        case TJS_W('4'): case TJS_W('5'): case TJS_W('6'): case TJS_W('7'):
        case TJS_W('8'): case TJS_W('9'): {
            // %数字：size 百分比。从 code 处（next）读数字串。
            int cur = next; // v136 = v29+1（回退到 code 位置重读数字）
            tagAccum = scanDigits(p, &cur, len); // sub_5A3F18
            // 注意：scanDigits 推进 cursor，render 主循环用 i 续接——这里需把 i 同步。
            //   通过返回 cur 让调用方更新（见 renderPercentTag 调用点 i 调整）。
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
                    onStyleChanged(objthis); // sub_5A1F28
                }
            }
            break;
        }
        case TJS_W(';'): // %;：恢复 curFontSize = defaultFontSize(+148)
            if(!_ignoreSize) {
                if(_curFontSize < 0.0f || _curFontSize != _defaultFontSize)
                    applyFontSize(objthis, _defaultFontSize); // LABEL_298
            }
            break;
        case TJS_W('C'): // 居中对齐 → +76（cascade，见下）
            if(_ignoreStyle) { // +59 → 直接走 bigfontsize
                applyBigFontSizeTag(objthis);
                break;
            }
            // 二进制 cascade：+76=0 → +76=1 → +76=-1（fall-through，最终 -1）。
            //   忠实复刻三连写（源码结构对齐反编译 LABEL_226/227 fall-through）。
            _curAlign = 0; _curAlign = 1; _curAlign = -1; // 最终 -1
            applyBigFontSizeTag(objthis); // 落入 LABEL_228
            break;
        case TJS_W('R'): // 右对齐
            if(_ignoreStyle) { applyBigFontSizeTag(objthis); break; }
            _curAlign = 1; _curAlign = -1; // cascade → -1
            applyBigFontSizeTag(objthis);
            break;
        case TJS_W('L'): // 左对齐
            if(_ignoreStyle) { applyBigFontSizeTag(objthis); break; }
            _curAlign = -1;
            applyBigFontSizeTag(objthis);
            break;
        case TJS_W('B'): // %B：bigfontsize
            applyBigFontSizeTag(objthis);
            break;
        case TJS_W('S'): // %S：smallfontsize(+156)。门控 +51(ignore_size)，在
            //   applySmallFontSizeTag 内判（对齐反编译 case 'S' `if(*(a1+51))`）。
            applySmallFontSizeTag(objthis);
            break;
        case TJS_W('b'): { // %b：bold（下一字符 0/1/其它）
            if(v36 >= len)
                break;
            int dflt = _defaultBold ? 1 : 0; // +66 (v115)
            bool gate = _ignoreType;          // +57 (v116)
            tjs_char arg = p[next + 1];       // v15[v36]（code 之后字符）
            // 注意 v36==next+1：码后字符。i 需再推进 1（v136=v29+3）。
            _percentCursor = next + 2;
            int val;
            if(arg == 48) val = 0;
            else if(arg == 49) val = 1;
            else val = dflt;
            if(gate) break; // +57 → 不写
            if((_curBold ? 1 : 0) != val) { // +62
                _curBold = (val != 0);
                onStyleChanged(objthis);
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
                onStyleChanged(objthis);
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
                        onStyleChanged(objthis);
                    }
                } else {
                    int idx = _defaultFaceIndex; // +96
                    if(_curFaceIndex != idx) {
                        _curFaceIndex = idx;
                        onStyleChanged(objthis);
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
            resetFontImpl(objthis);
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
    void applyFontSize(iTJSDispatch2 *objthis, float size) { // LABEL_298
        _curFontSize = size; // +116
        onStyleChanged(objthis); // sub_5A1F28
    }
    // %B / %C / %L / %R 共享 LABEL_228：curFontSize ← bigFontSize(+152)（!ignore_size）
    void applyBigFontSizeTag(iTJSDispatch2 *objthis) { // LABEL_228
        if(_ignoreSize) // +51
            return;
        float v98 = _curFontSize; // +116
        if(v98 < 0.0f) {
            applyFontSize(objthis, _defaultBigFontSize); // +152
            return;
        }
        if(v98 == _defaultBigFontSize) // 无变化
            return;
        applyFontSize(objthis, _defaultBigFontSize); // +152
    }
    // %S LABEL: curFontSize ← smallFontSize(+156)（!ignore_size，门控 +51）
    void applySmallFontSizeTag(iTJSDispatch2 *objthis) {
        if(_ignoreSize) // +51（二进制 %S 用 +0x33=51）
            return;
        float v109 = _curFontSize; // +116
        if(v109 >= 0.0f) {
            if(v109 == _defaultSmallFontSize) // +156 无变化
                return;
        }
        applyFontSize(objthis, _defaultSmallFontSize); // +156
    }

    // ============================================================
    // begin/end 平衡集普通字符落字 @0x5A228C（default 分支 a3!=0 路径）
    //   appendChar 后，按 begin(+24)/end(+32) 字符集做缩进锚点管理：
    //   - 字符 ∈ begin 集：首个（v17 && depth==0）记 lineStartX=pen + 记 begin 字符；
    //     v17=0; ++depth。
    //   - 字符 ∈ end 集且 --depth==0 且与起始 begin 字符配对（索引相同）→ lineStartX=0。
    //   返回 false 表示 appendChar 失败（中断渲染）。
    // ============================================================
    bool renderBalancedChar(iTJSDispatch2 *objthis, tjs_char c, bool &v17,
                            int &v133, tjs_char &v132) { // 0x5a263c..LABEL_319
        // 是否在 begin 集（+24）
        int beginIdx = (int)_begin.IndexOf(ttstr(c)); // v63 = found?
        bool v63 = (beginIdx != -1);
        char ok = appendChar(objthis, c); // sub_5A3880
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
                // 前置门 @0x5a2a5c/0x5a2a68（BL ttstr_length，IDA 误标
                //   operator delete）：begin(+24) 与 end(+32) 串长度相等
                //   （CMP W26,W0 @0x5a2a6c，B.NE 跳过配对判定 @0x5a2a70）。
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

    // ============================================================
    // render NCB 包装 @0x59FC28（TextRenderBase_ncb_render）
    //   解包 (text, x, y[, size, flag]) → 调真 render(objthis,text,x,y,flag)。
    //   1:1 复刻反编译：
    //   - numparams(a2)<3 → 返回 4294966292(=0xFFFFFC14=TJS_E_BADPARAMCOUNT)。
    //   - a2==3 → flag(v9)=0。
    //   - a2>=4 → param[3](=size) 仅做 real 类型强制（switch @0x59fcb0：
    //     object(1)/octet(3)→sub_A0E48C(v10,5)=ConvertError(Real)、
    //     string(2)→sub_A133A8 解析、int/real/void→无操作），**结果被丢弃**——
    //     size 不传给 render，只触发参数类型校验。= AsReal() 后丢弃。a2<5 → flag=0。
    //   - a2>=5 → param[4](=flag) boolCoerce → v9。
    //   - text = param[0]（拷贝到本地 variant），x = intCoerce(param[1])，
    //     y = intCoerce(param[2])。
    //   - 调 render(objthis, &text, x, y, flag) → bool → sub_A0FEF0(result, ok&1)。
    //   注意（BLOCKING，禁参数名推导）：x/y 在真 render 里语义**不是坐标**——
    //     x = begin/end 平衡集启用标志（render `if(!a3)` 门控），
    //     y = 每字 renderPos 步进初值（render 入口 `+192=(float)a4`）。
    // ============================================================
    static tjs_error render(tTJSVariant *result, tjs_int numparams,
                            tTJSVariant **param,
                            iTJSDispatch2 *objthis) { // 0x59FC28
        if(numparams < 3) // a2 < 3
            return TJS_E_BADPARAMCOUNT; // 4294966292 = 0xFFFFFC14
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        bool flag; // v9
        if(numparams == 3) {
            flag = false; // v9 = 0
        } else {
            // a2>=4：param[3]=size real 强制后丢弃（switch @0x59fcb0：
            //   object/octet→ConvertError(Real)、string→解析、int/real/void→无操作）。
            //   tTJSVariant::AsReal 逐 case 同构（object/octet 抛、string parse、
            //   void→0.0 不抛），结果丢弃。
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
        bool ok = t->renderImpl(objthis, text, (int)x, (int)y, flag);
        if(result)
            *result = (tjs_int)(ok ? 1 : 0); // sub_A0FEF0(a1, v16 & 1)
        return TJS_S_OK;
    }

    // ============================================================
    // 真 render 状态机 @0x5A228C（TextRenderBase_render）
    //   KAG 风格转义标签状态机：遍历 UTF-16 文本，按首字符分发到 #/$/%/\/[/&/普通。
    //   签名 render(this, &text, x, y, flag)：
    //   - flag(a5) bit0：清(0)→复位行列表/renderDelayAccum(+188)/keyWaitList；置(1)→续接。
    //   - x(a3)：begin/end 平衡集**启用标志**（!a3→普通 append；a3→begin/end 平衡逻辑）。
    //   - y(a4)：charDelayStep(+192) 初值（每字 renderPos 步进）。
    //   返回 bool（finishLine/appendChar 失败 → false=渲染中断；正常走完 → true）。
    //   平台边界：二进制 native this 即 dispatch（NativeClassBinder 风格），度量/eval/
    //     onFontChange 回调用 native+0；本地 native≠objthis，故落字层显式传 objthis。
    // ============================================================
    bool renderImpl(iTJSDispatch2 *objthis, const ttstr &text, int x, int y,
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
            return finishLine(objthis) & 1; // LABEL_321/322

        bool v17 = true;  // begin-run 起点标志（首字符或换行后置 1）
        int v133 = 0;     // begin/end 嵌套深度计数
        tjs_char v132 = 0; // begin-run 起始字符（用于 end 匹配校验）

        while(i < len) {
            int ch = (unsigned short)p[i]; // v31
            int next = i + 1; // v30 = v16+1
            i = i + 1;        // v136 = v16+1（默认推进 1；标签分支会再调整）
            tjs_char c = (tjs_char)ch;
            // 注意：begin/end 平衡 + 标签分发都在下面。多数标签分支结束 → LABEL_320
            //   (i=v136; 若 i>=len → 末尾 finishLine)；普通字符 → appendChar。
            bool advanced = true; // 默认走 LABEL_320 推进；置 false 表示需中断返回

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
                ttstr evalResult = evalDollarTag(objthis, tagAccum); // sub_5A4148
                tagAccum = evalResult;
                int n = (int)tagAccum.length(); // v50 = operator delete(...)
                if(n < 1)
                    goto cont;
                const tjs_char *q = tagAccum.c_str(); // v51
                for(int k = 0; k < n; ++k) {
                    if((appendChar(objthis, q[k]) & 1) == 0) {
                        // appendChar 失败 → LABEL_325 中断
                        return false;
                    }
                }
                // 全部展开成功（++v64>=v50 @0x5a295c → LABEL_319）：v17=0。
                //   空结果（n<1）走 LABEL_320 不动 v17。
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
                renderPercentTag(objthis, p, len, code, next, v36, tagAccum,
                                 y);
                i = _percentCursor;    // 同步标签扫描推进的 cursor
                goto cont;
            } else if(c == TJS_W('&')) {
                // ----- & ：消费标签内容（到 ';'），无字段副作用 -----
                tagAccum = scanTagUntil(p, &i, len, TJS_W(';')); // sub_5A3CE4 delim=59
                // goto LABEL_319：v17=0 后续接（等价 begin-run reset）
                v17 = false;
                goto cont;
            } else if(c == TJS_W('[')) {
                // ----- [ ：ruby 括号，消费到 ']'，!ignore_ruby 仅 refcount no-op -----
                tagAccum = scanTagUntil(p, &i, len, TJS_W(']')); // sub_5A3CE4 delim=93
                // if(!ignore_ruby) addref+release（净空操作，无 +528 写入——本 build
                //   ruby-via-bracket 不写 curRubyText，忠实复刻 0x5A28B0 无字段写）。
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
                    //   二进制 sub_5A5874 存 *(QWORD*)slot = *a2（renderCount 零扩展）。
                    _keyWaitList.push_back(KeyWaitItem{ _renderCount, 0 });
                    v17 = false;
                    goto cont;
                } else if(code == TJS_W('n')) {
                    // \n（反斜杠 n）：finishLine；失败→中断
                    if((finishLine(objthis) & 1) == 0)
                        return false; // LABEL_325
                    goto cont;
                } else if(code == TJS_W('r')) {
                    // \r：lineStartX(+196) = 0
                    _lineStartX = 0;
                    goto cont;
                } else if(code == TJS_W('t')) {
                    // \t：appendChar(9)（制表）
                    char ok = appendChar(objthis, 9);
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
                if((finishLine(objthis) & 1) == 0)
                    return false; // LABEL_325
                v17 = true;       // begin-run 复位
                goto cont;
            } else {
                // ----- 普通字符 -----
                if(!x) {
                    // x==0：无 begin/end 平衡，直接 appendChar
                    char ok = appendChar(objthis, c); // sub_5A3880
                    v17 = false; // LABEL_116
                    if((ok & 1) == 0)
                        return false; // LABEL_116→LABEL_322（v57=0，不调 finishLine）
                    goto cont;
                }
                // x!=0：begin/end 平衡集逻辑
                if(!renderBalancedChar(objthis, c, v17, v133, v132))
                    return false; // appendChar 失败 → LABEL_325
                goto cont;
            }
        cont:
            // LABEL_320：i 已由分支推进；若到末尾 → finishLine 收尾
            (void)advanced;
            if(i >= len)
                return finishLine(objthis) & 1; // LABEL_321/322
        }
        // 循环正常退出（i>=len 在 cont 已处理；兜底）
        return finishLine(objthis) & 1;
    }

    // onEval @0x5A0294：(expr) → TJS eval（在 this 上下文求值表达式 param[0]，结果写 result）。
    //   1:1 复刻反编译：
    //     *(result+16) = 0;                       // result.type = Void(0)
    //     return sub_8E3FA4(a2=expr, *a1=this dispatch, a3=result);
    //   sub_8E3FA4 = TVPExecuteExpression(expr, context, result)（内部 sub_97FE40，
    //     字符串 ref "../../src/core/base/ScriptMgnIntf.cpp" 确证为 ScriptMgnIntf eval 路径）。
    //   平台边界：二进制 native this 即 dispatch（NativeClassBinder native+0=iTJSDispatch2），
    //     context = *a1 = native this[0]；本地 native≠objthis，context 用 objthis（脚本子类对象）。
    static tjs_error onEval(tTJSVariant *result, tjs_int numparams,
                            tTJSVariant **param,
                            iTJSDispatch2 *objthis) { // 0x5A0294
        if(!self(objthis))
            return TJS_E_INVALIDOBJECT;
        if(result)
            result->Clear(); // *(a3+16)=0：result.type = Void
        if(numparams < 1 || !param || !param[0])
            return TJS_S_OK;
        ttstr expr(*param[0]); // a2：表达式字符串
        // TVPExecuteExpression(expr, context=objthis, result) = sub_8E3FA4
        TVPExecuteExpression(expr, objthis, result);
        return TJS_S_OK;
    }

    // getKeyWait @0x5A02DC：() → TJS Array of dict{pos, time}，源自 _keyWaitList(+480)。
    //   1:1 复刻反编译：① 建 Array(sub_9876D4) ② 遍历 keyWait 列表，count=(+488-+480)>>3
    //   ③ 每元素读 **低 int(index)**（LDRSW [base+8*i]）= v13 ④ 建 dict(sub_9C8440)，
    //   pos=v13(Integer)、time=v13(Integer)（**两者同值=index**；renderPos bits 高 int 不读）
    //   ⑤ dict add 到 Array（FuncCall L"add"）。
    //   纠正旧桩（返回空 Array）。注意 pos/time 均为低 int(index)，非 renderPos——见 KeyWaitItem。
    static tjs_error getKeyWait(tTJSVariant *result, tjs_int, tTJSVariant **,
                                iTJSDispatch2 *objthis) { // 0x5A02DC
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        iTJSDispatch2 *arr = TJSCreateArrayObject(); // sub_9876D4(0)
        tTJSVariant addHint;
        for(size_t i = 0; i < t->_keyWaitList.size(); ++i) {
            int v13 = t->_keyWaitList[i].index; // LDRSW 低 int（pos 与 time 同值）
            iTJSDispatch2 *dict = TJSCreateDictionaryObject(); // sub_9C8440(0)
            tTJSVariant vPos((tjs_int)v13);  // v16=4 Integer
            dict->PropSet(TJS_MEMBERENSURE, TJS_W("pos"), nullptr, &vPos, dict);
            tTJSVariant vTime((tjs_int)v13); // v16=4 Integer（同 v13）
            dict->PropSet(TJS_MEMBERENSURE, TJS_W("time"), nullptr, &vTime, dict);
            // add dict 到数组（二进制 FuncCall L"add"）。
            //   arg variant {Object=dict, ObjThis=null}（0x5a042c v16=1 /
            //   0x5a0430 v15[0]=v12, v15[1]=0）——objthis 槽为 null。
            tTJSVariant vDict(dict, nullptr);
            dict->Release();
            tTJSVariant *args[1] = { &vDict };
            arr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, args, arr);
        }
        // result variant {Object=arr, ObjThis=null}（0x5a04c8 *(a2+16)=1 /
        //   0x5a04cc *a2=v4, *(a2+8)=0）——objthis 槽为 null。
        //   （注意：getCharacters 的 result 在二进制为 (obj,obj)，两函数不一致，各自照抄。）
        if(result)
            *result = tTJSVariant(arr, nullptr);
        arr->Release();
        return TJS_S_OK;
    }

    // getCharacters @0x5A0694：(int start=a2, int count=a3) → TJS Array of per-char dict。
    //   1:1 复刻反编译：
    //   - count==0(!a3) → count = renderCount(+84) - start（**非 charList 末尾**，纠正旧 §4）。
    //   - charListCount = (+304-+296)>>3；clamp：v13 = (count+start<=charListCount)?count
    //       :(charListCount-start)。v13<1 → 空 Array。
    //   - 遍历 v14=0..v13：charItem = charList[v14+start]（8B 指针元素）。
    //   - face 缓存：faceIndex(+52) 变化时查 _faceTable[faceIndex]（越界→空串 byte_1506A57），
    //       缓存于 v30，每元素 face=该缓存名。
    //   - dict 字段（key UTF-16LE 经反编译确认；类型经 sub_5A2160/5A614C/5A6020/A0FB64 helper）：
    //       graph(+40,int 首字段 @0x5a081c) text(+0,string) x(+8,real) y(+12,real)
    //       cw(+16,real) size(+20,real) face(缓存名,string) color(+28,int) bold(+41,int)
    //       italic(+42,int) shadow(+43,int) edge(+44,int) shadowColor(+32,int)
    //       shadowDiff(+48,int) edgeColor(+36,int) ruby(子 Array，仅 +56!=+64)
    //       vertical(+45,int) delay(+24,real)
    //   - dict 经 sub_5A6550(arr, v14, dict) = PropSetByNum(index=v14) 落入数组。
    static tjs_error getCharacters(tTJSVariant *result, tjs_int numparams,
                                   tTJSVariant **param,
                                   iTJSDispatch2 *objthis) { // 0x5A0694
        TextRenderBase *t = self(objthis);
        if(!t)
            return TJS_E_INVALIDOBJECT;
        int start = (numparams >= 1 && param && param[0])
                        ? (int)param[0]->AsInteger()
                        : 0; // a2
        int count = (numparams >= 2 && param && param[1])
                        ? (int)param[1]->AsInteger()
                        : 0; // a3
        iTJSDispatch2 *arr = TJSCreateArrayObject(); // sub_9876D4(0)
        // count==0 → renderCount - start（+84）
        if(!count)
            count = t->_renderCount - start; // a3 = *(a1+84) - a2
        int charListCount = (int)t->_charList.size(); // (+304-+296)>>3 = v12
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
                CharItem *ci = t->_charList[srcIdx]; // charList[v14+a2]
                // face 缓存刷新（faceIndex 变化时重查 _faceTable）
                int fi = ci->faceIndex; // v17+52
                if(v15 != fi) {
                    if(fi < 0 || fi >= (int)t->_faceTable.size())
                        faceName = ttstr(); // sub_A13878(&byte_1506A57) 空串
                    else
                        faceName = t->_faceTable[fi]; // _faceTable[faceIndex]
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
                trDictSetInt(dict, TJS_W("color"), (int)ci->chColor); // +28
                trDictSetInt(dict, TJS_W("bold"), ci->bold ? 1 : 0);   // +41
                trDictSetInt(dict, TJS_W("italic"), ci->italic ? 1 : 0); // +42
                trDictSetInt(dict, TJS_W("shadow"), ci->shadow ? 1 : 0); // +43
                trDictSetInt(dict, TJS_W("edge"), ci->edge ? 1 : 0);     // +44
                trDictSetInt(dict, TJS_W("shadowColor"),
                             (int)ci->shadowColor); // +32
                trDictSetInt(dict, TJS_W("shadowDiff"),
                             (int)ci->shadowDiff); // +48
                trDictSetInt(dict, TJS_W("edgeColor"),
                             (int)ci->edgeColor); // +36
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
        if(result)
            *result = tTJSVariant(arr, arr);
        arr->Release();
        return TJS_S_OK;
    }
    // getCharacters/getKeyWait dict 字段写 helper（对应 sub_5A2160/5A614C/5A6020/A0FB64
    //   + dispatch vtable+48 = PropSet(MEMBERENSURE) 内联体）。
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
                             int val) { // sub_5A2160/5A6020/A0FB64 (type 4 Integer)
        tTJSVariant v((tjs_int)val);
        dict->PropSet(TJS_MEMBERENSURE, key, nullptr, &v, dict);
    }
    // ruby 子 Array（sub_5A6240@0x5A6240）：每 RubyItem → dict{text,x,y,size}，
    //   PropSetByNum(index) 落入 Array；Array 作为 dict["ruby"] 值。
    static void trDictSetRubyArray(iTJSDispatch2 *dict,
                                   const std::vector<RubyItem> &ruby) { // sub_5A6240
        iTJSDispatch2 *arr = TJSCreateArrayObject(); // sub_9876D4(0)
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
};

} // namespace textrender

using textrender::TextRenderBase;

// ============================================================
// NCB 注册（模块 TextRender.dll；全部 objectMember/flags=0，§5）
// ============================================================
NCB_REGISTER_CLASS(TextRenderBase) {
    // 构造器成员 = TextRenderBase_ncb_constructor @0x59D160（二进制 §2 的 17 个
    //   method-tag 成员之一）：TJS `new` 时立即 `*slot = operator new(0x250);
    //   TextRenderBase_ctor(obj, objthis)`——非惰性创建。本地 NCB_CONSTRUCTOR(())
    //   同样在 TJS 构造时立即建 native 实例（真 ctor = TextRenderBase() @0x5A111C，
    //   默认值群见类首）。
    NCB_CONSTRUCTOR(());

    // ---- 16 methods（+ 上面 1 构造器 = 二进制 17 个 method-tag 成员）----
    NCB_METHOD_RAW_CALLBACK(setOption, &Class::setOption, 0);
    NCB_METHOD_RAW_CALLBACK(setDefault, &Class::setDefault, 0);
    NCB_METHOD_RAW_CALLBACK(setRenderSize, &Class::setRenderSize, 0);
    NCB_METHOD_RAW_CALLBACK(clear, &Class::clear, 0);
    NCB_METHOD_RAW_CALLBACK(resetFont, &Class::resetFont, 0);
    NCB_METHOD(resetStyle);
    NCB_METHOD_RAW_CALLBACK(setFont, &Class::setFont, 0);
    NCB_METHOD_RAW_CALLBACK(setStyle, &Class::setStyle, 0);
    NCB_METHOD_RAW_CALLBACK(render, &Class::render, 0);
    NCB_METHOD_RAW_CALLBACK(newline, &Class::newline, 0);
    NCB_METHOD_RAW_CALLBACK(done, &Class::done, 0);
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
