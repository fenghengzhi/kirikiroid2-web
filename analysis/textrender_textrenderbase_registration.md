# textrender.dll — TextRenderBase NCB 注册分析

权威来源：libkrkr2.so（Android kirikiroid2）反编译。所有偏移/地址来自二进制，非本地推导。

## 0. 模块注册链 (TextRender.dll)

`TextRenderBase_moduleRegister` @ **0x42D01C**（原 sub_42D01C，静态初始化器）把一条 NCB 模块描述符链入全局模块链表 `xmmword_1AB8920+8`：

```
qword_1AB5170 = off_1A0B970          // NCB 类描述 vtable
qword_1AB5178 = L"TextRender.dll"    // 模块名 (UTF-16LE @ 区段内)
qword_1AB5180 = <prev head>          // next
qword_1AB5188 = L"TextRenderBase"    // 类名 (UTF-16LE @ 0x14c992e)
```

NCB 类描述 vtable `off_1A0B970`（字节已核实）：
```
[+0]  0x5242A8   (NCB 通用槽)
[+8]  TextRenderBase_ncb_enumMembers_create   @ 0x5A6650
[+16] 0  [+24] 0
[+32] 0x5242A8
[+40] TextRenderBase_ncb_enumMembers_create   @ 0x5A6650  (重复，create 路径)
```
> 注：原 prompt 给的 `[sub_5A6650, sub_5A67B4, 0, 0, sub_5242A8, sub_5A6A94]` 是 NCB enumerator 表的逻辑视图。实测 0x5A6650/0x5A67B4 是同一注册逻辑的两个变体：
> - `TextRenderBase_ncb_enumMembers_create` @ **0x5A6650**：`v7=1` → 仅构建类对象 (sub_5A6CFC)，注册期枚举。
> - `TextRenderBase_ncb_enumMembers_noCreate` @ **0x5A67B4**：`v7=0` → 真正向 TJS 引擎安装类 (调 vtable+96 即 RegisterMember)。
> 两者都调用核心注册函数 `TextRenderBase_ncb_registerMembers`。

## 1. NCB 成员注册机制（method vs property vs constant 的判定）

注册核心 = `TextRenderBase_ncb_registerMembers` @ **0x59BCCC**（原 sub_59BCCC，约 88KB 反编译）。

它**不**调用本仓库 SKILL 表里的 `ncb_addMember`(0x54242C) / `ncb_addConstant`(0x52FA58)。这是另一套 NCB 实现（NativeClassBinder proxy-object 风格）。每个成员是一个**手工构造的 proxy 对象**，再通过统一 helper 安装：

- **统一安装 helper** = `sub_5A6E64(classObj, L"<name>", proxyObj+32)`。
- **proxy 对象构造**：`operator new(size)` → `sub_9F6D2C(obj)`（ttstr/refcount 基类 ctor）→ 填字段。
- **种类判定 = proxy 对象的 type-tag `*(int*)(obj+16)`**（不是从名字猜）：
  - **tag == 1 → method**（`alloc 0x38/0x40`，name 标签 `L"Function"`，proxy vtable `off_1A0Bxxx`，**一个** Process 函数指针）。
  - **tag == 2 → property**（`alloc 0x50`，name 标签 `L"Property"`，proxy vtable `off_1A0Cxxx`，**两个**函数指针：getter@+48 / setter@+64）。
  - 无 constant（tag 3）成员。
- **method 便捷包装器**（内部 new + tag=1 + 安装）：
  - `sub_59D1B8(classObj, name, ProcessFn, flags)` → proxy vtable `off_1A0BAE8`
  - `sub_59EB78(classObj, name, ProcessFn, flags)` → proxy vtable `off_1A0BD28`
  - 两者断言 `!(flags&1 | fn)` 时报 `L"No method pointer."`。`flags` 全部传 `0`（无 staticMember 标志）。
- **property getter/setter 拆分（二进制确认，非名字猜测）**：proxy+48 = getter，proxy+64 = setter。**setter==0 → 只读 (RO)**。
  - 已逐字核实：`vertical` getter sub_5A0D74 读 `u8@+48`；setter sub_5A0D7C 写 `(this+48)=a2&1`。
  - 所有 `render*` / `maxScroll*` 属性 setter==0（`*(obj+64)=0; *(obj+72)=0; *(obj+56)=0`）→ **只读**。

