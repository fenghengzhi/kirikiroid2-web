# MotionPlayer Chain builder：nested ncb owner、快照角色映射与 hint（四参考，2026-08-16）

## 结论

本纵切面重新从 `reference/binaries/` 的四份当前参考完整恢复 shared
`hairControl` / `partsControl` builder，没有沿用旧 `libkrkr2.so` 注释。四端共同证明：

1. builder 不是一组无 owner 的 raw `PropGet`。它先复制输入 Variant，构造 loop-wide root
   `ncbPropAccessor`，只快照一次 `Count`；每行同时保留 outer source Variant 和由第二份副本
   构造的 metadata accessor。
2. `param` 的返回临时 Variant 直接构造另一个 accessor；这个 owner 在 spring 构造、动态状态
   覆盖、raw append、四个字符串读取和三次 HM6 publication 的整个成功前缀中保持存活。
3. `bp`、`p`、`pv` 依次构造三个同时存活的 nested array accessor。它们不是按同名写入内部
   字段，而是按快照角色映射：serialized `bp -> internal p`、serialized `p -> internal pv`、
   serialized `pv -> internal bp`。
4. 三个数组成功路径严格逆序释放 `pv -> p -> bp`，随后才是 param accessor、metadata
   accessor、outer source Variant；loop-wide root accessor 最后释放。
5. `param/op/p/pv/ofs/baseLayer` 与 Bust builder 共享 mutable member-hint slot；`enabled`、
   `var_lr`、`var_ud` 还分别与其他 builder family 共享。`bendR/bendS/bp/var_lrm` 是 Chain-only
   slot。
6. raw spring pointer 的异常泄漏窗口、deque append ownership commit、字符串写入后的 partial
   entry、以及 lr -> lrm -> ud 的 sparse original-index publication 均保持不变。

## 四端函数与 ABI

| 参考 | Chain builder | 函数大小 | spring allocation / ctor |
|---|---:|---:|---:|
| Android arm64-v8a | `0x668DB0` | `0xDAC` | `new(0xB0)` at `0x668FBC`, ctor `0x668FC8` |
| Android armeabi-v7a | `0x556B84` | `0x5C0` | `new(0xA8)` at `0x556CCE`, ctor `0x556CD4` |
| iOS arm64 | `0x1001A87C0` | `0x700` | `new(0xB0)` at `0x1001A88F0`, ctor `0x1001A88FC` |
| iOS armv7 | `0x1A7DCC` | `0x6BE` | `new(0xA8)` at `0x1A7F12`, ctor `0x1A7F1C` |

64 位对象大小 `176`，32 位对象大小 `168`；差异来自尾指针自然对齐。共同源对象字段偏移中，
`p`、`pv`、`bp` 分别从 `+92`、`+116`、`+140` 开始，每个包含两个 12-byte vec3。

## 完整成功路径

四端共同的 source-level 顺序为：

1. `ncbPropAccessor{tTJSVariant(chainControl)}`；读取一次 `Count`。
2. `root.GetValue<tTJSVariant>(metadataIndex)` 取得 outer source；再复制它构造 metadata
   accessor。
3. 读取 shared hinted `enabled`。false 行直接清理 metadata accessor 和 outer source，保留原始
   稀疏索引。
4. `metadata.GetValue<tTJSVariant>("param")` 的直接临时值构造 param accessor。
5. `new EmoteBustChainSpring(outerSource)`；constructor 继续从 outer row 读取 physics metadata。
6. param 依次读取 `op` Variant、`ofs` real、`bendR` real、`bendS` real。`op` 进入共享
   `springVec3FromVariant_guess`，helper 内部再复制 Variant 构造 vec3 accessor，并按 x/y/z 读取
   hinted real。
7. param 依次读取 `bp`、`p`、`pv` Variant，并分别直接构造三个 array accessor；此时三者同时
   存活。
