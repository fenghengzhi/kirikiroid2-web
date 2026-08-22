# MotionPlayer Player complete instance-layout ledger（四参考，2026-08-18）

## 结论

V248--V257 已分别闭合 `motion::Player` 从对象首地址到最终 raw dispatch slot 的十段连续
区域。本轮不再把单段结论孤立使用，而是重新打开四份参考、重新检查完整 constructor、normal
destructor 和 Engine 侧精确 allocation，把十段边界拼成一份覆盖 `+0x000..sizeof(Player)` 的
四 ABI 总账；同时用当前 `Player.h` 生成 ordinary/headless 两份 Clang record-layout dump。

结论是：当前源码的 native member region 有 **111 个顶层实例字段**，声明顺序与四参考共同的
constructor/destructor/consumer 证据一致。十段在四端都首尾相接并精确等于 allocation size，
没有未解释洞、重复成员或插在 native region 内的 Web/headless 私有字段。现阶段唯一“物理位置
确定、原始私有拼写/业务语义仍未知”的成员是 `_postDrawRegionDword_guess`；它已经由四端共同的
constructor zero store 和完整 Player code-cluster 无其他访问所约束，不是布局缺口。

最终 source-level 尾部为：

```cpp
double meshDivisionRatio;
PerNodeLayerStateMap perNodeLayerStateMap;
VariableSnapshotMap variableSnapshotMap;
VariableLabelScopeDeque variableLabelScopes;
iTJSDispatch2 *tailDispatchLoadMotionResidual_guess; // no initializer
// end native Player
```

其中最后一个 pointer 在四个 constructors 中故意不初始化、在四个 destructors 中没有 owner
动作；Android 保留的零-xref residual consumer 与 live `rootPlayer->currentDispatch` 路径仍严格
分离。完整尾部语义见 V257 报告，本轮没有改变该行为。

## 1. 十段连续边界

所有区间均为左闭右开。最后一列是 source-level 顶层字段数，不把 STL/Variant 内部 word、
natural padding 或数组元素误计为独立 Player 字段。

| # | 连续语义区 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | 字段数 |
|---:|---|---:|---:|---:|---:|---:|
| 1 | prefix、node label tree、camera/bounds、node deque | `[0x000,0x108)` | `[0x000,0x0C0)` | `[0x000,0x0D0)` | `[0x000,0x0A0)` | 20 |
| 2 | HM1/HM2、selected parameter、vector、ramp tree | `[0x108,0x1C8)` | `[0x0C0,0x120)` | `[0x0D0,0x158)` | `[0x0A0,0x0E4)` | 5 |
| 3 | post-ramp frame/type-1/root state | `[0x1C8,0x27C)` | `[0x120,0x1AC)` | `[0x158,0x20C)` | `[0x0E4,0x16C)` | 24 |
| 4 | six-Variant source workspace、raw SLA adaptor | `[0x27C,0x300)` | `[0x1AC,0x1F8)` | `[0x20C,0x290)` | `[0x16C,0x1B8)` | 7 |
| 5 | pending strings、camera velocity、affine、outside rect | `[0x300,0x360)` | `[0x1F8,0x250)` | `[0x290,0x2F0)` | `[0x1B8,0x210)` | 12 |
| 6 | draw region、tag cursor/event/live strings、canonical RM | `[0x360,0x3E0)` | `[0x250,0x2AC)` | `[0x2F0,0x370)` | `[0x210,0x26C)` | 14 |
| 7 | context、outline、meshline、tag Variant owners | `[0x3E0,0x444)` | `[0x2AC,0x2E8)` | `[0x370,0x3D4)` | `[0x26C,0x2A8)` | 4 |
| 8 | nine Boolean/control scalar cluster | `[0x444,0x4A0)` | `[0x2E8,0x348)` | `[0x3D4,0x430)` | `[0x2A8,0x304)` | 21 |
| 9 | HM3/HM4/variable-track deque | `[0x4A0,0x560)` | `[0x348,0x3A8)` | `[0x430,0x4B0)` | `[0x304,0x344)` | 3 |
| 10 | final raw dispatch / object end | `[0x560,0x568)` | `[0x3A8,0x3B0)` | `[0x4B0,0x4B8)` | `[0x344,0x348)` | 1 |
| | **总计** | **`0x568`** | **`0x3B0`** | **`0x4B8`** | **`0x348`** | **111** |

