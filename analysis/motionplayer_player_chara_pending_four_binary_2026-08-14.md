# MotionPlayer `Player::chara` / `stealthChara` 四端状态机（2026-08-14）

## 结论

四份当前参考二进制共同恢复出两层实现：一个只修改 live 槽的内层 writer，以及一个
协调 `pendingStealthChara` 的外层入口。源码级语义可写成：

```cpp
void Player::setCharaLiveSlots_guess(tjs_int flags,
                                     const ttstr &value) {
    const bool stealth = (flags & PlayFlagStealth) != 0;
    const ttstr &comparison = stealth ? stealthChara : chara;
    if(comparison == value) return;

    stealthChara = value;
    if(!stealth) chara = value;
    stealthMotion.Clear();
    motion.Clear();
    playing = false;
}

void Player::setCharaWithFlags_guess(tjs_int flags,
                                     const ttstr &value) {
    const bool stealth = (flags & PlayFlagStealth) != 0;
    if(stealth && stealthChara.IsEmpty()) {
        pendingStealthChara = value;
        return;
    }

    setCharaLiveSlots_guess(flags, value);
    if(!pendingStealthChara.IsEmpty()) {
        setCharaLiveSlots_guess(PlayFlagStealth,
                                pendingStealthChara);
        pendingStealthChara.Clear();
    }
}
```

这里的 `IsEmpty()` 是 `ttstr` owner 指针为空，不是运行时另做一次字符串长度判断。
Kirikiri 的普通空字符串分配也折叠为 null owner，因此脚本层常见的 `""` 仍进入该路径。

## 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| live-slot writer | `0x6AFDA0` | `0x580554` | `0x100107A2C` | `0x105098` |
| pending coordinator | `0x6AFEC8` | `0x5805FC` | `0x100107AFC` | `0x105140` |
| typed `chara` setter | `0x6BE27C` | `0x58A484` | `0x1001138D8` | `0x1112E4` |
| typed `chara` getter | `0x6D6850` | `0x598DC0` | `0x1001254A4` | `0x1246B8` |
| typed `stealthChara` getter | `0x6D6870` | `0x598DE0` | `0x1001254C4` | `0x1246D8` |
| typed `stealthChara` setter | `0x6D6890` | `0x598E00` | `0x1001254E4` | `0x1246F8` |
| Player NCB member registration | `0x6D3DA8` | `0x597EC8` | `0x1001244F8` | `0x123848` |

Android armv7 的两个 getter 原先只是 registration table 引用到的代码块。本轮在 recovery
IDB 中分别以 `[0x598DC0,0x598DE0)` 和 `[0x598DE0,0x598E00)` 建立函数边界。

Android armv7、iOS arm64 与 iOS armv7 的两个 setter 是共享 coordinator 的薄转发：
primary 传 flags 0，stealth 传 flags `0x10`。Android arm64 则把相同 coordinator 行为分别
内联/克隆到两个属性 setter 中；`0x6AFEC8` 仍保留共同逻辑形态。这个差别是优化与代码生成
差别，不是脚本协议或状态机差别。

## Player 字段布局证据

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `pendingStealthChara` | `+776` | `+508` | `+664` | `+444` |
| live primary `chara` | `+960` | `+668` | `+848` | `+604` |
| live `stealthChara` | `+968` | `+672` | `+856` | `+608` |
| primary motion label | `+976` | `+676` | `+864` | `+612` |
| stealth motion label | `+984` | `+680` | `+872` | `+616` |
| playing byte | `+1099` | `+751` | `+987` | `+687` |

64 位 `ttstr` 槽为 8 字节，32 位槽为 4 字节。getter 只 CopyRef/retain 相应 live owner；它们
不回退读取 pending 槽，所以 queued stealth 值在 materialize 之前对脚本不可见。

## live writer 的精确顺序

### 1. 比较槽由 Stealth 位选择

- flags 含 `0x10`：只以 live `stealthChara` 与新值比较；
- flags 不含 `0x10`：只以 live primary `chara` 与新值比较。

四端都实现 `ttstr::operator==` 的源级值语义：owner 相同立即相等；一方 owner 为空则
不等；否则比较长度，再比较 UTF-16 内容。Android arm64 将该逻辑展开为 owner/长度/宽字符
比较，其他目标部分保留 helper 调用。不同 owner 但内容相同仍是完全 no-op。

### 2. 赋值顺序固定

真正变化时先 CopyRef/retain 新值、再 release 原值；live `stealthChara` 永远先写。非
Stealth 分支随后再给 primary `chara` 独立 CopyRef 一次。不能把 primary 路径改写成一次
move 或把两个槽合并为共享的非 owning 指针。

