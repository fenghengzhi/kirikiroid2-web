# Autoload count 发布时序、重复调用与 getter dead-strip 四端恢复

日期：2026-08-17

## 1. 范围与结论

V213 已闭合物理 `.tpm` discovery/sort/load 的主数据流。本轮继续恢复其中的
`TVPAutoLoadPluginCount` 全局状态与 `TVPGetAutoLoadPluginCount` surface，事实源仍严格限定为
`reference/binaries/` 的 Android arm64-v8a、Android armeabi-v7a、iOS arm64、iOS armv7
四个当前参考二进制。

四端共同结论：

1. count 是 BSS 中零初始化的有符号 32 位整数；
2. 它记录本轮 discovery/sort 完成后的 **found-record 数**，不是 loader 成功数、registered
   module 数或去重后的文件名数；
3. 赋值发生在 `std::sort` 返回之后、empty check/load loop 之前；空发现也会明确发布 `0`；
4. 同 Name、不同 Path 的 record 仍分别计数；后续 module-map miss、already-loaded false 或
   callback failure 都不修正/递减 count；
5. 函数入口不先清零。startup、project-path、任一目录扫描或 sort 在赋值前失败时，保留上次
   成功到达发布点的旧值；发布之后的日志、字符串转换或 loader 异常则保留本轮新值；
6. 四端完整 global xref 都证明唯一 writer 是 `tvpLoadPlugins`；`TVPLoadPlugin`、
   `Plugins.link/unlink/getList` 和 NCB loader 本身都不修改它；
7. 两份 Android shared object 保留一个三/四条指令的 raw getter，且 getter 没有镜像内 caller；
8. 两份 iOS 最终镜像没有 getter function/global read xref，只保留 count store。这个一致差异与
   未调用 getter 被最终链接 dead-strip 相符；本文不把链接阶段推断成新的源码级平台分支。

## 2. 四端映射

| 目标 | `tvpLoadPlugins` | count 计算/发布 | count global | getter |
|---|---:|---:|---:|---:|
| Android arm64 | `0x907618` | `0x907844..0x907854` | `0x1AF12C0` | `0x9081B0` |
| Android armv7 | `0x6C77BC` | `0x6C787C..0x6C7888` | `0x1142A60` | `0x6C7ABC` |
| iOS arm64 | `0x1003F1CD4` | `0x1003F1DB4..0x1003F1DD0` | `0x101B97428` | final image 中不存在 |
| iOS armv7 | `0x3D923C` | `0x3D9358..0x3D9378` | `0x18A62C8` | final image 中不存在 |

recovery IDB 使用：

- `tvpLoadPlugins_guess`；
- `TVPAutoLoadPluginCount_guess`；
- Android-only `TVPGetAutoLoadPluginCount_guess`。

私有/已剥离实体继续保留 `_guess`。iOS 不制造一个没有 code/xref 证据的 getter label。

## 3. count 的四种 ABI 计算形态

count 都来源于 vector 的 `[begin,end)` 字节差，结果写为 32 位整数；公式随 V213 已恢复的
found-record ABI 改变：

| 目标 | record stride | 指令级公式 | 语义 |
|---|---:|---|---|
| Android arm64 | `0x10` | `(end - begin) >> 4` | `size()` |
| Android armv7 | `0x08` | `(end - begin) >> 3` | `size()` |
| iOS arm64 | `0x30` | `((end-begin) >> 4) * 0xAAAAAAAB` low 32 | `size()` |
| iOS armv7 | `0x18` | `((end-begin) >> 3) * 0xAAAAAAAB` low 32 | `size()` |

iOS 先除以 `0x10/0x08`，得到 `3 * size`，再用 `0xAAAAAAAB` 的 32 位模乘还原
`size`。这不是 hash/magic state；它只是编译器对固定 stride 48/24 的除法 lowering。

四端随后都只提交低 32 位，和源码从 `vector::size_type` 转成 `tjs_int` 一致。本文不为超出
可分配地址空间的理论 overflow 增加参考实现不存在的 clamp。

## 4. 精确发布点

共同控制流可写成：

```cpp
void tvpLoadPlugins_guess() {
    // startup + three scans
    std::sort(list.begin(), list.end());

    TVPAutoLoadPluginCount_guess = static_cast<tjs_int>(list.size());
    for(const auto &item : list) {
        TVPAddImportantLog(TJS_W("(info) Loading ") + item.Name);
        LoadModule(PathPlusSlashPlusName(item));
    }
}
```

因此 count 的名字虽含 “Load”，其 commit boundary 却是 discovery/sort：

- loader 一次都没执行的空列表仍把旧值改成 `0`；
- 两个目录各发现一个同名 `.tpm` 时 count 为 `2`，即使两个 full-path key 都 miss；
- 第一项日志/loader 抛异常时，count 已是整个 vector 的 size，而不是 `0` 或 `1`；
- loader 返回 false 只是被忽略，不影响循环和 count；
- callback 抛异常会终止循环，但已发布 count 不回滚。

## 5. 重复调用与异常矩阵

假设进入第二次调用前 count 为 `old`：

