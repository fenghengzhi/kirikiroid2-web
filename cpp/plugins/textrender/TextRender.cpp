//
// textrender.dll — TextRenderBase 原生类
//
// 四参考二进制联合取证基线：
//   analysis/textrender_four_binary_baseline_2026-08-10.md
//
// 本文件最初来自旧 Android libkrkr2.so 的单文件逆向。裸地址、反编译器标签和
// 单一 ABI 字段偏移现已移出实现文件；行为仍必须重新在 Android arm64/armv7 与
// iOS arm64/armv7 四个目标中定位。现已联合确认的注册面为 1 constructor + 16 methods +
// 33 properties，四个目标的地址、对象大小和标准库布局差异统一记录在 analysis。
//
// 绑定器机制：四个参考目标都表明 textrender 不自带独立绑定器——它与 motionplayer
//   共用同一条 ncbind 注册链。invoker 是 ncbind Functor/Property 模板按签名实例化的
//   多实例（共享 GETINSTANCE/Itanium PMF 解封/错误码序列），守护串
//   "Invalid instance type."/"No method pointer."/"Multiple constructors." 全在本地
//   cpp/core/plugin/ncbind.hpp。故忠实复刻 = 用本地 ncbind 既有设施重接，零改 ncbind.hpp。
//
// objthis 数据流：四个 factory 都把 TJS objthis 传给真构造器，构造器首个业务写入
//   是保存该指针；四条析构链都不 Release 它。因此它是借用的 dispatch 指针。
//   本地用 ncbind `Factory(&factory)`（工厂签名直收 objthis，见
//   DrawDeviceD3D.cpp:47/:349）`new TextRenderBase(objthis)` 复刻该数据流；
//   `objthis` 成员是裸指针、不 AddRef/不 Release（生命周期由 TJS 脚本对象持有，
//   native 仅借引用回调脚本 onGetTextWidth/onEval/onFontChange）。所有内部落字/度量/
//   eval 函数直接读 `objthis` 成员，不再层层传递同一形参。
//
// 字节布局复刻工作法：下面用语义字段名写普通 C++ 类，声明顺序按四个目标共同
// 证明的字段顺序排列，让各平台编译器自由计算 ABI 偏移。逐平台偏移只写入 analysis，
// 不进入可编译代码（wasm32 ABI 必然不同，禁 pragma pack/static_assert(offsetof)）。
//
#include <spdlog/spdlog.h>
#include <vector>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include "tjs.h"
#include "tjsArray.h"
#include "tjsHashSearch.h"
#include "tjsLex.h"
#include "ncbind.hpp"
#include "ScriptMgnIntf.h" // TVPExecuteExpression（onEval）

#define NCB_MODULE_NAME TJS_W("TextRender.dll")
#define LOGGER spdlog::get("plugin")

namespace textrender {

// ============================================================
// TJS 成员名 hint 槽。四个目标均保留 24 个连续、零初始化的 uint32，按以下
// 成员名顺序跨函数共享；dict 解析层的 PropGet 始终传 null hint，不使用这组缓存。
// 它们不是 TextRenderBase 字段，NCB Unregist 也不会清零，因而保持到镜像卸载/进程结束。
// ============================================================
static tjs_uint32 s_hintFace = 0;
static tjs_uint32 s_hintBold = 0;
static tjs_uint32 s_hintItalic = 0;
static tjs_uint32 s_hintOnFontChange = 0;
static tjs_uint32 s_hintOnGetTextWidth = 0;
static tjs_uint32 s_hintOnEval = 0;
static tjs_uint32 s_hintPos = 0;
static tjs_uint32 s_hintTime = 0;
static tjs_uint32 s_hintAdd = 0;
static tjs_uint32 s_hintGraph = 0;
static tjs_uint32 s_hintText = 0;
static tjs_uint32 s_hintX = 0;
static tjs_uint32 s_hintY = 0;
static tjs_uint32 s_hintCw = 0;
static tjs_uint32 s_hintSize = 0;
static tjs_uint32 s_hintColor = 0;
static tjs_uint32 s_hintShadow = 0;
static tjs_uint32 s_hintEdge = 0;
static tjs_uint32 s_hintShadowColor = 0;
static tjs_uint32 s_hintShadowDiff = 0;
static tjs_uint32 s_hintEdgeColor = 0;
static tjs_uint32 s_hintRuby = 0;
static tjs_uint32 s_hintVertical = 0;
static tjs_uint32 s_hintDelay = 0;

// ============================================================
// TextRenderBase — 文本布局引擎（非 Layer 子类）。字段顺序由四个构造/析构链、
//   公共方法和布局 helper 共同约束，逐平台偏移见联合取证基线。
// ============================================================
class TextRenderBase {
public:
    // ---- 元素与行结构 ----
    // CharItem/RubyItem 含 ttstr 和 vector，是带引用计数/析构的非平凡 RAII 对象，
    // 不能称为 POD。四参考文件的源码字段顺序一致，ABI 大小则不同：
    // RubyItem = 64 位 20B、32 位 16B；CharItem = 64 位 80B、32 位 64B。
    struct RubyItem {
        ttstr text;                      // 引用计数文本
        float x = 0;                     // ruby x 偏移
        float y = 0;                     // ruby y 偏移
        float span = 0;                  // rubySize * fontScale
    };
    struct CharItem {
        ttstr text;                      // 单字符文本（onGetTextWidth 输入）
        float x;                         // 构造不写；kinsoku 落字尾部回填
        float y;                         // 构造不写；kinsoku 落字尾部回填
        float cw;                        // appendChar 写 onGetTextWidth 返回值
        float size;                      // appendChar 写 fontScale×curFontSize
        float renderPos;                 // 构造不写；kinsoku 落字尾部回填
        tjs_uint32 chColor;
        tjs_uint32 shadowColor;
        tjs_uint32 edgeColor;
        bool graph;                      // 构造置 0；脚本面真名 "graph"
        bool bold;
        bool italic;
        bool shadow;
        bool edge;
        bool vertical;
        tjs_uint32 shadowDiff;
        int faceIndex;
        // appendChar 以 resize(size()+1)+back() 值初始化新槽。四目标扩容时都会
        // copy/AddRef 既有 RubyItem；iOS 的 split-buffer 交换也不是指针窃取式 move。
        std::vector<RubyItem> ruby;      // ruby 子标注

        // 四目标的内联构造序列都只直接构造 text、把 graph 置零并默认构造 ruby；
        // 其余标量由 appendChar/kinsoku 分阶段填写。直接接收字符可避免额外的 ttstr
        // 临时对象、AddRef 与 Release。
        explicit CharItem(tjs_char ch) : text(ch), graph(false) {}
    };
    // pending 行缓冲与 lineList 元素同为 Line。其 deque 控制块/Line 大小分别为：
    // Android arm64 80/112B，Android armv7 40/72B，iOS arm64 48/80B，
    // iOS armv7 24/56B；这些是标准库 ABI 差异，不应手写 padding 固化。
    struct Line {
        // Android/libstdc++ 节点容量为 6/8，默认即分配 8-entry map 和一个 node；
        // iOS/libc++ 节点容量为 51/64，默认惰性分配并可保留前后 spare node。
        std::deque<CharItem> chars;
        // 真对象构造器只默认构造 deque，不初始化以下标量；首次 clear() 全部清零。
        // 行内 max(行高 + char.y)。done 只读取它聚合全局 bottom；valign 不回写
        // Line metric，而是只改 CharItem.y 和对象级 top/bottom。
        float lineBottom;
        float lineHeight;             // max(charSize, curLineSize)
        float bboxLeft;
        float bboxTop;
        float bboxRight;
        float bboxBottom;
        int wordBreakRun;             // 行内 word-break run 起点字符数
        bool prevWasSpace;            // 上个落字是否空格
        // 源码层先按标准库语义清空 deque，再把其后的全部状态字段置零；只有
        // Android arm64 保留完整的 out-of-line 边界，其余三端把标量清零内联到
        // 调用者，只留下 std::deque<CharItem>::clear helper。
        // Android clear 保留 map 和一个 node；iOS clear 保留 map allocation 与至多
        // 两个 node，并把 start index 重置到一个 node 或两 node 布局的中点。
        void clear() {
            chars.clear();
            lineBottom = 0;
            lineHeight = 0;
            bboxLeft = 0;
            bboxTop = 0;
            bboxRight = 0;
            bboxBottom = 0;
            wordBreakRun = 0;
            prevWasSpace = false;
        }
    };
    // keyWait vector 元素固定为两个 32-bit int（8B）；clear 只归零逻辑长度，保留容量。
    struct KeyWaitItem {
        int index = 0; // \k 写 renderCount；getKeyWait 把它同时公开为 pos/time。
        int time = 0;  // done 写 charList[index].renderPos 位型；getKeyWait 不读取。
    };
    // face hash functor：空 Ptr 直接返回 0；非空 UTF-16 内容走 TJS
    // one-at-a-time hash，雪崩后的 0 改成 0xFFFFFFFF。
    struct FaceNameHash {
        size_t operator()(const ttstr &s) const {
            // 三个目标保留的共享函数均有 155/160 个跨引擎引用并逐句对应
            // tTJSHashFunc<ttstr>::Make；Android arm64 只在此调用点内联。
            // 这不是会读写字符串 Hint 的 ttstr_hasher。
            return (size_t)tTJSHashFunc<ttstr>::Make(s);
        }
    };
    struct FaceNameEq {
        bool operator()(const ttstr &a, const ttstr &b) const { return a == b; }
    };

    // ============================================================
    // 数据成员（声明顺序由四文件共同确认）。逐平台偏移与容器尺寸只记录在
    // analysis 基线表；这里保留共同源码的字段次序与语义。
    // ============================================================
    // objthis（dispatch 回指）：由 ncbind factory 在 TJS `new` 时注入。四个构造器
    // 都保存它但不 AddRef，四条析构链也都不 Release。
    iTJSDispatch2 *objthis = nullptr;

    // 禁则字符集字符串。二进制存 tTJSVariant*（refcounted
    //   string），仅接受 string/void，object/octet/int/real 抛转换错误。语义=ttstr。
    //   构造器以内置日文禁则集 4 串初始化（见构造函数体）。
    ttstr _following;                // L"following"
    ttstr _leading;                  // L"leading"
    ttstr _begin;                    // L"begin"
    ttstr _end;                      // L"end"
    ttstr _renderText;               // 落字累积文本（finishLine 追加换行/缩进）

    // 选项 byte：ignore_over 与 ignore_overy 写同一字段，后者覆盖前者；
    // kinsoku_max 经 bool-coerce 写 DWORD 0/1。
    //   四构造器共同默认值：vertical=0、word_break=1。
    bool _vertical = false;          // L"vertical"（ctor=0）
    bool _wordBreak = true;          // L"word_break"（ctor=1）
    bool _ignoreColor = false;       // L"ignore_color"
    bool _ignoreSize = false;        // L"ignore_size"
    bool _ignoreDelay = false;       // L"ignore_delay"
    bool _ignoreOverX = false;       // L"ignore_overx"
    bool _ignoreOverY = false;       // L"ignore_over" / L"ignore_overy"（同一字段）
    bool _widthTimeScale = false;    // L"width_time_scale"
    bool _ignoreRuby = false;        // L"ignore_ruby"
    bool _ignoreType = false;        // L"ignore_type"
    bool _ignoreFace = false;        // L"ignore_face"
    bool _ignoreStyle = false;       // L"ignore_style"
    bool _renderOver;                // ctor 不初始化，clear 置 false
    // 四构造器都显式清零、其后没有观察到 reader 的 dead bool；仍须保留源码槽位。
    bool _unusedOptionFlag_guess = false;

