// Execute the production dispatch/boarding lifecycle using inert stand-ins for EDF objects.
#include "../src/support_dispatch.cpp"
#include "../src/support_config.cpp"
#include "../src/support_protocol.h"
#include "../src/support_points_logic.h"
#include <set>
#include <string>
#include <vector>
#include "../src/map_buttons.h"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;Config config{};PlayerFix player{{0,0,0},2,1};
const Config& Cfg() noexcept {return config;}
bool testOnline=false,testHost=true;ULONGLONG fixtureMs=1000;
support::Environment missionEnvironment=support::Environment::normal;
bool missionUnderground=false;
support::Policy SupportMissionPolicy() noexcept {return {missionEnvironment,missionUnderground};}
bool InSession() noexcept {return testOnline;}
bool OnlineHostOnly() noexcept {return testHost;}
#ifndef SUPPORT_ROUTE_NATIVE_TEST
bool IsOnlineAuthority(const void*) noexcept {return true;}
#endif
ULONGLONG GameMs() noexcept {return fixtureMs;}
std::uint64_t GameFrame() noexcept {return fixtureMs;}
int traceLines=0;   // "SUPPORT TRACE" lines (support_dispatch.cpp Trace)
void Log(const char* f,...) noexcept {if(f && std::strncmp(f,"SUPPORT TRACE",13)==0)++traceLines;}
unsigned char objects[32][0x3000]{},controls[32][32]{},seats[32][edf::kSeatStride*14]{};
int made=0,deleted=0,boardRequests=0,activated=0,orders=0,followed=0,netRequests=0,releases=0,routeOrders=0;
int held=0;bool transactionActive=false;
// Observe the production ownership adapter boundary without replacing dispatcher creation/boarding/rollback.
unsigned pvpAttachCalls=0,pvpForgetCalls=0,pvpAttachedCount=0,pvpLifecycle=0,pvpAttachOrder=0,pvpSeatOrder=0,pvpActivateOrder=0;
bool pvpAttachOk=true,pvpAttachSawEveryObject=false;
int pvpSupportMode=0;
int PvpSupportTeam(const char*) noexcept {return pvpSupportMode;}
ObjRef pvpAttachedRefs[support_net::kMaxUnits]{};
std::vector<ObjRef> pvpForgotten;
bool PvpAttachSupportPlan(const support_net::Plan& plan,const ObjRef* refs,unsigned count) noexcept {
    ++pvpAttachCalls;pvpAttachedCount=count;pvpAttachOrder=++pvpLifecycle;
    pvpAttachSawEveryObject=refs && count==plan.count && count<=support_net::kMaxUnits;
    for(unsigned i=0;i<count && i<support_net::kMaxUnits;++i) {
        pvpAttachedRefs[i]=refs[i];
        pvpAttachSawEveryObject=pvpAttachSawEveryObject && refs[i].obj && refs[i].ctrl;
    }
    return pvpAttachOk && pvpAttachSawEveryObject;
}
void PvpForgetSupportObject(const ObjRef& ref) noexcept {
    if(ref.obj){++pvpForgetCalls;pvpForgotten.push_back(ref);}
}
bool SupportTransactionActive(std::uint64_t) noexcept {return transactionActive;}
bool peersNew=true;
bool SupportPeersAcceptVariants() noexcept {return peersNew;}
std::set<const void*> heldObjects;
bool HoldSupportSoldier(const ObjRef& ref,bool hold) noexcept {
    held+=hold ? 1 : -1;
    if(hold)heldObjects.insert(ref.obj);else heldObjects.erase(ref.obj);
    return true;
}
bool allReady=true,terrain=true,nativeFail=false;
#ifndef SUPPORT_ROUTE_NATIVE_TEST
Sea terrainSea=Sea::land;float waterSurface=0;
Sea SeaAt(float,float,float* out) noexcept {*out=waterSurface;return terrainSea;}
#endif
// Submarine carrier (catalog 16): production uses the real planned hull and pilot transaction below.
int subLaunches=0;unsigned char subHull[16]{};
unsigned char* SubLaunch(const float*,const float*) noexcept {++subLaunches;return subHull;}
bool SupportSubCatalog(int id) noexcept {return id==16;}
npc::navigation::Result routeResult=npc::navigation::Result::pending;
ObjRef Make(bool vehicle=false) noexcept {
    const int slot=made++;auto* o=objects[slot];std::memset(o,0,sizeof(objects[slot]));
    Put<void*>(o,kSelfCtrl,controls[slot]);Put<long>(controls[slot],8,1);Put<long>(controls[slot],12,1);
    if(vehicle){Put<void*>(o,kSeats,seats[slot]);Put<std::uint64_t>(o,kSeatCount,3);std::memset(seats[slot],0,sizeof(seats[slot]));}
    return ObjRef::Of(o);
}
ObjRef submarineParent{};bool subBindOk=true,subActivateOk=true,subDeleteOk=true;
int subActivated=0,subDeleted=0,dronePrepared=0,droneActivated=0,droneDeleted=0;
bool PlanSupportSubmarine(const float* target,const float*,float* matrix,float* afloat) noexcept {
    float surface=0;
    if(!allReady || SeaAt(0,0,&surface)!=Sea::water || !target || !matrix || !afloat)return false;
    const float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,target[0],-40,target[2],1};
    std::memcpy(matrix,m,sizeof(m));*afloat=10;return true;
}
bool ValidateSupportSubmarine(const float*,float) noexcept {float surface=0;return allReady && SeaAt(0,0,&surface)==Sea::water;}
unsigned char* PrepareSupportSubmarine(const float* matrix,float) noexcept {
    if(nativeFail)return nullptr;
    ++subLaunches;submarineParent=Make(true);auto* object=static_cast<unsigned char*>(const_cast<void*>(submarineParent.obj));
    std::memcpy(object+kPosition,matrix+12,12);return object;
}
bool ActivateSupportSubmarine(const ObjRef&) noexcept {++subActivated;return subActivateOk;}
bool DeleteSupportSubmarine(const ObjRef&) noexcept {++subDeleted;return subDeleteOk;}
bool BindSupportSubmarineOwner(const ObjRef&,const unsigned char[32]) noexcept {return subBindOk;}
std::uint64_t SubmarineSupportKey(const ObjRef& parent) noexcept {return parent.obj ? 0xAABBCCDDu : 0;}
bool ResolveSupportSubmarine(std::uint64_t key,ObjRef* out) noexcept {
    if(!out || key!=0xAABBCCDDu || !submarineParent)return false;*out=submarineParent;return true;
}
bool AddSupportSubmarineDrone(const ObjRef&,const ObjRef&) noexcept {return true;}
bool SupportDroneReady() noexcept {return allReady;}
unsigned char* PrepareSupportDrone(const float* matrix,const float*,const void*) noexcept {
    if(nativeFail)return nullptr;++dronePrepared;const auto object=Make(true);
    auto* raw=static_cast<unsigned char*>(const_cast<void*>(object.obj));std::memcpy(raw+kPosition,matrix+12,12);return raw;
}
bool ActivateSupportDrone(const ObjRef&) noexcept {++droneActivated;return true;}
bool DeleteSupportDrone(const ObjRef&) noexcept {++droneDeleted;return true;}
bool PvpRefreshSupportOwner(support_net::Plan* plan) noexcept {
    if(!plan || pvpSupportMode<0)return false;plan->team=static_cast<unsigned>(pvpSupportMode);return true;
}
bool carrierReady=true,carrierSlotReady=true,carrierReserveOk=true;
int carrierPrepared=0,carrierConfirmed=0,carrierCancelled=0,carrierReserved=0,carrierRecalls=0,carrierTransactionsCancelled=0;
ObjRef carrierDriver{};bool carrierNetworkAccepted=true;
unsigned carrierNetworkKind=99,carrierNetworkSlot=99;unsigned char carrierNetworkParent[32]{};float carrierNetworkAnchor[3]{};
bool SupportDroneReady(SupportDroneKind) noexcept {return allReady;}
bool CarrierSupportDroneReady(const ObjRef& parent,SupportDroneKind kind,unsigned slot) noexcept {
    return carrierReady && parent.obj && static_cast<unsigned>(kind)<3 && slot<6;
}
bool CarrierSupportSlotAvailable(const ObjRef&,unsigned) noexcept {return carrierSlotReady;}
bool ReserveCarrierSupportDrone(const ObjRef&,unsigned,std::uint64_t) noexcept {++carrierReserved;return carrierReserveOk;}
unsigned char* PrepareCarrierSupportDrone(const float* matrix,const float* anchor,const ObjRef& parent,SupportDroneKind,unsigned,
                                        std::uint64_t,bool) noexcept {
    ++carrierPrepared;return PrepareSupportDrone(matrix,anchor,parent.obj);
}
bool ConfirmCarrierSupportDrone(const ObjRef&,unsigned,std::uint64_t) noexcept {++carrierConfirmed;return true;}
bool CancelCarrierSupportDrone(const ObjRef&,unsigned,std::uint64_t) noexcept {++carrierCancelled;return true;}
void CancelCarrierSupportTransaction(std::uint64_t) noexcept {++carrierTransactionsCancelled;}
bool SupportDroneLeft(const ObjRef&) noexcept {return false;}
int ApplyCarrierSupportRecall(const ObjRef&) noexcept {++carrierRecalls;return 1;}
bool PvpNativeSupportDriver(const ObjRef&,ObjRef* out) noexcept {if(!out || !carrierDriver)return false;*out=carrierDriver;return true;}
bool PvpMakeInheritedSupportPlan(const ObjRef&,support_net::Plan* plan) noexcept {
    if(!plan || pvpSupportMode<0)return false;
    if(!plan->requester[0])plan->requester[0]=0x55;
    plan->team=static_cast<unsigned>(pvpSupportMode);return true;
}
std::uint64_t SupportSourceKey(const ObjRef& parent) noexcept {return parent.obj ? 0xAABBCCDDu : 0;}
bool ResolveSupportSource(std::uint64_t key,ObjRef* out) noexcept {return ResolveSupportSubmarine(key,out);}
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out) noexcept {
    if(!object || !out)return false;std::memset(out,0,32);out[0]=0x55;
    if(object==submarineParent.obj)out[0]=0x66;out[12]=5;return true;
}
// A player's requester ID (net_object_ref.h), the one the resolver below finds the driver by.
bool PlayerRequesterOf(const ObjRef& who,unsigned char out[32]) noexcept {
    std::memset(out,0,32);if(!who.obj)return false;
    out[0]=0x55;out[12]=5;return true;
}
bool ResolveMarkIdentities(const unsigned char* marker,const unsigned char* target,MarkIdentities* out) noexcept {
    if(!out)return false;*out={};
    if(target && target[0]==0x66)out->target=submarineParent;
    if(marker && marker[0]==0x55){out->player=carrierDriver;out->puid=const_cast<char*>("client");}
    return true;
}
bool SupportCommandRequesterMatches(void* puid,const char* authenticated) noexcept {
    return puid && authenticated && !std::strcmp(static_cast<const char*>(puid),authenticated);
}
bool SubmitCarrierDroneNetworkRequest(const unsigned char* parent,unsigned kind,unsigned slot,const float* anchor) noexcept {
    if(parent)std::memcpy(carrierNetworkParent,parent,32);if(anchor)std::memcpy(carrierNetworkAnchor,anchor,12);
    carrierNetworkKind=kind;carrierNetworkSlot=slot;return carrierNetworkAccepted;
}
SupportPlan preparedSubPlan{};std::uint64_t preparedSubToken=0;
std::uint64_t SubmitPreparedSupportPlan(const SupportPlan& plan) noexcept {preparedSubPlan=plan;return ++preparedSubToken;}
unsigned char* PlayerHuman() noexcept {return objects[31];}
int SupportAirCallCount() noexcept {return 21;}
// A sortie's second wave (airstrike.cpp, not linked here): what the dispatcher queued and settled.
int followUpsQueued=0,followUpsSettled=0;std::uint32_t followUpRequest=~0u,followUpSettledRequest=0;bool followUpOut=false;
void SupportFollowUpQueue(int,const float*,std::uint32_t request) noexcept {++followUpsQueued;followUpRequest=request;}
void SupportFollowUpSettle(std::uint32_t request,bool out) noexcept {++followUpsSettled;followUpSettledRequest=request;followUpOut=out;}
const wchar_t* SupportAirCallName(int) noexcept {return L"Heli";}
const wchar_t* airKeys[21]={L"INTERCEPTOR",L"INTERCEPTOR_F",L"STRIKE",L"STRIKE_F",L"MULTIROLE",L"MULTIROLE_F",L"FIGHTER",L"FIGHTER_F",
    L"CARRIER",L"CARRIER_F",L"HELI",L"HELI_F",L"BLAST_CARRIER",L"BLAST_CARRIER_F",L"DOLL_CARRIER",L"DOLL_CARRIER_F",L"SUB",
    L"GUNSHIP",L"GUNSHIP_F",L"MEDIC_HELI",L"MEDIC_HELI_F"};
