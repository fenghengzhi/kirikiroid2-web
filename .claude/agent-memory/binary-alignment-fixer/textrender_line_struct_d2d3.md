---
name: textrender-line-struct-d2d3
description: textrender 批1-7 偏差全清 (2026-06-13)——批7=查询层写入层重构为 ncbind 栈 holder accessor(ncbDictionaryAccessor/ncbArrayAccessor+SetValue/FuncCall,删自制 trDictSet* helper+手动 Release);accessor 真身 vtbl off_19FD968/1A0B930/1A0B950+dtor@0x529634;SetValue TargetT 控扩展;批1-6 见正文(D2 嵌套Line/三色零扩展/diff符扩/属性accessor SXTW 镜像等)
metadata:
  type: project
---

textrender D2+D3 已修复（2026-06-11，一轮审计 ✅ PASS，构建通过，无运行时验证手段——textrender 无差分/fixture 覆盖）。

**D2**：pending 行缓冲（对象+320）与 lineList 元素同型 112B `Line { deque<charItem> chars(+0..79); float lineBottom/lineHeight/bbL/bbT/bbR/bbB(+80..103); int wordBreakRun(+104); bool prevWasSpace(+108) }`。本地已删 LineItem+_pendingChars+8 个摊平 metric 字段，改 `Line _pendingLine` + `vector<Line> _lineList`，`Line::clear()` 复刻 0x5A1E68（deque clear + 零化相对 +80..+108）。
同型三重证据：① 0x5A1E68 取 +320 指针零化相对 +80..+108（STR/STP/STUR 三条，+101 处 8B STUR 覆盖 +104/+108）；② finishLine push（0x5a3758/0x5a3768）与扩容 sub_5A43E8 拷 a2+80/a2+93 两 OWORD = +80..+108 全范围（含 wordBreakRun/prevWasSpace）→ 源码层 = `lineList.push_back(pendingLine)`；③ 0x5A1B24 dtor 共用（函数体只触 deque 控制块，Line 尾部 POD，~Line≡~deque；kinsoku 80B 裸临时 deque 也复用它，不与同型冲突）。
关键顺序契约：finishLine push 后 Line::clear 再读 +408（`if(bboxLeft>penX)` 左操作数恒 0）；clear@0x59EC6C Line::clear 在前、竖排分支随后写 +416/+408=renderSizeW（横排不写保持 0）。

**D3**：clear@0x59EC6C `STR XZR,[#0x118]` 8B=+280 renderPos+284 renderPosSnap、`STUR XZR,[#0xBC]` 8B=+188 renderDelayAccum+192 charDelayStep——本地 clearImpl 已补 `_renderPosSnap=0`/`_charDelayStep=0`，旧注释"clear 不重置 +192"已证伪改正（代码 + analysis §3b/§7.6 同步）。

**D1/Q1/R1/R2 已修复（2026-06-11，一轮审计 ✅ PASS，构建通过，仍无运行时验证手段）**：
- D1 kinsoku used<1 边界补 `--_renderCount`（0x5a50a0/0x5a50a8 两路与 LABEL_94 tail-merge 共享尾 0x5a52cc/0x5a52d8；仅 --(+84) 不动 +108；leading 未命中 0x5a5078 直接 LABEL_107 不减）。
- Q1 CharItem+40 真名 graph（getCharacters@0x5a081c dict **首字段** L"graph" 先于 text，串 @0x14CA19A；sub_5A2160 byte→Integer）——rubyFlag 已全局改名 + dict 最前插入 + analysis §9.1/§3b-1 同步。
- R1 render `$` 展开全成功（++v64>=v50 @0x5a295c → LABEL_319）补 v17=false；空结果 LABEL_320 不动 v17。
- R2 renderBalancedChar 配对前置门 `_begin.GetLen()==_end.GetLen()`（0x5a2a5c/0x5a2a68 两处 BL _ZdlPvm_22 = IDA 误标的 tTJSVariantString 长度 getter（读 +60），CMP @0x5a2a6c，B.NE → LABEL_319）。
**模式**：`_ZdlPvm_22`（"operator delete"）在 textrender 全文 = ttstr length getter，凡见返回值参与 CMP/循环上界即按长度处理。

