// Real PVP membership uses the native team registry; no HUD-only enemy flags.
#include "pvp_native.h"
#include "coop_extension_api.h"
#include "crew.h"
#include "edf41classes.h"
#include "memory.h"
#include "mission_participants.h"
#include <atomic>
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kEnumAllTeams=0x5E0C80,kTeamManager=0x20B2978,kGameStatus=0x20B2890;
constexpr std::size_t kUser=0x1ED0,kUserCtrl=0x1ED8,kHumanVehicle=0x1548;
constexpr std::size_t kWeapons=0x1950,kWeaponCount=0x1960,kWeaponOwner=0x120,kWeaponTeam=0x214;
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr unsigned kMaxActors=32,kMaxObjects=2304,kMaxSupportOwned=2048;
constexpr std::uint32_t kRegistered=0x40,kNotCounted=0x2000;
constexpr unsigned char kEnumSignature[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83};
constexpr unsigned char kSetTeamSignature[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
constexpr unsigned char kLockSignature[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C};
struct Player { ObjRef ref{}; int index=-1; char puid[65]{}; int team=0; };
struct Desired { ObjRef ref{}; int team=0; bool player=false,vehicle=false; };
struct Collection { Player players[kMaxActors]{}; unsigned count=0,expected=0; Desired objects[kMaxObjects]{}; unsigned objectsCount=0; bool failed=false; };
struct Saved {
    ObjRef ref{};
    int originalTeam=0,assignedTeam=0;
    bool originalRegistered=false,originalNotCounted=false,changed=false,vehicle=false;
    void* lockPoint=nullptr;
    unsigned wanted=0;
};
Saved saved[kMaxObjects]{};
struct SupportOwner { ObjRef ref{}; char puid[65]{}; };
SupportOwner supportOwners[kMaxSupportOwned]{};
bool nativeFaulted=false,nativeHooksReady=false;
std::atomic<bool> radarActive{false};
pvp_protocol::State activeState{};
unsigned generation=0;
using ResolveFn=EDF6CoopResolveMissionPlayerPuidFn;

bool Live(const ObjRef& ref) noexcept {
    const auto* o=static_cast<const unsigned char*>(ref.obj);
    const auto* ctrl=static_cast<const unsigned char*>(ref.ctrl);
    // Saved references retain the native weak count. Inspect its still-owned
    // control block before touching an object that its scene may have destroyed.
    return ctrl && Readable(ctrl,16) && At<LONG>(ctrl,8)>0 && o &&
        Readable(o,kObjectFlags+4) && At<const void*>(o,kSelfCtrl)==ctrl && !(o[0x18]&4);
}
bool PuidValid(const char* text) noexcept {
    if(!text || !text[0] || !std::memchr(text,0,65))return false;
    for(const char* p=text;*p;++p)if(!((*p>='0' && *p<='9') || (*p>='a' && *p<='z') || (*p>='A' && *p<='Z')))return false;
    return true;
}
bool InstallRadarFriend() noexcept;
bool InstallVehicleOwnership() noexcept;
bool CodeReady() noexcept {
    return image && Matches(kEnumAllTeams,kEnumSignature,sizeof(kEnumSignature)) &&
        Matches(kSetTeam,kSetTeamSignature,sizeof(kSetTeamSignature)) && Matches(0x547A50,kLockSignature,sizeof(kLockSignature));
}
void __fastcall Ignore(void*) noexcept {}
struct Visitor { const void* const* vtable; Collection* collection; };
void __fastcall Visit(Visitor* visitor,const unsigned char* object) noexcept {
    Collection& c=*visitor->collection;
    if(c.failed || !object)return;
    __try {
        if(!IsSoldierClass(object))return;
        if(!Readable(object,kUserCtrl+8)){c.failed=true;return;}
        if(object[0x18]&4)return;
        const auto* user=At<const unsigned char*>(object,kUser);
        if(!user)return;
        const auto* control=At<const unsigned char*>(object,kUserCtrl);
        if(!Readable(user,0x4C) || !Readable(control,16) || At<LONG>(control,8)<=0){c.failed=true;return;}
        const int index=At<int>(user,0x48);
        if(index<0)return;
        for(unsigned i=0;i<c.count;++i) {
            if(c.players[i].ref.obj==object)return;
            if(c.players[i].index==index){c.failed=true;return;}
        }
        if(c.count>=kMaxActors || static_cast<unsigned>(index)>=c.expected){c.failed=true;return;}
        c.players[c.count].ref=ObjRef::Of(object);
        c.players[c.count++].index=index;
    } __except(EXCEPTION_EXECUTE_HANDLER){c.failed=true;}
}
const void* const kVisitorVtable[]={reinterpret_cast<const void*>(&Ignore),reinterpret_cast<const void*>(&Visit)};

bool AddObject(Collection& c,const void* raw,int team,bool playerObject=false,bool vehicleObject=false) noexcept {
    const ObjRef ref=ObjRef::Of(raw);
    if(!Live(ref))return false;
    for(unsigned i=0;i<c.objectsCount;++i)if(c.objects[i].ref.obj==raw) {
        if(c.objects[i].team!=team)return false; // conflicting players already in one vehicle: never choose by visitation order
        c.objects[i].player=c.objects[i].player || playerObject;
        c.objects[i].vehicle=c.objects[i].vehicle || vehicleObject;
        return true;
    }
    if(c.objectsCount==kMaxObjects)return false;
    c.objects[c.objectsCount++]={ref,team,playerObject,vehicleObject};return true;
}
int TeamFor(const pvp_protocol::State& state,const char* puid) noexcept {
    for(unsigned i=0;i<state.count;++i)if(!std::strcmp(state.entries[i].puid,puid))return state.entries[i].team;
    return 0;
}
bool AddVehicleCrew(Collection& c,unsigned char* vehicle,int team) noexcept {
    if(!vehicle || !Readable(vehicle,kSeatCount+8) || !AddObject(c,vehicle,team,false,true))return false;
    const unsigned seats=SeatCount(vehicle);
    if(!seats)return false;
    for(unsigned seatIndex=0;seatIndex<seats;++seatIndex) {
        const auto* seat=SeatAt(vehicle,seatIndex);
        if(!Readable(seat,kSeatRiderCtrl+8))return false;
        const auto* control=At<const unsigned char*>(seat,kSeatRiderCtrl);
        if(!control)continue;
        if(!Readable(control,16))return false;
        if(At<LONG>(control,8)<=0)continue;
        const auto* rider=At<const unsigned char*>(seat,kSeatRider);
        if(!rider || At<const void*>(rider,kSelfCtrl)!=control || !AddObject(c,rider,team))return false;
    }
    return true;
}
bool AddRide(Collection& c,const unsigned char* human,int team) noexcept {
    const auto* control=At<const unsigned char*>(human,kHumanVehicleCtrl);
    if(!control)return true;
    if(!Readable(control,16))return false;
    if(At<LONG>(control,8)<=0)return true;
    auto* vehicle=At<unsigned char*>(human,kHumanVehicle);
    return vehicle && At<const void*>(vehicle,kSelfCtrl)==control && AddVehicleCrew(c,vehicle,team);
}
bool AppendOwnedSupport(Collection& c,const pvp_protocol::State& state) noexcept {
    for(const auto& owner:supportOwners)if(owner.ref && Live(owner.ref)) {
        const int team=TeamFor(state,owner.puid);
        if(team!=1 && team!=2)continue; // disconnected owner: old Saved state is restored outside the desired set
        const auto* object=static_cast<const unsigned char*>(owner.ref.obj);
        if(IsSoldierClass(object)) {
            if(!AddObject(c,object,team,true) || !AddRide(c,object,team))return false;
        } else if(!AddVehicleCrew(c,const_cast<unsigned char*>(object),team))return false;
    }
    return true;
}
void ReleaseSupportOwner(SupportOwner& owner) noexcept {
    if(owner.ref.ctrl) {
        auto* control=const_cast<unsigned char*>(static_cast<const unsigned char*>(owner.ref.ctrl));
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+0xC))==0)
            reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(control,0)[1])(control);
    }
    owner={};
}
bool Collect(const pvp_protocol::State& state,Collection& c) noexcept {
    if(!nativeHooksReady || !CodeReady() || !state.count || state.count>pvp_protocol::kMaxMembers)return false;
    unsigned location=0;
    if(!ReadNativeMissionLocation(&location) || location!=4)return false;
    const HMODULE coop=GetModuleHandleW(L"EDF6Coop.dll");
    const auto resolve=coop ? reinterpret_cast<ResolveFn>(GetProcAddress(coop,"EDF6Coop_ResolveMissionPlayerPuid")) : nullptr;
    if(!resolve)return false; // no mission-slot ordinal fallback: the PUID resolver is required
    void* manager=At<void*>(image,kTeamManager);
    const auto* status=At<const unsigned char*>(image,kGameStatus);
    if(!Readable(manager,0x50) || !Readable(status,0x14FFC))return false;
    c.expected=At<unsigned>(status,0x14FF8);
    if(!c.expected || c.expected>kMaxActors || !Readable(At<void*>(manager,0x38),7*0x38))return false;
    Visitor visitor{kVisitorVtable,&c};
    reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kEnumAllTeams)(manager,&visitor);
    if(c.failed || c.count!=c.expected || At<void*>(image,kTeamManager)!=manager ||
       At<const void*>(image,kGameStatus)!=status || At<unsigned>(status,0x14FF8)!=c.expected)return false;
    bool found[pvp_protocol::kMaxMembers]{};
    // All registry traversal has finished before any SetTeam is allowed.
    for(unsigned i=0;i<c.count;++i) {
        EDF6CoopPeer who{};
        if(!Live(c.players[i].ref) || !resolve(c.players[i].index,&who) || !PuidValid(who.id))return false;
        std::memcpy(c.players[i].puid,who.id,sizeof(who.id));
        const int team=TeamFor(state,who.id);
        if(team!=1 && team!=2)return false;
        c.players[i].team=team;
        for(unsigned j=0;j<state.count;++j)if(!std::strcmp(state.entries[j].puid,who.id))found[j]=true;
        if(!AddObject(c,c.players[i].ref.obj,team,true))return false;
    }
    for(unsigned i=0;i<state.count;++i)if(!found[i])return false;
    for(unsigned i=0;i<c.count;++i)
        if(!AddRide(c,static_cast<const unsigned char*>(c.players[i].ref.obj),c.players[i].team))return false;
    return AppendOwnedSupport(c,state);
}
void ReleaseWeak(Saved& item) noexcept {
    if(item.ref.ctrl) {
        auto* control=const_cast<unsigned char*>(static_cast<const unsigned char*>(item.ref.ctrl));
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+0xC))==0)
            reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(control,0)[1])(control);
    }
    item={};
}
Saved* Find(const ObjRef& ref) noexcept {
    for(auto& item:saved)if(item.ref.obj==ref.obj && item.ref.ctrl==ref.ctrl)return &item;
    return nullptr;
}
bool RetainStrong(unsigned char* control) noexcept {
    if(!Readable(control,16))return false;
    auto* uses=reinterpret_cast<volatile LONG*>(control+8);
    LONG observed=InterlockedCompareExchange(uses,0,0);
    while(observed>0 && observed<0x7FFFFFFF) {
        const LONG was=InterlockedCompareExchange(uses,observed+1,observed);
        if(was==observed)return true;
        observed=was;
    }
    return false;
}
void ReleaseStrong(unsigned char* control) noexcept {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+8))==0) {
        reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(control,0)[0])(control);
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+0xC))==0)
            reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(control,0)[1])(control);
    }
}
int RadarFriendTeam(const void* hud,int requested) noexcept {
    // HUD updates may run apart from the game thread. Read only this published
    // mode bit and the HUD's own native weak reference, never Saved/activeState.
    if(requested!=2 || !radarActive.load(std::memory_order_acquire) || !Readable(hud,0x788))return requested;
    auto* control=At<unsigned char*>(hud,0x780);
    if(!RetainStrong(control))return requested;
    int selected=requested;
    __try {
        const auto* object=At<const unsigned char*>(hud,0x778);
        const ObjRef identity{object,control};
        if(Live(identity) && IsSoldierClass(object) && IsAnyPlayer(object)) {
            const int team=At<int>(object,kTeam);
            if(team==1 || team==2)selected=team;
        }
    } __finally {ReleaseStrong(control);}
    return selected;
}
void __fastcall RadarFriends(void* hud,void* list,int team) noexcept {
    int selected=team;
    __try {selected=RadarFriendTeam(hud,team);} __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<void(__fastcall*)(void*,void*,int)>(image+0x82B220)(hud,list,selected);
}
bool InstallRadarFriend() noexcept {
    static bool installed=false;
    if(installed)return true;
    constexpr unsigned char entry[]={0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0xC7,0x42,0x18,0,0};
    constexpr unsigned char arguments[]={0x41,0xB8,0x02,0,0,0,0x48,0x8D,0x54,0x24,0x30,0x49,0x8B,0xCE};
    if(!Matches(0x82B220,entry,sizeof(entry)) || !Matches(0x82BB42,arguments,sizeof(arguments)))return false;
    bool changed=false;
    installed=edf::RedirectCall(image+0x82BB50,image+0x82B220,reinterpret_cast<void*>(&RadarFriends),changed);
    if(installed)Log("PVP native radar friendly row follows the HUD's actual soldier team");
    return installed;
}
bool Weapons(const Desired& desired,int team) noexcept;
int VehicleAssignedTeam(const void* vehicle,int fallback) noexcept {
    if(!radarActive.load(std::memory_order_acquire))return fallback;
    const Saved* item=Find(ObjRef::Of(vehicle));
    return item && item->changed && item->vehicle && (item->assignedTeam==1 || item->assignedTeam==2) ? item->assignedTeam : fallback;
}
void __fastcall RecomputeVehicleTeam(void* vehicle,int nativeTeam,bool registered) noexcept {
    int selected=nativeTeam;
    __try {selected=VehicleAssignedTeam(vehicle,nativeTeam);} __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<void(__fastcall*)(void*,int,bool)>(image+kSetTeam)(vehicle,selected,registered);
    if(selected!=nativeTeam || radarActive.load(std::memory_order_acquire)) {
        __try {
            const ObjRef identity=ObjRef::Of(vehicle);
            const Saved* item=Find(identity);
            if(item && item->changed && item->vehicle &&
               (At<int>(vehicle,kTeam)!=selected || !Weapons({identity,selected,false,true},selected)))nativeFaulted=true;
        } __except(EXCEPTION_EXECUTE_HANDLER){nativeFaulted=true;}
    }
}
bool InstallVehicleOwnership() noexcept {
    static bool installed=false;
    if(installed)return true;
    constexpr unsigned char args[]={0x41,0xB0,0x01,0x8B,0xD5,0x48,0x8B,0xCF};
    if(!Matches(0x630589,args,sizeof(args)))return false;
    bool changed=false;
    installed=edf::RedirectCall(image+0x630591,image+kSetTeam,reinterpret_cast<void*>(&RecomputeVehicleTeam),changed);
    if(installed)Log("PVP native vehicle team recompute keeps its authenticated owner team");
    return installed;
}
Saved* Reserve(const Desired& desired) noexcept {
    if(Saved* item=Find(desired.ref))return item;
    for(auto& item:saved) {
        if(item.ref && !Live(item.ref))ReleaseWeak(item);
        if(item.ref)continue;
        auto* control=const_cast<unsigned char*>(static_cast<const unsigned char*>(desired.ref.ctrl));
        if(!Live(desired.ref))return nullptr;
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(control+0xC));
        item.ref=desired.ref;item.vehicle=desired.vehicle;return &item;
    }
    return nullptr;
}
bool OwnLock(const Saved& item) noexcept {
    if(!item.lockPoint || !Live(item.ref))return false;
    const auto* object=static_cast<const unsigned char*>(item.ref.obj);
    if(!Readable(object+0x360,0x20))return false;
    const auto* data=At<const unsigned char*>(object,0x368);
    const auto count=At<std::uint64_t>(object,0x378);
    if(count>1024 || !Readable(data,static_cast<std::size_t>(count)*16))return false;
    for(std::uint64_t i=0;i<count;++i)if(At<const void*>(data,static_cast<std::size_t>(i)*16)==item.lockPoint)
        return Readable(item.lockPoint,0x50) && At<const void*>(item.lockPoint,8)==item.ref.obj;
    return false;
}
void DisableLock(Saved& item) noexcept {
    if(OwnLock(item))Put<unsigned char>(item.lockPoint,0x2A,0);
}
bool SetNativeTeam(const ObjRef& ref,int team,bool registered) noexcept {
    if(!Live(ref))return false;
    auto* object=const_cast<void*>(ref.obj);
    const bool nowRegistered=(At<std::uint32_t>(object,kObjectFlags)&kRegistered)!=0;
    if(At<int>(object,kTeam)!=team || nowRegistered!=registered)
        reinterpret_cast<void(__fastcall*)(void*,int,bool)>(image+kSetTeam)(object,team,registered);
    return Live(ref) && At<int>(object,kTeam)==team && ((At<std::uint32_t>(object,kObjectFlags)&kRegistered)!=0)==registered;
}
bool SetWeaponTeam(const unsigned char* weapon,const ObjRef& owner,int team) noexcept {
    if(!weapon || !Readable(weapon,kWeaponTeam+4) || At<const void*>(weapon,kWeaponOwner)!=owner.obj)return false;
    Put<int>(const_cast<unsigned char*>(weapon),kWeaponTeam,team);return true;
}
bool Weapons(const Desired& desired,int team) noexcept {
    const auto* object=static_cast<const unsigned char*>(desired.ref.obj);
    if(IsSoldierClass(object)) {
        if(!Readable(object,kWeaponCount+8))return false;
        const auto count=At<std::uint64_t>(object,kWeaponCount);
        const auto* list=At<const unsigned char* const*>(object,kWeapons);
        if(count>32 || (count && !Readable(list,static_cast<std::size_t>(count)*8)))return false;
        for(std::uint64_t i=0;i<count;++i)if(list[i] && !SetWeaponTeam(list[i],desired.ref,team))return false;
    }
    if(desired.vehicle) {
        const unsigned seats=SeatCount(object);
        for(unsigned i=0;i<seats;++i) {
            const auto* seat=SeatAt(const_cast<unsigned char*>(object),i);
            if(!Readable(seat,kSeatWeaponCount+8))return false;
            const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
            const auto* holders=At<const unsigned char* const*>(seat,kSeatWeapons);
            if(count>16 || (count && !Readable(holders,static_cast<std::size_t>(count)*8)))return false;
            for(std::uint64_t j=0;j<count;++j) {
                if(!Readable(holders[j],kHolderWeapon+8))return false;
                const auto* weapon=At<const unsigned char*>(holders[j],kHolderWeapon);
                if(weapon && !SetWeaponTeam(weapon,desired.ref,team))return false;
            }
        }
    }
    return true;
}
bool Restore(Saved& item) noexcept {
    if(!Live(item.ref)){ReleaseWeak(item);return true;}
    if(item.changed) {
        if(!SetNativeTeam(item.ref,item.originalTeam,item.originalRegistered))return false;
        {
            auto* object=const_cast<void*>(item.ref.obj);
            const auto flags=At<std::uint32_t>(object,kObjectFlags);
            Put<std::uint32_t>(object,kObjectFlags,item.originalNotCounted ? flags|kNotCounted : flags&~kNotCounted);
            // Weapons are not GameObjects. Their emitter-team field is rebuilt from the
            // current owning object, as the native holder loop does; never SetTeam(weapon).
            Desired desired{item.ref,item.originalTeam,IsSoldierClass(object),item.vehicle};
            if(!Weapons(desired,item.originalTeam))return false;
        }
        item.changed=false;
    }
    DisableLock(item);
    // Retain a player's inert lock point with its weak identity until scene reset,
    // so toggling the mode reuses one native point rather than growing its list.
    // Vehicles/old crew have no added point and release their cache entry now.
    if(!OwnLock(item))ReleaseWeak(item);
    return true;
}
bool RestoreAll() noexcept {
    bool ok=true;
    for(auto& item:saved)if(item.ref && !Restore(item))ok=false;
    return ok;
}
bool ApplyCollected(Collection& c) noexcept {
    for(auto& owner:supportOwners)if(owner.ref && !Live(owner.ref))ReleaseSupportOwner(owner);
    if(++generation==0){generation=1;for(auto& item:saved)item.wanted=0;}
    // Reserve the entire write set first; a capacity failure must change no object.
    for(unsigned i=0;i<c.objectsCount;++i)if(!Reserve(c.objects[i]))return false;
    for(unsigned i=0;i<c.objectsCount;++i) {
        const Desired& desired=c.objects[i];Saved& item=*Find(desired.ref);item.wanted=generation;item.assignedTeam=desired.team;
        if(!item.changed) {
            item.originalTeam=At<int>(item.ref.obj,kTeam);
            const auto flags=At<std::uint32_t>(item.ref.obj,kObjectFlags);
            item.originalRegistered=(flags&kRegistered)!=0;item.originalNotCounted=(flags&kNotCounted)!=0;
            item.changed=true;
        }
        const bool registered=desired.player || desired.vehicle || item.originalRegistered;
        if(!SetNativeTeam(item.ref,desired.team,registered))return false;
        auto* object=const_cast<void*>(item.ref.obj);
        Put<std::uint32_t>(object,kObjectFlags,At<std::uint32_t>(object,kObjectFlags)|kNotCounted);
        if(!Weapons(desired,desired.team))return false;
        if(desired.player) {
            if(!OwnLock(item))item.lockPoint=edf41c::AddLockPoint(object);
            if(!OwnLock(item))return false;
            const auto* human=static_cast<const unsigned char*>(object);
            float position[3]={At<float>(human,kPosition),At<float>(human,kPosition+4)+1.2f,At<float>(human,kPosition+8)};
            if(!std::isfinite(position[0]+position[1]+position[2]))return false;
            edf41c::SetLockPoint(item.lockPoint,position);
            Put<unsigned char>(item.lockPoint,0x29,registered ? 1 : 0);
            Put<unsigned char>(item.lockPoint,0x2A,human[kDead] ? 0 : 1);
        }
    }
    // A player left a vehicle: restore that vehicle and its old crew immediately.
    for(auto& item:saved)if(item.ref && item.wanted!=generation && !Restore(item))return false;
    return true;
}
bool ApplyChecked(const pvp_protocol::State& state) noexcept {
    if(!state.enabled){radarActive.store(false,std::memory_order_release);if(!RestoreAll())return false;activeState=state;return true;}
    Collection collection{};
    if(!Collect(state,collection))return false;
    if(!ApplyCollected(collection))return false;
    activeState=state;radarActive.store(true,std::memory_order_release);return true;
}
}
bool InstallPvpNative() noexcept {
    __try {
        nativeHooksReady=CodeReady() && InstallRadarFriend() && InstallVehicleOwnership();
        if(!nativeHooksReady)Log("PVP native profile/hooks unavailable; mode stays disabled");
        return nativeHooksReady;
    } __except(EXCEPTION_EXECUTE_HANDLER){nativeHooksReady=false;return false;}
}
bool PvpNativePrepare(const pvp_protocol::State& state) noexcept {
    __try {
        if(!state.enabled)return true;
        Collection collection{};
        return Collect(state,collection);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool PvpNativeApply(const pvp_protocol::State& state) noexcept {
    __try {
        const auto previous=activeState;
        if(ApplyChecked(state))return true;
        // A requested baseline restore must never reactivate the old PVP state.
        // Keep Saved originals/weak identities for Protocol::Restoring's next frame.
        if(!state.enabled){activeState={};radarActive.store(false,std::memory_order_release);return false;}
        RestoreAll();activeState={};radarActive.store(false,std::memory_order_release);
        if(previous.enabled && !ApplyChecked(previous)){RestoreAll();activeState={};radarActive.store(false,std::memory_order_release);}
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){ResetPvpNative();return false;}
}
bool PvpNativeTick() noexcept {
    __try {
        if(nativeFaulted){nativeFaulted=false;return false;}
        if(!activeState.enabled)return true;
        Collection collection{};
        if(Collect(activeState,collection) && ApplyCollected(collection))return true;
        RestoreAll();activeState={};radarActive.store(false,std::memory_order_release);return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){ResetPvpNative();return false;}
}
void ResetPvpNative() noexcept {
    radarActive.store(false,std::memory_order_release);
    bool restored=false;
    __try {restored=RestoreAll();}
    __except(EXCEPTION_EXECUTE_HANDLER){Log("PVP scene reset encountered an invalidated native reference");}
    if(!restored) {
        Log("PVP original native teams not yet restored; retaining weak identities and original state");
        activeState={};radarActive.store(false,std::memory_order_release);return;
    }
    for(auto& item:saved)if(item.ref) {
        __try {ReleaseWeak(item);} __except(EXCEPTION_EXECUTE_HANDLER){item={};}
    }
    activeState={};radarActive.store(false,std::memory_order_release);generation=0;nativeFaulted=false;
}
bool PvpNativeAttachSupport(const char* puid,const ObjRef* objects,unsigned count,int team) noexcept {
    __try {
        if(!PuidValid(puid) || !objects || !count || count>16 || (team!=0 && team!=1 && team!=2))return false;
        if(!team && activeState.enabled)return false;
        if(team && (!activeState.enabled || TeamFor(activeState,puid)!=team))return false;
        unsigned slots[16]{};unsigned additions=0;
        // Validate identities/classes and reserve the whole batch before adding one owner.
        for(unsigned i=0;i<count;++i) {
            if(!Live(objects[i]))return false;
            const auto* object=static_cast<const unsigned char*>(objects[i].obj);
            if(!IsSoldierClass(object) && !SeatCount(object))return false;
            for(unsigned j=0;j<i;++j)if(objects[i].obj==objects[j].obj)return false;
            bool existing=false;
            for(const auto& owner:supportOwners)if(owner.ref.obj==objects[i].obj && owner.ref.ctrl==objects[i].ctrl) {
                if(std::strcmp(owner.puid,puid))return false;
                existing=true;break;
            }
            if(existing)continue;
            unsigned selected=kMaxSupportOwned;
            for(unsigned j=0;j<kMaxSupportOwned;++j) {
                bool reserved=false;for(unsigned k=0;k<additions;++k)reserved=reserved || slots[k]==j;
                if(!reserved && (!supportOwners[j].ref || !Live(supportOwners[j].ref))){selected=j;break;}
            }
            if(selected==kMaxSupportOwned)return false;
            slots[additions++]=selected;
        }
        const auto previous=activeState;
        unsigned added=0;
        for(unsigned i=0;i<count;++i) {
            bool existing=false;
            for(const auto& owner:supportOwners)existing=existing || (owner.ref.obj==objects[i].obj && owner.ref.ctrl==objects[i].ctrl);
            if(existing)continue;
            auto& owner=supportOwners[slots[added++]];
            if(owner.ref)ReleaseSupportOwner(owner);
            auto* control=const_cast<unsigned char*>(static_cast<const unsigned char*>(objects[i].ctrl));
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(control+0xC));
            owner.ref=objects[i];std::memcpy(owner.puid,puid,std::strlen(puid)+1);
        }
        if(!team)return true; // mode-off calls retain identity for a later mode-on
        if(PvpNativeApply(previous))return true;
        for(unsigned i=0;i<added;++i)ReleaseSupportOwner(supportOwners[slots[i]]);
        if(!PvpNativeApply(previous))nativeFaulted=true;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){nativeFaulted=true;return false;}
}
void PvpNativeForgetSupport(const ObjRef& object) noexcept {
    __try {
        if(Saved* item=Find(object)) {
            if(Restore(*item)){if(item->ref)ReleaseWeak(*item);}
            else nativeFaulted=true;
        }
        for(auto& owner:supportOwners)if(owner.ref.obj==object.obj && owner.ref.ctrl==object.ctrl)ReleaseSupportOwner(owner);
    } __except(EXCEPTION_EXECUTE_HANDLER){nativeFaulted=true;}
}
void PvpNativeClearSupportOwners() noexcept {
    for(auto& owner:supportOwners)if(owner.ref) {
        __try {ReleaseSupportOwner(owner);} __except(EXCEPTION_EXECUTE_HANDLER){owner={};}
    }
}
}
