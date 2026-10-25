// Original PVP caller + real PE-audited weapon adapter. Reuse only the NPC fixture's ABI setup.
#define main ExistingNpcFixtureMain
#include "pvp_controlled_npc_test.cpp"
#undef main
#include "../src/pvp_native.cpp"
namespace crew {
// team_native.cpp is not linked: the manager is a stand-in (the relations are pvp_native_fixture's concern).
bool SetTeamOverlay(const team_native::Overlay&) noexcept {return true;}
void ClearTeamOverlay() noexcept {}
int NativeTeamRows() noexcept {return team_native::kTeamCapacity;}
}

namespace native_live_fixture {
unsigned lockCount=0;
unsigned char points[32][0x50]{};
void* lockRows[64]{};
void Setup() {
    crew::ResetPvpNative();npc_fixture::Reset();lockCount=0;
    std::memset(lockRows,0,sizeof(lockRows));
    crew::Put<void*>(npc_fixture::actor.b,0x368,lockRows);
    crew::Put<std::size_t>(npc_fixture::actor.b,0x378,0);
}
crew::Collection Desired(int team) {
    crew::Collection c{};c.objectsCount=1;c.objects[0]={npc_fixture::actor.Ref(),team,true,false};return c;
}
void AllOriginalWeaponFamilies() {
    using namespace crew;using namespace npc_fixture;
    constexpr unsigned professions[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};
    for(unsigned actorTable:professions)for(const auto& t:npc_pvp_detail::kSharedWeapons) {
        Setup();Put<void*>(actor.b,0,image+actorTable);Put<void*>(weapons[0].b,0,image+t.table);
        auto c=Desired(1);CHECK(ApplyCollected(c));CHECK(At<int>(weapons[0].b,0x214)==1);
        c=Desired(2);CHECK(ApplyCollected(c));CHECK(At<int>(weapons[0].b,0x214)==2);
        CHECK(RestoreAll());CHECK(At<int>(weapons[0].b,0x214)==4 && At<int>(actor.b,kTeam)==5);
        ResetPvpNative();CHECK(At<LONG>(weapons[0].c,12)==1 && At<LONG>(actor.c,12)==1);
    }
    // The one stock FireType 1 resource is DLC Weapon_Sub (Psi Blast Wall).
    // Its retained BulletBase can have C30=0; neither mode nor FireCount is a proof of absence.
    Setup();for(const auto& t:npc_pvp_detail::kSharedWeapons)if(!std::strcmp(t.name,"Weapon_Sub"))Put<void*>(weapons[0].b,0,image+t.table);
    Put<unsigned>(weapons[0].b,0x208,1);Put<int>(weapons[0].b,0x368,40);Put<int>(bullets[0].b,0xC30,0);Held(1);
    auto c=Desired(1);CHECK(ApplyCollected(c));CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);
    c=Desired(2);CHECK(ApplyCollected(c));CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);
    CHECK(RestoreAll());CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);ResetPvpNative();
    Setup();Put<void*>(weapons[0].b,0,image+npc_pvp_detail::kWeapons[3].table);Held(1);
    Put<void*>(bullets[0].b,0,image+npc_pvp_detail::kShieldBullet.table);Put<void*>(bullets[0].b,0xE98,actor.b);Put<void*>(bullets[0].b,0xEA0,actor.c);Put<int>(bullets[0].b,0xEF0,6);
    Put<int>(bullets[0].b,0xB40,0x66666666);Put<int>(bullets[0].b,0x894,0x77777777);Put<void*>(bullets[0].b,0xAF0,nullptr);Put<void*>(bullets[0].b,0x888,nullptr);
    c=Desired(1);CHECK(ApplyCollected(c));c=Desired(2);CHECK(ApplyCollected(c));CHECK(At<int>(bullets[0].b,0xEF0)==2);
    CHECK(RestoreAll());CHECK(At<int>(bullets[0].b,0xEF0)==6 && At<int>(bullets[0].b,0xB40)==0x66666666 && At<int>(bullets[0].b,0x894)==0x77777777);ResetPvpNative();
    // A known table with any changed executable slot must fail before an emitter write.
    Setup();auto* table=image+npc_pvp_detail::kBasic.table;const auto* original=At<const void*>(table,40*8);Put<void*>(table,40*8,image+0xABCD);
    c=Desired(1);CHECK(!ApplyCollected(c));CHECK(At<int>(weapons[0].b,0x214)==4);CHECK(RestoreAll());Put<const void*>(table,40*8,original);ResetPvpNative();
}
void FlipAndBaseline() {
    using namespace crew;using namespace npc_fixture;
    Setup();Held(1);auto c=Desired(1);CHECK(ApplyCollected(c));
    CHECK(At<int>(actor.b,kTeam)==1 && At<int>(weapons[0].b,0x214)==1);
    CHECK(At<int>(bullets[0].b,0xB40)==1 && At<int>(bullets[0].b,0x894)==1);
    CHECK(At<LONG>(weapons[0].c,12)==2 && At<LONG>(bullets[0].c,12)==3);
    c=Desired(2);CHECK(ApplyCollected(c));
    CHECK(At<int>(actor.b,kTeam)==2 && At<int>(bullets[0].b,0xB40)==2 && At<int>(bullets[0].b,0x894)==2);
    CHECK(RestoreAll());CHECK(At<int>(actor.b,kTeam)==5 && At<int>(weapons[0].b,0x214)==4);
    CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);
    CHECK(At<LONG>(weapons[0].c,12)==1 && At<LONG>(bullets[0].c,12)==1);
    ResetPvpNative();
    Setup();c=Desired(1);CHECK(ApplyCollected(c));
    Put<int>(bullets[0].b,0xB40,1);Put<int>(bullets[0].b,0x894,1);Held(1);
    c=Desired(2);CHECK(ApplyCollected(c));CHECK(RestoreAll());
    // New bullets inherit the existing weapon baseline 4, never the temporary team or body's 5.
    CHECK(At<int>(bullets[0].b,0xB40)==4 && At<int>(bullets[0].b,0x894)==4);
    ResetPvpNative();
}
void DetachedAndDead() {
    using namespace crew;using namespace npc_fixture;
    Setup();Held(1);auto c=Desired(1);CHECK(ApplyCollected(c));
    Put<std::size_t>(actor.b,0x1960,0);Put<std::size_t>(actor.b,0x58,0);
    c=Desired(2);CHECK(ApplyCollected(c));
    // Retirement restores surviving storage without consulting the discarded holder/roster.
    CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);
    Put<LONG>(weapons[0].c,8,0);InterlockedDecrement(reinterpret_cast<volatile LONG*>(weapons[0].c+12));
    Put<LONG>(actor.c,8,0);InterlockedDecrement(reinterpret_cast<volatile LONG*>(actor.c+12));
    denied=watched=actor.b;watchedReads=0;
    CHECK(RestoreAll());CHECK(watchedReads==0);
    CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);
    CHECK(At<LONG>(actor.c,12)==0 && At<LONG>(weapons[0].c,12)==0 && At<LONG>(bullets[0].c,12)==1);
    denied=watched=nullptr;ResetPvpNative();
}
void ReadOnlyRetry() {
    using namespace crew;using namespace npc_fixture;
    Setup();Held(1);Put<int>(bullets[0].b,0x894,6);auto c=Desired(1);CHECK(ApplyCollected(c));c=Desired(2);CHECK(ApplyCollected(c));
    auto* protectedPage=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(bullets[0].b+0x894)&~std::uintptr_t(0xFFF));
    DWORD old=0;CHECK(VirtualProtect(protectedPage,0x1000,PAGE_READONLY,&old)!=0);
    pvp_protocol::State off{};CHECK(!PvpNativeApply(off));
    const auto* pending=Find(actor.Ref());CHECK(pending && pending->changed && pending->originalTeam==5);
    CHECK(At<int>(bullets[0].b,0x894)==2 && At<LONG>(bullets[0].c,12)==3);
    ResetPvpNative();CHECK(Find(actor.Ref())!=nullptr && At<LONG>(bullets[0].c,12)==3);
    CHECK(At<int>(weapons[0].b,0x214)==4 && At<LONG>(weapons[0].c,12)==2);
    // Protocol may reapply its previous requested state after partial rollback.
    // The independently different weapon baseline must survive that transaction.
    c=Desired(1);CHECK(!ApplyCollected(c));
    DWORD ignored=0;CHECK(VirtualProtect(protectedPage,0x1000,old,&ignored)!=0);
    CHECK(ApplyCollected(c));CHECK(PvpNativeApply(off));
    CHECK(At<int>(weapons[0].b,0x214)==4);CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==6);
    CHECK(At<LONG>(bullets[0].c,12)==1);ResetPvpNative();CHECK(At<LONG>(actor.c,12)==1);
}
void NativeStoppedBeamRetires() {
    using namespace crew;using namespace npc_fixture;
    Setup();Held(1);auto c=Desired(1);CHECK(ApplyCollected(c));Put<unsigned>(bullets[0].b,0xC34,1u<<14);
    c=Desired(2);CHECK(ApplyCollected(c));CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);
    CHECK(At<unsigned>(bullets[0].b,0xC34)==(1u<<14));CHECK(RestoreAll());ResetPvpNative();
}
void BodyLastTeamAndNativeLife() {
    using namespace crew;using namespace npc_fixture;
    Setup();Put<int>(actor.b,kTeam,-1);Put<int>(actor.b,0x318,4);auto c=Desired(1);
    CHECK(ApplyCollected(c));CHECK(At<int>(actor.b,kTeam)==-1 && At<int>(actor.b,0x318)==1);
    CHECK(Find(actor.Ref()) && At<unsigned char>(Find(actor.Ref())->lockPoint,0x2A)==0);
    c=Desired(2);CHECK(ApplyCollected(c));CHECK(At<int>(actor.b,kTeam)==-1 && At<int>(actor.b,0x318)==2);
    CHECK(RestoreAll());CHECK(At<int>(actor.b,kTeam)==-1 && At<int>(actor.b,0x318)==4);ResetPvpNative();
    Setup();Put<int>(actor.b,kTeam,-1);Put<int>(actor.b,0x318,4);c=Desired(2);CHECK(ApplyCollected(c));
    SetTeam(actor.b,2,true); // native revival, independent of our write epoch
    roster[0]=scene[0]=weapons[1].b;Put<int>(weapons[1].b,0x214,2);CHECK(ApplyCollected(c));
    CHECK(RestoreAll());CHECK(At<int>(actor.b,kTeam)==4 && At<int>(actor.b,0x318)==4 && At<int>(weapons[1].b,0x214)==4);ResetPvpNative();
    Setup();c=Desired(1);CHECK(ApplyCollected(c));Put<int>(actor.b,kTeam,-1);
    c=Desired(2);CHECK(ApplyCollected(c));CHECK(At<int>(actor.b,kTeam)==-1 && At<int>(actor.b,0x318)==2);
    CHECK(RestoreAll());CHECK(At<int>(actor.b,kTeam)==-1 && At<int>(actor.b,0x318)==6);ResetPvpNative();
    Setup();Put<int>(actor.b,kTeam,2);Put<int>(actor.b,0x318,4);c=Desired(2);
    CHECK(ApplyCollected(c));CHECK(At<int>(actor.b,0x318)==2); // native SetTeam may early-return on the matching 314
    CHECK(RestoreAll());CHECK(At<int>(actor.b,kTeam)==2 && At<int>(actor.b,0x318)==4);ResetPvpNative();
}
void OverlapRejected() {
    using namespace crew;using namespace npc_fixture;
    Setup();auto proof=Proof();controlled_pvp::Lease lease{};CHECK(controlled_pvp::Acquire(actor.Ref(),"owner1",proof.hooks,&lease));
    const unsigned previousWrites=nativeWrites;crew::Collection attempted{};auto c=Desired(2);
    CHECK(AddObject(attempted,actor.b,2,true) && attempted.objectsCount==0);CHECK(ApplyCollected(c));
    CHECK(nativeWrites==previousWrites && At<int>(actor.b,kTeam)==1 && Find(actor.Ref())==nullptr);
    CHECK(controlled_pvp::Restore(&lease));CHECK(At<int>(actor.b,kTeam)==5 && At<int>(weapons[0].b,0x214)==4);ResetPvpNative();
    Setup();c=Desired(1);CHECK(ApplyCollected(c));proof=Proof();CHECK(controlled_pvp::Acquire(actor.Ref(),"owner1",proof.hooks,&lease));
    const unsigned overlappingWrites=nativeWrites;attempted={};c=Desired(2);
    CHECK(!AddObject(attempted,actor.b,2,true));CHECK(!ApplyCollected(c));CHECK(!RestoreAll());
    CHECK(nativeWrites==overlappingWrites && At<int>(actor.b,kTeam)==1 && At<int>(weapons[0].b,0x214)==1);
    CHECK(Find(actor.Ref()) && Find(actor.Ref())->changed && Find(actor.Ref())->originalTeam==5);
    CHECK(controlled_pvp::Restore(&lease));CHECK(RestoreAll());
    CHECK(At<int>(actor.b,kTeam)==5 && At<int>(weapons[0].b,0x214)==4);ResetPvpNative();
}
void IncarnationAndClosedState() {
    using namespace crew;using namespace npc_fixture;
    Setup();Held(1);auto c=Desired(1);CHECK(ApplyCollected(c));
    Object replacement{};Identity(replacement);
    Put<void*>(bullets[0].b,kSelfCtrl,replacement.c);Put<int>(bullets[0].b,0xB40,6);Put<int>(bullets[0].b,0x894,6);
    CHECK(RestoreAll());CHECK(At<int>(bullets[0].b,0xB40)==6 && At<int>(bullets[0].b,0x894)==6);
    CHECK(At<LONG>(bullets[0].c,12)==1 && At<LONG>(replacement.c,12)==1);
    Put<void*>(nodes[0][0],0x18,replacement.c); // the native held node now names the new incarnation
    c=Desired(2);CHECK(ApplyCollected(c));CHECK(At<int>(bullets[0].b,0x894)==2 && At<LONG>(replacement.c,12)==3);
    CHECK(RestoreAll());CHECK(At<int>(bullets[0].b,0xB40)==6 && At<int>(bullets[0].b,0x894)==6);
    CHECK(At<LONG>(replacement.c,12)==1);ResetPvpNative();
    Setup();Held(1);Put<int>(bullets[0].b,0xB40,-1);c=Desired(1);CHECK(ApplyCollected(c));
    CHECK(At<int>(bullets[0].b,0xB40)==-1 && At<int>(bullets[0].b,0x894)==1);
    Put<int>(bullets[0].b,0x894,-1);c=Desired(2);CHECK(ApplyCollected(c));CHECK(RestoreAll());
    CHECK(At<int>(bullets[0].b,0xB40)==-1 && At<int>(bullets[0].b,0x894)==-1);ResetPvpNative();
}
}
namespace crew {
bool EnemyControlRadarObserverTeam(const void*,int*) noexcept{return false;}
bool IsSoldierClass(const void* o) noexcept {
    if(!Readable(o,8))return false;const auto* table=At<const unsigned char*>(o,0);
    for(unsigned rva:{0x17CDF28u,0x17D0FF8u,0x17CF5B8u,0x17CF100u})if(table==image+rva)return true;
    return false;
}
int VehicleClassOf(const void*) noexcept{return -1;}
bool ReadNativeMissionLocation(unsigned* location) noexcept{if(location)*location=4;return true;}
namespace edf41c {
void* AddLockPoint(void* object) noexcept {
    using namespace native_live_fixture;if(lockCount==32)return nullptr;auto* point=points[lockCount++];
    Put<void*>(point,8,object);Put<unsigned char>(point,0x2A,1);
    auto** rows=At<void**>(object,0x368);const auto n=At<std::size_t>(object,0x378);if(!rows || n>=32)return nullptr;
    rows[n*2]=point;Put<std::size_t>(object,0x378,n+1);return point;
}
void SetLockPoint(void* point,const float* xyz) noexcept{std::memcpy(static_cast<unsigned char*>(point)+0x10,xyz,12);}
}
}
namespace edf {
unsigned SeatCount(const unsigned char*) noexcept{return 0;}
bool RedirectCall(unsigned char*,void*,void*,bool& changed) noexcept{changed=false;return false;}
bool IsAnyPlayer(const unsigned char* p) noexcept{return crew::IsSoldierClass(p);}
}
int main() {
    using namespace crew;using namespace npc_fixture;using namespace native_live_fixture;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)return 2;Origin();
    unsigned char thunk[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};const auto target=reinterpret_cast<std::uintptr_t>(&SetTeam);std::memcpy(thunk+2,&target,8);std::memcpy(image+kSetTeam,thunk,sizeof(thunk));FlushInstructionCache(GetCurrentProcess(),image+kSetTeam,sizeof(thunk));
    AllOriginalWeaponFamilies();FlipAndBaseline();DetachedAndDead();ReadOnlyRetry();IncarnationAndClosedState();NativeStoppedBeamRetires();BodyLastTeamAndNativeLife();OverlapRejected();ResetPvpNative();
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;std::printf("pvp_native_live_emitters_test: %u checks, %u failed\n",checks,failed);return failed ? 1 : 0;
}
