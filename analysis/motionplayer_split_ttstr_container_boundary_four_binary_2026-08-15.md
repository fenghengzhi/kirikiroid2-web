# MotionPlayer `splitTtstr` 容器、调用链与边界（四参考，2026-08-15）

## 结论

四个 1.3.9 参考二进制包含同一个被剥离名称的共享 helper。它接收按值复制的
`ttstr remainder`、按引用传入的 `ttstr separator`，通过隐藏返回槽构造
`std::vector<ttstr>`。每次找到分隔符时先把前缀作为一个独立持有引用的
`ttstr` 放入 vector，再用分隔符后的子串替换本地 remainder；循环结束后无条件
追加最终 remainder。因此尾分隔符、连续分隔符和空输入都保留空元素。

本地名称继续使用 `splitTtstr_guess`：四个 stripped 目标能证明实现身份和用途，
不能证明原始 C++ 拼写。

本轮还修正了一处调用链级偏差。四个参考实现每轮都按以下次序调用长度 getter：

1. `separator.GetLen()`，用于 suffix 起点；
2. `remainder.GetLen()`；
3. 再次 `separator.GetLen()`，用于 suffix 长度。

旧移植把 `index + separator.GetLen()` 缓存为 `next`，从而每轮少调用一次
`separator.GetLen()`。虽然正常不可变 `ttstr` 下结果相同，但不符合参考调用链，
现已恢复为两次 getter 的表达式结构。

## 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `splitTtstrByDelimiter_guess` | `0x695114` | `0x571C50` | `0x1000F52D0` | `0xF1D20` |
| `ttstr::IndexOf` 路径 | `0xA0B4EC` | `0x75EED8` | `0x1001A0B90` | `0x19FDCC` |
| `ttstr(const tjs_char *)` 分配路径 | `0xA11FC0` | `0x761EF8` | `0x10025F4EC` | `0x2605D0` |
| split 异常清理 | 函数内 `0x6952B8..0x695340` | `.ARM.extab` generic entry `0xEF7048` | cold block `0x1000F5440..0x1000F5470` | SjLj handler `0xF1EB8..0xF1EFE` |

Android armv7 的 `.ARM.exidx` 条目把 `0x571C50` 指向 `.ARM.extab` 的 generic
记录，personality 为 `0xD33219`。主函数反汇编中没有像 Android arm64 那样展开的
cold cleanup block；这是一项异常 ABI/编译器表示差异，不能据此声称它没有 unwind
元数据。

## 共同控制流

四端共同源级形状可写为：

```cpp
std::vector<ttstr> split_guess(ttstr remainder,
                               const ttstr &separator) {
    std::vector<ttstr> pieces;
    for(;;) {
        int index = remainder.IndexOf(separator, 0);
        if(index < 0) break;

        pieces.push_back(remainder.SubString(0, index));
        remainder = remainder.SubString(
            index + separator.GetLen(),
            remainder.GetLen() - index - separator.GetLen());
    }
    pieces.push_back(remainder);
    return pieces;
}
```

关键机器级顺序如下：

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| vector 三指针清零 | `0x695148..0x69514C` | `0x571C70..0x571C74` | `0x1000F52F4..0x1000F52F8` | `0xF1D48..0xF1D5E` |
| `IndexOf(..., 0)` 与 signed miss | `0x69515C..0x695164` | `0x571CF0..0x571CF8` | `0x1000F53D8..0x1000F53E0` | `0xF1E50..0xF1E5A` |
| prefix `SubString` / push | `0x695178..0x6951D0` | `0x571C82..0x571C90` | `0x1000F5310..0x1000F535C` | `0xF1D88..0xF1DCE` |
| 两次 separator len、一次 remainder len | `0x6951D8..0x6951F0` | `0x571C98..0x571CAA` | `0x1000F5364..0x1000F537C` | `0xF1DDA..0xF1DF6` |
| suffix `SubString` / replace remainder | `0x695208..0x69523C` | `0x571CBA..0x571CE6` | `0x1000F5394..0x1000F53C8` | `0xF1E0E..0xF1E42` |
| 无条件 final push | `0x695250..0x695284` | `0x571CFA..0x571CFE` | `0x1000F53F0..0x1000F5424` | `0xF1E6A..0xF1E98` |

`IndexOf` 的结果虽然在 64 位 ABI 上由 `W0`/低 32 位返回，循环退出测试仍是
32 位 signed `< 0`。substring 的起点和长度则走 32 位 unsigned 加减；没有额外
overflow、范围或 separator 长度保护。当前源码的显式 unsigned cast 保留该域。

## vector 与 `ttstr` 所有权

返回对象是标准三指针 vector 布局：

| ABI | begin/end/capacity | element stride | 空 vector 大小 |
|---|---:|---:|---:|
| 两个 64 位目标 | 3 × 8-byte pointer | 8 bytes | 24 bytes |
| 两个 32 位目标 | 3 × 4-byte pointer | 4 bytes | 12 bytes |

每个 element 是一个 `ttstr` 的内部字符串对象指针。in-capacity push 会先增加该
字符串对象的 intrusive reference，再写入 end 并推进 end；grow helper 也复制这些
持有引用。prefix temporary 在 push 后立即析构。suffix 替换 remainder 时，四端都是
先持有新字符串、再释放旧 remainder、再销毁 suffix temporary，因此 remainder 始终
留下一个有效持有引用。