> **flags 结论**：全体成员均为 **objectMember**（flags=0，无 staticMember/0x1）。该 NCB 风格的 staticMember 标志走 method 包装器的 `flags&1`，本类没有任何成员设置它。

## 2. 完整成员注册表

注册顺序即下表（17 方法 + 33 属性 = 50）。`Process Fn` = method 唯一处理函数；property 列出 getter/setter。RW = 读写，RO = 只读。

### 方法 (method, tag=1, flags=0/objectMember)

| 名称 | Process 函数 | proxy vtable | 备注 |
|---|---|---|---|
| setOption | 0x59D2AC | off_1A0BAE8 | 解析 option dict (following/leading/begin/end/vertical/kinsoku/ignore_* 等→byte 字段 48..59,112) |
| setDefault | 0x59DEA8 | off_1A0BAE8 | 解析默认样式 dict (face/bold/fontsize/big/small/rubysize/rubyoffset/color/shadow/...→字段 96..228) |
| setRenderSize | 0x59EB70 | off_1A0BC08 | (float w, float h) → 写 +240/+244，然后调 clear |
| clear | 0x59EC6C | off_1A0BD28 | 复位渲染状态，重建行列表/字符列表/keyWait 列表 |
| resetFont | 0x59EEE0 | off_1A0BD28 | 把当前样式复位为 default* 字段 |
| resetStyle | 0x59EFBC | off_1A0BD28 | (薄包装 resetFont 同族) |
| setFont | 0x59EFD8 | off_1A0BAE8 | 解析 font dict (face/bold/fontsize/rubysize/rubyoffset/color/shadow/edge/...) |
| setStyle | 0x59F7AC | off_1A0BAE8 | 解析 style dict (含 linespacing/pitch/linesize/align/valign) |
| render | 0x59FC28 | off_1A0BE48 | NCB 包装；解包 (str, x, y[, size, flag]) → 调真 render 0x5A228C |
| newline | 0x59FECC | off_1A0BD28 | 强制换行 |
| done | 0x59FEE4 | off_1A0BD28 | 终结布局：align 偏移、计算行 AABB、生成排序索引 |
| onEval | 0x5A0294 | off_1A0BF68 | result.type=0；调 TJS eval sub_8E3FA4(arg, this) |
| getKeyWait | 0x5A02DC | off_1A0C088 | 返回 TJS Array of {pos,time}，源自列表 +480/+488 (4B 元素) |
| calcLineOffset | 0x5A05FC | off_1A0C1A8 | (int lineIdx) → float；读行列表 +432 (stride 112, field+80) |
| calcShowCount | 0x5A0644 | off_1A0C2C8 | (int width) → 可显示字符数；倒序扫字符列表 +296 |
| getCharacters | 0x5A0694 | off_1A0C3E8 | (int start, int count) → TJS Array of per-char dict (text/x/y/cw/size/face/color/bold/italic/shadow/edge/shadowColor/shadowDiff/edgeColor/ruby/vertical/delay) |

### 属性 (property, tag=2, flags=0/objectMember)

