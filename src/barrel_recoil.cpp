// Each twin barrel's own recoil on its own shot (barrel_recoil.h): the counters of the barrels the plugin fires in turn
// (stores.h TwinPairOf), the bone each one's muzzle hangs on, the kick written into that bone's local at the 603's last
// pose step (vulcan_rotor.cpp Build, before 0x1100B90 builds the worlds from the locals). Game thread.
#include "crew.h"
#include "barrel_recoil.h"
#include "body506.h"
#include "layout.h"
#include "memory.h"
#include "stores.h"
#include "edf/weapon.h"

namespace crew {
namespace {
constexpr std::size_t kWeaponReceived=0x1544,kWeaponPlayed=0xBD0;   // the shots heard from the operator's machine / played
constexpr std::size_t kBoneLocal=kBoneLocal506;                   // a bone record's local rows (its world at +0xB0)
constexpr int kVehicles=16,kBarrels=8,kBones=4;
constexpr ULONGLONG kStaleMs=2000;
struct Barrel { recoil::Counters prior{};std::uint64_t shotFrame=0;bool shot=false; };
struct Vehicle { ObjRef ref{};ULONGLONG seen=0;std::uint64_t frame=0;Barrel barrel[kBarrels]{};recoil::Bone bone[kBones]{}; };
Vehicle vehicles[kVehicles]{};

Vehicle* Of(const unsigned char* v,ULONGLONG now) noexcept {
    for(auto& x:vehicles)if(x.ref.Is(v))return &x;
    for(auto& x:vehicles)if(!x.ref || x.ref.obj==v || !x.ref.Is(x.ref.obj) || now-x.seen>kStaleMs){x=Vehicle{};x.ref=ObjRef::Of(v);return &x;}
    return nullptr;
}
}  // namespace

void BarrelRecoilPose(unsigned char* v) noexcept {
    if(!Cfg().enabled)return;
    __try {
        if(!v || v[kDead] || !Readable(v,kHolderCount+8))return;
        const auto holders=At<const unsigned char*>(v,kHolders);
        const auto count=At<std::uint64_t>(v,kHolderCount);
        if(!holders || !count || count>16 || !Readable(holders,count*kHolderStride))return;
        // Only a vehicle with twin barrels (the registry's: GroundVehicle.alternate) has anything to do.
        bool any=false;
        for(std::uint64_t i=0;i<count && !any;++i)any=TwinPairOf(At<const unsigned char*>(holders+i*kHolderStride,kHolderWeapon))!=0;
        if(!any)return;
        const ULONGLONG now=GameMs();
        const std::uint64_t frame=GameFrame();
        Vehicle* const x=Of(v,now);
        if(!x)return;
        x->seen=now;
        const bool step=x->frame!=frame;   // a second pose in one frame neither re-reads the counters nor re-kicks
        x->frame=frame;
        float back[kBones]{};unsigned char* records[kBones]{};int nb=0;
        int k=0;
        for(std::uint64_t i=0;i<count && k<kBarrels;++i) {
            const unsigned char* const weapon=At<const unsigned char*>(holders+i*kHolderStride,kHolderWeapon);
            if(!Readable(weapon,kWeaponReceived+4) || !TwinPairOf(weapon))continue;
            Barrel& b=x->barrel[k++];
            if(step) {
                const recoil::Counters c{weapon,At<std::int32_t>(weapon,kWeaponAmmo),At<std::int32_t>(weapon,kWeaponReceived),
                                         At<std::int32_t>(weapon,kWeaponPlayed)};
                if(recoil::Shot(b.prior,c)){b.shot=true;b.shotFrame=frame;}
                if(b.prior.weapon!=c.weapon)b.shot=false;   // another weapon in this slot: its own history from now
                b.prior=c;
            }
            const float off=b.shot && frame>=b.shotFrame ? recoil::Offset(frame-b.shotFrame) : 0.0f;
            // The bone this barrel's muzzle hangs on: both belts of one side share it, the larger kick wins.
            const unsigned char* const muzzle=edf::MuzzleAt(weapon,0);
            unsigned char* const record=muzzle ? At<unsigned char*>(muzzle,0) : nullptr;
            if(!record || !Readable(record,kBoneLocal+64,true))continue;
            int at=-1;
            for(int j=0;j<nb;++j)if(records[j]==record)at=j;
            if(at<0 && nb<kBones){at=nb++;records[at]=record;back[at]=0.0f;}
            if(at>=0 && off>back[at])back[at]=off;
        }
        for(int j=0;j<nb;++j) {
            recoil::Bone* bone=nullptr;
            for(auto& y:x->bone)if(y.record==records[j]){bone=&y;break;}
            if(!bone)for(auto& y:x->bone)if(!y.record){bone=&y;bone->record=records[j];bone->known=false;break;}
            if(!bone)continue;
            float current[16],base[16],out[16];
            std::memcpy(current,records[j]+kBoneLocal,64);
            std::memcpy(base,recoil::Base(*bone,current),64);
            if(!(back[j]>0.0f)) {
                // At rest: the base back if this file's kick is still the local (no animation rewrote it), else leave it.
                if(bone->known && std::memcmp(current,bone->written,64)==0)std::memcpy(records[j]+kBoneLocal,base,64);
                bone->known=false;continue;
            }
            if(!recoil::Back(base,back[j],out)){bone->known=false;continue;}
            std::memcpy(records[j]+kBoneLocal,out,64);
            Put<std::int32_t>(records[j],8,1);   // the local owns this bone this frame (0x1100010 reads it before local x parent)
            std::memcpy(bone->written,out,64);std::memcpy(bone->base,base,64);bone->known=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void ResetBarrelRecoil() noexcept {
    for(auto& x:vehicles)x=Vehicle{};
}
}  // namespace crew