调用者取得返回 vector 后可独立销毁输入、separator temporary 和本地 remainder。
例如 `ResourceManager_isExistMotion_guess` 四端都在 split 返回后立即销毁输入副本与
`"/"` temporary，读取 vector 的元素 1/2，最后销毁每个 element 并释放 vector buffer：

| 目标 | split call | caller-side vector destroy |
|---|---:|---:|
| Android arm64 | `0x6A6B3C` | `0x6A6F3C..0x6A6F6C`（内联 element loop） |
| Android armv7 | `0x57B7C2` | `0x57B932..0x57B934` |
| iOS arm64 | `0x100101B20` | `0x100101D80..0x100101D84` |
| iOS armv7 | `0xFED7C` | `0xFEF9E..0xFEFA0` |

异常路径同样负责销毁已构造的 prefix elements 和 vector buffer。Android arm64
在本函数内明确循环 Release 后 `operator delete`；iOS arm64 的相邻 cold block 先做
必要 temporary cleanup，再调用 vector destructor 并 `_Unwind_Resume`；iOS armv7 的
SjLj handler 按 call-site 状态选择 temporary cleanup，随后调用 vector destructor 并
`_Unwind_SjLj_Resume`。Android armv7 由上表的 EHABI generic extab 描述 unwind。

## 边界行为

### 空 separator

四套 `ttstr(const tjs_char *)` 构造路径都先检查输入指针和首个 UTF-16 code unit；
首 code unit 为零时直接产生 null string storage。四套 `IndexOf` 又都在 haystack 或
needle storage 为 null 时返回 `-1`，不会进入底层宽字符串搜索。因此规范 `ttstr`
状态下：

```text
split("abc", "") -> ["abc"]
split("", "")    -> [""]
```

这排除了“空 separator 导致 remainder 不前进而死循环”的错误推断。只有人为破坏
`ttstr` 不变量、制造非 null 但长度为零的内部字符串对象时，才可能落入底层空 needle
搜索；这不是正常脚本或 C++ 构造路径可产生的状态。

### 其余字符串边界

```text
split("", "/")       -> [""]
split("abc", "/")    -> ["abc"]
split("/abc", "/")   -> ["", "abc"]
split("abc/", "/")   -> ["abc", ""]
split("a//b", "/")   -> ["a", "", "b"]
split("a::b", "::")  -> ["a", "b"]
```

没有跳过空 element、trim、最大分段数或 separator 单字符限制。最后 remainder 无条件
push，保证返回 vector 至少一个 element；但多个原生 caller 随后直接读取元素 1、2，
没有 vector-size gate，因此畸形 path 的越界风险属于 caller 边界，不由 split helper
兜底。

## 完整调用面

四端 `xrefs_to` 都精确返回九个 code call site，函数集合及次序一致：

| caller | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_loadKrkrAtlasSource_guess` | `0x69323C` | `0x570FA2` | `0x1000F4114` | `0xF0C78` |
| `ResourceManager_isExistMotion_guess` | `0x6A6B3C` | `0x57B7C2` | `0x100101B20` | `0xFED7C` |
| `ResourceManager_findMotion_guess` | `0x6A7324` | `0x57BA3E` | `0x100101EE4` | `0xFF1A6` |
| `ResourceManager_findSource_guess`（外层 path） | `0x6A7F84` | `0x57BE24` | `0x1001025EC` | `0xFF91A` |
| `ResourceManager_findSource_guess`（blank dimensions） | `0x6A808C` | `0x57BF36` | `0x100102798` | `0xFFACC` |
| `Player_initEmoteMotion_guess` | `0x6B0468` | `0x58092C` | `0x100107EC0` | `0x105504` |
| `Player_updateMotionSubNodes_guess` | `0x6BB7A4` | `0x587FA6` | `0x1001114D0` | `0x10E79A` |
| `Player_updateParticleSystems_guess` | `0x6BCCC0` | `0x588B34` | `0x10011216C` | `0x10F980` |
| `Player_bindParameterValue_guess` | `0x6C1C9C` | `0x58C59C` | `0x100116518` | `0x113E82` |

这组调用面说明 helper 不是 ResourceManager 私有实现，而是 Player、资源查询、粒子、
子 motion 和参数绑定共同使用的 translation-unit/local utility。

## 本地迁移结果

- `cpp/plugins/motionplayer/MotionDispatch.h` 删除了旧式四个绝对地址注释，只保留四端
  共同语义与 `_guess` 置信度；
- suffix 表达式恢复两次 `separator.GetLen()`，匹配四端调用次数、求值数据流和 32 位
  unsigned 算术；
- 保持 pass-by-value remainder、独立 owning elements、final unconditional push、空
  separator singleton 与 caller 自负越界风险；
- 四份 recovery IDB 补充 exact boundary 注释、异常 cleanup 名称/书签并保存；
- 绝对地址只保留在本分析文档，不再进入编译源码注释。

## 验证

- `cmake --build --preset "Web Debug Build"`：成功，34 个 motionplayer 相关编译/链接
  step 全部完成；仅保留既有 `_tss`、`imagepacker.h [[nodiscard]]` 和 Emscripten 链接警告；
- 完整 `motionplayer-dll.cpp` Emscripten 响应文件语法检查：成功，仅既有 `_tss` 警告；
- 针对本轮源码、迁移文档和 `plan.md` 的 `git diff --check`：成功，仅工作树既有
  LF→CRLF 提示；
- 四份 recovery IDB `idb_save`：均返回 `ok=true`。
