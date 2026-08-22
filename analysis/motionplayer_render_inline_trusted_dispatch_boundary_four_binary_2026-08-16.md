# MotionPlayer render 内联图像传输块 trusted-input 边界四端复核（2026-08-16）

## 1. 结论

重新检查 `reference/binaries/` 四份 1.3.9 产物后，确认 portable
`callLayerOperateAffine_guess`、`callLayerAffineCopy_guess`、
`callLayerOperateRect_guess` 与 `callLayerMeshFamily_guess` 入口原有的 null/type
筛选不是参考实现的 admission boundary。

这些 helper 在本地只是从 command builder、完整 canvas renderer 和 accurate
`SeparateLayerAdaptor` renderer 的内联 Variant/`FuncCall` 片段中抽出的源码复用单元。
四端内联片段共同表现为：

1. 直接复制 source `tTJSVariant`，不先检查 `Type()==tvtObject`，也不调用
   `AsObjectNoAddRef()` 作 admission；
2. 直接使用外围 renderer 已经建立的 receiver、objthis 和 point storage；
3. 顺序物化参数 Variant 与 argv；
4. 执行一次间接 `FuncCall`；
5. normal renderer 不检查 HRESULT，立即按逆构造顺序销毁参数。

因此本轮删除抽取 helper 新增的 receiver/point null-recovery 和 source Object 类型筛选。
source Variant 现在原样进入 argv；若被调 TJS member 需要 Object，转换或拒绝属于该 member
边界。无效 receiver/points 则恢复为 trusted internal input 的自然失败，而不是由一个参考实现
不存在的 helper 入口提前返回 `TJS_E_FAIL`/`TJS_E_NATIVECLASSCRASH`。

## 2. 完整函数身份

这些 portable helper 不是四端 native call graph 中的真实独立函数。fresh xref 仍把所有生产
member literal 归入以下三个完整函数：

| 目标 | command builder | canvas renderer | accurate SLA renderer |
|---|---:|---:|---:|
| Android arm64-v8a | `0x6C2208` | `0x6C4820` | `0x6C7088` |
| Android armv7 | `0x58C7C4` | `0x58E2CC` | `0x590468` |
| iOS arm64 | `0x1001167BC` | `0x1001186E0` | `0x10011A9E8` |
| iOS armv7 | `0x114118` | `0x11653C` | `0x118D70` |

这也限定了本轮结论的含义：删除的是“源码抽取以后额外添加”的 admission guard，不是把某个
真实 native helper 的参数校验猜成不存在。

## 3. direct affine 与 buffered rect

`operateAffine` 的 member setup、间接调用如下：

| 目标 | member setup | indirect `FuncCall` |
|---|---:|---:|
| Android arm64-v8a | `0x6C6130..0x6C6148` | `0x6C6154` |
| Android armv7 | `0x58F876..0x58F882` | `0x58F88A` |
| iOS arm64 | `0x100119E4C..0x100119E5C` | `0x100119E64` |
| iOS armv7 | `0x1169DE..0x1169F0` | `0x1169F4` |

四端在 literal setup 前已经把 15 个参数槽指针连续写入 argv；从 member setup 到间接调用
没有条件分支，返回后第一批可见操作就是参数 Variant 析构。不存在 portable helper 原来的：

```text
if receiver/objthis/points is null: return error
if source.Type != Object or source.object is null: return error
```

buffered `operateRect` 同样成立：

| 目标 | member setup | indirect `FuncCall` | owning source copy |
|---|---:|---:|---:|
| Android arm64-v8a | `0x6C5914` | `0x6C5938` | `0x6C586C` |
| Android armv7 | `0x58F1FC` | `0x58F216` | `0x58F172` |
| iOS arm64 | `0x1001194CC` | `0x1001194E4` | `0x10011941C` |
| iOS armv7 | `0x117ACE` | `0x117AE4` | `0x117A28` |

这里 source 是 retained `bufLayer` Variant。owning copy、九参数 argv 和 `FuncCall` 之间没有
source type admission；HRESULT 也不控制后续 frame helper 或 item walk。

## 4. copy-family 与 mesh operate-family

canvas copy-family 的 member setup 到间接调用之间均为无条件直达：

