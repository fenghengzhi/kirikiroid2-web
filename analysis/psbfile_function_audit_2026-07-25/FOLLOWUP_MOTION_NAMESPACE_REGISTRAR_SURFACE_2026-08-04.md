# Motion namespace 注册器 / Player 所有权闭环（2026-08-04）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

在关闭 `emoteplayer.dll` 独立入口后，继续 fresh 反编译
`motionplayer_ncb_register@0x6D9B08` 与 `Motion_Player_ncb_register@0x6FDD04`，确认本地还留有
四项注册架构偏差：

- Motion 只注册了 21 个常量，缺少 `MaskModeStencil` / `MaskModeAlpha`；
- `Player` 先作为顶层 class 注册，再由 post callback alias 到 `Motion.Player`；
- 两个 namespace function 在独立 deferred 路径中重新查找 Motion，而非主注册器内顺序写入；
- post callback 用字典覆盖 `Player` 表内已注册的 `useD3D` 属性描述符。
- 同一 post callback 还创建两个二进制不存在的 `ShortCutInitial*KeyMap` 字典，且空的
  pre/unregister callback 也各自生成额外 auto-register node。

本轮已把 Motion 主表恢复为二进制的单一顺序：**23 constants → 11 subclasses → 2
namespace functions**。`Player` 现在只作为第六个 `Motion.Player` subclass 注册；顶层
`global.Player`、post alias、deferred function registrar 和 `useD3D` 后置覆盖均已删除。
Fresh 复核 `motionplayer_static_init` 后，整个本地 motionplayer pre/post/unregister callback
段也已删除；独立 `emoteplayer.dll` callback 不受影响。

## fresh 反编译证据

本轮 fresh decompile：

- `motionplayer_ncb_register@0x6D9B08..0x6DA28C`；
- `Motion_Player_ncb_register@0x6FDD04`；
- `Player_ncb_registerMembers@0x6D69C8`。
- `motionplayer_static_init@0x42EE18..0x42EF6C`。

关键逻辑压缩为九行：

```text
register 23 constants on Motion, ending MaskModeStencil=0 and MaskModeAlpha=1
register Point, Circle, Rect, Quad, LayerGetter
register Player by Motion_Player_ncb_register
register SourceCache, ObjSource, ResourceManager, SeparateLayerAdaptor, D3DAdaptor
create descriptor whose callback is Motion_doAlphaMaskOperation
add member "doAlphaMaskOperation" to the in-hand Motion dispatch
create descriptor whose callback is Motion_getD3DAvailable
tail-add member "getD3DAvailable" to the same Motion dispatch
Motion_Player_ncb_register initializes/registers Player only; it never publishes global.Player
```

`Player_ncb_registerMembers` 又独立证明 `useD3D` 已在成员表中以
`Player_getUseD3D` / `Player_setUseD3D` 读写 Property 注册；二进制没有把它替换成字典的
post-registration 路径。

`motionplayer_static_init` 只构造 LayerMeshSupport/Layer attachment、Motion class registrar
及共享静态容器/OwnerFilter，没有自定义 init/post/unregister callback 指针。完整 ELF 中
`ShortCutInitialPadKeyMap` 与 `ShortCutInitialGamePadKeyMap` 的 UTF-16LE 全字面量也均为零命中。

## 精确注册顺序

### 23 个常量

```text
LayerTypeObj, LayerTypeShape, LayerTypeLayout, LayerTypeMotion,
LayerTypeParticle, LayerTypeCamera,
ShapeTypePoint, ShapeTypeCircle, ShapeTypeRect, ShapeTypeQuad,
PlayFlagForce, PlayFlagChain, PlayFlagAsCan, PlayFlagJoin, PlayFlagStealth,
TransformOrderFlip, TransformOrderSlant, TransformOrderZoom, TransformOrderAngle,
CoordinateRecutangularXY, CoordinateRecutangularXZ,
MaskModeStencil, MaskModeAlpha
```

值依次为 `0..5`、`0..3`、`1/2/4/8/16`、`0/3/2/1`、`0/1`、`0/1`，全部以
`0x10000` static-member flag 注册。

### 11 个 subclass

```text
Point -> Circle -> Rect -> Quad -> LayerGetter -> Player -> SourceCache ->
ObjSource -> ResourceManager -> SeparateLayerAdaptor -> D3DAdaptor
```

