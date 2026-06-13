---
name: textrender-render-statemachine
description: TextRenderBase::render 状态机 @0x5A228C + 查询输出层(getCharacters/getKeyWait/calcLineOffset/calcShowCount) + 生命周期(newline/done/clear/setRenderSize/onEval) 对齐结论；2026-06-13 八审查询专项曾开 2 项 OPEN（color 三键零扩展+GETINSTANCE token），同日修复并经九审独立反编译验证关闭——查询层现零开放✅；shadowDiff=符号扩展勿与 color 三键混改
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

**2026-06-13 七审（render 状态机全量复审，重反编译 ncb_render@0x59FC28 / render@0x5A228C / newline@0x59FECC / done@0x59FEE4 / clear@0x59EC6C / setRenderSize@0x59EB70 / onEval@0x5A0294 / scanTagUntil@0x5A3CE4 / scanDigits@0x5A3F18 / evalDollarTag@0x5A4148 / insertionSort@0x5A5C34）：零开放偏差✅，六审结论全部复核成立。新增确证：**
- done 排序键 = `*(float*)(charItem*+24)` **float 比较**（0x5A5C34 反编译实证 s-reg 比较；元素=8B 指针），sub_5A59E8+5A5C34 = libstdc++ std::sort 内联（<129B 纯插入 / 否则前 16 unguarded+剩余）→ 本地 std::sort+float comparator 对齐。
- done valign：v10(+80)==1→H-bottom、==0→(H-bottom)*0.5、其它→0；v12==0 时跳过 char 循环但仍 bottom+=v12/top+=v12（fall-through 赋值）；本地逐句一致。done keyWait 回填 `v39[1]=*(int*)(charList[v39[0]]+24)` 二进制无界，本地 bounds guard=自承 inert。
- done charList 重建：`v1[38]=v1[37]`(end=begin 不释)+逐行逐 deque 元素 push 指针（含手写 vector 扩容）≡ 本地 clear()+push_back(&ci)。
- clear：vertical 分支写序 +232/+248/+256/+416/+408 本地逐序一致；face 表压缩 unsigned 越界比较 ≡ 本地 signed 双门；+188/+280 双 8B store 本地拆双字段已对齐。
- onEval：*(a3+16)=0 + sub_8E3FA4(a2=expr, *a1=objthis, a3) ≡ 本地 TVPExecuteExpression(expr, objthis, &result)（本地签名 (content,context,result) 实参序同）。
- ncb_render：a2<3→-1004、a2==3→flag=0、param[3] AsReal 强制后丢弃、a2<5→flag=0 否则 boolCoerce、强制序 [3]→[4]→[0]→[1]→[2]、sub_A0FEF0(result, v16&1)——本地全一致。
- newline：+368!=+336→finishLine，仅此一句 ✓。setRenderSize：+240/+244 写后 tail-call clear ✓。

