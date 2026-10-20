// Execute production support-drone prepare/activate/delete and common Launch, with native services as fixtures.
#include "../src/jet_spawn.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace fixture {
using namespace crew;
alignas(16) unsigned char vehicle[0x2200]{},control[16]{},seat[edf::kSeatStride]{};
alignas(16) unsigned char parent[0x2200]{},parentCtrl[16]{},parentSeat[edf::kSeatStride]{},pilot[0x400]{},pilotCtrl[16]{};
float matrixSeen[16]{};
Config config{};
bool inheritOk=true,entryOk=true,createOk=true,slotOk=true,hookOk=true;
unsigned creates=0,deletes=0,attaches=0,forgets=0,publishes=0,releases=0,terrain=0,failures=0,checks=0;
const void* seenParent=nullptr;
ObjRef seenChild{};
jet::Role role=jet::Role::drone;
unsigned nextControl=0,dollCreates=0,submits=0;
jet::Role parentDrones=jet::Role::drone;
bool host=true,carrierClass=true,dollCreateOk=true,dollInheritOk=true;
bool nativeAuthority=false,npcDriver=false,playerHeld=false;
unsigned endpoint=0;wchar_t dollPaths[3][260]{};
std::uint64_t submitted=100;
alignas(16) unsigned char controls[128][16]{},decoy[0x1120]{},decoyControl[16]{};
unsigned serial=0,lastForget=0,firstForget=0,lastDelete=0,lastAttach=0,lastPublish=0;
void Check(bool pass,const char* label) {++checks;if(!pass){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
void __fastcall NativeTeam(void* object,int team,bool) {Put<int>(object,kTeam,team);}
unsigned char* __fastcall NativeCreate(void*,const float* matrix,const wchar_t* path,jet::InitParam*) {
    ++creates;++serial;std::memcpy(matrixSeen,matrix,64);
    if(!createOk)return nullptr;
    if(std::wcsstr(path,L"e_throw_decoyscreen_")) {
        if(!dollCreateOk)return nullptr;
        wcscpy_s(dollPaths[endpoint],path);
        ++dollCreates;std::memset(decoy,0,sizeof(decoy));std::memset(decoyControl,0,sizeof(decoyControl));
        Put<const void*>(decoy,0,image+0x17D3F88);Put<const void*>(decoy,kSelfCtrl,decoyControl);
        Put<LONG>(decoyControl,8,1);Put<LONG>(decoyControl,12,1);std::memcpy(decoy+kMatrix,matrix,64);return decoy;
    }
    std::memset(vehicle,0,sizeof(vehicle));std::memset(seat,0,sizeof(seat));
    auto* ctrl=controls[nextControl++%128];std::memset(ctrl,0,16);
    Put<const void*>(vehicle,kSelfCtrl,ctrl);Put<LONG>(ctrl,8,1);Put<LONG>(ctrl,12,1);
    Put<void*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    std::memcpy(vehicle+kMatrix,matrix,64);
    for(const auto& body:jet::kBodies)if(!std::wcscmp(body.sgo,path)){role=body.role;break;}
    return vehicle;
}
void __fastcall NativeDollSetup(void* object,const float* matrix,float,int) {
    Put<const void*>(object,0x10D0,matrix);
}
void __fastcall NativeDelete(void* object) {++deletes;lastDelete=++serial;static_cast<unsigned char*>(object)[0x18]|=4;}
void Reset() {
    jet::ResetSupportDrones();
    for(auto& j:jet::jets)if(j.ref){jet::DollFree(jet::IndexOf(j));jet::DropRef(j.ref);j={};}
    inheritOk=entryOk=createOk=slotOk=hookOk=carrierClass=dollCreateOk=dollInheritOk=true;
    creates=deletes=attaches=forgets=publishes=releases=terrain=serial=lastForget=firstForget=lastDelete=lastAttach=lastPublish=0;
    seenParent=nullptr;seenChild={};dollCreates=submits=0;nextControl=0;
    nativeAuthority=npcDriver=playerHeld=false;
    config.enabled=true;config.jetPilot=true;config.jetFuelSec=90;
    jet::spawnOk=true;jet::levelOk=jet::bodyPartOk=false;jet::preloaded[static_cast<int>(jet::Body::drone)]=true;
    std::memset(parent,0,sizeof(parent));std::memset(parentCtrl,0,sizeof(parentCtrl));
    Put<void*>(parent,kSelfCtrl,parentCtrl);Put<LONG>(parentCtrl,8,1);
    std::memset(parentSeat,0,sizeof(parentSeat));std::memset(pilot,0,sizeof(pilot));std::memset(pilotCtrl,0,sizeof(pilotCtrl));
    Put<void*>(parent,kSeats,parentSeat);Put<std::uint64_t>(parent,kSeatCount,1);
    Put<void*>(parentSeat,kSeatRider,pilot);Put<void*>(parentSeat,kSeatRiderCtrl,pilotCtrl);
    Put<LONG>(pilotCtrl,8,1);Put<const void*>(pilot,0,image+0x17CDF28);Put<float>(pilot,0x2F8,100.0f);
}
void Jump(unsigned rva,const void* target) {
    unsigned char code[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    std::memcpy(code+2,&target,8);std::memcpy(image+rva,code,sizeof(code));
}
}
namespace crew {
const JetWeaponRow* JetWeapon(int) noexcept {return nullptr;}   // stores.cpp (not linked here): no jet weapons to preload
unsigned char* image=nullptr;
const Config& Cfg() noexcept {return fixture::config;}
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept {return 100;}
float BodyMark(const void*) noexcept {return 0;}
bool IsHelicopter(const void*) noexcept {return false;}
float MapRay(const float*,const float*,float*) noexcept {++fixture::terrain;return -1;}
void HeliCalled(unsigned char*,bool,const float*,DWORD) noexcept {}
bool HeliCommand(const void*,const Command&,const ObjRef&) noexcept {return false;}
bool HeliLeft(const void*) noexcept {return false;}
bool ProteusReady() noexcept {return false;}
bool MoveAreaBox(float*,float*) noexcept {return false;}
bool NpcDriver(const unsigned char*) noexcept {return fixture::npcDriver;}
bool IsOnlineAuthority(const void*) noexcept {return fixture::nativeAuthority;}
bool OnlineRunsHere(const void*) noexcept {return fixture::nativeAuthority;}
bool PlayerJetHolds(const void*) noexcept {return fixture::playerHeld;}
bool SeatNpcRider(unsigned char*,bool) noexcept {return false;}
bool OnlineMaySeatNpc(const void*) noexcept {return true;}
void NoteLocalCopy(const void*,const void* parent) noexcept {fixture::seenParent=parent;}
bool ReadNativeObjectId(const unsigned char*,unsigned char* id) noexcept {if(id){std::memset(id,0,32);id[0]=1;return true;}return false;}
bool FixBodyPart506(unsigned char*,const char*) noexcept {return true;}
int FaultLog(const char*,const EXCEPTION_POINTERS*) noexcept {return EXCEPTION_EXECUTE_HANDLER;}
bool PrepareNpcVehicle(unsigned char*,bool) noexcept {return true;}
bool InSession() noexcept {return true;}
bool OnlineHostOnly() noexcept {return fixture::host;}
float GroundClearance(const float*) noexcept {return kNoGround;}
void ReportSupportFailure(std::uint64_t) noexcept {}
bool PvpInheritSupport(const ObjRef& parent,const ObjRef* children,unsigned count) noexcept {
    ++fixture::attaches;fixture::lastAttach=++fixture::serial;fixture::seenParent=parent.obj;
    if(count==1 && children)fixture::seenChild=children[0];
    return count==1 && children && parent.obj && parent.ctrl && fixture::inheritOk &&
        (children[0].obj!=fixture::decoy || fixture::dollInheritOk);
}
void PvpForgetSupportObject(const ObjRef&) noexcept {++fixture::forgets;fixture::lastForget=++fixture::serial;if(!fixture::firstForget)fixture::firstForget=fixture::lastForget;}
void PreloadUltimate(void*) noexcept {}   // jet_spawn.cpp's PreloadJets loads the ultimate's circles (ultimate.cpp, not linked here)
namespace jet {
Jet jets[kMaxJets]{};
float FerryTurn(const Kind&) noexcept {return 0;}
bool Alive(const ObjRef& ref) noexcept {
    return ref.obj && ref.ctrl && At<LONG>(ref.ctrl,8)>0 && At<const void*>(ref.obj,kSelfCtrl)==ref.ctrl &&
        !(static_cast<const unsigned char*>(ref.obj)[0x18]&4);
}
Jet* FindJet(const unsigned char* object) noexcept {
    for(auto& j:jets)if(j.ref.obj==object && j.ref.ctrl==At<const void*>(object,kSelfCtrl))return &j;
    return nullptr;
}
void HoldRef(const ObjRef& ref) noexcept {Put<LONG>(const_cast<void*>(ref.ctrl),12,At<LONG>(ref.ctrl,12)+1);}
void DropRef(const ObjRef& ref) noexcept {if(ref.ctrl)Put<LONG>(const_cast<void*>(ref.ctrl),12,At<LONG>(ref.ctrl,12)-1);}
Jet* NewEntry(unsigned char* object,ULONGLONG ms) noexcept {
    if(!fixture::entryOk)return nullptr;
    for(auto& j:jets)if(!j.ref){
        j={};j.ref=ObjRef::Of(object);HoldRef(j.ref);j.role=object==fixture::parent ? Role::carrier : fixture::role;
        j.carrier.drones=fixture::parentDrones;j.carrier.sorties=kCarrierSorties;j.seen=ms;return &j;
    }
    return nullptr;
}
void ReleaseEntry(Jet& j) noexcept {++fixture::releases;PvpForgetSupportObject(j.ref);DollFree(IndexOf(j));DropRef(j.ref);j={};}
bool SlotFree() noexcept {return fixture::slotOk;}
bool HooksOk() noexcept {return fixture::hookOk;}
void JoinFlight(Jet& j,unsigned flight) noexcept {j.flight=flight;}
bool IsJetVehicle(const unsigned char* object,Role* role,Role* drones) noexcept {
    if(object==fixture::parent && !fixture::carrierClass)return false;
    if(role)*role=object==fixture::parent ? Role::carrier : fixture::role;
    if(drones)*drones=fixture::parentDrones;return true;
}
void PreloadShells(void*,bool) noexcept {}
void ResetShells() noexcept {}
void Toward(const float* from,const float* to,float* out) noexcept {for(unsigned i=0;i<3;++i)out[i]=to[i]-from[i];Normalize(out);}
void SetMode(Jet& j,Mode mode,ULONGLONG ms) noexcept {if(j.mode!=mode){j.mode=mode;j.modeAt=ms;}}
void Publish(bool) noexcept {++fixture::publishes;fixture::lastPublish=++fixture::serial;}
}
}

namespace crew {
bool SubmitCarrierSupportRecall(const ObjRef& parent) noexcept {
    return fixture::host ? ApplyCarrierSupportRecall(parent)>0 : true;
}
std::uint64_t SubmitCarrierSupportDrone(const ObjRef& parent,const float*,const float*,const float*,SupportDroneKind,unsigned slot) noexcept {
    ++fixture::submits;if(!fixture::host)return kCarrierDroneRequestQueued;
    const auto token=++fixture::submitted;
    return ReserveCarrierSupportDrone(parent,slot,token) ? token : 0;
}
}

int main() {
    using namespace crew;using namespace fixture;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    const unsigned char setupSig[]={0x0F,0x28,0xC2,0xF3,0x0F,0x11,0x91,0xF4,0x02,0x00,0x00};
    std::memcpy(image+0x5ACC30,setupSig,sizeof(setupSig));
    Check(jet::InstallDolls(),"exact native Decoy profile enables staged doll activation");
    Jump(0x5ACC30,reinterpret_cast<const void*>(&NativeDollSetup));
    Jump(jet::kCreateObject,reinterpret_cast<const void*>(&NativeCreate));
    Jump(kSetTeam,reinterpret_cast<const void*>(&NativeTeam));Jump(jet::kDelete,reinterpret_cast<const void*>(&NativeDelete));
    Put<void*>(image,jet::kObjectMgr,vehicle);
    float matrix[16]{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;matrix[12]=101;matrix[13]=-22;matrix[14]=303;
    const float anchor[]={31,32,33};
    // Three endpoint simulations each execute the production slot/prepare/activate/doll/delete implementation.
    for(unsigned peer=0;peer<3;++peer)for(unsigned raw=0;raw<3;++raw) {
        Reset();endpoint=peer;host=peer==0;parentDrones=raw==1 ? jet::Role::blast : raw==2 ? jet::Role::doll : jet::Role::drone;
        jet::preloaded[static_cast<int>(raw==1 ? jet::Body::blast : raw==2 ? jet::Body::doll : jet::Body::drone)]=true;
        const auto parentRef=ObjRef::Of(parent);const auto kind=static_cast<SupportDroneKind>(raw);
        const std::uint64_t token=1000+peer*100+raw;
        Check(CarrierSupportDroneReady(parentRef,kind,2),"registered actual carrier admits its exact child kind and slot");
        Check(!CarrierSupportDroneReady(parentRef,kind,6),"out of range slot refused");
        Check(raw!=0 || !CarrierSupportDroneReady(parentRef,SupportDroneKind::blast,2),"gun carrier cannot silently fabricate blast body");
        auto* v=PrepareCarrierSupportDrone(matrix,anchor,parentRef,kind,2,token,raw==0);
        Check(v && creates==1 && publishes==0 && terrain==0 && !std::memcmp(matrixSeen,matrix,64),"each endpoint prepares exact host matrix without local publication or terrain");
        auto* c=jet::FindJet(parent);auto* d=jet::FindJet(v);const auto child=ObjRef::Of(v);
        Check(c && c->supportMetadata && !jet::Flown(*c,100),"missing guest parent is metadata only");
        Check(d && d->supportPending && d->drone.mother==parentRef.ctrl && d->drone.slot==2 && d->drone.token==token &&
              d->flight==c->flight && !jet::Flown(*d,100),"child retains complete kind slot token mother and shared flight");
        const int sorties=c->carrier.sorties;
        Check(!c->carrier.ordered,"pending ordered intent cannot redirect existing drones before confirmation");
        d->t.target=reinterpret_cast<const void*>(1);
        Check(ReserveCarrierSupportDrone(parentRef,2,token) && !ReserveCarrierSupportDrone(parentRef,2,token+1),"pending token is idempotent and excludes a second transaction");
        Check(!ConfirmCarrierSupportDrone(parentRef,2,token+1) && !CancelCarrierSupportDrone(parentRef,2,token+1),"wrong token cannot apply or cancel the slot");
        Check(ActivateSupportDrone(child) && ConfirmCarrierSupportDrone(parentRef,2,token),"all ACK completion activates and confirms exact slot");
        Check(c->carrier.sorties==sorties-1 && !c->carrier.transaction[2].pending && c->carrier.dock[2]==jet::kDroneOut,"confirmation spends exactly one sortie");
        Check(raw==0 ? c->carrier.ordered && !std::memcmp(c->carrier.order,anchor,12) && !d->t.target : !c->carrier.ordered,
              "only confirmed player order changes the mother anchor; NPC center behavior stays intact");
        Check(jet::Flown(*d,100000),"active native support remains flown after a remote seen clock expires");
        d->mode=jet::Mode::patrol;host=false;
        Check(jet::RecallDrones(parent,100)==1 && d->mode==jet::Mode::patrol && ApplyCarrierSupportRecall(parentRef)==0,
              "guest recall queues an authenticated host action and never changes local AI");
        host=true;Check(ApplyCarrierSupportRecall(parentRef)==1 && d->mode==jet::Mode::recover,
              "host applies recall to the actual active child");
        Check(ConfirmCarrierSupportDrone(parentRef,2,token) && c->carrier.sorties==sorties-1,"duplicate apply never spends twice");
        Check((raw==2 && dollCreates==1) || (raw!=2 && dollCreates==0),"doll is created on every endpoint, only for actual doll body");
        if(raw==2){Check(At<const void*>(decoy,0x10D0)!=nullptr && attaches==2,"doll follows and inherits after its drone ownership");
            Check(!std::wcscmp(dollPaths[0],dollPaths[peer]),"registered child ID selects identical doll resource on all endpoints");}
        if(peer==0)jet::Dock(*d,*c,1000); // peers retire on the host's reliable Destroy; they do not run the AI.
        const auto before=c->carrier.dock[2];
        Check(DeleteSupportDrone(child) && !jet::FindJet(v),"reliable destroy removes each native child and entry");
        Check(c->carrier.sorties==sorties-1 && (peer!=0 || c->carrier.dock[2]==before),"active retirement never refunds sortie and preserves host recovery rearm");
        Check(raw!=2 || At<const void*>(decoy,0x10D0)==nullptr,"reliable destroy clears every endpoint doll follow pointer");
        const auto next=token+10;
        Check(ReserveCarrierSupportDrone(parentRef,2,next),"authenticated next host plan ignores peer-local cooldown clocks");
        Check(!CancelCarrierSupportDrone(parentRef,2,token) && c->carrier.transaction[2].token==next,"late old Cancel cannot touch replacement slot");
        jet::Jet old{};old.drone.slot=2;old.drone.token=token;old.drone.mother=parentRef.ctrl;
        const auto newDock=c->carrier.dock[2];jet::Dock(old,*c,2000);jet::Detonate(old,c,0,2000);
        Check(c->carrier.dock[2]==newDock,"late old Dock and Detonate cannot change replacement cooldown");
        Check(CancelCarrierSupportDrone(parentRef,2,next) && CancelCarrierSupportDrone(parentRef,2,next),"matching cancellation is idempotent");
        Check(!ReserveCarrierSupportDrone(parentRef,2,next),"cancelled token cannot resurrect a slot");
    }
    // Reject a failed attachment before child entry/publication and release only that pending token.
    Reset();parentDrones=jet::Role::drone;inheritOk=false;
    const auto parentRef=ObjRef::Of(parent);
    Check(!PrepareCarrierSupportDrone(matrix,anchor,parentRef,SupportDroneKind::gun,1,3000),"inherit failure refuses staged carrier child");
    auto* c=jet::FindJet(parent);Check(c && !c->carrier.transaction[1].token && c->carrier.sorties==jet::kCarrierSorties,"failed pending child spends no sortie");
    // Online LaunchOne routes through the dispatcher; it never falls back to local Launch.
    Reset();host=true;parentDrones=jet::Role::drone;auto* carrier=jet::NewEntry(parent,100);
    Check(jet::PlayerLaunchDrone(parent,anchor,10000) && submits==1 && creates==0,"host online launch submits a transaction without creating local copy");
    host=false;carrier->carrier.launchAt=0;
    Check(jet::PlayerLaunchDrone(parent,anchor,10000) && creates==0 && submits==2 && !carrier->carrier.transaction[1].token,"guest queues host request without reservation or local copy");
    Check(!ReserveCarrierSupportDrone(ObjRef::Of(parent),1,kCarrierDroneRequestQueued),"queued placeholder cannot become a native slot token");
    // Readiness is pure class/identity/body validation, with no metadata construction or local cooldown gate.
    Reset();carrierClass=false;const auto invalidParent=ObjRef::Of(parent);
    Check(!CarrierSupportDroneReady(invalidParent,SupportDroneKind::gun,0) && creates==0,"noncarrier native class cannot use child resource");
    carrierClass=true;Put<LONG>(parentCtrl,8,0);
    Check(!CarrierSupportDroneReady(invalidParent,SupportDroneKind::gun,0),"dead parent weak identity is rejected");
    Put<LONG>(parentCtrl,8,1);Check(!CarrierSupportDroneReady(invalidParent,static_cast<SupportDroneKind>(3),0),"unknown kind is rejected");
    // A doll resource or ownership failure aborts activation while all combat/publication remains pending.
    for(unsigned failure=0;failure<2;++failure) {
        Reset();endpoint=0;parentDrones=jet::Role::doll;jet::preloaded[static_cast<int>(jet::Body::doll)]=true;
        const auto owner=ObjRef::Of(parent);
        auto* v=PrepareCarrierSupportDrone(matrix,anchor,owner,SupportDroneKind::doll,0,4000+failure);
        const auto child=ObjRef::Of(v);if(failure)dollInheritOk=false;else dollCreateOk=false;
        Check(v && !ActivateSupportDrone(child) && jet::FindJet(v)->supportPending && publishes==0,"failed doll activation never publishes or enters combat");
        Check(DeleteSupportDrone(child) && !jet::FindJet(v),"failed doll activation rolls back native child and pending slot");
    }
    // Host preflight owns cooldown/magazine admission; guest may only queue a request.
    Reset();host=true;const auto preflightParent=ObjRef::Of(parent);
    Check(CarrierSupportSlotAvailable(preflightParent,0),"host preflight accepts a real ready carrier slot");
    auto* preflight=jet::FindJet(parent);preflight->carrier.dock[0]=10000;
    Check(!CarrierSupportSlotAvailable(preflightParent,0),"host preflight enforces actual cooldown");
    preflight->carrier.dock[0]=0;preflight->carrier.sorties=0;
    Check(!CarrierSupportSlotAvailable(preflightParent,0),"host preflight enforces empty magazine");
    preflight->carrier.sorties=1;Check(ReserveCarrierSupportDrone(preflightParent,1,9000),"one remaining sortie can be reserved");
    Check(!CarrierSupportSlotAvailable(preflightParent,0),"host preflight accounts for other pending reserved sorties");
    Check(CancelCarrierSupportDrone(preflightParent,1,9000) && CarrierSupportSlotAvailable(preflightParent,0),"failed pending request releases reserved magazine");
    host=false;preflight->carrier.sorties=0;for(auto& ready:preflight->carrier.dock)ready=10000;slotOk=false;
    Check(!CarrierSupportSlotAvailable(preflightParent,0),"guest cannot perform authority-only preflight");
    Check(jet::PlayerLaunchDrone(parent,anchor,20000) && creates==0,"guest request ignores stale local cooldown magazine and live table capacity");
    // Failed all-peer preparation never commits a pending player retarget.
    Reset();host=true;
    auto* pending=PrepareCarrierSupportDrone(matrix,anchor,ObjRef::Of(parent),SupportDroneKind::gun,0,9100,true);
    auto* parentEntry=jet::FindJet(parent);
    Check(pending && !parentEntry->carrier.ordered && DeleteSupportDrone(ObjRef::Of(pending)) &&
          !parentEntry->carrier.ordered,"cancelled staged player order leaves the old mother command intact");
    // Receiving a valid host plan never consults a guest's stale cooldown or magazine estimate.
    Reset();host=true;const auto guestParent=ObjRef::Of(parent);
    Check(CarrierSupportSlotAvailable(guestParent,0),"fixture establishes metadata before a guest receives host plan");
    auto* guestMetadata=jet::FindJet(parent);guestMetadata->carrier.sorties=0;guestMetadata->carrier.dock[0]=jet::kDroneOut;host=false;
    auto* accepted=PrepareCarrierSupportDrone(matrix,anchor,guestParent,SupportDroneKind::gun,0,9200);
    Check(accepted && ActivateSupportDrone(ObjRef::Of(accepted)) && ConfirmCarrierSupportDrone(guestParent,0,9200),
          "guest prepares and applies authenticated host plan despite stale out marker and zero sorties");
    Check(guestMetadata->carrier.sorties==0 && DeleteSupportDrone(ObjRef::Of(accepted)),
          "guest estimate remains bounded and reliable deletion drains exact token");
    // Session may fail before SpawnDeployment exists. Teardown must drain the metadata token itself.
    Reset();host=true;const auto noChildParent=ObjRef::Of(parent);
    Check(ReserveCarrierSupportDrone(noChildParent,3,9300) && creates==0,"Session reserves before any child or Deployment exists");
    auto* noChild=jet::FindJet(parent);noChild->carrier.transaction[3].ordered=true;
    CancelCarrierSupportTransaction(9300);
    Check(!noChild->carrier.transaction[3].token && !noChild->carrier.transaction[3].pending &&
          !noChild->carrier.transaction[3].ordered && CarrierSupportSlotAvailable(noChildParent,3),
          "transaction teardown releases no-child reservation and pending order for host retry");
    Check(ReserveCarrierSupportDrone(noChildParent,3,9301),"a fresh Session can reuse the cancelled no-child slot");
    CancelCarrierSupportTransaction(9300);
    Check(noChild->carrier.transaction[3].token==9301,"late old Session cancellation cannot clear a new slot token");
    CancelCarrierSupportTransaction(0);CancelCarrierSupportTransaction(kCarrierDroneRequestQueued);
    Check(noChild->carrier.transaction[3].token==9301,"invalid transaction placeholders never mutate native slot state");
    CancelCarrierSupportTransaction(9301);
    // Old parent dies and its address is reused before Session cancellation. Only its cached exact token drains.
    Reset();host=true;const auto deadParent=ObjRef::Of(parent);
    Check(ReserveCarrierSupportDrone(deadParent,4,9400),"old weak parent reserves a no-child transaction");
    auto* deadEntry=jet::FindJet(parent);Put<LONG>(parentCtrl,8,0);
    unsigned char newParentControl[16]{};Put<LONG>(newParentControl,8,1);Put<LONG>(newParentControl,12,1);
    Put<void*>(parent,kSelfCtrl,newParentControl);const auto newParent=ObjRef::Of(parent);
    Check(ReserveCarrierSupportDrone(newParent,4,9401),"replacement parent has its own weak identity and token");
    auto* replacementEntry=jet::FindJet(parent);
    CancelCarrierSupportTransaction(9400);
    Check(!deadEntry->carrier.transaction[4].token && !deadEntry->carrier.transaction[4].pending &&
          replacementEntry->carrier.transaction[4].token==9401,
          "dead-parent cancellation needs no live/source lookup and cannot touch replacement parent");
    CancelCarrierSupportTransaction(9401);
    // Drain these fixture-only weak entries before their stack control block leaves scope.
    for(auto& entry:jet::jets)if(entry.ref){jet::DropRef(entry.ref);entry={};}
    // A guest's first drone creates only metadata on the host. After that guest
    // dismounts, the host's real NPC pilot must be able to take the same entry over.
    Reset();host=true;parentDrones=jet::Role::drone;
    auto* takeoverChild=PrepareCarrierSupportDrone(matrix,anchor,ObjRef::Of(parent),SupportDroneKind::gun,2,9500,true);
    auto* metadata=jet::FindJet(parent);
    Check(takeoverChild && metadata && metadata->supportMetadata,"guest carrier first creates host metadata without taking its flight");
    Check(ActivateSupportDrone(ObjRef::Of(takeoverChild)) && ConfirmCarrierSupportDrone(ObjRef::Of(parent),2,9500),
          "guest carrier transaction confirms before authority handoff");
    Check(ReserveCarrierSupportDrone(ObjRef::Of(parent),3,9501),"another child remains pending across authority handoff");
    metadata->carrier.dock[1]=12345;metadata->m.vel[0]=77.0f;
    const auto beforeHandoff=metadata->carrier;const auto flightBefore=metadata->flight;
    Check(!jet::CarrierMetadataReady(*metadata,5000) && metadata->supportMetadata,"remote player control cannot promote host metadata");
    nativeAuthority=true;
    Check(!jet::CarrierMetadataReady(*metadata,5000),"authority without actual NPC driver leaves metadata inert");
    npcDriver=true;playerHeld=true;
    Check(!jet::CarrierMetadataReady(*metadata,5000) && metadata->supportMetadata,"player-held carrier cannot start autonomous flight");
    playerHeld=false;
    Put<unsigned char>(pilot,kHumanPlayer,1);Put<void*>(pilot,kHumanPad,pilot);
    Check(!jet::CarrierMetadataReady(*metadata,5000),"actual player seat cannot promote metadata even during native authority transition");
    Put<unsigned char>(pilot,kHumanPlayer,0);Put<void*>(pilot,kHumanPad,nullptr);
    pilot[kDead]=1;Check(!jet::CarrierMetadataReady(*metadata,5000),"dead native pilot cannot promote metadata");pilot[kDead]=0;
    Put<float>(pilot,0x2F8,0.0f);Check(!jet::CarrierMetadataReady(*metadata,5000),"zero-HP native pilot cannot promote metadata");
    Put<float>(pilot,0x2F8,100.0f);
    Check(jet::CarrierMetadataReady(*metadata,5000) && !metadata->supportMetadata && jet::Flown(*metadata,5000),
          "native authority with real NPC pilot promotes existing metadata into autonomous flight");
    Check(!std::memcmp(&metadata->carrier,&beforeHandoff,sizeof(beforeHandoff)) && metadata->flight==flightBefore,
          "promotion preserves confirmed and pending tokens, dock clocks, player intent, sorties and flight identity");
    Check(metadata->fuelMs>0 && metadata->bornAt==5000 && metadata->seen==5000 && metadata->modeAt==5000 &&
          metadata->lastStep==0 && metadata->m.vel[0]==77.0f,
          "promotion starts a fresh fuel/step lifecycle while preserving measured carrier motion");
    const auto promotedBorn=metadata->bornAt;
    Check(jet::CarrierMetadataReady(*metadata,6000) && metadata->bornAt==promotedBorn,"promotion is idempotent and never renews fuel twice");
    Check(DeleteSupportDrone(ObjRef::Of(takeoverChild)),"active child remains independently removable after carrier promotion");
    CancelCarrierSupportTransaction(9501);
    // More dead transactions than the live jet table must retain bounded weak tombstones and still create anew.
    Reset();ObjRef old[40]{};
    for(unsigned i=0;i<40;++i) {
        auto* v=PrepareSupportDrone(matrix,anchor,parent);old[i]=ObjRef::Of(v);
        Check(v!=nullptr,"dead ledger history does not exhaust live jet capacity");
        Check(ActivateSupportDrone(old[i]),"each retained dead history belonged to an activated drone");
        auto* j=jet::FindJet(v);jet::ReleaseEntry(*j);Put<LONG>(const_cast<void*>(old[i].ctrl),8,0);
    }
    auto* fresh=PrepareSupportDrone(matrix,anchor,parent);const auto freshRef=ObjRef::Of(fresh);
    Check(fresh && freshRef.obj==old[0].obj && freshRef.ctrl!=old[0].ctrl,"new entity reuses address with a new weak control identity");
    const auto deletesBefore=deletes;
    Check(DeleteSupportDrone(old[0]) && deletes==deletesBefore && jet::Alive(freshRef),"late old rollback cannot delete a replacement at the same address");
    Check(DeleteSupportDrone(freshRef),"new live transaction remains independently deletable");
    for(unsigned i=1;i<40;++i)Check(DeleteSupportDrone(old[i]),"old dead weak tombstone drains independently");
    Reset();
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    if(failures)return 1;
    std::printf("carrier drone transactions: %u checks passed\n",checks);return 0;
}
