# MotionPlayer Selector / Transition 四参考二进制复原（2026-08-11）

## 结论

本轮把 Selector 从“自身状态机已移植，但 Transition 尚未移植、跨控制器效果惰性”的旧结论修正为四参考二进制共同支持的完整链路：

1. 元数据主构建函数总是先构建 `transitionControl`，随后才读取并构建 `selectorControl`。
2. Transition deque 拥有一组单通道 `EmoteVarController`；Selector option 只按 label 借用其中的指针，不接管所有权。
3. 匹配成功时，Selector builder 清除 Transition entry 的直接写入 flag，并从原始变量绑定数组移除该 label。
4. Selector 构造时立即选择 index 0；之后 `setVariable` 的 TYPE 8 分支把 `{selection,duration,fade}` 放入 Selector 的 12 字节命令 deque。
5. Selector step 消费命令、调用 `applySelection`；`applySelection` 先读取 Transition controller 的当前值，再调用全局共享的 `EmoteVarController_setTarget_guess`。参考实现没有 Selector 私有的第二份 setter。
6. Selector 的 option 指针在生命周期上是 borrowed；Transition deque 才是唯一 owner。

因此，原先写在 `EmoteSelectorController.h` 中的 “NOT ported / still-open / every refCtl is null / applySelection inert” 均为从旧 `libkrkr2.so` 分析阶段遗留的失效注释。

2026-08-13 又以四端独立 range destructor、raw-emplace 和 EH 路径闭合了 Transition
entry 自身的单指针 owner 与异常边界；详见
`analysis/motionplayer_transition_entry_owner_emplace_four_binary_2026-08-13.md`。本文以下
状态机结论不变，但 lifecycle 中必须区分 metadata reset 与正常 Engine 析构。

同日又独立闭合 Selector entry、controller 真实 C++ constructor、option move、raw
emplace 与 EH 分层；详见
`analysis/motionplayer_selector_entry_owner_ctor_emplace_four_binary_2026-08-13.md`。特别是
旧端口的“默认构造后调用 free ctor”不能复现 `applySelection(0)` 抛异常时的成员 unwind
与 new-expression allocation 回收，现已恢复为真正 constructor。

## 四平台函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `EmoteVarController_step` | `0x663FD8` | `0x554014` | `0x1001A48C0` | `0x1A3E48` |
| `EmoteVarController_setTarget_guess` | `0x6646E0` | `0x5542B0` | `0x1001A4C44` | `0x1A418C` |
| `EmoteSelectorController_applySelection` | `0x665490` | `0x5549B8` | `0x1001A5514` | `0x1A4B04` |
| `EmoteSelectorController_enqueue_guess` | `0x6655C4` | `0x554AB8` | `0x1001A5640` | `0x1A4C10` |
| `EmoteSelectorController_reset_guess` | `0x665774` | `0x554B68` | `0x1001A56D4` | `0x1A4C7E` |
| `EmoteSelectorController_step` | `0x665850` | `0x554BC4` | `0x1001A5790` | `0x1A4CF6` |
| Selector ctor | `0x66B778` | `0x5583B6` | `0x1001B7DFC` | `0x1B75EC` |
| Transition builder | `0x66A8A4` | `0x557B84` | `0x1001A9C9C` | `0x1A9314` |
| Selector builder | `0x66ACDC` | `0x557E04` | `0x1001AA030` | `0x1A96D8` |
| 元数据主构建函数 | `0x67A8B0` | `0x560020` | `0x1001B4468` | `0x1B3F58` |

两个 libc++ 版本把 Transition entry 的 raw emplace 抽成了独立 helper：iOS ARM64 `0x1001A9F80`，iOS ARMv7 `0x1A963C`。Android ARM64 完全内联；Android ARMv7 普通路径内联，只在 block 边界调用 `0x5678E4`。四端都从 raw-pointer 实参槽复制 controller、构造空 label、写 `flag=1`，且不清 source raw slot。

Selector entry push-back 的平台形态不同：Android ARM64 内联；Android ARMv7 `0x55841C`；iOS ARM64 `0x1001AA9D8`；iOS ARMv7 `0x1AA0C8`。这些 helper 是确认 entry 内部初始化边界的重要证据。

本轮已在四个 IDB 中应用上述函数名和源级 prototype，并保存数据库。

## 元数据调用顺序与数据流

四个平台的元数据主构建函数都出现以下相邻顺序：

