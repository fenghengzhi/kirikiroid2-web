# `calcViewParam` shared/private member-hint identity（四参考，2026-08-16）

## 结论

本纵切面修正了一个由旧单目标分析遗留的真实源码结构错误：本地曾把
`calcViewParam` 使用的 17 个名字描述成 calc 专属 hint，并为其中 12 个已经存在的
process-wide backing word 再造了重复变量。四个当前参考二进制并不支持这种划分。

四端共同显示：

- `visible/src/blendMode/originX/originY/opacity/type/division` 复用插件中已有的同名
  process-wide hint；
- `left/top/right/bottom/width/height` 复用统一 geometry hint；
- `clip` 复用 `MotionNode::findSource` 使用的同一 hint；
- `coord/color` 复用 node-frame/render-source 家族的同一 hint；
- 只有一段连续的六槽序列属于这一局部区域，精确顺序为
  `mbp/invOffset/invMatrix/patch/cmesh/matrix`；
- 六槽中的 `patch` 也被 `Player::getCommandList` 复用，所以它不是函数私有槽；其余五槽
  在当前四参考里只发现 `calcViewParam` 这一类语义消费者。

因此，“字符串相同”本身既不能证明共享，也不能证明独立。决定 source-level identity 的
证据是四端 call operand 最终指向的 data address 是否相同。

## 函数映射

| 参考 | `Player_calcViewParam_guess` | 大小 |
|---|---:|---:|
| Android arm64 | `0x6CE908` | `0x152C` |
| Android armv7 | `0x594958` | `0x8E8` |
| iOS arm64 | `0x1001201CC` | `0xA88` |
| iOS armv7 | `0x11EED4` | `0xAAA` |

用于闭合跨消费者 identity 的函数：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `MotionNode_findSource_guess` | `0x691CC8` | `0x570500` | `0x1000F316C` | `0xEF97C` |
| `Player_getCommandList_guess` body | xref head `0x6D16E4` | `0x595FF0` | `0x100121EB0` | `0x120CF8` |

Android arm64 recovery 库仍把 `getCommandList` 的主体 chunk 归给
`EmotePlayer_getCommandList_guess @ 0x67F900` 的小 thunk；这属于已有 function-chunk 恢复
缺口，不改变 `0x6D16E4/0x6D16F8` 对 `patch` 槽的直接 data xref。本文以真实 xref head
描述该 body，不把错误的 chunk owner 当成源码调用关系。

## 共享槽映射

下表列出本次 fresh decompile 中 `calcViewParam` 实际复用的已有 backing word。`visible` 和
`opacity` 的跨消费者闭合已在
`motionplayer_visible_setpos_opacity_hint_family_four_binary_2026-08-16.md` 完成，本次重新在
四个 `calcViewParam` body 中读取 operand 确认；其余槽也都以本次四端地址重新核对。

| key | 本地语义名 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---:|---:|---:|---:|
| `visible` | `visibleMemberHint_guess` | `0x1AB5488` | `0x1111924` | `0x101B69950` | `0x187D5F4` |
| `src` | `srcMemberHint_guess` | `0x1AB5134` | `0x1111668` | `0x101B695FC` | `0x187D32C` |
| `blendMode` | `blendModeMemberHint_guess` | `0x1AB5448` | `0x11118E4` | `0x101B69910` | `0x187D5B4` |
| `originX` | `originXMemberHint_guess` | `0x1AB5214` | `0x1111748` | `0x101B696DC` | `0x187D40C` |
| `originY` | `originYMemberHint_guess` | `0x1AB5218` | `0x111174C` | `0x101B696E0` | `0x187D410` |
| `opacity` | `opacityMemberHint_guess` | `0x1AB5490` | `0x111192C` | `0x101B69958` | `0x187D5FC` |
| `type` | `typeMemberHint_guess` | `0x1AB5124` | `0x1111658` | `0x101B695EC` | `0x187D31C` |
| `division` | `divisionMemberHint_guess` | `0x1AB53EC` | `0x1111888` | `0x101B698B4` | `0x187D558` |
| `left` | `leftMemberHint_guess` | `0x1AB5224` | `0x1111758` | `0x101B696EC` | `0x187D41C` |
| `top` | `topMemberHint_guess` | `0x1AB5228` | `0x111175C` | `0x101B696F0` | `0x187D420` |
| `right` | `rightMemberHint_guess` | `0x1AB522C` | `0x1111760` | `0x101B696F4` | `0x187D424` |
| `bottom` | `bottomMemberHint_guess` | `0x1AB5230` | `0x1111764` | `0x101B696F8` | `0x187D428` |
| `width` | `widthMemberHint_guess` | `0x1AB520C` | `0x1111740` | `0x101B696D4` | `0x187D404` |
| `height` | `heightMemberHint_guess` | `0x1AB5210` | `0x1111744` | `0x101B696D8` | `0x187D408` |
| `clip` | `clipMemberHint_guess` | `0x1AB5220` | `0x1111754` | `0x101B696E8` | `0x187D418` |
| `coord` | `coordMemberHint_guess` | `0x1AB5140` | `0x1111674` | `0x101B69608` | `0x187D338` |
| `color` | `colorMemberHint_guess` | `0x1AB5148` | `0x111167C` | `0x101B69610` | `0x187D340` |

