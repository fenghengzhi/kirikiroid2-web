# MotionPlayer `doAlphaMaskOperation` 四参考函数体审计（2026-08-11）

> **2026-08-16 supersession：**本文第 1～3 节记录的四参考地址、裁剪顺序、
> 空交集行为、mode/op 分支和逐像素公式仍然有效；第 4 节中指出的旧移植偏差也
> 仍然有效。但是，第 5～6 节把参考实现的 `fillRect`/`update` 误解释成
> `tTJSNI_BaseLayer::FillRect`/`UpdateByScript` 原生调用，并且没有恢复参数 1/4 的
> 按值 `tTJSVariant` ABI、目标对象的独立持有和严格/延迟 Layer 转换。相关所有权、
> TJS 派发、临时对象析构和自然失败边界结论已由
> `motionplayer_alpha_mask_variant_abi_script_dispatch_four_binary_2026-08-16.md`
> 取代。第 5～6 节保留为历史实施记录，不再代表当前源码或最终参考结论。

## 1. 四文件映射与新反编译

| 目标 | 函数 | 大小 | Hex-Rays 伪代码 |
| --- | ---: | ---: | ---: |
| Android arm64 | `sub_6AC4E4@0x6AC4E4` | `0x185C` | 1131 行 |
| Android armv7 | `sub_57E1E8@0x57E1E8` | `0xEA8` | 1060 行 |
| iOS arm64 | `sub_100104E68@0x100104E68` | `0x13E8` | 1054 行 |
| iOS armv7 | `sub_10243C@0x10243C` | `0x11C8` | 1077 行 |

四份函数均在本轮重新反编译。11 个源码级参数在四目标中一致：

```cpp
(dstLayer, dstX, dstY,
 srcLayer, srcX, srcY,
 width, height, threshold, maskMode, op)
```

## 2. 共同控制流

### 2.1 裁剪和空交集

函数只读取目标 layer 的 `clipLeft/clipTop/clipWidth/clipHeight`。共同裁剪顺序：

```cpp
clipRight  = clipLeft + clipWidth;
clipBottom = clipTop  + clipHeight;

if (clipLeft > dstX) {
    srcX += clipLeft - dstX;
    width -= clipLeft - dstX;
    dstX = clipLeft;
}
if (clipTop > dstY) {
    srcY += clipTop - dstY;
    height -= clipTop - dstY;
    dstY = clipTop;
}
if (clipRight < dstX + width)
    width = clipRight - dstX;
if (clipBottom < dstY + height)
    height = clipBottom - dstY;
```

没有读取源 layer 的 clip 或图像尺寸，也没有修正负 `srcX/srcY`。有效交集条件是 `width >= 1 && height > 0`，等价于 `width > 0 && height > 0`。

没有交集时，函数不解析/访问源 layer：

```cpp
if (op == 1)
    dstLayer.fillRect(clipLeft, clipTop, clipWidth, clipHeight, 0);
return;
```

这里不检查 `maskMode`；即使 mode 非 0/1，`op==1` 的空交集仍清完整目标 clip。`fillRect` 自身负责更新该区域，函数不会再显式调用 `update`。

### 2.2 有效交集下的外部清零

只有 `maskMode` 为 0 或 1 且 `op==1` 时，先用四次 `fillRect(..., 0)` 清除目标完整 clip 中 overlap 之外的四个互不重叠条带：

```text
left   = [clipLeft, clipTop, dstX, clipBottom)
right  = [dstX+width, clipTop, clipRight, clipBottom)
top    = [dstX, clipTop, dstX+width, dstY)
bottom = [dstX, dstY+height, dstX+width, clipBottom)
```

`op==5` 虽然低两位也等于 1，但不会执行这个清零。非 0/1 的 `maskMode` 也不会执行。

### 2.3 mask mode 与 op

四目标软件分支逐字节公式一致。设 `S=src.alpha`、`D=dst.alpha`：

| `maskMode` | `op` | 软件结果/行为 |
| ---: | ---: | --- |
| 1 | 1 | `D = S * D / 255` |
| 1 | 2 | `D = (255-S) * D / 255` |
| 1 | 5 或 6 | `D = S + (255-S) * D / 255` |
| 0 | 1 | `S < threshold` 时 `D = 0` |
| 0 | 2 | `S >= threshold` 时 `D = 0` |
| 0 | 5 或 6 | `S >= threshold` 时 `D = 255` |
| 其他 | 任意 | overlap 像素不变 |
| 0/1 | 其他 | overlap 像素不变 |

GPU 路径与软件路径采用同一语义，使用六个惰性初始化的 program/blend 组合：`AlphaMask`、`AlphaMaskRev`、`AddAlphaMask`、`AlphaMaskThreshold`、`AlphaMaskThresholdCrop`、`AlphaMaskThresholdFill`。阈值 shader 在四份产物中相同。

