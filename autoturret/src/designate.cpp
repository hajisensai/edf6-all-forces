// The player's turret (the user, 2026-10-06: "auto-aim switchable to a lead circle; switch targets with a lock box:
// lock the enemy nearest where I look when I press the button"). For the seat of this machine's player in a vehicle
// the plugin aims for (the flak, a tank's gunner seat; the driver's seat of a gunner-seat tank too, whose gunners then
// fight the lock):
//  - The mode (ini AimModeKey / AimModeButton, the start from AimMode): AUTO, the turret aims itself (as before); LEAD,
//    the turret is the player's and the HUD draws the lead circle: where the gun's line must pass for the round, on its
//    real arc, to meet the target where the target will be (aimmath.h LeadSolve: the gun's own AmmoSpeed and drop,
//    the target's velocity as the aim tracks it). The time fuse still bursts the flak at the target's range.
//  - The lock (ini LockKey / LockButton): a press locks the enemy nearest the screen's centre (the angle off the
//    camera's view ray, EDF6VehicleCrew's; without it the gun's barrel) within LockCone and the lock range, that the
//    eye can see (EDF6VehicleCrew's map ray: terrain and buildings); pressed again, the next one out from the view, round
//    to the nearest after the last. Held LockClearMs: the lock is let go. The aim then works on the locked target alone
//    (AUTO: the turret fights it, never another while it is locked; LEAD: the circle is on it); the AI gunners of the
//    same vehicle take it first when their gun can reach it. The lock goes with the target (dead, no longer lockable,
//    beyond 1.5 x the lock range) or when the player leaves the seat.
//  - The readout (common/edf/aimlink.h TurretReadoutV1) for EDF6VehicleCrew's HUD: the lock box (a yellow square closing
//    in while the lock's track settles, then the jets' red diamond), the lead circle and the gun's bore cross.
// The seat's input: the pad flag and buttons (common/edf/layout.h kSeatPad / kSeatButtons: the stock seat's, which
// EDF6VehicleCrew reads for the jets and the artillery the same way); the keys on the keyboard while one of the game's
// windows is in front. Game thread, under the callers' __try.
#include <cmath>
#include <cstring>
#include "turret.h"
#include "aimmath.h"
#include "edf/aimlink.h"

