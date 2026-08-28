# Player.calcViewParam 视图投影（四参考二进制，2026-08-27）

## 1. endpoint 与完整取证

| 端 | 入口 | fresh full disassembly |
|---|---:|---:|
| Android arm64 | `0x6CE908` | 1349 instructions |
| Android armv7 | `0x594958` | 798 instructions |
| iOS arm64 | `0x1001201CC` | 613 instructions |
| iOS armv7 | `0x11EED4` | 977 instructions |

四端均已 fresh 全函数 decompile，并分页读取全部 3737 条指令；函数已统一命名、
注释、bookmark 并保存 IDB。体积差异来自 STL deque/Array append、Variant EH 与
ARMv7 SjLj 展开，不代表第二套算法。

## 2. frame prelude

共同源形状：

```cpp
if (frame < 0.0) frame = 0.0;
frameTickCount = frame;
if (frame > cachedTotalFrames) frame = cachedTotalFrames;
clampedEvalTime = frame;
queuing = true;
firstFrame = true;
frameProgress(0.0);
updateLayers();
```

- 两个比较均 ordered；`-0.0` 不进负数分支并保留符号，NaN 穿过两项比较；
- raw `frameTickCount` 在 upper cap 之前提交，evaluation cursor 才 cap；
- lower clamp 发生后 raw cursor 为 `+0.0`；negative total 仍能让 evaluation cursor
  被 upper compare 改成 negative；
- queuing/firstFrame 在 64 位端可合并为相邻 word store，但源语义是两个 true byte；
- 只调用 `frameProgress(0)` 和 `updateLayers`，不调用 calcBounds、事件派发或 lazy
  load；深层两函数的完整状态机由各自切片负责。

## 3. outer viewParams owner 与 live node loop

prelude 后，对 by-value `viewParams` 再构造一个 `ncbPropAccessor`：严格 AsObject、
只 AddRef Object dispatch，并让该 owner 跨越整个 node traversal。每个 index lookup
都通过同一 dispatch 执行 `PropGetByNum(flags=0, index=nodeIndex-1,
objthis=dispatch)`；status 忽略，返回 Variant 再构造 per-output accessor。

因此调用期间 owner 组成是：参数的 Object+ObjThis 两个 owner，加 outer accessor
的一个 Object owner；没有每个 index 的额外 AddRef。原本本地直接对参数 Variant
逐次取 raw Object，少了第三个跨循环 owner。本轮已恢复 accessor，并新增精确
`+3/-3` AddRef/Release、flags/index/receiver 测试。

node loop 从 flat index 1 开始、排除 synthetic root，并在每轮重读 live deque end；
re-entrant append 可以进入同一调用。物理节点 stride 沿用已证实的四端布局：Android
arm64/armv7 为 2632/2272，iOS arm64/armv7 为 2648/2228。

## 4. visible gate 与基本字段发布

```cpp
exportable =
    (type == 0 || type == 6 || (type == 3 && player.preview)) &&
    accumulated.active && accumulated.visible;
output.visible = exportable;
if (!exportable) continue;
```

preview 在每个 type-3 node 处读取 live Player byte。所有 output SetValue 使用
`TJS_MEMBERENSURE`、output Object 自身作 receiver/objthis、共享 hint；status 忽略，
异常传播。exportable 时继续按固定顺序发布：

1. `src`：active slot ttstr；
2. `blendMode`：active slot int；
3. `originX`；
4. `originY`；
5. `opacity`：accumulated int；
6. `mbp`；
7. `cmesh`；
8. `clip`；
9. 读取并覆写嵌套 `coord`；
10. 读取并覆写嵌套 `color`；
11. 读取并覆写嵌套 `matrix`。

`mbp` 只有 `meshType == 1 && meshControlPoints 非空` 时是 fresh 展平 Real Array
`[x0,y0,...]`，其余情况仍发布 Void。

## 5. cmesh ancestor gate 与 separator owner

`cmesh` 总是 fresh Array，但四端只有 `node.meshAncestor != nullptr` 才继续构造
separator Dictionary 和遍历 mesh chain：

```cpp
Array cmesh;
if (node.meshAncestor) {
    Dictionary separator{ type: "mesh.inherit.separator" };
    Variant separatorAlias(separator.Object, separator.Object);
    if (node.meshInheritanceSeparator) cmesh.push(separatorAlias);

    for (mesh = node.meshAncestor; mesh; mesh = mesh.meshAncestor) {
        if (mesh.meshInheritanceSeparator) cmesh.push(separatorAlias);
        if (!mesh.hasMeshData) continue;
        cmesh.push(buildMeshRecord(mesh));
    }
}
output.cmesh = cmesh;
```

所以“无 ancestor 但 node separator byte 为 true”仍必须返回空 Array，且不会分配
separator Dictionary。原本本地无条件构造 separator 并可能 append，既改变输出也
增加可抛 allocation/SetValue 边界；本轮已把整个 separator/chain block移入
`meshAncestor != nullptr` gate，并增加可见无 ancestor 项的空 cmesh 测试。

同一 separator dispatch 被所有 separator entry 作为 Object/ObjThis alias 复用；
不为每层创建新 Dictionary。chain 按 immediate ancestor 向根顺序遍历，separator
在该 ancestor 的 mesh record 之前 append；`hasMeshData == false` 只跳 record，
不跳 separator。

## 6. mesh record 数值与字段顺序

每个 live mesh record：

1. `negOffsetX/Y` 先从 float 取负；
2. 以 binary64 matrix 乘 float-promoted offset 并相加，再窄化为 float
   `invOffsetX/Y`；
