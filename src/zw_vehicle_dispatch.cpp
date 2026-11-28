// The Vehicler's vehicles made and handed out (zw_vehicle_dispatch.h), as commander.cpp makes its NPC: the requester's
// machine asks (offline it plans itself), the host plans with the requester its authenticated transport says, every peer
// validates the same plan and makes the same hull by its NetId, the made record activates with the transaction.
//  - The players around a spot (they must not stand under a new vehicle): the native every-team walk 0x5E0C80(manager,
//    visitor), its entry checked (mission_participants.cpp's signature) and its rows (team_walk.h WalkRows); a player is a
//    player-class soldier with its user (+0x1ED0), not deleted from the scene (+0x18 bit 2), not dead.
//  - The ground under a footprint: support_dispatch.cpp's Foot (heli.h MapGroundNear standable, SeaAt no deeper than a
//    wade, MapRay headroom) at its centre and four corners, all within kLevelSpread of the centre.
#include "zw_vehicle_dispatch.h"
#include "zw_vehicle_dispatch_logic.h"
#include "command_identity.h"
#include "edf_layout.h"
#include "ground_navigation.h"
#include "heli.h"
#include "memory.h"
#include "netprobe.h"
#include "objref.h"
#include "plugin_log.h"
#include "pvp.h"
#include "scene.h"
#include "soldier_classes_logic.h"
#include "soldier_factory.h"
#include "soldier_identity.h"
#include "support_call.h"
#include "support_policy.h"
#include "support_soldier.h"
#include "support_spawn.h"
#include "team_walk.h"
#include <cmath>
#include <cstring>