namespace autoturret {
namespace {
namespace link=edf::aimlink;
// A pilot seat not seen for this many game frames: the player left it (the lock goes). Frames, not the clock: a pause
// (the menu) stops them, and the lock survives it. The readout for the HUD goes stale on the clock (kReadoutMs).
constexpr ULONGLONG kPilotFrames=2;
constexpr ULONGLONG kReadoutMs=300;
constexpr ULONGLONG kAcquireMs=400;    // a new lock is "acquiring" while the aim's smoothed velocity of it settles
constexpr float kLostRange=1.5f;       // a lock beyond this many lock ranges from the vehicle is let go
constexpr float kSeeSlack=3.0f;        // m: a map hit this short of the target's point still sees it (it stands on the ground)
constexpr int kMostCandidates=64,kMostRays=24;
constexpr float kDegree=0.0174532925f;

struct Pilot {
    const void* vehicle;        // the vehicle and seat the player is at (and the vehicle's weak-this, a new one at the address is another)
    const void* ctrl;
    unsigned seat;
    const void* target;         // the lock (an enemy object) and its weak-this; since when
    const void* targetCtrl;
    ULONGLONG lockedAt;
    bool modeHeld,lockHeld,lockLong;   // the bindings as last read; lockLong: this press already let the lock go
    ULONGLONG lockDownAt;
    ULONGLONG seenFrame;
    float range;                // the lock range (m)
    bool keys;
};
Pilot pilot{};
link::Mode mode=link::Mode::autoAim;
int iniMode=-1;                 // AimMode as last read: an edit of it in the ini sets the mode, the key flips it between

SRWLOCK readoutLock=SRWLOCK_INIT;
link::TurretReadoutV1 readout{};
ULONGLONG readoutAt=0;

link::ViewRayFn viewRay=nullptr;
link::MapRayFn mapRay=nullptr;
link::SeatQueryFn cameraTurret=nullptr;
ULONGLONG viewTried=0,mapTried=0,cameraTried=0;

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || vk>0xFE)return false;
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

bool Held(const unsigned char* seat,bool keys,int vk,int button) noexcept {
    if(keys)return KeyHeld(vk);
    return button>0 && (At<std::uint16_t>(seat,edf::kSeatButtons)&static_cast<std::uint16_t>(button))!=0;
}

const char* ModeName() noexcept { return mode==link::Mode::leadCircle ? "lead circle" : "auto-aim"; }

void SyncIniMode() noexcept {
    if(cfg.aimMode==iniMode)return;
    iniMode=cfg.aimMode;
    mode=iniMode ? link::Mode::leadCircle : link::Mode::autoAim;
}

void Clear(const char* why) noexcept {
    if(pilot.target)Log("LOCK v=%p seat=%u: let go of t=%p (%s)",pilot.vehicle,pilot.seat,pilot.target,why);
    pilot.target=nullptr;pilot.targetCtrl=nullptr;
}

// The first lock point of `object` in this frame's world (a steady point, as the aim's own pick takes), or nullptr.
const Enemy* PointOf(const void* object) noexcept {
    int count=0;
    const Enemy* world=World(&count);
    for(int i=0;i<count;++i)if(world[i].object==object)return &world[i];
    return nullptr;
}

// Where the player looks: the camera's ray (EDF6VehicleCrew), else the gun's barrel, else the vehicle's nose.
void View(const unsigned char* vehicle,const float* muzzle,const float* bore,float* eye,float* dir) noexcept {
    if(link::Resolve(link::kCrewDll,link::kViewRay,viewRay,viewTried) && viewRay(eye,dir))return;
    if(muzzle && bore){std::memcpy(eye,muzzle,12);std::memcpy(dir,bore,12);return;}
    const float* m=reinterpret_cast<const float*>(vehicle+kMatrix);
    for(int i=0;i<3;++i){eye[i]=m[12+i]+m[4+i]*cfg.pivotHeight;dir[i]=m[8+i];}
}

// Whether the eye sees `p` (`distance` away): no terrain or building in between (EDF6VehicleCrew's map ray; without it
// everything in the cone counts as seen).
bool Sees(const float* eye,const float* p,float distance) noexcept {
    if(!link::Resolve(link::kCrewDll,link::kMapRay,mapRay,mapTried))return true;
    float hit[3];
    const float d=mapRay(eye,p,hit);
    return d<0.0f || d>=distance-kSeeSlack;
}

// A lock press: the enemies in the cone round the view within the lock range, nearest the view first, those the eye
// sees; the pick aim::NextPick makes among them. Nothing in sight: the lock stays as it is.
void Cycle(const unsigned char* vehicle,const float* muzzle,const float* bore) noexcept {
    float eye[3],dir[3];
    View(vehicle,muzzle,bore,eye,dir);
    const auto relation=Relations(At<std::int32_t>(vehicle,kTeam));
    if(!relation)return;
    struct Candidate { const void* object; float angle,distance,pos[3]; };
    Candidate c[kMostCandidates];
    int n=0,count=0;
    const Enemy* world=World(&count);
    const float cone=cfg.lockCone*kDegree;
    for(int i=0;i<count;++i) {
        const Enemy& e=world[i];
        if(e.object==vehicle || relation[e.team]!=kEnemyRelation)continue;
        float distance;
        const float angle=aim::OffView(eye,dir,e.pos,&distance);
        if(angle>cone || distance>pilot.range)continue;
        int at=0;
        while(at<n && c[at].object!=e.object)++at;
        if(at<n){if(angle<c[at].angle){c[at].angle=angle;c[at].distance=distance;std::memcpy(c[at].pos,e.pos,12);}continue;}
        if(n<kMostCandidates)c[n++]=Candidate{e.object,angle,distance,{e.pos[0],e.pos[1],e.pos[2]}};
    }
    for(int i=1;i<n;++i)for(int k=i;k>0 && c[k].angle<c[k-1].angle;--k){const Candidate t=c[k];c[k]=c[k-1];c[k-1]=t;}
    const void* seen[kMostRays];
    int visible=0,current=-1;
    for(int i=0;i<n && visible<kMostRays;++i) {
        if(!Sees(eye,c[i].pos,c[i].distance))continue;
        if(c[i].object==pilot.target)current=visible;
        seen[visible++]=c[i].object;
    }
    const int pick=aim::NextPick(visible,current);
    if(pick<0){Log("LOCK v=%p seat=%u: no enemy in sight (%d in the %.0f deg cone)",vehicle,pilot.seat,n,cfg.lockCone);return;}
    if(seen[pick]==pilot.target)return;   // the only one in sight is the one locked
    pilot.target=seen[pick];
    pilot.targetCtrl=At<const void*>(pilot.target,kSelfCtrl);
    pilot.lockedAt=GetTickCount64();
    Log("LOCK v=%p seat=%u: t=%p, %d of %d in sight (%s view)",vehicle,pilot.seat,pilot.target,pick+1,visible,
        viewRay ? "camera" : muzzle ? "barrel" : "nose");
}

// The lock is let go once its target is gone (dead, no longer lockable, another object at the address) or too far.
void Check(const unsigned char* vehicle) noexcept {
    if(!pilot.target)return;
    const Enemy* e=Same(pilot.target,pilot.targetCtrl) ? PointOf(pilot.target) : nullptr;
    if(!e){Clear("gone");return;}
    const float* p=reinterpret_cast<const float*>(vehicle+kPosition);
    const float d[3]={e->pos[0]-p[0],e->pos[1]-p[1],e->pos[2]-p[2]};
    if(Dot(d,d)>pilot.range*pilot.range*kLostRange*kLostRange)Clear("out of range");
}

bool Fresh() noexcept { return pilot.vehicle && Frame()<=pilot.seenFrame+kPilotFrames; }
}  // namespace

