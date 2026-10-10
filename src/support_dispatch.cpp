// Support requests select and verify an entry before any object exists. The host sends the resulting
// matrices to peers. Real soldiers board stationary hulls, then the existing vehicle controllers enter.
#include "crew.h"
#include "support_call.h"
#include "support_aircraft.h"
#include "support_config.h"
#include "support_policy.h"
#include "support_soldier.h"
#include "support_spawn.h"
#include "support_net.h"
#include "mission_crew_support.h"
#include "mission_participants.h"
#include "mission_participant_gate.h"
#include "ground_navigation.h"
#include "npcai.h"
#include "online_authority.h"
#include "memory.h"
#include "hudtext.h"
#include "transport.h"
#include "support_variants.h"
#include "airdrop.h"
#include <cwchar>
#include <cstring>
#include <new>
#include <type_traits>

namespace crew {
// Implemented by the real-crew adapter; no imaginary rider participates in deployment.
int NpcBoardCrew(unsigned char*,unsigned char* const*,int) noexcept;
int NpcSeatCrewNow(unsigned char*,unsigned char* const*,int) noexcept;
bool NpcPrepareVehicleRoutePost(unsigned char*,const float*,float) noexcept;
bool NpcReleaseVehicleCrew(unsigned char*) noexcept;
bool RegisterSupportObject(const void*,const unsigned char*) noexcept;
void ReportSupportFailure(std::uint64_t) noexcept;
bool SupportTransactionActive(std::uint64_t) noexcept;
bool HoldSupportSoldier(const ObjRef&,bool) noexcept;
namespace {
constexpr std::uint32_t kSoldier=kSupportRangerResource,kLeader=kSupportLeaderResource,
    kAircraft=kSupportAircraftResource,kVehicle=kSupportVehicleResource;
constexpr int kDeployments=16;
constexpr ULONGLONG kBoardLimit=120000,kCallCooldown=30000;
constexpr float kVehicleWaypointRadius=1.5f,kVehicleDriverHold=1.0f,kVehicleArrival=5.0f;
constexpr float kInfantryWaypointRadius=1.5f,kInfantryRouteStop=1.0f,kInfantryArrival=3.0f;
constexpr int kPlanCallsPerFrame=3;
static_assert(kInfantryRouteStop<kInfantryWaypointRadius,"leader must advance before quiet movement stops");
static_assert(kVehicleDriverHold<kVehicleWaypointRadius,"a route must advance before its driver stops");
struct Planning {
    bool active=false;std::uint32_t catalog=0;float target[3]{};
    // The request's composed load (support_call.h PackSupportLoadout; 0: the call's own) and, once Plan has looked at it,
    // whether it is planned (`composed`, the load in `load`) or the call's own load stands in.
    std::uint64_t loadout=0;bool loadKnown=false,composed=false;SupportLoadout load{};
    int edge=0;npc::navigation::RouteState navigation{};
    bool entriesMade=false;support::GroundEntries entries{};   // fixed at the request: the caller moves on meanwhile
};
Planning planning{};
// The planning and deployment rows hold route states (ground_navigation.h RouteState, ~150 KB each): `row={}` would
// build the empty value as a stack temporary first. Renew value-initialises the row where it stands.
template<class T>
void Renew(T& row) noexcept {
    static_assert(std::is_trivially_destructible_v<T>,"rows are plain data");
    ::new(static_cast<void*>(&row)) T{};
}

struct InfantryRoute {
    npc::navigation::RouteState navigation{};
    bool given=false,done=false,released=false;
};
struct Deployment {
    bool used=false,remote=false,started=false,delivered=false,networked=false,assigned=false;
    std::uint64_t id=0;ULONGLONG born=0;
    SupportPlan plan{};ObjRef objects[support_net::kMaxUnits]{};
    npc::navigation::RouteState navigation{};
    float vehicleSample[3]{};ULONGLONG vehicleSampleAt=0;
    InfantryRoute infantry[3]{}; // one bounded coarse route per leader, not per follower
    ULONGLONG retiredFrame[support_net::kMaxUnits]{}; // an aircraft's crew deleted at this frame + 1 (Retire)
    bool withdrawing=false; // its ground vehicle sent off by a map order (SupportWithdrawVehicle): back to its entry, removed
};
Deployment deployments[kDeployments]{};
bool offlinePending=false;std::uint64_t nextOffline=1;ULONGLONG callAt=0;
wchar_t status[160]{};
struct LocalRequest {
    bool shown=false,acceptNotices=false,hasNotice=false;
    std::uint32_t id=0;
    support_net::RequestStatus state=support_net::RequestStatus::accepted;
    wchar_t text[128]{};
};
LocalRequest localRequest{};
bool configured=false,configNoticeShown=false;
ULONGLONG dispatchFrame=~ULONGLONG{0};
void Status(const wchar_t* text) noexcept {_snwprintf_s(status,_countof(status),_TRUNCATE,L"%ls",text);}
// The sea rescue's own outcome line (2026-10-10, the user: the rescue and the map's support do not touch each other):
// its request, refusals and deployment never write the map's status / request notice; heli.cpp shows this one.
wchar_t rescueNote[160]{};
bool rescueSubmitting=false,rescueEnded=false;std::uint32_t rescueRequest=0;   // the rescue's support_net request id (a guest's)
void RescueNote(const wchar_t* text) noexcept {_snwprintf_s(rescueNote,_countof(rescueNote),_TRUNCATE,L"%ls",text);}
bool IsRescue(std::uint32_t catalog) noexcept;
// A refusal: shown, and logged with an ASCII cause (the log is not wide; the 2026-10-09 report had no trace at all
// of why nothing came).
void Refuse(std::uint32_t catalog,const char* cause,const wchar_t* text) noexcept {
    if(IsRescue(catalog))RescueNote(text);else Status(text);
    Log("SUPPORT plan catalog=%u refused: %s",catalog,cause);
}
// Whether this machine itself may create and own the deployment: offline, or the host of a world whose only actual
// participant it is (support_net.h SupportSoloHostWorld). Otherwise the request goes through the host's transaction.
bool LocalAuthority() noexcept {return !InSession() || SupportSoloHostWorld();}
void RequestNotice(std::uint32_t request,support_net::RequestStatus state) noexcept {
    using support_net::RequestStatus;
    // The rescue's own request: told to the rescue only (its id taken from the accepted notice of its submission).
    if(rescueSubmitting && state==RequestStatus::accepted && request){rescueRequest=request;rescueEnded=false;return;}
    if(request && request==rescueRequest) {
        if(state<RequestStatus::refused || rescueEnded)return;   // told once; its id is kept so it never reaches the map's line
        static const wchar_t* const kWhy[]={L"",L"",L"房主拒绝了救援请求：环境、资源或当前部署条件不满足",L"救援请求超时：未及时获得房主及全员确认",
                                            L"这次救援部署已取消",L"联机会话中断，救援请求未完成"};
        Log("SUPPORT rescue request %u ended without a heli (status %u)",request,static_cast<unsigned>(state));
        rescueEnded=true;RescueRequestFailed(kWhy[static_cast<unsigned>(state)]);
        return;
    }
    if(!localRequest.acceptNotices || !request || request<localRequest.id)return;
    if(request==localRequest.id && localRequest.hasNotice) {
        if(localRequest.state>=RequestStatus::refused)return;
        if(localRequest.state==RequestStatus::active && state==RequestStatus::accepted)return;
    }
    const wchar_t* text=nullptr;
    switch(state) {
    case RequestStatus::accepted:text=L"本机支援请求已受理，等待房主及全员确认";break;
    case RequestStatus::active:text=L"本机支援已获全员对象确认，开始部署";break;
    case RequestStatus::refused:text=L"房主拒绝了本机支援请求：环境、资源或当前部署条件不满足";break;
    case RequestStatus::timeout:text=L"本机支援请求超时：未及时获得房主及全员确认";break;
    case RequestStatus::cancelled:text=L"本机这次支援部署已取消";break;
    case RequestStatus::interrupted:text=L"联机会话中断，本机支援请求未完成";break;
    }
    if(!text)return;
    localRequest.id=request;localRequest.state=state;localRequest.shown=localRequest.hasNotice=true;
    _snwprintf_s(localRequest.text,_countof(localRequest.text),_TRUNCATE,L"%ls",text);
}
int AirCount() noexcept {return SupportAirCallCount();}
int GroundStart() noexcept {return AirCount()+2;}
bool InfantryCatalog(std::uint32_t catalog) noexcept {
    return catalog==static_cast<unsigned>(AirCount()) || catalog==static_cast<unsigned>(AirCount()+1);
}
npc::navigation::Profile InfantryRouteProfile() noexcept {
    npc::navigation::Profile profile;profile.cell=4;profile.waypointRadius=kInfantryWaypointRadius;profile.greed=npc::navigation::kRouteGreed;return profile;
}
bool GroundCatalog(std::uint32_t id,SupportVehicleKind& kind,SupportCrewMode& mode) noexcept {
    const int index=static_cast<int>(id)-GroundStart();
    if(index<0 || index>=kSupportVehicleCount*2)return false;
    kind=static_cast<SupportVehicleKind>(index/2);
    mode=index%2 ? SupportCrewMode::unmanned : SupportCrewMode::soldiers;return true;
}
// The transports (transport.h; the user, 2026-10-09: "飞机和直升机应该也有运输机", "飞机就空降"): four entries after the
// ground ones (so every older index, ini key and wire value stands): a squad / a platoon flown in by a transport
// helicopter (EDF6VC_HELI_TRANSPORT: it lands short of the point, they get off, it stays theirs: transport.cpp), and one
// dropped by a transport plane's stick over the point (EDF6VC_JET_TRANSPORT; the plane leaves). One aircraft each: the
// helicopter seats twelve, the plane twelve (a platoon); the plan is the hull, its pilot and the soldiers aboard.
constexpr int kTransportEntries=4;
int TransportStart() noexcept {return GroundStart()+kSupportVehicleCount*2;}
bool TransportCatalog(std::uint32_t id,bool& platoon,bool& plane) noexcept {
    const int k=static_cast<int>(id)-TransportStart();
    if(k<0 || k>=kTransportEntries)return false;
    platoon=(k&1)!=0;plane=k>=2;return true;
}
bool TransportSpec(std::uint32_t id,SupportAircraft* out) noexcept {
    bool platoon=false,plane=false;
    if(!out || !TransportCatalog(id,platoon,plane))return false;
    SupportAircraft spec{};
    spec.count=1;spec.fuelSeconds=plane ? 600u : 3600u;   // the helicopter stays theirs: its fuel is the mission's
    if(plane)spec.transportPlane=true;else spec.heli=static_cast<int>(HeliBody::transport410);
    *out=spec;return true;
}
unsigned TransportRiders(bool platoon) noexcept {return platoon ? 12u : 4u;}
// The container airdrops (airdrop.cpp; the user, 2026-10-09: "运输机还要能空投载具"): one entry per ground support
// vehicle, after the transports (every older index, ini key and wire value stands). The transport helicopter carries the
// game's own container (the stock Air Raider request's) to the point, hovers over it, lets it go and leaves; the
// container makes the vehicle, empty, where it lands. One helicopter, one container, one vehicle. The plan: the
// helicopter and its pilot. (The transport plane was tried first: on its ~600 m turn it never came over the point.)
constexpr int kAirdropEntries=kSupportVehicleCount;
int AirdropStart() noexcept {return TransportStart()+kTransportEntries;}
bool AirdropCatalog(std::uint32_t id,SupportVehicleKind& kind) noexcept {
    const int k=static_cast<int>(id)-AirdropStart();
    if(k<0 || k>=kAirdropEntries)return false;
    kind=static_cast<SupportVehicleKind>(k);return true;
}
// Its helicopter: the air assault's hull (EDF6VC_HELI_TRANSPORT), one, ten minutes' fuel (it leaves once the container
// is let go).
bool AirdropSpec(std::uint32_t id,SupportAircraft* out) noexcept {
    SupportVehicleKind kind{};
    if(!out || !AirdropCatalog(id,kind))return false;
    SupportAircraft spec{};
    spec.count=1;spec.fuelSeconds=600u;spec.heli=static_cast<int>(HeliBody::transport410);
    *out=spec;return true;
}
// The riders a transport hull seats (tools/make_jets.py: the helicopter's two door gunners and ten passengers, the
// plane's twelve passengers): a composed load fills up to these.
constexpr unsigned kTransportSeats=12;
// The sea rescue (support_call.h SupportRescueCatalog): the catalog's last entry, after the airdrops (every older index,
// ini key and wire value stands; an older peer's catalog ends before it, support_protocol.h kExtSeaRescue).
std::uint32_t RescueCatalog() noexcept {return static_cast<std::uint32_t>(AirdropStart()+kAirdropEntries);}
bool IsRescue(std::uint32_t catalog) noexcept {return catalog==RescueCatalog();}
// Entries that bring aircraft made in the air as a flown call does (crew groups, formation): the flown calls, and the
// rescue. The transports and the airdrops have their own plans (TransportCatalog / AirdropCatalog).
bool AirCatalog(std::uint32_t catalog) noexcept {return catalog<static_cast<unsigned>(AirCount()) || IsRescue(catalog);}
constexpr std::uint32_t kRescueFuelSec=900;   // the rescue heli's fuel (heli.cpp leaves on it), as the rescue always had
// The rescue's takeoff points (support_entry.h TakeoffRoute): the submarine carrier's deck nearest the swimmer (where the
// rescue always started), kDeckInset further into the deck than its nearest edge so the whole 410 stands on it. No stock
// map has an airfield or helipad the plugin has identified; such a point would be one more candidate here.
constexpr float kDeckInset=20.0f;
int RescueTakeoffSpots(const float* target,float (*spots)[3]) noexcept {
    float deck[3];
    if(!SubDeck(target,deck))return 0;
    const float d=support::FlatDistance(deck,target);
    if(d>=1.0f) {
        const float in[3]={deck[0]+(deck[0]-target[0])/d*kDeckInset,deck[1],deck[2]+(deck[2]-target[2])/d*kDeckInset};
        if(!SubDeck(in,deck))return 0;
    }
    std::memcpy(spots[0],deck,12);
    return 1;
}
// Its heli: one 410 (its door seats: a gunner and the swimmer's), the rescue's fuel.
bool RescueSpec(std::uint32_t id,SupportAircraft* out) noexcept {
    if(!out || !IsRescue(id))return false;
    *out=SupportAircraft{};out->heli=static_cast<int>(HeliBody::brute410);out->count=1;out->fuelSeconds=kRescueFuelSec;
    return true;
}
// The aircraft of an entry: an air call's, a transport's, an airdrop's, or the rescue's.
bool AircraftSpecOf(std::uint32_t id,SupportAircraft* out) noexcept {
    return id<static_cast<unsigned>(AirCount()) ? SupportAircraftSpec(static_cast<int>(id),out) :
           TransportSpec(id,out) || AirdropSpec(id,out) || RescueSpec(id,out);
}
// Whether `soldier` sits in a seat of `hull` now.
bool Seated(const unsigned char* hull,const ObjRef& soldier) noexcept {
    if(!soldier || !hull)return false;
    for(unsigned seat=0;seat<SeatCount(hull);++seat)if(At<const void*>(SeatAt(const_cast<unsigned char*>(hull),seat),kSeatRider)==soldier.obj)return true;
    return false;
}
npc::navigation::Profile VehicleRouteProfile(const SupportVehicleSpec& spec) noexcept {
    npc::navigation::Profile profile;
    profile.radius=std::hypot(spec.halfWidth,spec.halfLength);profile.height=spec.height;profile.cell=4;
    profile.waypointRadius=kVehicleWaypointRadius;profile.greed=npc::navigation::kRouteGreed;
    return profile;
}
bool Live(const ObjRef& ref) noexcept {
    return ref && Readable(ref.obj,kDead+1) && ref.Is(ref.obj) && !static_cast<const unsigned char*>(ref.obj)[kDead];
}
void Matrix(const float* at,const float* heading,float* out) noexcept {
    const float matrix[16]={heading[2],0,-heading[0],0,0,1,0,0,heading[0],0,heading[2],0,at[0],at[1],at[2],1};
    std::memcpy(out,matrix,sizeof(matrix));
}
bool Foot(float x,float z,float level,float& floor,float height=2.0f) noexcept {
    if(!MapGroundNear(x,z,level,&floor,true) || !std::isfinite(floor))return false;
    float surface=0.0f;const Sea sea=SeaAt(x,z,&surface);
    if(sea==Sea::unknown || (sea==Sea::water && (!std::isfinite(surface) || surface-floor>npc::navigation::Profile{}.maxWaterDepth)))return false;
    const float from[3]={x,floor+0.08f,z},to[3]={x,floor+height,z};float hit[3];
    return MapRay(from,to,hit)<0;
}
bool AddUnit(SupportPlan& plan,std::uint32_t resource,std::uint32_t parent,const float* at,const float* heading) noexcept {
    if(plan.count>=support_net::kMaxUnits)return false;
    auto& unit=plan.units[plan.count++];unit={};unit.resourceId=resource;unit.role=parent;Matrix(at,heading,unit.matrix);return true;
}
// The real crew gather beside their ground vehicle, on its own level (`rise`).
// `resources`: each one's (count of them).
bool AddCrew(SupportPlan& plan,unsigned parent,const float* entry,const float* heading,unsigned count,float width,
             const std::uint32_t* resources,float rise) noexcept {
    for(unsigned i=0;i<count;++i) {
        const float side=width+2.0f,back=-3.0f-static_cast<float>(i)*2.0f;
        float at[3]={entry[0]+heading[2]*side+heading[0]*back,entry[1],entry[2]-heading[0]*side+heading[2]*back};
        if(!Foot(at[0],at[2],entry[1],at[1]) || std::fabs(at[1]-entry[1])>rise)return false;
        if(!AddUnit(plan,resources[i],parent,at,heading))return false;
    }
    return true;
}
// The real crew an entry's aircraft carries: a 410 its pilot and both door gunners, else its pilot; the rescue's 410 its
// pilot and one door gunner (2026-10-10, the user: 「配炮手吧」). NpcSeatCrewNow seats them in seat order: the pilot in
// seat 0, the gunner in seat 1 (410_HELI_GUNNER_L, vehicle_riding_position's order: docs/rescue-re.md), and seat 2
// (410_HELI_GUNNER_R) stays free for the swimmer (heli.cpp DoorSeat finds the free one).
unsigned AirCrew(std::uint32_t catalog,const SupportAircraft& spec) noexcept {
    if(IsRescue(catalog))return 2u;
    return spec.heli==static_cast<int>(HeliBody::brute410) || spec.heli==static_cast<int>(HeliBody::medic410) ? 3u : 1u;
}
// The aircraft one call brings: the configured number (SupportAircraftCount_<key>) or the call's own, never past the
// plan's 16 units (each aircraft with its real crew).
// Whether this plan may use the configuration's weapons and aircraft counts: every peer of the session accepts them
// (support_net.h SupportPeersAcceptVariants). Otherwise an older peer's Validate refuses the plan mid-transaction
// with no reason anyone sees, so the host plans its protocol v2 plan and says why (once a request, logged).
bool legacyNoticed=false;
bool UseConfiguredLoadout() noexcept {
    if(SupportPeersAcceptVariants())return true;
    if(!legacyNoticed) {
        legacyNoticed=true;
        Log("SUPPORT plan: a peer runs an older All Forces (no soldier-weapon capability): rifles and default aircraft counts");
    }
    return false;
}
// Out-of-game loadouts (support_loadout.h; docs/feature-2026-10-09-loadout-editor.md): a soldier's colours and a vehicle's
// pylons are generated files (support_variants.h). A plan unit carries what it was meant to be (Unit::variant) and
// whether it is made so (kVariantApplied): only when this machine has the file and, online, every peer applies variants
// and has that file by its hello (support_protocol.h kExtVariants). Otherwise the unit is stock and says what it wanted,
// so a peer without the file lists it for its installer (NoteMissingVariant). Each stock fallback is logged and said once
// a request beside the support status (variantNote).
wchar_t variantNote[192]{};
void NoteVariant(const wchar_t* text,const wchar_t* file) noexcept {
    if(!variantNote[0])_snwprintf_s(variantNote,_countof(variantNote),_TRUNCATE,text,file);
}
std::uint64_t DecideVariant(std::uint32_t catalog,const wchar_t* file,std::uint64_t wanted) noexcept {
    if(!wanted || !file || !*file)return 0;
    if(!SupportVariantReady(file)) {
        NoteMissingVariant(file,"this machine's own preset");
        NoteVariant(L"预设文件 %ls 还没生成：本次按原版出动（退出游戏后运行安装器菜单 7 保存即可生成）",file);
        return wanted;
    }
    if(!SupportPeersApplyVariants()) {
        Log("SUPPORT plan catalog=%u: %ls stock: a peer runs an older All Forces (no unit variants)",catalog,file);
        NoteVariant(L"房间里有旧版全军出击：%ls 本次按原版出动（全员更新后生效）",file);
        return wanted;
    }
    if(!SupportPeersHaveVariantFile(VariantHash(file))) {
        Log("SUPPORT plan catalog=%u: %ls stock: a peer does not have that file (it lists it for its installer)",catalog,file);
        NoteVariant(L"房间里有人还没有 %ls：本次按原版出动；对方退出游戏后运行安装器菜单 7 即可自动生成",file);
        return wanted;
    }
    return wanted|kVariantApplied;
}
// Soldier `i` of a composed load: its preset colours (PresetLookFor) as a variant of `resource`'s template.
std::uint64_t SoldierVariant(std::uint32_t catalog,const SupportLoadout& load,int i,std::uint32_t resource) noexcept {
    if(catalog>=static_cast<unsigned>(kSupportConfigUnits))return 0;
    const auto look=PresetLookFor(SupportCfg().preset[catalog],load,i);
    if(!look.On())return 0;
    wchar_t file[96];
    if(!SupportLookFile(SupportSoldierWeapon(resource),IsSupportLeaderResource(resource),look,file,_countof(file)))return 0;
    return DecideVariant(catalog,file,LookVariant(look));
}
// The entry's tank / jets: its SupportVehicle_<key> pylons as a variant of the hull.
std::uint64_t HullVariant(std::uint32_t catalog) noexcept {
    if(catalog>=static_cast<unsigned>(kSupportConfigUnits))return 0;
    const auto& l=SupportCfg().vehicle[catalog];
    if(!l.On() || l.body!=VehicleBodyOfKey(SupportCallKey(static_cast<int>(catalog))))return 0;
    wchar_t file[96];
    if(!VehicleVariantFile(l,file,_countof(file)))return 0;
    return DecideVariant(catalog,file,LoadoutVariant(l));
}
// The file of a plan unit's variant (look or pylons; `path`: the CreateObject spelling), or false: none it can be.
bool VariantFile(const SupportPlan& plan,const support_net::Unit& unit,wchar_t* out,std::size_t capacity,bool path) noexcept {
    if(!unit.variant)return false;
    if(IsSupportSoldierResource(unit.resourceId)) {
        SupportLook look;
        return LookOfVariant(unit.variant,&look) &&
            SupportLookFile(SupportSoldierWeapon(unit.resourceId),IsSupportLeaderResource(unit.resourceId),look,out,capacity,path)>0;
    }
    const VehicleBody body=VehicleBodyOfKey(SupportCallKey(static_cast<int>(plan.catalogId)));
    VehicleLoadout l;
    return !unit.role && unit.resourceId>=kAircraft && LoadoutOfVariant(unit.variant,body,&l) && VehicleVariantFile(l,out,capacity,path)>0;
}
int AircraftCount(std::uint32_t catalog,const SupportAircraft& spec,unsigned group) noexcept {
    if(IsRescue(catalog))return 1;   // one heli for one swimmer, never configured
    const int chosen=catalog<static_cast<unsigned>(kSupportConfigUnits) && UseConfiguredLoadout() ? SupportCfg().aircraft[catalog] : 0;
    const int most=static_cast<int>(support_net::kMaxUnits/group);
    const int wanted=chosen>0 ? chosen : spec.count;
    return wanted<1 ? 1 : wanted>most ? most : wanted;
}
// The infantry's resources (configured weapons, support_config.h): member i of a 4 / 12-person request.
std::uint32_t InfantryResource(bool platoon,unsigned i) noexcept {
    const auto& c=SupportCfg();
    if(!UseConfiguredLoadout())return i%4==0 ? kSupportLeaderResource : kSupportRangerResource;
    if(i%4==0)return SupportSoldierResource(c.leader,true);
    return SupportSoldierResource(platoon ? c.platoon[(i/4)%3] : c.squad,false);
}
// The composed load the host plans for this request (the bar's composition panel; the user, 2026-10-09: "先点载具，然后选
// 里面的人并且可以点多次，直到座位满"): none asked, malformed or past the entry's seats, or a peer without kCapLoadout
// (its Validate would refuse it mid-transaction with no reason anyone sees): the call's own load, said once a request.
bool loadoutNoticed=false;
void ComposeOnce(std::uint32_t catalog) noexcept {
    if(planning.loadKnown)return;
    planning.loadKnown=true;planning.composed=false;planning.load={};
    if(!planning.loadout)return;
    SupportLoadout load;
    if(!UnpackSupportLoadout(planning.loadout,&load) || load.count>SupportCallSeats(static_cast<int>(catalog))) {
        Log("SUPPORT plan catalog=%u: composed load %llX malformed or past the seats: the call's own load",catalog,
            static_cast<unsigned long long>(planning.loadout));
        return;
    }
    if(!SupportPeersAcceptLoadout()) {
        loadoutNoticed=true;
        Log("SUPPORT plan catalog=%u: a peer runs an older All Forces (no composed loads): the call's own load",catalog);
        return;
    }
    planning.composed=true;planning.load=load;
    Log("SUPPORT plan catalog=%u: composed load of %d",catalog,load.count);
}
support_net::PlanResult Plan(std::uint32_t catalog,const float* target,std::uint64_t loadout,SupportPlan* out) noexcept {
    using support_net::PlanResult;
    if(!out || !target || catalog>=static_cast<unsigned>(SupportCallCount())) {
        Refuse(catalog,"invalid request or target",L"支援请求或目标无效");return PlanResult::refused;
    }
    if(!SupportCfg().Enabled(static_cast<int>(catalog))) {
        Refuse(catalog,"disabled by SupportDisabled in EDF6VehicleCrew.ini",L"该支援单位已在配置中停用（EDF6VehicleCrew.ini SupportDisabled）");
        return PlanResult::refused;
    }
    if(!Cfg().customNpcAi){Refuse(catalog,"CustomNpcAi=0",L"NPC 指挥功能未启用，无法调度支援机组");return PlanResult::refused;}
    if(!SupportSoldiersReady()){Refuse(catalog,"support soldiers not ready",SupportSoldierFailureText());return PlanResult::refused;}
    // Both entries stand on the measured play area (playarea.cpp: once a mission, kMeasureAfterMs in, a side a frame).
    // Until then MapPlayArea is the physics square with no ground: a call made in those seconds was refused (an air one as
    // "no clear air corridor", 2026-10-09 16:52:26, 50 ms before the area was in; the same call worked later). It waits.
    // The rescue too (its deck takeoff and its edge both stand on that area): the host's transaction waits; offline,
    // RescueHere says why and heli.cpp asks again.
    if(!PlayAreaMeasured())return PlanResult::pending;
    // The rescue plans in one go and never touches the map's request in planning (its entry search state, its composed
    // load): it has no seats to compose (SupportCallSeats 0) and brings its own crew.
    const bool rescue=IsRescue(catalog);
    if(!rescue && (!planning.active || planning.catalog!=catalog || std::memcmp(planning.target,target,12)!=0 || planning.loadout!=loadout)) {
        Renew(planning);planning.active=true;planning.catalog=catalog;std::memcpy(planning.target,target,12);planning.loadout=loadout;
        legacyNoticed=false;loadoutNoticed=false;variantNote[0]=0;
    }
    if(!rescue)ComposeOnce(catalog);
    const bool composed=!rescue && planning.composed;const SupportLoadout& load=planning.load;
    SupportPlan plan{};plan.catalogId=catalog;std::memcpy(plan.target,target,12);
    if(AirCatalog(catalog)) {
        // The rescue is not held to the mission's support rules (2026-10-10, the user: 「不用受限制吧」).
        if(!IsRescue(catalog) && !support::Allowed(SupportMissionPolicy(),support::Capability::air)) {
            Refuse(catalog,"mission forbids external air support or underground",L"本关限制外部航空支援，或处于地下环境");return PlanResult::refused;
        }
        SupportAircraft spec;support::Route route;
        if(!AircraftSpecOf(catalog,&spec)) {
            Refuse(catalog,"no verified entry for this unit",L"该单位尚无可用的实际入场方式");return PlanResult::refused;
        }
        if(IsRescue(catalog)) {
            // No Air Raider's call: it needs the rescue itself, a pilot the plugin flies, and real crews seated.
            if(!Cfg().seaRescue || !Cfg().npcBoarding || !Cfg().heliPilot) {
                Refuse(catalog,"SeaRescue/NpcBoarding/HeliPilot off",L"海上救援、真实机组登乘或直升机驾驶功能未启用");return PlanResult::refused;
            }
            // Every peer must have the rescue entry (support_protocol.h kExtSeaRescue): an older one's catalog ends before it.
            if(!SupportPeersAcceptRescue()) {
                Refuse(catalog,"a peer runs an older All Forces without the sea rescue entry",hudtext::Tr(hudtext::Tx::supportRescueNeedsUpdate));
                return PlanResult::refused;
            }
        } else if(!Cfg().jetAirRaider || !Cfg().npcBoarding || (spec.heli>=0 ? !Cfg().heliPilot : !Cfg().jetPilot)) {
            Refuse(catalog,"JetAirRaider/NpcBoarding/HeliPilot/JetPilot off",L"航空支援或真实机组驾驶功能未启用");return PlanResult::refused;
        }
        // Every peer must create it in the air the same way (support_protocol.h kCapAirborneAir): an older peer would
        // make the hull empty and its crew falling, so such a room is told why instead.
        if(!SupportPeersAcceptAirborne()) {
            Refuse(catalog,"a peer runs an older All Forces without airborne air support",hudtext::Tr(hudtext::Tx::supportAirNeedsUpdate));
            return PlanResult::refused;
        }
        spec.count=AircraftCount(catalog,spec,1+AirCrew(catalog,spec));
        // The rescue takes off from the nearest takeoff point (a submarine carrier's deck: support_entry.h TakeoffRoute),
        // the edge only when there is none it can climb out of (2026-10-10, the user: 「能从机场起飞就从机场起飞吧」).
        auto refusal=support::Refusal::noEntry;
        if(IsRescue(catalog)) {
            float spots[1][3];
            const int n=RescueTakeoffSpots(target,spots);
            refusal=n ? PlanTakeoffSupport(spec,target,spots,n,&route) : support::Refusal::noEntry;
            if(refusal==support::Refusal::none)Log("SUPPORT plan catalog=%u: takes off from the carrier deck (%.0f,%.0f,%.0f)",catalog,
                                                    route.from[0],route.from[1],route.from[2]);
            else Log("SUPPORT plan catalog=%u: %s; from the map's edge",catalog,n ? "no clear climb out from the carrier deck" : "no takeoff point");
        }
        if(refusal!=support::Refusal::none)refusal=PlanAirSupport(spec,target,player.pos,&route);
        if(refusal!=support::Refusal::none) {
            if(refusal==support::Refusal::noSky)Refuse(catalog,"no open sky over the target",L"此处没有开放天空，航空支援无法进入");
            else if(refusal==support::Refusal::noArea)Refuse(catalog,"the measured play area is too small for an entry",L"本图场地过小，无法安排空中入场");
            else Refuse(catalog,"no clear air corridor from any map edge",L"从地图边缘到目标没有净空的空中航线");
            return PlanResult::refused;
        }
        // Created in the air at the edge, in formation, flying in (support_entry.h AirFormationSlot); the real crew are
        // made inside their aircraft (the same matrix) and seated at once at spawn.
        const auto crew=UseConfiguredLoadout() ? SupportSoldierResource(SupportCfg().aircraftCrew,false) : kSupportRangerResource;
        const float spacing=spec.heli>=0 ? 0.6f : 1.0f;
        for(int i=0;i<spec.count;++i) {
            float at[3];support::AirFormationSlot(route,i,spacing,at);
            const unsigned parent=plan.count+1;
            if(!AddUnit(plan,kAircraft+kSupportAirborneOffset+catalog,0,at,route.heading)){Refuse(catalog,"plan full",L"支援单位过多");return PlanResult::refused;}
            plan.units[plan.count-1].variant=HullVariant(catalog);   // its pylons (SupportVehicle_<key>)
            for(unsigned c=0;c<AirCrew(catalog,spec);++c)if(!AddUnit(plan,crew,parent,at,route.heading)) {
                Refuse(catalog,"plan full",L"支援单位过多");return PlanResult::refused;
            }
        }
        Log("SUPPORT plan catalog=%u ready: %d aircraft in the air at (%.0f,%.0f,%.0f) heading (%.2f,%.2f), %u units",catalog,spec.count,
            route.from[0],route.from[1],route.from[2],route.heading[0],route.heading[2],plan.count);
        *out=plan;if(!IsRescue(catalog))planning.active=false;
        return PlanResult::ready;
    }
    if(bool platoon=false,plane=false;TransportCatalog(catalog,platoon,plane)) {
        if(!support::Allowed(SupportMissionPolicy(),support::Capability::air)) {
            Refuse(catalog,"mission forbids external air support or underground",L"本关限制外部航空支援，或处于地下环境");return PlanResult::refused;
        }
        SupportAircraft spec;TransportSpec(catalog,&spec);support::Route route;
        if(!Cfg().npcBoarding || (plane ? !Cfg().jetPilot : !Cfg().heliPilot)) {
            Refuse(catalog,"NpcBoarding/HeliPilot/JetPilot off",L"真实机组驾驶或登乘功能未启用，无法空运");return PlanResult::refused;
        }
        if(!SupportPeersAcceptAirborne() || !SupportPeersAcceptTransports()) {
            Refuse(catalog,"a peer runs an older All Forces without the transports",hudtext::Tr(hudtext::Tx::supportAirNeedsUpdate));
            return PlanResult::refused;
        }
        if(!SupportAircraftReady(spec)) {
            Refuse(catalog,plane ? "EDF6VC_JET_TRANSPORT.SGO not installed / preloaded" : "EDF6VC_HELI_TRANSPORT.SGO not installed / preloaded",
                   L"运输机资源未安装：请用安装器重新安装");
            return PlanResult::refused;
        }
        const auto refusal=PlanAirSupport(spec,target,player.pos,&route);
        if(refusal!=support::Refusal::none) {
            if(refusal==support::Refusal::noSky)Refuse(catalog,"no open sky over the target",L"此处没有开放天空，运输机无法进入");
            else if(refusal==support::Refusal::noArea)Refuse(catalog,"the measured play area is too small for an entry",L"本图场地过小，无法安排空中入场");
            else Refuse(catalog,"no clear air corridor from any map edge",L"从地图边缘到目标没有净空的空中航线");
            return PlanResult::refused;
        }
        // The hull in the air at the edge, its pilot and the soldiers made inside it and seated at once (BoardAirborne).
        const auto crew=UseConfiguredLoadout() ? SupportSoldierResource(SupportCfg().aircraftCrew,false) : kSupportRangerResource;
        if(!AddUnit(plan,kAircraft+kSupportAirborneOffset+catalog,0,route.from,route.heading) ||
           !AddUnit(plan,crew,1,route.from,route.heading)){Refuse(catalog,"plan full",L"支援单位过多");return PlanResult::refused;}
        const unsigned riders=composed ? static_cast<unsigned>(load.count) : TransportRiders(platoon);
        for(unsigned i=0;i<riders;++i) {
            const auto resource=composed ? SupportLoadoutResource(load,static_cast<int>(i)) : InfantryResource(platoon,i);
            if(!AddUnit(plan,resource,1,route.from,route.heading)) {
                Refuse(catalog,"plan full",L"支援单位过多");return PlanResult::refused;
            }
            if(composed)plan.units[plan.count-1].variant=SoldierVariant(catalog,load,static_cast<int>(i),resource);
        }
        Log("SUPPORT plan catalog=%u ready: a transport %s in the air at (%.0f,%.0f,%.0f) heading (%.2f,%.2f), %u soldiers aboard",catalog,
            plane ? "plane (paratroop drop)" : "helicopter (air assault)",route.from[0],route.from[1],route.from[2],route.heading[0],
            route.heading[2],riders);
        *out=plan;planning.active=false;return PlanResult::ready;
    }
    if(SupportVehicleKind kind{};AirdropCatalog(catalog,kind)) {
        const auto* spec=SupportVehicleInfo(kind);
        if(!support::Allowed(SupportMissionPolicy(),support::Capability::air) ||
           !support::Allowed(SupportMissionPolicy(),spec->wasteland ? support::Capability::civilianGround : support::Capability::militaryGround)) {
            Refuse(catalog,"mission forbids external air support or this vehicle",L"本关限制外部航空支援或该车辆");return PlanResult::refused;
        }
        // The stock container registers its vehicle on the room's network when in a session (0x5E91D7), from an
        // identity derived from its carrier's: the plugin's plane has none (docs/airdrop-vehicle-re.md section 4).
        if(InSession()) {
            Refuse(catalog,"container airdrops are offline only",L"运输机投送载具目前只能在单人（离线）任务中使用");return PlanResult::refused;
        }
        SupportAircraft air;AirdropSpec(catalog,&air);support::Route route;
        if(!Cfg().npcBoarding || !Cfg().heliPilot) {
            Refuse(catalog,"NpcBoarding/HeliPilot off",L"真实机组驾驶或登乘功能未启用，无法空运");return PlanResult::refused;
        }
        if(!SupportAircraftReady(air)) {
            Refuse(catalog,"EDF6VC_HELI_TRANSPORT.SGO not installed / preloaded",L"运输直升机资源未安装：请用安装器重新安装");
            return PlanResult::refused;
        }
        if(!AirdropReady(kind)) {
            Refuse(catalog,"container or vehicle not preloaded / airdrop profile off",L"集装箱或该车辆的资源不可用，无法投送");
            return PlanResult::refused;
        }
        const auto refusal=PlanAirSupport(air,target,player.pos,&route);
        if(refusal!=support::Refusal::none) {
            if(refusal==support::Refusal::noSky)Refuse(catalog,"no open sky over the target",L"此处没有开放天空，运输直升机无法进入");
            else if(refusal==support::Refusal::noArea)Refuse(catalog,"the measured play area is too small for an entry",L"本图场地过小，无法安排空中入场");
            else Refuse(catalog,"no clear air corridor from any map edge",L"从地图边缘到目标没有净空的空中航线");
            return PlanResult::refused;
        }
        const auto crew=UseConfiguredLoadout() ? SupportSoldierResource(SupportCfg().aircraftCrew,false) : kSupportRangerResource;
        if(!AddUnit(plan,kAircraft+kSupportAirborneOffset+catalog,0,route.from,route.heading) ||
           !AddUnit(plan,crew,1,route.from,route.heading)){Refuse(catalog,"plan full",L"支援单位过多");return PlanResult::refused;}
        Log("SUPPORT plan catalog=%u ready: a transport helicopter carrying a container (%ls) in the air at (%.0f,%.0f,%.0f) heading "
            "(%.2f,%.2f)",catalog,spec->sgo,route.from[0],route.from[1],route.from[2],route.heading[0],route.heading[2]);
        *out=plan;planning.active=false;return PlanResult::ready;
    }
    const auto area=MapPlayArea();
    // The ground entries alone need the measured ground (the air ones plan in the play box, refusing noArea themselves:
    // a map with no ground found round its centre still takes an air call, as before the transports).
    if(!area.ground){Refuse(catalog,"no ground found round the map's centre",L"无法测定本图的地面范围，无法规划支援入口");return PlanResult::refused;}
    SupportVehicleKind kind{};SupportCrewMode mode{};const bool vehicle=GroundCatalog(catalog,kind,mode);
    const auto* spec=vehicle ? SupportVehicleInfo(kind) : nullptr;
    if(vehicle && !support::Allowed(SupportMissionPolicy(),spec && spec->wasteland ? support::Capability::civilianGround : support::Capability::militaryGround)) {
        Refuse(catalog,"mission forbids external military support",L"本关限制外部军事支援；可选择步兵或民用轻卡");return PlanResult::refused;
    }
    if(vehicle && (!spec || !Cfg().npcBoarding || !SupportVehicleReady(kind,mode))) {
        Refuse(catalog,"vehicle resource not preloaded / NpcBoarding off",L"该车辆的资源或交付能力不可用");return PlanResult::refused;
    }
    // Entry candidates round the target within the route planner's reach (support_entry.h GroundEntryCandidates),
    // fixed when the request starts so a caller walking on cannot reshuffle the ones already tried.
    if(!planning.entriesMade) {
        planning.entries=support::GroundEntryCandidates(area,target,player.pos);planning.entriesMade=true;
        Log("SUPPORT plan catalog=%u: %d ground entry candidates round (%.0f,%.0f,%.0f)",catalog,planning.entries.count,target[0],target[1],target[2]);
    }
    if(planning.edge>=planning.entries.count) {
        Refuse(catalog,"no entry candidate connected to the target by a verified route",L"找不到与目的地连通的支援入口（周围 650-950 米内无可达路线）");
        return PlanResult::refused;
    }
    float entry[3]={planning.entries.at[planning.edge][0],target[1],planning.entries.at[planning.edge][1]};
    const auto nextEdge=[&](const char* why) {
        Log("SUPPORT plan catalog=%u entry %d (%.0f,%.0f) rejected: %s",catalog,planning.edge,entry[0],entry[2],why);
        ++planning.edge;Renew(planning.navigation);return PlanResult::pending;
    };
    if(!Foot(entry[0],entry[2],target[1],entry[1]))return nextEdge("no dry standable ground");
    auto profile=InfantryRouteProfile();
    if(spec)profile=VehicleRouteProfile(*spec);
    float waypoint[3];
    // A request plans across frames. It may take a few route steps a frame (each GroundNavigate call yields after
    // four edges); the navigator's shared per-frame query budget still bounds the work and keeps the NPCs' share.
    auto path=npc::navigation::Result::pending;
    for(int call=0;call<kPlanCallsPerFrame && path==npc::navigation::Result::pending;++call)
        path=GroundNavigate(planning.navigation,entry,target,2.0f,GameMs(),waypoint,profile);
    if(path==npc::navigation::Result::pending){Status(L"正在核实支援入口及可达路线");return PlanResult::pending;}
    if(path==npc::navigation::Result::blocked)return nextEdge("no route to the target");
    const float distance=support::FlatDistance(entry,target);
    const float heading[3]={(target[0]-entry[0])/distance,0,(target[2]-entry[2])/distance};
    if(vehicle) {
        entry[1]+=0.1f;
        // Its driver (the configured crew weapon), then its passengers: the composed load, or as many more crew.
        const auto driver=UseConfiguredLoadout() ? SupportSoldierResource(SupportCfg().vehicleCrew,false) : kSupportRangerResource;
        std::uint32_t crew[support_net::kMaxUnits];unsigned count=mode==SupportCrewMode::unmanned ? 1u : spec->seats;
        if(mode==SupportCrewMode::soldiers && composed)count=1+static_cast<unsigned>(load.count);
        if(count>support_net::kMaxUnits-1)count=support_net::kMaxUnits-1;
        for(unsigned i=0;i<count;++i)crew[i]=i>0 && composed ? SupportLoadoutResource(load,static_cast<int>(i-1)) : driver;
        if(!AddUnit(plan,kVehicle+static_cast<unsigned>(kind),0,entry,heading) ||
           !AddCrew(plan,1,entry,heading,count,spec->halfWidth,crew,0.55f))
            return nextEdge("no level ground for the crew");
        // The hull's pylons (SupportVehicle_<key>), the passengers' colours (unit 1 is the driver, 2.. the composed load).
        plan.units[plan.count-count-1].variant=HullVariant(catalog);
        for(unsigned i=1;composed && i<count;++i)
            plan.units[plan.count-count+i].variant=SoldierVariant(catalog,load,static_cast<int>(i-1),crew[i]);
    } else {
        const bool platoon=catalog!=static_cast<unsigned>(AirCount());
        const unsigned count=composed ? static_cast<unsigned>(load.count) : platoon ? 12u : 4u;
        for(unsigned i=0;i<count;++i) {
            float at[3]={entry[0]+static_cast<float>(i%4)*2.0f,entry[1],entry[2]+static_cast<float>(i/4)*2.0f};
            if(!Foot(at[0],at[2],entry[1],at[1]) || std::fabs(at[1]-entry[1])>0.55f)return nextEdge("no level ground for the squad");
            const auto resource=composed ? SupportLoadoutResource(load,static_cast<int>(i)) : InfantryResource(platoon,i);
            if(!AddUnit(plan,resource,0,at,heading))return nextEdge("plan full");
            if(composed)plan.units[plan.count-1].variant=SoldierVariant(catalog,load,static_cast<int>(i),resource);
        }
    }
    Log("SUPPORT plan catalog=%u ready: entry (%.0f,%.0f,%.0f), %.0f m to the target, %u units",catalog,entry[0],entry[1],entry[2],
        distance,plan.count);
    *out=plan;planning.active=false;return PlanResult::ready;
}

bool Validate(const SupportPlan& plan) noexcept {
    if(plan.catalogId==support_net::kMissionCrewCatalog)return ValidateMissionCrewPlan(plan);
    if(!support_net::ValidPlan(plan,false) || plan.catalogId>=static_cast<unsigned>(SupportCallCount()) || !SupportSoldiersReady())return false;
    SupportAircraft aircraft;SupportVehicleKind kind{};SupportCrewMode mode{};
    const bool air=AirCatalog(plan.catalogId),ground=GroundCatalog(plan.catalogId,kind,mode);
    const auto capability=air ? support::Capability::air : ground ?
        (SupportVehicleInfo(kind)->wasteland ? support::Capability::civilianGround : support::Capability::militaryGround) : support::Capability::infantry;
    const auto member=[](std::uint32_t id) noexcept {return IsSupportSoldierResource(id) && !IsSupportLeaderResource(id);};
    // Unit variants (support_loadout.h): a look on a soldier, pylons on the entry's own hull, each one that decodes. A hull
    // made from its loaded file must have it here: another weapon list than the host's would not be the same vehicle (a
    // soldier's colours alone may come stock: the same class, weapon and AI). The missing file goes on the pending list.
    for(unsigned i=0;i<plan.count;++i) {
        const auto& unit=plan.units[i];
        if(!unit.variant)continue;
        wchar_t file[96];
        if(!VariantFile(plan,unit,file,_countof(file),false)) {
            Log("SUPPORT validate catalog=%u: unit %u variant %016llX does not decode for it",plan.catalogId,i,
                static_cast<unsigned long long>(unit.variant));
            return false;
        }
        if((unit.variant&kVariantApplied) && !IsSupportSoldierResource(unit.resourceId) && !SupportVariantReady(file)) {
            NoteMissingVariant(file,"the host's plan");
            NoteVariant(L"房主的载具预设需要 %ls，本机没有：本次支援未出动；退出游戏后运行安装器菜单 7 即可生成",file);
            return false;
        }
    }
    if(bool platoon=false,plane=false;TransportCatalog(plan.catalogId,platoon,plane)) {
        // Its hull, its pilot, the soldiers aboard (a leader every four): all made inside the hull (role 1).
        if(!support::Allowed(SupportMissionPolicy(),support::Capability::air))return false;
        // Its riders: the call's own (4 / 12) or a composed load, one to the hull's seats.
        if(plan.count<3 || plan.count>2+kTransportSeats || plan.units[0].role || plan.units[0].resourceId!=kAircraft+kSupportAirborneOffset+plan.catalogId)return false;
        if(!member(plan.units[1].resourceId) || plan.units[1].role!=1)return false;
        for(unsigned i=2;i<plan.count;++i)
            if(plan.units[i].role!=1 || !IsSupportSoldierResource(plan.units[i].resourceId) ||
               IsSupportLeaderResource(plan.units[i].resourceId)!=((i-2)%4==0))return false;
        return true;
    }
    if(SupportVehicleKind dropped{};AirdropCatalog(plan.catalogId,dropped)) {
        // Its helicopter and its pilot (made inside it, role 1): the container is made by its owner when it flies in.
        if(!support::Allowed(SupportMissionPolicy(),support::Capability::air))return false;
        return plan.count==2 && !plan.units[0].role && plan.units[0].resourceId==kAircraft+kSupportAirborneOffset+plan.catalogId &&
               member(plan.units[1].resourceId) && plan.units[1].role==1;
    }
    // The rescue is not held to the mission's support rules (Plan does not ask them either).
    if(!IsRescue(plan.catalogId) && !support::Allowed(SupportMissionPolicy(),capability))return false;
    if(air && !AircraftSpecOf(plan.catalogId,&aircraft))return false;
    if(ground && !SupportVehicleReady(kind,mode))return false;
    // Weapons and the number of aircraft are the host's configuration: a peer checks the structure and that every
    // soldier is one of the stock templates, never against its own ini.
    if(air) {
        const unsigned group=1+AirCrew(plan.catalogId,aircraft);
        if(!plan.count || plan.count%group)return false;
        // The rescue: exactly one heli made in the air (no older host ever planned one on a runway).
        if(IsRescue(plan.catalogId) && (plan.count!=group || plan.units[0].resourceId!=kAircraft+kSupportAirborneOffset+plan.catalogId))
            return false;
        for(unsigned i=0;i<plan.count;++i) {
            const auto& unit=plan.units[i];
            // This version's airborne hull, or an older host's runway hull (applied as that host planned it).
            if(i%group==0) {
                if((unit.resourceId!=kAircraft+kSupportAirborneOffset+plan.catalogId && unit.resourceId!=kAircraft+plan.catalogId) ||
                   unit.role || plan.units[0].resourceId!=unit.resourceId)return false;
            }
            else if(!member(unit.resourceId) || unit.role!=i-i%group+1)return false;
        }
    } else if(ground) {
        // Unmanned: its driver alone. Crewed: its driver and up to a seat each more (a full crew of the call's own, or a
        // composed load of passengers: any soldier, a leader every four after the driver).
        const unsigned rows=SupportVehicleInfo(kind)->seats;
        if((mode==SupportCrewMode::unmanned ? plan.count!=2 : plan.count<2 || plan.count>rows+1) || plan.units[0].role ||
           plan.units[0].resourceId!=kVehicle+static_cast<unsigned>(kind))return false;
        if(!member(plan.units[1].resourceId) || plan.units[1].role!=1)return false;
        for(unsigned i=2;i<plan.count;++i)if(!IsSupportSoldierResource(plan.units[i].resourceId) || plan.units[i].role!=1)return false;
    } else {
        // The call's own 4 / 12, or a composed load of one to twelve.
        if(!plan.count || plan.count>static_cast<unsigned>(kSupportLoadoutMost))return false;
        for(unsigned i=0;i<plan.count;++i)
            if(plan.units[i].role || !IsSupportSoldierResource(plan.units[i].resourceId) ||
               IsSupportLeaderResource(plan.units[i].resourceId)!=(i%4==0))return false;
    }
    for(unsigned i=0;i<plan.count;++i) {
        const auto& unit=plan.units[i];
        if(unit.role && (unit.role>i || plan.units[unit.role-1].resourceId<kAircraft))return false;
        if(IsSupportSoldierResource(unit.resourceId))continue;
        if((unit.resourceId==kAircraft+plan.catalogId || unit.resourceId==kAircraft+kSupportAirborneOffset+plan.catalogId) &&
           AirCatalog(plan.catalogId))continue;
        if(unit.resourceId>=kVehicle && unit.resourceId<kVehicle+kSupportVehicleCount &&
           GroundCatalog(plan.catalogId,kind,mode) && unit.resourceId==kVehicle+static_cast<unsigned>(kind))continue;
        return false;
    }
    return true;
}
// A deployment's aircraft whose flight is over (SupportAircraftLeft: withdrawn far out for fuel, ammo or damage): its
// owner deletes the real crew it made, then the hull once the game has taken it as empty (team kTeamVehicle, the stock
// emptied step) or kRetireSettleFrames after (jet.cpp kReapSettleFrames: a hull deleted before that step stayed in team
// 5's set after it was freed). HeliReap / JetReap never delete under real soldiers, so nothing deleted them: out of fuel
// they hung at the map's edge for the rest of the mission (2026-10-09 16:59, two helis parked at x 1447 and -1450).
// A player aboard keeps the whole aircraft.
constexpr ULONGLONG kRetireSettleFrames=30;
void Retire(Deployment& deployed) noexcept {
    const auto& plan=deployed.plan;
    for(unsigned i=0;i<plan.count;++i) {
        const auto resource=plan.units[i].resourceId;
        if(plan.units[i].role || resource<kAircraft || resource>=kVehicle || !Live(deployed.objects[i]))continue;
        auto* hull=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj));
        bool aboard=false;
        for(unsigned seat=0;seat<SeatCount(hull);++seat)aboard=aboard || AnyPlayerIn(SeatAt(hull,seat));
        if(aboard)continue;
        auto& retired=deployed.retiredFrame[i];
        if(!retired) {
            if(!SupportAircraftLeft(deployed.objects[i]))continue;
            // Only the ones still aboard: a transport's soldiers got off and fight on (transport.cpp).
            for(unsigned k=0;k<plan.count;++k)if(plan.units[k].role==i+1 && Seated(hull,deployed.objects[k]))DeleteSupportSoldier(deployed.objects[k]);
            retired=GameFrame()+1;
            Log("SUPPORT deployment %llu: aircraft %u has left: its crew deleted",static_cast<unsigned long long>(deployed.id),i);
            continue;
        }
        if(At<std::int32_t>(hull,kTeam)!=kTeamVehicle && GameFrame()+1-retired<kRetireSettleFrames)continue;
        if(DeleteSupportAircraft(deployed.objects[i]))
            Log("SUPPORT deployment %llu: aircraft %u gone (deleted)",static_cast<unsigned long long>(deployed.id),i);
    }
}
// A support ground vehicle withdrawn by a map order (SupportWithdrawVehicle): driven back along a ground route to the entry
// it came in at, there its own crew still aboard deleted and then the hull once its seats are empty (as Retire does an
// aircraft's). With no driver it goes where it stands. A player aboard keeps it (the withdrawal ends).
void WithdrawGround(Deployment& deployed) noexcept {
    if(!Live(deployed.objects[0])){deployed.withdrawing=false;return;}
    auto* hull=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[0].obj));
    for(unsigned seat=0;seat<SeatCount(hull);++seat)if(AnyPlayerIn(SeatAt(hull,seat))) {
        deployed.withdrawing=false;Status(L"玩家已登乘，撤离取消");return;
    }
    auto& retired=deployed.retiredFrame[0];
    if(retired) {
        if(DeleteSupportVehicle(hull)) {
            Log("SUPPORT deployment %llu: its vehicle withdrawn (deleted)",static_cast<unsigned long long>(deployed.id));
            deployed.withdrawing=false;return;
        }
        if(GameFrame()-retired>120) {
            Log("SUPPORT deployment %llu: its withdrawn vehicle still has riders: left where it is",static_cast<unsigned long long>(deployed.id));
            deployed.withdrawing=false;
        }
        return;
    }
    SupportVehicleKind kind{};SupportCrewMode mode{};
    if(!GroundCatalog(deployed.plan.catalogId,kind,mode)){deployed.withdrawing=false;return;}
    const float* position=reinterpret_cast<const float*>(hull+kPosition);
    const float* exit=deployed.plan.units[0].matrix+12;
    bool there=!NpcDriver(hull);
    if(!there) {
        float waypoint[3];
        const auto path=GroundNavigate(deployed.navigation,position,exit,kVehicleArrival,GameMs(),waypoint,VehicleRouteProfile(*SupportVehicleInfo(kind)));
        if(path==npc::navigation::Result::moving)NpcPrepareVehicleRoutePost(hull,waypoint,kVehicleDriverHold);
        else if(path==npc::navigation::Result::arrived)there=true;
        else if(path==npc::navigation::Result::blocked)there=support::FlatDistance(position,player.pos)>300.0f;   // out of the way
        else NpcPrepareVehicleRoutePost(hull,position,kVehicleDriverHold);
    }
    if(!there)return;
    for(unsigned k=0;k<deployed.plan.count;++k)
        if(deployed.plan.units[k].role==1 && Seated(hull,deployed.objects[k]))DeleteSupportSoldier(deployed.objects[k]);
    retired=GameFrame()+1;
    Log("SUPPORT deployment %llu: its withdrawn vehicle is out: its crew deleted",static_cast<unsigned long long>(deployed.id));
}
void Destroy(std::uint64_t id) noexcept {
    for(auto& deployed:deployments)if(deployed.used && deployed.id==id) {
        bool preserve[support_net::kMaxUnits]{};
        bool occupied=false;
        // A player can board a newly visible hull before support becomes Active. Cancellation must
        // not invalidate that player's live ride, including a player replicated from another peer.
        for(unsigned i=0;i<deployed.plan.count;++i) {
            if(!Live(deployed.objects[i]))continue;
            auto* object=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj));
            if(deployed.plan.units[i].resourceId<kAircraft)preserve[i]=edf::IsAnyPlayer(object);
            else for(unsigned seat=0;seat<SeatCount(object);++seat)
                preserve[i]=preserve[i] || AnyPlayerIn(SeatAt(object,seat));
            occupied=occupied || preserve[i];
        }
        // Keep the real crew attached to a retained hull, releasing deployment holds. Do not kick
        // or delete its passengers as a side effect of rolling back unrelated, still-empty units.
        for(unsigned i=0;i<deployed.plan.count;++i) {
            const auto parent=deployed.plan.units[i].role;
            if(parent && parent<=deployed.plan.count && preserve[parent-1])preserve[i]=true;
            if(preserve[i] && deployed.plan.units[i].resourceId<kAircraft)HoldSupportSoldier(deployed.objects[i],false);
        }
        // Remove crew first so native seat ownership is released before deleting hulls.
        for(unsigned i=0;i<deployed.plan.count;++i)
            if(!preserve[i] && deployed.plan.units[i].resourceId<kAircraft)DeleteSupportSoldier(deployed.objects[i]);
        for(unsigned i=0;i<deployed.plan.count;++i) {
            if(preserve[i])continue;
            if(deployed.plan.units[i].resourceId>=kVehicle && Live(deployed.objects[i]))DeleteSupportVehicle(static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj)));
            else if(deployed.plan.units[i].resourceId>=kAircraft)DeleteSupportAircraft(deployed.objects[i]);
        }
        const bool rescue=IsRescue(deployed.plan.catalogId);
        Renew(deployed);
        const wchar_t* why=occupied ? L"部署已取消，保留玩家已经登乘的车辆及其机组" : L"支援部署取消，已撤销未完成的部署";
        if(rescue)RescueNote(why);else Status(why);
        return;
    }
    DestroyMissionCrewPlan(id);
}
// `networked`: a committed support_net transaction (registered IDs on every peer); else this machine's own
// deployment (LocalAuthority): unregistered objects only it simulates.
// Airborne aircraft of a just created deployment: each one's real crew (made inside it, unregistered) take their seats
// at once (NpcSeatCrewNow, the stock RideVehicle), then hull and crew are registered (networked) and, where the flight is
// run (not a remote copy), it flies on at once: a hull in the air must not wait for the all-peer barrier unflown. The
// held crew's AI and any native ride/follow work still wait for Active (Assign).
bool BoardAirborne(Deployment& deployed,bool networked) noexcept {
    const auto& plan=deployed.plan;
    for(unsigned i=0;i<plan.count;++i) {
        if(!IsSupportAirborneAircraft(plan.units[i].resourceId))continue;
        auto* vehicle=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj));
        unsigned char* crew[support_net::kMaxUnits];int count=0;
        for(unsigned k=0;k<plan.count;++k)if(plan.units[k].role==i+1)crew[count++]=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[k].obj));
        const int seated=NpcSeatCrewNow(vehicle,crew,count);
        if(seated!=count) {
            Log("SUPPORT spawn %llu: %d of %d crew seated in aircraft %u",static_cast<unsigned long long>(deployed.id),seated,count,i);
            return false;
        }
        if(networked) {
            if(!RegisterSupportObject(vehicle,plan.units[i].netId))return false;
            for(unsigned k=0;k<plan.count;++k)if(plan.units[k].role==i+1 && !RegisterSupportObject(deployed.objects[k].obj,plan.units[k].netId))return false;
        }
        if(!deployed.remote) {
            SupportAircraft spec;
            if(!AircraftSpecOf(plan.catalogId,&spec) || !ActivateSupportAircraft(vehicle,spec,plan.target,true))return false;
            deployed.started=true;
        }
    }
    return true;
}
bool SpawnDeployment(std::uint64_t id,const SupportPlan& plan,bool remote,bool networked) noexcept {
    if(!Validate(plan))return false;
    Deployment* deployed=nullptr;for(auto& row:deployments)if(!row.used){deployed=&row;break;}
    if(!deployed)return false;
    deployed->used=true;deployed->id=id;deployed->plan=plan;deployed->remote=remote;deployed->born=GameMs();deployed->networked=networked;
    for(unsigned i=0;i<plan.count;++i) {
        const auto& unit=plan.units[i];ObjRef object;
        // An airborne aircraft's crew: made unregistered, seated below, then registered (no ride event before peers
        // know the objects); every other soldier as before.
        const bool aboard=unit.role && IsSupportAirborneAircraft(plan.units[unit.role-1].resourceId);
        // The unit's variant file (Validate decoded it): made from it when applied and here; a wanted one this machine
        // lacks goes on its pending list (the host's preset), a soldier whose applied look is missing here comes stock.
        wchar_t file[96]{},path[128]{};
        const bool hasVariant=VariantFile(plan,unit,file,_countof(file),false) && VariantFile(plan,unit,path,_countof(path),true);
        const bool here=hasVariant && SupportVariantReady(file);
        if(hasVariant && !here)NoteMissingVariant(file,remote ? "the host's preset" : "this machine's own preset");
        const wchar_t* const made=here && (unit.variant&kVariantApplied) ? path : nullptr;
        if(unit.resourceId<kAircraft) {
            if(!(aboard ? CreateSupportSoldierUnregistered : ApplySupportSoldierResource)(unit.matrix,unit.resourceId,
                    networked ? unit.netId : nullptr,!networked,&object,made)) {
                Log("SUPPORT spawn %llu: soldier %u (resource %X) not created",static_cast<unsigned long long>(id),i,unit.resourceId);
                Destroy(id);return false;
            }
            deployed->objects[i]=object;
            if(!HoldSupportSoldier(object,true)){Destroy(id);return false;}
        } else {
            unsigned char* vehicle=nullptr;
            if(unit.resourceId<kVehicle) {
                SupportAircraft spec;
                if(AircraftSpecOf(plan.catalogId,&spec))vehicle=PrepareSupportAircraft(spec,unit.matrix,made);
            } else {
                SupportVehicleKind kind{};SupportCrewMode mode{};
                if(GroundCatalog(plan.catalogId,kind,mode))vehicle=SpawnSupportVehicle(kind,mode,unit.matrix+12,unit.matrix+8,nullptr,made);
            }
            if(!vehicle) {
                Log("SUPPORT spawn %llu: hull %u (resource %X) not created",static_cast<unsigned long long>(id),i,unit.resourceId);
                Destroy(id);return false;
            }
            object=ObjRef::Of(vehicle);deployed->objects[i]=object;
            if(networked && !IsSupportAirborneAircraft(unit.resourceId) && !RegisterSupportObject(vehicle,unit.netId)){Destroy(id);return false;}
        }
        deployed->objects[i]=object;
    }
    if(!BoardAirborne(*deployed,networked)){Destroy(id);return false;}
    Log("SUPPORT spawn %llu: catalog %u, %u units created (%s)",static_cast<unsigned long long>(id),plan.catalogId,plan.count,
        networked ? "networked" : InSession() ? "host of a one-player world" : "offline");
    if(IsRescue(plan.catalogId)) {
        // The rescue's own logic (heli.cpp) flies it where it is flown and boards the swimmer on the swimmer's machine;
        // the dispatcher keeps only the deployment's bookkeeping (held crew, cancel, losses), never the arrival order
        // every other air entry gets. Where it is flown it is the requester's (2026-10-10, the user: 「为什么会找不到」):
        // this machine's player for its own deployment, a guest's by the transaction's requester identity.
        deployed->delivered=true;
        ObjRef requester{};
        if(!remote) {
            if(!networked){if(unsigned char* own=PlayerHuman())requester=ObjRef::Of(own);}
            else if(!SupportTransactionRequester(id,&requester))
                Log("SUPPORT spawn %llu: the rescue's requester is not found among the mission's players",static_cast<unsigned long long>(id));
        }
        RescueHeliDeployed(static_cast<unsigned char*>(const_cast<void*>(deployed->objects[0].obj)),plan.target,!remote,requester);
        RescueNote(L"救援直升机已起飞，正在飞往落水的玩家");
        return true;
    }
    if(deployed->started)Status(L"空中支援已从场外空中入场，正在飞往目标");
    else Status(networked ? L"支援已在入口集结，等待所有玩家确认对象" : L"支援已在入口集结，真实机组正在登车");
    return true;
}
bool Spawn(std::uint64_t id,const SupportPlan& plan,bool remote) noexcept {
    if(plan.catalogId==support_net::kMissionCrewCatalog)return ApplyMissionCrewPlan(id,plan,remote);
    return SpawnDeployment(id,plan,remote,InSession());
}
bool Assign(Deployment& deployed) noexcept {
    const auto& plan=deployed.plan;ObjRef leader;
    // Native ride/follow events use a different channel. Never emit one until every peer has ACKed
    // construction, or it can arrive before that peer knows the soldier/vehicle's native identity.
    for(unsigned i=0;i<plan.count;++i)if(plan.units[i].resourceId<kAircraft)
        if(!HoldSupportSoldier(deployed.objects[i],false))return false;
    for(unsigned i=0;i<plan.count;++i) {
        const auto& unit=plan.units[i];
        if(IsSupportLeaderResource(unit.resourceId)){leader=deployed.objects[i];continue;}
        if(IsSupportSoldierResource(unit.resourceId) && !unit.role && leader && !FollowSupportSoldier(deployed.objects[i],leader))return false;
        if(unit.resourceId<kAircraft || deployed.remote || IsSupportAirborneAircraft(unit.resourceId))continue;   // aboard already
        unsigned char* crew[support_net::kMaxUnits];int count=0;
        for(unsigned k=0;k<plan.count;++k)if(plan.units[k].role==i+1)crew[count++]=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[k].obj));
        if(NpcBoardCrew(static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj)),crew,count)!=count)return false;
    }
    if(bool platoon=false,plane=false;TransportCatalog(plan.catalogId,platoon,plane)) {
        // The soldiers aboard as squads (a leader every four, from unit 2), then handed to their transport: a helicopter
        // flies them in and lands short of the point (transport.cpp TransportDeliver), a plane drops them over it.
        const void* tops[3]{};int squadCount=0;ObjRef top;
        for(unsigned i=2;i<plan.count;++i) {
            if(IsSupportLeaderResource(plan.units[i].resourceId)){top=deployed.objects[i];if(squadCount<3)tops[squadCount++]=top.obj;continue;}
            if(top && !FollowSupportSoldier(deployed.objects[i],top))return false;
        }
        if(!deployed.remote) {
            auto* hull=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[0].obj));
            const bool handed=plane ? JetFerry(hull,plan.target) && TransportParadrop(hull,plan.target)
                                    : TransportDeliver(hull,tops,squadCount,plan.target);
            if(!handed)Log("SUPPORT deployment %llu: its transport not handed over (it flies its call)",static_cast<unsigned long long>(deployed.id));
            deployed.delivered=true;
        }
        deployed.assigned=true;
        Status(plane ? L"运输机已入场，将在目标上空空降" : L"运输直升机已入场，将在目标附近降落放下小队");
        return true;
    }
    if(SupportVehicleKind kind{};AirdropCatalog(plan.catalogId,kind)) {
        // The helicopter flies to the point carrying the container (airdrop.cpp), hovers, lets it go there and leaves.
        if(!deployed.remote) {
            auto* hull=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[0].obj));
            if(!AirdropBegin(hull,kind,plan.target)) {
                Log("SUPPORT deployment %llu: no container to carry: the helicopter leaves",static_cast<unsigned long long>(deployed.id));
                HeliStartLeaving(hull);
                Status(L"运输直升机未能挂载集装箱，已返航");
            } else Status(L"运输直升机已入场，将在目标上空投下载具");
            deployed.delivered=true;
        }
        deployed.assigned=true;
        return true;
    }
    deployed.assigned=true;
    if(!IsRescue(plan.catalogId))Status(L"全员已确认，真实机组正在登车");   // the rescue's line is its own (RescueNote)
    return true;
}
void Configure() noexcept {
    if(configured)return;
    InstallMissionParticipantGate(&SupportMissionPlayerAllowed,&NoteSupportMissionPlayerCreated);
    ConfigureSupportNet({Plan,Validate,Spawn,Destroy,
        [](std::uint32_t ordinal,unsigned char* out) noexcept {
            return OnlineHostOnly() && DeriveSupportSoldierNetId(PlayerHuman(),ordinal,out);
        },&ReadMissionParticipants,&MissionParticipantGateReady,&MissionParticipantCreationsMatch,&RequestNotice,&SupportVariantHello});
    InstallMissionCrewSupport();configured=true;
}
}
// transport.cpp's WITHDRAW: a support vehicle of a deployment this machine made leaves the field. A helicopter or a plane
// flies off (StartLeave / JetWithdrawNow; Retire deletes it out there with its crew still aboard), a ground vehicle drives
// back to its entry (WithdrawGround). False: not a support vehicle of this machine's.
bool SupportWithdrawVehicle(const void* vehicle) noexcept {
    __try {
        for(auto& deployed:deployments) {
            if(!deployed.used || deployed.remote)continue;
            for(unsigned i=0;i<deployed.plan.count;++i) {
                const auto resource=deployed.plan.units[i].resourceId;
                if(deployed.plan.units[i].role || resource<kAircraft || !deployed.objects[i].Is(vehicle) || !Live(deployed.objects[i]))continue;
                // The rescue heli is the rescue's (heli.cpp flies it to the swimmer and on): no map order sends it off.
                if(IsRescue(deployed.plan.catalogId))return false;
                if(resource>=kVehicle) {
                    deployed.withdrawing=true;deployed.delivered=true;deployed.retiredFrame[0]=0;deployed.navigation.initialized=false;
                    Log("SUPPORT deployment %llu: its vehicle withdrawn by order: back to its entry",static_cast<unsigned long long>(deployed.id));
                    Status(L"支援车辆撤离中：返回入场点后离开战场");
                    return true;
                }
                Status(L"支援飞机撤离中：飞离战场");
                return IsHelicopter(vehicle) ? HeliStartLeaving(vehicle) : JetWithdrawNow(vehicle,"withdrawn by a map order");
            }
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
int SupportCallCount() noexcept {return AirCount()+2+kSupportVehicleCount*2+kTransportEntries+kAirdropEntries+1;}
int SupportMenuCount() noexcept {return SupportCallCount()-1;}   // the rescue (last) is no map call
int SupportRescueCatalog() noexcept {return static_cast<int>(RescueCatalog());}
const wchar_t* SupportCallKey(int index) noexcept {
    if(index<0)return nullptr;
    if(index<AirCount())return SupportAirCallKey(index);
    static const wchar_t* keys[]={L"SQUAD",L"PLATOON",L"TANK_CREWED",L"TANK_DELIVERY",L"TRANSPORT_CREWED",L"TRANSPORT_DELIVERY",
                                  L"TRUCK_CREWED",L"TRUCK_DELIVERY",L"SQUAD_HELI",L"PLATOON_HELI",L"SQUAD_AIRDROP",L"PLATOON_AIRDROP",
                                  L"TANK_AIRDROP",L"TRANSPORT_AIRDROP",L"TRUCK_AIRDROP",L"RESCUE"};
    static_assert(sizeof(keys)/sizeof(keys[0])==2+kSupportVehicleCount*2+kTransportEntries+kAirdropEntries+1,
                  "one key per infantry, ground, transport, airdrop and the rescue entry");
    const int rest=index-AirCount();
    return rest<static_cast<int>(sizeof(keys)/sizeof(keys[0])) ? keys[rest] : nullptr;
}
const wchar_t* SupportCallName(int index) noexcept {
    if(index<AirCount())return SupportAirCallName(index);
    if(index==AirCount())return L"步兵小队（4人）";
    if(index==AirCount()+1)return L"步兵大队（12人）";
    // Returned static strings can safely be copied into draw-thread snapshots.
    static const wchar_t* labels[]={L"坦克·有人",L"坦克·空车交付",L"装甲运兵车·有人",L"装甲运兵车·空车交付",L"民用轻卡·有人",L"民用轻卡·空车交付"};
    if(index==SupportRescueCatalog())return L"海上救援直升机";
    const int ground=index-GroundStart();
    if(ground>=0 && ground<6)return labels[ground];
    static const wchar_t* transports[kTransportEntries]={L"直升机机降·小队（4人）",L"直升机机降·大队（12人）",L"运输机空降·小队（4人）",
                                                         L"运输机空降·大队（12人）"};
    const int t=index-TransportStart();
    if(t>=0 && t<kTransportEntries)return transports[t];
    // One row, "直升机投送", its vehicles the chips (map_buttons.h GroupSupport groups by the part before "·").
    static const wchar_t* airdrops[kAirdropEntries]={L"直升机投送·坦克",L"直升机投送·装甲运兵车",L"直升机投送·民用轻卡"};
    const int a=index-AirdropStart();return a>=0 && a<kAirdropEntries ? airdrops[a] : L"支援";
}
int SupportCallSeats(int index) noexcept {
    if(index<0 || index>=SupportCallCount())return 0;
    const auto id=static_cast<std::uint32_t>(index);
    if(InfantryCatalog(id))return kSupportLoadoutMost;
    if(bool platoon=false,plane=false;TransportCatalog(id,platoon,plane))return static_cast<int>(kTransportSeats);
    SupportVehicleKind kind{};SupportCrewMode mode{};
    if(!GroundCatalog(id,kind,mode) || mode!=SupportCrewMode::soldiers || kind==SupportVehicleKind::tank)return 0;
    return static_cast<int>(SupportVehicleInfo(kind)->seats)-1;   // every stock row but the driver's
}
bool SupportCallPreset(int index,SupportLoadout* out) noexcept {
    if(!out)return false;
    *out=SupportLoadout{};
    const int room=SupportCallSeats(index);
    if(room<=0)return false;
    // What the entry brings uncomposed (InfantryResource's configured weapons): a squad of four, or a platoon of twelve.
    const auto id=static_cast<std::uint32_t>(index);
    bool platoon=false,plane=false;
    if(InfantryCatalog(id))platoon=index!=AirCount();
    else TransportCatalog(id,platoon,plane);
    const auto& c=SupportCfg();
    // The out-of-game preset (SupportPreset_<key>, validated against these seats when the ini was read), when there is one.
    if(index<kSupportConfigUnits && c.preset[index].count>0 && c.preset[index].count<=room) {
        *out=PresetLoadout(c.preset[index]);
        return true;
    }
    const int own=platoon ? 12 : 4;
    out->count=own<room ? own : room;
    for(int i=0;i<out->count;++i)out->soldier[i]=i%4==0 ? c.leader : platoon ? c.platoon[(i/4)%3] : c.squad;
    return true;
}
namespace {
// A request of entry `index` (a map / radio call): planned here (offline, a one-player world's host) or sent to the host.
bool Request(int index,const float* target,const SupportLoadout* load,wchar_t* note,std::size_t capacity) noexcept {
    if(!note || !capacity)return false;
    // A call made as it is (a radio weapon, airstrike.cpp) brings the entry's out-of-game preset, as the bar's panel does.
    SupportLoadout preset;
    if((!load || load->count<=0) && index>=0 && index<kSupportConfigUnits && SupportCfg().preset[index].count>0 &&
       SupportCallPreset(index,&preset))load=&preset;
    const std::uint64_t loadout=load && load->count>0 ? PackSupportLoadout(*load) : 0;
    // Clear even when Submit refuses before allocating a request id. That attempt must not retain
    // the previous request's successful/failed notice. Offline calls use their detailed local status.
    localRequest={};note[0]=0;
    Configure();bool accepted=false;
    if(!Cfg().enabled || !target || index<0 || index>=SupportCallCount())Status(L"支援请求不可用");
    else if(load && load->count>0 && (!loadout || load->count>SupportCallSeats(index)))Status(hudtext::Tr(hudtext::Tx::supportLoadoutTooMany));
    else if(!SupportCfg().Enabled(index)) {
        // Known before any request leaves this machine: the host would refuse it with no detail.
        Refuse(static_cast<std::uint32_t>(index),"disabled by SupportDisabled in EDF6VehicleCrew.ini",
               L"该支援单位已在配置中停用（EDF6VehicleCrew.ini SupportDisabled / 安装器菜单 7）");
    }
    else if(InSession() && !LocalAuthority()) {
        localRequest.acceptNotices=true;
        accepted=SubmitSupportRequest(index,target,note,capacity,loadout);
        if(!accepted || !localRequest.hasNotice) {
            localRequest.shown=true;localRequest.acceptNotices=accepted;localRequest.hasNotice=false;
            _snwprintf_s(localRequest.text,_countof(localRequest.text),_TRUNCATE,L"%ls",
                note[0] ? note : accepted ? L"本机支援请求已排队，等待房主确认" : L"本机支援请求未受理");
        }
        _snwprintf_s(note,capacity,_TRUNCATE,L"%ls",localRequest.text);
        return accepted;
    }
    else if(offlinePending)Status(L"上一项支援仍在核实入场路线");
    else if(callAt && GameMs()-callAt<kCallCooldown)Status(L"支援调度冷却中（30 秒）");
    else {
        Renew(planning);planning.active=true;planning.catalog=static_cast<unsigned>(index);std::memcpy(planning.target,target,12);
        planning.loadout=loadout;
        offlinePending=true;accepted=true;Status(L"正在安排支援入口及路线");
        Log("SUPPORT request catalog=%d accepted here (%s), load %llX",index,InSession() ? "host of a one-player world" : "offline",
            static_cast<unsigned long long>(loadout));
    }
    if(SupportCfg().problems[0] && !configNoticeShown) {
        // An invalid setting is said once a mission (the default it fell back to is used; the log has it too).
        configNoticeShown=true;
        const std::size_t used=std::wcslen(status);
        _snwprintf_s(status+used,_countof(status)-used,_TRUNCATE,L"（支援配置有误：%ls）",SupportCfg().problems);
    }
    _snwprintf_s(note,capacity,_TRUNCATE,L"%ls",status);return accepted;
}
}  // namespace
bool SupportCallAt(int index,const float* target,wchar_t* note,std::size_t capacity) noexcept {
    return SupportCallComposedAt(index,target,nullptr,note,capacity);
}
bool SupportCallComposedAt(int index,const float* target,const SupportLoadout* load,wchar_t* note,std::size_t capacity) noexcept {
    if(note && capacity && index==SupportRescueCatalog()) {
        // Only the rescue's own trigger asks for it (a swimmer to pick up and a carrier to ferry them to).
        Status(L"海上救援只在玩家落海时自动呼叫");_snwprintf_s(note,capacity,_TRUNCATE,L"%ls",status);return false;
    }
    return Request(index,target,load,note,capacity);
}
namespace {
// Offline / a one-player world's host: the rescue planned and made at once (an air plan never waits), with no queue,
// no cooldown and no map request in planning touched (2026-10-10, the user: 「不用受限制吧」).
bool RescueHere(const float* target) noexcept {
    SupportPlan plan;
    const auto planned=Plan(RescueCatalog(),target,0,&plan);
    // Pending: only the play area not measured yet (the first seconds of a mission); nothing waits here, heli.cpp asks
    // again. Refused: Refuse wrote why (rescueNote).
    if(planned==support_net::PlanResult::pending)RescueNote(hudtext::Tr(hudtext::Tx::rescueAreaPending));
    if(planned!=support_net::PlanResult::ready)return false;
    if(!SpawnDeployment(nextOffline++,plan,false,false)){RescueNote(L"救援直升机、真实机组或席位分配失败");return false;}
    return true;
}
// The rescue's request: the same switches and the same planner and transaction as a map call, never the map's queue,
// cooldown, status or request notice. A guest's goes to the host as any request (its id kept: RequestNotice).
bool RescueRequest(const float* target,wchar_t* note,std::size_t capacity) noexcept {
    Configure();rescueNote[0]=0;
    const auto index=RescueCatalog();
    bool accepted=false;
    if(!Cfg().enabled || !target)RescueNote(L"支援请求不可用");
    else if(!SupportCfg().Enabled(static_cast<int>(index)))
        Refuse(index,"disabled by SupportDisabled in EDF6VehicleCrew.ini",L"海上救援已在配置中停用（EDF6VehicleCrew.ini SupportDisabled / 安装器菜单 7）");
    else if(InSession() && !LocalAuthority()) {
        rescueSubmitting=true;
        accepted=SubmitSupportRequest(static_cast<int>(index),target,rescueNote,_countof(rescueNote));
        rescueSubmitting=false;
        if(!accepted)rescueEnded=true;
    }
    else accepted=RescueHere(target);
    _snwprintf_s(note,capacity,_TRUNCATE,L"%ls",rescueNote);
    return accepted;
}
}  // namespace
bool SupportRescueAt(const float* target,wchar_t* note,std::size_t capacity) noexcept {
    if(!note || !capacity)return false;
    const bool accepted=RescueRequest(target,note,capacity);
    Log("SUPPORT rescue request at (%.0f,%.1f,%.0f): %s",target ? target[0] : 0.0f,target ? target[1] : 0.0f,target ? target[2] : 0.0f,
        accepted ? "accepted" : "not accepted");
    return accepted;
}
SupportIcon SupportCallIcon(int index) noexcept {
    SupportAircraft spec;
    if(index==SupportRescueCatalog())return SupportIcon::heli;
    if(index<AirCount()) {
        if(!SupportAircraftSpec(index,&spec))return SupportIcon::sub;
        if(spec.heli>=0)return SupportIcon::heli;
        const auto role=static_cast<JetRole>(spec.jet);
        if(role==JetRole::gunship)return SupportIcon::gunship;
        return role==JetRole::carrier || role==JetRole::blastCarrier || role==JetRole::dollCarrier ? SupportIcon::carrier : SupportIcon::jet;
    }
    if(InfantryCatalog(static_cast<std::uint32_t>(index)))return index==AirCount() ? SupportIcon::squad : SupportIcon::platoon;
    if(bool platoon=false,plane=false;TransportCatalog(static_cast<std::uint32_t>(index),platoon,plane))return plane ? SupportIcon::jet : SupportIcon::heli;
    if(SupportVehicleKind dropped{};AirdropCatalog(static_cast<std::uint32_t>(index),dropped))return SupportIcon::heli;   // the carrier
    SupportVehicleKind kind{};SupportCrewMode mode{};
    if(!GroundCatalog(static_cast<std::uint32_t>(index),kind,mode))return SupportIcon::squad;
    return kind==SupportVehicleKind::tank ? SupportIcon::tank : kind==SupportVehicleKind::transport ? SupportIcon::apc : SupportIcon::truck;
}
SupportVariant SupportCallVariant(int index) noexcept {
    SupportAircraft spec;
    if(index<AirCount())return SupportAircraftSpec(index,&spec) ? (spec.follow ? SupportVariant::follow : SupportVariant::guard) : SupportVariant::none;
    if(bool platoon=false,plane=false;TransportCatalog(static_cast<std::uint32_t>(index),platoon,plane))
        return platoon ? SupportVariant::platoon : SupportVariant::squad;
    if(SupportVehicleKind dropped{};AirdropCatalog(static_cast<std::uint32_t>(index),dropped))
        return dropped==SupportVehicleKind::tank ? SupportVariant::tank : dropped==SupportVehicleKind::transport ? SupportVariant::apc :
               SupportVariant::truck;
    SupportVehicleKind kind{};SupportCrewMode mode{};
    if(!GroundCatalog(static_cast<std::uint32_t>(index),kind,mode))return SupportVariant::none;
    return mode==SupportCrewMode::unmanned ? SupportVariant::empty : SupportVariant::crewed;
}
SupportReadiness SupportCallReadiness() noexcept {
    // The same tests SupportCallAt makes before it plans, read without asking: a guest's host decides (the host of a
    // one-player world plans here, through the same pending plan and cooldown as offline).
    if(!Cfg().enabled)return {SupportReady::off,0};
    if(InSession() && !LocalAuthority())return {SupportReady::ready,0};
    if(offlinePending)return {SupportReady::planning,0};
    const ULONGLONG now=GameMs();
    if(callAt && now-callAt<kCallCooldown)return {SupportReady::cooldown,static_cast<int>((kCallCooldown-(now-callAt)+999)/1000)};
    return {SupportReady::ready,0};
}
void SupportCallStatus(wchar_t* out,std::size_t capacity) noexcept {
    if(!out || !capacity)return;
    // The last plan fell back for an older peer: said beside every later status of it (why the configured weapons and
    // counts did not come), until the next request or mission.
    if(legacyNoticed || loadoutNoticed)_snwprintf_s(out,capacity,_TRUNCATE,L"%ls（%ls）",localRequest.shown ? localRequest.text : status,
                                  hudtext::Tr(loadoutNoticed ? hudtext::Tx::supportLoadoutLegacyPeers : hudtext::Tx::supportLegacyPeers));
    else _snwprintf_s(out,capacity,_TRUNCATE,L"%ls",localRequest.shown ? localRequest.text : status);
    // A preset's colours or pylons that came stock (DecideVariant): said beside it too, until the next request or mission.
    if(variantNote[0]) {
        const std::size_t used=std::wcslen(out);
        _snwprintf_s(out+used,capacity-used,_TRUNCATE,L"（%ls）",variantNote);
    }
}
// AirdropTest (tests only, tests/autopilot): once a mission, kAirdropTestMs after the play area is measured, the airdrop
// entry asked as the map's support bar asks it, kAirdropTestAhead m from the player towards the play area's middle (a
// point by the map's edge is hovered short of: airdrop_logic.h ReleaseNow).
constexpr ULONGLONG kAirdropTestMs=8000;
constexpr float kAirdropTestAhead=40.0f;
ULONGLONG airdropTestSince=0;bool airdropTestAsked=false;
void AirdropTestCall() noexcept {
    if(!Cfg().airdropTest || airdropTestAsked || !PlayAreaMeasured() || !player.at)return;
    const ULONGLONG now=GameMs();
    if(!airdropTestSince){airdropTestSince=now;return;}
    if(now-airdropTestSince<kAirdropTestMs)return;
    airdropTestAsked=true;
    const auto area=MapPlayArea();
    float dx=(area.lo[0]+area.hi[0])*0.5f-player.pos[0],dz=(area.lo[1]+area.hi[1])*0.5f-player.pos[2];
    const float len=std::sqrt(dx*dx+dz*dz);
    if(len>1.0f){dx/=len;dz/=len;}else{dx=1.0f;dz=0.0f;}
    const float target[3]={player.pos[0]+kAirdropTestAhead*dx,player.pos[1],player.pos[2]+kAirdropTestAhead*dz};
    const int index=AirdropStart()+Cfg().airdropTest-1;
    wchar_t note[128]{};
    const bool asked=SupportCallAt(index,target,note,_countof(note));
    // The key, not the name: the log is narrow and %ls of Chinese text fails its whole line (the note is the HUD's).
    Log("SUPPORT AirdropTest: %ls asked at (%.0f,%.0f,%.0f), %.0f m from the player: %s",SupportCallKey(index),target[0],
        target[1],target[2],kAirdropTestAhead,asked ? "accepted" : "refused");
}

void SupportDispatchTick() noexcept {
    if(dispatchFrame==GameFrame())return;
    dispatchFrame=GameFrame();
    Configure();SupportNetTick();
    AirdropTestCall();
    if(offlinePending && !LocalAuthority()){offlinePending=false;planning.active=false;Status(L"已取消离线请求，请通过房主重新调度");}
    if(offlinePending) {
        SupportPlan plan;
        const auto result=Plan(planning.catalog,planning.target,planning.loadout,&plan);
        if(result!=support_net::PlanResult::pending) {
            offlinePending=false;
            if(result==support_net::PlanResult::ready) {
                if(SpawnDeployment(nextOffline++,plan,false,false))callAt=GameMs();else Status(L"支援资源、真实机组或席位分配失败");
            }
        }
    }
    for(auto& deployed:deployments) {
        if(!deployed.used)continue;
        bool anyLive=false;
        for(unsigned i=0;i<deployed.plan.count;++i)anyLive=anyLive || Live(deployed.objects[i]);
        if(!anyLive){Renew(deployed);continue;}
        if(!deployed.remote)Retire(deployed);
        if(!deployed.assigned) {
            if(deployed.networked && !SupportTransactionActive(deployed.id))continue;
            if(!Assign(deployed)) {
                const auto id=deployed.id;if(deployed.networked)ReportSupportFailure(id);else Destroy(id);continue;
            }
        }
        if(deployed.withdrawing && !deployed.remote){WithdrawGround(deployed);continue;}
        if(deployed.remote || deployed.delivered)continue;
        bool alive=true,boarded=true;
        for(unsigned i=0;i<deployed.plan.count;++i) {
            alive=alive && Live(deployed.objects[i]);
            const auto parent=deployed.plan.units[i].role;if(!parent || !alive)continue;
            auto* vehicle=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[parent-1].obj));
            bool seated=false;
            for(unsigned seat=0;seat<SeatCount(vehicle);++seat)
                seated=seated || At<const void*>(SeatAt(vehicle,seat),kSeatRider)==deployed.objects[i].obj;
            boarded=boarded && seated;
        }
        if(!alive && deployed.started) {
            // Once in the field, losses are gameplay. Never despawn the surviving deployed units.
            deployed.delivered=true;Status(L"支援途中遭受损失，存活单位继续执行任务");continue;
        }
        if(!alive || (!deployed.started && GameMs()-deployed.born>kBoardLimit)) {
            const auto id=deployed.id;if(deployed.networked)ReportSupportFailure(id);else Destroy(id);continue;
        }
        if(!deployed.started && boarded) {
            bool started=true;
            for(unsigned i=0;i<deployed.plan.count;++i) {
                const auto resource=deployed.plan.units[i].resourceId;
                auto* object=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj));
                if(resource>=kAircraft && resource<kVehicle) {
                    SupportAircraft spec;   // an older host's runway hull
                    started=AircraftSpecOf(deployed.plan.catalogId,&spec) && ActivateSupportAircraft(object,spec,deployed.plan.target,false) && started;
                } else if(resource>=kVehicle)started=NpcPrepareVehicleRoutePost(object,
                    reinterpret_cast<const float*>(object+kPosition),kVehicleDriverHold) && started;
                else if(IsSupportLeaderResource(resource))started=NpcPrepareSquadRoute(object,
                    reinterpret_cast<const float*>(object+kPosition),kInfantryRouteStop) && started;
            }
            if(started){deployed.started=true;Status(L"支援已出发，正在沿路线入场");}
        }
        if(!deployed.started)continue;
        // Ground hull follows verified route waypoints, not a line through walls. Arrival releases real
        // drivers only for empty delivery; the same objects remain in the mission and can return on foot.
        SupportVehicleKind kind{};SupportCrewMode mode{};
        if(GroundCatalog(deployed.plan.catalogId,kind,mode)) {
            auto* vehicle=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[0].obj));
            const float* position=reinterpret_cast<const float*>(vehicle+kPosition);float waypoint[3];
            const auto profile=VehicleRouteProfile(*SupportVehicleInfo(kind));
            const ULONGLONG now=GameMs();
            float speed=1e9f;
            if(deployed.vehicleSampleAt && now>=deployed.vehicleSampleAt) {
                const float dt=now>deployed.vehicleSampleAt ? std::fmin(static_cast<float>(now-deployed.vehicleSampleAt)*0.001f,1.0f/60.0f) : 1.0f/60.0f;
                const float dx=position[0]-deployed.vehicleSample[0],dy=position[1]-deployed.vehicleSample[1],dz=position[2]-deployed.vehicleSample[2];
                speed=std::sqrt(dx*dx+dy*dy+dz*dz)/dt;
            }
            std::memcpy(deployed.vehicleSample,position,12);deployed.vehicleSampleAt=now;
            const auto path=GroundNavigate(deployed.navigation,position,deployed.plan.target,kVehicleArrival,now,waypoint,profile);
            if(path==npc::navigation::Result::moving)NpcPrepareVehicleRoutePost(vehicle,waypoint,kVehicleDriverHold);
            else if(path==npc::navigation::Result::arrived) {
                if(!NpcPrepareVehicleRoutePost(vehicle,position,kVehicleDriverHold) || !(speed<=0.5f))continue;
                if(mode==SupportCrewMode::unmanned && !NpcReleaseVehicleCrew(vehicle))continue;
                if(mode==SupportCrewMode::unmanned) {
                    const auto playerRef=ObjRef::Of(PlayerHuman());
                    for(unsigned i=0;i<deployed.plan.count;++i)if(deployed.plan.units[i].role==1)
                        FollowSupportSoldier(deployed.objects[i],playerRef);
                } else if(kind!=SupportVehicleKind::tank) {
                    // A crewed APC / truck: the soldiers in its passenger seats are a squad and it is their transport
                    // (transport.cpp; the user, 2026-10-09: "卡车之类的运输载具改成断剑那种操作方式"): they get off here and
                    // guard the point; its driver stays aboard and it waits for their next far order.
                    // Its leader the passenger made as one (IsSupportLeaderResource), else the first: the seats need not
                    // be in the plan's order, and a leader following one of its own members inverts the squad.
                    const auto passenger=[&](unsigned i) noexcept {
                        return deployed.plan.units[i].role==1 && Live(deployed.objects[i]) && SeatCount(vehicle)>0 &&
                               At<const void*>(SeatAt(vehicle,0),kSeatRider)!=deployed.objects[i].obj && Seated(vehicle,deployed.objects[i]);
                    };
                    ObjRef top;
                    for(unsigned i=0;i<deployed.plan.count && !top;++i)
                        if(passenger(i) && IsSupportLeaderResource(deployed.plan.units[i].resourceId))top=deployed.objects[i];
                    for(unsigned i=0;i<deployed.plan.count && !top;++i)if(passenger(i))top=deployed.objects[i];
                    for(unsigned i=0;i<deployed.plan.count;++i)
                        if(passenger(i) && deployed.objects[i].obj!=top.obj)FollowSupportSoldier(deployed.objects[i],top);
                    const void* tops[1]={top.obj};
                    if(top && !TransportDeliver(vehicle,tops,1,deployed.plan.target))
                        Log("SUPPORT deployment %llu: its passengers not handed to their transport",static_cast<unsigned long long>(deployed.id));
                }
                deployed.delivered=true;Status(mode==SupportCrewMode::unmanned ? L"空车已到达交付点，司机已下车" : L"支援车辆已抵达目的地");
            } else NpcPrepareVehicleRoutePost(vehicle,position,kVehicleDriverHold); // wait for a verified route; never drive through the obstacle
        } else if(InfantryCatalog(deployed.plan.catalogId)) {
            int leader=0;bool complete=true,released=false;
            for(unsigned i=0;i<deployed.plan.count;++i)if(IsSupportLeaderResource(deployed.plan.units[i].resourceId) && leader<3) {
                auto& route=deployed.infantry[leader++];
                if(!route.done) {
                    auto* object=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj));
                    const auto* position=reinterpret_cast<const float*>(object+kPosition);float waypoint[3];
                    const auto path=GroundNavigate(route.navigation,position,deployed.plan.target,kInfantryArrival,GameMs(),waypoint,InfantryRouteProfile());
                    if(path==npc::navigation::Result::arrived) {
                        route.done=NpcFinishSquadRoute(object,deployed.plan.target);
                    } else {
                        // Pending/blocked requests hold only quiet movement; combat and native follower
                        // links stay owned by npcai. Never issue a far ordinary guard or reset AI per frame.
                        const bool accepted=NpcPrepareSquadRoute(object,path==npc::navigation::Result::moving ? waypoint : position,kInfantryRouteStop);
                        if(accepted)route.given=true;
                        else if(route.given){route.done=route.released=true;}
                    }
                }
                complete=complete && route.done;released=released || route.released;
            }
            if(complete && leader>0) {
                deployed.delivered=true;
                Status(hudtext::Tr(released ? hudtext::Tx::supportInfantryTransferred : hudtext::Tx::supportInfantryArrived));
            }
        } else {
            bool arrived=true;
            for(unsigned i=0;i<deployed.plan.count;++i)if(!deployed.plan.units[i].role) {
                const auto* pos=reinterpret_cast<const float*>(static_cast<const unsigned char*>(deployed.objects[i].obj)+kPosition);
                arrived=arrived && support::FlatDistance(pos,deployed.plan.target)<150;
            }
            if(arrived) {
                for(unsigned i=0;i<deployed.plan.count;++i)if(deployed.plan.units[i].resourceId>=kAircraft) {
                    SupportAircraft spec;
                    if(!AircraftSpecOf(deployed.plan.catalogId,&spec))continue;
                    if(spec.heli>=0)HeliCommand(deployed.objects[i].obj,Command{});
                    else JetCommand(deployed.objects[i].obj,Command{});
                }
                deployed.delivered=true;Status(L"支援已抵达并进入任务区域");
            }
        }
    }
}
void ResetSupportDispatch() noexcept {
    airdropTestSince=0;airdropTestAsked=false;
    // Mission reset invalidates the old objects; do not delete through last mission's borrowed pointers.
    Renew(planning);for(auto& row:deployments)Renew(row);offlinePending=false;callAt=0;nextOffline=1;status[0]=0;configNoticeShown=false;
    rescueNote[0]=0;rescueSubmitting=false;rescueEnded=false;rescueRequest=0;
    legacyNoticed=false;loadoutNoticed=false;variantNote[0]=0;
    localRequest={};
    dispatchFrame=~ULONGLONG{0};
    ResetMissionCrewSupport();
    ResetSupportNet();
    // MissionStart runs during preload, before native players are created. Installing lazily on the
    // first player frame would miss the creation callback and discard its admission observation.
    Configure();
}
} // namespace crew
