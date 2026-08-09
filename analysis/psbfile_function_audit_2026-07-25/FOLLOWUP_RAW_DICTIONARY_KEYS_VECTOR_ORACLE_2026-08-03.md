# Follow-up：raw Dictionary ordered keys 与 gnustl COW vector 生命周期 oracle

日期：`2026-08-03`。本轮只使用 Android arm64 `libkrkr2.so` 与仓库已有、未修改的
`reference/xp3/logo_test/m2logo.mtn`，为
`PSBRawNode_GetDictionaryKeys_guess@0x598E64` 增加 direct raw oracle。观察面覆盖
hidden-sret、Android 旧 libstdc++ `std::vector<std::string>` 三指针拓扑、单指针 COW
`std::string`、精确 reserve、packed key 顺序以及目标内析构。没有修改 `cpp/` 生产实现，
没有生成或改写 fixture，也没有生成 APK 或 Android 二进制。

fresh Android 证据与当前生产实现一致，未发现新的确定 GAP；审计统计仍为
`99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。

## fresh Android arm64 证据

本轮重新反编译并核对：

- `PSBRawNode_GetDictionaryKeys_guess @ 0x598E64`
- `std::vector<std::string>::reserve @ 0x599174`
- `PSB_DecodeName_guess @ 0x597B1C`
- 二进制具名 `std::vector<std::string>::~vector @ 0x918690`
- 两个真实 callsite：`sub_695DE8 + 0x384 @ 0x69616C` 与
  `sub_695DE8 + 0xA1C @ 0x696804`

不超过 10 行的关键伪代码：

```text
keys(self, result X8): result={null,null,null}; if category(node)!=7: return
construct reusable COW string; keys=PsbArray(node+1); reserve(result, keys.count)
for i in [0,count): DecodeName(string, owner, keys[i]); result.emplace_back(string&)
destroy reusable string; return
reserve(v,n): if capacity<n, allocate 8*n and move each one-pointer string
reserve tail: destroy/reset old elements, free old storage, publish begin/end/capacityEnd
vector_dtor(v): for each string data pointer, decrement COW refcount
vector_dtor: delete rep when old refcount<=0; delete begin storage; do not clear v
```

关键边界：

- `0x598E98..0x598E9C` 在读取 tag 前把 hidden `X8` 指向的 24-byte result 完整置为
  `{0,0,0}`；非 Dictionary 正常返回这个精确空拓扑。
- Dictionary gate 后才构造复用 string。keys packed array 的 count 直接传给
  `reserve@0x599174`；result 初始为空，所以 capacity 精确等于 count。
- Android 旧 libstdc++ 中 vector 是 `{begin,end,capacityEnd}` 三指针，每个 string 元素
  恰为一个 data pointer。COW rep 位于 data 前 24 字节：length、capacity、32-bit
  refcount；循环中的 lvalue copy 与复用临时析构后，vector 独占 rep 时 refcount 为 0。
- `0x918690` 是二进制自身保留名称的目标内 destructor。它逐项做 COW release，再释放
  vector storage，不把三只 header 指针清零。
- 两个真实调用者都把 24-byte result 放在栈上并按 8-byte 元素遍历；第二个 caller 在
  `0x696788..0x696820` 内联等价 string/vector cleanup，交叉证明元素尺寸和析构拓扑。

## 本地生产实现逐行对照

`cpp/plugins/psbfile/PSBRawFile.cpp` 当前 `GetDictionaryKeys`：

1. `std::vector<std::string> result;` 对应 hidden result 三指针置零。
2. `GetTypeCategory()!=7` 直接返回默认空 vector，对应 gate 与精确空拓扑。
3. gate 后构造复用 `std::string key`，对应 `0x598EF8..0x598F04`。
4. 构造 `keys` 与死值 `offsets` 两个 packed view；Android O3 只保留消费的 keys，iOS
   同源 arm64 保留第二个死 view。当前源码保留两者及 `(void)offsets`。
5. `result.reserve(keys.nElementCount)` 对应 `BL 0x599174`。
6. 循环按 packed 下标调用 `DecodeName_guess(key,owner,keys[i])`，再
   `result.emplace_back(key)`；这是 lvalue COW copy，不改成 move 或预解码 host map。
7. 普通 RAII 返回负责复用 string 与 result 的异常清理，数据流、调用链、内部容器和
   生命周期均与 fresh 证据一致。

因此本轮不修改生产代码；新增 oracle 只把已对齐但此前没有 direct runtime 观察面的
容器结构固定下来。

## hidden-sret 与析构所有权

`CALL_SRET` 的 ABI carrier 从 16 扩到 32 字节，仍带 user-provided 空析构，因此
AAPCS64 由编译器在 `X8` 提供结果地址，普通参数保持 `X0..X7`。本 oracle 请求复制前
24 字节。完整 harness 的本机 arm64 `-O2` 汇编仍明确生成：

```asm
add x8, sp, #288
blr x28
```

空析构只阻止 harness 误释放目标对象。复制到 guest 输出槽后的 live vector 始终调用
`libkrkr2.so + 0x918690` 回收；不会让 harness 的 C++ runtime 解释、析构或释放
libkrkr2 创建的 STL 对象。

## 天然 pin 与 oracle 断言

输入 SHA-256：
`4382de8283cc0782fd269b16d3157bf3a9ec28916440f9192690eb178c0c18fe`。

只读 host parser 固定：

| 节点 | decoded offset | tag | ordered keys |
| --- | ---: | ---: | --- |
| root | `0xB4B` | `0x21` | `id,label,metadata,object,screenSize,source,spec,stereovisionProfile,version` |
| `object` | `0xB72` | `0x21` | `m2cheeseware_logo` |
| `version` | `0x5399` | `0x1E` | 非 Dictionary，期望空 vector |

设备 case 的生命周期：

1. raw load 得 owner ref=1；`GetRoot@0x598A3C` 得 root，ref `1 → 2`。
2. strict lookup `object` 与 `version`，ref `2 → 3 → 4`。
3. root `GetDictionaryKeys` 返回 size/capacity 均为 9 的 vector；九个元素逐项要求
   data pointer 非空、COW length 精确、capacity 不小于 length、refcount=0、尾 NUL、
   UTF-8 与 packed 顺序一致，九个 rep 地址互异。
4. `object` 返回 size/capacity 均为 1、内容为 `m2cheeseware_logo` 的同形 vector。
5. Real `version` 返回 `{0,0,0}`，证明非 Dictionary 在 reserve/name decode 前返回。
6. 每次调用前后及目标析构后 owner ref 都必须保持 4；vector 不借用 owner。
7. 每只 vector 由 `0x918690` 恰好析构一次，输出槽的三指针位模式保持不变。
8. 顺序释放 object、version、root holders，ref `4 → 3 → 2 → 1`；最后释放 raw
   PSBFile 触发 terminal owner 析构。异常路径按已取得的 vector/holder 所有权精确清理。

## trace 与当前验证

shape trace 从 59 扩为 63 个唯一地址，新增：

- `0x598E64`：1 个普通整数参数，hidden-sret，return kind `void`
- `0x599174`：2 个整数参数，`void`
- `0x597B1C`：3 个整数参数，`void`
- `0x918690`：1 个整数参数，`void`

当前验证：

- 四个相关 Python 文件 `py_compile`：通过。
- runner `--help`：已描述 ordered gnustl COW key-vector 生命周期。
- 天然 pin：root 9 keys、object 1 key、version tag `0x1E`，通过。
- COW vector host parser smoke：真实 root 九键、capacity=9、空 vector 三零，全部通过。
- adapter 控制流 smoke：真实 `m2logo.mtn` 字节驱动完整
  `1 → 2 → 3 → 4`、三组 vector/目标析构与 `3 → 2 → 1 → terminal` cleanup，通过；
  该内存模型不替代 Android runtime。
- `CALL_SRET` 24-byte 协议编码 smoke：通过。
- 完整 `harness.cpp` 本机 arm64 `-std=c++11 -O2`：编译通过，间接调用前设置 `X8`。
- shape trace：63/63 唯一，`ADDR_NAMES/ARG_COUNTS/RETURN_KINDS` 无缺项。
- IDB：把具名 destructor `0x918690` 的参数从裸 `__int64` 收窄为
  `std_vector_string_arm64*`，持久化三个局部变量语义名并保存成功。
- `verify_audit.py` 与 `git diff --check` 继续作为最终机械门禁。
- 后续 legacy-ABI ARM64 harness 已重建、重打包并固定哈希；真实 Android raw key-vector
  路径在无 trace 和单次全量 trace 中均为 `ok`。产物、命令和事件数见
  [FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。
  该结果不改变 fresh 反编译证据或 99/15/0 审计统计。
