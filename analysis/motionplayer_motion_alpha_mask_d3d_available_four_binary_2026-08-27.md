# Motion `doAlphaMaskOperation` / `getD3DAvailable` 四二进制联合恢复

> 终态注记（2026-08-29）：下文第 12 节的“整个 motionplayer 目标尚未完成”只记录本 slice
> 取证当时的阶段状态，已由后续 163 项逐任务闭包和最终报告取代；本 slice 当前为
> `CLOSED_STATIC`，全局当前状态见 `analysis/motionplayer_tasks_status.tsv`。

日期：2026-08-27

## 1. 本 slice 的闭合范围

本报告闭合 `Motion` namespace 最后的两个非 subclass NCB callback：

- `doAlphaMaskOperation`：11 个实参的 wrapper、目标 clip 读取与裁剪、空交集的惰性源对象、
  software/GPU 两条数据流、alpha/threshold 两种 mask mode、`op=1/2/5/6` 的精确行为、
  外部区域清零、render-method 静态缓存、脚本可见 `fillRect`/`update`、Variant owner 和边界；
- `getD3DAvailable`：零参数 wrapper 的剩余参数行为，以及对 process-cached software-renderer
  判定的逻辑取反。

四个参考二进制共同构成权威。本轮对八个 callback 都重新取得完整反编译/反汇编证据；
对反编译器显示不完整的宽成员名，另以 UTF-16LE 原始字节搜索在四端完成核验。逐行对照
没有发现需要修改的本地运行语义。

## 2. callback 地址与 fresh 指令覆盖

| callback | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `doAlphaMaskOperation` | `0x6AC4E4`，1509 条 | `0x57E1E8`，1433 条 | `0x100104E68`，1197 条 | `0x10243C`，1654 条 |
| `getD3DAvailable` | `0x6ADD40`，7 条 | `0x57F4A8`，5 条 | `0x10010654C`，6 条 | `0x103908`，5 条 |

四个 alpha callback 均已完整分页反汇编至 `cursor.done=true`，并取得 fresh Hex-Rays body；
四个 availability callback 的完整指令序列也已逐条读取。指令数差异来自 AArch32/AArch64、
Android/iOS C++ ABI、Variant 临时量展开和编译器布局，并不表示源级分支集合不同。

四端函数已统一命名为 `Motion_doAlphaMaskOperation_guess` 和
`Motion_getD3DAvailable_guess`，添加函数注释与书签，并原位保存四份 IDB。

## 3. NCB wrapper 边界

`doAlphaMaskOperation` 是 Motion class dispatch 上的 namespace-level typed free function，
不是 `Player` 实例方法。共同形状为：

```text
doAlphaMaskOperation(
    dstLayer: Variant, dstX: int, dstY: int,
    srcLayer: Variant, srcX: int, srcY: int,
    width: int, height: int, threshold: int,
    maskMode: int, op: int) -> void
```

wrapper 先通过通用 NCB receiver/member-name gate，清空 result，再执行 11 参数最低计数检查；
参数不足返回 `TJS_E_BADPARAMCOUNT`。两个 Layer 参数按完整 Variant 值物化，整数参数执行
typed conversion；普通调用完成后 result 保持 Void。

`getD3DAvailable` 的 typed arity 是零：负参数计数仍在通用 gate 中失败并已清空 result；
非负的剩余参数全部忽略、不做 conversion。成功结果是 Integer/Boolean 语义的 0 或 1。

## 4. clip、owner 与空交集共同伪代码

```text
alphaMask(dstVariant, dstX, dstY, srcVariant, srcX, srcY,
          width, height, threshold, maskMode, op):
    destinationCopy = CopyRef(dstVariant)
    destination = ncbPropAccessor(destinationCopy)
    clear destinationCopy

    clipLeft   = destination.getIntValue("clipLeft")
    clipTop    = destination.getIntValue("clipTop")
    clipWidth  = destination.getIntValue("clipWidth")
    clipHeight = destination.getIntValue("clipHeight")
    dstClip = [clipLeft, clipTop,
               clipLeft + clipWidth, clipTop + clipHeight]

    if dstClip.left > dstX:
        srcX += dstClip.left - dstX
        width -= dstClip.left - dstX
        dstX = dstClip.left
    if dstClip.top > dstY:
        srcY += dstClip.top - dstY
        height -= dstClip.top - dstY
        dstY = dstClip.top
    if dstX + width > dstClip.right:
        width = dstClip.right - dstX
    if dstY + height > dstClip.bottom:
        height = dstClip.bottom - dstY

    overlap = [dstX, dstY, dstX + width, dstY + height]
    if width <= 0 or height <= 0:
        if op == 1:
            dst.fillRect(dstClip.left, dstClip.top,
                         dstClip.width, dstClip.height, 0)
        return

    sourceLayer      = strict native Layer conversion(srcVariant)
    destinationLayer = strict native Layer conversion(dstVariant)
    ... software/GPU setup, operation matrix ...
    dst.update(dstX, dstY, width, height)
```

