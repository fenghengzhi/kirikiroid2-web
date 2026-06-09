---
name: textrender-dict-layer-audit
description: TextRenderBase dict 解析层(setOption/setDefault/setFont/setStyle/resolveFaceIndex/resetFont/resetStyle/clear)审计结论与3处偏差
metadata:
  type: project
---

2026-06-09 审计 cpp/plugins/textrender/TextRender.cpp dict 解析层 vs libkrkr2.so。结论 ⚠️部分偏差。

**已逐 key/逐分支验证为 ✅ 完全对齐**:
- setOption@0x59D2AC: 18 key 顺序/偏移全对; ignore_over(0x59d9bc)与ignore_overy(0x59da44)**都写+54**(同址覆盖,本地两处都写_ignoreOverY正确); kinsoku_max boolCoerce写DWORD(+112,0x59d714); 每byte字段boolCoerce(switch case1/3/4→qword0,case2→sub_A13294,case5→real0.0,default→0)
- setDefault@0x59DEA8: fontsize存在→+148并回填缺失big(+152)/small(+156)/ruby(+160)=+148 bits(0x59e17c/0x59e1b0/0x59e1e4); 缺失→各自独立realCoerce; linesize缺失fallback读fontsize key(0x59e918→+176); 逐key顺序全对
- setFont@0x59EFD8: changed(v5)仅face(0x59f0cc idx变)/bold(0x59f184)/fontsize(0x59f240 `<0||!=`)触发; rubysize(0x59f2f4 `<0||!=`写+128但NO changed); rubyoffset/color/shadow*/edge* NO changed; if(v5)sub_5A1F28
- setStyle@0x59F7AC: 只读linespacing(+136)/pitch(+140)/linesize(+144,缺失fallback fontsize)/align(**+76** 0x59fb08)/valign(**+80** 0x59fb9c); NOT调onStyleChanged, NOT读font键
- resolveFaceIndex内联hash@0x5A14DC: 1025*(acc+ch)^((..)>>6), 末尾*9, *32769^>>11, 逐位对齐FaceNameHash
- onStyleChanged@0x5A1F28/clear@0x59EC6C/setRenderSize@0x59EB70: 逻辑对齐

**❌ 3处真实偏差**:
1. **resolveFaceIndex(993行)push_back faceTable在二进制不存在【2026-06-09 field-level穷尽确证,确定性结论】**。证据链: (a)全二进制288处`STR/STP [Xn,#464]`(faceTable end指针)穷尽扫描, TextRender簇内**仅clear@0x59ee14一处**=`*(this+464)=begin`即vector::clear复位(逐元素Release后end=begin), 其余287处全属cocos2d/opencv/openal等无关结构. (b)resolveFaceIndex@0x5A14DC未命中分支=`idx=(this[58]-this[57])>>3`(=(+464-+456)/8=faceTable.size()), 然后`*(int*)faceHash_intern(this+536,&ttstr)=idx`只把size存进**+536 faceHash节点**(operator new 0x20={next@0,ttstr*@8 AddRef,idx@16})不push +456. (c)faceHash_intern sub_5A181C/faceHash_find sub_5A172C全核, 无一触碰+456. (d)6 callers(ctor sub_5A111C/setDefault/setFont/sub_5A0E0C/clear/render)全核无一在调用前后push. 结论: faceTable二进制ctor分配后**恒空(size恒0)→所有face idx恒退化为0**; clear/getCharacters@0x5a07a0/onStyleChanged@0x5a1f64/dtor@0x5a6c18对+456的read全是**对空vector的零迭代/恒空读**(非死代码,是编译器为vector<ttstr*>字段生成的标准访问,运行时inert). 修复=**删993行push**(本地push会让第2个face拿idx1破坏退化语义). 这是结论(a):二进制不存在的多余写入,应删.
2. **resetFont@0x59EEE0(436-448)**: 本地简化"全cur=default"。二进制有changed门控(curFaceIndex+72!=+96||bold+62!=+66||italic+65!=+69||+148!=+116)→写+72=+96/+116=+148/+62/+65并调onStyleChanged; rubysize+128`<0||!=`; 无条件+132=+164/+63=+67/+64=+68/OWORD+200..212=+216..228。本地缺+72(faceIndex)/+132(rubyoffset)复位+缺onStyleChanged
3. **resetStyle@0x59EFBC(449-455)**: 二进制只复位5字段且NOT调resetFont: +136=+168/+140=+172/+144=+176/**+76=+100(align)**/**+80=+104(valign)**。本地多调resetFont()+缺align/valign复位

**良性⚠**: setOptionStr/face对object/octet/int/real转字符串而非sub_A0E48C(throw,__noreturn)守护; FaceNameHash空指针(返0)vs空串(返0xFFFFFFFF)本地都返0xFFFFFFFF(注释128-129写反,功能等价); onStyleChanged用TJSCreateDictionaryObject vs off_1A0B930内部dispatch+FuncCall objthis用*(a1+0)vs本地objthis参数

子函数地址: faceHash_intern sub_5A181C(节点{next,ttstr*,idx}32B); ctor sub_5A111C; get_defaultFace sub_5A0DA8; set_defaultFace sub_5A0E0C
