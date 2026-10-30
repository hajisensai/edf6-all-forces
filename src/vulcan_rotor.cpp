// M163 six-barrel pose (the quad Vulcan's four rotors) at V603's native final world-build boundary. No private
// draw-palette write: 1100F10 copies ALL model bones in slot 3, including the
// declared visual leaf. The original 40 physical bones and CAS stay native.
#include "crew.h"
#include "vulcan_rotor.h"
#include "barrel_recoil.h"
#include "body506.h"
#include "layout.h"
#include "memory.h"
#include "seat_turret.h"
#include "stores.h"
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
constexpr unsigned kVt603=0x17DC620,kBuildCall=0x621654,kBuildWorld=0x1100B90;
constexpr unsigned kWheelCall=0x674B7F,kDrawWheels=0x66F430;
constexpr std::size_t kState=0xBD4,kReceived=0x1544,kPlayed=0xBD0;
constexpr int kRotors=64;
constexpr ULONGLONG kStaleMs=2000;
const unsigned char kBuildCallCode[]={0xE8,0x37,0xF5,0xAD,0x00};
const unsigned char kBuildSiteCode[]={0x48,0x8D,0x53,0x60,0x48,0x8D,0x8B,0xE0,0x0E,0x00,0x00,0xE8,0x37,0xF5,0xAD,0x00};
const unsigned char kWheelSiteCode[]={0x48,0x8B,0xCF,0xE8,0xAC,0xA8,0xFF,0xFF};
using BuildFn=void(__fastcall*)(void*,const void*);
using WheelsFn=void(__fastcall*)(unsigned char*);
bool installed=false;
struct Rotor {
    ObjRef ref{};const void* bones=nullptr;unsigned char* record[vulcanrotor::kMostBones]{};int count=0;
    float bind[vulcanrotor::kMostBones][16]{};vulcanrotor::Sample sample[vulcanrotor::kMostHolders]{};
    vulcanrotor::Motion motion{};ULONGLONG seen=0,lastMs=0,frame=0;bool stepped=false;
};
Rotor rotors[kRotors]{};
SRWLOCK rotorLock=SRWLOCK_INIT;

