# Player chara / motion 四属性（四参考二进制，2026-08-26）

## 1. callback 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| getChara | `0x6D6850` | `0x598DC0` | `0x1001254A4` | `0x1246B8` |
| setChara | `0x6BE27C` | `0x58A484` | `0x1001138D8` | `0x1112E4` |
| getStealthChara | `0x6D6870` | `0x598DE0` | `0x1001254C4` | `0x1246D8` |
| setStealthChara | `0x6D6890` | `0x598E00` | `0x1001254E4` | `0x1246F8` |
| getMotion | `0x6D6924` | `0x598E08` | `0x1001254F4` | `0x124700` |
| setMotion | `0x6BEF00` | `0x58AD8C` | `0x1001143D0` | `0x111E00` |
| getStealthMotion | `0x6D6944` | `0x598E28` | `0x100125514` | `0x124720` |
| setStealthMotion | `0x6D6964` | `0x598E48` | `0x100125534` | `0x124740` |

32 个 property callback 均已 fresh decompile + disassemble。Android armv7 的
`0x598DC0..0x598E00` 原被 IDA 误作相邻函数 interior labels；本轮按两个
`0x20` leaf getter 恢复函数边界后再完成 fresh 反编译。

## 2. 四个 live ttstr owner

| 端 | chara | stealthChara | motion | stealthMotion |
|---|---:|---:|---:|---:|
| Android arm64 | `+0x3C0` | `+0x3C8` | `+0x3D0` | `+0x3D8` |
| Android armv7 | `+0x29C` | `+0x2A0` | `+0x2A4` | `+0x2A8` |
| iOS arm64 | `+0x350` | `+0x358` | `+0x360` | `+0x368` |
| iOS armv7 | `+0x25C` | `+0x260` | `+0x264` | `+0x268` |

四个 getter 都是按值返回 `ttstr`：复制指针并增加字符串 owner refcount；不返回
borrowed `const ttstr&`，不进行 lazy materialization。空 `ttstr` 仍按空 owner
形状返回。

## 3. property setter 到共同 coordinator 的映射

```cpp
setChara(v)         -> setCharaWithFlags(0, v)
setStealthChara(v)  -> setCharaWithFlags(0x10, v)
setMotion(v)        -> play(0, v)
setStealthMotion(v) -> play(0x10, v)
```

参数是 typed NCB 已 materialize 的 by-value `ttstr`。四个 setter 都没有 raw
`objthis` bridge，也没有 setter-specific load state machine。

编译器拆分差异很有信息量：

- Android arm64 把两个 outer coordinator 分别 inline 到四个 property setter，
  chara setter 调 `Player_setCharaLiveSlots`，motion setter 调 `Player_playImpl`；
- Android armv7、iOS arm64、iOS armv7 把 property setter 编译成 3–4 条指令的
  tail thunk，flags 分别为 0/0x10，outer coordinator 保留为独立函数。

这支持本地“public property wrapper → flags coordinator → live implementation”的
三层源代码结构，而不是为四个 property 复制四份状态机。

## 4. pending stealth owner 协议

| 端 | pending motion | pending chara |
|---|---:|---:|
| Android arm64 | `+0x300` | `+0x308` |
| Android armv7 | `+0x1F8` | `+0x1FC` |
| iOS arm64 | `+0x290` | `+0x298` |
| iOS armv7 | `+0x1B8` | `+0x1BC` |

两个 coordinator 共享同一控制形状：

```cpp
if ((flags & 0x10) && stealthChara.empty()) {
    pending = value;                // CopyRef; last-write-wins
    return;
}

liveImpl(flags, value);
if (!pending.empty()) {
    liveImpl(0x10, pending);        // 直接借用 persistent field 本身
    pending.clear();                // 仅在 nested call 正常返回后
}
```

因此 stealth property 在 primary chara 尚未 materialize 时只排队，live getter
仍为空；motion 请求也不会提前访问 ResourceManager。后续 primary 调用先执行
自身请求，再 flush pending。替换 pending 时先 AddRef 新 owner、Release 旧 owner。
若 nested live call 抛出，clear 尚未执行，pending owner 保留；这是刻意的非事务
边界。

## 5. chara live writer

四端 inner helper 的共同源代码是：

```cpp
slot = stealth ? stealthChara : chara;
if (slot == value) return;          // pointer fast path + UTF-16 value equality

stealthChara = value;
if (!stealth) chara = value;
stealthMotion.clear();
motion.clear();
playing = false;
```

相同文字但不同 ttstr owner 是完整 no-op；不会清 motion、不会改 playing。
真实 primary change 同时写两个 chara owner；真实 stealth change 只保留 primary、
更新 stealth。两个 motion label 和 playing 仅在真实 chara change 后清除；已加载的
motion content/context owner 不在此 helper 中释放。

## 6. motion 边界与本地状态

本轮证明 property setter、pending coordinator 与 `playImpl` 入口的 flags/owner/
异常边；`playImpl` 内部的 load、commit、direct-edit initializer 和 failure path
归后续播放状态机切片，不在此报告重复宣称完成。

本地四个 getter、property wrapper、两个 coordinator 与 chara live writer 均与
四端一致。既有单元测试覆盖 pending last-write-wins/live invisibility、primary
flush、value-equality no-op、真实 chara invalidation、typed motion property 不传
objthis，以及 stealth motion 无 ResourceManager 的 queue path。本轮无需改语义
代码。正式工具链不可用，状态为 `EVIDENCED_4_4`。

所有相关 IDB 函数已统一命名/注释并保存；完整 `playImpl` 仍保留为后续明确的
dependency edge。
