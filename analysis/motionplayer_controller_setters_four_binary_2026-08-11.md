# MotionPlayer coord/scale/rotate/color controller setter 四参考二进制审计

> **2026-08-16 unordered 边界勘误：** Var setter 的 `B.LS/BLS` 在 NaN 时不跳
> immediate，本文 Var 伪码的 `duration <= 0.0f` 仍然正确；Angle setter 则使用
> `B.LE/BLE`，NaN 会 immediate，portable C++ 必须写成
> `!(duration > 0.0f)`。五类 controller 的条件码分叉和 20 个原始指令点见
> `analysis/motionplayer_controller_duration_unordered_split_four_binary_2026-08-16.md`。

> **2026-08-12 构造细化：** 本文关于 20B word 写序和 alpha/duration alias 的
> 结论仍有效，但旧本地实现曾用 `keyframe{}` 把未覆盖 word 清零，并通过完整
> temporary `push_back`。四端 emplace helper 的原位构造、未初始化 word 边界和
> Engine 内最后一份私有重复 setter 的删除见
> `analysis/motionplayer_var_controller_emplace_four_binary_2026-08-12.md`。

## 结论

`Motion.EmotePlayer` 的 `setCoord/setScale/setRotate/setColor` 不把目标值写入
`Player` 或 `EmoteEngine` 尾部影子字段。四个入口统一执行：解析脚本参数、把
脚本 `ease` 转换为幂指数、置 Engine dirty、把目标写入 Engine 直属 controller。

`Motion.D3DEmotePlayer` 的对应方法沿壳对象的 primary `EmoteObject` 进入同一组
Engine controller。D3D 方法本体不执行脚本 ease 转换，而是把收到的 `power`
原样转换为 `float`。`setScale` 另外保存壳层 user scale，并把
`baseScale * userScale` 送给 Engine；公开 getter 却仍是常量：`getScale()==1`、
`getRot()==0`、`getColor()==0`。

这轮还确认了一个固定容器边界：普通变量 controller 的 native keyframe 恰好
20 字节，只有四个前置 float word 和一个 power word。push helper 先把 duration
写到 word 3，再从 word 0 开始复制 `count` 个 channel。因此 color controller 的
`count==4` 会用 alpha 覆盖 duration；后续 step 同时把该 word 当 alpha 目标和
duration 使用。这是四份参考二进制共有的真实边界行为，不应以一个额外 duration
字段“修正”。

旧端口中 `_coordX/_coordY/_rot/_color` Engine 尾部影子、Player 侧四组 animator
状态以及直接写状态的 convenience setter 均没有本轮四目标证据，现已删除。

## Motion.EmotePlayer 脚本入口映射

| 目标 | `setCoord` | `setScale` | `setRotate` | `setColor` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x66F440` | `0x66F6FC` | `0x66F948` | `0x66FB5C` |
| Android armv7 | `0x55A450` | `0x55A548` | `0x55A628` | `0x55A6E8` |
| iOS arm64 | `0x1001AD778` | `0x1001AD894` | `0x1001AD970` | `0x1001ADA3C` |
| iOS armv7 | `0x1ACE5C` | `0x1ACF52` | `0x1AD010` | `0x1AD0CC` |

2026-08-15 对 16 个入口再次 fresh decompile，并恢复了连续注册槽的 native-instance
raw callback ABI、argc 错误码、参数转换次序与 float 窄化提交点。完整证据见
`analysis/motionplayer_primary_raw_controller_setters_four_binary_2026-08-15.md`。
共同的脚本 ease 转换先在 double 域完成，再在 controller 调用边界窄化为 float：

```cpp
double scriptEaseToPower(double ease) {
    if (ease > 0.0) return ease + 1.0;
    if (ease < 0.0) return 1.0 / (1.0 - ease);
    return 1.0;
}
```

归一化调用链为：

```cpp
setCoord(x, y, duration = 0, ease = 0):
    engine.dirty = true
    varSetTarget(engine.position, {float(x), float(y)},
                 float(duration), scriptEaseToPower(ease), engine.append)

setScale(scale, duration = 0, ease = 0):
    engine.dirty = true
    varSetTarget(engine.scale, {float(scale)},
                 float(duration), scriptEaseToPower(ease), engine.append)

setRotate(angle, duration = 0, ease = 0):
    engine.dirty = true
    angleSetTarget(engine.angle, float(angle),
                   float(duration), scriptEaseToPower(ease), engine.append)

