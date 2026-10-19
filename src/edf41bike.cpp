// The original 4.1 408 uses one gun bone and its original two-axis ragdoll
// joint, not the Car's two separate turret bodies. Old VehicleBike 2F5900
// calls 2D6BE0 with the SAME local matrix twice, then builds worlds and sends
// animation-driven proxies to physics. EDF6 VehicleWeaponAim::Pose 5FACD0
// implements that same-bone operation too. Keep the real source's eight
// proxies; the data adapter explicitly selects gun simulation row 1 -> native
// mode 2 after CarBase resets all proxies to dynamic mode 4. No guessed angular
// velocity, fabricated hinge or substitute bike.
#include "crew.h"
#include "edf41bike.h"
#include "body506.h"
#include "ifc_model.h"
#include "layout.h"
#include "memory.h"
#include <cmath>
#include <cstdint>

namespace crew {
namespace {
constexpr unsigned kCarVtable=0x17E01B0,kCarFrame=0x65A850,kCarPose=0x65A690;
constexpr unsigned kWeaponCall=0x65B37E,kSetWeapon=0x633330;
constexpr unsigned kAimVtable=0x17D8A68,kAimSeVtable=0x17D8A90;
constexpr unsigned kAimStep=0x5FBDA0,kAimSeStep=0x5FCD80,kAimPose=0x5FACD0,kAimRegister=0x5FB790;
constexpr unsigned kPhysicsToBones=0x6EDCA0,kBuildWorld=0x1100B90,kBonesToPhysics=0x6ED800,kPull=0x62C000;
constexpr std::size_t kRagdoll=0x1320,kCas=0x1180,kSgo=0x100,kSeatRightX=0x2D0,kSeatRightY=0x2D4,kSeatFire=0x2E4;
constexpr std::size_t kSeatHolders=0xC8,kSeatHoldersCount=0xD8;
constexpr int kFrameSlot=4,kPoseSlot=45;
using VehicleFn=void(__fastcall*)(unsigned char*);
using SetWeaponFn=void(__fastcall*)(unsigned char*,void*,void*,int);
using AimStepFn=void(__fastcall*)(unsigned char*,const float*);
using AimPoseFn=void(__fastcall*)(unsigned char*,float*,float*);
struct AxisMap { int bone; float lo,hi; };
static_assert(sizeof(AxisMap)==12,"5FB1E0 copies a native 12-byte axis mapping");
using AimRegisterFn=void(__fastcall*)(unsigned char*,int,const AxisMap*);
using SyncFn=void(__fastcall*)(void*);
using WorldFn=void(__fastcall*)(void*,const void*);
using PullFn=void(__fastcall*)(void*);
void *nextFrame=nullptr,*nextPose=nullptr;
bool on=false;
const unsigned char kWeaponCallCode[]={0xE8,0xAD,0x7F,0xFD,0xFF};
const unsigned char kCarFrameCode[]={0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0xE8,0xE2,0x91,0x01,0x00};
const unsigned char kCarPoseCode[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48};
const unsigned char kAimPoseCode[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x57};
const unsigned char kAimRegisterCode[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x63,0xC2,0x48,0x8B,0xD9,0x4D,0x8B,0xD0};
const unsigned char kPhysicsToBonesCode[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48};
const unsigned char kBonesToPhysicsCode[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48};
const unsigned char kBuildWorldCode[]={0xC6,0x81,0xB0,0x00,0x00,0x00,0x01,0x0F,0x28,0x02,0x48,0x8B};

bool Tagged(const unsigned char* v) noexcept {
    if(!Readable(v,kSgo+0x18) || At<const void*>(v,0)!=image+kCarVtable || SeatCount(v)!=2)return false;
    const auto* root=v+kSgo;
    const auto alt=ifcmodel::AlternativeOf(root);
    if(alt>=ifcmodel::kVariantAlternatives)return false;
    ifcmodel::Find found{edf41bike::kSchema,-1};
    ifcmodel::Visitor(image,ifcmodel::kVariantFind,alt)(root,&found);
    return found.index>=0;
}
unsigned char* Operator(unsigned char* v) noexcept {
    return SeatCount(v)==2 ? SeatAt(v,edf41bike::kOperatorSeat) : nullptr;
}
unsigned char* Aim(unsigned char* seat) noexcept { return seat+kSeatAim; }
bool KnownAim(const unsigned char* aim) noexcept {
    if(!Readable(aim,0xC4))return false;
    const void* vt=At<const void*>(aim,0);
    return vt==image+kAimVtable || vt==image+kAimSeVtable;
}
void Limits(unsigned char* aim) noexcept {
    const auto limits=edf41bike::kNativeLimits;
    const float lo[2]={limits.yawLo,limits.pitchLo},hi[2]={limits.yawHi,limits.pitchHi};
    for(int i=0;i<2;++i) {
        const auto at=kAimAxes+static_cast<std::size_t>(i)*kAxisStride;
        Put<float>(aim,at+kAxisMin,lo[i]);Put<float>(aim,at+kAxisMax,hi[i]);
        Put<float>(aim,at+kAxisAngle,edf41bike::Bound(At<float>(aim,at+kAxisAngle),lo[i],hi[i]));
    }
}
// A registered bone mapping is also the native network-send admission bit:
// 6770B9 calls 5FB7E0, which requires a nonempty map on at least one axis.
// Constructor-time models normally exist; a still-loading model initializes
// once when its bone records arrive. Never copy the vectors or their owners.
void RegisterMappings(unsigned char* v,unsigned char* aim) noexcept {
    auto* gun=BoneRecord506(v+kModelInst506,L"gun");
    if(!gun)return;
    const int bone=At<std::int32_t>(gun,0x0C);
    const int count=At<std::int32_t>(v+kModelInst506,kInstBoneCount);
    if(bone<0 || bone>=count)return;
    const auto limits=edf41bike::kNativeLimits;
    const AxisMap maps[2]={{bone,limits.yawLo,limits.yawHi},{bone,limits.pitchLo,limits.pitchHi}};
    for(int i=0;i<2;++i) {
        const auto at=kAimAxes+static_cast<std::size_t>(i)*kAxisStride;
        if(At<std::uint64_t>(aim,at+0x38)==0)
            reinterpret_cast<AimRegisterFn>(image+kAimRegister)(aim,i,&maps[i]);
    }
    Limits(aim);
}
// This is the Car's own weapon-building call, with the real gunner's embedded
// Aim as its second argument. Native 633330/5FB810 fills speed/acceleration
// from the source gun entry. Never copy an Aim containing shared references.
void __fastcall SetWeapon(unsigned char* v,void* aim,void* entry,int holder) {
    unsigned char* target=nullptr;
    __try {
        if(on && Tagged(v)) {
            auto* seat=Operator(v);
            if(seat && KnownAim(Aim(seat)))target=Aim(seat);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { target=nullptr; }
    reinterpret_cast<SetWeaponFn>(image+kSetWeapon)(v,target ? target : aim,entry,holder);
    if(target)RegisterMappings(v,target);
}

bool Ours(unsigned char* v) noexcept {
    return Tagged(v) && BoneRecord506(v+kModelInst506,edf41bike::kMarker) && BoneRecord506(v+kModelInst506,L"gun");
}
void Update(unsigned char* v) noexcept {
    auto* seat=Operator(v);
    if(!seat || !KnownAim(Aim(seat)))return;
    RegisterMappings(v,Aim(seat));
    const Rider rider=SeatRider(seat);
    const bool occupied=rider==Rider::dummy || edf::LivingSoldierInSeat(image,seat);
    const bool local=rider==Rider::player;
    const bool npc=AiGunner(v,seat);
    const float fire=At<float>(seat,kSeatFire);
    const auto decision=edf41bike::Decide(Cfg().enabled,v[kDead]!=0,occupied,local,npc,MapHoldsKeys(),std::isfinite(fire) && fire>=0.8f,At<std::uint8_t>(Aim(seat),0xC0)!=0);
    if(!decision.update)return;
    alignas(16) float input[4]{};
    if(decision.input) {
        const float x=At<float>(seat,kSeatRightX),y=At<float>(seat,kSeatRightY);
        if(std::isfinite(x) && std::isfinite(y)){input[0]=-x;input[1]=y;}
    }
    auto* aim=Aim(seat);
    Limits(aim);
    const auto table=At<void* const*>(aim,0);
    // Remote Aim objects take their native +C0 interpolation branch. They
    // advance their replicated angles here but never pull a trigger twice.
    // NPC/Dummy seats are empty on remote peers, so +C0 also admits an
    // interpolation-only update with no occupant and no input/fire rights.
    reinterpret_cast<AimStepFn>(table[2])(aim,input);
    Limits(aim);
    if(!decision.fire)return;
    const auto holders=At<void* const*>(seat,kSeatHolders);
    const auto count=At<std::uint64_t>(seat,kSeatHoldersCount);
    if(count!=1 || !Readable(holders,sizeof(void*)))return;
    reinterpret_cast<PullFn>(image+kPull)(holders[0]);
}
void __fastcall Frame(unsigned char* v) {
    reinterpret_cast<VehicleFn>(nextFrame)(v);
    if(!on)return;
    __try { if(Ours(v))Update(v); }
    __except(EXCEPTION_EXECUTE_HANDLER) { Log("EDF41BIKE gunner frame rejected unreadable native state v=%p",v); }
}
void Pose408(unsigned char* v,unsigned char* gun,unsigned char* aim) noexcept {
    reinterpret_cast<SyncFn>(image+kPhysicsToBones)(v+kRagdoll);
    auto* local=reinterpret_cast<float*>(gun+kBoneLocal506);
    // The same local matrix twice is the original 408's single-bone branch.
    // gun is an animation-driven proxy: the final sync moves its actual
    // collision body and retains the original ragdoll's two-axis limits.
    reinterpret_cast<AimPoseFn>(image+kAimPose)(aim,local,local);
    reinterpret_cast<WorldFn>(image+kBuildWorld)(v+kModelInst506,v+0x60);
    reinterpret_cast<SyncFn>(image+kBonesToPhysics)(v+kRagdoll);
}
void __fastcall Pose(unsigned char* v) {
    bool ours=false;unsigned char *gun=nullptr,*aim=nullptr;
    __try {
        if(on && Cfg().enabled && !v[kDead] && Ours(v) && !At<void*>(v,kCas)) {
            auto* seat=Operator(v);
            if(seat && KnownAim(Aim(seat))) {
                gun=BoneRecord506(v+kModelInst506,L"gun");aim=Aim(seat);ours=gun!=nullptr;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ours=false; }
    if(!ours){reinterpret_cast<VehicleFn>(nextPose)(v);return;}
    __try { Pose408(v,gun,aim); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("EDF41BIKE pose rejected unreadable native state v=%p; native Car pose",v);
        reinterpret_cast<VehicleFn>(nextPose)(v);
    }
}
}  // namespace
bool IsEdf41Bike(const void* vehicle) noexcept {
    __try { return Ours(static_cast<unsigned char*>(const_cast<void*>(vehicle))); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool InstallEdf41Bike() noexcept {
    if(on)return true;
    bool known=false;
    __try {
        known=Matches(kCarFrame,kCarFrameCode,sizeof(kCarFrameCode)) && Matches(kCarPose,kCarPoseCode,sizeof(kCarPoseCode)) &&
              Matches(kWeaponCall,kWeaponCallCode,sizeof(kWeaponCallCode)) && Matches(kAimPose,kAimPoseCode,sizeof(kAimPoseCode)) &&
              Matches(kAimRegister,kAimRegisterCode,sizeof(kAimRegisterCode)) &&
              Matches(kPhysicsToBones,kPhysicsToBonesCode,sizeof(kPhysicsToBonesCode)) &&
              Matches(kBonesToPhysics,kBonesToPhysicsCode,sizeof(kBonesToPhysicsCode)) && Matches(kBuildWorld,kBuildWorldCode,sizeof(kBuildWorldCode));
        known=known && At<const void*>(image+kAimVtable,16)==image+kAimStep && At<const void*>(image+kAimSeVtable,16)==image+kAimSeStep &&
               At<const void*>(image+kAimVtable,24)==image+kAimPose && At<const void*>(image+kAimSeVtable,24)==image+kAimPose;
    } __except(EXCEPTION_EXECUTE_HANDLER) { known=false; }
    if(!known){Log("EDF41BIKE refused: native Car/Aim/physics profile differs");return false;}
    auto table=reinterpret_cast<void**>(image+kCarVtable);
    bool changed=false;
    const bool weapon=RedirectCall(image+kWeaponCall,image+kSetWeapon,reinterpret_cast<void*>(&SetWeapon),changed);
    const bool frame=weapon && edf::ChainVtableSlot(table+kFrameSlot,reinterpret_cast<void*>(&Frame),&nextFrame);
    const bool pose=frame && edf::ChainVtableSlot(table+kPoseSlot,reinterpret_cast<void*>(&Pose),&nextPose);
    on=weapon && frame && pose;
    Log("EDF41BIKE source 408: seat1 native build=%d input=%d single-bone physical pose=%d",weapon,frame,pose);
    return on;
}
}  // namespace crew