这些地址不是“同名字符串附近的猜测”。在强制重编译后的四端 pseudocode 中，相关
`SetValue`/`GetValue` 调用直接显示上述命名符号；例如四端都把 `division` operand 回读为
`&divisionMemberHint_guess`，把 `clip` operand 回读为 `&clipMemberHint_guess`。

## 连续六槽序列

| 顺序 | key / 本地语义名 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 1 | `mbp` / `calcMbpMemberHint_guess` | `0x1AB54B4` | `0x1111950` | `0x101B69974` | `0x187D620` |
| 2 | `invOffset` / `calcInvOffsetMemberHint_guess` | `0x1AB54B8` | `0x1111954` | `0x101B69978` | `0x187D624` |
| 3 | `invMatrix` / `calcInvMatrixMemberHint_guess` | `0x1AB54BC` | `0x1111958` | `0x101B6997C` | `0x187D628` |
| 4 | `patch` / `patchMemberHint_guess` | `0x1AB54C0` | `0x111195C` | `0x101B69980` | `0x187D62C` |
| 5 | `cmesh` / `calcCmeshMemberHint_guess` | `0x1AB54C4` | `0x1111960` | `0x101B69984` | `0x187D630` |
| 6 | `matrix` / `calcMatrixMemberHint_guess` | `0x1AB54C8` | `0x1111964` | `0x101B69988` | `0x187D634` |

每端均为六个相邻、步长 4 的 backing word，没有插槽、padding 或平台差异。四端
`calcViewParam` 强制重编译后的命中数也相同：

| 符号 | 每个 body 中的语义调用次数 |
|---|---:|
| `calcMbpMemberHint_guess` | 2 |
| `calcInvOffsetMemberHint_guess` | 1 |
| `calcInvMatrixMemberHint_guess` | 1 |
| `patchMemberHint_guess` | 1 |
| `calcCmeshMemberHint_guess` | 2 |
| `calcMatrixMemberHint_guess` | 1 |

同一个 key 出现两次不是重复 backing word：`mbp`/`cmesh` 的两个控制分支把同一个地址
传入 dispatch helper。

## `patch` 与 `clip` 的跨消费者闭合

### `patch`

四端 `patchMemberHint_guess` 都同时出现于 `calcViewParam` 和 `getCommandList`：

| 参考 | calc xref head | get-command xref head |
|---|---:|---:|
| Android arm64 | `0x6CF968` / `0x6CF97C` | `0x6D16E4` / `0x6D16F8` |
| Android armv7 | `0x594E3E` / `0x594E4C` | `0x596528` / `0x596540` |
| iOS arm64 | `0x100120710` | `0x1001222AC` |
| iOS armv7 | `0x11F490` / `0x11F496` / `0x11F49E` | `0x1211A6` / `0x1211AC` / `0x1211B4` |

32 位端同一语义地址常由多条 materialization 指令共同形成，所以 raw xref 数量不同；
semantic consumer 集合仍精确相同。旧源码已有 `patchMemberHint_guess`，但把定义放在宽泛
command block，同时又为 calc 另造 `calcPatchMemberHint_guess`。本次删除后者，并把唯一
`patchMemberHint_guess` 放回六槽序列的正确第四位。

### `clip`

四端 `clipMemberHint_guess` 都同时出现于 `MotionNode_findSource_guess` 和
`Player_calcViewParam_guess`：

| 参考 | find-source xref head | calc xref head |
|---|---:|---:|
| Android arm64 | `0x692998` / `0x6929A0` | `0x6CF148` / `0x6CF158` / `0x6CF218` / `0x6CF228` |
| Android armv7 | `0x570B0A` / `0x570B0E` | `0x595002` / `0x595010` / `0x595042` / `0x595050` |
| iOS arm64 | `0x1000F39F8` | `0x100120964` / `0x1001209BC` |
| iOS armv7 | `0xF0218` / `0xF0224` | `0x11F6E0` / `0x11F6E6` / `0x11F6EE` / `0x11F724` / `0x11F72A` / `0x11F732` |

