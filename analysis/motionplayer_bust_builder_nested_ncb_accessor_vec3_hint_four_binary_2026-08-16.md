# MotionPlayer Bust builder nested `ncbPropAccessor`、vec3 helper 与共享 hint（四参考，2026-08-16）

## 结论

四份当前参考共同恢复出 Bust builder 的完整 owner/数据流：

```text
copied bustControl Variant
  -> loop-wide root ncbPropAccessor
     -> retained outer metadata source Variant
        -> second-copy metadata ncbPropAccessor
           -> enabled
           -> param direct-temporary -> param ncbPropAccessor
              -> new EmoteSpringState(retained outer source)
              -> op/p/pv direct-temporary Variant
                 -> springVec3FromVariant_guess
                    -> helper-owned copied-Variant ncbPropAccessor
                    -> x/y/z GetValue<tjs_real> -> caller-visible floats
              -> ofs GetValue<tjs_real> -> float
              -> raw spring pointer appended to deque #1             [commit]
              -> baseLayer / var_lr / var_ud strings
              -> HM6[var_lr] = {0, original metadataIndex}
              -> HM6[var_ud] = {0, original metadataIndex}
           -> release param accessor
        -> release metadata accessor
     -> destroy retained outer metadata source Variant
  -> release root accessor after all rows
```

旧 portable builder 已恢复真实 ncbind 类型、source Variant 作用域、typed getter 与 hint identity；
raw spring owner gap、append commit、sparse dual publication 和 no-rollback 边界保持不变。

## 函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteEngine_buildBustControl_guess` | `0x6683F8` | `0x55659C` | `0x1001A7DDC` | `0x1A730C` |
| `springVec3FromVariant_guess` | `0x668C1C` | `0x556A34` | `0x1001A836C` | `0x1A78F0` |
| `EmoteSpringState_ctor_guess` | `0x65FBEC` | `0x556328` | `0x1001A7BFC` | `0x1A70D0` |

四个 stripped helper 原名不可恢复，因此统一使用 `_guess`。Android/iOS 64-bit decompiler 把 12-byte
float aggregate 错投影成单 `float` return 并把另两项留在额外 FP registers；32-bit 则显示显式 output
地址。caller 的三个连续 float 写入证明它们是同一源码级三分量结果，不应按错误的单 float 原型移植。

## Root、outer source 与 param accessor

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| input copy / root accessor | `0x66842C..0x668480` | `0x5565B6..0x5565CC` | `0x1001A7E04..0x1001A7E28` | `0x1A732E..0x1A737E` |
| Count snapshot | `0x66848C` | `0x5565D2` | `0x1001A7E34` | `0x1A738C` |
| outer indexed source | `0x6684DC..0x6684F0` | `0x556630` | `0x1001A7E70` | `0x1A73B2` |
| metadata second copy/accessor | `0x6684E8..0x668544` | `0x55663C..0x55664E` | `0x1001A7E7C..0x1001A7E94` | `0x1A73BE..0x1A73D4` |
| enabled | `0x668560` | `0x55666E` | `0x1001A7EB0` | `0x1A73FE` |
| param read/accessor | `0x66858C..0x6685F4` | `0x55668C..0x55669E` | `0x1001A7ED4..0x1001A7EF0` | `0x1A7426..0x1A743C` |

root Count 与 outer index 都使用 flags 0。outer getter 的 result 先成为 retained `metadata` source；
第二份 copy 只用于 metadata accessor。spring constructor 接收 retained source，而不是 accessor、
`param` 或第二份 copy。disabled row 在 param/new 前跳到公共 metadata cleanup，原始
`metadataIndex` 不压缩。

param getter 的 result 则直接构造 nested accessor，没有额外跨 builder 尾部生存的 source Variant。
这个 accessor从 param read 后一直活到 label/HM6 publication结束。

所有 typed getter 都消费 getter 写入的 Variant，不测试 getter HRESULT。测试 probe 因而可以在写出
有效值后返回 `TJS_E_FAIL`，四层构建仍继续。

## Bust/Chain 共用的 builder hints

