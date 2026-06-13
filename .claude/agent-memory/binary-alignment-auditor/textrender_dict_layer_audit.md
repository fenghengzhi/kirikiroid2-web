---
name: textrender-dict-layer-audit
description: TextRenderBase dict 解析层+NCB 包装层审计; 2026-06-13 九审推翻八审"零偏差": 新发现 3 微差(onStyleChanged 应为 ncbDictionaryAccessor+SetValue / resolveFaceIndex 按值 / dict 方法 v-dtor 先于 Release)
metadata:
  type: project
---

## 2026-06-13 N2 已修复并复证（resolveFaceIndex 形参按值 + 6 站点全证）
TextRender.cpp:1536 已改 `int resolveFaceIndex(ttstr name)`（原 const ttstr&）。本轮独立反编译复核 callee 0x5A14DC 体内对 a2/*a2 **零 AddRef/Release**（仅读 *a2 取 c_str + 透传 a2 给 find/intern），= caller-copy/caller-destroy by-value ABI。**全部 6 调用点逐一证毕**（非仅九审的 2 个）：ctor@0x5a1298 ttstr_createFromWide("normal") prvalue→槽→Release(0x5a12c0)；setDefault@0x59df74 STR XZR 槽→Release(0x59df90)；setFont@0x59f0a4 同款；clear@0x59ee18 X20→AddRef 拷贝→Release(0x59ee48)；set_defaultFace@0x5a0e2c *X1→AddRef 拷贝→Release(0x5a0e60)；render %f@0x5a2b8c var_80→AddRef 拷贝→Release(0x5a2bc0)。本地 6 站点全单对象传入（ctor prvalue / 其余命名 lvalue），by-value 形参编译器自动生成的拷贝构造恰复刻各站点 caller-copy token，无多余命名局部+额外拷贝。九审 N2 顾虑"留具名局部会变两对象"已排除：单一具名 lvalue 经 by-value 形参 = 一次拷贝构造 = 二进制 caller-copy，不是两对象。函数体 intern/faceTable 恒空退化此前已对齐。**resolveFaceIndex 全链完全对齐，N2 闭。** P3 单行签名改动与颜色键符号扩展/NativeInstanceSupport token/ncbind 绑定层零重叠，无副作用。

## 2026-06-13 九审（dict 解析层+状态复位 9 函数评估性复审，独立全量重反编译）：八审"零偏差"被部分推翻，新增 3 微差（评估任务，未改代码）
键表/顺序/coerce/写入字段/回调时机/changed 门控/回填三联/linesize fallback 等八审结论全部复核维持 ✅。新发现：
- **N1(P2, onStyleChanged@0x5A1F28)**: 二进制 = 栈上 **ncbDictionaryAccessor**（holder {vtbl,_obj}; vtbl off_1A0B930={0x529634,0x5A2244 派生dtor(textrender TU)} → dtor 切 off_19FD968={0x529634,0x530E4C 基类dtor(ncbind 代码区)}）+ **ncbPropAccessor::SetValue**（bold/italic = outlined 共享实例 sub_5A2160 ≡ ncbind.hpp:753 `PropSet(f,key,hint,&var,_obj)`；face = ttstr 实例内联 sub_A0FE2C+vtbl48）；dict 由 accessor dtor 在 FuncCall **之后**（函数末 0x5a20b4-20c4）释放。本地 TextRender.cpp:1541-1565 = 裸 TJSCreateDictionaryObject + 手写 PropSet×3 + FuncCall **前**显式 dict->Release()(1561)。运行时等价（vDict 持引用）但源码结构/调用链/释放时机三 token 偏差。修法：改 ncbDictionaryAccessor dict; dict.SetValue(...,&s_hintX); tTJSVariant vDict(dict.GetDispatch(),nullptr); 删提前 Release。
- **N2(P3, resolveFaceIndex@0x5A14DC)**: 参数**按值 ttstr**（双独立调用点证据：clear@0x59ee18-38 v17→v27 二次 AddRef 拷贝传参 + set_defaultFace@0x5a0e30-54 同款 caller-copy；const& 不会产生该拷贝；callee 不销毁=caller-destroy by-value ABI ✓）。本地 TextRender.cpp:1521 `const ttstr&` → clear/set_defaultFace 调用点各少一对拷贝 AddRef/Release token。⚠修复联动：setDefault@0x59df84/setFont@0x59f0b4/ctor 调用点二进制为**单对象**（prvalue/RVO 直接构造进实参槽），本地具名 faceName+const& 现恰好同为单对象；若只改签名按值而留具名局部会变两对象≠二进制——需调用点同步改 prvalue 形态（如返回 ttstr 的内联 helper）。
- **N3(P4, 4 dict 方法共有)**: 二进制 reused 局部 variant 的 dtor（sub_A0F778）**先于**尾部 dict Release（setOption 0x59dd7c→0x59ddb4 / setDefault 0x59eacc→0x59eb04 / setFont 0x59f708→0x59f740 / setStyle 0x59fba4→0x59fbdc；两 opaque call 不可重排=源码序，源码 v 在内层块作用域）。本地 `tTJSVariant v` 函数级作用域 → Release 语句先执行、v 后析构，次序相反。修法：dict 方法体内 key 读取段包一层 `{ tTJSVariant v; ... }` 再 Release。
- 维持已知：方法入口冗余前置 type check（671-672 与 AsObject 内部 throw 重复，七审 P4）；入参封送边界 v59 copy 早析构归 invoker 层裁定（七审 D1/D2/D3），本审未重开。
- setOption/setDefault/setFont/setStyle/setRenderSize/clear/resetFont/resetStyle 函数体其余检查点全部 PASS（含 clear 完整 22 步顺序、face 再 intern 越界→byte_1506A57 空串、vertical 分支 5 写序 +232/+248/+256/+416/+408、resetFont 路径3 自赋值=编译器保值维持）。

## 2026-06-13 八审（dict 解析层专项，6 函数独立重反编译）：零偏差 ✅
setOption@0x59D2AC 18 键 / setDefault@0x59DEA8 17 键（fontsize 回填三联 + 显式 big/small/ruby 在 fontsize 存在时被忽略 + linesize fallback）/ setFont@0x59EFD8 11 键（changed 仅 face/bold/fontsize；face PropGet FAIL→LABEL_15 即 changed=0 初始化）/ setStyle@0x59F7AC 5 键 / resetFont@0x59EEE0 三路门控 / resetStyle@0x59EFBC 纯 5 字段——键名、顺序、类型分发（string 键 {2→store,0→空串,1/3/4/5→throw(,2)}）、写入字段、回调时机全部 1:1。新确证两点：① resetFont 路径3（italic/fontsize 触发）二进制 *v3=v4 写 +62=旧+62 是编译器保值（该路径前提 +62==+66，自赋值≡赋默认值），本地 `_curBold=_defaultBold` 即源码原 token，非偏差；② (bool)tTJSVariant operator bool（tjsVariant.h:911-925）逐 case ≡ 二进制 boolCoerce switch。group 复位内部 store 顺序（本地 fontSize 先于 faceIndex vs 二进制相反）= 指令调度，inert。

## 2026-06-12 七审（方案 A 重接后，工作树未提交 diff vs 2780bf18）
绑定层重接：13 raw callback → 15 typed NCB_METHOD + Factory(&factory)，仅 render 保留 raw。
**D1/D2/D3 全部消除**（本会话独立重反编译验证）：
- D1 ✓：dict 方法经 ncbind doInvoke:1178 numparams<1→-1004 ≡ 二进制 0x5A71E0 @0x5a7238。
- D2 ✓：instance-missing 经 instanceGetter ncbind.hpp:1041-1043 SetError(-1008) ≡ 0x5A71E0 三连 GETINSTANCE 检查。
- D3 ✓：方法体 dictVar.AsObject()（AddRef，tjsVariant.h:682 = 二进制 0x59d2f8 vtbl[0] AddRef 内联）+ 尾 dict->Release()（=0x59ddb4 vtbl[1]）；throw 路径不 Release（同二进制）。
- **D5 半证伪**：旧记录"本地 typed 路径缺 result->Clear()"是错的——Clear 在 doInvokeBase 构造器 ncbind.hpp:1097 `if(r) r->Clear()`，顺序 objthis→Clear→numparams→instance 与二进制 dict/void/setRenderSize invoker 完全一致。仍真的一半：membername 二进制直返 -1001 vs 本地转发 BaseT::FuncCall（库级版本漂移，P3，全 NCB 类共有）。
**唯一开放偏差 = render raw 三联（同根，一个签名改动全修）**：
- 二进制 render 槽 off_1A0BE48 slot2 = **共享 raw 包装 invoker 0x5A77F4**（= ncbind ncbRawCallbackMethod<T*> 模板实例，含 flags&TJS_STATICMEMBER 分支=a1+58&1）：objthis→-1008、Clear、**GETINSTANCE→-1008（先于 numparams）**、调 Process(result,numparams,params,**native**)=0x59FC28（其内 numparams<3→-1004）。
- R1(P1)：本地 self(objthis,err=true) 缺实例时抛 "Invalid instance type."（fallback -1006）≠ -1008。
- R2(P1)：本地 numparams 检查先于实例取得，二进制相反。
- R3(P2)：本地签名第4参=iTJSDispatch2* → 选中 tTJSNativeClassMethodCallback/TJSCreateNativeClassMethod 包装；二进制= ncbind T* facade。
- 修法：render 签名改 `(tTJSVariant*,tjs_int,tTJSVariant**,TextRenderBase*)`，删 self()/throw/-1006，NCB_METHOD_RAW_CALLBACK 自动选 ncbRawCallbackMethod<T*>（ncbind.hpp:1504-1543 与 0x5A77F4 逐句同构）。
**invoker slot2 地址全表**（proxy vtbl+16）：dict=0x5A71E0/void=0x5A76EC/setRenderSize=0x5A741C→0x5A74C8(numparams 在内层先于 objthis,版本细节 inert)/render=0x5A77F4/onEval=0x5A7904(marshal 0x5A7B28=variant→ttstr)/getKeyWait=0x5A7C40(0-arg)/calcLineOffset=0x5A7E18(≥1)/calcShowCount=0x5A8098(≥1)/getCharacters=0x5A830C(≥2,marshal 0x5A8428=双 intCoerce)/ctor=0x5A70C4(含 numparams==1&&void→S_OK quirk ≡ ncbind.hpp:1416)。
P3 残留：onEval 本地收 tTJSVariant 体内转 ttstr，二进制 invoker 层转（0x5A7B28 sub_A0BAF4）——改 `onEval(ttstr)` 即同构。P4：AsObject 前置 type 检查与 AsObject 内部 throw 冗余（异常相同）；onGetTextWidth/evalDollarTag/onStyleChanged 的 if(!objthis) 守护为二进制不存在分支（factory 后不可能 null，inert）。

## 2026-06-12 六审：函数体维持零偏差 ✓；首次深挖 NCB 包装层，新开 5 项（D1/D2/D3 已被七审标记消除，D5 半证伪，仅供历史参照）
二进制 textrender 方法 = **typed 成员函数经 ncbind 模板封送**（非 raw callback）。invoker 解剖：
注册 helper sub_59D1B8(dict签名)/sub_59EB78(void签名)/setRenderSize 0x40B adaptor；
facade(ncbIMethodObject) vtable=0x19FE1F8；签名 invoker=外层 vtable slot2：dict=sub_5A71E0、
setRenderSize=sub_5A741C→sub_5A74C8、void=sub_5A76EC。统一顺序：membername→-1001 直返；
!objthis→-1008；result Clear(sub_A0F790)；numparams<N→**-1004**；NativeInstanceSupport(2,
classID@dword_1AB5158) 失败→**-1008**。本地 ncbind typed 路径(doInvoke ncbind.hpp:1178→1183)
numparams→instance 顺序与二进制一致；本地 raw 路径框架(tjsNative.cpp:87) membername/objthis/
result-Clear 也对。开放偏差（全在本地 raw callback 体内/库层）：
- **D1(P2)**: dict 方法 numparams<1 → 本地经 paramAsDict 返 null → **-1006 INVALIDOBJECT**；二进制 **-1004 BADPARAMCOUNT**。TextRender.cpp 1231/1305/1386/1459。
- **D2(P2)**: instance-missing → 二进制返 -1008；本地 self()=GetNativeInstance(objthis,**err=true**) adaptor 缺失时直接 TVPThrowExceptionMessage("Invalid instance type.")，fallback -1006 也不等 -1008。1175-1177。
- **D3(P3)**: dict 生命周期 — 二进制 AsObject()(AddRef)+尾部 Release；本地 AsObjectNoAddRef 无配对（运行期安全，源码 token 不同）。
- **D4(信息)**: tvtObject 但指针 null → 二进制 PropGet null-deref 崩溃；本地 nullptr 守护（注释标注，可接受）。
- **D5(P3,库级)**: 二进制 ncbind membername→直返 -1001（旧版 ncbind），本地转发 BaseT::FuncCall 哈希查找；本地 typed 路径(ncbNativeClassMethod::FuncCall:1336)缺 result->Clear()。版本漂移，影响所有 NCB 类，非 textrender 局部。
修复建议：setOption/setDefault/setStyle 不需要 objthis → 改 typed `void f(tTJSVariant)` 经 ncbind 自动封送即全对齐；需 objthis 的(setFont/clear/resetFont/setRenderSize)保持 raw 但 numparams 改 -1004、self 改 err=false+返 -1008。

2026-06-11 第二轮独立复核（全函数重新 decompile）。旧 3 偏差**全部已修复并验证**：
- resolveFaceIndex 不再 push _faceTable（恒空退化语义已复刻）✓
- resetFont@0x59EEE0 三路 group 门控(+72!=+96→+62!=+66→+65!=+69||+148!=+116)→写+72/+116/+62/+65+onStyleChanged；rubysize 门控(+128 <0||!=)；无条件+132/+63/+64/16B色块 — 本地 1:1 ✓
- resetStyle@0x59EFBC 纯 5 字段(+136=+168/+140=+172/+144=+176/+76=+100/+80=+104)无 resetFont 无回调 — 本地 1:1 ✓

**✅ 本轮确认完全对齐**: setOption 18-key 顺序+ignore_over/overy 同写+54+kinsoku_max DWORD; setDefault key 顺序+fontsize 回填(仅缺失 key)+显式 big/small/ruby 在 fontsize 存在时被忽略+linesize fallback fontsize; setFont changed 仅 face/bold/fontsize(脏哨兵<0||!=), rubysize 同哨兵无 changed; setStyle 仅 5 键写当前样式无回调; setRenderSize 写+240/+244 尾调 clear; onEval=result.type=0+sub_8E3FA4(TVPExecuteExpression(expr,*this,result)); 三套 coerce: boolCoerce≡(bool)tTJSVariant 逐 case 全等(nullsub_22=TJSSetFPUE, real!=0.0, string sub_A13294=ToInteger!=0), intCoerce≡AsInteger(obj/octet throw 4), realCoerce≡AsReal(throw 5, string sub_A133A8); PropGet flag 1024=MUSTEXIST, &0x80000000≡TJS_FAILED; onStyleChanged face OOB 经 unsigned 比较→空串(本地 idx>=0&&<size 等价)。

**2026-06-11 第四批修复后五审复核：本层全部偏差已清零**:
1. ~~clear 两处 8B 清零只清一半~~ **✅已修复（三审验证）**: clearImpl 现含 `_renderPos=0;_renderPosSnap=0`(=STR XZR 8B@+280) + `_renderDelayAccum=0;_charDelayStep=0`(=STUR XZR 8B@+188)。见 [[textrender-kinsoku-placeline]] D3。
2. ~~string-only 键错误路径静默~~ **✅已修复（五审验证）**: setOptionStr(TextRender.cpp 1276-1292) string→store/void→空串/default→TJSThrowVariantConvertError(v,tvtString)，1:1 对照 0x59d358((u)(t-3)<3)/0x59d368(==1→LABEL_10) sub_A0E48C(,2u)；setDefault face(1312-1320 ↔0x59df54-0x59df74)/setFont face(1395-1402 ↔0x59f084-0x59f0a4) 同构 throw 已实装。
3. ~~paramAsDict 返错码~~ **✅已修复（五审验证）**: paramAsDict(1210-1216) type!=tvtObject→TJSThrowVariantConvertError(*param[0],tvtObject)，对照 setOption@0x59d2e4/setDefault@0x59dee0/setFont@0x59f010 `if(type!=1) sub_A0E48C(v,1u)`。type==object 但 ptr null→返回 nullptr 由调用方 INVALIDOBJECT 守护=注明的平台守护（二进制该路径 v3=0 后续 PropGet 直接 null 解引用崩溃）。
4. ~~onStyleChanged arg variant (dict,dict)~~ **✅已修复（五审验证）**: 本地 1164 `tTJSVariant vDict(dict,(iTJSDispatch2*)nullptr)` = 二进制 0x5a2054 v12[0]=dict/v12[1]=0。PropSet flag 512/bold(+62)/italic(+65) Integer 均对齐。

子函数: sub_5A2160=intPropSet(holder,key,byte*,flag,hint); sub_9C8440=createDict; sub_A13878(&byte_1506A57)=空串 ttstr; sub_8E3FA4=TVPExecuteExpression。