**第四批（2026-06-11，一轮审计 ✅ 7/7 PASS，构建通过）边界抛错/结构细节组全部关闭**：
- R3 evalDollarTag@0x5A4148：onEval 返回 object/octet/int/real → TJSThrowVariantConvertError(String)（octet/int/real 走 `(u)(ty-3)<3`，object 单判同抛）；string 取值/void 空串；FuncCall 返回码不检查。
- N1 ncb_render@0x59FC28：a2>=4 时 `(void)param[3]->AsReal()` 强制后丢弃——本地 tTJSVariant::AsReal（tjsVariant.h:937）逐 case 与二进制 switch 同构（object/octet 抛 Real、string 解析、void 0.0 不抛），可直接用作 sub_A0E48C(,5u) 强制组的等价物。
- Q2/Q3+vDict：getKeyWait add-arg/result 与 onStyleChanged onFontChange arg 三处 variant ObjThis 槽均 null（(obj,nullptr)）；**getCharacters result 是 (obj,obj)——二进制两函数自身不一致，各自照抄，勿"统一"**。
- D4 countdown：`v21=(+88)-1; if(v21>=0){回写;return}` 先算后条件回写，负值不回写（+88 永不为负，非无条件 `--`）。
- 抛错组：paramAsDict type!=object → throw(Object)；setOptionStr 四串键 + setDefault/setFont face 键 object/octet/int/real → throw(String)（string 取值/void 空串）。
**模式**：textrender dict/参数层的 sub_A0E48C(v, N) = TJSThrowVariantConvertError(v, (tTJSVariantType)N)（1=Object,2=String,4=Integer,5=Real）；real/int 强制组直接等价 AsReal/AsInteger，但 string 强制组**不是** ttstr(v)——非 string/void 必须显式 throw。
textrender 三簇（dict 解析层/render 状态机+查询层/落字层）已知偏差全部清零；仍 open 仅注册/生命周期审计簇（auditor 称 ctor@0x5A111C 默认值群，需与 C1 批已修内容核对是否 stale）。

**第五批（2026-06-13，八审 2 项查询层 OPEN → 当日修复，一轮九审 ✅ PASS，构建通过，仍无运行时验证手段）**：
- D1 getCharacters color/shadowColor/edgeColor 零扩展（0x5a092c/0x5a0a04/0x5a0a7c `LDR W8` u32→tvtInteger 正值）：trDictSetInt 形参 int→tjs_int64 + 三调用点传裸 tjs_uint32 成员。**shadowDiff 走 sub_5A6020(int*)=符号扩展，(int) cast 必须保留**。
- D2 三处 TJSCreateArrayObject 后补 NativeInstanceSupport(GETINSTANCE, TJSGetArrayClassID(), &ni) dead-token（getKeyWait 0x5a0338/getCharacters 0x5a0708/ruby 0x5a62b0；ni 不消费 ≡ 二进制 &ni->Items 死值；idiom 同 tjsArray.cpp:1338-1341）。
**模式**：同一函数内同语义字段（四个 u32 颜色/diff）扩展方式可不同——由二进制成员 load 指令（LDR W=零扩 / LDRSW 或 int* 解引=符扩）逐键定，勿统一；helper 形参用 tjs_int64 让扩展决策留在调用点，与二进制 load-site 结构对应。grep 陷阱：tjs2 头文件构造函数藏在 TJS_METHOD_DEF 宏里，`grep "tTJSVariant("` 全空 ≠ 无 ctor。

**第六批（2026-06-13，P1 颜色属性 accessor 符号性，一轮审计 ✅ 完全对齐，构建通过，仍无 fixture）**：
- defaultChColor/defaultShadowColor/defaultShadowDiff/defaultEdgeColor 4 个 NCB property（get@0x5A0F4C/0x5A0F70/0x5A0F80/0x5A0FA4）的 TR_RW 边界类型 tjs_uint32→**tjs_int**；后备字段 `_defaultChColor` 等保持 tjs_uint32（颜色按位语义）。
- 证据：getter 体本身是 `LDR W0,[X0,#off]`（与 defaultAlign 0x5A0F0C 字面相同，零扩载入 W0），符号性**不在 getter 体**，而在共享的 i32 PropGet invoker **@0x5A913C** —— @0x5a91ec `SXTW X8,W0`（带符号扩展）+ variant type=4。故 0xFFFFFFFF 装箱后脚本面=-1 非 4294967295。
- proxy vtbl **base=0x1A0C858**（PropGet slot @0x1A0C888=+0x30=0x5A913C；PropSet @0x5A9260）；X25=0x1A0C858 在 ncb_registerMembers @0x59c684 单次加载、全程未重载 → 6 个 i32 RW 属性（defaultAlign/defaultValign + 4 颜色）共享同一 invoker，故颜色应与 defaultAlign 同为 tjs_int。
- **与第五批 D1 互为镜像且并存**：accessor 端（本批）符号扩展(SXTW invoker)；getCharacters dict 端（第五批）零扩展(LDR W trDictSetInt) —— **绝不要因"都是颜色"而统一**。
- IDB 笔误已就地纠正：invoker @0x5A913C 旧注释 base 写成 0x1A0C868，实测应为 0x1A0C858（slot 在 0x1A0C888），已 set_comments+idb_save；本地 cpp 注释一直正确。

**模式（NCB 属性 accessor 符号性判别）**：property getter 的脚本面符号性由**注册时分配的 proxy vtbl 的 PropGet invoker 装箱指令**决定（SXTW=带符号 tjs_int / 直接零扩=tjs_uint32），**不是** getter 函数体的 LDR 宽度。判别步骤：① get_qword 读 proxy vtbl base 找 PropGet slot(+0x30) 的 invoker 地址；② disasm invoker 看 BLR getter 之后是 SXTW 还是直接 STR W；③ 同一 vtbl 被多个属性共享（X25 单次加载未重载），符号性对该桶全体一致，可借已知属性(如 defaultAlign=tjs_int)反推。本地对策：getter 返回类型 = NCB 边界签名(tjs_int/tjs_uint32)，后备字段按内部语义(颜色用 tjs_uint32)，二者用隐式位保留转换衔接。

