// Airstrike takeovers (docs/mission-airstrike-re.md): the bombers the game flies past on rails become jets
// the plugin flies (jet.cpp), which drop the bombers' own bombs, can be shot down, and stay on as strike
// jets for their sortie.
//  - Every bomber: both calls of BombingPlane_Init (0x5AABB0) are redirected, the Air Raider's bomber
//    call (from its IndirectFireControl's step, 0x2B924E) and the missions' strafing planes
//    (DemoAirStrike's ctor, 0x5B4423: RM034A/B, M116, M118). After the stock init the plugin launches a
//    bomber jet where the plane starts, along its heading, carrying its payload (JetLaunchBomber: the
//    same bombs, damage, spread, seed and owner, released from the jet), and the plane is deleted at its
//    first update (its vtable's slot 5), before it moves or drops a thing. The DemoAirStrike deletes
//    itself once its plane is gone; the Air Raider's call keeps no hold of its planes.
//  - The Air Raider's call (its one call of IFC_Start, 0x6A8DFB): with enemies in the air round the
//    target, fighters go along as escorts (up to cfg.jetMaxPerCall), from 1 km behind the target as seen
//    from the player.
// When no jet can be launched (the jet SGOs missing or not preloaded this mission) the stock bombers fly.
// The other scripted strikes (DemoIndirectFire, gunship fire, missiles, satellite laser) are shells out
// of the sky with no plane to take over, and stay stock.
#include "crew.h"
#include "memory.h"
#include <cmath>

