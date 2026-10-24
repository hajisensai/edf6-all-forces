// The original 4.1 408 uses one gun bone and its original two-axis ragdoll
// joint, not the Car's two separate turret bodies. Old VehicleBike 2F5900
// calls 2D6BE0 with the SAME local matrix twice, then builds worlds and sends
// animation-driven proxies to physics. EDF6 VehicleWeaponAim::Pose 5FACD0
// implements that same-bone operation too. Keep the real source's eight
// proxies; the data adapter gives the gun simulation row 0 (out of the physics
// world, its joint to the body removed, posed from the bone like the wheels):
// as row 1 (native mode 2, keyframed) its ragdoll joint held the dynamic chassis
// where it was made, in the air (docs/edf41-bike-re.md 「悬空」, 2026-10-21 run).
// No guessed angular velocity, fabricated hinge or substitute bike.
#include "crew.h"
#include "edf41bike.h"
#include "host_settings.h"
#include "body506.h"
#include "ifc_model.h"
#include "layout.h"
#include "memory.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace crew {
namespace {
constexpr unsigned kCarVtable=0x17E01B0,kCarFrame=0x65A850,kCarPose=0x65A690;
constexpr unsigned kWeaponCall=0x65B37E,kSetWeapon=0x633330;
// Car setup 0x65A910 builds its guns only when seat 0 owns one: 0x65B14B loads the seat array, 0x65B152 tests seat 0's
// holder count (+0xD8) and 0x65B15A skips the whole build loop (to 0x65B3EF) when it is 0. The 408's one gun belongs to
// seat 1 (vehicle_weapon_setting [['gun',1]]), so no path (call, script, placement) ever built it: no weapon object in
// the vehicle's holder (+0x638), and seat 1's Aim turn parameters (+0x90, 0x5FB810) stayed 0 (2026-10-21 run).
constexpr unsigned kGunGate=0x65B14B,kGunLoop=0x65B160,kGunSkip=0x65B3EF;
// Inside the loop each vehicle_weapon_setting row's seat (its [1], ebx) is tested at 0x65B31B: another seat than 0 jumps
// to 0x65B3A6 (the next row, no holder index used); seat 0 goes on to 0x65B323 and builds at 0x65B37E.
constexpr unsigned kGunRow=0x65B31B,kRowBuild=0x65B323,kRowSkip=0x65B3A6;
constexpr unsigned kAimVtable=0x17D8A68,kAimSeVtable=0x17D8A90;
constexpr unsigned kAimPose=0x5FACD0,kAimRegister=0x5FB790;
constexpr unsigned kPhysicsToBones=0x6EDCA0,kBuildWorld=0x1100B90,kBonesToPhysics=0x6ED800,kPull=0x62C000;
constexpr std::size_t kRagdoll=0x1320,kCas=0x1180,kSgo=0x100,kSeatRightX=0x2D0,kSeatRightY=0x2D4,kSeatFire=0x2E4;
constexpr std::size_t kSeatHolders=0xC8,kSeatHoldersCount=0xD8;
constexpr int kFrameSlot=4,kPoseSlot=45;
constexpr std::size_t kStubMost=96;
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
// mov rax,[r15+0x608]; cmp qword [rax+0xD8],0; jbe 0x65B3EF
const unsigned char kGunGateCode[]={0x49,0x8B,0x87,0x08,0x06,0x00,0x00,0x48,0x83,0xB8,0xD8,0x00,0x00,0x00,0x00,0x0F,0x86,0x8F,0x02,0x00,0x00};
// test ebx,ebx; jne 0x65B3A6
const unsigned char kGunRowCode[]={0x85,0xDB,0x0F,0x85,0x83,0x00,0x00,0x00};
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
    int why=0;   // Debug=1 log: 1 off / not allowed, 2 not the 408, 3 no known seat-1 Aim, 4 routed, 5 unreadable
    __try {
        why=1;
        if(on && Edf41BikeGunnerAllowed()) {
            why=2;
            if(Tagged(v)) {
                why=3;
                auto* seat=Operator(v);
                if(seat && KnownAim(Aim(seat))){target=Aim(seat);why=4;}
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { target=nullptr;why=5; }
    reinterpret_cast<SetWeaponFn>(image+kSetWeapon)(v,target ? target : aim,entry,holder);
    if(target)RegisterMappings(v,target);
    if(Cfg().debug) {   // Debug=1: where the Car's gun went, and the Aim's turn parameters as 5FB810 left them
        const auto* built=target ? target : static_cast<unsigned char*>(aim);
        Log("EDF41BIKE weapon build v=%p holder %d why=%d aim=%p turn=(%.3f,%.3f,%.3f)",v,holder,why,built,
            At<float>(built,0x90),At<float>(built,0x94),At<float>(built,0x98));
    }
}

// The two stock tests that keep a Car from building a seat-1 gun, and what this asks at each. Only the 408 with its gunner
// seat open (Edf41BikeGunnerAllowed: every member of the room has the seat and its built gun, #219) and its one gun
// holder on seat 1 passes: the native loop then builds that row as it builds any seat-0 gun, its Aim routed to seat 1
// by SetWeapon above (0x633330 -> 0x5FB810 fills the turn speeds). Anything else takes the stock branch.
bool OperatorGun(unsigned char* v) noexcept {
    bool build=false;
    __try {
        if(on && Edf41BikeGunnerAllowed() && Tagged(v)) {
            auto* seat=Operator(v);
            build=seat && KnownAim(Aim(seat)) && At<std::uint64_t>(seat,kSeatHoldersCount)==1;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { build=false; }
    return build;
}
// 0x65B152: seat 0 owns no gun (else the stock test already ran the loop).
bool __fastcall BuildsGun(unsigned char* v) noexcept {
    const bool build=OperatorGun(v);
    if(Cfg().debug)Log("EDF41BIKE gun build gate v=%p: %s",v,build ? "the loop runs for the seat 1 gun" : "stock skip");
    return build;
}
// 0x65B31B: a vehicle_weapon_setting row of another seat than 0 (`seat`, the row's [1]).
bool __fastcall BuildsRow(unsigned char* v,int seat) noexcept {
    const bool build=seat==static_cast<int>(edf41bike::kOperatorSeat) && OperatorGun(v);
    if(Cfg().debug)Log("EDF41BIKE gun row v=%p seat %d: %s",v,seat,build ? "built" : "stock skip");
    return build;
}
// A stub: the stock test first (its own bytes, `test`), when it picks the build the stock way that goes on at once;
// else fn(r15 = the vehicle, edx = ebx) decides between `build` and `skip`. Every register the build or the skip
// reads is non-volatile (r12-r15, rbx, rsi, rdi, rbp, xmm6-11; 0x65B160 / 0x65B323 / 0x65B3A6 / 0x65B3EF write rax,
// rcx, rdx, r8 before reading them), and rsp is 16-aligned in the function's body.
struct Stub { unsigned char code[kStubMost]; std::size_t size; };
Stub DecideStub(const unsigned char* test,std::size_t testSize,unsigned char jumpToBuild,void* fn,std::uintptr_t build,
                std::uintptr_t skip) noexcept {
    Stub out{};
    std::size_t n=0;
    auto put=[&](std::initializer_list<unsigned char> bytes) noexcept { for(auto c:bytes)out.code[n++]=c; };
    auto put64=[&](std::uintptr_t value) noexcept { std::memcpy(out.code+n,&value,8);n+=8; };
    std::memcpy(out.code,test,testSize);n=testSize;
    put({jumpToBuild,0x1D});                       // the stock pick: build (0x1D past the decision below)
    put({0x48,0x83,0xEC,0x20});                    // sub rsp,0x20
    put({0x4C,0x89,0xF9});                         // mov rcx,r15
    put({0x89,0xDA});                              // mov edx,ebx
    put({0x48,0xB8});put64(reinterpret_cast<std::uintptr_t>(fn));   // mov rax,fn
    put({0xFF,0xD0});                              // call rax
    put({0x48,0x83,0xC4,0x20});                    // add rsp,0x20
    put({0x84,0xC0});                              // test al,al
    put({0x74,0x0E});                              // je skip
    put({0xFF,0x25,0,0,0,0});put64(build);         // build: jmp [rip]
    put({0xFF,0x25,0,0,0,0});put64(skip);          // skip: jmp [rip]
    out.size=n;
    return out;
}
// Puts `stub` near `at` and a rel32 jmp to it over the `size` stock bytes (`stock`, checked), nops after.
bool Divert(unsigned rva,const unsigned char* stock,std::size_t size,const Stub& stub) noexcept {
    void* stubAt=AllocateNearCode(image+rva,stub.code,stub.size);
    if(!stubAt)return false;
    unsigned char jump[32];
    std::memset(jump,0x90,size);
    const auto rel=reinterpret_cast<std::intptr_t>(stubAt)-reinterpret_cast<std::intptr_t>(image+rva+5);
    if(rel<INT32_MIN || rel>INT32_MAX)return false;
    const auto rel32=static_cast<std::int32_t>(rel);
    jump[0]=0xE9;std::memcpy(jump+1,&rel32,4);
    return edf::PatchCode(image+rva,stock,jump,size);
}
bool InstallGunGate() noexcept {
    // seat 0's holder count: mov rax,[r15+0x608]; cmp qword [rax+0xD8],0; then jne (a seat-0 gun: the loop)
    const unsigned char countTest[]={0x49,0x8B,0x87,0x08,0x06,0x00,0x00,0x48,0x83,0xB8,0xD8,0x00,0x00,0x00,0x00};
    const unsigned char seatTest[]={0x85,0xDB};   // test ebx,ebx; then je (a seat-0 row: built)
    const Stub gate=DecideStub(countTest,sizeof(countTest),0x75,reinterpret_cast<void*>(&BuildsGun),
                               reinterpret_cast<std::uintptr_t>(image+kGunLoop),reinterpret_cast<std::uintptr_t>(image+kGunSkip));
    const Stub row=DecideStub(seatTest,sizeof(seatTest),0x74,reinterpret_cast<void*>(&BuildsRow),
                              reinterpret_cast<std::uintptr_t>(image+kRowBuild),reinterpret_cast<std::uintptr_t>(image+kRowSkip));
    return Divert(kGunGate,kGunGateCode,sizeof(kGunGateCode),gate) && Divert(kGunRow,kGunRowCode,sizeof(kGunRowCode),row);
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
    if(Cfg().debug && decision.input) {   // Debug=1: the gunner's live angles and trigger, once a second, 120 lines a run
        static ULONGLONG said=0;
        static int lines=0;
        const ULONGLONG now=GetTickCount64();
        if(now-said>=1000 && lines<120) {
            said=now;++lines;
            // +0x90..+0x98 the Aim's turn parameters (5FB810 from the gun entry), axis +0x0C the turn speed (5FBC00)
            // the one holder's weapon (+0x10) and its rounds left (+0xBE8, layout.h kWeaponAmmo): the shots really fired
            const auto gunHolders=At<void* const*>(seat,kSeatHolders);
            const void* gunWeapon=At<std::uint64_t>(seat,kSeatHoldersCount)==1 && Readable(gunHolders,8) && Readable(gunHolders[0],0x18) ?
                At<const void*>(gunHolders[0],0x10) : nullptr;
            const int rounds=gunWeapon && Readable(gunWeapon,0xBEC) ? At<std::int32_t>(gunWeapon,0xBE8) : -1;
            Log("EDF41BIKE gunner v=%p weapon=%p rounds=%d yaw=%.3f [%.3f,%.3f] v%.4f pitch=%.3f [%.3f,%.3f] v%.4f turn=(%.3f,%.3f,%.3f) remote=%d input=(%.2f,%.2f) fire=%d",
                v,gunWeapon,rounds,At<float>(aim,kAimAxes+kAxisAngle),At<float>(aim,kAimAxes+kAxisMin),At<float>(aim,kAimAxes+kAxisMax),At<float>(aim,kAimAxes+0xC),
                At<float>(aim,kAimAxes+kAxisStride+kAxisAngle),At<float>(aim,kAimAxes+kAxisStride+kAxisMin),At<float>(aim,kAimAxes+kAxisStride+kAxisMax),
                At<float>(aim,kAimAxes+kAxisStride+0xC),At<float>(aim,0x90),At<float>(aim,0x94),At<float>(aim,0x98),At<std::uint8_t>(aim,0xC0),
                input[0],input[1],decision.fire ? 1 : 0);
        }
    }
    if(!decision.fire)return;
    const auto holders=At<void* const*>(seat,kSeatHolders);
    const auto count=At<std::uint64_t>(seat,kSeatHoldersCount);
    if(count!=1 || !Readable(holders,sizeof(void*)))return;
    reinterpret_cast<PullFn>(image+kPull)(holders[0]);
}
void __fastcall Frame(unsigned char* v) {
    reinterpret_cast<VehicleFn>(nextFrame)(v);
    if(Cfg().debug)Edf41BikeDiagFrame(v);   // Debug=1: every Car's first minute of physics (edf41bike_diag.cpp)
    if(!on || !Edf41BikeGunnerAllowed())return;   // a member's build lacks the seat: a stock Car for everyone
    __try { if(Ours(v))Update(v); }
    __except(EXCEPTION_EXECUTE_HANDLER) { Log("EDF41BIKE gunner frame rejected unreadable native state v=%p",v); }
}
void Pose408(unsigned char* v,unsigned char* gun,unsigned char* aim) noexcept {
    reinterpret_cast<SyncFn>(image+kPhysicsToBones)(v+kRagdoll);
    auto* local=reinterpret_cast<float*>(gun+kBoneLocal506);
    // The same local matrix twice is the original 408's single-bone branch.
    // gun is an animation-driven proxy (mode 1, out of the world): the final
    // sync only carries its body along with the bone (0x6EDAB6).
    reinterpret_cast<AimPoseFn>(image+kAimPose)(aim,local,local);
    reinterpret_cast<WorldFn>(image+kBuildWorld)(v+kModelInst506,v+0x60);
    reinterpret_cast<SyncFn>(image+kBonesToPhysics)(v+kRagdoll);
}
void __fastcall Pose(unsigned char* v) {
    bool ours=false;unsigned char *gun=nullptr,*aim=nullptr;
    __try {
        if(on && Edf41BikeGunnerAllowed() && Cfg().enabled && !v[kDead] && Ours(v) && !At<void*>(v,kCas)) {
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
              Matches(kGunGate,kGunGateCode,sizeof(kGunGateCode)) && Matches(kGunRow,kGunRowCode,sizeof(kGunRowCode)) &&
              Matches(kAimRegister,kAimRegisterCode,sizeof(kAimRegisterCode)) &&
              Matches(kPhysicsToBones,kPhysicsToBonesCode,sizeof(kPhysicsToBonesCode)) &&
              Matches(kBonesToPhysics,kBonesToPhysicsCode,sizeof(kBonesToPhysicsCode)) && Matches(kBuildWorld,kBuildWorldCode,sizeof(kBuildWorldCode));
        // Slot 2 (the step) is called through the vtable, so another module's chained hook runs as it does for
        // every other seat: the turret camera (turretcam.cpp, AddSe) and the stabilizer (stab.cpp, plain) chain
        // it before this installs (plugin.cpp), so requiring the stock pointer there refused the 408 in every
        // real game (2026-10-19 log: 「EDF41BIKE refused: native Car/Aim/physics profile differs」). Slot 3 (the
        // pose, which this calls by address) must stay stock.
        known=known && At<const void*>(image+kAimVtable,16)!=nullptr && At<const void*>(image+kAimSeVtable,16)!=nullptr &&
               At<const void*>(image+kAimVtable,24)==image+kAimPose && At<const void*>(image+kAimSeVtable,24)==image+kAimPose;
    } __except(EXCEPTION_EXECUTE_HANDLER) { known=false; }
    if(!known){Log("EDF41BIKE refused: native Car/Aim/physics profile differs");return false;}
    auto table=reinterpret_cast<void**>(image+kCarVtable);
    bool changed=false;
    const bool weapon=RedirectCall(image+kWeaponCall,image+kSetWeapon,reinterpret_cast<void*>(&SetWeapon),changed);
    const bool frame=weapon && edf::ChainVtableSlot(table+kFrameSlot,reinterpret_cast<void*>(&Frame),&nextFrame);
    const bool pose=frame && edf::ChainVtableSlot(table+kPoseSlot,reinterpret_cast<void*>(&Pose),&nextPose);
    const bool gate=pose && InstallGunGate();
    on=weapon && frame && pose && gate;
    Log("EDF41BIKE source 408: seat1 native build=%d input=%d single-bone physical pose=%d gun gate=%d",weapon,frame,pose,gate);
    return on;
}
}  // namespace crew
