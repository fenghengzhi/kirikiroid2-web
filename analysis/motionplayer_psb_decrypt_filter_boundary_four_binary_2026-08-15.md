# MotionPlayer PSB decrypt filter 数据流与边界四参考复核（2026-08-15）

## 结论

本轮从四份当前 1.3.9 recovery IDB 重新反编译 `setEmotePSBDecryptSeed`、
`setEmotePSBDecryptFunc`、两种 filter invoker 和 process-wide replacement。四端共同证明
当前 portable 数据流、closure owner 和故意保留的 accessor 引用泄漏均与参考一致；
源码算法无需修改。实际需要纠正的是旧叙述中的两个问题：

1. 编译源码和测试名仍夹带绝对地址或“Android callback shape”，容易把四端共同结构
   误读成旧单库证据；这些说明现已迁成纯语义描述。
2. seed filter 不是无条件每四个 byte 才生成一次 word。`remaining == 0` 是逐 byte 检查的
   refill sentinel；若生成 word 的尚未消费高位后缀为零，下一 word 会提前生成。

## 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| seed setter | `0x683110` | `0x564EC0` | `0x1001B8D68` | `0x1B83AC` |
| seed invoker | `0x6837AC` | `0x56522E` | `0x1001B92E8` | `0x1B8992` |
| function setter | `0x683240` | `0x564F58` | `0x1001B8E50` | `0x1B84D0` |
| function invoker | `0x683994` | `0x5652C0` | `0x1001B94A8` | `0x1B8AB0` |
| replacement wrapper | `0x6A5BB0` | `0x57B174` | `0x1001010B0` | `0xFE1E0` |
| iOS shared replacement body | — | — | `0x1001010C4` | `0xFE1F0` |

四份文件均已 stripped，写回 IDB 的职责名继续使用 `_guess` 后缀，不声称恢复作者符号。

## Seed setter 与逐 byte stream

四端 setter 的共同入口边界：

```cpp
if (count < 1)
    return TJS_E_BADPARAMCOUNT;
tjs_int64 captured = ordinaryTJSIntegerConversion(*p[0]);
installFilter(captured);
return TJS_S_OK;
```

只有 `p[0]` 被读取，额外参数完全忽略。capture 保存完整 64 位 Integer；invoker 只把低
32 位作为第四个 xorshift state word。stream 的源级共同结构为：

```cpp
uint32_t x = 123456789;
uint32_t y = 362436069;
uint32_t z = 521288629;
uint32_t w = uint32_t(capturedSeed);
uint32_t remaining = 0;

for (byte : [encryptData, chunkOffsets)) {
    if (remaining == 0) {
        uint32_t t = x ^ (x << 11);
        x = y;
        y = z;
        z = w;
        w = w ^ (w >> 19) ^ t ^ (t >> 8);
        remaining = w;
    }
    byte ^= uint8_t(remaining);
    remaining >>= 8;
}
```

这里 `remaining == 0` 同时承担“尚无 word”和“剩余高位后缀全零”的 sentinel。普通非零
word 最多供四个 byte；例如右移两次后剩余 16 位恰为零，第三个 byte 前就会 advance，
而不是继续输出两个固定零 byte。portable 的逐 byte gate 已经保留这一边界。

四端都只处理正的 32 位有效长度。零/负长度直接返回，不接触 buffer。路径没有空 owner、
反向 header 指针或越界 header 的防御检查；这些由上游 PSB owner 形状约束。

## Function closure owner

function setter 同样只要求 `count >= 1`，只转换 `p[0]`。Integer 等非 Object 输入在 closure
转换处抛异常，不返回错误码。成功路径 CopyRef 两个 dispatch 指针：

```text
EmotePSBDecryptClosure
  Object  -- owning AddRef
  ObjThis -- owning AddRef（非空时）
```

