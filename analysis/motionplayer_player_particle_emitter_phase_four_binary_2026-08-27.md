# Player particle-emitter phase 四参考二进制联合恢复

日期：2026-08-27

## 1. 四端函数

### 1.1 phase root

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6BC1B0` | 193 |
| Android armv7 | `0x588820` | 172 |
| iOS arm64 | `0x100111A6C` | 167 |
| iOS armv7 | `0x10F2CC` | 178 |

### 1.2 crossfade derivative helper

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6BE920` | 78 |
| Android armv7 | `0x58A978` | 96 |
| iOS arm64 | `0x100113ECC` | 70 |
| iOS armv7 | `0x1118B0` | 88 |

八个函数均 fresh decompile，并完整读取 disassembly，cursor 全部 `done=true`。phase root 在
motion-sub 之后、particle-system 之前，每端只有 updateLayers root 一个直接调用者；derivative
helper 只由本 phase 的 model dt 2/3 路径调用。

## 2. 外层门控与失效路径

preview 为真时整 phase 返回。普通路径按 physical deque order 遍历非根 node，只处理 type 6。
任一条件成立即走失效路径：

- `!node.accumulated.active`；
- active slot `done`；
- active slot `src` backing pointer 为 null。

失效路径严格按顺序清 `emitterActive`、Clear retained `emitterDtgt`、把 timer 写 0，然后 continue。
它故意不清 `emitterOffsetActive`，也不覆盖旧 XYZ offset；因此上 frame 的 valid/output 可与本 frame
inactive emitter 共存。src 判断是 backing null，不是做额外字符扫描。

## 3. retained identity 与 timer

live emitter 把 active slot `src` 当 identity。timer 分支为：

```text
if node.flags == 0:
    timer += player.deltaTime
else if emitterActive && retainedDtgt == activeSrc:
    timer += player.deltaTime
else:
    emitterActive = true
    retainedDtgt = activeSrc
    parentTime = node.parameterEntry
        ? node.parameterEntry.value
        : player.frameTickCount
    timer = parentTime - active.clipStartTime
          + active.modelTimeOffset
```

零 flags 无条件 accumulate；即使 retained identity 已被前一 frame 失效路径清空，也不会重新发布
identity。非零 flags 的 equality 是 ttstr 内容 equality：共享 pointer 快捷命中，否则先长度再 raw
字符比较，不产生新字符串。

reset 分支先发布 active byte 和 retained ttstr，之后才读取 parameter pointer、start 和 timeOffset；
因此重入/异常前沿不是事务式。parameter pointer只在 reset 分支 live-load，null fallback 是 frameTick，
不是 selected parameter 或 clampedEvalTime；该子链已由 C14 独立闭合。

两条 timer 分支汇合后才把 `emitterOffsetActive=false`。因此 live emitter每 frame都会先使旧 offset
失效，再由 trigger 选择性重新发布；失效 early-out 则保留旧 valid bit。

## 4. model trigger 状态机

trigger 取 active slot `modelDt`，不是 motion dt 或 particle trigger。

model dt 4 使用 active `modelDtgt` 做 raw-label lookup。命中后写 valid=true，并写
`target.accumulated.pos - emitter.accumulated.pos` 三轴；miss 留 valid=false、XYZ旧值不变。mapped deque
index 由共享 resolver 未检查消费。

model dt 3 总是调用 derivative helper。helper 条件不满足时保持汇合点刚写的 false 和旧 XYZ。

model dt 2 在 Player `noUpdateYet` 或 timer 精确等于 `0.0` 时调用 derivative helper；否则写
valid=true，并直接复制 emitter node 的本 frame `deltaPosX/Y/Z`。`-0.0 == 0.0`，NaN 不等于零。

其它 trigger 保持 valid=false、XYZ旧值。所有路径都没有 coordinateMode 的额外过滤。

## 5. derivative helper

helper 只有 active slot `crossfading` 且 other slot 未 done 时工作，否则完全无副作用。ratio 使用
Player clampedEvalTime，不读取 node parameter pointer：

```text
ratio = (player.clampedEvalTime - active.clipStartTime)
      / (other.clipStartTime - active.clipStartTime)
candidate = ratio + 0.0001
firstTime  = candidate < 1.0 ? ratio : 0.9999
secondTime = min(candidate, 1.0)       // common source expression
first  = interpolate(active, other, firstTime)
second = interpolate(active, other, secondTime)
valid = true
offset = second - first
```

两个三轴输出 stack record在调用前清零；两次 interpolation 都完成后才发布 valid和XYZ。因此第一次
或第二次 TJS easing/curve dispatch 抛异常时，phase 汇合点已经把 valid清 false，但 helper不会提交
新输出。

四端正常 finite 域一致。candidate 为 NaN 时，共同源形状在 AArch64 optimized profile保留 NaN作为
second operand结果，而两个 ARMv7 构建的 ordered VMOVLT选择 1.0；first 四端都选 0.9999。这是同一
C++ compare/select在目标编译器上的可记录 platform profile，本地用 common source `std::min`
形状并在测试注释中显式记录 ARMv7差异。正/负 Infinity 和零 denominator均不净化。

## 6. owner、容器与边界

phase 不创建 child，也不访问 particle Array；真正的 child 生命周期属于后续 particle-system。
本 phase 的唯一持久 owner变化是 retained `ttstr emitterDtgt` 的 AddRef/Release。raw target node不被
持有，derivative helper只借用两个 slot的 Variant/position输入；interpolation内部 callback异常直接
传播。

node active-slot index和deque位置按 native未检查规则读取。target resolve、string equality、timer
arithmetic与position interpolation之间都没有锁；重入造成的 live field变化按具体 load frontier
可观察。

## 7. 本地对照与结论

`updateLayersPhase3_ParticleEmitter` 和 `updateEmitterCrossfadeDelta_guess` 的门控、identity、timer、
trigger、partial publication、旧值保留、参数 fallback、sample-time profile与 owner顺序逐项匹配。
此前已经依据同一 phase root证据删除 integer parameter resolver；完整 helper复核未发现额外差异，
本轮无需修改编译语义。

phase roots此前已统一命名；本轮又统一命名四个 derivative helper，给 root/helper追加注释、bookmark
并保存四库。

## 8. 验证限制

已有用例覆盖 active model block、失效路径保留旧 offset-valid、零 flags不重建identity、target delta
和 unordered endpoint profile。已执行 coverage严格12列、duplicate-ID检查和 `git diff --check`。
当前环境缺少正式CMake/Ninja/Emscripten依赖工具链，不能声称unit/Web build通过。
