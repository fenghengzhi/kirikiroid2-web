# Private Motion GLL command/queue builder（四参考二进制，2026-08-27）

## 1. fresh 全函数证据

| 端 | native builder | body instructions | 独立 cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6DBB18` | 761 | body 内 landing pads |
| Android armv7 | `0x59CB20` | 671 | — |
| iOS arm64 | `0x10012B7D0` | 465 | table-driven unwind |
| iOS armv7 | `0x12A304` | 703 | `0x12AAC6`, 121 instructions |

四端均已 fresh full decompile，并按 240 条分页读取全部 2600 条 body 指令；iOS
armv7 的 23-call-site SjLj cleanup 亦已读取全部 121 条。入口、queue phase、armv7
cleanup 已注释/bookmark并保存四个 IDB。

函数大小差异来自 libc++/gnustl deque map/block、vector growth、Variant EH 和 ARMv7
soft division展开；四端具有同一 builder 算法和同一 private queue element 逻辑。

## 2. 入口快照与两阶段数据流

入口立即把 Player `_priorDraw` byte 快照到 call-local bool；之后即使
`requireLayerId` 或 source loading re-enter Player 并修改公开 priorDraw 属性，本次
调用的所有 gate 和 opacity 仍使用旧值。返回 stencilCount 初始为 0。

```text
snapshot priorDraw
if !priorDraw:
    reset/assign stencil refs over mainList
    recompute clip and lazy renderLayerId for drawFlag items

clear native private-Layer deque
for item in mainList:
    apply queue filter using the same priorDraw snapshot
    publish persistent source descriptor/color objects
    resolve source Layer texture
    emplace queue element (takes texture owner)
    append affine points or swap mesh vector