`ncbPropAccessor` 持有 destination dispatch 的独立引用直到函数尾；最初的临时 Variant
随即 Clear，但 accessor owner 继续存活。四个 clip property 都先做
`TJS_MEMBERMUSTEXIST` probe，再做普通读取；不存在或读取失败时 helper 的有效默认值为 0。
坐标、差值、和与乘 pitch 都保持 32 位目标机整数行为。

空交集在任何严格 source Layer conversion、texture lookup 或 renderer setup 之前返回。因此
非 Object source 在这个路径不会抛错。只有 `op==1` 会用五个 Integer Variant 对完整
`dstClip` 调一次 `fillRect(..., 0)`；其他 op 不 dispatch，空路径也不调用 `update`。

## 5. 非空交集的共同数据流

四端在确认 `width>0 && height>0` 后按同一顺序执行：

1. 先严格转换 source Layer，再严格转换 destination Layer；
2. 从 source main image 取得 texture，构造唯一的 source texture/rect element；
3. 再判断 renderer：
   - software：取得 source pixel data、destination writable pixel buffer、两个 pitch，并把指针
     平移到 `srcX/srcY` 与 `dstX/dstY`；
   - GPU：先取得 destination reference texture，再以 `true, &overlap` 取得 render texture；
4. 最后才验证 `maskMode/op` 分支；
5. 所有非空路径最终都以四个 Integer Variant 调用 `Layer.update(overlap)`。

第 4 点是可见边界：不支持的 `maskMode` 或 `op` 不是无副作用的早退。它已经执行严格
Layer conversion，并取得 software writable buffer 或 GPU render target，随后跳过像素/
OperateRect 主体，但仍 dispatch `update`。本地实现保留了这一先 setup、后 admission 的顺序。

software 循环只写目标 BGRA 像素的 Alpha 字节；RGB 原样保留。每行分别按 source/destination
pitch 前进，循环范围严格是裁剪后的 `width * height`。

## 6. alpha mask mode（`maskMode == 1`）

共同操作矩阵为：

| op | software 目标 Alpha | GPU method | blend tuple |
|---:|---|---|---|
| 5 或 6 | `srcA + (255-srcA)*dstA/255` | `AddAlphaMask` | `GL_FUNC_ADD, ZERO, ONE, ONE, ONE_MINUS_SRC_ALPHA` |
| 2 | `(255-srcA)*dstA/255` | `AlphaMaskRev` | `GL_FUNC_ADD, ZERO, ONE, ZERO, ONE_MINUS_SRC_ALPHA` |
| 1 | `dstA*srcA/255`，此前清零 overlap 外区域 | `AlphaMask` | `GL_FUNC_ADD, ZERO, ONE, ZERO, SRC_ALPHA` |

三个 GPU method 使用同一 passthrough fragment shader：

```glsl
void main() { gl_FragColor = texture2D(tex0, v_texCoord0); }
```

每个 method 都由独立的 function-static hint、guard 和 raw method pointer 缓存；之后通过一个
source texture element 调用 `OperateRect`。四端的数值常量分别反解为
`GL_FUNC_ADD=32774`、`GL_ZERO=0`、`GL_ONE=1`、`GL_SRC_ALPHA=770`、
`GL_ONE_MINUS_SRC_ALPHA=771`，与本地调用完全一致。

## 7. threshold stencil mode（`maskMode == 0`）

共同操作矩阵为：

| op | software 目标 Alpha | GPU method | blend tuple |
|---:|---|---|---|
| 5 或 6 | `srcA >= threshold` 时写 255，否则保留 | `AlphaMaskThresholdFill` | `GL_MAX, ZERO, ONE, ZERO, ONE_MINUS_SRC_ALPHA` |
| 2 | `srcA >= threshold` 时写 0，否则保留 | `AlphaMaskThresholdCrop` | `GL_FUNC_ADD, ZERO, ONE, ZERO, ONE_MINUS_SRC_ALPHA` |
| 1 | 先清零 overlap 外区域；`srcA < threshold` 时写 0，否则保留 | `AlphaMaskThreshold` | `GL_FUNC_ADD, ZERO, ONE, ZERO, SRC_ALPHA` |

三个 GPU method 使用完全相同的 threshold shader：

```glsl
uniform float threshold;
void main() {
    gl_FragColor = vec4(
        0, 0, 0,
        step(threshold, texture2D(tex0, v_texCoord0).a));
}
```

`AlphaMaskThresholdFill` 的 equation 是 `GL_MAX=32776`，另两个为 `GL_FUNC_ADD`。每个
method 另有独立的 function-static `EnumParameterID("threshold")` 结果；每次调用都以原始
整数 `threshold` 执行 `SetParameterOpa`，没有在 CPU 侧 clamp 或归一化。

## 8. `op == 1` 的四条外部清零 strip

alpha 与 threshold 两种 mode 的 `op==1` 都在 overlap 内运算前，以脚本 `fillRect` 清除
destination clip 中 overlap 之外的区域。顺序固定为：

