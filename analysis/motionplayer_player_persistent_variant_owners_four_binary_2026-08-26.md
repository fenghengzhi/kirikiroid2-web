# Player persistent Variant owner 属性（四参考二进制，2026-08-26）

## 1. 范围

闭合：

- #3 `resourceManager`（read-only）；
- #11 `tags`（read-only）；
- #12/#13 `motionKey/project`（同一 read/write owner）；
- #25 `outline`；
- #26 `meshline`。

共 32 个 fresh callback decompile、每端 8 个不同 callback。

## 2. 四端字段顺序与 callback

| 属性 | Android arm64 | 字段 | Android armv7 | 字段 | iOS arm64 | 字段 | iOS armv7 | 字段 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| resourceManager get | `0x6D67F4` | `+0x3E0` | `0x598D3C` | `+0x2AC` | `0x100125448` | `+0x370` | `0x124642` | `+0x26C` |
| motionKey/project get | `0x692FC0` | `+0x3F4` | `0x570E6C` | `+0x2B8` | `0x1000F3D84` | `+0x384` | `0xF06F8` | `+0x278` |
| motionKey/project set | `0x6B1D58` | 同上 | `0x5817A4` | 同上 | `0x100109168` | 同上 | `0x106994` | 同上 |
| outline get/set | `0x6D6B10/0x6D6B1C` | `+0x408` | `0x598F8A/0x598F98` | `+0x2C4` | `0x100125648/0x100125654` | `+0x398` | `0x12486E/0x12487C` | `+0x284` |
| meshline get/set | `0x6D6B24/0x6D6B30` | `+0x41C` | `0x598FA0/0x598FAE` | `+0x2D0` | `0x10012565C/0x100125668` | `+0x3AC` | `0x124884/0x124892` | `+0x290` |
| tags get | `0x6D69F8` | `+0x430` | `0x598E50` | `+0x2DC` | `0x100125544` | `+0x3C0` | `0x124748` | `+0x29C` |

LP64 Variant 为 20 字节，ILP32 Variant 为 12 字节；表中连续偏移直接证明这五项的
物理 owner 顺序。源代码只需保存 owner 声明/析构顺序，不能硬编码 ABI padding。

## 3. 共同语义

getter 共同伪代码：

```cpp
Variant get() const {
    return VariantCopyConstruct(persistentField);
}
```

setter：

```cpp
void set(Variant value) {
    persistentField = value;
}
```

Variant CopyRef/assignment 的类型依赖：

- Object：复制 object/objthis 指针并 AddRef；
- String：复制字符串 owner 并增加原子引用计数；
- Octet：复制 octet owner；
- Integer/Real：复制 POD payload；
- Void：保持 Void；
- setter 先安全取得新 owner，再释放旧 owner，支持 source 与 destination alias。

这些 callback 不做类型转换、字符串化、Object unwrap、容器 clone、motion lookup、
dirty 标记或 renderer 调用。

## 4. 每项 owner 边界

### resourceManager

构造器从输入 ResourceManager dispatch CopyRef 到三个独立 Variant owner；本属性返回
其中 canonical owner 的又一个 CopyRef。getter 不返回 native ResourceManager 指针，
也不新建 wrapper。调用者持有返回 Variant 时，该 dispatch 可越过 Player 析构继续
存活。

### tags

返回当前 persistent tag-frame-source Variant。它不重新读取 motion["tag"]，不克隆
Array，也不根据当前 frame 过滤。公开表面 read-only；返回值仍是独立 owner。

### motionKey / project

两个名字在四端 registrar 完全复用同一 getter/setter 地址，访问同一 Variant。
setter 只 copy-assign，不触发 play/load/findMotion。任一名字写入后，另一个名字立即
读取同一 owner。

### outline / meshline

两者是相邻但独立 owner；构造初始都是 Void。任意 Variant 类型原样保留。
renderer 消费时可以临时 CopyRef，但 property setter 本身没有验证或 publication。

## 5. 异常与生命周期

正常 CopyRef 不分配新的 dispatch/container。底层 String/Octet/Object refcount 操作
依照各自 owner 规则；callback 没有 catch/rollback 层。setter 覆盖旧 owner 后，
旧 owner 的最后一次 Release 可同步触发其析构，这是普通 Variant assignment 的
可观察生命周期边界。

Player 自动成员析构按真实 owner 声明逆序释放这些字段；完整 Player 声明/析构顺序
仍由构造器/析构器总账最终统一验收，本报告只证明五个坐标和 accessor owner flow。

## 6. 本地与测试

本地：

- `getResourceManager/getTags/getMotionKey/getOutline/getMeshline` 返回 by-value Variant；
- `project` 直接别名 motionKey；
- 三个 setter 使用完整 Variant assignment；
- 没有额外 clone/unwrap。

现有单元用例覆盖 tags read-only owner、motionKey/project alias、outline/meshline
owner 与跨覆盖后的引用存活。当前工具链缺失，未在本轮执行；状态为
`EVIDENCED_4_4`。

