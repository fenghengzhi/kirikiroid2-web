---
name: textrender-render-chain-token-microdiffs
description: textrender render 链 5 项 token 级微差修复模式（operator+= in-place / RAII 析构序 / 裸 wcscmp / resize+back / 裸 c_str 扫描）及其反编译识别特征
metadata:
  type: project
---

textrender render 链十三审 5 项 token 级微差全部修复 + auditor PASS（完全对齐）。文件 cpp/plugins/textrender/TextRender.cpp。这些是「STL/ttstr 惯用写法 vs 二进制实际 token」的可复用识别模式，未来 textrender 及其它模块的同类微差可直接套用。

**项1 — ttstr 拼接 in-place operator+= vs a=a+b**（finishLine @0x5A34B8 三处：0x5a365c/0x5a36f0/0x5a37c0）
- 识别特征：二进制每处 sub_A13ABC(append) 前有 `atomic_load(refcount) → sub_A0BC58(Independ深拷) → ttstr_c_str` 三段序列。这 == ttstr::operator+= 的 Independ()+TJSAppendVariantString（tjsString.h:233-260）。`a=a+b` 会构造临时（多一次 alloc）无此 in-place independ 序列。
- 修复：`x = x + y` → `x += y`。operator+= 重载有 tjs_char/const tjs_char*/const tTJSString& 三种，按二进制 append 实参类型选。

**项2 — 临时容器 RAII 析构序 vs return 表达式内析构**（kinsoku @0x5A4A7C，tmp deque v81）
- 识别特征：二进制 `pendingLine_dtor_guess(v81)` 在所有出口（失败 LABEL_113 @0x5a5328 return 0 / 正常 @0x5a5338）都在落字 LABEL_10 **之前**。本地 `std::deque tmp; ...; return placeChar(...)` 让 tmp 活到 return 表达式求值之后（块尾析构 = return 之后），与二进制相反。
- 修复：把 tmp 用显式 `{...}` 块包住重排+finishLine+drain，块闭合后再落字。失败路径 `return false` 留块内（return 触发块尾析构 = LABEL_113）。早期落字分支（following&&used<max&&back∉following @0x5a5338 fall-through）用 bool 标志跳过 finishLine、同样块闭合后落字。
- 验证手段：disasm 确认 fall-through 分支是 `B loc_5A5338`（跳过 finishLine 直奔 dtor+落字）。

**项3 — 裸 wcscmp vs 构造临时 ttstr 比较**（updateWordBreakState @0x5a4b8c）
- 识别特征：二进制 `wcscmp_utf16(ttstr_c_str(x), L"...")==0`（wcscmp_utf16 @0x9B1ED0 = 裸 UTF-16 逐元素比较，返回 0=相等）。本地 `x == ttstr(TJS_W("..."))` 构造临时 ttstr。
- 修复：`TJS_strcmp(x.c_str(), TJS_W("...")) == 0`。TJS_strcmp（tjsConfig.h:55）= ttstr operator== 内部用的同款 UTF-16 c_str 比较，对应 wcscmp_utf16。

**项4 — vector resize+back 就地写 vs push_back 临时拷贝**（appendChar ruby 分支 @0x5a3a84）
- 识别特征：二进制经 `_M_default_append`（@0x5A5374 rubyVec_defaultAppend，新元素 memset 0）增长 vector，再对新 back 元素就地写各字段（v28-3/-2/-20/-1 = x/y/text/span @0x5a3b34..0x5a3b3c）。push_back(临时) 会先构造临时再拷入。
- 修复：`v.resize(v.size()+1); auto &slot = v.back(); slot.a=..; slot.b=..;` 按二进制写入字段顺序就地赋值（RubyItem 默认成员初始化器=值初始化=memset 0 等价）。

**项5 — 裸 c_str 指针线性扫描求索引 vs ttstr::IndexOf(临时)**（render @0x5A228C begin/end 平衡集，三处：0x5a2640/0x5a29d8/0x5a30d8）
- 识别特征：二进制内联 `p=c_str-1; do{ch=p[1];++p;}while(ch!=target && ch);` 逐字符走查；命中索引=(p-c_str)（end 处 byte-diff>>1=char 数），未命中(ch==0)=-1 哨兵。空集走 off_1AA7EF8 空串 sentinel（首字符0）立即未命中。本地 `set.IndexOf(ttstr(c))` 构造临时 + 子串查找。
- 修复：static helper `scanCharIndex(const ttstr&, tjs_char)` 复刻指针走查（c_str()空串返 TJSNullStrPtr 首字符0，安全返回-1）。注意 begin 用 byte-diff 符号位判 found，end/配对用 (p-c_str)>>1 char 索引——C++ `tjs_char*` 指针减法已得 char 数，统一用 `(p-cstr)` 即可，begin 仅用 found/not-found 不用数值。

通用教训：「STL/ttstr 写得更地道」≠ 源码 token。识别 in-place(operator+=) 看 Independ 前置序列；识别 resize+back 看 _M_default_append 调用；识别裸扫描看 c_str()+指针走查（而非 IndexOf 子串调用）；RAII 容器析构序看 dtor 调用相对落字/return 的位置。