    // 当前样式（setFont/setStyle 改写）
    bool _curBold;                   // ctor 不初始化，resetFont 写
    bool _curShadow;                 // ctor 不初始化，resetFont 写
    bool _curEdge;                   // ctor 不初始化，resetFont 写
    bool _curItalic;                 // ctor 不初始化，resetFont 写
    // 默认样式（setDefault 改写；resetFont/Style 复位为这些）。四构造器共同值为
    // bold=0、shadow=1、edge=0、italic=0。
    bool _defaultBold = false;
    bool _defaultShadow = true;
    bool _defaultEdge = false;
    bool _defaultItalic = false;
    int _curFaceIndex;               // 当前 face index；ctor 不初始化
    int _curAlign;                   // setStyle L"align"；当前样式而非默认值
    int _curValign;                  // setStyle L"valign"；当前样式而非默认值
    int _renderCount;                // ctor 不初始化，clear 置 0
    int _charBufCountdown;           // 组合字符累积倒计数；ctor 不初始化
    int _unusedResetStateA_guess;    // ctor 不初始化，clear 置 0；未观察到读取
    // default face 保存的是 face table 索引，setDefault 经 resolveFaceIndex 写入。
    //   defaultFace 属性 getter/setter 经 _faceTable/resolveFaceIndex 间接读写此 index。
    int _defaultFaceIndex;           // ctor 末尾及 setDefault 均经 resolveFaceIndex 写
    int _defaultAlign = -1;
    int _defaultValign = -1;
    int _kinsokuUsed;                // 本行 kinsoku 已用次数；ctor 不初始化
    int _kinsokuMax = 1;             // L"kinsoku_max"（bool-coerce→0/1）
    float _curFontSize = -1.0f;      // 脏哨兵（首次 resetFont 组复位必触发）
    // 四个平台在这里都有两个 4B 槽，从未被构造器初始化，插件函数中也没有观察到
    // 读写；它们与相邻默认 big/small 字号的顺序对应，属于必须保留的 vestigial 字段。
    float _curBigFontSize;           // dead/uninitialized
    float _curSmallFontSize;         // dead/uninitialized
    float _curRubySize = -1.0f;      // 脏哨兵（resetFont `<0||!=` 门控）
    float _curRubyOffset;            // setFont L"rubyoffset"；ctor 不初始化
    float _curLineSpacing;           // ctor 不初始化
    float _curPitch;                 // ctor 不初始化
    float _curLineSize;              // ctor 不初始化
    // 四构造器共同默认值：font/big/small/ruby = 24/48/12/10，rubyOffset=-2，
    // lineSpacing=6，pitch=0，lineSize=24，timeScale=fontScale=1。
    float _defaultFontSize = 24.0f;
    float _defaultBigFontSize = 48.0f;
    float _defaultSmallFontSize = 12.0f;
    float _defaultRubySize = 10.0f;
    float _defaultRubyOffset = -2.0f;
    float _defaultLineSpacing = 6.0f;
    float _defaultPitch = 0;
    float _defaultLineSize = 24.0f;
    float _timeScale = 1.0f;
    float _fontScale = 1.0f;
    float _renderDelayAccum;             // ctor 不初始化，clear 置 0
    float _charDelayStep = 1.0f;         // 每字 renderPos 步进
    float _lineStartX;                   // 行首 X；ctor 不初始化，clear 置 0
    // 当前/默认颜色块。
    tjs_uint32 _curChColor;              // ctor 不初始化，resetFont 写
    tjs_uint32 _curShadowColor;          // ctor 不初始化，resetFont 写
    tjs_uint32 _curShadowDiff;           // ctor 不初始化，resetFont 写
    tjs_uint32 _curEdgeColor;            // ctor 不初始化，resetFont 写
    tjs_uint32 _defaultChColor = 0xFFFFFFFF;
    tjs_uint32 _defaultShadowColor = 0xFF000000;
    tjs_uint32 _defaultShadowDiff = 1;
    tjs_uint32 _defaultEdgeColor = 0xFF0080FF;
    float _penX;                     // 横排 pen X（竖排：列 X）；ctor 不初始化
    float _penY;                     // 横排 pen Y（竖排：行内 Y）；ctor 不初始化
    float _renderSizeW = 0;
    float _renderSizeH = 0;
    float _renderLeft;               // ctor 不初始化
    float _renderTop;                // ctor 不初始化
    float _renderRight;              // ctor 不初始化
    float _renderBottom;             // ctor 不初始化
    // ruby bbox 累加器。bottom 槽在四个目标中都存在，但没有观察到读写；其余三项
    // 由 appendChar 的 ruby 分支维护。
    float _rubyLeft;                 // ctor 不初始化
    float _rubyTop;                  // ctor 不初始化
    float _rubyRight;                // ctor 不初始化
    float _rubyBottom;               // dead/uninitialized
    float _renderPos;                // 当前落字累积渲染位置；ctor 不初始化
    float _renderPosSnap;            // renderPos 快照；ctor 不初始化
    int   _unusedResetStateB_guess;  // ctor 不初始化，clear 置 0；未观察到读取
    // 扁平字符索引：非 owning vector<CharItem*>，元素宽度随指针 ABI 为 8B/4B。
    // 它只在 done() 重建；lineList 被 clear/reallocate 后到下次 done 前可能暂时悬空。
    std::vector<CharItem *> _charList;
    // pending 行缓冲与 lineList 元素同型；各平台大小见上方 Line 注释。
    Line _pendingLine;
    // finishLine 总是深拷贝 pending Line。vector 扩容搬移既有行时，Android/libstdc++
    // 逐行深拷贝，iOS/libc++ 则 move/窃取 deque 控制块；共享源码仍只是 push_back。
    std::vector<Line> _lineList;
    std::vector<ttstr> _faceTable;   // index→face
    std::vector<KeyWaitItem> _keyWaitList;
    // 内部 UTF-16 累积 buffer。四目标均按 2B 平凡元素增长：新槽写字符、整体
    // memmove/memcpy 旧内容，再提交三指针；标准库只影响容量公式与临时缓冲形态。
    std::vector<tjs_char> _accumBuf;
    // 当前 ruby 文本。参考实现的空判据是底层 string 指针非空；本地 ttstr 空串表示
    //   Ptr==nullptr
    //   （tjsString.h:390 IsEmpty；空串 Alloc 返回 null，tjsVariantString.cpp:547），
    //   故 `!_curRubyText.IsEmpty()` 与四份二进制的底层指针非空判据一致。
    ttstr _curRubyText;
    // face hash 表（resolveFaceIndex intern：face 名→index）。四文件共同容器语义为
    // std::unordered_map<ttstr,int>。Android/libstdc++ 的默认构造展开带 bucket hint 10，
    // iOS/libc++ 的默认构造为空 bucket；这是标准库实现差异，不是源码显式参数。
    // clear 释放节点但保留当前 bucket allocation；hash functor = FaceNameHash
    // （与四个 intern/rehash 路径逐步核对，见 analysis）。
    std::unordered_map<ttstr, int, FaceNameHash, FaceNameEq> _faceHash;

    // ============================================================
    // 构造 / 析构
    // ============================================================
    // 四个构造器都由 ncbind factory 接收 objthis，分配平台 ABI 对应的对象大小，
    // 首句保存裸 objthis，再初始化相同的字段序列；不 AddRef objthis。
    // 标量默认值落在各字段初始化器；此处补 4 个禁则集字符串和末尾 face intern。
    explicit TextRenderBase(iTJSDispatch2 *objthis_)
        : objthis(objthis_) {
        // 内置日文禁则集 4 串（四文件 UTF-16LE 原始字节完全一致）。
        // following（68 码点，行头禁则：闭括/句读/长音/拗促音等；
        //   含 U+3000 全角空格，故全部用 \u 转义书写，逐码点复刻、编码无歧义）
        _following = ttstr(
            TJS_W("%),:;]}。，、") // %),:;]}。，、
            TJS_W("．：；゛゜ヽヾゝゞ々") // ．：；゛゜ヽヾゝゞ々
            TJS_W("’”）〕］｝〉》」』") // ’”）〕］｝〉》」』
            TJS_W("】°′″℃￠％‰　!") // 】°′″℃￠％‰<U+3000>!
            TJS_W(".?・？！ーぁぃぅぇ") // .?・？！ーぁぃぅぇ
            TJS_W("ぉっゃゅょゎァィゥェ") // ぉっゃゅょゎァィゥェ
            TJS_W("ォッャュョヮヵヶ") // ォッャュョヮヵヶ
            TJS_W(""));
        // leading（19 码点，行尾禁则：开括/通货记号；首码点 U+005C 反斜杠）
        _leading = ttstr(
            TJS_W("\\$([{‘“（〔［") // 首码点 U+005C 反斜杠须 \\ 转义，次 U+0024 '$'
            TJS_W("｛〈《「『【￥＄￡") // ｛〈《「『【￥＄￡
            TJS_W(""));
        // begin（10 码点，开括平衡集）
        _begin = ttstr(
            TJS_W("「『（‘“〔［｛〈《") // 「『（‘“〔［｛〈《
            TJS_W(""));
        // end（10 码点，闭括平衡集，与 begin 按索引一一配对）
        _end = ttstr(
            TJS_W("」』）’”〕］｝〉》") // 」』）’”〕］｝〉》
            TJS_W(""));
        // 构造器末尾：defaultFaceIndex = resolveFaceIndex(L"normal")。
        _defaultFaceIndex = resolveFaceIndex(ttstr(TJS_W("normal")));
    }
    // 四文件析构链都按声明逆序销毁 faceHash/列表/ttstr，且不 Release objthis。
    // Android 保留独立对象析构 helper；iOS 将其内联进
    // ncbInstanceAdaptor<TextRenderBase>::_deleteInstance。
    ~TextRenderBase() = default;

    // objthis 由 ncbind factory wrapper 直接注入；参数个数/内容在此 raw callback 中
    // 不参与构造，成功时只把完整对象写入 result。对象大小由本平台 C++ ABI 决定。
    static tjs_error factory(TextRenderBase **result, tjs_int /*numparams*/,
                             tTJSVariant ** /*params*/, iTJSDispatch2 *objthis) {
        *result = new TextRenderBase(objthis);
        return TJS_S_OK;
    }

    // ============================================================
    // Property accessors（四文件均为 22 RW + 11 RO）
    // ============================================================
#define TR_RW(type, prop, field)                                               \
    type get_##prop() const { return field; }                                  \
    void set_##prop(type v) { field = v; }
#define TR_RO(type, prop, field)                                               \
    type get_##prop() const { return field; }

    // RW (22)。浮点 property 的源码边界就是 float；四个目标都直接从单精度寄存器
    // 返回/接收，不在 accessor 内提升为 double。
    TR_RW(bool, vertical, _vertical)
    TR_RW(float, timeScale, _timeScale)
    TR_RW(float, fontScale, _fontScale)

    // face index → name 的公共 helper 在 Android armv7 与两份 iOS 保留独立边界，
    // Android arm64 全内联。参数是 uint32；负 int 传入后按大正数越界并返回 L""。
    ttstr getFaceName(tjs_uint32 index) const {
        if(_faceTable.size() <= index)
            // 三个保留 helper 都调用 L"" 字面量构造入口；不能简化成 ttstr()。
            return ttstr(TJS_W(""));
        return _faceTable[index];
    }
    // defaultFace 由 index 间接表示；无符号越界时 getter 返回空串，setter intern 名称。
    ttstr get_defaultFace() const {
        return getFaceName((tjs_uint32)_defaultFaceIndex);
    }
    void set_defaultFace(ttstr v) {
        _defaultFaceIndex = resolveFaceIndex(v);
    }
    TR_RW(float, defaultFontSize, _defaultFontSize)
    TR_RW(float, defaultBigFontSize, _defaultBigFontSize)
    TR_RW(float, defaultSmallFontSize, _defaultSmallFontSize)
    TR_RW(float, defaultLineSize, _defaultLineSize)
    TR_RW(float, defaultLineSpacing, _defaultLineSpacing)
    TR_RW(float, defaultPitch, _defaultPitch)
    TR_RW(tjs_int, defaultAlign, _defaultAlign)
    TR_RW(tjs_int, defaultValign, _defaultValign)
    TR_RW(float, defaultRubySize, _defaultRubySize)
    TR_RW(float, defaultRubyOffset, _defaultRubyOffset)

    // 颜色 property 的脚本边界是带符号 32-bit integer；后备字段仍保留无符号位型。
    // 因此 0xFFFFFFFF 在 property getter 上是 -1，而 getCharacters 字典中的颜色会
    // 以零扩展值返回，两条公开路径不能合并为同一种装箱行为。
    TR_RW(tjs_int, defaultChColor, _defaultChColor)
    TR_RW(bool, defaultShadow, _defaultShadow)
    TR_RW(tjs_int, defaultShadowColor, _defaultShadowColor)
    TR_RW(tjs_int, defaultShadowDiff, _defaultShadowDiff)
    TR_RW(bool, defaultEdge, _defaultEdge)
    TR_RW(tjs_int, defaultEdgeColor, _defaultEdgeColor)
    TR_RW(bool, defaultBold, _defaultBold)
    TR_RW(bool, defaultItalic, _defaultItalic)

