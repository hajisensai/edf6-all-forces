// Airstrike takeovers (docs/mission-airstrike-re.md): the planes the game flies past on rails become
// jets the plugin flies (jet.cpp), which can be shot down and hunt their targets for a sortie.
//  - The Air Raider's bomber calls (Weapon_RadioContact): its one call of IFC_Start (0x6A8DFB) is
//    redirected; after the stock start the plugin launches up to cfg.jetMaxPerCall jets from 1 km behind
//    the target as seen from the player, fighters when the enemies there mostly fly, and zeroes the
//    plane count the caller then copies, so the stock bombers never come (the weapon goes back to idle).
//  - The missions' strafing planes (DemoAirStrike, RM034A/B, M116, M118): its factory's create
//    (vtable slot 2) runs the stock one, which makes the BombingPlane, then launches a strike jet from
//    where that plane starts, along its heading, and deletes the plane; the DemoAirStrike deletes itself
//    once its plane is gone.
// When no jet can be launched (the jet SGOs missing or not preloaded this mission) the stock planes fly.
// The other scripted strikes (DemoIndirectFire, gunship fire, missiles, satellite laser) are shells out
// of the sky with no plane to take over, and stay stock.
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <iterator>

namespace crew {
namespace {
constexpr unsigned kIfcStart=0x2B5DA0,kRadioCall=0x6A8DFB;
constexpr std::size_t kIfcPlanes=0x80,kIfcLeft=0x84,kStartTarget=0x50;
constexpr unsigned kStrikeFactorySlot=0x17D46B0,kStrikeCreate=0x5B3F00,kStrikePlaneStore=0x5B4481;
constexpr unsigned kBombingPlaneVtable=0x17D3A30,kDelete=0x118A1B0;
constexpr std::size_t kStrikePlane=0x168,kStrikePlaneCtrl=0x170,kCtrlUses=8;
constexpr float kApproach=1000.0f,kAboveTarget=150.0f,kMinClear=100.0f,kWingSpacing=70.0f,kWingStep=15.0f;
constexpr float kLookRadius=350.0f,kFlyerClear=15.0f;

const unsigned char kIfcStartSig[]={0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x60,0x48,0x8B,0x05};
// call IFC_Start; then the caller marks the call active and copies the plane count (+0x16E0 -> +0x16E4)
const unsigned char kRadioCallSig[]={0xE8,0xA0,0xCF,0xC0,0xFF,0xC6,0x87,0xEC,0x16,0x00,0x00,0x01,0x8B,0x87,0xE0,0x16};
const unsigned char kStrikeCreateSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0xB9,0x80,0x01,0x00,0x00,0xE8,0x9D};
// After SetTeam(plane, 2, 1): the plane's weak-this (object +0x28, control +0x30) stored at +0x168/+0x170.
const unsigned char kStrikePlaneSig[]={0x48,0x8B,0x47,0x30,0x49,0x8B,0xD7,0x48,0x85,0xC0,0x74,0x0B,0x4C,0x8B,0x7F,0x28,
                                       0xF0,0xFF,0x40,0x0C,0x48,0x8B,0xD0,0x4D,0x89,0xBE,0x68,0x01,0x00,0x00,0x49,0x8B,
                                       0x8E,0x70,0x01,0x00,0x00,0x49,0x89,0x96,0x70,0x01,0x00,0x00};

using IfcStartFn=std::uintptr_t(__fastcall*)(void*,const void*);
using CreateFn=unsigned char*(__fastcall*)(void*,void*);
using DeleteFn=void(*)(void*);
CreateFn nextStrikeCreate=nullptr;

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

// Raises `p` to at least kMinClear over the ground (terrain or buildings) under it.
void ClearGround(float* p) noexcept {
    const float top[3]={p[0],p[1]+600.0f,p[2]},bottom[3]={p[0],p[1]-1500.0f,p[2]};
    float hit[3];
    if(MapRay(top,bottom,hit)>=0.0f && p[1]<hit[1]+kMinClear)p[1]=hit[1]+kMinClear;
}

// Launches up to `planes` jets at `target` (see the file comment); how many went.
int LaunchFlight(int planes,const float* target) noexcept {
    const int n=planes<1 ? 1 : planes>cfg.jetMaxPerCall ? cfg.jetMaxPerCall : planes;
    float dir[3]={0,0,1};
    if(player.at && GetTickCount64()-player.at<10000) {
        const float dx=target[0]-player.pos[0],dz=target[2]-player.pos[2],l=std::sqrt(dx*dx+dz*dz);
        if(l>5.0f){dir[0]=dx/l;dir[2]=dz/l;}
    }
    Census census{target,0,0};
    VisitEnemiesOf(player.at ? player.team : 0,&Count,&census);
    const int fighters=census.flyers>census.ground ? n : census.flyers>0 && n>=2 ? 1 : 0;
    const float side[3]={dir[2],0,-dir[0]};
    int launched=0;
    for(int i=0;i<n;++i) {
        const float off=(static_cast<float>(i)-static_cast<float>(n-1)*0.5f)*kWingSpacing;
        float from[3]={target[0]-dir[0]*kApproach+side[0]*off,target[1]+kAboveTarget+kWingStep*static_cast<float>(i),
                       target[2]-dir[2]*kApproach+side[2]*off};
        ClearGround(from);
        const bool fighter=i<fighters;
        if(JetLaunch(fighter,from,dir,target,cfg.jetSortieSec) || (fighter && JetLaunch(false,from,dir,target,cfg.jetSortieSec)))++launched;
    }
    Log("AIRSTRIKE flight: %d/%d jets (%d fighters; enemies there: %d flying, %d on the ground) at (%.0f,%.0f,%.0f)",
        launched,n,fighters,census.flyers,census.ground,target[0],target[1],target[2]);
    return launched;
}

// The Air Raider's bomber call (redirected call at kRadioCall): IFC_Start(ifc = weapon+0x1660, params).
std::uintptr_t __fastcall RadioStartHook(void* ifc,const void* params) {
    const auto result=reinterpret_cast<IfcStartFn>(image+kIfcStart)(ifc,params);
    if(!cfg.enabled || !cfg.jetAirRaider)return result;
    __try {
        auto c=static_cast<unsigned char*>(ifc);
        const std::int32_t planes=At<std::int32_t>(c,kIfcPlanes);
        const float* target=reinterpret_cast<const float*>(static_cast<const unsigned char*>(params)+kStartTarget);
        if(planes<=0 || !std::isfinite(target[0]+target[1]+target[2]))return result;
        if(LaunchFlight(planes,target)>0){Put<std::int32_t>(c,kIfcPlanes,0);Put<std::int32_t>(c,kIfcLeft,0);}
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

// DemoAirStrike's factory create (slot 2): (factory, params) -> the new DemoAirStrike.
unsigned char* __fastcall StrikeCreateHook(void* factory,void* params) {
    unsigned char* strike=nextStrikeCreate(factory,params);
    if(!strike || !cfg.enabled || !cfg.jetMissionStrike)return strike;
    __try {
        const auto plane=At<unsigned char*>(strike,kStrikePlane);
        const auto ctrl=At<const unsigned char*>(strike,kStrikePlaneCtrl);
        if(!plane || !ctrl || At<std::int32_t>(ctrl,kCtrlUses)<=0 || At<const unsigned char*>(plane,0)!=image+kBombingPlaneVtable)return strike;
        const float* from=reinterpret_cast<const float*>(plane+kPosition);
        const float* heading=reinterpret_cast<const float*>(strike+kMatrix+0x20);
        const float* target=reinterpret_cast<const float*>(strike+kPosition);
        if(!JetLaunch(false,from,heading,target,cfg.jetSortieSec))return strike;
        reinterpret_cast<DeleteFn>(image+kDelete)(plane);
        Log("AIRSTRIKE mission strafing plane %p replaced by a jet",plane);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return strike;
}
}  // namespace

bool InstallAirstrikes() noexcept {
    __try {
        bool radio=false,mission=false;
        if(Matches(kIfcStart,kIfcStartSig,sizeof(kIfcStartSig)) && Matches(kRadioCall,kRadioCallSig,sizeof(kRadioCallSig))) {
            bool changed=false;
            radio=RedirectCall(image+kRadioCall,image+kIfcStart,reinterpret_cast<void*>(&RadioStartHook),changed);
            if(!radio && changed)Log("AIRSTRIKE radio call half patched");
        } else Log("AIRSTRIKE bomber call: profile mismatch");
        const auto slot=reinterpret_cast<void**>(image+kStrikeFactorySlot);
        if(Matches(kStrikeCreate,kStrikeCreateSig,sizeof(kStrikeCreateSig)) &&
           Matches(kStrikePlaneStore,kStrikePlaneSig,sizeof(kStrikePlaneSig)) && *slot) {
            void* const current=*slot;
            if(current!=image+kStrikeCreate)Log("AIRSTRIKE mission strike: chaining onto %p (another plugin)",current);
            nextStrikeCreate=reinterpret_cast<CreateFn>(current);
            mission=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&StrikeCreateHook));
        } else Log("AIRSTRIKE mission strike: profile mismatch");
        Log("HOOK airstrikes bomberCall=%d missionStrike=%d",radio,mission);
        return radio || mission;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
