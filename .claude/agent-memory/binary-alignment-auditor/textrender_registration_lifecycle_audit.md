---
name: textrender-registration-lifecycle-audit
description: TextRenderBase 注册/生命周期/accessor 层审计：50成员全对齐；ctor@0x5A111C 默认值群偏差已于2026-06-11修复并复审PASS(含4禁则集逐码点核对)
metadata:
  type: project
---

2026-06-11 审计 TextRender.cpp 注册/生命周期/容器/33 accessor 层 vs libkrkr2.so。

**✅ 完全对齐**:
- 注册链 0x42D01C/0x59BCCC: 50 成员 = **构造器(名=类名,Process=TextRenderBase_ncb_constructor@0x59D160,本次rename) + 16 method + 33 property**，名称/顺序/kind(tag1=Function,tag2=Property)/RO-RW(+48 getter/+64 setter,setter缺=RO)/flags=0 全与本地 NCB 块一致。sub_5A6E64 内 L"Multiple constructors." 守护确证首成员=构造器。
- 33 accessor 0x5A0D74–0x5A1080 逐个 decompile 全对齐：defaultFace index-based(get faceTable[+96] OOB→空串/set resolveFaceIndex)；renderLines=magic-div-112=size()；renderDelay=+188*+180；maxScrollOffset vertical?240-248:244-260；maxScrollLine 自尾行 -28float(=+84 lineHeight) 步进算法本地 1:1。
- 生命周期 0x5A6A60(24B wrapper)/0x5A6A94(条件 dtor+delete)/0x5A6B88(dtor 释放序=字段逆序,charList 仅释 buffer 不释元素=非拥有指针) — 本地等价(ncbInstanceAdaptor 平台边界)。
- 容器选型全对齐：faceHash 实为 **libstdc++ std::unordered_map 标准布局**(buckets@536/count@544/before_begin@552/size@560/policy@568 mlf=1.0/single_bucket@584,节点{next,key,val,cached_hash@24})→本地 unordered_map 是同款源码容器，非"语义等价替代"。
- faceTable 恒空再独立复核：簇内 0x59B000-0x5A7100 仅 clear@0x59EE14 一处 STR #0x1D0(end=begin 复位)，resolveFaceIndex/intern 均不 push。强断言第三次成立。

**✅ 已修复（2026-06-11 修复后复审 PASS）：真 ctor TextRenderBase_ctor@0x5A111C 默认值群已在本地 ctor+字段初始化器完整复刻**。复审独立取证：4 禁则集 UTF-16LE 常量 get_bytes 逐码点比对（following@0x14C9DF8=68cp / leading@0x14C9E82=19cp / begin@0x14C9EAA=10cp / end@0x14C9EC0=10cp，常量区连续存放、NUL 边界以 split-at-NUL 确认）全一致；打包常量展开核对：QWORD+112=0xBF80000000000001(kinsokuMax=1/curFontSize=-1f)、QWORD+148=0x4240000041C00000(fontSize=24/big=48)、xmm_14C95D0=(12,10,-2,6)f、xmm_14C95E0=(0,24,1,1)f、xmm_14C95F0=(0xFFFFFFFF,0xFF000000,1,0xFF0080FF) 全正确；末尾 resolveFaceIndex(L"normal")→+96、_faceHash(10) bucket hint 一致；二进制不初始化区本地全零（无非零越权值）；diff 无越界改动。原偏差记录（供历史参照）：
"惰性创建/无显式 ctor"（analysis 文档 §3）被证伪：构造器成员 Process = new(0x250)+ctor。ctor 写入而本地 `=default`+成员初始化器不符的：
- following/leading/begin/end = 内置日文禁则集字符串 @0x14C9DF8/0x14C9E82/0x14C9EAA/0x14C9EC0（本地空串→kinsoku 默认死）
- +49 word_break=1; +112 kinsoku_max=1; +116 curFontSize=-1.0f(脏哨兵); +128 curRubySize=-1.0f(哨兵); +100/+104 defaultAlign/Valign=-1（本地0=center→默认布局错）; +67 defaultShadow=1; +152 bigFontSize=48; xmmword_14C95D0: small=12/rubySize=10/rubyOffset=-2/lineSpacing=6; xmmword_14C95E0: pitch=0/lineSize=24/timeScale=1/fontScale=1; xmmword_14C95F0: chColor=**0xFFFFFFFF**(本地0xffffff)/shadowColor=0xFF000000/shadowDiff=1/edgeColor=0xFF0080FF; +192 charDelayStep=1.0
- ctor 末尾 intern L"normal"→+96（本地 _faceHash 构造后空）；hashmap 构造带 bucket hint 10(_M_next_bkt(0xA))
- ctor 不初始化 +60/+62..65/+72..92/+108/+132..147/+188/+196..212/+232..292（原版依赖 clear()；本地零init=良性加固）

**待纠正文档/注释**: analysis §3"真对象由 method 首次调用惰性 new"错；TextRender.cpp resolveFaceIndex 注释首段"push 在 setFont/setDefault 调用方"残留自相矛盾（次段已正确）。
