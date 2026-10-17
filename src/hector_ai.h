// EDF4.1 Hector's attack behaviour that EDF6's Berserker keys cannot say (src/edf41ai.cpp hooks the game with it,
// tools/hector_ai_check.cpp checks it offline). No game, no Windows. Source: EDF41.exe, class Hector (vtable 0xA987E0;
// jobs/4bf89026/tmp/behav_hector/NOTES.md and hector_all.txt there): weapon pick 0x223B10, the AI 0x223470, the phase
// reset 0x223970.
//  - Weapon pick (every 300 + rand(300) frames; EDF6's gun_attack interval in the object's data): the arms whose
//    hector_weapon_range is greater than the horizontal (x, z) distance to the target are the candidates. None: nothing
//    fires. One: that arm. Both: if rand(2) == 0 or the two arms hold the same weapon class (typeid equal) both fire,
//    otherwise one of them, chosen by another rand(2).
//  - Back-off cycle: a counter starts at dwellBase + rand(dwellRand) frames and counts down only while the target is
//    within `near` (150) units, by 2 a frame inside the stop radius (stopMin + rand * (stopMax - stopMin), drawn again
//    every cycle) and by 1 outside it. At zero the Hector walks backward facing the target for retreatBase +
//    rand(retreatRand) frames, then the cycle starts again. Inside `near` it walks up to the target in a straight line
//    and stands inside the stop radius.
//  - Speed (0x223470 / 0x2216F0): the walk command is the object's +0x278 (1.0, GameObjectBase's default, 0x28D0EF),
//    -0.85 of it backing off (0x223832, 0x2238BB); the local velocity eases 10 % a frame to 0.1 x the command, x 60 per
//    second (0x2218D0, 0x221A31, 0x221B30): 6 units/s forward, 5.1 backward, everywhere (no slower battle walk). EDF6's
//    MoveTo (0x33E3B0) makes the walk vector |speed| long and the class scales it by WalkingEnemyBase.MoveSpeed[0] = 6
//    (0x53D7CC); the class's own speeds are 0.125 in the chase state (0x34DA46) and 0.9..1.2 x linear_speed following,
//    so Steer gives the speed 4.1 has.
//  - Weapon aim (class Hector_WeaponBase, 0x22ADF0 with 0x22AA90): from the start of an attack (0x2259D8, +0xF2 = 1) to
//    its end (0x229FFB) the weapon object on the arm bone turns towards the target: the target in the weapon's frame
//    gives pitch -atan2(y, |xz|) and yaw atan2(x, z); pitch is held in [-pi/2, 0.4 pi]; the arm on side 1 (arms_r,
//    hector_weapon's third field) gives up (aims back to 0) past a yaw of -0.6 pi and stops at 0.7 pi, side 0 the mirror;
//    both angles ease 10 % a frame (0x22AFCA). Out of an attack they shrink 5 % a frame (x 0.95, 0x22B001). EDF6's
//    Berserker turns no bone for its guns (gun_control's chase_speed / swing_angle move the battery's aim point,
//    0x34F97D..0x34FB74, which only steers the shot, 0x6D2AAF): ArmAim does what 4.1's weapon did.
#pragma once
#include <cmath>
#include <cstdint>