Rotor* Of(unsigned char* v,ULONGLONG now) noexcept {
    for(auto& rotor:rotors)if(rotor.ref.Is(v))return &rotor;
    for(auto& rotor:rotors)if(!rotor.ref || rotor.ref.obj==v || now-rotor.seen>kStaleMs){rotor=Rotor{};rotor.ref=ObjRef::Of(v);return &rotor;}
    return nullptr;
}
bool M61(const unsigned char* weapon) noexcept {
    std::size_t n=0;const wchar_t* file=WeaponFile(weapon,&n);
    if(!file)return false;
    for(const wchar_t* prefix:vulcanrotor::kGunPrefixes) {
        const std::size_t k=std::wcslen(prefix);
        if(n>k && _wcsnicmp(file,prefix,k)==0)return true;
    }
    return false;
}
bool Activity(unsigned char* v,Rotor& rotor) noexcept {
    const auto holders=At<const unsigned char*>(v,kHolders);
    const auto count=At<std::uint64_t>(v,kHolderCount);
    if(!holders || count>16 || !Readable(holders,count*kHolderStride))return false;
    bool shot=false,seen[vulcanrotor::kMostHolders]{};
    for(std::uint64_t i=0;i<count && i<vulcanrotor::kMostHolders;++i) {
        const auto* weapon=At<const unsigned char*>(holders+i*kHolderStride,kHolderWeapon);
        if(!Readable(weapon,kReceived+4) || !M61(weapon))continue;
        seen[i]=true;
        const vulcanrotor::Reading reading{weapon,At<std::int32_t>(weapon,kWeaponAmmo),At<std::int32_t>(weapon,kState),
                                           At<std::int32_t>(weapon,kReceived),At<std::int32_t>(weapon,kPlayed)};
        shot=vulcanrotor::Observe(rotor.sample[i],reading) || shot;
    }
    for(int i=0;i<vulcanrotor::kMostHolders;++i)if(!seen[i])rotor.sample[i]=vulcanrotor::Sample{};
    return shot;
}
bool Resolve(unsigned char* v,Rotor& rotor) noexcept {
    const auto* inst=v+kModelInst506;
    const auto bones=At<const void*>(inst,kInstBones506);
    if(!bones)return false;
    if(bones==rotor.bones)return rotor.count>0;
    // Every rotor the model has, kBone first (the others only with it); the first missing one ends the list.
    unsigned char* record[vulcanrotor::kMostBones]{};float bind[vulcanrotor::kMostBones][16];int n=0;
    for(;n<vulcanrotor::kMostBones;++n) {
        record[n]=BoneRecord506(inst,vulcanrotor::kBones[n]);
        if(!record[n] || !Readable(record[n]+kBoneLocal506,64,true))break;
        // Immutable MDB bind, never an instance local left at the last drawn phase.
        // Same native skeleton definitions/count contract as katyusha.cpp.
        if(!BoneBind506(inst,record[n],bind[n]))return false;
    }
    if(!n)return false;
    rotor=Rotor{rotor.ref};rotor.bones=bones;rotor.count=n;
    std::memcpy(rotor.record,record,sizeof(record));std::memcpy(rotor.bind,bind,sizeof(bind));
    return true;
}
void FollowIdlers(unsigned char* v) noexcept {
    if(!installed || !Cfg().enabled || !Readable(v,kHolderCount+8) || v[kDead] || At<const void*>(v,0)!=image+kVt603)return;
    const auto* inst=v+kModelInst506;
    if(!BoneRecord506(inst,vulcanrotor::kBone))return;
    for(int side=0;side<2;++side){
        auto* driver=BoneRecord506(inst,side==0 ? L"tire_moveG_l" : L"tire_moveG_r");
        auto* follower=BoneRecord506(inst,side==0 ? L"tire_moveH_l" : L"tire_moveH_r");
        if(!driver || !follower || !Readable(driver+kBoneWorld506,64) || !Readable(follower+kBoneWorld506,64,true))continue;
        float g[16],h[16],out[16];
        if(!BoneBind506(inst,driver,g) || !BoneBind506(inst,follower,h) ||
           !vulcanrotor::Follow(g,h,reinterpret_cast<const float*>(driver+kBoneWorld506),out))continue;
        // 66F430 writes final wheel WORLDS directly and leaves record+8 as is.
        // G/H have one parent, so the immutable local-bind difference also
        // maps their world binds. Preserve the native ownership flag.
        std::memcpy(follower+kBoneWorld506,out,sizeof(out));
    }
}
void PoseLocked(unsigned char* v,ULONGLONG now,ULONGLONG frame) noexcept {
    if(!Cfg().enabled || !Readable(v,kHolderCount+8) || v[kDead] || At<const void*>(v,0)!=image+kVt603)return;
    if(!BoneRecord506(v+kModelInst506,vulcanrotor::kBone))return; // source V603/BMPT left exactly native
    Rotor* const rotor=Of(v,now);
    if(!rotor || !Resolve(v,*rotor))return;
    rotor->seen=now;
    if(!rotor->stepped || rotor->frame!=frame) {
        const float seconds=rotor->stepped && now>=rotor->lastMs ? static_cast<float>(now-rotor->lastMs)*0.001f : 1.0f/60.0f;
        const bool shot=Activity(v,*rotor);
        vulcanrotor::Step(rotor->motion,shot,now,seconds);
        rotor->lastMs=now;rotor->frame=frame;rotor->stepped=true;
    }
    for(int k=0;k<rotor->count;++k) {
        float local[16];vulcanrotor::Local(rotor->bind[k],rotor->motion.phase,local);
        std::memcpy(rotor->record[k]+kBoneLocal506,local,sizeof(local));
        // 1100010 tests this dirty/local-ownership flag before local * parent.
        Put<std::int32_t>(rotor->record[k],8,1);
    }
}
void Pose(unsigned char* v) noexcept {
    if(!Cfg().enabled)return;
    const ULONGLONG now=GameMs(),frame=GameFrame();
    AcquireSRWLockExclusive(&rotorLock);
    __try { PoseLocked(v,now,frame); }
    __finally { ReleaseSRWLockExclusive(&rotorLock); }
}
void __fastcall Build(void* inst,const void* matrix) {
    if(inst && installed) {
        __try { Pose(static_cast<unsigned char*>(inst)-kModelInst506); }
        __except(EXCEPTION_EXECUTE_HANDLER) { Log("VULCAN rotor rejected unreadable native instance"); }
        // The same last local step for the BMPT's barrels: each one's own recoil on its own shot (barrel_recoil.h).
        BarrelRecoilPose(static_cast<unsigned char*>(inst)-kModelInst506);
        SeatTurretsPose(static_cast<unsigned char*>(inst)-kModelInst506);   // its gunner seats' own turrets (seat_turret.h)
    }
    reinterpret_cast<BuildFn>(image+kBuildWorld)(inst,matrix); // exactly once, for every native 603
}
void __fastcall DrawWheels(unsigned char* v) {
    reinterpret_cast<WheelsFn>(image+kDrawWheels)(v); // native final writer first, exactly once
    __try { FollowIdlers(v); }
    __except(EXCEPTION_EXECUTE_HANDLER) { Log("VULCAN half-idler rejected unreadable native instance"); }
}
}  // namespace
bool InstallVulcanRotor() noexcept {
    if(installed)return true;
    bool known=false;
    __try { known=Matches(kBuildCall,kBuildCallCode,sizeof(kBuildCallCode)) && Matches(kBuildCall-11,kBuildSiteCode,sizeof(kBuildSiteCode)) &&
                 Matches(kWheelCall-3,kWheelSiteCode,sizeof(kWheelSiteCode)); }
    __except(EXCEPTION_EXECUTE_HANDLER){known=false;}
    if(!known){Log("VULCAN rotor refused: native 603 final world-build call differs");return false;}
    bool changed=false;
    const bool wheels=RedirectCall(image+kWheelCall,image+kDrawWheels,reinterpret_cast<void*>(&DrawWheels),changed);
    installed=wheels && RedirectCall(image+kBuildCall,image+kBuildWorld,reinterpret_cast<void*>(&Build),changed);
    Log("VULCAN visual rotor pre-worldbuild and post-wheel hooks=%d",installed);
    return installed;
}
void ResetVulcanRotors() noexcept {
    AcquireSRWLockExclusive(&rotorLock);
    for(auto& rotor:rotors)rotor=Rotor{};
    ReleaseSRWLockExclusive(&rotorLock);
}
}  // namespace crew
