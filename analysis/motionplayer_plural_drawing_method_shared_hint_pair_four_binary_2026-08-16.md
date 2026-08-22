# plural drawing-method 共享 member-hint pair（四参考，2026-08-16）

## 结论

MotionLayerExtensions 的所有 `Layer.drawLines` dispatch 共用一个 32-bit backing word，所有
`Layer.drawBeziers` dispatch 共用紧邻的另一个 word。四个当前参考二进制中的精确 consumer
划分为：

- `drawLinesMemberHint_guess`：debug mesh、public mesh frame、public Bezier-patch mesh frame；
- `drawBeziersMemberHint_guess`：debug Bezier control frame、public Bezier-patch frame。

这两个 word 在四端始终相差 4，但彼此不相同；它们也不是 Player renderer 所用的单数
`drawLineMemberHint_guess`。旧 `MotionLayerExtensions.cpp` 分别在两个 source helpers 中各放
一份 `drawLines` local static，又在两个 Bezier helpers 中各放一份 `drawBeziers` local static，
因而形成两组错误 duplicate。V171 将其恢复为 translation-unit 内共享的相邻二槽 family。

## UTF-16LE literal 取证

普通字符串搜索会受 TJS wide literal 和 IDA string typing 影响，本轮按 UTF-16LE bytes fresh
搜索：

- `drawLines`：`64 00 72 00 61 00 77 00 4C 00 69 00 6E 00 65 00 73 00 00 00`；
- `drawBeziers`：`64 00 72 00 61 00 77 00 42 00 65 00 7A 00 69 00 65 00 72 00 73 00 00 00`。

Motionplayer literal 地址为：

| literal | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `drawLines` | `0x14C1BE0` | `0xD783B2` | `0x10195B7DA` | `0x174DB3E` |
| `drawBeziers` | `0x14C1B1C` | `0xD782EE` | `0x10195B7EE` | `0x174DB52` |

iOS 两端还各找到另一份 literal（`0x101976874/0x1768C20` 与
`0x1019767B0/0x1768B5C`），其 xrefs 位于非 motionplayer 的 `sub_1002F...`/`sub_2F...`
components。本纵切面只追踪上述 motionplayer literal，未把其他组件的同名字符串误纳入
插件 hint family。

## 函数映射

### `drawLines` consumers

| 语义函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `MotionLayer_drawMeshDebug_guess` | `0x69D1F0` | `0x5765C8` | `0x1000FAB34` | `0xF7C04` |
| `MotionLayerExtensions_drawMeshFrame_guess` | `0x69F5E4` | `0x577B50` | `0x1000FC9C0` | `0xF996C` |
| `MotionLayerExtensions_drawBezierPatchMeshFrame_guess` | `0x6A0B3C` | `0x5786AC` | `0x1000FDAF8` | `0xFAAA4` |

### `drawBeziers` consumers

| 语义函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `MotionLayer_drawBezierDebug_guess` | `0x69D7B0` | `0x5768E8` | `0x1000FB054` | `0xF80C0` |
| `MotionLayerExtensions_drawBezierPatchFrame_guess` | `0x6A0210` | `0x578168` | `0x1000FD250` | `0xFA220` |

literal xrefs、data xrefs 与 fresh decompile 三种证据给出完全相同的 3+2 consumer partition。
Android armv7 存在少量落在 recovery function chunk 外的 address-materialization xrefs，但它们
仍指向相邻函数的同一 data item，不构成额外 source-level consumer。

## 相邻 backing words

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `drawLinesMemberHint_guess` | `0x1AB5250` | `0x1111780` | `0x101B69718` | `0x187D444` |
| `drawBeziersMemberHint_guess` | `0x1AB5254` | `0x1111784` | `0x101B6971C` | `0x187D448` |

raw data-xref 计数为：

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `drawLines` | 12 | 18 | 6 | 12 |
| `drawBeziers` | 8 | 12 | 4 | 8 |

64-bit iOS 通常每个 source call 形成一个直接 data xref；A64 与 32-bit 端可能为同一地址实参
产生两至三条 materialization refs。强制重编译并命名 data 后，五个语义函数在四端都精确
显示自己的共享 symbol 两次——对应 horizontal 与 vertical 两处 source call sites。