因此 calc 的 `clip` publication 必须使用已有 `clipMemberHint_guess`，不能保留
`calcClipMemberHint_guess`。

## 本地源码修正

### 删除的伪重复槽

下列 12 个声明/定义/调用点已删除或改回共享槽：

- `calcBlendModeMemberHint_guess`；
- `calcOriginXMemberHint_guess` / `calcOriginYMemberHint_guess`；
- `calcDivisionMemberHint_guess`；
- `calcPatchMemberHint_guess`；
- `calcLeftMemberHint_guess` / `calcTopMemberHint_guess` /
  `calcRightMemberHint_guess` / `calcBottomMemberHint_guess`；
- `calcWidthMemberHint_guess` / `calcHeightMemberHint_guess`；
- `calcClipMemberHint_guess`。

对应 `PlayerLayerQuery.cpp` callsite 现在分别使用
`blendMode/originX/originY/division/patch/left/top/right/bottom/width/height/clip` 的真实共享
变量。

### 保留的六槽结构

`MotionDispatch.h` 与 `RuntimeSupport.cpp` 现在按四参考的连续顺序声明/定义：

```text
calcMbpMemberHint_guess
calcInvOffsetMemberHint_guess
calcInvMatrixMemberHint_guess
patchMemberHint_guess
calcCmeshMemberHint_guess
calcMatrixMemberHint_guess
```

由于参考符号已 stripped，恢复名继续遵守 `_guess` 约定；唯一已有、跨消费者的 `patch`
名称也保留 `_guess`。

## 测试

现有 `motionplayer draw cache and playback state` 使用真实 motion fixture。为每个
`calcViewParam` 输出 Dictionary 增加了透明 `ViewParamHintProbe`：

- `PropSet`/`PropGet` 先记录 member、flags、hint pointer 和 `objthis`；
- 再把调用转发给真实 backing Dictionary，所以原有输出值检查保持不变；
- 每个节点的 `visible` publication 必须使用 `visibleMemberHint_guess`；
- 至少一个 exportable 节点必须出现后续调用；
- `src/blendMode/originX/originY/opacity/clip/coord/color` 必须传入对应共享 word；
- `mbp/cmesh/matrix` 必须传入六槽家族里的对应 word；
- publication 使用 `TJS_MEMBERENSURE`，数组读使用 flags 0，所有接收者必须仍是原 probe
  dispatch。

当前构建配置没有注册可运行 CTest，因此不能把 syntax-only 冒充运行时 test execution；
普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套测试 TU 语法检查均真实成功。

## IDB 写回

四个 recovery IDB 均完成：

- 六个连续槽与 `clip` 共 7 个地址重建为独立 size-4 `unsigned int` data item；
- 写入上述七个 `_guess` 数据名；
- 数据项、`calcViewParam`、代表性 `patch`/`clip` consumer xref 写入注释；
- `calcViewParam` 增加 `V166 calcViewParam shared/private member-hint identity` bookmark；
- `calcViewParam`、`MotionNode_findSource_guess`、`getCommandList` 相关函数强制重编译；
- readback 在四端 `calcViewParam` 均显示完整六槽名及共享
  `blendMode/originX/division/clip` 名，在四端两个跨消费者中也回读到同一个 `patch`/`clip`
  符号；
- 四库均原位保存成功。

## 构建与产物验证

- 普通 Emscripten 测试 TU syntax-only：成功；
- `KRKR2_WASMTIME_HEADLESS=1` 测试 TU syntax-only：成功；
- `cmake --build out/web/debug`：成功，最终链接完成；
- `cmake --build out/wasmtime/debug`：成功，最终链接完成；
- Web wasm：`85,647,486` bytes，539 imports / 69 exports；
- Headless wasm：`84,994,627` bytes，538 imports / 69 exports；
- 两个 wasm 均由 Node `WebAssembly.Module` 成功解析；
- 两个 wasm 均由 `llvm-objdump -h` 完整列出
  TYPE/IMPORT/FUNCTION/TABLE/TAG/GLOBAL/EXPORT/START/ELEM/DATACOUNT/CODE/DATA/name/
  target_features sections；
- Web/Headless 两配置 CTest 均未注册测试。

本纵切面只证明并修正 `calcViewParam` 的 member-hint identity 与分组；不把它外推为整个
`calcViewParam` 数据流、所有 mesh 分支或所有输出边界已经完全恢复。
