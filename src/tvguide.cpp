// TV guidance for the Tempest cruise missiles (src/tvguide.h; the user, 2026-10-06: "the Tempest missiles get TV
// guidance"). Docs: docs/tvguide-re.md. All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
//  - The Tempest (Weapon_LaserMarkerCallFire, eWeapon087/089/094/095/098) calls in one MissileBullet01 that flies
//    straight for CP[8] = 720 frames, then homes on the laser's hit (its lock entry is the laser marker's own: entry +8
//    the soldier holding the laser). No other weapon's round has that delay. A round of the vtable with that delay,
//    young (kTakeWithin frames), whose entry is the local player's, is the player's Tempest: taken here.
//  - Steering (its nose rows set along the new velocity too: see Fly): before the stock step its homing delay (+0x13B8) is set past any age (missile.cpp's kNoStockHoming),
//    so neither the stock steering nor guidance.cpp's turns it; this turns its own velocity (+0x13D0) toward the
//    heading the player steers at most its own stock turn a frame (CP[5], +0x13A4), its speed kept; the stock step
//    then speeds it up, clamps it, builds its frame along it and moves it.
//  - Handing it back (Esc / B, the map opened): its delay put back to 720 (it is past that: it homes on the laser at
//    once, by guidance.cpp's PN); it is not taken again.
//  - Fire boosts it (the user, 2026-10-06: "left button speeds the missile up, and it cannot be taken back"): its
//    own top speed (CP[6], +0x13A8) times TempestTvBoost, its acceleration (CP[4], +0x13A0) enough to reach that in
//    kBoostFrames; once, for good (handed back it keeps it). Its turn a frame stays its own: faster, wider turns.
//    It blasts when it meets something, as stock.
//  - The view is the map's camera hook's (map.cpp Camera: one owner of the camera step), the soldier held by the map's
//    hold (map.cpp MapHumanFrame); after the key that ended it the hold stays until it is let go.
// Online each machine flies its own copy of the round (docs/guidance-re.md §6): the other players' copies home on the
// laser as stock.
#include "tvguide.h"
#include "crew.h"
#include "memory.h"
#include "pn.h"
#include <atomic>
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kVtable=0x17A1C10;
constexpr std::size_t kDelay=0x13B8,kTurn=0x13A4,kOwn=0x13D0,kFlown=0x1400,kLock=0xB10,kEntryOwner=0x08;
constexpr std::size_t kAccel=0x13A0,kTop=0x13A8;
constexpr float kBoostFrames=30.0f;     // the boost reaches its top speed in about this many frames
constexpr std::size_t kPos=0x90,kNose=0x80,kFlags=0xC34;
constexpr std::uint32_t kTempestDelay=720,kNoStockHoming=1000000,kRoundDead=1;
constexpr std::int32_t kTakeWithin=60;     // frames since launch: a Tempest older than this is not taken
constexpr ULONGLONG kStaleFrames=3,kFreshMs=300;
// The game's look-to (a frame looking along a direction; map.cpp, guidance.cpp use it the same way).
constexpr std::size_t kLookTo=0x4E220;
const unsigned char kLookToSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x0F,0x10,0x0A};
using LookToFn=float*(__fastcall*)(float*,const float*);
constexpr float kMouseTurn=0.003f;        // rad a mouse unit (times TempestTvMouseSpeed)
constexpr float kMostPitch=1.45f;         // rad from level: no straight up or down (the heading would be lost)
constexpr float kNoseAhead=4.0f,kLookAhead=300.0f;   // m: the eye before the nose (clear of its model), the look

struct Tv {
    unsigned char* b;          // the round flown (only touched in frames it was updated in)
    const void* human;         // the soldier it is for
    std::int32_t age;          // its frames since launch, as last seen (a younger one at that address is another)
    ULONGLONG seen;            // the game frame it was last updated in
    bool active,armed,draining;
    bool boost,boosted;        // fire asked for the boost; it is applied to the round (once, for good)
    float yaw,pitch;           // the steer asked since its last update (rad)
};
Tv tv{};
struct Declined { const unsigned char* b; std::int32_t age; } declined{};   // handed back: not taken again
std::atomic<bool> holding{false};
// The view (under `lock`; the camera reads it on its thread).
struct View { bool on; const void* human; float eye[3],look[3]; ULONGLONG at; };
View view{};
SRWLOCK lock=SRWLOCK_INIT;