const wchar_t* SupportAirCallKey(int index) noexcept {return index>=0 && index<21 ? airKeys[index] : nullptr;}
std::uint32_t SupportAirCallPt(int index) noexcept {return index>=0 && index<21 ? 1800u : 0u;}
// The strikes (airstrike.cpp kStrikes, not linked here): a barrage and the ultimate, after the airdrops (36, 37).
const wchar_t* strikeKeys[2]={L"BARRAGE_ROCKETS",L"ULTIMATE"};
int SupportStrikeCount() noexcept {return 2;}
const wchar_t* SupportStrikeKey(int k) noexcept {return k>=0 && k<2 ? strikeKeys[k] : nullptr;}
const wchar_t* SupportStrikeName(int k) noexcept {return k==0 ? L"火箭齐射" : L"天罚";}
std::uint32_t SupportStrikePt(int k) noexcept {return k==0 ? 2000u : k==1 ? 15000u : 0u;}
int SupportStrikeBarrage(int k) noexcept {return k==0 ? 0 : -1;}
int strikesStarted=0;bool strikeStarts=true;
bool SupportStrikeAt(int,const float*,wchar_t* note,std::size_t capacity) noexcept {
    ++strikesStarted;if(note && capacity)note[0]=0;return strikeStarts;
}
support::Refusal PreviewAirRoute(const SupportAircraft&,const float*,const float*,support::Route* route) noexcept {
    *route={{-1400,150,0},{1,0,0}};return support::Refusal::none;
}
int warnedDeployments=0;
void WarnDeployment(const support_net::Plan&) noexcept {++warnedDeployments;}
// The pool (support_points.cpp, not linked here): the real rules (support_points_logic.h) on a fixture's pool.
support_points::Settings fixturePt{1000000,1000000,1.0f};
support_points::Pool fixturePool;support_points::Ledger fixtureLedger;
struct PoolStart { PoolStart(){fixturePool.Start(fixturePt);} } poolStart;
std::int32_t SupportPoints() noexcept {return fixturePool.points;}
std::int32_t SupportPointsPrice(std::uint32_t pt) noexcept {return support_points::Price(pt,fixturePt.scale);}
// The held price (support_points.cpp SupportPointsHold); fixtureHoldRefused: as a guest whose earlier changes the host has
// not confirmed (support_points_net.h kChangesMost), the pool holding enough.
std::int32_t fixtureHeld=0;bool fixtureHoldRefused=false;int fixtureHoldsDropped=0;
bool SupportPointsHold(std::int32_t price) noexcept {
    if(price<=0)return true;
    if(fixtureHeld>0 || fixtureHoldRefused || !fixturePool.Spend(price))return false;
    fixtureHeld=price;return true;
}
void SupportPointsKeepHeld() noexcept {fixtureHeld=0;}
void SupportPointsOweHeld(std::uint32_t request) noexcept {
    if(fixtureHeld>0)fixtureLedger.Keep(request,fixtureHeld);
    fixtureHeld=0;
}
void SupportPointsDropHeld() noexcept {
    if(fixtureHeld>0){fixturePool.Unspend(fixtureHeld);++fixtureHoldsDropped;}
    fixtureHeld=0;
}
void SupportPointsRefund(std::int32_t price) noexcept {fixturePool.Refund(price,fixturePt);}
void SupportPointsSettle(std::uint32_t request,bool back) noexcept {
    const std::int32_t price=fixtureLedger.Settle(request);
    if(back)fixturePool.Refund(price,fixturePt);
}
bool soloHost=false;
bool SupportSoloHostWorld() noexcept {return soloHost;}
bool SupportAircraftSpec(int id,SupportAircraft* out) noexcept {
    if(id<0 || id>=21 || id==16)return false;
    *out={-1,static_cast<int>(HeliBody::eros506),1,300,false};return true;
}
int plannedAircraft=0,plannedHeli=-2;
support::Refusal planRefusal=support::Refusal::none;
int plannedTransports=0;
// The one air planner (support_aircraft.h PlanAirSupport): every entry's spec, a transport's too (its plane no jet or heli row).
support::Refusal PlanAirSupport(const SupportAircraft& spec,const float*,const float*,support::Route* route) noexcept {
    plannedAircraft=spec.count;plannedHeli=spec.heli;
    if(spec.transportPlane || spec.heli==static_cast<int>(HeliBody::transport410))++plannedTransports;
    *route={{-1400,150,0},{1,0,0}};return !terrain ? support::Refusal::noEntry : planRefusal;
}
SupportAircraft lastPrepared{};std::wstring lastAircraftVariant;
unsigned char* PrepareSupportAircraft(const SupportAircraft& spec,const float* matrix,const wchar_t* variant) noexcept {
    lastPrepared=spec;lastAircraftVariant=variant ? variant : L"";
    auto ref=Make(true);std::memcpy(static_cast<unsigned char*>(const_cast<void*>(ref.obj))+kPosition,matrix+12,12);
    auto* hull=static_cast<unsigned char*>(const_cast<void*>(ref.obj));
    // The transports' hulls seat their pilot and twelve (tools/make_jets.py TRANSPORT_*).
    if(spec.transportPlane || spec.heli==static_cast<int>(HeliBody::transport410))Put<std::uint64_t>(hull,kSeatCount,13);
    return hull;
}
// The transports' entry points (transport.cpp, jet.cpp, heli.cpp, airstrike.cpp, jet_spawn.cpp, support_net.cpp).
int delivered=0,deliveredSquads=0,paradrops=0,ferries=0,jetWithdrawals=0,heliLeaves=0;
const void* deliveredTops[3]{};const void* deliveredHull=nullptr;
bool transportReady=true,peersTransports=true,heliHull=false,driverAboard=true;
bool deliveredHere=false;
bool TransportDeliver(const void* v,const void* const* tops,int n,const float*,bool here) noexcept {
    ++delivered;deliveredHere=here;deliveredSquads=n;deliveredHull=v;for(int i=0;i<n && i<3;++i)deliveredTops[i]=tops[i];return n>0;
}
bool TransportParadrop(const void*,const float*) noexcept {++paradrops;return true;}
float ferryHeading[3]{};
bool JetFerry(const void*,const float*,const float* heading) noexcept {++ferries;ferryHeading[0]=heading[0];ferryHeading[2]=heading[2];return true;}
bool JetWithdrawNow(const void*,const char*) noexcept {++jetWithdrawals;return true;}
bool HeliStartLeaving(const void*) noexcept {++heliLeaves;return true;}
// The container airdrops (airdrop.cpp).
int airdrops=0;bool airdropReady=true,airdropTakes=true;SupportVehicleKind lastAirdrop{};const void* airdropPlane=nullptr;
bool AirdropReady(SupportVehicleKind) noexcept {return airdropReady;}
bool AirdropBegin(const void* plane,SupportVehicleKind kind,const float*) noexcept {
    ++airdrops;lastAirdrop=kind;airdropPlane=plane;return airdropTakes;
}
bool IsHelicopter(const void*) noexcept {return heliHull;}
#ifndef SUPPORT_ROUTE_NATIVE_TEST
bool NpcDriver(const unsigned char*) noexcept {return driverAboard;}
#endif
bool SupportAircraftReady(const SupportAircraft&) noexcept {return transportReady;}
bool SupportPeersAcceptTransports() noexcept {return peersTransports;}
bool peersLoadout=true;
bool SupportPeersAcceptLoadout() noexcept {return peersLoadout;}
bool lastAirborne=false;
bool ActivateSupportAircraft(unsigned char*,const SupportAircraft&,const float*,bool airborne) noexcept {
    if(!pvpActivateOrder)pvpActivateOrder=++pvpLifecycle;
    ++activated;lastAirborne=airborne;return true;
}
bool peersAirborne=true,peersRescue=true;
bool SupportPeersAcceptAirborne() noexcept {return peersAirborne;}
bool SupportPeersAcceptRescue() noexcept {return peersRescue;}
// heli.cpp's half of the sea rescue: what the dispatcher hands it.
int rescueDeployed=0,rescueFailed=0;unsigned char* rescueHeli=nullptr;float rescueTarget[3]{};bool rescueFlown=false;
ObjRef rescueRequester{},hostRequester{};
wchar_t rescueWhy[160]{};
void RescueHeliDeployed(unsigned char* v,const float* target,bool flown,const ObjRef& requester) noexcept {
    ++rescueDeployed;rescueHeli=v;std::memcpy(rescueTarget,target,12);rescueFlown=flown;rescueRequester=requester;
}
bool SupportTransactionRequester(std::uint64_t,ObjRef* out) noexcept {*out=hostRequester;return static_cast<bool>(hostRequester);}
bool SupportAuthenticatedRequester(std::uint32_t,ObjRef* out) noexcept {*out=hostRequester;return static_cast<bool>(hostRequester);}
// A submarine carrier: its deck nearest a point (crew.h SubDeck), when one is out; the takeoff planner's answer.
bool carrierOut=false;float carrierDeck[3]={300,193,-40};
bool SubDeck(const float* from,float* deck) noexcept {
    if(!carrierOut)return false;
    deck[0]=carrierDeck[0]+(from[0]>carrierDeck[0] ? 1.0f : -1.0f);deck[1]=carrierDeck[1];deck[2]=carrierDeck[2];return true;
}
// Where helicopters stood this mission (heli.cpp RescueTakeoffPads): the fixture's pads.
int padCount=0;float padAt[2][3]={{-200,12,40},{600,30,-300}};
int RescueTakeoffPads(float (*out)[3],int most) noexcept {
    int n=0;for(;n<padCount && n<most;++n)std::memcpy(out[n],padAt[n],12);return n;
}
support::Refusal takeoffRefusal=support::Refusal::none;int takeoffSpots=-1;
support::Refusal PlanTakeoffSupport(const SupportAircraft&,const float* target,const float (*spots)[3],int count,support::Route* route) noexcept {
    takeoffSpots=count;
    if(takeoffRefusal!=support::Refusal::none || count<1)return support::Refusal::noEntry;
    *route={{spots[0][0],spots[0][1]+support::kTakeoffLift,spots[0][2]},{0,0,0}};
    const float d=support::FlatDistance(spots[0],target);route->heading[0]=(target[0]-spots[0][0])/d;route->heading[2]=(target[2]-spots[0][2])/d;
    return support::Refusal::none;
}
void RescueRequestFailed(const wchar_t* why) noexcept {++rescueFailed;_snwprintf_s(rescueWhy,_TRUNCATE,L"%ls",why ? why : L"");}
bool DeleteSupportAircraft(const ObjRef& ref) noexcept {if(ref)++deleted;return true;}
bool aircraftLeft=false;
bool SupportAircraftLeft(const ObjRef&) noexcept {return aircraftLeft;}
bool SupportSoldiersReady() noexcept {return allReady;}
const wchar_t* soldierFailure=L"支援兵员创建发生异常，本局已停用";
const wchar_t* SupportSoldierFailureText() noexcept {return soldierFailure;}
std::uint32_t lastResources[16]{};int resourceCount=0;bool lastLocal=false;const unsigned char* lastId=nullptr;
std::wstring lastLooks[16];
bool ApplySupportSoldierResource(const float* matrix,std::uint32_t resource,const unsigned char* id,bool local,ObjRef* out,
                                 const wchar_t* look) noexcept {
    if(nativeFail)return false;
    if(resourceCount<16){lastLooks[resourceCount]=look ? look : L"";lastResources[resourceCount++]=resource;}lastLocal=local;lastId=id;
    *out=Make();std::memcpy(static_cast<unsigned char*>(const_cast<void*>(out->obj))+kPosition,matrix+12,12);return true;
}
bool CreateSupportSoldierUnregistered(const float* matrix,std::uint32_t resource,const unsigned char* id,bool local,ObjRef* out,
                                      const wchar_t* look) noexcept {
    return ApplySupportSoldierResource(matrix,resource,id,local,out,look);
}
int seatNow=0,registeredBeforeSeat=0;bool seatFail=false;
int NpcSeatCrewNow(unsigned char* v,unsigned char* const* crew,int count) noexcept {
    if(!pvpSeatOrder)pvpSeatOrder=++pvpLifecycle;
    ++seatNow;if(seatFail)return 0;
    int seated=0;
    for(int i=0;i<count && i<static_cast<int>(SeatCount(v));++i){Put<const void*>(SeatAt(v,static_cast<unsigned>(i)),kSeatRider,crew[i]);++seated;}
    return seated;
}
bool DeriveSupportSoldierNetId(const void*,unsigned,unsigned char*) noexcept {return true;}
bool FollowSupportSoldier(const ObjRef&,const ObjRef&) noexcept {++followed;return true;}
bool DeleteSupportSoldier(const ObjRef& ref) noexcept {
    if(ref) {
        ++deleted;
        // The native adapter's ReleaseWeak also clears its owned/held record on deletion.
        if(heldObjects.erase(ref.obj) && held>0)--held;
    }
    return true;
}
int registered=0;bool registerSupportOk=true;
bool RegisterSupportObject(const void*,const unsigned char*) noexcept {++registered;if(!seatNow)++registeredBeforeSeat;return registerSupportOk;}
bool SupportVehicleReady(SupportVehicleKind,SupportCrewMode) noexcept {return true;}
std::wstring lastHullVariant;
unsigned char* SpawnSupportVehicle(SupportVehicleKind,SupportCrewMode,const float*,const float*,const void*,const wchar_t* variant) noexcept {
    lastHullVariant=variant ? variant : L"";return nullptr;
}
// support_variants.cpp: the variant files this machine has, the ones it was told it lacks, and the room's peers.
std::set<std::wstring> variantFiles;std::vector<std::wstring> missingNoted;
bool SupportVariantReady(const wchar_t* file) noexcept {return file && variantFiles.count(file)!=0;}
void NoteMissingVariant(const wchar_t* file,const char*) noexcept {missingNoted.push_back(file);}
void SupportVariantHello(std::uint32_t* ext,unsigned char*) noexcept {if(ext)*ext=support_net::kExtVariants;}
bool peersApplyVariants=true,peersHaveFiles=true;
bool SupportPeersApplyVariants() noexcept {return peersApplyVariants;}
bool peersApplyBackpacks=true;
bool SupportPeersApplyBackpacks() noexcept {return peersApplyBackpacks;}
bool SupportPeersHaveVariantFile(std::uint64_t) noexcept {return peersHaveFiles;}
bool DeleteSupportVehicle(unsigned char*) noexcept {++deleted;return true;}
support_net::Hooks configuredHooks{};int netConfigured=0,gatesInstalled=0;
bool creationSeen=false,gateBeforeNetwork=false;
MissionPlayerCreated installedCreatedObserver=nullptr;
bool InstallMissionParticipantGate(MissionParticipantAdmission,MissionPlayerCreated created) noexcept {
    ++gatesInstalled;installedCreatedObserver=created;return true;
}
bool MissionParticipantGateReady() noexcept {return true;}
bool SupportMissionPlayerAllowed(int) noexcept {return true;}
void NoteSupportMissionPlayerCreated(int,const ObjRef&) noexcept {creationSeen=true;}
bool MissionParticipantCreationsMatch(const ObjRef*,unsigned) noexcept {return true;}
bool ReadMissionParticipants(void**,unsigned,unsigned*,unsigned*) noexcept {return true;}
void ConfigureSupportNet(const support_net::Hooks& hooks) noexcept {
    configuredHooks=hooks;++netConfigured;gateBeforeNetwork=gatesInstalled>0;creationSeen=false;
}
bool submitReady=true;unsigned noticeRequest=0;
std::uint64_t lastNetLoadout=0;
bool SubmitSupportRequest(int,const float*,wchar_t* note,std::size_t size,std::uint64_t loadout) noexcept {
    ++netRequests;lastNetLoadout=loadout;
    if(!submitReady){_snwprintf_s(note,size,_TRUNCATE,L"联机扩展尚未就绪");return false;}
    if(configuredHooks.notice)configuredHooks.notice(++noticeRequest,support_net::RequestStatus::accepted);
    _snwprintf_s(note,size,_TRUNCATE,L"request queued");return true;
}
int protocolTicks=0;
void SupportNetTick() noexcept {++protocolTicks;}
void SeeFrame(const void*) noexcept {}
void PlayAreaTick() noexcept {}
void ResetSupportNet() noexcept {creationSeen=false;}
bool ValidateMissionCrewPlan(const SupportPlan&) noexcept {return false;}
bool ApplyMissionCrewPlan(std::uint64_t,const SupportPlan&,bool) noexcept {return false;}
void DestroyMissionCrewPlan(std::uint64_t) noexcept {}
void InstallMissionCrewSupport() noexcept {}
void ResetMissionCrewSupport() noexcept {}
void ReportSupportFailure(std::uint64_t id) noexcept {Destroy(id);}
namespace support_net {bool ValidPlan(const Plan& p,bool) noexcept {return p.count>0 && p.count<=kMaxUnits;}}
int NpcBoardCrew(unsigned char*,unsigned char* const*,int count) noexcept {++boardRequests;return count;}
#ifndef SUPPORT_ROUTE_NATIVE_TEST
bool NpcPrepareVehicleRoutePost(unsigned char*,const float*,float) noexcept {return true;}
#endif
bool NpcReleaseVehicleCrew(unsigned char*) noexcept {++releases;return true;}
// The stock get-off before a seated soldier is deleted (support_dispatch.cpp Unseat): the seat cleared, counted.
int unseated=0;
bool NpcMoveSeat(unsigned char* v,unsigned seat,int to) noexcept {
    if(to>=0)return false;
    ++unseated;
    auto* h=At<unsigned char*>(SeatAt(v,seat),kSeatRider);
    Put<const void*>(SeatAt(v,seat),kSeatRider,nullptr);
    if(h)Put<void*>(h,kSoldierVehicle,nullptr);
    return true;
}
int squadOrders=0;mapcmd::Command lastSquadOrder{};
bool NpcSquadSetOrder(const void*,const mapcmd::Command& c,bool) noexcept {++squadOrders;lastSquadOrder=c;return true;}
bool SquadCommand(const void*,const Command&) noexcept {++orders;return true;}
bool NpcPrepareSquadRoute(unsigned char*,const float*,float) noexcept {++routeOrders;return true;}
bool NpcFinishSquadRoute(unsigned char*,const float*) noexcept {return true;}
int heliCommands=0;
bool HeliCommand(const void*,const Command&,const ObjRef&) noexcept {++heliCommands;return true;}
bool JetCommand(const void*,const Command&,const ObjRef&) noexcept {return true;}
bool areaMeasured=true,areaGround=true;
bool PlayAreaMeasured() noexcept {return areaMeasured;}
PlayArea MapPlayArea() noexcept {return {{-1500,-1500},{1500,1500},areaGround,0,true};}
bool MapGroundNear(float,float,float,float* y,bool) noexcept {*y=0;return terrain;}
#ifndef SUPPORT_ROUTE_NATIVE_TEST
float MapRay(const float*,const float*,float*) noexcept {return -1;}
#endif
#ifndef SUPPORT_ROUTE_NATIVE_TEST
npc::navigation::Result GroundNavigate(npc::navigation::RouteState&,const float* from,const float*,float,std::uint64_t,float* waypoint,npc::navigation::Profile) noexcept {
    std::memcpy(waypoint,from,12);return routeResult;
}
#endif
}
#ifndef SUPPORT_ROUTE_NATIVE_TEST
int main() {
    using namespace crew;
    int checks=0;const auto check=[&](bool condition,const char* why){++checks;if(!condition){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}};
    float target[3]={0,0,0};wchar_t note[128];
    ResetSupportDispatch();
    check(configured && netConfigured==1 && gateBeforeNetwork,"mission preload installs admission gate before configuring support networking");
    check(configuredHooks.participants==&ReadMissionParticipants && configuredHooks.admissionReady==&MissionParticipantGateReady,
          "network quorum reads actual mission actors and the verified upper admission gate");
    check(configuredHooks.ownChannel && configuredHooks.ownChannel(38) && !configuredHooks.ownChannel(29) &&
          !configuredHooks.ownChannel(0) && !configuredHooks.ownChannel(21) && !configuredHooks.ownChannel(36),
          "the protocol is told which catalog entry is the sea rescue (its own channel and cooldown)");
    check(installedCreatedObserver==&NoteSupportMissionPlayerCreated && configuredHooks.createdMatches==&MissionParticipantCreationsMatch,
          "successful native actor creation observer and exact identity matcher are both wired before creation");
    installedCreatedObserver(0,ObjRef{});
    check(SupportCallAt(0,target,note,128) && made==0,"request only queues: no objects materialise on click");
    SupportDispatchTick();
    check(creationSeen && netConfigured==1,"first frame does not reset creation observations already collected during mission start");
    // 2026-10-09: air support flies in from the edge. Created in the air, its real crew made inside it and seated at
    // once, the flight on at spawn: no runway, no walk aboard, no takeoff.
    check(made==2 && boardRequests==0 && seatNow==1 && activated==1 && lastAirborne,
          "hull and real crew created, crew seated at once, airborne flight starts at spawn");
    check(At<float>(objects[0],kPosition)==-1400 && At<float>(objects[0],kPosition+4)==150,"hull starts in the air at the planned edge");
    check(At<float>(objects[1],kPosition+4)==150 && At<const void*>(seats[0],kSeatRider)==objects[1],
          "the real soldier is made inside its aircraft and sits in the driver's seat at spawn");
    check(IsSupportAirborneAircraft(deployments[0].plan.units[0].resourceId),"the plan marks the hull airborne");
    fixtureMs+=100;SupportDispatchTick();
    fixtureMs+=100;SupportDispatchTick();check(activated==1,"activation is once, not every frame");
    // 2026-10-09 16:59: out of fuel, two support helis flew off and hung at the map's edge for the rest of the mission
    // (the plugin's reaps never delete under real soldiers). Its owner retires it: crew first, the hull once settled.
    check(deleted==0,"a flying support aircraft is never retired");
    aircraftLeft=true;fixtureMs+=1;SupportDispatchTick();
    check(deleted==1 && deployments[0].used,"left: the dispatcher deletes the real crew it made, the hull not yet");
    fixtureMs+=1;SupportDispatchTick();
    check(deleted==1,"the hull waits for the game to take it as empty (a delete before that leaves it in team 5's set)");
    Put<std::int32_t>(objects[0],kTeam,kTeamVehicle);fixtureMs+=1;SupportDispatchTick();
    check(deleted==2,"once empty (team 5) the hull is deleted");
    objects[0][kDead]=1;objects[1][kDead]=1;fixtureMs+=1;SupportDispatchTick();
    check(deleted==2 && !deployments[0].used,"gone: nothing deleted twice, the deployment row is free");
    aircraftLeft=false;
    auto invalid=deployments[0].plan;invalid.units[1].role=0;
    check(!Validate(invalid),"a manifest cannot detach the specified crew from its aircraft");
    invalid=deployments[0].plan;invalid.count=1;
    check(!Validate(invalid),"a manned aircraft manifest cannot omit its real pilot");
    // No cooldown after a delivery (2026-10-25, the user: 「支援调度 30 秒的全局冷却 删掉」): the next call is taken at once.
    check(SupportCallAt(0,target,note,128) && SupportCallReadiness().state==SupportReady::planning,
          "a call right after a delivery is taken (no cooldown)");
    ResetSupportDispatch();made=deleted=boardRequests=activated=0;terrain=false;fixtureMs+=40000;
    {const std::int32_t pt=SupportPoints();
    followUpsQueued=0;
    check(SupportCallAt(0,target,note,128),"invalid terrain request can be queued for explicit validation");
    check(SupportPoints()==pt-1800,"the map's pt (2026-10-26): a call is paid when it is taken");
    check(followUpsQueued==0,"a sortie's second wave waits for its first to be made (it is paid with it)");
    SupportDispatchTick();check(!made && !offlinePending,"no safe entry refuses without spawning at target");
    check(SupportPoints()==pt,"its plan refused: its pt back");
    check(followUpsQueued==0,"its plan refused: no second wave either (2026-10-27 review: a refunded first wave's second came free)");
    ResetSupportDispatch();terrain=true;made=0;fixtureMs+=40000;
    check(SupportCallAt(0,target,note,128),"a call on good terrain is queued");
    check(followUpsQueued==0,"still planned: no second wave yet");
    SupportDispatchTick();
    check(made==2 && SupportPoints()==pt-1800,"delivered: its pt stay spent");
    check(followUpsQueued==1 && followUpRequest==0,"made: its second wave's wait starts");
    ResetSupportDispatch();made=0;fixtureMs+=40000;fixturePool.points=1000;
    check(!SupportCallAt(0,target,note,128) && !offlinePending && fixturePool.points==1000 && std::wcsstr(note,L"1800"),
          "not enough pt: refused with the price, nothing asked, nothing taken");
    followUpsQueued=0;
    check(SupportCallUnpaidAt(0,target,note,128) && offlinePending && fixturePool.points==1000,"a second wave is not paid");
    SupportDispatchTick();
    check(made==2 && followUpsQueued==0,"a second wave made brings no wave of its own");
    ResetSupportDispatch();made=0;fixtureMs+=40000;fixturePool.Start(fixturePt);
    const int started=strikesStarted;
    check(SupportCallAt(36,target,note,128) && strikesStarted==started+1 && !offlinePending && !made &&
          fixturePool.points==fixturePt.start-2000,"a strike starts here, never planned or sent, and is paid");
    strikeStarts=false;
    check(!SupportCallAt(37,target,note,128) && fixturePool.points==fixturePt.start-2000,"a strike that does not start costs nothing");
    strikeStarts=true;
    check(warnedDeployments>0,"a deployment made shows its route (support_warning.h)");
    SupportPlan strikePlan{};
    check(Plan(36,target,0,&strikePlan)==support_net::PlanResult::refused,"a host never plans a strike");
    fixturePool.Start(fixturePt);ResetSupportDispatch();terrain=false;made=0;fixtureMs+=40000;}
    // 2026-10-09 16:52:26: an air call made 50 ms before the play area was measured was refused ("no clear air corridor"),
    // the same call worked later. Until the area is in, a request waits; then it is planned as usual.
    ResetSupportDispatch();terrain=true;areaMeasured=false;made=0;
    check(SupportCallAt(0,target,note,128),"a call before the area is measured is queued");
    SupportDispatchTick();fixtureMs+=16;SupportDispatchTick();
    check(!made && offlinePending && SupportCallReadiness().state==SupportReady::planning,"it waits (dispatching), never refused for it");
    areaMeasured=true;fixtureMs+=16;SupportDispatchTick();
    check(made==2 && !offlinePending,"once the area is in, the same request is planned and flies in");
    // A map with no ground found round its centre: its ground entries cannot be planned, its air ones can (as before).
    ResetSupportDispatch();terrain=true;areaGround=false;made=0;fixtureMs+=40000;
    SupportCallAt(0,target,note,128);SupportDispatchTick();
    check(made==2,"no ground measured: an air call still flies in");
    ResetSupportDispatch();made=0;routeResult=npc::navigation::Result::moving;
    SupportCallAt(21,target,note,128);SupportDispatchTick();
    check(!made && !offlinePending,"...a ground entry is refused");
    areaGround=true;
    ResetSupportDispatch();terrain=true;nativeFail=true;made=deleted=0;
    SupportCallAt(0,target,note,128);SupportDispatchTick();
    check(made==1 && deleted==1 && !deployments[0].used,"partial crew construction rolls the hull back");
    ResetSupportDispatch();nativeFail=false;made=deleted=0;
    seatFail=true;activated=0;
    SupportCallAt(0,target,note,128);SupportDispatchTick();seatFail=false;
    check(deleted==2 && !deployments[0].used && activated==0,"a crew that cannot take its seats removes crew and hull before any flight");
    ResetSupportDispatch();nativeFail=false;made=deleted=0;routeResult=npc::navigation::Result::pending;
    SupportCallAt(21,target,note,128);SupportDispatchTick();
    check(!made && offlinePending,"infantry waits for full ground route before creation");
    routeResult=npc::navigation::Result::moving;++fixtureMs;SupportDispatchTick();
    check(made==4 && followed==3 && routeOrders>0 && orders==0,"verified infantry entry creates real squad with three native followers and a short-route order");
    // 2026-10-10 "我摇的人还没到目的地成棍木了": a walk-in whose route stays blocked held the squad's quiet movement where it stood.
    // Past kStuckMs without way it is let go with the point as its own guard order (its AI walks / fights on).
    {
        auto& d=deployments[0];
        routeResult=npc::navigation::Result::blocked;squadOrders=0;
        fixtureMs+=16;SupportDispatchTick();fixtureMs+=kStuckMs/2;SupportDispatchTick();
        check(!d.delivered && squadOrders==0,"a walk-in briefly without a route keeps its route");
        fixtureMs+=kStuckMs;SupportDispatchTick();
        check(d.infantry[0].released && d.delivered && squadOrders==1 && lastSquadOrder.order==mapcmd::Order::guard &&
              lastSquadOrder.at[0]==d.plan.target[0] && lastSquadOrder.at[2]==d.plan.target[2],
              "no way past kStuckMs: the squad let go to guard the point on its own");
        routeResult=npc::navigation::Result::pending;
    }
    ResetSupportDispatch();made=0;routeResult=npc::navigation::Result::pending;
    {const std::int32_t pt=SupportPoints();
    SupportCallAt(21,target,note,128);testOnline=true;SupportDispatchTick();
    check(!offlinePending && !made,"entering an online session cancels an uncommitted offline request");
    check(SupportPoints()==pt,"...and gives its pt back");
    // Online: paid once the host has it (its accepted notice inside the submit), given back when it comes to nothing.
    ResetSupportDispatch();fixtureMs+=40000;
    followUpsQueued=followUpsSettled=0;
    check(SupportCallAt(0,target,note,128) && SupportPoints()==pt-1800,"an online call is paid as the host takes it");
    check(followUpsQueued==1 && followUpRequest==noticeRequest,"its second wave is held under its open request");
    configuredHooks.notice(noticeRequest,support_net::RequestStatus::refused);
    check(SupportPoints()==pt,"refused by the host: its pt back");
    check(followUpsSettled==1 && followUpSettledRequest==noticeRequest && !followUpOut,"...and its second wave is forgotten");
    configuredHooks.notice(noticeRequest,support_net::RequestStatus::refused);
    check(SupportPoints()==pt,"settled once");
    ResetSupportDispatch();fixtureMs+=40000;
    check(SupportCallAt(0,target,note,128) && SupportPoints()==pt-1800,"another online call");
    followUpsSettled=0;
    configuredHooks.notice(noticeRequest,support_net::RequestStatus::active);
    check(followUpsSettled==1 && followUpSettledRequest==noticeRequest && followUpOut,"out: its second wave's wait starts");
    configuredHooks.notice(noticeRequest,support_net::RequestStatus::interrupted);
    check(SupportPoints()==pt-1800,"delivered: its pt stay spent, whatever comes after");
    // A strike called while an online request is open (2026-10-27 review): its own line is shown, the request is still
    // followed (its later outcome shown and settled).
    ResetSupportDispatch();fixtureMs+=40000;
    check(SupportCallAt(0,target,note,128),"an online call waiting for the host");
    const unsigned open=noticeRequest;
    check(SupportCallAt(36,target,note,128) && strikesStarted>0,"a strike meanwhile");
    SupportCallStatus(note,128);
    check(!std::wcsstr(note,L"支援调度中"),"the strike's line is shown, not the request's");
    configuredHooks.notice(open,support_net::RequestStatus::active);
    SupportCallStatus(note,128);
    check(std::wcsstr(note,L"支援已出动")!=nullptr,"the open request's outcome still reaches the HUD after the strike");
    check(fixtureHeld==0,"every held price ended with its call");
    // 2026-10-30 review: a guest whose earlier pt changes the host has not confirmed (no room for another change) is not
    // asked at all, the pool holding enough: before, its request went and the pt it then could not spend were never paid.
    ResetSupportDispatch();fixtureMs+=40000;fixturePool.Start(fixturePt);
    {const int before=netRequests;fixtureHoldRefused=true;
     check(!SupportCallAt(0,target,note,128) && netRequests==before && SupportPoints()==fixturePt.start,
           "the price not held: no request sent, nothing taken");
     check(std::wcsstr(note,L"房主确认")!=nullptr,"the HUD says why");
     fixtureHoldRefused=false;}
    // A request not taken (the transport not ready): the held price goes back as it was (nothing spent, no refund).
    ResetSupportDispatch();fixtureMs+=40000;fixturePool.Start(fixturePt);
    {const int dropped=fixtureHoldsDropped;submitReady=false;
     check(!SupportCallAt(0,target,note,128) && SupportPoints()==fixturePt.start && fixturePool.spent==0 &&
           fixtureHoldsDropped==dropped+1 && fixtureHeld==0,"not taken: the held price put back");
     submitReady=true;}
    fixturePool.Start(fixturePt);netRequests=0;}
    testOnline=false;
    ResetSupportDispatch();testOnline=true;made=0;
    check(SupportCallAt(0,target,note,128) && netRequests==1 && !made,"online click uses reliable request rather than local spawn");
    ResetSupportDispatch();made=boardRequests=0;held=0;seatNow=0;registered=0;registeredBeforeSeat=0;activated=0;SupportPlan networkPlan;
    pvpLifecycle=pvpAttachOrder=pvpSeatOrder=pvpActivateOrder=0;
    check(Plan(0,target,0,&networkPlan)==support_net::PlanResult::ready && Spawn(10,networkPlan,false),"committed online plan creates registered stand-ins");
    check(pvpAttachSawEveryObject && pvpAttachedCount==networkPlan.count && pvpAttachOrder &&
          pvpAttachOrder<pvpSeatOrder && pvpSeatOrder<pvpActivateOrder,
          "ownership is attached to every real unit after creation and before airborne boarding or activation");
    check(seatNow==1 && registered==2 && registeredBeforeSeat==0,"airborne hull and crew are registered only after the crew is seated");
    check(activated==1,"the host flies its airborne copy at once: a hull in the air never waits unflown for the barrier");
    SupportDispatchTick();
    check(held==1 && boardRequests==0 && !deployments[0].assigned,"spawn before all-peer ACK holds native AI and emits no boarding request");
    transactionActive=true;++fixtureMs;SupportDispatchTick();
    check(held==0 && boardRequests==0 && deployments[0].assigned,"all-peer active barrier releases held crew; nobody walks aboard");
    {const int before=activated;const auto seated=seatNow;
     check(Spawn(11,networkPlan,true) && seatNow==seated+1 && activated==before,"a peer's copy seats its crew identically but never flies it");}
    // A room with an older peer (no kCapAirborneAir): refused with its reason, never half in the air.
    peersAirborne=false;made=0;
    check(Plan(0,target,0,&networkPlan)==support_net::PlanResult::refused && !made,"older peer: air support refused before anything exists");
    SupportCallStatus(note,128);
    check(std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportAirNeedsUpdate))!=nullptr,"older peer: the HUD says why");
    peersAirborne=true;
    // Legacy local runway sequencing is an offline-only compatibility path.
    testOnline=false;
    {SupportPlan legacy;check(Plan(0,target,0,&legacy)==support_net::PlanResult::ready,"plan for the legacy fixture");
     legacy.units[0].resourceId=kSupportAircraftResource+legacy.catalogId;
     check(Validate(legacy),"an older host's runway plan still validates");
     ResetSupportDispatch();transactionActive=false;boardRequests=0;activated=0;seatNow=0;
     check(SpawnDeployment(30,legacy,false,false) && seatNow==0 && activated==0,"legacy hull: no seating at spawn, no flight yet");
     SupportDispatchTick();check(boardRequests==1,"legacy hull: its crew walks aboard as before");}
    // Maximum protocol-sized deployment: ownership must reach every one of eight hull/pilot pairs, beyond old small lists.
    {ResetSupportDispatch();made=deleted=held=seatNow=activated=0;SupportPlan maximum=networkPlan;maximum.count=support_net::kMaxUnits;
     for(unsigned i=0;i<maximum.count;++i) {
         maximum.units[i]=networkPlan.units[i%2];maximum.units[i].role=i%2 ? i : 0;
     }
     pvpLifecycle=pvpAttachOrder=pvpSeatOrder=pvpActivateOrder=0;
     check(SpawnDeployment(50,maximum,false,false) && made==16 && pvpAttachedCount==16 && pvpAttachSawEveryObject &&
           pvpAttachedRefs[15].obj==objects[15] && pvpAttachOrder<pvpSeatOrder && pvpSeatOrder<pvpActivateOrder,
           "all sixteen support objects are attached without truncation before the first aircraft seats or flies");
     pvpForgetCalls=0;pvpForgotten.clear();Destroy(50);
     check(deleted==16 && pvpForgetCalls==16 && !held && !deployments[0].used,"empty maximum deployment forgets every ownership record on rollback");}
    {ResetSupportDispatch();made=deleted=held=seatNow=activated=0;pvpAttachOk=false;pvpForgetCalls=0;
     check(!SpawnDeployment(51,networkPlan,false,false) && made==2 && deleted==2 && !held && !deployments[0].used &&
           !seatNow && !activated && pvpForgetCalls==2,
           "ownership attachment failure rolls all constructed objects back before any crew boarding or flight");
     pvpAttachOk=true;}
    // Rollback must respect a real player's independent boarding action, on either machine.
    testOnline=true;
    for(bool remotePlayer:{false,true}) {
        ResetSupportDispatch();made=deleted=held=0;transactionActive=false;
        check(Spawn(20,networkPlan,false),"prepare rollback occupancy fixture");
        unsigned char human[0x400]{},control[32]{};
        Put<unsigned char>(human,kHumanPlayer,1);Put<void*>(human,kHumanPad,human);
        if(remotePlayer)Put<unsigned char>(human,edf::kRiderNet+edf::kNetFlags,1);
        Put<long>(control,edf::kCtrlUses,1);Put<void*>(seats[0],kSeatRider,human);Put<void*>(seats[0],kSeatRiderCtrl,control);
        pvpForgetCalls=0;pvpForgotten.clear();Destroy(20);
        check(!deleted && !held && !deployments[0].used,"cancel preserves occupied hull and real crew, releases their holds, and drops deployment ownership");
        check(!pvpForgetCalls && pvpForgotten.empty(),"a player-retained hull and its preserved real crew keep their ownership bindings");
    }
    ResetSupportDispatch();testOnline=true;submitReady=true;
    SupportCallAt(0,target,note,128);const auto firstRequest=noticeRequest;
    check(configuredHooks.notice!=nullptr,"local request status callback is wired");
    Status(L"other peer planning");SupportCallStatus(note,128);
    check(std::wcsstr(note,L"调度中")!=nullptr,"accepted means automatic dispatch and is not overwritten by another peer's planner");
    configuredHooks.notice(firstRequest,support_net::RequestStatus::refused);
    Status(L"other peer deployment active");SupportCallStatus(note,128);
    check(std::wcsstr(note,L"房主拒绝")!=nullptr,"local refusal is persistent despite unrelated host deployment progress");
    configuredHooks.notice(firstRequest,support_net::RequestStatus::active);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"房主拒绝")!=nullptr,"late active cannot reopen the same rejected request");
    SupportCallAt(0,target,note,128);const auto secondRequest=noticeRequest;
    configuredHooks.notice(firstRequest,support_net::RequestStatus::cancelled);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"调度中")!=nullptr,"old cancellation cannot overwrite a newer local request");
    configuredHooks.notice(secondRequest,support_net::RequestStatus::active);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"已出动")!=nullptr && !std::wcsstr(note,L"已到达"),"active reports deployment rather than claiming arrival");
    submitReady=false;check(!SupportCallAt(0,target,note,128),"pre-id transport refusal returns failure");
    SupportCallStatus(note,128);
    check(std::wcsstr(note,L"联机扩展尚未就绪")!=nullptr,"new pre-id refusal clears the previous active result");
    configuredHooks.notice(secondRequest,support_net::RequestStatus::cancelled);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"联机扩展尚未就绪")!=nullptr,"older allocated request cannot cover the newest unallocated attempt");
    submitReady=true;SupportCallAt(0,target,note,128);
    configuredHooks.notice(noticeRequest,support_net::RequestStatus::timeout);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"请求超时")!=nullptr,"timeout is visible instead of waiting forever");
    SupportCallAt(0,target,note,128);
    configuredHooks.notice(noticeRequest,support_net::RequestStatus::interrupted);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"会话中断")!=nullptr,"interrupted session has a distinct terminal message");
    testOnline=false;SupportCallAt(0,target,note,128);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"正在安排")!=nullptr && !localRequest.shown,"offline request restores its detailed planner channel");
    ResetSupportDispatch();SupportCallStatus(note,128);check(!note[0],"mission reset clears old request notices");
    allReady=false;
    const int madeBeforeFailure=made;
    check(SupportCallAt(0,target,note,128),"unavailable-soldier request still enters the real planner");
    SupportDispatchTick();SupportCallStatus(note,128);
    check(std::wcscmp(note,soldierFailure)==0 && made==madeBeforeFailure,
          "native creation failure survives the planner/UI boundary without becoming resource unavailable");
    soldierFailure=L"支援兵员资源尚未预载";
    ++fixtureMs;SupportCallAt(0,target,note,128);SupportDispatchTick();SupportCallStatus(note,128);
    check(std::wcscmp(note,soldierFailure)==0,"actual preload failure retains its separate diagnostic");
    config.customNpcAi=false;
    ++fixtureMs;SupportCallAt(0,target,note,128);SupportDispatchTick();SupportCallStatus(note,128);
    check(std::wcsstr(note,L"NPC 指挥功能未启用")!=nullptr && std::wcscmp(note,soldierFailure)!=0 && made==madeBeforeFailure,
          "disabled NPC configuration takes precedence over soldier resource or constructor failure");
    config.customNpcAi=true;allReady=true;
    float footY=0;
    terrainSea=Sea::water;waterSurface=1;
    check(!Foot(0,0,0,footY),"soldier collection point cannot use a submerged floor as dry ground");
    waterSurface=0.2f;check(Foot(0,0,0,footY),"shallow wading matches the navigation allowance");
    terrainSea=Sea::unknown;check(!Foot(0,0,0,footY),"unknown water state is not treated as a safe entry");
    terrainSea=Sea::land;
    // Submarine calls now use the same plan, real crew, owner, registration and all-peer activation transaction.
    {
        const bool wasOnline=testOnline;
        ResetSupportDispatch();testOnline=false;fixtureMs+=40000;subLaunches=0;made=0;
        SupportPlan subPlan{};
        check(PlanSubmarine(16,target,nullptr,0,&subPlan)==support_net::PlanResult::refused && subLaunches==0,
              "submarine planning over land refuses before construction");
        terrainSea=Sea::water;
        check(PlanSubmarine(16,target,nullptr,0,&subPlan)==support_net::PlanResult::ready && subPlan.count==2 &&
              subPlan.units[0].resourceId==kSupportSubmarineResource && subPlan.units[1].role==1 && subLaunches==0,
              "host plans the exact underwater hull and its real Ranger without constructing either");
        check(Validate(subPlan) && SpawnDeployment(90001,subPlan,false,false) && subLaunches==1 && made==2,
              "the exact offline submarine plan creates one hull and its real crew");
        check(Assign(deployments[0]) && subActivated==1 && deployments[0].delivered,
              "submarine assignment seats the crew and activates without aircraft or ground routing");
        Destroy(90001);check(subDeleted==1,"submarine rollback uses its real native deletion adapter");
        testOnline=true;const int before=netRequests;
        check(SupportCallAt(16,target,note,128) && netRequests==before+1 && subLaunches==1,
              "online map submarine sends exactly one authenticated host request and creates no local copy");
        testOnline=wasOnline;terrainSea=Sea::land;
    }
    // One-participant online worlds must share Session native IDs and tokens with later submarine/child requests.
    ResetSupportDispatch();testOnline=true;soloHost=true;made=0;netRequests=0;registered=0;resourceCount=0;fixtureMs+=40000;
    check(SupportCallAt(0,target,note,128) && netRequests==1 && !offlinePending,"solo-world normal support uses the same registered Session as submarine children");
    SupportDispatchTick();
    check(!made && !registered && !deployments[0].used && nextOffline==1,
          "solo-world queued request creates no unregistered row or colliding local token");
    check(SupportCallAt(16,target,note,128) && netRequests==2 && !offlinePending && !made,
          "normal and submarine requests cannot create two independent token domains in a solo online world");
    SupportPlan noBypass{};check(Plan(0,target,0,&noBypass)==support_net::PlanResult::ready &&
          !SpawnDeployment(1,noBypass,false,false) && !made,"online creation rejects an unregistered local bypass even when called directly");
    const int rescueBefore=netRequests;
    check(SupportRescueAt(target,note,128) && netRequests==rescueBefore+1 && !made && !offlinePending,
          "solo-world rescue also stays in the single registered transaction domain");
    soloHost=false;ResetSupportDispatch();netRequests=0;
    check(SupportCallAt(0,target,note,128) && netRequests==1,"a host with other participants still uses the reliable protocol");
    testOnline=false;
    // Out-of-mission configuration (support_config.h), read from a real ini as plugin.cpp does.
    wchar_t ini[MAX_PATH];GetTempPathW(MAX_PATH,ini);wcscat_s(ini,L"edf6_support_dispatch_test.ini");
    FILE* f=nullptr;_wfopen_s(&f,ini,L"wb");check(f!=nullptr,"config ini written");
    const char text[]="[VehicleCrew]\r\nSupportDisabled=FIGHTER\r\nSupportSquadWeapon=shotgun\r\nSupportSquadLeaderWeapon=sniper\r\n"
                      "SupportPlatoonWeapons=flame,rocket,rifle\r\nSupportAircraftCrewWeapon=rocket\r\nSupportAircraftCount_HELI=3\r\n";
    std::fwrite(text,1,sizeof(text)-1,f);std::fclose(f);
    LoadSupportConfig(ini);DeleteFileW(ini);
    ResetSupportDispatch();made=0;fixtureMs+=40000;
    check(!SupportCallAt(6,target,note,128) && std::wcsstr(note,L"已在配置中停用") && !offlinePending && !made,
          "a unit disabled in the ini is refused with its reason before any planning");
    ResetSupportDispatch();made=0;resourceCount=0;routeResult=npc::navigation::Result::moving;
    check(SupportCallAt(21,target,note,128),"squad request accepted");SupportDispatchTick();
    check(made==4 && resourceCount==4 && lastResources[0]==SupportSoldierResource(SupportWeapon::sniper,true) &&
          lastResources[1]==SupportSoldierResource(SupportWeapon::shotgun,false) && lastResources[3]==SupportSoldierResource(SupportWeapon::shotgun,false),
          "the configured squad leader and member weapons are the soldiers actually created");
    ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
    SupportCallAt(22,target,note,128);SupportDispatchTick();
    check(made==12 && lastResources[0]==SupportSoldierResource(SupportWeapon::sniper,true) &&
          lastResources[1]==SupportSoldierResource(SupportWeapon::flame,false) && lastResources[5]==SupportSoldierResource(SupportWeapon::rocket,false) &&
          lastResources[9]==SupportSoldierResource(SupportWeapon::rifle,false),"each platoon squad gets its configured weapon");
    ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
    SupportCallAt(10,target,note,128);SupportDispatchTick();   // the stub's aircraft: one pilot each
    check(plannedAircraft==3 && made==6 && lastResources[0]==SupportSoldierResource(SupportWeapon::rocket,false),
          "the configured aircraft count reaches the runway planner and the crew carry the configured weapon");
    SupportPlan hostPlan=deployments[0].plan;
    check(Validate(hostPlan),"a peer accepts the host's configured weapons and count (structure, not its own ini)");
    hostPlan.units[1].resourceId=0x503;check(!Validate(hostPlan),"a non-template soldier resource is refused");
    // A room with an older All Forces peer (no kCapSoldierVariants): the host plans the protocol v2 plan an older
    // Validate accepts (rifles, each call's own aircraft count) and says why, instead of a silent mid-transaction refusal.
    peersNew=false;
    ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;routeResult=npc::navigation::Result::moving;
    SupportCallAt(21,target,note,128);SupportDispatchTick();
    bool rifles=made==4;for(int i=0;i<resourceCount;++i)rifles=rifles && lastResources[i]==(i%4 ? kSupportRangerResource : kSupportLeaderResource);
    SupportCallStatus(note,128);
    check(rifles && std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportLegacyPeers))!=nullptr,
          "older peer: rifle leader/members (ids 2/1) and the HUD says the configured weapons were not synchronised");
    ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
    SupportCallAt(10,target,note,128);SupportDispatchTick();
    check(plannedAircraft==1 && made==2 && lastResources[0]==kSupportRangerResource,"older peer: the call's own aircraft count, rifle crew");
    const auto old=deployments[0].plan;bool v2=true;
    for(unsigned i=0;i<old.count;++i)v2=v2 && (old.units[i].resourceId>=kSupportAircraftResource || old.units[i].resourceId<=2);
    check(v2,"the whole plan uses only resources a protocol v2 Validate knows");
    peersNew=true;ResetSupportDispatch();SupportCallStatus(note,128);
    check(!std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportLegacyPeers)),"the older-peer notice ends with the mission");
    // The transports (transport.cpp; the user, 2026-10-09: "飞机和直升机应该也有运输机", "飞机就空降"): four entries after the
    // ground ones, every older index and key as it was; a hull in the air, its pilot and the soldiers made inside it.
    check(SupportCallCount()==39 && !std::wcscmp(SupportCallKey(28),L"TRUCK_DELIVERY") && !std::wcscmp(SupportCallKey(29),L"SQUAD_HELI") &&
          !std::wcscmp(SupportCallKey(32),L"PLATOON_AIRDROP"),"the transports appended: every older index and key stands");
    check(SupportCallIcon(29)==SupportIcon::heli && SupportCallIcon(31)==SupportIcon::jet && SupportCallVariant(30)==SupportVariant::platoon &&
          SupportCallVariant(31)==SupportVariant::squad,
          "a helicopter row and a plane row, each with a squad and a platoon chip");
    ResetSupportDispatch();made=deleted=seatNow=activated=delivered=followed=resourceCount=0;fixtureMs+=40000;peersNew=true;
    check(SupportCallAt(30,target,note,128),"a helicopter assault (a platoon) queued");
    SupportDispatchTick();
    const SupportPlan assault=deployments[0].plan;
    check(pvpAttachedCount==14 && pvpAttachSawEveryObject && pvpAttachedRefs[13].obj==objects[13],
          "transport ownership includes pilot, hull and all twelve passengers without truncation");
    check(made==14 && assault.count==14 && lastPrepared.heli==static_cast<int>(HeliBody::transport410) && seatNow==1 && activated==1,
          "a platoon in one transport helicopter: hull, pilot and twelve soldiers made inside it, seated, flying in at spawn");
    check(At<const void*>(SeatAt(objects[0],0),kSeatRider)==objects[1] && At<const void*>(SeatAt(objects[0],12),kSeatRider)==objects[13],
          "the pilot at the stick, the twelve in its other seats");
    check(delivered==1 && deliveredSquads==3 && followed==9 && deliveredHull==objects[0] && deliveredTops[0]==objects[2] &&
          deliveredTops[1]==objects[6] && deliveredTops[2]==objects[10],
          "three squads (a leader every four, the three after it following him) handed to their helicopter");
    check(deployments[0].delivered && Validate(assault),"handed over: the dispatcher leaves them to transport.cpp; a peer accepts the plan");
    SupportPlan bad=assault;bad.units[3].resourceId=kSupportLeaderResource;check(!Validate(bad),"a leader out of place is refused");
    bad=assault;bad.units[1].role=0;check(!Validate(bad),"a pilot detached from its hull is refused");
    // A composed load (support_call.h SupportLoadout): any number of squads up to the hull's twelve riders.
    bad=assault;bad.count=10;check(Validate(bad),"two squads (a composed load) are a valid plan");
    bad=assault;bad.units[14]=bad.units[2];bad.count=15;check(!Validate(bad),"thirteen riders, past the hull's seats, are refused");
    bad=assault;bad.units[0].resourceId=kSupportAircraftResource+kSupportAirborneOffset+31;check(!Validate(bad),"another entry's hull is refused");
    ResetSupportDispatch();made=0;ferries=paradrops=0;plannedTransports=0;ferryHeading[0]=ferryHeading[2]=0;fixtureMs+=40000;
    SupportCallAt(31,target,note,128);SupportDispatchTick();
    check(made==6 && lastPrepared.transportPlane && ferries==1 && paradrops==1 && delivered==1,
          "a paratroop plane: four soldiers aboard, it flies on to the point attacking nothing and drops them there");
    check(ferryHeading[0]==1.0f && ferryHeading[2]==0.0f,
          "its passes along the heading it was planned on (the route's, in its matrix), from its first frame (BoardAirborne)");
    check(plannedTransports==1 && plannedHeli==-1 && plannedAircraft==1,
          "the plane planned through the one air planner with its own spec (transportPlane, no heli row, one aircraft)");
    ResetSupportDispatch();made=0;transportReady=false;fixtureMs+=40000;
    SupportCallAt(29,target,note,128);SupportDispatchTick();
    SupportCallStatus(note,128);
    check(!made && !offlinePending && std::wcsstr(note,L"运输机资源未安装"),"the transport's SGO not installed: refused with why, nothing made");
    transportReady=true;peersTransports=false;ResetSupportDispatch();made=0;fixtureMs+=40000;
    SupportCallAt(29,target,note,128);SupportDispatchTick();
    check(!made && !offlinePending,"a peer without the transports: refused before anything is made");
    peersTransports=true;
    // The container airdrops (airdrop.cpp; the user, 2026-10-09: "运输机还要能空投载具"): one entry per ground vehicle
    // after the transports, one row (the plane) with the vehicles as its chips.
    check(!std::wcscmp(SupportCallKey(33),L"TANK_AIRDROP") && !std::wcscmp(SupportCallKey(34),L"TRANSPORT_AIRDROP") &&
          !std::wcscmp(SupportCallKey(35),L"TRUCK_AIRDROP") && !std::wcscmp(SupportCallKey(36),L"BARRAGE_ROCKETS") && !SupportCallKey(39),"three airdrop entries appended after the transports, the strikes after them");
    check(SupportCallIcon(33)==SupportIcon::heli && SupportCallVariant(33)==SupportVariant::tank &&
          SupportCallVariant(34)==SupportVariant::apc && SupportCallVariant(35)==SupportVariant::truck,
          "the airdrop row: the helicopter, its chips the tank, the APC, the truck");
    {
        const wchar_t* names[3]={SupportCallName(33),SupportCallName(34),SupportCallName(35)};
        mapbtn::Group groups[3]{};
        check(mapbtn::GroupSupport(names,3,groups,3)==1 && groups[0].count==3,"the three airdrops are one row of the bar");
    }
    check(SupportCallSeats(33)==0 && !SupportCallPreset(34,nullptr),"an airdrop has no seats to compose (the vehicle comes empty)");
    ResetSupportDispatch();made=airdrops=ferries=heliLeaves=0;fixtureMs+=40000;
    check(SupportCallAt(34,target,note,128),"an APC airdrop queued");
    SupportDispatchTick();
    {
        const SupportPlan drop=deployments[0].plan;
        check(made==2 && drop.count==2 && lastPrepared.heli==static_cast<int>(HeliBody::transport410) && !ferries && airdrops==1 &&
              lastAirdrop==SupportVehicleKind::transport && airdropPlane==objects[0] && !heliLeaves,
              "an airdrop: the transport helicopter and its pilot, handed to airdrop.cpp with the APC's container");
        check(Validate(drop),"its plan is valid");
        SupportPlan wrong=drop;wrong.count=3;wrong.units[2]=wrong.units[1];check(!Validate(wrong),"no riders in an airdrop plan");
        wrong=drop;wrong.units[0].resourceId=kSupportAircraftResource+kSupportAirborneOffset+33;check(!Validate(wrong),"another airdrop's helicopter is refused");
        wrong=drop;wrong.units[1].role=0;check(!Validate(wrong),"a pilot detached from its helicopter is refused");
    }
    ResetSupportDispatch();made=airdrops=ferries=heliLeaves=0;airdropTakes=false;fixtureMs+=40000;
    SupportCallAt(33,target,note,128);SupportDispatchTick();
    check(airdrops==1 && heliLeaves==1,"no container made: the helicopter is sent off, not left at the point");
    airdropTakes=true;
    ResetSupportDispatch();made=airdrops=0;airdropReady=false;fixtureMs+=40000;
    SupportCallAt(35,target,note,128);SupportDispatchTick();SupportCallStatus(note,128);
    check(!made && !airdrops && std::wcsstr(note,L"集装箱"),"the container or the vehicle not preloaded: refused with why, nothing made");
    airdropReady=true;
    ResetSupportDispatch();made=airdrops=0;testOnline=true;
    {
        SupportPlan plan{};
        check(Plan(33,target,0,&plan)==support_net::PlanResult::refused && !made && !airdrops,
              "in a session: refused (the stock container registers its vehicle on the room's network)");
    }
    testOnline=false;heliLeaves=0;
    // Retired with its soldiers got off: only the pilot still aboard is deleted (Retire), never the ones fighting on.
    ResetSupportDispatch();made=deleted=0;fixtureMs+=40000;
    SupportCallAt(29,target,note,128);SupportDispatchTick();
    check(made==6,"a squad's helicopter");
    for(unsigned seat=1;seat<6;++seat)Put<const void*>(SeatAt(objects[0],seat),kSeatRider,nullptr);
    aircraftLeft=true;fixtureMs+=1;SupportDispatchTick();aircraftLeft=false;
    check(deleted==1,"left: its pilot deleted, the four who got off left alone");
    heliHull=true;
    check(SupportWithdrawVehicle(objects[0]) && heliLeaves==1,"WITHDRAW: a support helicopter flies off");
    heliHull=false;
    check(!SupportWithdrawVehicle(objects[3]),"a soldier is no support vehicle");
    // Left with the squad it delivered still seated (2026-10-11 review): the pilot deleted, the four put off, not deleted.
    ResetSupportDispatch();made=deleted=unseated=0;fixtureMs+=40000;
    SupportCallAt(29,target,note,128);SupportDispatchTick();
    check(made==6 && delivered>0,"a squad's helicopter, its squad handed over");
    aircraftLeft=true;fixtureMs+=1;SupportDispatchTick();aircraftLeft=false;
    check(deleted==1 && unseated==4,"left with them aboard: its pilot deleted, the four delivered put off");
    // A crewed APC arrived (Broken Arrow, the user 2026-10-09: "卡车之类的运输载具改成断剑那种操作方式"): the soldiers in its
    // passenger seats a squad, handed to it as their transport; its driver stays at the wheel.
    ResetSupportDispatch();made=deleted=followed=delivered=0;fixtureMs+=40000;
    {
        auto apc=Make(true),driver=Make(),first=Make(),second=Make();
        auto& d=deployments[0];d.used=d.assigned=d.started=true;d.id=7;d.born=fixtureMs;
        d.plan.catalogId=static_cast<unsigned>(GroundStart()+2);d.plan.count=4;
        d.plan.units[0].resourceId=kVehicle+1;
        for(unsigned i=1;i<4;++i){d.plan.units[i].resourceId=kSoldier;d.plan.units[i].role=1;}
        d.objects[0]=apc;d.objects[1]=driver;d.objects[2]=first;d.objects[3]=second;
        auto* v=static_cast<unsigned char*>(const_cast<void*>(apc.obj));
        for(unsigned i=0;i<3;++i){Put<const void*>(SeatAt(v,i),kSeatRider,d.objects[i+1].obj);}
        routeResult=npc::navigation::Result::arrived;
        SupportDispatchTick();fixtureMs+=16;SupportDispatchTick();
        check(d.delivered && delivered==1 && deliveredSquads==1 && deliveredTops[0]==first.obj && deliveredHull==apc.obj && followed==1,
              "arrived: its passengers one squad (the second following the first), handed to the APC; the driver not among them");
        // WITHDRAW: back to its entry (here at once: nobody drives it), its own crew aboard deleted, then the hull. The
        // squad it delivered, still seated (2026-10-11 review: one that left its trip while aboard), is put off, never
        // deleted with it.
        driverAboard=false;deleted=0;unseated=0;
        check(SupportWithdrawVehicle(apc.obj) && d.withdrawing,"WITHDRAW: a support APC goes back to its entry");
        fixtureMs+=16;SupportDispatchTick();
        check(deleted==1 && unseated==2 && !Seated(v,first) && !Seated(v,second),
              "out: its driver deleted, the two delivered soldiers still aboard put off (not deleted)");
        fixtureMs+=16;SupportDispatchTick();
        check(deleted==2 && !d.withdrawing,"then the hull");
        driverAboard=true;routeResult=npc::navigation::Result::pending;
    }
    // 2026-10-10 "我摇的人还没到目的地成棍木了": a route that never comes (pending / blocked) held the APC and its passengers
    // where they stood for good. Past kStuckMs making no way it delivers where it stands; each seated soldier deleted later
    // is put out of its seat first (Unseat), so no freed rider is left in a seat.
    ResetSupportDispatch();made=deleted=followed=delivered=unseated=0;deliveredHere=false;fixtureMs+=40000;
    {
        auto apc=Make(true),driver=Make(),first=Make(),second=Make();
        auto& d=deployments[0];d.used=d.assigned=d.started=true;d.id=8;d.born=fixtureMs;
        d.plan.catalogId=static_cast<unsigned>(GroundStart()+2);d.plan.count=4;
        d.plan.units[0].resourceId=kVehicle+1;
        for(unsigned i=1;i<4;++i){d.plan.units[i].resourceId=kSoldier;d.plan.units[i].role=1;}
        d.objects[0]=apc;d.objects[1]=driver;d.objects[2]=first;d.objects[3]=second;
        auto* v=static_cast<unsigned char*>(const_cast<void*>(apc.obj));
        for(unsigned i=0;i<3;++i) {
            Put<const void*>(SeatAt(v,i),kSeatRider,d.objects[i+1].obj);
            Put<const void*>(static_cast<unsigned char*>(const_cast<void*>(d.objects[i+1].obj)),kSoldierVehicle,v);
        }
        routeResult=npc::navigation::Result::blocked;traceLines=0;
        SupportDispatchTick();
        check(traceLines==5,"the trace: a header and one line for each of its four units");
        fixtureMs+=1000;SupportDispatchTick();
        check(traceLines==5,"...rate-limited (not every frame)");
        fixtureMs+=kStuckMs/2;SupportDispatchTick();
        check(!d.delivered && !delivered && traceLines==10,"no way yet: it waits for its route (traced again)");
        fixtureMs+=kStuckMs;SupportDispatchTick();
        check(d.delivered && delivered==1 && deliveredHere && deliveredTops[0]==first.obj,
              "making no way past kStuckMs: its passengers handed over to get off where it stands");
        // WITHDRAW (as the delivery's end does): out at the entry, its seated driver put out of the seat before deletion.
        driverAboard=false;deleted=0;
        check(SupportWithdrawVehicle(apc.obj),"it goes back");
        fixtureMs+=16;SupportDispatchTick();
        check(unseated==3 && deleted==1,"each seated soldier out of its seat; only its driver deleted (the delivered two put off)");
        driverAboard=true;routeResult=npc::navigation::Result::pending;
    }
    // A composed load (the bar's composition panel; the user, 2026-10-09: "支援栏是断剑那种，先点载具，然后选里面的人"; a click
    // a whole squad). The wire form first: PackSupportLoadout / UnpackSupportLoadout round trip, malformed refused.
    {
        SupportLoadout l{};l.count=8;
        for(int i=0;i<4;++i){l.soldier[i]=SupportWeapon::wingLance;l.soldier[4+i]=SupportWeapon::fencerCannon;}
        SupportLoadout back{};
        check(UnpackSupportLoadout(PackSupportLoadout(l),&back) && back.count==8 && back.soldier[0]==SupportWeapon::wingLance &&
              back.soldier[7]==SupportWeapon::fencerCannon,"a load packs into a request's 64 bits and back");
        check(PackSupportLoadout(SupportLoadout{})==0 && !UnpackSupportLoadout(0,&back) && back.count==0,"no load: 0, the call's own");
        check(!UnpackSupportLoadout(0xDu,&back) && !UnpackSupportLoadout(0x1u|(0xFull<<4),&back) &&
              !UnpackSupportLoadout(PackSupportLoadout(l)|(1ull<<60),&back),
              "malformed: thirteen soldiers, a kind past the table, bits past the count");
        check(SupportLoadoutResource(l,0)==SupportSoldierResource(SupportWeapon::wingLance,true) &&
              SupportLoadoutResource(l,4)==SupportSoldierResource(SupportWeapon::fencerCannon,true) &&
              SupportLoadoutResource(l,5)==SupportSoldierResource(SupportWeapon::fencerCannon,false),"a leader every four, each squad its own");
    }
    check(SupportCallSeats(21)==12 && SupportCallSeats(22)==12 && SupportCallSeats(29)==12 && SupportCallSeats(32)==12 &&
          SupportCallSeats(GroundStart()+2)==4 && SupportCallSeats(GroundStart()+4)==4 && SupportCallSeats(GroundStart())==0 &&
          SupportCallSeats(GroundStart()+3)==0 && SupportCallSeats(0)==0,
          "seats: the infantry and the transports twelve, a crewed APC / truck its four passenger rows; a tank, an empty delivery, an air call none");
    {
        SupportLoadout p{};
        check(SupportCallPreset(21,&p) && p.count==4 && p.soldier[0]==SupportWeapon::sniper && p.soldier[1]==SupportWeapon::shotgun,
              "a squad's preset: the ini's leader and members");
        check(SupportCallPreset(30,&p) && p.count==12 && p.soldier[4]==SupportWeapon::sniper && p.soldier[5]==SupportWeapon::rocket,
              "a platoon's preset: three squads, the ini's platoon weapons");
        check(!SupportCallPreset(0,&p) && p.count==0,"an air call has no load");
    }
    {
        SupportLoadout l{};l.count=8;
        for(int i=0;i<4;++i){l.soldier[i]=SupportWeapon::wingLance;l.soldier[4+i]=SupportWeapon::fencerCannon;}
        ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;routeResult=npc::navigation::Result::moving;peersNew=true;
        check(SupportCallComposedAt(21,target,&l,note,128),"a composed squad call queued");SupportDispatchTick();
        check(made==8 && resourceCount==8 && lastResources[0]==SupportSoldierResource(SupportWeapon::wingLance,true) &&
              lastResources[3]==SupportSoldierResource(SupportWeapon::wingLance,false) &&
              lastResources[4]==SupportSoldierResource(SupportWeapon::fencerCannon,true) && Validate(deployments[0].plan),
              "the composed squads walk in: a Wing Diver squad and a Fencer squad, each with its own leader; a peer accepts it");
        ResetSupportDispatch();made=deleted=seatNow=activated=delivered=followed=resourceCount=0;fixtureMs+=40000;
        SupportLoadout one{};one.count=4;for(int i=0;i<4;++i)one.soldier[i]=SupportWeapon::wingThunderBow;
        check(SupportCallComposedAt(29,target,&one,note,128),"a composed helicopter assault queued");SupportDispatchTick();
        check(made==6 && deployments[0].plan.count==6 && resourceCount==5 && lastResources[1]==SupportSoldierResource(SupportWeapon::wingThunderBow,true) &&
              Validate(deployments[0].plan),"one squad aboard the transport: hull, pilot and the four composed soldiers");
        ResetSupportDispatch();made=0;fixtureMs+=40000;
        SupportLoadout many{};many.count=12;
        check(!SupportCallComposedAt(GroundStart()+2,target,&many,note,128) && !offlinePending && !made &&
              std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportLoadoutTooMany))!=nullptr,
              "twelve soldiers for an APC's four seats: refused with the reason, nothing planned");
        // An older peer (no kCapLoadout): the call's own load, and the HUD says the chosen load was not sent.
        peersLoadout=false;ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;routeResult=npc::navigation::Result::moving;
        SupportCallComposedAt(21,target,&l,note,128);SupportDispatchTick();SupportCallStatus(note,128);
        check(made==4 && lastResources[0]==SupportSoldierResource(SupportWeapon::sniper,true) &&
              std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportLoadoutLegacyPeers))!=nullptr,
              "older peer: the squad's own four (the ini's), and the HUD says why");
        peersLoadout=true;ResetSupportDispatch();
        // Online (not the host): the load goes to the host in the request.
        testOnline=true;submitReady=true;lastNetLoadout=0;fixtureMs+=40000;
        check(SupportCallComposedAt(21,target,&l,note,128) && lastNetLoadout==PackSupportLoadout(l),"a guest's request carries its load");
        testOnline=false;ResetSupportDispatch();
    }
    {
        // A crewed APC with a composed load: its driver, then one squad of passengers (a leader first); a peer accepts it.
        SupportPlan apc{};apc.catalogId=static_cast<unsigned>(GroundStart()+2);apc.count=6;std::memcpy(apc.target,target,12);
        apc.units[0].resourceId=kVehicle+1;
        apc.units[1].resourceId=kSoldier;apc.units[1].role=1;
        for(unsigned i=2;i<6;++i){apc.units[i].resourceId=SupportSoldierResource(SupportWeapon::fencerShotgun,i==2);apc.units[i].role=1;}
        for(auto& u:apc.units)u.matrix[0]=u.matrix[5]=u.matrix[10]=u.matrix[15]=1;
        check(Validate(apc),"a crewed APC: driver and a composed squad of passengers");
        apc.units[1].resourceId=kLeader;check(!Validate(apc),"its driver is a member, never a leader");
        apc.units[1].resourceId=kSoldier;apc.count=7;apc.units[6]=apc.units[5];check(!Validate(apc),"six aboard a five-seat APC is refused");
    }
    {
        // Out-of-game loadouts (support_loadout.h, docs/feature-2026-10-09-loadout-editor.md), from a real ini.
        wchar_t path[MAX_PATH];GetTempPathW(MAX_PATH,path);wcscat_s(path,L"edf6_support_loadout_test.ini");
        FILE* out=nullptr;_wfopen_s(&out,path,L"wb");check(out!=nullptr,"loadout ini written");
        const char loadText[]="[VehicleCrew]\r\nSupportPreset_SQUAD=lance@1E3A8A*2,cannon,rifle@X:FFFFFF\r\n"
                              "SupportVehicle_TANK_CREWED=HE,AP:25\r\nSupportVehicle_STRIKE=MK82:6,MK82:6\r\n"
                              "SupportVehicle_SQUAD=HE\r\nSupportVehicle_FIGHTER=AP:20\r\n";
        std::fwrite(loadText,1,sizeof(loadText)-1,out);std::fclose(out);
        LoadSupportConfig(path);DeleteFileW(path);
        const auto& c=SupportCfg();
        const int tankEntry=GroundStart();
        check(c.preset[21].count==4 && c.preset[21].look[0].primary==0x1E3A8A && c.vehicle[tankEntry].On() &&
              c.vehicle[tankEntry].count==1 && c.vehicle[2].On() && c.vehicle[2].count==2,"presets and pylons read from the ini");
        check(!c.vehicle[21].On() && !c.vehicle[6].On() && std::wcsstr(c.problems,L"SupportVehicle_SQUAD") &&
              std::wcsstr(c.problems,L"SupportVehicle_FIGHTER"),"pylons on an entry without a vehicle, AP on a jet: refused and named");
        const std::wstring lanceL=L"EDF6VC_NPC_LANCE_L_1E3A8A_X.SGO",lance=L"EDF6VC_NPC_LANCE_1E3A8A_X.SGO",
                           rifle=L"EDF6VC_NPC_RIFLE_X_FFFFFF.SGO",tankFile=L"EDF6VC_LO_TANK_4000000000000C81.SGO",
                           strikeFile=L"EDF6VC_LO_STRIKE_4000000000056562.SGO";
        // Offline, every file there: the soldiers made from their coloured copies, the uncoloured one stock.
        variantFiles={lanceL,lance,rifle,tankFile,strikeFile};missingNoted.clear();
        peersNew=true;ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;routeResult=npc::navigation::Result::moving;
        check(SupportCallAt(21,target,note,128),"a preset squad call queued");SupportDispatchTick();
        const auto& sq=deployments[0].plan;
        check(made==4 && (sq.units[0].variant&kVariantApplied) && (sq.units[1].variant&kVariantApplied) && !sq.units[2].variant &&
              (sq.units[3].variant&kVariantApplied) && Validate(sq),"offline: the coloured soldiers' units carry their applied looks");
        check(lastLooks[0]==L"app:/object/edf6vc_npc_lance_l_1e3a8a_x.sgo" && lastLooks[1]==L"app:/object/edf6vc_npc_lance_1e3a8a_x.sgo" &&
              lastLooks[2].empty() && lastLooks[3]==L"app:/object/edf6vc_npc_rifle_x_ffffff.sgo","each made from its own file, a leader's its _L");
        // A file missing here: stock, listed for the installer, said beside the status.
        variantFiles.erase(lance);ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
        SupportCallAt(21,target,note,128);SupportDispatchTick();
        check(made==4 && !(deployments[0].plan.units[1].variant&kVariantApplied) && deployments[0].plan.units[1].variant &&
              lastLooks[1].empty() && !missingNoted.empty() && missingNoted.back()==lance,"a missing look: stock, wanted, listed");
        SupportCallStatus(note,128);
        check(std::wcsstr(note,L"EDF6VC_NPC_LANCE_1E3A8A_X.SGO")!=nullptr,"the status names the file not yet made");
        variantFiles.insert(lance);
        // The host of a room: applied only when every peer applies variants and has the file.
        testOnline=true;soloHost=false;SupportPlan plan{};
        peersApplyVariants=false;ResetSupportDispatch();plan={};
        check(Plan(21,target,PackSupportLoadout(PresetLoadout(c.preset[21])),&plan)==support_net::PlanResult::ready &&
              plan.units[0].variant && !(plan.units[0].variant&kVariantApplied),"an older peer: stock, what it wanted kept");
        peersApplyVariants=true;peersHaveFiles=false;ResetSupportDispatch();plan={};
        Plan(21,target,PackSupportLoadout(PresetLoadout(c.preset[21])),&plan);
        check(plan.units[0].variant && !(plan.units[0].variant&kVariantApplied),"a peer without the file: stock (it lists the file)");
        peersHaveFiles=true;ResetSupportDispatch();plan={};
        Plan(21,target,PackSupportLoadout(PresetLoadout(c.preset[21])),&plan);
        check((plan.units[0].variant&kVariantApplied) && Validate(plan),"every peer has it: applied online too");
        // A peer's view of a host's plan: a look it lacks comes stock (Validate passes); pylons it lacks refuse the plan.
        variantFiles.erase(lanceL);missingNoted.clear();
        check(Validate(plan),"a soldier's look a peer lacks does not refuse the plan");
        // A Bloom false positive is harmless for colours, but a backpack changes the weapon list: an applied pack
        // missing here must refuse preparation instead of materialising the same NetId with a different armament.
        unsigned char collision[kVariantBloomBytes]{};
        for(const wchar_t* present:{L"EDF6VC_NPC_RIFLE_X_X_AWEAPON245.SGO",L"EDF6VC_NPC_RIFLE_L_X_X_AWEAPON258.SGO",
                                    L"EDF6VC_NPC_FLAME_X_X_AWEAPON252.SGO"})BloomAdd(collision,VariantHash(present));
        const std::wstring packFile=L"EDF6VC_NPC_RIFLE_X_X_AHANDGRENADE01.SGO";
        check(BloomHas(collision,VariantHash(packFile.c_str())) && !variantFiles.count(packFile),
              "three real resources can make the hello falsely announce a missing grenade resource");
        SupportPlan packed=plan;SupportLook packLook;packLook.pack=1;
        packed.units[1].resourceId=SupportSoldierResource(SupportWeapon::rifle,false);
        packed.units[1].variant=kVariantApplied|LookVariant(packLook);
        missingNoted.clear();
        check(!Validate(packed) && !missingNoted.empty() && missingNoted.back()==packFile,
              "an applied backpack missing here refuses the whole plan and names the missing resource");
        variantFiles.insert(packFile);
        check(Validate(packed),"the exact backpack resource here: the peer prepares normally");
        variantFiles.erase(packFile);packed.units[1].variant&=~kVariantApplied;
        check(Validate(packed),"a wanted but unapplied pack uses stock on every peer");
        variantFiles.insert(lanceL);
        // The tank: the stock 105 mm and 25 AP rounds beside it, one hull (the user: 一半ap一半he).
        ResetSupportDispatch();plan={};
        check(Plan(static_cast<std::uint32_t>(tankEntry),target,0,&plan)==support_net::PlanResult::ready &&
              plan.units[0].resourceId==kVehicle && plan.units[0].variant==(LoadoutVariant(c.vehicle[tankEntry])|kVariantApplied) &&
              !plan.units[1].variant && Validate(plan),"the tank's hull carries its pylons; its crew none");
        variantFiles.erase(tankFile);missingNoted.clear();
        check(!Validate(plan) && !missingNoted.empty() && missingNoted.back()==tankFile,
              "a peer without the tank's file refuses the plan (another weapon list) and lists the file");
        variantFiles.insert(tankFile);
        SupportPlan garbage=plan;garbage.units[0].variant=kVariantApplied|kVariantVehicle|0x9ull;
        check(!Validate(garbage),"pylons that do not decode for the hull refuse the plan");
        garbage=plan;garbage.units[1].variant=plan.units[0].variant;
        check(!Validate(garbage),"pylons on a soldier refuse the plan");
        testOnline=false;soloHost=false;ResetSupportDispatch();
        // A strike call: each aircraft made from the loaded file.
        lastAircraftVariant.clear();made=0;fixtureMs+=40000;
        check(SupportCallAt(2,target,note,128),"a strike call queued");SupportDispatchTick();
        check(made>0 && lastAircraftVariant==L"app:/object/edf6vc_lo_strike_4000000000056562.sgo" &&
              (deployments[0].plan.units[0].variant&kVariantApplied),"the strike jets are made from their all-bombs file");
        ResetSupportDispatch();
    }
    {
        // The same packed preset before/after a peer update: only its pack downgrades; the generated/preloaded
        // compatible resource still supplies its colours immediately (no pending-list round trip).
        wchar_t path[MAX_PATH];GetTempPathW(MAX_PATH,path);wcscat_s(path,L"edf6_backpack_compat_test.ini");
        FILE* out=nullptr;_wfopen_s(&out,path,L"wb");check(out!=nullptr,"packed compatibility ini written");
        const char compatibilityText[]="[VehicleCrew]\r\nSupportPreset_SQUAD=rifle@1E3A8A+aHandGrenade01*4\r\n";
        std::fwrite(compatibilityText,1,sizeof(compatibilityText)-1,out);std::fclose(out);LoadSupportConfig(path);DeleteFileW(path);
        variantFiles={L"EDF6VC_NPC_RIFLE_L_1E3A8A_X_AHANDGRENADE01.SGO",L"EDF6VC_NPC_RIFLE_1E3A8A_X_AHANDGRENADE01.SGO",
                      L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO",L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"};
        testOnline=true;peersApplyBackpacks=false;peersApplyVariants=true;peersHaveFiles=true;
        ResetSupportDispatch();SupportPlan plan{};SupportLook look;
        const auto load=PackSupportLoadout(PresetLoadout(SupportCfg().preset[21]));
        check(Plan(21,target,load,&plan)==support_net::PlanResult::ready &&
              (plan.units[0].variant&kVariantApplied) && LookOfVariant(plan.units[0].variant,&look) && !look.pack && look.primary==0x1E3A8A,
              "older peer: its pack is absent but the colours are applied on the first plan");
        wchar_t madePath[128];
        check(SupportSoldierLookPath(plan.units[0].resourceId,plan.units[0].variant,true,madePath,128)==madePath &&
              !std::wcscmp(madePath,L"app:/object/edf6vc_npc_rifle_l_1e3a8a_x.sgo"),"the downgraded plan uses its colour-only resource");
        peersApplyBackpacks=true;ResetSupportDispatch();plan={};
        check(Plan(21,target,load,&plan)==support_net::PlanResult::ready &&
              (plan.units[0].variant&kVariantApplied) && LookOfVariant(plan.units[0].variant,&look) && look.pack==1 && look.primary==0x1E3A8A,
              "every peer updated: the same preset applies its pack and its colours");
        testOnline=false;ResetSupportDispatch();
    }
    {
        // Stage 5 (docs/feature-2026-10-12-room-loadout-menu.md): every support unit's real crew wears SupportCrewColour_<KEY>
        // (else SupportCrewColour) - the tank's / transport's / truck's driver and crew, every aircraft's pilot and gunners,
        // the transports' and airdrops' pilots, the mission vehicles' support crews - each seat its own look variant.
        wchar_t path[MAX_PATH];GetTempPathW(MAX_PATH,path);wcscat_s(path,L"edf6_crew_colour_test.ini");
        FILE* out=nullptr;_wfopen_s(&out,path,L"wb");check(out!=nullptr,"crew colour ini written");
        const char crewText[]="[VehicleCrew]\r\nSupportCrewColour=1E3A8A\r\nSupportCrewColour_TANK_CREWED=X:FFFFFF\r\n"
                              "SupportCrewColour_SQUAD=123456\r\nSupportCrewColour_FIGHTER=bogus\r\nSupportCrewColour_MISSION=FFFFFF\r\n"
                              "SupportCrewColour_HELI=X\r\nSupportPreset_TRANSPORT_CREWED=lance@C0272D*2\r\n";
        std::fwrite(crewText,1,sizeof(crewText)-1,out);std::fclose(out);
        LoadSupportConfig(path);DeleteFileW(path);
        const auto& c=SupportCfg();
        const int tankEntry=GroundStart(),apcEntry=GroundStart()+2;
        check(c.crewDefault.set && c.crewDefault.look.primary==0x1E3A8A && c.crew[tankEntry].set && c.crew[tankEntry].look.secondary==0xFFFFFF &&
              c.crewMission.set && c.MissionCrewLook().primary==0xFFFFFF && c.CrewLook(2).primary==0x1E3A8A && !c.CrewLook(10).On(),
              "crew colours read: the default, a unit's own, the mission's, X over the default");
        check(!c.crew[21].set && std::wcsstr(c.problems,L"SupportCrewColour_SQUAD") && std::wcsstr(c.problems,L"SupportCrewColour_FIGHTER") &&
              c.CrewLook(6).primary==0x1E3A8A,"an infantry squad has no crew, a bad value is refused: both named, the default stands");
        const std::wstring member=L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",tankCrew=L"EDF6VC_NPC_RIFLE_X_FFFFFF.SGO",
                           mission=L"EDF6VC_NPC_RIFLE_FFFFFF_X.SGO",missionLead=L"EDF6VC_NPC_RIFLE_L_FFFFFF_X.SGO",
                           lance=L"EDF6VC_NPC_LANCE_L_C0272D_X.SGO",lance2=L"EDF6VC_NPC_LANCE_C0272D_X.SGO";
        const std::wstring memberPath=L"app:/object/edf6vc_npc_rifle_1e3a8a_x.sgo";
        variantFiles={member,tankCrew,mission,missionLead,lance,lance2};missingNoted.clear();
        peersNew=true;peersApplyVariants=true;peersHaveFiles=true;testOnline=false;routeResult=npc::navigation::Result::moving;
        // An aircraft (the strike jet): its pilot made aboard from the coloured file.
        ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
        check(SupportCallAt(2,target,note,128),"a strike call queued");SupportDispatchTick();
        const auto& air=deployments[0].plan;
        check(air.count==2 && (air.units[1].variant&kVariantApplied) && Validate(air) && lastLooks[0]==memberPath,
              "an aircraft's pilot: the default crew colour, made from its file");
        check(!air.units[0].variant || (air.units[0].variant&kVariantVehicle),"the hull carries pylons or nothing, never a look");
        // X for the helicopter's crew: stock even though the default is set.
        ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
        SupportCallAt(10,target,note,128);SupportDispatchTick();
        check(made>0 && !deployments[0].plan.units[1].variant && lastLooks[0].empty(),"SupportCrewColour_HELI=X: its crew stock");
        // A transport (helicopter air assault, the call's own riders): its pilot coloured, the riders not (they are a squad).
        ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
        check(SupportCallAt(29,target,note,128),"a helicopter assault queued");SupportDispatchTick();
        const auto& tr=deployments[0].plan;
        check(tr.count==6 && (tr.units[1].variant&kVariantApplied) && !tr.units[2].variant && lastLooks[0]==memberPath &&
              lastLooks[1].empty() && Validate(tr),"a transport's pilot coloured, its riders the squad's own");
        // An airdrop (offline only): its helicopter's pilot.
        ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
        check(SupportCallAt(33,target,note,128),"a tank airdrop queued");SupportDispatchTick();
        check(deployments[0].plan.count==2 && (deployments[0].plan.units[1].variant&kVariantApplied) && lastLooks[0]==memberPath,
              "an airdrop's pilot coloured");
        // A tank: its own crew colour on every crew seat (the driver and the call's own crew), the hull no look.
        ResetSupportDispatch();SupportPlan plan{};
        check(Plan(static_cast<std::uint32_t>(tankEntry),target,0,&plan)==support_net::PlanResult::ready && plan.count>=2,"a tank planned");
        bool crewLooks=plan.units[0].resourceId==kVehicle && !plan.units[0].variant;
        SupportLook look;
        for(unsigned i=1;i<plan.count;++i)
            crewLooks=crewLooks && (plan.units[i].variant&kVariantApplied) && LookOfVariant(plan.units[i].variant,&look) &&
                      look.primary==kStockColour && look.secondary==0xFFFFFF;
        check(crewLooks && Validate(plan),"the tank's crew: SupportCrewColour_TANK_CREWED on every seat (the plan once said 'its crew none')");
        wchar_t made1[128];
        check(SupportSoldierLookPath(plan.units[1].resourceId,plan.units[1].variant,true,made1,128)==made1 &&
              !std::wcscmp(made1,L"app:/object/edf6vc_npc_rifle_x_ffffff.sgo"),"the crew seat is made from its file (the spawn path's own lookup)");
        // A crewed APC with a composed load: its driver the crew colour, its passengers the preset's.
        ResetSupportDispatch();plan={};
        check(Plan(static_cast<std::uint32_t>(apcEntry),target,PackSupportLoadout(PresetLoadout(c.preset[apcEntry])),&plan)==
              support_net::PlanResult::ready && plan.count==4,"a crewed APC with two lance passengers");
        check(LookOfVariant(plan.units[1].variant,&look) && look.primary==0x1E3A8A && LookOfVariant(plan.units[2].variant,&look) &&
              look.primary==0xC0272D && LookOfVariant(plan.units[3].variant,&look) && look.primary==0xC0272D,
              "the driver wears the crew colour, the passengers their preset's");
        // The file missing here: stock, listed, the status says so; the plan still ready (colours are cosmetic).
        variantFiles.erase(tankCrew);missingNoted.clear();ResetSupportDispatch();plan={};
        check(Plan(static_cast<std::uint32_t>(tankEntry),target,0,&plan)==support_net::PlanResult::ready &&
              plan.units[1].variant && !(plan.units[1].variant&kVariantApplied) && !missingNoted.empty() && missingNoted.back()==tankCrew,
              "a crew file not made yet: stock, wanted, on the pending list");
        SupportCallStatus(note,128);
        check(std::wcsstr(note,L"EDF6VC_NPC_RIFLE_X_FFFFFF.SGO")!=nullptr,"the status names the crew file not yet made");
        variantFiles.insert(tankCrew);
        // Online: applied only when every peer applies variants and has the file; a peer lacking it never refuses the plan.
        testOnline=true;soloHost=false;
        peersApplyVariants=false;ResetSupportDispatch();plan={};
        Plan(static_cast<std::uint32_t>(tankEntry),target,0,&plan);
        check(plan.units[1].variant && !(plan.units[1].variant&kVariantApplied),"an older peer in the room: the crew stock, what it wanted kept");
        peersApplyVariants=true;peersHaveFiles=false;ResetSupportDispatch();plan={};
        Plan(static_cast<std::uint32_t>(tankEntry),target,0,&plan);
        check(plan.units[1].variant && !(plan.units[1].variant&kVariantApplied),"a peer without the crew file: stock");
        peersHaveFiles=true;ResetSupportDispatch();plan={};
        Plan(static_cast<std::uint32_t>(tankEntry),target,0,&plan);
        check((plan.units[1].variant&kVariantApplied) && Validate(plan),"every peer has it: applied online");
        variantFiles.erase(tankCrew);missingNoted.clear();
        check(Validate(plan),"a peer missing the crew's file still accepts the plan (only pylons refuse)");
        wchar_t lookPath[128];
        check(!SupportSoldierLookPath(plan.units[1].resourceId,plan.units[1].variant,true,lookPath,128) && !missingNoted.empty() &&
              missingNoted.back()==tankCrew,"...it makes that seat stock and lists the file");
        variantFiles.insert(tankCrew);
        SupportPlan garbage=plan;garbage.units[1].variant=kVariantApplied|kVariantVehicle|1;
        check(!Validate(garbage),"pylons on a crew seat refuse the plan");
        // The mission vehicles' support crews: SupportCrewColour_MISSION, a leader's _L file.
        check((SupportCrewVariant(support_net::kMissionCrewCatalog,kSupportLeaderResource)&kVariantApplied) &&
              SupportSoldierLookPath(kSupportLeaderResource,SupportCrewVariant(support_net::kMissionCrewCatalog,kSupportLeaderResource),
                                     false,lookPath,128) && !std::wcscmp(lookPath,L"app:/object/edf6vc_npc_rifle_l_ffffff_x.sgo"),
              "the mission crew's leader is made from its own _L file");
        check(!SupportCrewVariant(21,kSupportRangerResource) && !SupportCrewVariant(2,kSupportVehicleResource),
              "no crew colour for an infantry squad or a hull");
        testOnline=false;ResetSupportDispatch();
        // Nothing configured: exactly the plans of before (no variant on any crew seat).
        LoadSupportConfig(L"Z:\\nonexistent\\edf6_none.ini");
        ResetSupportDispatch();plan={};
        Plan(static_cast<std::uint32_t>(tankEntry),target,0,&plan);
        bool none=plan.count>=2;for(unsigned i=0;i<plan.count;++i)none=none && !plan.units[i].variant;
        check(none,"no crew colour configured: no variant anywhere (the plan as it always was)");
        ResetSupportDispatch();
    }
    // ---- The sea rescue entry (2026-10-09, the user: 「给救援加一个支援目录项，走正规的呼叫支援流程」; 2026-10-10: no support
    // limits, a gunner, takeoff from the carrier, the requester's own) ----
    {
    const int rescue=SupportRescueCatalog();
    check(rescue==38 && SupportCallCount()==39 && SupportMenuCount()==38,
          "the rescue is the catalog's last entry, after the transports, the airdrops and the strikes (never #100's TransportStart 29)");
    check(std::wcscmp(SupportCallKey(rescue),L"RESCUE")==0 && std::wcscmp(SupportCallName(rescue),L"海上救援直升机")==0 &&
          SupportCallIcon(rescue)==SupportIcon::heli && SupportCallVariant(rescue)==SupportVariant::none,"its key, name and icon");
    check(std::wcscmp(SupportCallKey(28),L"TRUCK_DELIVERY")==0 && std::wcscmp(SupportCallKey(21),L"SQUAD")==0 &&
          std::wcscmp(SupportCallKey(29),L"SQUAD_HELI")==0 && std::wcscmp(SupportCallKey(35),L"TRUCK_AIRDROP")==0,
          "every older entry keeps its index (the wire's catalog id), the transports' and airdrops' too");
    check(SupportCallSeats(rescue)==0 && !SupportCallPreset(rescue,nullptr),"the rescue has no seats to compose");
    const float sea[3]={120,-3,-40};
    ResetSupportDispatch();testOnline=false;made=0;resourceCount=0;seatNow=0;activated=0;heliCommands=0;fixtureMs+=40000;
    rescueDeployed=rescueFailed=0;carrierOut=false;
    check(!SupportCallAt(rescue,sea,note,128) && !offlinePending && std::wcsstr(note,L"落海"),"a map or radio call cannot ask for the rescue");
    Status(L"map line");
    check(SupportRescueAt(sea,note,128) && !offlinePending && made==3,"offline: the rescue is planned and made at once (no queue)");
    const auto& rp=deployments[0].plan;
    check(rp.count==3 && rp.catalogId==static_cast<unsigned>(rescue) &&
          rp.units[0].resourceId==kSupportAircraftResource+kSupportAirborneOffset+static_cast<unsigned>(rescue) &&
          rp.units[1].role==1 && rp.units[2].role==1 && IsSupportSoldierResource(rp.units[1].resourceId) &&
          IsSupportSoldierResource(rp.units[2].resourceId),"one 410 with its pilot and one door gunner (2026-10-10: 「配炮手吧」)");
    check(seatNow==1 && At<const void*>(seats[0],kSeatRider)==objects[1] &&
          At<const void*>(seats[0]+edf::kSeatStride,kSeatRider)==objects[2] &&
          At<const void*>(seats[0]+2*edf::kSeatStride,kSeatRider)==nullptr,
          "seated at once: pilot in seat 0, gunner in seat 1, seat 2 (the other door) left for the swimmer");
    check(plannedHeli==static_cast<int>(HeliBody::brute410) && plannedAircraft==1 && takeoffSpots==-1,
          "no carrier deck to take off from: planned from the edge's air route");
    check(At<float>(objects[0],kPosition)==-1400 && activated==1 && lastAirborne,"made in the air at the edge entry, flying at once");
    check(rescueDeployed==1 && rescueFlown && rescueHeli==objects[0] && !std::memcmp(rescueTarget,sea,12) && rescueRequester.obj==objects[31],
          "handed to the rescue with the request's point, flown here, for this machine's player (the requester)");
    check(deployments[0].delivered && std::wcscmp(status,L"map line")==0,"the rescue never writes the map's support line");
    for(int i=0;i<3;++i){fixtureMs+=100;SupportDispatchTick();}
    check(heliCommands==0 && activated==1,"no arrival release, no second activation");
    {SupportPlan wrong=rp;wrong.count=4;wrong.units[3]=wrong.units[1];
     check(!Validate(wrong),"a rescue plan with both door gunners is refused (one door is the swimmer's)");
     wrong=rp;wrong.count=2;
     check(!Validate(wrong),"a rescue plan without its gunner is refused");
     wrong=rp;wrong.units[0].resourceId=kSupportAircraftResource+static_cast<unsigned>(rescue);
     check(!Validate(wrong),"a rescue hull on a runway is refused");
     check(Validate(rp),"a peer accepts the host's rescue plan");}
    // The map's call and the rescue do not hold each other up (2026-10-10, the user: 「不用受限制吧」).
    check(SupportRescueAt(sea,note,128) && rescueDeployed==2,"a second rescue right after: no cooldown");
    ResetSupportDispatch();fixtureMs+=40000;made=0;routeResult=npc::navigation::Result::pending;
    check(SupportCallAt(21,target,note,128) && offlinePending,"a map call is planning its ground route");
    check(SupportRescueAt(sea,note,128) && made==3 && offlinePending && planning.catalog==21u,
          "a rescue meanwhile is made at once and leaves the map's planning as it was");
    routeResult=npc::navigation::Result::moving;
    missionEnvironment=support::Environment::noExternalSupport;
    ResetSupportDispatch();fixtureMs+=40000;made=0;
    check(SupportRescueAt(sea,note,128) && made==3,"a mission that forbids external air support still gets its rescue");
    {SupportPlan rescuePlan=deployments[0].plan;check(Validate(rescuePlan),"and a peer validates it there");}
    // The mission's rule refuses a map call at once (SupportCallForbidden): nothing queued, planned or made.
    check(!SupportCallAt(0,target,note,128) && !offlinePending && made==3 && std::wcsstr(status,L"本关限制"),
          "a map air call there is refused at once by the mission's rule");
    check(SupportCallForbidden(0) && !SupportCallForbidden(AirCount()) && !SupportCallForbidden(AirCount()+1),
          "NO_SUPPORT: the air call forbidden, the infantry not");
    missionEnvironment=support::Environment::normal;
    // Underground (the user, 2026-10-14: 「只留步兵+钻地车」): only soldiers; the aircraft, the support hulls, the
    // transports, the airdrops and the submarine carrier all refused at once, said as underground.
    missionUnderground=true;
    ResetSupportDispatch();fixtureMs+=40000;made=0;
    {
        bool othersForbidden=true;
        for(int i=0;i<SupportMenuCount();++i)
            if(!InfantryCatalog(static_cast<std::uint32_t>(i)))othersForbidden=othersForbidden && SupportCallForbidden(i);
        check(othersForbidden && !SupportCallForbidden(AirCount()) && !SupportCallForbidden(AirCount()+1),
              "underground every map call but the infantry is forbidden");
        check(SupportCallForbidden(16) && !SupportCallForbidden(SupportRescueCatalog()),"the submarine carrier too; the rescue is not the map's");
    }
    check(!SupportCallAt(0,target,note,128) && !offlinePending && std::wcsstr(status,L"地下"),"an air call underground: refused, said why");
    check(!SupportCallAt(GroundStart(),target,note,128) && !offlinePending && std::wcsstr(status,L"地下"),"a tank underground: refused");
    check(SupportCallAt(AirCount(),target,note,128) && offlinePending,"the infantry underground: planned");
    missionUnderground=false;
    ResetSupportDispatch();fixtureMs+=40000;made=0;
    // Takeoff from the carrier's deck (2026-10-10, the user: 「能从机场起飞就从机场起飞吧」): the deck spot nearest the
    // swimmer, moved into the deck, handed to the takeoff planner; the hull made on it.
    ResetSupportDispatch();fixtureMs+=40000;made=0;carrierOut=true;takeoffSpots=-1;
    check(SupportRescueAt(sea,note,128) && takeoffSpots==1,"one takeoff point: the carrier's deck");
    check(At<float>(objects[0],kPosition+4)==carrierDeck[1]+support::kTakeoffLift && std::fabs(At<float>(objects[0],kPosition)-carrierDeck[0])<2.0f,
          "the hull is made on the deck, kTakeoffLift over it, not at the edge");
    // A pad a helicopter stood on (helipad.h) is a takeoff point like the deck: both handed to the planner.
    ResetSupportDispatch();fixtureMs+=40000;made=0;padCount=2;
    check(SupportRescueAt(sea,note,128) && takeoffSpots==3,"the deck and both pads are the takeoff candidates");
    ResetSupportDispatch();fixtureMs+=40000;made=0;carrierOut=false;
    check(SupportRescueAt(sea,note,128) && takeoffSpots==2 && At<float>(objects[0],kPosition)==padAt[0][0] &&
          At<float>(objects[0],kPosition+4)==padAt[0][1]+support::kTakeoffLift,"no carrier: it takes off from a pad");
    padCount=0;carrierOut=true;
    ResetSupportDispatch();fixtureMs+=40000;made=0;takeoffRefusal=support::Refusal::noEntry;
    check(SupportRescueAt(sea,note,128) && At<float>(objects[0],kPosition)==-1400,"no clear climb out from the deck: the edge instead");
    takeoffRefusal=support::Refusal::none;carrierOut=false;
    // Refusals come back with their reason (heli.cpp logs it, shows it and asks again).
    ResetSupportDispatch();fixtureMs+=40000;made=0;
    config.seaRescue=false;
    check(!SupportRescueAt(sea,note,128) && !made && std::wcsstr(note,L"海上救援"),"SeaRescue=0: refused, the reason returned");
    config.seaRescue=true;
    peersRescue=false;
    check(!SupportRescueAt(sea,note,128) && !made && std::wcscmp(note,hudtext::Tr(hudtext::Tx::supportRescueNeedsUpdate))==0,
          "a peer without the rescue entry: refused before anything exists, and said why");
    peersRescue=true;planRefusal=support::Refusal::noSky;
    check(!SupportRescueAt(sea,note,128) && !made && std::wcsstr(note,L"开放天空"),"no open sky: refused with its reason");
    planRefusal=support::Refusal::noEntry;
    check(!SupportRescueAt(sea,note,128) && !made && std::wcsstr(note,L"航线"),"no air corridor: refused with its reason");
    planRefusal=support::Refusal::none;transportReady=false;
    check(!SupportRescueAt(sea,note,128) && !made && std::wcsstr(note,L"救援直升机资源"),"its 410 not preloaded: refused with why");
    transportReady=true;
    planRefusal=support::Refusal::none;
    seatFail=true;deleted=0;const int handed=rescueDeployed;
    check(!SupportRescueAt(sea,note,128) && rescueDeployed==handed && deleted==3,"a crew that cannot take its seats: rolled back, nothing handed over");
    seatFail=false;
    // Online through the host: the request travels; its terminal status reaches the rescue once, and the map's own
    // request keeps its notices (its outcome is not the rescue's).
    ResetSupportDispatch();testOnline=true;soloHost=false;netRequests=0;rescueFailed=0;
    check(SupportCallAt(0,target,note,128),"a map call first");
    const auto mapRequest=noticeRequest;
    check(SupportRescueAt(sea,note,128) && netRequests==2 && rescueRequest==noticeRequest && !offlinePending,"online: the rescue is a host request");
    configuredHooks.notice(mapRequest,support_net::RequestStatus::active);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"已出动")!=nullptr && rescueFailed==0,"the map's earlier request still gets its own outcome");
    configuredHooks.notice(rescueRequest,support_net::RequestStatus::refused);
    configuredHooks.notice(noticeRequest,support_net::RequestStatus::timeout);
    check(rescueFailed==1,"the host's refusal reaches the rescue once");
    SupportCallStatus(note,128);
    check(std::wcsstr(note,L"已出动")!=nullptr,"and is not shown on the map's line");
    SupportCallAt(0,target,note,128);
    configuredHooks.notice(noticeRequest,support_net::RequestStatus::refused);
    check(rescueFailed==1,"a map call's refusal is no rescue failure");
    // The host's committed plan: flown on the host for the transaction's requester, a copy on a peer.
    SupportPlan hostRescue;rescueDeployed=0;activated=0;hostRequester=ObjRef::Of(objects[30]);
    check(Plan(static_cast<std::uint32_t>(rescue),sea,0,&hostRescue)==support_net::PlanResult::ready && Spawn(40,hostRescue,false) &&
          rescueDeployed==1 && rescueFlown && activated==1 && rescueRequester.obj==objects[30],
          "host: registered, flown, handed over for the requester by identity");
    hostRequester=ObjRef{};
    check(Spawn(41,hostRescue,true) && rescueDeployed==2 && !rescueFlown && !rescueRequester && activated==1,"peer: its copy handed over, not flown");
    check(Spawn(42,hostRescue,false) && rescueDeployed==3 && rescueFlown && !rescueRequester,
          "host: a requester not found is handed over as none (the rescue sends the heli away)");
    testOnline=false;
    // The play area not measured yet (the first seconds of a mission, playarea.cpp): the plan waits; offline nothing waits
    // here, so the rescue says why and heli.cpp asks again. The map's planning is left as it was.
    ResetSupportDispatch();testOnline=false;fixtureMs+=40000;made=0;areaMeasured=false;
    check(!SupportRescueAt(sea,note,128) && !made && std::wcscmp(note,hudtext::Tr(hudtext::Tx::rescueAreaPending))==0,
          "area not measured: no heli, and the reason returned (not an empty note)");
    {SupportPlan waiting;check(Plan(static_cast<std::uint32_t>(rescue),sea,0,&waiting)==support_net::PlanResult::pending && !planning.active,
                               "the host's rescue plan waits for the area and leaves the map's planning untouched");}
    areaMeasured=true;
    check(SupportRescueAt(sea,note,128) && made==3,"measured: the rescue comes");
    // A map order never sends the rescue heli off (it is the rescue's, heli.cpp flies it to the swimmer).
    heliHull=true;heliLeaves=0;
    check(!SupportWithdrawVehicle(objects[0]) && heliLeaves==0,"WITHDRAW: the rescue heli is not a map's support vehicle to send off");
    heliHull=false;
    // A composed load in planning for a map call is not the rescue's (it plans no load and touches no planning).
    ResetSupportDispatch();fixtureMs+=40000;made=0;routeResult=npc::navigation::Result::pending;
    {SupportLoadout squad{};squad.count=4;for(auto& w:squad.soldier)w=SupportWeapon::wingLance;
     check(SupportCallComposedAt(21,target,&squad,note,128) && offlinePending,"a composed map call is planning");
     SupportDispatchTick();
     const auto loadout=planning.loadout;const bool composed=planning.composed;
     check(SupportRescueAt(sea,note,128) && made==3 && planning.loadout==loadout && planning.composed==composed && planning.catalog==21u,
           "a rescue meanwhile leaves the map's composed load as it was");}
    routeResult=npc::navigation::Result::moving;ResetSupportDispatch();
    }
    ResetSupportDispatch();testOnline=true;config.enabled=true;
    const int ticksBefore=protocolTicks;
    unsigned char remoteActor[0x400]{};remoteActor[kHumanPlayer]=1;
    ++fixtureMs;SupportActorFrame(remoteActor);
    check(protocolTicks==ticksBefore+1,"remote actor without local input advances the support protocol");
    SupportActorFrame(objects[0]);
    check(protocolTicks==ticksBefore+1,"other actors in the same scene frame do not double-step support");
    ++fixtureMs;SupportActorFrame(objects[0]);
    check(protocolTicks==ticksBefore+2,"NPC-only update continues service progress next frame");
    SupportActorFrame(nullptr);
    check(protocolTicks==ticksBefore+2,"missing actor cannot create a service frame");
    std::printf("support_dispatch_test: %d checks passed\n",checks);
    return 0;
}
#endif
