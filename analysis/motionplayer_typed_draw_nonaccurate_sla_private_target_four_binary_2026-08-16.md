# motionplayer typed draw 与非 accurate SLA 私有目标：四二进制复核

日期：2026-08-16

## 结论

本轮重新以 `reference/binaries/` 中四个参考二进制为准，收紧了 `Player::draw` 的两个 typed-adaptor 分支，以及 non-accurate SeparateLayerAdaptor（SLA）分支的私有 Layer 生命周期和命令构建顺序。先前移植代码中若干“安全封装”与状态发布并不存在于参考实现：SLA 原生指针已经在 `draw` 调用点解包，目标 Layer 由 adaptor 的 `targetLayer` 严格取得，私有 Layer 仅在其 Variant 为 Void 时创建，并在每次 draw 时跟随目标 Layer 的 Rect 尺寸。non-accurate 分支把 builder 返回的 stencil candidate 数写入私有 Layer 原生状态，并仅在没有递归投递窗口更新事件时调用 `Update(false)`。

两个 typed outer helper 以及 `D3DAdaptor` 的 Player-render helper 都是 `void`。旧移植代码中的 `bool` 返回值、重复 NCB 解包、友好失败路径、正尺寸门控、图像尺寸回退、source-key 门控和 `_lastCanvas` 发布都没有四目标交叉证据，现已删除；V246 又证明 `_lastCanvas` 整个字段不存在于四端 Player 布局。

下列恢复名带 `_guess` 的部分仍然表示 stripped 符号语义命名，不宣称是原始 C++ 标识符。

## 样本与函数地址

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| SLA typed outer helper | `0x6D2A38` | `0x597328` | `0x1001233C8` | `0x12257C` |
| `Player::draw` 中 SLA 调用点 | `0x6D348C` | `0x5978F0` | `0x100123D78` | `0x123024` |
| D3D typed outer helper | `0x6D2F70` | `0x59761C` | `0x100123844` | `0x122AAC` |
| `Player::draw` 中 D3D 调用点 | `0x6D3424` | `0x5978B0` | `0x100123D10` | `0x122FCC` |
| `D3DAdaptor` Player-render member | `0x6AB204` | `0x57D2CC` | `0x100104284` | `0x101680` |
| ensure private motion GLL | `0x6D2D28` | `0x5974D0` | `0x100123670` | `0x122884` |
| private command builder | `0x6DBB18` | `0x59CB20` | `0x10012B7D0` | `0x12A304` |
| SLA outer 中 builder 调用点 | `0x6D2BA0` | `0x59741C` | `0x10012352C` | `0x122742` |

## typed outer helper 的真实 ABI

四个 `Player::draw` 版本在识别 NCB 类后，都会先从 TJS 对象取得已经解包的原生 adaptor 指针，再调用相应 typed helper。调用返回后直接跳入 `draw` 的公共清理路径；没有目标读取返回寄存器、归一化布尔值或按返回值分支。两个 outer helper 自身也都以容器析构/栈清理结束，没有语义返回值。

因此本地声明恢复为：

```cpp
void Player::renderToSeparateLayerAdaptor(SeparateLayerAdaptor *sla);
void Player::renderToD3DAdaptor(D3DAdaptor *adaptor);
```

这还修正了一项源结构错误：SLA helper 不再接收 `iTJSDispatch2 *` 后重复执行一次 NCB native-instance 查找。`Player::draw` 已经完成这一层类型识别和解包，callee 直接消费 `SeparateLayerAdaptor *`。

## D3D 路径的数据流与所有权

D3D outer helper 的四目标调用序列一致：

| 目标 | prepare | projection | adaptor member |
|---|---:|---:|---:|
| Android arm64 | `0x6D2FB0` | `0x6D2FBC` | `0x6D2FCC` |
| Android armv7 | `0x59764C` | `0x597654` | `0x59765E` |
| iOS arm64 | `0x100123874` | `0x100123880` | `0x100123890` |
| iOS armv7 | `0x122B1C` | `0x122B26` | `0x122B34` |

流程为：构造 main/aux prepared-item vector，调用 prepare，成功时计算投影，随后把 `Player *` 和 main list 交给 adaptor member，最后析构两个 vector。反编译的 adaptor member 形态稳定为 `this = D3DAdaptor *`、第二参数 `Player *`、第三参数 prepared-item vector。它先以 `canvasCaptureEnabled` 字节门控整个路径；开启后在 adaptor 一侧构造 source/target getter 函数对象，再调用 Player 的批量渲染器，投影偏移为 `+0.5, +0.5`。

因此原本挂在 `Player` 上的 `renderFromPlayer_guess` 已迁移为 `D3DAdaptor::renderFromPlayer_guess`。为了让这一 adaptor-owned helper 调用 Player 的内部 batch renderer，本地源结构以 `friend class D3DAdaptor` 表达参考调用边界。

## SLA outer 的准确顺序

四目标一致的主干是：

1. 构造 main/aux prepared-item vector。
2. `prepareRenderItems`；失败即进入 vector 清理并返回。
3. 计算投影。
4. 读取/缓存 accurate-SLA 配置。
5. accurate 分支：执行 accurate renderer，随后 CopyRef adaptor 的 `targetLayer`，再调用 post-draw helper。
6. non-accurate 分支：确保私有 Layer；从私有 Layer 原生对象读取宽高；构建命令；写入 builder 返回的 stencil count；若窗口更新事件当前不在投递中，则调用 Layer `Update(false)`。
7. 析构 main/aux vector；无返回值。

参考实现没有以下旧移植步骤：