边界恒等式逐端成立：上一段 end 与下一段 begin 完全相同，最后一段 end 与 Engine 的
`operator new` size 完全相同。不存在需要用匿名 byte array、保留槽或 port-only pointer 填补的
间隙。

## 2. 111 字段的规范声明分组

当前 `Player.h` 的 native declaration order 可压缩为下列十组。括号内数量与上表逐段相加：

1. **prefix / node / camera / bounds（20）**：`rootPlayer`、`parentPlayer`、
   `currentDispatch`、node-label tree、九个 camera/stereovision doubles、两个 camera-offset
   floats、四个 bounds doubles、node deque。
2. **post-node containers（5）**：HM1 eval-cascade map、HM2 raw-double result map、selected
   parameter raw alias、parameter vector、parameter-ramp multimap。
3. **frame/type-1/root（24）**：三个 post-ramp doubles、四个 frame-state bools、division
   Variant、故意 indeterminate motion index、motion-list/content Variants、priority Variant、
   root cursor/time pair、delta/damping、六个 render/control bools、root-content Variant。
4. **source workspace（7）**：find-source RM、SourceCache object、descriptor、internal layer、
   colors、work layer 六个 Variants，以及手工管理的 `SeparateLayerAdaptor *`。
5. **pending/velocity/affine/rect（12）**：两个 pending ttstr、三个 velocity doubles、四个
   affine linear doubles、两个 affine translation floats、四-float outside rect array。
6. **draw/tag/event/live strings（14）**：complex draw region、未知 dword、两个 bool、
   pixelate division、tag cursor/two times、event vector、四个 live ttstr、canonical RM Variant。
7. **late Variant owners（4）**：find-motion context、outline、meshline、tag-frame source。
8. **scalar/control（21）**：九个连续 bool、FOV/zFactor/frameTick/lastTime/loopTime 五个
   doubles、completion/mask/count/color 四个 dwords、outside/speed/mesh 三个 doubles。
9. **late containers（3）**：HM3 per-node layer-state map、HM4 variable snapshot map、variable
   label-scope deque。
10. **tail（1）**：故意 indeterminate、raw/non-owning 的 residual dispatch pointer。

这份顺序还固定了几个容易被旧注释误合并的边界：

- `lastTime/loopTime` 不属于最早的三个 post-ramp doubles；
- tag stream 与较早的 root/priority stream 是分离的 persistent owners；
- canonical ResourceManager Variant 是第三份 constructor CopyRef，不是 native RM by-value 或
  `_sourceCacheNative` fast pointer；
- HM3/HM4/deque 在 mesh division 后，最终 raw dispatch 又在 deque 后；
- headless render snapshot map 不属于四参考的 native Player。

## 3. padding 与 ABI 差异

总账中所有空 byte 都可由字段类型的自然对齐或 STL ABI 解释；它们不代表匿名成员。

- Android armv7 的最终 pointer 位于 `+0x3A8..+0x3AB`，只有
  `+0x3AC..+0x3AF` 是 class 尾部 8-byte alignment padding。
- Android arm64、iOS arm64 和 iOS armv7 的最后 pointer 都直接抵达对象 end，没有额外 tail
  padding。
- Android 使用旧 libstdc++ container layouts；iOS 使用 libc++ layouts，因此同一 source-level
  map/deque 声明具有不同 header size。HM3/HM4 的 Android default construction出现 hint 10 / 
  eager 11 buckets，而 iOS 保持 lazy bucketless；这仍是默认构造差异，不是 source-level
  `reserve(10)`。
- 普通 Web32 与 iOS armv7 采用相同的 32-bit pointer width，但 Web32 的 double/class alignment
  使对象大 `0x10`：ramp map 后第一个 double 前 `+4`、root-frame cursor 后 double pair 前
  `+4`、九 bool 后 cameraFov 前 `+4`、最后 pointer 后 class tail `+4`。

最后一点由本地 Clang record-layout dump 直接验证，不能拿 Web32 的 `sizeof` 反推四个 native
STL 实现之一。

## 4. constructor、destructor 与 allocation 三重闭合

