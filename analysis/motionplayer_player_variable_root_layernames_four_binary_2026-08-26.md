# Player set/getVariable、modifyRoot、getLayerNames（四参考二进制，2026-08-26）

## 1. callback 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| setVariable raw | `0x6CE250` | `0x594680` | `0x10011FD04` | `0x11E978` |
| getVariable | `0x6CA77C` | `0x592810` | `0x10011D3D8` | `0x11BD50` |
| modifyRoot | `0x6CA490` | `0x5926DE` | `0x10011D1DC` | `0x11BB72` |
| getLayerNames | `0x6CE4C0` | `0x594798` | `0x10011FE88` | `0x11EB7C` |

16 个 endpoint 均已 fresh decompile + disassemble。Android arm64 的
`setVariable` 与 `getLayerNames` 此前分别被 IDA 吞进前一函数的 EH tail；本轮
用精确起止 `0x6CE250..0x6CE3F8`、`0x6CE4C0..0x6CE6FC` 恢复代码和函数边界，
然后重新 fresh 反编译。

## 2. setVariable raw callback

共同 callback 形状：

```cpp
tjs_error setVariableRaw(
    Variant * /*result*/, int argc, Variant **argv, Player *self) {
    if (argc < 2)
        return TJS_E_BADPARAMCOUNT;

    ttstr key(*argv[0]);
    int mode = argc == 2 ? 0 : int(argv[2]->AsInteger());
    double value = argv[1]->AsReal();
    self->bindParameterValue(key, mode, value);
    return TJS_S_OK;
}
```

边界和顺序：

- ncbind method object 在进入 callback 前清 result、解析 native `Player*`；null
  receiver/wrong-native 在 outer layer 返回 `TJS_E_NATIVECLASSCRASH`。
- callback 自身只做 `argc < 2` gate；恰好 2 个参数时 mode=0，3 个或更多时只
  读取 argv[2]，其余忽略。
- 转换顺序固定为 key ttstr、可选 mode Integer、value Real；任一步异常都会
  阻止 binder。key 临时 owner 在正常/异常退出时释放。
- result 不由 callback 写入；Void publication 是 outer raw method object 的
  责任。
- count 已满足时不额外检查 argv/element null，保留 native malformed-call crash
  boundary。

本地原先把第四参数声明为 legacy `iTJSDispatch2*`，并在 callback 内再次解析
Player；这会选择 ncbind 的另一套 method template，改变 receiver failure/result
clear 的责任层。本轮已改为参考一致的 `Player*` 签名、删除二次解析，并增加
编译期函数指针类型断言。

完整 `bindParameterValue` 的 HM1/HM2/HM3/parameter-ramp 写传播属于后续变量
数据流切片；本报告只关闭 script raw entry 的 ABI 与 owner/arity/转换边界。

## 3. getVariable 与 HM1/HM2

共同 label splitter：first `::` 优先；若不存在再取 first `/`；空 scope 和空
suffix 都是有效 split。split lookup 会重建 `scope + "::" + suffix`，因此
slash 输入被规范到 HM1 的 double-colon key；unsplit 保留原始 ttstr。

```cpp
double getVariable(ttstr label) {
    Parts p = splitParameterLabel(label);
    if (p.split) {
        auto it = HM1.find(p.scope + L"::" + p.suffix);
        return it == HM1.end() ? 0.0 : it->second.writeVal;
    }
    auto it = HM2.find(label);
    return it == HM2.end() ? 0.0 : it->second;
}
```

| 端 | HM1 | HM2 |
|---|---:|---:|
| Android arm64 | `Player+0x108` | `Player+0x140` |
| Android armv7 | `Player+0xC0` | `Player+0xDC` |
| iOS arm64 | `Player+0xD0` | `Player+0xF8` |
| iOS armv7 | `Player+0xA0` | `Player+0xB4` |

两者都是 `unordered_map<ttstr,...>`，使用相同的 backing-aware equality 和
mutable Hint hash cache。lookup 可在传入 ttstr 的共享 backing 上发布首次计算的
32-bit hash；null backing 的 hash 为 0，非 null 的零结果改成 `UINT32_MAX`。
getter 不插入缺失 key，不走 operator[]，missing 精确返回 `+0.0`。

## 4. modifyRoot

```cpp
void modifyRoot() { nodes[0].delta.dirty = true; }
```

| 端 | root dirty |
|---|---:|
| Android arm64 | `Node+0x630` |
| Android armv7 | `Node+0x540` |
| iOS arm64 | `Node+0x640` |
| iOS armv7 | `Node+0x520` |

它无条件写 1；不读旧值、不递归、不重算 transform。element zero lookup 无
empty-deque guard，依赖构造器创建 synthetic root。

## 5. getLayerNames

```cpp
Variant getLayerNames(ttstr filter) {
    Array result = createFreshArray();
    for (const auto &[label, index] : nodeLabelMap) {
        if (filter.empty() || label.IndexOf(filter, 0) >= 0)
            result.Items.emplace_back(String(label));
    }
    return result;
}
```

它遍历的是 raw-label `std::map<ttstr,int,ttstr_utf16_less>`，不是 flat node
deque：

- tree in-order；null backing 排最前，其余按 UTF-16 code unit lexicographic；
- mapped flat-node index、node type、visibility 与 child recursion 都不参与；
- duplicate label 在建树时已折叠为唯一 map key，getter 不二次去重；
- null/empty filter 枚举全部 key；非空 filter 做 case-sensitive UTF-16 substring
  search，不是 prefix/glob/regex；
- 每个 key 作为 String Variant CopyRef 直接追加到 native Items；result 每次 fresh。

map root/sentinel 坐标分别为 Android arm64 `+0x30/+0x20`、Android armv7
`+0x18/+0x10`、iOS arm64 `+0x18/+0x20`、iOS armv7 `+0x0C/+0x10`；差异是
libstdc++/libc++ tree ABI，不应写进 portable source container。

## 6. 验证状态

既有测试覆盖 direct HM1/HM2、`::`/`/`/empty label、raw callback argc/mode、
modifyRoot dirty，以及 getLayerNames fresh/typed arity/empty/full/substring 结果。
本轮语义修复仅涉及 setVariable raw callback 的签名与解析责任层。

四个 IDB 已命名 split helper、binder 和四个 endpoint 并保存。正式工具链不可用，
未执行测试；setVariable raw entry 状态为 `IMPLEMENTED`，其余为
`EVIDENCED_4_4`。完整 binder 写传播保留为后续依赖项。

该后续依赖已由
`motionplayer_player_variable_binder_dataflow_four_binary_2026-08-27.md` 闭合：四端 binder、
HM1 cache rebuild、ParameterRampMap equal-range updater、全部 caller 与换树重建闸门均已
fresh 审计；同时修正了旧树 reset 把 `writeVal` 误当成 `weight` 的本地字段错误。

## 7. 2026-08-27 getLayerNames EH 闭包

`motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md` 已闭合
getLayerNames 的 String append/reserve 与 caller owner 前沿：Android arm64、iOS arm64
LSDA cold 与 iOS armv7 SjLj 路径在异常时析构局部 Array Variant；只有 Android armv7 的
完整函数/相邻 catalog 无本帧 cleanup。四端都在完整遍历后才复制返回 owner，不会发布
partial Array。modifyRoot/getLayerNames 联合 row 现为 `IMPLEMENTED`。这里不改变
setVariable/binder 其它独立依赖项的状态。
