// A remote gunner's recoil on the vehicle's authority (docs/online-re.md §10; docs/recoil-re.md for the recoil itself).
//
// A vehicle gun's body recoil is added to the hull's rigid body by the machine whose copy of the gun fired that frame
// (fire 0x696FD0 sets weapon+0xBD4 = 1 and +0xBD8 = FireRecoil at 0x6982A5; VehicleBase slot 5 0x630250 then calls the
// class's slot 48 for that holder, which adds FireRecoil x the mount's BodyRecoil to the hull, 0x5F9FB0). The hull's pose
// is sent by the authority (the machine of seat 0's rider, 0x630DF0) and pulled toward by every other machine (CarBase
// slot 51 0x672AD0, heli 0x651F90); no velocity is sent.
//
// When another machine's player fires a gunner seat's gun, that machine fires it and sends the shot count (weapon message
// type 1, 0x695F20). Every other machine gets it through the vehicle (its NetworkObject 0x632380 -> VehicleBase slot 52
// 0x630900: holder index, then the weapon's slot 30 0x6922F0 -> 0x690420). 0x690420 stores the new count
// (weapon+0x1544, 0x690540) but fires its copy only when the gun's operator (the vehicle's NetworkObject slot 11
// 0x62D950: its seat's rider, or the seat's last rider) is not another machine's (0x690565..0x690588): the rounds come
// from the shooter's machine as network bullets. So the authority's copy never fires, its hull never gets the recoil,
// and the shooter's own kick is pulled back out by the authority's pose: the gunner's recoil never moves the vehicle.
//
// Here: VehicleBase slot 52 of the classes whose slot 48 adds body recoil is chained. After the stock handler, for each
// holder whose shot count went up while its operator is another machine's (the shots 0x690420 left unfired), and only on
// the authority, the class's own slot 48 runs once per shot with the gun's FireRecoil as the shot's strength (+0xBD8 for
// the call, put back after): the same hull push, camera shake and class extras a local shot gets, now on the machine
// whose pose the others follow. Holders whose mount recoil is not BodyRecoil (the 403's gunner machine guns: AimRecoil,
// which jitters that seat's aim) are left alone.
//
// Counted once: the authority adds what it never had; the shooter's machine keeps its own local kick (instant feedback)
// and is pulled onto the authority's pose like every non-authority copy, which is absolute (position and orientation),
// so the two do not add up. Machines that are neither add nothing. A local operator or no operator: 0x690420 fires the
// copy itself (the stock recoil), nothing added here.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "gunnerrecoil.h"
#include "memory.h"