void Publish(bool on,const float* eye,const float* look) noexcept {
    AcquireSRWLockExclusive(&lock);
    view.on=on;view.human=tv.human;view.at=GetTickCount64();
    if(on){std::memcpy(view.eye,eye,12);std::memcpy(view.look,look,12);}
    ReleaseSRWLockExclusive(&lock);
}

void End(const char* why) noexcept {
    if(!tv.active)return;
    tv.active=false;
    holding.store(tv.draining);
    Publish(false,nullptr,nullptr);
    Log("TV end (%s): round %p at %d frames",why,static_cast<void*>(tv.b),tv.age);
}

// Handed back to its laser: it homes again (it is past its stock delay), not taken again.
void HandBack(const char* why) noexcept {
    if(GameFrame()-tv.seen<=1)Put<std::uint32_t>(tv.b,kDelay,kTempestDelay);
    declined=Declined{tv.b,tv.age};
    End(why);
}

bool IsPlayersTempest(const unsigned char* b,std::int32_t age) noexcept {
    if(At<std::uint32_t>(b,kDelay)!=kTempestDelay || (At<std::uint32_t>(b,kFlags)&kRoundDead))return false;
    if(age<0 || age>kTakeWithin || (declined.b==b && age>=declined.age))return false;
    const auto entry=At<const unsigned char*>(b,kLock);
    const unsigned char* const human=PlayerHuman();
    return human && Readable(entry,kEntryOwner+8) && At<const void*>(entry,kEntryOwner)==human;
}

// The heading `dir` turned `yaw` right and `pitch` up, into `out` (unit).
void Heading(const float* dir,float yaw,float pitch,float* out) noexcept {
    const float h=std::atan2(dir[0],dir[2])+yaw;
    const float p=vec::Clamp(std::asin(vec::Clamp(dir[1],-1.0f,1.0f))+pitch,-kMostPitch,kMostPitch);
    out[0]=std::cos(p)*std::sin(h);out[1]=std::sin(p);out[2]=std::cos(p)*std::cos(h);
}

// The boost, once: its top speed raised (TempestTvBoost times its own), its acceleration to reach it in kBoostFrames.
void Boost(unsigned char* b) noexcept {
    tv.boosted=true;
    const float top=At<float>(b,kTop),accel=At<float>(b,kAccel);
    if(!std::isfinite(top) || !(top>0.0f))return;
    const float boosted=top*vec::Clamp(Cfg().tempestTvBoost,1.0f,10.0f);
    Put<float>(b,kTop,boosted);
    const float need=(boosted-top)/kBoostFrames;
    if(!std::isfinite(accel) || accel<need)Put<float>(b,kAccel,need);
}