namespace hector {
constexpr int kArms=2;
constexpr unsigned kArmLeft=1u,kArmRight=2u;       // PickArms' mask: arms_l is gun_control's entry 0

// An address alone is not an instance: the game's weak-this control block changes on respawn/reuse.
// A missing Hector marker must also invalidate state, even if the control block still matches.
constexpr bool CacheMatches(const void* cachedControl,const void* liveControl,bool marked) noexcept {
    return marked && liveControl && cachedControl==liveControl;
}

// hector_ai_setting [720, 1440, 540, 720, 150, 20, 80] of the 4.1 object, and what each arm holds.
struct Config {
    float range[kArms]{};       // hector_weapon_range of arms_l, arms_r
    bool sameWeapon=false;      // both arms hold the same weapon class
    float dwellBase=720.0f,dwellRand=1440.0f;
    float retreatBase=540.0f,retreatRand=720.0f;
    float closeIn=150.0f;
    float stopMin=20.0f,stopMax=80.0f;
};

// A small xorshift: the cycle needs rand(n), not a particular sequence.
struct Rng {
    std::uint64_t s=0x9E3779B97F4A7C15ull;
    void Seed(std::uint64_t v) noexcept { s=v ? v : 0x9E3779B97F4A7C15ull; for(int i=0;i<4;++i)Next(); }
    std::uint64_t Next() noexcept {
        s^=s<<13; s^=s>>7; s^=s<<17;
        return s;
    }
    float Unit() noexcept { return static_cast<float>(Next()>>40)/16777216.0f; }                 // [0, 1)
    int Below(int n) noexcept { return n<=1 ? 0 : static_cast<int>(Next()>>33)%n; }               // [0, n)
};

enum class Phase { Approach, Retreat };
enum class Event { None, BackoffStart, BackoffEnd };

struct Cycle {
    Phase phase=Phase::Approach;
    float counter=0.0f;         // frames left of the advance dwell
    int retreatLeft=0;          // frames left of the back-off
    float stopRadius=50.0f;
};

inline int Roll(float base,float spread,Rng& rng) noexcept { return static_cast<int>(base)+rng.Below(static_cast<int>(spread)); }

// The cycle's start: the advance phase with a fresh counter and stop radius.
inline Cycle Begin(const Config& c,Rng& rng) noexcept {
    Cycle y;
    y.phase=Phase::Approach;
    y.counter=static_cast<float>(Roll(c.dwellBase,c.dwellRand,rng));
    y.stopRadius=c.stopMin+rng.Unit()*(c.stopMax-c.stopMin);
    y.retreatLeft=0;
    return y;
}

// One frame with the target `dist` away (horizontal). Returns what changed.
inline Event Step(Cycle& y,const Config& c,Rng& rng,float dist) noexcept {
    if(y.phase==Phase::Retreat) {
        if(--y.retreatLeft>0)return Event::None;
        y=Begin(c,rng);
        return Event::BackoffEnd;
    }
    if(dist>=c.closeIn)return Event::None;
    y.counter-=dist<y.stopRadius ? 2.0f : 1.0f;
    if(y.counter>0.0f)return Event::None;
    y.phase=Phase::Retreat;
    y.retreatLeft=Roll(c.retreatBase,c.retreatRand,rng);
    return Event::BackoffStart;
}

constexpr float kWalkSpeed=1.0f;       // 4.1's walk command (+0x278)
constexpr float kRetreatSpeed=0.85f;   // backing off: -0.85 x the command, facing the target

// How the walk is steered this frame. `takeover` false: the game's own goal (outside `near`, it knows the way round
// obstacles). `retreat`: walk away from the target. `go` false: stand (inside the stop radius). `speed`: MoveTo's speed
// argument in every case (the length of the walk vector the class multiplies by MoveSpeed[0]).
struct Walk {
    bool takeover=false;
    bool retreat=false;
    bool go=true;
    float speed=kWalkSpeed;
};

inline Walk Steer(const Cycle& y,const Config& c,float dist,bool haveTarget=true) noexcept {
    Walk w;
    if(!haveTarget)return w;
    if(y.phase==Phase::Retreat) { w.takeover=true; w.retreat=true; w.speed=kRetreatSpeed; return w; }
    if(dist>=c.closeIn)return w;
    w.takeover=true;
    w.go=dist>y.stopRadius;
    w.speed=w.go ? kWalkSpeed : 0.0f;
    return w;
}

// ---- weapon aim -------------------------------------------------------------------------------------------------------
constexpr float kPi=3.14159265358979f;
constexpr float kPitchMin=-0.5f*kPi,kPitchMax=0.4f*kPi;
constexpr float kYawGiveUp=0.6f*kPi,kYawStop=0.7f*kPi;
constexpr float kAimEase=0.1f,kAimRelax=0.95f;

struct ArmAim {
    float pitch=0.0f,yaw=0.0f;
};

// The angles that point the arm's +Z at `l` (the target seen in the arm's frame as the animation has it), within 4.1's
// limits for the arm on `side` (0 arms_l, 1 arms_r). false: the target is behind the arm's reach, aim back to 0.
inline bool AimAngles(const float* l,int side,float& pitch,float& yaw) noexcept {
    pitch=0.0f; yaw=0.0f;
    const float flat=std::sqrt(l[0]*l[0]+l[2]*l[2]);
    if(!(flat>1e-6f || std::fabs(l[1])>1e-6f))return false;
    float p=-std::atan2(l[1],flat);
    const float w=flat>1e-6f ? std::atan2(l[0],l[2]) : 0.0f;
    const float out=side==1 ? w : -w;   // side 1's limits as 4.1 has them, side 0 the mirror
    if(out<-kYawGiveUp)return false;
    float y=out>kYawStop ? kYawStop : out;
    if(side!=1)y=-y;
    if(p<kPitchMin)p=kPitchMin;
    if(p>kPitchMax)p=kPitchMax;
    pitch=p; yaw=y;
    return true;
}

// One frame: towards (pitch, yaw) by 10 % while attacking, else 5 % back to 0.
inline void StepAim(ArmAim& a,bool attacking,float pitch,float yaw) noexcept {
    if(attacking) {
        a.pitch+=(pitch-a.pitch)*kAimEase;
        a.yaw+=(yaw-a.yaw)*kAimEase;
    } else {
        a.pitch*=kAimRelax;
        a.yaw*=kAimRelax;
    }
}

// Row-vector 4x4 matrices (rows x, y, z, position; v' = v M), the game's layout.
inline void Mul4(const float* a,const float* b,float* out) noexcept {
    float r[16];
    for(int i=0;i<4;++i)
        for(int j=0;j<4;++j)
            r[i*4+j]=a[i*4+0]*b[0*4+j]+a[i*4+1]*b[1*4+j]+a[i*4+2]*b[2*4+j]+a[i*4+3]*b[3*4+j];
    for(int i=0;i<16;++i)out[i]=r[i];
}

// The inverse of an affine matrix (any invertible 3x3 part, last column 0 0 0 1). false: singular.
inline bool InverseAffine(const float* m,float* out) noexcept {
    const float a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
    const float A=e*i-f*h,B=-(d*i-f*g),C=d*h-e*g;
    const float det=a*A+b*B+c*C;
    if(!(std::fabs(det)>1e-12f))return false;
    const float k=1.0f/det;
    float r[16]={A*k,-(b*i-c*h)*k,(b*f-c*e)*k,0.0f, B*k,(a*i-c*g)*k,-(a*f-c*d)*k,0.0f, C*k,-(a*h-b*g)*k,(a*e-b*d)*k,0.0f,
                 0.0f,0.0f,0.0f,1.0f};
    for(int j=0;j<3;++j)r[12+j]=-(m[12]*r[0+j]+m[13]*r[4+j]+m[14]*r[8+j]);
    for(int j=0;j<16;++j)out[j]=r[j];
    return true;
}

// Rx(pitch) Ry(yaw): in the arm's own frame, +Z goes to (cos p sin y, -sin p, cos p cos y).
inline void AimRotation(float pitch,float yaw,float* out) noexcept {
    const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw);
    const float r[16]={cy,0.0f,-sy,0.0f, sp*sy,cp,sp*cy,0.0f, cp*sy,-sp,cp*cy,0.0f, 0.0f,0.0f,0.0f,1.0f};
    for(int i=0;i<16;++i)out[i]=r[i];
}

// The target in a frame: the components of `to - frame position` along the frame's (normalised) axes.
inline void InFrame(const float* frame,const float* to,float* l) noexcept {
    const float d[3]={to[0]-frame[12],to[1]-frame[13],to[2]-frame[14]};
    for(int row=0;row<3;++row) {
        const float* v=frame+row*4;
        const float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        l[row]=n>1e-6f ? (d[0]*v[0]+d[1]*v[1]+d[2]*v[2])/n : 0.0f;
    }
}

// The arms that start an attack (kArmLeft | kArmRight) with the target `dist` away; 0 when no arm reaches it.
inline unsigned PickArms(const Config& c,Rng& rng,float dist) noexcept {
    const bool left=c.range[0]>dist,right=c.range[1]>dist;
    if(!left && !right)return 0u;
    if(left!=right)return left ? kArmLeft : kArmRight;
    if(rng.Below(2)==0 || c.sameWeapon)return kArmLeft|kArmRight;
    return rng.Below(2)==0 ? kArmLeft : kArmRight;
}
}  // namespace hector
