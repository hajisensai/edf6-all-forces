// The player's turret (the user, 2026-10-06: "switch targets with a lock box: lock the enemy nearest where I look when I
// press the button"). For the seat of this machine's player in a vehicle the plugin aims for (the flak, a tank's gunner
// seat; the driver's seat of a gunner-seat tank too, whose gunners then fight the lock):
//  - Auto-aim, one way (the user, 2026-10-25: "这个自动瞄准是不是多余了一个版本，删掉一个"). The switchable lead-circle
//    mode (ini AimMode / AimModeKey / AimModeButton: the turret the player's, a circle drawn where to put the gun's line)
//    is gone: the stock vehicle HUD's own lead mark (EDF6VehicleCrew, docs/hud-re.md §7.3) shows that for the gun the
//    player aims, and under EDF6VehicleCrew's turret camera the turret already follows the view; the toggle only put
//    "auto-aim on" and "[key] auto-aim off" on one HUD. The turret aims itself (no turret camera) or goes onto the lock.
//  - Off by default (the user, 2026-10-25: "怎么自动开启自动瞄准了，而且也没啥提示欸……自动瞄准不要开启，开启以后应该有被
//    瞄准敌人的高亮圈之类的"). The ini's AutoAim (0) is the state at each mission's start (MissionResetV1); AutoAimKey / AutoAimButton flip it
//    (J, no pad button; not V, EDF6VehicleCrew's first person key in every vehicle seat). Off: the player's gun is never turned by this plugin (with or without the turret camera, first
//    person, a gunner seat), no lock is taken and the lock binding is not read (Q is then EDF6VehicleCrew's NPC mark
//    alone); the HUD says "auto-aim off" and the key that turns it on, and a flip shows the new state large for a moment.
//    On: the lock below, and the HUD rings the enemy the gun is turned onto, or the one a lock press would take now.
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
#include "edf/input_source.h"

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
constexpr float kNoRange=1000.0f;     // m: the lock range of a gun whose reach is not read

struct Pilot {
    const void* vehicle;        // the vehicle and seat the player is at (and the vehicle's weak-this, a new one at the address is another)
    const void* ctrl;
    unsigned seat;
    const void* target;         // the lock (an enemy object) and its weak-this; since when
    const void* targetCtrl;
    ULONGLONG lockedAt;
    bool lockHeld,lockLong;     // the binding as last read; lockLong: this press already let the lock go
    ULONGLONG lockDownAt;
    ULONGLONG seenFrame;
    float range;                // the lock range (m)
    bool keys;
    const void* spot;           // the enemy nearest the view at the last lock press (aim::LockPick's sameSpot)
    const void* preview;        // the enemy a lock press would take now (this frame's, auto-aim on): the HUD's ring
    float previewAt[3];
    bool toggleHeld;            // the auto-aim binding as last read
    const void* velTarget;      // the lock whose motion lockVel is (m/frame, smoothed over the frames it is held)
    float lockLast[3],lockVel[3];
    int lockFrames;
};
Pilot pilot{};
bool autoAimOn=false;            // the player's auto-aim (off at the start: ini AutoAim)
int iniAutoAim=-1;               // AutoAim as last read: an edit of it in the ini sets the state, the binding flips it between

SRWLOCK readoutLock=SRWLOCK_INIT;
link::TurretReadoutV1 readout{};
ULONGLONG readoutAt=0;

ULONGLONG publishedFrame=0;      // the frame and vehicle of the last PublishAim (the PlayerAimV4 export publishes once)
const void* publishedVehicle=nullptr;
const void* publishedGun=nullptr;

link::ViewRayFn viewRay=nullptr;
link::MapRayFn mapRay=nullptr;
link::SeatQueryFn cameraTurret=nullptr;
link::StabilizerFn stabilizer=nullptr;
link::InputHeldFn inputHeld=nullptr;
link::SeatQueryFn turretObserver=nullptr;
link::SeatQueryFn aimsTurret=nullptr;
link::PayloadAimFn payloadAim=nullptr;
ULONGLONG payloadTried=0;
ULONGLONG aimsTried=0;
ULONGLONG observerTried=0;
ULONGLONG viewTried=0,mapTried=0,cameraTried=0,stabTried=0,heldTried=0;

