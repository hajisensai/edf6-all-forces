// Every stock homing round flies by proportional navigation (pn.h): the player's, the NPCs' and the enemies' alike
// (the user, 2026-10-06: "all the guidance ours", "the NPCs' weapons' guidance too"), at the stock round's own
// strength: its turn limit and thrust stay its own numbers, only the law that spends them changes.
// The stock law is pure pursuit (docs/missile-re.md "why the stock missiles circle"): each frame its velocity (type 1)
// or its nose (type 2: thrust along the nose, the velocity follows) turns straight at the lock point, at most a fixed
// angle; a target inside that turning circle is circled for ever, and a crossing one is chased from behind. PN leads it.
// Three classes home on a weapon's lock (docs/guidance-re.md): MissileBullet01 (most missiles, the Air Raider's called
// cruise missiles among them), MissileBullet02 (the Blood Storms) and HomingLaserBullet01 (the Wing Diver's homing
// lasers: a point round whose trail is an effect that follows it). Each steers in one of two functions called from
// one place each in its update, before it moves: the six calls are redirected here (the stock data untouched). A
// round the plugin does not steer (the setting off, the plugin's own missiles (missile.cpp steers those before the
// update), the stock homing gate not open yet, no live lock) goes to the stock function as it was.
//  - The gates as stock: MissileBullet01/02 steer from CP[8] frames on once CP[9] more have passed (0: at once);
//    HomingLaserBullet01 from CP[6] for CP[7] frames (0: for ever). Each round's numbers are read off the round, after
//    its per-round random scaling (CP[10]).
//  - Type 1: speed + accel, at most the top speed, as stock; the turn across its path at most |own| x turn a frame
//    (the stock turn at that speed), spent by PN (StockMissileNav) on the lock point, its velocity from frame to frame
//    led by its target's (missile.cpp TrackRound: the flares, the missile warnings and the target's motion are the
//    plugin missiles' own). The round's frame (rows 0-2 at +0x60) built from its velocity by the game's look-to
//    0x4E220, as stock; its position row left to the update.
//  - Type 2: the push (CP accel) along a nose that PN picks within the stock nose turn a frame: its share across the
//    flight what PN asks (at most all of it), the rest along; then the stock top speed.
// Each machine flies its own copy of a round (no network ownership in these updates, docs/guidance-re.md §6): every
// player in a room wants the same setting. All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include "pn.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using vec::Dot;using vec::Len;using vec::Normalize;

// A homing class's layout: where its numbers are on the round, its steering calls (type 1, type 2) and their targets.
struct Kind {
    const char* name;
    std::size_t vtable;
    std::size_t type,accel,turn,top,delay,extra;   // CP[0]; accel, turn (rad a frame), top speed (m a frame); the gate
    bool extraLasts;                               // extra: true the steering's duration, false a further delay
    std::size_t own,inherited;                     // velocities (inherited 0: none)
    std::size_t frames;                            // its frame count (the age TrackRound tells rounds apart by)
    std::size_t site[2],target[2];                 // the calls of type 1, type 2 and what they call
    unsigned char sig[2][16];
};
constexpr std::size_t kNone=0;
const Kind kKinds[]={
    {"MissileBullet01",0x17A1C10,0x1380,0x13A0,0x13A4,0x13A8,0x13B8,0x13BC,false,0x13D0,0x13E0,0x1400,
     {0x26AA85,0x26AA76},{0x269AF0,0x269EE0},
     {{0x48,0x89,0x5C,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8D,0x6C,0x24,0xE0,0x48,0x81},
      {0x48,0x89,0x5C,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8D,0x6C,0x24,0xC0,0x48,0x81}}},
    {"MissileBullet02",0x17A1E28,0x1380,0x13A0,0x13A4,0x13A8,0x13B4,0x13B8,false,0x13C0,0x13D0,0x13F0,
     {0x26ECCA,0x26ECBE},{0x26D4B0,0x26D8D0},
     {{0x48,0x89,0x5C,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x81,0xEC,0x30,0x01,0x00,0x00},
      {0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x55,0x41,0x56,0x41,0x57,0x48}}},
    {"HomingLaserBullet01",0x179FA48,0x1170,0x1174,0x1178,0x117C,0x1180,0x1184,true,0x1190,kNone,0x11A0,
     {0x250918,0x25090B},{0x24FA60,0x24FE80},
     {{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x57,0x48,0x81,0xEC,0x30,0x01},
      {0x48,0x89,0x5C,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8D,0x6C,0x24,0xC0,0x48,0x81}}},
};
constexpr int kKindCount=sizeof(kKinds)/sizeof(kKinds[0]);
constexpr std::size_t kLock=0xB10,kPos=0x90,kRows=0x60,kNose=0x80;   // every class: B+0xB10 entry/ctrl, the frame
// The plugin's own missiles (missile.cpp: CP[8] 1000000 never lets the stock steering start, CP[9] the mark).
constexpr std::uint32_t kNoStockHoming=1000000;
constexpr std::size_t kLookTo=0x4E220;
const unsigned char kLookToSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x0F,0x10,0x0A};
using LookToFn=float*(__fastcall*)(float*,const float*);
using SteerFn=void(__fastcall*)(void*,std::uint32_t);
using GateFn=bool(__fastcall*)(void*,std::uint32_t);   // MissileBullet02's: (frames >= CP[8]), its split test's gate
bool lookOk=false;

// Whether round `b` of kind `k` is steered stock: its homing gate not open at `t` frames.
bool GateOpen(const Kind& k,const unsigned char* b,std::uint32_t t) noexcept {
    const std::uint32_t delay=At<std::uint32_t>(b,k.delay),extra=At<std::uint32_t>(b,k.extra);
    if(t<delay)return false;
    if(extra==0)return true;
    return k.extraLasts ? t-delay<extra : t-delay>extra;
}