8. 读取顺序不是 accessor 的构造顺序重排，而是第一组两项、第二组两项、第三组两项：
   `bp[0/1] -> internal p[0/1]`，`p[0/1] -> internal pv[0/1]`，
   `pv[0/1] -> internal bp[0/1]`。每个 indexed Variant 都直接进入共享 vec3 helper。
9. raw emplace 把 spring pointer 复制入 deque entry，形成 ownership commit；源 raw local不清零。
10. metadata accessor 依次读取 typed string `baseLayer`、`var_lr`、`var_lrm`、`var_ud`。
11. HM6 依次 under lr/lrm/ud 写入 `{type=callerTag,index=originalMetadataIndex}`。
12. 成功清理：`pv array -> p array -> bp array -> param accessor -> metadata accessor -> outer
    source`。下一轮复用相同 root；函数末尾才释放 root。

## `bp/p/pv` 不是同名字段直连

这一点是本轮对旧 portable 实现最重要的纠正。四端的 literal、accessor identity 和对象写偏移
同时给出相同映射：

| serialized key | accessor 构造序位 | indexed read 次序 | 对象写偏移 | internal field | solver 角色 |
|---|---:|---:|---:|---|---|
| `bp` | 1 | 1–2 | `+92`, `+104` | `p[0/1]` | 每步重建的 target/base positions |
| `p` | 2 | 3–4 | `+116`, `+128` | `pv[0/1]` | 当前 positions |
| `pv` | 3 | 5–6 | `+140`, `+152` | `bp[0/1]` | 当前 velocities |

代表性写入起点：

| mapping | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `bp[0] -> p[0]` | `0x669250` | `0x556E24` | `0x1001A8A9C` | `0x1A80CC` |
| `p[0] -> pv[0]` | `0x6692D0` | `0x556E76` | `0x1001A8B04` | `0x1A8134` |
| `pv[0] -> bp[0]` | `0x669370` | `0x556EC8` | `0x1001A8B6C` | `0x1A819E` |

UTF-16LE 原始字节也排除了 IDA 把 `pv` 渲染成 `p` 的歧义：

| key | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `bp\0` | `0x15033E0` | `0xD84444` | `0x10195FD9C` | `0x1752100` |
| `p\0` | `0x1506366` | `0x5572F4` | `0x10195FD42` | `0x17520A6` |
| `pv\0` | `0x14D3936` | `0xD84406` | `0x10195FD46` | `0x17520AA` |

精确 bytes 分别为 `62 00 70 00 00 00`、`70 00 00 00`、
`70 00 76 00 00 00`。物理邻接和 IDA 自动字符串边界不参与语义裁决。

## Hint identity

Chain-only slots：

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `bendR` | `0x1AB4F44` | `0x11114DC` | `0x101B69FF4` | `0x187DA14` |
| `bendS` | `0x1AB4F48` | `0x11114E0` | `0x101B69FF8` | `0x187DA18` |
| `bp` | `0x1AB4F4C` | `0x11114E4` | `0x101B69FFC` | `0x187DA1C` |
| `var_lrm` | `0x1AB4F50` | `0x11114E8` | `0x101B6A000` | `0x187DA20` |

四端 xref 重新检查后，这四个 slot 的代码消费者都只在 shared Chain builder 中。ARM64 的
ADRP/ADD、ARMv7 的 literal materialization 会产生不同数量的 IDA data xref，这是 codegen/IDA
表示差异，不是额外 consumer。

Bust/Chain 共享 slots：

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `param` | `0x1AB4F24` | `0x11114BC` | `0x101B69FD4` | `0x187D9F4` |
| `op` | `0x1AB4F28` | `0x11114C0` | `0x101B69FD8` | `0x187D9F8` |
| `p` | `0x1AB4F2C` | `0x11114C4` | `0x101B69FDC` | `0x187D9FC` |
| `pv` | `0x1AB4F30` | `0x11114C8` | `0x101B69FE0` | `0x187DA00` |
| `ofs` | `0x1AB4F34` | `0x11114CC` | `0x101B69FE4` | `0x187DA04` |
| `baseLayer` | `0x1AB4F38` | `0x11114D0` | `0x101B69FE8` | `0x187DA08` |