本轮在四份 recovery IDB 中重新从完整 Player constructor 起点向后检查全部 member construction
和 POD stores，并从 normal destructor 起点反向核对所有 nontrivial teardown；另外回到 owning
Engine 的精确 allocation site 核对立即数：

| 目标 | Player constructor | Player destructor | Engine allocation | exact size |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6CC110` | `0x6CCEBC` | `0x67BA1C` | `0x568` |
| Android armv7 | `0x5935C4` | `0x593C24` | `0x560AC4` | `0x3B0` |
| iOS arm64 | `0x10011EC04` | `0x10011F2A0` | `0x1001B803C` | `0x4B8` |
| iOS armv7 | `0x11D488` | `0x11DCC4` | `0x1B7850` | `0x348` |

三种证据互相约束：

- constructor 的 nontrivial construction/stores 从 prefix 一直抵达 variable deque，之后不写
  final raw pointer；
- normal destructor 按声明逆序销毁 late deque/HM4/HM3、Variants/strings/vector/region、source
  workspace/adaptor、root/type-1 owners、parameter containers、node deque/tree；POD 与最终 raw
  pointer没有虚构的 owner cleanup；
- allocation size 精确抵达第十段 end，不给附加 hidden sidecar 或 port-only cache 留空间；
- constructor unwind 只清理已经成功构造的 nontrivial prefix，和 normal destructor 的声明逆序
  一致，未发现跨段重复 owner。

## 5. 本地 record-layout：ordinary 与 headless 隔离

对完整 `motionplayer-dll.cpp` 参数集增加 `-Xclang -fdump-record-layouts` 后，ordinary Web32
记录得到：

```text
top-level native instance fields: 111
final native pointer:             +848 (0x350)
sizeof(Player):                   856 (0x358)
dsize(Player):                    852 (0x354)
align(Player):                    8
```

字段出现顺序逐项与上面的 111-field ledger 相同。ordinary 版本的最终 pointer 后只有 4-byte
class tail padding，没有 production/headless field 插入 native region。

同一 dump 在 `KRKR2_WASMTIME_HEADLESS=1` 下得到：

```text
final native pointer:             +848
_renderLayerStates:               +852
_nextLayerAbsolute:               +872
sizeof(Player):                   880
dsize(Player):                    876
align(Player):                    8
```

因此 headless-only fields 严格位于 native tail pointer **之后**；第一个 headless field 合法复用
ordinary class 的 tail padding。静态 differential fields 不影响任何 Player instance layout。
这也解释了为什么“只比较最终 sizeof”不足以证明隔离，必须同时检查字段 offset 与 dsize。

## 6. 源码与注释清理

V258 没有新增或重排字段，也没有改变运行时代码。唯一源码改动是把 final pointer 后一段已经
漂移、重复描述 source workspace 的旧注释删除，替换为：

```cpp
// === end native Player instance layout ===
```

这样 headless-only fields 的边界不再被过时的“motion/source state”说明混淆。完整 native 区域
仍由 V248--V257 的四参考证据恢复；旧 `libkrkr2.so` 绝对地址或已失效的 `_sourceCacheNative`
解释没有重新进入编译源码注释。

十段原始纵切面记录：

- `motionplayer_player_prefix_currentdispatch_nodelabel_camera_bounds_deque_layout_four_binary_2026-08-18.md`
- `motionplayer_player_post_node_hm1_hm2_parameter_container_layout_four_binary_2026-08-18.md`
- `motionplayer_player_post_ramp_frame_core_type1_root_owner_layout_four_binary_2026-08-18.md`
- `motionplayer_player_source_workspace_raw_adaptor_layout_four_binary_2026-08-18.md`
- `motionplayer_player_pending_velocity_affine_rect_contiguous_layout_four_binary_2026-08-18.md`
- `motionplayer_player_drawregion_tag_event_live_strings_layout_four_binary_2026-08-18.md`
- `motionplayer_player_canonical_context_outline_meshline_tag_variant_cluster_four_binary_2026-08-18.md`
- `motionplayer_player_tag_end_scalar_control_cluster_four_binary_2026-08-18.md`
- `motionplayer_player_hm3_hm4_variable_deque_contiguous_layout_four_binary_2026-08-18.md`
- `motionplayer_player_final_tail_dispatch_residual_four_binary_2026-08-18.md`

## 7. recovery IDB 写回

四库各追加三处 V258 ledger 注释和三枚 bookmark，共 **12 comments / 12 bookmarks / 0
renames**：

1. Player constructor 起点：完整十段 boundary array；
2. Player destructor 起点：完整 reverse-lifetime ledger；
3. owning Engine allocation：ABI-specific exact `operator new` size。

iOS armv7 继续使用 different-path 安全保存：

- pre-V258 backup：
  `out/idb-recovery/v258-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v258.i64`，
  377,592,016 bytes，SHA-256
  `4185719910DB9B1FB7047B12B2E6E322A54D39366779874EDD56DED735E24AEF`；
- candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v258.i64`；
- `C:\IDA\idat.exe -A` probe 退出 0；
- canonical loose files 移入 `pre-v258-canonical-loose/`，MCP readback loose files 移入
  `verify-readback-loose/`，没有删除；
