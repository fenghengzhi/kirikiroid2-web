# MotionPlayer 简单弹簧构造、步进与封装：四参考二进制联合证据（2026-08-11）

## 1. 范围与结论

本文只收束 hair/parts 共用的 72 字节简单弹簧路径：

1. motionplayer 静态初始化中的零向量与静止单位向量；
2. `EmoteSpringState` 构造；
3. 单步弹簧求解；
4. Engine deque #1 的遍历、分步积分与 HM7 输出；
5. 四个产物的 ABI/STL 差异和小时间间隔边界。

胸部两段链式弹簧不是本文结论的一部分。它在四份参考中的构造、求解和 Engine 封装地址列在第 9 节，供下一条竖切继续取证；本地 `EmoteSpring.cpp/.h` 中该段旧 `libkrkr2.so` 注释不能当作本文证据。

联合结论：四份产物来自同一套普通 C++ 简单弹簧结构。状态对象有一个首帧 byte 和 17 个 float，总 ABI 大小为 72 字节；步进函数的真实源码级返回类型应为 `void`，可观察结果只有状态写回和两个归一化角度输出。Engine 端使用 `std::deque` 保存 owning node，按不超过 `1.1f` 的步长追赶新锚点，并把两个输出写入 HM7。

## 2. 四文件函数映射

| 参考文件 | 静态初始化 | 72B 状态构造 | 简单弹簧步进 | Engine deque #1 封装 | 坐标解析 | HM7 upsert |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `0x42EEE0` | `0x65F828` | `0x65FB48` | `0x678B28` | `0x678D50` | `0x683D24` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `0x3013BC` | `0x55176C` | `0x551910` | `0x55EE98` | `0x55F098` | `0x56559C` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `0x1001CAE20` | `0x1001A18C4` | `0x1001A1A8C` | `0x1001B29D0` | `0x1001B2C60` | `0x10010BD28` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `0x1C8EB2` | `0x1A099C` | `0x1A0BE0` | `0x1B24D8` | `0x1B2774` | `0x1096A4` |

四份函数均已在本轮通过原生 `mcp__idalib__decompile` 重新反编译；ARMv7 求解尾部又通过完整函数 `disasm` 检查，未复用旧 `libkrkr2.so` 地址。

### 2.1 2026-08-15 fresh step 复核

在语义化重写 `EmoteSpring.cpp` 前，又对四个 recovery IDB 的 step 做了一次带地址的完整 fresh decompile；当前函数大小分别为 Android A64 `0x200`、Android A32 `0x20E`、iOS A64 `0x200`、iOS A32 `0x260`。本次复核没有把本文早期伪代码或工作树旧注释当作证据，重新确认了以下源码级顺序：

1. `firstFlag != 0` 时读取原 `storedX/Y`，清 flag，再写 `prevDeltaX/Y = stored-input`；这一分支不回写 `storedX/Y`。
2. 非首帧分支先计算 `prevDelta+input`，再把结果回写 `storedX/Y`。
3. 负角的 `sinf(-angleRad)` 与正角的 `cosf(angleRad)` 均保留；没有把负号移到其他项的证据。
4. 三个末尾状态槽是一起构造、一起从 `pv` 读入并按同一积分公式更新的 XYZ velocity。原本的本地名 `accZ` 只是旧反编译临时语义，现已更正为 `velZ`。
5. `*outX` 的裸指针 store 一定发生在读取 `biasY/leverY` 计算 Y 之前。对应指令/伪代码位置为：Android A64 `0x65FCF4 -> 0x65FD10`，Android A32 `0x551AD8 -> 0x551AF4`，iOS A64 `0x1001A1C34 -> 0x1001A1C54`，iOS A32 `0x1A0DEE -> 0x1A0DF2/0x1A0DF6`。

第 5 点不是无关的编译器排程：函数没有 `restrict`、null 或 alias guard。若 `outX` 指向 `self->biasY` 或 `self->leverY`，X 输出会先覆盖该字段，随后 Y 公式读取覆盖后的值；若 `outX == outY`，最终该地址只保留后写的 Y。null 输出指针仍按原生裸 store 边界失效。源码因此必须在 X store 后才读取两个 Y 字段，不能预先缓存它们或合并为“两输出同时计算”。

写入四个 IDB 的未知源码名均带 `_guess`：

- `motionplayer_staticInit_guess`
- `EmoteSpringState_ctor_guess`
- `EmotePhysics_springStep_guess`
- `EmoteEngine_stepHairPartsSpring_guess`
- `EmoteSpring_zeroVec3_guess`
- `EmoteSpring_restUnitVec3_guess`

