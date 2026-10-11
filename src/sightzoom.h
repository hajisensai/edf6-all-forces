// A vehicle sight's magnification (the user, 2026-10-08: a sight HUD on every vehicle and weapon with a sight, tanks
// and the gunship above all): a key (ini SightZoomKey, 'Z') or pad button (SightZoomButton, R3) steps it 1x -> 3x ->
// 6x -> 1x while the player sits at a vehicle's gun. How the game zooms (docs/zoom-re.md, H): the player's camera
// (CharacterGhostCamera, slot 4 0xF86A0) sets its vertical field of view every frame from its zoom (cam+0x410: the
// soldier's scope, 1 riding): cam+0x24 = (pi/4) / zoom, and builds the projection and the culling frustum from that;
// A scope requires a real model-authored optic anchor for the selected weapon. sightzoom.cpp sets cam+0x220 to
// that eye and the actual bore direction as well as changing FOV. It retracts its own matrix and FOV before the next
// original step, including the no-target branch. An armed vehicle alone is not proof of an installed optic. Arithmetic:
// tools/gunsight_check.cpp; production hook and seat lifecycle: tests/sightzoom_test.cpp.
#pragma once
#include <cmath>

namespace crew {
namespace sightzoom {
// The seat's actual fire-control surface, not the vehicle class or the fact that a player is aboard.
enum class Kind { none, optical, missile, rocket, indirect, sensor, flight, mech };
enum class Mask { none, round, sensor };
constexpr bool Magnifies(Kind k) noexcept { return k!=Kind::none && k!=Kind::indirect; }
// A magnified seat looks through its one physical optic (sightzoom.cpp SeatCapability needs the model-authored lens):
// the gun, the coaxial gun and a gun-launched missile or rocket picked on the same seat share that eyepiece, so they
// share its round field (2026-10-11: one sight for both triggers). Only an electronic sensor (an energy weapon's
// display, the gunship gunner's) is a rectangle.
constexpr Mask MaskOf(Kind k) noexcept {
    return k==Kind::optical || k==Kind::missile || k==Kind::rocket ? Mask::round :
        k==Kind::sensor ? Mask::sensor : Mask::none;
}
constexpr float kSteps[]={1.0f,3.0f,6.0f};
constexpr int kStepCount=static_cast<int>(sizeof(kSteps)/sizeof(kSteps[0]));
constexpr float kBaseFov=0.785398163f;   // the stock camera's (the constant at 0x1765A20, pi/4)

// The step after `i` (the last goes back to 1x).
constexpr int Next(int i) noexcept { return i>=0 && i+1<kStepCount ? i+1 : 0; }
// The magnification of step `i` (out of range: 1x).
constexpr float At(int i) noexcept { return i>=0 && i<kStepCount ? kSteps[i] : 1.0f; }

// The field of view with the camera's own zoom `camZoom` (cam+0x410) and the sight's `zoom`; either not a number or
// under 1 taken as 1 (never wider than the stock view).
inline float Fov(float camZoom,float zoom) noexcept {
    const float c=std::isfinite(camZoom) && camZoom>=1.0f ? camZoom : 1.0f;
    const float z=std::isfinite(zoom) && zoom>=1.0f ? zoom : 1.0f;
    return kBaseFov/c/z;
}

// A turn rate (the turret camera's, rad/frame) at the sight's `zoom`: the same sweep across the screen at any zoom.
inline float Rate(float rate,float zoom) noexcept { return std::isfinite(zoom) && zoom>1.0f ? rate/zoom : rate; }

// --- The sight's range setting (zeroing; the user, 2026-10-11: "这个瞄具支持调整瞄具吧，也就是类似战雷那样，鼠标滚轮可以调
// 到100m、200m之类的"; then: the 100 m was only an example). Through the optic the camera looks along the barrel
// (sightzoom.cpp), so the reticle's centre is the bore line and a dropping round lands under it. Set to a range (the
// wheel / SightRangeUpKey / SightRangeDownKey / the pad's d-pad, up to the gun's reach, at most kZeroMost; 0 the bore
// line) the line of sight is laid that much below the bore: the gun stands the round's superelevation over it, and a
// round fired with the reticle's centre on a target that far lands on it (a tank sight's range drum, War Thunder's sight
// distance). The steps (ini SightRangeStep): 0, the default, as a range drum is graduated, fine where a small error in
// range misses and coarse far out: kFineStep to kFineTo, kMidStep to kMidTo, kFarStep beyond; or one fixed step. The
// range belongs to the seat's sight, every weapon on it solved for its own round. ---
constexpr int kZeroMost=4000;
constexpr int kFineStep=25,kFineTo=300,kMidStep=50,kMidTo=1000,kFarStep=100;
constexpr int kZeroStep=kFineStep;   // the finest step: the least reach worth a setting
// The step from `z` going up (`up`) or down with the graduated drum (`step` 0) or a fixed `step`.
constexpr int ZeroStepAt(int z,bool up,int step) noexcept {
    return step>0 ? step : up ? (z<kFineTo ? kFineStep : z<kMidTo ? kMidStep : kFarStep)
                              : (z<=kFineTo ? kFineStep : z<=kMidTo ? kMidStep : kFarStep);
}
// The range after `notches` steps (up +) from `zero`, held to 0 .. min(`most`, kZeroMost); on the step's marks.
constexpr int NextZero(int zero,int notches,int most,int step=0) noexcept {
    const int top=most>0 && most<kZeroMost ? most : kZeroMost;
    int z=zero<0 ? 0 : zero;
    for(int n=notches;n>0;--n){const int d=ZeroStepAt(z,true,step);z=(z/d+1)*d;if(z>=top){z=top;break;}}
    for(int n=notches;n<0;++n){const int d=ZeroStepAt(z,false,step);z=z%d ? z/d*d : z-d;if(z<=0){z=0;break;}}
    return z>top ? top : z;
}
static_assert(NextZero(0,1,0)==25 && NextZero(275,1,0)==300 && NextZero(300,1,0)==350 && NextZero(1000,1,0)==1100 &&
              NextZero(1100,-1,0)==1000 && NextZero(1000,-1,0)==950 && NextZero(300,-1,0)==275 && NextZero(25,-3,0)==0 &&
              NextZero(3950,5,0)==kZeroMost && NextZero(700,3,820)==820 && NextZero(0,2,0,200)==400 && NextZero(450,-1,0,200)==400,
              "zero steps");
// The superelevation, rad, that carries a round leaving at `speed` (m a frame) and falling `drop` (m a frame squared:
// the game's rounds keep their speed, rounds.cpp) to the level `range` m out: the low root of sin(2a) = drop range /
// speed^2; 0 for no range, no drop or no speed; 45 degrees past the round's reach.
inline float ZeroAngle(float speed,float drop,float range) noexcept {
    if(!(speed>0.0f) || !(drop>0.0f) || !(range>0.0f))return 0.0f;
    const float s=drop*range/(speed*speed);
    return s>=1.0f ? 0.785398163f : 0.5f*std::asin(s);
}
// Unit direction `dir` lowered by `angle` rad toward the ground (about its level right axis): the line of sight under a
// bore raised by its superelevation. `dir` straight up or down: unchanged.
inline void Lowered(const float* dir,float angle,float* out) noexcept {
    float u[3]={-dir[1]*dir[0],1.0f-dir[1]*dir[1],-dir[1]*dir[2]};   // world up less its share along dir
    const float l=std::sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
    if(!(l>1e-5f) || angle==0.0f){out[0]=dir[0];out[1]=dir[1];out[2]=dir[2];return;}
    const float c=std::cos(angle),s=std::sin(angle);
    for(int i=0;i<3;++i)out[i]=dir[i]*c-u[i]/l*s;
}

static_assert(Next(0)==1 && Next(1)==2 && Next(kStepCount-1)==0 && Next(-3)==0,"1x -> 3x -> 6x -> 1x");
static_assert(At(0)==1.0f && At(kStepCount)==1.0f,"out of range is 1x");
}  // namespace sightzoom
}  // namespace crew
