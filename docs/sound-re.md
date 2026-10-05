# 声音系统逆向（EDF.dll TimeDateStamp 0x678CCB46）

插件喷气机音效（`src/jetsound.cpp`）和喷气机导弹音效（`pylib/vcobjects.py` 的 `jet_guns`）用到的部分。可信度 H = 读代码确认，M = 推断，L = 猜测。

## 1. 中间件

- 静态链接 CRI ADX2：`CRI AtomEx/PCx64 Ver.2.22.5`、`CRI Atom/PCx64 Ver.2.22.89`；初始化 `0x7AE100`，加载 `APP:/SOUND/ADX/TIKYUUX.ACF`、`TIKYUUX_SE.ACB`（常驻）、`SOUND/SEPRESET.SGO`（`0x7B0690`）。(H)
- **游戏不用 ADX2 的 3D 定位**：游戏代码直接调用的 146 个 CRI 函数里没有 3D source / listener / velocity。它每帧自己按最近的监听者算音量（超过参考距离后 1/r）、声像角度和内部距离（`0x7ACD20`）。所以引擎里**没有多普勒**，监听者也只有位置。(H)

## 2. 游戏的声音封装（插件直接调用）

全局：`SeManager* = *(EDF+0x20B2950)`，`SoundSystem* = *(EDF+0x20B2948)`（`0x705950` 每帧以它调用 `0x7AF3E0`）。(H)

**SePreset**（0x80 字节，`0x7B3200` 初始化）：+0x00 位置；+0x10 内部半径；+0x14 参考距离（超过后音量 = ref/dist）；+0x18 剔除倍数（开播时距离 > ref×倍数就不播，只在开播时判断）；+0x20 起 5 个地面材质槽 `{bank*, cueIdx}`；+0x70 音高；+0x74 音量。SEPRESET 条目 `[cue, vol, pitch, interior, ref, cullMul]`（`0x7B3840` 写入）。(H)

**SoundHandle**（16 字节，零初始化）：`{ctrl*, inst*}`。inst（0x70）：+0 脏标记，+0x28 音高，+0x2C 音量，+0x40 位置。系统链表另持一份引用：句柄析构不停声，循环声必须显式停止。(H)

| 功能 | 地址与签名 | 可信度 |
|---|---|---|
| 按名字填 preset | `bool 0x7B16F0(SeManager*, SePreset* out, const wchar_t* name, bool)`；名字不在 SEPRESET 里时填默认空 preset（bank = null）并返回 false，无报错路径 | H |
| 按名字播放 | `bool 0x7B2A80(SeManager*, const Vec4* pos, const wchar_t* name, SoundHandle* out)` | H |
| 按 preset 播放 | `0x7B4510(SePreset*, const Vec4* pos, SoundHandle* out)`；bank 为 null 时直接返回 | H |
| 设位置 / 音高 / 音量 | `0x7A8C20(h, const float[3])` / `0x7A8BF0(h, float)` / `0x7A8C70(h, float)`：只写 inst 并置脏，下一帧 `0x7AF3E0` 统一生效 | H |
| 音高换算 | cents = clamp(p×0.5, 0, 1)×2400 − 1200：p = 1 不变，0..2 = 低/高一个八度 | H |
| 是否在播 | `bool 0x7A8A20(h)` | H |
| 停止 / 释放 | `0x7A8CD0(h, int fadeFrames)`（≤0 立即）/ `0x7A8730(h)` | H |

**监听者**：`0x7AF3E0` 每帧对 4 个相机（`0x1195BE0(*(EDF+0x20B2958), i)`）取矩阵 +0x220..+0x250，位置（+0x250 行）存进 `SoundSystem+0x58` 的数组（数量 +0x68，每项 0x50 字节，位置在最前，其后是逆矩阵）。监听者 0 是玩家相机。(H)

原版用法：导弹 `MissileBullet01`（`0x26A880`）点火时 `0x7B4510`，每帧 `0x7A8C20`，结束时 `0x7A8CD0(h, 4)`；空袭机 `BombingPlane` 用 `0x7B2A80(L"空爆機体通過")`。(H)

## 3. 载具的声音

- `V506_HELI.SGO`：`game_sound = ['app:/sound/adx/tikyuu4_vehicle_heli302.acb']`，`heli_se_table` 16 项（SEPRESET 名）：[0] 旋翼启动到怠速，[1] 旋翼主循环，其余为着陆、受损、搭乘、燃料等。HeliBase vtable 第 54 项 `0x653AE0`（每帧由第 60 项调用）按旋翼状态驱动这些循环声，音高/音量随旋翼转速而不是速度。(H/M)
- 插件喷气机由 506 改出：以前一直在放直升机旋翼声。现在生成时把 [0]、[1] 改成不存在的名字 `EDF6VC_SILENT`（preset 为空，播放直接返回），其余各项不动。(H)
- `BOMBER401/501.SGO` 没有声音键；原版没有喷气涡扇循环声。

## 4. 现成音效（`TIKYUUX_SE.ACB`，常驻）

