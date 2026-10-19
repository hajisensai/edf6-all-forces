// Executes the production native cache/write/restore functions against a small
// fake object/control-block ABI. No EDF.dll, game installation or hooks are used.
#include "../src/pvp_native.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
bool EnemyControlRadarObserverTeam(const void*,int*) noexcept {return false;}
}

namespace {
unsigned lockCalls=0,deletedBlocks=0,deadObjectReads=0;
const void* watchDead=nullptr;
const void* unreadableWeapon=nullptr;
const void* refusedTeamObject=nullptr;
bool refuseTeamChange=false;
alignas(16) unsigned char lockPoints[64][0x50]{};
void __fastcall DestroyControl(void*) noexcept { ++deletedBlocks; }
void __fastcall NativeSetTeam(void* object,int team,bool registered) noexcept {
    if(refuseTeamChange || object==refusedTeamObject)return;
    edf::Put<int>(object,edf::kTeam,team);
    const auto flags=edf::At<unsigned>(object,0x380);
    edf::Put<unsigned>(object,0x380,registered ? flags|0x40u : flags&~0x40u);
}
int failures=0;
unsigned checks=0;
void Check(bool pass,const char* expression,int line) {
    ++checks;
    if(!pass){std::fprintf(stderr,"line %d: %s\n",line,expression);++failures;}
}
#define CHECK(value) Check((value),#value,__LINE__)
struct FakeObject {
    alignas(16) unsigned char bytes[0x2100]{};
    alignas(16) unsigned char control[0x20]{};
    void* points[16]{};
    void* weapons[1]{};
    unsigned char weapon[0x220]{};
    unsigned char weaponControl[0x20]{};
    unsigned char seat[0x348]{};
    FakeObject(bool soldier,int team,bool registered,bool seatedVehicle=false) {
        static void* controlTable[2]={nullptr,reinterpret_cast<void*>(&DestroyControl)};
        edf::Put<void*>(control,0,controlTable);
        edf::Put<LONG>(control,8,1);edf::Put<LONG>(control,12,1);
        edf::Put<void*>(bytes,0,soldier ? reinterpret_cast<void*>(1) : reinterpret_cast<void*>(2));
        edf::Put<void*>(bytes,edf::kSelf,bytes);edf::Put<void*>(bytes,edf::kSelfCtrl,control);
        edf::Put<int>(bytes,edf::kTeam,team);edf::Put<unsigned>(bytes,0x380,registered ? 0x40u : 0u);
        edf::Put<void*>(bytes,0x368,points);
        if(seatedVehicle){edf::Put<void*>(bytes,edf::kSeats,seat);edf::Put<unsigned>(bytes,edf::kSeatCount,1);}
        if(soldier) {
            weapons[0]=weapon;edf::Put<void*>(bytes,0x1950,weapons);edf::Put<std::uint64_t>(bytes,0x1960,1);
            edf::Put<void*>(weapon,0x120,bytes);edf::Put<int>(weapon,0x214,team);
            edf::Put<void*>(weapon,edf::kSelf,weapon);edf::Put<void*>(weapon,edf::kSelfCtrl,weaponControl);
            edf::Put<void*>(weaponControl,0,controlTable);edf::Put<LONG>(weaponControl,8,1);edf::Put<LONG>(weaponControl,12,1);
        }
    }
};
}
namespace edf {
bool Readable(const void* pointer,std::size_t,bool) noexcept {
    if(pointer==watchDead)++deadObjectReads;
    return pointer!=nullptr && pointer!=unreadableWeapon;
}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept { return true; }
unsigned SeatCount(const unsigned char* object) noexcept {
    const unsigned count=At<unsigned>(object,kSeatCount);return count<=16 ? count : 0;
}
bool RedirectCall(unsigned char*,void*,void*,bool& changed) noexcept { changed=false;return false; }
bool IsAnyPlayer(const unsigned char* object) noexcept { return At<const void*>(object,0)==reinterpret_cast<void*>(1); }
}
// This older fixture isolates the cache lifecycle at the typed adapter boundary.
// pvp_native_live_emitters_test runs the same caller with the real PE-audited adapter.
namespace crew::controlled_pvp {bool OwnsMember(const ObjRef&) noexcept{return false;}}
namespace crew {
bool PvpNativeWeaponEmitters(const ObjRef& owner,const void* weapon,controlled_pvp::Emitter* out,unsigned cap,unsigned* n) noexcept {
    if(n)*n=0;
    if(!out || !n || !cap || !Readable(weapon,0x218) || At<const void*>(weapon,0x120)!=owner.obj)return false;
    const ObjRef storage{weapon,At<const void*>(weapon,kSelfCtrl)};
    if(!storage.ctrl || !Readable(storage.ctrl,16) || At<LONG>(storage.ctrl,8)<=0)return false;
    out[0]={controlled_pvp::EmitterKind::ownerTeamCache,owner,static_cast<const unsigned char*>(weapon)+0x214,
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(storage.ctrl)),storage};*n=1;return true;
}
controlled_pvp::Life PvpNativeReadWeaponEmitter(const controlled_pvp::Emitter& e,int* team) noexcept {
    if(!team || !Readable(e.storage.ctrl,16))return controlled_pvp::Life::invalid;
    if(At<LONG>(e.storage.ctrl,8)<=0)return controlled_pvp::Life::gone;
    if(!Readable(e.storage.obj,kSelfCtrl+8) || !Readable(e.object,4))return controlled_pvp::Life::invalid;
    if(At<const void*>(e.storage.obj,kSelfCtrl)!=e.storage.ctrl)return controlled_pvp::Life::gone;
    *team=At<int>(e.object,0);return controlled_pvp::Life::live;
}
bool PvpNativeWriteWeaponEmitter(const controlled_pvp::Emitter& e,int team) noexcept {
    int current=-1;if(PvpNativeReadWeaponEmitter(e,&current)!=controlled_pvp::Life::live || !Readable(e.object,4,true))return false;
    Put<int>(const_cast<void*>(e.object),0,team);return true;
}
}
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
bool IsSoldierClass(const void* object) noexcept { return At<const void*>(object,0)==reinterpret_cast<void*>(1); }
int VehicleClassOf(const void* object) noexcept { return At<const void*>(object,0)==reinterpret_cast<void*>(2) ? 0 : -1; }
bool ReadNativeMissionLocation(unsigned* location) noexcept { if(location)*location=4;return true; }
namespace edf41c {
void* AddLockPoint(void* object) noexcept {
    if(lockCalls==_countof(lockPoints))return nullptr;
    auto* point=lockPoints[lockCalls++];
    Put<void*>(point,8,object);Put<unsigned char>(point,0x2A,1);
    auto** points=At<void**>(object,0x368);const auto count=At<std::uint64_t>(object,0x378);
    points[count*2]=point;Put<std::uint64_t>(object,0x378,count+1);return point;
}
void SetLockPoint(void* point,const float* position) noexcept { std::memcpy(static_cast<unsigned char*>(point)+0x10,position,12); }
}
}
int main() {
    // The production call site still calls a checked native-address function.
    // This fixture image supplies only that function as an absolute x64 jump.
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x550000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    CHECK(crew::image!=nullptr);if(!crew::image)return 1;
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto target=reinterpret_cast<std::uintptr_t>(&NativeSetTeam);std::memcpy(jump+2,&target,8);
    std::memcpy(crew::image+crew::kSetTeam,jump,sizeof(jump));
    FlushInstructionCache(GetCurrentProcess(),crew::image+crew::kSetTeam,sizeof(jump));
    FakeObject human(true,0,true);
    crew::Collection collection{};collection.objectsCount=1;
    collection.objects[0]={crew::ObjRef::Of(human.bytes),1,true,false};
    for(unsigned i=0;i<100;++i) {
        CHECK(crew::ApplyCollected(collection));
        CHECK(edf::At<int>(human.bytes,edf::kTeam)==1);
        CHECK(edf::At<int>(human.weapon,0x214)==1);
        CHECK((edf::At<unsigned>(human.bytes,0x380)&0x2000u)!=0);
        CHECK(crew::RestoreAll());
        CHECK(edf::At<int>(human.bytes,edf::kTeam)==0);
        CHECK(edf::At<int>(human.weapon,0x214)==0);
        CHECK((edf::At<unsigned>(human.bytes,0x380)&0x2000u)==0);
    }
    CHECK(lockCalls==1);CHECK(edf::At<std::uint64_t>(human.bytes,0x378)==1);
    CHECK(edf::At<unsigned char>(lockPoints[0],0x2A)==0);
    // The hostile pass already reads the actor's live team. The stock friendly
    // pass has a literal 2, so its isolated hook must map only our active player.
    CHECK(crew::ApplyCollected(collection));crew::activeState.enabled=true;crew::radarActive.store(true);
    unsigned char radar[0x790]{};edf::Put<void*>(radar,0x778,human.bytes);edf::Put<void*>(radar,0x780,human.control);
    CHECK(crew::RadarFriendTeam(radar,2)==1);CHECK(crew::RadarFriendTeam(radar,5)==5);
    CHECK(crew::RestoreAll());CHECK(crew::RadarFriendTeam(radar,2)==2);
    CHECK(edf::At<LONG>(human.control,12)==2); // one native, one retained weak cache identity
    crew::ResetPvpNative();CHECK(edf::At<LONG>(human.control,12)==1);
    for(const auto& item:crew::saved)CHECK(!item.ref);

    // A failed baseline restore must retain the original team and weak identity,
    // including a scene-reset attempt, so the next game frame can finish it.
    CHECK(crew::ApplyCollected(collection));refuseTeamChange=true;
    pvp_protocol::State disabled{};
    CHECK(!crew::PvpNativeApply(disabled));
    auto* pendingRestore=crew::Find(collection.objects[0].ref);
    CHECK(pendingRestore && pendingRestore->changed && pendingRestore->originalTeam==0);
    crew::ResetPvpNative();CHECK(crew::Find(collection.objects[0].ref)!=nullptr);
    CHECK(edf::At<LONG>(human.control,12)==2);
    refuseTeamChange=false;CHECK(crew::PvpNativeApply(disabled));
    CHECK(edf::At<int>(human.bytes,edf::kTeam)==0);
    crew::ResetPvpNative();CHECK(edf::At<LONG>(human.control,12)==1);

    FakeObject vehicle(false,5,false);
    crew::Collection riding{};riding.objectsCount=1;riding.objects[0]={crew::ObjRef::Of(vehicle.bytes),1,false,true};
    CHECK(crew::ApplyCollected(riding));CHECK(edf::At<LONG>(vehicle.control,12)==2);
    CHECK(crew::RestoreAll());CHECK(edf::At<LONG>(vehicle.control,12)==1);
    CHECK(edf::At<int>(vehicle.bytes,edf::kTeam)==5);
    CHECK((edf::At<unsigned>(vehicle.bytes,0x380)&0x40u)==0);
    for(const auto& item:crew::saved)CHECK(!item.ref);

    CHECK(crew::ApplyCollected(collection));
    watchDead=human.bytes;deadObjectReads=0;
    edf::Put<LONG>(human.control,8,0);edf::Put<LONG>(human.control,12,1); // native destruction released its implicit weak
    const auto oldRef=crew::ObjRef::Of(human.bytes);
    CHECK(!crew::Live(oldRef));CHECK(deadObjectReads==0); // control block checked before dead object
    FakeObject replacement(true,0,true);
    // Simulate an address reuse: an object at the same address has a new control.
    edf::Put<void*>(human.bytes,edf::kSelfCtrl,replacement.control);
    watchDead=nullptr;
    crew::Desired reused{crew::ObjRef::Of(human.bytes),1,true,false};
    CHECK(crew::Reserve(reused)!=nullptr);
    CHECK(deletedBlocks==1);CHECK(edf::At<LONG>(replacement.control,12)==2);
    crew::ResetPvpNative();CHECK(edf::At<LONG>(replacement.control,12)==1);
    // Standalone support units keep authenticated ownership while mode-off;
    // enabling and changing an owner's faction reuses the same native path.
    FakeObject supportNpc(true,2,true),supportVehicle(false,5,true,true);
    const crew::ObjRef supportRefs[]={crew::ObjRef::Of(supportNpc.bytes),crew::ObjRef::Of(supportVehicle.bytes)};
    CHECK(!crew::PvpNativeAttachSupport("bad owner!",supportRefs,2,0));
    CHECK(!crew::PvpNativeAttachSupport("owner1",supportRefs,17,0));
    CHECK(crew::PvpNativeAttachSupport("owner1",supportRefs,2,0));
    CHECK(edf::At<int>(supportNpc.bytes,edf::kTeam)==2);
    CHECK(edf::At<int>(supportVehicle.bytes,edf::kTeam)==5);
    CHECK(edf::At<LONG>(supportNpc.control,12)==2);
    pvp_protocol::State ownedState{};ownedState.enabled=true;ownedState.count=1;
    std::memcpy(ownedState.entries[0].puid,"owner1",7);ownedState.entries[0].team=1;
    crew::Collection owned{};CHECK(crew::AppendOwnedSupport(owned,ownedState));CHECK(owned.objectsCount==2);
    CHECK(crew::ApplyCollected(owned));
    CHECK(edf::At<int>(supportNpc.bytes,edf::kTeam)==1 && edf::At<int>(supportNpc.weapon,0x214)==1);
    CHECK(edf::At<int>(supportVehicle.bytes,edf::kTeam)==1);
    crew::radarActive.store(true);
    CHECK(crew::VehicleAssignedTeam(supportVehicle.bytes,5)==1);
    crew::RecomputeVehicleTeam(supportVehicle.bytes,5,true);
    CHECK(edf::At<int>(supportVehicle.bytes,edf::kTeam)==1); // native empty-vehicle team 5 cannot overwrite ownership
    ownedState.entries[0].team=2;owned={};CHECK(crew::AppendOwnedSupport(owned,ownedState));
    CHECK(crew::ApplyCollected(owned));CHECK(edf::At<int>(supportNpc.bytes,edf::kTeam)==2);
    CHECK(edf::At<int>(supportVehicle.bytes,edf::kTeam)==2);
    CHECK(crew::VehicleAssignedTeam(supportVehicle.bytes,5)==2);
    crew::RecomputeVehicleTeam(supportVehicle.bytes,5,true);CHECK(edf::At<int>(supportVehicle.bytes,edf::kTeam)==2);
    CHECK(crew::RestoreAll());CHECK(edf::At<int>(supportVehicle.bytes,edf::kTeam)==5);
    CHECK(edf::At<int>(supportNpc.bytes,edf::kTeam)==2);
    crew::ResetPvpNative(); // transport loss restores teams but preserves owner metadata
    CHECK(edf::At<LONG>(supportNpc.control,12)==2);
    ownedState.entries[0].team=1;owned={};CHECK(crew::AppendOwnedSupport(owned,ownedState));
    CHECK(crew::ApplyCollected(owned));CHECK(edf::At<int>(supportVehicle.bytes,edf::kTeam)==1);
    crew::PvpNativeForgetSupport(supportRefs[1]);CHECK(edf::At<int>(supportVehicle.bytes,edf::kTeam)==5);
    CHECK(edf::At<LONG>(supportVehicle.control,12)==1);
    CHECK(crew::RestoreAll());crew::ResetPvpNative();crew::PvpNativeClearSupportOwners();
    CHECK(edf::At<LONG>(supportNpc.control,12)==1);
    for(const auto& owner:crew::supportOwners)CHECK(!owner.ref);
    const unsigned failureChecksStart=checks;
    // Start from a complete applied batch, then fail recollection of its second
    // weapon. Independent cache restores can progress, but the body's Saved state
    // stays pending until every captured weapon can be verified/restored.
    {
        FakeObject first(true,0,true),partial(true,2,false),tail(true,0,true);
        unsigned char secondWeapon[0x220]{},secondControl[0x20]{};
        edf::Put<void*>(secondWeapon,0x120,partial.bytes);edf::Put<int>(secondWeapon,0x214,2);
        edf::Put<void*>(secondWeapon,edf::kSelfCtrl,secondControl);
        edf::Put<LONG>(secondControl,8,1);edf::Put<LONG>(secondControl,12,1);
        void* partialWeapons[]={partial.weapon,secondWeapon};
        edf::Put<void*>(partial.bytes,0x1950,partialWeapons);edf::Put<std::uint64_t>(partial.bytes,0x1960,2);
        crew::Collection batch{};batch.objectsCount=3;
        batch.objects[0]={crew::ObjRef::Of(first.bytes),1,true,false};
        batch.objects[1]={crew::ObjRef::Of(partial.bytes),1,true,false};
        batch.objects[2]={crew::ObjRef::Of(tail.bytes),1,true,false};
        const unsigned firstFlags=edf::At<unsigned>(first.bytes,0x380);
        const unsigned partialFlags=edf::At<unsigned>(partial.bytes,0x380);
        const unsigned tailFlags=edf::At<unsigned>(tail.bytes,0x380);
        CHECK(crew::ApplyCollected(batch));
        unreadableWeapon=secondWeapon;
        CHECK(!crew::ApplyCollected(batch));
        CHECK(edf::At<int>(first.bytes,edf::kTeam)==1);
        CHECK(edf::At<int>(first.weapon,0x214)==1);
        CHECK(edf::At<int>(partial.bytes,edf::kTeam)==1);
        CHECK(edf::At<int>(partial.weapon,0x214)==1);
        CHECK(edf::At<int>(secondWeapon,0x214)==1);
        CHECK(edf::At<unsigned>(partial.bytes,0x380)==(partialFlags|0x40u|0x2000u));
        CHECK(edf::At<int>(tail.bytes,edf::kTeam)==1 && edf::At<int>(tail.weapon,0x214)==1);
        CHECK(edf::At<unsigned>(tail.bytes,0x380)==(tailFlags|0x2000u));
        CHECK(edf::At<LONG>(first.control,12)==2 && edf::At<LONG>(partial.control,12)==2 && edf::At<LONG>(tail.control,12)==2);
        CHECK(crew::Find(batch.objects[0].ref)->changed);
        CHECK(crew::Find(batch.objects[1].ref)->changed);
        CHECK(crew::Find(batch.objects[2].ref)->changed);

        CHECK(!crew::PvpNativeApply(disabled));
        CHECK(edf::At<int>(first.bytes,edf::kTeam)==0 && edf::At<int>(first.weapon,0x214)==0);
        CHECK(edf::At<unsigned>(first.bytes,0x380)==firstFlags);
        CHECK(crew::Find(batch.objects[0].ref) && !crew::Find(batch.objects[0].ref)->changed);
        CHECK(edf::At<int>(partial.bytes,edf::kTeam)==1 && edf::At<int>(partial.weapon,0x214)==2);
        CHECK(edf::At<unsigned>(partial.bytes,0x380)==(partialFlags|0x40u|0x2000u));
        const auto* partialSaved=crew::Find(batch.objects[1].ref);
        CHECK(partialSaved && partialSaved->changed);
        CHECK(partialSaved && partialSaved->originalTeam==2 && !partialSaved->originalRegistered && !partialSaved->originalNotCounted);
        CHECK(edf::At<LONG>(partial.control,12)==2);
        CHECK(crew::Find(batch.objects[2].ref) && !crew::Find(batch.objects[2].ref)->changed && edf::At<LONG>(tail.control,12)==2);
        CHECK(!crew::activeState.enabled && !crew::radarActive.load());

        unreadableWeapon=nullptr;
        CHECK(crew::PvpNativeApply(disabled));
        CHECK(edf::At<int>(first.bytes,edf::kTeam)==0 && edf::At<int>(first.weapon,0x214)==0);
        CHECK(edf::At<int>(partial.bytes,edf::kTeam)==2 && edf::At<int>(partial.weapon,0x214)==2 && edf::At<int>(secondWeapon,0x214)==2);
        CHECK(edf::At<int>(tail.bytes,edf::kTeam)==0 && edf::At<int>(tail.weapon,0x214)==0);
        CHECK(edf::At<unsigned>(first.bytes,0x380)==firstFlags && edf::At<unsigned>(partial.bytes,0x380)==partialFlags && edf::At<unsigned>(tail.bytes,0x380)==tailFlags);
        CHECK(crew::Find(batch.objects[1].ref) && !crew::Find(batch.objects[1].ref)->changed && edf::At<LONG>(partial.control,12)==2);
        crew::ResetPvpNative();
        CHECK(edf::At<LONG>(first.control,12)==1 && edf::At<LONG>(partial.control,12)==1 && edf::At<LONG>(tail.control,12)==1);
        CHECK(!crew::Find(batch.objects[0].ref) && !crew::Find(batch.objects[1].ref) && !crew::Find(batch.objects[2].ref));
    }
    // RestoreAll must continue after a failed middle object and retain only its
    // pending original state/weak reference. A scene-reset attempt must not erase
    // that pending state, and retrying after the injected fault is removed must
    // finish every team/weapon/flag and release each weak count exactly once.
    {
        FakeObject first(true,2,true),middle(true,0,false),last(true,2,true);
        crew::Collection batch{};batch.objectsCount=3;
        batch.objects[0]={crew::ObjRef::Of(first.bytes),1,false,false};
        batch.objects[1]={crew::ObjRef::Of(middle.bytes),1,false,false};
        batch.objects[2]={crew::ObjRef::Of(last.bytes),1,false,false};
        const unsigned firstFlags=edf::At<unsigned>(first.bytes,0x380);
        const unsigned middleFlags=edf::At<unsigned>(middle.bytes,0x380);
        const unsigned lastFlags=edf::At<unsigned>(last.bytes,0x380);
        CHECK(crew::ApplyCollected(batch));
        CHECK(edf::At<int>(first.bytes,edf::kTeam)==1 && edf::At<int>(middle.bytes,edf::kTeam)==1 && edf::At<int>(last.bytes,edf::kTeam)==1);
        CHECK(edf::At<int>(first.weapon,0x214)==1 && edf::At<int>(middle.weapon,0x214)==1 && edf::At<int>(last.weapon,0x214)==1);
        CHECK(edf::At<LONG>(first.control,12)==2 && edf::At<LONG>(middle.control,12)==2 && edf::At<LONG>(last.control,12)==2);
        refusedTeamObject=middle.bytes;
        CHECK(!crew::RestoreAll());
        CHECK(edf::At<int>(first.bytes,edf::kTeam)==2 && edf::At<int>(first.weapon,0x214)==2);
        CHECK(edf::At<unsigned>(first.bytes,0x380)==firstFlags);
        CHECK(!crew::Find(batch.objects[0].ref) && edf::At<LONG>(first.control,12)==1);
        CHECK(edf::At<int>(middle.bytes,edf::kTeam)==1 && edf::At<int>(middle.weapon,0x214)==0);
        CHECK(edf::At<unsigned>(middle.bytes,0x380)==(middleFlags|0x2000u));
        const auto* middleSaved=crew::Find(batch.objects[1].ref);
        CHECK(middleSaved && middleSaved->changed && middleSaved->originalTeam==0);
        CHECK(middleSaved && !middleSaved->originalRegistered && !middleSaved->originalNotCounted);
        CHECK(edf::At<LONG>(middle.control,12)==2);
        CHECK(edf::At<int>(last.bytes,edf::kTeam)==2 && edf::At<int>(last.weapon,0x214)==2);
        CHECK(edf::At<unsigned>(last.bytes,0x380)==lastFlags);
        CHECK(!crew::Find(batch.objects[2].ref) && edf::At<LONG>(last.control,12)==1);
        crew::ResetPvpNative();
        middleSaved=crew::Find(batch.objects[1].ref);
        CHECK(middleSaved && middleSaved->changed && middleSaved->originalTeam==0);
        CHECK(edf::At<LONG>(middle.control,12)==2);
        CHECK(edf::At<LONG>(first.control,12)==1 && edf::At<LONG>(last.control,12)==1);
        CHECK(!crew::activeState.enabled && !crew::radarActive.load());
        refusedTeamObject=nullptr;
        CHECK(crew::RestoreAll());
        CHECK(edf::At<int>(first.bytes,edf::kTeam)==2 && edf::At<int>(first.weapon,0x214)==2);
        CHECK(edf::At<int>(middle.bytes,edf::kTeam)==0 && edf::At<int>(middle.weapon,0x214)==0);
        CHECK(edf::At<int>(last.bytes,edf::kTeam)==2 && edf::At<int>(last.weapon,0x214)==2);
        CHECK(edf::At<unsigned>(first.bytes,0x380)==firstFlags && edf::At<unsigned>(middle.bytes,0x380)==middleFlags && edf::At<unsigned>(last.bytes,0x380)==lastFlags);
        CHECK(!crew::Find(batch.objects[0].ref) && !crew::Find(batch.objects[1].ref) && !crew::Find(batch.objects[2].ref));
        CHECK(edf::At<LONG>(first.control,12)==1 && edf::At<LONG>(middle.control,12)==1 && edf::At<LONG>(last.control,12)==1);
        CHECK(crew::RestoreAll());
        CHECK(edf::At<LONG>(first.control,12)==1 && edf::At<LONG>(middle.control,12)==1 && edf::At<LONG>(last.control,12)==1);
    }
    const unsigned failureInjectionChecks=checks-failureChecksStart;
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    if(failures)return 1;
    std::printf("PVP native fixture: %u added failure-injection checks passed\n",failureInjectionChecks);
    std::puts("PVP native fixture: toggle, support ownership, radar/vehicle native recompute, restore, weak and ABA checks passed");return 0;
}
