// The stock vehicles' HUD (the user, 2026-10-06: "give every stock vehicle our display, a HUD of its own, and the impact
// points too"; ini StockVehicleHud): while the player drives or mans a stock vehicle (any class crew.cpp hooks: the tanks,
// the Grape and the trucks, the Depth Crawler, the Begaruta / Nix mechs, the bikes, the flak, the Naegling and the
// howitzer, the Maser...; not the plugin's aircraft and carrier, which have their own), what its seat holds and does,
// gathered here once a frame on the game thread and drawn by hud.cpp StockVehicleHud (docs/hud-re.md §7):
//  - the vehicle: its kind, its nose and the way its first weapon points (the hull / turret indicator against the
//    camera's look: CameraRay), its level speed (its own position's change over the game clock), its HP, whether the
//    seat's gun stabilizer holds it (stab.cpp StabState);
//  - the seat's weapons (holders seat+0xC8, at most kStockArms): label by the round's class (rounds.cpp), rounds left of
//    the magazine (+0xBE8 / AmmoCount +0x248), the reload as the stock gauge reads it (weapon status 0x692100: once
//    empty, 1 - left / ReloadTime: +0xE68 / +0x20C, or +0xE7C / +0x22C where that one is set);
//  - each weapon's impact point from its own muzzles (edf::MeanMuzzle, as fire builds the shot): rounds.cpp RoundLands
//    flies its round as the game does (an arc, or the rockets' motor) to the first ground within kReach; a homing one's
//    lock as the jets' stores read it (lockon.h WeaponLock), else its LockonRange. The Katyusha's lofted launcher is launcher.cpp's
//    (its cross and ripple ring): listed here, not aimed twice;
//  - an arc gun's sight against the enemy under the view (Target: picked within kPickCone of the screen's centre, kept
//    while within kKeepCone, its velocity measured off its lock point): the pipper where the round passes it and the
//    lead mark where it is then (roundaim.h GunSight, the jets' gun sight's convention), unless the round meets the map
//    first; neither (the sky, nothing there): only the boresight. It used to put the pipper where the round crossed
//    kReach, which in the sky is a point fixed under the boresight (the user, 2026-10-06: "it never moves and does not
//    match", a cannon aimed at a flying saucer);
//  - the selected store, where something lets the player pick one (SetStockSelectedStore; feat/ov-payload);
//  - the fuel tank (v_fuel01, which every seat of a heli or a bike lists): no weapon, so not among the arms; its FuelTank
//    is read instead (stockgauge.cpp FuelGauge: the share left and the time at its burn);
//  - the threats: missiles homing on it, jets locking it (the jets' threat ring's sources).
// A stock heli gets only the stores here (its sight is helisight.cpp's, its instruments HeliHud's). With the HUD on, the
// player's seat's stock aim lines are hidden (crew.cpp AimLines, PlayerStockOwnSight): its impact points replace them.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "lockon.h"
#include "stores.h"
#include "vecmath.h"
#include "edf/weapon.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr ULONGLONG kFreshMs=200;        // a readout not refreshed this long (game ms) is gone: the player got out
constexpr std::uint64_t kMostMuzzles=64;
constexpr float kReach=3000.0f;          // m: the farthest an impact point is looked for (the near camera's far clip)
constexpr float kThreatRadius=30.0f;     // m: a missile's lock point this near the vehicle is homing on it (playerjet.cpp's)
constexpr float kPickCone=0.087f;        // rad (5 deg) off the screen's centre an enemy is ranged on
constexpr float kKeepCone=0.26f;         // rad (15 deg): the ranged one is kept this far off (the lead and the drop move the
                                         // view off it while the pipper is put on its lead mark)
constexpr float kTargetTau=0.3f,kTargetMost=300.0f;   // s, m/s: its velocity smoothed; a jump faster is a respawn
constexpr std::size_t kWeaponLockRange=0x6D0;
// The weapon status the stock gauge reads (0x692100): rounds, magazine, the reload's counters and their full times.
constexpr std::size_t kAmmoMax=0x248,kReloadFrames=0x20C,kReloadLeft=0xE68,kChargeTime=0x22C,kChargeLeft=0xE7C;
constexpr std::size_t kCooldown=0xE0C,kAmmoSource=0xF18;