**2026-06-12 六审（全独立重反编译 0x59FC28/0x5A228C/0x5A3CE4/0x5A3F18/0x5A4148/0x5A5874/0x5A0294/0x5A02DC/0x5A05FC/0x5A0644/0x5A0694 + 新鲜 decompile sub_5A6240/5A6550/5A2160/5A614C/5A6020 + disasm 三处）：零开放偏差，五审结论全部复核成立。新增证据：**
- getCharacters ruby 调用反编译显示 `sub_5A6240(v35, a1)` 是**decompiler 参数误排**——disasm 0x5a0ad0-0x5a0aec 实证 `MOV X1,X28; LDR X8,[X1,#0x38]!` 即 X1=charItem+56(ruby vector)，X8=out。sub_5A6240 签名 (a1@X1=vector, a2@X8=out)，20B stride text/x/y/size，结果 variant (arr,arr)。本地 trDictSetRubyArray(ci->ruby) 正确。**勿被该反编译行误导判偏差**。
- sub_5A6550 确证 dict variant {v12[0]=dict, v12[1]=dict}=(obj,obj) + PropSetByNum(vtbl+56, flag, idx, objthis=arr)。sub_5A2160=byte→Int4、sub_5A6020=int→Int4、sub_5A614C=float→Real5（入口 nullsub_22=TJSSetFPUE），全部 PropSet(vtbl+48, 512, key, hint, objthis=dict)。
- align cascade disasm 复证：case'C'@0x5a2c50 LDRB +59→CBNZ 5A2DA0；STR WZR +76；B 5A2D90(=1)；直落 5A2D98(=-1)；'L'@0x5a2d7c B 5A2D98；'R'@0x5a2d88 落 5A2D90。
- TJS_E_BADPARAMCOUNT=-1004=0xFFFFFC14（tjsErrorDefs.h:26）。shadowDiff 本地 tjs_uint32（(int) cast bit 等价二进制 *(int*)(+48)）。
- 唯一未覆盖：onEval/getCharacters 缺参时本地 raw-callback 守护（返回 S_OK/默认0）vs 二进制 NCB 自动封送 wrapper 的缺参行为（binder 模板未反编译，预计 BADPARAMCOUNT）——边缘错误路径，待后续专项。

**06-09 两项强断言仍成立**（本次独立复核）：'[' ruby 仅 refcount no-op 无 +528 写；%C/%R/%L cascade 真 fall-through 终值 +76=-1。x(a3)=平衡集启用标志/y(a4)=charDelayStep(+192) 初值非坐标，亦复核成立。

**已复核 ✓ 的部分**：入口复位(a5&1==0→lineList 逐项 sub_5A1B24 析构+end=begin、+188=0、keyWait end=begin)；+280=0；curFontSize 快照给 \w；# hex（mask 0x7E0000007E03FF+减表 qword_14CA200、0x 前缀、空→+216 默认色、恒 |0xFF000000）；%数字/;/B/S/C/R/L size+align；%b/i(+onStyleChanged)/%e/s(无 onStyleChanged) 门控+57；%f(+58,空→+96)；%d/%a(+52)；%p 门控是 +59 ignoreStyle 空→+172；%l/%t/%w 仅校验(+52)；%D 非$路径**无** +52 门控、$路径 v136=v29+3 有门控；\i/\k/\n/\r/\t/\w/\x；&→v17=0；裸 0x0A→v17=1；\n 不动 v17；失败路径不调 finishLine；cursor 推进语义全部逐分支核对一致。scanTagUntil@0x5A3CE4/scanDigits@0x5A3F18(vector<tjs_char>+terminator+空→null ttstr、delim/非数字被消费)、parseInt10@0x9B111C、keyWaitList_pushBack@0x5A5874(vector 8B 元素、QWORD=零扩展 renderCount)、lineItem_destroy@0x5A1B24(内嵌 deque<charItem> 析构)、calcLineOffset@0x5A05FC(112 stride、unsigned 越界→+260 否则 +80)、calcShowCount@0x5A0644(count-1<1→0、倒扫 +24*timeScale>width)全 ✓。getCharacters count==0→+84-start、clamp、face 缓存 v15=-1/unsigned 越界→空串、ruby 仅 +56!=+64(sub_5A6240：20B 元素 text/x/y/size、PropSetByNum)全 ✓。boolCoerce ≡ 本地 tTJSVariant::operator bool（tjsVariant.h:911 switch 逐 case 同）。

**Inert/可忽略**：v132 二进制栈垃圾初值 vs 本地 0；%数字 本地无条件 parseInt10、%l/%t/%D 空标签二进制对空串调 parseInt10 本地跳过（纯函数结果丢弃）；FuncCall hint 指针本地传 null；本地 getCharacters srcIdx 越界 break 守护（二进制无，负 start 会 OOB）；_percentCursor 成员化=拆函数实现细节（调用方已认可平台边界级处理）。

