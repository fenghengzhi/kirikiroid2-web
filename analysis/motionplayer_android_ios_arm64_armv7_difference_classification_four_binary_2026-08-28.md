# MotionPlayer Android/iOS、arm64/armv7 差异总分类四参考审计

日期：2026-08-28  
原始任务：`MP-B11`

## 1. 结论

四个参考二进制之间的差异必须先按成因分类，再决定是否进入portable common source。横向复核后，
可用以下五类完整解释当前task-local差异：

1. **平台/source条件**：原始源码本身按OS、pointer width或STL family选择不同分支/成员顺序；
2. **ABI**：pointer width、alignment、calling convention、object/vtable layout、exception模型和Thumb
   function pointer表示不同，但logical source相同；
3. **STL实现**：Android old libstdc++与iOS libc++的container/string header、node、block、bucket、
   rehash、growth、candidate和cleanup不同；
4. **编译器/链接器**：inline、tail merge、vectorize、FMA、compare-select、function outlining、cold
   cleanup和dead strip不同；
5. **不可由portable源码消除的平台运行时边界**：GPU/driver/WebGL/context/presentation，以及两个已
   确认AArch64/ARMv7 FP instruction边界。

没有发现应归为“尚未解释的task-local二进制差异”的条目。coverage中的6条`EVIDENCE_BLOCKED`均是
最终全量账本/验收任务尚未收口，不是Android/iOS差异未知；不能把它们伪装成platform boundary。

本地实现已经对真正source条件使用明确、最小的编译期分支，对ABI/STL/compiler差异保持portable
source，对不可控运行时边界保留独立disposition。本轮无需production修改。

## 2. 本轮 fresh 四端证据

本轮使用原生`mcp__idalib__*`对64个差异归因代表范围重新执行decompile、完整disassembly、
strings/constants/callees和`xrefs_to/from`。所有decompile成功，所有disassembly未截断。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | `xrefs_from` | IDB 更新 |
|---|---:|---:|---:|---:|---|
| Android arm64 | 16 | 6,634 | 94 | 17 | 16条分类注释、1个书签 |
| Android armv7 | 16 | 3,404 | 32 | 17 | 16条分类注释、1个书签 |
| iOS arm64 | 16 | 3,068 | 89 | 17 | 16条分类注释、1个书签 |
| iOS armv7 | 16 | 4,980 | 86 | 16 | 16条分类注释、1个书签 |
| 合计 | 64 | 18,086 | 301 | 67 | 64条注释、4个书签；四库原位保存 |

部分instruction target位于函数内部的FP instruction或merged entry，`analyze_batch`解析到完整containing
function；额外from xref来自这些internal entry，不代表额外source function。

## 3. 四端代表映射

| 差异archetype | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player ctor/layout | `0x6CC110`，593 | `0x5935C4`，281 | `0x10011EC04`，226 | `0x11D488`，499 |
| Engine ctor/layout | `0x67B76C`，848 | `0x560948`，304 | `0x1001B7FB0`，187 | `0x1B7788`，318 |
| Resource container ctor | `0x6A5CAC`，177 | `0x57B1EC`，63 | `0x100101158`，44 | `0xFE254`，93 |
| SLA two-tree ctor | `0x6C3DB4`，92 | `0x58DBDC`，67 | `0x1001298C4`，50 | `0x128890`，101 |
| manager vector add | `0xA72C14`，67 | `0x7970B0`，34 | `0x1002DC360`，22 | `0x2DBD28`，21 |
| TJS Array deque grow | `0x6DFC90`，47 | `0x5A099C`，40 | `0x1000FAED8`，47 | `0xF7F90`，54 |
| timeline unordered insert | `0x685060`，75 | `0x5669AC`，60 | `0x1001A6938`，148 | `0x1A6074`，237 |
| SourceCache list push/copy | `0x6E8040`，47 | `0x5A67DE`，33 | `0x100100E54`，38 | `0xFDFB0`，91 |
| cursor lower clamp | `0x6D6AC4` in 16-body | `0x598F18` in 21-body | `0x1001255FC` in 16-body | `0x12480C` in 20-body |
| skipToSync clamp | `0x6D0B84` in 251-body | `0x595DA6` in 180-body | `0x100121C40` in 129-body | `0x1209BA` in 202-body |
| setWind width branch | `0x66DD8C`，84 | `0x559900`，114 | `0x1001AC718`，99 | `0x1ABF24`，120 |
| KRKR atlas record/source | `0x6931C8`，1,997 | `0x570F54`，1,014 | `0x1000F4098`，1,136 | `0xF0BE4`，1,667 |
| D3D static guard/EH | `0x6AB08C`，84 | `0x57D184`，100 | `0x100104130`，67 | `0x10149C`，130 |
| plugin autoload OS rule | `0x907618`，740 | `0x6C77BC`，153 | `0x1003F1CD4`，139 | `0x3D923C`，326 |
| calcView EH/ABI | `0x6CE908`，1,349 | `0x594958`，798 | `0x1001201CC`，613 | `0x11EED4`，977 |
| Bezier compiler/ISA lowering | `0x69DE30`，167 | `0x576C7C`，142 | `0x1000FB4A8`，107 | `0xF854C`，124 |

