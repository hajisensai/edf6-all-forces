// npc_arms.cpp offline (docs/feature-npc-weapon-tier.md): the weapon load bookkeeping of a plan's soldiers across the
// mission boundary and a queue that did not take, with the preload and its completion query recorded. No game.
#include "../src/npc_arms.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config cfg{};
const Config& Cfg() noexcept { return cfg; }
void Log(const char*,...) noexcept {}
bool armsReady=false;
bool SupportSoldierArmsReady() noexcept { return armsReady; }
unsigned char* WeaponRecord(unsigned char*,std::uint32_t) noexcept { return nullptr; }
}
namespace {
using namespace crew;
int checks=0;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
int queued=0;bool queueFaults=false,done=false;
void FakePreload(void*,const wchar_t*,std::int32_t kind,std::int32_t all) {
    Check(kind==2 && all==-1,"stock resource preload flags");
    if(queueFaults)RaiseException(0xE0420010,0,0,nullptr);
    ++queued;
}
bool FakeDone(void*) { return done; }
alignas(16) unsigned char preloadMgr[64]{},objectMgr[64]{};
support_net::Plan RiflePlan(int id) {
    support_net::Plan plan{};
    plan.count=1;
    plan.units[0].resourceId=SupportSoldierResource(SupportWeapon::rifle,false);
    npctier::Arms a;a.on=true;a.id=static_cast<std::uint16_t>(id);
    plan.units[0].arms=npctier::PackArms(a);
    return plan;
}
}
int main() {
    using support_net::PlanResult;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x20C0000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(image!=nullptr,"private image");
    Put<void*>(image,kPreloadMgr,preloadMgr);Put<void*>(image,kObjectMgr,objectMgr);
    profileState=1;preloadFn=&FakePreload;loadDoneFn=&FakeDone;
    const auto plan=RiflePlan(26);
    Check(NpcArmsLoaded(plan)==PlanResult::pending && queued==1,"a weapon not loaded this mission: its load queued, pending");
    Check(NpcArmsLoaded(plan)==PlanResult::pending && queued==1,"still loading: not queued again");
    done=true;
    Check(NpcArmsLoaded(plan)==PlanResult::ready && queued==1,"its load complete: ready");
    Check(NpcArmsLoaded(plan)==PlanResult::ready && queued==1,"loaded this mission: ready at once");
    // 2026-11-09 review F2: the next mission, the same object manager (a startup singleton, or the same heap block).
    // The mission start begins the sets empty: the last mission's loads are not taken for this one's.
    done=false;
    PreloadNpcArms();
    Check(NpcArmsLoaded(plan)==PlanResult::pending && queued==2,"the next mission, same object manager: queued again, pending");
    done=true;
    Check(NpcArmsLoaded(plan)==PlanResult::ready,"the next mission's load complete: ready");
    // A pending load at the mission boundary is not carried over either.
    const auto other=RiflePlan(3);
    done=false;
    Check(NpcArmsLoaded(other)==PlanResult::pending && queued==3,"another weapon: queued");
    PreloadNpcArms();
    Check(NpcArmsLoaded(other)==PlanResult::pending && queued==4,"the last mission's pending load: asked for again");
    // Another object manager between mission starts still clears the sets (Follow).
    done=true;
    Check(NpcArmsLoaded(other)==PlanResult::ready,"loaded");
    alignas(16) static unsigned char nextMgr[64]{};
    Put<void*>(image,kObjectMgr,nextMgr);done=false;
    Check(NpcArmsLoaded(other)==PlanResult::pending && queued==5,"another object manager: queued again");
    // 2026-11-09 review F3: a queue that faulted (0x7A3780's SEH) leaves nothing asked for, so a later completion
    // (the queue empty) never marks that never-queued weapon loaded.
    PreloadNpcArms();
    const auto third=RiflePlan(6);
    queueFaults=true;
    Check(NpcArmsLoaded(third)==PlanResult::refused && queued==5,"the queue faulted: refused");
    queueFaults=false;
    Check(NpcArmsLoaded(third)==PlanResult::pending && queued==6,
          "the next plan with the weapon whose queue faulted: queued now, never taken for loaded");
    done=true;
    Check(NpcArmsLoaded(third)==PlanResult::ready,"then loaded on completion");
    // No preload manager: refused, nothing asked for.
    PreloadNpcArms();
    Put<void*>(image,kPreloadMgr,nullptr);
    Check(NpcArmsLoaded(third)==PlanResult::refused && queued==6,"no preload manager: refused");
    Put<void*>(image,kPreloadMgr,preloadMgr);
    Check(NpcArmsLoaded(third)==PlanResult::ready && queued==7,"the manager back: queued, then (done) ready");
    // 2026-11-21 (docs/feature-npc-weapon-tier.md 2.5): a Fencer's second hand is loaded as its main one; a second hand
    // alone, or one that is no Fencer hand weapon, refused.
    {PreloadNpcArms();done=false;
     support_net::Plan fencer{};fencer.count=1;fencer.units[0].resourceId=SupportSoldierResource(SupportWeapon::fencerPileBanker,false);
     npctier::Arms spear;spear.on=true;spear.id=715;npctier::Arms dexter;dexter.on=true;dexter.id=773;
     fencer.units[0].arms=npctier::PackArms(spear);fencer.units[0].offhand=npctier::PackArms(dexter);
     const int before=queued;
     Check(NpcArmsLoaded(fencer)==PlanResult::pending && queued==before+2,"both hands queued");
     done=true;Check(NpcArmsLoaded(fencer)==PlanResult::ready && queued==before+2,"both loaded: ready");
     auto alone=fencer;alone.units[0].arms=0;
     Check(NpcArmsLoaded(alone)==PlanResult::refused,"a second hand alone refused");
     auto rifle=fencer;npctier::Arms r;r.on=true;r.id=26;rifle.units[0].offhand=npctier::PackArms(r);
     Check(NpcArmsLoaded(rifle)==PlanResult::refused,"a rifle as a second hand refused");}
    // The requester's two hands from its Fencer loadout (whatever class it plays now): GameStatus class 3's slots 1L..2R.
    {alignas(16) static unsigned char gs[0x7000]{};
     Put<void*>(image,kGameStatus,gs);Put<std::int32_t>(gs,kClass,0);   // playing a Ranger
     const std::int32_t fencerSlots[4]={773,715,690,803};
     for(int i=0;i<4;++i)Put<std::int32_t>(gs,kWeapons+3*kClassStride+i*4,fencerSlots[i]);
     support_net::RequesterArms arms;
     armsReady=false;LocalRequesterArms(&arms);
     Check(!arms.kind[0] && !arms.offhand[static_cast<int>(SupportWeapon::fencerPileBanker)],"the weapon hooks not in: none");
     armsReady=true;LocalRequesterArms(&arms);
     const int pile=static_cast<int>(SupportWeapon::fencerPileBanker),cannon=static_cast<int>(SupportWeapon::fencerCannon);
     Check((arms.kind[pile]&0xFFFFu)==715 && (arms.offhand[pile]&0xFFFFu)==773 && (arms.offhand[pile]&npctier::kArmsOn),
           "the pile banker: the spear and the Dexter beside it (group 1)");
     Check((arms.kind[cannon]&0xFFFFu)==803 && (arms.offhand[cannon]&0xFFFFu)==690,"the cannon: group 2's NC101 and hammer");
     Check(arms.kind[0] && !arms.offhand[0],"a Ranger kind: no second hand");
     const int before=queued;
     done=false;PreloadNpcArms();
     Check(queued==before+1 && NpcArmsLoaded(RiflePlan(static_cast<int>(arms.kind[0]&0xFFFFu)))==PlanResult::ready,
           "the default rifle is the only eager support weapon");
     support_net::Plan call{};
     call.count=1;call.units[0].resourceId=SupportSoldierResource(SupportWeapon::fencerPileBanker,false);
     call.units[0].arms=arms.kind[pile];call.units[0].offhand=arms.offhand[pile];
     Check(NpcArmsLoaded(call)==PlanResult::pending && queued==before+3,"the Fencer request queues both weapons on demand");
     done=true;
     Check(NpcArmsLoaded(call)==PlanResult::ready,"the Fencer request can start after its weapons load");
     std::uint64_t a=0,b=0;
     Check(PresetHandArms(772,715,&a,&b) && (a&0xFFFFu)==772 && (b&0xFFFFu)==715,"a preset's two hands as unit words");
     Check(!PresetHandArms(26,715,&a,&b) && !a && !b,"a preset hand that is no Fencer weapon: none");
     armsReady=false;Check(!PresetHandArms(772,715,&a,&b),"the hooks not in: no preset hands");
     Put<void*>(image,kGameStatus,nullptr);}
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("npc_arms_test: %d checks passed\n",checks);
}