无论 mode/op 是否支持，只要裁剪后的 overlap 非空，尾部都执行：

```cpp
dstLayer.update(dstX, dstY, width, height);
```

四个外部 `fillRect` 位于像素合成之前，各自通过 layer API 更新对应条带；尾部 `update` 只覆盖 overlap。

## 3. 平台/编译差异

- Android 用 `fillRect`/`update` 宽字符串；iOS 反编译把相同宽字符串错误显示为首字符 `"f"`/`"u"`。参数个数、构造的五个 fill 参数和四个 update 参数一致。
- Android arm64 的寄存器分配和 variant 临时对象内联最多，函数也最大；两份 32 位产物有显式栈保护尾部。
- 软件/GPU 选择 helper、native layer/texture 偏移、guard/static program 存储布局均随 ABI 变化；裁剪、分支、像素公式和调用次序没有语义差异。

## 4. 本地逐行对照（修改前）

当前实现位于 `cpp/plugins/motionplayer/PlayerRenderInternal.cpp` 的
`applyMotionAlphaMask_guess`。旧名中的 `0x6AF104` 属于过时的单目标映射；在当前 Android
arm64 参考 IDB 中，该地址实际落在 `Player_readInitialParameterValue_guess` 中间，不能再用作
alpha-mask compositor 的身份。对照结果：

| 共同参考行为 | 修改前本地行为 | 结论 |
| --- | --- | --- |
| 只按目标 clip 裁剪 | 额外裁剪负源坐标、源图尺寸、目标图尺寸 | 改变越界/非法输入边界；应移除额外容错 |
| 空交集且 `op==1` 清完整目标 clip，且不访问源 layer | 一开始就要求源 layer 有效；只尝试清 requestedRect 与无效 overlap 的差集 | 所有权/早退与清除范围错误 |
| 有效交集仅 `op==1` 清完整 clip 的 overlap 外部 | `(itemFlags & 3)==1`，同时命中 `op==1` 和 `op==5`；outer rect 是 requestedRect | `op==5` 被误清，`op==1` 清除范围过小 |
| 非 0/1 mode 不改像素 | 任意非零 mode 都走 alpha 公式 | 非法 mode 边界错误 |
| 每个非空 overlap 末尾调用 script `update` | 直接写 bitmap 后不调用 layer update | 缺少更新通知/onPaint 语义 |
| 外部条带通过 layer `fillRect` | 直接 `bitmap.FillMask`，不经过 layer face、ImageModified、Update | 绕过 layer API 生命周期/更新语义 |
| 六个 op 公式 | CPU 公式与参考一致 | 保留 |
| 阈值 `<`/`>=` 边界 | 与参考一致 | 保留 |

## 5. 修改方案

1. 先解析目标 layer 并按目标 clip 做四步裁剪；源 layer 延迟到确认 overlap 非空之后解析。
2. 空交集时仅对 `op==1` 调用 `dstLayer->FillRect(dstClip, 0)`。
3. 用 `maskMode==0 || maskMode==1` 限制有效 mode；只有有效 mode 的 `op==1` 清四条带。
4. `clearLayerAlphaOutsideRect` 改用 `tTJSNI_BaseLayer::FillRect`，恢复 face、ImageModified 与每条带 Update。
5. 保留已正确的六组 CPU 公式；mode 非 0/1 或 op 不支持时不写像素。
6. 每个非空 overlap 尾部调用 `dstLayer->UpdateByScript(overlapRect)`，恢复脚本 `update` 的 `CallOnPaint` 行为。

## 6. 实施与验证结果

上述六项已全部落入 `PlayerRenderInternal.cpp`；`main.cpp` 的 namespace entry 继续复用同一 compositor。修改后：

- `git diff --check` 通过。
- `out/web/debug` 增量构建成功：10 个步骤完成，`PlayerRenderInternal.cpp`、`motionplayer` 静态库和最终 WebAssembly 均重新链接。
- `out/wasmtime/debug` 的 `krkr2_wasmtime_guest` 增量构建成功：9 个步骤完成并重新生成 guest wasm。
- 构建输出只有仓库既有的 TJS、imagepacker 与 Emscripten warnings，没有本轮新增错误。
- 完整 Wasmtime motion-playback 执行仍受主机 Python `wasmtime/PyOpenGL/glfw/Pillow` 缺失与 PyPI DNS 不可达限制；这不影响两套编译验证。

四个 IDB 均先通过 rename dry-run，再将本函数写为 `Motion_doAlphaMaskOperation_guess` 并保存成功。名称保留 `_guess`，因为注册字符串能证明脚本方法名，但不能证明原始 C++ 符号的精确拼写。