## 4. 真正的平台/source条件

### 4.1 pointer-width setWind

两个64位目标和两个32位目标分别一致，stop predicate是实质业务分叉：

```text
64-bit: amp==0 || normalizedMax==normalizedMin || (freqX==0 && freqY==0)
32-bit: amp==0 || freqX==0
```

这不是optimizer把同一逻辑化简成不同汇编；collapsed endpoints与`freqX=0,freqY!=0`的结果真实不同。
本地用`sizeof(void*)`编译期分支复刻，不能选择其中一端冒充四端共同结果。

### 4.2 KRKR atlas record member order

Android old-libstdc++目标的record owner顺序是`PSBRawNode -> rect/tail -> std::string`；iOS libc++是
`std::string -> rect/tail -> PSBRawNode`。vector grow copy和reverse destruction顺序随之改变，故不是
padding。本地以`_LIBCPP_VERSION`保留唯一有证据的STL-family source member-order分支。

### 4.3 plugin autoload OS名字规则

Android发现`.tpm`后保留原filename；iOS去掉末4 bytes并追加`.dll`。这是OS source条件，不是
libc++ string ABI。三目录扫描、Name-only sort、count publication和完整Path/Name交给loader仍共享。

### 4.4 平台后端适配

storage、Window/Layer、render manager和Web/Cocos接口必然由平台层实现；只有G23报告列出的GPU末位、
WebGL resource/context loss和Cocos到physical screen呈现属于不可消除边界。正常CPU dataflow、draw
products和GL capability不能因此降级。

## 5. ABI差异

### 5.1 pointer width与alignment

LP64/ILP32导致pointer、size_t、Variant、container header和raw owner宽度不同。32位Android与iOS还可
因double alignment规则产生不同record offset。实例：Player/Engine/ResourceManager object size分别为
不同四元组，但constructor logical member order和destructor reverse owner closure相同。

portable source只声明真实members/containers，不填充原生padding，也不要求Web `sizeof(Player)`匹配
任一reference。

### 5.2 calling convention与function pointer

AArch64寄存器/stack ABI与ARMv7不同；armv7 Thumb function pointer低bit和IDA code xref识别也不同。
sret、aggregate parameters、double alignment和virtual thunk形状可导致wrapper/internal entry数量不同，
不能从地址/size差异推断API缺失。

### 5.3 exception ABI

- Android arm64：DWARF/main-body landing pads；
- Android armv7：EHABI/extab，有时没有独立可见cleanup body；
- iOS arm64：Mach-O LSDA和cold cleanup；
- iOS armv7：SjLj call-site state machine。

相同RAII owner可产生完全不同cleanup函数/xref。必须结合normal dtor、pending owner和另外三端，不能把
“没看到显式landing”分类为source不清理。

## 6. STL差异

### 6.1 vector

四端均为contiguous`[begin,end,cap)`；growth factor、copy/move helper和cold cleanup不同。Manager Add
仍共同为push pointer后AddRef。iterator invalidation来自共同vector source，而不是平台业务branch。

### 6.2 deque

Android old libstdc++使用约512-byte element blocks和start/finish iterator；iOS libc++使用约4096-byte
blocks、pointer-map split buffer与absolute start/size。TJS Variant每block数量因此是25/42对204/341，
但logical `std::deque<Variant>`、元素顺序和owners一致。

allocation-failure时Android可先改变map capacity再失败，iOS常用temporary split-buffer后swap；这种
私有capacity状态没有一个portable业务条件可同时复制，source继续用`std::deque`。

### 6.3 list/tree/unordered