setColor(argb, duration = 0, ease = 0):
    values = {float(argb & 0xff), float((argb >> 8) & 0xff),
              float((argb >> 16) & 0xff), float((argb >> 24) & 0xff)}
    engine.dirty = true
    varSetTarget(engine.color, values,
                 float(duration), scriptEaseToPower(ease), engine.append)
```

`duration/ease` 缺省均从 `0.0` 开始；因此省略或显式传 `ease==0` 最终都得到
power `1.0`。入口只要求各自的目标值参数；缺少目标参数返回
`TJS_E_BADPARAMCOUNT`。argc 覆盖的 slot 没有 null 容错，转换严格按目标值、可选
duration、可选 ease 的顺序进行。

## Motion.D3DEmotePlayer 方法映射

| 目标 | `setCoord` | `setScale` | `setRot` | `setColor` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x5305CC` | `0x530640` | `0x5306C4` | `0x5306F4` |
| Android armv7 | `0x494B34` | `0x494BB0` | `0x494C36` | `0x494C98` |
| iOS arm64 | `0x100232F7C` | `0x100232FF8` | `0x100233054` | `0x100233084` |
| iOS armv7 | `0x231B9E` | `0x231C20` | `0x231C86` | `0x231CDE` |

| 目标 | `getScale` | `getRot` | `getColor` |
|---|---:|---:|---:|
| Android arm64 | `0x5306BC` | `0x5306EC` | `0x530700` |
| Android armv7 | `0x494C2C` | `0x494C80` | `0x494CA0` |
| iOS arm64 | `0x10023304C` | `0x10023307C` | `0x100233090` |
| iOS armv7 | `0x231C7C` | `0x231CD4` | `0x231CE6` |

28 个方法均已 fresh decompile。D3D setter 的归一伪代码是：

```cpp
Engine *engine = self->primaryEmoteObject->engine;
engine->dirty = true;

setCoord(x, y, duration, power):
    varSetTarget(engine->position, {float(x), float(y)},
                 float(duration), float(power), engine->append)

setScale(scale, duration, power):
    self->userScale = float(scale)
    finalScale = self->baseScale * self->userScale
    varSetTarget(engine->scale, {finalScale},
                 float(duration), float(power), engine->append)

setRot(angle, duration, power):
    angleSetTarget(engine->angle, float(angle),
                   float(duration), float(power), engine->append)

setColor(argb, duration, power):
    unpack four bytes in low-to-high order
    varSetTarget(engine->color, rgba,
                 float(duration), float(power), engine->append)
```

Android arm64 把 color unpack 与 controller 调用内联在 D3D method 中；其余三端
调用独立 helper：Android armv7 `0x55A788`、iOS arm64 `0x1001ADAF4`、iOS
armv7 `0x1AD16A`。这是编译器内联差异，数据流一致。

D3D getter 三组均为常量 leaf，不读取壳层 user scale 或 Engine controller。
这些 setter 也不做 primary null guard；`load/clone` 建立 primary EmoteObject 是
调用前置条件。

## D3D ncbind 调用边界

沿 registrar 创建的 Function dispatch 对象继续追踪其 vtable `FuncCall` 槽，得到：

| 目标 | `setCoord` adapter | `setScale/setRot` adapter | `setColor` adapter |
|---|---:|---:|---:|
| Android arm64 | `0x5441E0` | `0x5445F0` | `0x544AB8` |
| Android armv7 | `0x4A55A0` | `0x4A5868` | `0x4A5C80` |
| iOS arm64 | `0x1002478A8` | `0x100247C44` | `0x100248170` |
| iOS armv7 | `0x248524` | `0x248948` | `0x248F94` |

12 个 adapter body 均已 fresh decompile。四端边界完全一致：

```cpp
if (membername != nullptr) return TJS_E_MEMBERNOTFOUND; // -1001
if (objthis == nullptr) return TJS_E_NATIVECLASSCRASH;  // -1008
if (numparams < required) return TJS_E_BADPARAMCOUNT; // -1004
resolve native instance;
convert exactly the required leading parameters;
invoke stored C++ member pointer;
```

