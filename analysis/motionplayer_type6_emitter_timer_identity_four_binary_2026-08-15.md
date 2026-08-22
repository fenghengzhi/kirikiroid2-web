# MotionPlayer type-6 emitter timer、retained identity 与 offset 四参考纵切（2026-08-15）

## 裁决

本轮针对 motionplayer compiled source 最后一处 `LABEL_*` 残留，重新反编译四个当前
`reference/binaries/` 的 type-6 emitter pass。现有分支算法正确，不需要修复；但原注释
把“累计 timer”和“重新解析 src identity”绑定到单一 Android ARM64 Hex-Rays label，未
表达两个反直觉的四端共同生命周期边界：

1. node flags byte 为 0 时无条件累计 timer，即使 retained-active 为 false、retained src
   为空或不同，也不会初始化/更新 retained identity；
2. inactive、active slot done 或 active src 为 null 的早退会清 retained-active、释放
   retained src 并清 timer，却不会清上一帧 offset-valid；offset-valid 只在 live 分支完成
   timer 处理后才清。

本轮删除旧 label 注释，并用连续三次 emitter pass 锁定 initialize、inactive early-out 和
flagless accumulate 的提交顺序。

## 四平台入口

| 语义恢复名 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `Player_updateParticleEmitters_guess` | `0x6BC1B0` | `0x588820` | `0x100111A6C` | `0x10F2CC` |
| 函数大小 | `0x30C` | `0x226` | `0x29C` | `0x24E` |
| timer accumulate | `0x6BC368` | `0x5888C8` | `0x100111B08` | `0x10F340` |
| retained identity re-resolve | `0x6BC328` | `0x5888D4` | `0x100111B1C` | `0x10F34C` |
| offset-valid clear | `0x6BC390` | `0x588930` | `0x100111B90` | `0x10F3A6` |

名字带 `_guess`，不声称恢复了 stripped 原始符号。四端 active node 的字段偏移因 Player、
MotionNode、slot 和 `ttstr` ABI 不同而变化，但分支、引用计数、double timer 和三维 offset
提交顺序一致。

## 节点遍历与早退

pass 在 preview 时整体返回；否则从 node index 1 正向遍历，跳过非 type 6。对 type-6 node：

```text
if !accumulated.active || activeSlot.done || activeSlot.src.Ptr == null:
    emitterActive = false
    release emitterDtgt; emitterDtgt.Ptr = null
    emitterTimer = 0
    continue
```

`ttstr::IsEmpty()` 在当前端口中正是 `Ptr == nullptr`，不是 length==0，因此本地表达与四端
指针门一致。active src 是 retained identity；motion.dtgt、model.dtgt 和 prt.trigger 都不
参与这个早退身份判断。

早退发生在 offset-valid clear 之前。故一个上一帧 mode 2/3/4 已写出的
`emitterOffsetActive=true` 及三个数值会跨 inactive pass 保留；只有 retained identity 与
timer 被清。下游必须同时遵守 emitter live 状态，不能把 offset-valid 单独当作 live flag。

## timer 分支真值表

通过 live 门后，四端逻辑为：

| flags byte | retained active | retained src == active src | 行为 |
| ---: | ---: | ---: | --- |
| 0 | 任意 | 任意 | `timer += Player._deltaTime` |
| 非 0 | false | 不比较 | 重新解析 |
| 非 0 | true | true | `timer += Player._deltaTime` |
| 非 0 | true | false | 重新解析 |

字符串相等先走对象指针相同的快路，否则比较 native string metadata/UTF-16 内容；不按
hash-only 判断。flags==0 的第一行优先级最高，所以刚被 early-out 清成 inactive/null 的
node 下一次 live pass 若 flags 仍为 0，只会从 timer 0 累加，不会 retain 当前 src。

### 累计分支

读 Player 本帧 speed-scaled `_deltaTime` double，加到已有 emitterTimer double 后写回。没有
finite、符号、上限或零值检查；NaN/无穷/负 delta 直接传播。该分支不写 emitterActive，
也不 CopyRef/release emitterDtgt。