64 位 closure payload 为两个 8-byte pointer，32 位为两个 4-byte pointer。随后 payload
进入一个引用计数 control block；`std::function` target 只捕获该 control-block pointer。
临时 target、全局 target 和 setter 局部 holder 的增减计数次序在 ABI 上不同，但四端
共同所有权结果相同：全局 filter 是最后一个长寿 owner，替换或进程静态析构时释放
Object/ObjThis。

## Function invoker 参数、调用与泄漏

四端 invoker 的共同数据流：

```cpp
auto *accessor = new CBinaryAccessor(owner.data, owner.size);
tTJSVariant accessorValue(accessor); // AddRef
tTJSVariant sizeValue(tjs_int64(owner.size));
tTJSVariant *params[] = {&accessorValue, &sizeValue};
closure.FuncCall(/*name=*/nullptr, /*hint=*/nullptr,
                 /*result=*/nullptr, 2, params, closure.ObjThis);
// sizeValue then accessorValue destruct
```

调用结果完全忽略；脚本异常继续向上传播，局部 Variant 仍按展开路径析构。若 closure 的
ObjThis 非空，它就是调用 context；不是传入 null context。参数顺序固定为 accessor 在前、
size 在后。

`new CBinaryAccessor` 产生初始引用，构造 Object Variant 又 AddRef；四端在 Variant 析构前后
都没有与初始引用对应的独立 Release。Variant 析构只减掉自己的一份，因此 accessor 在每次
function-filter 调用后保留一份泄漏引用。portable 刻意不增加 `accessor->Release()`，以保持
该 shipped lifetime boundary。

## Process-wide replacement

Android 两端在 wrapper 内展开 replacement；iOS 两端 wrapper 转调共享 body。共同顺序：

```text
copy-construct incoming target into temporary
swap temporary with process-global std::function
destroy temporary (therefore destroy the former global target)
return global target address / wrapper result
```

这保证新 target 的复制若抛异常，旧 global target 仍保持不变；复制成功后旧 target 在
replacement 返回前销毁。seed/function 两条 setter 共用同一 global slot，故安装 seed 会
同步释放此前 function closure 的 Object/ObjThis owner，反向替换也相同。

### 2026-08-17 V187：内部容器、静态生命周期与并发边界补证

从 `commandKeyMemberHint_guess` 的物理后继重新追踪后，四端已把该 process-global slot
闭合为一个 typed `std::function<void(PSBRawOwner &)>` object，而不是另一个 hint：Android
libstdc++ 的 arm64/armv7 object 分别为 32/16 B，布局为 erased buffer + manager + invoker；
iOS libc++ 分别为 32/20 B，布局为 inline storage + active-target pointer。libc++ pointer
用 null/self/external 区分 empty/inline/heap target。

四端 static initializer 都只清空 empty discriminator 并登记 `atexit` dtor；最后安装的
target 跨所有 ResourceManager instance 存活到下一次 replacement 或 process teardown。
`ResourceManager::load` 直接把 global lvalue 传给 PSB loader，不制作 snapshot，也没有锁。
因此 setter/load 并发是 shipped C++ data race，不能在 portable 层擅自同步。完整地址、xref、
copy/swap/destroy helper 与 IDB typed readback 见
`motionplayer_psb_owner_filter_std_function_layout_static_lifetime_four_binary_2026-08-17.md`。

## 本地落点与验证范围

- `ResourceManager.cpp` 删除 setter/invoker 的四端绝对地址编译注释，保留语义、owner、
  sentinel 与泄漏边界；
- 单元测试名从 Android-only 迁为 four-reference，测试仍覆盖少参数、额外参数忽略、
  普通 Integer 转换、非法 closure 抛异常、ObjThis、参数顺序/size、global replacement
  释放旧 closure；
- `psbfile_four_binary_audit_2026-08-10.md` 同步纠正 fixed-four-byte 的过度概括；
- recovery IDB 写入 setter、invoker、replacement 的 `_guess` 名称与边界注释并保存。
