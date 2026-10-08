// Production spawn transaction with recording native services; no game required.
#include "../src/support_soldier.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "../src/support_net.h"

namespace crew {
unsigned char* image=nullptr;
Config cfg{};bool session=false,host=true;
const Config& Cfg() noexcept { return cfg; }
bool InSession() noexcept { return session; }
bool OnlineHostOnly() noexcept { return host; }
bool IsSoldierClass(const void* o) noexcept { return At<unsigned>(o,0)==0x534F4C44; }
void Log(const char*,...) noexcept {}
}
namespace {
using namespace crew;
alignas(16) unsigned char objects[32][0x2100]{},controls[32][16]{};
int made=0,deleted=0,preloads=0,failAt=-1,checks=0;
bool wrong=false,initGood=true,registerGood=true;
unsigned char registeredId[32]{};
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
unsigned char* __fastcall Create(void*,const float* m,const wchar_t* path,support_native::InitParam* init) {
    if(made==failAt)return nullptr;
    const int i=made++;
    initGood=initGood && init->vtable==image+support_native::kInitVtable;
    initGood=initGood && (reinterpret_cast<std::uintptr_t>(m)&15u)==0;
    for(unsigned char c:init->rest)initGood=initGood && c==0;
    initGood=initGood && (wcscmp(path,support_native::kBodies[0])==0 || wcscmp(path,support_native::kBodies[1])==0);
    auto* o=objects[i];auto* c=controls[i];std::memset(o,0,sizeof objects[i]);std::memset(c,0,16);
    Put<unsigned>(o,0,wrong ? 0 : 0x534F4C44);Put<void*>(o,0x28,o);Put<void*>(o,0x30,c);
    Put<LONG>(c,8,1);Put<LONG>(c,12,1);std::memcpy(o+0x90,m+12,12);
    return o;
}
void __fastcall Delete(void* o){++deleted;Put<unsigned char>(o,0x18,4);}
void __fastcall Team(void* o,int t,bool children){Check(t==2 && children,"native friend team");Put<int>(o,kTeam,t);}
void __fastcall Level(void*,float n){Check(n==1,"native difficulty multiplier");}
void __fastcall Follow(void* o,void* leader,bool keep){Check(!keep,"native follow keepOffset=false");Put<void*>(o,0x548,leader);}
void __fastcall Preload(void*,const wchar_t*,int kind,int all){Check(kind==2 && all==-1,"stock resource preload flags");++preloads;}
const unsigned char* __fastcall Register(void*,ObjRef* weak,const unsigned char* id) {
    // Native 781950 consumes the caller's weak argument once.
    InterlockedDecrement(reinterpret_cast<volatile LONG*>(const_cast<unsigned char*>(static_cast<const unsigned char*>(weak->ctrl))+12));
    auto* o=const_cast<void*>(weak->obj);
    Put<unsigned>(o,0x128,host ? 2u : 1u);Put<void*>(o,0x130,registeredId);
    std::memcpy(registeredId,id,32);
    return registerGood ? registeredId : nullptr;
}
void Setup() {
    using namespace support_native;
    ResetSupportSoldiers();made=deleted=preloads=0;failAt=-1;wrong=false;session=false;host=true;
    profile=true;faulted=false;create=&Create;destroy=&Delete;team=&Team;level=&Level;follow=&Follow;preload=&Preload;registerObject=&Register;
    Put<void*>(image,kObjectManager,objects);Put<void*>(image,kPreloadManager,controls);Put<void*>(image,kNetworkManager,registeredId);
    PreloadSupportSoldiers();
}
}
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x20C0000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(image!=nullptr,"private image");
    float poses[12][16]{};
    for(int i=0;i<12;++i){poses[i][0]=poses[i][5]=poses[i][10]=poses[i][15]=1;poses[i][12]=float(i*2);}
    Setup();alignas(16) support_net::Plan wirePlan{};
    for(unsigned i=0;i<support_net::kMaxUnits;++i) {
        std::memcpy(wirePlan.units[i].matrix,poses[i%12],sizeof poses[0]);
        ObjRef spawned;
        Check(ApplySupportSoldierSpawn(wirePlan.units[i].matrix,i%4==0,nullptr,&spawned) && initGood,
              "normal and leader wire matrices reach native CreateObject 16-aligned");
        Check(std::memcmp(static_cast<const unsigned char*>(spawned.obj)+kPosition,wirePlan.units[i].matrix+12,12)==0,
              "aligned native copy preserves requested position exactly");
    }
    Setup();Check(preloads==2 && SupportSoldiersReady(),"two Root resources queued for current mission");
    ObjRef one;Check(SpawnSupportSoldier(poses[0],&one) && initGood,"native InitParam and fixed real resource");
    Check(At<LONG>(one.ctrl,8)==1 && At<LONG>(one.ctrl,12)==2,"retain weak only, scene owns strong");
    Check(SupportSoldierHeld(one.obj),"new soldiers start held before network Active");
    Check(HoldSupportSoldier(one,false) && !SupportSoldierHeld(one.obj),"Active can release its exact soldier identity");
    Check(HoldSupportSoldier(one,true) && SupportSoldierHeld(one.obj),"same owned soldier can be held again");
    Check(!HoldSupportSoldier(ObjRef{objects[1],controls[1]},true) && !SupportSoldierHeld(objects[1]),"arbitrary mission NPC cannot be held");
    Put<unsigned char>(objects[0],kDead,1);
    Check(!HoldSupportSoldier(one,true) && !SupportSoldierHeld(one.obj),"dead soldier does not keep an AI hold");Put<unsigned char>(objects[0],kDead,0);
    Put<LONG>(controls[0],8,0);
    Check(!HoldSupportSoldier(one,false) && !SupportSoldierHeld(one.obj),"expired weak identity rejected before object access");Put<LONG>(controls[0],8,1);
    Put<void*>(objects[0],kSelfCtrl,controls[1]);Put<LONG>(controls[1],8,1);
    Check(!SupportSoldierHeld(objects[0]) && !HoldSupportSoldier(one,false) && !HoldSupportSoldier(ObjRef::Of(objects[0]),true),
          "reused address with another control block does not inherit ownership or hold");
    Put<void*>(objects[0],kSelfCtrl,controls[0]);
    Check(!DeleteSupportSoldier(ObjRef{objects[1],controls[1]}),"cannot delete arbitrary NPC");
    Check(DeleteSupportSoldier(one) && deleted==1 && At<LONG>(one.ctrl,12)==1,"owned rollback returns weak count");
    Check(!SupportSoldierHeld(one.obj) && !HoldSupportSoldier(one,true),"deleted entry cannot be held");
    Check(!DeleteSupportSoldier(one),"rollback idempotent");
    Setup();SupportSquad squad;
    Check(SpawnSupportSquad(SupportSquadKind::rangerPlatoon,poses[0],12,&squad),"platoon transaction succeeds");
    Check(squad.count==12 && squad.leaderCount==3,"three real four-person squads");
    for(unsigned i=0;i<12;++i)Check(At<const void*>(squad.members[i].obj,0x548)==(i%4 ? squad.leaders[i/4].obj : nullptr),"actual follow tree");
    Check(!FollowSupportSoldier(squad.leaders[0],squad.members[1]),"follow cycle is rejected");
    unsigned char playerSample[0x2100]{},playerCtrl[16]{};
    Put<void*>(playerSample,kSelfCtrl,playerCtrl);Put<LONG>(playerCtrl,8,1);
    Put<unsigned char>(playerSample,edf::kHumanPlayer,1);Put<void*>(playerSample,edf::kHumanPad,playerSample);
    Check(FollowSupportSoldier(squad.leaders[0],ObjRef::Of(playerSample)),"delivered crew can follow a live actual playerSample");
    ResetSupportSoldiers();Check(deleted==0,"new mission never calls delete into previous scene");
    for(int i=0;i<12;++i)Check(At<LONG>(controls[i],12)==1,"mission reset releases each retained weak once");
    Check(!DeleteSupportSoldier(squad.members[0]),"stale mission handle rejected");
    Check(!SupportSoldierHeld(squad.members[0].obj) && !HoldSupportSoldier(squad.members[0],true),"mission reset clears holds and ownership");
    Setup();failAt=2;Check(!SpawnSupportSquad(SupportSquadKind::rangerSquad,poses[0],4,&squad),"partial batch fails");
    Check(deleted==2 && squad.count==0 && SupportSoldierLastFailure()==SupportSpawnFailure::create,"partial batch rolls back all members");
    Setup();wrong=true;Check(!SpawnSupportSoldier(poses[0],&one) && deleted==1,"wrong class is deleted");
    Setup();poses[0][0]=std::numeric_limits<float>::quiet_NaN();
    Check(!SpawnSupportSquad(SupportSquadKind::rangerSquad,poses[0],4,&squad) && made==0,"all transforms checked before any spawn");poses[0][0]=1;
    Setup();Put<void*>(image,support_native::kObjectManager,objects[1]);
    Check(!SpawnSupportSoldier(poses[0],&one) && made==0,"changed scene manager requires new preload");
    Setup();session=true;Check(SupportSoldiersReady(),"online resources ready for authenticated Apply");
    Check(!SpawnSupportSoldier(poses[0],&one) && made==0,"online direct creation blocked without replication");
    unsigned char id[32]{};Put<unsigned>(id,0xC,5);Put<unsigned>(id,4,0xE0000001);
    Check(ApplySupportSoldierSpawn(poses[0],false,id,&one),"host applies authenticated event and native registration");
    Check(At<unsigned>(one.obj,0x128)==2 && At<LONG>(one.ctrl,12)==2,"host native owner and weak consumption");
    Check(!RegisterSupportObject(one.obj,id),"registered object cannot be registered twice");
    host=false;Check(ApplySupportSoldierSpawn(poses[1],false,id,&one),"client applies same canonical soldier event");
    Check(At<unsigned>(one.obj,0x128)==1 && At<LONG>(one.ctrl,12)==2,"client native remote owner and balanced weak");
    Check(SupportSoldierHeld(one.obj),"client replica also held pending all-peer Active");
    Put<unsigned>(id,0xC,0);const int before=made;
    Check(!ApplySupportSoldierSpawn(poses[2],false,id,&one) && made==before,"playerSample identity type rejected before allocation");Put<unsigned>(id,0xC,5);
    registerGood=false;Check(!ApplySupportSoldierSpawn(poses[2],false,id,&one) && deleted==1,"failed registration rolls back");
    unsigned char netCtrl[16]{},netEntry[0x48]{},readId[32]{};
    Put<LONG>(netCtrl,0,1);Put<void*>(netCtrl,8,netEntry);Put<void*>(playerSample,0x130,netCtrl);Put<unsigned>(playerSample,0x128,2);
    std::memcpy(netEntry+8,id,32);std::memset(netEntry+28,0xA5,4);
    Check(ReadNativeObjectId(playerSample,readId) && std::memcmp(readId,id,32)==0,"native ID read canonicalizes unspecified padding");
    ResetSupportSoldiers();VirtualFree(image,0,MEM_RELEASE);
    std::printf("support_soldier_test: %d checks passed\n",checks);
}
