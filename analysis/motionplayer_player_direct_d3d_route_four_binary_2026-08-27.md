# Player direct D3DAdaptor 路径（四参考二进制，2026-08-27）

## 1. 四端入口与完整函数

| 端 | helper | body instructions | 独立 cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6D2F70` | 54 | body 内 landing pads |
| Android armv7 | `0x59761C` | 45 | — |
| iOS arm64 | `0x100123844` | 48 | table-driven unwind |
| iOS armv7 | `0x122AAC` | 88 | `0x122B94`, 47 instructions |

四端已 fresh full decompile，并读取全部 235 条 body 指令；iOS armv7 的 SjLj cleanup
亦已 fresh decompile并读取全部 47 条指令。helper、直接 callee 和 armv7 cleanup 已
命名，入口已注释/bookmark，四个 IDB 已保存。

## 2. 精确共同源形状

```cpp
void Player::renderToD3DAdaptor(D3DAdaptor *adaptor) {
    PreparedRenderItemList mainList;
    PreparedRenderItemList auxList;
    if (!prepareRenderItems(mainList, auxList)) return;
    applyPreparedRenderItemProjection(mainList);
    adaptor->renderFromPlayer(this, mainList);
}
```

四端调用地址：

| 端 | prepare | projection | `D3DAdaptor::renderFromPlayer` |
|---|---:|---:|---:|
| Android arm64 | `0x6D2544` | `0x6D2644` | `0x6AB204` |
| Android armv7 | `0x596DF0` | `0x596EB0` | `0x57D2CC` |
| iOS arm64 | `0x100122F68` | `0x100123038` | `0x100104284` |
| iOS armv7 | `0x121FDC` | `0x1220F0` | `0x101680` |

没有 canvas target Variant、TJS 属性读写、width/height 查询、clear、capture 或
post-draw。auxList 必须存在并传给 prepare，但 prepare 成功后本 helper 不再读取它；
projection 和 adaptor renderer 只收到 mainList。

## 3. 参数、容器与异常边界

`adaptor` 是从顶层 `Player::draw` 的 NCB payload 借入的 raw native pointer；本
helper 不 AddRef、不判空，也不拥有它。顶层只在 payload 非空时进入，因此正常公开
路线成立；若将 private helper 以畸形空指针直接调用，prepare false 仍能返回，prepare
true 后会在最终成员调用处触发 native null 边界。

两个 list 均 default-construct 为空 pointer-vector。prepare false 直接进入公共析构；
prepare、projection 或 renderFromPlayer 抛异常时，已分配的 aux/main 连续指针缓冲区
均被释放，指针指向的 PreparedRenderItem 不由 vector 销毁。Android arm64 在
`0x6D3010..0x6D3044` 内联两级 landing-pad cleanup；iOS armv7 的 `0x122B94` 把
call-site 0/1/2 合流为 aux、main 删除后 `Unwind_SjLj_Resume`。不存在成功后额外
commit 或失败回滚。

## 4. 本地偏差与恢复

本地原有核心三调用顺序正确，但在 list 构造前插入了 logo trace sidecar。启用时会：

1. 查询 matched motion path；
2. 做 path filter；
3. 调用 `adaptor->getWidth()` 和 `getHeight()`；
4. 格式化并发布日志。

参考 helper 完全没有这些调用。尤其两个尺寸 getter 发生在 prepare 之前，会改变
可观察虚调用顺序和最先抛出的异常。本轮已从 `PlayerRenderTargets.cpp:927` 删除整个
sidecar，恢复为精确四调用源形状（两个构造计作 RAII，三个显式函数调用）。

## 5. 验证与剩余范围

顶层 typed draw 测试继续覆盖 D3D class-ID 命中和 sticky `useD3D` 提交；本轮静态
逐行对照确认 helper 不再含 trace、尺寸 getter或额外 gate。`git diff --check` 与
覆盖表 12 列校验通过。正式 CMake/unit/Web build 因本机缺少 CMake、Ninja、
Emscripten 且没有既有 build/out 未运行。

本项只闭合 Player 的 direct D3D coordinator。`D3DAdaptor::renderFromPlayer` envelope
后来由 `MP-R14-MOTION-PRIVATE-OPENGL-ENVELOPE` 闭合，shared deep renderer 外层、batch、
method 与 stencil 又由 `MP-R14-D3D-DEEP-BATCH-STENCIL` 闭合，公共 mesh helper 随后由
`MP-R14-D3D-MESH-SUBMIT-CELLS` 闭合，projection随后由
`MP-G11-PLAYER-PREPARED-PROJECTION` 闭合；`prepareRenderItems` wrapper、递归builder调用边界
与stable-sort owner/EH随后由`MP-G11-PLAYER-PREPARE-SORT-WRAPPER`闭合，递归builder深层主体
又由`MP-G11-PLAYER-APPEND-PREPARED-ITEMS`闭合。当前相邻剩余项转为持久item最终析构与
MotionNode deque生命周期。
