# 载具武器瞄准线（红线）逆向

2026-10-03。目标：NPC 驾驶的直升机 / 喷气机不显示从炮口向前的红色瞄准线，玩家自己坐上去时照旧显示。

## 谁创建它（H）

- `Weapon_VehicleShoot` 构造函数 `0x6B3250`。
  - 工厂 `0x79D370` → `0x79D393` 会走到这里。
  - `Weapon_VehicleMaser`（`0x6B08A0`）、`Weapon_VehicleRailGun`（`0x6B31C0`）、`Weapon_VehicleSwingShoot`（`0x6B3AA0`）也都经过它。
- 构造函数读 `custom_parameter`（`weapon+0x1A0` 的 variant 数组）：
  - **第 0 项**取整（`0x6B3398`–`0x6B33E4`），大于 0 才 `new` 一个 0x140 字节的 `WeaponAimLine`：
    - 构造函数 `0x687550(line, 段数)`；
    - vtable `0x17E2418`；
    - 段数上限 256，存在 `line+0x130`。
  - 第 1 项是 float，存 `weapon+0x1640`。
  - 第 2 项非零时置 `weapon+0x1644`。
- 例如 V_506HELI_GATLING01_L 的 `custom_parameter = [60.0, 1.0, 1.0]`，就是 60 段。
- 存放位置：
  - `weapon+0x1630` 是共享引用控制块 `{strong, weak, obj}`；
  - `weapon+0x1638` 是裸指针。

## 谁画它（H）

- 武器每帧函数 `0x6B3660`：`weapon+0x1638` 为空就跳过（`0x6B3675`），否则在 `0x6B3822` 调 `0x6899F0(line, …)`。
- `0x6899F0` 先把 `line+0x88` 清 0（本帧顶点数）。如果 `line+0x130`（段数）为 0，就直接跳到结尾（`0x689A54` → `0x689BFA`），**不生成任何线段**。
- `0x6B0F60` 是 `Weapon_VehicleShoot` 的析构函数体（先释放瞄准线，再析构基类），**不能**拿来单独释放瞄准线。

## 做法（src/crew.cpp `AimLines`）

1. 每辆载具每帧（输入钩子里），遍历每个座位的武器（`seat+0xC8` 是 holder 数组、`+0xD8` 是数量，`holder+0x10` 是武器）。
2. 取 `weapon+0x1638`，确认它的 vtable 是 `0x17E2418`。
3. 按座位上的乘员处理：
   - NPC（RideAi 的 dummy）：记下段数，然后把 `line+0x130` 写成 0；
   - 玩家：把记下的段数写回。
4. 不改任何 SGO，也不改共享文件，玩家自己的瞄准线不受影响。
