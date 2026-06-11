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

注册顺序即下表（**1 构造器 + 16 方法** + 33 属性 = 50；旧"17 方法"计法把构造器混入方法，已更正——构造器也是 method-tag 成员，但语义是 TJS `new` 入口）。`Process Fn` = method 唯一处理函数；property 列出 getter/setter。RW = 读写，RO = 只读。

### 构造器 (method-tag=1, flags=0/objectMember)

| 名称 | Process 函数 | 备注 |
|---|---|---|
| (constructor) | TextRenderBase_ncb_constructor @0x59D160 | TJS `new` 时立即 `*slot = operator new(0x250); TextRenderBase_ctor(obj, objthis)`——**非惰性创建**。真 ctor = 0x5A111C（默认值群见 §3b-0） |

### 方法 (method, tag=1, flags=0/objectMember)

| 名称 | Process 函数 | proxy vtable | 备注 |
|---|---|---|---|
| setOption | 0x59D2AC | off_1A0BAE8 | 解析 option dict (following/leading/begin/end/vertical/kinsoku/ignore_* 等→byte 字段 48..59,112) |
| setDefault | 0x59DEA8 | off_1A0BAE8 | 解析默认样式 dict (face/bold/fontsize/big/small/rubysize/rubyoffset/color/shadow/...→字段 96..228) |
| setRenderSize | 0x59EB70 | off_1A0BC08 | (float w, float h) → 写 +240/+244，然后调 clear |
| clear | 0x59EC6C | off_1A0BD28 | 复位渲染状态，重建行列表/字符列表/keyWait 列表 |
| resetFont | 0x59EEE0 | off_1A0BD28 | face/bold/italic/fontsize 变化门控组复位+onStyleChanged；rubySize 门控；rubyOffset/shadow/edge+4色块无条件 |
| resetStyle | 0x59EFBC | off_1A0BD28 | **5 字段纯复位**(lineSpacing/pitch/lineSize/align/valign←default)；**不调 resetFont、无回调**(旧"薄包装 resetFont"已证伪) |
| setFont | 0x59EFD8 | off_1A0BAE8 | 解析 font dict (face/bold/fontsize/rubysize/rubyoffset/color/shadow/edge/...) |
| setStyle | 0x59F7AC | off_1A0BAE8 | 解析 style dict (含 linespacing/pitch/linesize/align/valign) |
| render | 0x59FC28 | off_1A0BE48 | NCB 包装；解包 (str, x, y[, size, flag]) → 调真 render 0x5A228C |
| newline | 0x59FECC | off_1A0BD28 | 强制换行 |
| done | 0x59FEE4 | off_1A0BD28 | 终结布局：align 偏移、计算行 AABB、生成排序索引 |
| onEval | 0x5A0294 | off_1A0BF68 | result.type=0；调 TJS eval sub_8E3FA4(arg, this) |
| getKeyWait | 0x5A02DC | off_1A0C088 | 返回 TJS Array of {pos,time}，源自列表 +480/+488 (8B 双 int 元素，见 §9.2) |
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
obj[8]  = 0;                 // → 指向真正的 TextRenderBase C++ 对象
                             //   （由构造器成员 0x59D160 在 TJS new 时立即填入，非惰性——旧"惰性创建"已证伪）
