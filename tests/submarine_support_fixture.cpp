// Execute the production submarine support lifecycle against a small native ABI.
// No game DLL, hooks, installed resources or live session are required.
#include "../src/subcarrier.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace submarine_fixture {
struct Object {
    alignas(16) unsigned char bytes[0x2400]{};
    alignas(16) unsigned char control[0x20]{};
    alignas(16) unsigned char seats[edf::kSeatStride]{};
    alignas(16) unsigned char driver[0x2100]{};
};
Object hull{},ownerActor{};
crew::Config config{};
crew::Sea sea=crew::Sea::water;
float waterY=100.0f,groundY=-1000.0f;
bool session=false,host=true,runsHere=true,authority=true,bodyOk=true,fixOk=true,prepareOk=true;
bool createOk=true,correctBody=true,driverLiving=false,driverSoldier=true,driverPlayer=false,playerAboard=false,nativeIdOk=true;
unsigned creates=0,deletes=0,forgets=0,forgetOrder=0,deleteOrder=0,sequence=0;
unsigned prepareCalls=0,crewRequests=0,droneLaunches=0,enemyVisits=0,laserTicks=0,bodyFixes=0,weakDeletes=0,ownerResolves=0;
bool resolveOwner=true;unsigned char lastOwnerMarker[32]{};
unsigned char* presetDrone=nullptr;bool droneFlying=false;const void* watchedDeadObject=nullptr;unsigned deadObjectReads=0;
ULONGLONG milliseconds=1000,frame=1;
float createdMatrix[16]{};
bool modelBones=false;alignas(16) unsigned char boneRecords[2][0x140]{};int boneTag=1;
int checks=0,failures=0;
void Check(bool value,const char* why,int line) {
    ++checks;if(!value){++failures;std::fprintf(stderr,"line %d: %s\n",line,why);}
}
#define CHECK(value) submarine_fixture::Check(!!(value),#value,__LINE__)
void __fastcall DeleteControl(void*) noexcept {++weakDeletes;}
void MakeObject() {
    hull=Object{};
    static void* table[2]={nullptr,reinterpret_cast<void*>(&DeleteControl)};
    edf::Put<void*>(hull.control,0,table);
    edf::Put<LONG>(hull.control,8,1);edf::Put<LONG>(hull.control,12,1);
    edf::Put<void*>(hull.bytes,edf::kSelf,hull.bytes);edf::Put<void*>(hull.bytes,edf::kSelfCtrl,hull.control);
    edf::Put<void*>(hull.bytes,edf::kSeats,hull.seats);edf::Put<std::uint64_t>(hull.bytes,edf::kSeatCount,1);
    edf::Put<float>(hull.bytes,crew::kHp,30000.0f);edf::Put<float>(hull.bytes,crew::kHpMax,30000.0f);
    edf::Put<void*>(hull.seats,edf::kSeatRider,hull.driver);
}
unsigned char* __fastcall Create(void*,const float* matrix,const wchar_t*,void*) noexcept {
    CHECK((reinterpret_cast<std::uintptr_t>(matrix)&15u)==0);
    ++creates;if(!createOk)return nullptr;
    MakeObject();std::memcpy(createdMatrix,matrix,sizeof(createdMatrix));
    std::memcpy(hull.bytes+edf::kMatrix,matrix,sizeof(createdMatrix));return hull.bytes;
}
void __fastcall SetTeam(void* object,std::int32_t team,bool registered) noexcept {
    edf::Put<int>(object,edf::kTeam,team);
    edf::Put<unsigned>(object,0x380,registered ? 0x40u : 0u);
}
void __fastcall Delete(void* object) noexcept {
    CHECK(object==hull.bytes);++deletes;deleteOrder=++sequence;
    hull.bytes[0x18]|=4;edf::Put<LONG>(hull.control,8,0);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(hull.control+12))==0)DeleteControl(hull.control);
}
void Jump(unsigned rva,const void* target) {
    unsigned char code[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto address=reinterpret_cast<std::uintptr_t>(target);std::memcpy(code+2,&address,8);
    std::memcpy(crew::image+rva,code,sizeof(code));
}
void Reset() {
    crew::ResetSubs();MakeObject();
    config=crew::Config{};config.debug=false;
    sea=crew::Sea::water;waterY=100.0f;groundY=-1000.0f;
    session=false;host=runsHere=authority=bodyOk=fixOk=prepareOk=createOk=correctBody=driverSoldier=true;
    driverLiving=driverPlayer=playerAboard=false;nativeIdOk=true;
    presetDrone=nullptr;droneFlying=false;watchedDeadObject=nullptr;deadObjectReads=0;
    creates=deletes=forgets=forgetOrder=deleteOrder=sequence=prepareCalls=crewRequests=droneLaunches=enemyVisits=laserTicks=bodyFixes=weakDeletes=0;
    milliseconds=1000;++frame;ownerResolves=0;resolveOwner=true;ownerActor=Object{};modelBones=false;std::memset(boneRecords,0,sizeof(boneRecords));
    edf::Put<void*>(ownerActor.bytes,edf::kSelfCtrl,ownerActor.control);edf::Put<LONG>(ownerActor.control,8,1);
    crew::spawnOk=crew::subOk=crew::preloaded=true;crew::broken=false;
    crew::player=crew::PlayerFix{};
}
void GoodPlan(float* matrix,float* afloat) {
    const float target[3]={250.0f,0.0f,400.0f},from[3]={250.0f,0.0f,-100.0f};
    CHECK(crew::PlanSupportSubmarine(target,from,matrix,afloat));
    CHECK(crew::ValidateSupportSubmarine(matrix,*afloat));
}
}
namespace edf {
bool Readable(const void* pointer,std::size_t,bool) noexcept {
    if(pointer && pointer==submarine_fixture::watchedDeadObject)++submarine_fixture::deadObjectReads;
    return pointer!=nullptr;
}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept {return true;}
unsigned SeatCount(const unsigned char* vehicle) noexcept {return vehicle ? At<unsigned>(vehicle,kSeatCount) : 0;}
Rider SeatRider(const unsigned char*,const unsigned char*) noexcept {return Rider::none;}
bool AnyPlayerIn(const unsigned char*,const unsigned char*) noexcept {return submarine_fixture::playerAboard;}
bool LivingSoldierInSeat(const unsigned char*,const unsigned char*) noexcept {return submarine_fixture::driverLiving;}
bool IsAnyPlayer(const unsigned char*) noexcept {return submarine_fixture::driverPlayer;}
bool RedirectCall(unsigned char*,void*,void*,bool& changed) noexcept {changed=false;return false;}
}
namespace crew {
unsigned char* image=nullptr;
PlayerFix player{};
const Config& Cfg() noexcept {return submarine_fixture::config;}
ULONGLONG GameMs() noexcept {return submarine_fixture::milliseconds;}
ULONGLONG GameFrame() noexcept {return submarine_fixture::frame;}
void Log(const char*,...) noexcept {}
bool InSession() noexcept {return submarine_fixture::session;}
bool OnlineHostOnly() noexcept {return submarine_fixture::host;}
bool OnlineRunsHere(const void*) noexcept {return submarine_fixture::runsHere;}
bool IsOnlineAuthority(const void*) noexcept {return submarine_fixture::authority;}
bool Body506Ok() noexcept {return submarine_fixture::bodyOk;}
PluginBody BodyOf(const void*) noexcept {return submarine_fixture::correctBody ? PluginBody::sub : PluginBody::none;}
float BodyMark(const void*) noexcept {return 0.0f;}
bool FixBodyPart506(unsigned char*,const char*) noexcept {++submarine_fixture::bodyFixes;return submarine_fixture::fixOk;}
bool BodyPartOk() noexcept {return true;}
void LevelVehicle(unsigned char*) noexcept {}
bool PrepareNpcVehicle(unsigned char*,bool) noexcept {++submarine_fixture::prepareCalls;return submarine_fixture::prepareOk;}
bool SeatNpcRider(unsigned char*,bool) noexcept {++submarine_fixture::crewRequests;return true;}
bool IsSoldierClass(const void*) noexcept {return submarine_fixture::driverSoldier;}
void NoteLocalCopy(const void*,const void*) noexcept {}
bool ResolveMarkIdentities(const unsigned char* marker,const unsigned char*,MarkIdentities* out) noexcept {
    ++submarine_fixture::ownerResolves;
    if(marker)std::memcpy(submarine_fixture::lastOwnerMarker,marker,32);
    if(out)*out={};
    if(!submarine_fixture::resolveOwner || !out)return false;
    out->player=ObjRef::Of(submarine_fixture::ownerActor.bytes);return true;
}
void PvpForgetSupportObject(const ObjRef& object) noexcept {
    CHECK(object.obj==submarine_fixture::hull.bytes);++submarine_fixture::forgets;submarine_fixture::forgetOrder=++submarine_fixture::sequence;
}

Sea SeaAt(float,float,float* surface) noexcept {if(surface)*surface=submarine_fixture::waterY;return submarine_fixture::sea;}
float MapRay(const float* from,const float* to,float* hit) noexcept {
    const float y=submarine_fixture::groundY;
    if(y>from[1] || y<to[1])return -1.0f;
    hit[0]=from[0];hit[1]=y;hit[2]=from[2];return from[1]-y;
}
float GameStep(ULONGLONG) noexcept {return 1.0f/60.0f;}
bool VisitEnemies(const unsigned char*,EnemyVisitor,void*) noexcept {++submarine_fixture::enemyVisits;return true;}
void CarrierLaserFrame(const unsigned char*) noexcept {++submarine_fixture::laserTicks;}
unsigned char* JetLaunchDrone(const float*,const float*,const float*,DWORD,const void*,bool) noexcept {++submarine_fixture::droneLaunches;return submarine_fixture::presetDrone;}
bool JetFlying(const void* object,const void*) noexcept {return submarine_fixture::droneFlying && object==submarine_fixture::presetDrone;}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t* name) noexcept {
    if(!submarine_fixture::modelBones)return nullptr;
    return submarine_fixture::boneRecords[std::wcscmp(name,kSystems[0].bone)==0 ? 0 : 1];
}
void KeepPlayerViewProj(const void*,const float*) noexcept {}
bool FriendInLine(const float*,const float*,const void*) noexcept {return false;}
void HudDraw(const float*,void*,const void*,const CarrierPanel*,int) noexcept {}
bool MapHidesStockHud(const void*) noexcept {return false;}
bool SubmitSubmarineDrone(const ObjRef&,const float*,const float*,const float*,const unsigned char*) noexcept {++submarine_fixture::droneLaunches;return false;}
bool Body506MessageOk() noexcept {return true;}
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out) noexcept {
    if(!submarine_fixture::nativeIdOk || !object || !out)return false;
    std::memset(out,0,32);out[0]=7;out[4]=object==submarine_fixture::hull.bytes ? 1 : 2;return true;
}
}
namespace submarine_fixture {
void TestValidationAndPlanning() {
    Reset();float matrix[16]{},afloat=0.0f;GoodPlan(matrix,&afloat);
    CHECK(matrix[12]==250.0f && matrix[14]==1400.0f);
    CHECK(matrix[13]<=afloat);CHECK(afloat==waterY-crew::kDraft);
    float bad[16]{};
    auto Reject=[&](unsigned at,float value){std::memcpy(bad,matrix,sizeof(bad));bad[at]=value;CHECK(!crew::ValidateSupportSubmarine(bad,afloat));};
    Reject(0,2.0f);Reject(0,-1.0f);Reject(1,0.2f);Reject(4,0.2f);Reject(5,0.0f);Reject(8,1.0f);Reject(15,0.0f);
    Reject(12,std::numeric_limits<float>::infinity());Reject(13,std::numeric_limits<float>::quiet_NaN());Reject(13,afloat+1.0f);
    CHECK(!crew::ValidateSupportSubmarine(matrix,std::numeric_limits<float>::infinity()));
    CHECK(!crew::ValidateSupportSubmarine(matrix,afloat+1.0f));CHECK(!crew::ValidateSupportSubmarine(nullptr,afloat));
    sea=crew::Sea::unknown;CHECK(!crew::ValidateSupportSubmarine(matrix,afloat));
    const float target[3]={0,0,0};CHECK(!crew::PlanSupportSubmarine(target,nullptr,bad,&afloat));
    sea=crew::Sea::land;CHECK(!crew::PlanSupportSubmarine(target,nullptr,bad,&afloat));
    sea=crew::Sea::water;session=true;host=false;CHECK(!crew::PlanSupportSubmarine(target,nullptr,bad,&afloat));
    host=true;CHECK(crew::PlanSupportSubmarine(target,nullptr,bad,&afloat));
    const float invalid[3]={0,std::numeric_limits<float>::quiet_NaN(),0};CHECK(!crew::PlanSupportSubmarine(invalid,nullptr,bad,&afloat));
    CHECK(!crew::PlanSupportSubmarine(target,invalid,bad,&afloat));
}
void TestPrepareAndPending() {
    Reset();float matrix[16]{},afloat=0.0f;GoodPlan(matrix,&afloat);
        alignas(16) float transportMatrix[17]{};std::memcpy(transportMatrix+1,matrix,sizeof(matrix));
    CHECK((reinterpret_cast<std::uintptr_t>(transportMatrix+1)&15u)!=0);
    auto* object=crew::PrepareSupportSubmarine(transportMatrix+1,afloat);CHECK(object==hull.bytes);
    CHECK(creates==1 && prepareCalls==1 && bodyFixes==1 && crewRequests==0);
    CHECK(std::memcmp(matrix,createdMatrix,sizeof(matrix))==0);
    CHECK(edf::At<LONG>(hull.control,12)==2);
    auto* state=crew::FindSub(object);CHECK(state && state->pending && !state->ready);
    CHECK(state && std::memcmp(state->post,matrix+12,12)==0 && state->floor==afloat);
    object[crew::kFireGun]=object[crew::kFireMissile]=1;
    float lin[3]={1,2,3},ang[3]={4,5,6};CHECK(crew::SubBodyStep(object,lin,ang));
    CHECK(lin[0]==0 && lin[1]==0 && lin[2]==0 && ang[0]==0 && ang[1]==0 && ang[2]==0);
    CHECK(object[crew::kFireGun]==0 && object[crew::kFireMissile]==0);
    ++frame;crew::SubFrame(object);CHECK(enemyVisits==0 && droneLaunches==0);
    CHECK(!crew::ActivateSupportSubmarine(crew::ObjRef::Of(object)));
    CHECK(state && state->pending);
    CHECK(crew::DeleteSupportSubmarine(crew::ObjRef::Of(object)));
    CHECK(forgets==1 && deletes==1 && forgetOrder<deleteOrder);
    CHECK(edf::At<LONG>(hull.control,12)==0 && weakDeletes==1);CHECK(crew::FindSub(object)==nullptr);
}
void TestActivationAndRemote() {
    Reset();float matrix[16]{},afloat=0.0f;GoodPlan(matrix,&afloat);
    auto* object=crew::PrepareSupportSubmarine(matrix,afloat);CHECK(object);
    auto ref=crew::ObjRef::Of(object);const unsigned char offlineOwner[32]{};
    CHECK(crew::BindSupportSubmarineOwner(ref,offlineOwner));driverLiving=true;driverSoldier=false;
    CHECK(!crew::ActivateSupportSubmarine(ref));driverSoldier=true;driverPlayer=true;
    CHECK(!crew::ActivateSupportSubmarine(ref));driverPlayer=false;
    CHECK(crew::ActivateSupportSubmarine(ref));CHECK(!crew::FindSub(object)->pending);
    CHECK(crew::DeleteSupportSubmarine(ref));
    Reset();GoodPlan(matrix,&afloat);object=crew::PrepareSupportSubmarine(matrix,afloat);CHECK(object);ref=crew::ObjRef::Of(object);
    session=true;runsHere=false;authority=false;CHECK(!crew::ActivateSupportSubmarine(ref));
    CHECK(!crew::BindSupportSubmarineOwner(ref,offlineOwner));
    edf::Put<std::uint16_t>(object,0x128,1);
    CHECK(!crew::ActivateSupportSubmarine(ref));
    unsigned char owner[32]={3},otherOwner[32]={4},badOwner[32]{};badOwner[20]=1;
    CHECK(!crew::BindSupportSubmarineOwner(ref,badOwner));
    CHECK(crew::BindSupportSubmarineOwner(ref,owner));CHECK(!crew::BindSupportSubmarineOwner(ref,otherOwner));
    const auto key=crew::SubmarineSupportKey(ref);CHECK(key!=0);crew::ObjRef resolved{};
    CHECK(!crew::ResolveSupportSubmarine(key,&resolved));
    CHECK(crew::ActivateSupportSubmarine(ref));CHECK(crew::ResolveSupportSubmarine(key,&resolved));
    CHECK(resolved.obj==ref.obj && resolved.ctrl==ref.ctrl);
    Object child{};edf::Put<void*>(child.bytes,edf::kSelfCtrl,child.control);
    edf::Put<LONG>(child.control,8,1);edf::Put<LONG>(child.control,12,1);
    const auto childRef=crew::ObjRef::Of(child.bytes);
    CHECK(crew::AddSupportSubmarineDrone(ref,childRef));CHECK(edf::At<LONG>(child.control,12)==2);
    CHECK(crew::AddSupportSubmarineDrone(ref,childRef));CHECK(edf::At<LONG>(child.control,12)==2);
    CHECK(!crew::FindSub(object)->pending && driverLiving==false);
    float lin[3]={7,8,9},ang[3]={10,11,12};object[crew::kFireGun]=object[crew::kFireMissile]=1;
    CHECK(!crew::SubBodyStep(object,lin,ang));
    CHECK(lin[0]==7 && lin[1]==8 && lin[2]==9 && ang[0]==10 && ang[1]==11 && ang[2]==12);
    ++frame;crew::SubFrame(object);CHECK(enemyVisits==0 && droneLaunches==0);
    CHECK(object[crew::kFireGun]==1 && object[crew::kFireMissile]==1);
    playerAboard=true;CHECK(!crew::DeleteSupportSubmarine(ref));CHECK(forgets==0 && deletes==0);
    playerAboard=false;CHECK(crew::DeleteSupportSubmarine(ref));CHECK(forgetOrder<deleteOrder);
    CHECK(edf::At<LONG>(child.control,12)==1);CHECK(!crew::ResolveSupportSubmarine(key,&resolved));
}
void TestOwnerFollow() {
    Reset();float matrix[16]{},afloat=0.0f;GoodPlan(matrix,&afloat);
    auto* object=crew::PrepareSupportSubmarine(matrix,afloat);CHECK(object);
    edf::Put<std::uint16_t>(object,0x128,2);const unsigned char owner[32]={3};
    CHECK(crew::BindSupportSubmarineOwner(crew::ObjRef::Of(object),owner));
    auto* state=crew::FindSub(object);CHECK(state);if(!state)return;
    state->sea=false;std::memset(state->post,0,sizeof(state->post));
    crew::player.at=milliseconds;crew::player.pos[0]=-5000.0f;
    edf::Put<float>(ownerActor.bytes,edf::kPosition,5000.0f);
    crew::Follow(*state,1.0f);CHECK(state->post[0]>0 && state->post[2]==0);
    CHECK(ownerResolves==1 && std::memcmp(lastOwnerMarker,owner,32)==0);
    const float followed=state->post[0];resolveOwner=false;crew::Follow(*state,1.0f);
    CHECK(state->post[0]==followed); // missing owner never falls back to the host's local player
    resolveOwner=true;ownerActor.bytes[edf::kDead]=1;crew::Follow(*state,1.0f);CHECK(state->post[0]==followed);
    ownerActor.bytes[edf::kDead]=0;edf::Put<LONG>(ownerActor.control,8,0);crew::Follow(*state,1.0f);CHECK(state->post[0]==followed);
    edf::Put<LONG>(ownerActor.control,8,1);edf::Put<float>(ownerActor.bytes,edf::kPosition,std::numeric_limits<float>::quiet_NaN());
    crew::Follow(*state,1.0f);CHECK(state->post[0]==followed);
    std::memset(state->ownerNativeId,0,sizeof(state->ownerNativeId));std::memset(state->post,0,sizeof(state->post));
    state->moving=false;const auto resolvedBeforeLegacy=ownerResolves;crew::Follow(*state,1.0f);
    CHECK(state->post[0]<0 && ownerResolves==resolvedBeforeLegacy);
    CHECK(crew::DeleteSupportSubmarine(crew::ObjRef::Of(object)));
}
void TestNativeSubsystemSnapshots() {
    Reset();milliseconds=100000;float matrix[16]{},afloat=0.0f;GoodPlan(matrix,&afloat);
    auto* object=crew::PrepareSupportSubmarine(matrix,afloat);CHECK(object);if(!object)return;
    const auto ref=crew::ObjRef::Of(object);session=true;edf::Put<std::uint16_t>(object,0x128,2);
    const unsigned char owner[32]={3};CHECK(crew::BindSupportSubmarineOwner(ref,owner));
    driverLiving=true;CHECK(crew::ActivateSupportSubmarine(ref));auto* state=crew::FindSub(object);CHECK(state);if(!state)return;
    state->wear[0]=crew::PartHp(*state,0)*0.25f;
    state->wear[1]=crew::PartHp(*state,1);state->down[1]=true;state->downAt[1]=milliseconds-30000;
    state->emptyAt[0]=milliseconds-2000;state->missileAt=milliseconds-1000;state->droneAt=milliseconds-2000;
    state->surfacing=true;state->sea=true;
    state->turret[0].rec=boneRecords[0];state->turret[0].posed=true;state->turret[0].aim[0]=1.0f;
    crew::SubSupportState snapshots[3]{};
    CHECK(crew::EnumerateSubSupportStates(snapshots,3)==1);const auto sent=snapshots[0];
    CHECK(crew::sub_support::ValidState(sent));CHECK(sent.downMask==2 && sent.flags==crew::sub_support::kFlags);
    CHECK(sent.wear[0]==state->wear[0] && sent.wear[1]==state->wear[1]);
    CHECK(sent.repairRemainingMs[1]==60000 && sent.reloadRemainingMs[0]==10000);
    CHECK(sent.missileRemainingMs==1500 && sent.droneRemainingMs==8000);
    CHECK(sent.turretMask==1 && sent.turretAim[0][0]==1.0f);
    crew::SubSupportState read{};CHECK(crew::ReadSubSupportState(sent.netId,&read));CHECK(crew::sub_support::SameState(sent,read));
    auto wrong=sent;wrong.netId[31]^=1;const auto createdBefore=creates;
    CHECK(!crew::ReadSubSupportState(wrong.netId,&read));CHECK(read.netId[0]==0 && creates==createdBefore);
    CHECK(!crew::ApplySubSupportState(sent)); // an authority cannot consume a remote presentation update
    authority=false;CHECK(crew::EnumerateSubSupportStates(snapshots,3)==0);
    authority=true;runsHere=false;CHECK(crew::EnumerateSubSupportStates(snapshots,3)==0);
    CHECK(!crew::ApplySubSupportState(sent));authority=false;CHECK(!crew::ApplySubSupportState(sent)); // registered local bit is insufficient
    edf::Put<std::uint16_t>(object,0x128,1);state->pending=true;
    CHECK(!crew::ApplySubSupportState(sent));CHECK(!crew::ReadSubSupportState(sent.netId,&read));state->pending=false;
    unsigned char baseline[sizeof(*state)]{};std::memcpy(baseline,state,sizeof(*state));
    auto invalid=sent;invalid.wear[0]+=1.0f;invalid.wear[3]=crew::PartHp(*state,3)+1.0f;
    CHECK(!crew::ApplySubSupportState(invalid));CHECK(std::memcmp(baseline,state,sizeof(*state))==0);
    invalid=sent;invalid.wear[1]-=1.0f;CHECK(!crew::ApplySubSupportState(invalid));CHECK(std::memcmp(baseline,state,sizeof(*state))==0);
    CHECK(!crew::ApplySubSupportState(wrong));CHECK(std::memcmp(baseline,state,sizeof(*state))==0);
    crew::subs[1]=*state;crew::subs[1].weakHeld=false;
    CHECK(crew::EnumerateSubSupportStates(snapshots,3)==0);CHECK(!crew::ReadSubSupportState(sent.netId,&read));
    CHECK(!crew::ApplySubSupportState(sent));CHECK(std::memcmp(baseline,state,sizeof(*state))==0);crew::subs[1]=crew::Sub{};
    edf::Put<std::uint16_t>(object,0x128,0);CHECK(!crew::ReadSubSupportState(sent.netId,&read));CHECK(!crew::ApplySubSupportState(sent));
    edf::Put<std::uint16_t>(object,0x128,1);object[crew::kDead]=1;
    CHECK(!crew::ReadSubSupportState(sent.netId,&read));CHECK(!crew::ApplySubSupportState(sent));object[crew::kDead]=0;
    edf::Put<LONG>(hull.control,8,0);CHECK(!crew::ReadSubSupportState(sent.netId,&read));CHECK(!crew::ApplySubSupportState(sent));
    edf::Put<LONG>(hull.control,8,1);
    invalid=sent;invalid.wear[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!crew::ApplySubSupportState(invalid));
    invalid=sent;invalid.reloadRemainingMs[3]=crew::sub_support::kReloadMs+1;CHECK(!crew::ApplySubSupportState(invalid));
    CHECK(std::memcmp(baseline,state,sizeof(*state))==0);
    CHECK(crew::EnumerateSubSupportStates(nullptr,3)==0 && crew::EnumerateSubSupportStates(snapshots,0)==0);
    CHECK(!crew::ReadSubSupportState(sent.netId,nullptr));
    unsigned char nativeBefore[sizeof(hull.bytes)]{};std::memcpy(nativeBefore,hull.bytes,sizeof(nativeBefore));
    milliseconds=2000;CHECK(crew::ApplySubSupportState(sent)); // reload/drone timestamps become exactly zero
    CHECK(std::memcmp(nativeBefore,hull.bytes,sizeof(nativeBefore))==0 && creates==createdBefore);
    CHECK(state->remoteState && state->down[1] && state->wear[1]==sent.wear[1]);
    CHECK(state->downAt[1]==milliseconds-30000 && state->emptyAt[0]==milliseconds-2000);
    CHECK(state->missileAt==milliseconds-1000 && state->droneAt==milliseconds-2000);
    CHECK(crew::ReadSubSupportState(sent.netId,&read));CHECK(crew::sub_support::SameState(sent,read));
    const float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for(auto& record:boneRecords)std::memcpy(record+crew::kBoneLocal506,identity,sizeof(identity));
    modelBones=true;edf::Put<void*>(object+crew::kModelInst506,crew::kInstBones506,&boneTag);
    object[crew::kFireGun]=0xA5;object[crew::kFireMissile]=0x5A;const auto beforeAi=enemyVisits,beforeDrones=droneLaunches;
    milliseconds+=1000;++frame;crew::SubFrame(object);
    CHECK(enemyVisits==beforeAi && droneLaunches==beforeDrones && creates==createdBefore);
    CHECK(object[crew::kFireGun]==0xA5 && object[crew::kFireMissile]==0x5A);
    CHECK(std::memcmp(boneRecords[0]+crew::kBoneLocal506,identity,sizeof(identity))!=0);
    CHECK(std::memcmp(boneRecords[1]+crew::kBoneLocal506,identity,sizeof(identity))==0);
    CHECK(state->turret[0].posed && state->turret[0].aim[0]==1.0f);
    CHECK(crew::ReadSubSupportState(sent.netId,&read));
    CHECK(read.repairRemainingMs[1]==59000 && read.reloadRemainingMs[0]==9000);
    CHECK(read.missileRemainingMs==500 && read.droneRemainingMs==7000);
    milliseconds+=crew::kRepairMs;++frame;crew::SubFrame(object);
    CHECK(state->down[1] && state->wear[1]==sent.wear[1]); // presentation never runs the authority's repair/AI
    CHECK(enemyVisits==beforeAi && droneLaunches==beforeDrones);
    CHECK(crew::DeleteSupportSubmarine(ref));
}
void TestRemoteAdoptionAndFullCapacity() {
    Reset();session=true;runsHere=false;authority=false;config.subHullHp=999999.0f;
    edf::Put<std::uint16_t>(hull.bytes,0x128,1);
    edf::Put<float>(hull.bytes,crew::kHpMax,60000.0f);edf::Put<float>(hull.bytes,crew::kHp,12345.0f);
    unsigned char nativeBefore[sizeof(hull.bytes)]{};std::memcpy(nativeBefore,hull.bytes,sizeof(nativeBefore));
    auto* adopted=crew::Adopt(hull.bytes);CHECK(adopted && adopted->tier==2.0f);
    CHECK(std::memcmp(nativeBefore,hull.bytes,sizeof(nativeBefore))==0);
    CHECK(edf::At<float>(hull.bytes,crew::kHpMax)==60000.0f && edf::At<float>(hull.bytes,crew::kHp)==12345.0f);
    crew::ResetSubs();CHECK(edf::At<LONG>(hull.control,12)==1);
    Object occupied[crew::kMaxSubs]{};
    for(auto& object:occupied) {
        edf::Put<void*>(object.bytes,edf::kSelfCtrl,object.control);
        edf::Put<LONG>(object.control,8,1);edf::Put<LONG>(object.control,12,1);
        CHECK(crew::Adopt(object.bytes)!=nullptr);
    }
    CHECK(crew::LiveSubs()==crew::kMaxSubs);
    float lin[3]={13.0f,-17.0f,19.0f},ang[3]={-23.0f,29.0f,-31.0f};
    CHECK(!crew::SubBodyStep(hull.bytes,lin,ang));
    CHECK(lin[0]==13.0f && lin[1]==-17.0f && lin[2]==19.0f);
    CHECK(ang[0]==-23.0f && ang[1]==29.0f && ang[2]==-31.0f);
    CHECK(std::memcmp(nativeBefore,hull.bytes,sizeof(nativeBefore))==0 && crew::FindSub(hull.bytes)==nullptr);
    CHECK(enemyVisits==0 && droneLaunches==0 && creates==0);
    runsHere=true;authority=true;CHECK(crew::SubBodyStep(hull.bytes,lin,ang));
    CHECK(lin[0]==0 && lin[1]==0 && lin[2]==0 && ang[0]==0 && ang[1]==0 && ang[2]==0);
    crew::ResetSubs();for(const auto& object:occupied)CHECK(edf::At<LONG>(object.control,12)==1);
}
void TestOfflineBayWeakLifecycle() {
    Reset();float matrix[16]{},afloat=0.0f;GoodPlan(matrix,&afloat);
    auto* object=crew::PrepareSupportSubmarine(matrix,afloat);CHECK(object);if(!object)return;
    auto* state=crew::FindSub(object);CHECK(state);if(!state)return;
    auto* child=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    CHECK(child);if(!child){CHECK(crew::DeleteSupportSubmarine(crew::ObjRef::Of(object)));return;}
    alignas(16) unsigned char childControl[0x20]{};
    static void* controlTable[2]={nullptr,reinterpret_cast<void*>(&DeleteControl)};
    edf::Put<void*>(childControl,0,controlTable);edf::Put<LONG>(childControl,8,1);edf::Put<LONG>(childControl,12,1);
    edf::Put<void*>(child,edf::kSelfCtrl,childControl);presetDrone=child;droneFlying=true;
    state->surfacing=false;state->hasTarget=true;state->droneAt=0;milliseconds=20000;
    crew::Bay(*state,matrix,milliseconds);
    CHECK(state->drone[0].obj==child && state->droneWeak[0]);CHECK(edf::At<LONG>(childControl,12)==2);
    const auto initialForgets=forgets;crew::DropDrone(*state,0);
    CHECK(edf::At<LONG>(childControl,12)==1 && !state->drone[0] && forgets==initialForgets);
    milliseconds+=crew::kBayGapMs;crew::Bay(*state,matrix,milliseconds);CHECK(edf::At<LONG>(childControl,12)==2);
    edf::Put<LONG>(childControl,8,0);
    CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(childControl+12))==1); // native destruction releases its implicit weak
    const auto controlDeletesBefore=weakDeletes;watchedDeadObject=child;deadObjectReads=0;
    DWORD oldProtect=0;CHECK(VirtualProtect(child,0x3000,PAGE_NOACCESS,&oldProtect)!=0);
    state->hasTarget=false;crew::Bay(*state,matrix,++milliseconds);
    CHECK(!state->drone[0] && !state->droneWeak[0]);CHECK(deadObjectReads==0);
    CHECK(edf::At<LONG>(childControl,12)==0 && weakDeletes==controlDeletesBefore+1);
    crew::Bay(*state,matrix,++milliseconds);CHECK(weakDeletes==controlDeletesBefore+1 && deadObjectReads==0);
    DWORD unusedProtect=0;CHECK(VirtualProtect(child,0x3000,oldProtect,&unusedProtect)!=0);watchedDeadObject=nullptr;
    edf::Put<LONG>(childControl,8,1);edf::Put<LONG>(childControl,12,1);
    state->hasTarget=true;state->droneAt=0;milliseconds+=crew::kBayGapMs;
    crew::Bay(*state,matrix,milliseconds);CHECK(edf::At<LONG>(childControl,12)==2);
    const auto beforeDropForgets=forgets;crew::DropSub(*state);
    CHECK(edf::At<LONG>(childControl,12)==1 && edf::At<LONG>(childControl,8)==1);
    CHECK(forgets==beforeDropForgets && weakDeletes==controlDeletesBefore+1); // live child remains owned by its own PVP/deployment lifecycle
    CHECK(edf::At<LONG>(hull.control,12)==1 && crew::FindSub(object)==nullptr);
    presetDrone=nullptr;droneFlying=false;VirtualFree(child,0,MEM_RELEASE);
}
void TestPrepareFailuresAndIdentity() {
    Reset();float matrix[16]{},afloat=0.0f;GoodPlan(matrix,&afloat);
    createOk=false;CHECK(!crew::PrepareSupportSubmarine(matrix,afloat));CHECK(creates==1 && deletes==0);
    createOk=true;prepareOk=false;CHECK(!crew::PrepareSupportSubmarine(matrix,afloat));CHECK(deletes==1 && crew::LiveSubs()==0);
    prepareOk=true;fixOk=false;CHECK(!crew::PrepareSupportSubmarine(matrix,afloat));CHECK(deletes==2 && crew::LiveSubs()==0);
    fixOk=true;correctBody=false;CHECK(!crew::PrepareSupportSubmarine(matrix,afloat));CHECK(deletes==3 && crew::LiveSubs()==0);
    correctBody=true;auto* object=crew::PrepareSupportSubmarine(matrix,afloat);CHECK(object);const auto ref=crew::ObjRef::Of(object);
    unsigned char otherControl[0x20]{};CHECK(!crew::ActivateSupportSubmarine({object,otherControl}));
    CHECK(!crew::DeleteSupportSubmarine({object,otherControl}));CHECK(crew::FindSub(object));
    const auto beforeDeath=forgets;object[crew::kDead]=1;crew::Sweep();CHECK(crew::FindSub(object)==nullptr);CHECK(forgets==beforeDeath+1);
    CHECK(edf::At<LONG>(hull.control,12)==1);
    Reset();GoodPlan(matrix,&afloat);object=crew::PrepareSupportSubmarine(matrix,afloat);CHECK(object);
    CHECK(edf::At<LONG>(hull.control,12)==2);crew::ResetSubs();CHECK(edf::At<LONG>(hull.control,12)==1);
    CHECK(crew::FindSub(object)==nullptr);(void)ref;
}
}
int main() {
    using namespace submarine_fixture;
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    CHECK(crew::image);if(!crew::image)return 1;
    edf::Put<void*>(crew::image,crew::kObjectMgr,hull.bytes);
    Jump(crew::kCreateObject,reinterpret_cast<const void*>(&Create));
    Jump(crew::kSetTeam,reinterpret_cast<const void*>(&SetTeam));
    Jump(crew::kDelete,reinterpret_cast<const void*>(&Delete));
    FlushInstructionCache(GetCurrentProcess(),crew::image,0x2100000);
    TestValidationAndPlanning();TestPrepareAndPending();TestActivationAndRemote();TestOwnerFollow();TestNativeSubsystemSnapshots();TestRemoteAdoptionAndFullCapacity();TestOfflineBayWeakLifecycle();TestPrepareFailuresAndIdentity();
    crew::ResetSubs();VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("submarine_support_fixture: %d checks, %d failed\n",checks,failures);return failures ? 1 : 0;
}
