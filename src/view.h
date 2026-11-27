// The camera's view distance and depth (view.cpp, viewdiag.cpp).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include "config.h"
#include "world.h"

namespace crew {
void ViewDiagTick() noexcept;     // viewdiag.cpp: once a frame, logs (VIEWDIAG) when the view's eye leaves the map or the near pass's clip breaks
bool InstallUmbraWatch() noexcept;   // viewdiag.cpp: logs (VIEWDIAG umbra) a box handed to Umbra out of any map's reach
// view.cpp: while the map view is open (map.cpp, game thread, each frame) the cameras' far clip raised to `farClip` and
// their near clip to `nearClip` (the depth range for a view from up to 3 km); off: the values from before put back.
void ViewMapClip(bool on,float farClip,float nearClip) noexcept;
// m: how far the near camera draws (view.cpp: the mission's FarClipZ, 1000 m in every mission, raised to ini
// ViewDistance); past it only what has the far-render bit is drawn (jet_spawn.cpp FarRender).
constexpr float kStockFarClip=1000.0f;
// ViewDistance -1 (the default since 2026-10-10, the user: "改一下上限，改成无限"): no limit of the plugin's -- the near
// camera reaches past anything the world holds: its diagonal (2.83 x WorldHalf) and no nearer than the stock far pass's
// end (20000 m). view.cpp also never sets it nearer than the mission's own far pass end.
constexpr float kUnlimitedView=-1.0f,kStockDistantFar=20000.0f;
inline float UnlimitedViewDistance() noexcept { const float d=2.83f*WorldHalf();return d>kStockDistantFar ? d : kStockDistantFar; }
// m: the near camera's far clip the plugin asks for (0: none, the mission's own).
inline float ViewDistanceWanted() noexcept { return Cfg().viewDistance<0.0f ? UnlimitedViewDistance() : Cfg().viewDistance; }
inline float NearDrawDistance() noexcept { const float v=ViewDistanceWanted();return v>kStockFarClip ? v : kStockFarClip; }
bool InstallViewDepth() noexcept; bool ViewDepthReady() noexcept; void ViewWriteNear(void* camera,float value) noexcept;
// Release a near plane written through ViewWriteNear: while `written` is still the camera's value, the native baseline
// the shared clip lease recorded before any plugin write comes back. A newer value of another owner (the map) is left.
// Never a private snapshot of the camera: that may hold another layer's temporary value (the map's 0.5..5 m).
void ViewReturnNear(void* camera,float written) noexcept;
}  // namespace crew