## 3. 静态常量与构造数据流

### 3.1 四端静态初始化

相关全局起点：

| 参考文件 | zero vec3 | rest unit vec3 |
| --- | ---: | ---: |
| Android arm64 | `0x1AB4E68` | `0x1AB4E74` |
| Android armv7 | `0x1111400` | `0x111140C` |
| iOS arm64 | `0x101B69F10` | `0x101B69F20` |
| iOS armv7 | `0x187D938` | `0x187D944` |

它们位于零填充段，所以直接读文件字节只能看到零。四份静态初始化函数都明确执行等价写入：

```cpp
zeroVec3 = {0.0f, 0.0f, 0.0f};
restUnitVec3 = {0.0f, 1.0f, 0.0f};
```

64 位把 rest X/Y 合成写成 `0x3F80000000000000`；小端解释后仍是 `{0.0f, 1.0f}`。32 位分别写两个 DWORD。四端 per-property fallback 全局未被改为非零值，保持静态零初始化。

### 3.2 构造伪代码

四端共同控制流：

```cpp
void construct(State *self, const Variant &dict) {
    self->firstFlag = 1;
    copy(self->storedXYZ, zeroVec3);
    copy(self->posXYZ, zeroVec3);
    copy(self->velXYZ, zeroVec3);

    ncbPropAccessor object{Variant(dict)};
    self->k_a    = float(object.GetValue<real>(L"gravity",  gravityHint));
    self->k_b    = float(object.GetValue<real>(L"spring",   springHint));
    self->drag   = float(object.GetValue<real>(L"friction", frictionHint));
    self->leverX = float(object.GetValue<real>(L"scale_x",  scaleXHint));
    self->leverY = float(object.GetValue<real>(L"scale_y",  scaleYHint));
}
```

