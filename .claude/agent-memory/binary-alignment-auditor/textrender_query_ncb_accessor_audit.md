---
name: textrender-query-ncb-accessor-audit
description: textrender 查询层 getCharacters@0x5A0694/buildRubyArray@0x5A6240/getKeyWait@0x5A02DC：D1 P2重构已修=ncbind栈holder十审✅;11审三微差(face缓存tTJSVariant/+61死字段/13浮点属性float签名)已闭案;D5颜色getter SXTW仍开放;新发现renderLeft/Top/Right/Bottom@0x5A1018+应为float(S0)当前double(后续)
metadata:
  type: project
---

## 2026-06-13 十审（P2 重构后复审）：D1 已解决 = ✅ 完全对齐
本轮把 dict/array 写入层从「裸 iTJSDispatch2* + static trDictSet* helper + 手动提前 Release」改写为 ncbind 栈 holder accessor（ncbArrayAccessor/ncbDictionaryAccessor + SetValue/FuncCall，ncbind.hpp:602-830）。独立反编译三函数复证：源码结构/数据流/调用链/生命周期/容器全部 1:1，重点核查项 1-10 全 PASS 无回归。
- getKeyWait@0x5A02DC ✅：count 一次提升+begin 每迭代重读;pos/time 同源低 int(v16=4);add=FuncCall objthis=arr param1 by-value{dict,null};result{arr,**null**}@0x5a04cc;dict 迭代尾 Release/arr 函数尾 0x5a04b4;holder 全内联 SROA(源码层仍 ncbArrayAccessor)。
- getCharacters@0x5A0694 ✅：!a3→+84-start;clamp v13;face 缓存 v15=-1+OOB 空串;17 键序逐地址核对;color/shadowColor/edgeColor LDR W 零扩展 vs shadowDiff sub_5A6020 符扩(两者分别正确);ruby 两步分离(sub_5A6240@0x5a0aec+SetValue@0x5a0b28 gate +56!=+64);v34=&ni->Items 死值;落数组 sub_5A6550={dict,dict};result{arr,**arr**}@0x5a0bf4。
- buildRubyArray@0x5A6240 ✅：a1=&ruby vector,stride 20(text@0/x@8/y@12/span@16);text→x→y→size(span)键序;v18=&ni->Items 死值;result{arr,**arr**}@0x5a6448;rd 迭代尾/arr 函数尾 Release。
- 关键映射：_toVariant(iTJSDispatch2*)={r,r}(ncbind.hpp:818)=sub_5A6550 variant{obj,obj};FuncCall 模板 param1 by-value+objthis=_obj(778-779)=getKeyWait add;~ncbPropAccessor if(_obj)Release(629-633)=dtor 0x529634;子类 addref=false(824/828)=TJSCreate*Object 无额外 AddRef。
- 注意 wasm32 accessor 指针 4B vs ARM64 8B = 允许平台差(对齐标的源码结构非字节布局)。
- 以下 D1 段为历史记录（已解决，勿据此再判结构偏差）。


2026-06-13 独立重审 textrender 查询层（onEval/getKeyWait/calcLineOffset/calcShowCount/getCharacters/buildRubyArray/33 accessor/faceHash/hint 槽）。**两项新偏差证伪了此前"查询层九审 PASS / 33 accessor 零偏差"结论**（见 [[textrender-registration-lifecycle-audit]] 同步勘误）。

## D1（开放，结构偏差）：查询层 dict/array 写入 = ncbind accessor 类，本地裸 dispatch 自制 helper
铁证链（getCharacters@0x5A0694 / buildRubyArray@0x5A6240）：
- 栈 holder {vptr,_obj@+8}：dict vtbl=0x1A0B930、array vtbl=0x1A0B950、基类 ncbPropAccessor vtbl=0x19FD968（dtor 期 vptr 复位）。
- dtor 0x529634 = `if(_obj) _obj->Release()`；deleting 0x5A2244/0x5A6104。
- 出线 SetValue 实例：0x5A2160(u8)/0x5A614C(float)/0x5A6020(int)/0x5A6550(byNum dispatch)——与本地 ncbind.hpp:602/752-757 `tTJSVariant var; _toVariant(var,val); PropSet(f,key,hint,&var,_obj)==TJS_S_OK` 逐 token 吻合；`_toVariant(iTJSDispatch2*)=Variant(r,r)` 正解释 0x5A6550 的 AddRef×2 + {v7,v7,type1}。
- ncbDictionaryAccessor()/ncbArrayAccessor() ctor = TJSCreate*Object 无额外 AddRef ✓。
getKeyWait@0x5A02DC 强证据（holder 被全内联+SROA 故无 vptr store）：per-key 二步 variant 形（默认构造+temp+CopyRef@0xA0FB64）、"add" 走 FuncCall 包装 by-value 形参拷贝（0x5a043c copy-ctor 0xA0F5E0）、dict 在迭代尾 0x5a0498 Release（=accessor dtor 位）。
本地 TextRender.cpp:904-1092 用裸 iTJSDispatch2* + static trDictSetStr/Real/Int + 手动提前 Release——行为面（键名/键序/hint/flag512/类型扩展/净引用计数）全对，但源码结构/调用链/生命周期 token 不对。修法：改用 ncbDictionaryAccessor/ncbArrayAccessor + SetValue(key,val,512,&hint)。
派生 token 差：face 缓存二进制=函数级 tTJSVariant(v30, operator=(ttstr) 刷新)，本地=块级 ttstr；dict Release 位置（迭代尾 vs PropSet 前）；getKeyWait add 双 variant vs 单 variant。

