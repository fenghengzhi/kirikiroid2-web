# MotionPlayer common builder 叶节点的强引用、void 边界与自然失败（四参考二进制，2026-08-16）

> 2026-08-18 V238 follow-up：本文 leaf Variant/raw Object owner结论保持有效；createdOrChanged
> 之后的 geometry owner现已进一步闭合。native没有 persistent `localCorners/localMeshPoints`，
> type 1使用 `commandBezierPatchPoints`，type 2使用 `commandCompositeMeshPoints`；共享 helper是
> 16-caller独立函数，返回 owning Array Variant并直接追加 native Items。详见
> `analysis/motionplayer_mesh_point_array_variant_leaf_local_geometry_four_binary_2026-08-18.md`。
>
> 2026-08-18 V239 follow-up：descriptor/color/source/size的完整 owner与partial prefix也已闭合。
> accepted clip四边是 callback前 snapshot，setSize与geometry offset继续用它；corners、command
> vectors与SourceState则在 callback后 live读取。详见
> `analysis/motionplayer_leaf_clip_snapshot_descriptor_source_size_prefix_four_binary_2026-08-18.md`。

## 结论

四份参考二进制都把普通叶节点 materialization 内联在 `Player_buildRenderCommands_guess` 的 drawable-item 分支里；它不是一个返回成功/失败 Boolean 的 native helper。共同控制流是：

1. 调用 `SeparateLayerAdaptor_resolveLayerNode_guess`；
2. 把 resolver 的返回 Variant copy-assign 到 PreparedRenderItem 的持久 `leafLayer` 字段；
3. 再从 `item.leafLayer` copy-construct 一个临时 Variant，并经 `tTJSVariant::AsObject()` 获取独立 AddRef 的 raw Object owner；
4. 依次销毁 item-copy 临时 Variant和 resolver-return 临时 Variant；
5. 此后才测试 `createdOrChanged`；false 只跳过 source/Layer copy 工作，仍释放刚取得的 raw owner；
6. true 分支用该 raw owner跨越 descriptor/color 写入、source resolve、width/height 查询、neutralColor、setSize 与 affine/mesh/bezier copy；
7. 正常尾部与异常展开都释放该独立 owner。

本地此前抽出的 `emitPreparedLeafLayerCopy_guess` 偏离了这条链：它返回 `bool`，增加 adaptor-null、负 extent 与 leaf-object-null 三个 recovery return，并从 resolver 临时返回 Variant 使用 `AsObjectNoAddRef()`。这些分支会把原版的自然异常/故障改成可恢复失败；借用指针还会在后续可重入 TJS callback 中缺少原版的独立生存期保证。本轮全部收紧。

## 四端函数与 resolver call

| 平台 | common builder | leaf resolver call |
|---|---:|---:|
| Android arm64 | `0x6C2208` | `0x6C270C` |
| Android armv7 | `0x58C7C4` | `0x58CABE` |
| iOS arm64 | `0x1001167BC` | `0x100116B30` |
| iOS armv7 | `0x114118` | `0x114666` |

完整 resolver 在四端分别位于 `0x6C3F28`、`0x58DCD4`、`0x100117E88`、`0x115B34`。每个 resolver 都只有三个 production caller：adaptor assignment、common builder 与 accurate renderer；本纵切面只修正 common-builder caller 的叶节点 owner/边界。

## 无重复 adaptor/negative-extent recovery

在 drawable clip admission 到 resolver call 的连续区间内扫描所有 floating compare：

| 平台 | 扫描区间 | FCMP/VCMP/VCMPE 命中 |
|---|---:|---:|
| Android arm64 | `0x6C2580..0x6C270C` | 0 |
| Android armv7 | `0x58C980..0x58CABE` | 0 |
| iOS arm64 | `0x100116970..0x100116B30` | 0 |
| iOS armv7 | `0x1144C0..0x114666` | 0 |

caller 的前置 drawable/clip 分支已经决定是否进入该路径。进入后没有再次计算并拒绝 `clipWidth < 0 || clipHeight < 0`，也没有 adaptor pointer 条件返回；adaptor 已由同一 builder 的更早控制流构造并发布。端口 helper 中的两个 return 因而不是参考实现边界。

## resolver 返回值发布与独立 Object owner