`Player@0x6D9F2C` 是第六个 row。Motion registrar 内没有 EmotePlayer；后者仍只由独立
`emoteplayer.dll` entry 挂接。

### 2 个 namespace function

- `0x6DA1B0/BC` 物化 `Motion_doAlphaMaskOperation@0x6AF104`，`0x6DA1F0` 写入成员；
- `0x6DA218/24` 物化 `Motion_getD3DAvailable@0x6B0960`，`0x6DA260` tail 写入成员。

两者都使用注册器手里的同一 Motion dispatch，顺序位于 D3DAdaptor 之后。

## 本地逐行对照

| Android 数据流 | 本地实现 | 对照 |
| --- | --- | --- |
| 23 constants first | `NCB_REGISTER_CLASS(Motion)` 的 23 个 `Variant` | 新增两个 MaskMode 常量并恢复二进制顺序 |
| Player 是第六个 subclass | `NCB_REGISTER_SUBCLASS(Player)` + 第六条 `NCB_SUBCLASS(Player, Player)` | 删除顶层 class + post alias |
| 后续五个 subclass | 同一 Motion block 内 SourceCache 至 D3DAdaptor | 顺序与 11 条 call edge 一致 |
| 两个 free function 紧随其后 | 同一 Motion block 内两个 `Method(...)` | 删除独立 deferred registrar 与 Motion re-lookup |
| `useD3D` 是表内 RW Property | `NCB_PROPERTY(useD3D, getUseD3D, setUseD3D)` | 删除字典 marker 的 `PropSet` 覆盖 |
| 没有 `global.Player` | 单测先断言顶层查找失败，再断言 `Motion.Player` 为 Object | 固定 owner 边界 |
| motionplayer 无自定义 callback | 不再声明三个 `NCB_*_REGIST_CALLBACK` | 删除两个 keymap 字典表达式与三只额外 node |

## 机械门禁

`verify_elf_surface.py` 现输出：

```text
emote_registration_surface=true fdes=8 manifest_fdes=0 external_fdes=8 utf16=7 forbidden_motion_callback_literals=0 materializations=3 entry_materializations=1 setter_materializations=2 direct_edges=11 motion_constants=23 motion_subclasses=11 motion_function_materializations=2 motion_functions=2 forbidden_motion_hits=0 semantic_words=52 byte_ranges=5 range_bytes=3404 single_entry=true setters_in_entry=true sha256=true
```

在原有 emote entry 门禁外，新增精确验证：

1. 23 个常量调用地址、BL word 与共同 target `ncb_addConstant_wrapper@0x6DA28C`；
2. registrar 内到该 wrapper 的调用集合恰好为 23 条；
3. 两个 namespace callback 的 ADRP/ADD 地址、word 与解析 target；
4. `doAlphaMaskOperation` 的普通 BL member-add 与 `getD3DAvailable` 的 tail member-add；
5. 11 个 subclass row 及 EmotePlayer direct/name 引用为零；
6. `motionplayer_ncb_register` 完整 FDE hash。
7. `motionplayer_static_init` 完整 FDE hash，以及两个 keymap UTF-16 literal 的全 ELF 零命中。

扩展后的 canonical surface 为 10,062 bytes，SHA-256 为
`646464a8fc8db91f853a2de15df7105b66611645344ba23ec6e8b5adf3f46a9d`。

## 验证

- macOS Debug `motionplayer-dll`：21/21 test cases、1555/1555 assertions；
- 单测确认 `global.Player` 不存在、`Motion.Player` 存在、raw `useD3D` descriptor 为 Object；
- 两个 MaskMode 常量与两个 namespace function 均存在；
- Web Debug 完整链接成功；
- Wasmtime 完整 guest smoke 采集 `m2logo=25`、`yuzulogo=63` 帧，注册/播放链没有历史的
  0-event 静默失效；
- 当前工作树对缓存 golden 仍有 `m2logo=964`、`yuzulogo=251` 个全字段差异；
  `--only-structural` 仍有 35/21 个 opacity/active 差异。因此本轮只把 full-guest smoke
  记为通过，不把数值 oracle 误报为绿色；
- 全部 Wasmtime 运行只使用既有 XP3/spec/golden，未创建或改写 fixture。