| 第二次调用位置 | observable count |
|---|---|
| entry / internal startup 抛异常 | `old` |
| project path 构造抛异常 | `old` |
| 任一 `TVPListDir`/discovery callback 抛异常 | `old` |
| sort 抛异常 | `old` |
| sort 完成且 list 为空 | `0` |
| 发布 `N` 后首项日志抛异常 | `N` |
| 发布 `N` 后 joined-string/ttstr 转换抛异常 | `N` |
| 发布 `N` 后 module callback 抛异常 | `N` |
| 全部循环完成 | `N` |

这是普通非原子全局；四端都没有 lock、atomic、thread-local、generation counter 或 loaded-count
companion state。调用方应把它理解成最后一次到达发布点的 snapshot，而不是事务状态。

## 6. global xref 与 getter surface

### 6.1 Android

Android 两端的 global xref 集合只有：

- `tvpLoadPlugins_guess` 的 address materialization/store；
- `TVPGetAutoLoadPluginCount_guess` 的 address materialization/load。

LP64 getter 等价于：

```cpp
int TVPGetAutoLoadPluginCount_guess() {
    return TVPAutoLoadPluginCount_guess;
}
```

arm64 为 `ADRP + LDR W0 + RET`；armv7 为 literal/address add + `LDR R0 + BX LR`。两端 getter
都没有内部 call xref、参数、null gate、lazy initialization 或 count reset。其存在更像 shared
library ABI surface，而非游戏内部控制流消费者。

### 6.2 iOS

iOS 两端 global xref 集合都只有 `tvpLoadPlugins_guess` 的 store；全镜像没有任何读取该 global
的指令，也没有可对应 raw getter 的函数。两种 ABI 都出现同一结果，排除了“只是 IDA 漏认某一
端短 getter”的弱解释。

由于当前源码 header 以 export macro 声明 getter，而 iOS 最终镜像没有动态 shared-object ABI
约束，最保守解释是 unreferenced getter 在最终链接被 dead-strip。可证事实止于“最终镜像无 getter
和 read xref”；不据此在共同源码中加入 `#ifndef __APPLE__`。

## 7. 当前源码与回归

`cpp/core/plugin/PluginImpl.cpp` 的算法本已匹配四端，本轮没有改变产品行为，只把已恢复边界写清：

- global 是 zero-initialized 32-bit snapshot；
- entry 不清零；
- assignment 在 loop 前；
- pre-publication failure 保留旧值；
- post-publication failure 保留新值；
- Android getter 是 raw load，iOS absence 属于最终镜像差异而非擅自添加的源码分支。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 在既有 full-path miss 回归中捕获 getter 值，并确认
两次 direct `TVPLoadPlugin` miss 前后 count 不变。这固化“只有 discovery owner 发布 count”，
且不引入为了测试而存在的产品 reset/set API。

没有为完整 `tvpLoadPlugins` 异常矩阵增加 mock seam：参考实现没有该 seam，而当前构建的
`TVPListDir`/startup 涉及真实全局初始化与文件系统。异常矩阵由四端控制流和完整 global xref
闭合，测试只覆盖能够不改变产品结构的直接-loader 负边界。

## 8. recovery IDB 写回

四库累计写回并保存关闭：

- 10 项 semantic rename：4 个 `tvpLoadPlugins_guess`、4 个 count global、2 个 Android getter；
- 10 项 type application：4 个 `void(void)` loader、4 个 `int` global、2 个 `int(void)` getter；
- 14 项 function/store/global/getter comment；
- 10 项 publication/global/getter bookmark。

四库均已保存，最终 `idb_list` 为零 session。

## 9. 验证与产物

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` syntax-only 均通过；唯一告警仍是既有 `_tss`
  literal-operator deprecation；
- `cmake --build out/web/debug` 通过；
- `cmake --build out/wasmtime/debug` 通过；
- 两个 build tree 的 CTest 都正常返回 0，仍无已注册 CTest；
- 两份 Wasm 均 `WebAssembly.validate == true`；
- imports/exports 保持 Web `539/69`、Wasmtime `538/69`；
- `git diff --check` 返回 0，仅有仓库既有 LF/CRLF 提示。

产品产物与 V213 字节级一致，因为本轮产品侧只增加注释，而新增断言位于 unit-test TU：

| 产物 | size | SHA-256 |
|---|---:|---|
| Web `index.wasm` | 85,657,793 B | `858A3677901252A11D37637BC3BE7423D1ACD9D019080E64E18276379CE49D55` |
| Wasmtime `index.wasm` | 85,004,934 B | `FC8847E666976A424C9BD1A4780E5124F071D114CB6373B1F6985AC350A22C08` |

section 也与 V213 完全相同：

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41AB5` | `0x19E9A63` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185E4E` | `0x3141CE4` |

## 10. 未过度推断的部分

- iOS 最终镜像无 getter/read xref 是直接证据；“dead-strip”是与四端构建形态一致的链接解释，
  不是从 binary 反推出的精确 linker flag；
- count 的正常可达范围远低于 `tjs_int` overflow，本文保留 cast 行为，不为理论超大 vector
  添加 clamp；
- 本纵切面闭合 autoload count，不代表 motionplayer 总目标已经完成。
