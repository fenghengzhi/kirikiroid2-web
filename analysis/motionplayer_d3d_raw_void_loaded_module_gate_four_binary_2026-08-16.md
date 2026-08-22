# motionplayer D3D raw renderer：void ABI、loaded-module gate 与自然失效边界

日期：2026-08-16

## 结论

本轮 fresh 复核 `reference/binaries/` 四个参考二进制，闭合了 direct-texture/D3DAdaptor 共用 raw renderer 的剩余入口与容器边界。raw renderer、`Player` direct-texture wrapper 和 `D3DAdaptor` Player-render member 都是 `void`；参考调用点从不读取返回寄存器。旧移植中的 `bool` 成功值是人为制造的 ABI。

direct-texture wrapper 也不是以 `target != nullptr` 或 Player motion Variant 非 Void 为入口条件。它先从 Player 的 ResourceManager owner 取得原生对象，以当前 motion-context key 查 `_loadedModules`；只有 module-map miss 会在构造 prepared-item vectors 和访问 target texture 之前返回。命中后才 prepare/project，构造捕获 loaded-record 的 source callable，SetRenderTarget，并调用 raw renderer。

raw renderer 的 prepared pointer vectors 和 mask-child pointer vectors 是内部不变量容器：四端均直接解引用元素，没有空 item、空 child 或 self-child recovery。当前移植里这些防御性门控会把无效状态静默吞掉，改变原版自然 fault/UB 边界，现已删除。

恢复名保留 `_guess`，表示 stripped 符号的语义命名，而非宣称原始 C++ 标识符。

## 四端函数与调用点

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| raw renderer | `0x6AB39C` | `0x57D3DC` | `0x100104450` | `0x101850` |
| `D3DAdaptor` member 调 raw | `0x6AB2E0` | `0x57D356` | `0x100104360` | `0x10177E` |
| direct-texture wrapper | `0x6D3048` | `0x5976AC` | `0x100123970` | `0x122C10` |
| direct wrapper 调 raw | `0x6D3280` | `0x5977AC` | `0x100123AB8` | `0x122D92` |
| direct source getter | `0x6F3BAC` | `0x5B057A` | `0x100146084` | `0x1465BE` |

raw renderer 的完整 code-xref 集在四端都恰好是两个：`D3DAdaptor_renderFromPlayer_guess` 与 `Player_drawToTexture_guess`。两个 caller 在调用后都直接进入 callable/vector 清理，不做 test/branch/store。函数正常尾声为 batch flush、可选 EndStencil 和栈对象析构；32 位 Hex-Rays 先前显示的 `int` 是 stack-canary/尾部寄存器伪返回。恢复 prototype 统一为 void 八参数：target、target getter、target rect、source getter、prepared list、Player、x offset、y offset。

direct wrapper 的唯一生产 caller 是 D3DLayer listener `Draw` 链；它同样忽略结果。因此本地 ABI 改为：

```cpp
void Player::drawToTexture_guess(iTVPTexture2D *target, float x, float y);
```

## direct-texture 的入口顺序

四端 wrapper 的共同顺序是：

1. 将 Player 保存的 ResourceManager Variant 转成对象，并用 NCB class id 取得 native ResourceManager。
2. 将 Player 当前 motion-context Variant 复制/转换成查找 key。
3. 在 ResourceManager loaded-module unordered_map 中查找该 key。
4. miss：销毁临时 key owner并返回；此时没有 prepared vector，也没有读取 target。
5. hit：取得 mapped loaded-record 地址；构造 main/aux pointer vectors。
6. `prepareRenderItems`；失败则直接清理 vectors。
7. `applyPreparedRenderItemProjection_guess`。
8. 构造一个捕获 loaded-record 的 type-erased source getter。
9. RenderManager `SetRenderTarget(target)`，从 target texture 本体读取 width/height，组成 `(0,0,width,height)`。
10. 以 caller 提供的 float x/y 调 raw renderer，随后析构两个 callable 与 vectors。

因此边界不是“只要 Player 有 motion content 就可以画”。例如 ResourceManager 已 `unload` 当前 module、但 Player 仍保留 motion Variant 时，原版仍在步骤 3/4 停止。旧 `hasMotionContent()` gate 会继续使用已失效的 source records，行为错误。

参考实现也没有显式 target-null gate。module miss 或 prepare failure 可以在 target 访问前返回；一旦流程到达步骤 9，target 必须满足内部不变量，空 target 会在自然解引用处失败。本地移除了入口处的友好空值处理，保留这个有顺序的边界。

## source callable 的捕获与读取