- 再次从 TJS dispatch 解包 `SeparateLayerAdaptor`；
- 通过一个可失败的 `resolveSeparateLayerRenderTarget` facade 查找目标；
- 以目标宽高必须为正数作为 draw 门控；
- 用图像尺寸补偿 Layer Rect 尺寸；
- 将 SLA 的目标写入 Player `_lastCanvas`；
- 把 outer helper 的完成状态包装成 `bool`。

诊断模式仍可为日志解析 motion path/目标对象，但这些操作已保持在 trace/headless 门控之内，不再污染参考默认路径的属性访问顺序。

## 私有 Layer 的对象生命周期

ensure helper 的四目标入口首先严格把 `sla.targetLayer` Variant 转成 `tTJSNI_Layer *`。随后检查 adaptor 内第三个 Variant 槽（本地 `_privateMotionGLL`）：

- 仅当该 Variant 为 Void 时创建私有 Layer，并通过 Variant copy-assignment 发布；
- 只在创建时设置 absolute 与 visible；
- 不把它加入 active-map，也没有友好恢复或 fallback target；
- 无论新建还是复用，每次调用都从目标 Layer Rect 计算 `right - left` 与 `bottom - top`，并据此调整私有 Layer 尺寸；
- 返回私有 Layer dispatch/native 状态，供 outer 继续读尺寸和构建命令。

这意味着目标 Layer 的尺寸变化必须传播到同一个私有对象，而不是销毁重建。本地单元测试新增了 `64x32 -> 17x9` 的复用/重尺寸断言。

## private command builder 的容器与边界行为

四份 builder 反编译和调用点共同确定：

- `priorDraw == false` 的 preprocessing 发生在旧 private deque 清理之前；
- 随后清空旧命令 deque，再按 prepared-item 顺序构建本帧队列；
- admission 要求低 blend nibble 不等于 `6`、raw flag 16 清零、skip flag 0 清零、opacity 非零；
- prior-draw 路径额外检查 skip flag 1；
- source admission 看 `sourceState->blank == false`，不额外以 source key 是否为空门控；
- 没有 list item 空指针门控，也没有 cached-native-ResourceManager 可用性门控；
- resolved texture 为空时仍追加命令项，而不是静默丢弃；
- helper 返回 stencil candidate count；写入私有 Layer 原生状态由 outer caller 在返回后完成。

反编译 ABI 可见 builder 接收队列/尺寸/prepared list/Player 上下文等七个观察参数；本地目前仍将它表达为 `Player` 私有 member，以最小改动保持已恢复的数据依赖。这个 C++ 所属关系尚不能仅凭 stripped ABI 断言为原始类布局，因此函数名保留 `_guess`，后续获得更强类型证据时再收紧。

## 窗口更新重入边界

non-accurate outer 在写入 stencil count 后读取一个进程全局字节；字节为零才调用 Layer `Update(false)`。该全局的其他交叉引用位于 EventIntf 的窗口更新事件投递循环，故本地对应到已有全局 `TVPWindowUpdateEventsDelivering`。

| 目标 | delivery flag | Layer update wrapper |
|---|---:|---:|
| Android arm64 | `0x1ADFF14` | `0x7FD32C` |
| Android armv7 | `0x1138C33` | `0x62C6B4` |
| iOS arm64 | `0x102517783` | `0x1000750DC` |
| iOS armv7 | `0x2143963` | `0x7230E` |

各 update wrapper 都构造完整本地 Rect 后进入 Layer 内部 update。这里的条件不是“尺寸有效”或“目标可见”，而是避免在窗口更新事件已经投递时递归触发同类更新。

## 源代码变更

- `Player.h`：两个 typed helper 改为 `void`；SLA 参数改为原生 adaptor；删除 target-resolution facade；private builder 返回 `int`；D3D Player-render helper归属 adaptor。
- `PlayerDrawDispatch.cpp`：把已解包的 SLA native pointer 直接传给 typed helper。
- `D3DAdaptor.h`：声明 adaptor-owned Player-render member。
- `PlayerRenderTargets.cpp`：恢复 D3D/SLA outer 顺序、non-accurate builder admission/返回值、stencil 写回、窗口更新重入门控，并移除 `_lastCanvas` 发布与非参考 fallback。
- `PrivateMotionGLL.h/.cpp`：ensure 只接收 adaptor，严格取得目标 Layer，Void-only 创建，并在每次调用中按目标 Rect 重新定尺寸。
- `PlayerRenderInternal.h/.cpp`：删除不再使用、也无四目标证据的 `queryLayerCanvasSize` facade。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：覆盖私有 Layer 首次尺寸及复用后的尺寸同步。

## IDB 类型与注释持久化

四份 recovery IDB 均已为以下五类函数写入恢复原型和综合注释，并 force-recompile 后重新读取确认：SLA outer、D3D outer、private ensure、private builder、D3DAdaptor Player-render member。最后均通过原位 save 持久化：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 验证

- ordinary Web syntax-only（包含 `motionplayer-dll.cpp`）：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `Web Debug Build` 的 `motionplayer` target：31/31，通过；
- `Wasmtime Headless Debug Build` 的 `motionplayer` target：31/31，通过；
- 完整 `Web Debug Build`：3/3，通过，包含最终 `index.html`/Wasm 链接，因此 `TVPWindowUpdateEventsDelivering` 的跨翻译单元引用已验证；
- scoped `git diff --check`、报告尾随空白检查及旧 facade/signature 扫描：通过。

编译只出现项目既有的 `_tss` literal-operator、`imagepacker.h` `nodiscard` 与 Emscripten pthread/JSPI 警告；没有本轮新增错误或链接失败。