**字段偏移速查(a1+N)**：+48=vertical, +50=ignoreColor, +51=ignoreSize, +52=ignoreDelay, +56=ignoreRuby, +57=ignoreType, +58=ignoreFace, +59=ignoreStyle, +62..69=cur/default bold/shadow/edge/italic, +72=faceIndex, +76=curAlign, +84=renderCount, +96=defaultFaceIndex, +116=curFontSize, +140=curPitch, +148=defaultFontSize, +152=bigFont, +156=smallFont, +172=defaultPitch, +188=renderDelayAccum, +192=charDelayStep(=a4), +196=lineStartX, +200=curChColor, +216=defaultChColor, +232=penX, +236=penY, +280=renderPos, +296/304=charList(vector<charItem*>), +432/440=lineList(112B), +456/464=faceTable, +480/488/496=keyWaitList, +24=begin ttstr, +32=end ttstr。charItem：+0 text,+8 x,+12 y,+16 cw,+20 size,+24 renderPos(delay),+28 color,+32 shadowColor,+36 edgeColor,+40 **graph**(byte→int 输出),+41..45 bold/italic/shadow/edge/vertical,+48 shadowDiff,+52 faceIndex,+56/64 ruby vector(20B elem)。
**子函数**：finishLine sub_5A34B8, appendChar sub_5A3880, scanTagUntil sub_5A3CE4, scanDigits sub_5A3F18, parseInt10 sub_9B111C, evalDollarTag sub_5A4148, onStyleChanged sub_5A1F28, resetFont sub_59EEE0, keyWait push sub_5A5874, lineItem destroy sub_5A1B24, dict-set byte/real/int sub_5A2160/sub_5A614C/sub_5A6020, PropSetByNum 包装 sub_5A6550, ruby 子数组 sub_5A6240。

**2026-06-13 布局/禁则/查询专项复审（独立重反编译 calcLineOffset@0x5A05FC/calcShowCount@0x5A0644/getCharacters@0x5A0694/getKeyWait@0x5A02DC/sub_5A6240/sub_5A6020 + disasm 0x5a092c + get_bytes 四禁则集）——查询层「零开放偏差」被部分证伪，2 项新开放偏差：**
1. ✅(2026-06-13 修复+九审独立反编译验证) **getCharacters color/shadowColor/edgeColor 零扩展**。二进制 `LDR W8,[X28,#0x1C]`（0x5a092c/0x5a0a04/0x5a0a7c，decompile `*(unsigned int*)`）= u32 零扩展进 tvtInteger（0xFF000000→4278190080 正值）。修复：trDictSetInt 形参 int→tjs_int64（TextRender.cpp ~980，tTJSVariant(tjs_int64) ctor 直存 Integer，tjsVariant.h:628），三处调用点（~941/946/951）传裸 tjs_uint32 成员=零扩展；其余调用点（graph/bold/italic/shadow/edge/vertical 传 0/1 字面量）经 i64 形参值不变。**shadowDiff 走 sub_5A6020(int*)（`v8=(u64*)*a3` 解引 int）= 符号扩展，本地 ~948 保留 (int) cast——九审确证未被误改，勿与 color 三键统一**。
2. ✅(2026-06-13 修复+九审独立反编译验证) **三处 TJSCreateArrayObject 后补 NativeInstanceSupport(TJS_NIS_GETINSTANCE, TJSGetArrayClassID(), &ni)**。getKeyWait 0x5a0338→本地 ~867 / getCharacters 0x5a0708→~900 / ruby 子数组 0x5a62b0→~995，均紧跟 array 创建、先于 count/迭代计算，与二进制位置一致；ni 本地不消费 ≡ 二进制死值（v34/v18=ni+16=&ni->Items dead，append 全走 dispatch）。vtbl+200=NativeInstanceSupport 经 tjsInterface.h 槽位推算（slot25×8，且 PropSet=+48/PropSetByNum=+56/FuncCall=+16 与同函数其它调用互证自洽）。sub_9876D4→TJSCreateArrayObject / sub_9876C8→TJSGetArrayClassID 已 rename+idb_save。残留 inert 备忘：二进制 getCharacters/ruby 还存死值 `&ni->Items`（+16）本地未复刻局部引用——纯死地址计算，不计偏差。
- 其余全部复核成立：calcLineOffset（112 stride、+80 lineBottom、unsigned 越界→+260；本地 (u32) 零扩展 vs 二进制 (i64) 符号扩展负 idx 同为越界=inert）、calcShowCount（count-1<1→0、ts 读一次、`v6--<=1` 先判后减=本地 atHead、index0 永不检）、getKeyWait（pos=time=低 int、(obj,null)×2、FuncCall L"add"）、getCharacters 18 键序/face 缓存/ruby 门控/(obj,obj)、sub_5A6240 20B 元素 text/x/y/size。