四端 direct wrapper 构造的 callable closure 持有 loaded-record 派生指针，但四个独立 getter body 都不读取该捕获；getter 只做：

```text
return preparedItem.sourceState->texture
```

这不是 atlas retry，也不从 loaded map 二次找 source。closure 捕获仍是源结构/对象布局证据：它在 raw renderer 返回后才析构。portable 源保留 `[loadedResource]` 捕获，并显式标记 operator 内不使用该值，以同时表达 module 生命周期 gate 与 getter 的实际读取权威。

## raw renderer 的 item gate 顺序

四端逐 item 的相同顺序是：

1. 直接从 prepared pointer vector 读取并解引用 item；
2. `(blendMode & 0x0f) == 6` 跳过；
3. `skipFlag0 != 0` 或 `rawFlag16 != 0` 跳过；
4. priorDraw 且 `skipFlag1 == 0` 跳过；
5. 直接解引用 `sourceState`，其 `blank` byte 非零则跳过；
6. 读取 signed opacity；priorDraw 时执行 C++ signed `/ 2`（向零截断）；
7. `opacity <= 0 && stencilMaskRef == 0` 跳过；
8. 调 type-erased source getter；空 callable 走 `std::bad_function_call`；
9. 重新读取同一个 `sourceState` 的 `textureRect`；宽或高非正则跳过；
10. packed color、stencil transition、method selection，再按 mesh type 进入 affine/common-mesh 路径。

没有 source texture null gate。getter 可以返回空 texture，后续 batch/backend 保留其原生边界。此前本地 `opacity >= 0 ? opacity / 2 : (opacity + 1) / 2` 数值上模拟了向零除法，但参考生成形态来自普通 signed `/= 2`；portable 源已恢复为该表达式。

## stencil/clip preprocessing 的指针容器边界

`priorDraw == false` 时，四端先遍历整个 prepared pointer vector，直接把每个 item 的两个 stencil byte 清零。第二轮 candidate scan 同样直接解引用每个 item。候选条件保持为：blend low nibble 非 6、drawFlag 非零、rawFlag16 为零、opacity 非零、parentItem 非空。

分配 ref 后，从 parentItem 开始沿 parent 链遍历：

- 每个 ancestor 直接写 stencil mask ref；
- 每个 ancestor 的 child pointer vector 逐项直接解引用并写 ref；
- ancestor 本身的 drawable 判断不要求 opacity；child drawable 判断额外要求 opacity 非零；
- parent 链到 null 时结束。

参考实现没有 child-null、`child == ancestor` 或 prepared-item-null 分支。随后 clip pass 也只以 item drawFlag 作为门，直接访问 item。portable 中四处空指针/self recovery 已删除，避免把损坏的内部拓扑转成“少画一项”的静默结果。

## 源码与测试变更

- `Player.h`：direct-texture 与两个本地 raw-render overload 全部由 `bool` 改为 `void`。
- `PlayerRenderTargets.cpp`：
  - direct wrapper 恢复 ResourceManager loaded-module lookup-first 顺序；
  - 删除 target-null 与 motion-Variant gate；
  - source callable 保留 loaded-record 捕获但直接读取 persistent descriptor texture；
  - 删除 prepared item / mask child 的空值与 self-child recovery；
  - priorDraw opacity 恢复为 signed `/= 2`；
  - raw renderer 与 adaptor wrapper 不再制造/传播成功布尔值。
- `motionplayer-dll.cpp`：增加 direct-texture member pointer 的 void ABI 静态断言；增加“Player 仍有 motion content、ResourceManager 已 unload、target 为 null”用例，验证 module-map miss 必须早于 target 访问。

## IDB 更新

四份 recovery IDB 已对 raw renderer 与 direct wrapper 写入 void prototype、追加本轮 ABI/loaded-map/自然失效边界注释，并 force-recompile 后重新读取确认；四库随后均已原位保存成功。

## 验证

- ordinary Web syntax-only（包含新增 ABI 与 unload 边界测试）：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `Web Debug Build` 的 `motionplayer` target：30/30，通过；
- `Wasmtime Headless Debug Build` 的 `motionplayer` target：30/30，通过；
- 完整 `Web Debug Build`：3/3，通过，最终 `index.html`/Wasm 链接成功；
- scoped `git diff --check`、两份本轮报告尾随空白检查及旧 bool/null-recovery 形态扫描：通过。

当前环境仍没有可直接执行的 native Catch2 motionplayer binary，因此新增用例报告为完整翻译单元双配置编译通过，不虚报运行时执行。警告仅为仓库既有 `_tss`、`imagepacker.h` `nodiscard` 与 Emscripten pthread/JSPI/JS-library 提示。