    // RO (11)。
    TR_RO(bool, renderOver, _renderOver)
    tjs_int get_renderLines() const {
        return (tjs_int)_lineList.size();
    }
    TR_RO(tjs_int, renderCount, _renderCount)
    float get_renderDelay() const {
        return _renderDelayAccum * _timeScale;
    }
    TR_RO(float, renderLeft, _renderLeft)
    TR_RO(float, renderTop, _renderTop)
    TR_RO(float, renderRight, _renderRight)
    TR_RO(float, renderBottom, _renderBottom)
    TR_RO(ttstr, renderText, _renderText)
    // 直接返回视口末端减布局末端；不 clamp，负值/NaN 原样传播。
    float get_maxScrollOffset() const {
        return _vertical ? (_renderSizeW - _renderLeft)
                         : (_renderSizeH - _renderBottom);
    }

    // 从视口尺寸起，自最后一行向前减 lineHeight，得到能从尾部完整容纳的行位置。
    // remaining==0 仍算容纳；全部行都能放下时固定返回 1，一行都放不下则返回 0。
    // 无尺寸/行高规范化；负值与 NaN 按原生 float 比较自然流过。
    float get_maxScrollLine() const {
        int count = (int)_lineList.size();
        float result = 0.0f;
        if(count >= 1) {
            int offset = 0;
            float remaining = _vertical ? _renderSizeW : _renderSizeH;
            int idx = count - 1;
            for(;;) {
                remaining = remaining - _lineList[idx].lineHeight;
                if(remaining < 0.0f)
                    break;
                --offset;
                if(count + offset <= 0)
                    return 1.0f;
                --idx;
            }
            if(offset != 0)
                return (float)(count + offset);
        }
        return result;
    }

#undef TR_RW
#undef TR_RO

    // ============================================================
    // NCB typed 方法（实例方法，经四个目标共有的 ncbind invoker 模板分发）
    //   numparams/-1004(BADPARAMCOUNT)/-1008(NATIVECLASSCRASH)/result-Clear 由 ncbind
    //   invoker 模板自然产出。
    // ============================================================
    // setRenderSize：写入两个视口尺寸后调用 clear。
    void setRenderSize(float w, float h) {
        _renderSizeW = w;
        _renderSizeH = h;
        clear();
    }
    // 四文件共同控制流：复位全部渲染状态、重建列表，并清空 face 表后重新 intern
    // 旧 default face；clear→resetFont 可触发 onStyleChanged（读 objthis 成员）。
    void clear() {
        // pending Line 清空（deque 清空并零化行 metric）。
        _pendingLine.clear();
        if(_vertical) {
            // 竖排：pen X、全局左右边界与 pending 行左右边界均从 renderSizeW 起步。
            _penX = _renderSizeW;
            _renderLeft = _renderSizeW;
            _renderRight = _renderSizeW;
            _pendingLine.bboxRight = _renderSizeW;
            _pendingLine.bboxLeft = _renderSizeW;
        } else {
            // 横排：bbox left/right 不写，保持 Line::clear 后的 0
            _penX = 0;
            _renderLeft = 0;
            _renderRight = 0;
        }
        _penY = 0;
        _renderTop = 0;
        _renderBottom = 0;
        _kinsokuUsed = 0;
        _curRubyText.Clear();
        _charBufCountdown = 0;
        _lineStartX = 0;
        _accumBuf.clear();
        resetFont();
        // 当前样式从对应 default 字段复位。
        _curPitch = _defaultPitch;
        _curLineSize = _defaultLineSize;
        _curLineSpacing = _defaultLineSpacing;
        _curAlign = _defaultAlign;
        _curValign = _defaultValign;
        // 行列表清空时析构每个 Line 的嵌套 deque。
        _lineList.clear();
        // charList 是非 owning 指针列表；CharItem 由 Line deque 拥有，此处不 delete。
        _charList.clear();
        _renderPos = 0;
        _renderPosSnap = 0;
        _unusedResetStateB_guess = 0;
        _renderDelayAccum = 0;
        _charDelayStep = 0;
        _unusedResetStateA_guess = 0;
        _renderCount = 0;
        _renderOver = false;
        _keyWaitList.clear();
        _renderText.Clear();
        // face 表压缩：取旧 default face name → 清表 → 重 intern → 写回索引。
        ttstr defFaceName = getFaceName((tjs_uint32)_defaultFaceIndex);
        _faceHash.clear();
        _faceTable.clear();
        _defaultFaceIndex = resolveFaceIndex(defFaceName);
    }
    // resetFont：当前样式从 default* 复位。三路变化检测命中 → 全组复位 +
    //   onStyleChanged（读 objthis 成员）；rubySize 单独门控；其余无条件复位。
    void resetFont() {
        int defFace = _defaultFaceIndex;
        bool doGroup;
        if(_curFaceIndex != defFace)
            doGroup = true;
        else if(_curBold != _defaultBold)
            doGroup = true;
        else if(_curItalic != _defaultItalic ||
                _defaultFontSize != _curFontSize)
            doGroup = true;
        else
            doGroup = false;
        if(doGroup) {
            _curFontSize = _defaultFontSize;
            _curFaceIndex = defFace;
            _curBold = _defaultBold;
            _curItalic = _defaultItalic;
            onStyleChanged();
        }
        if(_curRubySize < 0.0f || _defaultRubySize != _curRubySize)
            _curRubySize = _defaultRubySize;
        _curRubyOffset = _defaultRubyOffset;
        _curShadow = _defaultShadow;
        _curEdge = _defaultEdge;
        // 四个相邻的 current 色值从对应 default 色值整体复位。
        _curChColor = _defaultChColor;
        _curShadowColor = _defaultShadowColor;
        _curShadowDiff = _defaultShadowDiff;
        _curEdgeColor = _defaultEdgeColor;
    }
    // resetStyle：5 个字段从 default 复位。**不调 resetFont、无 onStyleChanged**。
    void resetStyle() {
        _curLineSpacing = _defaultLineSpacing;
        _curPitch = _defaultPitch;
        _curLineSize = _defaultLineSize;
        _curAlign = _defaultAlign;
        _curValign = _defaultValign;
    }
    // newline：四文件都只在 pending deque 非空时调用 finishLine，并丢弃其
    // bool 返回值。over-Y 失败对这个 void 方法静默可见，具体残留状态见 analysis。
    void newline() {
        if(!_pendingLine.chars.empty())
            finishLine();
    }
    // done：终结布局。① pending 非空 → finishLine（返回值有意忽略）
    //   ② 遍历行列表算全局 bbox
    //   ③ valign 偏移加到每 char.y + 调整全局 top/bottom ④ charList 从各行 deque 铺
    //   charItem 指针 ⑤ keyWait 列表 index→renderPos 回填 ⑥ charList 按 renderPos 排序。
    void done() {
        // ① pending 非空 → finishLine。四文件都丢弃 false 并继续收束已有行。
        if(!_pendingLine.chars.empty())
            finishLine();
        // ② 按行聚合全局 bbox。
        for(const Line &li : _lineList) {
            if(_renderTop > li.bboxTop)
                _renderTop = li.bboxTop;
            if(_renderBottom < li.bboxBottom)
                _renderBottom = li.bboxBottom;
            if(_renderLeft > li.bboxLeft)
                _renderLeft = li.bboxLeft;
            if(_renderRight < li.bboxRight)
                _renderRight = li.bboxRight;
        }
        // ③ 横排时计算 valign 的整数像素偏移。
        if(!_vertical) {
            int valign = _curValign;
            float currentBottom = _renderBottom;
            int delta;
            if(valign == 1)
                delta = (int)(float)(_renderSizeH - currentBottom);
            else if(valign == 0)
                delta = (int)(float)((float)(_renderSizeH - currentBottom) * 0.5f);
            else // 其它：无偏移
                delta = 0;
            if(delta != 0) {
                for(Line &li : _lineList)
                    for(CharItem &ci : li.chars)
                        ci.y = ci.y + (float)delta;
            }
            float currentTop = _renderTop;
            _renderBottom = currentBottom + (float)delta;
            _renderTop = currentTop + (float)delta;
        }
        // ④ charList 重建：从各行 deque 按布局顺序铺非 owning CharItem 指针。
        // clear 先提交；若后续 vector allocation 抛出，已铺好的 prefix 保留，
        // 旧的完整 charList 不回滚，KeyWait 与 sort 也尚未执行。
        _charList.clear();
        for(Line &li : _lineList)
            for(CharItem &ci : li.chars)
                _charList.push_back(&ci); // 指向行 deque 内元素（deque 元素地址稳定）
        // ⑤ keyWait 列表 time 段回填：读 index(低 int) 索引 flatten 顺序的
        //   charList，
        //   把该 CharItem 的 renderPos float bits 写入 time；index 不动。
        //   无边界检查，依赖脚本不变量：
        //   \k push 的 renderCount 必落在 done 重建后的 charList 内），1:1 照搬。
        for(size_t i = 0; i < _keyWaitList.size(); ++i) {
            int idx = _keyWaitList[i].index;
            memcpy(&_keyWaitList[i].time, &_charList[idx]->renderPos,
                   sizeof(_keyWaitList[i].time));
        }
        // ⑥ charList 按 charItem.renderPos 升序 std::sort。Android 是 16 项阈值的
        // libstdc++ introsort/final-insertion，iOS 是 30 项 insertion 阈值的 libc++
        // sort；两者都不稳定。相等值不保证 flatten 顺序，NaN 也不做规范化。
        std::sort(_charList.begin(), _charList.end(),
                         [](const CharItem *a, const CharItem *b) {
                             return a->renderPos < b->renderPos;
                         });
    }
    // 返回行列表第 lineIdx 项的 offset；越界返回 renderBottom。四目标都从 float
    // 字段载入后显式提升为 double 返回，因此公开 C++ 签名必须保留 double。
    double calcLineOffset(tjs_int lineIdx) {
        // 索引先按平台 size_t 做无符号比较；负数也因此走越界后备。
        if(_lineList.size() <= (size_t)lineIdx)
            return _renderBottom;
        return _lineList[lineIdx].lineBottom;
    }
    // char 列表倒扫，找在给定 width 内可显示的字符数。
    tjs_int calcShowCount(tjs_int width) {
        tjs_int count = (tjs_int)_charList.size();
        if(count - 1 < 1) // count <= 1
            return 0;
        float ts = _timeScale;
        tjs_int index = count - 1;
        tjs_int result = count;
        while((float)(_charList[index]->renderPos * ts) > (float)width) {
            bool atHead = (index <= 1);
            --index;
            --result;
            if(atHead)
                return 0;
        }
        return result;
    }