```text
left   = [clip.left, clip.top, overlap.left, clip.bottom]
right  = [overlap.right, clip.top, clip.right, clip.bottom]
top    = [max(clip.left, overlap.left), clip.top,
          min(clip.right, overlap.right), overlap.top]
bottom = [max(clip.left, overlap.left), overlap.bottom,
          min(clip.right, overlap.right), clip.bottom]
```

每个 strip 只有在 `left < right && top < bottom` 时 dispatch；参数都是五个 Integer
Variant，实际 argv 是 `left, top, right-left, bottom-top, 0`，receiver 是 retained
destination dispatch，四次调用复用同一个 result Variant。顺序不能合并成一个 fill、改成
native buffer clear，或移到 strict Layer conversion之前，因为脚本可见调用与异常
partial commit 都会改变。最终 `update` 同样传 `dstX, dstY, width, height`，而不是右/下边界。

若任一 `fillRect`、property read、Layer conversion、texture/buffer access、method compile、
parameter set、OperateRect 或最终 `update` 抛出，函数沿 C++ 异常路径释放已经构造的 Variant
临时 owner；已经发生的目标 buffer 写入、外部 strip 清零或脚本 dispatch 不回滚。

## 9. 宽字符串原始字节核验

Hex-Rays 在 Android armv7、iOS 两端的部分位置把宽键误显示为单个 `"c"`。本轮按
UTF-16LE 原始字节搜索 `clipLeft/clipTop/clipWidth/clipHeight/fillRect/update`，所有搜索均
读至 `cursor.done=true`。alpha callback 实际使用的四键位于：

| 平台 | `clipLeft` | `clipTop` | `clipWidth` | `clipHeight` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x14C202C` | `0x14C203E` | `0x14C204E` | `0x14C2062` |
| Android armv7 | `0xD78792` | `0xD85036` | `0xD85046` | `0xD787A4` |
| iOS arm64 | `0x10195B806` | `0x10195B818` | `0x10195B828` | `0x10195B83C` |
| iOS armv7 | `0x174DB6A` | `0x174DB7C` | `0x174DB8C` | `0x174DBA0` |

四端 UTF-32LE 搜索均为零命中；这与本组字面量的实际 UTF-16LE 存储一致。不能从
反编译器残留的单字符显示推导源码成员名。

## 10. `getD3DAvailable` 精确结论

四端完整函数都只有同一语义：

```text
getD3DAvailable():
    return logical_not(TVPIsSoftwareRenderManager())
```

Android arm64 使用 `MVN` 后 `AND #1`，另外三端使用 `EOR #1`；输入 helper 的有效返回值
是缓存后的 bool 0/1，所以四端都得到规范化的 0/1。函数不查询当前 D3D adaptor、texture、
window 或 Layer，也不创建 renderer。底层 `TVPIsSoftwareRenderManager` 的 process-static
cache 和 RenderManager 来源已由先前 renderer root slice 闭合；本函数只做逻辑取反。

## 11. 与本地源码和测试逐行对照

本地实现对应：

- `cpp/plugins/motionplayer/main.cpp:647`：11 参数 namespace callback 转发；
- `cpp/plugins/motionplayer/main.cpp:665`：availability 的逻辑取反；
- `cpp/plugins/motionplayer/PlayerRenderInternal.cpp:882`：四 strip 清零 helper；
- `cpp/plugins/motionplayer/PlayerRenderInternal.cpp:910`：owner、clip、空交集、strict Layer
  conversion、software/GPU setup、完整操作矩阵和最终 update；
- `cpp/plugins/motionplayer/PlayerRenderInternal.cpp:1217`：两个 owning Variant 的 by-value
  ABI wrapper。

现有单元覆盖对应：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:6258`：四个 clip property 的 probe/read 顺序、
  空交集不读取无效 source，以及 `op==1` 的完整 dst clip Integer fill；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:14968`：Motion namespace ownership、wrapper
  member/receiver gate、11 参数不足时 result Void；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:14992`：zero-arg availability 的负计数失败、
  surplus argument 不转换和 `!TVPIsSoftwareRenderManager()` 返回值。

四端软件公式、GPU method 名、shader、blend tuple、静态 threshold parameter、clip/dispatch
顺序与本地逐行一致。本轮无需修改 C++ 运行语义，只新增证据、台账状态和 IDB 元数据。

## 12. 状态结论与验证边界

`Motion #35 doAlphaMaskOperation` 与 `Motion #36 getD3DAvailable` 已从
`BODY_PENDING_SEPARATE_SLICE` 提升为 `IMPLEMENTED`。NCB 总账的 pending 数由 89 降为 87，
`IMPLEMENTED` 由 41 增为 43；316 条注册证据仍为 316/316，`UNMAPPED=0`。

本轮完成四端 fresh decompile/disassembly、UTF-16LE raw-byte 搜索、源码/测试逐行对照、
IDB 命名/注释/书签/保存、确定性台账重生成和 strict TSV 校验。当前环境没有 CMake、Ninja、
Emscripten，独立语法检查也受缺失第三方头文件阻塞，因此不宣称正式 native/Web 构建或
运行测试已完成。剩余完整 root-reachable helper、对象/容器和 87 个 pending NCB callback
仍须继续闭合，整个 motionplayer 目标尚未完成。
