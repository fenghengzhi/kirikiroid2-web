---
name: tjs2-disasm
description: 将 KiriKiri2 游戏归档中的已编译 TJS2 字节码文件（.tjs）反汇编为人类可读的 VM 指令。当需要理解游戏脚本逻辑、逆向工程已编译 TJS2 字节码、分析 Config.tjs/MainWindow.tjs/Initialize.tjs 行为、在编译脚本中查找字符串引用，或追踪 KiriKiri2 游戏中的函数调用流程时使用。也在用户要求"反编译 TJS"、"反汇编 .tjs"、"读取编译脚本"、"转储字节码"或想了解编译后游戏脚本的功能时触发。
---

# TJS2 字节码反汇编器

## 功能说明

将已编译的 TJS2 字节码文件（KiriKiri2 `.xp3` 归档中的 `.tjs` 文件）反汇编为可读的 VM 指令列表。这些文件不是纯文本——它们是带有 `TJS2100` 文件头的编译字节码。

输出内容包括：
- 脚本中所有的函数/类/属性上下文
- VM 指令（cp、call、jf、jnf、tt、ceq 等）
- 字符串常量、整数常量、对象引用
- 控制流（跳转、分支、try/catch 块）

## 前置条件

需要先构建 `tjsdump` 工具（原生 macOS/Linux 二进制文件，不是 Emscripten 版本）：

```bash
export VCPKG_ROOT=/Users/bytedance/vcpkg
cmake --preset "MacOS Release Config" -DBUILD_TOOLS=ON -DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison
cmake --build out/macos/release --target tjsdump
```

二进制文件位置：`tools/bin/mac/rel/tjsdump`

## 用法

### 基本反汇编

```bash
tools/bin/mac/rel/tjsdump /path/to/script.tjs
```

### 先从 XP3 解包，再反汇编

```bash
# 从 xp3 归档中解包所有文件
tools/bin/mac/rel/xp3 -o /tmp/extracted game.xp3

# 反汇编特定脚本
tools/bin/mac/rel/tjsdump /tmp/extracted/game/system/MainWindow.tjs
```

### 在反汇编输出中搜索特定字符串

```bash
# 在 MainWindow.tjs 中查找所有 "d3dMode" 的引用
tools/bin/mac/rel/tjsdump /path/to/MainWindow.tjs | grep "d3dMode"

# 转储到文件以便详细分析
tools/bin/mac/rel/tjsdump /path/to/MainWindow.tjs > /tmp/mainwindow_disasm.txt
```

### 查找特定函数

```bash
# 查找 initD3D 函数定义及其函数体
tools/bin/mac/rel/tjsdump /path/to/MainWindow.tjs | sed -n '/^(function) initD3D/,/^([a-z]/p'
```

## 读取输出

### 上下文头
```
(top level script) global 0x...     ← 顶层代码
(function) initD3D 0x...            ← 函数定义
(class) KAGWindow 0x...             ← 类定义
(property) isD3D 0x...              ← 属性 getter/setter
```

### VM 指令
```
*N = (type)"value"    ← 常量定义（字符串、整数、对象）
NN instruction args   ← 地址 NN 处的 VM 操作码
```

关键操作码：
- `cp %dst, %src` — 复制寄存器
- `call %result, %func(args)` — 函数调用
- `tt %reg` / `tf %reg` — 测试真 / 测试假
- `jf addr` / `jnf addr` — 假则跳转 / 非假则跳转
- `jmp addr` — 无条件跳转
- `ceq %a, %b` — 比较相等
- `cgt %a, %b` — 比较大于
- `entry addr, %reg` — try 块入口（catch 在 addr 处）
- `extry` — 退出 try 块
- `new %result, %class(args)` — 创建新对象
- `chgthis %func, %obj` — 将函数绑定到对象
- `gpi %result, %obj.%prop` — 间接获取属性
- `spi %obj.%prop, %value` — 间接设置属性
- `cl %reg` — 清空寄存器（设为 void）
- `srv %reg` — 设置返回值
- `ret` — 返回

### 寄存器约定
- `%-N` — 局部变量 / 函数参数（负数 = 局部变量）
- `%N` — 临时寄存器（正数 = 临时变量）
- `%-1` 通常是 `this`

## 常见分析模式

### 查找什么条件门控某个代码路径
```bash
# 示例：什么控制 D3D 初始化？
tjsdump MainWindow.tjs | grep -B5 -A10 "DrawDeviceD3D"
```

### 追踪函数调用链
```bash
# 查找所有引用 "isD3D" 的函数
tjsdump MainWindow.tjs | grep -n "isD3D" | head -20
```

### 查找脚本在哪里被加载
```bash
# 在所有脚本中搜索 "D3D.tjs" 的引用
for f in /tmp/extracted/**/*.tjs; do
  result=$(tjsdump "$f" 2>&1 | grep "D3D.tjs" | head -1)
  [ -n "$result" ] && echo "=== $(basename $f) ===" && echo "$result"
done
```