fresh xrefs-to 表明下列六个 slot 的 consumer 集合在四端都包含 Bust 与 shared Chain builder：

| hint | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| param | `0x1AB4F24` | `0x11114BC` | `0x101B69FD4` | `0x187D9F4` |
| op | `0x1AB4F28` | `0x11114C0` | `0x101B69FD8` | `0x187D9F8` |
| p | `0x1AB4F2C` | `0x11114C4` | `0x101B69FDC` | `0x187D9FC` |
| pv | `0x1AB4F30` | `0x11114C8` | `0x101B69FE0` | `0x187DA00` |
| ofs | `0x1AB4F34` | `0x11114CC` | `0x101B69FE4` | `0x187DA04` |
| baseLayer | `0x1AB4F38` | `0x11114D0` | `0x101B69FE8` | `0x187DA08` |

portable 源码新增一组 Engine-local process-wide mutable slots：

```text
engineParamHint_guess
engineOpHint_guess
enginePHint_guess
enginePvHint_guess
engineOfsHint_guess
engineBaseLayerHint_guess
```

本轮 Bust 已使用；Chain outer builder 的完整 accessor 迁移在当时留到下一条 fresh 四端纵切，并已在
`analysis/motionplayer_chain_builder_nested_ncb_accessor_role_hint_four_binary_2026-08-16.md`
闭合。Chain 复用同一组变量，没有另建同名属性的 call-site-local hints。

`enabled` 继续复用所有 controller builders 的 Engine-wide slot。`var_lr/var_ud` 继续复用 V137
确认的 Bust/Chain/Clamp slots：

| hint | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| var_lr | `0x1AB4F3C` | `0x11114D4` | `0x101B69FEC` | `0x187DA00C` |
| var_ud | `0x1AB4F40` | `0x11114D8` | `0x101B69FF0` | `0x187DA010` |

## vec3 helper source identity 与 x/y/z hints

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| input copy/accessor/temp dtor | `0x668C4C..0x668CA0` | `0x556A48..0x556A5C` | `0x1001A8390..0x1001A83B4` | `0x1A7912..0x1A795E` |
| x | `0x668CC4` | `0x556A80` | `0x1001A83D8` | `0x1A79A4` |
| y | `0x668CE4` | `0x556A9E` | `0x1001A83F8` | `0x1A79D6` |
| z | `0x668D08` | `0x556AB6` | `0x1001A841C` | `0x1A79FE` |
| accessor cleanup | `0x668D18..0x668D28` | `0x556ACA..0x556AD6` | `0x1001A8428..0x1001A843C` | `0x1A7A08..0x1A7A16` |

helper不是 raw dictionary getter集合：每次调用先 copy input Variant、构造一个本地
`ncbPropAccessor`，再按 x→y→z 执行 `GetValue<tjs_real>` 并逐项窄化 float。`op/p/pv` 每次都走一次
完整 helper owner 生命周期；Chain 的每个 segment也复用同一 helper。

hint identity：

| hint | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| x | `0x1AB4FF0` | `0x1111588` | `0x101B6A0A0` | `0x187DAC0` |
| y | `0x1AB4FF4` | `0x111158C` | `0x101B6A0A4` | `0x187DAC4` |
| z | `0x1AB5058` | `0x11115DC` | `0x101B6A108` | `0x187DB14` |

fresh xrefs-to 还证明 x/y slots 被 `EmoteEngine_resolveShapeAnchor_guess` 共享，z只由 vec3 helper
消费。本地引入 `enginePointX/Y/ZHint_guess`；当前 helper已使用，shape-anchor 的 raw property path应在
其独立纵切中复用 x/y，而不能把这项发现顺手扩张成未经完整复核的函数改写。

