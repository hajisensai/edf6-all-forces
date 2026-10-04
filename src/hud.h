// hud.cpp: the vehicle HUD (docs/hud-re.md), drawn from the follower gauge's call (subcarrier.cpp GaugeHook).
// Included by crew.h.
#pragma once
namespace crew {
bool InstallHud() noexcept;                         // at load: checks the draw and text functions it calls
void HudSee(unsigned char* vehicle) noexcept;       // from every vehicle's input hook (game thread): a readout's data
// Once a game frame (crew.cpp FrameTick, game thread): what HudSee gathered is published, whole, for the draw.
void HudPublish() noexcept;
// A carrier's panel (subcarrier.cpp fills it every draw from its game-thread copies): the hull, each deck part.
struct CarrierPanel {
    float hull,hullMax;
    int parts;
    struct Part { const char* name; float hp,max,repairSec; bool down; } part[4];
};
// From the follower gauge's draw (the draw thread): the published readouts and `count` carrier panels.
// `viewProj`, `ctx` and `viewport` as the gauge drawer 0x804300 gets them.
void HudDraw(const float* viewProj,void* ctx,const void* viewport,const CarrierPanel* panels,int count) noexcept;
}  // namespace crew