### 3. 无效化范围固定

角色发生真实变化后按顺序：

1. release/null stealth motion label；
2. release/null primary motion label；
3. 写 `playing=false`。

它不清当前 motion content Variant，也不清 find-motion context Variant。因而已加载对象仍被
Player 持有，只是两个 live label 与播放标记被失效；下一次 `play` 会按新角色重新查找并提交。

同值路径在任何写入之前返回，所以 live chara owner、两个标签、playing、content/context
全部保持不变。

## pending coordinator 的精确边界

### Stealth-first queue

当且仅当本次带 Stealth 位且 live `stealthChara` owner 为空时，入口把新值 copy-assign 到
持久 `pendingStealthChara` 后立即返回。连续写入为 last-write-wins：后一个 owner retain 后
释放前一个 owner。这里不是 move，也不发布到 live getter。

### materialize 与 flush

primary 写入不会 queue。它先经 live writer 把 incoming 值发布到 primary 与 stealth 两个
live 槽，然后检查 pending。若 pending 非空，第二次以 `Stealth` 调用 live writer，把 pending
覆盖到 live stealth 槽；最后才 release/null pending。

第二次调用的字符串实参是持久 pending 字段本身的直接引用，没有先 CopyRef 成局部量。这使
pending owner 覆盖整个 nested call；只有调用正常返回后才清字段。实现不应通过提前 move/
clear 改变这一 owner 窗口。虽然当前 live writer 没有显式脚本回调，保留该顺序仍是精确的
对象生命周期要求。

一旦 live stealth 槽已经存在，后续 stealth setter 直接更新 live stealth 槽，不再碰 primary
chara，也不会创建 pending。

## NCB 属性 ABI

四端 registration table 均把 `chara`、`stealthChara` 注册为普通 typed property。setter
接收 NCB 生成的按值 `ttstr`，再借用该局部给 flags-based native helper；getter CopyRef 返回
live `ttstr`。这两条 setter 都不：

- 保存 `objthis` 到 Player；
- 暴露 `_currentDispatch`；
- 调用 `onFindMotion`；
- 改写 motion content/context。

`onFindMotion` 只属于显式 `play` wrapper 建立 raw current-dispatch 后的 load 数据流，不能把
旧 motion-property 补偿逻辑移植到 chara setter。

## 旧单目标证据失效

本纵切面重新检查后，旧源码/注释使用过的地址不能描述当前四个参考目标：

- `0x6B29C0` 位于当前 Android arm64 的 node-tree builder 内部；
- `0x6C0E9C` 位于 prepared render-item append 逻辑内部；
- `0x6D94B0` 位于 `PrivateMotionGLL` shader 相关函数内部。

因此旧 helper 名 `setCharaSlotLike_0x6B29C0` 已删除，替换为仅表达四端共同语义的
`setCharaLiveSlots_guess` / `setCharaWithFlags_guess`。绝对地址只保留在本证据文档与 recovery
IDB 中，不进入可编译源码注释。

## 本地修正与回归覆盖

本地实现已恢复：

- live writer 的值相等完整 no-op；
- primary 同时写两个 live chara 槽，stealth 只写 stealth live 槽；
- 真实变化只清两个 motion label 与 playing，保留 content/context；
- stealth-first copy queue、last-write-wins 与 getter 不可见性；
- primary 发布后以 pending 字段直接引用进行 nested Stealth flush，再清 pending；
- typed `setChara(ttstr)` / `setStealthChara(ttstr)` 的 flags 转发。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 覆盖：

- 两次 stealth-first 写入只 materialize 最后一个值；
- primary 写入后 primary/stealth live 槽的分流，以及后续 live stealth 独立更新；
- 不同 owner、相同 UTF-16 内容的 primary 与 stealth 赋值均保持 label/playing；
- 真正的 primary/stealth 变化清两个 label 与 playing；
- chara 变化后 motion content owner 仍存在。

## IDB 改进

四份 recovery IDB 均已：

- 命名 live writer、pending coordinator、两个 typed getter 与两个 typed setter；
- 给 writer/coordinator/setter 应用 `void` native 类型；
- 注明值比较、赋值/无效化顺序、pending copy/direct-reference/释放窗口；
- 注明 getter 只返回 live 槽；
- 保存到各自 recovery IDB。

## 验证

- `Web Debug Build` 完整目标构建成功；
- 聚合 `motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten defines/includes/ABI 参数执行
  `-fsyntax-only` 成功；唯一诊断是仓库既有 `_tss` literal-operator 弃用 warning；
- `git diff --check` 通过；仅有工作树既有 LF/CRLF 提示。