```cpp
transitionControl = propGet(metadata, "transitionControl");
EmoteEngine_buildTransitionControl_guess(engine, transitionControl);

if (metadata has "selectorControl") {
    EmoteEngine_buildSelectorControl_guess(engine, selectorControl);
}
```

对应调用位置为：

| 平台 | Transition call | Selector call |
|---|---:|---:|
| Android ARM64 | `0x67AC8C` | `0x67ACCC` |
| Android ARMv7 | `0x56021A` | `0x560240` |
| iOS ARM64 | `0x1001B4720` | `0x1001B4760` |
| iOS ARMv7 | `0x1B426A` | `0x1B42A2` |

完整数据流是：

```text
transitionControl metadata
  -> owning Transition deque entries
  -> scalar EmoteVarController(count=1)
  -> HM6 {type=7, metadataIndex}

selectorControl metadata
  -> option label first-match scan over Transition deque
  -> borrow entry.ctl
  -> clear entry.flag
  -> remove raw variable binding for option label
  -> move option vector into Selector ctor
  -> ctor applySelection(index=0)
  -> owning Selector deque entry
  -> HM6 {type=8, metadataIndex}

setVariable(TYPE 8)
  -> Selector command deque {selection,duration,fade}
  -> Selector step
  -> applySelection
  -> EmoteVarController_step(dt=0)
  -> EmoteVarController_setTarget_guess(append=false)
  -> Transition progress step
  -> HM7[label] scalar output
```

### Builder 的共同伪代码

```cpp
for (int metadataIndex = 0; metadataIndex < transitionCount; ++metadataIndex) {
    element = transitionControl[metadataIndex];
    if (!element.enabled)
        continue;

    ctl = new EmoteVarController;
    EmoteVarController_ctor(ctl, 1);
    transitionDeque.push_back({ctl, emptyLabel, flag = 1});
    transitionDeque.back().label = element.label;
    HM6[transitionDeque.back().label] = {7, metadataIndex};
}

for (int metadataIndex = 0; metadataIndex < selectorCount; ++metadataIndex) {
    element = selectorControl[metadataIndex];
    label = element.label;
    if (!element.enabled) {
        removeVariableLabel(label);
        continue;
    }

    vector<Option> options;
    for (optionMetadata : element.optionList) {
        refCtl = nullptr;
        for (transitionEntry : transitionDeque) {
            if (transitionEntry.label == optionMetadata.label) {
                refCtl = transitionEntry.ctl;       // borrow, not transfer
                transitionEntry.flag = 0;
                removeVariableLabel(optionMetadata.label);
                break;                              // first match only
            }
        }
        options.push_back({refCtl,
                           optionMetadata.offValue,
                           optionMetadata.onValue});
    }

    selector = new EmoteSelectorController(move(options)); // applies index 0
    selectorDeque.emplace_back(selector);            // raw -> unique owner
    selectorDeque.back().label = label;
    HM6[label] = {8, metadataIndex};
}
```

两个 builder 都把 HM6 的 index 写成原始 metadata loop index，而不是 enabled entry 在 deque 中的压缩下标。disabled 元素仍会增加 loop index；这是四份参考实现保留的边界行为，本地没有“修正”为 deque size。

## Selector 控制器源结构

### 共同的源级字段

```cpp
struct SelectorCommandTrack {
    deque<ThreeFloatCommand> queue;
    int32_t baseState;             // Selector step 不读取
};

struct Selector {
    SelectorCommandTrack commandTrack;
    int32_t selectedState;
    int32_t selectedIndex;
    float invDuration;
    float accum;
    // 64 位 ABI 在这里自然插入 4 字节对齐；不是显式源字段
    vector<Option> options;
};
```

命令 element 固定为三个 float：`{selection, duration, fade}`。它和 angle controller 使用相同的 12 字节 POD，但 Selector 只需要 deque 与基础 state，并不包含 angle controller 的 `currentRad/targetRad/startRad/phase` 等标量插值字段。本地原先直接嵌入完整 `EmoteAngleController`，属于结构性过度建模，本轮已拆为 `EmoteSelectorCommandTrack_guess`。

### 四平台对象布局

| 字段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| command deque ABI size | `80` | `40` | `48` | `24` |
| base state / ABI gap | `+80` | `+40` | `+48` | `+24` |
| selector state | `+84` | `+44` | `+52` | `+28` |
| selected index | `+88` | `+48` | `+56` | `+32` |
| inverse duration | `+92` | `+52` | `+60` | `+36` |
| accumulator | `+96` | `+56` | `+64` | `+40` |
| option vector begin/end/cap | `+104/+112/+120` | `+60/+64/+68` | `+72/+80/+88` | `+44/+48/+52` |
| controller allocation size | `0x80` | `0x48` | `0x60` | `0x38` |

