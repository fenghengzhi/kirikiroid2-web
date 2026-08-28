# Player camera-node phase 四参考二进制联合恢复

日期：2026-08-27

## 1. 四端函数

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6BAE08` | 164 |
| Android armv7 | `0x587748` | 154 |
| iOS arm64 | `0x1001108C4` | 151 |
| iOS armv7 | `0x10E048` | 149 |

四端均fresh decompile并完整读取disassembly，cursor全部 `done=true`。该helper位于visibility之后、
shape-AABB之前；没有Variant owner、分配、logger或外部callback，只有raw-label lookup与atan2。

## 2. first-winner与hasCamera

函数入口无条件 `hasCamera=false`，即便只有root也覆盖上一frame。随后从index 1按physical order扫描，
第一个 `nodeType==5 && accumulated.active`的node获胜，立即写hasCamera=true并在完成全部camera工作后
break。active slot done、drawFlag、source.valid和preview都不是camera-node gate。

没有命中时camera offsets、FOV、positions、target与angle全部保留旧值，只有hasCamera变false。

## 3. target/focus与offset

active slot `cameraTarget`按ttstr backing pointer判定；本仓库 `IsEmpty()`正是 `Ptr==nullptr`，与四端
load一致。backed target调用raw-label resolver；miss/null则：

- `targetNode=null`，用于决定是否覆盖persistent camera target；
- `focusNode=&cameraNode`，用于计算camera offset。

focus坐标来自vertex phase的 `vertexPosX/Y/Z`，root基准来自synthetic root同组字段：

```text
dxFloat = float(-(focus.x - root.x))
dyFloat = float(-(focus.y + zFactor*focus.z
                  - root.y - zFactor*root.z))

offsetX = float(int(rootPlayer.affine.m11*dxFloat
                  + rootPlayer.affine.m12*dyFloat + 0.5))
offsetY = float(int(rootPlayer.affine.m21*dxFloat
                  + rootPlayer.affine.m22*dyFloat + 0.5))
```

两个delta先各自缩窄float，再提升参与root Player affine；结果加0.5后向零转signed int，再转float。
负值因此不是round-to-nearest。rootPlayer可以不同于this，nested child使用整条root affine链的owner。

offset无条件更新，不受cameraActive影响。NaN/out-of-range的FP-to-int为ISA机器边界；portable helper
只对正常finite域复现，不应随意换`round`。

## 4. cameraActive publication

只有cameraActive为true时继续：

1. FOV从获胜node复制；
2. camera position从node vertex X/Y/Z复制；
3. 只有target lookup命中才覆盖persistent target X/Y/Z；empty/miss保留上一frame值；
4. 计算 `atan2(cameraZ-targetZ, cameraX-targetX)`；
5. 角度变换为 `radians * -57.2957795 + 90.0`，以while循环归一化到 `[0,360)`。

四端都先把raw atan2结果临时写入cameraAngle槽，再写归一化结果；在无异步observer的普通C++路径
最终值等价。无限值/NaN可能让归一化loop表现异常，是native未防御浮点边界。

cameraActive=false时FOV/position/target/angle全部保持旧值，但offset与hasCamera仍按本frame更新。

## 5. 本地与ABI

本地helper的first-winner、backing target gate、focus fallback、float narrowing、root affine、+0.5 toward-
zero量化、cameraActive gate、target miss retention与angle循环逐项匹配，无需修改。

四端node stride/deque寻址不同；32位用VFP conversion，AArch64用FCVTZS。source-level不保存任何ABI
offset。四库已统一命名、注释、bookmark并保存。

## 6. 验证限制

coverage严格12列、duplicate-ID与`git diff --check`执行；正式CMake/Ninja/Emscripten工具链不可用，
不能声称unit/Web build通过。