return stencilCount   // priorDraw path stays zero
```

auxList 是真实形参，但四端 body 完全不读取；caller 仍必须传它以保留 ABI/source
signature。

本地原来在三个位置重读 `_priorDraw`。第一阶段中的 `dispatchRequireLayerId` 是脚本
callback，第二阶段 source resolver 也可 re-enter，因此重读会让一个调用混用两种模式。
本轮已在函数入口恢复 `const bool priorDraw = _priorDraw` 并贯穿全部 gate/opacity。

## 3. non-prior stencil 与 clip phase

仅 `priorDraw == false` 执行：

1. 第一遍把每个 main item 的 `stencilMaskRef`、`stencilWriteRef` 两个 byte 清零；
2. 再遍历候选：low blend 6、drawFlag false、rawFlag16 true、opacity 0、无 parent 任一
   成立都跳过；
3. stencilCount 以 unbounded signed int 增长，再窄化为 uint8 写 ref；第一次越过 255
   时以函数内 static flag 只记录一次 `MMotionPlayer / StencilCount overflow(256)`；
4. ref 写入 item stencilWrite，并沿 parent chain 和 child pointer-vector传播
   stencilMask；链中没有 drawable target 时把 write ref 清回 0；
5. 对 drawFlag item 以 canvas float bounds 和 paintBox/viewport计算 ordered clip；
   无效 clip 或 rawFlag16 只清 rawFlag21；有效时写 rawFlag21、四 float clipRect，清
   leafLayer Variant；
6. rawFlag20 false 时无参数调用 `requireLayerId`，转换结果为 int32写 renderLayerId，
   再把 rawFlag20 置 true。回调抛异常时前面的 clip/leafLayer提交不回滚。

clip 的 AArch64 `FMAXNM`、compare/select、FRINTM/FRINTP 与 ARMv7 VFP 序列对应本地
`std::fmax`、显式 `<` select 和 floor/ceil。相等/reversed/NaN 的处理沿用已恢复的
`computeD3DClip_guess` 平台共同 profile。

## 4. queue filter 与 prior opacity

native deque 在第二阶段开始时无条件 clear，priorDraw 也不例外。每个 item 的 queue
admission 必须同时满足：

- `(blendMode & 0xF) != 6`；
- skipFlag0 false；
- rawFlag16 false；
- opacity 非零；
- priorDraw 为 false，或 skipFlag1 为 true；
- `sourceState->blank` false。

注意 opacity gate 使用原始值，折半发生在通过 gate 之后。priorDraw opacity 是 C++
signed `/ 2` 的 toward-zero 结果。四端都生成等价的
`(x + signbit) arithmetic_shift_right`：例如 `-3 -> -1`、`-2 -> -1`、`-1 -> 0`。
本地原有负数专门式 `(x + 1) / 2` 会把负偶数错误地再 toward-zero一次（`-2 -> 0`），
本轮已改为直接 `opacity /= 2`。

## 5. persistent descriptor、source texture 与 owner

每个 admitted item 先覆盖 Player 持久 Dictionary：

1. descriptor `key`；
2. descriptor `src`（二进制字符串表在部分端渲染为 `s`，实际字段为既有 src hint）；
3. descriptor `blendMode`；
4. color Dictionary numeric index 0..3，以 uint32 零扩展为 TJS Integer。

然后以 item sourceState 和 descriptor 解析 source Layer，再取得 native main-image
texture。临时 descriptor/color accessors、source Variant 和 AsObject owner 都按每 item
析构；普通失败 HRESULT 不短路，严格 Variant 转换异常传播。

queue element 构造时若 texture 非空立即 AddRef；private deque 独占该 owner，元素
析构时 Release。sourceState 自身 texture 字段仍是 atlas/cache 的非 owning borrow，
不能与 queue owner 合并。若后续 point vector allocation/swap 抛异常，已经成功
emplace 的元素及 texture owner 保留在 private Layer queue，不回滚到调用前 queue。

iOS armv7 `0x12AAC6` 逐 call-site 清理临时字符串、Variant和两个 retained Object
dispatch；析构再次抛出转 terminate。队列元素的生命周期由 deque 管理，不由 SjLj
临时 cleanup重复释放。

## 6. private queue element 与内部容器

64 位端 element stride `0x58`，32 位端 stride `0x48`。共同声明顺序由四端字段写入
联合确定：

```cpp
struct PrivateMotionGLLRenderItem {
    vector<MeshPoint> points;
    uint32_t opacity;
    uint8_t stencilMaskRef;
    uint8_t stencilWriteRef;
    int32_t blendMode;
    int32_t geometryType;
    int32_t meshDivX;
    int32_t meshDivY;
    uint32_t packedColors[4];
    int32_t sourceRect[4];
    iTVPTexture2D *sourceTexture; // owning AddRef
};
```

deque clear 使用本平台 STL 的 map/block算法并保留符合实现的空块状态；emplace 在当前
block 有空间时 placement-construct，边界时扩 map/分配新 block。代码不通过 TJS
dispatch或 private class ID 操作 deque。

几何 payload：

- type 0：从 corners `(0,1),(2,3),(6,7)` 依次 push 三点；meshDiv 字段保持 dormant；
- type 1：把 item meshPoints 的三指针 vector state 与新 element points swap，随后以
  commandPatchDivision 和 source width/height 的 uint32 wrap/divide profile写 divX/Y；
- type 2：swap commandCompositeMeshPoints vector state，并复制 item meshDivX/Y；
- 其他 type：不写 point payload。

本地 element 声明、texture owner、affine push、type1/type2 swap 和 dormant字段已与
该布局一致。

## 7. 本地结构恢复

原本 Player builder 以 private script dispatch 为参数，clear、每次 append、最终 trace
size query 都重新做 NativeInstanceSupport。这与四端 native member直接访问 deque
不符，也增加可观察 class-ID调用。现已：

- 把 builder 形参恢复成 borrowed `tTJSNI_BaseLayer*`；
- 增加 native Layer overload，builder 直接 clear/emplace/read queue；
- dispatch overload 只保留给 class-ID helper/unit用途，并转发到 native overload；
- caller 从 `ensurePrivateMotionGLL` 得到 native pointer后直接传入 builder/stencil/Update；
- 删除 builder 的 motion-path、begin/end日志和 queue-size trace；
- 恢复入口 priorDraw snapshot 和正确负数 `/2`。

由此 direct SeparateLayer legacy coordinator 不再保留上一报告中的 dispatch-based
builder gap，可从 `EVIDENCED_4_4` 升级为 `IMPLEMENTED`。accurate renderer和 accurate
post-draw 仍是不同 callee，不由本 builder 覆盖。

## 8. 测试与验证

既有 private class unit case 已扩展为同时通过 retained dispatch 和 borrowed native
Layer观察同一 deque，验证 native append后两种 size一致、type2 vector swap清空输入、
native clear 对 dispatch视图可见；ensure 返回类型/复用/resize测试继续保留。stencil、
clip、Bezier division、source descriptor/owner和 queue item逻辑已有相邻专门 unit cases。

`git diff --check` 与 coverage 12 列校验通过。正式 CMake/unit/Web build 因本机没有
CMake/Ninja/Emscripten且无既有 build/out，仍未运行。builder 记为 `IMPLEMENTED`；
private Layer `Draw_GPU` 消费 deque 的 blend/stencil/geometry backend 是下一独立深层项。