2026-08-16 fresh source-identity 续证确认这里不是抽象的 `DispatchWrapper`：copied Variant
构造真实 `ncbPropAccessor` 后立即析构，五次 hinted `GetValue<tjs_real>` 忽略 getter
HRESULT，再由 ctor 执行 double→float 收窄；root accessor 活到构造尾。`gravity/scale_x/
scale_y` 三槽还与 BustChain ctor 共用。详见
`analysis/motionplayer_spring_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

构造函数故意不写 `biasY` 和 `prevDeltaX/Y`。`biasY` 由 node builder 的后续字段填充覆盖；两个 delta 在第一次步进时才定义。把这些槽在构造中补零会改变原始源码结构。

IDA 把 UTF-16LE `friction` 错显示成 ASCII `"f"`。四端原始字节都为：

```text
66 00 72 00 69 00 63 00 74 00 69 00 6F 00 6E 00 00 00
```

本轮已把四库对应数据重建为 `unsigned short aFriction[9]`，重新反编译后调用参数稳定显示为 `aFriction`，不再把单字符当成真实键名。

## 4. 72 字节状态结构

四个求解函数读取相同偏移；32/64 位均不含指针，所以对象 ABI 大小共同为 72 字节：

| 偏移 | 类型 | 语义 |
| ---: | --- | --- |
| `+0` | byte | 首次步进标志 |
| `+4` | float | `gravity` / rest-vector acceleration scale |
| `+8` | float | `spring` / anchor displacement scale |
| `+12` | float | `friction` / velocity damping scale |
| `+16` | float | Y 输出 bias，构造后由 builder 填入 |
| `+20/+24` | float | X/Y 输出倍率 `scale_x/scale_y` |
| `+28/+32` | float | 上一帧 anchor-relative delta |
| `+36/+40/+44` | float | tracked/stored XYZ |
| `+48/+52/+56` | float | integrated position XYZ |
| `+60/+64/+68` | float | integrated velocity/accumulator XYZ |

本地源码用普通字段声明让编译器自然布局；表中的 ABI 偏移只用于证据核对，不通过 `_padN` 或 `static_assert` 硬编码到 wasm 源码。

## 5. 简单弹簧共同伪代码

下面保留四端共同的计算次序和精确字面量：

```cpp
void springStep(State *s, float *outX, float *outY,
                float inputX, float inputY,
                float forceX, float forceY,
                float dt, float outputScale, float angleRad) {
    float trackedX;
    float trackedY;
    if (s->firstFlag) {
        trackedX = s->storedX;
        trackedY = s->storedY;
        s->firstFlag = 0;
        s->prevDeltaX = trackedX - inputX;
        s->prevDeltaY = trackedY - inputY;
    } else {
        trackedX = s->prevDeltaX + inputX;
        trackedY = s->prevDeltaY + inputY;
        s->storedX = trackedX;
        s->storedY = trackedY;
    }

    float sn = sinf(-angleRad);
    float cs = cosf(angleRad);
    float springDt = s->k_b * dt;
    float restDt = s->k_a * dt;
    float dampingDt = s->drag * dt;

    float forceLocalX = ((cs * forceX) - (sn * forceY)) * dt;
    float forceLocalY = ((sn * forceX) + (cs * forceY)) * dt;
    float nextVX = (cs * 0.0f - sn * 1.0f) * restDt
                 + s->velX + forceLocalX
                 + springDt * (trackedX - s->posX);
    float nextVY = (sn * 0.0f + cs * 1.0f) * restDt
                 + s->velY + forceLocalY
                 + springDt * (trackedY - s->posY);
    float nextVZ = s->velZ + springDt * (s->storedZ - s->posZ);

    nextVX -= dampingDt * nextVX;
    nextVY -= dampingDt * nextVY;
    nextVZ -= dampingDt * nextVZ;
    s->velX = nextVX;
    s->velY = nextVY;
    s->velZ = nextVZ;
    s->posX += nextVX * dt;
    s->posY += nextVY * dt;
    s->posZ += nextVZ * dt;

    float x = -((trackedX - s->posX) * outputScale * s->leverX)
            * 0.0451603944f;
    *outX = atanf(x) / 0.0392699082f;

    float y = (-(trackedY - s->posY) * outputScale - s->biasY)
            * s->leverY * 0.0451603944f;
    *outY = atanf(y) / 0.0392699082f;
}
```

四份均保留 `0.0451603944f` 与 `0.0392699082f` 的精确值；没有证据支持把它们改写成 `M_PI` 表达式。

## 6. 为什么返回类型是 `void`

最初本地实现把函数写成 `float` 并返回 `*outY`。四端尾部汇编否定了这一结论：

- Android arm64 与 iOS arm64：第二次 `atanf` 后除以 `0.0392699082`，归一化值写 `*outY`；函数退出时 S0 恰好仍是归一化值。
- Android armv7 与 iOS armv7：第二次 `atanf` 的原始结果留在核心返回寄存器 R0；归一化值通过 VFP 双精度除法转回 float 后只写 `*outY`，随后直接恢复寄存器并退出。
- 每个产物对该函数都只有两个代码调用点，全部位于本文的 Engine wrapper，两个调用点都忽略退出寄存器，只在调用后读取 `outX/outY` 栈槽。

同一共享源码不可能一边承诺返回原始 `atanf`，另一边承诺返回归一化值；而 `void` 函数允许退出寄存器保留最后一个临时结果。四端差异因此是编译器/ABI 的死寄存器残留，不是平台条件返回语义。源码已改成 `void`，没有加入按架构选择返回值的伪兼容分支。

## 7. Engine 封装、容器和边界

四端共同源码结构：

```cpp
float currentForce[2];                    // 不初始化
int count = bustOuterForce->count;
if (count >= 1)
    memcpy(currentForce, bustOuterForce->currentValue,
           sizeof(float) * count);        // 无 null 检查、无容量截断

