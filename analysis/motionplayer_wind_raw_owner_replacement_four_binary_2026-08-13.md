# motionplayer wind raw-owner replacement / failure boundary（四参考，2026-08-13）

本纵切面重新检查四份当前参考二进制中的 `EmoteEngine` 构造、正常析构与
`setWind`，用来判定 `_windEmitter` 究竟是单指针 `unique_ptr`、普通借用 pointer，
还是需要手写管理的 raw owner。结论是第三种：它是 Engine 唯一释放的 raw owner，且
replacement 路径保留了一个 `unique_ptr` 无法自然表达的 allocation-failure 悬空边界。

## 1. 四端入口与 Engine 字段

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| Engine constructor | `0x67B76C` | `0x560948` | `0x1001B7FB0` | `0x1B7788` |
| Engine normal destructor | `0x67C898` | `0x5610E8` | `0x1001B8B4C` | `0x1B814E` |
| Engine `setWind` | `0x66DD8C` | `0x559900` | `0x1001AC718` | `0x1ABF24` |
| wind owner offset | `+1128` | `+564` | `+760` | `+380` |

四端每次都以 `operator new(0x61C)` 分配 emitter。对象布局也是四端相同的
1564 bytes：128 个 12-byte particle slot，之后依次为 `startPos`、`endPos`、
`gate + alignment`、`yHi`、`yLo`、`velocity` 和 `emitAccumulator`。四份 recovery
IDB 均已加入 `EmoteWindParticle12B_guess` 与 `EmoteWindEmitter1564B_guess`，尺寸分别
校验为 12 和 1564 bytes。

## 2. 构造与正常析构

constructor 不分配 emitter，只把 owner slot 和相邻五个 float cache 初始化为零：

- Android arm64 在 direct-controller owner 之后用两个 16-byte zero store 覆盖从
  `+1128` 开始的 owner/cache 区间；
- Android armv7 从 `+564` 开始调用 `memclr(..., 0x1C)`；
- iOS arm64 明确向 `+760` 写入零，并清理随后的 cache；
- iOS armv7 的向量零写覆盖从 `+380` 开始的同一区间。

正常析构的第一阶段在四端一致：读取 owner slot，非空就直接调用 `operator delete`。
`EmoteWindEmitter` 没有非平凡 destructor，因此没有额外对象析构调用。机器码也没有在
Engine 即将死亡时再清 owner slot；随后才销毁 HM7..HM4、Variant、direct controller、
Player、timeline/mirror 容器以及十组 deque。

这证明 emitter 是 Engine owner 而不是 borrow；但仅凭正常析构还不能区分 raw pointer
和被高度内联的 `unique_ptr`。决定性证据来自 replacement。

## 3. stop 与 replacement 是两种不同的 owner 更新协议

四端 stop 分支完全一致：

```cpp
if (shouldStop) {
    if (engine->windEmitter) {
        operator delete(engine->windEmitter);
        engine->windEmitter = nullptr;
    }
    return; // five cached wind parameters are deliberately retained
}
```

但当 emitter 已存在且 normalized angle interval 改变时，四端共同次序是：

```cpp
EmoteWindEmitter *emitter = engine->windEmitter;
if (emitter)
    operator delete(emitter);             // member slot is not cleared

emitter = static_cast<EmoteWindEmitter *>(operator new(0x61C));
initializeEmitter(emitter, ...);
engine->windEmitter = emitter;            // first replacement-slot write
```

也就是说，旧 allocation 已释放到 replacement allocation 成功之间，Engine 字段仍保存
旧地址。若 `operator new` 抛异常，`setWind` 不会清 field；之后 Engine 正常析构或再次进入
stop 分支，都会对同一旧地址再执行一次 `operator delete`。这不是推荐行为，而是四端共同
保留的原始边界。

`std::unique_ptr::reset(new T)` 必须先完成整个 `new` expression 才进入 `reset`，因而
allocation failure 时会保留**仍然存活**的旧对象；先 `reset()` 再 `new` 又必须先把 owner
slot 置空。两种写法都不能生成上述“旧对象已经释放但 slot 仍非空”的中间状态。因此
`_windEmitter` 不能像 Player 和七个 direct controller 一样迁移到 `unique_ptr`。

## 4. 与其余 wind 数据流的关系

raw ownership 不表示所有 wind pointer 都拥有对象：

- `EmoteEngine::_windEmitter` 是唯一 owner；
- chain spring 的 `collisionCurve` 只是逐次刷新得到的 borrow；
- progress 仅在 owner 非空且 `gate != 0` 时调用 emitter step；
- Engine 正常析构先删除 wind，稍后才销毁仍保存 borrow 的 chain spring；spring 析构不会
  解引用该指针，所以参考实现容许这个短暂悬空期；
- stop 会 delete + null owner，但不会改写五个 cache；下一次 non-stop 调用因为 owner 为空
  必然重建 emitter，不会错误复用 cache。

64-bit 与 32-bit 的 stop predicate 仍是已确认的平台宽度差异：两份 64-bit 目标检查
zero amplitude、collapsed interval、两 frequency 同时为零；两份 32-bit 目标只检查 zero
amplitude 或 zero `freqX`。本纵切面没有把该差异误归因于所有权实现。

## 5. 本地恢复与验证范围

本地继续使用 raw pointer，并保留 replacement 的局部 pointer 流程：先 delete 局部旧值，
再分配/初始化，最后写回 member。源码注释现在明确禁止用 `unique_ptr` 或“先清 field”进行
RAII 美化。Engine 正常析构只执行 delete，不再人为写入一个参考机器码不可见、且对象死亡
后没有作用的 null store；stop 分支仍严格 delete + null。

四份 recovery IDB 已写入：

- `EmoteEngine_setWind_guess` 的统一六参数 prototype；
- constructor、normal destructor 与 `setWind` 的 raw-owner/异常边界注释；
- 两个 wind POD 类型及精确字段布局。

绝对地址仅保留在本文的四端映射中；compiled source comment 只描述共同语义。