`var_lr` 与 `var_ud` 继续复用 Bust/Chain/Clamp family slots；`enabled` 继续复用所有 leaf builder
的 broader slot。shared vec3 helper 的 x/y/z slot identity 已在上一纵切面闭合。

## 生命周期、异常与 partial commit

- root `Count` getter 和所有 named/indexed getter 都忽略 HRESULT；probe 在写出可用 Variant 后返回
  `TJS_E_FAIL`，portable 仍消费写出的值。
- param lookup 在 allocation 之前；new-expression constructor throw 仍有 allocation rollback。
- constructor 成功以后，spring 只由 raw local 暂持。任何 scalar、array、indexed vec3 lookup 或
  deque growth failure 都保留 native leak window。
- append 成功以后，deque entry 已拥有 spring。之后 string assignment 或 map publication failure
  会留下 partial entry 和已经完成的 publication prefix，不回滚。
- 三个 array accessor 在第一次 indexed read 之前已经全部构造。成功路径与 C++ unwind 都按
  declaration reverse order 销毁；没有提前释放某个数组来降低 owner 数量。
- empty/equal/duplicate lr/lrm/ud key 不做 gate。单行 collision 时 ud 最后写，跨行则后一个 enabled
  metadata index 覆盖 map value；已 append owner 全部保留。

## Portable 修改与回归探针

`EmoteEngine::buildChainControl_guess` 已改为：

- loop-wide root、per-row metadata、direct-temporary param 和三个 simultaneous array 的真实
  `ncbPropAccessor` 结构；
- typed bool/Variant/real/ttstr getters 和恢复的 mutable hint pointers；
- serialized `bp/p/pv` 到 internal `p/pv/bp` 的正确角色映射；
- 原有 raw-owner gap、deque ownership commit、sparse triple publication 与 reverse cleanup。

完整测试翻译单元中的 builder probe 新增 Chain kind，并覆盖：

- root Count 一次、index 0 一次、exact flags/hint/objthis；
- getter 写值后返回失败仍被 ncb path 消费；
- outer row 与 param owner 在外部 storage drop 后仍存活；
- 三个 array dispatch 在六次 indexed read 中始终同时存活，读序均为 0/1，成功析构顺序严格
  `pv -> p -> bp`；param 析构发生在三个数组之后；
- 七个 vec3 使用不同值，直接断言 `bp -> p`、`p -> pv`、`pv -> bp`；
- Chain 与 Bust/Clamp 的共享 hint pointer 相等，四个 Chain-only pointer 非空且彼此/共享槽不同；
- baseLayer、三 key、type tag 和 original metadata index publication。

## IDB 写回与验证

四个 recovery IDB 已写入 V140 line comments 与 builder bookmark，覆盖 root/Count、outer source、
param、Chain-only scalar hints、三 array owner、三组角色映射、raw append、var_lrm、三 publication 和
逆序 cleanup。四函数 force-recompile 后重新 decompile；每库 `15/15` 条 V140 comment readback
成功，随后四库原位保存。

验证结果：

- ordinary Emscripten 完整 motionplayer test TU `-fsyntax-only`：通过，仅既存 `_tss` warning；
- headless define 下同一 test TU `-fsyntax-only`：通过，仅同一 warning；
- `Web Debug Build`：`3/3` 通过并重链最终 Web wasm；
- `Wasmtime Headless Debug Build`：`4/4` 通过；
- `out/web/debug/index.wasm` 与
  `out/wasmtime/debug/krkr2_wasmtime_guest.wasm` 均通过 `WebAssembly.Module` parse。

当前构建预设没有可执行该 Catch2 翻译单元的 native runtime test target，因此这里不把 Ninja 的
`3/3`、`4/4` 进度误称为测试用例数；行为探针由两个编译面验证其可编译性。