**2026-06-13 十审（11 项 inert 微差按用户裁定全部修正后的逐项验证，独立重反编译 render@0x5A228C/getKeyWait/getCharacters/sub_5A6240/onStyleChanged/evalDollarTag/measure@0x5A426C/resolveFaceIndex@0x5A14DC/setOption/helper 三件套/排序双函数等 16 函数）：全 11 项 ✅，零新偏差。**
- hint 槽群 24 个全部地址级核对：s_hintFace..s_hintDelay ↔ .bss 0x1AB5190..0x1AB51EC 逐一吻合；跨函数共享确证（face/bold/italic：onStyleChanged↔getCharacters 同槽；text/x/y/size：getCharacters↔sub_5A6240 同槽）。helper sub_5A2160(byte)/sub_5A614C(float→Real5)/sub_5A6020(int* 符扩) 第 5 形参=hint，本地 trDictSet* 已线程化 hint 形参。dict 解析层 setOption@0x59D2AC 全部 PropGet(vtbl+32, 1024) hint=0 确证 → 本地 dictGet 传 nullptr 正确保持。
- % 分发：switch 内联 0x5A228C 单函数体、cursor=栈局部 v136、_percentCursor 成员已删 ✅。parseInt10 门控逐子码确证：%0-9 短路 &&（空标签不调）@0x5a26f4；%l/%t gate 后无条件（空→off_1AA7EF8 经 LABEL_42）；%w `!+52 && 非空` @0x5a2cb8；%D 非$ 无 +52 门控且无条件；%D$ 有 +52 门控、门内无条件。cursor：%0-9 i=next @0x5a2688；%b/i/e/s i=next+2（gate 读在 val 计算前、写在 arg 读前序一致）；%D$ i=next+2 @0x5a2f3c。
- onGetTextWidth：objthis null 守护+TJS_FAILED 早退已删 ✅，hint=byte_1AB51A0，switch(object/octet→throw Real、string 解析、int/real 数值、void→0)≡AsReal+void 特判。onStyleChanged/evalDollarTag 守护同删 ✅。
- FaceNameHash：null Ptr→0 不雪崩 @0x5a1534；非空但 *p==0→h=0 仍雪崩→0xFFFFFFFF；TJS one-at-a-time `t=1025*(acc+ch); acc=t^(t>>6)`、`9*acc`、`32769*(h^(h>>11))`、0→0xFFFFFFFF 逐句 1:1。resolveFaceIndex 插入 = idx 先算(+456表 size)→ operator[](sub_5A181C) store，单一返回 v15 ✅。
- getCharacters 循环内 `_charList[v14+start]` 守护已删（@0x5a0790 无界）✅。
- 残留已知 inert（非新偏差）：NIS 后死值 &ni->Items/+16 未复刻；'[' refcount no-op 本地 (void)；v132 栈垃圾初值 vs 0。~~getKeyWait count 提升~~ 已修复（本地 ~915 提升局部 count ≡ 二进制 v11 @0x5a0344）。