这里最关键的源结构推论是：64 位的 `accum` 之后存在 4 字节 ABI padding，32 位没有。因此源代码不能声明一个显式 `pad` 字段；否则 wasm32/iOS ARMv7/Android ARMv7 都会把 option vector 错后移 4 字节。本轮已删除本地显式 `pad`。

### Option 布局

| 平台位宽 | stride | 字段 |
|---|---:|---|
| 64 位 | `16` | pointer `+0`, off `+8`, on `+12` |
| 32 位 | `12` | pointer `+0`, off `+4`, on `+8` |

本地旧名 `SelectorOption16B` 只描述 64 位结果，会误导 wasm32；现改为源义名称 `EmoteSelectorOption_guess`，让自然 ABI 决定 16/12 字节 stride。

## 内部 deque 实现

| 容器 element | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| Selector command（12B） | 42/块，504B | 42/块，504B | 341/块 | 341/块 |
| Transition entry | 24B，21/块 | 12B，42/块 | 24B，170/块 | 12B，341/块 |
| Selector engine entry | 48B，10/块 | 24B，21/块 | 48B，85/块 | 24B，170/块 |

Engine 内的 deque header 起点也随 STL ABI 改变：

| 平台 | Transition deque header | Selector deque header |
|---|---:|---:|
| Android ARM64 | `engine+560` | `engine+640` |
| Android ARMv7 | `engine+280` | `engine+320` |
| iOS ARM64 | `engine+336` | `engine+384` |
| iOS ARMv7 | `engine+168` | `engine+192` |

这些偏移只用于逆向对照，不能硬编码到 Web 源码；本地用 typed `std::deque` 表达相同所有权和 front-pop/back-push 行为。

### Selector engine entry 的未初始化 gate

四个平台的 Selector entry push-back 都明确写：

- controller pointer；
- 空 label，随后 builder 再复制实际 label；
- `targets` vector 的 begin/end/cap 为零。

但它们都不写 controller/label 与 targets 之间的 gate byte：64 位为 entry `+16`，32 位为 `+8`。Android ARM64 内联路径写 qword 0、1、3、4、5，跳过 qword 2；其余三个 helper 也跳过同一逻辑字段。因此这个 TYPE 8 直接入队 gate 是 native raw allocation 遗留的 indeterminate boundary，不应伪造为默认 1，也不应从 Transition entry 复制。

## Selector 状态机

> 2026-08-16 更正：本节原先把 duration gate 写成普通
> `duration <= 0`，并把 selection 转换只概括成“普通 C++ 截断、无 clamp”。四端
> fresh 指令复核证明唯一入队条件是 ordered `duration > 0`，所以 NaN duration 走
> immediate clear/apply；immediate、step 与 reset 三个 selection 消费点均使用
> signed-int32 toward-zero saturation（NaN→0，正/负溢出→`INT_MAX/INT_MIN`）。精确
> 顺序与回归见
> `motionplayer_selector_index_unordered_duration_four_binary_2026-08-16.md`。

### enqueue 的源级签名

四平台共同源顺序是：

```cpp
void enqueue(Selector *self,
             float selection,
             float duration,
             float fade,
             bool append);
```

AArch64 把三个 float 放入 FP 参数寄存器，同时把 `append` 放入第二个整数参数寄存器；未修类型时 Hex-Rays 因而把 bool 显示在 float 之前。两个 ARMv7 版本的落位与局部变量复制清楚证明 bool 是最后一个源参数。本地原 helper 的 `(self, bool, float, float, float)` 已纠正，并从 Engine 私有重复实现移回 Selector 模块。

共同逻辑：

```cpp
if (!(duration > 0)) {
    queue.clear();
    selectorState = 0;
    applySelection((int)selection, 0, 0);
    return;
}
if (!append) {
    queue.clear();
    selectorState = 0;
}
queue.push_back({selection, duration, fade});
```

`selection` 在三个消费点均通过 native signed-int32 toward-zero saturating conversion；
它没有额外的业务范围 clamp。`append=true` 仅在 ordered-positive duration 路径保留旧命令。

### step

```cpp
if (selectorState != 0) {
    if (selectorState == 1) {
        accum += invDuration * dt;
        if (accum >= 1) {
            accum = 1;
            selectorState = 0;
        }
    }
} else if (!queue.empty()) {
    command = queue.front();
    queue.pop_front();
    applySelection((int)command.selection,
                   command.duration,
                   command.fade);
    invDuration = 1 / command.duration;
    ++selectorState;
    accum = 0;
}
*out = (float)selectedIndex;
```