3. `invOffset` fresh Real Array `[x,y]`；
4. `invMatrix` fresh Real Array `[m11,m12,m21,m22]`；
5. `patch` 从 transformed mesh control points 展平为 fresh Real Array；
6. `division` 以 uint32 解释 raw meshDivision，乘 Player ratio，按 target unsigned
   conversion profile窄化，然后 cap 到 50；
7. fresh Dictionary 按 `type=1, division, invOffset, invMatrix, patch` 顺序发布，
   再以 Object/ObjThis 同一 dispatch append。

`calcViewMeshDivision_guess` 已显式实现 NaN、<=0、>=2^32 与 uint32 cap，避免 portable
C++ out-of-range conversion UB。它与 prepared/command 的 signed division helpers
是三条不同边界，不能合并。

## 7. clip 与嵌套数组写回

`clipAABB == nullptr` 时发布 Void。非空时读取四个 float，width/height 在 float
精度相减，fresh Dictionary 依次发布 `left, top, right, bottom, width, height`，再
以 Object/ObjThis alias 写入 `clip`；不做 ordered validity 分类。

随后从 output Dictionary 以 flags 0/hint 读取 `coord`、`color`、`matrix`，每个结果
严格构造 accessor：

- coord index 0/1/2 写 accumulated posX/Y/Z Real；
- color index 0..3 从 16 raw bytes 逐个 memcpy 为 uint32，再零扩展 Integer64；
- matrix index 0..3 写 accumulated m11/m12/m21/m22 Real。

所有 numeric PropSetByNum 使用 `TJS_MEMBERENSURE`；数组长度不先检查，动态/畸形
dispatch 的 status 忽略而异常传播。

## 8. 本地修改与验证

本轮语义修改位于 `PlayerLayerQuery.cpp:436`：

- 增加跨整个 traversal 的 outer viewParams accessor owner；
- 恢复 `meshAncestor != nullptr` 对 separator allocation、node separator 和 chain
  traversal 的共同 gate。

测试扩展位于 `tests/unit-tests/plugins/motionplayer-dll.cpp:26289`：除既有字段/hint/
嵌套数组 happy path 外，新增 outer owner 的精确引用计数和 index receiver 序列，
以及可见无 ancestor node 的空 cmesh 断言。

本切片记为 `IMPLEMENTED`；`git diff --check` 通过。正式 CMake/unit/Web build 仍因
本机无 CMake/Ninja/Emscripten 未执行。各 SetValue/Array growth 异常的逐 call-site
Android EH table、iOS LSDA/SjLj cleanup case 留到最终 ABI exception ledger；普通
owner、输出、re-entrant receiver 和数值边界已闭合。

## 9. per-call-site ABI EH 闭包

最终账本再次 fresh decompile 四端主体，并按 300-instruction 页完整读取
1349/798/613/977 条指令。另完整读取 iOS arm64 `0x100120C58` 的 176 条 LSDA cold
cleanup、三个 terminate thunk，以及 iOS armv7 `0x11F97E` 的 367 条、82-case SjLj
cleanup。

共同异常边界按 source lifetime 可精确归并为：

1. frame prelude 的 raw cursor、clamped cursor、queuing/firstFrame、`frameProgress(0)` 和
   `updateLayers()` 均在任何 view output 之前提交；后续异常不回滚 Player 状态；
2. outer `viewParams` accessor 一旦构造完成，跨整个 live node loop 保持 Object owner，
   任一后续异常最终 Release；
3. 当前 numeric index 结果 Variant 与 output accessor 只有在各自构造成功后进入 cleanup。
   先前 node 对象以及当前 output 已经成功 SetValue 的字段都保留；
4. `mbp`、`cmesh`、separator、mesh record 的 invOffset/invMatrix/patch、clip Dictionary 以及
   coord/color/matrix 读取结果按实际 live prefix 逆序析构。SetValue/append 已 CopyRef 的
   owner不会因局部析构而撤销发布；
5. mesh chain 中较早 record/separator 已 append 到 cmesh 时保持；当前 record 创建失败只
   清当前 nested owners。后续 output.cmesh/clip 或 nested PropSet 失败同样不清前缀；
6. accessor/Variant cleanup 自身抛出进入 terminate/abort，原异常不能被第二个异常替换。

目标差异：

- Android arm64 的 in-body landing region 位于约 `0x6CFA8C..0x6CFE30`。多入口按当前
  mesh/clip/nested owner live set 汇合，最后 Release per-output 与 outer accessor 后
  `_Unwind_Resume`；
- Android armv7 完整 798 条主体无本帧 unwind landing，只有正常路径 cleanup；
- iOS arm64 的 176 条 cold body 由 LSDA 选择多个前缀入口，依次清 nested Variant、
  temporary accessor dispatch、output accessor 和 outer accessor，再 `__Unwind_Resume`；
  三个相邻 thunk处理不同 cleanup destructor throw；
- iOS armv7 的 call-site 0..81 进入 82-case SjLj switch：前半覆盖 outer/output owner 与
  mbp/cmesh，后半覆盖 mesh record、clip 和 coord/color/matrix；若 cleanup Release 抛出，
  cases 37..40、53..54、73..81 等汇入 terminate，case 82 abort，正常异常尾调用
  `__Unwind_SjLj_Resume`。

本地 `ncbPropAccessor`、`tTJSVariant` 与 fresh Array/Dictionary helper 的词法 RAII 和
prefix-publication 顺序与联合证据一致；无需 catch、rollback 或手工目标分支。四个 IDB 已
补充 landing/cold/SjLj 命名、注释、书签并保存。因此早期留下的“精确 per-call-site ABI
exception cleanup”已闭合；`frameProgress`、`updateLayers` 的普通深层依赖也已经由 C15、
C20..C29 独立闭合。