## 调用形状

五类 consumers 共同使用：

```text
LayerClass.FuncCall(
    flags = 0,
    name = "drawLines" or "drawBeziers",
    hint = &correspondingPluralHint,
    result = &temporary,
    numparams = 2,
    params = [&appearanceCopy, &pointArrayCopy],
    objthis = owningLayer)
```

普通 HRESULT 不参与控制流。每次调用拥有自己的 appearance/points argument copies 和 result
Variant；回调后按 scope 析构。inclusive horizontal/vertical loop、边界 appearance 选择、
Bezier 三点 curve 以及首/尾 curve reverse 规则已在此前纵切面闭合，V171 只修正传入
FuncCall 的 hint identity。

## 源码修正

`MotionLayerExtensions.cpp` 的匿名 namespace 现在只定义：

```text
drawLinesMemberHint_guess
drawBeziersMemberHint_guess
```

随后：

- `drawGridFrame_guess` 与 `drawGridDebug_guess` 的四处 source call 统一使用第一槽；
- `drawBezierControlFrame_guess` 与公共 `drawBezierPatchFrame` 的四处 source call 统一使用
  第二槽；
- 删除四个 function-local statics。

二槽保持 TU 内部 linkage，因为四端 xrefs 没有跨出 MotionLayerExtensions 语义组件；没有把它们
错误扩张进跨 TU 的 `MotionDispatch` family。名称保留 `_guess`，因为 stripped binaries 不能
恢复原始 C++ identifier/linkage spelling。

## 回归探针

新增 `Layer frame drawing keeps one plural hint per drawing method`：

1. `ScopedCoreScriptEngine` 初始化真实 motionplayer NCB surface；
2. 暂时把 global `Layer` class 替换为 recording dispatch，并以 RAII 恢复原值；
3. `drawMeshFrame` 在 0×0 division 下产生两次 `drawLines`，两者 hint 非空且相同；
4. `drawBezierPatchMeshFrame` 再产生两次 `drawLines`，要求与前一 public path 的指针精确相同；
5. `drawBezierPatchFrame` 产生 `drawBeziers` calls，全程共用另一个非空指针，且与
   `drawLines` 指针严格不同；
6. 同时锁定 flags 0、两个参数与 owner-as-objthis。

该 probe 不需要真实 native texture/layer，因而不会把 GPU 环境差异混入 dispatch identity
测试。internal debug path 的跨 helper identity 则由四端 data xrefs 直接覆盖。两个构建树未
注册 CTest，故本轮只声明双配置 syntax-only 编译成功，不声称 probe 已由 CTest 运行。

## IDB 写回

四个 recovery IDB 均完成：

- 相邻二槽共 8 个地址重建为 size-4 `unsigned int`，命名为
  `drawLinesMemberHint_guess` / `drawBeziersMemberHint_guess`；
- 两个 data item、五个函数入口与每函数一处代表性 operand 写入 V171 注释；
- 每库五个 V171 bookmarks；
- 共 20 个函数强制重编译，readback 全部为对应 symbol 两次；
- 四库均原位保存成功。

## 验证

- ordinary Emscripten 测试 TU syntax-only：成功，仅有项目既有 `_tss` warning；
- `KRKR2_WASMTIME_HEADLESS=1` 测试 TU syntax-only：成功，同一 warning；
- `cmake --build out/web/debug`：成功，最终链接完成；
- `cmake --build out/wasmtime/debug`：成功，最终链接完成；
- Web wasm：`85,647,270` bytes，539 imports / 69 exports；
- Headless wasm：`84,994,411` bytes，538 imports / 69 exports；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- 两份 wasm 均由 `llvm-objdump -h` 列出完整
  TYPE/IMPORT/FUNCTION/TABLE/TAG/GLOBAL/EXPORT/START/ELEM/DATACOUNT/CODE/DATA/name/
  target_features sections；
- 两份产物与 V170 字节数完全相同，import/export 和 section sizes 也不变；零初始化内部 BSS
  identity 收敛没有产生可见 file-size delta；
- Web/Headless 两配置 CTest 均未注册测试。

本纵切面只合并四参考 address identity 已闭合的 plural method pair；不会把它们与单数
`drawLine` 或其他同名组件 mechanically 合并。