float threshold = dt - 0.0001f;
for (Node &node : hairPartsDeque) {
    float anchorX = node.anchorX;
    float anchorY = node.anchorY;
    resolveShapeAnchor(engine, node.shapeLabel, &anchorX, &anchorY);

    float outX;                            // 故意不初始化
    float outY;
    if (node.initFlag) {
        node.initFlag = 0;
        springStep(node.spring, &outX, &outY,
                   anchorX, anchorY, currentForce[0], currentForce[1],
                   dt, bustScale, player->getAngleRad());
    } else if (threshold > 0.0f) {
        float elapsed = 0.0f;
        do {
            float subDt = fminf(dt - elapsed, 1.1f);
            elapsed += subDt;
            float f = elapsed / dt;
            float x = (1.0f - f) * node.anchorX + f * anchorX;
            float y = (1.0f - f) * node.anchorY + f * anchorY;
            springStep(node.spring, &outX, &outY,
                       x, y, currentForce[0], currentForce[1],
                       subDt, bustScale, player->getAngleRad());
        } while (threshold > elapsed);
    }

    node.anchorX = anchorX;
    node.anchorY = anchorY;
    HM7[node.keyX] = outX;
    HM7[node.keyY] = outY;
}
```

关键边界：

1. `count == 0` 时 `currentForce[0/1]` 未定义；`count == 1` 时第二项未定义；`count > 2` 时按源码数组容量发生越界。正常构造的该控制器计数为 2，但封装本身没有安全化。
2. 非首次节点且 `dt <= 0.0001f` 时不会调用 solver，却仍把未初始化的 `outX/outY` upsert 到 HM7。旧本地实现把它们预置为零，错误地定义了参考源码没有定义的结果。
3. 首次节点不受 `threshold` 门控，即使 `dt <= 0.0001f` 也步进一次并清除 node init flag。
4. `resolveShapeAnchor` 失败时保留进入 helper 前的旧 anchor 值；wrapper 仍继续求解/写回。
5. 大 `dt` 的插值分步上限固定为 `1.1f`，循环条件使用预先计算的 `dt - 0.0001f`，不是 `elapsed < dt` 的源码简化。
6. solver 自身不把 `dt == 0` 当作早退。首帧仍清 flag、定义两个 delta 并计算/写入两个输出；非首帧仍重建并写回 tracked X/Y。IEEE-754 的 NaN 也不被筛掉，例如 NaN anchor 会经过乘加、状态写回与 `atanf` 继续传播。

### 7.1 `std::deque` 的平台实现差异

节点源字段在 64 位 ABI 中占 48 字节，在 32 位 ABI 中占 28 字节。四份 wrapper 的 block 迁移表达式不同，但都证明源码容器是 `std::deque<Node>`，不是平坦 vector：

| 参考 | 节点步长 | 反编译出的单 block 有效跨度 | STL/编译器表现 |
| --- | ---: | ---: | --- |
| Android arm64 | 48B | 480B（10 节点） | libstdc++ deque map/block 迭代 |
| Android armv7 | 28B | 504B（18 节点） | libstdc++ deque map/block 迭代 |
| iOS arm64 | 48B | 4080B（85 节点） | libc++ 大 block 迭代 |
| iOS armv7 | 28B | 4088B（146 节点） | libc++ 大 block 迭代 |

这些跨度是 STL 内联展开，不进入共享源码；本地继续使用 `std::deque<EmoteHairPartsNode48B>`。

2026-08-13 的 entry-owner 纵切面进一步闭合了这里尚未展开的所有权与异常边界：
首成员是单指针 `unique_ptr<EmoteSpringState>`，builder 使用真正的参数 constructor，
随后保留 raw local 到直接 emplace 接管。constructor failure 由 new-expression 回收，
`op/p/pv/ofs` 或 grow failure 则泄漏；完整四端证据见
`analysis/motionplayer_hair_parts_entry_owner_ctor_emplace_four_binary_2026-08-13.md`。

## 8. 本地实现逐项对照与修正

| 四端共同证据 | 修改前本地 | 本轮修正 |
| --- | --- | --- |
| solver 只通过 `outX/outY` 返回结果 | 声明为 `float` 并 `return outY` | 声明/定义改成 `void`，删除伪返回 |
| rest vector 由四端静态初始化共同证明为 `(0,1,0)` | 常量名和注释只引用旧 `libkrkr2.so` 地址 | 改为语义名 `kSpringRestUnitX/Y`，地址移入本文 |
| ctor 的五个 UTF-16LE 属性和默认 0 完全一致 | 行为已相同，但注释只引用旧单文件 | 保留数据流，清除旧地址并记录四端证据 |
| wrapper 使用未初始化 `float currentForce[2]` 和直接 `memcpy` | `float cur[8] = {}`，null 检查，逐项复制且截断到 8 | 恢复 2 项未初始化数组、直接按 `count` memcpy |
| 每节点 `outX/outY` 不初始化 | 每轮预置为 `0.0f` | 恢复未初始化源码 token并注明小 dt 边界 |
| wrapper/solver 地址不应进入编译源码 | `EmoteSpring` 和目标 wrapper 有大量旧地址注释 | 已验证的简单弹簧段改为语义注释；地址集中到本文 |
| constructor throw 必须回收 allocation，构造后读取/grow failure 必须保留泄漏 | free ctor + 外层 raw delete loop 无法同时表达两个阶段 | 恢复参数 constructor、entry `unique_ptr` 与 raw-pointer emplace 接管 |
| stripped solver 没有可证实源码名 | 本地公开入口仍使用无后缀推测名 | 改为 `EmotePhysics_springStep_guess` 并同步 wrapper/tests |
| `+60/+64/+68` 是 `pv` XYZ velocity | Z 槽命名为 `accZ` | 改为 `velZ`，保持字段布局和公式不变 |
| X 输出先写，Y 字段随后读取 | 计算顺序虽碰巧相同，但没有边界回归 | 保留顺序并增加 `outX=&biasY` 别名测试 |

单元测试直接覆盖 solver 的首帧分支、rest-unit Y 加速度、精确 atan 归一化输出和非首帧 tracked-anchor 重建。2026-08-15 又加入首帧 `dt == 0`、非首帧 NaN 传播，以及 `outX=&state.biasY` 的顺序可观察别名回归。未对 wrapper 的未初始化栈槽构造运行时断言，因为读取未初始化值本身不适合作为稳定测试 oracle；它由四份机器控制流和编译级回归守护。

## 9. 链式弹簧后续映射（不属于本文已验证语义）

> **后续闭环（2026-08-15）：**本节当时列出的 chain solver/ctor/wrapper 已由
> `motionplayer_bust_chain_spring_four_binary_2026-08-11.md` 完整复核；Android ARM64 的
> 风查询与 post-bend 内联、其余三端独立 helper、自然成员布局、Engine wrapper、未初始化
> 输出和边界测试均已闭合。本节地址表只保留为前期导航，不再表示仍有待办。

| 参考文件 | chain solver | chain ctor | Engine chain wrapper |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x665D84` | `0x6662D8` | `0x6790C8` |
| Android armv7 | `0x555010` | `0x5554F0` | `0x55F2F4` |
| iOS arm64 | `0x1001A5BDC` | `0x1001A6104` | `0x1001B2F2C` |
| iOS armv7 | `0x1A51CC` | `0x1A5710` | `0x1B2ABC` |

