# MotionPlayer `EmoteEngine::directEdit` receiver/字段命名四参考复原（2026-08-15）

## 1. 结论

Motion.EmotePlayer 的 `directEdit` 属性由 `EmoteEngine` 内一个独立 byte 支撑。它不是：

- `Player::_syncWaiting`（timeline cooperative-stop 状态）；
- `Player::_directEdit`（inner Player 自己的 direct-edit motion/angle 模式）；
- Player physics-disabled byte；
- setter 传入 Boolean 的普通可逆存储。

四份当前参考完全一致：getter 原样读取 Engine byte；setter 忽略 typed Boolean 参数并把
常数 `1` 写到该 byte；通用 `setVariable` 和 Engine progress physics-tail gate 读取同一
byte。constructor 清零后没有脚本侧 clear 路径。

本地 Engine 字段旧名 `_syncWaiting` 会与真实 `Player::_syncWaiting` 混淆，并把 progress
gate 误解释为同步等待。本轮将 Engine 字段重命名为 `_directEdit`，只迁移
EmoteEngine/EmotePlayer receiver 链；inner Player 的同步状态完全不改。

## 2. 字段与访问器

| 目标 | Engine byte | getter | setter | getter load / setter store |
|---|---:|---:|---:|---:|
| Android arm64 | `+1159` | `0x67F358` | `0x67F360` | `0x67F35C / 0x67F364` |
| Android armv7 | `+591` | `0x562104` | `0x56210A` | `0x562108 / 0x56210C` |
| iOS arm64 | `+791` | `0x1001B61F4` | `0x1001B61FC` | `0x1001B61F8 / 0x1001B6200` |
| iOS armv7 | `+407` | `0x1B5FDC` | `0x1B5FE2` | `0x1B5FE0 / 0x1B5FE4` |

归一化访问器为：

```cpp
bool getDirectEdit(EmoteEngine *engine) {
    return engine->directEditByte;
}

void setDirectEdit(EmoteEngine *engine, bool /*ignored*/) {
    engine->directEditByte = true;
}
```

setter 机器码没有读取第二参数、Variant value 或旧 byte；false、Void 经 ncbind typed
conversion 后与 true 一样触发置一。重复赋值也重复 store，不做 change detection。

## 3. constructor 生命周期

四端 `EmoteEngine_ctor_guess`：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x67B76C` | `0x560948` | `0x1001B7FB0` | `0x1B7788` |

directEdit byte 位于 wind float cache 后、selector/queuing/dirty/debug byte 前。constructor
通过邻接宽 store/memset 将其清零：Android arm64 的 wind/cache store 覆盖末字节，
Android armv7 的 0x1c-byte clear 以该 byte 结束，iOS 两端的邻接 qword/vector clear
同样覆盖它。成功构造的初值为 false；后续 controller seed 只把 dirty 置一，不改变
directEdit。

normal destructor 不需要对 plain byte 做动作。constructor unwind 也没有独立 cleanup；
byte 只有在完整 Engine 对象可用后才经属性 setter 置一。

## 4. `setVariable` 数据流

四端函数入口：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x66E608` | `0x559D84` | `0x1001ACDBC` | `0x1AC5F4` |

HM6 命中 category 0、1、2 时的 byte read 位于：

`0x66E738 / 0x559E00 / 0x1001ACE54 / 0x1AC66C`。

- directEdit=false：这些物理/constant-feed 类型由单独 pass 处理，本函数提前返回；
- directEdit=true：跳出 category switch，落入共同 `HM7[key] = value`；
- HM6 miss：不论 directEdit 值都落入 HM7；
- category 4..8：仍按对应 typed deque/controller 分支处理。

旧本地注释写成“sync-waiting 时落入 HM7”会把 Engine 属性与 Player timeline stop 混为
一条状态机；字段重命名后改成 direct-edit routing gate。

## 5. progress physics-tail gate

四端 `EmoteEngine_progressCore_guess` 入口：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x67A3F8` | `0x55FEF0` | `0x1001B4304` | `0x1B3E10` |

在 controller slices、HM7-to-Player bind、clamp controls 和一次 Player progress bridge
之后，四端执行同一条件：

```cpp
if (originalDt != 0.0 && !engine.directEdit)
    runPhysicsOnlyTail(float(originalDt));
```

byte check 位于
`0x67A7F8 / 0x55FFBA / 0x1001B43EC / 0x1B3EE4`。这里使用原始 double dt 比较零，
只在进入 tail 后转换一次 float；不是已经被 `<=1.1` slice loop 消耗完的局部 dt。
directEdit=true 跳过三个 outer-force controllers、hair/parts springs 和 bust chain physics，
但不跳过此前的 Player progress bridge。

## 6. 与 inner Player 两个 byte 的隔离

`Player::_syncWaiting` 的四端 offset 为 `+1098/+750/+986/+686`。它由 sync-enabled
timeline streams 在命中 sync frame 时置一，frame core 反复将其与 motionCompleted 一起
作为 cooperative-stop gate；`releaseSyncWait` 清零。它有真实的 set/clear 生命周期，
与 Engine directEdit 的 one-way property 不同。

`Player::_directEdit` 还是另一项 Player byte，用于 Player 角度/Emote motion 的 direct-edit
路径。两个类可以各自拥有同名 source concept；关键是 receiver 不能互换。EmotePlayer 的
Engine owner 与 inner Player owner 之间没有 byte alias 或同步写入。

## 7. 本地结果

- `EmoteEngine.h`：backing byte `_syncWaiting` -> `_directEdit`；
- `EmotePlayer.h`：one-way setter/getter 明确访问 Engine `_directEdit`；
- `EmoteEngine.cpp`：HM6 category 0..2 route 与 physics-tail gate 改用语义名，相关注释
  不再描述“sync waiting”；
- tests：Engine constructor default 与 typed property one-way assertion 改用新名；
- `Player::_syncWaiting`、`Player::_directEdit` 及它们的所有 writer/reader 保持不动。

绝对地址只保存在本分析文件和四份 recovery IDB；compiled source 只保留跨 ABI 的行为与
receiver 语义。
