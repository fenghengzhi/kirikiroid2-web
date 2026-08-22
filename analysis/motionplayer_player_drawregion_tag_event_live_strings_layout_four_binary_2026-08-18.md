# Player drawRegion / tag POD / event vector / live strings 布局（V253，2026-08-18）

## 1. 结论

V252 的 `particleOutsideRect` 后，四份参考共同恢复出：

```text
tTVPComplexRect drawRegion
uint32           postDrawRegionDword_unknown   // ctor=0; no other access
bool             type3RootTransformAlreadyPropagated
bool             useD3D
padding to int32
int32            pixelateDivision
int32            layerFrameCursor             // no Player-ctor initialization
padding to double on 64-bit
double           layerCurTime                  // no Player-ctor initialization
double           layerNextTime                 // no Player-ctor initialization
vector<MotionEvent> pendingEvents
ttstr            chara
ttstr            stealthChara
ttstr            motion
ttstr            stealthMotion
tTJSVariant      canonicalResourceManager      // third independent CopyRef
```

`drawRegion`不是把 rect末端到 event vector之间全部吞掉的72/64-byte opaque object：其真实
`tTVPComplexRect` size只有40/32 bytes，后面还有32-byte POD状态区。该区首个 dword在四端完整 Player
代码簇中都只有 constructor zero一个访问；原始 private name无法从 stripped binaries恢复，因此 portable
明确保留 `_postDrawRegionDword_guess`，不把旧 `frameLastTime` 或其他猜测强行套上。

## 2. 四端布局矩阵

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| drawRegion | `+0x360` | `+0x250` | `+0x2F0` | `+0x210` |
| ComplexRect native size | `0x28` / 40 | `0x20` / 32 | `0x28` / 40 | `0x20` / 32 |
| unknown ctor-zero dword | `+0x388` | `+0x270` | `+0x318` | `+0x230` |
| type3 root marker | `+0x38C` | `+0x274` | `+0x31C` | `+0x234` |
| useD3D | `+0x38D` | `+0x275` | `+0x31D` | `+0x235` |
| pixelateDivision | `+0x390` | `+0x278` | `+0x320` | `+0x238` |
| layerFrameCursor | `+0x394` | `+0x27C` | `+0x324` | `+0x23C` |
| layerCurTime | `+0x398` | `+0x280` | `+0x328` | `+0x240` |
| layerNextTime | `+0x3A0` | `+0x288` | `+0x330` | `+0x248` |
| pendingEvents vector | `+0x3A8` | `+0x290` | `+0x338` | `+0x250` |
| chara | `+0x3C0` | `+0x29C` | `+0x350` | `+0x25C` |
| stealthChara | `+0x3C8` | `+0x2A0` | `+0x358` | `+0x260` |
| motion | `+0x3D0` | `+0x2A4` | `+0x360` | `+0x264` |
| stealthMotion | `+0x3D8` | `+0x2A8` | `+0x368` | `+0x268` |
| canonical RM Variant | `+0x3E0` | `+0x2AC` | `+0x370` | `+0x26C` |

64-bit vector header为24 bytes，32-bit为12 bytes；64-bit cursor后有4-byte double alignment padding，
32-bit位置天然让后继 double对齐到8。四端 string pair/vector末端与 canonical Variant均无未解释 gap。

## 3. tTVPComplexRect 身份与内部容器

constructor/dtor helper：

| 目标 | ctor | dtor |
| --- | ---: | ---: |
| Android arm64 | `0x7DE724` | `0x7DE88C` |
| Android armv7 | `0x61B1AC` | `0x61B2A6` |
| iOS arm64 | `0x1001FFEA8` | `0x100200048` |
| iOS armv7 | `0x1FE356` | `0x1FE464` |

四端初始化形状与 `tTVPComplexRect::Init()` 一致：

```text
Head = null
Current = null
Count = 0
Bound = {0,0,0,0}
BoundValid = false
```