    // ============================================================
    // dict 解析方法（setOption/setDefault/setFont/setStyle）。
    //   dict 参数经 ncbind 封送为 tTJSVariant；方法体内的 ncbPropAccessor
    //   持有 AsObject() 增加的 dispatch 引用。它先于工作 variant 构造，因而正常与
    //   支持展开的目标都是先析构 variant，再 Release dictionary。
    // ============================================================
    // setOption：(dict) → option 标量字段和四个禁则字符串；键顺序与覆盖关系见 analysis。
    void setOption(tTJSVariant dictVar) {
        if(dictVar.Type() != tvtObject)
            TJSThrowVariantConvertError(dictVar, tvtObject);
        ncbPropAccessor dict(dictVar);
        tTJSVariant v;
        // 四段字符串查询直接属于 setOption。每个 string 分支各构造一个短 ttstr，
        // 赋值完成后立即析构；void 写空串，其它类型抛 String 转换错误。
        if(dict.checkVariant(TJS_W("following"), v)) {
            if(v.Type() == tvtString)
                _following = ttstr(v);
            else if(v.Type() == tvtVoid)
                _following = ttstr();
            else
                TJSThrowVariantConvertError(v, tvtString);
        }
        if(dict.checkVariant(TJS_W("leading"), v)) {
            if(v.Type() == tvtString)
                _leading = ttstr(v);
            else if(v.Type() == tvtVoid)
                _leading = ttstr();
            else
                TJSThrowVariantConvertError(v, tvtString);
        }
        if(dict.checkVariant(TJS_W("begin"), v)) {
            if(v.Type() == tvtString)
                _begin = ttstr(v);
            else if(v.Type() == tvtVoid)
                _begin = ttstr();
            else
                TJSThrowVariantConvertError(v, tvtString);
        }
        if(dict.checkVariant(TJS_W("end"), v)) {
            if(v.Type() == tvtString)
                _end = ttstr(v);
            else if(v.Type() == tvtVoid)
                _end = ttstr();
            else
                TJSThrowVariantConvertError(v, tvtString);
        }
        // --- byte/DWORD 选项：按二进制顺序直接执行 TJS variant 真值转换 ---
        if(dict.checkVariant(TJS_W("vertical"), v))
            _vertical = v.operator bool();
        if(dict.checkVariant(TJS_W("kinsoku_max"), v))
            _kinsokuMax = v.operator bool() ? 1 : 0;
        if(dict.checkVariant(TJS_W("word_break"), v))
            _wordBreak = v.operator bool();
        if(dict.checkVariant(TJS_W("ignore_color"), v))
            _ignoreColor = v.operator bool();
        if(dict.checkVariant(TJS_W("ignore_size"), v))
            _ignoreSize = v.operator bool();
        if(dict.checkVariant(TJS_W("ignore_delay"), v))
            _ignoreDelay = v.operator bool();
        if(dict.checkVariant(TJS_W("ignore_over"), v))
            _ignoreOverY = v.operator bool();
        if(dict.checkVariant(TJS_W("ignore_overy"), v))
            _ignoreOverY = v.operator bool();   // 后出现的同义键覆盖 ignore_over。
        if(dict.checkVariant(TJS_W("ignore_overx"), v))
            _ignoreOverX = v.operator bool();
        if(dict.checkVariant(TJS_W("width_time_scale"), v))
            _widthTimeScale = v.operator bool();
        if(dict.checkVariant(TJS_W("ignore_ruby"), v))
            _ignoreRuby = v.operator bool();
        if(dict.checkVariant(TJS_W("ignore_type"), v))
            _ignoreType = v.operator bool();
        if(dict.checkVariant(TJS_W("ignore_face"), v))
            _ignoreFace = v.operator bool();
        if(dict.checkVariant(TJS_W("ignore_style"), v))
            _ignoreStyle = v.operator bool();
    }
    // setDefault：(dict) → default* 字段（face/字号族/颜色族/对齐/间距）。
    void setDefault(tTJSVariant dictVar) {
        if(dictVar.Type() != tvtObject)
            TJSThrowVariantConvertError(dictVar, tvtObject);
        ncbPropAccessor dict(dictVar);
        tTJSVariant v;
        // face → resolveFaceIndex。string→取值；void→空串；其它类型转换错误。
        if(dict.checkVariant(TJS_W("face"), v)) {
            ttstr faceName;
            if(v.Type() == tvtString)
                faceName = ttstr(v);
            else if(v.Type() == tvtVoid)
                faceName = ttstr();
            else
                TJSThrowVariantConvertError(v, tvtString);
            _defaultFaceIndex = resolveFaceIndex(faceName);
        }
        if(dict.checkVariant(TJS_W("bold"), v))
            _defaultBold = v.operator bool();
        // fontsize 存在时，把缺省 big/small/ruby 字号回填为同一值。
        bool hasFontsize = dict.checkVariant(TJS_W("fontsize"), v);
        if(hasFontsize) {
            _defaultFontSize = (float)v.AsReal();
            if(!dict.checkVariant(TJS_W("bigfontsize"), v))
                _defaultBigFontSize = _defaultFontSize;
            if(!dict.checkVariant(TJS_W("smallfontsize"), v))
                _defaultSmallFontSize = _defaultFontSize;
            if(!dict.checkVariant(TJS_W("rubysize"), v))
                _defaultRubySize = _defaultFontSize;
        } else {
            // fontsize 缺失：独立读 big/small/ruby；各键缺失时保留字段原值。
            if(dict.checkVariant(TJS_W("bigfontsize"), v))
                _defaultBigFontSize = (float)v.AsReal();
            if(dict.checkVariant(TJS_W("smallfontsize"), v))
                _defaultSmallFontSize = (float)v.AsReal();
            if(dict.checkVariant(TJS_W("rubysize"), v))
                _defaultRubySize = (float)v.AsReal();
        }
        if(dict.checkVariant(TJS_W("rubyoffset"), v))
            _defaultRubyOffset = (float)v.AsReal();
        if(dict.checkVariant(TJS_W("color"), v))
            _defaultChColor = (tjs_uint32)v.AsInteger();
        if(dict.checkVariant(TJS_W("shadow"), v))
            _defaultShadow = v.operator bool();
        if(dict.checkVariant(TJS_W("shadowcolor"), v))
            _defaultShadowColor = (tjs_uint32)v.AsInteger();
        if(dict.checkVariant(TJS_W("shadowdiff"), v))
            _defaultShadowDiff = (tjs_uint32)v.AsInteger();
        if(dict.checkVariant(TJS_W("edge"), v))
            _defaultEdge = v.operator bool();
        if(dict.checkVariant(TJS_W("edgecolor"), v))
            _defaultEdgeColor = (tjs_uint32)v.AsInteger();
        if(dict.checkVariant(TJS_W("linespacing"), v))
            _defaultLineSpacing = (float)v.AsReal();
        if(dict.checkVariant(TJS_W("pitch"), v))
            _defaultPitch = (float)v.AsReal();
        // linesize 缺失时回退读取 fontsize。
        if(dict.checkVariant(TJS_W("linesize"), v))
            _defaultLineSize = (float)v.AsReal();
        else if(dict.checkVariant(TJS_W("fontsize"), v))
            _defaultLineSize = (float)v.AsReal();
        if(dict.checkVariant(TJS_W("align"), v))
            _defaultAlign = (tjs_int)v.AsInteger();
        if(dict.checkVariant(TJS_W("valign"), v))
            _defaultValign = (tjs_int)v.AsInteger();
    }

    // setFont：更新当前字体/描边字段；只有 face/bold/fontsize 变化会触发回调。
    void setFont(tTJSVariant dictVar) {
        if(dictVar.Type() != tvtObject)
            TJSThrowVariantConvertError(dictVar, tvtObject);
        ncbPropAccessor dict(dictVar);
        tTJSVariant v;
        bool changed = false;
        // face：present→resolveFaceIndex；idx 变则更新并置 changed=true。
        if(dict.checkVariant(TJS_W("face"), v)) {
            ttstr faceName;
            if(v.Type() == tvtString)
                faceName = ttstr(v);
            else if(v.Type() == tvtVoid)
                faceName = ttstr();
            else
                TJSThrowVariantConvertError(v, tvtString);
            int idx = resolveFaceIndex(faceName);
            if(_curFaceIndex != idx) {
                _curFaceIndex = idx;
                changed = true;
            }
        }
        if(dict.checkVariant(TJS_W("bold"), v)) {
            bool b = v.operator bool();
            if(_curBold != b) {
                _curBold = b;
                changed = true;
            }
        }
        if(dict.checkVariant(TJS_W("fontsize"), v)) {
            float f = (float)v.AsReal();
            if(_curFontSize < 0.0f || _curFontSize != f) {
                _curFontSize = f;
                changed = true;
            }
        }
        if(dict.checkVariant(TJS_W("rubysize"), v)) {
            float f = (float)v.AsReal();
            if(_curRubySize < 0.0f || _curRubySize != f) // 不置 changed。
                _curRubySize = f;
        }
        if(dict.checkVariant(TJS_W("rubyoffset"), v))
            _curRubyOffset = (float)v.AsReal();
        if(dict.checkVariant(TJS_W("color"), v))
            _curChColor = (tjs_uint32)v.AsInteger();
        if(dict.checkVariant(TJS_W("shadow"), v))
            _curShadow = v.operator bool();
        if(dict.checkVariant(TJS_W("shadowcolor"), v))
            _curShadowColor = (tjs_uint32)v.AsInteger();
        if(dict.checkVariant(TJS_W("shadowdiff"), v))
            _curShadowDiff = (tjs_uint32)v.AsInteger();
        if(dict.checkVariant(TJS_W("edge"), v))
            _curEdge = v.operator bool();
        if(dict.checkVariant(TJS_W("edgecolor"), v))
            _curEdgeColor = (tjs_uint32)v.AsInteger();
        if(changed)
            onStyleChanged();
    }

    // setStyle：更新当前间距/对齐；linesize 缺失时回退 fontsize，不触发字体回调。
    void setStyle(tTJSVariant dictVar) {
        if(dictVar.Type() != tvtObject)
            TJSThrowVariantConvertError(dictVar, tvtObject);
        ncbPropAccessor dict(dictVar);
        tTJSVariant v;
        if(dict.checkVariant(TJS_W("linespacing"), v))
            _curLineSpacing = (float)v.AsReal();
        if(dict.checkVariant(TJS_W("pitch"), v))
            _curPitch = (float)v.AsReal();
        if(dict.checkVariant(TJS_W("linesize"), v))
            _curLineSize = (float)v.AsReal();
        else if(dict.checkVariant(TJS_W("fontsize"), v))
            _curLineSize = (float)v.AsReal();
        if(dict.checkVariant(TJS_W("align"), v))
            _curAlign = (tjs_int)v.AsInteger();
        if(dict.checkVariant(TJS_W("valign"), v))
            _curValign = (tjs_int)v.AsInteger();
    }

    // ============================================================
    // onEval / getKeyWait / getCharacters（NCB typed 查询/eval 方法）
    // ============================================================
    // onEval：(expr) → TJS eval（在 objthis 上下文求值表达式，结果写 result）。
    // 四个参考目标都直接在隐藏返回槽构造 void variant，再把同一返回槽交给
    // TVPExecuteExpression；参数 variant→ttstr 的转换位于 NCB invoker 层。
    tTJSVariant onEval(ttstr expr) {
        tTJSVariant result; // result.type=0（构造即 void）
        TVPExecuteExpression(expr, objthis, &result);
        return result;
    }

    // getKeyWait：() → Array<dict{pos,time}>。pos/time 都读取 KeyWaitItem 的低
    // 32-bit index；done 写入的高 32-bit renderPos 位型不参与此查询。
    tTJSVariant getKeyWait() {
        ncbArrayAccessor arr;
        // Native instance 指针只为保留原始 accessor idiom；后续 append 走 dispatch。
        tTJSArrayNI *ni = nullptr;
        arr.GetDispatch()->NativeInstanceSupport(
                TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
                (iTJSNativeInstance **)&ni);
        // 循环上界只快照一次；下标访问每轮仍从 vector 的 begin 读取。
        unsigned int count = (unsigned int)_keyWaitList.size();
        for(unsigned int i = 0; i < count; ++i) {
            ncbDictionaryAccessor dict;
            int index = _keyWaitList[i].index;
            dict.SetValue(TJS_W("pos"), (tjs_int)index, 512u,
                          &s_hintPos);
            dict.SetValue(TJS_W("time"), (tjs_int)index, 512u,
                          &s_hintTime);
            // FuncCall 的 param1 按值接收，故命名 vDict 之外还有一个参数副本。
            tTJSVariant vDict(dict.GetDispatch(), (iTJSDispatch2 *)nullptr);
            arr.FuncCall(0u, TJS_W("add"), &s_hintAdd, nullptr,
                         vDict);
        }
        tTJSVariant result(arr.GetDispatch(), (iTJSDispatch2 *)nullptr);
        return result;
    }

