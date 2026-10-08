// The high camera's toggle (README 高视角; docs/camera-re.md §5): while the player drives a vehicle turretcam.cpp places
// the camera of, a key (ini HighCamKey, 'C') or pad button (ini HighCamButton, R3) switches between the vehicle's view and
// an observation view over the current weapon's real predicted endpoint. Which vehicles offer it (HighCamClass): 1 those with an indirect-fire
// weapon (the Katyusha, the self-propelled howitzer), 2 also the big ones (turretcam.cpp TurretCamLarge: a camera rig
// kLargeRig m long or more: the Titan, the drill tank, the Proteus, the big mechs), 3 every vehicle turretcam.cpp serves.
//  - How (H, docs/camera-re.md §3b): the riding camera follows the seat's MAB camera locators, not the vehicle's
//    game_object_camera_setting (the block this file used to rewrite, +0x170 / +0x180: never read while riding: the
//    old high view did nothing). turretcam.cpp now places the riding camera, the high view as one of its rigs; this
//    file keeps the player's choice and tells it (HighCamOn), and the HUD its hint (PlayerHighCam).
//  - The choice is kept from vehicle to vehicle in the mission; the view goes back when the player gets out, the vehicle
//    is wrecked, HighCam or the plugin is switched off in the ini, and on a new mission.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "edf/weapon.h"
#include <cstring>

namespace crew {
namespace {
// The seat's input (docs/stores-re.md §4): 1 = a pad (0 keyboard and mouse), the pad's button bits.
constexpr std::size_t kSeatPad=0x2B0,kSeatButtons=0x2E8;
// An indirect-fire weapon: marked lofted (the Katyusha's rockets) or ground (the howitzer's shells) and its rounds
// living at least this many frames (the howitzer 1200, the Katyusha 1500; the stock-class guns that share the ground
// mark, the Bohr's grenades 100 and EDF6AutoTurret's flak, live under 2 s).
constexpr std::int32_t kIndirectLife=600;
constexpr ULONGLONG kCueMs=200;

struct HighCam {
    ObjRef ref;                 // the vehicle the toggle is offered in now (or last)
    const void* v;
    bool on;                    // the high view is on in it
    bool want;                  // the player's choice, kept from vehicle to vehicle in the mission
    bool held;                  // the key / button as last read (a press is the edge)
};
HighCam cam{};

struct Cue { bool on,keys; ULONGLONG at; const void* v; };
Cue cue{};
SRWLOCK cueLock=SRWLOCK_INIT;

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

}  // namespace

// Whether the seat holds an indirect-fire weapon (kIndirectLife).
bool IndirectFireSeat(const unsigned char* seat) noexcept {
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>16 || !Readable(holders,count*8))return false;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,edf::kWeaponAccuracyScale+4))continue;
        const std::int32_t mark=At<std::int32_t>(w,edf::kWeaponMark);
        if((mark==edf::kMarkLofted || mark==edf::kMarkGround) && At<std::int32_t>(w,edf::kWeaponAmmoAlive)>=kIndirectLife)return true;
    }
    return false;
}

namespace {

// Whether the toggle is offered in `v` (seat 0 the player's): turretcam.cpp places its camera, and HighCamClass
// takes it in.
bool Offered(const unsigned char* v,const unsigned char* seat) noexcept {
    if(!TurretCamServes(v))return false;
    const int cls=Cfg().highCamClass;
    return (cls>=1 && IndirectFireSeat(seat)) || (cls>=2 && TurretCamLarge(v)) || cls>=3;
}

void Publish(bool on,bool keys,const void* v) noexcept {
    AcquireSRWLockExclusive(&cueLock);
    cue=Cue{on,keys,GetTickCount64(),v};
    ReleaseSRWLockExclusive(&cueLock);
}

void Off(const char* why) noexcept {
    if(cam.on)Log("HIGHCAM v=%p off (%s)",cam.v,why);
    cam.on=false;cam.ref=ObjRef{};cam.v=nullptr;cam.held=true;
    Publish(false,true,nullptr);
}

// The key or button pressed this frame (its edge).
bool Pressed(const unsigned char* seat,bool keys) noexcept {
    const Config& c=Cfg();
    const bool down=keys ? KeyHeld(c.highCamKey) : (At<std::uint16_t>(seat,kSeatButtons)&static_cast<std::uint16_t>(c.highCamButton))!=0;
    const bool press=down && !cam.held;
    cam.held=down;
    return press;
}
}  // namespace

void HighCamFrame(unsigned char* v) noexcept {
    const Config& c=Cfg();
    const bool mine=cam.v==v;
    if(!c.enabled || !c.highCam || SeatCount(v)==0){if(mine)Off(c.enabled ? "HighCam=0" : "plugin off");return;}
    const unsigned char* seat=SeatAt(v,0);
    const bool driven=!v[kDead] && SeatRider(seat)==Rider::player && At<const void*>(seat,kSeatRider)==PlayerHuman() && Offered(v,seat);
    if(!driven){if(mine)Off(v[kDead] ? "the vehicle is wrecked" : "the player got out, or the view is not offered");return;}
    if(!mine || !cam.ref.Is(v)) {   // a vehicle the player has just taken
        cam.ref=ObjRef::Of(v);cam.v=v;cam.on=false;cam.held=true;   // a key held while boarding is no press
        Log("HIGHCAM v=%p: the high view is offered (class %d); %s (key 0x%X, pad button 0x%X)",v,c.highCamClass,cam.want ? "on" : "off",
            c.highCamKey,c.highCamButton);
    }
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    if(MapHoldsKeys())cam.held=true;
    else if(Pressed(seat,keys)) {
        cam.want=!cam.want;
        Log("HIGHCAM v=%p %s by the %s",v,cam.want ? "on" : "off",keys ? "key" : "pad button");
    }
    cam.on=cam.want;
    Publish(cam.on,keys,v);
}

bool HighCamOffered(unsigned char* v) noexcept {
    const Config& c=Cfg();
    return c.enabled && c.highCam && SeatCount(v)>0 && Offered(v,SeatAt(v,0));
}

bool HighCamOn(const void* vehicle) noexcept {
    if(!Cfg().enabled || !Cfg().highCam)return false;
    AcquireSRWLockShared(&cueLock);
    const Cue c=cue;
    ReleaseSRWLockShared(&cueLock);
    // Mode is latched; only the HUD cue expires. Slow frames read this before republishing their cue.
    // Owner/configuration exits explicitly revoke it, independently of draw freshness.
    return c.v==vehicle && c.on;
}

bool PlayerHighCam(bool* on,bool* keys) noexcept {
    AcquireSRWLockShared(&cueLock);
    const Cue c=cue;
    ReleaseSRWLockShared(&cueLock);
    if(!c.at || !c.v || GetTickCount64()-c.at>kCueMs)return false;
    *on=c.on;*keys=c.keys;
    return true;
}

void ResetHighCam() noexcept {
    cam=HighCam{};
    AcquireSRWLockExclusive(&cueLock);
    cue=Cue{};
    ReleaseSRWLockExclusive(&cueLock);
}
}  // namespace crew