void PilotFrame(const unsigned char* vehicle,unsigned seatIndex,const unsigned char* seat,const float* muzzle,const float* bore,float range) noexcept {
    SyncIniMode();
    const auto now=GetTickCount64();
    const void* ctrl=At<const void*>(vehicle,kSelfCtrl);
    if(pilot.vehicle!=vehicle || pilot.ctrl!=ctrl || pilot.seat!=seatIndex || !Fresh()) {
        if(pilot.target)Clear("the player changed seats");
        pilot=Pilot{};
        pilot.vehicle=vehicle;pilot.ctrl=ctrl;pilot.seat=seatIndex;
        pilot.modeHeld=pilot.lockHeld=pilot.lockLong=true;   // a binding held while boarding is no press
        Log("PILOT v=%p seat=%u: %s, lock %s / pad 0x%X, mode %s / pad 0x%X",vehicle,seatIndex,ModeName(),
            cfg.lockKey ? "on" : "off",cfg.lockButton,cfg.modeKey ? "on" : "off",cfg.modeButton);
    }
    pilot.seenFrame=Frame();
    pilot.range=cfg.lockRange>0.0f ? cfg.lockRange : range;
    pilot.keys=At<std::uint8_t>(seat,edf::kSeatPad)==0;
    const bool modeDown=Held(seat,pilot.keys,cfg.modeKey,cfg.modeButton);
    if(modeDown && !pilot.modeHeld) {
        mode=mode==link::Mode::autoAim ? link::Mode::leadCircle : link::Mode::autoAim;
        Log("PILOT v=%p seat=%u: %s (%s)",vehicle,seatIndex,ModeName(),pilot.keys ? "key" : "pad button");
    }
    pilot.modeHeld=modeDown;
    const bool lockDown=Held(seat,pilot.keys,cfg.lockKey,cfg.lockButton);
    if(lockDown && !pilot.lockHeld){pilot.lockDownAt=now;pilot.lockLong=false;}
    if(lockDown && !pilot.lockLong && now-pilot.lockDownAt>=cfg.lockClearMs){pilot.lockLong=true;Clear("held");}
    if(!lockDown && pilot.lockHeld && !pilot.lockLong)Cycle(vehicle,muzzle,bore);   // a short press: on its release
    pilot.lockHeld=lockDown;
    Check(vehicle);
}