- 引擎：`vhc_transport_boosterMove`（循环，SEPRESET「輸送機ブースター稼働」`[0.89, 1, 5, 100, 10]`）——插件的喷气机引擎声。
- 导弹：发射 `weapon_KUBAKU_missile_shot`；飞行循环 `weapon_KUBAKUBallisticMissle01_go`。原版 506 导弹是 `weapon_VHC_heli302_missile` / `weapon_VHC_heli302_missileGo`（1.6 秒，非循环）。
- 武器的声音写法（DSGO）：`FireSe` 和 `Ammo_CustomParameter[11]`（MissileBullet01 的飞行声）都是 `[0, cue, 音量, 音高, 内部距离, 参考距离]`。

## 5. 插件做法（`src/jetsound.cpp` + `src/jetaudio.cpp`）

最初借游戏的推进器循环声（`輸送機ブースター稼働`）当引擎声，实测听感不对（2026-10-04 用户反馈），改为插件自己出声：

- **输出**：插件自建 XAudio2 引擎（默认输出设备），每架喷气机两个循环单声道 voice：轰鸣（尾喷的宽带噪声，滤波噪声合成：<90 Hz 低吼 + <450 Hz 主体 + 1.8 kHz 嘶声，缓慢起伏）与涡轮尖啸（1400 Hz 叶片通过频率及其谐波 + 470 Hz 轴频 + 叶片频率附近的窄带噪声）。两段都是 3 秒无缝循环（首尾等功率交叉淡化），插件启动时合成一次。
- **自定义录音**：DLL 旁边放 `EDF6VehicleCrew_jet.wav`（16 位 PCM，单声道或立体声，任意采样率）就用它当轰鸣、不再合成尖啸。
- **混音（每帧，游戏线程）**：
  - 监听者 = 声音系统的监听者 0（`SoundSystem+0x58` 的第一项：相机位置，后接相机世界矩阵之逆的 4 行；相机坐标 L = p.x·R1 + p.y·R2 + p.z·R3 + R4，+x 是相机左方）。左右声道等功率分配（右 = −L.x/距离，扩散 0.75）。
  - 距离：轰鸣 180 m 内满音量、之外 1/r；尖啸 90 m 内满音量、之外 (90/r)^1.3；低通截止频率从 16 kHz（近）按几何插值降到 1.2 kHz（2500 m 外）。
  - 方向性：尖啸主要向机头方向传播（×0.35..1，按机头与指向相机方向的点积），轰鸣主要向机尾（×0.6..1）。
  - 转速 = 速度 / 150 m/s：轰鸣音量 0.45..1、播放速率 0.85..1.15；尖啸音量 0.2..1、播放速率 0.75..1.3；再乘多普勒比 (c − v_听者·û)/(c − v_源·û)（c = 340，速度沿视线分量限制在 0.8c 内；相机速度由监听者位置帧差得到，单帧移动超过 60 m 视为镜头切换不计）。
  - 主音量 = 游戏的主音量 × 效果音量（`GameStatus+0x2E8` × `+0x2F0`）× `JetSoundVolume`。
- **暂停**：游戏每帧经 `Beat` 报一次心跳；守护线程（每 25 ms 检查）发现 150 ms 没有心跳（暂停、读盘、菜单）就把主 voice 静音，心跳恢复时恢复音量。
- **生命周期**：喷气机 200 ms（游戏时间）没有更新就销毁它的 voice；插件或 `JetSound` 关掉时全部销毁；新任务开始时全部销毁。

## 6. 待实机验证

- 旋翼静音：不存在的 preset 名在 506 的声音表里不报错（代码上无报错路径）。
- 合成引擎声的听感与音量；多普勒与引擎音高变化的手感；相机 +x 是左方（左右声道方向）。
- 导弹飞行循环声在弹体结束时淡出。

## 7. 锁定提示音（NPC 的不再传到玩家耳边）

- 每把武器初始化（0x68A920）时无条件按名字加载两个 SEPRESET：0x68E90B「ロックオンサーチ」→ weapon+0xCC0，0x68E928「ロックオン完了」→ weapon+0xD40（`weapon_Common_lockonSearch` / `_lockonLocked`，参考距离 10000 m，10 km 内满音量）。武器 SGO 里没有锁定音键。(H)
- 锁定 tick 0x6963A0 播放它们（0x69656F / 0x6965D9 → 0x7B4510），条件是武器持有者（weapon+0x120）没有 +0x1A 第 3 位（对象在跑 AI：原版 NPC 士兵有，玩家没有）。载具没有这一位，所以 NPC 驾驶的载具锁定时照样在玩家耳边响。(H/M)
- 插件（`src/jetsound.cpp` LockSound，每台载具每帧）：座位上坐的不是本机玩家时，把该座位所有武器这两个 preset 的 `{bank, cueIdx}`（+0x20）清零（0x7B4510 遇 bank 为空直接返回）；本机玩家坐的座位写回（全局同一个 cue，第一次见到时记下）。