    // getCharacters：(start,count) → 每字符 dictionary 的 Array。count==0 时取
    // renderCount-start；越过 charList 尾部时只钳制 count，不单独拒绝负 start。
    tTJSVariant getCharacters(tjs_int start, tjs_int count) {
        ncbArrayAccessor arr;
        tTJSArrayNI *ni = nullptr;
        arr.GetDispatch()->NativeInstanceSupport(
                TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
                (iTJSNativeInstance **)&ni);
        // 保留原始 accessor idiom；元素实际全部经 dispatch 写入。
        std::deque<tTJSVariant> *items = &ni->Items;
        (void)items;
        if(!count)
            count = _renderCount - start;
        int charListCount = (int)_charList.size();
        int resultCount;
        if(count + start <= charListCount)
            resultCount = count;
        else
            resultCount = charListCount - start;
        if(resultCount >= 1) {
            int lastFaceIndex = -1;
            // 函数级单个 variant 缓存 face 名；faceIndex 不变时复用。
            tTJSVariant faceName;
            for(int offset = 0; offset < resultCount; ++offset) {
                // 四个目标都先构造本轮 dictionary holder，再读取 CharItem 并按需
                // 刷新 face 缓存；因此 face 赋值抛出时本轮字典已经进入清理链。
                ncbDictionaryAccessor dict;
                // 无单独边界检查；负 start 依赖脚本侧不变量。
                CharItem *ci = _charList[offset + start];
                // face 缓存刷新（faceIndex 变化时重查 _faceTable）
                int fi = ci->faceIndex;
                if(lastFaceIndex != fi) {
                    faceName = getFaceName((tjs_uint32)fi);
                    lastFaceIndex = fi;
                }
                // 属性顺序在四个目标中一致；源码层统一经 holder.SetValue，是否
                // 内联只是各编译器的代码生成差异。
                dict.SetValue(TJS_W("graph"), (tjs_int)(ci->graph ? 1 : 0), 512u,
                              &s_hintGraph);
                dict.SetValue(TJS_W("text"), ci->text, 512u,
                              &s_hintText);
                dict.SetValue(TJS_W("x"), (tjs_real)ci->x, 512u,
                              &s_hintX);
                dict.SetValue(TJS_W("y"), (tjs_real)ci->y, 512u,
                              &s_hintY);
                dict.SetValue(TJS_W("cw"), (tjs_real)ci->cw, 512u,
                              &s_hintCw);
                dict.SetValue(TJS_W("size"), (tjs_real)ci->size, 512u,
                              &s_hintSize);
                dict.SetValue(TJS_W("face"), faceName, 512u,
                              &s_hintFace);
                // 三个颜色按 uint32 零扩展成 TJS integer；shadowDiff 则符号扩展。
                dict.SetValue(TJS_W("color"), (tTVInteger)(tjs_uint32)ci->chColor,
                              512u, &s_hintColor);
                dict.SetValue(TJS_W("bold"), (tjs_int)(ci->bold ? 1 : 0), 512u,
                              &s_hintBold);
                dict.SetValue(TJS_W("italic"), (tjs_int)(ci->italic ? 1 : 0), 512u,
                              &s_hintItalic);
                dict.SetValue(TJS_W("shadow"), (tjs_int)(ci->shadow ? 1 : 0), 512u,
                              &s_hintShadow);
                dict.SetValue(TJS_W("edge"), (tjs_int)(ci->edge ? 1 : 0), 512u,
                              &s_hintEdge);
                dict.SetValue(TJS_W("shadowColor"),
                              (tTVInteger)(tjs_uint32)ci->shadowColor, 512u,
                              &s_hintShadowColor);
                dict.SetValue(TJS_W("shadowDiff"), (tjs_int)(int)ci->shadowDiff,
                              512u, &s_hintShadowDiff);
                dict.SetValue(TJS_W("edgeColor"),
                              (tTVInteger)(tjs_uint32)ci->edgeColor, 512u,
                              &s_hintEdgeColor);
                // ruby 子数组先独立构造为返回 variant，再作为属性写入。
                if(!ci->ruby.empty()) {
                    tTJSVariant vRuby = buildRubyArray(ci->ruby);
                    dict.SetValue(TJS_W("ruby"), vRuby, 512u,
                                  &s_hintRuby);
                }
                dict.SetValue(TJS_W("vertical"),
                              (tjs_int)(ci->vertical ? 1 : 0), 512u,
                              &s_hintVertical);
                dict.SetValue(TJS_W("delay"), (tjs_real)ci->renderPos, 512u,
                              &s_hintDelay);
                // 数值 SetValue 内部把 dictionary 转成 {Object=dict,ObjThis=dict}。
                arr.SetValue((tjs_int32)offset, dict.GetDispatch(),
                             512u);
            }
        }
        tTJSVariant result(arr.GetDispatch(), arr.GetDispatch());
        return result;
    }
    // 每个 RubyItem 形成 dict{text,x,y,size}；返回 variant 的 Object/ObjThis
    // 两槽都指向子数组，外层函数再负责写入 ruby 属性。
    static tTJSVariant buildRubyArray(
            const std::vector<RubyItem> &ruby) {
        ncbArrayAccessor arr;
        tTJSArrayNI *ni = nullptr;
        arr.GetDispatch()->NativeInstanceSupport(
                TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
                (iTJSNativeInstance **)&ni);
        std::deque<tTJSVariant> *items = &ni->Items;
        (void)items;
        for(size_t i = 0; i < ruby.size(); ++i) {
            ncbDictionaryAccessor rd;
            const RubyItem &r = ruby[i];
            rd.SetValue(TJS_W("text"), r.text, 512u,
                        &s_hintText);
            rd.SetValue(TJS_W("x"), (tjs_real)r.x, 512u,
                        &s_hintX);
            rd.SetValue(TJS_W("y"), (tjs_real)r.y, 512u,
                        &s_hintY);
            rd.SetValue(TJS_W("size"), (tjs_real)r.span, 512u,
                        &s_hintSize);
            arr.SetValue((tjs_int32)i, rd.GetDispatch(),
                         512u);
        }
        tTJSVariant vArr(arr.GetDispatch(), arr.GetDispatch());
        return vArr;
    }

    // ============================================================
    // render 使用专用 NCB Process 封送体，再由共享 raw-method 包装取得原生实例。
    //   共享包装的可观察顺序是：校验成员调用与 objthis、清 result、取得实例，随后
    //   Process 才检查参数个数；因此无实例错误优先于参数个数错误。
    //   Process 解包 (text, pairMode_guess, baseDelay_guess[, size,
    //   continueRender_guess])。`_guess` 表明这些只是四端完整读写集得到的语义别名；
    //   二进制不保留原作者的形参拼写。
    //   size 会按 real 强制转换并传给真 render；四个主体均保留该 float 形参但不读取，
    //   不能在包装层提前丢弃。缺省 size/continueRender_guess 都为零；第 5 参数按 TJS
    //   布尔规则转换。pairMode_guess 只作 begin/end 配对门控，baseDelay_guess 初始化
    //   每字 renderPos 步进并作为 `%d/%D` 的默认或比例基数，它们都不是坐标。
    // ============================================================
    static tjs_error render(tTJSVariant *result, tjs_int numparams,
                            tTJSVariant **param,
                            TextRenderBase *t) {
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;
        float size;
        bool continueRender_guess;
        if(numparams == 3) {
            size = 0.0f;
            continueRender_guess = false;
        } else {
            // 第 4 参数强制转 real，并继续传给主体的未使用 size 形参。
            size = (float)param[3]->AsReal();
            // 缺少第 5 参数时不续写；存在时按 TJS 真值转换。
            if(numparams < 5)
                continueRender_guess = false;
            else
                continueRender_guess = param[4]->operator bool();
        }
        bool ok;
        {
            // 四个 Process 都在 renderImpl 后立即析构 text，然后才向
            // result 装箱；内层作用域保留这个别名/异常可观的先后顺序。
            ttstr text(*param[0]);
            tjs_int pairMode_guess = (tjs_int)param[1]->AsInteger();
            tjs_int baseDelay_guess = (tjs_int)param[2]->AsInteger();
            ok = t->renderImpl(text, (int)pairMode_guess,
                               (int)baseDelay_guess, size,
                               continueRender_guess);
        }
        if(result)
            *result = (tjs_int)(ok ? 1 : 0);
        return TJS_S_OK;
    }

    // ============================================================
    // 落字 / 行布局 / render 状态机（内部实现，读 objthis 成员回调脚本）
    // ============================================================

    // 字宽度量回调：FuncCall(L"onGetTextWidth", text[str], size[real])，
    //   返回值按 result.type 强制转 double（忠实移植，脚本层取字宽——非平台边界）。
    //   回调目标 = 借用的 objthis dispatch 成员。
    //   无 objthis null 检查、返回码不检查；回调后直接转换 result 的实际内容。
    //   返回值分发为：object/octet 抛 Real 转换错、string 解析、
    //   int(4)/real(5)→数值、void→0.0 —— 与 tTJSVariant::AsReal 逐 case 同构。
    float onGetTextWidth(const ttstr &text, float size) {
        tTJSVariant result;                 // type 预置 void
        tTJSVariant vText(text);            // arg0: string（AddRef）
        tTJSVariant vSize((tjs_real)size);  // arg1: real
        tTJSVariant *args[2] = { &vText, &vSize };
        objthis->FuncCall(0, TJS_W("onGetTextWidth"), &s_hintOnGetTextWidth,
                          &result, 2, args, objthis);
        return (float)result.AsReal();
    }

    // 累积 UTF-16 buffer + 度量 + ruby + 落字入口。
    bool appendChar(tjs_char ch) {
        // 四份都是 vector<tjs_char>::push_back；满容量时先分配新 buffer，写新字符，
        // 再平凡搬迁旧码元并提交，因此分配失败不会改变旧 vector。
        _accumBuf.push_back(ch);
        // 倒计数先减一，仅当结果非负才回写并返回 true。
        {
            int countdown = _charBufCountdown - 1;
            if(countdown >= 0) {
                _charBufCountdown = countdown;
                return true;
            }
        }
        // buffer 非恰好 1 字符 → 不落字，返回 false（二进制 `(end-begin)!=2`）
        if(_accumBuf.size() != 1)
            return false;
        // 恰好 1 字符且计数耗尽：度量字宽 + 构造 charItem 蓝图。
        // 四份都构造两个独立 ttstr：一个作为度量输入，另一个直接成为栈上 CharItem
        // 蓝图的 text 字段；后者没有具名临时和额外 AddRef/Release 对。
        // fontScale*curFontSize 也在度量与字段写入处独立重算。
        // 这一个是具名局部，不是只活到回调结束的实参临时：四端都在 kinsoku
        // 返回并析构 CharItem 的 ruby/text 之后才 Release 它。
        ttstr measureText((tjs_char)ch);
        float cw = onGetTextWidth(measureText,
                                  _fontScale * _curFontSize);
        CharItem v((tjs_char)ch);       // text 原位构造，无额外 AddRef/Release
        v.cw = cw;
        v.size = _fontScale * _curFontSize;
        v.graph = false;
        v.bold = _curBold;
        v.italic = _curItalic;
        v.shadow = _curShadow;
        v.edge = _curEdge;
        v.vertical = _vertical;
        v.chColor = _curChColor;
        v.shadowColor = _curShadowColor;
        v.edgeColor = _curEdgeColor;
        v.shadowDiff = _curShadowDiff;
        v.faceIndex = _curFaceIndex;
        // ruby 分支只在横排且当前 ruby 文本非空时进入。
        if(!_vertical && !_curRubyText.IsEmpty()) {
            float rubyCw =
                onGetTextWidth(_curRubyText,
                               _fontScale * _curRubySize);
            // ruby 子项：x = cw*0.5 - rubyCw*0.5；y = -(rubySize*fontScale) - rubyOffset
            float rubyX = (float)(cw * 0.5f) - (float)(rubyCw * 0.5f);
            float rubyY = (float)-(float)(_curRubySize * _fontScale)
                          - _curRubyOffset;
            // 四份共同源码形态是 resize(size()+1)+back()：新 RubyItem 全字段值初始化，
            // 随后按 x、y、text、span 顺序就地赋值；不是 push_back 一个临时对象。
            // 扩容时既有项的 text 都经历配对的 AddRef/Release。
            v.ruby.resize(v.ruby.size() + 1);
            RubyItem &slot = v.ruby.back();
            slot.x = rubyX;
            slot.y = rubyY;
            slot.text = _curRubyText;
            slot.span = _curRubySize * _fontScale;
            _curRubyText.Clear();
            // ruby bbox 累加（left/top/right；bottom 槽在四端均未观察到读取）。
            float penX = _penX;
            float penYpos = _penY;
            float ry = rubyY + penYpos;
            if(ry < _rubyTop)
                _rubyTop = ry;
            float rx = rubyX + penX;
            if(rx < _rubyLeft)
                _rubyLeft = rx;
            float rxr = rubyCw + rx;
            if(rxr > _rubyRight)
                _rubyRight = rxr;
            if(penYpos < _rubyTop)
                _rubyTop = penYpos;
        } else if(!_vertical) {
            // 无 ruby 的横排字符仍把当前 penY 纳入 rubyTop。
            if(_penY < _rubyTop)
                _rubyTop = _penY;
        }
        // 清空 UTF-16 累积 buffer，再进入 kinsoku 落字。
        _accumBuf.clear();
        return kinsoku(v);
    }