namespace crew {
namespace {
constexpr unsigned kIfcStart=0x2B5DA0,kRadioCall=0x6A8DFB;
constexpr std::size_t kIfcPlanes=0x80,kStartTarget=0x50;
constexpr unsigned kBomberInit=0x5AABB0,kRadioBomber=0x2B924E,kMissionBomber=0x5B4423;
constexpr unsigned kPlaneUpdateSlot=0x17D3A30+5*8,kPlaneUpdate=0x5AB240,kDelete=0x118A1B0;
constexpr std::size_t kPlaneVelocity=0xB80;
constexpr float kApproach=1000.0f,kAboveTarget=150.0f,kWingSpacing=70.0f,kWingStep=15.0f;
constexpr float kLookRadius=350.0f,kFlyerClear=15.0f;

const unsigned char kIfcStartSig[]={0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x60,0x48,0x8B,0x05};
// call IFC_Start; then the caller marks the call active and copies the plane count (+0x16E0 -> +0x16E4)
const unsigned char kRadioCallSig[]={0xE8,0xA0,0xCF,0xC0,0xFF,0xC6,0x87,0xEC,0x16,0x00,0x00,0x01,0x8B,0x87,0xE0,0x16};
const unsigned char kBomberInitSig[]={0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41};
const unsigned char kRadioBomberSig[]={0xE8,0x5D,0x19,0x2F,0x00,0x90,0x48,0x8B,0x4D,0x18};
const unsigned char kMissionBomberSig[]={0xE8,0x88,0x67,0xFF,0xFF,0x90,0xBB,0xFF,0xFF,0xFF};
const unsigned char kPlaneUpdateSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89};

using IfcStartFn=std::uintptr_t(__fastcall*)(void*,const void*);
// BombingPlane_Init(plane, &target, &owner weak, damage, spread, speed a frame, target_adjust,
// target_distance, &bombing_plane_param, seed)
using BomberInitFn=void(__fastcall*)(unsigned char*,const float*,const void*,float,float,float,float,float,const void*,std::int32_t);
using PlaneUpdateFn=void(__fastcall*)(unsigned char*,const void*);
using DeleteFn=void(*)(void*);
PlaneUpdateFn nextPlaneUpdate=nullptr;

// Launch sources (JetLaunch): an Air Raider's call (its escorts and bombers), a mission's strike.
const char kRadioSource='r',kMissionSource='m';   // distinct values: identical constants may be folded

// Bombers whose jets fly instead, by their weak-this control block: deleted at their first update.
const void* doomed[32]{};
unsigned doomNext=0;

// Enemies round the target: those well off the ground count as flyers.
struct Census { const float* at; int flyers,ground; };
void Count(void* ctx,const void*,const float* p) noexcept {
    auto& c=*static_cast<Census*>(ctx);
    const float dx=p[0]-c.at[0],dz=p[2]-c.at[2];
    if(dx*dx+dz*dz>kLookRadius*kLookRadius)return;
    const float down[3]={p[0],p[1]-200.0f,p[2]};
    float hit[3];
    const bool flying=MapRay(p,down,hit)<0.0f || p[1]-hit[1]>kFlyerClear;
    (flying ? c.flyers : c.ground)+=1;
}

// Escort fighters for a bomber call of `planes` at `target` (see the file comment): all of them when the
// enemies there mostly fly, one when some do; how many went.
int LaunchEscorts(int planes,const float* target) noexcept {
    Census census{target,0,0};
    VisitEnemiesOf(player.at ? player.team : 0,&Count,&census);
    const int cap=planes<1 ? 1 : planes>cfg.jetMaxPerCall ? cfg.jetMaxPerCall : planes;
    const int n=census.flyers==0 ? 0 : census.flyers>census.ground ? cap : 1;
    if(n==0)return 0;
    float dir[3]={0,0,1};
    if(player.at && GetTickCount64()-player.at<10000) {
        const float dx=target[0]-player.pos[0],dz=target[2]-player.pos[2],l=std::sqrt(dx*dx+dz*dz);
        if(l>5.0f){dir[0]=dx/l;dir[2]=dz/l;}
    }
    const float side[3]={dir[2],0,-dir[0]};
    int launched=0;
    for(int i=0;i<n;++i) {
        const float off=(static_cast<float>(i)-static_cast<float>(n-1)*0.5f)*kWingSpacing;
        const float from[3]={target[0]-dir[0]*kApproach+side[0]*off,target[1]+kAboveTarget+kWingStep*static_cast<float>(i),
                             target[2]-dir[2]*kApproach+side[2]*off};
        if(JetLaunch(true,from,dir,target,cfg.jetSortieSec,&kRadioSource))++launched;
    }
    Log("AIRSTRIKE escorts: %d/%d fighters (enemies there: %d flying, %d on the ground) at (%.0f,%.0f,%.0f)",
        launched,n,census.flyers,census.ground,target[0],target[1],target[2]);
    return launched;
}

// The Air Raider's bomber call (redirected call at kRadioCall): IFC_Start(ifc = weapon+0x1660, params).
std::uintptr_t __fastcall RadioStartHook(void* ifc,const void* params) {
    const auto result=reinterpret_cast<IfcStartFn>(image+kIfcStart)(ifc,params);
    if(!cfg.enabled || !cfg.jetAirRaider)return result;
    __try {
        const std::int32_t planes=At<std::int32_t>(ifc,kIfcPlanes);
        const float* target=reinterpret_cast<const float*>(static_cast<const unsigned char*>(params)+kStartTarget);
        if(planes>0 && std::isfinite(target[0]+target[1]+target[2]))LaunchEscorts(planes,target);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

// After the stock init of `plane`: its jet, and the plane doomed (see the file comment).
void TakeOver(const char* who,unsigned char* plane,const float* target,const BombLoad& load,const void* source) noexcept {
    __try {
        const float* from=reinterpret_cast<const float*>(plane+kPosition);
        const float* heading=reinterpret_cast<const float*>(plane+kPlaneVelocity);
        if(!std::isfinite(target[0]+target[1]+target[2]) || !JetLaunchBomber(from,heading,target,load,cfg.jetSortieSec,source))return;
        doomed[doomNext++%32]=At<const void*>(plane,kSelfCtrl);
        Log("AIRSTRIKE %s bomber %p: its jet drops the bombs",who,plane);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void __fastcall RadioBomberHook(unsigned char* plane,const float* target,const void* owner,float damage,float spread,
                                float speed,float adjust,float reach,const void* param,std::int32_t seed) {
    reinterpret_cast<BomberInitFn>(image+kBomberInit)(plane,target,owner,damage,spread,speed,adjust,reach,param,seed);
    if(cfg.enabled && cfg.jetAirRaider)TakeOver("air raider",plane,target,BombLoad{owner,damage,spread,speed,adjust,reach,param,seed},&kRadioSource);
}

void __fastcall MissionBomberHook(unsigned char* plane,const float* target,const void* owner,float damage,float spread,
                                  float speed,float adjust,float reach,const void* param,std::int32_t seed) {
    reinterpret_cast<BomberInitFn>(image+kBomberInit)(plane,target,owner,damage,spread,speed,adjust,reach,param,seed);
    if(cfg.enabled && cfg.jetMissionStrike)TakeOver("mission",plane,target,BombLoad{owner,damage,spread,speed,adjust,reach,param,seed},&kMissionSource);
}

// BombingPlane slot 5 (update): a doomed plane is deleted instead.
void __fastcall PlaneUpdateHook(unsigned char* plane,const void* frame) {
    __try {
        const void* const ctrl=At<const void*>(plane,kSelfCtrl);
        for(auto& d:doomed) {
            if(!ctrl || d!=ctrl)continue;
            d=nullptr;
            reinterpret_cast<DeleteFn>(image+kDelete)(plane);
            return;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    nextPlaneUpdate(plane,frame);
}

bool Redirect(unsigned site,const unsigned char* sig,std::size_t size,void* hook,const char* name) noexcept {
    if(!Matches(site,sig,size)){Log("AIRSTRIKE %s: profile mismatch",name);return false;}
    bool changed=false;
    const bool ok=RedirectCall(image+site,image+kBomberInit,hook,changed);
    if(!ok && changed)Log("AIRSTRIKE %s half patched",name);
    return ok;
}
}  // namespace

bool InstallAirstrikes() noexcept {
    __try {
        bool escorts=false,radio=false,mission=false;
        if(Matches(kIfcStart,kIfcStartSig,sizeof(kIfcStartSig)) && Matches(kRadioCall,kRadioCallSig,sizeof(kRadioCallSig))) {
            bool changed=false;
            escorts=RedirectCall(image+kRadioCall,image+kIfcStart,reinterpret_cast<void*>(&RadioStartHook),changed);
            if(!escorts && changed)Log("AIRSTRIKE radio call half patched");
        } else Log("AIRSTRIKE bomber call: profile mismatch");
        // The plane update first: a bomber taken over must never fly.
        const auto slot=reinterpret_cast<void**>(image+kPlaneUpdateSlot);
        bool update=false;
        if(Matches(kBomberInit,kBomberInitSig,sizeof(kBomberInitSig)) && Matches(kPlaneUpdate,kPlaneUpdateSig,sizeof(kPlaneUpdateSig)) && *slot) {
            void* const current=*slot;
            if(current!=image+kPlaneUpdate)Log("AIRSTRIKE plane update: chaining onto %p (another plugin)",current);
            nextPlaneUpdate=reinterpret_cast<PlaneUpdateFn>(current);
            update=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&PlaneUpdateHook));
        } else Log("AIRSTRIKE bomber: profile mismatch");
        if(update) {
            radio=Redirect(kRadioBomber,kRadioBomberSig,sizeof(kRadioBomberSig),reinterpret_cast<void*>(&RadioBomberHook),"air raider bomber");
            mission=Redirect(kMissionBomber,kMissionBomberSig,sizeof(kMissionBomberSig),reinterpret_cast<void*>(&MissionBomberHook),"mission bomber");
        }
        Log("HOOK airstrikes escorts=%d airRaiderBombers=%d missionBombers=%d",escorts,radio,mission);
        return escorts || radio || mission;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