## 2026-06-13 十一审（三微差 token 级修复复审）：✅ 全部对齐，受保护工作零回归
本轮三处修复独立反编译复证，全部 ✅：
- 修复1 ctor +61 死字段：ctor@0x5A111C `STRB WZR,[X19,#0x3D]`@0x5a11f0 = +61 唯一 writer（本审 disasm 独立复证），相邻 +60(_renderOver)/+62(_curBold)；本地 `bool _unused61` 声明序=偏移序。零 reader（deep-analyzer 三层 + 本审 ctor 复证）。
- 修复2 face 缓存 ttstr→tTJSVariant：v30=tTJSVariant(16B+vtype)，sub_A0FE2C@0xA0FE2C 已反编译=operator=(String)(`*(a1+16)=2`+AddRef)；逐字符 sub_A0FB64 CopyRef + SetValue("face")@0x5a091c；尾 sub_A0F778@0x5a0bf8 单析构。OOB/in-bounds 两路都经 sub_A0FE2C 写 vtype=2。本地 tTJSVariant faceName 循环外 + operator=(const ttstr&) 1:1。
- 修复3 13 浮点属性 double→float NCB 签名：抽查 timeScale@0x5A0D88/defaultFontSize@0x5A0EAC 均 `LDR S0;RET`；renderDelay@0x5A1008 `FMUL S0,S0,S1`；maxScrollOffset@0x5A1058 `FSUB S0,S0,S1`；maxScrollLine@0x5A1080 `SCVTF S0,W8`/`FMOV S2,#1.0` 全程 S。后备字段本就 float，仅边界签名改。这把上一审「13 浮点属性微差」消除。
受保护工作回归核实（全 ✅ 未碰坏）：defaultChColor get@0x5A0F4C `LDR W0`(SXTW 装箱,P1 带符号)/defaultAlign get@0x5A0F0C `LDR W0`(int)/getCharacters color 三键零扩展 vs shadowDiff sub_5A6020 符扩(P2)/ruby 两步分离/hint 槽/resolveFaceIndex 按值(P3)/render 链未触碰。

## 后续工作（新发现，非本轮偏差，本轮未要求改）
renderLeft@0x5A1018 经本审 disasm = `LDR S0,[X0,#0xF8];RET`（单精度 S0），但本地 TextRender.cpp:431-434 `TR_RO(double, renderLeft/Top/Right/Bottom)` 仍 double = 与修复3 同类的存量数据流偏差（4 个 RO 属性）。下轮抽查 renderTop@0x5A1020/Right@0x5A1028/Bottom@0x5A1030 确认均 S0 后改 `TR_RO(float,...)`。本审已确认 renderLeft 一处 S0 作起点证据。

## D5（开放，逻辑/边界偏差，脚本可见）：颜色属性 getter 符号扩展
注册段 X25=proxy vtbl 0x1A0C858 全程未重载，defaultAlign/defaultValign/defaultChColor/defaultShadowColor/defaultShadowDiff/defaultEdgeColor 六属性共享之；其 PropGet invoker=0x5A913C，0x5a91ec `SXTW W0→X8` = getter 返回按【带符号 int32】装箱 → 源码 getter 签名是 tjs_int。本地 TR_RW(tjs_uint32,...) 零扩展 → 0xFFFFFFFF 脚本面 4294967295 vs 二进制 -1。**与 getCharacters dict color 三键（LDR W 零扩展，正值）并存于二进制——两处必须分别复刻，勿混改**。

## 微差（开放，低危）
- 13 个浮点属性 get/set 原型二进制为 float（getter 返回 S0），本地 double（行为经 variant 完全一致）。
- resolveFaceIndex 形参按值 ttstr（set_defaultFace@0x5A0E0C 内可见 caller-copy/caller-destroy 编排），本地 const&。
- face OOB 判定二进制单条无符号比较，本地 fi<0||fi>=size 双子句（等价）。

## 本轮 PASS（独立逐句重证）
onEval@0x5A0294（type=0 预置+sub_8E3FA4=TVPExecuteExpression 三参 ctx 重载）；calcLineOffset@0x5A05FC（magic-div-112、(u64)(int) 符扩 OOB→+260、+80 lineBottom）；calcShowCount@0x5A0644（count<=1→0、倒扫 renderPos*timeScale>(float)w、v6<=1 先判后减）；faceHash 三函数（null Ptr→0 不雪崩/空内容→雪崩→0xFFFFFFFF/one-at-a-time 1025*…^>>6、9*、32769*(h^h>>11)/find+operator[] 双哈希拓扑/32B 节点 value 零初始化）；33 accessor 后备字段+RO/RW+a2&1；hint 槽 .bss 0x1AB5190..51EC 跨函数同名同槽（xref 证：face 5190=getCharacters+onStyleChanged、text 51B8/size 51C8=getCharacters+ruby）；getCharacters 17 键序/零扩展(color三键)/符扩(shadowDiff sub_5A6020)/ruby 两步分离/NIS+Items 死 token/keyWait pos+time 同源低 int；RubyItem stride=20B（指令级 +8/+4/+4/+4 确证，wasm 端布局 ABI 自由）。

## 交叉核查建议（未在本轮取证）
dict 解析层（setOption/setFont/setStyle/setDefault）的"手动 AddRef/Release+PropGet(1024,hint=0)"形与 ncbPropAccessor(const tTJSVariant&) ctor + checkVariant(key,var) 完全同形——该层此前"零偏差"结论可能同样把 ncbPropAccessor 内联误判为手写；下次审 dict 层时检查 setOption 等是否有 holder vptr store / 0x19FD968 引用。