    // 是否达到换行边界：Android ARMv7 与两份 iOS 都保留独立函数边界；
    // Android ARM64 把同一判断内联进 kinsoku。
    bool isOver(const CharItem &c) const {
        if(_vertical) {
            return _renderSizeH > 0.0f &&
                   _renderSizeH <= (float)(_penY + c.size) &&
                   !_ignoreOverY;
        }
        return _renderSizeW > 0.0f &&
               _renderSizeW <= (float)(_penX + c.cw) &&
               !_ignoreOverX;
    }

    // 落字 + kinsoku 禁则：把 char 落到 pending deque，处理行尾/行首禁则。
    bool kinsoku(CharItem &c) {
        // 达到 over 边界时先执行 kinsoku 重排；未达到时直接落入后面的共同落字段。
        // 临时 deque 必须在当前字落下之前析构；finishLine/递归失败也先析构再返回。
        // Android/libstdc++ 默认构造立即分配 map+首 node，iOS/libc++ 则 lazy 分配。
        // 四个 kinsoku 都没有包围该生命期的本地 EH landing，不能把显式返回路径
        // 的析构误当成异常回滚保证。
        if(isOver(c)) {
            std::deque<CharItem> tmp;
            bool placeWithoutFinish = false; // 跳过 finishLine 直接落字
            if(!_wordBreak) {
                // 把超过 wordBreakRun 的尾部字符（trailing run）移到 tmp。
                // 入口读一次 wordBreakRun 做 >=1 门控；循环条件每轮重读成员，
                // 不是局部缓存。
                if(_pendingLine.wordBreakRun >= 1) {
                    while((int)_pendingLine.chars.size() >
                          _pendingLine.wordBreakRun) {
                        // 弹 pending 末字符到 tmp 前端；push_front 深拷贝 CharItem。
                        tmp.push_front(_pendingLine.chars.back());
                        _pendingLine.chars.pop_back();
                        --_renderCount;
                    }
                }
            } else if(_following.IndexOf(c.text) != -1) {
                // 当前字在 following 集（following 字符，可触发 kinsoku 计数下移）
                int used = _kinsokuUsed;
                if(used >= _kinsokuMax) {
                    while(_pendingLine.chars.size() >= 2) {
                        if(used < 1) {
                            // max<=0 边界：仅当末字符在 leading 集时下移一个。
                            if(_leading.IndexOf(_pendingLine.chars.back().text) != -1) {
                                tmp.push_front(_pendingLine.chars.back());
                                _pendingLine.chars.pop_back();
                                --_renderCount;
                            }
                            break;
                        }
                        tmp.push_front(_pendingLine.chars.back());
                        _pendingLine.chars.pop_back();
                        --_kinsokuUsed;
                        --_renderCount;
                        used = _kinsokuUsed;
                    }
                } else {
                    // following 集 && used < max：++kinsokuUsed 后查 pending 末字符是否
                    // 也在 following 集；命中或 pending 空则 finishLine，否则直接落字，
                    // 不 finishLine、不换行。
                    _kinsokuUsed = used + 1;
                    if(_pendingLine.chars.empty()) {
                        // pending 空：随后 finishLine。
                    } else if(_following.IndexOf(_pendingLine.chars.back().text)
                              != -1) {
                        // 行尾也在 following 集：随后 finishLine。
                    } else {
                        // back 不在 following 集 → 直接落字，不换行。
                        //   四文件此处都先析构 tmp 再落字，故标记后跳出 tmp 块。
                        placeWithoutFinish = true;
                    }
                }
            } else {
                // 当前字不在 following 集：行尾禁则（leading）处理
                if(_pendingLine.chars.size() >= 3) {
                    // 检查倒数第二个字符。
                    const CharItem &second =
                        _pendingLine.chars[_pendingLine.chars.size() - 2];
                    if(_leading.IndexOf(second.text) == -1) {
                        // 倒数第2字符不在 leading 集，且末字符在 leading 集 → 下移末字符
                        if(_leading.IndexOf(_pendingLine.chars.back().text) != -1) {
                            tmp.push_front(_pendingLine.chars.back());
                            _pendingLine.chars.pop_back();
                            --_renderCount;
                        }
                    }
                }
                // pending size>=2 且末字符在 leading 集时再下移一个。
                if(_pendingLine.chars.size() >= 2) {
                    if(_leading.IndexOf(_pendingLine.chars.back().text) != -1) {
                        tmp.push_front(_pendingLine.chars.back());
                        _pendingLine.chars.pop_back();
                        --_renderCount;
                    }
                }
            }
            // 行结束；placeWithoutFinish 时跳过，直奔落字。
            if(!placeWithoutFinish) {
                if(!finishLine())
                    return false;
                // drain tmp：以 deque 前向迭代器把回退字符递归重排到下一行，
                // 源码 token 是迭代器/range-for，非 operator[]。
                for(CharItem &item : tmp) { // while(end != cursor){ kinsoku(*cursor); ++cursor; }
                    if(!kinsoku(item))
                        return false;
                }
            }
            // tmp 在此析构；下面才进入当前字的共同落字段。
        }

        // 共同落字段直接属于 kinsoku；四个目标都没有独立 placeChar 边界。
        c.x = _penX;
        c.y = _vertical ? _penY : (_penY - c.size);
        c.renderPos = _renderPos;
        if(_renderDelayAccum <= _renderPos)
            _renderDelayAccum = c.renderPos;
        // push_back 深拷贝 CharItem（包括 ruby vector）；跨 node 时的 map/node 复用、
        // 增长和异常回滚由目标平台的 libstdc++/libc++ ABI 决定。
        _pendingLine.chars.push_back(c);

        if(!_wordBreak) {
            bool isSpace = false;
            if(c.text.c_str())
                isSpace = (TJS_strcmp(c.text.c_str(), TJS_W(" ")) == 0);
            if(isSpace) {
                _pendingLine.prevWasSpace = true;
            } else {
                if(_pendingLine.prevWasSpace)
                    _pendingLine.wordBreakRun =
                        (int)_pendingLine.chars.size();
                _pendingLine.prevWasSpace = false;
            }
        }

        _renderPosSnap = _renderPos;
        if(_widthTimeScale) {
            float rate = _charDelayStep;
            if(_vertical) {
                _renderCount += 1;
                _renderPos = (float)(rate * (float)(c.size /
                              (float)(_fontScale * _curFontSize)))
                              + _renderPosSnap;
                float newPenY = c.size + _penY;
                _penY = newPenY;
                if(_pendingLine.bboxBottom < newPenY)
                    _pendingLine.bboxBottom = newPenY;
                _penY = _curPitch + newPenY;
                return true;
            }
            _renderCount += 1;
            _renderPos = (float)(rate * (float)(c.cw /
                          (float)(_fontScale * _curFontSize)))
                          + _renderPosSnap;
        } else {
            _renderPos = _charDelayStep + _renderPosSnap;
            _renderCount += 1;
            if(_vertical) {
                float newPenY = c.size + _penY;
                _penY = newPenY;
                if(_pendingLine.bboxBottom < newPenY)
                    _pendingLine.bboxBottom = newPenY;
                _penY = _curPitch + newPenY;
                return true;
            }
        }
        float newPenX = c.cw + _penX;
        _penX = newPenX;
        if(_pendingLine.bboxRight < newPenX)
            _pendingLine.bboxRight = newPenX;
        _penX = _curPitch + newPenX;
        return true;
    }

    // finishLine：行结束。横排路径（!vertical）=
    // 行宽/over 检测 → align 偏移
    //   → align 缩进填充 → 落字到行（写坐标 + 拼接 renderText）→ push lineItem → 清 pending
    //   → renderText 追加换行 → pen 复位 + 行间距。竖排直接进入共同清理尾声。
    bool finishLine() {
        if(!_vertical) {
            float maxCharSize = 0.0f;
            for(const CharItem &ci : _pendingLine.chars)
                if(ci.size > maxCharSize)
                    maxCharSize = ci.size;
            float lineHeight = (maxCharSize <= _curLineSize)
                                   ? _curLineSize : maxCharSize;
            float renderHeight = _renderSizeH;
            if(renderHeight > 0.0f &&
               renderHeight < (float)(lineHeight + _penY)) {
                _renderOver = true;
                if(!_ignoreOverY) {
                    _pendingLine.clear();
                    return false;
                }
            }
            float alignOffset;
            int align = _curAlign;
            if(align == 1) {
                alignOffset = _renderSizeW - _penX;
            } else if(align == 0) {
                alignOffset = (float)(_renderSizeW - _penX) * 0.5f;
            } else {
                alignOffset = 0.0f;
            }
            // pending 非空就进入缩进段，不要求 alignOffset 非零。
            if(!_pendingLine.chars.empty()) {
                // 唯一的局部 class temporary 是这个 U+3000 ttstr 实参；正常返回后
                // 立即析构。四个 finishLine 都没有本地 EH landing，不能把这条正常
                // Release 或后续 renderText/lineList 的增量写入解释成异常回滚。
                float indentWidth = onGetTextWidth(ttstr((tjs_char)0x3000),
                                                   _fontScale * _curFontSize);
                if(indentWidth == 0.0f)
                    indentWidth = _curFontSize;
                int indentCount = (int)((float)(alignOffset +
                    _pendingLine.chars.front().x) / indentWidth);
                for(; indentCount >= 1; --indentCount)
                    // 就地 operator+= 单字符 0x3000，非 a=a+b 临时再赋值。
                    _renderText += (tjs_char)0x3000;
            }
            float lineBottom = 0.0f;
            for(CharItem &ci : _pendingLine.chars) {
                float charBottom = lineHeight + ci.y;
                if(lineBottom < charBottom)
                    lineBottom = charBottom;
                ci.x += alignOffset;
                ci.y = charBottom;
                if(ci.text.c_str())
                    // 就地 operator+=，非构造临时 a=a+b。
                    _renderText += ci.text;
            }
            _pendingLine.lineHeight = lineHeight;
            _pendingLine.lineBottom = lineBottom;
            float bboxLeft = alignOffset + _pendingLine.bboxLeft;
            float bboxRight = alignOffset + _pendingLine.bboxRight;
            float nextPenY = lineHeight + _penY;
            _pendingLine.bboxLeft = bboxLeft;
            _pendingLine.bboxRight = bboxRight;
            _penY = nextPenY;
            if(_pendingLine.bboxBottom < nextPenY)
                _pendingLine.bboxBottom = nextPenY;
            // push：整个 pending Line 拷入 lineList（源码层 = push_back(pendingLine)）。
            _lineList.push_back(_pendingLine);
            _pendingLine.clear(); // metric 全零化
            // renderText += L"\n"（行尾换行）
            // 就地 operator+=(L"\n")，非 a=a+b。
            _renderText += TJS_W("\n");
            // pen X 复位到行首 + 行间距推进
            _penX = _lineStartX;
            // 这里读取的是 Line::clear 后的 bboxLeft（左操作数恒 0）。
            if(_pendingLine.bboxLeft > _penX)
                _pendingLine.bboxLeft = _penX;
            _penY = _curLineSpacing + _penY;
        }
        // 横排成功与竖排都执行同一清理尾声，且顺序固定为 kinsoku、ruby、accum。
        _kinsokuUsed = 0;
        _curRubyText.Clear();
        _accumBuf.clear();
        return true;
    }

