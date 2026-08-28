# Player class-static 默认值与直接渲染开关（四参考二进制，2026-08-26）

## 1. 范围与结论

本纵切面闭合 Player 表面：

- #1 `defaultSyncActive`；
- #2 `defaultTransformOrder`；
- #64 `useD3D`；
- #65 `pixelateDivision`。

前两项是带 `TJS_STATICMEMBER` 的 class-static property：callback 没有 `this`
参数，descriptor 不查询 Player adaptor/native instance。后两项是普通 per-Player
直接字段。这个结构差异会影响脚本属性 flags、无实例访问和 native-instance
失败边界，不能只把两类都描述为“某个 getter/setter”。

## 2. 四端映射

| 属性/角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| defaultSyncActive get | `0x6D67D8` | `0x598D24` | `0x100125430` | `0x124626` |
| defaultSyncActive set | `0x6D67E4` | `0x598D30` | `0x10012543C` | `0x124634` |
| defaultSyncActive global | `0x1AB54A8` | `0x111160C` | `0x102517794` | `0x2143970` |
| defaultTransformOrder get | `0x6ADD5C` | `0x57F4B8` | `0x100106564` | `0x103918` |
| defaultTransformOrder set | `0x6ADE94` | `0x57F54C` | `0x1001066A8` | `0x103A84` |
| defaultTransformOrder[4] | `0x1AA10D8` | `0x1102090` | `0x101ADF750` | `0x1831844` |
| useD3D get/set | `0x6931C0 / 0x6D6D00` | `0x570F4C / 0x59909E` | `0x1000F4090 / 0x100125878` | `0xF0BDC / 0x124ACC` |
| useD3D field | `Player+0x38D` | `Player+0x275` | `Player+0x31D` | `Player+0x235` |
| pixelateDivision get/set | `0x6D6D0C / 0x6D6D14` | `0x5990A4 / 0x5990AA` | `0x100125880 / 0x100125888` | `0x124AD2 / 0x124AD8` |
| pixelateDivision field | `Player+0x390` | `Player+0x278` | `Player+0x320` | `Player+0x238` |

四端文件初值原始读取一致：

```text
defaultSyncActive = false
defaultTransformOrder = {0, 3, 2, 1}
useD3D = false                  // 每个 Player 构造
pixelateDivision = 100          // 每个 Player 构造
```

四个 IDB 已把全局命名为 `Player_defaultSyncActive` /
`Player_defaultTransformOrder`，callback 也已统一命名和注释后保存。

## 3. Static property 结构证据

`defaultSyncActive` 的 getter 只读全局 byte，setter 的唯一形参就是新 Boolean；
四端都没有接收或解引用 Player 指针。`defaultTransformOrder` getter 只有返回
Variant 的隐藏 sret 参数，setter 只有输入 Variant；同样没有 Player 指针。

ncbind 的 `InvokeCommand` 会在函数 traits 的 `ClassT == void` 时：

- 选择 `noInstanceGetter`，不从 `objthis` 查 native instance；
- 给 descriptor 设置 `TJS_STATICMEMBER`。

因此原始源结构必须是四个 `static` C++ 成员函数。仅在非 static 成员函数内部访问
static global 虽能得到相同数值，却会生成普通实例 property 和不同的失败边界。

## 4. defaultTransformOrder getter

共同伪代码：

```cpp
static Variant getDefaultTransformOrder() {
    Array result = createFreshArray();
    for (int i = 0; i < 4; ++i)
        result.Items.push_back(Integer(defaultTransformOrder[i]));
    return result;
}
```

每次调用都创建新的 TJS Array 和独立 Items 容器；不会返回共享 global Array。
元素固定按索引顺序作为 signed Int32 Variant append。Android arm64 使用
20-byte Variant、25 元素 libstdc++ block；Android armv7 使用 12-byte Variant、
42 元素 block；iOS 两端使用此前已闭合的 libc++ Array Items deque。只有 4 个元素，
正常路径不需要第二数据 block，但仍遵循相同 append/grow helper。

## 5. defaultTransformOrder setter

共同伪代码：

```cpp
static void setDefaultTransformOrder(Variant input) {
    Dispatch *array = input.AsObjectNoAddRef();       // 非 Object 在此抛转换异常
    bool used[4] = {false, false, false, false};

    for (int i = 0; i < 4; ++i) {
        Variant elem;
        if (FAILED(array->PropGetByNum(
                TJS_MEMBERMUSTEXIST /* 1024 */, i, &elem, array)))
            throw L"illegul size of transform order";

        int v = int(elem);                            // 普通 TJS Int32 转换
        if ((unsigned)v > 3 || used[v])
            throw L"illegul variable for transform order";

        defaultTransformOrder[i] = v;                // 立即写，不事务回滚
        used[v] = true;
    }
}
```

精确边界：

- 输入必须能按 Object Variant 转换；null object 随后的虚调用仍沿原生崩溃/异常边界，
  没有主动 null guard；
- 索引读取 flag 固定为 1024，ObjThis 是同一个 Array dispatch；
- 只读 0..3；额外元素被忽略；
- 负值通过 unsigned range check 被拒绝；
- 重复值和大于 3 的值使用同一个 variable-error 文本；
- 每一项验证成功后立刻写 global，再读取下一项；第 k 项失败时 0..k-1 的写入永久
  保留，当前项和后缀保持旧值，没有 rollback；
- 元素的普通 Variant→Int32 转换异常不被改写成自定义 variable error。

两个错误字符串在四端均以完整 UTF-16LE 原始字节唯一命中并回到 setter：

- `illegul size of transform order`
- `illegul variable for transform order`

原始拼写 `illegul` 是可观察 API，不能“修正”为 `illegal`。

## 6. useD3D 与 pixelateDivision

两项都是 per-Player direct field：

```cpp
bool getUseD3D() const { return useD3D; }
void setUseD3D(bool v) { useD3D = v; }

int getPixelateDivision() const { return pixelateDivision; }
void setPixelateDivision(int v) { pixelateDivision = v; }
```

`useD3D` getter 不探测平台 D3D 能力，setter 也不即时重建 renderer。
`pixelateDivision` 是未验证 signed Int32，不 clamp 正数；它与
`D3DEmoteModule::pixelateDivision` 是不同对象的独立字段。

## 7. 本地差异与修复

本地算法和初值原本已经匹配，但 #1/#2 四个函数声明为非 static。这样
`NCB_PROPERTY` 会实例化需要 native Player 的普通 descriptor，而参考四端使用
static descriptor。

本轮已修复：

- `get/setDefaultSyncActive` 改为 static；
- `get/setDefaultTransformOrder` 改为 static，并移除 getter 的 `const`；
- 单元翻译单元加入四个 `!std::is_member_function_pointer_v` 静态断言；
- Player 表面 TSV 将 #1/#2 标为 `STATIC_RW_PROPERTY`。

这使 `&Class::method` 变为普通函数指针，ncbind 自动选择
`TJS_STATICMEMBER + noInstanceGetter`，与四端 descriptor 结构一致。正式构建仍因
当前机器缺少 CMake/Ninja/Emscripten 无法运行。