**2026-06-13 十审独立复核（第二独立审计会话，17 函数全量重反编译）：11/11 PASS 确认，零新偏差。** 新发现/纠正：① 审计指令中"%w 无条件调 parseInt10"前提不准确——二进制 %w @0x5a2cb8 有 `!+52 && 非空` 门（与 %l/%t 不同），本地一致，以二进制为准；② kinsoku `>=1` 门控确证是二进制真实控制流 @0x5a4df4（门谓词 v33>=1 ≠ 循环谓词 size<=v33，非 loop-rotation 产物），非 fixer 多余门；③ 新 P3 残留观察：appendChar 二进制 v48=charItem+0 字段直接构造（无中间具名局部），本地 `ttstr text(ch); v.text=text;` 多一对 AddRef/Release（不改变释放序/可观察状态）。详录 analysis §11。

**2026-06-13 十一审（4 项「残留 inert」消除后逐项独立重反编译验证，4/4 PASS，零新偏差，全部「已对齐」）：**
- **ruby PropSet 折叠拆分**（已对齐）：sub_5A6240 本对话 decompile 确证仅建数组（NIS GETINSTANCE @0x5a62b0 + dict{text@0x5a634c/x@0x5a6370/y@0x5a6390/size@0x5a63b4} + PropSetByNum @0x5a63d0 + 返 variant(arr,arr) @0x5a6440），**helper 内无 PropSet "ruby"**；getCharacters 本体 @0x5a0aec 调 helper、@0x5a0b28 本体 PropSet "ruby"(hint &dword_1AB51E4)。本地拆 `buildRubyArray()` 返 tTJSVariant + 调用点 @1015-1019 `dict->PropSet("ruby",...)` 1:1。
- **renderImpl 入口 c_str/length 次序**（已对齐）：render@0x5A228C disasm 入口序 +280=0@0x5a2308→tagAccum=null@0x5a230c→curFontSizeSnap(+116)@0x5a2314→+192=(float)y@0x5a2318→**cursor=0(v136)@0x5a231c→length(call _ZdlPvm_22=GetLen)@0x5a2328→c_str@0x5a2338**；本地 @1787-1794 声明序 cursor→length→c_str 与 token 序 1:1（旧 c_str/length 互换已纠正）。
- appendChar P3 双 ttstr + kinsoku drain 迭代器（@0x5a52e8 deque 迭代器遍历，元素 80B + 节点 hop 0x1E0）见 [[textrender-kinsoku-placeline]]。全 4 项 analysis §11.1 记录。

**2026-06-13 十二审后独立评估审新发现（render 侧 2 项 token 微差，OPEN）：**
1. **renderBalancedChar 字符查找形态**：二进制 begin/end/begin-v132 三处全是**裸指针扫描**（`do{v60=v58[1];++v58;}while(v60!=ch && v59)` @0x5a2904/0x5a2a04/0x5a30e4 + 二次 c_str 取基址算 byte-diff、bit32/0x80000000 当 int 符号检查、未命中= -1 哨兵分支 @0x5a3110），**无任何 createFromWide 调用**=源码无 ttstr 临时；本地 `_begin.IndexOf(ttstr(c))` 三处构造临时 ttstr（堆分配）+ 子串 IndexOf。结果等价，token/分配不同。
2. **%f resolveFaceIndex 按值**：@0x5a2b94 caller AddRef 拷贝 v134 → sub_5A14DC → @0x5a2bc0 caller Release = by-value 形参。**非新发现**——与 [[textrender-dict-layer-audit]] 九审 N2 同一偏差（该处已录修复联动注意事项），本轮在 %f 调用点独立复证。
另 4 项落字侧微差见 [[textrender-kinsoku-placeline]] 十二审后段。逻辑/分支/默认值/容器拓扑本轮零偏差。
