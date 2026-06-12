---
name: textrender-registration-lifecycle-audit
description: TextRenderBase 注册/生命周期/accessor 层审计：50成员全对齐；2026-06-13 八审独立重取证再 PASS 零偏差(33 accessor 全反编译+ctor常量/4禁则集逐字节复核)
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

**2026-06-12 七审（方案 A 重接后）**：六审两条低危观察均已解决——①成员声明序已重排为二进制偏移序(+0 objthis→…→+536 faceHash)，默认析构逆序=dtor@0x5A6B88 逆偏移序 ✓；②objthis 字段已复刻为首成员（ctor@0x5A111C 首句 `*(this+0)=objthis` 本会话重证；dtor 不 Release +0；Factory(&factory)→0x59D160 `new(0x250)+ctor(obj,objthis)` 数据流 1:1，ncbind 工厂签名第4参=objthis 与 0x59D160 a4 一致），旧"objthis 平台边界"注释已撤、无残留矛盾。方法体经归一化 diff 验证零漂移。残留见 [[textrender-dict-layer-audit]] render raw 三联。

**2026-06-13 八审（独立全量重取证）再次 PASS，零开放偏差**。本会话证据：50 成员表经 py_eval 全提取（名/序/tag/RO-RW/getter-setter 指针全核，11 RO 的 +64/+72/+56 全=0）；33 属性 accessor **全部**反编译（非抽样），后备字段/类型/`a2&1` bool 规范化逐一对上；ctor@0x5A111C 三 xmm 常量 + 4 禁则集 get_bytes 逐码点再核；0x59D160/0x5A6A60/0x5A6A94/0x5A6B88 生命周期链重证（dtor 逆偏移序释放=本地逆声明序析构；+0 不 Release；charList 仅释 buffer）；off_1A0B970 vtable 字节再证=[0x5A6650,0x5A67B4,0,0]。invoker 级类型转换（bool/float/int coerce）由"共用 ncbind"结论（§10 已证）保证与本地同模板，无需逐 invoker 比对。无任何 memory/analysis 记录被证伪。

**2026-06-12 独立复审（六审）再次 PASS，零开放偏差**。独立重取证：50 成员顺序/指针全提取（首成员名=类名 v2=**a1, Process=0x59D160；RW 属性 +48=getter OWORD/+64=setter OWORD，RO +64=0）；ctor 三 xmm 常量 + 4 禁则集 get_bytes 逐码点再核全一致；12 accessor + 4 setter 反编译 1:1（maxScrollLine 0x6DB6...*>>4=size() 即 112B 元素 magic-div，非源码 token）。两条低危观察（均判可接受，非偏差）：① 本地字段声明序按语义分组≠二进制偏移序 → 析构顺序与 dtor@0x5A6B88 逆序不同（字段间无依赖，inert）；② 二进制 ctor 首句 +0=objthis 字段本地未复刻（line 52-53 已标注平台边界，objthis 经 raw-callback 线程化传递），连带 clear/resetFont/newline/done 在二进制与 resetStyle 同走 sub_59EB78 模板而本地拆 raw/typed——是该边界的派生差异。