- candidate 替换 canonical 后重新打开并回读三处 V258 comments，再关闭。

最终 IDB：

| IDB | size | SHA-256 |
|---|---:|---|
| Android arm64 | 366,728,756 | `595853A85AB1E8E0C0D69DC82146C2E03866CD4245834186AFFC5B13C7ACD568` |
| Android armv7 | 345,870,749 | `B54F0DF1103CBC1D9F59541820C10ED17D93668D33A017ED588944010504218B` |
| iOS arm64 | 334,884,807 | `7D6FE46E2516EE09F094A10CAA4AA2DCC39CE8BB0168C7425367B2D80FD9B055` |
| iOS armv7 | 377,632,976 | `E5AE32E4FD0FA101D9A4B7DBD3786C44A6344836F0F2C43AE0A4024AEBBC8350` |

最终 IDA MCP session 数为 0。

## 8. 验证与 Wasm 基线

实际完成：

- 完整 `motionplayer-dll.cpp` ordinary Web syntax-only：通过；
- 同一完整 TU 加 `KRKR2_WASMTIME_HEADLESS=1`：通过；
- 两种语法模式另以串行方式重跑，均通过；唯一诊断仍是仓库既有 `_tss` deprecated spacing
  warning；
- Web Debug：33-step rebuild/link 通过；
- Wasmtime Headless Debug：62-step rebuild/link 通过；
- `krkr2_wasmtime_guest`：2-step rebuild/link/exnref conversion 通过；
- 三目标随后串行复核均为 `ninja: no work to do`；
- `git diff --check` 退出 0，无 whitespace error，仅工作树既有 LF/CRLF warning；
- IDA MCP session 数为 0。

最终产物：

| wasm | size | FUNCTION | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Web `index.wasm` | 85,655,322 | `0x1BD31` | `0x1A4109D` | `0x5A3E40` | `0x3185F7B` | `86B8A97B03BCF141509E225CB2FE4DAB1EB6CD766AA0C2354181A326CFBBEDA9` |
| Wasmtime `index.wasm` | 85,002,463 | `0x1BA50` | `0x19E904B` | `0x5A1090` | `0x3141E11` | `7D05FBF6BBAECE99BC8F231D53FDA89AB5BE7E209BE6DEB6FCDCEFA05C1EC37F` |
| guest | 151,479,103 | `0x1618E` | `0x13D7DCD` | `0x4D1630` | `0x1421EBA` | `F4E8EB881323F89DE444A2E74F6E3E8A98C470A21A6AF1BAF4CD96348DBE286C` |

两份主 wasm 的 size、列出 sections 和 SHA-256 均与 V257 完全相同。guest 的 size 与四个列出
sections 也完全相同；仅 SHA-256 改变，因为 comment-only header edit移动了 DWARF/source line
metadata。没有 executable FUNCTION/CODE 变化，不能把 guest hash 改变解释为 native layout 或
运行时行为变化。

## 9. 本轮闭合与下一审计面

V258 闭合的是 Player **声明布局与对象边界**，不是整个 motionplayer 已经一比一完成。下一步
应在这份 111-field 固定账本上审计完整 constructor initialization coverage：逐字段区分 STL/
owner 自动构造、显式默认值、constructor-body 延迟提交、故意 indeterminate POD，以及只在 child
construction 后覆盖的 raw aliases；四端共同验证后再决定是否需要源码修正。这样可以避免把
“字段位置已恢复”误等同于“初始状态和异常回滚也已恢复”。
