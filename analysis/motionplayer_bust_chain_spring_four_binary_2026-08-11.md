# MotionPlayer bust/chain spring：四参考二进制纵向复原（2026-08-11）

## 结论

这一纵切面覆盖两段链弹簧的构造、元数据覆盖、核心求解、风粒子查询、弯曲后处理与 Engine 包装调用。四份参考共同证明：

> 2026-08-13 生命周期补记：四端 builder 的 `new + ctor(elem)`、raw emplace、entry
> clear/full destructor 已在
> `motionplayer_chain_entry_owner_ctor_emplace_four_binary_2026-08-13.md` 独立闭环。
> `EmoteBustChainSpring_ctor_guess(self, dict)` 已纠正为真实参数 constructor；deque #2/#3
> element 中的 spring 字段已纠正为单指针 `unique_ptr` owner。本文后续数学与字段结论保持有效。

> 2026-08-16 metadata source-identity 续证：root、`length`、`scale_x`、`scale_y` 都是实际
> `ncbPropAccessor`；三个 nested owner 各读完 index 0/1 即顺序 Release，root 最后释放。
> `gravity/scale_x/scale_y` hint 与 simple spring 共用，`length` 复用 controller-state 槽，
> 其余七个标量使用 BustChain-only family。详见
> `analysis/motionplayer_spring_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

- 链状态的源字段序列是标量参数、`length[2]`、`ofs/bendR/bendS`、按段交错的 `scale[2][2]`、`prevDelta[2]`、`op[3]`、`p/pv/bp[2][3]`，最后是一个借用的风发射器指针。
- 指针在 ARM32 自然位于 `+164`，对象大小 `168`；在 ARM64 因 8 字节自然对齐位于 `+168`，对象大小 `176`。旧源码中的 `_pin164` 是把 ABI 填充误判成了源成员。
- 三份参考保留独立风查询 helper 与弯曲后处理 helper；Android ARM64 将两者内联。共同源结构应保留 helper，而不是按一个优化产物把逻辑焊进调用者。
- 核心求解器与弯曲后处理的调用者都忽略返回寄存器。后处理在三个 ABI 上留下不同的退出残值，因此其共同源签名是 `void`。
- Engine 包装器使用未初始化的 `float currentForce[2]` 和三个未初始化输出；`count >= 1` 时按 `count * 4` 字节直接复制，没有空指针检查或容量上限。非首帧且 `dt <= 0.0001f` 时仍把这些未写输出插入 HM7。这是四份参考共同保留的边界行为。

源码已据此移除链状态的字节偏移访问和伪填充字段，改为自然 C++ 成员/数组；地址只保留在本分析文档中。

## 地址映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| 128 槽风力查询 | 内联于核心求解器 | `0x554FA0` | `0x1001A5B78` | `0x1A5160` |
| 两段链核心求解器 | `0x665D84` | `0x555010` | `0x1001A5BDC` | `0x1A51CC` |
| 弯曲深度/相位后处理 | 内联于 Engine 包装器 | `0x555408` | `0x1001A6030` | `0x1A5634` |
| 链状态构造 | `0x6662D8` | `0x5554F0` | `0x1001A6104` | `0x1A5710` |
| Engine 链包装器 | `0x6790C8` | `0x55F2F4` | `0x1001B2F2C` | `0x1B2ABC` |
| 链元数据 builder | `0x668DB0` | `0x556B84` | `0x1001A87C0` | `0x1A7DCC` |

四个 builder 的构造调用点分别为 `0x668FC8`、`0x556CD4`、`0x1001A88FC`、`0x1A7F1C`。实际 `operator new` 参数在两份 64 位参考均为 `0xB0`，两份 32 位参考均为 `0xA8`。

### 2026-08-15 fresh solver/helper 复核

在移除 `EmoteSpring.cpp` 链 solver 的 `v20..v94` 反编译临时名之前，本轮重新完整反编译四个核心函数、三个独立 post helper，并重新检查 Android A64 wrapper 的两份内联 post 体。没有复用旧 `libkrkr2.so` 注释。核心函数当前大小为 Android A64 `0x484`、Android A32 `0x3D2`、iOS A64 `0x454`、iOS A32 `0x452`；三个独立 post helper 大小分别为 `0xD8/0xD4/0xD4`。

fresh 证据补充了早期文档没有明确锁定的边界：

1. 两个约束条件都是 ordered `>`：先比较 `distanceSquared > restLength*restLength`，再比较 `sqrt(distanceSquared) > 0.015625f`。任一参与值为 NaN 时不会进入约束分支。
2. 第 1 段的过长修正先执行 `pv[1] += extension*direction`，这个位置修正不乘 `dt`；只有随后沿方向移除 velocity projection 的量乘 `dt`。因此 `dt == 0` 仍会移动过长的第 1 段位置，同时不改变其约束投影速度。
3. 外力 Z 与 gravity Z 都保留显式的 `dt * 0.0f`/`gravityDt * 0.0f`。普通有限 `dt` 时看似是 no-op，但 NaN `dt` 会让 Z velocity/position 传播 NaN；不能在语义化过程中删成“Z 不受力”。
4. `udEft` 只与循环索引 0、1 直接比较，没有 clamp 或默认段。超出 `{0,1}` 时 `outLastY` 完全不写；wrapper 后续仍消费该未定义栈槽。
5. post 的 `fabs(lastY) <= 28.0f` 同样是 ordered compare；`lastY=NaN` 落入增长分支，而不是衰减分支。post 先写 `*outSeg1 += bend`，再写 `*outSeg0 -= bend`，裸指针 alias 顺序仍可观察。

四个 stripped 核心函数的 IDB 与本地源码名都已统一为 `EmoteBustChainSpring_step_guess`。Android A64 仍保留“风查询与 post 在优化产物中内联、共享源码中维持 helper”的平台边界。

## 共同源布局与 ABI 投影

字段语义和偏移由四个构造函数、四个核心求解器及四个 builder 交叉约束：

| 源字段 | ARM32 偏移 | ARM64 偏移 | 数据流 |
|---|---:|---:|---|
| `firstFlag` | `+0` | `+0` | 首次求解分支，首次后清零 |
| `gravity` | `+4` | `+4` | `gravity * dt` 的静止方向加速度 |
| `frictionX/Y` | `+8/+12` | 同左 | 每步 X/Y 速度阻尼 |
| `bRate` | `+16` | 同左 | 第 0 段伸长约束的速度增量比例 |
| `vBound` | `+20` | 同左 | 第 1 段沿约束方向的速度投影消除比例 |
| `udEft` | `+24` | 同左 | 选择写 `outLastY` 的段索引 |
| `bendSpd/Vol` | `+28/+32` | 同左 | 后处理相位速度与摆幅 |
| `length[2]` | `+36` | 同左 | 两段各自静止长度 |
| `ofs` | `+44` | 同左 | 所选段 Y 角计算偏置；由 builder 覆盖 |
| `bendR/S` | `+48/+52` | 同左 | 相位与深度；ctor 清零、builder 可恢复 |
| `scale[2][2]` | `+56` | 同左 | 每段 `{scale_x, scale_y}`，8 字节段步长 |
| `prevDelta[2]` | `+72` | 同左 | 根位置相对输入锚点的跨帧差；ctor 不写 |
| `op[3]` | `+80` | 同左 | 根点；ctor 清零，builder 从 `param.op` 覆盖 |
| `p[2][3]` | `+92` | 同左 | 每步重建的目标链位置；builder 也可恢复快照 |
| `pv[2][3]` | `+116` | 同左 | 两段当前位置 |
| `bp[2][3]` | `+140` | 同左 | 两段速度 |
| ABI 对齐填充 | 无 | `+164..+167` | 仅 64 位尾指针自然对齐，不是源字段 |
| `collisionCurve` | `+164` | `+168` | 借用 `EmoteWindEmitter*`，Engine 每次包装调用前覆盖 |
| `sizeof` | `168` | `176` | 与 builder 的 `new(0xA8/0xB0)` 一致 |

旧注释把 `+100/+112` 解释成整数 flag，把 `+164` 解释成 float pin；四参考否定了这两点。`op[2]`、`p[*][2]` 全程作为浮点 Z 坐标参与距离和积分，`+164` 只在 64 位存在为对齐空洞。

## 构造与元数据覆盖

四个构造函数的共同顺序为：

1. `firstFlag = 1`。
2. `bendR = bendS = 0`。反编译中的 `QWORD +48 = 0` 是两个 float，不是只写 `bendR`。
3. `op = {0,0,0}`。旧移植只清零 X/Y，遗漏了 Z。
4. 清零 `p/pv/bp` 和尾部风指针。ARM64 因中间对齐洞表现为 `memset(+92, 0, 0x48)` 加独立 `QWORD +168=0`；ARM32 优化器可把连续尾区合并至包含 `+164` 指针。
5. root `ncbPropAccessor` 读取 `gravity`、`friction_x`、`friction_y`、`b_rate`、`v_bound`、整数 `ud_eft`、`bend_spd`、`bend_vol`；real 值由 double 窄化为 float，getter HRESULT 被忽略。
6. `length`、`scale_x`、`scale_y` 各由返回 Variant 构造独立 nested accessor，取索引 0/1 后立即 Release；scale 的对象内排列是按段交错的 `{x0,y0,x1,y1}`。
7. 用运行时静态初始化得到的 `(0,1,0)` 构造两段初始 `p`，复制至 `pv`，再次把 `bp` 两段清零。

`ofs` 和 `prevDelta` 在 ctor 中故意不写。builder 随后用 `param` 覆盖 `op/ofs/bendR/bendS/p/pv/bp`；`prevDelta` 只在第一次求解时由 `op - anchor` 写入。把这些局部或字段预先“安全初始化”会改变参考的源 token 和异常/边界可见状态。

构造函数引用的 UTF-16 字面量曾被 IDA 错切成单字符 `"f"/"b"/"v"/"u"`。逐字节读取确认实际完整内容为：

- `friction_x`：`66 00 72 00 69 00 63 00 74 00 69 00 6f 00 6e 00 5f 00 78 00 00 00`
- `friction_y`：同前缀，尾字符 `79 00 00 00`
- `b_rate`、`v_bound`、`ud_eft`、`bend_spd`、`bend_vol` 均为完整 UTF-16LE 零终止串。

对应数据项已在四个 IDB 中修复为完整 `unsigned short[]`。

## 风力查询 helper

三份独立 helper 与 Android ARM64 的内联体完全一致：

```cpp
float lookup(const EmoteWindEmitter *emitter, float segmentX) {
    for (int i = 0; i < 128; ++i) {
        const auto &slot = emitter->slots[i];
        if (slot.active) {
            float half = slot.yPos * 0.5f + 4.0f;
            if (slot.lifePos - half < segmentX &&
                slot.lifePos + half > segmentX)
                return slot.yPos * emitter->velocity;
        }
    }
    return 0.0f;
}
```

关键边界：

- 固定扫描 128 个、12 字节 stride 的槽；首个命中立即返回。
- 两端比较均严格，不包含恰好等于区间端点的 X。
- inactive 槽完全跳过。
- helper 自身不接收/处理 null；核心求解器只在尾指针非 null 时调用。
- 返回值是真实语义返回，与弯曲 helper 的退出寄存器残值不同。

## 两段核心求解数据流

共同流程：

1. 首帧记录 `prevDelta = op.xy - anchor.xy` 并清 `firstFlag`；后续帧用 `prevDelta + anchor` 重建 `op.xy`。
2. 每步从 `op` 重建目标 `p[0] = op + (0,length[0],0)`，再由 `p[0]` 重建 `p[1]` 并增加 `length[1]`。
3. 将外力 `(forceX, forceY)` 按 `angleRad` 旋转并乘 `dt`。
4. 对段 0、1 顺序求解：前点分别为 `op` 与已经更新过的 `pv[0]`。
5. 仅当当前距离平方大于静止长度平方，且 `sqrt(distance2) > 0.015625f` 时施加伸长约束。
6. 段 0 用 `bRate * extension * dt` 累加速度；段 1 先把位置沿单位方向推进 extension，再用 `vBound * dot(direction,bp[1]) * dt` 消除速度投影。
7. 累加旋转外力、`gravity * dt` 静止方向项和可选风力，再施加 X/Y 摩擦并积分 `pv`。
8. 每段写 X 角：`atan(targetXError * scale[seg][0] * scale * -0.0451603944f) / 0.0392699082f`。
9. 当 `seg == udEft` 时写 Y 角，使用 `ofs`、目标/实际 Y 差、`scale[seg][1]` 与正的 `0.0451603944f`。

循环中的第二段 parent 是本次调用里已经完成约束、外力、风力、阻尼和积分后的 `pv[0]`，不是进入函数时的旧快照，也不是目标数组 `p[0]`。这条同调用依赖决定了两段更新顺序，不能并行化或先统一算完两个 delta。

四个调用者都忽略核心求解器的退出寄存器，已统一标成 `void`。

ARM32 的真实传参槽进一步确定共同源顺序为
`(state, anchorX, anchorY, outSeg0, outSeg1, outLastY, forceX, forceY, dt, scale, angleRad)`：
入口直接把 `R1/R2` 当两个 anchor 浮点位，把 `R3` 当第一个输出指针；调用点随后在
栈上依次放另外两个输出指针、四个步进浮点量和 angle。旧移植把三个输出指针提前到
anchor 之前，虽然在本地调用闭环内数值可工作，却不符合共同源函数结构。

## 弯曲后处理 helper

三个独立实现与 Android ARM64 两处内联体的共同伪代码：

```cpp
void post(State *s, float lastY, float *out0, float *out1, float dt) {
    float amount = dt * 0.03125f;
    if (fabs(lastY) <= 28.0f)
        s->bendS = max(s->bendS - amount, 0.0f);
    else
        s->bendS = min(s->bendS + amount, 1.0f);
    s->bendR = fmod(s->bendR + s->bendS * s->bendSpd * dt,
                    6.28318531f);
    float bend = sin(s->bendR) * s->bendS * s->bendVol;
    *out1 += bend;
    *out0 -= bend;
}
```

Android ARMv7 与 iOS ARMv7 退出时残留 `sinf` 结果，iOS ARM64 残留更新后的 `*out0`；调用者均不消费。这一 ABI 间不一致是 `void` 源签名的直接证据。

参数顺序也由原始寄存器分配而非反编译器猜测确定：ARM32 为
`R0=self, R1=lastY bits, R2=outSeg0, R3=outSeg1, dt=stack`；iOS ARM64
为 `X0=self, S0=lastY, X1=outSeg0, X2=outSeg1, S1=dt`。因此共同源
顺序是 `(state, lastY, outSeg0, outSeg1, dt)`。

## Engine 包装器、容器和边界行为

四个包装器共同执行：

1. 声明未初始化 `float currentForce[2]`。
2. 若 controller `count >= 1`，直接 `memcpy(currentForce, currentValue, count * 4)`；无 controller/currentValue null 检查，也无 `count <= 2` 限制。
3. 遍历链 deque，解析 shape anchor，把 `spring->collisionCurve` 设为 Engine 当前 `_windEmitter` 借用指针。
4. 声明未初始化 `outSeg0/outSeg1/outLastY`。
5. 节点 init 分支无条件以完整 `dt` 求解一次并执行后处理。
6. 非 init 仅当 `dt - 0.0001f > 0` 时进入子步；每步 `min(dt-acc, 1.1f)`，锚点在旧值与当前解析值间线性插值；每个子步之后执行后处理。
7. 无论是否真正求解，写回解析后的 anchor，再依次把 `outSeg1`、`outSeg0`、`outLastY` upsert 到 HM7 的 keyA/keyB/keyC。

deque 内部块布局随标准库不同：

| 参考 | 节点大小 | 块大小/节点数 |
|---|---:|---:|
| Android ARM64（libstdc++） | 56 | 504 / 9 |
| Android ARMv7（libstdc++） | 32 | 512 / 16 |
| iOS ARM64（libc++） | 56 | 4088 / 73 |
| iOS ARMv7（libc++） | 32 | 4096 / 128 |

这与简单弹簧 deque 的节点/块参数不同，不能把宿主 STL 块公式硬编码进跨平台源码；本地继续使用普通 `std::deque` 和自然节点成员。

## IDB 改进

四个数据库已执行并保存：

- 核心、构造、Engine 包装器早期分别恢复为 `EmoteBustChainSpring_step`、`EmoteBustChainSpring_ctor`、`EmoteEngine_stepBust`；2026-08-15 按 stripped-name 规则把核心更正为 `EmoteBustChainSpring_step_guess`，2026-08-16 同理把 ctor 更正为 `EmoteBustChainSpring_ctor_guess`。
- 三个独立查询/后处理统一命名为 `EmoteWindEmitter_lookupForce_guess`、`EmoteBustChainSpring_postBend_guess`。
- 导入显式 ABI 投影类型 `EmoteBustChainSpring_guess`。由于 IDA 当前 TIL 的默认 pack 会错误地把首个 float 放在 `+2`，IDA-only 类型显式描述首部 3 字节 gap；64 位版本还显式描述尾指针对齐 gap。复核结果为 ARM32 `sizeof=168, pointer=+164`，ARM64 `sizeof=176, pointer=+168`。这些 gap 不进入项目源结构。
- 核心和后处理应用 `void` 原型，查询应用真实 `float` 返回原型；构造按无源返回值标注。
- 为布局、边界复制、未初始化输出、内联差异和 helper 语义添加函数注释。
- 修复所有本纵切面发现的截断 UTF-16 数据项，并将四个 IDB 原地保存。

## 源码与测试

修改范围：

- `cpp/plugins/motionplayer/EmoteSpring.h`
- `cpp/plugins/motionplayer/EmoteSpring.cpp`
- `cpp/plugins/motionplayer/EmoteEngine.h`
- `cpp/plugins/motionplayer/EmoteEngine.cpp`
- `tests/unit-tests/plugins/motionplayer-dll.cpp`

新增确定性测试覆盖：

- 风查询完整 128 槽扫描、严格区间端点和力值。
- `lastY` 的 `<= 28`/`> 28` 分支、深度双向 clamp、相位更新及两输出等量反向偏移。
- 两段首次求解的 delta、目标链重建、每段位置/速度与三个输出。
- `dt == 0` 时第 1 段仍执行过长位置修正，而 velocity projection 保持零。
- `udEft` 超出 0/1 时 `outLastY` sentinel 不变。
- NaN `dt` 通过显式零向量乘法传播到两个段的 Z velocity/position。
- post helper 的 NaN `lastY` 增长分支，以及 `outSeg0 == outSeg1` 时先加后减的裸指针写入顺序。

阶段验证：

- `cmake --build --preset "Web Debug Build" --target motionplayer`：通过，仅有仓库既存警告。
- `cmake --build --preset "Wasmtime Headless Debug Build" --target motionplayer`：通过，仅有仓库既存警告。
- 使用 Web Debug `EmoteEngine.cpp` 的实际编译参数对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 Emscripten
  `-fsyntax-only`：通过，仅有仓库既存的 `_tss` 弃用警告。
- `cmake --build --preset "Web Debug Build"`：完整 Web 链接通过；仅有既存的
  pthread/memory-growth、JSPI 实验性与 JS library 警告。
- `cmake --build --preset "Wasmtime Headless Debug Build" --target krkr2_wasmtime_guest`：
  guest WASM 编译、链接及 exnref 转换通过，仅有仓库既存警告。
- `git diff --check`：通过；仅报告工作树现有的 LF/CRLF 转换提示。

本轮没有声称运行 Catch2 测试用例：当前可用构建树没有可直接执行该翻译单元的
原生测试目标，因此以两个 WASM 构建面、完整链接和真实编译参数语法检查共同验证。

### 2026-08-15 当前验证增量

- 完整测试翻译单元的 Emscripten `-fsyntax-only`：通过，仅有仓库既有 `_tss` deprecated warning；
- 完整 `cmake --build --preset "Web Debug Build"`：通过并重新链接最终 `index.html/index.wasm`；
- `EmoteSpring.cpp` 的简单 spring 与两段 chain solver 已无 `aN/vNN/LABEL_*` 反编译残留；
- 当前 headless/Wasmtime 路径受仓库既有 `math/Mat4.h` include 问题阻断，本增量没有重复早期环境的通过结论；
- 四库核心函数已改为 `_guess`，约束、`dt==0` 位置修正、NaN 零乘法、`udEft` miss 与 post ordered compare 均已注释/书签化并在重新反编译后保存。
