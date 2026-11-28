// The Vehicler's vehicles made and handed out (zw_vehicle_dispatch.h), as commander.cpp makes its NPC: the requester's
// machine asks (offline it plans itself), the host plans with the requester its authenticated transport says, every peer
// validates the same plan and makes the same hull by its NetId, the made record activates with the transaction.
//  - The players around a spot (they must not stand under a new vehicle): the native every-team walk 0x5E0C80(manager,
//    visitor), its entry checked (mission_participants.cpp's signature) and its rows (team_walk.h WalkRows); a player is a
//    player-class soldier with its user (+0x1ED0), not deleted from the scene (+0x18 bit 2), not dead.
//  - The ground under a footprint: support_dispatch.cpp's Foot (heli.h MapGroundNear standable, SeaAt no deeper than a
//    wade, MapRay headroom) at its centre and four corners, all within kLevelSpread of the centre.
//  - The resupply (kZwDeliverCatalog, zw_vehicle_dispatch.h): the price held at the first plan call, then a ground entry
//    proved (support_dispatch.cpp's admission: a corridor of 2 cells, GroundNavigate), the hold kept for the transaction
//    (Held: the session does not give the plan's transaction to the planner) and taken by the host's Apply, settled at
//    activation, refunded on a refusal, a rollback or a transaction that never came (kHoldMs). The courier's drive is the
//    host's (offline this machine's), as support_dispatch.cpp drives a ground call: NpcBoardCrew, GroundNavigate's
//    waypoints through NpcPrepareVehicleRoutePost, the stop and NpcReleaseVehicleCrew; the old vehicle the same way out.
#include "zw_vehicle_dispatch.h"
#include "zw_vehicle_dispatch_logic.h"
#include "command_identity.h"
#include "edf_layout.h"
#include "ground_navigation.h"
#include "heli.h"
#include "lazy_rows.h"
#include "memory.h"
#include "net_object_ref.h"
#include "netprobe.h"
#include "npcai.h"
#include "objref.h"
#include "online_authority.h"
#include "playarea.h"
#include "plugin_log.h"
#include "pvp.h"
#include "scene.h"
#include "soldier_classes_logic.h"
#include "soldier_factory.h"
#include "soldier_identity.h"
#include "support_call.h"
#include "support_entry.h"
#include "support_policy.h"
#include "support_soldier.h"
#include "support_spawn.h"
#include "team_walk.h"
#include "vehicler_logistics.h"
#include "view.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

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
// The resupply's drive (support_dispatch.cpp's ground call's numbers).
constexpr float kWaypointRadius=1.5f,kDriverHold=1.0f,kArrival=5.0f;
static_assert(kDriverHold<kWaypointRadius,"a route must advance before its driver stops");
constexpr ULONGLONG kStuckMs=20000;      // making no way this long: the courier gets off where it is
constexpr float kStuckMove=5.0f;         // m: moved this far from where its wait began, it is making way
constexpr ULONGLONG kBoardMs=45000;      // the courier aboard (the new vehicle, the old one) within this, else it walks
constexpr ULONGLONG kOldWaitMs=60000;    // the old vehicle's riders off it within this, else it is left (ownerless)
constexpr ULONGLONG kOutMs=180000;       // the old vehicle at its exit within this, else deleted where it stands
constexpr float kOutOfSight=300.0f;      // m: an old vehicle that cannot go on is deleted only this far from this machine's player
constexpr ULONGLONG kHoldMs=90000;       // a held price with no transaction made for it this long: given back
constexpr int kPlanCallsPerFrame=3;
constexpr float kCourierSide=1.5f;       // m: the courier beside the hull, past its half width
constexpr float kCourierReach=15.0f;     // Validate: the courier no farther than this from its hull
constexpr unsigned kMostHeld=8;

using objref::Alive;
using zwplace::Footprint;

