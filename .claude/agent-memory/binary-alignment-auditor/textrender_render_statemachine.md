---
name: textrender-render-statemachine
description: TextRenderBase::render 状态机 @0x5A228C + 查询输出层(getCharacters/getKeyWait/calcLineOffset/calcShowCount) 对齐结论；2026-06-11 第四批修复后五审：全部 6 偏差(graph/$ v17/length 门/evalDollarTag throw/ncb param[3]/getKeyWait objthis)已修✅零开放
metadata:
  type: project
---

TextRenderBase render 状态机 @0x5A228C (NCB wrapper @0x59FC28) + 查询输出层。本地 cpp/plugins/textrender/TextRender.cpp renderImpl(~1911)/renderPercentTag(~1539)/renderBalancedChar(~1821)/render-wrapper(~1871)/getCharacters(~2142)/getKeyWait(~2102)/calcLineOffset(~1022)/calcShowCount(~1031)。

**2026-06-11 复审发现 3 行为偏差 → 当日修复并经第四轮独立反编译验证全部 ✅ 对齐：**

1. ✅(已修) **getCharacters `graph` dict 字段**。二进制 0x5a081c 第一个 PropSet 即 sub_5A2160(L"graph", charItem+40 byte→Integer type4, flag 512)，先于 0x5a0858 L"text"。串 @0x14CA19A UTF-16LE `graph\0` get_bytes 确证。本地 CharItem+40 已改名 rubyFlag→graph(grep 0 残留)，getCharacters 首字段 trDictSetInt("graph", ci->graph?1:0)；全 18 字段顺序 graph,text,x,y,cw,size,face,color,bold,italic,shadow,edge,shadowColor,shadowDiff,edgeColor,[ruby],vertical,delay 与二进制逐一吻合。
2. ✅(已修) **render `$` eval 展开成功后 v17=0**。二进制 ++v64>=v50 @0x5a295c → LABEL_319(v17=0)；空结果 v50<1 @0x5a284c → LABEL_320 不动 v17；appendChar 失败 → LABEL_325 return false 不 finishLine。本地三路全对齐(成功补 v17=false)。
3. ✅(已修) **renderBalancedChar begin.GetLen()==end.GetLen() 门控**。disasm 0x5a2a58-0x5a2a70 确证：LDR [X19,#0x18]/[X19,#0x20] + BL _ZdlPvm_22(decompile 证实=tTJSVariantString 长度 getter, `return *(u32*)(ptr+60)`, IDA 误标 operator delete) + CMP W26,W0 @0x5a2a6c, B.NE→loc_5A3194(LABEL_319 v17=0)。门在 --depth==0(0x5a2a4c)之内、indexOf 配对(0x5a2a74)之前。本地位置/不等时行为(仅跳过置零仍 v17=false)一致。
4. ✅(已修, 2026-06-11 五审验证) **evalDollarTag@0x5A4148**：本地(TextRender.cpp 1603-1620) 按二进制分发序复刻：(u)(type-3)<3(octet/int/real)@0x5a41d8→throw(String)；==2 取值@0x5a422c；==1(object)@0x5a41e8→同抛；void→空串@0x5a41f8；FuncCall 返回码不检查。
5. ✅(已修, 五审验证) **ncb_render param[3](size)**：本地 1982 `(void)param[3]->AsReal()`(a2>=4 时)；AsReal(tjsVariant.h 937-955) 逐 case 与 switch@0x59fcb0 同构(object/octet→throw(,5u)、string→sub_A133A8 解析、int/real/void 不抛、入口 TJSSetFPUE=nullsub_22@0x59fc88)；强制顺序 param[3]→param[4]→text→param[1]→param[2] 与二进制一致(throw 顺序保真)。
6. ✅(已修, 五审验证) **getKeyWait 两处 variant objthis**：本地 2220 vDict(dict,nullptr)=0x5a0430 v15[1]=0；2229 result=tTJSVariant(arr,nullptr)=0x5a04cc *(a2+8)=0。getCharacters 的 (obj,obj) 是二进制自身行为(result objthis=arr/dict 经 sub_5A6550 objthis=dict)，两函数各自照抄，勿互改。

**06-09 两项强断言仍成立**（本次独立复核）：'[' ruby 仅 refcount no-op 无 +528 写；%C/%R/%L cascade 真 fall-through 终值 +76=-1。x(a3)=平衡集启用标志/y(a4)=charDelayStep(+192) 初值非坐标，亦复核成立。

**已复核 ✓ 的部分**：入口复位(a5&1==0→lineList 逐项 sub_5A1B24 析构+end=begin、+188=0、keyWait end=begin)；+280=0；curFontSize 快照给 \w；# hex（mask 0x7E0000007E03FF+减表 qword_14CA200、0x 前缀、空→+216 默认色、恒 |0xFF000000）；%数字/;/B/S/C/R/L size+align；%b/i(+onStyleChanged)/%e/s(无 onStyleChanged) 门控+57；%f(+58,空→+96)；%d/%a(+52)；%p 门控是 +59 ignoreStyle 空→+172；%l/%t/%w 仅校验(+52)；%D 非$路径**无** +52 门控、$路径 v136=v29+3 有门控；\i/\k/\n/\r/\t/\w/\x；&→v17=0；裸 0x0A→v17=1；\n 不动 v17；失败路径不调 finishLine；cursor 推进语义全部逐分支核对一致。scanTagUntil@0x5A3CE4/scanDigits@0x5A3F18(vector<tjs_char>+terminator+空→null ttstr、delim/非数字被消费)、parseInt10@0x9B111C、keyWaitList_pushBack@0x5A5874(vector 8B 元素、QWORD=零扩展 renderCount)、lineItem_destroy@0x5A1B24(内嵌 deque<charItem> 析构)、calcLineOffset@0x5A05FC(112 stride、unsigned 越界→+260 否则 +80)、calcShowCount@0x5A0644(count-1<1→0、倒扫 +24*timeScale>width)全 ✓。getCharacters count==0→+84-start、clamp、face 缓存 v15=-1/unsigned 越界→空串、ruby 仅 +56!=+64(sub_5A6240：20B 元素 text/x/y/size、PropSetByNum)全 ✓。boolCoerce ≡ 本地 tTJSVariant::operator bool（tjsVariant.h:911 switch 逐 case 同）。

**Inert/可忽略**：v132 二进制栈垃圾初值 vs 本地 0；%数字 本地无条件 parseInt10、%l/%t/%D 空标签二进制对空串调 parseInt10 本地跳过（纯函数结果丢弃）；FuncCall hint 指针本地传 null；本地 getCharacters srcIdx 越界 break 守护（二进制无，负 start 会 OOB）；_percentCursor 成员化=拆函数实现细节（调用方已认可平台边界级处理）。

**字段偏移速查(a1+N)**：+48=vertical, +50=ignoreColor, +51=ignoreSize, +52=ignoreDelay, +56=ignoreRuby, +57=ignoreType, +58=ignoreFace, +59=ignoreStyle, +62..69=cur/default bold/shadow/edge/italic, +72=faceIndex, +76=curAlign, +84=renderCount, +96=defaultFaceIndex, +116=curFontSize, +140=curPitch, +148=defaultFontSize, +152=bigFont, +156=smallFont, +172=defaultPitch, +188=renderDelayAccum, +192=charDelayStep(=a4), +196=lineStartX, +200=curChColor, +216=defaultChColor, +232=penX, +236=penY, +280=renderPos, +296/304=charList(vector<charItem*>), +432/440=lineList(112B), +456/464=faceTable, +480/488/496=keyWaitList, +24=begin ttstr, +32=end ttstr。charItem：+0 text,+8 x,+12 y,+16 cw,+20 size,+24 renderPos(delay),+28 color,+32 shadowColor,+36 edgeColor,+40 **graph**(byte→int 输出),+41..45 bold/italic/shadow/edge/vertical,+48 shadowDiff,+52 faceIndex,+56/64 ruby vector(20B elem)。
**子函数**：finishLine sub_5A34B8, appendChar sub_5A3880, scanTagUntil sub_5A3CE4, scanDigits sub_5A3F18, parseInt10 sub_9B111C, evalDollarTag sub_5A4148, onStyleChanged sub_5A1F28, resetFont sub_59EEE0, keyWait push sub_5A5874, lineItem destroy sub_5A1B24, dict-set byte/real/int sub_5A2160/sub_5A614C/sub_5A6020, PropSetByNum 包装 sub_5A6550, ruby 子数组 sub_5A6240。