StockHudReadout latest{};
ULONGLONG latestMs=0;
// The player's vehicle's last place (its speed), and the selection published for it.
struct Track { ObjRef ref; float pos[3]; ULONGLONG ms; float speed; } track{};
struct Selection { const void* vehicle; unsigned seat; int store; ULONGLONG frame; } selection{nullptr,0,-1,0};
// The enemy the guns are ranged on (see the top), and its velocity.
struct Target { ObjRef ref; float at[3],vel[3]; ULONGLONG ms; bool ok; } target{};

bool StockVehicle(const void* v) noexcept {
    return BodyOf(v)==PluginBody::none && !IsPlayerJet(v) && !IsSub(v);
}

// The seat the player sits in (its index), or -1.
int PlayerSeatOf(unsigned char* v) noexcept {
    const unsigned count=SeatCount(v);
    for(unsigned i=0;i<count;++i)if(SeatRider(SeatAt(v,i))==Rider::player)return static_cast<int>(i);
    return -1;
}

// The reload as 0x692100 shows it: while empty (and not cooling down, nor fed from elsewhere), the share done.
void Reload(const unsigned char* w,StockArm& a) noexcept {
    a.reload=1.0f;a.reloadSec=-1.0f;
    a.canReload=At<std::int32_t>(w,kReloadFrames)>=0;
    if(a.ammo>0 || At<float>(w,kCooldown)>=1.1920929e-7f || At<const void*>(w,kAmmoSource))return;
    const float charge=At<float>(w,kChargeTime);
    if(charge>0.0f) {
        a.reload=vec::Clamp(1.0f-At<float>(w,kChargeLeft)/charge,0.0f,1.0f);
        return;
    }
    const std::int32_t full=At<std::int32_t>(w,kReloadFrames),left=At<std::int32_t>(w,kReloadLeft);
    if(full<=0)return;
    a.reload=vec::Clamp(1.0f-static_cast<float>(left)/static_cast<float>(full),0.0f,1.0f);
    a.reloadSec=left>0 ? static_cast<float>(left)/60.0f : 0.0f;
}

// The enemy under the view (see the top): each enemy's lock point off the camera's ray (`eye`, `look`), the nearest
// within kPickCone, or the one already ranged while within kKeepCone; its velocity off its lock point's moves.
struct TargetPick { float eye[3],look[3],best; const void* found; float at[3]; bool keep; float keepAt[3]; };
void PickVisit(void* ctx,const void* object,const float* aim) noexcept {
    auto& p=*static_cast<TargetPick*>(ctx);
    const float d[3]={aim[0]-p.eye[0],aim[1]-p.eye[1],aim[2]-p.eye[2]};
    const float l=vec::Len(d);
    if(!(l>1.0f) || l>kReach)return;
    const float off=std::acos(vec::Clamp(vec::Dot(d,p.look)/l,-1.0f,1.0f));
    if(target.ref.Is(object) && off<=kKeepCone){p.keep=true;std::memcpy(p.keepAt,aim,12);}
    if(off>kPickCone || off>=p.best)return;
    p.best=off;p.found=object;std::memcpy(p.at,aim,12);
}
void RangeTarget(const unsigned char* v,const StockHudReadout& r,const float* eye,ULONGLONG ms) noexcept {
    TargetPick p{};
    p.best=kPickCone;
    if(!r.lookOk || r.heli){target=Target{};return;}
    std::memcpy(p.eye,eye,12);std::memcpy(p.look,r.look,12);
    VisitEnemies(v,&PickVisit,&p);
    const void* obj=p.keep ? target.ref.obj : p.found;
    const float* at=p.keep ? p.keepAt : p.at;
    if(!obj){target=Target{};return;}
    if(!p.keep){target=Target{ObjRef::Of(obj),{at[0],at[1],at[2]},{0.0f,0.0f,0.0f},ms,true};return;}
    const float dt=static_cast<float>(ms-target.ms)*0.001f;
    if(dt>0.0f) {
        float vel[3];
        for(int i=0;i<3;++i)vel[i]=(at[i]-target.at[i])/dt;
        if(!(vec::Len(vel)<=kTargetMost))std::memset(target.vel,0,12);
        else for(int i=0;i<3;++i)target.vel[i]+=(vel[i]-target.vel[i])*(1.0f-std::exp(-dt/kTargetTau));
        target.ms=ms;
    }
    std::memcpy(target.at,at,12);
}

