# `Player::playImpl` 共享 `type` member-hint identity（四参考，2026-08-16）

## 结论

本地 `PlayerTimeline.cpp` 曾为成功 load 后的 motion-content `"type"` 读取保留一个
TU-local：

```cpp
tjs_uint32 motionTypeMemberHint_guess = 0;
```

四个当前参考二进制共同否定这个边界。`Player::playImpl` 传入的地址就是插件全局
`typeMemberHint_guess`；它与 frame parser、SeparateLayerAdaptor publication、accurate
separate-layer rendering、`calcViewParam`、`skipToSync` 和 `getCommandList` 使用同一个
process-wide 32-bit backing word。参考实现不存在 playImpl-local duplicate。

这会影响真实行为，而不只是命名：TJS dispatch 可以读写 hint word，原版让上述所有调用点
观察同一个缓存状态；TU-local duplicate 会把 play 路径隔离出去。

## 函数与数据映射

| 参考 | `Player_playImpl_guess` | 大小 | `typeMemberHint_guess` |
|---|---:|---:|---:|
| Android arm64 | `0x6AF664` | `0x73C` | `0x1AB5124` |
| Android armv7 | `0x580158` | `0x302` | `0x1111658` |
| iOS arm64 | `0x100107540` | `0x3D4` | `0x101B695EC` |
| iOS armv7 | `0x104AE8` | `0x43A` | `0x187D31C` |

四端 fresh decompile 都把 retained motion-content getter 回读为：

```text
motionDispatch.PropGet(
    flags = 0,
    name = "type",
    hint = &typeMemberHint_guess,
    result = temporary,
    objthis = motionDispatch)
ignore ordinary status
motionType = temporary.AsInteger()
```

对应的 playImpl data-xref head：

| 参考 | xref head |
|---|---:|
| Android arm64 | `0x6AF99C` / `0x6AF9A4` |
| Android armv7 | `0x5802EE` / `0x5802F2` |
| iOS arm64 | `0x100107724` |
| iOS armv7 | `0x104D0E` / `0x104D18` |

32 位端一处 source-level address 通常由多条 address-materialization 指令共同形成，故 raw
xref 数不同；最终 data target 完全一致。

## 七类共享消费者

下表给出每类语义在四端的首个代表性 xref head。它们全部解析到上表同一个 data item：

| consumer | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `MotionNodeFrameSlot_parse_guess` | `0x68FBC8` | `0x56EE5E` | `0x1000F151C` | `0xED714` |
| `SeparateLayerAdaptor_assignFromAdaptor_guess` | `0x6A9E40` | `0x57CA04` | `0x100103758` | `0x100B26` |
| `Player_playImpl_guess` | `0x6AF99C` | `0x5802EE` | `0x100107724` | `0x104D0E` |
| accurate separate-layer render | `0x6C87D8` | `0x5917CE` | `0x10011BDBC` | `0x11A33C` |
| `Player_calcViewParam_guess` | `0x6CEEE8` | `0x594C72` | `0x100120508` | `0x11F24C` |
| `Player_skipToSync_guess` | `0x6D09AC` | `0x595D02` | `0x100121B10` | `0x1208DA` |
| `Player_getCommandList_guess` | `0x6D1C88` | `0x5967A2` | `0x1001226A0` | `0x12164A` |

Android arm64 的 accurate-render function 名为
`Player_renderAccurateSeparateLayerAdaptor_guess @ 0x6C7088`；Android armv7 为
`0x590468`，iOS arm64 为 `0x10011A9E8`，iOS armv7 为 `0x118D70`。Android arm64
`getCommandList` body 仍受既有 function-chunk owner 恢复缺口影响，表中使用真实 data-xref
head，不把它错误归类成 `EmotePlayer_getCommandList_guess` 的源码 body。

该集合还说明 `type` 槽不是“frame parser family 私有”或“calcViewParam 私有”。它是跨越
多个 source-level helper 和发布/读取方向的插件级共享缓存。

## 与 `playImpl` 生命周期的关系

本次不改变此前四端已闭合的 owner 和提交顺序：

1. load result 非 Void 后先提交 live motion label；
2. 结果容器以 owning Object retain 读取 numeric element 0/1；
3. committed motion-content 再独立 owning-retain 成 dispatch；
4. 在这个 retained dispatch 上读取 `type`；
5. type 1 再读取 `division`/`motionList` 并进入 Emote initializer，type 0 进入 ordinary
   initializer，其他值不初始化；
6. 最后才 Release motion-content dispatch 与 load-result dispatch。

`type` getter 仍使用 flags 0、同一 dispatch 作 `objthis`、忽略 ordinary HRESULT，再无条件
做 Integer conversion。本次只修正传入该 getter 的缓存 word identity。

## 源码与测试修正

### 源码

- 删除 `PlayerTimeline.cpp` namespace-local `motionTypeMemberHint_guess`；
- `readMotionProperty(TJS_W("type"), ...)` 改为
  `&detail::typeMemberHint_guess`；
- `division` 与 `motionList` 的既有共享槽不变。

### 测试

既有 `play retains the complete findMotion result through type dispatch` 测试原来只检查 hint
非 null，无法区分全局共享槽与错误的 local static。`LifetimeMotionDispatch` 现在保存实际
`tjs_uint32 *`，测试精确要求：

```text
state.typeHint == &motion::detail::typeMemberHint_guess
```

同时保留以下断言：load-result container 在 getter 期间仍活着、getter flags 为 0、返回后
container 已析构。这样 identity 修正没有弱化原先的 owner/flags 覆盖。

## IDB 写回

四个 recovery IDB 均完成：

- `typeMemberHint_guess` 地址重建/确认成独立 size-4 `unsigned int` data item；
- data item 注释记录七类共享 consumer 与“无 play-local duplicate”；
- `Player_playImpl_guess` 入口和代表性 call operand 写入 V167 注释；
- 增加 `V167 playImpl shared type member-hint identity` bookmark；
- 四个 `Player_playImpl_guess` 强制重编译；
- readback 四端均为 `typeMemberHint_guess` hits=2、`motionTypeMemberHint_guess` hits=0
  （一次实参符号，一次注释文本命中）；
- 四库均原位保存成功。

## 验证

- ordinary Emscripten 测试 TU syntax-only：成功；
- `KRKR2_WASMTIME_HEADLESS=1` 测试 TU syntax-only：成功；
- `cmake --build out/web/debug`：成功，最终链接完成；
- `cmake --build out/wasmtime/debug`：成功，最终链接完成；
- Web wasm：`85,647,465` bytes，539 imports / 69 exports；
- Headless wasm：`84,994,606` bytes，538 imports / 69 exports；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- 两份 wasm 均由 `llvm-objdump -h` 列出完整
  TYPE/IMPORT/FUNCTION/TABLE/TAG/GLOBAL/EXPORT/START/ELEM/DATACOUNT/CODE/DATA/name/
  target_features sections；
- 相对 V166，两份 wasm 都精确减少 21 bytes，import/export 数不变；
- Web/Headless 两配置 CTest 均未注册测试。

本纵切面只修正 playImpl 的 `type` member-hint identity；不会把这个共享结论外推到字符串
相同但地址不同的其他 key。