namespace crew {
namespace gunner_recoil {
// The holders (veh+0x638, count +0x648, 0x48 each: weapon at +0x10, the mount's recoil record's type at +0x20, 0 =
// BodyRecoil: SetWeaponObject 0x633330 -> 0x5FA4B0); the weapon's received shot count, FireRecoil and the recoil
// strength the class's slot 48 reads (0x62D7F0).
constexpr std::size_t kHolders=0x638,kHolderCount=0x648,kHolderStride=0x48,kHolderWeapon=0x10,kHolderRecoilType=0x20;
constexpr std::size_t kWeaponShots=0x1544,kWeaponFireRecoil=0x37C,kWeaponRecoil=0xBD8;
constexpr std::size_t kVehicleNet=0x120,kNetOperatorSlot=11,kNetFlags=0x8;
constexpr std::int32_t kBodyRecoil=0;
constexpr int kMostShots=16;   // one message's shots (a burst between two sends); more is a broken count

Shot Decide(std::int32_t before,std::int32_t after,bool remoteOperator,bool authority,std::int32_t recoilType) noexcept {
    if(!authority || !remoteOperator || recoilType!=kBodyRecoil || after<=before)return Shot{0};
    const std::int64_t n=static_cast<std::int64_t>(after)-before;
    return Shot{static_cast<int>(n>kMostShots ? kMostShots : n)};
}
}  // namespace gunner_recoil

namespace {
using namespace gunner_recoil;
using MessageFn=void(__fastcall*)(unsigned char*,void*);
using FireFn=void(__fastcall*)(unsigned char*,unsigned char*);
using OperatorFn=const unsigned char*(__fastcall*)(void*,const void*);

constexpr std::size_t kSlotFired=48,kSlotWeaponMessage=52;
constexpr int kMostHolders=16;
// The classes whose slot 48 adds body recoil (0x5F9FB0's callers), their vtables and that slot 48.
struct RecoilClass { unsigned vtable,fired; const char* name; };
const RecoilClass kRecoilClasses[]={
    {0x17D8B50,0x5FD9F0,"402_Rocket"},{0x17D8FA0,0x5FED60,"403_Tank"},{0x17D9458,0x5FFEE0,"404_Tank"},
    {0x17DA508,0x617B80,"503_Bike"},{0x17DADB0,0x61AEA0,"505_Tank"},{0x17DC250,0x620960,"601_Tank"},
    {0x17DC620,0x621700,"603_Flak"},{0x17E01B0,0x65A740,"Car"},
};
constexpr int kRecoilClassCount=static_cast<int>(sizeof(kRecoilClasses)/sizeof(kRecoilClasses[0]));
constexpr unsigned kWeaponMessage=0x630900,kOperator=0x62D950;
// 0x630900 (read the holder index, hand the rest to its weapon's slot 30) and 0x62D950's head; 0x690420's store of the
// new count and its operator test, which this file mirrors.
const unsigned char kWeaponMessageSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0xFA,
                                         0x48,0x8B,0xCA,0xE8};
const unsigned char kOperatorSig[]={0x41,0x57,0x48,0x83,0xEC,0x30,0x4C,0x69,0x99,0xF8,0x04,0x00,0x00,0x40,0x03,0x00,0x00};
const unsigned char kStoreShotsSig[]={0x89,0xAB,0x44,0x15,0x00,0x00};          // 0x690540 mov [rbx+0x1544],ebp
const unsigned char kOperatorTestSig[]={0x0F,0xB6,0x40,0x08,0x24,0x01};         // 0x69056D movzx eax,byte [rax+8]; and al,1

void* next[kRecoilClassCount]{};   // what each class's slot 52 held: the stock 0x630900 or another plugin's hook
bool on=false;
ULONGLONG faultAt=0;

int Fault(const char* where,const EXCEPTION_POINTERS* e) noexcept {
    const ULONGLONG now=GetTickCount64();
    if(!faultAt || now-faultAt>10000) {
        faultAt=now;
        Log("FAULT %s: %08lX (the remote gunner's recoil is skipped for this message)",where,e->ExceptionRecord->ExceptionCode);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

const unsigned char* Holder(const unsigned char* v,std::uint64_t i) noexcept {
    return At<const unsigned char*>(v,kHolders)+i*kHolderStride;
}

bool RemoteOperator(unsigned char* v,const unsigned char* weapon) noexcept {
    void* net=v+kVehicleNet;
    const auto fn=reinterpret_cast<OperatorFn>((*reinterpret_cast<void***>(net))[kNetOperatorSlot]);
    const unsigned char* op=fn(net,weapon);
    return op && (At<unsigned char>(op,kNetFlags)&1)!=0;
}

void Fire(unsigned char* v,int cls,unsigned char* holder,int shots) noexcept {
    unsigned char* weapon=At<unsigned char*>(holder,kHolderWeapon);
    const float kept=At<float>(weapon,kWeaponRecoil);
    const auto fired=reinterpret_cast<FireFn>(image+kRecoilClasses[cls].fired);
    __try {
        for(int s=0;s<shots;++s) {
            Put<float>(weapon,kWeaponRecoil,At<float>(weapon,kWeaponFireRecoil));
            fired(v,holder);
        }
    } __finally {
        // MessageHook contains native faults; do not leave its temporary strength in the live weapon.
        Put<float>(weapon,kWeaponRecoil,kept);
    }
}

void After(unsigned char* v,int cls,const std::int32_t* before,std::uint64_t n) noexcept {
    const bool authority=VehicleAuthority(v);
    for(std::uint64_t i=0;i<n;++i) {
        unsigned char* holder=const_cast<unsigned char*>(Holder(v,i));
        const unsigned char* weapon=At<const unsigned char*>(holder,kHolderWeapon);
        if(!weapon)continue;
        const std::int32_t after=At<std::int32_t>(weapon,kWeaponShots);
        if(after==before[i])continue;
        const Shot shot=Decide(before[i],after,RemoteOperator(v,weapon),authority,At<std::int32_t>(holder,kHolderRecoilType));
        if(shot.count<=0)continue;
        Fire(v,cls,holder,shot.count);
        if(Cfg().debug)Log("GUNNER RECOIL v=%p %s holder %llu: %d shot(s) of the remote gunner on this machine's hull",v,
                           kRecoilClasses[cls].name,static_cast<unsigned long long>(i),shot.count);
    }
}

template<int I> void __fastcall MessageHook(unsigned char* v,void* reader) {
    std::int32_t before[kMostHolders];
    std::uint64_t n=0;
    bool watch=false;
    __try {
        n=At<std::uint64_t>(v,kHolderCount);
        watch=on && n<=kMostHolders && InSession();
        for(std::uint64_t i=0;watch && i<n;++i) {
            const unsigned char* weapon=At<const unsigned char*>(Holder(v,i),kHolderWeapon);
            before[i]=weapon ? At<std::int32_t>(weapon,kWeaponShots) : 0;
        }
    } __except(Fault("gunner recoil (before)",GetExceptionInformation())) { watch=false; }
    reinterpret_cast<MessageFn>(next[I])(v,reader);
    if(!watch)return;
    __try { After(v,I,before,n); }
    __except(Fault("gunner recoil",GetExceptionInformation())) {}
}

template<int... I> struct Table { static constexpr MessageFn hooks[]={&MessageHook<I>...}; };
using Hooks=Table<0,1,2,3,4,5,6,7>;
static_assert(sizeof(Hooks::hooks)/sizeof(Hooks::hooks[0])==kRecoilClassCount,"one hook per class");
}  // namespace

bool InstallGunnerRecoil() noexcept {
    bool sigs=false;
    __try {
        sigs=edf::Matches(image,kWeaponMessage,kWeaponMessageSig,sizeof(kWeaponMessageSig))
          && edf::Matches(image,kOperator,kOperatorSig,sizeof(kOperatorSig))
          && edf::Matches(image,0x690540,kStoreShotsSig,sizeof(kStoreShotsSig))
          && edf::Matches(image,0x69056D,kOperatorTestSig,sizeof(kOperatorTestSig));
    } __except(EXCEPTION_EXECUTE_HANDLER) { sigs=false; }
    if(!sigs){Log("GUNNER RECOIL off: the weapon message path is not the one read (docs/online-re.md §10)");return false;}
    int hooked=0;
    for(int i=0;i<kRecoilClassCount;++i) {
        auto vt=reinterpret_cast<void**>(image+kRecoilClasses[i].vtable);
        if(vt[kSlotFired]!=image+kRecoilClasses[i].fired){Log("GUNNER RECOIL %s: slot 48 not stock, class skipped",kRecoilClasses[i].name);continue;}
        if(!edf::ChainVtableSlot(vt+kSlotWeaponMessage,reinterpret_cast<void*>(Hooks::hooks[i]),&next[i])) {
            Log("GUNNER RECOIL %s: slot 52 patch failed, class skipped",kRecoilClasses[i].name);
            continue;
        }
        if(next[i]!=image+kWeaponMessage)Log("GUNNER RECOIL %s: chained onto %p (another plugin)",kRecoilClasses[i].name,next[i]);
        ++hooked;
    }
    on=hooked>0;
    Log("HOOK gunner recoil=%d/%d",hooked,kRecoilClassCount);
    return on;
}
}  // namespace crew