`setCoord` 的 `required==4`；`setScale/setRot/setColor` 的 `required==3`。检查是
小于而非不等于，所以多余参数被忽略。C++ 默认实参不会影响 member-pointer 模板
的 arity；原版脚本层不允许省略 duration 或 power。端口 raw callback 现采用相同
下限并返回 `TJS_E_BADPARAMCOUNT`；null/wrong native receiver 也恢复为
`TJS_E_NATIVECLASSCRASH`，并保持 null receiver 检查先于 count、wrong-type native
解析后于 count 的原始次序。D3D C++ 声明也不再提供会掩盖该边界的默认值。

## 共享 helper 映射和源码级参数顺序

| 目标 | var setter | angle setter | 20B push helper | color helper |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6646E0` | `0x663870` | `0x664870` | D3D 内联 |
| Android armv7 | `0x5542B0` | `0x553AD4` | `0x55433C` | `0x55A788` |
| iOS arm64 | `0x1001A4C44` | `0x1001A4308` | `0x1001A4CDC` | `0x1001ADAF4` |
| iOS armv7 | `0x1A418C` | `0x1A3798` | `0x1A41F4` | `0x1AD16A` |

四端共同的源码级参数顺序是：

```cpp
varSetTarget(controller, values, duration, power, append);
angleSetTarget(controller, angle, duration, power, append);
```

AArch64 未类型化伪代码容易显示成 `controller, values, append, duration, power`：
前两个指针和末尾 bool 使用 `x/w` 整数参数寄存器，三个 float 使用独立的 `s`
寄存器组，Hex-Rays 会按物理寄存器观察顺序拼接未知原型。ARMv7 call site 把
`duration/power` 放入 `r2/r3`、把 `append` 放入栈上，明确给出了真实源码顺序。
把同一原型应用到四端并重新反编译 caller 后，所有调用均恢复为
`duration -> power -> append`。

这个差异不是四个平台使用了不同源函数签名，而是 ABI 与缺失类型共同造成的
反编译展示差异。

## var controller 的状态机与 20 字节边界

共同伪代码：

```cpp
void varSetTarget(Controller *c, const float *values,
                  float duration, float power, bool append) {
    if (duration <= 0.0f) {
        c->queue.clear();
        c->state = 0;
        for (int i = 0; i < c->count; ++i)
            c->currentValue[i] = values[i];
        return;
    }
    if (!append) {
        c->queue.clear();
        c->state = 0;
    }
    pushFixed20B(c->queue, values, c->count, duration, power);
}
```

重要边界：

- immediate 路径不重置 `phase/invDuration/power/target/start`，只清 queue、置
  state 0、复制 current。
- 非 append 的动画路径同样不重置 phase；只清 queue、置 state 0，再 push。
- helper 不 clamp `count`；直属 controller 的已知 count 为 position 2、scale 1、
  color 4。
- push helper 的实际写序是 `word[3]=duration`、`word[4]=power`，然后
  `word[i]=values[i] (0<=i<count)`。
- 所以 count 1/2 保留 word 3 duration；count 4 把 word 3 覆盖为 alpha。
- step 在开始 keyframe 时无条件读取 `word[3]` 计算 `1/word[3]`，并复制前
  `count` 个 word 作为 channel 目标。color 因而同时得到 alpha channel 与
  `invDuration=1/alpha`。
- power 是 float bit pattern，不是整数；push、controller 状态和 `powf` 之间以
  32 位 word 原样传递。

Android 的 libstdc++ deque 以 20 字节元素、每块 25 项组织数据区；Android
arm64 可见 500 字节 block 分配，armv7 走等价的 25 项边界。iOS libc++ 的相同
20 字节元素每块容纳 204 项。deque header、map 指针和块容量不同，但元素 stride、
push 写序、front/pop 语义和 alpha/duration 别名完全一致。

本地结构现显式建模为：

```cpp
struct EmoteVarKeyValue20B {
    float channelAndDuration[4];
    float powCount;
};
static_assert(sizeof(EmoteVarKeyValue20B) == 20);
```

这样保留 native alias，又避免旧 `float channel[3]` 后越界写第四项造成的 C++ UB。

## angle controller 状态机与 12 字节元素

共同伪代码：

```cpp
while (angle < 0.0f) angle += 6.2832f;
while (angle >= 6.2832f) angle -= 6.2832f;