## 动态状态、append 与 publication

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| op | `0x66863C..0x668668` | `0x5566CA..0x5566E4` | `0x1001A7F30..0x1001A7F4C` | `0x1A7480..0x1A74A0` |
| p | `0x668698..0x6686C4` | `0x5566F8..0x556712` | `0x1001A7F70..0x1001A7F8C` | `0x1A74C8..0x1A74E8` |
| pv | `0x6686F4..0x668720` | `0x556728..0x556742` | `0x1001A7FB0..0x1001A7FCC` | `0x1A7510..0x1A7530` |
| ofs | `0x668740` | `0x556760` | `0x1001A7FEC` | `0x1A755E` |
| append owner commit | inline `0x668768` / growth `0x6687DC` | `0x55676E` | `0x1001A8004` | `0x1A756E` |
| baseLayer | `0x668840` | `0x55679A` | `0x1001A8050` | `0x1A75BE` |
| var_lr | `0x6688C4` | `0x5567E0` | `0x1001A80BC` | `0x1A7636` |
| var_ud | `0x66894C` | `0x55682A` | `0x1001A8120` | `0x1A7698` |
| lr/ud publication | `0x6689B0 / 0x6689C0` | `0x556868 / 0x556876` | `0x1001A816C / 0x1001A817C` | `0x1A76DE / 0x1A76F4` |

new-expression覆盖 spring constructor throw；constructor成功后到 append成功前只有 raw pointer，
vec3/property/conversion/deque-growth异常维持原版 leak窗口。append之后 entry取得 owner，后续
baseLayer/var_lr/var_ud或 map异常保留部分 entry和此前 publication，无 rollback。相等、空和重复 key
仍允许；同一 row 的 var_ud 后写，冲突时覆盖 var_lr map value但不移除 deque owner。

## Iteration cleanup

| cleanup | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| param accessor | `0x6689D4..0x6689E4` | `0x556884..0x55688C` | `0x1001A8190..0x1001A81A8` | `0x1A7702..0x1A7710` |
| metadata accessor | `0x6689FC..0x668A14` | `0x5568A0..0x5568A8` | `0x1001A81B0..0x1001A81C4` | `0x1A7714..0x1A7722` |
| retained outer source | `0x668A1C` | `0x5568AC` | `0x1001A81CC` | `0x1A7728` |
| root accessor | `0x668A2C..0x668A44` | `0x5568C8..0x5568D0` | `0x1001A81E0..0x1001A81F4` | `0x1A773A..0x1A7748` |

disabled路径跳过 param，因此只执行 metadata accessor→outer source；enabled路径先多释放 param。root
在整个循环后释放。portable声明顺序自然产生相同逆序。

## Portable probe 与验证

controller-builder mega-probe加入 Bust kind：outer array在 indexed getter内放弃 element owner；element
在 param getter内放弃 param owner；param dispatch保留一个 vec3 dispatch。所有 getter写值后返回
`TJS_E_FAIL`。probe核对：

- root Count/index、metadata enabled/param/constructor fields/baseLayer/var_lr/var_ud 的 flags、hint、
  objthis 与顺序；
- param reads严格 `op,p,pv,ofs`；vec3 reads严格三轮 `x,y,z`；
- stored/pos/vel 与 ofs 的 real→float值流；
- param与 vec3 dispatch都恰好析构一次；
- Bust enabled与其他 builders共享，Bust var_lr/var_ud pointer与 Clamp精确相同；
- builder six-property family 与 point x/y/z hints非 null且预期互异。

普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 test TU syntax-only通过，仅有既有 `_tss` warning；Web
presets关闭 Catch2 tests，故不宣称 probe已在 Web runtime运行。Web Debug `3/3`、Wasmtime Headless
Debug `4/4`重编译通过；两个最终 wasm都以 `llvm-objdump -h` 返回 0。

四库均完成 helper rename、builder/helper V139 comments、三项 bookmarks、双函数 force-recompile、
decompile与 listing/disasm comment readback，并在回读后原位保存。A64大函数 disasm structured response
被裁剪，改用受限 listing comment搜索找齐本纵切 `25/25` 个 V139点。

Chain outer builder在本纵切当时尚未闭合；只因四端共同调用同一已恢复 helper，portable Chain的七个
vec3 call先切换到 `springVec3FromVariant_guess`。其 root/outer/param/array accessor与 `var_lrm`
hint现已由后续 `motionplayer_chain_builder_nested_ncb_accessor_role_hint_four_binary_2026-08-16.md` 闭合。