// An arc gun's sight (see the top) from its muzzle `pos` along `dir`, its map hit worked out (a.hit, a.at, a.flight).
void GunMarkOf(const unsigned char* w,const RoundModel& m,const float* pos,const float* dir,StockArm& a) noexcept {
    roundaim::Round round{};
    float shooter[3];
    if(m.lobbed || !ArcRoundOf(w,m,&round,shooter)) {
        if(!a.hit)a.range=0.0f;   // no ground: no pipper (rounds that are not ranged keep the boresight alone)
        return;
    }
    a.ladder=gunsight::Of(round,pos,dir,shooter);
    const roundaim::GunMark g=roundaim::GunSight(round,pos,dir,shooter,a.hit,a.at,a.flight*60.0f,target.ok ? target.at : nullptr,
                                                 target.vel);
    if(g.mark==roundaim::SightMark::none){a.range=0.0f;return;}   // `at` stays where the round ends (twin guns told apart by it)
    a.ranged=g.mark==roundaim::SightMark::ranged;
    a.hit=g.mark==roundaim::SightMark::ground;
    a.inReach=g.inReach;
    std::memcpy(a.at,g.pipper,12);std::memcpy(a.lead,g.lead,12);
    a.flight=g.frames/60.0f;
    a.range=vec::Dist(pos,a.lead);
}

// One weapon's line and its impact point (see the top). `aim`: work the point out (not for a heli's).
void Arm(const unsigned char* w,bool aim,StockArm& a) noexcept {
    RoundModel m{};
    if(!ReadRound(w,&m))m.label="WPN";
    strncpy_s(a.label,m.label ? m.label : "WPN",_TRUNCATE);
    a.ammo=At<std::int32_t>(w,kWeaponAmmo);
    a.ammoMax=WeaponStatusOk() ? At<std::int32_t>(w,kAmmoMax) : -1;
    if(WeaponStatusOk())Reload(w,a);
    else{a.reload=1.0f;a.reloadSec=-1.0f;a.canReload=true;}
    a.kind=m.kind;a.lobbed=m.lobbed;
    a.lofted=At<std::int32_t>(w,edf::kWeaponMark)==edf::kMarkLofted;
    if(a.lofted)strncpy_s(a.label,"ROCKETS",_TRUNCATE);
    if(!aim || a.lofted || m.kind==RoundKind::none)return;
    float pos[3],dir[3];
    if(!edf::MeanMuzzle(w,kMostMuzzles,pos,dir) || !vec::Normalize(dir))return;
    std::memcpy(a.bore,dir,12);
    a.aimed=true;
    if(m.kind==RoundKind::homing) {
        const float range=At<float>(w,kWeaponLockRange);
        a.range=std::isfinite(range) && range>0.0f ? range : 0.0f;
        a.lock=WeaponLock(w,a.at,&a.lockProgress);
        if(a.lock)a.range=vec::Dist(pos,a.at);
        return;
    }
    a.hit=RoundLands(w,m,pos,dir,kReach,a.at,&a.flight);
    a.range=vec::Dist(pos,a.at);
    if(m.kind==RoundKind::arc)GunMarkOf(w,m,pos,dir,a);
}

void Kind(const unsigned char* v,StockHudReadout& r) noexcept {
    const char* name=VehicleClassName(v);
    std::size_t n=0;
    for(;name && name[n] && n+1<sizeof(r.kind);++n) {
        const char c=name[n];
        r.kind[n]=c=='_' ? ' ' : (c>='a' && c<='z') ? static_cast<char>(c-'a'+'A') : c;
    }
    r.kind[n]='\0';
}

void Speed(unsigned char* v,StockHudReadout& r,ULONGLONG ms) noexcept {
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    if(!track.ref.Is(v) || !track.ms || ms-track.ms>500){track=Track{ObjRef::Of(v),{p[0],p[1],p[2]},ms,0.0f};}
    else if(ms>track.ms) {
        const float dt=static_cast<float>(ms-track.ms)*0.001f,now=vec::Flat(p,track.pos)/dt;
        track.speed+=(now-track.speed)*vec::Clamp(dt/0.25f,0.0f,1.0f);   // 0.25 s smoothing: the frame's jitter out
        std::memcpy(track.pos,p,12);track.ms=ms;
    }
    r.speed=track.speed;
}

void Threats(const unsigned char* v,StockHudReadout& r) noexcept {
    int n=MissilesHomingAt(r.pos,kThreatRadius,r.threatAt,kStockThreats);
    if(n>kStockThreats)n=kStockThreats;
    for(int i=0;i<n;++i)r.threatKind[i]=2;
    const int locks=jet::LockersOf(v,r.threatAt+n,kStockThreats-n);
    for(int i=n;i<n+locks;++i)r.threatKind[i]=1;
    r.threats=n+locks;
}

