# ResourceManager::random 持久 TJS dispatch 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四端 `ResourceManager::random` 都不持有或调用 native C++ PRNG。构造器执行脚本表达式
`new Math.RandomGenerator()` 并把结果保存在一个持久 `tTJSVariant`；每次 random 调用直接借用该
Variant 内的 dispatch，以同一指针同时作为 object 和 objthis，零参数调用宽字符串成员
`random`，忽略 TJS status，再把局部 result Variant按 `AsReal()` 规则转换成 double。

本地 `ResourceManager.cpp` 已逐行匹配 receiver、member hint、result owner、status忽略和转换/析构
顺序，本轮无需修改运行时 C++。本轮补齐独立四端 body 报告、宽字符串边界、EH cleanup、IDB
符号和 NCB body 状态。

## 2. 四端函数映射

| 平台 | callback | 完整指令 | cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6A894C` | 73 | 主函数尾部 landing `0x6A8A60` |
| Android armv7 | `0x57C1CC` | 48 | 无独立 landing body |
| iOS arm64 | `0x100102C90` | 37 | 无独立 landing body |
| iOS armv7 | `0x1000F0` | 72 | SjLj dispatcher `0x1001B6`，12 条 |

四个 callback 均在本轮 fresh decompile并完整读取 disassembly；iOS armv7 cleanup也完整
decompile/disasm。Android arm64 landing与armv7 SjLj都在异常时析构已经构造的 result Variant后
resume；cleanup自身异常进入运行库terminate/abort边。

## 3. `random` 字符串证据

IDA普通字符串列表只覆盖大量ASCII/UTF-8 `random` 子串，不能标识这条TJS宽字符串调用。按
全编码流程补做UTF-8、UTF-16LE和UTF-32LE原始字节搜索，所有cursor均完成；目标callback引用的
精确UTF-16LE命中为：

| 平台 | UTF-16LE地址 | 原始边界 |
|---|---:|---|
| Android arm64 | `0x14CC24C` | `72 00 61 00 6E 00 64 00 6F 00 6D 00 00 00` |
| Android armv7 | `0xD7E5A0` | 同上 |
| iOS arm64 | `0x10195BE96` | 同上 |
| iOS armv7 | `0x174E1FA` | 同上 |

每处都读取前后字节，确认前一个宽字符串的终止边界、六个UTF-16 code unit和终止符。iOS两端
原先只显示`"r"`，本轮将四端字面量类型修正为`unsigned short[7]`并重命名为
`u_random_method_name`；重新反编译后refs已指向正确symbol，即使Hex-Rays字符串渲染仍可能保留
旧cache中的单字符文本。

四端独立member-hint word为：Android arm64 `0x1AB5300`、Android armv7 `0x11117F8`、
iOS arm64 `0x101B697C8`、iOS armv7 `0x187D4C8`。最后一处原本被IDA错误渲染为相邻Bezier
cache base加offset；本轮建立/命名`randomMemberHint_guess`边界。该hint是可变缓存word，不是
随机数状态。

## 4. 共同源码伪代码

```text
double ResourceManager::random():
    generator = randomGenerator.AsObjectNoAddRef()
    result = Variant(Void)
    status = generator.FuncCall(
        flags=0,
        memberName=L"random",
        memberHint=&randomMemberHint,
        result=&result,
        numparams=0,
        params=null,
        objthis=generator)
    ignore(status)
    value = result.AsReal()
    destroy result
    return value
```

### 4.1 receiver 与调用参数

- receiver从持久 `_randomGenerator` 直接借用，不CopyRef、不AddRef，也不构造closure副本。
- Variant不是object时走TJS object conversion/throw helper；不会返回null后安全fallback。反编译中
  helper后的零寄存器是noreturn/exception路径的优化残影，不是源码null容错。
- object和objthis是同一dispatch。
- flags为0；成员hint是上述进程全局可变word；result指向栈上Void Variant。
- numparams严格为0，params严格为null；没有隐藏seed、frame或Player参数。

### 4.2 status、结果转换与异常

- 四端都不分支检查FuncCall返回status。Android arm64在返回值上调用一个`nullsub`，不改变控制流。
- 调用失败而result仍为Void时，`AsReal()`得到`0.0`；没有重试、native fallback或NaN sentinel。
- Integer/Real/String/Object等其他result类型完全交给TJS Variant的Real转换语义；函数不自己
  clamp、normalize或限制`[0,1)`。
- FuncCall或result转换异常直接传播。result一旦构造就由landing/SjLj/普通RAII析构；没有catch和
  fallback return。
- 函数不修改RandomGenerator Variant、ResourceManager set/module map或任何Player字段。

### 4.3 并发和重入边界

member hint与脚本RandomGenerator对象都是共享可变状态，函数无锁。并发调用是否安全完全取决于
TJS dispatch/脚本对象；本层不提供mutex、thread-local PRNG或原子序列。借用receiver也意味着本层
不为异常/重入额外延长对象寿命。

## 5. 平台差异

- Android arm64将`AsReal()`按Variant tag展开为switch；Android armv7和iOS调用公共转换helper。
- 64位返回经浮点寄存器，32位返回经double寄存器对/整数寄存器对；IDA的`long double`/`__int64`
  原型是ABI恢复噪声，共同源码返回普通double。
- Android arm64和iOS armv7显式保留异常cleanup；另两端没有独立可见landing。普通owner顺序一致，
  不能据此推导两套算法。
- iOS两端的wide literal/hint最容易被IDA并入单字符或Bezier cache符号；原始字节和xref消除了该
  展示差异。

## 6. 本地逐行对照

`cpp/plugins/motionplayer/ResourceManager.cpp::random` 当前为：

```cpp
auto *generator = _randomGenerator.AsObjectNoAddRef();
tTJSVariant result;
(void)generator->FuncCall(
    0, TJS_W("random"), &motion::detail::randomMemberHint_guess,
    &result, 0, nullptr, generator);
return result.AsReal();
```

逐行对应共同伪代码：borrow receiver、先完成object conversion、再构造Void result、同一objthis、
零参数、status显式丢弃、AsReal、作用域尾析构。无需添加null检查、CopyRef、native RNG或错误码gate。

## 7. 验证边界

- 四端fresh body/cleanup、全编码字符串搜索、边界读取和IDB类型/符号改善均完成。
- NCB生成器已通过`py_compile`；正式输出和独立临时目录重生成逐字节一致；主台账/原生证据
  分别严格为18/12列，coverage所有非空行严格为12列，`git diff --check`通过。`random`已把
  全局pending/implemented从`108/22`推进到`107/23`。
- 当前环境缺少CMake/Ninja/Emscripten及完整依赖头，不能宣称正式unit/Web build。
