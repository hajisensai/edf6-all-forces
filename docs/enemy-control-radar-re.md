# 敌体控制与原生右上雷达的 observer 合同

只读核对的二进制：Steam `EDF.dll`，TimeDateStamp `0x678CCB46`。本次没有向游戏目录写入文件，也没有启动、驱动或附加游戏。下述原生结论由 `tests/enemy_control_radar_native_audit.py` 在真实文件上验证；运行时显示与联机仍需另行实机验收。

## 原生中心、旋转和敌我来源

`ui::HUiHudRader` 的 vtable `0x17FCF10` slot 2 是 `0x82B4D0`。更新从**本 HUD** 的 `+0x778/+0x780` 弱引用锁定对象，再由 `SceneObject` dynamic_cast 成 `SoldierBase`（`0x82B541..0x82B59D`），放到 RSI。没有从全局 P1 取中心。

- `0x82B671/+0x82B675` 读这个 Soldier 的 `+0x70/+0x80` 朝向轴；有有效乘车引用时改取 `vehicle+0x208` 的原生 basis。`0x4E100` 输出 `pitch=-atan2(y,hypot(x,z))` 与 `yaw=atan2(x,z)`。pitch 超过 `+.75` 时用 `+0x70`，小于 `-.75` 时用其反向，避免近垂直前向丢失 yaw。
- `0x82B767..0x82B79E` 组建本次栈上 `RSP+0x60` 的 float4x4 旋转矩阵。`0x82B7A5` 直接读 **source Soldier `+0x90`** 到栈 `RSP+0x50` 中心。因此 source 留在原地、敌体飞远时，原生雷达确实仍围绕 source。
- 所有普通点、名字、目的标记、线段和地图边界均继续接收这同一对 center/matrix。`0x82C690` 先做目标位置减 center，再沿原生 `HUD+0x8B0` 缩放及原生距离截断；布局与分屏上下文仍是原来的 HUD。
- 两次敌对集合遍历在 `0x82B89A/0x82BB80` 直接读 source `+0x314`，交给 `0x5E0F20` 按原生阵营关系表枚举。它们使用原生红色 `(1,0,0,1)`。
- 友方一组在 `0x82BB42` 硬编码 team 2，走 `0x82B220` 原生单阵营表，使用青色 `(0,.75,1,1)`。现有 PVP wrapper 已按 HUD owner 阵营修正此处。team 4 白色、team 5 黄色以及其它标记沿原版处理。

无 PVP 时，source 通常 team 2、所控敌体通常 team 1，故仅移动中心还会保留错误的敌我视角；必须连同这三处 observer team 输入一起跟随受控对象的真实 team。PVP 下受控体跟随 owner 时，二者自然相同。

## 实现边界

`src/enemy_control_radar.h/.cpp` 接收完整、纯值的 `View[]` 发布集；只有 game thread 的 core 能读 local active lease 的真实 target `+0x90/+0x70/+0x80/+0x314`。每行含 source 的 object/control **不透明身份键**，不把 target 指针交给 render。退出、reset、pending、restoring、远端控制不发布行。core 清空集会立即取消跟随；发布超过 500 ms 则回退原参。非法浮点、重复 owner、无效朝向和 0..6 之外的 team（包括 -1）均不进入原生枚举。

中心 hook 只覆盖 `0x82B7A5` 的两条完整原指令（13 bytes）：先重放 source load/store，再匹配本 HUD 的 weak pair，把本次 native 栈 center/matrix 覆盖成发布的数值。没有写 source 或 target 的世界、物理、朝向字段，没有构造假 Soldier/GameObject，也没有移动所有标记的全局偏移。

另外两处六字节 `mov edx,[rsi+314]` 边界先重放原指令，再只替换 EDX。原生友方 wrapper `pvp_native.cpp::RadarFriendTeam` 优先问同一纯快照 resolver；没有受控快照时保留既有 PVP/普通玩家原路径。PVP wrapper 始终由 `InstallPvp()` 安装，不要求 PVP 模式开启。三个新 stub 均保存原 flags、全部 volatile GPR 和 XMM0..5，保持 native x64 栈对齐/影子空间，跳回原下一条指令；原生所有范围、缩放、旋转下游、形状、标记及队伍集合遍历都继续运行。

所有新 hook 安装前核对完整指令边界、原生 prologue/栈布局、R14 HUD 来源、heading 建立和投影输入合同；安装失败回滚已写边界。普通玩家、其它本地 HUD、object 地址复用但 weak control 不同、无快照、退出或过期均重放原参数。地图与第一人称的既有路径不改。

## 离线验证

- `tests/enemy_control_radar_native_audit.py <EDF.dll>`：真实 PE 只读验证 vtable/RTTI、source center 根因、heading 及垂直退化、所有 native helper/敌我输入/颜色、原生缩放与距离截断，以及全部生产安装指纹。
- `tests/enemy_control_radar_native_fixture.cpp`：直接 include 生产实现，分配 synthetic native image 并实际执行已安装的三个机器码边界。覆盖 source/target 相距数千米、两个本地 owner、真实 target team 1/2、heading 和原生垂直退化、原始 GPR/XMM/flags 保持、source/HUD 字节不变、无控/退出/过期/ABA/坏数据/错误 image 原参回退。
- `tests/pvp_native_fixture.cpp`：原有 PVP toggle、support owner、雷达、还原和 ABA 回归仍通过；其独立链接 seam 保持无敌体快照时的原行为。

这些检查证明生产机器码调用合同与离线行为，不构成游戏内雷达画面、手柄操作、分屏显示或双机联机验收。