完成当前 ramp 的同一次 step 不会继续消费下一命令，因为 native 是互斥的 `if/else` 结构。有效 enqueue 路径只会放入正 duration；如果外部直接破坏 deque 塞入 0，step 仍会执行原生的 `1/0`。

### applySelection 与共享 setter

四份新鲜反编译的共同伪代码：

```cpp
selectedIndex = index;
for (size_t i = 0; i < options.size(); ++i) {
    Option &option = options[i];
    if (!option.refCtl)
        continue;

    float target = ((int)i == selectedIndex)
                     ? option.onValue
                     : option.offValue;
    float current;
    EmoteVarController_step(option.refCtl, &current, 0);
    float delta = current - target;

    if (option.refCtl->state != 0 ||
        !option.refCtl->queue.empty() ||
        fabs(delta) >= 1.0e-7f) {
        float scaledDuration =
            fabs(delta / (option.onValue - option.offValue)) * duration;
        EmoteVarController_setTarget_guess(
            option.refCtl, &target, scaledDuration, fade, false);
    }
}
```

四份 apply 都调用同一份共享 setter；本地原有的私有 `Animator_setKeyframes` 是重复实现，已删除。

注意 `EmoteVarController_step(..., dt=0)` 在 guard 之前执行。如果 borrowed controller 空闲但已有 queue，它会先开始/弹出当前 keyframe，然后 apply 再根据更新后的 state/queue/current 决定是否替换目标；调用顺序不能简化为直接读取 `currentValue[0]`。

### reset

- command deque 非空：读取并应用 `back().selection`，然后清空整个 deque；不是提交 front。
- deque 为空但 selector state 非零：以 duration/fade 0 重应用当前 `selectedIndex`。
- 两种路径都先把 selector state 置零。
- 已空闲且无命令：完全不调用 `applySelection`。

## Selector 同步与 target API

### 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| sync selector controls | `0x66E0FC` | `0x559A8C` | `0x1001AC8A4` | `0x1AC0D0` |
| is selector target | `0x67F7DC` | `0x562378` | `0x1001B64D0` | `0x1B6394` |
| activate selector target | `0x672BFC` | `0x55B908` | `0x1001AF2F0` | `0x1AEBE4` |
| deactivate selector target | `0x672FD4` | `0x55BAD4` | `0x1001AF628` | `0x1AEE48` |

四份 IDB 中已分别命名为 `EmoteEngine_syncSelectorControls_guess`、`EmoteEngine_isSelectorTarget_guess`、`EmoteEngine_activateSelectorTarget_guess` 和 `EmoteEngine_deactivateSelectorTarget_guess`，应用原型后重新反编译。

### sync 的真实调用链

共同数据流为：

1. 创建新的 script Array，令公开 variable keys 引用它；
2. 把当前 raw variable-label Array 的 `Items` 内容复制到新 Array；
3. 设置 Engine dirty；
4. 对每个 Selector entry，把 entry gate 写成全局 `selectorEnabled`；
5. enabled 时清空 Selector command deque、令 selector state 为 0，并立即 `applySelection(0,0,0)`；
6. disabled 时对公开 keys 执行 `std::remove(label)`，但故意忽略返回的新 end，不 erase/shrink；
7. 对 `entry.targets`：enabled 时从 raw labels 删除 target label，disabled 时通过共享 `EmoteVarController_setTarget_guess(targetCtl,&zero,0,0,false)` 归零。

Android ARMv7、iOS ARM64、iOS ARMv7 都保留对共享 setter 的显式调用；Android ARM64 把同一路径展开得更深。本地原实现直接 `queue.clear/state=0/fill_n(currentValue,0)`，结果对正常单通道 target 接近，但丢掉了源代码级共享 setter 调用链及其完整字段写入/边界行为，现已改回共享 setter。

### `targets` 是休眠容器，不是 option 的别名

四端 Selector entry push 都把 targets 的 begin/end/cap 清零；option builder 只把匹配 Transition 的 **controller pointer** 写入 `Selector::optionList[].refCtl`，从不把 Transition entry pointer 写入 `entry.targets`。

进一步对完整 motionplayer 函数区做了字段访问集合核对：