### re-resolve 分支

按顺序：

1. `emitterActive=true`；
2. CopyRef active src，release 旧 retained src，再存新 pointer；
3. node 有 parameter binding 时取 parameter entry 的 double value，否则取 Player
   frameTick double；
4. 读取 active slot 的 clipStartTime 与 modelTimeOffset；
5. 写 `timer = parentOrFrameTime - clipStartTime + modelTimeOffset`。

timer 初始化不加本帧 `_deltaTime`。node 的 parameter pointer 和 map index由更早构建阶段
保证；该 pass 不做修复性插入或边界 clamp。

## offset mode

无论 timer 走哪条 live 分支，接着先写 `emitterOffsetActive=false`，但不清三个 offset
double；随后只看 active model block 的 `model.dt`：

- `4`：用 `model.dtgt` 在 ordered raw-label map 查 target；找到才写 valid=true，并写
  `target.accumulated.pos - emitter.accumulated.pos` 三个 double；找不到保持 false/旧数值；
- `3`：无条件调用 crossfade position derivative helper；
- `2`：Player `_noUpdateYet` 为 true 或 timer 精确等于 `0.0` 时调用 derivative helper，
  否则 valid=true 并复制 emitter node 的三个 accumulated position delta；
- 其他值：保持 valid=false 与旧 offset 数值。

timer 的 `==0.0` 同时接受正零和负零，NaN 走 running-timer 分支。mode 4 raw-label lookup
消费 map 中的 node index 且不增加边界 gate；这条边界已在 raw-label map 纵切中记录。

## 所有权与异常边界

retained emitterDtgt 是 owning `ttstr`。inactive early-out release 后立即清 pointer；
re-resolve 先增新 src ref 再 release 旧 ref，因此新旧指针相同不会提前销毁。timer、valid
和 offset 都是 node 内嵌标量，没有独立 owner。

mode 2/3 的 interpolation helper 可能读取 Variant control-point/curve 数据；timer 和
offset-valid=false 在进入 helper 前已经提交，helper 失败不回滚。mode 4 map lookup不插入，
miss 也不重置旧三个 offset 数值。

## 本地、测试与 IDB 更新

- `PlayerUpdateParticles.cpp` 删除 `doAccumulate` 后的 Android ARM64 `LABEL_27/LABEL_21`
  注释，保留清晰的 flags/active/src 真值分派；
- 既有 type-6 回归先验证 flags!=0 的初次 retained identity 和 mode-4 target delta；随后
  验证 inactive pass 清 identity/timer 但保留 valid；最后验证 flags==0 live pass 不重建
  identity，只累计零 delta，并在未知 mode 下清 valid；
- 四份 recovery IDB 在 accumulate、re-resolve、offset clear 与 early-out 处加入平台无关
  注释并强制刷新当前 decompile。

## 验证

- 完整 `motionplayer-dll.cpp` Emscripten 单翻译单元 `-fsyntax-only`：通过，仅有既有 `_tss`
  deprecated warning；新增三阶段 retained-identity/offset-valid 回归已编译；
- `cmake --build --preset "Web Debug Build"`：通过；重新编译
  `PlayerUpdateParticles.cpp` 与同轮注释迁移影响的 `EmoteEngine.cpp`，成功链接最终 Wasm
  与 `index.html`。输出只有既有 `_tss`、pthread memory-growth、JSPI/internal-symbol
  警告；
- 限定文件的 `git diff --check` 与行尾空白扫描：通过；全
  `cpp/plugins/motionplayer` 的 `LABEL_*` 扫描为空；
- 四份 recovery IDB 已强制刷新 emitter pass、保存并回读；入口大小分别为
  `0x30C/0x226/0x29C/0x24E`，early-out、accumulate、re-resolve 和 offset-valid clear 注释
  均可解析。