// EDF6VehicleCrew's map view holds the keys (aimlink.h InputHeldV1): Q turns the map there, not the lock.
bool MapHolds() noexcept { return link::Resolve(link::kCrewDll,link::kInputHeld,inputHeld,heldTried) && inputHeld(); }

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || vk>0xFE || MapHolds())return false;
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (edf::AsyncKey(vk)&0x8000)!=0;
}

bool Held(const unsigned char* seat,bool keys,int vk,int button) noexcept {
    if(keys)return KeyHeld(vk);
    return button>0 && (At<std::uint16_t>(seat,edf::kSeatButtons)&static_cast<std::uint16_t>(button))!=0;
}

void SyncIniAutoAim() noexcept {
    if(cfg.autoAim==iniAutoAim)return;
    iniAutoAim=cfg.autoAim;
    autoAimOn=iniAutoAim!=0;
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

// The enemies in the cone round the view within the lock range, nearest the view first, those the eye sees (at most
// `most`; `current` the index of the lock among them, -1: not there). False: no relations read.
struct Seen { int candidates,visible,current; const void* object[kMostRays]; float pos[kMostRays][3]; };
bool InSight(const unsigned char* vehicle,const float* eye,const float* dir,int most,Seen* out) noexcept {
    out->candidates=out->visible=0;out->current=-1;
    const auto relation=Relations(At<std::int32_t>(vehicle,kTeam));
    if(!relation)return false;
    struct Candidate { const void* object; float angle,distance,pos[3]; };
    Candidate c[kMostCandidates];
    int n=0,count=0;
    const Enemy* world=World(&count);
    const float cone=cfg.lockCone*kDegree;
    const float* origin=reinterpret_cast<const float*>(vehicle+kPosition);
    for(int i=0;i<count;++i) {
        const Enemy& e=world[i];
        if(e.object==vehicle || relation[e.team]!=kEnemyRelation)continue;
        float distance;
        const float angle=aim::OffView(eye,dir,e.pos,&distance);
        // The lock range from the vehicle, as Check lets it go: the eye may sit tens of metres behind it (the Titan's
        // camera 37 m), which took the far end of the range off what the crosshair was on.
        const float off[3]={e.pos[0]-origin[0],e.pos[1]-origin[1],e.pos[2]-origin[2]};
        if(angle>cone || Dot(off,off)>pilot.range*pilot.range)continue;
        int at=0;
        while(at<n && c[at].object!=e.object)++at;
        if(at<n){if(angle<c[at].angle){c[at].angle=angle;c[at].distance=distance;std::memcpy(c[at].pos,e.pos,12);}continue;}
        if(n<kMostCandidates)c[n++]=Candidate{e.object,angle,distance,{e.pos[0],e.pos[1],e.pos[2]}};
    }
    for(int i=1;i<n;++i)for(int k=i;k>0 && c[k].angle<c[k-1].angle;--k){const Candidate t=c[k];c[k]=c[k-1];c[k-1]=t;}
    out->candidates=n;
    if(most>kMostRays)most=kMostRays;
    for(int i=0;i<n && out->visible<most;++i) {
        if(!Sees(eye,c[i].pos,c[i].distance))continue;
        if(c[i].object==pilot.target)out->current=out->visible;
        out->object[out->visible]=c[i].object;std::memcpy(out->pos[out->visible],c[i].pos,12);
        ++out->visible;
    }
    return true;
}

// A lock press: of the enemies in sight (InSight), the pick aim::LockPick makes (the crosshair's first). Nothing in
// sight: the lock stays as it is.
void Cycle(const unsigned char* vehicle,const float* muzzle,const float* bore) noexcept {
    float eye[3],dir[3];
    View(vehicle,muzzle,bore,eye,dir);
    Seen seen;
    if(!InSight(vehicle,eye,dir,kMostRays,&seen))return;
    const int visible=seen.visible;
    const int pick=aim::LockPick(visible,seen.current,visible>0 && seen.object[0]==pilot.spot);
    pilot.spot=visible>0 ? seen.object[0] : nullptr;
    if(pick<0){Log("LOCK v=%p seat=%u: no enemy in sight (%d in the %.0f deg cone, %.0f m)",vehicle,pilot.seat,seen.candidates,cfg.lockCone,pilot.range);return;}
    if(seen.object[pick]==pilot.target)return;   // the only one in sight is the one locked
    pilot.target=seen.object[pick];
    pilot.targetCtrl=At<const void*>(pilot.target,kSelfCtrl);
    pilot.lockedAt=GetTickCount64();
    Log("LOCK v=%p seat=%u: t=%p, %d of %d in sight (%s view)",vehicle,pilot.seat,pilot.target,pick+1,visible,
        viewRay ? "camera" : muzzle ? "barrel" : "nose");
}

// The enemy a lock press would take now with no lock held (the first in sight: LockPick's pick with no lock), every
// frame, auto-aim on or off: the HUD's ring while on with nothing locked, and EDF6VehicleCrew's Q mark's pick in this
// seat either way (aimlink.h TurretLockV1).
void Preview(const unsigned char* vehicle,const float* muzzle,const float* bore) noexcept {
    pilot.preview=nullptr;
    float eye[3],dir[3];
    View(vehicle,muzzle,bore,eye,dir);
    Seen seen;
    if(!InSight(vehicle,eye,dir,1,&seen) || seen.visible<1)return;
    pilot.preview=seen.object[0];
    std::memcpy(pilot.previewAt,seen.pos[0],12);
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

// The lock's motion, a frame at a time (PilotFrame runs once a game frame): its first lock point's step, smoothed as the
// aim's own tracks are (plugin.cpp Lead), for the lead of a turret the camera steers (PlayerAimV4).
void TrackLock() noexcept {
    const Enemy* e=pilot.target ? PointOf(pilot.target) : nullptr;
    if(!e || pilot.velTarget!=pilot.target) {
        pilot.velTarget=e ? pilot.target : nullptr;pilot.lockFrames=0;std::memset(pilot.lockVel,0,sizeof(pilot.lockVel));
        if(e)std::memcpy(pilot.lockLast,e->pos,12);
        return;
    }
    for(int i=0;i<3;++i) {
        const float step=e->pos[i]-pilot.lockLast[i];
        pilot.lockVel[i]=pilot.lockFrames ? pilot.lockVel[i]+0.3f*(step-pilot.lockVel[i]) : step;
    }
    ++pilot.lockFrames;
    std::memcpy(pilot.lockLast,e->pos,12);
}
}  // namespace

void PilotFrame(const unsigned char* vehicle,unsigned seatIndex,const unsigned char* seat,const float* muzzle,const float* bore,float range) noexcept {
    SyncIniAutoAim();
    const auto now=GetTickCount64();
    const void* ctrl=At<const void*>(vehicle,kSelfCtrl);
    if(pilot.vehicle!=vehicle || pilot.ctrl!=ctrl || pilot.seat!=seatIndex || !Fresh()) {
        if(pilot.target)Clear("the player changed seats");
        pilot=Pilot{};
        pilot.vehicle=vehicle;pilot.ctrl=ctrl;pilot.seat=seatIndex;
        pilot.toggleHeld=pilot.lockHeld=pilot.lockLong=true;   // a binding held while boarding is no press
        Log("PILOT v=%p seat=%u: auto-aim %s (key 0x%X / pad 0x%X), lock key 0x%X / pad 0x%X",vehicle,seatIndex,
            autoAimOn ? "on" : "off",cfg.autoAimKey,cfg.autoAimButton,cfg.lockKey,cfg.lockButton);
    }
    pilot.seenFrame=Frame();
    pilot.range=cfg.lockRange>0.0f ? cfg.lockRange : range>0.0f ? range : kNoRange;
    pilot.keys=At<std::uint8_t>(seat,edf::kSeatPad)==0;
    const bool toggleDown=Held(seat,pilot.keys,cfg.autoAimKey,cfg.autoAimButton);
    if(toggleDown && !pilot.toggleHeld) {
        autoAimOn=!autoAimOn;
        Log("PILOT v=%p seat=%u: auto-aim %s (%s)",vehicle,seatIndex,autoAimOn ? "on" : "off",pilot.keys ? "key" : "pad button");
        if(!autoAimOn)Clear("auto-aim off");
    }
    pilot.toggleHeld=toggleDown;
    // The lock binding is the lock's only while auto-aim is on (off: EDF6VehicleCrew's Q mark alone acts on it). Its state
    // is followed either way, so a press made while off is no lock once auto-aim is on.
    const bool lockDown=Held(seat,pilot.keys,cfg.lockKey,cfg.lockButton);
    if(!autoAimOn)pilot.lockLong=true;
    if(lockDown && !pilot.lockHeld){pilot.lockDownAt=now;pilot.lockLong=!autoAimOn;}
    if(lockDown && !pilot.lockLong && now-pilot.lockDownAt>=cfg.lockClearMs){pilot.lockLong=true;Clear("held");}
    if(!lockDown && pilot.lockHeld && !pilot.lockLong)Cycle(vehicle,muzzle,bore);   // a short press: on its release
    pilot.lockHeld=lockDown;
    Check(vehicle);
    TrackLock();
    Preview(vehicle,muzzle,bore);
}

bool AutoAimOn() noexcept { SyncIniAutoAim();return autoAimOn; }

const void* Designated(const unsigned char* vehicle,float* world) noexcept {
    if(!AutoAimOn() || !Fresh() || pilot.vehicle!=vehicle || !pilot.target)return nullptr;
    if(At<const void*>(vehicle,kSelfCtrl)!=pilot.ctrl || !Same(pilot.target,pilot.targetCtrl))return nullptr;
    const Enemy* e=PointOf(pilot.target);
    if(!e)return nullptr;
    if(world)std::memcpy(world,e->pos,12);
    return pilot.target;
}

bool ObservesTurret(const unsigned char* vehicle,unsigned seat) noexcept {
    const auto fn=link::Resolve(link::kCrewDll,link::kTurretObserver,turretObserver,observerTried);
    return fn && fn(vehicle,seat);
}

bool ActualWeapon(const unsigned char* vehicle,unsigned seat,const unsigned char* source,const unsigned char** out) noexcept {
    if(!out)return false;
    *out=nullptr;
    const auto fn=link::Resolve(link::kCrewDll,link::kPayloadAim,payloadAim,payloadTried);
    if(!fn)return false;
    link::PayloadAimV1 reply{sizeof(link::PayloadAimV1),nullptr};
    if(!fn(vehicle,seat,source,&reply) || reply.size!=sizeof(reply))return false;
    const auto weapon=static_cast<const unsigned char*>(reply.weapon);
    if(weapon && Readable(weapon,kAmmoGravity+4))*out=weapon;
    return true; // a handled empty/invalid pointer cannot silently restore a different native gun
}

bool CrewAims(const unsigned char* vehicle,unsigned seat) noexcept {
    return link::Resolve(link::kCrewDll,link::kAimsTurret,aimsTurret,aimsTried) && aimsTurret(vehicle,seat);
}

bool CameraTurret(const unsigned char* vehicle,unsigned seat) noexcept {
    return link::Resolve(link::kCrewDll,link::kCameraTurret,cameraTurret,cameraTried) && cameraTurret(vehicle,seat);
}

edf::aimlink::PlayerGun PlayerControlRule(const unsigned char* vehicle,unsigned seat,bool locked) noexcept {
    if(!AutoAimOn())return {false,false};   // auto-aim off: the player's gun is the player's, the stick no drag
    if(ObservesTurret(vehicle,seat))return {false,false};
    return link::PlayerGunRule(CameraTurret(vehicle,seat),locked);
}

bool Stabilized(const unsigned char* vehicle,unsigned seat,const float* axes,float* held,float* hull) noexcept {
    held[0]=axes[0];held[1]=axes[1];hull[0]=hull[1]=0.0f;
    if(!link::Resolve(link::kCrewDll,link::kStabilizer,stabilizer,stabTried))return false;
    float h[2],d[2];
    if(!stabilizer(vehicle,seat,h,d))return false;
    for(int i=0;i<2;++i)if(!std::isfinite(h[i]) || !std::isfinite(d[i]))return false;
    held[0]=h[0];held[1]=h[1];hull[0]=d[0];hull[1]=d[1];
    return true;
}

namespace {
link::PriorityZoneFn priorityZone=nullptr;
ULONGLONG zoneTried=0;
struct ZoneCache { ULONGLONG tick; bool on; link::PriorityZoneV1 z; } zoneCache{};
constexpr std::size_t kTargetOfEnemy=0x518;   // GameObjectBase: the object's own target (weak, its object pointer)
}  // namespace

float PriorityWeight(const Enemy& e) noexcept {
    const ULONGLONG now=GetTickCount64();
    if(now!=zoneCache.tick) {   // a frame's turrets share one answer (the tick: no clock of the game's here)
        zoneCache.tick=now;zoneCache.on=false;
        if(link::Resolve(link::kCrewDll,link::kPriorityZone,priorityZone,zoneTried))zoneCache.on=priorityZone(&zoneCache.z);
    }
    if(!zoneCache.on)return 1.0f;
    const void* target=nullptr;
    __try {
        if(e.object && Readable(static_cast<const unsigned char*>(e.object)+kTargetOfEnemy,8))
            target=At<const void*>(e.object,kTargetOfEnemy);
    } __except(EXCEPTION_EXECUTE_HANDLER){target=nullptr;}
    return link::PriorityWeight(zoneCache.z,e.pos,target);
}

void PublishAim(const unsigned char* vehicle,bool ownGun,const void* target,const float* world,const void* gun) noexcept {
    link::TurretReadoutV1 r{};
    const bool on=AutoAimOn();
    r.mode=on ? link::Mode::autoAim : link::Mode::manual;r.keys=pilot.keys;r.ownGun=ownGun;
    r.modeKey=cfg.autoAimKey;r.modeButton=cfg.autoAimButton;
    r.lockKey=cfg.lockKey;r.lockButton=cfg.lockButton;
    if(!on)target=nullptr;   // off: nothing ringed, nothing locked (a target the flak's fuse tracks is the fuse's)
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
    } else if(on && r.lock==link::Lock::none && pilot.preview && Fresh() && pilot.vehicle==vehicle) {
        r.target=true;std::memcpy(r.at,pilot.previewAt,12);
    }
    publishedFrame=Frame();publishedVehicle=vehicle;publishedGun=gun;
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

// EDF6VehicleCrew's Q mark asks it on its game thread, once a frame (common/edf/aimlink.h TurretLockV1).
extern "C" __declspec(dllexport) bool __cdecl EDF6AutoTurret_TurretLockV1(edf::aimlink::TurretLockV1* out) {
    using namespace autoturret;
    if(!out || out->size!=sizeof(edf::aimlink::TurretLockV1))return false;
    *out=edf::aimlink::TurretLockV1{sizeof(edf::aimlink::TurretLockV1),nullptr,0,false,false,nullptr,{}};
    __try {
        if(!image || !cfg.enabled || !Fresh())return false;
        out->autoAim=AutoAimOn();out->keys=pilot.keys;
        out->lockKey=cfg.lockKey;
        out->target=out->autoAim && pilot.target && Same(pilot.target,pilot.targetCtrl) ? pilot.target : nullptr;
        if(pilot.preview){out->pick=pilot.preview;std::memcpy(out->pickAt,pilot.previewAt,12);}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// EDF6VehicleCrew's MissionStart (aimlink.h MissionResetV1), game thread: auto-aim back to the ini's, the lock let go.
extern "C" __declspec(dllexport) void __cdecl EDF6AutoTurret_MissionResetV1() {
    using namespace autoturret;
    iniAutoAim=-1;SyncIniAutoAim();
    if(pilot.target)Log("LOCK: let go of t=%p (a new mission)",pilot.target);
    pilot=Pilot{};
    Log("PILOT: a new mission: auto-aim %s (ini AutoAim)",autoAimOn ? "on" : "off");
}

namespace autoturret {
namespace {
// The PlayerAimV4 export's work (aimlink.h V4): the seat's bindings and lock kept up once this frame (its own input hook
// may have done it already: the flak, a 403 / 404 tank), the readout published for the gun EDF6VehicleCrew's turret
// camera turns (once a frame), and with a lock the point where that gun's round meets the lock: aim::LeadSolve, against
// the lock's own motion (TrackLock).
bool PlayerAim(const unsigned char* v,unsigned s,const unsigned char* gun,edf::aimlink::PlayerAimV4* out) noexcept {
    ReloadConfigIfChanged();
    if(!image || !cfg.enabled || !v || !Readable(v,edf::kSeatCount+8) || v[kDead] || s>=edf::SeatCount(v))return false;
    const auto seat=edf::SeatAt(const_cast<unsigned char*>(v),s);
    if(edf::SeatRider(image,seat)!=edf::Rider::player)return false;
    SeeVehicleOnce(v);
    const unsigned char* actual=nullptr;
    if(ActualWeapon(v,s,gun,&actual))gun=actual;
    Shot shot{};
    float muzzle[3],bore[3],life=0.0f,reach=0.0f;
    const bool barrel=gun && Readable(gun,kAmmoGravity+4) && Readable(gun+edf::kWeaponMatrix,0x40) && edf::MeanMuzzle(gun,64,muzzle,bore);
    if(barrel) {
        shot.speed=At<float>(gun,kAmmoSpeed);
        const float gravity=At<float>(gun,kAmmoGravity);
        if(std::isfinite(gravity) && gravity>0.0f)shot.drop=gravity*Down(v)/kFramesPerSecondSq;
        life=static_cast<float>(At<std::int32_t>(gun,kAmmoAlive));
        reach=shot.speed*life;
        if(!std::isfinite(reach) || reach<0.0f)reach=0.0f;
    }
    if(!(pilot.vehicle==v && pilot.seat==s && pilot.ctrl==At<const void*>(v,kSelfCtrl) && pilot.seenFrame==Frame()))
        PilotFrame(v,s,seat,barrel ? muzzle : nullptr,barrel ? bore : nullptr,reach);
    out->mode=AutoAimOn() ? link::Mode::autoAim : link::Mode::manual;
    float world[3];
    const void* target=Designated(v,world);
    if(publishedFrame!=Frame() || publishedVehicle!=v || publishedGun!=gun)
        PublishAim(v,true,target,target ? world : nullptr,gun);
    const auto rule=edf::aimlink::PlayerGunRule(true,target!=nullptr);
    if(!rule.steer || !barrel || ObservesTurret(v,s))return true;
    float dir[3],frames=0.0f;
    if(!(shot.speed>0.0f)){std::memcpy(out->point,world,12);out->steer=true;return true;}   // no round speed read: at the lock
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    out->steer=aim::LeadSolve(m,muzzle,world,pilot.lockVel,shot.speed,shot.drop,out->point,dir,&frames) && (life<=0.0f || frames<=life);
    return true;
}
}  // namespace
}  // namespace autoturret

// EDF6VehicleCrew's turret camera asks once a game frame for the seat it serves (common/edf/aimlink.h V4).
extern "C" __declspec(dllexport) bool __cdecl EDF6AutoTurret_PlayerAimV4(const void* vehicle,unsigned seat,const void* gun,
                                                                         edf::aimlink::PlayerAimV4* out) {
    if(!out)return false;
    *out=edf::aimlink::PlayerAimV4{};
    __try { return autoturret::PlayerAim(static_cast<const unsigned char*>(vehicle),seat,static_cast<const unsigned char*>(gun),out); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