| 名称 | RW/RO | getter(+48) | setter(+64) | 后备字段 | 类型 |
|---|---|---|---|---|---|
| vertical | RW | 0x5A0D74 | 0x5A0D7C | +48 | bool/u8 |
| timeScale | RW | 0x5A0D88 | 0x5A0D90 | +180 | float |
| fontScale | RW | 0x5A0D98 | 0x5A0DA0 | +184 | float |
| defaultFace | RW | 0x5A0DA8 | 0x5A0E0C | +96 (face index; 名→idx via resolveFaceIndex) | int + face table |
| defaultFontSize | RW | 0x5A0EAC | 0x5A0EB4 | +148 | float |
| defaultBigFontSize | RW | 0x5A0EBC | 0x5A0EC4 | +152 | float |
| defaultSmallFontSize | RW | 0x5A0ECC | 0x5A0ED4 | +156 | float |
| defaultLineSize | RW | 0x5A0EDC | 0x5A0EE4 | +176 | float |
| defaultLineSpacing | RW | 0x5A0EEC | 0x5A0EF4 | +168 | float |
| defaultPitch | RW | 0x5A0EFC | 0x5A0F04 | +172 | float |
| defaultAlign | RW | 0x5A0F0C | 0x5A0F14 | +100 | int |
| defaultValign | RW | 0x5A0F1C | 0x5A0F24 | +104 | int |
| defaultRubySize | RW | 0x5A0F2C | 0x5A0F34 | +160 | float |
| defaultRubyOffset | RW | 0x5A0F3C | 0x5A0F44 | +164 | float |
| defaultChColor | RW | 0x5A0F4C | 0x5A0F54 | +216 | u32 (color) |
| defaultShadow | RW | 0x5A0F5C | 0x5A0F64 | +67 | bool/u8 |
| defaultShadowColor | RW | 0x5A0F70 | 0x5A0F78 | +220 | u32 |
| defaultShadowDiff | RW | 0x5A0F80 | 0x5A0F88 | +224 | u32/int |
| defaultEdge | RW | 0x5A0F90 | 0x5A0F98 | +68 | bool/u8 |
| defaultEdgeColor | RW | 0x5A0FA4 | 0x5A0FAC | +228 | u32 |
| defaultBold | RW | 0x5A0FB4 | 0x5A0FBC | +66 | bool/u8 |
| defaultItalic | RW | 0x5A0FC8 | 0x5A0FD0 | +69 | bool/u8 |
| renderOver | RO | 0x5A0FDC | — | +60 | bool/u8 |
| renderLines | RO | 0x5A0FE4 | — | count(+432..+440)/112 | int |
| renderCount | RO | 0x5A1000 | — | +84 | int |
| renderDelay | RO | 0x5A1008 | — | +188 * +180 (delay*timeScale) | float |
| renderLeft | RO | 0x5A1018 | — | +248 | float |
| renderTop | RO | 0x5A1020 | — | +252 | float |
| renderRight | RO | 0x5A1028 | — | +256 | float |
| renderBottom | RO | 0x5A1030 | — | +260 | float |
| renderText | RO | 0x5A1038 | — | +40 (tTJSVariant) | string/variant |
| maxScrollOffset | RO | 0x5A1058 | — | vertical? +240-+248 : +244-+260 | float |
| maxScrollLine | RO | 0x5A1080 | — | 行列表扫描计算 | float |

> property proxy vtable `off_1A0Cxxx` 按 getter/setter 签名类型分桶（C508=RW-bool, C628=RW-float, C868=RW-int/color, C748=defaultFace 特例, C988/CAA8/CBC8/CCE8=各 RO 类型），仅用于 NCB 调用分发，移植不需复刻。

## 3. 构造 / 析构 / NCB 实例工厂

NCB 有**两层对象**，勿混淆：

### 3a. NCB 包装实例 (24B)
`TextRenderBase_ncb_createNativeInstance` @ **0x5A6A60**（NCB CreateNativeInstance 回调，由 buildClassObject@0x5A690C 装入 `v2[21]`）：
```c
obj = new(0x18);             // 24 字节
obj[0]  = off_1A0B990;       // NCB 实例 vtable
obj[8]  = 0;                 // → 指向真正的 TextRenderBase C++ 对象（惰性创建）
obj[16] = 0;                 // bool: 是否外部持有(不 delete)
```
NCB 实例 vtable `off_1A0B990`（已核实）= `[0x5242A8, TextRenderBase_ncb_nativeInstance_dtor@0x5A6A94, 0x5242B4]`。
`TextRenderBase_ncb_nativeInstance_dtor` @ **0x5A6A94**：若 `obj+8 != 0 && !obj+16`，调 `TextRenderBase_dtor(obj+8)` + `operator delete`。

### 3b. 真正的 TextRenderBase C++ 对象 (≥ 0x250 = 592 字节)
析构 = `TextRenderBase_dtor` @ **0x5A6B88**（揭示完整布局）。已识别字段：

