# MotionPlayer Player final tail dispatch residual（四参考，2026-08-18）

## 结论

V256 把 `Player` 的 HM3、HM4 与 variable-track deque 连续区闭合到四端下一边界。本轮
继续检查该边界和四份精确 Player allocation，证明 deque 后不是对象终点或普通 alignment
padding，而是恰好一个 pointer-width 的最终成员：

```cpp
deque<VariableLabelScope> variableLabelScopes;
iTJSDispatch2 *tailDispatchLoadMotionResidual_guess; // raw, non-owning,
                                                     // deliberately uninitialized
// end of native Player
```

该 slot 不是 Engine back-pointer、SourceCache/ResourceManager native fast pointer，也不是活跃
`rootPlayer->currentDispatch` 的别名。四个 Player constructors 都不写它，四个 destructors
都不 AddRef/Release/clear 它，全镜像没有 producer。

Android 两端各保留一份零 code/data xref 的 load-motion residual function；它直接读取这个
尾 slot，并通过它调用 `onFindMotion`。iOS 两端把整个 residual consumer 裁掉，但精确对象
大小仍保留同一个尾成员。活跃 load helper 四端一致地读取
`player.rootPlayer->currentDispatch`，绝不读取尾 slot。

这也纠正了旧分析中的一项过宽判断：Android 的额外 load implementation 不是 live helper
的“同语义编译器 clone”。两者虽复用 request/member-hint identity 和后半段 ResourceManager
查找形状，但 callback receiver、result slot 生命周期以及可达性都不同。

## 1. 四 ABI 的最终对象边界

| 目标 | variable deque | deque size/end | final raw pointer | exact Player size | tail padding |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `+0x510` | `0x50`, end `+0x560` | `+0x560` | `0x568` | none |
| Android armv7 | `+0x380` | `0x28`, end `+0x3A8` | `+0x3A8` | `0x3B0` | 4 bytes after pointer |
| iOS arm64 | `+0x480` | `0x30`, end `+0x4B0` | `+0x4B0` | `0x4B8` | none |
| iOS armv7 | `+0x32C` | `0x18`, end `+0x344` | `+0x344` | `0x348` | none |

Android armv7 的 `+0x3AC..+0x3AF` 才是为 class 的 8-byte alignment 保留的真正 tail
padding；`+0x3A8..+0x3AB` 有 Android residual 的直接 pointer load，不能把完整 8 bytes
都解释为 padding。

四端 allocation 证据：

| 目标 | owning Engine constructor allocation | size |
|---|---:|---:|
| Android arm64 | `0x67BA1C` | `operator new(0x568)` |
| Android armv7 | `0x560AC4` | `operator new(0x3B0)` |
| iOS arm64 | `0x1001B803C` | `operator new(0x4B8)` |
| iOS armv7 | `0x1B7850` | `operator new(0x348)` |

particle/type-3 child creation使用相同 ABI size。对象大小、deque header size 和 Android
直接访问三组证据共同排除了“iOS 只是随机多 padding”的解释：这是同一 source-level
pointer member 在四个 ABI 中的落点。

## 2. constructor / destructor / producer 集

完整四端 constructor instruction range 中，final displacement 均无 store：

| 目标 | Player constructor | deque 构造/初始化起点 | final slot store |
|---|---:|---:|---:|
| Android arm64 | `0x6CC110` | `0x6CC3DC..0x6CC3E8` | none |
| Android armv7 | `0x5935C4` | `0x593744` | none |
| iOS arm64 | `0x10011EC04` | `0x10011ED90` | none |
| iOS armv7 | `0x11D488` | `0x11D72C` | none |

这不是优化后把 `nullptr` store 合并到别处：

- 两个 Android 产品对 final displacement 的完整 motionplayer code search 只有 residual
  function 的两次 load，没有任何 store；
- 两个 iOS 产品在 Player/motionplayer code range 中连 load 都没有；
- Engine 在完成 Player constructor 后直接发布 unique owner并继续构造下一 controller，
  中间没有把 Engine/dispatch/native pointer 写回 Player；
- 四端 normal destructor 的第一个 automatic-member cleanup 都是 preceding variable deque，
  final slot 没有 Release、delete、clear 或 null store；
- constructor unwind 同样无需清理 trivial uninitialized pointer。

因此可恢复的声明必须没有 `= nullptr` initializer。为它补零虽看似安全，会把 allocator 残留
变成稳定 null，并改变 Android residual 在被非正规地址调用时的边界；这不是参考源码行为。

## 3. Android tail-dispatch residual