// The courier's way (the host's, offline this machine's; a guest's records stay `follow`).
enum class Leg : std::uint8_t {
    follow,     // nothing to drive here (a start vehicle, a guest's copy, a delivery done)
    board,      // the courier walking to the new vehicle's driver seat
    drive,      // driving it to the parking spot
    waitOld,    // got off; the old vehicle has riders: waiting for it to empty
    boardOld,   // walking to the old vehicle
    driveOut,   // driving the old vehicle back to the entry
    retire,     // every machine: the courier and the old vehicle being deleted
};
// Every machine: a hull made for a Vehicler.
struct Made {
    std::uint64_t transaction=0,serial=0;
    WeakRef vehicle{};
    unsigned char requester[32]{};   // the plan's requester (net_object_ref.h; offline the local player's, a start's none)
    WeakRef owner{};                 // offline: the human it was made for
    int kind=-1;
    bool own=false,active=false,remote=false;
    // Its owner's from the courier's hand-over on (a start vehicle at once); no longer once a resupply replaced it.
    bool handed=true,replaced=false;
    // A resupply's: its courier, the NetIds every machine knows them by, the vehicle it replaces, the held price.
    bool deliver=false;
    WeakRef courier{};
    unsigned char hullNet[32]{},courierNet[32]{},oldNet[32]{};
    WeakRef old{};
    std::uint64_t oldTransaction=0;
    int oldKind=-1;
    std::uint64_t ticket=0;
    zwplace::Footprint park{};
    float parkAt[3]{};               // the plan's target: the parking spot's centre on its ground
    float exit[3]{};                 // the entry it came in at (the old vehicle goes out there)
    Leg leg=Leg::follow;
    ULONGLONG since=0,stuckSince=0,sampleAt=0;
    float stuckAt[3]{},sample[3]{};
    ULONGLONG retireFrame=0;
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
// This machine's own start and resupply requests (the split screen's two players offline).
struct Local {
    WeakRef human{};
    ZwStartState state=ZwStartState::none;
    std::uint32_t request=0;
    std::uint64_t transaction=0;
    // A resupply asked and not yet made (its record then says whether it is on its way).
    bool delivering=false;
    std::uint32_t deliverRequest=0;
    // Offline: planned here, frame by frame.
    bool planOffline=false;
    int deliverKind=-1;
    std::uint32_t unlocked=0;
};
Local locals[kLocals]{};
bool submitting=false,submittingDeliver=false;
Local* submittingFor=nullptr;
std::uint64_t nextOffline=kOfflineFirst;
const unsigned char* offlineOwner=nullptr;   // set around an offline Apply
ZwDeliverLedger ledger{};
// Host (offline this machine): the resupply being planned (one at a time: a plan holds a route state, ~150 KB) and the
// prices held for plans made, until the host's Apply takes them (or kHoldMs gives them back).
struct DeliverPlanning {
    bool active=false;
    unsigned char requester[32]{};
    int kind=-1;
    std::uint64_t ticket=0;
    ULONGLONG since=0;
    zwplace::Footprint park{};
    float level=0.0f;
    bool entriesMade=false;
    support::GroundEntries entries{};
    int edge=0;
    npc::navigation::RouteState navigation{};
};
LazyRows<DeliverPlanning,1> planningRow;
struct Held { bool used=false;unsigned char requester[32]{};std::uint64_t ticket=0;ULONGLONG since=0; };
Held held[kMostHeld]{};
// The couriers' routes, one a made record (the same index).
LazyRows<npc::navigation::RouteState,kMostMade> routes;
// Rows holding route states: `row={}` would build the value as a stack temporary first (support_dispatch.cpp's Renew).
template<class T>
void Renew(T& row) noexcept {
    static_assert(std::is_trivially_destructible_v<T>,"rows are plain data");
    ::new(static_cast<void*>(&row)) T{};
}

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
    std::memcpy(m->hullNet,u.netId,sizeof(m->hullNet));
    if(m->own && networked)
        if(Local* l=FindLocal(PlayerHuman()))if(l->state==ZwStartState::pending)l->transaction=tx;
    Log("ZW VEHICLE spawn %llu: %ls at (%.1f,%.1f,%.1f), %s%s",static_cast<unsigned long long>(tx),kZwVehicles[kind].row,
        u.matrix[12],u.matrix[13],u.matrix[14],networked ? "networked" : "offline",m->own ? " (this machine's player's)" : "");
    return true;
}
void ActivateDelivery(Made& m) noexcept;
// Every machine, every frame: a made hull active once its transaction is (offline at once); its owner's request ready.
void Activate() noexcept {
    for(auto& m:made) {
        if(!m.transaction || m.active)continue;
        if(InSession() && !SupportTransactionActive(m.transaction))continue;
        m.active=true;
        if(m.deliver){ActivateDelivery(m);continue;}
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

// ---- the resupply ----
bool SameRequester(const unsigned char* a,const unsigned char* b) noexcept { return !NoRequester(a) && !std::memcmp(a,b,32); }
float Flat(const float* a,const float* b) noexcept { return support::FlatDistance(a,b); }
void SetMatrix(float* m,const float* at,float fx,float fz) noexcept {
    for(int i=0;i<16;++i)m[i]=0.0f;
    m[0]=fz;m[2]=-fx;m[5]=1;m[8]=fx;m[10]=fz;m[15]=1;m[12]=at[0];m[13]=at[1];m[14]=at[2];
}
bool MatrixOk(const float* m) noexcept {
    for(int i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
    return std::fabs(m[8]*m[8]+m[10]*m[10]-1.0f)<=1e-3f && m[9]==0.0f && m[15]==1.0f;
}
npc::navigation::Profile RouteProfile(int kind) noexcept {
    npc::navigation::Profile profile;
    if(const ZwVehicleSpec* v=ZwVehicleInfo(kind)){profile.radius=std::hypot(v->halfWidth,v->halfLength);profile.height=v->height;}
    profile.cell=4;profile.waypointRadius=kWaypointRadius;profile.greed=npc::navigation::kRouteGreed;
    return profile;
}
// A live vehicle's footprint as it stands (its kind's envelope).
bool FootprintOf(const unsigned char* vehicle,int kind,Footprint* out) noexcept {
    const ZwVehicleSpec* v=ZwVehicleInfo(kind);
    if(!v || !vehicle)return false;
    __try {
        if(!Readable(vehicle,kMatrix+64))return false;
        const float* m=reinterpret_cast<const float*>(vehicle+kMatrix);
        float fx=m[8],fz=m[10];
        if(!zwplace::Unit(fx,fz) || !std::isfinite(m[12]+m[14]))return false;
        *out=Footprint{m[12],m[14],fx,fz,v->halfWidth,v->halfLength};
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Seated(unsigned char* vehicle,const ObjRef& soldier) noexcept {
    if(!vehicle || !soldier)return false;
    __try {
        for(unsigned seat=0;seat<SeatCount(vehicle);++seat)if(At<const void*>(SeatAt(vehicle,seat),kSeatRider)==soldier.obj)return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return false;
}
// Anyone in any seat of `vehicle` (a player, an NPC, a script's dummy).
bool Occupied(unsigned char* vehicle) noexcept {
    __try {
        for(unsigned seat=0;seat<SeatCount(vehicle);++seat)if(SeatRider(SeatAt(vehicle,seat))!=Rider::none)return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return true;}
    return false;
}
bool PlayerAboard(unsigned char* vehicle) noexcept {
    __try {
        for(unsigned seat=0;seat<SeatCount(vehicle);++seat)if(AnyPlayerIn(SeatAt(vehicle,seat)))return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return true;}
    return false;
}
// The courier put off `vehicle`.
void Unseat(unsigned char* vehicle,const ObjRef& soldier) noexcept {
    if(!vehicle || !soldier)return;
    __try {
        for(unsigned seat=0;seat<SeatCount(vehicle);++seat)
            if(At<const void*>(SeatAt(vehicle,seat),kSeatRider)==soldier.obj){NpcMoveSeat(vehicle,seat,-1);return;}
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
// Speed (m/s) from the last sample; huge for the first.
float SpeedOf(Made& m,const float* position,ULONGLONG now) noexcept {
    float speed=1e9f;
    if(m.sampleAt && now>m.sampleAt) {
        const float dt=std::fmin(static_cast<float>(now-m.sampleAt)*0.001f,1.0f);
        const float dx=position[0]-m.sample[0],dy=position[1]-m.sample[1],dz=position[2]-m.sample[2];
        speed=std::sqrt(dx*dx+dy*dy+dz*dz)/dt;
    }
    std::memcpy(m.sample,position,12);m.sampleAt=now;
    return speed;
}
// No way made for kStuckMs (the wait restarts whenever it moves kStuckMove).
bool Stuck(Made& m,const float* position,ULONGLONG now) noexcept {
    if(!m.stuckSince || Flat(position,m.stuckAt)>kStuckMove){m.stuckSince=now;std::memcpy(m.stuckAt,position,12);return false;}
    return now-m.stuckSince>kStuckMs;
}
std::size_t IndexOf(const Made& m) noexcept { return static_cast<std::size_t>(&m-made); }
Held* HeldOf(const unsigned char* requester) noexcept {
    for(auto& h:held)if(h.used && SameRequester(h.requester,requester))return &h;
    return nullptr;
}
void Refund(std::uint64_t ticket,const char* why) noexcept {
    if(!ticket)return;
    if(ledger.refund)ledger.refund(ticket);
    Log("ZW DELIVER price (ticket %llu) given back: %s",static_cast<unsigned long long>(ticket),why);
}
// The host's resupply of `requester` not yet done: planned, held for its transaction, made and not active, or its courier
// still driving it in / the old one off.
bool Busy(const unsigned char* requester) noexcept {
    if(planningRow.Allocated() && planningRow[0].active && SameRequester(planningRow[0].requester,requester))return true;
    if(HeldOf(requester))return true;
    for(const auto& m:made)
        if(m.transaction && m.deliver && SameRequester(m.requester,requester) && (!m.active || (!m.remote && m.leg!=Leg::follow)))return true;
    return false;
}
void EndPlanning(DeliverPlanning& p,const char* why) noexcept {
    Refund(p.ticket,why);
    p.active=false;p.ticket=0;
}
void RequesterFlat(const unsigned char* soldier,float* at,float* fx,float* fz,bool* ok) noexcept {
    *ok=false;
    __try {
        if(!Readable(soldier,kMatrix+64))return;
        const float* m=reinterpret_cast<const float*>(soldier+kMatrix);
        *fx=m[8];*fz=m[10];std::memcpy(at,soldier+kPosition,12);
        *ok=std::isfinite(at[0]+at[1]+at[2]) && zwplace::Unit(*fx,*fz);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

// Host (offline this machine): the plan of a resupply of `kind` for `soldier` (reference `id`), across frames.
support_net::PlanResult PlanDeliver(const unsigned char* soldier,const unsigned char* id,int kind,std::uint32_t unlocked,
                                    SupportPlan* out,const char* who) noexcept {
    using support_net::PlanResult;
    if(!planningRow.Allocate("ZW DELIVER planning"))return PlanResult::refused;
    DeliverPlanning& p=planningRow[0];
    const ULONGLONG now=GameMs();
    if(p.active && !SameRequester(p.requester,id)) {
        if(now-p.since<120000)return PlanResult::pending;   // another player's first (the session's planning deadline)
        EndPlanning(p,"its plan was left past the planning deadline");
    }
    if(p.active && p.kind!=kind)EndPlanning(p,"another kind asked");
    if(!p.active) {
        const ZwVehicleSpec* v=ZwVehicleInfo(kind);
        const char* why=nullptr;
        if(!v)why="no such kind";
        else if(kind>=32 || !(unlocked&(1u<<kind)))why="that kind is not unlocked by the requester";
        else if(!ZwVehicleReady(kind))why="its kind is not installed / preloaded here";
        else if(UnderGround())why="underground (no vehicle is driven in there)";
        else if(!ledger.hold || !ledger.settle || !ledger.refund)why="no logistics ledger set";
        else if(!soldier || !Alive(ObjRef::Of(soldier)) || soldier::ZwType(soldier)!=zwclass::kVehicler)why="the requester is not a live Vehicler";
        else if(Busy(id))why="a resupply of the requester's is already planned / on its way / driving the old one off";
        if(why){Log("ZW DELIVER plan (%s) refused: %s",who,why);return PlanResult::refused;}
        float at[3]{},fx=0,fz=1;bool ok=false;
        RequesterFlat(soldier,at,&fx,&fz,&ok);
        if(!ok){Log("ZW DELIVER plan (%s) refused: the requester's position / heading is not readable",who);return PlanResult::refused;}
        // The parking spot beside the requester as it stands now, clear of the vehicles of this module (planned, made, on
        // their way) and of every player.
        Footprint taken[kMostPlanned+kMostMade]{};int takenCount=0;
        for(const auto& pl:planned)if(pl.used)taken[takenCount++]=pl.spot;
        for(const auto& m:made) {
            if(!m.transaction || !Alive(m.vehicle))continue;
            if(m.deliver && !m.handed)taken[takenCount++]=m.park;
            else if(Footprint f{};FootprintOf(m.vehicle.Obj(),m.kind,&f))taken[takenCount++]=f;
        }
        People people{};
        if(!WalkPeople(&people)){Log("ZW DELIVER plan (%s) refused: the players around could not be read (the team walk)",who);return PlanResult::refused;}
        float centre=at[1];
        const float level=at[1],height=v->height;
        const auto clear=[&](const Footprint& f) noexcept { return Clear(f,level,height,&centre); };
        Footprint park{};
        if(!zwplace::Place(at[0],at[2],fx,fz,v->halfWidth,v->halfLength,taken,takenCount,people.at,people.count,clear,&park)) {
            Log("ZW DELIVER plan (%s) refused: no clear, level spot beside the requester",who);return PlanResult::refused;
        }
        std::uint64_t ticket=0;
        if(!ledger.hold(id,ZwVehiclePrice(kind),&ticket) || !ticket) {
            Log("ZW DELIVER plan (%s) refused: %u logistics points not held (too few, or a call held already)",who,ZwVehiclePrice(kind));
            return PlanResult::refused;
        }
        Renew(p);
        p.active=true;std::memcpy(p.requester,id,32);p.kind=kind;p.ticket=ticket;p.since=now;p.park=park;p.level=centre;
        Log("ZW DELIVER plan (%s): %ls for %u points (ticket %llu), parking at (%.1f,%.1f,%.1f)",who,v->row,ZwVehiclePrice(kind),
            static_cast<unsigned long long>(ticket),park.x,centre,park.z);
    }
    const ZwVehicleSpec* v=ZwVehicleInfo(p.kind);
    const float target[3]={p.park.x,p.level,p.park.z};
    if(!p.entriesMade) {
        p.entries=support::GroundEntryCandidates(MapPlayArea(),target,target,NearDrawDistance(),[](const float*) noexcept {return false;});
        p.entriesMade=true;p.edge=0;
        Log("ZW DELIVER plan (%s): %d ground entry candidates round the parking spot",who,p.entries.count);
    }
    for(;;) {
        if(p.edge>=p.entries.count) {
            Log("ZW DELIVER plan (%s) refused: no entry connected to the parking spot by a verified route",who);
            EndPlanning(p,"no entry");return PlanResult::refused;
        }
        float entry[3]={p.entries.at[p.edge][0],target[1],p.entries.at[p.edge][1]};
        const auto next=[&](const char* why) noexcept {
            Log("ZW DELIVER plan (%s) entry %d (%.0f,%.0f) rejected: %s",who,p.edge,entry[0],entry[2],why);
            ++p.edge;Renew(p.navigation);
        };
        if(!Foot(entry[0],entry[2],target[1],v->height,entry[1])){next("no dry standable ground");continue;}
        auto profile=RouteProfile(p.kind);
        profile.horizon=2.0f*profile.cell;   // a safe first corridor, as support_dispatch.cpp admits a ground call
        float waypoint[3];
        auto path=npc::navigation::Result::pending;
        for(int call=0;call<kPlanCallsPerFrame && path==npc::navigation::Result::pending;++call)
            path=GroundNavigate(p.navigation,entry,target,2.0f,GameMs(),waypoint,profile);
        if(path==npc::navigation::Result::pending)return PlanResult::pending;
        if(path==npc::navigation::Result::blocked){next("no route to the parking spot");continue;}
        const float distance=Flat(entry,target);
        if(!(distance>1.0f)){next("on the parking spot");continue;}
        const float hx=(target[0]-entry[0])/distance,hz=(target[2]-entry[2])/distance;
        // The courier beside the hull, on its right, else its left.
        float side[3]{};bool placed=false;
        for(int s=0;s<2 && !placed;++s) {
            const float off=(v->halfWidth+kCourierSide)*(s ? -1.0f : 1.0f);
            side[0]=entry[0]+hz*off;side[2]=entry[2]-hx*off;
            float floor=0.0f;
            placed=Foot(side[0],side[2],entry[1],1.8f,floor) && std::fabs(floor-entry[1])<=kRise;
            side[1]=floor;
        }
        if(!placed){next("no ground for the courier beside the hull");continue;}
        Held* h=nullptr;
        for(auto& e:held)if(!e.used){h=&e;break;}
        if(!h){Log("ZW DELIVER plan (%s) refused: no held record free",who);EndPlanning(p,"no held record");return PlanResult::refused;}
        *out=SupportPlan{};
        out->catalogId=zwdispatch::kZwDeliverCatalog;out->count=2;
        std::memcpy(out->target,target,sizeof(target));
        std::memcpy(out->requester,id,32);
        out->units[0].resourceId=ZwVehicleResource(p.kind);
        const float hullAt[3]={entry[0],entry[1]+0.1f,entry[2]};
        SetMatrix(out->units[0].matrix,hullAt,hx,hz);
        out->units[1].resourceId=kSupportRangerResource;out->units[1].role=1;
        SetMatrix(out->units[1].matrix,side,hx,hz);
        // The price stays held for the transaction the session makes of this plan (the host's Apply takes it).
        *h=Held{};h->used=true;std::memcpy(h->requester,id,32);h->ticket=p.ticket;h->since=now;
        Log("ZW DELIVER plan (%s) ready: entry (%.0f,%.0f,%.0f), %.0f m to the parking spot",who,entry[0],entry[1],entry[2],distance);
        p.active=false;p.ticket=0;
        return PlanResult::ready;
    }
}

bool ValidDeliver(const SupportPlan& plan) noexcept {
    if(plan.count!=2 || UnderGround())return false;
    const auto& h=plan.units[0];const auto& c=plan.units[1];
    const int kind=ZwVehicleKindOfResource(h.resourceId);
    if(kind<0 || !ZwVehicleReady(kind) || h.role || h.variant || h.arms || h.offhand)return false;
    if(c.resourceId!=kSupportRangerResource || c.role!=1 || c.variant)return false;
    if(!MatrixOk(h.matrix) || !MatrixOk(c.matrix) || !std::isfinite(plan.target[0]+plan.target[1]+plan.target[2]))return false;
    if(Flat(h.matrix+12,plan.target)<support::kGroundMinJourney-1.0f)return false;   // driven in, never made on the spot
    return Flat(c.matrix+12,h.matrix+12)<=kCourierReach;
}

// Every machine: the resupply's hull and courier made (their NetIds registered online), the vehicle it replaces found.
bool ApplyDeliver(std::uint64_t tx,const SupportPlan& plan,bool remote) noexcept {
    if(Made* m=FindMade(tx))return m->vehicle.obj!=nullptr;
    Made* m=FreeMade();
    if(!m){Log("ZW DELIVER spawn %llu: no record free",static_cast<unsigned long long>(tx));return false;}
    Held* h=nullptr;
    if(!remote && !(h=HeldOf(plan.requester))) {
        Log("ZW DELIVER spawn %llu: no price held for it (given back after %llu s): rolled back",static_cast<unsigned long long>(tx),
            static_cast<unsigned long long>(kHoldMs/1000));
        return false;
    }
    const auto& hu=plan.units[0];const auto& cu=plan.units[1];
    const int kind=ZwVehicleKindOfResource(hu.resourceId);
    const bool networked=InSession();
    unsigned char* vehicle=SpawnZwVehicle(kind,hu.matrix+12,hu.matrix+8,nullptr);
    if(!vehicle){Log("ZW DELIVER spawn %llu: kind %d not made",static_cast<unsigned long long>(tx),kind);return false;}
    ObjRef objects[2]={ObjRef::Of(vehicle),ObjRef{}};
    if(networked && !RegisterSupportObject(vehicle,hu.netId)) {
        Log("ZW DELIVER spawn %llu: its hull's NetId not registered: rolled back",static_cast<unsigned long long>(tx));
        DeleteSupportVehicle(vehicle);return false;
    }
    if(!ApplySupportSoldierResource(cu.matrix,cu.resourceId,networked ? cu.netId : nullptr,!networked,&objects[1],nullptr,cu.arms,cu.offhand) ||
       !HoldSupportSoldier(objects[1],true)) {
        Log("ZW DELIVER spawn %llu: its courier not made: rolled back",static_cast<unsigned long long>(tx));
        if(objects[1].obj)DeleteSupportSoldier(objects[1]);
        DeleteSupportVehicle(vehicle);return false;
    }
    if(!PvpAttachSupportPlan(plan,objects,2)) {
        Log("ZW DELIVER spawn %llu: not attached to its PvP owner: rolled back",static_cast<unsigned long long>(tx));
        PvpForgetSupportObject(objects[0]);PvpForgetSupportObject(objects[1]);
        DeleteSupportSoldier(objects[1]);DeleteSupportVehicle(vehicle);return false;
    }
    *m=Made{};
    m->transaction=tx;m->serial=++serial;m->vehicle=objects[0];m->kind=kind;m->remote=remote;
    std::memcpy(m->requester,plan.requester,sizeof(m->requester));
    if(!networked && offlineOwner)m->owner=ObjRef::Of(offlineOwner);
    m->own=!networked || SupportPlanRequesterIsLocal(plan);
    m->deliver=true;m->handed=false;m->courier=objects[1];
    std::memcpy(m->hullNet,hu.netId,32);std::memcpy(m->courierNet,cu.netId,32);
    std::memcpy(m->exit,hu.matrix+12,12);std::memcpy(m->parkAt,plan.target,12);
    if(const ZwVehicleSpec* v=ZwVehicleInfo(kind))
        m->park=Footprint{plan.target[0],plan.target[2],hu.matrix[8],hu.matrix[10],v->halfWidth,v->halfLength};
    if(h){m->ticket=h->ticket;*h=Held{};}
    // The vehicle it replaces: the owner's current one (the last handed to it), ownerless from now on.
    Made* old=nullptr;
    for(auto& o:made) {
        if(&o==m || !o.transaction || !o.active || !o.handed || o.replaced)continue;
        const bool same=networked ? SameRequester(o.requester,m->requester) : m->owner && o.owner.Is(m->owner.Obj());
        if(same && (!old || o.serial>old->serial))old=&o;
    }
    const bool oldAlive=old && Alive(old->vehicle);
    if(old) {
        old->replaced=true;
        m->old=old->vehicle;m->oldTransaction=old->transaction;m->oldKind=old->kind;
        std::memcpy(m->oldNet,old->hullNet,32);
    }
    // This machine's player's request is made: its record says from here on whether it is on its way.
    if(m->own)
        for(auto& l:locals)
            if(l.delivering && (networked ? l.human.Is(PlayerHuman()) : m->owner && l.human.Is(m->owner.Obj()))){l.delivering=false;break;}
    Log("ZW DELIVER spawn %llu: %ls at the entry (%.1f,%.1f,%.1f) with its courier, %s%s; it replaces %s",static_cast<unsigned long long>(tx),
        kZwVehicles[kind].row,hu.matrix[12],hu.matrix[13],hu.matrix[14],networked ? "networked" : "offline",
        m->own ? " (this machine's player's)" : "",old ? (oldAlive ? "the owner's vehicle" : "a wrecked vehicle") : "none");
    return true;
}

// Every machine: a resupply's transaction active. The replaced vehicle's record dropped (ownerless; the delivery keeps its
// reference), the courier free to move; the host's (offline this machine's) price spent and the courier sent aboard.
void ActivateDelivery(Made& m) noexcept {
    if(Made* o=FindMade(m.oldTransaction))if(o!=&m && o->replaced)*o=Made{};
    if(Alive(m.courier))HoldSupportSoldier(m.courier,false);
    Log("ZW DELIVER %llu active: %ls%s",static_cast<unsigned long long>(m.transaction),kZwVehicles[m.kind].row,
        Alive(m.vehicle) ? "" : " (already gone)");
    if(m.remote)return;
    if(m.ticket){ledger.settle(m.ticket);m.ticket=0;}
    m.since=GameMs();m.stuckSince=0;m.sampleAt=0;
    unsigned char* courier[1]={m.courier.Obj()};
    if(!routes.Allocate("ZW DELIVER routes") || !Alive(m.vehicle) || !Alive(m.courier) || NpcBoardCrew(m.vehicle.Obj(),courier,1)!=1) {
        Log("ZW DELIVER %llu: its courier not sent aboard (no route row, hull / courier gone, or refused): handed over at the entry",
            static_cast<unsigned long long>(m.transaction));
        m.handed=true;m.leg=Leg::follow;
        if(InSession())VehiclerSendDelivery(false,m.hullNet,m.courierNet,false);
        return;
    }
    Renew(routes[IndexOf(m)]);
    m.leg=Leg::board;
}

bool Follow(Made& m) noexcept {
    const unsigned char* owner=OwnerOf(m);
    return Alive(m.courier) && owner && FollowSupportSoldier(m.courier,ObjRef::Of(owner));
}
// The courier's hand-over: the vehicle its owner's; the courier off to the old vehicle (alive) or following the owner.
void Handover(Made& m) noexcept {
    m.handed=true;
    if(Alive(m.old) && Alive(m.courier)) {
        Log("ZW DELIVER %llu: handed over; the courier goes for the old vehicle",static_cast<unsigned long long>(m.transaction));
        m.leg=Leg::waitOld;m.since=GameMs();
        if(InSession())VehiclerSendDelivery(false,m.hullNet,m.courierNet,false);
        return;
    }
    const bool follows=Follow(m);
    Log("ZW DELIVER %llu: handed over; %s; the courier %s",static_cast<unsigned long long>(m.transaction),
        m.old ? "the old vehicle is wrecked (left to the game)" : "no old vehicle",follows ? "follows the owner" : "stands where it is");
    m.leg=Leg::follow;m.old=WeakRef{};
    if(InSession())VehiclerSendDelivery(false,m.hullNet,m.courierNet,follows);
}
// The courier gives up on the old vehicle (left ownerless where it is) and follows the owner.
void LeaveOld(Made& m,const char* why) noexcept {
    if(Alive(m.old))Unseat(m.old.Obj(),m.courier);
    const bool follows=Follow(m);
    Log("ZW DELIVER %llu: the old vehicle left where it is (%s); the courier %s",static_cast<unsigned long long>(m.transaction),why,
        follows ? "follows the owner" : "stands where it is");
    m.leg=Leg::follow;m.old=WeakRef{};
    if(InSession())VehiclerSendDelivery(false,m.hullNet,m.courierNet,follows);
}
// Drive `vehicle` toward `to` one frame; true once there (arrived, or stuck: `stuck` set). `stopped`: at rest now.
bool DriveStep(Made& m,unsigned char* vehicle,int kind,const float* to,bool* stuck,bool* stopped) noexcept {
    const float* position=reinterpret_cast<const float*>(vehicle+kPosition);
    const ULONGLONG now=GameMs();
    *stopped=SpeedOf(m,position,now)<=0.5f;
    float waypoint[3];
    const auto path=GroundNavigate(routes[IndexOf(m)],position,to,kArrival,now,waypoint,RouteProfile(kind));
    *stuck=path!=npc::navigation::Result::arrived && Stuck(m,position,now);
    if(path==npc::navigation::Result::moving && !*stuck){NpcPrepareVehicleRoutePost(vehicle,waypoint,kDriverHold);return false;}
    NpcPrepareVehicleRoutePost(vehicle,position,kDriverHold);   // hold (a pending route waits; never through the obstacle)
    return path==npc::navigation::Result::arrived || *stuck;
}
void StartRetire(Made& m) noexcept {
    m.leg=Leg::retire;m.retireFrame=GameFrame()+1;
    Log("ZW DELIVER %llu: the old vehicle and its courier being deleted",static_cast<unsigned long long>(m.transaction));
}
// Every machine: the courier off the old vehicle and both deleted, over frames as support_dispatch.cpp withdraws a ground
// vehicle (a vehicle with riders is not deleted: anyone of ours put off after 30 frames, given up after 121).
void RetireStep(Made& m) noexcept {
    unsigned char* old=objref::Exists(m.old) ? m.old.Obj() : nullptr;
    const ULONGLONG since=GameFrame()+1-m.retireFrame;
    if(m.courier && objref::Exists(m.courier)) {
        if(old && Seated(old,m.courier))Unseat(old,m.courier);
        if(!old || !Seated(old,m.courier) || since>60){PvpForgetSupportObject(m.courier);DeleteSupportSoldier(m.courier);m.courier=WeakRef{};}
    }
    bool done=!old;
    if(old && !PlayerAboard(old)) {
        if(since>30)for(unsigned seat=0;seat<SeatCount(old);++seat)if(SeatRider(SeatAt(old,seat))==Rider::other)NpcMoveSeat(old,seat,-1);
        if(!Occupied(old)){PvpForgetSupportObject(m.old);done=DeleteSupportVehicle(old);}
    }
    if(!done && since<=121)return;
    Log("ZW DELIVER %llu: the old vehicle %s",static_cast<unsigned long long>(m.transaction),
        done ? "deleted at the field's edge" : "still has riders: left where it is");
    m.old=WeakRef{};m.leg=Leg::follow;
}
// The host's (offline this machine's) couriers, one frame; every machine's retirements.
void Couriers() noexcept {
    for(auto& m:made) {
        if(!m.transaction || !m.deliver || !m.active)continue;
        if(m.leg==Leg::retire){RetireStep(m);continue;}
        if(m.remote || m.leg==Leg::follow || !routes.Allocated())continue;
        const ULONGLONG now=GameMs();
        unsigned char* hull=Alive(m.vehicle) ? m.vehicle.Obj() : nullptr;
        if(m.leg==Leg::board || m.leg==Leg::drive) {
            if(!hull || !Alive(m.courier)) {
                Log("ZW DELIVER %llu: the %s lost on the way",static_cast<unsigned long long>(m.transaction),hull ? "courier" : "vehicle");
                Handover(m);continue;
            }
            if(m.leg==Leg::board) {
                if(Seated(hull,m.courier)){m.leg=Leg::drive;m.stuckSince=0;m.sampleAt=0;Renew(routes[IndexOf(m)]);continue;}
                if(now-m.since>kBoardMs) {
                    Log("ZW DELIVER %llu: the courier not aboard in %llu s: handed over at the entry",
                        static_cast<unsigned long long>(m.transaction),static_cast<unsigned long long>(kBoardMs/1000));
                    Handover(m);
                }
                continue;
            }
            if(PlayerAboard(hull)){Log("ZW DELIVER %llu: a player boarded it on the way",static_cast<unsigned long long>(m.transaction));Handover(m);continue;}
            bool stuck=false,stopped=false;
            if(!DriveStep(m,hull,m.kind,m.parkAt,&stuck,&stopped))continue;
            if(!(stopped || stuck) || !NpcReleaseVehicleCrew(hull))continue;
            Log("ZW DELIVER %llu: %s, %.0f m from the parking spot",static_cast<unsigned long long>(m.transaction),
                stuck ? "made no way: delivered where it stands" : "arrived",Flat(reinterpret_cast<const float*>(hull+kPosition),m.parkAt));
            Handover(m);continue;
        }
        if(!Alive(m.courier)){Log("ZW DELIVER %llu: the courier lost: the old vehicle left",static_cast<unsigned long long>(m.transaction));m.leg=Leg::follow;m.old=WeakRef{};continue;}
        unsigned char* old=Alive(m.old) ? m.old.Obj() : nullptr;
        if(!old){LeaveOld(m,"wrecked meanwhile");continue;}
        if(m.leg==Leg::waitOld) {
            if(Seated(hull,m.courier))continue;   // still getting off the new one
            if(Occupied(old)) {
                if(now-m.since>kOldWaitMs)LeaveOld(m,"its riders did not get off");
                continue;
            }
            unsigned char* courier[1]={m.courier.Obj()};
            if(NpcBoardCrew(old,courier,1)!=1){LeaveOld(m,"the courier could not be sent aboard");continue;}
            m.leg=Leg::boardOld;m.since=now;
            continue;
        }
        if(PlayerAboard(old)){LeaveOld(m,"a player took it");continue;}
        if(m.leg==Leg::boardOld) {
            if(Seated(old,m.courier)){m.leg=Leg::driveOut;m.since=now;m.stuckSince=0;m.sampleAt=0;Renew(routes[IndexOf(m)]);continue;}
            if(now-m.since>kBoardMs)LeaveOld(m,"the courier not aboard in time");
            continue;
        }
        // driveOut
        bool stuck=false,stopped=false;
        const bool there=DriveStep(m,old,m.oldKind,m.exit,&stuck,&stopped);
        const unsigned char* me=PlayerHuman();
        const bool unseen=!me || Flat(reinterpret_cast<const float*>(old+kPosition),reinterpret_cast<const float*>(me+kPosition))>kOutOfSight;
        if((there && (!stuck || unseen)) || now-m.since>kOutMs) {
            StartRetire(m);
            if(InSession())VehiclerSendDelivery(true,m.oldNet,m.courierNet,false);
        }
    }
}
// Host: prices held for a plan whose transaction never came.
void ExpireHolds() noexcept {
    const ULONGLONG now=GameMs();
    for(auto& h:held)if(h.used && now-h.since>kHoldMs){Refund(h.ticket,"no transaction was made of its plan");h=Held{};}
}
// Offline: this machine's resupply requests planned here, made and activated at once.
void OfflineDeliveries() noexcept {
    if(InSession())return;
    for(auto& l:locals) {
        if(!l.planOffline)continue;
        unsigned char* human=Alive(l.human) ? l.human.Obj() : nullptr;
        unsigned char id[32]{};
        if(!human || !ZwRequesterRef(human,id)){l.planOffline=false;l.delivering=false;continue;}
        SupportPlan plan{};
        const auto r=PlanDeliver(human,id,l.deliverKind,l.unlocked,&plan,"offline");
        if(r==support_net::PlanResult::pending)continue;
        l.planOffline=false;
        if(r==support_net::PlanResult::refused){l.delivering=false;continue;}
        const std::uint64_t tx=nextOffline++;
        offlineOwner=human;
        const bool applied=ValidDeliver(plan) && ApplyDeliver(tx,plan,false);
        offlineOwner=nullptr;
        if(!applied) {
            if(Held* h=HeldOf(id)){Refund(h->ticket,"not made");*h=Held{};}
            l.delivering=false;continue;
        }
        Activate();
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
    for(const auto& m:made)
        if(m.transaction && m.active && m.vehicle.Is(vehicle))return Alive(m.vehicle) && m.handed && !m.replaced ? OwnerOf(m) : nullptr;
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
        if(m.transaction && m.active && m.handed && !m.replaced && Alive(m.vehicle) && OwnerOf(m)==human && (!best || m.serial>best->serial))best=&m;
    return best ? best->vehicle.Obj() : nullptr;
}

void SetZwDeliverLedger(const ZwDeliverLedger& l) noexcept { ledger=l; }

bool ZwRequesterRef(const unsigned char* human,unsigned char out[32]) noexcept {
    if(!human)return false;
    if(InSession())return PlayerRequesterOf(static_cast<const void*>(human),out);
    const unsigned char* me=PlayerHuman();
    return me && netref::MakePlayer(human==me ? 0 : 1,out);
}

bool RequestZwDeliver(unsigned char* human,int kind,const float* around) noexcept {
    if(!human || !ZwVehicleInfo(kind) || !around)return false;
    Local* l=LocalFor(human);
    if(!l){Log("ZW DELIVER request refused: no local record free");return false;}
    if(ZwDeliverPending(human)){Log("ZW DELIVER request refused: one is on its way already");return false;}
    if(soldier::ZwType(human)!=zwclass::kVehicler){Log("ZW DELIVER request refused: not a Vehicler");return false;}
    if(UnderGround()){Log("ZW DELIVER request refused: underground (no vehicle is driven in there)");return false;}
    const int slot=human==PlayerHuman() ? 0 : 1;
    const std::uint32_t unlocked=ZwUnlockedKinds(slot);
    if(kind>=32 || !(unlocked&(1u<<kind))){Log("ZW DELIVER request refused: %ls not unlocked",kZwVehicles[kind].row);return false;}
    if(!ZwVehicleReady(kind)){Log("ZW DELIVER request refused: %ls not ready here (not installed / not preloaded)",kZwVehicles[kind].row);return false;}
    if(!InSession()) {
        l->delivering=true;l->planOffline=true;l->deliverKind=kind;l->unlocked=unlocked;l->deliverRequest=0;
        Log("ZW DELIVER request (offline): %ls, planning",kZwVehicles[kind].row);
        return true;
    }
    if(human!=PlayerHuman()){Log("ZW DELIVER request refused: online only this machine's player asks");return false;}
    wchar_t note[128]{};
    l->delivering=true;l->deliverRequest=0;
    submittingDeliver=true;submittingFor=l;
    const bool sent=SubmitSupportRequest(static_cast<int>(zwdispatch::kZwDeliverCatalog),around,note,_countof(note),
                                         ZwDeliverLoadout(kind,unlocked));
    submittingDeliver=false;submittingFor=nullptr;
    if(!sent)l->delivering=false;
    Log("ZW DELIVER request: %ls %s (request %u)",kZwVehicles[kind].row,sent ? "sent to the host" : "not sent",l->deliverRequest);
    return sent;
}

unsigned char* ZwDeliverVehicle(const unsigned char* human) noexcept {
    if(!human)return nullptr;
    for(const auto& m:made) {
        if(!m.transaction || !m.deliver || m.handed || !Alive(m.vehicle))continue;
        if(InSession() ? m.own && human==PlayerHuman() : m.owner.Is(human))return m.vehicle.Obj();
    }
    return nullptr;
}

bool ZwDeliverPending(const unsigned char* human) noexcept {
    if(!human)return false;
    if(const Local* l=FindLocal(human))if(l->delivering)return true;
    for(const auto& m:made) {
        if(!m.transaction || !m.deliver || m.handed)continue;
        if(InSession() ? m.own && human==PlayerHuman() : m.owner.Is(human))return true;
    }
    return false;
}

bool ZwDeliverInTransit(const unsigned char requester[32]) noexcept { return requester && Busy(requester); }

void ZwDeliverHeard(bool retired,const unsigned char hull[32],const unsigned char courier[32],bool follows) noexcept {
    if(!hull || !courier || !InSession() || OnlineHostOnly())return;   // a guest's
    for(auto& m:made) {
        if(!m.transaction || !m.deliver || !m.remote || std::memcmp(m.courierNet,courier,32))continue;
        if(!retired) {
            if(std::memcmp(m.hullNet,hull,32))continue;
            m.handed=true;
            const bool followed=follows && Follow(m);
            Log("ZW DELIVER %llu: the host handed it over%s",static_cast<unsigned long long>(m.transaction),
                followed ? "; the courier follows the owner" : "");
            return;
        }
        if(std::memcmp(m.oldNet,hull,32))continue;
        if(m.leg!=Leg::retire)StartRetire(m);
        return;
    }
    Log("ZW DELIVER: the host's word of a delivery (%s) matches no record here",retired ? "retired" : "handed");
}

support_net::PlanResult PlanZwVehicle(std::uint32_t peer,std::uint32_t catalog,const float* target,std::uint64_t loadout,
                                      SupportPlan* out) noexcept {
    using support_net::PlanResult;
    (void)target;
    if(!out)return PlanResult::refused;
    ObjRef requester{};
    unsigned char id[32]{};
    const char* what=catalog==zwdispatch::kZwDeliverCatalog ? "DELIVER" : "START";
    if(!SupportAuthenticatedRequester(peer,&requester) || !Alive(requester) || !PlayerRequesterOf(requester,id)) {
        Log("ZW %s plan for peer %u refused: its player is not found alive in the sealed world",what,peer);
        return PlanResult::refused;
    }
    if(catalog==zwdispatch::kZwDeliverCatalog) {
        char who[24];
        std::snprintf(who,sizeof(who),"peer %u",peer);
        return PlanDeliver(static_cast<const unsigned char*>(requester.obj),id,static_cast<int>(loadout&0xFFu),
                           static_cast<std::uint32_t>(loadout>>8),out,who);
    }
    if(loadout>=static_cast<std::uint64_t>(kZwVehicleCount)) {
        Log("ZW START plan for peer %u refused: no kind %llu",peer,static_cast<unsigned long long>(loadout));
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
    if(plan.catalogId==zwdispatch::kZwDeliverCatalog)return ValidDeliver(plan);
    if(plan.catalogId!=zwdispatch::kZwStartCatalog || plan.count!=1 || UnderGround())return false;
    const auto& u=plan.units[0];
    const int kind=ZwVehicleKindOfResource(u.resourceId);
    if(kind<0 || !ZwVehicleReady(kind) || u.role || u.variant || u.arms || u.offhand || !MatrixOk(u.matrix))return false;
    const float dx=u.matrix[12]-plan.target[0],dz=u.matrix[14]-plan.target[2];
    return std::isfinite(dx+dz) && dx*dx+dz*dz<=kPlanReach*kPlanReach;
}

bool ApplyZwVehiclePlan(std::uint64_t tx,const SupportPlan& plan,bool remote) noexcept {
    if(!tx || !ValidateZwVehiclePlan(plan))return false;
    return plan.catalogId==zwdispatch::kZwDeliverCatalog ? ApplyDeliver(tx,plan,remote) : Apply(tx,plan,remote);
}

void DestroyZwVehiclePlan(std::uint64_t tx) noexcept {
    Made* m=FindMade(tx);
    if(!m)return;
    // Before activation the hull (and a resupply's courier) is still the transaction's (empty, nobody could board it):
    // deleted, a resupply's price given back and the vehicle it would have replaced its owner's again. Once active it is
    // the world's (its owner may be aboard): kept, only its record dropped.
    if(!m->active) {
        if(m->courier && objref::Exists(m->courier)){PvpForgetSupportObject(m->courier);DeleteSupportSoldier(m->courier);}
        if(Alive(m->vehicle)){PvpForgetSupportObject(m->vehicle);DeleteSupportVehicle(m->vehicle.Obj());}
        if(m->ticket)Refund(m->ticket,"its transaction rolled back");
        if(Made* o=FindMade(m->oldTransaction))if(o!=m)o->replaced=false;
        if(m->deliver && m->own)
            for(auto& l:locals)
                if(l.delivering && (InSession() ? l.human.Is(PlayerHuman()) : m->owner && l.human.Is(m->owner.Obj())))l.delivering=false;
    }
    for(auto& l:locals)if(l.transaction==tx && l.state!=ZwStartState::none)l.state=ZwStartState::refused;
    if(!m->deliver)Forget(*m);
    Log("ZW VEHICLE %llu rolled back",static_cast<unsigned long long>(tx));
    *m=Made{};
}

bool ZwVehicleRequestNotice(std::uint32_t req,support_net::RequestStatus status) noexcept {
    using support_net::RequestStatus;
    if(submitting && submittingFor && status==RequestStatus::accepted && req){submittingFor->request=req;return true;}
    if(submittingDeliver && submittingFor && status==RequestStatus::accepted && req){submittingFor->deliverRequest=req;return true;}
    if(!req)return false;
    for(auto& l:locals) {
        if(l.request==req) {
            if(status>=RequestStatus::refused && l.state==ZwStartState::pending) {
                l.state=ZwStartState::refused;
                Log("ZW START request %u ended without a vehicle (status %u): the player walks in",req,static_cast<unsigned>(status));
            }
            return true;
        }
        if(l.deliverRequest==req) {
            if(status>=RequestStatus::refused && l.delivering) {
                l.delivering=false;
                Log("ZW DELIVER request %u ended without a vehicle (status %u)",req,static_cast<unsigned>(status));
            }
            return true;
        }
    }
    return false;
}

void ZwVehicleTick() noexcept {
    Activate();
    OfflineDeliveries();
    if(!InSession() || OnlineHostOnly())ExpireHolds();
    Couriers();
    VehiclerLogisticsTick();
}

void ResetZwVehicles() noexcept {
    // Mission teardown owns its objects: borrowed references dropped, nothing deleted or written.
    for(auto& m:made)m=Made{};
    for(auto& p:planned)p=Planned{};
    for(auto& l:locals)l=Local{};
    for(auto& h:held)h=Held{};
    if(planningRow.Allocated())Renew(planningRow[0]);
    submitting=submittingDeliver=false;submittingFor=nullptr;offlineOwner=nullptr;serial=0;
    ResetVehiclerLogistics();
}
}  // namespace crew