| method | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `affineCopy` | `0x6C547C -> 0x6C54A4` | `0x58ED12 -> 0x58ED18` | `0x100119208 -> 0x100119220` | `0x1177F4 -> 0x11780A` |
| `meshCopy` | `0x6C5270 -> 0x6C5298` | `0x58EAFE -> 0x58EB06` | `0x100118E84 -> 0x100118E9C` | `0x1173B0 -> 0x1173C6` |
| `bezierPatchCopy` | `0x6C5638 -> 0x6C5660` | `0x58EED0 -> 0x58EED6` | `0x100119010 -> 0x100119028` | `0x117590 -> 0x1175A6` |

同样的 source-copy 语义也出现在 command builder 与 accurate renderer 的 copy-family block；
fresh UTF-16LE search/xref 没有发现位于完整函数之间、可承载这些 admission guard 的独立 helper。

operate-family 证据：

| method | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `operateMesh` | `0x6C5F30 -> 0x6C5F54` | `0x58F6D4 -> 0x58F6EA` | `0x100119A9C -> 0x100119AB4` | `0x1167F6 -> 0x11680C` |
| `operateBezierPatch` | `0x6C6300 -> 0x6C6324` | `0x58F9E6 -> 0x58F9FC` | `0x100119C48 -> 0x100119C60` | `0x116B68 -> 0x116B7E` |

每个 block 在 source owning copy 后只按既有 geometry branch 进入对应参数物化；进入具体
method block 后没有 source `Type`/null 分流。`callLayerMeshFamily_guess` 因而可以继续复用
copy/operate 两种 argv shape，但不能在共同入口添加参考内联块不存在的筛选。

## 5. 生产调用者的前置所有权

删除 guard 不会放宽完整 renderer 的 geometry admission：type `0/1/2` 分支仍由外围函数
决定，mesh Array 仍按原有 owner 顺序创建与释放。normal production callers 在进入这些抽取
block 前已经建立：

- concrete target Layer owner；
- Layer class receiver（operate-family）；
- stack/local point array 或 owning TJS mesh Array；
- owning source Variant；普通 canvas/accurate 路径还会先通过 source object 读取尺寸。

也就是说，valid render path 的输出不变；变化只发生在直接误用 internal extracted helper 或
外围不变量已经损坏时。旧实现会伪造一个早期 HRESULT，参考边界则继续复制 Variant/解引用
trusted pointer，并由实际 TJS dispatch 或自然失败决定结果。

## 6. 源码与回归测试

`cpp/plugins/motionplayer/PlayerRenderInternal.cpp` 删除四处 synthetic guard：

- `callLayerOperateAffine_guess`；
- `callLayerAffineCopy_guess`；
- `callLayerOperateRect_guess`；
- `callLayerMeshFamily_guess`。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 的 render-helper argv contract 测试新增 Integer
source Variant。测试分别确认 `affineCopy`、`operateAffine`、`meshCopy`、`operateMesh` 和
`operateRect` 仍发生一次 dispatch，且 source 在对应 argv 槽保持 Integer 91，而不是在
portable helper 入口被过滤掉。

绝对地址只保留在本文和 recovery IDB，不进入编译源码注释。

## 7. recovery IDB

四份 recovery IDB 的 command builder、canvas renderer 和 accurate renderer 共 12 个完整
函数入口都追加 trusted-inline dispatch 注释，记录 receiver/points/source 无 helper admission、
source Variant 原样复制、normal HRESULT 不分流。四份 recovery IDB 随后均已原位保存。

## 8. 验证

- ordinary Emscripten 单元翻译单元 syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `Web Debug Build` 的 `motionplayer` target：通过；
- `Wasmtime Headless Debug Build` 的 `motionplayer` target：通过；
- 完整 `Web Debug Build` 最终 `index.html`/Wasm 链接：通过；
- 定向 residual scan 确认四个 transfer helper 不再包含 `sourceObject.Type()`、
  `sourceObject.AsObjectNoAddRef()` 或 receiver/points admission pattern；
- scoped `git diff --check`：通过；新分析文档无行尾空白。

输出只有项目既有的 `_tss` literal-operator、pthread + memory-growth、JSPI experimental 与
JS library dependency warning，本轮没有新增编译或链接错误。
