# Player internal render Layer materializer（四参考二进制，2026-08-27）

## 1. 入口与完整取证

| 端 | materializer | body instructions | iOS armv7 SjLj cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6CB57C` | 398 | — |
| Android armv7 | `0x592F7C` | 212 | — |
| iOS arm64 | `0x10011E2BC` | 178 | — |
| iOS armv7 | `0x11CAC8` | 298 | `0x11CDE0`, 131 instructions |

四端 1086 条主函数指令和四份 fresh decompile已经完整取得；iOS armv7的 22-state、
131 条 SjLj cleanup也已完整读取。四端入口、armv7 cleanup已命名/注释/bookmark并保存
IDB。

Android arm64把共享 Layer factory内联进本函数，另外三端保留独立 factory调用；这解释
了 body体积差异。iOS armv7另为可抛调用建立 SjLj state machine。除此之外，gate、
Variant owner、persistent发布时点、尺寸读取、两次 Layer创建和失败残留完全一致。

## 2. 唯一 gate：primary Variant type

materializer只检查 Player primary `_internalRenderLayer` Variant的 type word是否为 Void：

| 端 | primary Variant | type word | work Variant |
|---|---:|---:|---:|
| Android arm64 | Player `+0x2B8` | `+0x2C8` | `+0x2E0` |
| Android armv7 | Player `+0x1D0` | `+0x1D8` | `+0x1E8` |
| iOS arm64 | Player `+0x248` | `+0x258` | `+0x270` |
| iOS armv7 | Player `+0x190` | `+0x198` | `+0x1A8` |

type非 Void时立即返回。它不要求 primary是 Object，不检查 work Layer是否存在/有效，也
不尝试修复尺寸。因此 Integer、String、typed-null Object或任意非 Void residue都同样
永久关闭这一 lazy创建窗口。

## 3. target owner与 `window` Variant

进入创建路径后，四端先复制完整 target Variant，从临时值严格取得 Object-only AddRef，
再立即析构临时 Variant。这个 target raw owner活到整个 materializer尾部；所有属性读取
都以它同时作为 receiver和 objthis。

随后通过 process-global `window` hint读取一次 target.window，保存为完整 Variant owner。
PropGet HRESULT不参与分支；getter留下的 Variant值原样成为两次 Layer factory调用的
owner参数。两个 factory的 parent参数始终是原始完整 target Variant，而不是 target raw
owner，也不会在第二次创建前重读 `window`。

本地：

```cpp
ncbPropAccessor targetAccessor{tTJSVariant(target)};
tTJSVariant owner = targetAccessor.GetValue("window", VariantTag, ...);
```

保持了相同的 Object-only accessor owner和完整 window Variant生命周期。

## 4. primary Layer 创建与提前发布

第一次 factory等价于 `createLayerVariant_guess(owner, target)`。共同 factory边界是：

1. 取得全局 script dispatch raw owner；
2. `CreateNew("Layer", argc=2, [owner,target], objthis=global)`；
3. 忽略 HRESULT；
4. 以 `created`同时作为 Object/ObjThis构造结果 Variant；
5. Release raw `created`，再 Release raw global；
6. 返回完整 Layer closure Variant。

没有 null/status保护；普通失败若留下 null `created`会继续触及 raw release/Variant边界，
调用者不提供 fallback。

factory结果首先 copy-assign到 persistent primary Variant。这个发布发生在以下动作之前：

- primary Layer严格 Object转换；
- height/width读取；
- primary `setSize`；
- work Layer创建和发布。

赋值后又从 persistent primary复制临时 Variant、取得一份 Object-only AddRef、立即销毁
临时 Variant与factory result；留下的 primary raw owner一直活到 work raw owner释放之后。

## 5. 尺寸读取与第一次 `setSize`

primary发布并取得 raw owner后，四端固定：

1. `HasValue("height")`，存在则 Integer `GetValue("height")`，否则 0；
2. `HasValue("width")`，存在则 Integer `GetValue("width")`，否则 0；
3. primary.`setSize(width,height)`。

两个 getter都复用最初的 target accessor和各自 process-global hint。没有正值gate、范围
修正或 width/height交换。调用创建 width Integer Variant后创建 height Integer Variant，
argv顺序为 width、height；无 result，HRESULT忽略；返回后先析构 height再析构 width。

这也意味着 ordinary failed HRESULT不阻止 work Layer继续创建；只有真正抛出的异常才
中断控制流。

## 6. work Layer 创建、发布与正常清理

第二次调用同一个 factory，参数仍是最初保留的 window Variant和原始 target Variant。
factory结果先 copy-assign到 persistent work Variant，然后从 persistent work取得独立
Object-only owner，销毁临时 copy与factory result，再发完全相同的
`work.setSize(width,height)`。

正常尾部 owner逆序为：

1. work setSize的 height、width参数 Variant；
2. work raw Object owner；
3. primary raw Object owner；
4. window Variant；
5. target accessor的 Object-only owner。

iOS armv7 22-state cleanup逐一覆盖两组尺寸参数、两个factory result、window Variant和
三个 raw Object owner，确认异常只清理已经构造的局部 owner，不撤销 persistent赋值。

## 7. 部分失败与不可修复状态

失败时点决定下次是否会重试：

- target转换、window getter或第一次 factory在 primary赋值前抛出：primary仍为 Void，
  后续调用可以重试；
- primary赋值之后的任何异常：primary已非 Void，后续调用立即返回；
- primary Object转换或第一次 setSize抛出：work仍可能为 Void，但不会再补建；
- 第二次 factory在 work赋值前抛出：primary保留，work仍为旧值；
- work赋值后的 Object转换或 setSize抛出：两个 persistent Variant都保留，尺寸可能只
  部分应用；
- 两次 setSize的普通失败 HRESULT均被忽略，函数继续或正常返回；
- materializer从不验证 Layer validity，也不清理任何已发布 Variant。

这是刻意的 one-shot lazy publication，不应改写成事务式初始化或“检测 work缺失后补建”。

## 8. 本地对照与验证状态

本地 `Player::materializeInternalRenderLayers_guess`、
`getInternalWorkspaceDimension_guess`、`setInternalWorkspaceLayerSize_guess`和共享
`createLayerVariant_guess`已经保持上述四端共同结构；本项无需新的语义代码修改。

本项标记 `IMPLEMENTED`。已完成四份 fresh decompile、全部 1086 条主函数指令、完整
131 条 armv7 cleanup、本地 factory/setSize封装展开对照、IDB命名/注释/bookmark/save、
coverage 12列检查与 `git diff --check`。现有 workspace-dimension probe覆盖双读、hint、
objthis、缺失为零和负 Integer不钳制。

正式 CMake/unit/Web build仍因本机缺少 CMake、Ninja、Emscripten且没有既有 build/out
目录而未运行。