    // face 名 → 稳定 index。形参按值 ttstr；四平台调用点均构造/拷贝形参，
    // 调用后按值对象析构。
    int resolveFaceIndex(ttstr name) {
        int idx;
        auto it = _faceHash.find(name);
        if(it != _faceHash.end()) {
            idx = it->second;
        } else {
            idx = (int)_faceTable.size();
            _faceHash[name] = idx;
            // 四参考文件均不向 faceTable push：表保持空，所有新 face 退化为 index 0。
        }
        return idx;
    }

    // onStyleChanged：当前样式变化后，构造 dict{face, bold, italic}
    // 并对脚本对象 FuncCall(L"onFontChange", dict)。四个目标均由
    // ncbDictionaryAccessor 持有字典，且没有 objthis null 检查。
    void onStyleChanged() {
        ncbDictionaryAccessor dict;
        // face: _faceTable[_curFaceIndex]；索引为负或越界时写空串。
        ttstr faceName = getFaceName((tjs_uint32)_curFaceIndex);
        // SetValue 内部的临时 variant 在每次 PropSet 返回后立即析构；faceName
        // 本身则保持到回调返回之后。
        dict.SetValue(TJS_W("face"), faceName, TJS_MEMBERENSURE, &s_hintFace);
        dict.SetValue(TJS_W("bold"), (tjs_int)(_curBold ? 1 : 0),
                      TJS_MEMBERENSURE, &s_hintBold);
        dict.SetValue(TJS_W("italic"), (tjs_int)(_curItalic ? 1 : 0),
                      TJS_MEMBERENSURE, &s_hintItalic);
        // FuncCall(L"onFontChange", dict)：objthis 上的脚本回调。
        //   arg variant {Object=dict, ObjThis=null}——objthis 槽为 null。
        //   holder 的原始引用与 vDict 的参数引用在整个回调期间同时存活。
        tTJSVariant vDict(dict.GetDispatch(), (iTJSDispatch2 *)nullptr);
        tTJSVariant *args[1] = { &vDict };
        objthis->FuncCall(0, TJS_W("onFontChange"), &s_hintOnFontChange,
                          nullptr, 1, args, objthis);
    }

    // dict 查询直接使用 ncbPropAccessor::checkVariant：它内联为
    // PropGet(TJS_MEMBERMUSTEXIST=0x400, key, nullHint, &work, dict)，并以
    // TJS_SUCCEEDED（返回码非负）判 present。四文件都不会在相邻查询间 Clear work。

