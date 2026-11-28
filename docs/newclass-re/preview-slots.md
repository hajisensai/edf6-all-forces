# 新兵种：手持槽 / 背包槽与菜单 3D 预览（2026-11-28 切兵种闪退）

EDF.dll Steam 0x678CCB46，地址均为 RVA。置信度：**H** = 汇编核过或游戏数据实际导出；**M** = 由多处证据推出；**L** = 未核的猜测。

## 0. 结论

- 闪退根因 **H**：菜单 3D 预览按 **菜单 SGO** 的 `soldier_weapon_slot` 槽位表，把对应槽的武器行的 `animation_model` 挂到手上，且不检查这个键是否存在。新兵种的菜单 SGO 是游侠的原样拷贝，表为 `[[0, 1], 'weapon', 0, 0]`，载具手的槽 0（载具槽，类别 400）因此被当成手持槽。槽 0 里的 `ZW_V1..V9` / `ZW_VehicleNone` 都是 `aSupportNone` 的克隆，没有 `animation_model`，于是在预览线程上抛 `InvalidVariantException`（E06D7363）。
- 原版契约 **H**：手持表只放 support = 0 的槽，`soldier_sub_weapon_slot` 只放 support = 1 的槽，四个原版兵种的菜单和战斗 SGO 都是这样。原版的 `aSupportNone` / `eVehicleNone` 永远不会出现在手持槽里，所以原版从来不触发这个问题。
- 修法（数据 / 契约，`tools/make_zw_classes.py` 的 `SoldierClass.hands / subs / menu_hands`）：
  - 载具手：战斗手持表 `[1]`，背包表 `[0]`，菜单槽位表 `[1, 1, 1]`。
  - 刻印剑士：战斗手持表 `[0, 1]` 不变（近战要在手上，`runeblade_melee.cpp SlotInHand`），背包表 `[2]`（原来的 3 是悬空的），菜单槽位表 `[1, 1, 1]`。
  - 守卫在 `tests/zw_classes_test.py`：手持 / 背包表必须等于 `src/soldier_classes_logic.h kClasses` 中 support 0 / 1 的槽；菜单表列出的槽所在类别里不能有 none / vehicle 行；有游戏时，这些类别里每一行生成出来的 SGO 都要有 `animation_model`，并核对原版四兵种也满足这条契约。

## 1. 预览路径

调用栈（dump `EDF6.exe.173128.dmp`，预览工作线程）：`0x7B5BBB ← 0x93D2D7 ← 0x9424D6 → 0x93F910 → 0x940100 → 0x93AA00 → 0x6BF380 → 0x6BB890 _CxxThrowException`。**H**

- **`0x93F910`**（异步构建，第二个参数是 Desc 的拷贝）**H**
  - 置 ModelSlot+0x70 = 手数：1，Fencer（Desc+0x20 == 3）为 2，此时 +0x75 = 1。
  - 在 0x93FF30 把 Desc+0x54 的 int[6] 武器行号拷进 ModelSlot+0xA4。
  - 0x93FF5A 调 `0x940100(ModelSlot, Desc+0x50)`。Desc+0x50 是最后一次 msg 4 改的槽号：0x93B8AF 写 view+0x368 = 槽号，然后 weapons[slot] = id。
- **`0x940100`** **H**
  - 0x9401B7 从模型文档（ModelSlot+0x40）读 `soldier_weapon_slot`（字符串 0x17D29D0）。
  - 0x940233–0x94024A 计算 `esi = 槽号 % 3`，Fencer 用 `槽号 / 2`。
  - 对每个条目：`esi >= size(entry[0])` 就跳过（0x9403A4 → 0x94129E）；否则 `ebx = entry[0][esi]`，行号 = ModelSlot+0xA4[ebx]。
  - 用 `0xE1CF0` 取行，0x94068F 调 `0x93AA00`。
- **`0x93AA00`** → 0x93AD30 调 `0x6BF380`，查行文档的 `animation_model`，被插件的 `MenuModelLoad` 钩住（`src/room_preview.cpp`）。**H**
  - 键缺失时得到空 variant，`0x6BB890` 抛 InvalidVariantException。**H**
  - 插件钩子只在文档里有 `menu_animation_model` 时换键，不改变缺键时的行为。**H**
- 循环结束后，0x941310 不判空，直接解引用每只手的模型 +0x50。**M**（只读了汇编，没有在实机触发过。）
  - 所以菜单表要写满 3 项。只写 `[1]` 的话，编辑槽 0 时会整轮跳过，手上可能是旧的或空的。