| 目标 | residual entry | size | tail test | tail receiver load | return-slot clear | direct code/data xrefs |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x6CD42C` | `0x7A8` | `0x6CD464` | `0x6CD53C` | `0x6CD8E4` | 0 |
| Android armv7 | `0x593F60` | `0x264` | `0x593F7C` | `0x593FE8` | `0x594142` | 0 |

两个 body 的共同源级形态为：

```cpp
tTJSVariant tailDispatchLoadMotionResidual_guess(ttstr chara, ttstr motion) {
    if (tailDispatchLoadMotionResidual_guess != nullptr) {
        request = Dictionary{chara, motion};
        tTJSVariant callbackResult; // independent local
        tailDispatchLoadMotionResidual_guess->onFindMotion(
            request, result = &callbackResult);
        response = CopyRef(callbackResult).AsObject();
        chara  = required response.chara  or empty on failed PropGet;
        motion = required response.motion or empty on failed PropGet;
    }

    tTJSVariant result; // initialized only after the callback phase
    rm = CopyRef(player.resourceManager).AsObject();
    path = "motion/" + chara + "/" + motion;
    rm.findMotion(CopyRef(player.motionContext), CopyRef(path), &result);
    return result;
}
```

关键边界：

- tail pointer 是 raw borrow；residual 不 AddRef/Release 字段；
- request Dictionary 的 `chara/motion` 写入和 `onFindMotion/findMotion` 使用与 live helper
  相同的 process-wide member-hint slots；
- callback output 被 CopyRef/强制 Object conversion并用于替换局部字符串；
- 返回 Variant 在 callback 之后才初始化为 Void，随后只交给 `findMotion`；
- 因而 `findMotion` 失败且不写 output 时，residual 返回 Void，而不是 callback Object；
- 两端函数入口均无 code ref、data ref、registrar binding 或 live caller。

`_guess` 名称强调这里只恢复了行为角色，无法从 stripped image 证明原始 C++ spelling。
recovery IDB 现命名为 `Player_tailDispatchLoadMotionResidual_guess`。

## 4. 与 live `Player_loadMotion` 的严格分离

| 目标 | live helper | current dispatch gate |
|---|---:|---:|
| Android arm64 | `0x6AE2F0` | `0x6AE338`: `player.rootPlayer + 0x10` |
| Android armv7 | `0x57F654` | `0x57F67C`: `player.rootPlayer + 0x08` |
| iOS arm64 | `0x1001067BC` | `0x100106804`: `player.rootPlayer + 0x10` |
| iOS armv7 | `0x103BBC` | `0x103C2C`: `player.rootPlayer + 0x08` |

live helper 的共同差异：

1. 入口即初始化唯一 hidden-sret Variant；
2. `onFindMotion` 与后续 `findMotion` 都把 output 指向这同一个 Variant；
3. callback receiver 来自 canonical root Player 的 prefix `_currentDispatch`，使 child load
   也复用本次最外层 play/progress bridge 的脚本 receiver；
4. callback 期间独立 retain current dispatch；
5. `findMotion` 失败且不写 output 时，callback Object 原样成为返回值；
6. 两个 Android residual 的 final tail pointer、独立 callback result 和未初始化边界完全不
   参与 live flow。

因此不能把两个字段合并，也不能让 live helper fallback 到 tail slot。反过来，也不能给 tail
slot 加 initializer并以为只是初始化同一个 current-dispatch cache。

## 5. iOS 的 dead-strip 边界

iOS 两端对 UTF-16 `onFindMotion` 的 byte search 都只有一个 literal。其 code xrefs只进入 live
helper，剩余 xrefs进入 Player member registrar；不存在第二个 load body。Player code range 对
final displacement也没有访问。

这说明同一 source-level tail member仍影响 iOS `sizeof(Player)`，但无用 private residual 被
Darwin 链接/死码裁剪删除。Android toolchain留下 body不能反推 iOS 有另一套高层语义；iOS
没有 body也不能把 pointer member从共同类声明中删除。

## 6. 本地源码恢复与 port-only fast pointer 清理

本轮在 `Player.h` 的 native member region 中恢复：

```cpp
detail::VariableLabelScopeDeque _variableLabelScopes;
iTJSDispatch2 *_tailDispatchLoadMotionResidual_guess;
```

pointer 明确不提供 initializer、owner wrapper 或 destructor logic。`PlayerCore.cpp` 同时保留一个
private、零 caller 的 source-shaped residual method，复原 Android 的 direct tail receiver和
独立 callback/return Variant；它不注册到 NCB，也不接入 live load flow。

原 `_sourceCacheNative` 恰好也占一个 pointer width，但 fresh consumer 证明它语义错误：native
tail slot 被 residual 当作 `iTJSDispatch2 *` 调 `FuncCall`，绝不是 C++ `SourceCache *`。本轮：

- 删除 `_sourceCacheNative` member 和 constructor-body cache write；
- 三个 Web render helper 从稳定的 retained ResourceManager owner 通过 `nativeRM()` 按需
  unwrap；
- 保持原有 null/invalid RM 的不安全边界，没有新增 availability guard；
- 不改变三份 RM Variant owner、SourceCache base 或渲染 helper 的业务数据流。

同时修正此前过时的分析文字：

- `motionplayer_node_frame_merge_member_hint_family_four_binary_2026-08-16.md`；
- `motionplayer_player_load_parameter_node_member_hint_family_four_binary_2026-08-16.md`；
- `motionplayer_player_dead_wind_facade_backpointer_four_binary_2026-08-16.md`；
- `motionplayer_player_source_workspace_lifecycle_four_binary_2026-08-14.md`；
- 两份仍引用 `_sourceCacheNative` gate 名称的 render 分析。

## 7. recovery IDB 写回

四库共写回 16 comments、12 bookmarks、2 semantic renames：

- Android arm64：5 comments、3 bookmarks、1 rename；
- Android armv7：5 comments、3 bookmarks、1 rename；
- iOS arm64：3 comments、3 bookmarks；
- iOS armv7：3 comments、3 bookmarks。

Android 注释覆盖 final field test/load、零-xref residual entry、live/result-slot 差异和对象边界；
iOS 注释覆盖 deque->tail->allocation end，以及 live root-currentDispatch 路由。

iOS armv7 different-path 安全保存：

- pre-V257 backup：
  `out/idb-recovery/v257-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v257.i64`，
  377,592,016 bytes，SHA-256
  `59B13FEC4DD6819B38D80C4E87CC3E4CAC418C4731BDA2E8B3CCB9A20FD9E57A`；
- candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v257.i64`；
- `C:\IDA\idat.exe -A` 独立 probe 退出 0；
- canonical loose working files 移入 `pre-v257-canonical-loose/`，MCP readback loose files
  移入 `verify-readback-loose/`，均未删除；
