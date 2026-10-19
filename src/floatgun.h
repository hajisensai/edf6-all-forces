// The float guns (floatgun.cpp; README 悬浮炮; docs/gravity-re.md §7; the user, 2026-10-20: 「我希望给 银之人（5代主线最后一关）增加可以
// 拾取怪死后掉落的武器/载具坏掉之后掉落的炮塔，在周围进行自动攻击（类似悬浮炮？）」): in EDF5's last mission (M099, ini FloatGuns 1; or
// every mission, 2) a killed colonist drops its gun and a wrecked vehicle leaves its main gun; the player walking up to one
// picks it up, and it floats round them, firing the rounds it fired (its own bullet class, damage, speed and rhythm, as the
// game had them for that enemy or vehicle) at the nearest enemy it sees, for FloatGunSec. Offline only.
#pragma once

namespace crew {
bool InstallFloatGuns() noexcept;                // at load: the code checks, the dropped gun's update hook
void FloatGunsMissionStart() noexcept;           // mission.cpp MissionStart: which mission this is; nothing kept from the last
void FloatGunNoteKill(const void* object) noexcept;   // damagestats.cpp: `object` was killed by this hit (the game thread's damage function)
void FloatGunTick() noexcept;                    // the shared tick (crew.cpp): drops, pick-ups, the floating guns' flight and fire
}  // namespace crew