| 偏移 | 类型 | 含义 |
|---|---|---|
| +8/+16/+24/+32/+40 | tTJSVariant* | following / leading / begin / end / renderText (各 refcount，dtor Release) |
| +48 | bool | vertical |
| +50/+51/+52/+56 | bool | ignore_color / ignore_size / ignore_delay / ignore_ruby (setOption) |
| +57/+58/+59 | bool | ignore_type / ignore_face / ignore_style |
| +60 | bool | renderOver |
| +62..+69 | bool×8 | bold/?/shadow(67)/edge(68)/italic(69) 当前样式 (62=bold cur,65=italic cur,63=shadow cur,64=edge cur,66=default bold,67=default shadow,68=default edge,69=default italic) |
| +72 | int | 当前 face index |
| +84 | int | renderCount |
| +88/+92/+96 | int | misc / cur face default index (96) |
| +100/+104 | int | defaultAlign / defaultValign |
| +108/+112 | int | / kinsoku_max(112) |
| +116..+184 | float×N | 当前样式 fontsize(116)/rubysize(128)/pitch(140)/linespacing-cur(136)/linesize-cur(144)/default fontsize(148)/big(152)/small(156)/rubysize(160)/rubyoffset(164)/linespacing(168)/pitch(172)/linesize(176)/timeScale(180)/fontScale(184)/renderDelay accum(188) |
| +192/+196 | float/int | render width / current x cursor |
| +200/+204/+208/+212 | u32 | cur chColor / shadowColor / shadowDiff / edgeColor |
| +216/+220/+224/+228 | u32 | default chColor / shadowColor / shadowDiff / edgeColor |
| +232/+236 | int | x-pen (horiz/vert) |
| +240/+244 | float | renderSize w / h |
| +248/+252/+256/+260 | float | renderLeft / renderTop / renderRight / renderBottom (bbox) |
| +280/+288 | int | misc state |
| +296 / +304 | ptr | **char 列表** std::vector<charItem*> begin/end (8B 元素=指针；charItem 内部 +8 x,+12 y,+16 cw,+20 size,+24 lineY,+28 color,+32 shadowColor,+36 edgeColor,+40 text ttstr,+41 bold,+42 italic,+43 shadow,+44 edge,+45 vertical,+48 shadowDiff,+52 lineIdx,+56/+64 ruby vec) |
| +320 | (内嵌 ttstr/hashset, dtor sub_5A1B24) | accumulated text buffer |
| +408/+416 | int | vert pen aux |
| +424 | float | line baseline aux |
| +432 / +440 | ptr | **行列表** std::vector<lineItem> begin/end (**stride 112**；lineItem +22..+25 bbox floats,+80 offset float,+88/+96 ruby sub-vec) |
| +456 / +464 / (+472) | ptr | **face 表** std::vector<tTJSVariant*> begin/end/cap (8B 元素；index→face ttstr) |
| +480 / +488 / +496 | ptr | **keyWait 列表** std::vector<int> begin/end/cap (4B 元素 = pos) |
| +504 / +512 | ptr | (内部 buffer，clear 时 reset) |
| +528 | tTJSVariant* | 当前 face ttstr (cache) |
| +536 / +544 / +552 / +560 / (+584 inline) | — | **face hash 表** (resolveFaceIndex 用：bucket array @536, count @544, list head @552, size @560, inline buckets @584；hash 链表节点 {next, ttstr*, idx}) |

无显式独立 ctor 被命名；真对象由 NCB method 首次调用时惰性 `new` 并存入 NCB 包装 obj+8。**基类**：tTJSVariant-style refcount base（sub_9F6D2C 初始化），无 KiriKiri Layer 继承——它是独立文本布局引擎，不是 Layer 子类。

## 4. 关键方法签名 / 行为骨架（够写桩）

所有 NCB Process 函数签名形如 `__int64 fn(nativeThis, tTJSVariant* result, int numparams, tTJSVariant** params, iTJSDispatch2* objthis)`（method 包装器风格）；property accessor 形如 `getter(nativeThis)→value` / `setter(nativeThis, value)`。