void DebugLog(const StockHudReadout& r) noexcept {
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || now-at<2000)return;
    at=now;
    Log("VHUD %s seat %u: %.0f km/h hp %.0f/%.0f arms %d selected %d threats %d fuel %d %.3f %.0fs",r.kind,r.seat,r.speed*3.6f,r.hp,
        r.hpMax,r.arms,r.selected,r.threats,r.fuel.ok,r.fuel.share,r.fuel.sec);
    for(int i=0;i<r.arms;++i) {
        const StockArm& a=r.arm[i];
        Log("VHUD   %d %s %d/%d reload %.2f (%.1fs) kind %d aimed %d hit %d ranged %d%s %.0fm %.1fs lock %d",i,a.label,a.ammo,a.ammoMax,
            a.reload,a.reloadSec,static_cast<int>(a.kind),a.aimed,a.hit,a.ranged,a.ranged && !a.inReach ? " (out of reach)" : "",a.range,
            a.flight,a.lock);
    }
}
}  // namespace

void SetStockSelectedStore(const void* vehicle,unsigned seat,int store) noexcept {
    selection=Selection{vehicle,seat,store,GameFrame()};
}

bool PlayerStockOwnSight(const void* vehicle) noexcept {
    if(!Cfg().enabled || !Cfg().stockVehicleHud)return false;
    __try { return HudReady() && StockVehicle(vehicle) && !IsHelicopter(vehicle); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void StockHudFrame(unsigned char* v) noexcept {
    if(!Cfg().stockVehicleHud || v[kDead] || !StockVehicle(v))return;
    const int seatIndex=PlayerSeatOf(v);
    if(seatIndex<0)return;
    const unsigned char* const seat=SeatAt(v,static_cast<unsigned>(seatIndex));
    const ULONGLONG ms=GameMs();
    StockHudReadout r{};
    r.zoom=SightZoomNow(v);
    Kind(v,r);
    r.seat=static_cast<unsigned>(seatIndex);
    r.heli=IsHelicopter(v);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    std::memcpy(r.pos,v+kPosition,12);
    std::memcpy(r.hull,m+8,12);
    float eye[3];
    r.lookOk=CameraRay(eye,r.look);
    r.hp=At<float>(v,kHp);r.hpMax=At<float>(v,kHpMax);
    Speed(v,r,ms);
    r.stab=StabState(v,r.seat);
    RangeTarget(v,r,eye,ms);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
    // The store the payload switch has picked (payload.cpp: the secondary fires it), found by its weapon: selected.
    const unsigned char* const picked=PayloadPicked(v);
    // SetStockSelectedStore's index is the holder's, the arms' skip the tank: its arm found as the list is walked.
    const int store=selection.vehicle==v && selection.seat==r.seat && GameFrame()-selection.frame<=2 ? selection.store : -1;
    int pickedArm=-1,storeArm=-1;
    if(n<=8 && Readable(holders,n*8))
        for(std::uint64_t i=0;i<n && r.arms<kStockArms;++i) {
            if(!Readable(holders[i],kHolderWeapon+8))continue;
            const unsigned char* const w=At<const unsigned char*>(holders[i],kHolderWeapon);
            if(!Readable(w,kChargeLeft+4) || IsFuelTank(w))continue;
            if(picked && w==picked)pickedArm=r.arms;
            if(static_cast<int>(i)==store)storeArm=r.arms;
            StockArm& a=r.arm[r.arms++];
            Arm(w,!r.heli,a);
            if(!r.aimOk && a.aimed){std::memcpy(r.aim,a.bore,12);r.aimOk=true;}
        }
    r.selected=pickedArm>=0 ? pickedArm : storeArm;
    FuelGauge(v,&r.fuel);
    Threats(v,r);
    latest=r;latestMs=ms;
    DebugLog(r);
}

bool PlayerStockHud(StockHudReadout* out) noexcept {
    if(!latestMs || GameMs()-latestMs>kFreshMs)return false;
    *out=latest;
    return true;
}

void ResetStockHud() noexcept {
    latest=StockHudReadout{};latestMs=0;track=Track{};selection=Selection{nullptr,0,-1,0};target=Target{};
}
}  // namespace crew
