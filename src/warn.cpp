// The cockpit's warnings (warn.h; the user, 2026-10-06: "告警换成更现实一点的……圆圈告警……拉起、失速提示音和拉起提示"):
// for the aircraft the player flies (a plugin jet or rotor craft: playerjet.cpp PlayerJetHud; a stock helicopter:
// heli.cpp PlayerHeliHud), once a frame:
//  - PULL UP / TERRAIN / SINK RATE: the ground-proximity warning (ClosureIn, GpwsOf), a helicopter's SINK RATE near the
//    ground too (kSinkWarn under kSinkLow);
//  - STALL: the jet's wing cannot hold its path (playerjet.cpp Air);
//  - MISSILE / LOCK: a missile homing on it, an enemy's lock on it (the threats PlayerJetSymbols carries); a launch: more
//    missiles coming than within kMissileMemoryMs before;
//  - GEAR / GEAR SPEED / WEIGHT ON WHEELS: the landing gear (gear.cpp GearHudLatest);
//  - LOW FUEL: its tank (the readouts' FuelReading, stockgauge.cpp FuelGauge; the stock FUEL gauge is gone with
//    HideStockGauges, its number now the HUD's stores line).
// Published whole for the HUD (WarnLatest: hud.cpp's annunciator, RWR scope) and sounded (jetaudio.cpp Warn: the threat
// beeps always, as before; with WarnAudio the launch warble, the stall horn and the callouts).
#include "warn.h"
#include "body506.h"
#include "gear.h"
#include "jetaudio.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr float kRise=5.0f;                  // m: met this much higher than the ground under it, it is TERRAIN
constexpr ULONGLONG kMissileMemoryMs=1500;   // a missile coming that was not counted this long before: a launch
constexpr ULONGLONG kGearFreshMs=500;        // the gear's readout older than this: no jet with gear flown
SRWLOCK publishLock=SRWLOCK_INIT;
Warnings published{};
bool flown=false;
// Game thread.
Warnings state{};
int missilesSeen=0;           // the most missiles coming within kMissileMemoryMs (a lock point flickering in and out of
ULONGLONG missilesSeenAt=0;   // the homing radius is not a new launch each time it comes back)

void Publish(bool any) noexcept {
    AcquireSRWLockExclusive(&publishLock);
    published=state;flown=any;
    ReleaseSRWLockExclusive(&publishLock);
}
}  // namespace

float ClosureIn(const float* pos,const float* vel,float climb,float clear,float gentle,float within,bool* rising) noexcept {
    *rising=false;
    const float sink=-climb;
    float t=-1.0f;
    if(clear!=kNoGround && sink>gentle && clear<sink*within)t=clear/sink;
    const float end[3]={pos[0]+vel[0]*within,pos[1]+vel[1]*within,pos[2]+vel[2]*within};
    float hit[3];
    const float d=MapRay(pos,end,hit);
    if(d<0.0f)return t;
    const float under=clear!=kNoGround ? pos[1]-clear : hit[1];
    const bool up=hit[1]>under+kRise;
    if(!(sink>gentle) && !up)return t;   // the ground ahead met in a landing's glide: a landing
    const float speed=std::sqrt(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]);
    const float ahead=speed>0.1f ? d/speed : 0.0f;
    if(t<0.0f || ahead<t){t=ahead;*rising=up;}
    return t;
}

Gpws GpwsOf(float impactIn,bool rising) noexcept {
    if(impactIn<0.0f || impactIn>kTerrainSeconds)return Gpws::none;
    if(impactIn<=kPullUpSeconds)return Gpws::pullUp;
    return rising ? Gpws::terrain : Gpws::sinkRate;
}

void WarnTick() noexcept {
    const ULONGLONG now=GetTickCount64();
    if(audio::Running())audio::Beat(GameEffectVolume());   // the warnings' own beat: the jets' engines may be off or absent
    PlayerJetReadout j{};
    PlayerHeliReadout h{};
    const PlayerJetSymbols* y=nullptr;
    const HeliFlight* f=nullptr;
    Gpws g=Gpws::none;
    bool stall=false;
    const FuelReading* fuel=nullptr;
    if(PlayerJetHud(&j)){y=&j.sym;g=j.gpws;stall=j.stall;f=j.rotor ? &j.heli : nullptr;fuel=&j.fuel;}
    else if(PlayerHeliHud(&h)){y=&h.sym;g=h.f.gpws;f=&h.f;fuel=&h.fuel;}
    if(!y) {
        if(flown){state.on=0;Publish(false);}
        missilesSeen=0;
        return;
    }
    unsigned on=0;
    if(g==Gpws::pullUp)on|=1u<<kWarnPullUp;
    else if(g==Gpws::terrain)on|=1u<<kWarnTerrain;
    else if(g==Gpws::sinkRate || (f && !f->landed && f->ground && f->clear<kSinkLow && f->climb<-kSinkWarn))on|=1u<<kWarnSinkRate;
    if(stall)on|=1u<<kWarnStall;
    int missiles=0;
    bool locked=false;
    for(int i=0;i<y->threats && i<kMostThreats;++i){missiles+=y->threatKind[i]==2;locked=locked || y->threatKind[i]==1;}
    if(missiles)on|=1u<<kWarnMissile;
    if(locked)on|=1u<<kWarnLock;
    if(missiles>=missilesSeen || now-missilesSeenAt>kMissileMemoryMs) {
        if(missiles>missilesSeen)state.launchAt=now;
        missilesSeen=missiles;missilesSeenAt=now;
    }
    GearHud gear{};
    if(GearHudLatest(&gear) && now-gear.tick<=kGearFreshMs) {
        if(gear.warn)on|=1u<<kWarnGear;
        if(gear.overspeed)on|=1u<<kWarnGearSpeed;
        if(gear.blocked)on|=1u<<kWarnWow;
    }
    if(fuel && FuelLow(*fuel))on|=1u<<kWarnFuel;
    for(int k=0;k<kWarnCount;++k)if((on>>k&1u) && !(state.on>>k&1u))state.litAt[k]=now;
    state.on=on;state.tick=now;
    Publish(true);
    const bool launch=missiles>0 && state.launchAt && now-state.launchAt<kLaunchMs;
    audio::Cockpit c{missiles ? 2 : locked ? 1 : 0,false,false,0u};
    if(Cfg().warnAudio) {
        c.launch=launch;c.stall=stall;
        const struct { int warn; int call; } kCalls[]={{kWarnPullUp,audio::kCallPullUp},{kWarnStall,audio::kCallStall},
            {kWarnTerrain,audio::kCallTerrain},{kWarnSinkRate,audio::kCallSinkRate},{kWarnGear,audio::kCallGear}};
        for(const auto& m:kCalls)if(on>>m.warn&1u)c.callouts|=1u<<m.call;
        if(launch)c.callouts|=1u<<audio::kCallMissile;
    }
    if(c.threat || c.stall || c.callouts || audio::Running())audio::Warn(c);
}

bool WarnLatest(Warnings* out) noexcept {
    AcquireSRWLockShared(&publishLock);
    const bool fresh=flown && GetTickCount64()-published.tick<=kGearFreshMs;
    if(fresh)*out=published;
    ReleaseSRWLockShared(&publishLock);
    return fresh;
}
}  // namespace crew
