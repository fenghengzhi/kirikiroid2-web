---
name: textrender-token-microdiffs-round14
description: textrender 十四审 token 微差三修一PASS——+61死字段/face缓存tTJSVariant/13浮点属性float签名；dict变体生命周期证伪item1
metadata:
  type: project
---

十四审（2026-06-13）textrender 四项 token 微差清单结果：

**item1 setOption/setDefault/setFont/setStyle dict 变体作用域 = PASS（描述错误，未改）**
四个 dict 解析方法（0x59D2AC/0x59DEA8/0x59EFD8/0x59F7AC）的 PropGet 临时变体是**单个函数级栈变量**（如 setOption v57/v58），跨全部 key 复用，函数尾 `sub_A0F778` 单次析构（紧接 dict Release vtbl+8）。**不是**内层块作用域、析构早于 dict Release。本地已用函数级 `tTJSVariant v;` 复用，已对齐。把变体改进内层块作用域反而**偏离**二进制。
- 教训：「reused variant 应在内层块」这类描述需反编译核实析构点，勿轻信。dict 方法的变体 = 函数级单实例 + 尾部单次析构。

**item2 ctor +61 dead bool field = 已修**
ctor @0x5A111C 0x5a11f0 `STRB WZR,[X19,#0x3D]`（+61 写 0），全二进制零 reader（deep-analyzer 三层扫描：83 函数指令级 + spanning 宽访问裁决 + 全 .text 兜底，仅 ctor 一处）。本地结构体 +60(_renderOver)/+62(_curBold) 间补 `bool _unused61=false`。死字段也是源码 token，ctor 字段初始化器 `=false` 即复刻 STRB WZR。

**item3 getCharacters face 缓存 ttstr→tTJSVariant = 已修**
getCharacters @0x5A0694 face 缓存 v30 是**函数级 tTJSVariant**（v30[2]+v31=16B union+4B vtype），非 ttstr：
- sub_A0FE2C @0xA0FE2C = tTJSVariant::operator=(String)（设 vtype=2 + AddRef），faceIdx 变时刷新
- sub_A0FB64 @0xA0FB64 = tTJSVariant CopyRef（按 vtype switch 拷贝），逐字符拷进 temp + SetValue("face")
- 函数尾 sub_A0F778 @0x5a0bf8 单次析构
本地改 `tTJSVariant faceName`（循环外），`operator=(const tTJSString&)` 复刻刷新，`dict.SetValue(key,faceName)` ncbind 模板 identity convertor 直传变体复刻 CopyRef+SetValue。OOB/in-bounds 两路都产 String 变体。

**item4 13 浮点属性 NCB 边界签名 double→float = 已修**
13 个 getter `LDR S0,[X,#off]`（单精度）直接 S0 返回，无 FCVT 升 double：timeScale/fontScale/defaultFontSize/Big/Small/LineSize/LineSpacing/Pitch/RubySize/RubyOffset + 复合 RO renderDelay(FMUL S0)/maxScrollOffset(FSUB S0)/maxScrollLine(全程 S)。后备字段本就 float，仅 TR_RW/getter 返回签名 double→float。
- **勿误改**：defaultAlign/Valign(0x5A0F0C/0x5A0F1C) = LDR W0 int；颜色 4 属性 = tjs_int SXTW（P1）。
- NCB_PROPERTY 按 getter/setter 名注册，类型由 ncbind 模板从签名推断，注册块无需改。

**遗留 = 十五审已闭环（2026-06-13）**：renderLeft/Top/Right/Bottom 四 RO 浮点属性 NCB 边界签名 double→float 已改完。disasm 全确认单条 `LDR S0`+RET（无 FCVT）：renderLeft@0x5A1018 [X0,#0xF8]+248 / renderTop@0x5A1020 [#0xFC]+252 / renderRight@0x5A1028 [#0x100]+256 / renderBottom@0x5A1030 [#0x104]+260。后备字段 _render* 仍 float，仅 TR_RO 边界类型改。审计 ✅完全对齐，构建通过，无回归（颜色/align/13浮点属性等未碰）。textrender render* 浮点边界签名全量收敛。

构建通过（TextRender.cpp.o 干净编译）；无 textrender fixture/test → 无运行时验证（非谎称）。审计 ✅完全对齐、P1/P2/P3/Group A 无回归。
