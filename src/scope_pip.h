// A soldier's scope as a picture in the picture (testhub #32, 2026-11-10: "步兵瞄准太抽象了 应当是类似于写实战术类游戏那样的
// 镜内放大效果（即倍镜外面是没缩放的，里面是缩放的样子）"; the maintainer: "32 改，应该改的都改", then of the first
// version's magnified centre: "画质不能接受"). InfantryScope=2: the view round the scope keeps the stock camera's
// unmagnified field, only the lens, a circle round the screen's centre, shows the scope's power, rendered by a second
// viewport of the engine's own (viewport 3, a TraceCamera at the lens's narrow field, scope_lens.cpp) at the lens's own
// resolution. docs/scope-pip-re.md has the reverse engineering and the frame's order.
//
// Pure arithmetic shared by the camera step (infantryscope.cpp), the lens view (scope_lens.cpp), the GPU pass
// (scope_pip_gpu.cpp) and the HUD (hud.cpp InfantryScope); tools/scope_pip_check.cpp checks it offline.
#pragma once
#include <cmath>

namespace crew {
namespace scopepip {
// InfantryScope's values (EDF6VehicleCrew.ini): 0 the stock zoom and crosshair, 1 the whole screen zoomed under a real
// optic's eyepiece (2026-10-11), 2 the picture in the picture (2026-11-10, the default).
enum class Mode : int { stock=0, fullScreen=1, pictureInPicture=2 };
constexpr int kModeLowest=0,kModeHighest=2,kModeDefault=2;
constexpr Mode ModeOf(int value) noexcept {
    return value<=0 ? Mode::stock : value==1 ? Mode::fullScreen : Mode::pictureInPicture;
}

constexpr float kBaseFov=0.785398163f;   // the stock camera's vertical FOV (docs/zoom-re.md §1: 0x1765A20, pi/4)
// The lens: its radius this share of the view's smaller side (a scope held at the eye in a tactical shooter covers
// about a third of the screen's height; the rest of the view stays the unmagnified world). The rim round it (the
// scope's tube and eyecup, drawn by the HUD) kRim of the radius wide; the lens's last kFeatherIn..1 darkens (vignette).
constexpr float kLensRound=0.30f;
constexpr float kRim=0.16f;
constexpr float kFeatherIn=0.90f;
constexpr int kFeathers=4;
constexpr float kFeatherAlpha[kFeathers]={0.08f,0.20f,0.40f,0.65f};
constexpr float FeatherFrom(int b) noexcept { return kFeatherIn+(1.0f-kFeatherIn)*static_cast<float>(b)/kFeathers; }
constexpr float FeatherTo(int b) noexcept { return kFeatherIn+(1.0f-kFeatherIn)*static_cast<float>(b+1)/kFeathers; }
// A magnification under this is no lens at all (the camera's own FOV is the scope's already: the frame the stock zoom
// still rendered, or a scope of 1x).
constexpr float kLeast=1.01f;

inline float LensRadius(float w,float h) noexcept { return kLensRound*(w<h ? w : h); }
inline float RimOut(float r) noexcept { return r*(1.0f+kRim); }

// The scope's vertical FOV: the stock camera's step writes pi/4 / zoom (zoom = the soldier's +0x1D0, the largest open
// scope's power; docs/zoom-re.md §1 0xF8906).
inline float ScopeFov(float zoom) noexcept { return zoom>0.0f ? kBaseFov/zoom : kBaseFov; }
// A perspective projection's vertical scale (its [1][1], cot(fovY/2)) for a vertical FOV.
inline float YScale(float fov) noexcept { return 1.0f/std::tan(0.5f*fov); }
// The focal ratio of a picture with vertical scale `yScale` against the scope's (vertical FOV `scopeFov`): how much more
// the stock zoomed camera magnifies than a frame rendered at `yScale`. ~1 for a frame the stock zoom rendered itself
// (no lens then). Not `zoom` itself: pi/4 / z is an angle, and tan(pi/8) / tan(pi/8 / z) is a little more than z. 0 when
// either is unusable.
inline float Magnification(float frameYScale,float scopeFov) noexcept {
    if(!(frameYScale>0.0f) || !std::isfinite(frameYScale) || !(scopeFov>0.0f) || !(scopeFov<3.1f))return 0.0f;
    const float m=YScale(scopeFov)/frameYScale;
    return std::isfinite(m) ? m : 0.0f;
}
// The px a unit of tan spans in the lens on a view `h` px high (the reticle's focal: its marks are angles at the
// scope's own field of view, whatever the frame round the lens was rendered at).
inline float LensFocal(float h,float scopeFov) noexcept { return 0.5f*h*YScale(scopeFov); }

// The lens view (scope_lens.cpp, docs/scope-pip-re.md §3.2): a square S x S render whose texture spans the lens's
// diameter (S = LensViewSide(r), the lens's px 1:1), at the vertical FOV that puts an angle where the stock zoom puts it
// in the lens: r * cot(phi / 2) = LensFocal(h, scopeFov). Independent of S. 0 when unusable.
inline int LensViewSide(float r) noexcept {
    if(!(r>0.0f) || !std::isfinite(r) || r>8192.0f)return 0;
    return static_cast<int>(std::ceil(2.0f*r));
}
inline float LensViewFov(float h,float r,float scopeFov) noexcept {
    if(!(h>0.0f) || !(r>0.0f) || !(scopeFov>0.0f) || !(scopeFov<3.1f))return 0.0f;
    const float phi=2.0f*std::atan(r*std::tan(0.5f*scopeFov)/(0.5f*h));
    return std::isfinite(phi) && phi>0.0f ? phi : 0.0f;
}
// The scope's FOV the lens really shows, from the lens view's own projection (its [5], `lensYScale`) as rendered: the
// HUD's reticle is cut to it (LensFocal(h, LensShownFov) = r * lensYScale), so the marks sit on the picture even when
// the frame is a few steps behind the camera. 0 when unusable.
inline float LensShownFov(float h,float r,float lensYScale) noexcept {
    if(!(h>0.0f) || !(r>0.0f) || !(lensYScale>0.0f) || !std::isfinite(lensYScale))return 0.0f;
    const float f=2.0f*std::atan(0.5f*h/(r*lensYScale));
    return std::isfinite(f) && f>0.0f ? f : 0.0f;
}
// How much the lens magnifies over the frame round it: the lens's focal in the frame's px (r * lensYScale) against the
// frame's own (0.5 * h * frameYScale). Under kLeast: no lens (the frame is the stock zoom's, or a scope of 1x).
inline float LensPower(float h,float r,float frameYScale,float lensYScale) noexcept {
    if(!(h>0.0f) || !(r>0.0f) || !(frameYScale>0.0f) || !(lensYScale>0.0f))return 0.0f;
    const float p=r*lensYScale/(0.5f*h*frameYScale);
    return std::isfinite(p) ? p : 0.0f;
}
// The lens views the engine's viewport kLensViewport (4 slots, 0..3; local split screens use 0 and 1).
constexpr unsigned kLensViewport=3;

// The camera step's FOV in the picture in the picture: the stock step wrote `written` = pi/4 / camZoom (its own write,
// 0xF8906: the same float division) and the frame keeps the unmagnified pi/4; anything else (another camera logic's
// value, the state 1 close-up's easing) is left as it is: 0. camZoom is the camera's own copy of the soldier's zoom
// (cam+0x410).
inline float Unzoomed(float written,float camZoom) noexcept {
    if(!(camZoom>=kLeast) || !std::isfinite(camZoom) || !std::isfinite(written))return 0.0f;
    const float stock=kBaseFov/camZoom;
    return std::fabs(written-stock)<=stock*1e-5f ? kBaseFov : 0.0f;
}

// When the camera may keep the frame unmagnified (the lens draws the power): the picture in the picture is wanted, the
// world's end is hooked (the lens pass has its place) and the pass has not refused this device's surface, and either the
// lens drew since this scope opened (lensAt >= openedAt: the pass works on this view) or the scope opened within
// kProbationMs (the first frames, before any pass ran). Otherwise the stock zoom stays and the HUD draws the
// whole-screen eyepiece (mode 1's picture).
// Not "the lens drew lately": a frame the stock zoom rendered never gets a lens (Magnification ~1), so once the camera
// zoomed again (a pause: the soldier's frame stops publishing, the camera step still runs, docs/hud-re.md §10, so the
// paused frames are the stock zoom's; a render hitch past kLensFreshMs) the lens could never draw again and the scope
// stayed mode 1's until it closed. A surface the pass
// cannot draw on refuses (lensRefused), which is what turns the picture in the picture off.
constexpr unsigned long long kLensFreshMs=250,kProbationMs=500;
constexpr bool LensDrew(unsigned long long now,unsigned long long lensAt) noexcept {
    return lensAt!=0 && now>=lensAt && now-lensAt<=kLensFreshMs;
}
constexpr bool KeepUnzoomed(bool wanted,bool hooked,bool refused,unsigned long long now,unsigned long long openedAt,
                            unsigned long long lensAt) noexcept {
    return wanted && hooked && !refused && openedAt!=0 && now>=openedAt &&
           ((lensAt!=0 && lensAt>=openedAt) || now-openedAt<=kProbationMs);
}

// The lens on a w x h target: centre and radius. LensSample() is where in the lens view's texture (0..1) the lens shows
// at (x, y): the texture spans the lens's diameter (the GPU pass samples exactly this; scope_pip_gpu.cpp kShaders).
struct Lens { float cx,cy,r; };
inline Lens LensFor(float w,float h) noexcept { return Lens{0.5f*w,0.5f*h,LensRadius(w,h)}; }
inline bool InLens(const Lens& l,float x,float y) noexcept {
    const float dx=x-l.cx,dy=y-l.cy;
    return dx*dx+dy*dy<=l.r*l.r;
}
inline void LensSample(const Lens& l,float x,float y,float* u,float* v) noexcept {
    *u=0.5f+(x-l.cx)/(2.0f*l.r);*v=0.5f+(y-l.cy)/(2.0f*l.r);
}
// The lens's bounding square clipped to the target (the GPU pass's scissor), integer px: left, top, right, bottom.
inline void Scissor(const Lens& l,float w,float h,long* left,long* top,long* right,long* bottom) noexcept {
    auto clamp=[](float v,float hi) noexcept { return v<0.0f ? 0.0f : v>hi ? hi : v; };
    *left=static_cast<long>(std::floor(clamp(l.cx-l.r,w)));*top=static_cast<long>(std::floor(clamp(l.cy-l.r,h)));
    *right=static_cast<long>(std::ceil(clamp(l.cx+l.r,w)));*bottom=static_cast<long>(std::ceil(clamp(l.cy+l.r,h)));
}
}  // namespace scopepip
}  // namespace crew