- candidate 替换 canonical 后重新打开，回读三处 V257 comments，再关闭。

最终 IDB：

| IDB | size | SHA-256 |
|---|---:|---|
| Android arm64 | 366,728,756 | `ADF895A00B7F859BAEF0E7EACA6AFB56BFEAEBA226C282ADFCB5E89153526EC0` |
| Android armv7 | 345,870,749 | `9A2DF4FBFBDD5B15DE46DABCD8EDE52DD3C2BA2EE22EEDDD49AF440DCD6877B9` |
| iOS arm64 | 334,876,615 | `0C7119B3F85AD85E91012E187798A1569064287C75AAD54AC974B21717A637F6` |
| iOS armv7 | 377,592,016 | `4185719910DB9B1FB7047B12B2E6E322A54D39366779874EDD56DED735E24AEF` |

最终 IDA MCP session 数为 0。

## 8. 验证与 Wasm 基线

实际完成：

- 完整 `motionplayer-dll.cpp` 普通 Web `-fsyntax-only`：通过；
- 同一完整 TU 加 `KRKR2_WASMTIME_HEADLESS=1`：通过；
- Web Debug：33-step rebuild/link 通过；
- Wasmtime Headless Debug：62-step rebuild/link 通过；
- `krkr2_wasmtime_guest`：2-step rebuild/link/exnref conversion 通过；
- 三目标 `--parallel 1` 复核均为 `ninja: no work to do`；
- `git diff --check` 无 whitespace error，仅工作树既有 LF/CRLF warning；
- IDA MCP session 数为 0。

最终产物：

| wasm | size | FUNCTION | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Web `index.wasm` | 85,655,322 | `0x1BD31` | `0x1A4109D` | `0x5A3E40` | `0x3185F7B` | `86B8A97B03BCF141509E225CB2FE4DAB1EB6CD766AA0C2354181A326CFBBEDA9` |
| Wasmtime `index.wasm` | 85,002,463 | `0x1BA50` | `0x19E904B` | `0x5A1090` | `0x3141E11` | `7D05FBF6BBAECE99BC8F231D53FDA89AB5BE7E209BE6DEB6FCDCEFA05C1EC37F` |
| guest | 151,479,103 | `0x1618E` | `0x13D7DCD` | `0x4D1630` | `0x1421EBA` | `202BBF26BB6ECE33D24C6EA5D5F693D010A3E199E4CAA5232A3F01C9D55CDE8A` |

相对 V256，两份主 wasm 的 FUNCTION/DATA/name 不变，总大小和 CODE 均精确减少 24 bytes；
guest FUNCTION/DATA/name不变，CODE 减少 28 bytes。减少来自删除 constructor 中 port-only
native cache write并让三个使用点按需 unwrap后的优化结果。

private residual method 无 live caller，最终 executable FUNCTION/CODE section 没有增加；但 guest
保留 DWARF，source-shaped dead method使未列出的 debug/custom sections相对定义前中间产物增加
760 bytes。因此 guest 相对 V256 的总文件大小最终增加 688 bytes，同时 CODE 仍减少 28 bytes。
不能把 DWARF 文件大小变化误解为 runtime residual 已被接入。
