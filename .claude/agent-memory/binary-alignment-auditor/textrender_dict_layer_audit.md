---
name: textrender-dict-layer-audit
description: TextRenderBase dict 解析层+状态复位层(setOption/setDefault/setFont/setStyle/setRenderSize/clear/resetFont/resetStyle/onEval/coerce/onStyleChanged)审计结论; 2026-06-11 五审后零开放偏差
metadata:
  type: project
---

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