obj[16] = 0;                 // bool: 是否外部持有(不 delete)
```
NCB 实例 vtable `off_1A0B990`（已核实）= `[0x5242A8, TextRenderBase_ncb_nativeInstance_dtor@0x5A6A94, 0x5242B4]`。
`TextRenderBase_ncb_nativeInstance_dtor` @ **0x5A6A94**：若 `obj+8 != 0 && !obj+16`，调 `TextRenderBase_dtor(obj+8)` + `operator delete`。

### 3b. 真正的 TextRenderBase C++ 对象 (≥ 0x250 = 592 字节)
析构 = `TextRenderBase_dtor` @ **0x5A6B88**（揭示完整布局）。已识别字段：

| 偏移 | 类型 | 含义 |
|---|---|---|
| +8/+16/+24/+32/+40 | tTJSVariant* | following / leading / begin / end / renderText (各 refcount，dtor Release)。following..end 仅 setOption 写，仅接受 string/void（object/octet/int/real → TJSThrowVariantConvertError(String)） |
| +48 | bool | vertical (setOption) |
| +49..+59 | bool | **setOption byte 全表（批2 反编译逐 key 确认）**：49=word_break,50=ignore_color,51=ignore_size,52=ignore_delay,53=ignore_overx,54=ignore_over **且** ignore_overy（同址，后者覆盖前者），55=width_time_scale,56=ignore_ruby,57=ignore_type,58=ignore_face,59=ignore_style。全为 boolCoerce=(bool)variant |
| +60 | bool | renderOver |
| +62..+69 | bool×8 | bold/?/shadow(67)/edge(68)/italic(69) 当前样式 (62=bold cur,65=italic cur,63=shadow cur,64=edge cur,66=default bold,67=default shadow,68=default edge,69=default italic) |
| +72 | int | 当前 face index (setFont L"face" → resolveFaceIndex) |
| +76/+80 | int | **当前样式 align / valign（setStyle L"align"/L"valign" 写；≠ 默认 +100/+104）** |
| +84 | int | renderCount |
| +88/+92/+96 | int | misc / cur face default index (96 = setDefault L"face" → resolveFaceIndex) |
| +100/+104 | int | defaultAlign / defaultValign (setDefault) |
| +108/+112 | int | / kinsoku_max(112，setOption；**bool-coerce 写 DWORD，存 0/1 非整数值**) |
| +116..+184 | float×N | 当前样式 fontsize(116)/rubysize(128)/**rubyoffset-cur(132，setFont L"rubyoffset")**/linespacing-cur(136，setStyle)/pitch-cur(140，setStyle)/linesize-cur(144，setStyle)/default fontsize(148)/big(152)/small(156)/rubysize(160)/rubyoffset(164)/linespacing(168)/pitch(172)/linesize(176)/timeScale(180)/fontScale(184)/renderDelay accum(188) |
| +192/+196 | float | charDelayStep（每字 renderPos 步进；ctor=1.0f，render 入口=(float)a4，clear STUR 8B 连 +188 一起清零）/ lineStartX（行首 X） |
| +200/+204/+208/+212 | u32 | cur chColor / shadowColor / shadowDiff / edgeColor |
| +216/+220/+224/+228 | u32 | default chColor / shadowColor / shadowDiff / edgeColor |
| +232/+236 | int | x-pen (horiz/vert) |
| +240/+244 | float | renderSize w / h |
| +248/+252/+256/+260 | float | renderLeft / renderTop / renderRight / renderBottom (bbox) |
| +280/+288 | int | misc state |
| +296 / +304 | ptr | **char 列表** std::vector<charItem*> begin/end (8B 元素=指针；charItem = 80B POD，字段表见 §3b-1) |
| +320..+431 | **Line（112B 嵌套结构体）** | **pending 行缓冲，与 lineList 元素同型**（2026-06-11 确证，D2 修复依据）。+320..+399 = std::deque<charItem> 80B 控制块（行内禁则/kinsoku 重排缓冲，元素表见 §3b-2；+320 即 sub_5A4A7C 的 `v4=a1+320` deque this）；+400..+428 = 行 metric 内嵌字段（相对 +80..+108）：+400 lineBottom / +404 lineHeight / +408 bboxLeft / +412 bboxTop / +416 bboxRight / +420 bboxBottom / +424 wordBreakRun(int) / +428 prevWasSpace(bool)。同型三重证据：① Line::clear @0x5A1E68 取 +320 指针、deque 清空后零化相对 +80..+108（STR XZR,[X19,#0x50]+STP XZR,XZR,[X19,#0x58]+STUR XZR,[X19,#0x65]）；② finishLine push（0x5a3758/0x5a3768）与扩容 sub_5A43E8（0x5a44a8/0x5a44b0）都拷 a2+80/a2+93 两 OWORD（=+80..+108 全范围）入 lineItem+80/+93；③ Line dtor @0x5A1B24 同为 pending 行与 lineList 元素析构 |
| +432 / +440 | ptr | **行列表** std::vector<Line> begin/end (**stride 112**；元素与 pending Line(+320) 同型：+0..+79 嵌套 deque<charItem>、+80..+108 metric 含 +104 wordBreakRun/+108 prevWasSpace；done 读 bbox float[22..25]、calcLineOffset 读 +80) |
| +456 / +464 / (+472) | ptr | **face 表** std::vector<tTJSVariant*> begin/end/cap (8B 元素；index→face ttstr) |
| +480 / +488 / +496 | ptr | **keyWait 列表** std::vector begin/end/cap (8B 双 int 元素 {index,time}，见 §9.2；纠正旧 "4B 元素=pos") |
| +504 / +512 | ptr | (内部 buffer，clear 时 reset) |
| +528 | tTJSVariant* | 当前 face ttstr (cache) |
| +536 / +544 / +552 / +560 / (+584 inline) | — | **face hash 表** (resolveFaceIndex 用：bucket array @536, count @544, list head @552, size @560, inline buckets @584；hash 链表节点 {next, ttstr*, idx}) |

**真 ctor = `TextRenderBase_ctor` @0x5A111C**（旧"无显式独立 ctor/惰性创建"已证伪——构造器成员 `TextRenderBase_ncb_constructor` @0x59D160 在 TJS `new` 时立即 `operator new(0x250)` + ctor 并存入 NCB 包装 obj+8）。默认值群见 §3b-0。**基类**：tTJSVariant-style refcount base（sub_9F6D2C 初始化——注：0x5A111C 首句 `+0=objthis` 直接覆写，回指 dispatch），无 KiriKiri Layer 继承——它是独立文本布局引擎，不是 Layer 子类。

### 3b-0. ctor 默认值群（TextRenderBase_ctor @0x5A111C，C1 偏差修复依据）

| 偏移/字段 | ctor 默认值 | 二进制证据 |
|---|---|---|
| +0 | = objthis（dispatch 回指） | `*(QWORD*)a1 = a2`（平台边界：本地 ncbInstanceAdaptor 不持有） |
| +8 following | 内置禁则集（68 码点）`%),:;]}。，、．：；゛゜ヽヾゝゞ々’”）〕］｝〉》」』】°′″℃￠％‰<U+3000>!.?・？！ーぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ` | ttstr_createFromWide(0x14C9DF8) |
| +16 leading | 内置禁则集（19 码点）`\$([{‘“（〔［｛〈《「『【￥＄￡` | ttstr_createFromWide(0x14C9E82) |
| +24 begin | 开括平衡集（10 码点）`「『（‘“〔［｛〈《` | ttstr_createFromWide(0x14C9EAA) |
| +32 end | 闭括平衡集（10 码点）`」』）’”〕］｝〉》` | ttstr_createFromWide(0x14C9EC0) |
| +40 renderText | null | `*(QWORD*)(a1+40)=0` |
| +48/+49 | vertical=0、word_break=**1** | `WORD+48 = 0x0100` |
| +50..+59 | ignore* / width_time_scale 全 0 | `QWORD+50=0`、`WORD+58=0` |
| +61 | 0（未识别 byte） | `BYTE+61=0` |
| +66..+69 | defaultBold=0、defaultShadow=**1**、defaultEdge=0、defaultItalic=0 | `BYTE+66=0`、`WORD+67=1`、`BYTE+69=0` |
| +100/+104 | defaultAlign=**-1**、defaultValign=**-1** | `QWORD+100 = -1` |
| +112 kinsoku_max | **1** | `QWORD+112 = 0xBF80000000000001` 低 32 位 |
| +116 curFontSize | **-1.0f**（脏哨兵，保证首次 resetFont 组复位触发） | 同上高 32 位 = 0xBF800000 |
| +128 curRubySize | **-1.0f**（脏哨兵） | `DWORD+128 = 0xBF800000` |
| +148/+152 | defaultFontSize=24、defaultBigFontSize=**48** | `QWORD+148 = 0x4240000041C00000` |
| +156..+168 | defaultSmall=**12**、defaultRubySize=**10**、defaultRubyOffset=**-2**、defaultLineSpacing=**6** | `OWORD+156 = xmmword_14C95D0 = (12,10,-2,6)f` |
| +172..+184 | defaultPitch=0、defaultLineSize=**24**、timeScale=1、fontScale=1 | `OWORD+172 = xmmword_14C95E0 = (0,24,1,1)f` |
| +192 charDelayStep | **1.0f** | `DWORD+192 = 0x3F800000` |
| +216..+228 | defaultChColor=**0xFFFFFFFF**、defaultShadowColor=**0xFF000000**、defaultShadowDiff=**1**、defaultEdgeColor=**0xFF0080FF** | `OWORD+216 = xmmword_14C95F0` |
| +240/+244 | renderSize w/h = 0 | `QWORD+240 = 0` |
| +296..+399 / +432..+535 | 容器区清零（charList、pending deque 控制块、lineList、faceTable、keyWaitList、buffer、+528） | `memset(+296,0,0x68)` + dequeInit(+320) + `memset(+432,0,0x68)` |
| +536..+584 faceHash | 空表，bucket hint = `_M_next_bkt(0xA)` | 0x5a125c |
| +96 defaultFaceIndex | = resolveFaceIndex(L"normal")（intern "normal" 进 faceHash，恒 0） | 0x5a12a4..0x5a12b4 |
| 其余（+60、+62..+65、+72..+92、+108、+400..+431 等） | **不初始化**（依赖随后 setRenderSize→clear；本地零值初始化是安全加固，可保留） | ctor 无对应 store |

### 3b-1. charItem POD（80 字节）

证据：拷贝构造 `sub_5A4838`@0x5A4838（incref +0、OWORD 拷 +8/+24/+40、deep-copy +56 ruby vec）；落字构造 `TextRenderBase_appendChar_guess`@0x5A3880（栈上 v48..v64 80B 蓝图，`sub_5A4A7C(a1,&v48)`）；坐标回填 `sub_5A4A7C`@0x5A4A7C（LABEL_10 写 +8/+12/+24）；消费 `calcShowCount`@0x5A0644 读 +24。

| 偏移 | 类型 | 字段 | 证据 |
|---|---|---|---|
| +0 | ttstr*（refcounted） | text（单字符文本） | appendChar `v48=ttstr_createFromWide(&v66)`；sub_5A4838 +0 atomic incref |
| +8 | float | x（pen 横坐标） | sub_5A4A7C LABEL_10 `*(float*)(a2+8)=*(a1+232)` |
| +12 | float | y（pen 纵坐标） | sub_5A4A7C LABEL_10 `*(float*)(a2+12)=v10`（含 +236-cw 竖排修正） |
| +16 | float | cw（字宽，onGetTextWidth 回调返回） | appendChar `v49=v23=sub_5A426C(...)` |
| +20 | float | size（有效字号 = fontScale×curFontSize） | appendChar `v50=v25=*(a1+184)**(a1+116)` |
| +24 | float | renderPos（落字时累积渲染位置 = +280 当前 pen） | sub_5A4A7C LABEL_10 `*(float*)(a2+24)=*(a1+280)`；calcShowCount 读它倒扫 |
| +28 | u32 | chColor | appendChar `v51=*(a1+200)` |
| +32 | u32 | shadowColor | appendChar `v52=*(a1+204)` |
| +36 | u32 | edgeColor | appendChar `v53=*(a1+212)` |
| +40 | u8 | graph（构造置 0；脚本面真名经 getCharacters dict 首字段 L"graph" @0x5a081c 确认，串 @0x14CA19A） | appendChar `v54=0` |
| +41 | u8 | bold | appendChar `v55=*(a1+62)` |
| +42 | u8 | italic | appendChar `v56=*(a1+65)` |
| +43 | u8 | shadow | appendChar `v57=*(a1+63)` |
| +44 | u8 | edge | appendChar `v58=*(a1+64)` |
| +45 | u8 | vertical | appendChar `v59=*(a1+48)` |
| +48 | u32 | shadowDiff | appendChar `v60=*(a1+208)` |
| +52 | int | faceIndex | appendChar `v61=*(a1+72)` |
| +56/+64/+72 | std::vector<rubyItem>（20B 元素） | ruby 子标注 | appendChar v62/v63/v64；sub_5A4838 深拷贝；元素 = {tTJSVariant*@+0, ptr@+8, int@+16}（sub_5A5374 _M_default_append 步进 20，incref +0、拷 +8/+16） |

> **纠正**：旧 §3b 注释（“+40 text ttstr,+41 bold...,+8 x,+16 cw,+24 lineY,+28 color”）字段错位（text 实际在 +0 不在 +40，缺 renderPos/edgeColor/shadowDiff/faceIndex 准确位置），已被上表替换。证据为 sub_5A4838/sub_5A3880/sub_5A4A7C 三处反编译交叉确认。
> char 列表（真对象 +296/+304）元素是 charItem* 指针（8B/elem），堆上 80B charItem 由落字路径 new。

### 3b-2. pending Line 的嵌套 std::deque<charItem>（真对象 +320，元素 80B；= Line::chars，Line 整体见 §3b 表 +320..+431 行）

证据：`sub_5A4A7C`@0x5A4A7C（kinsoku 重排主体，`v4=a1+320`）；node push `sub_5A57CC`@0x5A57CC（`operator new(0x1E0)`=480B node=6×80B）；元素析构 `sub_5A5760`@0x5A5760（释放 +0 ttstr + +56 ruby vec + operator delete）；临时 deque init `sub_5A15B0`@0x5A15B0；deque 析构 `sub_5A1B24`（TextRenderBase_dtor 对 +320 调用）。

- **元素 = 80B charItem**（同 §3b-1，复用同一 POD）。
- **node block = 480 字节 = 6 elem/node**（`+480`/`*6`/`+400=480-80` 反复出现）。
- libstdc++ `std::deque` 标准 8-指针控制块映射（真对象绝对偏移）：

| 偏移 | deque 成员 | 含义 |
|---|---|---|
| +320 | `_M_impl._M_map`? | sub_5A15B0 `a1[0]=v6=new(8*mapsize)`（map 指针数组基址）→ 真对象 +320 |
| +328 | `_M_map_size` | sub_5A15B0 `a1[1]=v3=max(elems/6+3,8)` |
| +336 | `_M_start._M_cur` | sub_5A15B0 `a1[2]=v11=*v8`（start node 首字节） |
| +344 | `_M_start._M_first` | sub_5A15B0 `a1[3]=v11` |
| +352 | `_M_start._M_last` | sub_5A15B0 `a1[4]=v11+480` |
| +360 | `_M_start._M_node` | sub_5A15B0 `a1[5]=v8`（map 中 start 槽指针） |
| +368 | `_M_finish._M_cur` | sub_5A15B0 `a1[6]=v12+80*(n%6)`；落字 push `*(a1+368)+=80` |
| +376 | `_M_finish._M_first` | sub_5A15B0 `a1[7]=v12` |
| +384 | `_M_finish._M_last` | sub_5A15B0 `a1[8]=v12+480` |
| +392 | `_M_finish._M_node` | sub_5A57CC `a1[9]=v5`（map 中 finish 槽指针） |

> size 计算（sub_5A4A7C 反复出现，`-6` 项为 6 elem/node 的边界修正）：
> `size = (finish.cur-finish.first)/80 + 6*(finish.node-map)/8 - 6*(start.first-?)/80 - 6`。这是 libstdc++ deque `size()` 对 80B 元素、6/node 的内联展开，**不是源码 token**（遵字节布局复刻工作法：判源码拓扑看构造点 push/pop，不看消费循环上界）。源码层即 `std::deque<charItem>`。
> 用途：sub_5A4A7C 把因禁则（kinsoku，行首/行尾不可出现的标点）需回退到下一行的尾部字符，从主 char 列表暂存到此 pending deque，换行后再 `sub_5A4A7C(a1, pendingElem)` 重新落字（函数尾部 LABEL_107 的 `while(v79!=v76)` 自递归 drain）。

## 4. 关键方法签名 / 行为骨架（够写桩）

所有 NCB Process 函数签名形如 `__int64 fn(nativeThis, tTJSVariant* result, int numparams, tTJSVariant** params, iTJSDispatch2* objthis)`（method 包装器风格）；property accessor 形如 `getter(nativeThis)→value` / `setter(nativeThis, value)`。

- **render** `TextRenderBase_ncb_render`@0x59FC28 → `TextRenderBase_render`@0x5A228C。numparams≥3：`(string text, int x, int y[, real size, bool flag])`；不足 3 返回 `0xFFFFFC14`(=TJS_E_BADPARAMCOUNT)。真 render 是一台 **KAG 风格转义标签状态机**：遍历 UTF-16 文本，`#`=color, `$`=emoji/face-run, `%<code>`=样式控制(%数字=size%,%b/i/s/e=bold/italic/shadow/edge,%f=face,%d/%a=delay,%C/L/R/S/B/D=对齐,%p=pitch,...), `\i/\w/\k/\n/\r/\t/\x`=特殊布局指令, `\n`/普通字符→逐字 append (sub_5A3880)。**不依赖 TVPGetFontRasterizer 这一层**——字形度量在 sub_5A3880 / face 表内部完成；本类只产出 char/line 几何 + 颜色，光栅化由调用方（绘制层）做。返回 bool→`sub_A0FEF0(result, ok)`。
- **dict 解析的 3 套值强制转换（批2 反编译实证）**：每 key 用 `PropGet(TJS_MEMBERMUSTEXIST=0x400=1024, L"key", 0, &v, dict)`（= vtable+32），返回值 `& 0x80000000 != 0`（key 不存在）→ 跳过该 key。type tag = tTJSVariantType(Void0/Object1/String2/Octet3/Integer4/Real5)。
  - **boolCoerce**：switch type，case1/3/4→`qword==0`、case2→`sub_A13294(str)==0`、case5→`real==0.0`、default(void)→false；写 `!isZero`。**等价 `(bool)variant`（operator bool）**——object 不抛错（取 ptr!=0）。
  - **intCoerce**：case1(object)/case3(octet)→`sub_A0E48C(v,4)`（**= TJSThrowVariantConvertError，__noreturn 抛错**）、case2(string)→`sub_A13294`、case4→raw int、case5→(int)real、default(void)→0。**等价 `variant.AsInteger()`**。
  - **realCoerce**：case1/3→throw、case2→`sub_A133A8`、case4→(double)int、case5→raw real、default→0.0。**等价 `variant.AsReal()`**。
- **setOption** @0x59D2AC：`(dict)` 1 参。key 顺序：following,leading,begin,end(string/void→ttstr，其它抛错，+8/+16/+24/+32)，vertical(+48),kinsoku_max(+112 DWORD,boolCoerce→0/1),word_break(+49),ignore_color(+50),ignore_size(+51),ignore_delay(+52),ignore_over(+54),ignore_overy(+54 覆盖),ignore_overx(+53),width_time_scale(+55),ignore_ruby(+56),ignore_type(+57),ignore_face(+58),ignore_style(+59)。byte 全 boolCoerce。
- **setFont** @0x59EFD8：`(dict)` 1 参。key 顺序：face(resolveFaceIndex→+72，idx 变则 changed),bold(+62，变则 changed),fontsize(+116，`<0||!=` 脏哨兵变则 changed),rubysize(+128，`<0||!=` 但**不置 changed**),rubyoffset(+132，无条件),color(+200),shadow(+63),shadowcolor(+204),shadowdiff(+208),edge(+64),edgecolor(+212)。`changed` 仅由 face/bold/fontsize 触发；末尾 `if(changed) onStyleChanged(0x5A1F28)`。
- **setStyle** @0x59F7AC：**NON setFont-isomorphic**（旧"与 setFont 同构"描述错误，已纠正）。`(dict)` 1 参，只读：linespacing(+136),pitch(+140),linesize(+144，缺失则回退读 fontsize key),align(+76 当前样式),valign(+80 当前样式)。**不读 font 系键、不调 onStyleChanged**。注意 align/valign 写当前样式 +76/+80，≠ setDefault 的默认 +100/+104。
- **setDefault** @0x59DEA8：`(dict)` 1 参。key 顺序：face(→resolveFaceIndex→+96 int),bold(+66),fontsize(+148 — **存在时**回填缺省 bigfontsize→+152/smallfontsize→+156/rubysize→+160 **仅当对应 key 缺失**；**缺失时**独立读 big/small/ruby，缺则保持 0.0),rubyoffset(+164),color(+216),shadow(+67),shadowcolor(+220),shadowdiff(+224),edge(+68),edgecolor(+228),linespacing(+168),pitch(+172),linesize(+176 — 缺失则回退读 fontsize key),align(+100),valign(+104)。
- **getCharacters** @0x5A0694：`(int start, int count)`；count==0→到末尾。返回新建 **TJS Array**（sub_9876D4 创建），每元素一个 dict 含 text/x/y/cw/size/face/color/bold/italic/shadow/edge/shadowColor/shadowDiff/edgeColor/ruby/vertical/delay。
- **calcLineOffset** @0x5A05FC：`(int lineIdx)`→**float**；越界返回 +260(bottom)，否则行列表 `+432 + 112*idx + 80`。

## 5. 本地 cpp 骨架实现要点

- 类名 `TextRenderBase`，注册到模块 `TextRender.dll`（NCB），全部成员 objectMember(flags=0)。
- 1 个 `NCB_CONSTRUCTOR(())`（对应二进制构造器成员 0x59D160，TJS new 时立即建真对象）+ 16 个 `NCB_METHOD*`：setOption,setDefault,setRenderSize,clear,resetFont,resetStyle,setFont,setStyle,render,newline,done,onEval,getKeyWait,calcLineOffset,calcShowCount,getCharacters。
- 默认构造函数复刻 0x5A111C 默认值群（§3b-0）：标量用字段初始化器、4 禁则集 + faceHash(10) + resolveFaceIndex(L"normal") 在 ctor 体。
- 22 个 `NCB_PROPERTY(RW)` + 11 个 `NCB_PROPERTY_RO`（render*/maxScroll* 全 RO，见表 §2）。
- numparams：render≥3；getCharacters=2；calcLineOffset=1；calcShowCount=1；setFont/setStyle/setDefault/setOption=1(dict)；setRenderSize=2；clear/resetFont/resetStyle/newline/done/onEval/getKeyWait=0~1。
- 后备字段按 §3b 表落到 C++ 成员（用语义字段名，勿硬凑 ARM64 偏移——偏移仅供本文档对照）。
- 桩阶段：方法体 STUB_WARN + 返回合理默认（render→true，getCharacters/getKeyWait→空 Array，calc*→0/字段值，属性读后备字段、写后备字段）。后续填 0x5A228C 状态机 + 字形度量。

## 6. IDB 已重命名符号（本次）

29 个函数已 rename + idb_save：moduleRegister/ncb_registerMembers/ncb_enumMembers_create/ncb_enumMembers_noCreate/ncb_buildClassObject/ncb_createNativeInstance/ncb_nativeInstance_dtor/dtor/render/ncb_render/setFont/setStyle/setDefault/setOption/setRenderSize/clear/resetFont/resetStyle/newline/done/onEval/getKeyWait/calcLineOffset/calcShowCount/getCharacters/resolveFaceIndex/appendChar/finishLine/onStyleChanged_guess。
> 第三轮更新：appendChar(0x5A3880)/finishLine(0x5A34B8) 已**去 _guess**（render/newline/done 调用站点确证，见 §6 第三轮 + §7）。onStyleChanged_guess 仍保留 _guess（命名未由二进制字符串确证）。

第二轮（批0+批1 地基分析）新增 8 个 rename + idb_save：
- `TextRenderBase_charItem_copy` @0x5A4838（charItem 80B 拷贝构造，揭示 POD 字段）
- `TextRenderBase_pendingDeque_pushNode` @0x5A57CC（pending deque 追加 480B node = 6×80B）
- `TextRenderBase_charItem_destroy` @0x5A5760（charItem 析构：释 +0 ttstr + +56 ruby vec）
- `TextRenderBase_pendingDeque_init` @0x5A15B0（std::deque<charItem> 初始化，8 控制指针）
- `TextRenderBase_faceHash_find` @0x5A172C（face hash 桶链查找，resolveFaceIndex helper）
- `TextRenderBase_faceHash_intern` @0x5A181C（face hash intern，new 0x20 节点 {next,ttstr*,idx}）
- `TextRenderBase_appendChar_kinsoku` @0x5A4A7C（落字 + 禁则重排主体，pending deque this=a1+320）
- `TextRenderBase_rubyVec_defaultAppend` @0x5A5374（ruby 子 vector 的 _M_default_append，20B elem）

第三轮（批3 落字/行布局层）新增/去 _guess + idb_save：
- `TextRenderBase_finishLine` @0x5A34B8（去 _guess——newline@0x59FECC、done@0x59FEE4、render@0x5A228C `\n`/文本末 三处均调它做行结束，命名已确证）
- `TextRenderBase_appendChar` @0x5A3880（去 _guess——render@0x5A228C 0x5a28e0/0x5a2950 落字调它，已确证）
- `TextRenderBase_measureTextWidth` @0x5A426C（字宽度量回调，FuncCall L"onGetTextWidth"）
- `TextRenderBase_lineItem_copyFromPending` @0x5A4588（lineItem 拷贝构造：pending deque→lineItem 嵌套 deque + metric）
- `TextRenderBase_deque_copyElems` @0x5A46C0（deque<charItem> 逐元素拷贝）
- `TextRenderBase_pendingLine_clear` @0x5A1E68（**2026-06-11 由 pendingDeque_clear 改名**：实为 Line::clear——deque 清空（释元素保控制块）+ 零化相对 +80..+108 全部行 metric/wordBreakRun/prevWasSpace）
- `TextRenderBase_charList_insertionSortByRenderPos` @0x5A5C34（done 末尾 charList 按 char+24 renderPos 排序）
- `TextRenderBase_deque_pushFrontNode` @0x5A5548（临时 deque 头部追加 node）
- `wcscmp_utf16` @0x9B1ED0（UTF-16 wcscmp，kinsoku word_break 空格检测用）

## 7. 批3：落字 / 行布局层（appendChar / 度量 / kinsoku / finishLine / newline / done）

权威：反编译 0x5A3880 / 0x5A426C / 0x5A4A7C / 0x5A34B8 / 0x59FECC / 0x59FEE4 / 0x59EC6C / 0x5A228C(render dispatch，批4 参考)。

### 7.1 度量回调契约（onGetTextWidth）—— 忠实移植，非平台边界
`TextRenderBase_measureTextWidth` @0x5A426C：`(*(*a1+16))(*a1, 0, L"onGetTextWidth", hintCache, &result, 2, [text(str), size(real)], *a1, ...)` = iTJSDispatch2::FuncCall。
- 参数：arg0 = 单字符文本（string），arg1 = size = fontScale×curFontSize（real，a3）。
- 返回按 result.type：case2(string)→sub_A133A8(AsReal)、case4(int)→(double)、case5(real)→raw、case1/3(object/octet)→sub_A0E48C 抛错、default(void/FuncCall 失败)→**0.0**。
- **关键：`*a1` = native this[0]**。二进制 NativeClassBinder 风格里 native this 本身就是 iTJSDispatch2（native+0 = vtable），FuncCall 回调脚本子类覆盖的 onGetTextWidth。本地走 ncbInstanceAdaptor，native(C++) 与 objthis(dispatch) 是两个对象 → 内部落字函数须显式接收 objthis（平台边界：技术原因是本地 NCB 不把脚本对象等同于 native 对象）。

### 7.2 appendChar @0x5A3880
1. push 字符(UTF-16)到内部累积 buffer（+504/+512，2B 元素 vector）。
2. 组合字符倒计数（0x5a3970..0x5a3984）：`v21 = *(a1+88) - 1; if(v21 >= 0){ *(a1+88) = v21; return 1; }` —— **先算后条件回写**，v21 为负不回写（+88 永不为负，非无条件 `--`）；buffer 长度 != 2（非单字符）→ return 0。
3. 恰好 1 字符且计数耗尽：度量 cw → 构造 charItem 蓝图（v48..v64，§3b-1）→ ruby 处理 → 清 buffer → `sub_5A4A7C` 落字。
- **ruby**（!vertical 且 +528 有 ruby 文本）：度量 ruby cw → push RubyItem{x=cw/2-rubyCw/2, y=-(rubySize·fontScale)-rubyOffset, span=rubySize·fontScale, text} → 释放 +528 → 更新 ruby bbox(+264 left/+268 top/+272 right)。无 ruby(horizontal) → 仅 +268=min(+268,penY)。vertical → 跳过 ruby 段。ruby bbox(+264/+268/+272) clear 不初始化、finishLine/done 不消费 → **dead-but-faithful**（消费者可能在批4 属性/render，标注）。

### 7.3 kinsoku 禁则 @0x5A4A7C（落字 + 行尾/行首禁则重排）
- **禁则字符集来自 setOption 脚本传入**（_following/+8、_leading/+16、_begin/+24、_end/+32），**非二进制内联常量**（无需 get_bytes）。查询用 ttstr_indexOf（=wcsstr 子串查找，空 haystack/needle→-1，等价 ttstr.IndexOf）。
- over 检测：vertical→renderSizeH(+244) vs penY+size，门控 _ignoreOverY(+54)；horizontal→renderSizeW(+240) vs penX+cw，门控 _ignoreOverX(+53)。命中（尺寸<=0 或未超 或 ignore）→ 直接落字（placeChar）。
- 换行重排（临时 deque 暂存回退字符）：
  - **!word_break**(+49)：把超过 _wordBreakRun(+424) 个的尾部 run 移到 tmp（--renderCount）。
  - **word_break 且 当前字 ∈ _following**：kinsoku 计数。_kinsokuUsed(+108) >= _kinsokuMax(+112) 用尽 → while pop 末字符到 tmp(--used/--renderCount，used<1 边界仅 pop leading 字符不减计数)；否则 ++_kinsokuUsed。
  - **word_break 且 当前字 ∉ _following**：行尾禁则。size>=3 且 倒数第2 ∉ leading 且 末字符 ∈ leading → pop 末字符。LABEL_94：size>=2 且 末字符 ∈ leading → 再 pop 一个。
  - LABEL_107：finishLine → 失败 return 0；成功正向 drain tmp 自递归 kinsoku → 落字 placeChar(!vertical)。
- placeChar(LABEL_10)：char.x=penX；char.y=horizontal?penY-size:penY；char.renderPos=renderPos(始终)，delayAccum=max(delayAccum,renderPos)（**char.renderPos 不取 max，仅 delayAccum 取**）；push 到 pending deque；!word_break→更新 wordbreak state(+428 prevSpace/+424 run)；推进 renderPos（width_time_scale?rate·cw/size:charDelayStep+192）+pen（penX+=cw;penX=pitch+penX / 竖排 penY+=size;penY=pitch+penY）+行 bbox(+416/+420)。

### 7.4 finishLine @0x5A34B8（去 _guess）
横排(!vertical)：① 行高 v11=max(行内 max char.size, curLineSize+144) ② over 检测(renderSizeH < v11+penY → renderOver=1，!ignore_over→Line::clear(0x5A1E68，deque+metric 全清) return 0) ③ align 偏移 v14(curAlign+76：1→right=W-penX，0→center=(W-penX)/2，其它→0) ④ align 缩进：pending 非空时按全角空格(0x3000)宽填充行首到 renderText ⑤ 落字到行：每 char.x+=v14、char.y=v11+char.y、拼接 char.text 到 renderText ⑥ 写 pending Line metric(+400/+404/+408/+416/+420，即 Line 相对 +80..+100)、penY+=v11 ⑦ push = 整个 pending Line 拷入 lineList（sub_5A4588 拷 deque + 0x5a3758/0x5a3768 两 OWORD 拷 +80..+108 **全范围含 +424 wordBreakRun/+428 prevWasSpace**；扩容 sub_5A43E8 同。源码层 = lineList.push_back(pendingLine)） ⑧ Line::clear(0x5a378c)——metric 全零化，随后 0x5a37c8 读 +408 做 `if(bboxLeft>penX)` 时左操作数恒 0 ⑨ renderText+=L"\n" ⑩ penX=lineStartX(+196)、penY+=linespacing(+136)。
竖排：直接到 LABEL_56。LABEL_56：释放 +528 ruby、_kinsokuUsed=0、清 buffer、return 1。
内联常量：L"　"(0x3000 全角空格，word_14CA1EE)、L"\n"(0x000A)。

### 7.5 newline @0x59FECC / done @0x59FEE4
- newline：pending 非空(+368!=+336) → finishLine。
- done：① pending 非空→finishLine ② 遍历 lineList 算全局 bbox(读 lineItem bbox float[22..25]) ③ valign(+80) 偏移(1→W底对齐 H-bottom，0→center (H-bottom)/2)加到每 char.y、调整全局 top/bottom ④ charList 从各行 deque 铺 charItem 指针 ⑤ keyWait 列表 index→charList[idx].renderPos(float bits 存 int 槽) ⑥ charList 按 char.renderPos(+24) 排序(introsort sub_5A59E8 + insertion sort sub_5A5C34)。

### 7.6 clear @0x59EC6C（批3 重写，原为简化桩）
Line::clear(0x5A1E68，pending deque 清空 + +400..+428 metric 全零化)；**随后** vertical→penX/left/right/pendingLine.bboxL/R(+408/+416)=renderSizeW，horizontal→penX/left/right=0（bbox 不写、保持 Line::clear 后的 0）；penY/top/bottom/+108=0；释放 +528；buffer/+196/+88=0；resetFont；cur 样式从 default 复位(+140/+144/+136/+76/+80←+172/+176/+168/+100/+104)；lineList/charList/keyWait 清空；`STR XZR,[#0x118]` 8B=+280 renderPos **及 +284 renderPosSnap** 清零；+288=0；`STUR XZR,[#0xBC]` 8B=+188 renderDelayAccum **及 +192 charDelayStep** 清零（D3 修复依据，证伪旧注释"clear 不重置 +192"）；+92/+84/+60=0；释放 renderText；face 表压缩(取旧 default face name→清 hash+table→重 intern→写回 +96)。

### 7.7 批4（render 状态机 0x5A228C）前置就绪情况
render(objthis=a1, str, x, y, flag) 是 KAG 转义状态机，逐 UTF-16 遍历：`#`=color(sub_5A3CE4 取到 `;`)、`$`=emoji/face-run(sub_5A4148 + 逐字 sub_5A3880)、`%<code>`=样式(0-9/;=size%、b/i/s/e=bold/italic/shadow/edge、f=face、d/a=delay、C/L/R/S/B/D=align、p=pitch、t/l/w=度量)、`\i`=lineStartX=pen、`\k`=keyWait push renderCount、`\n`=finishLine、`\r`=lineStartX=0、`\t`=appendChar(9)、`\w`=pen+=fontsize、`\x`=结束、`\n`(0x0A)=finishLine、普通字符→sub_5A3880(begin/end 集成对 push/pop 平衡逻辑)。**被调用方 appendChar/finishLine/done 本批已就位**；render 本体需新增字段 +24/+32(begin/end 平衡集，已在 §3b)、render 入口复位(+440=+432 清行/+188=0/+280=0/+192=(float)flag)。批4 可直接复用本批落字层。
- **当前 oracle-inert**：render 状态机未实现 → 落字层除 newline/done 两个 NCB 直接方法外无运行时驱动；无 textrender 单元测试/differential fixture（验证缺口，honest gap，不捏造物料）。构建通过 = 非回归守护。

## 8. 批4：render 状态机本体（NCB wrapper 0x59FC28 + 真 render 0x5A228C）

权威：反编译 0x59FC28 / 0x5A228C / 0x5A3CE4 / 0x5A3F18 / 0x5A4148 / 0x9B111C / 0x5A5874 / 0x5A1B24 + disasm 0x5A2C50/0x5A2D88（align cascade）+ get_bytes 0x14CA200（hex 表）/0x14CA1EE（全角空格）。

### 8.1 NCB wrapper TextRenderBase_ncb_render @0x59FC28
- `numparams(a2) < 3` → 返回 `4294966292`(=0xFFFFFC14=TJS_E_BADPARAMCOUNT)。
- `a2==3` → flag(v9)=0。`a2>=4` → param[3]=size **仅 real 类型强制后丢弃**（switch @0x59fcb0：object(1)/octet(3)→sub_A0E48C(,5u)=ConvertError(Real)、string(2)→sub_A133A8 解析、int/real/void→无操作 = `AsReal()` 丢弃），不传给 render；`a2<5` → flag=0；`a2>=5` → flag=boolCoerce(param[4])。
- text=param[0]（sub_A0BAF4 拷贝），x=intCoerce(param[1])，y=intCoerce(param[2])。
- 调 `TextRenderBase_render(objthis, &text, x, y, flag)` → bool → sub_A0FEF0(result, ok&1)。

### 8.2 真 render TextRenderBase_render @0x5A228C 参数语义（BLOCKING：禁参数名推导，全部反编译确证）
- **a5=flag**：bit0==0 → 入口复位（清行列表 +432..+440 逐项 sub_5A1B24 / +188 renderDelayAccum=0 / 清 keyWaitList +480..+488）；bit0==1 → 续接（不复位）。无条件：+280 renderPos=0、+192 charDelayStep=(float)a4(=y)。
- **a3=x**：**begin/end 平衡集启用标志**（`if(!a3)` → 普通 append 无平衡；`if(a3)` → begin(+24)/end(+32) 平衡逻辑）。**不是 X 坐标**。
- **a4=y**：**每字 renderPos 步进初值**（+192=charDelayStep）。**不是 Y 坐标**。
- v13 = curFontSize(+116) 快照（给 `\w` 用）；v136=cursor=0；v14=text.length（IDA 误标 operator delete）；v15=text c_str。

### 8.3 转义标签全表（字符 → 行为 → 字段写入，证据地址）

| 首字符 | 行为 | 字段写入 / 门控 | 证据 |
|---|---|---|---|
| `#` | hex 颜色（scanTagUntil 到 ';'） | !ignore_color(+50)；空内容→+200=+216；`0x`前缀跳过；hex decode→+200=acc\|0xFF000000；无效 digit 即终止 | 0x5a25ac..0x5a256c；hex 表 qword_14CA200 |
| `$` | eval/face-run（scanTagUntil 到 ';' → onEval → 返回串逐字 appendChar） | 逐字 sub_5A3880；失败→render 中断 | 0x5a27c0；evalDollarTag sub_5A4148 |
| `%数字` | size 百分比（scanDigits） | !ignore_size(+51)；val>0→+116=(val/100)*+148 else +116=+148；变→+116+onStyleChanged | 0x5a2688..0x5a2b10 |
| `%;` | 恢复默认字号 | !ignore_size；+116=+148 + onStyleChanged | 0x5a2ef4 |
| `%B` | bigfontsize | !ignore_size；+116=+152 + onStyleChanged | LABEL_228 0x5a2da0 |
| `%S` | smallfontsize | !ignore_size(+51)；+116=+156 + onStyleChanged | case 'S' 0x5a2f10 |
| `%C/%L/%R` | 对齐 + bigfontsize | !ignore_style(+59) 门控；**+76 cascade 写 0→1→-1 / 1→-1 / -1，三者最终均 -1**（fall-through 5a2d90→5a2d98 实证）；随后落 LABEL_228 bigfontsize | disasm 0x5a2c50/0x5a2d7c/0x5a2d88 |
| `%b` | bold（码后 0/1/其它） | +57(ignore_type) 门控；0→0,1→1,其它→+66；变→+62+onStyleChanged | 0x5a2fe0 |
| `%i` | italic | +57 门控；其它→+69；变→+65+onStyleChanged | 0x5a2ccc |
| `%e` | edge（**无 onStyleChanged**） | +57 门控；其它→+68；变→+64 | 0x5a2fa8 |
| `%s` | shadow（**无 onStyleChanged**） | +57 门控；其它→+67；变→+63 | 0x5a3018 |
| `%f` | face（scanTagUntil ';'） | !ignore_face(+58)；空→idx=+96 else resolveFaceIndex；变→+72+onStyleChanged | 0x5a2b4c |
| `%d` | delay 百分比（scanTagUntil ';'） | !ignore_delay(+52)；+192=(val/100)*a4(=y) | 0x5a2d14 |
| `%a` | absolute delay | !ignore_delay；+192=(float)val（空→a4） | 0x5a2e40 |
| `%p` | pitch | !ignore_style(+59)；+140=val（空→+172） | 0x5a2dd0 |
| `%l/%t/%w` | parseInt10 仅校验（无字段写） | !ignore_delay(+52) | 0x5a2bf4/0x5a3060/0x5a2c74 |
| `%r` | resetFont | sub_59EEE0 | 0x5a2c48 |
| `%D` | 码后`$`→嵌套 eval-delay(!ignore_delay 校验)；否则当 delay 标签(parseInt10 校验无写) | +52 | 0x5a2f34 |
| `%其它` | 消费标签到 ';'（无字段写） | — | default 0x5a2eb8 |
| `\i` | lineStartX=pen | +196 = (vertical?+236:+232) | 0x5a2748 |
| `\k` | keyWait push renderCount | +480.push_back(+84)；sub_5A5874 | 0x5a2a94 |
| `\n`(反斜杠) | finishLine | 失败→中断 | 0x5a2ac0 |
| `\r` | lineStartX=0 | +196=0 | 0x5a2ac8 |
| `\t` | appendChar(9) | 失败→render 中断(return false，**不调 finishLine**) | 0x5a2ad8 |
| `\w` | pen+=curFontSizeSnap(v13) | +236/+232 += v13 | 0x5a2a84 |
| `\x` | goto LABEL_319（仅 v17=0，**不终止**） | — | 0x5a273c case 'x' |
| `\其它` | 忽略 | — | default |
| `&` | 消费标签到 ';'（无字段写） | goto LABEL_319 | 0x5a2770 |
| `[` | ruby 括号（scanTagUntil ']'） | !ignore_ruby(+56) 仅 refcount no-op，**无 +528 写**（本 build ruby-via-bracket 不写 curRubyText） | 0x5a2878/0x5a28b0 |
| 裸 0x0A | finishLine | 失败→中断；成功→v17=1 | 0x5a2624 |
| 普通(!a3) | appendChar | 失败→return false(不调 finishLine) | LABEL_64 0x5a28e0 |
| 普通(a3) | begin/end 平衡 appendChar | 见 8.4 | 0x5a2640.. |

### 8.4 begin/end 平衡集（a3!=0，default 分支）
- 字符 ∈ begin(+24)：appendChar 后，首个（v17 && depth(v133)==0）→ lineStartX(+196)=pen + 记起始 begin 字符(v132)；v17=0；++depth。
- 字符 ∈ end(+32) 且 `--depth==0` 且起始 begin 字符 v132 在 begin 集索引 == 当前 end 字符在 end 集索引（配对）→ lineStartX=0。
- 任何成功路径末尾 v17=0（LABEL_319）。appendChar 失败 → LABEL_325 return false。

### 8.5 helper 子函数
- `TextRenderBase_render_scanTagUntil` @0x5A3CE4：读到 delim（';'/']'）的子串 ttstr，cursor 推进到 delim 后。
- `TextRenderBase_render_scanDigits` @0x5A3F18：读连续数字 0-9 的子串，cursor 停在非数字。
- `TextRenderBase_render_evalDollarTag` @0x5A4148：native+0 上 FuncCall(L"onEval", content)（返回码不检查）→ 按返回 variant type 分发：octet/int/real（(unsigned)(type-3)<3 @0x5a41d8）与 object（type==1 @0x5a41e8）→ sub_A0E48C(,2u)=TJSThrowVariantConvertError(String)；string(2)→取值；void(0)→空串（平台边界：本地传 objthis）。
- `ttstr_parseInt10` @0x9B111C：UTF-16 串 → 十进制 int（跳 <=0x20 空白、可选 '-'、*10+digit）。≠ AsInteger（无 0x 前缀处理）。
- `TextRenderBase_keyWaitList_pushBack` @0x5A5874：std::vector<int>::push_back（keyWait 满时重分配）。
- `TextRenderBase_pendingLine_dtor` @0x5A1B24：Line 析构（pending 行与 lineList 元素**同型共用**——render/clear 清行列表逐项调用；2026-06-11 与 lineItem_destroy 确认为同一函数。函数体只触及 +0..+79 deque 控制块：Line 尾部 metric 全是 POD，~Line ≡ ~deque 成员析构，故 kinsoku 的 80B 裸临时 deque LABEL_113/0x5a5338 也复用同一 helper——不与同型结论冲突）。

### 8.6 平台边界 / 实现细节
- **native≠objthis**（NativeClassBinder native≡dispatch vs 本地 ncbInstanceAdaptor 双对象）：appendChar/finishLine/evalDollarTag/onStyleChanged 显式接收 objthis 以回调脚本 onGetTextWidth/onEval/onFontChange。技术原因见 §7.1。
- **_percentCursor**：非二进制字段。二进制 render 是单函数、cursor v136 是栈局部；本地把 % 分发拆成成员函数 renderPercentTag，需把推进后的 cursor 传回主循环。实现细节，不进数据契约。
- **%C/L/R align cascade=-1**：忠实复刻 fall-through，三者最终 curAlign=-1（编译器消去死写）。看似原版 quirk，但反编译/disasm 确证（5a2d90→5a2d98 直线无分支），照搬。**禁从 KAG 居中/左右直觉改写**。

### 8.7 验证缺口（honest gap）
render 状态机已忠实移植 + 构建/链接通过。无 textrender 单元测试 / differential fixture 驱动 render（脚本 onGetTextWidth 回调 + KAG escape 文本需鉴赏/对话运行时触发，SDL3 worker 输入无法 headless 自动化）。批5（getCharacters/getKeyWait/属性核对/集成审计）后可考虑端到端运行时验证。当前为非回归守护（构建通过 + logo/现有差分不受影响——textrender 不在 wasmtime guest 子集）。

### 8.8 IDB 重命名（本批）
6 个：scanTagUntil(0x5A3CE4)/scanDigits(0x5A3F18)/evalDollarTag(0x5A4148)/ttstr_parseInt10(0x9B111C)/keyWaitList_pushBack(0x5A5874)/lineItem_destroy(0x5A1B24)。ncb_render(0x59FC28)/render(0x5A228C) 此前已命名。

## 9. 批5：查询输出层 + 属性核对 + 集成审计（收尾）

权威：反编译 0x5A0694(getCharacters) / 0x5A02DC(getKeyWait) / 0x5A0294(onEval) /
0x5A5874(keyWaitList_pushBack) / 0x59FEE4(done keyWait 段) / 0x5A6240(ruby 子数组) /
0x5A6550(数组 append) / 0x5A2160(byte→int 字段) / 0x5A614C(float→real 字段) /
0x5A6020(int 字段) / 0x5A1080(maxScrollLine) / 0x5A1008/1058/0FE4(属性核对) /
0x5A0DA8/0E0C(defaultFace) + disasm 0x5A0360(getKeyWait LDRSW) + get_bytes 0x14CA1EE。

### 9.1 getCharacters @0x5A0694（getCharacters dict key 表）
签名 `(int start=a2, int count=a3)`。
- **count==0(!a3) → count = renderCount(+84) - start**（**纠正旧 §4 "到末尾"**——是 renderCount-start，非 charList 末尾）。
- charListCount=(+304-+296)>>3；clamp v13=(count+start<=charListCount)?count:(charListCount-start)；v13<1→空 Array。
- 建 Array(sub_9876D4)，遍历 v14=0..v13：charItem=charList[v14+start]（8B 指针元素）。
- **face 缓存**：上一 faceIndex(v15,初 -1)；charItem.faceIndex(+52) 变化时查 _faceTable[fi]
  （越界→空串 sub_A13878(&byte_1506A57)），缓存名 v30，每元素 face=v30。
- dict 字段（key UTF-16LE 经反编译确认无截断；类型经 helper 确认）：

| key | charItem 偏移 | 类型 | setter helper |
|---|---|---|---|
| graph | +40 | int | sub_5A2160(byte→int)；**dict 首字段** @0x5a081c，先于 text；key 串 @0x14CA19A UTF-16LE "graph" |
| text | +0 | string | sub_A0FE2C+vtable48 |
| x | +8 | real | sub_5A614C |
| y | +12 | real | sub_5A614C |
| cw | +16 | real | sub_5A614C |
| size | +20 | real | sub_5A614C |
| face | 缓存名 v30 | string | sub_A0FB64(v30)+vtable48 |
| color | +28 | int | sub_A0FB64(type4) |
| bold | +41 | int | sub_5A2160(byte→int) |
| italic | +42 | int | sub_5A2160 |
| shadow | +43 | int | sub_5A2160 |
| edge | +44 | int | sub_5A2160 |
| shadowColor | +32 | int | sub_A0FB64(type4) |
| shadowDiff | +48 | int | sub_5A6020(int) |
| edgeColor | +36 | int | sub_A0FB64(type4) |
| ruby | +56 vec | object(子Array) | **仅 +56!=+64**；sub_5A6240 |
| vertical | +45 | int | sub_5A2160 |
| delay | **+24(renderPos)** | real | sub_5A614C |

- dict 落入数组：sub_5A6550(arr, v14, dict) = PropSetByNum(index=v14)。
- **ruby 子 Array**（sub_5A6240@0x5A6240）：每 RubyItem(20B：text+0/x+8/y+12/**size=span+16**)→
  dict{text,x,y,size}，PropSetByNum 落入子 Array。getCharacters 的 ruby 字段 = 该子 Array。
  **这是 charItem.ruby 子标注 vector 的消费端**（与对象级 ruby bbox +264/+268/+272 无关，见 §9.5）。

### 9.2 getKeyWait @0x5A02DC（getKeyWait 解码）
- **keyWait 元素是 8B 双 int `{int index; int time;}`**（**纠正旧 §3b/memory "4B 元素=renderPos float bits"**）。
  证据：keyWaitList_pushBack@0x5A5874 用 (end-begin)>>3 计数 + operator new(8*n) + *(QWORD*)slot=*a2；
  done keyWait 段 v39 步进 2 ints；getKeyWait disasm 0x5a036c `LDRSW X24,[X8,X28]`（X28 +=8）读低 int。
- 数据流：\k push {renderCount,0}（低 int=renderCount，高 int=0）→ done 重写高 int=charList[index].renderPos bits
  （低 int 索引不动）→ **getKeyWait 读低 int(index)，pos=time=index（同值）**。
- **renderPos bits(高 int) 是 dead-for-getKeyWait**（done 算了但 getKeyWait 不读）——dead-but-faithful。
- getKeyWait 行为：建 Array(sub_9876D4)，每元素建 dict(sub_9C8440){pos=index(Integer), time=index(Integer)}，
  FuncCall L"add" 落入 Array。**纠正旧桩（返回空 Array）**。
- **variant objthis 槽均为 null**：add 的 arg variant {Object=dict, ObjThis=0}（0x5a042c v16=1 / 0x5a0430
  v15[0]=v12, v15[1]=0）；最终 result variant {Object=arr, ObjThis=0}（0x5a04c8/0x5a04cc *(a2+8)=0）。
  注意 getCharacters 的 result 为 (obj,obj)——两函数二进制自身不一致，各自照抄。
  onStyleChanged@0x5A1F28 的 onFontChange arg variant 同为 {dict, 0}（0x5a2050/0x5a2054 v12[1]=0）。

### 9.3 onEval @0x5A0294
`*(result+16)=0`（result.type=Void）；`return sub_8E3FA4(a2=expr, *a1=this dispatch, a3=result)`。
- sub_8E3FA4 = `sub_97FE40(engine, expr, result, context, 0,0)`，字符串 ref "../../src/core/base/ScriptMgnIntf.cpp"
  确证 = **TVPExecuteExpression(expr, context, result)**（ScriptMgnIntf eval 路径）。
- 本地：result.Clear() + TVPExecuteExpression(param[0], objthis, result)。**纠正旧桩（仅 result.Clear()）**。
- 平台边界：context 二进制用 native+0(=dispatch)，本地用 objthis（脚本子类对象，native≠objthis）。

### 9.4 属性公式核对结果（4 个 flagged + defaultFace 修正）
| 属性 | 地址 | 二进制公式 | 本地 | 结论 |
|---|---|---|---|---|
| renderDelay | 0x5A1008 | +188 * +180 | _renderDelayAccum*_timeScale | ✓ 已对齐 |
| maxScrollOffset | 0x5A1058 | vertical?(+240-+248):(+244-+260) | 同 | ✓ 已对齐 |
| renderLines | 0x5A0FE4 | (+440-+432)/112（magic div stride112） | _lineList.size() | ✓ 等价 |
| maxScrollLine | 0x5A1080 | 视口(竖排+240/横排+244)起自尾行向前减 lineHeight(+84) 统计可容纳行号 | **批5 实装**（原桩=0.0） | ✓ 修正 |
| **defaultFace** | 0x5A0DA8/0E0C | **INDEX-based**：get 查 _faceTable[+96]（越界→空串），set=resolveFaceIndex→+96 | **批5 修正**（删幻影 _defaultFace ttstr，改间接读写 +96） | ✓ 修正 |

maxScrollLine 算法（0x5A1080）：lineCount<1→0.0；v6=视口尺寸；自 lineList[count-1].lineHeight 向前
（i-=112B）`v6-=lineHeight; if(v6<0)break; --v4; if(count+v4<=0)return 1.0`；break 后 v4!=0→return count+v4，否则 0.0。

其余属性抽检（0x5A0EAC defaultFontSize=+148 / 0x5A0F4C defaultChColor=+216 / 0x5A1000 renderCount=+84 /
0x5A1018 renderLeft=+248 / 0x5A1038 renderText=+40）均与 §2 表/本地字段一致，无偏差。

### 9.5 集成审计结论
- **数据流贯通**：render→appendChar→kinsoku→placeChar(push _pendingLine.chars)→finishLine(push_back(_pendingLine)→_lineList)
  →done(铺 _charList 指针 + valign + keyWait time 回填 + renderPos 排序)。消费端 getCharacters(读 _charList
  charItem 全字段 + ruby 子 vec)/getKeyWait(读 _keyWaitList.index)/calcLineOffset(_lineList[i].lineBottom)/
  calcShowCount(_charList renderPos 倒扫)/renderLines·maxScrollLine(_lineList) 全部正确消费。✓
- **ruby bbox(+264/+268/+272)**：appendChar ruby 分支写，**clear(0x59EC6C)不 init、finishLine/done/任何属性/
  getCharacters 均不读**（交叉核实 clear 全表 + getCharacters dict 字段无此偏移 + 无其它 reader）→
  **确认 dead-but-faithful**（对象级 ruby bbox 在本 build 无消费者）。注意：getCharacters 的 ruby 字段读的是
  **charItem 级 ruby 子 vector(+56)**，是 LIVE 消费端，与对象级 bbox 是两个东西。
- **resolveFaceIndex faceTable.push —— 旧"在调用方"断言已证伪（field-level 穷尽核实）**：
  此前认为二进制在 resolveFaceIndex 调用方 push faceTable，本地合并到 resolveFaceIndex。**错**。
  对全二进制 288 处 `STR [Xn,#464]`（vector end 指针）+ 全部 6 个 caller + intern 子函数
  sub_5A181C 反编译核实：**二进制从无 faceTable push**。face 名只进 faceHash 节点(+536)；
  faceTable(+456) 恒空 → idx=size() 恒 0 → 所有 face 退化为 idx 0（原版退化行为）。
  clear/getCharacters/defaultFace getter/onStyleChanged 对 _faceTable[idx] 的读取在二进制是
  恒空 vector 的 inert 读（编译器为通用 vector<ttstr*> 字段生成的标准访问）。本地已删除多余 push
  （TextRender.cpp resolveFaceIndex），1:1 复刻恒空退化。
- **keyWait float bits 转换消费端**：done 写高 int=renderPos bits，但 getKeyWait 读低 int=index →
  高 int 无消费者（dead-but-faithful，§9.2）。本地 KeyWaitItem{index,time} 忠实保留双 int。
- **onStyleChanged 调用者链路**：setFont(face/bold/fontsize 变)/render %数字·%;·%B·%C/L/R·%S·%b·%i·%f →
  onStyleChanged(FuncCall L"onFontChange" dict{face,bold,italic})。链路完整。✓

### 9.6 line ~1820 "Potentially unintended semicolon" 核查
原 `#` 颜色分支 `if(!parseHexColor(tagAccum)) ;`（parseHexColor 恒返回 true → if 恒假 → 裸 `;` 死代码）。
反编译 0x5A228C case '#' 确认：tagAccum 非空→hex decode 写 +200；空→`*(+200)=*(+216)` 默认色。
**是误导性死代码（非真 bug）**，已改为 `if(!IsEmpty()) parseHexColor(); else _curChColor=_defaultChColor;`，
控制流与二进制 1:1，clang-tidy 警告消除。

### 9.7 全 50 成员对齐状态总表
**1 构造器 + 16 method**：(constructor @0x59D160→ctor 0x5A111C，C1 默认值群偏差已修复，见 §3b-0) +
setOption/setDefault/setRenderSize/clear/resetFont/resetStyle/setFont/setStyle/render/newline/
done/onEval/getKeyWait/calcLineOffset/calcShowCount/getCharacters —— **全部完全对齐**
（render 状态机批4、落字层批3、查询层批5、ctor 默认值群批6/C1）。
**33 property**：22 RW + 11 RO，全部 getter/setter 字段+公式经反编译核对 1:1（defaultFace 批5 修正为 index-based，
maxScrollLine 批5 实装）。

**平台边界（明确技术原因）**：
- native≠objthis（NativeClassBinder native≡dispatch vs 本地 ncbInstanceAdaptor 双对象）→ appendChar/finishLine/
  evalDollarTag/onEval/onStyleChanged/onGetTextWidth 显式传 objthis 回调脚本。
- 容器实现选型：_charList=vector<CharItem*>、_pendingChars=deque<CharItem>、_keyWaitList=vector<KeyWaitItem>、
  _faceHash=unordered_map（intern 表语义等价 + 忠实复刻内联 hash）—— 选型对齐，ABI 偏移不对齐（字节布局工作法）。

**dead-but-faithful 缺口**：
- 对象级 ruby bbox +264/+268/+272（appendChar 写，无任何消费者，§9.5）。
- keyWait 元素 time(高 int)=renderPos bits（done 写，getKeyWait 不读，§9.2）。

**验证状态**：web/debug 构建+链接通过（仅 pre-existing 警告）。headless playwright 千恋万花：
`textrender.dll Success` + `TextRender.tjs 読み込みました`（extends TextRenderBase 编译/加载成功），
**零 textrender abort/exception/No-method/RuntimeError/undefined-symbol**。游戏在 custom.tjs(218)
`movieQualitySelectMenuItem does not exist`（游戏自身菜单配置问题，**非 textrender**）处停在标题前——
pre-existing 游戏启动 quirk，非本批回归。**textrender 模块完成度：50/50 成员忠实对齐，收尾完成。**

### 9.8 IDB 重命名（批5）
（getCharacters/getKeyWait/onEval 此前已命名）；helper sub_5A6240(ruby 子数组)/sub_5A6550(数组 append)/
sub_5A2160(byte字段)/sub_5A614C(float字段)/sub_5A6020(int字段) 保留 sub_ 名（NCB dict-setter 内联 helper，
无二进制字符串证据可命名，留待后续）。