- **render** `TextRenderBase_ncb_render`@0x59FC28 → `TextRenderBase_render`@0x5A228C。numparams≥3：`(string text, int x, int y[, real size, bool flag])`；不足 3 返回 `0xFFFFFC14`(=TJS_E_BADPARAMCOUNT)。真 render 是一台 **KAG 风格转义标签状态机**：遍历 UTF-16 文本，`#`=color, `$`=emoji/face-run, `%<code>`=样式控制(%数字=size%,%b/i/s/e=bold/italic/shadow/edge,%f=face,%d/%a=delay,%C/L/R/S/B/D=对齐,%p=pitch,...), `\i/\w/\k/\n/\r/\t/\x`=特殊布局指令, `\n`/普通字符→逐字 append (sub_5A3880)。**不依赖 TVPGetFontRasterizer 这一层**——字形度量在 sub_5A3880 / face 表内部完成；本类只产出 char/line 几何 + 颜色，光栅化由调用方（绘制层）做。返回 bool→`sub_A0FEF0(result, ok)`。
- **setFont** @0x59EFD8：`(dict)` 1 参。对 dict 逐 key `PropGet`(vtable+32)：face/bold/fontsize/rubysize/rubyoffset/color/shadow/shadowcolor/shadowdiff/edge/edgecolor。face 经 `resolveFaceIndex`(0x5A14DC) 转 index 写 +72。改动后若有变化调 onStyleChanged(0x5A1F28)。返回 release(dict)。
- **setStyle** @0x59F7AC：与 setFont 同构，多读 linespacing/pitch/linesize/align/valign(→+168/+172/+176/+100/+104)。1 参 dict。
- **setDefault** @0x59DEA8：`(dict)` 1 参。读同名 key 写 **default* 字段** (face→+96, bold→+66, fontsize→+148, bigfontsize→+152, smallfontsize→+156, rubysize→+160, rubyoffset→+164, color→+216, shadow→+67, shadowcolor→+220, shadowdiff→+224, edge→+68, edgecolor→+228, linespacing→+168, pitch→+172, linesize→+176, align→+100, valign→+104)。
- **getCharacters** @0x5A0694：`(int start, int count)`；count==0→到末尾。返回新建 **TJS Array**（sub_9876D4 创建），每元素一个 dict 含 text/x/y/cw/size/face/color/bold/italic/shadow/edge/shadowColor/shadowDiff/edgeColor/ruby/vertical/delay。
- **calcLineOffset** @0x5A05FC：`(int lineIdx)`→**float**；越界返回 +260(bottom)，否则行列表 `+432 + 112*idx + 80`。

## 5. 本地 cpp 骨架实现要点

- 类名 `TextRenderBase`，注册到模块 `TextRender.dll`（NCB），全部成员 objectMember(flags=0)。
- 17 个 `NCB_METHOD*`：setOption,setDefault,setRenderSize,clear,resetFont,resetStyle,setFont,setStyle,render,newline,done,onEval,getKeyWait,calcLineOffset,calcShowCount,getCharacters。
- 22 个 `NCB_PROPERTY(RW)` + 11 个 `NCB_PROPERTY_RO`（render*/maxScroll* 全 RO，见表 §2）。
- numparams：render≥3；getCharacters=2；calcLineOffset=1；calcShowCount=1；setFont/setStyle/setDefault/setOption=1(dict)；setRenderSize=2；clear/resetFont/resetStyle/newline/done/onEval/getKeyWait=0~1。
- 后备字段按 §3b 表落到 C++ 成员（用语义字段名，勿硬凑 ARM64 偏移——偏移仅供本文档对照）。
- 桩阶段：方法体 STUB_WARN + 返回合理默认（render→true，getCharacters/getKeyWait→空 Array，calc*→0/字段值，属性读后备字段、写后备字段）。后续填 0x5A228C 状态机 + 字形度量。

## 6. IDB 已重命名符号（本次）

29 个函数已 rename + idb_save：moduleRegister/ncb_registerMembers/ncb_enumMembers_create/ncb_enumMembers_noCreate/ncb_buildClassObject/ncb_createNativeInstance/ncb_nativeInstance_dtor/dtor/render/ncb_render/setFont/setStyle/setDefault/setOption/setRenderSize/clear/resetFont/resetStyle/newline/done/onEval/getKeyWait/calcLineOffset/calcShowCount/getCharacters/resolveFaceIndex/appendChar_guess/finishLine_guess/onStyleChanged_guess（后三者 `_guess`，行为已读但命名未由二进制字符串确证）。