if (!(duration > 0.0f)) {
    queue.clear();
    state = 0;
    currentRad = angle;
} else {
    if (!append) {
        queue.clear();
        state = 0;
    }
    queue.push_back({angle, duration, power});
}
```

这里的“不重置 phase”严格只描述 setter：setter 清 queue/state 或直接入队时不写
phase。后续 state-0 step 真正消费 keyframe 时，四端都会把 phase 清零；宽存储
细节和旧注释修正见
`analysis/motionplayer_angle_controller_lifecycle_four_binary_2026-08-11.md`。
12 字节 keyframe 的第三 word 是 float power；
旧端口把它声明成 `uint32_t` 虽能保存 bits，却错误表达了内部类型。现在字段为
float，并有 12 字节 static assert。

## Engine controller 字段和控制字节

| 目标 | position | scale | color | angle | append | dirty |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `+1072` | `+1080` | `+1088` | `+1096` | `+1161` | `+1162` |
| Android armv7 | `+536` | `+540` | `+544` | `+548` | `+593` | `+594` |
| iOS arm64 | `+704` | `+712` | `+720` | `+728` | `+793` | `+794` |
| iOS armv7 | `+352` | `+356` | `+360` | `+364` | `+409` | `+410` |

四个 setter 都读取同一个 append byte，并写相邻 dirty byte。32/64 位和
libstdc++/libc++ 只改变绝对偏移；字段相对顺序一致。

## 本地逐行比较与修正

- 新增共享 `EmoteVarController_setTarget_guess` 与
  `EmoteAngleController_setTarget_guess`，参数顺序与四端源码级调用一致。
- Motion setters 改为 Engine dirty + controller enqueue；移除 Player 侧直接状态
  写入和 Engine 尾部 `_coordX/_coordY/_rot/_color` 影子。
- D3D setters 改为沿 primary EmoteObject 进入 Engine controller；setScale 保留
  壳层 `baseScale * userScale` 数据流。
- D3D raw callbacks 恢复 ncbind 的精确参数下限：coord 4，其余三项 3；移除
  convenience 默认补参和 D3D C++ 默认实参。
- Motion ease 使用正/负/零三分支转换；D3D member body 传 raw power。
- color 按低字节到高字节展开，并保留 count 4 对 duration word 的覆盖。
- `EmoteVarKeyValue20B` 改为四 word alias + power 的精确 20 字节布局；所有
  producer 统一先写 duration、再写 power、最后只复制 count 个 channel；未覆盖
  word 保持未初始化，并直接在 deque slot 中构造。
- `EmoteAngleKeyValue12B::powCount` 改为 float，仍保留 raw-word copy 语义。
- 删除 Player 侧四组不存在的 animator 状态/API；保留 Engine 的真实 append
  byte `_emoteAnimatorFlag`。

## IDB 回写

四份 IDB 已写入本页映射中的 Motion setter、D3D setter/getter、var/angle helper、
20B push helper、color helper 和 D3D FuncCall adapter `_guess` 名称及 prototype。
ARMv7 先因旧 AArch64 顺序产生了错误的参数绑定，随后已以四端共同的源码级顺序
修正；重新反编译 helper 与 caller 后，`duration/power/append` 均正确。四份
数据库均已原位保存。

## 验证状态

- 新增 controller 直接测试，覆盖 immediate、append、replace、phase 不重置、
  angle normalize、20B alpha/duration alias 和 `invDuration=1/alpha`。
- 新增 Motion setter 测试，覆盖 dirty、四 controller queue、正/负/零 ease 变换、
  color byte unpack 和固定 20 字节边界。
- D3D 测试覆盖 setRot/setColor 后常量 getter 仍返回 0。
- D3D raw-callback 测试覆盖 coord 少于 4 项、其余三 setter 少于 3 项时返回
  `TJS_E_BADPARAMCOUNT`。
- Web Debug 与 Wasmtime Headless Debug 完整增量构建通过；再次构建均为
  `ninja: no work to do`。
- 使用 Web 编译参数和 Catch2 头目录对完整 motionplayer 单元测试翻译单元执行
  Emscripten `-fsyntax-only` 通过，仅出现项目既有 `_tss` 告警。
- `git diff --check` 通过，仅有工作区 LF/CRLF 提醒。
- Windows 原生 Catch2 仍在外部 vcpkg `cocos2dx:x64-windows` overlay 的
  `unzip.cpp` MSVC C2491（`unzSeek64`/`unzEndOfFile`）处阻塞；尚未进入项目测试
  编译阶段，本轮未修改全局 vcpkg 规避。