void Fly(unsigned char* b) noexcept {
    if(tv.boost && !tv.boosted)Boost(b);
    float* own=reinterpret_cast<float*>(b+kOwn);
    const float speed=vec::Len(own);
    float dir[3]={own[0],own[1],own[2]};
    if(!std::isfinite(speed) || !vec::Normalize(dir))std::memcpy(dir,b+kNose,12);
    const float turnStock=At<float>(b,kTurn);
    const float turn=std::isfinite(turnStock) && turnStock>0.0f ? turnStock : 0.03f;
    float want[3],out[3];
    Heading(dir,tv.yaw,tv.pitch,want);
    tv.yaw=tv.pitch=0.0f;
    pn::Toward(dir,want,turn,out);
    if(std::isfinite(speed) && speed>0.0f)for(int i=0;i<3;++i)own[i]=out[i]*speed;
    // Its nose along it (rows 0-2): a type 2 Tempest (A1, A2) pushes along its nose, which the stock turns only in
    // its homing (held off here) and its update rebuilds from the velocity only for type 0 (0x26AA8F..0x26AB25).
    if(Matches(kLookTo,kLookToSig,sizeof(kLookToSig))) {
        alignas(16) float m[16];
        alignas(16) const float along[4]={out[0],out[1],out[2],0.0f};   // it reads 16 bytes
        reinterpret_cast<LookToFn>(image+kLookTo)(m,along);
        std::memcpy(b+0x60,m,48);
    }
    const float* pos=reinterpret_cast<const float*>(b+kPos);
    const float eye[3]={pos[0]+out[0]*kNoseAhead,pos[1]+out[1]*kNoseAhead,pos[2]+out[2]*kNoseAhead};
    const float look[3]={eye[0]+out[0]*kLookAhead,eye[1]+out[1]*kLookAhead,eye[2]+out[2]*kLookAhead};
    Publish(true,eye,look);
}
}  // namespace

bool TvSteer(unsigned char* b) noexcept {
    if(At<const void*>(b,0)!=image+kVtable)return false;
    const std::int32_t age=At<std::int32_t>(b,kFlown);
    if(tv.active && tv.b==b) {
        if(age<tv.age){End("its address reused");return false;}
        tv.age=age;tv.seen=GameFrame();
        if(At<std::uint32_t>(b,kFlags)&kRoundDead){End("it hit");return false;}
        Fly(b);
        return true;
    }
    if(tv.active || tv.draining || !Cfg().enabled || !Cfg().tempestTv || !IsPlayersTempest(b,age))return false;
    tv=Tv{b,PlayerHuman(),age,GameFrame(),true,false,false,false,false,0.0f,0.0f};
    holding.store(true);
    Put<std::uint32_t>(b,kDelay,kNoStockHoming);
    Log("TV take: the player's Tempest %p at %d frames",static_cast<void*>(b),age);
    Fly(b);
    return true;
}

bool TvFrame(unsigned char* human,bool mapOpen,const TvInput& in) noexcept {
    if(tv.draining) {
        if(in.fire || in.leave)return true;
        tv.draining=false;holding.store(tv.active);
    }
    if(!tv.active || tv.human!=human)return false;
    if(GameFrame()-tv.seen>kStaleFrames){End("the round is gone");return false;}
    if(mapOpen){HandBack("the map opened");return false;}
    if(!tv.armed){tv.armed=!in.fire;}   // the button that called it may still be down: armed once it is let go
    else if(in.fire && !tv.boost){tv.boost=true;Log("TV boost: round %p",static_cast<void*>(tv.b));}
    if(in.leave){tv.draining=true;HandBack("Esc / B");return true;}
    const float stock=GameFrame()-tv.seen<=1 ? At<float>(tv.b,kTurn) : 0.0f;   // read only while it is fresh
    const float turn=std::isfinite(stock) && stock>0.0f ? stock : 0.03f;
    const float k=kMouseTurn*vec::Clamp(Cfg().tempestTvMouseSpeed,0.1f,5.0f);
    tv.yaw+=in.dx*k+in.rx*turn;
    tv.pitch+=-in.dy*k+in.ry*turn;
    tv.yaw=vec::Clamp(tv.yaw,-0.5f,0.5f);tv.pitch=vec::Clamp(tv.pitch,-0.5f,0.5f);
    return true;
}

bool TvView(const void** human,float* eye,float* look) noexcept {
    AcquireSRWLockShared(&lock);
    const View v=view;
    ReleaseSRWLockShared(&lock);
    if(!v.on || GetTickCount64()-v.at>kFreshMs)return false;
    *human=v.human;std::memcpy(eye,v.eye,12);std::memcpy(look,v.look,12);
    return true;
}

bool TvHoldsKeys() noexcept { return holding.load(std::memory_order_relaxed); }

void ResetTv() noexcept {
    tv=Tv{};declined=Declined{};holding.store(false);
    Publish(false,nullptr,nullptr);
}
}  // namespace crew