    // ============================================================
    // render 状态机 helper（scanTagUntil / scanDigits / parseHexColor /
    //   evalDollarTag / scanCharIndex）。% 分发和 begin/end 平衡段都直接位于
    //   renderImpl 主体内，cursor 为栈局部。四文件地址见联合取证基线。
    // ============================================================
    // 从 *cursor 起读字符直到遇到 delim（或到 len）。
    // 局部 vector 的容量增长和 EH 清理是标准库/ABI 差异，不在共享源码手写。
    static ttstr scanTagUntil(const tjs_char *text, int *cursor, int len,
                              tjs_char delim) {
        std::vector<tjs_char> buf;
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
            return ttstr();
        buf.push_back(0);
        return ttstr(&buf[0]);
    }
    // 从 *cursor 起读连续数字字符（0-9）；遇到的第一个非数字已被消费。
    static ttstr scanDigits(const tjs_char *text, int *cursor, int len) {
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
    // 纯解析 helper：ARMv7 Android、ARM64/ARMv7 iOS 均保留为独立函数，
    // render 调用者负责补不透明 alpha 并写入 _curChColor；ARM64 Android 将其内联。
    static tjs_uint32 parseHexColor(const tjs_char *p) {
        tjs_char first = p[0];
        if(first == 48 && (p[1] | 0x20) == 0x78) // '0' 且 ('x'|'X')
            p += 2;
        tjs_uint32 acc = 0;
        for(;;) {
            tjs_char ch = *p++;
            // 四文件内联体逐分支对应 TJS2 核心 TJSHexNum；不要在插件中
            // 另造一份 digit decoder。
            int hv = TJSHexNum(ch);
            if(hv < 0) { // 非 hex digit → 终止
                return acc;
            }
            acc = (tjs_uint32)hv | (16u * acc);
        }
    }
    // evalDollarTag：对脚本对象 FuncCall(L"onEval", tagContent) → 返回值按 type 分发。
    // 回调目标 = objthis 成员；无 null 检查，也不检查返回码。四个参考目标都先
    // 构造 result、再构造 arg，因此正常与可见异常路径均按 arg→result 析构。
    ttstr evalDollarTag(const ttstr &content) {
        tTJSVariant result;
        tTJSVariant arg(content);
        tTJSVariant *args[1] = { &arg };
        objthis->FuncCall(0, TJS_W("onEval"), &s_hintOnEval, &result, 1, args,
                          objthis);
        tjs_int ty = (tjs_int)result.Type();
        if((tjs_uint)(ty - 3) < 3) // octet/int/real
            TJSThrowVariantConvertError(result, tvtString);
        if(ty == tvtString)
            return ttstr(result);
        if(ty == tvtObject)
            TJSThrowVariantConvertError(result, tvtString);
        // 与 getFaceName 的 L"" 构造不同：四端这里直接把返回 ttstr 槽写成 null。
        return ttstr();
    }

    // begin/end 配对集字符查找：Android armv7 与两份 iOS 保留独立函数，Android
    // arm64 把同一裸 c_str 线性扫描内联进三处调用点。命中返回指针差形成的字符索引，
    // 未命中靠 NUL terminator 返回 -1；不构造单字符 ttstr，也不走 IndexOf 子串查找。
    // 空串经 ttstr 的首字符 NUL sentinel 立即未命中。
    static int scanCharIndex(const ttstr &set, tjs_char target) {
        const tjs_char *cstr = set.c_str();
        const tjs_char *p = cstr - 1;
        tjs_char ch;
        do {
            ch = p[1]; // 从第二个字符开始继续线性扫描。
            ++p;
        } while(ch != target && ch); // 直到匹配 target 或遇 0
        if(!ch)
            return -1;
        return (int)(p - cstr);
    }
    // 真 render 状态机。
    //   签名 render(this, &text, pairMode_guess, baseDelay_guess, size,
    //   continueRender_guess)：size 由公开包装传入但主体不读取；continueRender_guess
    //   bit0 清/续；pairMode_guess 为 begin/end 配对门控；baseDelay_guess 为
    //   charDelayStep 初值及 `%d/%D` 基数。
    //   度量/eval/onFontChange 读 objthis 成员。
    bool renderImpl(const ttstr &text, int pairMode_guess, int baseDelay_guess,
                    float /*size*/, bool continueRender_guess) {
        // 续写标志 bit0 为零时先清行列表、delay 累积和 keyWait 列表。
        if((continueRender_guess & 1) == 0) {
            _lineList.clear();
            _renderDelayAccum = 0;
            _keyWaitList.clear();
        }
        _renderPos = 0;
        // 唯一跨整个函数存活的局部 ttstr。14 个 scanTagUntil 返回值、
        // scanDigits/evalDollarTag 返回值和 resolveFaceIndex 按值参数均为短临时，
        // 在赋值/调用后立即析构；具体平台 EH 差异见联合取证基线。
        ttstr tagAccum;
        float curFontSizeSnap = _curFontSize;
        _charDelayStep = (float)baseDelay_guess;
        // 四份入口取值次序都是 cursor=0 → length → c_str。
        int i = 0;
        int len = (int)text.length();
        const tjs_char *p = text.c_str();
        if(i >= len)
            return finishLine() & 1;

        bool atRunStart = true;
        int pairDepth = 0;
        // 配对起始字符刻意不初始化；pairDepth 归零的 begin 路径总会先写后读。
        // 四个目标的入口都没有初始化该栈槽，不能用零值掩盖边界。
        tjs_char pairBeginChar; // NOLINT(cppcoreguidelines-init-variables)

        while(i < len) {
            int ch = (unsigned short)p[i];
            int next = i + 1;
            i = i + 1;
            tjs_char c = (tjs_char)ch;
            // 注意：begin/end 平衡 + 标签分发都在下面。

            if(c == TJS_W('#')) {
                // ----- # 颜色 hex 解析 -----
                tagAccum = scanTagUntil(p, &i, len, TJS_W(';'));
                if(_ignoreColor)
                    goto cont;
                if(!tagAccum.IsEmpty())
                    _curChColor = parseHexColor(tagAccum.c_str()) | 0xFF000000u;
                else
                    _curChColor = _defaultChColor;
                goto cont;
            } else if(c == TJS_W('$')) {
                // ----- $ eval：标签内容交给脚本回调，返回串逐字 append -----
                tagAccum = scanTagUntil(p, &i, len, TJS_W(';'));
                // 返回 ttstr 是本条赋值的临时量；赋值后立即 Release，不活到字符循环末尾。
                tagAccum = evalDollarTag(tagAccum);
                int n = (int)tagAccum.length();
                if(n < 1)
                    goto cont;
                const tjs_char *q = tagAccum.c_str();
                for(int k = 0; k < n; ++k) {
                    if((appendChar(q[k]) & 1) == 0)
                        return false;
                }
                atRunStart = false;
                goto cont;
            } else if(c == TJS_W('%')) {
                // % 样式控制；扫描 cursor 是本地 i，对象没有 cursor 字段。
                if(next >= len)
                    goto cont;
                int argIndex = i + 1;
                i = next + 1;
                tjs_char code = p[next];
                switch(code) {
                case TJS_W('0'): case TJS_W('1'): case TJS_W('2'):
                case TJS_W('3'): case TJS_W('4'): case TJS_W('5'):
                case TJS_W('6'): case TJS_W('7'): case TJS_W('8'):
                case TJS_W('9'): {
                    // %数字：回退到 code 位置重新扫描完整的字号百分比。
                    i = next;
                    tagAccum = scanDigits(p, &i, len);
                    if(!_ignoreSize) {
                        float targetFontSize;
                        int percent;
                        // 四文件都短路：标签为空时不调用共享核心 TJS_atoi。
                        if(!tagAccum.IsEmpty() &&
                           (percent = TJS_atoi(tagAccum.c_str())) > 0)
                            targetFontSize = (float)((float)percent / 100.0f) *
                                             _defaultFontSize;
                        else
                            targetFontSize = _defaultFontSize;
                        if(_curFontSize < 0.0f ||
                           _curFontSize != targetFontSize) {
                            _curFontSize = targetFontSize;
                            onStyleChanged();
                        }
                    }
                    break;
                }
                case TJS_W(';'): // %;：恢复 curFontSize = defaultFontSize。
                    if(!_ignoreSize) {
                        if(_curFontSize < 0.0f ||
                           _curFontSize != _defaultFontSize) {
                            _curFontSize = _defaultFontSize;
                            onStyleChanged();
                        }
                    }
                    break;
                // 四个目标都保留这条 C -> R -> L -> B 的源码 fall-through。
                // 因而未忽略 style 时，%C/%R/%L 最终都会把 align 写成 -1，
                // 随后还共同执行 %B 的 big-font-size 路径。
                case TJS_W('C'):
                    if(!_ignoreStyle)
                        _curAlign = 0;
                    [[fallthrough]];
                case TJS_W('R'):
                    if(!_ignoreStyle)
                        _curAlign = 1;
                    [[fallthrough]];
                case TJS_W('L'):
                    if(!_ignoreStyle)
                        _curAlign = -1;
                    [[fallthrough]];
                case TJS_W('B'):
                    if(!_ignoreSize &&
                       (_curFontSize < 0.0f ||
                        _curFontSize != _defaultBigFontSize)) {
                        _curFontSize = _defaultBigFontSize;
                        onStyleChanged();
                    }
                    break;
                case TJS_W('S'):
                    if(!_ignoreSize &&
                       (_curFontSize < 0.0f ||
                        _curFontSize != _defaultSmallFontSize)) {
                        _curFontSize = _defaultSmallFontSize;
                        onStyleChanged();
                    }
                    break;
                case TJS_W('b'): { // %b：bold（下一字符 0/1/其它）
                    if(argIndex >= len)
                        break;
                    int dflt = _defaultBold ? 1 : 0;
                    bool gate = _ignoreType;
                    i = next + 2;
                    tjs_char arg = p[argIndex];
                    int val;
                    if(arg == 48) val = 0;
                    else if(arg == 49) val = 1;
                    else val = dflt;
                    if(gate) break;
                    if((_curBold ? 1 : 0) != val) {
                        _curBold = (val != 0);
                        onStyleChanged();
                    }
                    break;
                }
                case TJS_W('i'): { // %i：italic
                    if(argIndex >= len)
                        break;
                    int dflt = _defaultItalic ? 1 : 0;
                    bool gate = _ignoreType;
                    i = next + 2;
                    tjs_char arg = p[argIndex];
                    int val;
                    if(arg == 48) val = 0;
                    else if(arg == 49) val = 1;
                    else val = dflt;
                    if(gate) break;
                    if((_curItalic ? 1 : 0) != val) {
                        _curItalic = (val != 0);
                        onStyleChanged();
                    }
                    break;
                }
                case TJS_W('e'): { // %e：edge（无 onStyleChanged）
                    if(argIndex >= len)
                        break;
                    int dflt = _defaultEdge ? 1 : 0;
                    bool gate = _ignoreType;
                    i = next + 2;
                    tjs_char arg = p[argIndex];
                    int val;
                    if(arg == 48) val = 0;
                    else if(arg == 49) val = 1;
                    else val = dflt;
                    if(gate) break;
                    if((_curEdge ? 1 : 0) != val)
                        _curEdge = (val != 0); // 注意：无 onStyleChanged
                    break;
                }
                case TJS_W('s'): { // %s：shadow（无 onStyleChanged）
                    if(argIndex >= len)
                        break;
                    int dflt = _defaultShadow ? 1 : 0;
                    bool gate = _ignoreType;
                    i = next + 2;
                    tjs_char arg = p[argIndex];
                    int val;
                    if(arg == 48) val = 0;
                    else if(arg == 49) val = 1;
                    else val = dflt;
                    if(gate) break;
                    if((_curShadow ? 1 : 0) != val)
                        _curShadow = (val != 0); // 无 onStyleChanged
                    break;
                }
                case TJS_W('f'): { // %f：face（标签到 ';' → resolveFaceIndex）
                    tagAccum = scanTagUntil(p, &i, len,
                                            TJS_W(';'));
                    if(!_ignoreFace) {
                        if(!tagAccum.IsEmpty()) {
                            int idx = resolveFaceIndex(tagAccum);
                            if(_curFaceIndex != idx) {
                                _curFaceIndex = idx;
                                onStyleChanged();
                            }
                        } else {
                            int idx = _defaultFaceIndex;
                            if(_curFaceIndex != idx) {
                                _curFaceIndex = idx;
                                onStyleChanged();
                            }
                        }
                    }
                    break;
                }
                case TJS_W('d'): { // %d：charDelayStep=(val/100)*baseDelay_guess
                    tagAccum = scanTagUntil(p, &i, len,
                                            TJS_W(';'));
                    if(!_ignoreDelay) {
                        float delayStep = (float)baseDelay_guess;
                        if(!tagAccum.IsEmpty())
                            delayStep =
                                (float)((float)(int)TJS_atoi(tagAccum.c_str()) /
                                         100.0f) * (float)baseDelay_guess;
                        _charDelayStep = delayStep;
                    }
                    break;
                }
                case TJS_W('a'): { // %a：absolute delay（标签到 ';' → charDelayStep=val）
                    tagAccum = scanTagUntil(p, &i, len,
                                            TJS_W(';'));
                    if(!_ignoreDelay) {
                        int absoluteDelay = baseDelay_guess;
                        if(!tagAccum.IsEmpty())
                            absoluteDelay = TJS_atoi(tagAccum.c_str());
                        _charDelayStep = (float)absoluteDelay;
                    }
                    break;
                }
                case TJS_W('p'): { // %p：pitch；空标签恢复默认值
                    tagAccum = scanTagUntil(p, &i, len,
                                            TJS_W(';'));
                    if(!_ignoreStyle) {
                        if(!tagAccum.IsEmpty())
                            _curPitch =
                                (float)TJS_atoi(tagAccum.c_str());
                        else
                            _curPitch = _defaultPitch;
                    }
                    break;
                }
                case TJS_W('l'): { // %l：标签到 ';' → TJS_atoi（仅校验，无字段写）
                    tagAccum = scanTagUntil(p, &i, len,
                                            TJS_W(';'));
                    if(!_ignoreDelay)
                        // 这里无条件调用 TJS_atoi；空标签也传空 c_str。
                        TJS_atoi(tagAccum.c_str()); // 结果丢弃
                    break;
                }
                case TJS_W('t'): { // %t：标签到 ';' → TJS_atoi（无字段写）
                    tagAccum = scanTagUntil(p, &i, len,
                                            TJS_W(';'));
                    if(!_ignoreDelay)
                        // 与 %l 相同，无条件调用 TJS_atoi。
                        TJS_atoi(tagAccum.c_str());
                    break;
                }
                case TJS_W('w'): { // %w：标签到 ';' → TJS_atoi（无字段写）
                    tagAccum = scanTagUntil(p, &i, len,
                                            TJS_W(';'));
                    // 此子码有非空门控，与 %l/%t 不同。
                    if(!_ignoreDelay && !tagAccum.IsEmpty())
                        TJS_atoi(tagAccum.c_str());
                    break;
                }
                case TJS_W('r'): // %r：resetFont
                    resetFont();
                    break;
                case TJS_W('D'): {
                    // %D：紧随 '$' 只改变起始 cursor 与 ignoreDelay 门控；两路都把
                    // 标签交给 TJS_atoi 并丢弃结果，绝不调用 evalDollarTag。
                    // `%D` 位于文本尾时 p[argIndex] 读到 c_str 的 NUL terminator。
                    if(p[argIndex] != 36) {
                        tagAccum = scanTagUntil(p, &i, len,
                                                TJS_W(';'));
                        // 非 $ 路径无 ignore_delay 门控；TJS_atoi 无条件调用。
                        TJS_atoi(tagAccum.c_str());
                    } else {
                        i = next + 2; // 跳过 '$'
                        tagAccum = scanTagUntil(p, &i, len,
                                                TJS_W(';'));
                        if(!_ignoreDelay)
                            // 无条件 TJS_atoi（空标签亦调）。
                            TJS_atoi(tagAccum.c_str());
                    }
                    break;
                }
                default: // 其它 %X：消费到 ';'，无字段写
                    tagAccum = scanTagUntil(p, &i, len, TJS_W(';'));
                    break;
                }
                goto cont;
            } else if(c == TJS_W('&')) {
                // ----- & ：消费标签内容（到 ';'），无字段副作用 -----
                tagAccum = scanTagUntil(p, &i, len, TJS_W(';'));
                atRunStart = false;
                goto cont;
            } else if(c == TJS_W('[')) {
                // ----- [ ：ruby 括号，消费到 ']' -----
                tagAccum = scanTagUntil(p, &i, len, TJS_W(']'));
                if(!_ignoreRuby) {
                    // 条件性 AddRef 后立即 Release，净效果为零，但四文件均保留。
                    ttstr rubyText = tagAccum;
                }
                goto cont;
            } else if(c == TJS_W('\\')) {
                // ----- \ 布局指令 -----
                if(next >= len)
                    goto cont;
                i = next + 1;
                tjs_char code = p[next];
                if(code == TJS_W('i')) {
                    // \i：lineStartX = 当前主轴 pen。
                    _lineStartX = _vertical ? _penY : _penX;
                    goto cont;
                } else if(code == TJS_W('k')) {
                    // \k：追加 {renderCount,0}；四份的 64-bit/两次 32-bit store 都把
                    // time 高半字明确清零，vector 扩容只平凡复制既有 8B 记录。
                    _keyWaitList.push_back(KeyWaitItem{ _renderCount, 0 });
                    atRunStart = false;
                    goto cont;
                } else if(code == TJS_W('n')) {
                    // \n（反斜杠 n）：finishLine；失败→中断
                    if((finishLine() & 1) == 0)
                        return false;
                    goto cont;
                } else if(code == TJS_W('r')) {
                    // \r：lineStartX = 0
                    _lineStartX = 0;
                    goto cont;
                } else if(code == TJS_W('t')) {
                    // \t：appendChar(9)（制表）
                    char ok = appendChar(9);
                    atRunStart = false;
                    if((ok & 1) == 0)
                        return false;
                    goto cont;
                } else if(code == TJS_W('w')) {
                    // \w：主轴 pen += render 入口快照的当前字号。
                    if(_vertical)
                        _penY = curFontSizeSnap + _penY;
                    else
                        _penX = curFontSizeSnap + _penX;
                    atRunStart = false;
                    goto cont;
                } else if(code == TJS_W('x')) {
                    // \x 只清 begin-run 起点标志，不终止渲染。
                    atRunStart = false;
                    goto cont;
                } else {
                    // 其它 \ 指令忽略。
                    goto cont;
                }
            } else if(ch == 10) {
                // ----- 裸 0x0A 换行 → finishLine -----
                if((finishLine() & 1) == 0)
                    return false;
                atRunStart = true;
                goto cont;
            } else {
                // ----- 普通字符 -----
                if(!pairMode_guess) {
                    // pairMode_guess==0：无 begin/end 平衡，直接 appendChar。
                    char ok = appendChar(c);
                    atRunStart = false;
                    if((ok & 1) == 0)
                        return false;
                    goto cont;
                }
                // pairMode_guess!=0：begin/end 配对逻辑直接属于 render 主体。是否命中 begin
                // 必须在 appendChar 前求出，但 appendChar 仍先于所有状态修改。
                int beginIdx = scanCharIndex(_begin, c);
                if(!appendChar(c))
                    return false;
                if(beginIdx >= 0) {
                    if(atRunStart && pairDepth == 0) {
                        _lineStartX = _vertical ? _penY : _penX;
                        pairBeginChar = c;
                    }
                    ++pairDepth;
                } else {
                    int endIdx = scanCharIndex(_end, c);
                    if(endIdx >= 0 && --pairDepth == 0 &&
                       (int)_begin.GetLen() == (int)_end.GetLen() &&
                       scanCharIndex(_begin, pairBeginChar) == endIdx) {
                        _lineStartX = 0;
                    }
                }
                atRunStart = false;
                goto cont;
            }
        cont:
            // i 已由分支推进；若到末尾 → finishLine 收尾
            if(i >= len)
                return finishLine() & 1;
        }
        // 循环正常退出（i>=len 在 cont 已处理；兜底）
        return finishLine() & 1;
    }
};

} // namespace textrender

using textrender::TextRenderBase;

// ============================================================
// NCB 注册（模块 TextRender.dll；全部 objectMember/flags=0）。
// 使用 ncbind 既有 Factory(objthis 注入) 和 typed 成员复刻共同注册拓扑。
// 宏会在 ClassRegist 链上放入一个进程期静态 registrar；四个目标均未为它登记
// 静态析构。Unregist 的成员遍历是空操作，全局类成员删除及 class info 清理在 End 中完成。
// render 因 bespoke 封送保留 RAW：至少三个参数，第四项转换为 float 后继续传给
// renderImpl 的未使用 size 形参，第五项转换为 bool；其余 15 个 method 使用 typed
// invoker，让实例取得、参数错误码和返回值封送沿用 ncbind 实现。
// ============================================================
NCB_REGISTER_CLASS(TextRenderBase) {
    // 四文件 factory wrapper 都先处理单个 void 参数的“只声明、不装实例”特例；
    // 其余调用把 objthis 注入 callback，成功后安装到 native adaptor。安装失败时会
    // 立即析构并 delete callback 刚创建的对象。
    Factory(&TextRenderBase::factory);

    // ---- 16 methods（+ 上面 1 构造器 = 二进制 17 个 method-tag 成员）----
    NCB_METHOD(setOption);
    NCB_METHOD(setDefault);
    NCB_METHOD(setRenderSize);
    NCB_METHOD(clear);
    NCB_METHOD(resetFont);
    NCB_METHOD(resetStyle);
    NCB_METHOD(setFont);
    NCB_METHOD(setStyle);
    NCB_METHOD_RAW_CALLBACK(render, &Class::render, 0);
    NCB_METHOD(newline);
    NCB_METHOD(done);
    NCB_METHOD(onEval);
    NCB_METHOD(getKeyWait);
    NCB_METHOD(calcLineOffset);
    NCB_METHOD(calcShowCount);
    NCB_METHOD(getCharacters);

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
