// The supports' warnings on the ground (support_warning_logic.h; the user, 2026-10-26: 「支援要有攻击警告红圈，直升机等没有
// 红圈的可以给出路线预览图之类的」): a strike's red outline (the area its rounds reach) and a flown or driven support's cyan
// route, on every machine (each puts its own: a barrage's and the ultimate's where they start here, a deployment's where its
// plan is applied here), until it is over; and the map's preview of the support armed on its bar under the pointer, before
// it is called (this machine's map only). hud.cpp draws them on the view (outside the map: the outlines alone) and on the
// map (with their words).
#pragma once
#include <cstdint>

namespace crew {
namespace support_net { struct Plan; }
// A warning's look: a strike's area (red), a route (cyan, an arrow at its end), a ring where a support circles (cyan).
namespace warning { enum class Look : unsigned char { strike, route, orbit }; }

// Game thread.
void WarnBarrage(int kind,const float* target,const float* heading,int endFrame) noexcept;   // barrage.cpp, at its start
void WarnUltimate(const float* at,unsigned long long ms) noexcept;                           // ultimate.cpp, at its start
void WarnDeployment(const support_net::Plan& plan) noexcept;                                  // support_dispatch.cpp, applied
// The map's preview (mapcmd.cpp, each frame a support is armed and the pointer is on the ground): catalog entry `catalog`
// at `target`; -1 none. Kept while asked for again within a moment.
void WarnPreview(int catalog,const float* target) noexcept;
void SupportWarningTick() noexcept;    // once a frame (crew.cpp SharedFrameTick): over ones dropped, time left counted
void ResetSupportWarnings() noexcept;  // a new mission (mission.cpp)

// Draw thread (hud.cpp): a copy of each warning shown.
constexpr int kWarnPoints=64;
struct WarningView {
    warning::Look look;
    bool closed;            // a ring / an area (the last point joins the first); else a route (its end an arrow)
    bool preview;           // the map's preview (dashed, fainter)
    int points;
    float pt[kWarnPoints][3];
    float label[3];         // where its words go
    wchar_t text[40];       // its name and the time left ("火箭齐射 12s")
};
constexpr int kWarningViews=17;
int SupportWarningViews(WarningView* out,int most) noexcept;
}  // namespace crew