Android armv7、iOS arm64、iOS armv7 另保留独立的 chain 后处理 helper，分别位于 `0x555408`、`0x1001A6030`、`0x1A5634`；Android arm64 对应逻辑可能内联或与 solver 合并，必须在下一轮通过完整调用链解释，不能据三份独立函数直接宣称第四份“缺失”。

## 10. IDB 改进与验证状态

四库均完成：

1. 四个目标函数/两个全局 dry-run 零冲突后正式 rename；
2. solver/ctor/wrapper/static-init 设置 `void` 函数类型；
3. 导入 IDA 专用的 `EmoteSpringState72_guess`，显式记录首 byte 后 ABI gap，使字段精确落到 `+4..+68`；该 gap 只用于反编译数据库，不进入共享 C++ 源码；
4. ARMv7 使用其硬浮点机器参数排列显示 solver，避免源码形参顺序直接套入 IDA 后把 output pointer 误认成 float；
5. 修复四份 UTF-16LE `friction` 数据边界；
6. 给 ctor、solver、wrapper、static-init 追加四端语义与边界注释。

验证结果：

- `cmake --build --preset "Web Debug Build" --target motionplayer`：通过；
- `cmake --build --preset "Wasmtime Headless Debug Build" --target motionplayer`：通过；
- 复用 Web Debug `compile_commands.json` 的真实 Emscripten 参数，并加入 `out/syntax-check` 的 Catch2/test-config 头目录，对完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过，仅有仓库既有 `_tss` warning；
- 完整 `cmake --build --preset "Web Debug Build"`：成功链接最终 `index.html/index.wasm`；
- `cmake --build --preset "Wasmtime Headless Debug Build" --target krkr2_wasmtime_guest`：成功链接 guest wasm 并完成 exnref 转换；
- 两个完整目标再次增量构建均返回 `ninja: no work to do`；
- `git diff --check`：通过，仅有工作树既有 LF→CRLF 提示；
- 当前 `out/windows/debug` 没有可运行的有效原生 Catch2 构建目标，因此新增测试完成的是完整翻译单元编译验证，没有被误报成运行时测试通过；
- 四份 IDB 均在最终类型、字符串边界和重新反编译确认后原位保存成功。

### 10.1 2026-08-15 当前验证增量

- 完整测试翻译单元的 Emscripten `-fsyntax-only`：通过，仅有仓库既有 `_tss` deprecated warning；
- 完整 `cmake --build --preset "Web Debug Build"`：通过并重新链接 `index.html/index.wasm`；
- simple solver 中的反编译临时名 `aN/vNN` 已全部移除；余下 `EmoteSpring.cpp` 的 `vNN` 集中在下一条尚待 fresh 四端复核的 bust-chain solver，不能用本节结论批量重命名；
- 当前 headless/Wasmtime 路径仍受仓库既有 `math/Mat4.h` include 问题阻断，本增量没有把它误报为通过；
- 四个 recovery IDB 的首帧、非首帧、X store 和 Y 字段读取边界已追加当前注释，并在最终重新反编译后原位保存。
