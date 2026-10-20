// The float guns (floatgun.cpp; README 悬浮炮; docs/gravity-re.md §7; the user, 2026-10-20: 「我希望给 银之人（5代主线最后一关）增加可以
// 拾取怪死后掉落的武器/载具坏掉之后掉落的炮塔，在周围进行自动攻击（类似悬浮炮？）」): in EDF5's last mission (M099, ini FloatGuns 1; or
// every mission, 2) a killed colonist drops its gun; the player walking up to one picks it up, and it floats round them,
// firing the rounds it fired (its own bullet class, damage, speed and rhythm, as the game had them for that enemy) at the
// nearest enemy it sees, for FloatGunSec. A wrecked vehicle leaves nothing (the user, 2026-10-20: 「载具炸了以后还是个整体其实」:
// the game keeps the wreck whole; the gravity gun picks that up).
// Online (the user, 2026-10-20: 「只在离线时生效…不可接受」) the guns are the host's to give (gravity_net_protocol.h): every
// machine shows each gun where it lies and round whoever has it, and fires it; its damage counts on its owner's machine alone.
#pragma once
#include "command_net.h"
#include <cstddef>
#include <cstdint>

namespace crew {
bool InstallFloatGuns() noexcept;                // at load: the code checks, the dropped gun's update hook
void FloatGunsMissionStart() noexcept;           // mission.cpp MissionStart: which mission this is; nothing kept from the last
void FloatGunTick() noexcept;                    // the shared tick (crew.cpp): deaths, drops, pick-ups, the guns' flight and fire
// support_net.cpp's transport (CommandContext and its one poll).
void UpdateFloatGunNetwork(const CommandNetworkContext& context,std::uint64_t now) noexcept;
bool ReceiveFloatGunNetwork(std::uint32_t peer,const char* authenticatedPuid,const void* bytes,std::size_t size,
                            std::uint64_t now) noexcept;
void ResetFloatGunNetwork() noexcept;
}  // namespace crew
