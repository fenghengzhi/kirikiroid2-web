# PSBFile 114 入口 IDB prototype 最终机械复扫

日期：`2026-08-02`。

## 范围

在 dispatch vtable、typed NCB wrapper 和 class-info/registration 三轮补型之后，本轮从
`functions/0xADDR.md` 机械取得全部 114 个 canonical 地址，并一次性导出 114/114 当前
prototype。严格筛选只保留会掩盖语义的 `__n128`、泛化 `void *`、无依据
`__int64/_QWORD/_DWORD`，不把真实的 `uint64_t/size_t` 业务参数误报为问题。

筛选出的最后五项均在主会话 fresh decompile 后纠正：

| 地址 | 旧状态 | 当前类型与证据 |
| --- | --- | --- |
| `0x42CF28` | `__n128()` | `.init_array@0x19EA090` 固定为 `void(void)`；Q0 只是两指针合并 store 后残留。 |
| `0x598AAC` | `PSBRawOwner *(self,data,size)` | 普通构造器改为 `void(self,data,uint64 size)`；尾部 X0 是 incoming self 残留。 |
| `0x599174` | `void(__int64 *, uint64)` | 精确 mangled symbol 与三指针访问固定为 `void(std_vector_string_arm64 *, size_t)`。 |
| `0x59A330` | `void *(PSBFile *, uint8, uint8)` | class-object CreateNew/NIS 数据流固定为 `iTJSDispatch2 *(PSBFile *, bool sticky, bool throwOnFail)`。 |
| `0x59B7E8` | `void(char **, __int64)` | 精确 mangled symbol `...IJRSs...` 与字段访问固定为 `void(std_vector_string_arm64 *, std_string_cow_arm64 *)`。 |

## 复反编译结果

- 模块静态初始化只建立 class-autoreg/pre-register 对象并更新 `_top` 链，不再显示向量返回；
- owner 构造器只写 `refCount/data/size` 与非空 data 下的 header views，不再显示
  `return self`；
- `vector::reserve` 直接显示 `self->begin/end/capacityEnd`、COW string `data` 迁移与
  `newCapacity`；
- `CreateAdaptor` 的 bool gate 不再显示位掩码泛化，返回与局部 result 均为
  `iTJSDispatch2 *`；
- emplace slow path 直接显示 vector/string ABI 记录与 lvalue copy-emplace 参数。

应用结果 `applied=5, failed=0`，随后再次导出 114/114 prototype。严格泛化筛选只剩三条
合法 `unsigned __int64`：`PSBFile::Adopt` 的 raw buffer size、`PSBRawOwner` 构造器的
同一 size、`vector::reserve` 的 AArch64 `size_t`；不存在剩余 `void *` prototype、
`__n128` 或无依据 `__int64/_QWORD/_DWORD`。

最后已 `idb_save` 到
`C:\Users\fenghengzhi\libkrkr2\libkrkr2\libkrkr2.so.i64`。本轮没有发现生产代码
差异，审计统计保持 `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