### 1.1 菜单表的含义

`entry[0]` 的**位置**是「最近编辑的槽号 % 3」，**值**是要挂到手上的槽。**H**

原版的例子：
- 游侠 `[0, 1]`：编辑槽 0 / 1 时显示该槽；编辑槽 2 / 3（背包）时 2 % 3 = 2 越界跳过，3 % 3 = 0 显示槽 0。
- 空降兵 `[0, 1, 2]`。
- Fencer 有两只手，`[[1, 3], 'weapon_r', 0, '_r']` 和 `[[0, 2], 'weapon_l', 0, '_l']`，按槽号 / 2 取。

新兵种用 `[1, 1, 1]`：编辑哪个槽，手上都显示槽 1（载具手的手枪 / 刻印剑士的远程武器）。刻印剑士的近战槽不进菜单表，因为它的默认值 `ZW_MeleeNone` 是 aSupportNone 克隆，没有模型；而且所有真实近战行都还是 LOCKED。

## 2. 原版数据（CONFIG.SGO SoldierInit、P5xx 战斗 / 菜单 SGO，实际导出）**H**

| 兵种 | 各槽 support | 菜单 / 战斗手持 | 战斗背包 |
|---|---|---|---|
| 游侠 | 0, 0, 1, 1（默认 aSupportNone 在槽 3） | `[0, 1]` | `[2]`, `[3]` |
| 空降兵 | 0, 0, 1, 1 | `[0, 1]` | `[2]`, `[3]` |
| 重装（AR） | 0, 0, 0, 1, 1（eVehicleNone 在槽 4） | `[0, 1, 2]` | `[3]`, `[4]` |
| Fencer | 0, 0, 0, 0, 1, 1 | 右 `[1, 3]` / 左 `[0, 2]` | `[4]`, `[5]` |

修复前的新兵种：
- 菜单 SGO 逐字节等于游侠的。
- 战斗 SGO 是游侠 DSGO 只改了 `xgs_scene_object_class`，即手持 `[0, 1]`、背包 `[2]`、`[3]`。
- 载具手只有 2 个槽，刻印剑士只有 3 个槽，所以背包里的 3（以及载具手的 2）是悬空的。

## 3. 战斗是否同样崩

- 战斗按槽依次造武器（0x5A3F90 → 0x58F360）放进 human+0x1950。**H**
  - 武器初始化 0x68A920 调 `0x55A30(doc, "animation_model")`，结果与 r12 = -1 比较，相等就 `je 0x68C362`，也就是**缺键时跳过**。0x68AE26–0x68AE39 这一段 **H**。
  - 0x68DA8A 的第二次读取同样受 `[rbp-0x48] == -1` 保护。**H**
  - 预载 0x7A5400 在 0x7A5E69 先检查 variant 类型。**M**
  - 原版每局游侠都会造出 aSupportNone，佐证了战斗造武器不会因为缺 `animation_model` 而崩。**H**
- 背包输入 0x5A1D70 在 0x5A1E73 用 +0x1960 对背包下标做边界检查，悬空的下标取到 null，安全。**H**
- 修复前战斗里不会崩，但不对：
  - 载具手手持表含槽 0，开局拿的可能是载具附件，也就是空手。**M**
  - 刻印剑士背包引用了不存在的槽 3。**H**（数据层面）
  - 修复后两项都按契约对齐。
- 实际影响仍待实机：日志里没有用兵种 4 / 5 打过任务。

## 4. 未在实机验证的点

- 修复后，用兵种 4 / 5 打开整备 / 预览，逐个切换槽不闪退，手上显示槽 1 的武器。
- 载具手战斗开局手持手枪；载具槽（背包 [0]）的背包键行为与载具出动不受影响（载具出动走存档行 `wequip`，不读 +0x1950 的下标，**M**）。
- 刻印剑士战斗中近战 / 远程切换、`SlotInHand` 仍是槽 0 / 1；背包键对应槽 2。
- 刻印剑士默认 `ZW_MeleeNone` 在手上时，战斗里显示为空手（与原版 aSupportNone 一样跳过模型，**M**）。
- 第 3、4 套外观的菜单 SGO 内嵌了 MAB 原始块，现在改用 `sgo.write()` 写（16 字节对齐，与原版的 0x280 一致）；读回的值除 `soldier_weapon_slot` 外与原版逐项相等（测试已核）。游戏能否加载这种布局，有 `vcobjects` 派生载具（同一个写法）的先例，**M**。
