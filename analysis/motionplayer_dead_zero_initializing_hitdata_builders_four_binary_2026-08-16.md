# HitTestInternal 三个零初始化 HitData builder 四端清理（2026-08-16）

## 结论

`HitTestInternal.h` 中的以下三个 inline helper 全部零 caller：

```cpp
makeCircleHitData(...)
makeRectHitData(...)
makeQuadHitData(...)
```

它们并非无害的构造便利函数：三者都以 `HitData hit{}` 开始，先把完整 type + 15-double
记录清零，再写当前 shape 使用的少数槽位。四个当前参考二进制共同保留的边界恰好相反：

- 公开 Circle/Rect/Quad 默认构造只分配完整记录并写 type；15 个 double 全不初始化；
- node-owned shape producer 先写 type，再只写当前 shape 使用的槽位；其他槽位保留此前帧值或
  构造时不定内容；
- LayerGetter.shape 复制完整记录，不按 shape 类型清除未使用槽。

因此这些 helper 即使目前未调用，也表达了一条错误的 whole-record-zero source topology，未来
一旦被复用就会消除原版可观察的不定值与跨帧残留。三者已完整删除；`HitData` 布局与唯一真实
consumer `hitTestHitData` 保持不变。

## 四端映射

### 公开默认构造叶函数

| shape | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Circle | `0x6DD810` | `0x59E41C` | `0x10012DDC8` | `0x12C9D8` |
| Rect | `0x6DE3D0` | `0x59F04C` | `0x10012ED6C` | `0x12D940` |
| Quad | `0x6DED5C` | `0x59FA1C` | `0x10012F9A4` | `0x12E614` |

### node-owned producer

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6BB274` | `0x587BAC` | `0x100110CE0` | `0x10E46C` |

## 默认构造的分配与写入

四端 fresh decompile 共同显示：

```text
p = operator new(0x80)             // A64, Android ARMv7, iOS arm64
p = operator new(0x7c)             // iOS ARMv7 double alignment ABI
p->type = 1 / 2 / 3
publish p into the NCB instance adaptor
```

从 allocation 返回到 adaptor publication 之间没有 `memset`、向量化零 store、double store 或
值初始化 helper。iOS ARMv7 的 0x7c 与其 4-byte double alignment 一致；其余三端为 0x80。
所有目标都只覆盖起始 type word。

本地 public facade 已正确采用只写 type 的构造：`GeometryShapeBase_guess(shapeType)` 不对继承的
`values` 做 aggregate/value initialization。本轮删除的 builder 是该边界旁边最后一条相反的
共享头入口。

## node producer 的跨帧部分写入

四端 `Player_updateShapeGeometry_guess` 只遍历非根、type-1 且 active slot `done == 0` 的
节点。通过 gate 后共同执行：

```text
geometry.type = node.shapeType

type 0: write values[0..1]
type 1: write values[0..2]
type 2: write values[3..6]
type 3: write values[7..14]
default: write no double slot
```

它不在 type store 前清记录，也不在 switch 后清“其他 shape”的槽位。例：circle 帧写入
`0..2`，下一帧切 rect 时只覆盖 `3..6`，旧 circle 槽仍保留；未知 type 甚至只更新 type。

三个旧 helper 的 `HitData hit{}` 会把上述 retained slots 变成确定的 +0.0，还会把未进入任何
合法 producer 的原生不定内容定义化，不能作为等价测试/构造抽取保留。

## typeOverride 也是无来源 surface

三者还接受 `typeOverride`，允许用 circle 槽数据返回 rect/quad/未知 type 记录。四端公开
构造的 type 是类固定常量，node producer 的 type 则来自 node 字段；没有一条“传自定义 type
同时按另一 shape 写槽位”的共同 factory。因此删除 helper 也移除了这层混合语义入口。

## 源码与恢复库修正

- `cpp/plugins/motionplayer/HitTestInternal.h`
  - 删除 `makeCircleHitData`、`makeRectHitData`、`makeQuadHitData`；
  - 保留 ABI `HitData`、两项 static_assert 与 `hitTestHitData`。
- 四份 recovery IDB
  - 在 12 个 Circle/Rect/Quad constructor 叶函数注明只写 type、无 15-double 初始化；
  - 在四个 node producer 注明按 shape 部分覆盖、无 whole-record zero；
  - 强制刷新并回读全部 16 个函数，随后原位保存四份 recovery IDB。

## 验证

- 三个 builder 在 `cpp/` 与 `tests/` 中零匹配；
- `HitData`、`hitTestHitData`、node `shapeGeometry` 与差分 guest 入口均保留；
- ordinary/headless Emscripten syntax-only 均成功；仅出现既有 `_tss` warning；
- Web Debug 与 Wasmtime Headless Debug `motionplayer` 均 32/32 成功；
- `geometry_hit_test_wasm` 1/1 重新生成成功；
- Web Debug 完整增量构建 3/3 成功并重新链接 `index.html/index.wasm`。