**第七批（2026-06-13，P2 查询层写入层结构重构，一轮审计 ✅ 三函数全完全对齐，构建通过 TextRender.cpp.o，仍无 fixture）**：
- getKeyWait@0x5A02DC / getCharacters@0x5A0694 / buildRubyArray@0x5A6240 的 dict/array 写入层从「裸 iTJSDispatch2* + 自制 static trDictSet{Str,Real,Int} helper + 手动提前 Release()」重构为 **ncbind 栈 holder accessor**：`ncbDictionaryAccessor`/`ncbArrayAccessor`（ncbind.hpp:823/827，addref=false）+ `dict.SetValue(key,val,512u,&hint)` / `arr.SetValue((tjs_int32)idx, dispatch, 512u)` / `arr.FuncCall(0u,L"add",&hint,nullptr,vDict)`。三个自制 helper 已删（无其它调用者）。
- **accessor 真实身份（IDB 已命名，本批复证）**：off_19FD968=ncbPropAccessor 基类 vtbl / off_1A0B930=ncbDictionaryAccessor vtbl / off_1A0B950=ncbArrayAccessor vtbl；holder 布局 {vptr@+0,_obj@+8} 栈对象；虚 dtor @0x529634 = vptr 复位基类 + if(_obj)_obj->Release()（≡ ncbind.hpp:629-633）。SetValue 出线实例：sub_5A2160=SetValue<u8>、sub_5A614C=SetValue<float>、sub_5A6020=SetValue<int*>(符扩)、sub_5A6550=SetValue<idx,dispatch>(variant{obj,obj} 经 vtbl+56 PropSetByNum)。二进制对各调用点内联与否（text/face/color/shadowColor/edgeColor/ruby 内联 vtbl+48 vs 其余出线）是编译器决策，**源码层统一 holder.SetValue**。
- **扩展语义经 SetValue 模板 TargetT 精确控制**（替代旧 trDictSetInt 的 tjs_int64 形参）：color/shadowColor/edgeColor 三键 `(tTVInteger)(tjs_uint32)` 零扩展；shadowDiff `(tjs_int)(int)` 符号扩展；graph/bold/.../vertical `(tjs_int)(?1:0)`；x/y/cw/size/delay `(tjs_real)`。`_toVariant(iTJSDispatch2*)→{r,r}`（ncbind.hpp:818）= sub_5A6550 的 variant{obj,obj}。
- **release 时机 = 栈 holder 析构自然在作用域尾**（迭代尾 per-dict @0x5a0bac/0x5a63e8；函数尾 arr @0x5a04b4/0x5a0c1c/0x5a645c），删除旧手动提前 Release()——这是本批纠正的核心三维偏（源码结构/调用链/生命周期）。
- result variant objthis 槽：getKeyWait={arr,null}（@0x5a04cc *(a2+8)=0）；getCharacters/buildRubyArray={arr,arr}（@0x5a0bf4/@0x5a6440 *(a+8)=arr）——第四批 Q2/Q3 的"两函数自身不一致勿统一"经 accessor 改写仍成立。
- 第五/六批所有 PASS 项（键序、24 hint 槽、三色零扩展、shadowDiff 符扩、ruby 两步分离、NIS GETINSTANCE &ni->Items 死值 token）经重构全保留未改坏。
- **平台差**：ncbind accessor 在 wasm32 下指针 4B ABI 与 ARM64 不同是允许平台差，对齐标的=源码结构非字节布局。
- onStyleChanged@0x5A1F28 走 objthis->FuncCall 而非新建 dict，不属 accessor 写入层，未纳入本批（题目要求谨慎，且其本就不是 trDictSet 调用者）。
**模式（裸 dispatch helper → ncbind accessor 判别）**：反编译里出现栈对象 `{vptr=&off_XXXX, _obj}` 且 vptr 在三个固定 off_（基类/Array/Dict）间切换、尾部 if(_obj)Release dtor、配 sub_ 出线（第 1 实参 &holder、内部 *(a1+8) 解引 _obj 调 vtbl+48/+56）——即 ncbPropAccessor::SetValue/SetValueByNum 模板实例。本地应直接用 ncbDictionaryAccessor/ncbArrayAccessor + SetValue，**勿用裸 dispatch + 自制 helper + 手动 Release**（行为等价但三维结构偏）。SetValue 模板 TargetT 即扩展控制点：(tTVInteger)零扩 / (tjs_int)符扩 / (tjs_real) / iTJSDispatch2* 产{r,r}。

**模式沉淀**：摊平字段 vs 嵌套结构体的判别证据 = ①专用 clear/dtor helper 接收子对象指针并按相对偏移零化、②push/拷贝按整段 OWORD 范围搬运（含"看似无关"的尾字段）、③同一 dtor 被两个容器位置共用。命中任一即应怀疑源码是嵌套 struct 而非对象级摊平字段。