namespace crew {
static_assert(kZwVehicleResource==kSupportVehicleResource+0x200u,"the Vehicler's plan resources sit at the vehicles' + 0x200");
static_assert(kZwVehicleResource+kZwVehicleMost<=kSupportVehicleResource+0x300u,"clear of anything above");
static_assert(zwdispatch::kZwStartCatalog<support_net::kMissionCrewCatalog && zwdispatch::kZwDeliverCatalog<support_net::kMissionCrewCatalog,
              "a support transaction catalog");
namespace {
constexpr unsigned kEnumAllTeams=0x5E0C80,kTeamManager=0x20B2978;
constexpr unsigned char kEnumSignature[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83};
constexpr std::size_t kUser=0x1ED0,kSceneFlags=0x18,kReadable=0x1EE0;
constexpr unsigned kMostMade=16,kMostPlanned=16,kMostPeople=32,kLocals=2;
constexpr float kRise=1.5f;         // m: the spot's ground no farther above / below the player's feet
constexpr float kLevelSpread=1.0f;  // m: the footprint's corners within this of its centre's ground
constexpr float kPlanReach=25.0f;   // m: a plan's vehicle no farther than this from its target (Validate; Place's farthest ~19 m)
constexpr std::uint64_t kOfflineFirst=0x5000000000000000ULL;   // offline transactions (commander's 0x4000..., the map's 1...)

using objref::Alive;
using zwplace::Footprint;

// Every machine: a hull made for a Vehicler.
struct Made {
    std::uint64_t transaction=0,serial=0;
    WeakRef vehicle{};
    unsigned char requester[32]{};   // online: the plan's requester (net_object_ref.h)
    WeakRef owner{};                 // offline: the human it was made for
    int kind=-1;
    bool own=false,active=false,remote=false;
};
Made made[kMostMade]{};
std::uint64_t serial=0;
// Host (offline this machine): the starts planned this mission, for the next one's placement and the one-a-player rule.
struct Planned {
    bool used=false;
    unsigned char requester[32]{};
    WeakRef human{};   // offline
    Footprint spot{};
};
Planned planned[kMostPlanned]{};
// This machine's own start requests (the split screen's two players offline).
struct Local {
    WeakRef human{};
    ZwStartState state=ZwStartState::none;
    std::uint32_t request=0;
    std::uint64_t transaction=0;
};
Local locals[kLocals]{};
bool submitting=false;
Local* submittingFor=nullptr;
std::uint64_t nextOffline=kOfflineFirst;
const unsigned char* offlineOwner=nullptr;   // set around an offline Apply
ZwDeliverLedger ledger{};

bool NoRequester(const unsigned char* id) noexcept {
    for(int i=0;i<32;++i)if(id[i])return false;
    return true;
}
Made* FindMade(std::uint64_t tx) noexcept {
    for(auto& m:made)if(m.transaction && m.transaction==tx)return &m;
    return nullptr;
}
Made* FreeMade() noexcept {for(auto& m:made)if(!m.transaction)return &m;return nullptr;}
Local* FindLocal(const unsigned char* human) noexcept {
    if(!human)return nullptr;
    for(auto& l:locals)if(l.human.Is(human))return &l;
    return nullptr;
}
Local* LocalFor(unsigned char* human) noexcept {
    if(Local* l=FindLocal(human))return l;
    for(auto& l:locals)if(!l.human || !objref::Exists(l.human)){l=Local{};l.human=ObjRef::Of(human);return &l;}
    return nullptr;
}
// The owner a made record's vehicle answers to now, live; nullptr when not (or no longer) found.
const unsigned char* OwnerOf(const Made& m) noexcept {
    if(!InSession())return Alive(m.owner) ? m.owner.Obj() : nullptr;
    if(NoRequester(m.requester))return nullptr;
    MarkIdentities ids{};
    if(!ResolveMarkIdentities(m.requester,nullptr,&ids) || !Alive(ids.player))return nullptr;
    return static_cast<const unsigned char*>(ids.player.obj);
}

// --- the players around ---
struct People {
    float at[kMostPeople][2]{};
    int count=0;
    bool failed=false;
};
struct Visitor { const void* const* vtable; People* people; };
void __fastcall Ignore(void*) noexcept {}
void __fastcall Visit(Visitor* v,const unsigned char* object) noexcept {
    People& p=*v->people;
    if(p.failed || !object)return;
    __try {
        if(!IsPlayerClassSoldier(object) || !Readable(object,kReadable) || (At<unsigned char>(object,kSceneFlags)&4))return;
        if(!At<const void*>(object,kUser) || object[kDead])return;
        const float* at=reinterpret_cast<const float*>(object+kPosition);
        if(!std::isfinite(at[0]+at[2]))return;
        if(p.count>=kMostPeople){p.failed=true;return;}
        p.at[p.count][0]=at[0];p.at[p.count][1]=at[2];++p.count;
    } __except(EXCEPTION_EXECUTE_HANDLER){p.failed=true;}
}
const void* const kVisitorVtable[]={reinterpret_cast<const void*>(&Ignore),reinterpret_cast<const void*>(&Visit)};
bool WalkPeople(People* out) noexcept {
    __try {
        if(!Matches(kEnumAllTeams,kEnumSignature,sizeof(kEnumSignature)))return false;
        void* manager=At<void*>(image,kTeamManager);
        if(!Readable(manager,0x50) || !team_walk::WalkRows(manager))return false;
        Visitor visitor{kVisitorVtable,out};
        reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kEnumAllTeams)(manager,&visitor);
        return !out->failed && At<void*>(image,kTeamManager)==manager;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// --- the ground ---
bool Foot(float x,float z,float level,float height,float& floor) noexcept {
    if(!MapGroundNear(x,z,level,&floor,true) || !std::isfinite(floor))return false;
    float surface=0.0f;const Sea sea=SeaAt(x,z,&surface);
    if(sea==Sea::unknown || (sea==Sea::water && (!std::isfinite(surface) || surface-floor>npc::navigation::Profile{}.maxWaterDepth)))return false;
    const float from[3]={x,floor+0.08f,z},to[3]={x,floor+height,z};float hit[3];
    return MapRay(from,to,hit)<0;
}
// Whether the ground under `f` takes a vehicle of `height`: standable, dry, level and open at its centre and corners, the
// centre within kRise of `level` (the player's feet). `centre` gets its centre's ground.
bool Clear(const Footprint& f,float level,float height,float* centre) noexcept {
    float floor=0.0f;
    if(!Foot(f.x,f.z,level,height,floor) || std::fabs(floor-level)>kRise)return false;
    const float rx=f.fz,rz=-f.fx;
    for(int i=0;i<4;++i) {
        const float along=(i&1 ? 1.0f : -1.0f)*f.halfLength,side=(i&2 ? 1.0f : -1.0f)*f.halfWidth;
        float corner=0.0f;
        if(!Foot(f.x+f.fx*along+rx*side,f.z+f.fz*along+rz*side,floor,height,corner) || std::fabs(corner-floor)>kLevelSpread)return false;
    }
    *centre=floor;return true;
}

// --- the plan ---
bool UnderGround() noexcept { return SupportMissionPolicy().underground; }
// A start vehicle of `kind` beside `soldier` (a live Vehicler), clear of what this mission planned and of every player.
// `why` gets the refusal.
bool BuildStart(const unsigned char* soldier,int kind,SupportPlan* out,Footprint* spot,const char** why) noexcept {
    const ZwVehicleSpec* v=ZwVehicleInfo(kind);
    if(!v || !ZwVehicleReady(kind)){*why="its kind is not installed / preloaded here";return false;}
    if(UnderGround()){*why="underground (no vehicle is made there)";return false;}
    if(!soldier || !Alive(ObjRef::Of(soldier)) || soldier::ZwType(soldier)!=zwclass::kVehicler){*why="the requester is not a live Vehicler";return false;}
    float at[3]{},fx=0,fz=1;
    __try {
        if(!Readable(soldier,kMatrix+64))return false;
        const float* m=reinterpret_cast<const float*>(soldier+kMatrix);
        fx=m[8];fz=m[10];
        std::memcpy(at,soldier+kPosition,sizeof(at));
    } __except(EXCEPTION_EXECUTE_HANDLER){*why="the requester is not readable";return false;}
    if(!std::isfinite(at[0]+at[1]+at[2]) || !zwplace::Unit(fx,fz)){*why="the requester's position / heading is not readable";return false;}
    Footprint taken[kMostPlanned]{};int takenCount=0;
    for(const auto& p:planned)if(p.used)taken[takenCount++]=p.spot;
    People people{};
    if(!WalkPeople(&people)){*why="the players around could not be read (the team walk)";return false;}
    float centre=at[1];
    const float level=at[1],height=v->height;
    const auto clear=[&](const Footprint& f) noexcept { return Clear(f,level,height,&centre); };
    Footprint f{};
    if(!zwplace::Place(at[0],at[2],fx,fz,v->halfWidth,v->halfLength,taken,takenCount,people.at,people.count,clear,&f)) {
        *why="no clear, level spot beside the requester (another vehicle, a player or the ground)";return false;
    }
    *out=SupportPlan{};
    out->catalogId=zwdispatch::kZwStartCatalog;out->count=1;
    std::memcpy(out->target,at,sizeof(at));
    auto& u=out->units[0];
    u.resourceId=ZwVehicleResource(kind);
    // Facing the requester's way: rows right, up, forward (the stock's), then the spot.
    u.matrix[0]=f.fz;u.matrix[2]=-f.fx;u.matrix[5]=1;u.matrix[8]=f.fx;u.matrix[10]=f.fz;u.matrix[15]=1;
    u.matrix[12]=f.x;u.matrix[13]=centre;u.matrix[14]=f.z;
    *spot=f;
    return true;
}
Planned* FreePlanned() noexcept {for(auto& p:planned)if(!p.used)return &p;return nullptr;}

bool Apply(std::uint64_t tx,const SupportPlan& plan,bool remote) noexcept {
    if(Made* m=FindMade(tx))return m->vehicle.obj!=nullptr;
    Made* m=FreeMade();
    if(!m){Log("ZW VEHICLE spawn %llu: no record free",static_cast<unsigned long long>(tx));return false;}
    const auto& u=plan.units[0];
    const int kind=ZwVehicleKindOfResource(u.resourceId);
    const bool networked=InSession();
    unsigned char* vehicle=SpawnZwVehicle(kind,u.matrix+12,u.matrix+8,nullptr);
    if(!vehicle){Log("ZW VEHICLE spawn %llu: kind %d not made",static_cast<unsigned long long>(tx),kind);return false;}
    const ObjRef object=ObjRef::Of(vehicle);
    if((networked && !RegisterSupportObject(vehicle,u.netId)) || !PvpAttachSupportPlan(plan,&object,1)) {
        Log("ZW VEHICLE spawn %llu: kind %d not registered (its NetId / its PvP owner): rolled back",static_cast<unsigned long long>(tx),kind);
        PvpForgetSupportObject(object);DeleteSupportVehicle(vehicle);return false;
    }
    *m=Made{};
    m->transaction=tx;m->serial=++serial;m->vehicle=object;m->kind=kind;m->remote=remote;
    std::memcpy(m->requester,plan.requester,sizeof(m->requester));
    if(!networked && offlineOwner)m->owner=ObjRef::Of(offlineOwner);
    m->own=!networked || SupportPlanRequesterIsLocal(plan);
    if(m->own && networked)
        if(Local* l=FindLocal(PlayerHuman()))if(l->state==ZwStartState::pending)l->transaction=tx;
    Log("ZW VEHICLE spawn %llu: %ls at (%.1f,%.1f,%.1f), %s%s",static_cast<unsigned long long>(tx),kZwVehicles[kind].row,
        u.matrix[12],u.matrix[13],u.matrix[14],networked ? "networked" : "offline",m->own ? " (this machine's player's)" : "");
    return true;
}
// Every machine, every frame: a made hull active once its transaction is (offline at once); its owner's request ready.
void Activate() noexcept {
    for(auto& m:made) {
        if(!m.transaction || m.active)continue;
        if(InSession() && !SupportTransactionActive(m.transaction))continue;
        m.active=true;
        Local* l=nullptr;
        if(!InSession())l=m.owner ? FindLocal(m.owner.Obj()) : nullptr;
        else if(m.own)l=FindLocal(PlayerHuman());
        if(l && l->state==ZwStartState::pending && (!InSession() || l->transaction==m.transaction))l->state=ZwStartState::ready;
        Log("ZW VEHICLE %llu active: %ls%s",static_cast<unsigned long long>(m.transaction),kZwVehicles[m.kind].row,
            Alive(m.vehicle) ? "" : " (already gone)");
    }
}
void Forget(const Made& m) noexcept {
    for(auto& p:planned) {
        if(!p.used)continue;
        const bool same=InSession() ? !NoRequester(m.requester) && !std::memcmp(p.requester,m.requester,sizeof(p.requester)) :
                                      m.owner && p.human.Is(m.owner.Obj());
        if(same){p=Planned{};break;}
    }
}
}  // namespace

bool RequestZwStart(unsigned char* human,int kind) noexcept {
    if(!human || !ZwVehicleInfo(kind))return false;
    Local* l=LocalFor(human);
    if(!l){Log("ZW START request refused: no local record free");return false;}
    if(l->state==ZwStartState::pending || l->state==ZwStartState::ready){Log("ZW START request refused: already asked this mission");return false;}
    if(!ZwVehicleReady(kind)){Log("ZW START request refused: %ls not ready here (not installed / not preloaded)",kZwVehicles[kind].row);return false;}
    if(UnderGround()){Log("ZW START request refused: underground");return false;}
    if(soldier::ZwType(human)!=zwclass::kVehicler){Log("ZW START request refused: not a Vehicler");return false;}
    if(!InSession()) {
        SupportPlan plan{};Footprint spot{};const char* why="";
        l->state=ZwStartState::pending;l->transaction=0;
        if(!BuildStart(human,kind,&plan,&spot,&why) || !ValidateZwVehiclePlan(plan)) {
            l->state=ZwStartState::refused;Log("ZW START (offline) refused: %s",why);return true;
        }
        const std::uint64_t tx=nextOffline++;
        offlineOwner=human;
        const bool applied=Apply(tx,plan,false);
        offlineOwner=nullptr;
        if(!applied){l->state=ZwStartState::refused;return true;}
        if(Planned* p=FreePlanned()){p->used=true;p->human=ObjRef::Of(human);p->spot=spot;}
        l->transaction=tx;
        Activate();
        return true;
    }
    if(human!=PlayerHuman()){Log("ZW START request refused: online only this machine's player asks");return false;}
    float at[3]{};
    __try {std::memcpy(at,human+kPosition,sizeof(at));} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    wchar_t note[128]{};
    l->state=ZwStartState::pending;l->request=0;l->transaction=0;
    submitting=true;submittingFor=l;
    const bool sent=SubmitSupportRequest(static_cast<int>(zwdispatch::kZwStartCatalog),at,note,_countof(note),static_cast<std::uint64_t>(kind));
    submitting=false;submittingFor=nullptr;
    if(!sent && l->state==ZwStartState::pending)l->state=ZwStartState::none;
    Log("ZW START request: %ls %s (request %u)",kZwVehicles[kind].row,sent ? "sent to the host" : "not sent",l->request);
    return sent;
}

ZwStartState ZwStartStatus(const unsigned char* human,unsigned char** vehicle) noexcept {
    if(vehicle)*vehicle=nullptr;
    const Local* l=FindLocal(human);
    if(!l)return ZwStartState::none;
    if(l->state!=ZwStartState::ready)return l->state;
    for(const auto& m:made)
        if(m.transaction && m.transaction==l->transaction) {
            if(!Alive(m.vehicle))break;
            if(vehicle)*vehicle=m.vehicle.Obj();
            return ZwStartState::ready;
        }
    return ZwStartState::ready;
}

const unsigned char* ZwVehicleOwner(const void* vehicle) noexcept {
    if(!vehicle)return nullptr;
    for(const auto& m:made)if(m.transaction && m.active && m.vehicle.Is(vehicle))return Alive(m.vehicle) ? OwnerOf(m) : nullptr;
    return nullptr;
}
int ZwVehicleKind(const void* vehicle) noexcept {
    if(!vehicle)return -1;
    for(const auto& m:made)if(m.transaction && m.active && m.vehicle.Is(vehicle))return Alive(m.vehicle) ? m.kind : -1;
    return -1;
}
unsigned char* ZwOwnedVehicle(const unsigned char* human) noexcept {
    if(!human)return nullptr;
    const Made* best=nullptr;
    for(const auto& m:made)
        if(m.transaction && m.active && Alive(m.vehicle) && OwnerOf(m)==human && (!best || m.serial>best->serial))best=&m;
    return best ? best->vehicle.Obj() : nullptr;
}

void SetZwDeliverLedger(const ZwDeliverLedger& l) noexcept { ledger=l; }
bool RequestZwDeliver(unsigned char* human,int kind,const float* around) noexcept {
    (void)human;(void)around;
    Log("ZW DELIVER request (%d) refused: the resupply is not in this build (the courier's route, arrival and drive-off)",kind);
    return false;
}
bool ZwDeliverPending(const unsigned char* human) noexcept { (void)human;return false; }

support_net::PlanResult PlanZwVehicle(std::uint32_t peer,std::uint32_t catalog,const float* target,std::uint64_t loadout,
                                      SupportPlan* out) noexcept {
    using support_net::PlanResult;
    (void)target;
    if(!out)return PlanResult::refused;
    if(catalog!=zwdispatch::kZwStartCatalog) {
        Log("ZW DELIVER plan for peer %u refused: the resupply is not in this build",peer);
        return PlanResult::refused;
    }
    if(loadout>=static_cast<std::uint64_t>(kZwVehicleCount)) {
        Log("ZW START plan for peer %u refused: no kind %llu",peer,static_cast<unsigned long long>(loadout));
        return PlanResult::refused;
    }
    ObjRef requester{};
    unsigned char id[32]{};
    if(!SupportAuthenticatedRequester(peer,&requester) || !Alive(requester) || !PlayerRequesterOf(requester,id)) {
        Log("ZW START plan for peer %u refused: its player is not found alive in the sealed world",peer);
        return PlanResult::refused;
    }
    for(const auto& p:planned)
        if(p.used && !std::memcmp(p.requester,id,sizeof(id))) {
            Log("ZW START plan for peer %u refused: its start vehicle was already planned this mission",peer);
            return PlanResult::refused;
        }
    Planned* slot=FreePlanned();
    if(!slot){Log("ZW START plan for peer %u refused: no planned record free",peer);return PlanResult::refused;}
    Footprint spot{};const char* why="";
    if(!BuildStart(static_cast<const unsigned char*>(requester.obj),static_cast<int>(loadout),out,&spot,&why)) {
        Log("ZW START plan for peer %u refused: %s",peer,why);
        return PlanResult::refused;
    }
    slot->used=true;std::memcpy(slot->requester,id,sizeof(id));slot->spot=spot;
    Log("ZW START plan for peer %u: %ls at (%.1f,%.1f,%.1f)",peer,kZwVehicles[loadout].row,out->units[0].matrix[12],
        out->units[0].matrix[13],out->units[0].matrix[14]);
    return PlanResult::ready;
}

bool ValidateZwVehiclePlan(const SupportPlan& plan) noexcept {
    if(plan.catalogId!=zwdispatch::kZwStartCatalog || plan.count!=1 || UnderGround())return false;
    const auto& u=plan.units[0];
    const int kind=ZwVehicleKindOfResource(u.resourceId);
    if(kind<0 || !ZwVehicleReady(kind) || u.role || u.variant || u.arms || u.offhand)return false;
    const float* m=u.matrix;
    for(int i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
    const float heading=m[8]*m[8]+m[10]*m[10];
    if(std::fabs(heading-1.0f)>1e-3f || m[9]!=0.0f || m[15]!=1.0f)return false;
    const float dx=m[12]-plan.target[0],dz=m[14]-plan.target[2];
    return std::isfinite(dx+dz) && dx*dx+dz*dz<=kPlanReach*kPlanReach;
}

bool ApplyZwVehiclePlan(std::uint64_t tx,const SupportPlan& plan,bool remote) noexcept {
    return tx && ValidateZwVehiclePlan(plan) && Apply(tx,plan,remote);
}

void DestroyZwVehiclePlan(std::uint64_t tx) noexcept {
    Made* m=FindMade(tx);
    if(!m)return;
    // Before activation the hull is still the transaction's (empty, nobody could board it): deleted. Once active it is the
    // world's (its owner may be aboard): kept, only its record dropped.
    if(!m->active && Alive(m->vehicle)) {
        PvpForgetSupportObject(m->vehicle);
        DeleteSupportVehicle(m->vehicle.Obj());
    }
    for(auto& l:locals)if(l.transaction==tx && l.state!=ZwStartState::none)l.state=ZwStartState::refused;
    Forget(*m);
    Log("ZW VEHICLE %llu rolled back",static_cast<unsigned long long>(tx));
    *m=Made{};
}

bool ZwVehicleRequestNotice(std::uint32_t req,support_net::RequestStatus status) noexcept {
    using support_net::RequestStatus;
    if(submitting && submittingFor && status==RequestStatus::accepted && req){submittingFor->request=req;return true;}
    if(!req)return false;
    for(auto& l:locals) {
        if(l.request!=req)continue;
        if(status>=RequestStatus::refused && l.state==ZwStartState::pending) {
            l.state=ZwStartState::refused;
            Log("ZW START request %u ended without a vehicle (status %u): the player walks in",req,static_cast<unsigned>(status));
        }
        return true;
    }
    return false;
}

void ZwVehicleTick() noexcept { Activate(); }

void ResetZwVehicles() noexcept {
    // Mission teardown owns its objects: borrowed references dropped, nothing deleted or written.
    for(auto& m:made)m=Made{};
    for(auto& p:planned)p=Planned{};
    for(auto& l:locals)l=Local{};
    submitting=false;submittingFor=nullptr;offlineOwner=nullptr;serial=0;
}
}  // namespace crew