| 平台 | item leaf copy-assign | item leaf copy-construct | `AsObject()` / inline AddRef | 两个临时 Variant 析构 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6C271C` | `0x6C2728` | `0x6C272C..0x6C2768` | `0x6C2770`, `0x6C2778` |
| Android armv7 | `0x58CACA` | `0x58CAD2` | `0x58CADC` | `0x58CAE4`, `0x58CAEC` |
| iOS arm64 | `0x100116B40` | `0x100116B4C` | `0x100116B64` | `0x100116B70`, `0x100116B78` |
| iOS armv7 | `0x11467A` | `0x114688` | `0x114698` | `0x1146A0`, `0x1146A6` |

Android arm64 把 Object conversion 内联。`0x6C2740` 的 `CBZ X0` 只属于 `AsObject()` 内部“typed Object 的 dispatch 是否为空、是否执行 AddRef”逻辑；它把结果规范化到 X27，却不形成 caller-level item skip。另三端直接调用已恢复名称的 `tTJSVariant_AsObject_guess`，同样没有紧随其后的 `if (!object) continue/return`。

这一区别不是冗余引用：resolver return 临时值和从 item 复制的临时 Variant 都在 source descriptor 及 source resolver callback 之前销毁，仍然跨越所有这些 callback 生存的是 `AsObject()` 取得的 raw owner。即使重入脚本替换了 `item.leafLayer`，当前 copy 操作仍持有目标 Layer。

## 唯一的 caller gate 与 release 汇合点

| 平台 | `createdOrChanged` test | raw Object normal release |
|---|---:|---:|
| Android arm64 | `0x6C277C..0x6C2780` | `0x6C3158..0x6C3164` |
| Android armv7 | `0x58CAF0..0x58CAF6` | `0x58D114..0x58D122` |
| iOS arm64 | `0x100116B7C..0x100116B80` | `0x100117360..0x100117370` |
| iOS armv7 | `0x1146AA..0x1146B0` | `0x114E12..0x114E20` |

四端 false branch 都直接汇入 release/下一 item 尾部；true branch 执行完整 source/copy 子图后也汇入同一 release。raw pointer 可以是 null，这是 `AsObject()` conversion 本身的表示；caller 没有额外友好检查。若 `createdOrChanged=true` 而 Object 不可用，随后对 neutralColor/setSize/copy 的首次自然解引用决定失败边界。

Android armv7、iOS arm64、iOS armv7 的反编译栈对象还分别显示 `v192/v193`、`v199/v200`、`v209/v210` 这组编译器 cleanup owner；Android arm64 则把相同 cleanup 展开为 X27 与 landing-pad 状态。源码用局部 RAII guard 重现 normal return 和 exception unwind 的 Release，而不是在每个调用后手写回收。

## 源码修正

`cpp/plugins/motionplayer/Player.h` 与 `PlayerRenderExecute.cpp` 已：

- 把本地抽取 helper 的 ABI 从 `bool` 改为 `void`，caller 不再丢弃伪返回值；
- 删除 `_renderSeparateLayerAdaptor` null return 与重复 negative-extent return；
- 保持 resolver return 先发布到 `item.leafLayer`；
- 从该持久 Variant 复制临时 owner，再调用 `AsObject()` 获取独立 AddRef；
- 让 item-copy 和 resolver-return 两个临时 Variant 在任何 descriptor/source callback 之前按逆序销毁；
- 用 RAII 将 raw Object owner 保持到 false gate return 或完整 copy 尾部，并覆盖异常展开；
- 删除 leaf-object-null recovery，只保留 native 的 `createdOrChanged` gate；
- compiled-source 注释只描述四端共同所有权/控制流，不嵌入平台绝对地址。

## IDB 更新

四份 recovery IDB 的 common-builder 函数头、resolver/AsObject 所有权位置、`createdOrChanged` test 与最终 raw Object release 都已追加注释，并保存到 `out/ida-recovery/` 对应平台目录。

## 验证

- ordinary Emscripten 单元测试翻译单元语法检查：通过；
- `KRKR2_WASMTIME_HEADLESS=1` 同一翻译单元语法检查：通过；
- Web Debug motionplayer archive：通过；
- Wasmtime Headless Debug motionplayer archive：通过；
- Web Debug 完整目标：链接通过；
- 只有既有 `_tss` literal-operator、imagepacker `nodiscard`、pthread/memory-growth、JSPI 与 Emscripten JS library 警告。