- ARMv7 唯一取得 Selector deque 对象基址的代码只有构造、析构/清空、metadata builder；唯一 entry push helper `0x55841C` 也只有 builder `0x557E04` 一个调用点；
- 其余取得 Selector begin/end 的函数仅是 reset、sync、timeline set-variable、getAnimating、progress、serialize、restore，以及三个 target API，全部只读 entries/targets；
- ARM64/iOS 的对应访问集合相同；三个非内联 entry push helper也都只有各自 builder 一个 xref；
- builder push 后没有把 entry 地址交给任何会保留别名的对象，HM6 只接收 label 与 `{type,index}`。

因此在正常 native 对象生命周期里 targets 永远为空。其可观察结果是：

- `isSelectorTarget(label)` 仍会先把每个 Selector entry gate 同步成全局 `selectorEnabled`，但总是返回 false；
- activate/deactivate 的内层循环从不进入，正常情况下完全无操作；
- 若通过内存破坏或插件外部私有 ABI 注入非空 targets，activate/deactivate 才会按 targets 下标选择 option、分别写 gate 0/1、以 dt=0 重算全部 Selector/Transition HM7，并在首个 target 命中后立即 return；
- 不能为了让 API “有用”而把 `optionList` 自动镜像到 targets，这会制造参考插件不存在的关联与行为。

## Selector 保存/恢复

### 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| serialize selector array | `0x674CD0` | `0x55CAD0` | `0x1001B0B6C` | `0x1B04A0` |
| restore selector array | `0x677C48` | `0x55E578` | `0x1001B2218` | `0x1B1CD4` |
| restore one controller | `0x665950` | `0x554C68` | `0x1001A588C` | `0x1A4DD0` |

对应命名为 `EmoteEngine_serializeSelectorState_guess`、`EmoteEngine_restoreSelectorState_guess`、`EmoteSelectorController_restoreState_guess`。serialize 的 AArch64 隐藏返回对象走 X8，ARMv7 走常规 sret，因此只改名而没有用错误的普通双参数原型覆盖 ABI；另外两类函数应用了双指针原型并重新反编译。

每个 Selector 保存项严格只有五个属性，写入顺序也是表中顺序：

| 属性 | native 字段 | 类型 |
|---|---|---|
| `label` | engine entry label | `ttstr` |
| `value` | `selectedIndex` | integer |
| `phase` | `selState` | integer |
| `speed` | `invDuration` | float/real |
| `tick` | `accum` | float/real |

命令 deque、command-track base state、option vector、entry gate 和 dormant targets 都不保存。恢复也只按上述四个 controller 字段逐项“存在则覆盖”，不会清命令、不会应用 selection、不会驱动 borrowed Transition controller、不会恢复 entry gate。

restore 的 label 路径有一个四端一致的重要越界行为：它先跳过非 object item 和缺少 `label` 的 item；有 label 后对 Selector deque 做 first-match 线性搜索，但 **不检查搜索结果是否等于 end**，直接解引用并调用单-controller restore。因此重复 label 恢复第一项；未知 label 进入 end-iterator 未定义行为，而不是静默跳过。本地此前多出的 `if(found != end)` 已删除。

## 边界行为

1. `selectedIndex` 不验证上下界。负数或大于 option count 的 index 会使全部非空 option 走 offValue，同时对外仍输出该原始 index 的 float。
2. option label 使用 first-match 线性扫描；重复 Transition label 只借用第一项并立即 break。
3. 找不到 Transition label 时 refCtl 保持 null；apply 对该 option 静默跳过。
4. 比较阈值严格为 `fabs(delta) >= 1e-7f`；恰好等于阈值会写入。
5. `onValue == offValue` 没有特判：
   - 非零 delta 会产生无穷比例；
   - 再乘 0 duration 可产生 NaN；
   - shared setter 的 `duration <= 0` 对 NaN 为 false，因此可能排入 NaN duration keyframe。
6. Constructor 总会 `applySelection(0,0,0)`。空 option vector 仅把 selectedIndex 设为 0；不会访问不存在的元素。
7. Transition/Selector builder 的 HM6 index 保留 metadata index，即使前面存在 disabled 元素；不做 deque 压缩修复。
8. TYPE 8 Selector entry gate 保持未初始化，这是四平台共同的原始边界，而非应当美化的默认值。
9. 正常生命周期中 Selector entry targets 永远为空；target API 是注册且可调用、但匹配路径不可达的休眠表面。
10. Selector restore 对缺少 label 的 item 安全跳过，对存在但未知的 label 无 end guard，会触发 native 未定义行为。

## 对象生命周期

