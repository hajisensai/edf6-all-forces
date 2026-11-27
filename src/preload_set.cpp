// This mission's preload set: preload_set.h.
#include "preload_set.h"
#include "config.h"
#include "plugin_log.h"
#include "jet.h"
#include "debug_spawn.h"
#include "jet_internal.h"
#include "room_pvp.h"
#include "support_call.h"
#include "support_config.h"
#include <Windows.h>

namespace crew::preload {
namespace {
// The names preload_set_logic.h gives jet_internal.h's bodies.
#define EDF6_PRELOAD_BODY(n) static_assert(static_cast<int>(jet::Body::n)==body::n,"preload_set_logic.h body::" #n)
EDF6_PRELOAD_BODY(strike);EDF6_PRELOAD_BODY(fighter);EDF6_PRELOAD_BODY(bomber401);EDF6_PRELOAD_BODY(bomber501_2);
EDF6_PRELOAD_BODY(interceptor);EDF6_PRELOAD_BODY(multirole);EDF6_PRELOAD_BODY(carrier);EDF6_PRELOAD_BODY(drone);
EDF6_PRELOAD_BODY(blast);EDF6_PRELOAD_BODY(doll);EDF6_PRELOAD_BODY(heli410);EDF6_PRELOAD_BODY(heli506);
EDF6_PRELOAD_BODY(gunship);EDF6_PRELOAD_BODY(blastCarrier);EDF6_PRELOAD_BODY(dollCarrier);EDF6_PRELOAD_BODY(enemyFighter);
EDF6_PRELOAD_BODY(primerFighter);EDF6_PRELOAD_BODY(centipede);EDF6_PRELOAD_BODY(dragonfly);EDF6_PRELOAD_BODY(heliMedic);
EDF6_PRELOAD_BODY(heliTransport);EDF6_PRELOAD_BODY(transportPlane);EDF6_PRELOAD_BODY(dnFighter);EDF6_PRELOAD_BODY(dnDefender);
EDF6_PRELOAD_BODY(dnBomber);EDF6_PRELOAD_BODY(dnInterceptor);EDF6_PRELOAD_BODY(xmFighter);EDF6_PRELOAD_BODY(xmAttack);
EDF6_PRELOAD_BODY(xmHeavyAttack);EDF6_PRELOAD_BODY(xmBomber);
#undef EDF6_PRELOAD_BODY
static_assert(jet::kBodyCount==kBodyItems,"a body item per jet_internal.h body");
static_assert(kSupportWeaponCount==kSoldierItems,"a soldier item per support_call.h SupportWeapon");

SRWLOCK lock=SRWLOCK_INIT;
Set decided{};
bool hasDecision=false;
const char* decidedHow="";

const char* ModeName(Mode m) noexcept { return m==Mode::all ? "all (PreloadMode=1)" : m==Mode::none ? "none (PreloadMode=2)" : "needed"; }
bool Everything() noexcept {
    const Config& c=Cfg();
    if(c.debugSpawn)return true;
    __try {
        if(TestRangeMode())return true;
        pvp_protocol::State lockState{};
        return RoomPvpMatch(&lockState,nullptr);
    } __except(EXCEPTION_EXECUTE_HANDLER){return true;}
}
void Take(const Set& s,const char* how) noexcept {
    AcquireSRWLockExclusive(&lock);
    decided=s;hasDecision=true;decidedHow=how;
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace

Set LocalNeeds() noexcept {
    const Mode m=ModeOf(Cfg().preloadMode);
    if(m!=Mode::needed)return ByMode(m,Set{});
    if(Everything())return All();
    Set s=Always();
    SupportPreloadCatalog(SupportCfg(),s);
    AddEquippedCallItems(s);
    return s;
}
void DecideOffline() noexcept { Take(LocalNeeds(),"offline: this machine's"); }
void DecideOnline(const Decision& d) noexcept {
    Take(d.set,d.source==Source::host ? "online: the host's" : d.source==Source::local ? "online: this machine's (the host, or no room)" :
         "online: the host said none (nothing of the plugin's)");
}
void BeginMission() noexcept {
    AcquireSRWLockExclusive(&lock);
    const bool had=hasDecision;
    const Set taken=decided;
    const char* how=decidedHow;
    hasDecision=false;
    ReleaseSRWLockExclusive(&lock);
    const Set s=had ? taken : LocalNeeds();
    if(!had)how="no decision came: this machine's";
    detail::missionSet[0].store(s.w[0],std::memory_order_relaxed);
    detail::missionSet[1].store(s.w[1],std::memory_order_relaxed);
    char text[kTextSize]{};
    Format(s,text,sizeof(text));
    Log("PRELOAD this mission's set %s (%d items, %s; this machine's PreloadMode %s)",text,s.Count(),how,
        ModeName(ModeOf(Cfg().preloadMode)));
}
}  // namespace crew::preload

namespace crew {
void SupportPreloadCatalog(const SupportConfig& cfg,preload::Set& out) noexcept {
    using preload::Item;
    for(int i=0;i<SupportCallCount();++i) {
        SupportCatalogReach r;
        if(!cfg.Enabled(i) || !SupportCatalogReachOf(i,r))continue;
        if(r.sub){out.Add(Item::sub);out.Add(Item::laser);out.Add(preload::Body(preload::body::drone));}
        if(r.aircraft)AddSupportAircraftItems(r.spec,out);
        // A sortie's second wave is called with the first, whatever its own entry says (SupportCallUnpaidAt).
        const int next=i<SupportAirCallCount() ? SupportAirCallThen(i) : -1;
        if(SupportAircraft then;next>=0 && SupportAircraftSpec(next,&then))AddSupportAircraftItems(then,out);
        if(r.chute)out.Add(Item::chute);
        if(r.rope)out.Add(Item::rope);
        if(r.airdrop)out.Add(Item::airdrop);
        if(r.hulls)out.Add(Item::groundHulls);
        if(r.drill)out.Add(Item::drillTransport);
        if(r.ultimate)out.Add(Item::ultimate);
    }
}
}  // namespace crew