- list：Android sentinel不维护cached size，iOS维护；Remove可scan-delete或temporary splice；
- ordered tree：header/node布局不同，但in-order、unique/multi、swap/erase语义相同；
- unordered：prime-like与power-of-two bucket policy、node field顺序、candidate-first/find-first、empty
  bucket初始化与rehash策略不同；hash/equality/logical nodes相同。

unordered physical iteration order本来就不跨STL稳定。本地不人为排序，也不硬编码任何reference bucket。

### 6.4 string

Android旧libstdc++与iOS libc++的`std::string`表示、SSO/COW-era owner和move/dtor不同；logical narrow
keys仍是`std::string`。ttstr/TJS UTF-16 key是另一层，不能从STL narrow string差异推导case/hash变化。

## 7. 编译器与链接器差异

### 7.1 inline、outlining与tail merge

同一helper可能在Android arm64内联、其余三端保留函数，或被tail merged进相邻callback。Hex-Rays函数
size/名称不等于source function数量。shared Layer factory、Engine progress、assign secondary entry和
getCommandList forward均是已处理实例。

### 7.2 vectorization、FMA与compare-select

Bezier/mesh/geometry在不同ISA可能scalar、NEON vectorized、FMA或non-FMA。若source association/order可
由四端恢复，本地显式保持括号和阶段；不能只按某一端机器instruction重写整个算法。

某些exact machine结果仍属ISA boundary：cursor与skip lower clamp在两套AArch64为`FMAX`，两套ARMv7
为`VCMPE`+conditional move。普通finite值一致，NaN payload/signed-zero最低层可能不同。因此coverage
保留两条独立`PLATFORM_BOUNDARY`，不假装四端bit-exact，也不扭曲common source。

### 7.3 guard、dead strip与symbol disposition

D3D function-local static guard在三端有显式abort cleanup、Android armv7当前镜像没有独立landing；
Android保留`releaseTargetTexture`叶helper而iOS dead-strip/inline。共同static state machine与member
仍存在，symbol缺失不是source feature缺失。

## 8. 不可避免运行时边界

当前coverage只有三条非final-audit `PLATFORM_BOUNDARY`：

1. `MP-B11-PLAYER-CURSOR-FP`；
2. `MP-B11-PLAYER-SKIP-FP`；
3. `MP-G23-WEB-COCOS-REFERENCE-RENDER-PLATFORM-BOUNDARIES`。

前两条是ISA/FP machine边界；第三条只允许GPU/driver末位、WebGL resource/context/failure model和
post-Cocos physical presentation差异。它们不授权放宽ordinary finite arithmetic、container/state、
draw-call顺序或target framebuffer语义。

libm transcendental末位、GPU shader transcendental和driver raster/filter也按调用点记录为runtime
platform结果；source函数、参数、publication和branch仍须一致。

## 9. unknown与evidence-blocked分离

本轮在task-local差异表中没有留下`UNKNOWN`。这不等于最终工程已经验收：coverage另有6条
`EVIDENCE_BLOCKED`，对应`MP-F03..MP-F08`的完整非NCB函数分母、对象/owner总账、报告索引、实现测试
映射、最终gap集合和acceptance。这些需要后续final-audit重建，不能被B11“差异已分类”提前关闭。

同样，未运行build/runtime是verification gap，不是未知二进制差异或platform boundary。

## 10. 本地对照

| 分类 | 本地策略 |
|---|---|
| pointer-width真实branch | `PlayerCore.cpp` setWind按`sizeof(void*)`表达 |
| STL-family真实member-order | `PlayerResource.cpp`仅对atlas record用`_LIBCPP_VERSION` |
| iOS/Android autoload name | `PluginImpl.cpp`按平台改写suffix |
| ABI布局 | portable members/types，不伪造native padding |
| STL container | vector/deque/list/map/multimap/set/unordered原生source类型 |
| EH | RAII/source order，不手写某端landing state machine |
| FP ISA boundary | common source+独立coverage disposition，不全局sanitize |
| Web render boundary | G23列出的三类，不删除可表达的render语义 |

`rg`与coverage复核没有发现第二个无证据的STL-family业务分叉，也没有用“Web不同”“oracle看不到”
给普通路径打platform标签。

## 11. 最终判定

`MP-B11`没有剩余task-local静态差异或未解释difference。64个fresh范围、已有STL/owner/FP/render报告和
完整coverage disposition共同形成分类账本；64条IDA分类注释、4个书签已写入并保存到四库。

正式cross-platform runtime trace、Web Debug、GPU framebuffer与final acceptance仍属于`MP-V`和
`MP-F03..F08`，不由本任务冒领。
