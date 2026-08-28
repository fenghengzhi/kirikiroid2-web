# Player 实例 transformOrder / coordinate（四参考二进制，2026-08-26）

## 1. callback 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| getTransformOrder | `0x6C9568` | `0x591F98` | `0x10011C83C` | `0x11B0A0` |
| setTransformOrder | `0x6C96A4` | `0x59202C` | `0x10011C8E8` | `0x11B194` |
| getCoordinate | `0x6D6B50` | `0x598FC8` | `0x100125688` | `0x1248AC` |
| setCoordinate | `0x6B1D60` | `0x5817AC` | `0x100109170` | `0x10699C` |

transformOrder 位于 synthetic root：

- Android arm64 / iOS arm64：node `+0x54..+0x63`；
- Android armv7 / iOS armv7：node `+0x44..+0x53`。

coordinateMode：

- 64 位 node `+0x18`；
- 32 位 node `+0x10`。

## 2. getter

```cpp
Variant getTransformOrder() const {
    Array result = createFreshArray();
    for (int i = 0; i < 4; ++i)
        result.Items.push_back(Integer(root.transformOrder[i]));
    return result;
}
```

每次创建新 Array；不返回 class-static global，不共享 Items，不暴露 root 内存。
元素是四个 signed Int32 Variant，顺序固定 0..3。

## 3. instance setter

```cpp
void setTransformOrder(Variant input) {
    Dispatch *array = input.AsObjectNoAddRef();
    bool used[4] = {false, false, false, false};

    for (int i = 0; i < 4; ++i) {
        Variant elem;
        int v = 0;
        if (SUCCEEDED(array->PropGetByNum(
                TJS_MEMBERMUSTEXIST /*1024*/, i, &elem, array)))
            v = int(elem);

        if ((unsigned)v > 3 || used[v])
            throw L"illegul variable for transform order";

        if (root.transformOrder[i] != v) {
            root.transformOrder[i] = v;
            root.delta.dirty = true;
        }
        used[v] = true;
    }
}
```

与 class-static setter 的关键差异：

- static 版本的 fetch 失败立即抛
  `illegul size of transform order`；
- instance 版本的 fetch 失败把该元素当 0，不使用 size-error 文本。

因此：

- 三元素 `[1,2,3]` 在 index 3 fetch 失败后隐式得到 0，作为完整
  `[1,2,3,0]` 成功；
- 两个缺失项通常在第二个隐式 0 处触发 duplicate variable error；
- 额外 index >=4 完全忽略；
- 输入必须为 Object Variant；转换失败走普通 Variant conversion exception；
- 成功元素立即比较/写入，后续失败不 rollback 已写前缀和 dirty；
- 当前值相等的 slot 不 dirty，但 setter 仍继续验证后续 slot；
- negative 经 unsigned range check 拒绝；
- successful fetch 的普通 Variant→Int32 转换异常不改写成自定义文本；
- 错误文本保留原始 typo `illegul variable for transform order`。

## 4. coordinate

```cpp
int getCoordinate() const { return root.coordinateMode; }
void setCoordinate(int v) { root.coordinateMode = v; }
```

完整 signed Int32 原样保存，无验证、无 transform 重排、无 delta dirty。
它与 transformOrder 物理相邻但行为独立。

## 5. 本地与测试

本地 `PlayerCore.cpp` 已正确区分 static/instance 两套 setter，并保留 fetch-failure
隐式 0、incremental write 和 conditional dirty。coordinate 也是直接字段。

本轮新增单元用例锁定：

- 三元素 `[1,2,3]` 成功补 0；
- 两个缺失项的 duplicate throw 与已写前缀；
- coordinate 写入不 dirty。

四个 IDB 已统一命名和注释并保存。正式工具链缺失，测试未实际执行；状态为
`EVIDENCED_4_4`。