64-bit对象因两8-byte pointers与末端对齐为40 bytes；32-bit为32 bytes。dtor读取 Count；非零时从
Head沿 circular `Next`链逐个删除 `tTVPRegionRect`，直到回到当前 Head。它不清后继 Player字段，
也没有内嵌 event vector。Player.clear/renderToCanvas持续复用同一 region owner。

Player ctor calls为 `0x6CC2C8 / 0x5936CE / 0x10011ED04 / 0x11D662`；normal Player dtor
calls为 `0x6CD0AC / 0x593CE4 / 0x10011F378 / 0x11DDF4`。

## 4. post-region POD 与未初始化 tag triple

首个 unknown dword仅有四端 constructor zero：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6CC5E0` | `0x5938D6` | `0x10011EF48` | `0x11D9D0` |

对各端完整 Player code cluster按相应 displacement扫描均没有第二个访问。它不是 Variant、pointer或
container owner；目前能可靠恢复的只有4-byte size、constructor zero和destructor跳过。

后继两个 byte已由独立纵切面闭合：type-3 child construction把第一个置true且 updateLayers消费；
public useD3D和D3D draw route读写第二个。两 byte后自然 padding，再是无校验、默认100的
pixelateDivision int32。

tag cursor/current/next的关键修正是：四端 Player constructor都没有写这三项。它们由 ordinary motion
初始化在 forward/rewind/reseek前发布。forward direct readers为：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6B3F60` | `0x582C24` | `0x10010AA6C` | `0x108464` |

cursor按 signed storage/wrapping int32 arithmetic使用；current/next为double。portable旧 `=0/0.0`
initializer改变了“构造后、首次 motion init前”的原始边界，本轮删除。

## 5. 单一 MotionEvent vector

constructor把 begin/end/cap全部置null；Android armv7以0x1C-byte clear、iOS armv7以重叠 vector stores
同时清 vector header与后继 live strings，这是 optimizer folding。vector元素共同为：

```text
struct MotionEvent {
    int type;
    tTJSVariant param1;
    tTJSVariant param2;
};
```

元素 stride在64-bit为44 bytes、32-bit为28 bytes。normal dtor从end向begin逆向：每项先 param2、后
param1，再释放allocation。outlined dtor为 Android armv7 `0x593B5C`、iOS arm64 `0x10012A140`、
iOS armv7 `0x128ECC`；Android arm64在Player dtor `0x6CD070..0x6CD0A8`内联同一循环。

这是唯一 persistent event vector：enqueue、child prepend/clear、live-end dispatch与destructor都引用同一
header；没有第二个 child-render/event container。

## 6. 四个 live string owners

fresh chara writer映射：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6AFDA0` | `0x580554` | `0x100107A2C` | `0x105098` |

它以 Stealth位在 chara/stealthChara中选择 comparison slot；真实变化总先CopyRef stealthChara，非
Stealth再独立CopyRef chara，然后按 stealthMotion→motion清标签并清 playing。同值路径在任何提交前返回。

playImpl成功路径则总先CopyRef stealthMotion，非Stealth再CopyRef motion。四端字段地址与上述 physical
order一致，因此不能继续采用旧portable `chara,motion,...,stealthChara,stealthMotion`分散布局。

normal destructor在更后五个 Variant释放后依次按 reverse order释放：

```text
stealthMotion -> motion -> stealthChara -> chara
-> pendingEvents -> drawRegion
```

随后跨过32-byte POD group，进入 V252 pending pair。canonical RM紧随 live strings，是 constructor参数
同一 dispatch的第三个独立 CopyRef；它不是 source-workspace前两槽的alias storage。

## 7. portable 源码修改

`cpp/plugins/motionplayer/Player.h` 已把以下连续声明移到 `_particleOutsideRect` 后：

- `_drawRegion`；
- ctor-zero、无第二访问的 `_postDrawRegionDword_guess`；
- type-3 marker、D3D mode、pixelateDivision；
- 无 member initializer 的 layer cursor/current/next；
- `_pendingEvents`；
- `_chara/_stealthChara/_motionKey/_stealthMotion`；
- canonical `_resourceManager`。

旧位置重复声明已删除。其他 late owners仍保持待证位置；没有把 canonical RM后五-Variant cluster的后四项
提前猜进本轮。行为代码未改写，变化集中于 class layout、automatic owner lifetime与过度初始化修正。

## 8. IDB 写回与 iOS armv7 安全保存

四库各写回8 comments/8 bookmarks，共32/32。semantic renames共18项：四端 ComplexRect ctor/dtor与
live chara writer、三端outlined MotionEvent vector dtor，以及 iOS armv7 forward/full-reseek/rewind
timeline functions；所有 stripped identities保留 `_guess`。

iOS armv7 different-path保存：

- V252 canonical备份：
  `out/idb-recovery/v253-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v253.i64`；
- V253 candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v253.i64`；
- candidate经独立`C:\IDA\idat.exe -A` probe，退出码0；
- old loose `id0/id1/nam`移入`pre-v253-canonical-loose/`；
- canonical替换后MCP回读全部7个新名称与V253 constructor comment；
- candidate/canonical均为377,370,832 bytes，SHA-256
  `7FDD0C44EF29F21C6B15B3820E616555AA84502F33AB6F325F630EEC0D034207`。

