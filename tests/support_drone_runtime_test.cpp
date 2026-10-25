// Execute production support-drone prepare/activate/delete and common Launch, with native services as fixtures.
#include "../src/jet_spawn.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace fixture {
using namespace crew;
alignas(16) unsigned char vehicle[0x2200]{},control[16]{},seat[edf::kSeatStride]{};
alignas(16) unsigned char parent[0x400]{},parentCtrl[16]{};
float matrixSeen[16]{};
Config config{};
bool inheritOk=true,entryOk=true,createOk=true,slotOk=true,hookOk=true;
unsigned creates=0,deletes=0,attaches=0,forgets=0,publishes=0,releases=0,terrain=0,failures=0,checks=0;
const void* seenParent=nullptr;
ObjRef seenChild{};
jet::Role role=jet::Role::drone;
unsigned serial=0,lastForget=0,firstForget=0,lastDelete=0,lastAttach=0,lastPublish=0;
void Check(bool pass,const char* label) {++checks;if(!pass){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
void __fastcall NativeTeam(void* object,int team,bool) {Put<int>(object,kTeam,team);}
unsigned char* __fastcall NativeCreate(void*,const float* matrix,const wchar_t* path,jet::InitParam*) {
    ++creates;++serial;std::memcpy(matrixSeen,matrix,64);
    if(!createOk)return nullptr;
    std::memset(vehicle,0,sizeof(vehicle));std::memset(control,0,sizeof(control));std::memset(seat,0,sizeof(seat));
    Put<const void*>(vehicle,kSelfCtrl,control);Put<LONG>(control,8,1);Put<LONG>(control,12,1);
    Put<void*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    std::memcpy(vehicle+kMatrix,matrix,64);
    for(const auto& body:jet::kBodies)if(!std::wcscmp(body.sgo,path)){role=body.role;break;}
    return vehicle;
}
void __fastcall NativeDelete(void* object) {++deletes;lastDelete=++serial;static_cast<unsigned char*>(object)[0x18]|=4;}
void Reset() {
    jet::ResetSupportDrones();
    for(auto& j:jet::jets)if(j.ref){jet::DropRef(j.ref);j={};}
    inheritOk=entryOk=createOk=slotOk=hookOk=true;
    creates=deletes=attaches=forgets=publishes=releases=terrain=serial=lastForget=firstForget=lastDelete=lastAttach=lastPublish=0;
    seenParent=nullptr;seenChild={};
    config.enabled=true;config.jetPilot=true;config.jetFuelSec=90;
    jet::spawnOk=true;jet::levelOk=jet::bodyPartOk=false;jet::preloaded[static_cast<int>(jet::Body::drone)]=true;
    std::memset(parent,0,sizeof(parent));std::memset(parentCtrl,0,sizeof(parentCtrl));
    Put<void*>(parent,kSelfCtrl,parentCtrl);Put<LONG>(parentCtrl,8,1);
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
void HeliSortie(unsigned char*,SortiePass,const float*) noexcept {}   // a sortie heli's pass (heli.cpp, not linked here)
bool HeliCommand(const void*,const Command&,const ObjRef&) noexcept {return false;}
bool HeliLeft(const void*) noexcept {return false;}
bool ProteusReady() noexcept {return false;}
bool MoveAreaBox(float*,float*) noexcept {return false;}
bool NpcDriver(const unsigned char*) noexcept {return false;}
bool SeatNpcRider(unsigned char*,bool) noexcept {return false;}
bool OnlineMaySeatNpc(const void*) noexcept {return true;}
void NoteLocalCopy(const void*,const void* parent) noexcept {fixture::seenParent=parent;}
bool ReadNativeObjectId(const unsigned char*,unsigned char* id) noexcept {if(id){std::memset(id,0,32);id[0]=1;return true;}return false;}
bool FixBodyPart506(unsigned char*,const char*) noexcept {return true;}
int FaultLog(const char*,const EXCEPTION_POINTERS*) noexcept {return EXCEPTION_EXECUTE_HANDLER;}
bool PrepareNpcVehicle(unsigned char*,bool) noexcept {return true;}
bool PvpInheritSupport(const ObjRef& parent,const ObjRef* children,unsigned count) noexcept {
    ++fixture::attaches;fixture::lastAttach=++fixture::serial;fixture::seenParent=parent.obj;
    if(count==1 && children)fixture::seenChild=children[0];
    return count==1 && children && parent.obj==fixture::parent && parent.ctrl==fixture::parentCtrl && fixture::inheritOk;
}
bool ReserveCarrierSupportDrone(const ObjRef&,unsigned,std::uint64_t) noexcept {return true;}
bool CancelCarrierSupportDrone(const ObjRef&,unsigned,std::uint64_t) noexcept {return true;}
void PvpForgetSupportObject(const ObjRef&) noexcept {++fixture::forgets;fixture::lastForget=++fixture::serial;if(!fixture::firstForget)fixture::firstForget=fixture::lastForget;}
void PreloadUltimate(void*) noexcept {}   // jet_spawn.cpp's PreloadJets loads the ultimate's circles (ultimate.cpp, not linked here)
namespace jet {
Jet jets[kMaxJets]{};
void SortieBegin(Jet&,SortiePass,const float*,const float*,const float*,ULONGLONG) noexcept {}   // jet_sortie.cpp, not linked here
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
    for(auto& j:jets)if(!j.ref){j={};j.ref=ObjRef::Of(object);HoldRef(j.ref);j.role=fixture::role;j.seen=ms;return &j;}
    return nullptr;
}
void ReleaseEntry(Jet& j) noexcept {++fixture::releases;PvpForgetSupportObject(j.ref);DropRef(j.ref);j={};}
bool SlotFree() noexcept {return fixture::slotOk;}
bool HooksOk() noexcept {return fixture::hookOk;}
void JoinFlight(Jet& j,unsigned flight) noexcept {j.flight=flight;}
bool IsJetVehicle(const unsigned char*,Role* role,Role*) noexcept {if(role)*role=fixture::role;return true;}
bool DollMake(int,const unsigned char*,DWORD) noexcept {return true;}
bool DollReady() noexcept {return true;}
bool PreloadDolls(void*,bool) noexcept {return true;}
void PreloadShells(void*,bool) noexcept {}
void ResetShells() noexcept {}
void Publish(bool) noexcept {++fixture::publishes;fixture::lastPublish=++fixture::serial;}
}
}
int main() {
    using namespace crew;using namespace fixture;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    Jump(jet::kCreateObject,reinterpret_cast<const void*>(&NativeCreate));
    Jump(kSetTeam,reinterpret_cast<const void*>(&NativeTeam));Jump(jet::kDelete,reinterpret_cast<const void*>(&NativeDelete));
    Put<void*>(image,jet::kObjectMgr,vehicle);
    float matrix[16]{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;matrix[12]=101;matrix[13]=-22;matrix[14]=303;
    const float anchor[]={31,32,33};
    Reset();Check(SupportDroneReady(),"preloaded native drone is ready");
    auto* v=PrepareSupportDrone(matrix,anchor,parent);
    Check(v==vehicle && creates==1 && attaches==1 && deletes==0,"prepare attaches one new native drone");
    Check(!std::memcmp(matrixSeen,matrix,64) && !std::memcmp(vehicle+kMatrix,matrix,64) && terrain==0,"prepare preserves exact host matrix without terrain query");
    auto* j=jet::FindJet(v);const auto ref=ObjRef::Of(v);
    Check(j && j->supportDrone && j->supportPending && j->launched && !jet::Flown(*j,100),"prepared entry remains unpublished and pending");
    Check(publishes==0 && lastAttach>0 && seenChild.obj==v && seenParent==parent,"ownership precedes first publication");
    Check(At<LONG>(control,12)==3,"entry and rollback record each retain weak identity");
    Check(ActivateSupportDrone(ref) && !j->supportPending && publishes==1 && jet::Flown(*j,100),"activation is the first publication");
    Check(lastPublish>lastAttach && ActivateSupportDrone(ref) && publishes==1,"activation is idempotent and follows owner inheritance");
    Check(DeleteSupportDrone(ref) && deletes==1 && releases==1 && At<LONG>(control,12)==1,"delete releases transaction and flight weak identities");
    Check(firstForget<lastDelete && lastForget>lastDelete && !DeleteSupportDrone(ref),"forget precedes native delete and repeated delete is refused");
    Reset();inheritOk=false;
    Check(!PrepareSupportDrone(matrix,anchor,parent) && deletes==1 && publishes==0 && !jet::jets[0].ref,"inherit failure deletes before entry or publication");
    Check(lastForget<lastDelete && At<LONG>(control,12)==1,"inherit rollback forgets before native delete");
    Reset();entryOk=false;
    Check(!PrepareSupportDrone(matrix,anchor,parent) && deletes==1 && publishes==0 && lastForget<lastDelete,"entry capacity failure forgets and deletes attached object");
    Reset();createOk=false;Check(!PrepareSupportDrone(matrix,anchor,parent) && attaches==0 && deletes==0,"native create failure has no owner side effects");
    Reset();matrix[4]=std::numeric_limits<float>::infinity();
    Check(!PrepareSupportDrone(matrix,anchor,parent) && creates==0,"invalid exact matrix rejected before creation");matrix[4]=0;
    Reset();v=PrepareSupportDrone(matrix,anchor,parent);const auto dead=ObjRef::Of(v);j=jet::FindJet(v);
    // The normal sweep can drop a destroyed entry before the reliable transaction retires it.
    jet::ReleaseEntry(*j);Put<LONG>(control,8,0);
    Check(DeleteSupportDrone(dead) && deletes==0 && At<LONG>(control,12)==1,"rollback record survives prior entry release and native object death");
    Reset();v=PrepareSupportDrone(matrix,anchor,parent);const auto old=ObjRef::Of(v);
    unsigned char replacement[16]{};Put<LONG>(replacement,8,1);Put<void*>(vehicle,kSelfCtrl,replacement);
    Check(!ActivateSupportDrone(old) && DeleteSupportDrone(old) && deletes==0,"weak identity replacement never activates or deletes another object");
    Reset();
    for(const auto body:{jet::Body::drone,jet::Body::blast,jet::Body::doll}) {
        jet::preloaded[static_cast<int>(body)]=true;
        const float from[]={0,200,0},heading[]={0,0,1};
        auto* launched=jet::Launch(body,from,heading,anchor,90,50,parent);
        Check(launched && attaches>0 && lastPublish>lastAttach && seenParent==parent,"all carrier drone kinds inherit before common Launch publication");
        if(launched)jet::ReleaseEntry(*launched);
    }
    jet::ResetSupportDrones();
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    if(failures)return 1;
    std::printf("support drone runtime: %u checks passed\n",checks);return 0;
}
