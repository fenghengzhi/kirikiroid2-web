# MotionPlayer basic Layer dispatch natural-failure 边界四端复核（2026-08-16）

## 1. 结论

继图像传输 dispatch block 之后，本轮复核了 portable render helper 中剩余的 basic Layer
调用：`setSize`、argc=4/5 `fillRect`、argc=4/0 `setClip` 以及 Layer integer property
读取。四份当前参考二进制共同证明：这些调用都直接使用外围 renderer 已经建立的 receiver/
objthis owner；不存在 helper 入口的 null-to-HRESULT 或 null-to-zero recovery。

因此 `PlayerRenderInternal.cpp` 删除以下 synthetic guard：

- `callLayerSetSizeReal_guess` 的 null receiver -> `TJS_E_FAIL`；
- `callLayerFillRect4_guess` / `callLayerFillRect5_guess` 的 null receiver ->
  `TJS_E_FAIL`；
- `callLayerSetClip_guess` / `callLayerResetClip_guess` 的 null Layer class/objthis ->
  `TJS_E_FAIL`；
- `callLayerPropGetInt_guess` 的 null Layer class/object -> Integer 0。

这些 helper 是 native 内联 block 的 portable source extraction，不是新的容错 API。owner
损坏时现在恢复直接解引用的 natural-failure 边界；valid renderer 数据流、argv、member hint、
objthis 和 HRESULT 忽略策略不变。

## 2. `setSize(Real, Real)`

accurate renderer 的 source-copy/masked-layer 路径与普通 renderer/command builder 都复用相同
argument shape。fresh 四端 member setup 与间接 call：

| 目标 | member setup | indirect `FuncCall` |
|---|---:|---:|
| Android arm64-v8a | `0x6C78F4` | `0x6C791C` |
| Android armv7 | `0x590B56` | `0x590B6A` |
| iOS arm64 | `0x10011B018` | `0x10011B030` |
| iOS armv7 | `0x119570` | `0x119586` |

每端在进入 member setup 前已经建立具体 Layer receiver，并已构造两个 type-5 Real Variant。
setup 到 dispatch 之间没有条件分支；调用返回后直接析构参数。参考函数中也没有可承载
portable null check 的独立 `callLayerSetSize` function boundary。

## 3. `fillRect` 的两种故意参数形状

command builder 的 leaf clear 使用 argc=5：

| 目标 | member setup | indirect `FuncCall` |
|---|---:|---:|
| Android arm64-v8a | `0x6C3654` | `0x6C367C` |
| Android armv7 | `0x58D6C6` | `0x58D6CE` |
| iOS arm64 | `0x1001177C4` | `0x1001177DC` |
| iOS armv7 | `0x115244` | `0x11525A` |

ancestor short-circuit 则故意使用 argc=4，并忽略 `TJS_E_BADPARAMCOUNT`：

| 目标 | member setup | indirect `FuncCall` |
|---|---:|---:|
| Android arm64-v8a | `0x6C81C4` | `0x6C81EC` |
| Android armv7 | `0x59134E` | `0x591362` |
| iOS arm64 | `0x10011B87C` | `0x10011B894` |
| iOS armv7 | `0x119DB8` | `0x119DCE` |

两组 block 都是 receiver vtable load、argv setup、间接调用、参数析构的连续序列，没有 null
receiver recovery。argc=4 分支的语义尤其说明不能把 helper error 当成 admission：即便真实 TJS
member 返回错误，caller 仍终止 ancestor walk 并继续后续 publication/operate 流程。

## 4. `setClip` / reset

viewport branch 的 argc=4 `setClip` 代表性四端 block：

| 目标 | member setup | indirect `FuncCall` |
|---|---:|---:|
| Android arm64-v8a | `0x6C49DC` | `0x6C4A00` |
| Android armv7 | `0x58E482` | `0x58E490` |
| iOS arm64 | `0x100118880` | `0x1001188A0` |
| iOS armv7 | `0x116C28` | `0x116C3E` |

完整 item walk 后的 argc=0 reset：

| 目标 | member setup | indirect `FuncCall` |
|---|---:|---:|
| Android arm64-v8a | `0x6C63B8` | `0x6C63DC` |
| Android armv7 | `0x58FA72` | `0x58FA82` |
| iOS arm64 | `0x100119F14` | `0x100119F38` |
| iOS armv7 | `0x117F84` | `0x117FAE` |

两类调用都经 Layer class receiver、target Layer objthis 分派。四个 Real clip 参数或 argc=0
reset 已经由外围 branch 决定；helper 不负责验证 owner，也不在 owner 缺失时伪造失败 HRESULT。

## 5. integer property helper 的真实失败边界

accurate renderer 的 target `width`/`height` 读取调用了四端可独立识别的小 helper：

| 目标 | property helper | width call | height call |
|---|---:|---:|---:|
| Android arm64-v8a | `0x6C6D98` | `0x6C7150` | `0x6C7178` |
| Android armv7 | `0x5900E0` | `0x5904D4` | `0x5904F4` |
| iOS arm64 | `0x10011A72C` | `0x10011AA88` | `0x10011AAB0` |
| iOS armv7 | `0x1189DC` | `0x118E46` | `0x118E78` |

fresh decompile 的共同伪代码为：

```text
Variant value;
accessor.dispatch->PropGet(flags, member, hint, &value, objthis);
result = value.AsInteger();
value.~Variant();
return result;
```

helper 首先从 retained accessor 内直接取 dispatch 并解引用 vtable；没有 null test。PropGet
HRESULT 不参与分支，输出 Variant 仍经 `AsInteger` 转换并析构。Android arm64 因优化器把
`AsInteger` switch 展开到 caller-visible code，但边界与另外三端一致。

所以本地旧 `if(!layerClassObject || !layerObject) return 0` 同时伪造了 owner recovery 和
一个从未发生的 property value。删除后，owner 失败与参考实现一致；真实 PropGet failure 的
Variant/AsInteger 行为保持不变。

## 6. 源码、测试与 IDB

`cpp/plugins/motionplayer/PlayerRenderInternal.cpp` 删除上述六组 pointer guard，并在 basic
dispatch helper 起点说明它们是 trusted inline extracts。已有
`Player_renderToCanvas helpers preserve trusted TJS argv contracts` 单元用例继续覆盖：

- `setSize` 的两个 Real 参数；
- argc=4/5 `fillRect` 的精确 type/value；
- argc=4/0 `setClip` 的 receiver/objthis 与参数；
- width property 的 receiver/objthis 和 Integer conversion。

四份 recovery IDB 的 command builder、canvas renderer、accurate renderer 以及四个真实
property helper 共 16 个函数入口都追加 natural-failure 注释，随后四份 IDB 均已原位保存。

绝对地址只记录在本文与 recovery IDB，不进入编译源码注释。

## 7. 验证

- ordinary Emscripten 单元翻译单元 syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `Web Debug Build` 的 `motionplayer` target：通过；
- `Wasmtime Headless Debug Build` 的 `motionplayer` target：通过；
- 完整 `Web Debug Build` 最终 `index.html`/Wasm 链接：通过；
- basic dispatch helper scoped residual scan：六组目标 guard 均为零命中；
- scoped `git diff --check`：通过；新文档无行尾空白。

输出只有既有 `_tss`、pthread + memory-growth、JSPI experimental 与 JS library dependency
warning，本轮没有新增编译或链接错误。
