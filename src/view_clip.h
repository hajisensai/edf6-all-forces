// The view distance's clip planes (view.cpp), kept apart so tools/view_clip_check.cpp runs them offline.
// The game draws with two passes (docs/view-distance-re.md): the near one from 0.1 m to FarClipZ (env+0x1A0, camera
// +0x2C; 1000 m in every mission) draws the nodes with mask bit25|bit27 (vehicles, soldiers, enemies, the near
// ground); the far one from DistantViewNearClipZ (+0x1A4 / +0x30; 500 m stock, no MAE sets it) to DistantViewFarClipZ
// (+0x1A8 / +0x34; 20000) draws only the far-render nodes (bit26): the far-only scenery, e.g. the horizon's mountain
// ring and far ground of every map (NW_HENDEN's ig_FarMt_Henden_Out rises from 1500 m out), the sky dome of the
// simulator missions (ev609_VirtualDome, a FarEventObject, radius 10 km).
// A longer view distance raises only the near pass's end (and the far pass's end, so it is never nearer than the
// near one's). The far pass's start stays where the mission put it: far-only scenery is drawn by nothing else, so a
// start pushed out to the new near end clips every far-only mesh nearer than it (2026-10-06, the user in the
// simulator mission on NW_HENDEN with ViewDistance 3000: the far pass started at 2500 m, the mountain ring's slopes
// nearer than that were cut away and its upper part hung in the sky as a band).
#pragma once

namespace viewclip {

// One clip set in the game's order: the near pass's far clip, the far pass's start and end (env +0x1A0/+0x1A4/+0x1A8,
// camera +0x2C/+0x30/+0x34).
struct Clip { float farClip; float distantNear; float distantFar; };

// `c` with the near pass reaching `want` (never lowered); true when it changed. The far pass's start is never touched.
inline bool Raise(Clip& c,float want) noexcept {
    if(!(c.farClip>0.0f) || c.farClip>=want)return false;
    c.farClip=want;
    if(c.distantFar<want)c.distantFar=want;
    return true;
}

// True when the far pass draws a far-only node `d` metres down the view.
inline bool FarPassDraws(const Clip& c,float d) noexcept { return d>=c.distantNear && d<=c.distantFar; }

}  // namespace viewclip