const void* Designated(const unsigned char* vehicle,float* world) noexcept {
    if(!Fresh() || pilot.vehicle!=vehicle || !pilot.target)return nullptr;
    if(At<const void*>(vehicle,kSelfCtrl)!=pilot.ctrl || !Same(pilot.target,pilot.targetCtrl))return nullptr;
    const Enemy* e=PointOf(pilot.target);
    if(!e)return nullptr;
    if(world)std::memcpy(world,e->pos,12);
    return pilot.target;
}

bool LeadCircle() noexcept { return mode==link::Mode::leadCircle; }

bool CameraTurret(const unsigned char* vehicle,unsigned seat) noexcept {
    return link::Resolve(link::kCrewDll,link::kCameraTurret,cameraTurret,cameraTried) && cameraTurret(vehicle,seat);
}

void PublishAim(const unsigned char* vehicle,bool ownGun,const void* target,const float* world,const float* muzzle,const float* bore,
                const Shot* shot,const float* vel,float life) noexcept {
    link::TurretReadoutV1 r{};
    r.mode=mode;r.keys=pilot.keys;r.ownGun=ownGun;
    r.modeKey=cfg.modeKey;r.lockKey=cfg.lockKey;r.modeButton=cfg.modeButton;r.lockButton=cfg.lockButton;
    float locked[3];
    if(Designated(vehicle,locked)) {
        const ULONGLONG held=GetTickCount64()-pilot.lockedAt;
        r.lock=held<kAcquireMs ? link::Lock::acquiring : link::Lock::locked;
        r.lockProgress=held<kAcquireMs ? static_cast<float>(held)/static_cast<float>(kAcquireMs) : 1.0f;
        std::memcpy(r.at,locked,12);
    }
    if(target && world) {
        r.target=true;
        if(r.lock==link::Lock::none)std::memcpy(r.at,world,12);
        float aimAt[3],dir[3],frames;
        const float* m=reinterpret_cast<const float*>(vehicle+kMatrix);
        if(muzzle && bore && shot && vel && aim::LeadSolve(m,muzzle,world,vel,shot->speed,shot->drop,aimAt,dir,&frames)) {
            const float d[3]={aimAt[0]-muzzle[0],aimAt[1]-muzzle[1],aimAt[2]-muzzle[2]};
            r.range=std::sqrt(Dot(d,d));
            for(int i=0;i<3;++i){r.leadAt[i]=muzzle[i]+dir[i]*r.range;r.boreAt[i]=muzzle[i]+bore[i]*r.range;}
            r.flight=frames/60.0f;r.inReach=life<=0.0f || frames<=life;r.lead=std::isfinite(r.range);
        }
    }
    AcquireSRWLockExclusive(&readoutLock);
    readout=r;readoutAt=GetTickCount64();
    ReleaseSRWLockExclusive(&readoutLock);
}

const unsigned char* SeatGun(const unsigned char* seat) noexcept {
    const auto holders=At<const unsigned char* const*>(seat,kSeatWeapons);
    if(At<std::uint64_t>(seat,kSeatWeaponCount)==0 || !Readable(holders,8) || !Readable(holders[0],kHolderWeapon+8))return nullptr;
    return At<const unsigned char*>(holders[0],kHolderWeapon);
}
}  // namespace autoturret

// EDF6VehicleCrew's HUD reads it once a frame (common/edf/aimlink.h); false with the player at no turret of ours now.
extern "C" __declspec(dllexport) bool __cdecl EDF6AutoTurret_TurretReadoutV1(edf::aimlink::TurretReadoutV1* out) {
    using namespace autoturret;
    if(!out)return false;
    AcquireSRWLockShared(&readoutLock);
    const bool fresh=readoutAt && GetTickCount64()-readoutAt<=kReadoutMs;
    if(fresh)*out=readout;
    ReleaseSRWLockShared(&readoutLock);
    return fresh;
}