- Transition deque entry 拥有其 `EmoteVarController` 和 label。
- Selector deque entry 拥有其 `EmoteSelectorController`、label 与 targets vector。
- Selector controller 拥有 command deque 与 option vector buffer。
- `Option::refCtl` 和 Selector entry `targets[]` 均是 non-owning pointer。
- Selector ctor 通过 vector move/swap 接管 builder 临时 option buffer，并把源 vector 留空。
- metadata reset 在四端都按声明正序先 clear Transition #8、再 clear Selector #9；这会让 `option.refCtl` 短暂悬空，但 Selector destructor 只释放自身容器，从不解引用或 delete borrowed pointer。
- 正常 Engine destructor 则按声明逆序先销毁 Selector #9、再销毁 Transition #8；borrower 先于 owner 死亡，不存在上述悬空窗口。旧版本文把 reset 顺序误写成正常 teardown 顺序，现已纠正。
- Selector entry 的具体逆成员析构是 `targets -> label -> controller`；controller 再按
  `option vector -> command deque` 清理。真实 constructor 的 `applySelection(0)` 若抛出，
  同一成员逆序先执行，随后由外围 new-expression 回收 controller allocation。

## 本地修正

本轮源码变更：

- 删除 Selector 私有重复 `Animator_setKeyframes`，改调共享 `EmoteVarController_setTarget_guess`；
- `SelectorOption16B` 改为 `EmoteSelectorOption_guess`；
- Selector command track 从完整 `EmoteAngleController` 改为 deque + base state；
- 删除只属于 64 位 ABI padding 的显式 `pad` 字段；
- 把 Selector enqueue 从 Engine 私有 helper 移入 `EmoteSelectorController` 模块；
- 修正 enqueue 源参数顺序；
- reset 统一命名为 `EmoteSelectorController_reset_guess`；
- sync 的 target 归零路径改回共享 `EmoteVarController_setTarget_guess`；
- 明确 targets 没有 writer，保留三个 target API 的正常 inert 行为；
- Selector 保存/恢复 helper 改用四端统一命名，并删除未知 label 的额外 end guard；
- Transition entry 改为单指针 `unique_ptr` owner，builder 直接从 raw pointer emplace；删除 reset/dtor 的显式 delete loop，并保留 ctor-failure 回收、grow-failure 泄漏与 post-emplace 不回滚边界；
- Selector entry 也由其独立四端证据改为单指针 `unique_ptr` owner；builder 直接 raw
  emplace，gate 继续不初始化，targets 继续为空 borrowed vector；
- Selector 从“默认构造 + free ctor”改为真正 C++ constructor，以恢复 option/command
  member unwind、new-expression ctor-failure delete、grow-failure 泄漏与 post-emplace
  不回滚的三段异常边界；
- 清除 Selector/Transition 相关旧 `libkrkr2.so` 地址和“尚未移植/惰性”注释；
- 新增借用控制器、共享 setter、duration 缩放、append/replace、reset-last-command、五字段保存/恢复与 dormant-target 单元覆盖。

## 验证记录

- 四平台 apply/enqueue/reset/step/ctor/builder/metadata dispatcher 均在应用 prototype 后重新反编译。
- 四平台 Transition-before-Selector 构建顺序逐一核对。
- 三个独立 Selector entry push helper与 Android ARM64 内联路径逐一核对，确认 gate 未初始化、targets 三指针清零。
- 四平台 sync/is/activate/deactivate、Selector serialize/restore 和单-controller restore 均在命名/应用可验证原型后重新反编译；serialize 保留各 ABI 的隐藏返回约定。
- 对 Android ARMv7/ARM64 的完整 motionplayer 字段访问集合及三个独立 push-helper xref 做了无 writer 审计，iOS 以同构 builder/API/serialize/restore 集合交叉确认。
- 四个 IDB 已在新增名称和原型后再次保存成功。
- `Web Debug Build` 已在本轮最终源码上通过；只出现仓库既有 warning。
- `Wasmtime Headless Debug Build` 已在本轮最终源码上通过，普通 motionplayer 与 guest object 两条路径都重新编译了 Engine/Selector。
- 独立 Emscripten/Node smoke 已实际运行并通过：构造初始选择、借用 Transition、共享 setter 的缩放 duration、append 两条命令、reset 提交末项均符合预期；临时产物已清理。
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 已在新增五字段持久化/dormant-target 覆盖后用 Emscripten `-fsyntax-only` 通过；仅有仓库既有 `_tss` warning，临时 `test_config.h` 已清理。
