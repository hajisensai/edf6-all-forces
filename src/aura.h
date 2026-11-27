// The aura vehicles (aura.cpp; the rules src/aura_logic.h; docs/feature-aura-vehicles.md): the command, repair and ammo
// trucks (tools/make_aura_vehicles.py), each told by its model's marker bone. Online the host decides which auras stand
// and sends them (AFAU, state stream auraVehicles); every machine applies them.
#pragma once
#include "aura_logic.h"
#include "plugin_net_logic.h"

namespace crew {
bool InstallAura() noexcept;                       // the signatures (plugin.cpp, at load): off when one is not as expected
void AuraVehicleFrame(unsigned char* vehicle) noexcept;   // each vehicle's frame (crew.cpp): an aura truck seen
void AuraTick() noexcept;                          // once a frame (crew.cpp SharedFrameTick): the auras applied
void ResetAura() noexcept;                         // a new mission (mission.cpp)
plugin_net::Channel AuraNetChannel() noexcept;     // support_net.cpp registers it

// The HUD's view (hud.cpp AuraHud, the draw thread): each standing aura's edge on the ground near the player, and what
// reaches the local player (on foot or its vehicle).
constexpr int kAuraRing=48;
struct AuraRingView {
    aura::Kind kind;
    int points;
    float pt[kAuraRing][3];
};
struct AuraView {
    int rings;
    AuraRingView ring[aura::kMaxSources];
    aura::Effect here;
};
bool AuraViewNow(AuraView* out) noexcept;
}  // namespace crew