// The round's frame (rows 0-2: right, up, nose) looking along `dir`, by the game's own look-to.
void Face(unsigned char* b,const float* dir) noexcept {
    alignas(16) float m[16];
    reinterpret_cast<LookToFn>(image+kLookTo)(m,dir);
    std::memcpy(b+kRows,m,48);
}

float Finite(float v,float lo,float hi) noexcept { return std::isfinite(v) ? vec::Clamp(v,lo,hi) : lo; }

// Steers round `b` of kind `k`, type `type` (1 or 2), `t` frames into its flight, by PN. False: left to the stock.
bool Steer(const Kind& k,int type,unsigned char* b,std::uint32_t t) noexcept {
    if(!Cfg().enabled || !Cfg().stockMissilePN || !lookOk)return false;
    if(At<const void*>(b,0)!=image+k.vtable)return false;
    if(At<std::uint32_t>(b,k.delay)==kNoStockHoming)return false;   // the plugin's own (missile.cpp)
    if(!GateOpen(k,b,t))return false;
    float* own=reinterpret_cast<float*>(b+k.own);
    float vel[3]={own[0],own[1],own[2]};
    if(k.inherited)for(int i=0;i<3;++i)vel[i]+=At<float>(b,k.inherited+4*i);
    float dir[3]={vel[0],vel[1],vel[2]};
    const float* nose=reinterpret_cast<const float*>(b+kNose);
    if(!Normalize(dir))std::memcpy(dir,nose,12);
    const float* pos=reinterpret_cast<const float*>(b+kPos);
    float aim[3],tv[3];
    if(!TrackRound(b,static_cast<std::int32_t>(t),pos,dir,b+kLock,aim,tv))return false;
    const float accel=Finite(At<float>(b,k.accel),0.0f,100.0f),turn=Finite(At<float>(b,k.turn),0.0f,3.2f);
    const float top=Finite(At<float>(b,k.top),0.0f,1000.0f),nav=Finite(Cfg().stockMissileNav,2.0f,6.0f);
    float a[3];
    if(type==1) {
        const float own0=Len(own);
        if(!std::isfinite(own0))return false;
        float speed=own0+accel;
        if(top>0.0f && speed>top)speed=top;
        pn::Lateral(pos,vel,aim,tv,nav,own0*turn,a);
        if(!pn::Turn(own,a,speed))return false;
        float facing[3]={own[0],own[1],own[2]};
        if(Normalize(facing))Face(b,facing);
        return true;
    }
    pn::Lateral(pos,vel,aim,tv,nav,accel,a);
    float push[3];
    pn::Thrust(vel,nose,a,accel,turn,push);
    Face(b,push);
    for(int i=0;i<3;++i)own[i]+=push[i]*accel;
    const float speed=Len(own);
    if(top>0.0f && std::isfinite(speed) && speed>top)for(int i=0;i<3;++i)own[i]*=top/speed;
    return true;
}

SteerFn next[kKindCount][2]{};

template<int K,int T>
void __fastcall SteerHook(void* b,std::uint32_t t) {
    bool done=false;
    __try { done=Steer(kKinds[K],T+1,static_cast<unsigned char*>(b),t); }
    __except(EXCEPTION_EXECUTE_HANDLER){done=false;}
    if(!done)next[K][T](b,t);
}

// MissileBullet02's steering returns whether its homing delay is past (its split test runs only then): as stock.
template<int T>
bool __fastcall GateHook(void* b,std::uint32_t t) {
    bool done=false;
    __try { done=Steer(kKinds[1],T+1,static_cast<unsigned char*>(b),t); }
    __except(EXCEPTION_EXECUTE_HANDLER){done=false;}
    if(!done)return reinterpret_cast<GateFn>(next[1][T])(b,t);
    return t>=At<std::uint32_t>(static_cast<unsigned char*>(b),kKinds[1].delay);
}

void* const kHooks[kKindCount][2]={
    {reinterpret_cast<void*>(&SteerHook<0,0>),reinterpret_cast<void*>(&SteerHook<0,1>)},
    {reinterpret_cast<void*>(&GateHook<0>),reinterpret_cast<void*>(&GateHook<1>)},
    {reinterpret_cast<void*>(&SteerHook<2,0>),reinterpret_cast<void*>(&SteerHook<2,1>)},
};
}  // namespace

bool InstallGuidance() noexcept {
    int done=0;
    __try {
        lookOk=Matches(kLookTo,kLookToSig,sizeof(kLookToSig));
        if(!lookOk){Log("HOOK guidance=0 (unexpected EDF.dll code: the look-to)");return false;}
        for(int k=0;k<kKindCount;++k)for(int t=0;t<2;++t) {
            const Kind& kind=kKinds[k];
            if(!Matches(kind.target[t],kind.sig[t],16)){Log("GUIDANCE %s type %d: unexpected code, stock",kind.name,t+1);continue;}
            next[k][t]=reinterpret_cast<SteerFn>(image+kind.target[t]);
            bool changed=false;
            if(RedirectCall(image+kind.site[t],image+kind.target[t],kHooks[k][t],changed))++done;
            else Log("GUIDANCE %s type %d: call not redirected%s",kind.name,t+1,changed ? " (half patched)" : "");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    Log("HOOK guidance=%d of %d steering calls (stock homing rounds by proportional navigation, setting %d)",done,
        kKindCount*2,Cfg().stockMissilePN);
    return done>0;
}
}  // namespace crew