四份最终 V253 IDB：

| IDB | size | SHA-256 |
| --- | ---: | --- |
| Android arm64 | 366,515,675 | `A657CC8049457524677A5F1ADD27584F2704A724E474047C93E466207965DCE6` |
| Android armv7 | 345,755,972 | `D64A1EABCDE35568A1206685AF4851E2B89A0E06B919FDA0B3141C9A0DA6CE4E` |
| iOS arm64 | 334,737,266 | `33F31EF02E1042EEC23E8747FD3F7A003F86753FF031A01B099438650043538C` |
| iOS armv7 | 377,370,832 | `7FDD0C44EF29F21C6B15B3820E616555AA84502F33AB6F325F630EEC0D034207` |

## 9. 验证与 wasm 基线

- complete motionplayer Catch2 TU ordinary/headless syntax：通过，仅既有 `_tss` warning；
- Web 33-step affected build：通过；
- Wasmtime 62-step affected build：通过；
- guest 2-step build/link及 exnref转换：通过；
- Web/Wasmtime/guest三条 no-work复验：通过；
- IDA session audit：0。

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Web `index.wasm` | 85,655,362 | `0x1BD31` | `0x1A410C5` | `0x5A3E40` | `0x3185F7B` | `7136B4C19AC3DC736FAE2A812D2D684DC0E64BEF2F23DFAB844F2BA5C39ED08A` |
| Wasmtime `index.wasm` | 85,002,503 | `0x1BA50` | `0x19E9073` | `0x5A1090` | `0x3141E11` | `7B70D1CC8C2C4D0DDC1FC55692DDE4D6999C3AC17BD573398D1EFA0618B3DB71` |
| Wasmtime guest | 151,478,487 | `0x1618E` | `0x13D7DF9` | `0x4D1630` | `0x1421EBA` | `4393F9489A2B0628B3FDF24B2C3BF687D9658EA5ADDC4B1D3B83A69025D837CC` |

相对 V252，Web/Wasmtime各增8 bytes且全部来自CODE；FUNCTION/DATA/name不变。guest增59 bytes，CODE
增24 bytes，其余来自debug/custom metadata。主要变化来自自动owner析构重排、tag triple default stores
删除和 unknown dword/layout displacement。

## 10. V254 follow-up

V254 已闭合 canonical RM、find-motion context、outline、meshline、tag source五个连续 Variant owner；
constructor只CopyRef第一项，后四项Void，normal dtor严格逆序。play/context incremental commit、style properties、
tag-before-priority提交、源码迁移与IDB/build基线见
`analysis/motionplayer_player_canonical_context_outline_meshline_tag_variant_cluster_four_binary_2026-08-18.md`。
