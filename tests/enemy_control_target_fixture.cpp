// Includes the production implementation. The supported native weak setter's
// exact non-RIP machine code executes against owned fixture objects.
#include "../src/enemy_control_target.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;
using namespace crew::enemy_target;
unsigned checks=0,failures=0,deletes=0,walkCalls=0,notifyCalls=0;
bool mapReady=true,nativeSender=true,nativeIds=true;
float mapDistance=-1.0f;
// Fixture players: an actor named by its mission slot (net_object_ref.h), as EncodeNetRef names a player soldier.
std::vector<std::pair<const unsigned char*,int>> playerSlots;
unsigned resolveCalls=0;
const void* opaque=nullptr;
unsigned refusedSignature=0;
std::vector<const unsigned char*> actors;
const void* bodyObjects[8]{};
unsigned directBody=0;
char notification[16]{};unsigned notificationSize=0;
const void* notifiedTarget=nullptr;
void Check(bool pass,const char* text,int line){++checks;if(!pass){++failures;std::fprintf(stderr,"line %d: %s\n",line,text);}}
#define CHECK(x) Check((x),#x,__LINE__)
void __fastcall Delete(void*) noexcept {++deletes;}
void* controlVtable[]={nullptr,reinterpret_cast<void*>(&Delete)};
struct Actor {
    alignas(16) unsigned char bytes[0x600]{},ctrl[0x20]{},point[0x50]{},pointCtrl[0x20]{};
    ObjRef points[2]{};
    unsigned char id[32]{};
    void* vtable[40]{};
    void* network[16]{};
    Actor(unsigned identity,int team,float x,float y,float z,bool locked=true) {
        id[0]=static_cast<unsigned char>(identity);id[24]=static_cast<unsigned char>(identity+10);
        Put<void*>(ctrl,0,controlVtable);Put<LONG>(ctrl,8,1);Put<LONG>(ctrl,12,1);
        Put<void*>(pointCtrl,0,controlVtable);Put<LONG>(pointCtrl,8,1);Put<LONG>(pointCtrl,12,1);
        Put<void*>(bytes,0,vtable);Put<void*>(bytes,kSelf,bytes);Put<void*>(bytes,kSelfCtrl,ctrl);
        Put<int>(bytes,kTeam,team);Put<unsigned>(bytes,kObjectFlags,0x40);
        Put<float>(bytes,kPosition,x);Put<float>(bytes,kPosition+4,y);Put<float>(bytes,kPosition+8,z);
        Put<void*>(bytes,0x120,network);bytes[0x591]=1;
        if(locked) {
            Put<void*>(point,8,bytes);point[0x29]=point[0x2A]=1;
            std::memcpy(point+0x10,bytes+kPosition,12);points[0]={point,pointCtrl};
            Put<void*>(bytes,0x368,points);Put<std::uint64_t>(bytes,0x378,1);
        }
        actors.push_back(bytes);
    }
    ObjRef Ref() const {return {bytes,ctrl};}
};
struct TeamData {alignas(16) unsigned char manager[0x50]{},rows[64*0x38]{};int relations[64][64]{};} teams;
void Teams() {
    std::memset(&teams,0,sizeof(teams));Put<void*>(image,kTeams,teams.manager);Put<void*>(teams.manager,0x38,teams.rows);
    for(unsigned i=0;i<64;++i){Put<void*>(teams.rows+i*0x38,0x18,teams.relations[i]);Put<std::uint64_t>(teams.rows+i*0x38,0x28,64);}
    teams.relations[1][2]=2;teams.relations[3][4]=2;
}
void __fastcall Walk(const void*,int team,void* visitor) noexcept {
    ++walkCalls;CHECK(team==1 || team==3);
    const auto* vt=At<void* const*>(visitor,0);
    for(const auto* actor:actors)reinterpret_cast<void(__fastcall*)(void*,const void*)>(vt[1])(visitor,actor);
}
const void* __fastcall Body(std::uint32_t id) noexcept {return id<8 ? bodyObjects[id] : nullptr;}
void LogNotify(char c){if(notificationSize+1<sizeof(notification))notification[notificationSize++]=c;}
void* __fastcall Ctor(void* p,int size) noexcept {CHECK(size==0x40);LogNotify('C');return p;}
bool __fastcall Byte(void*,signed char kind) noexcept {CHECK(kind==1);LogNotify('B');return true;}
void __fastcall Dtor(void*) noexcept {LogNotify('D');}
bool __fastcall Owner(void*) noexcept {return nativeSender;}
void __fastcall Serialize(void*,void*,const void* target) noexcept {notifiedTarget=target;LogNotify('S');}
void __fastcall Send(void*,void*,bool reliable) noexcept {CHECK(reliable);++notifyCalls;LogNotify('N');}
void Leaf(unsigned at,const void* function) {
    const unsigned char jump[]={0xFF,0x25,0,0,0,0};std::memcpy(image+at,jump,6);std::memcpy(image+at+6,&function,8);
}
// Exact EDF.dll 678CCB46 bytes [364EB0,364F84); no external relative call/RIP data.
const unsigned char kSetterCode[]=
"\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x20\x48\x8B\x42\x08"
"\x33\xED\x48\x8B\xF2\x48\x8B\xF9\x8B\xCD\x8B\xD5\x48\x85\xC0\x74\x0A\x48\x8B\x0E\xF0\xFF\x40\x0C"
"\x48\x8B\xD0\x48\x89\x8F\x28\x05\x00\x00\xBB\xFF\xFF\xFF\xFF\x48\x8B\x8F\x30\x05\x00\x00\x48\x89"
"\x97\x30\x05\x00\x00\x48\x85\xC9\x74\x12\x8B\xC3\xF0\x0F\xC1\x41\x0C\x83\xF8\x01\x75\x06\x48\x8B"
"\x01\xFF\x50\x08\x48\x8B\x46\x08\x48\x8B\xD5\x48\x85\xC0\x74\x0A\x48\x8B\x2E\xF0\xFF\x40\x0C\x48"
"\x8B\xD0\x48\x8B\x8F\x20\x05\x00\x00\x48\x89\xAF\x18\x05\x00\x00\x48\x89\x97\x20\x05\x00\x00\x48"
"\x85\xC9\x74\x12\x8B\xC3\xF0\x0F\xC1\x41\x0C\x83\xF8\x01\x75\x06\x48\x8B\x01\xFF\x50\x08\x48\x8B"
"\x4E\x08\x48\x85\xC9\x74\x10\xF0\x0F\xC1\x59\x0C\x83\xFB\x01\x75\x06\x48\x8B\x01\xFF\x50\x08\x48"
"\x8B\x5C\x24\x30\x48\x8B\x6C\x24\x38\x48\x8B\x74\x24\x40\x48\x83\xC4\x20\x5F\xC3";
static_assert(sizeof(kSetterCode)-1==0xD4);
void Hooks() {
    Leaf(kWalk,reinterpret_cast<const void*>(&Walk));Leaf(kBodyObject,reinterpret_cast<const void*>(&Body));
    Leaf(kStreamCtor,reinterpret_cast<const void*>(&Ctor));Leaf(kStreamByte,reinterpret_cast<const void*>(&Byte));
    Leaf(kStreamDtor,reinterpret_cast<const void*>(&Dtor));Leaf(kOnline,reinterpret_cast<const void*>(&Owner));
    std::memcpy(image+kNativeSet,kSetterCode,sizeof(kSetterCode)-1);
}
}
namespace edf {
bool Readable(const void* p,std::size_t,bool) noexcept {return p && p!=opaque;}
bool Matches(const unsigned char*,std::size_t at,const unsigned char*,std::size_t) noexcept {return at!=refusedSignature;}
}
namespace crew {
bool ReadNativeObjectId(const unsigned char* object,unsigned char* id) noexcept {
    // Actor bytes are first in Actor, including distinct high native-ID bytes.
    if(!nativeIds)return false;
    for(const auto* actor:actors)if(actor==object){std::memcpy(id,reinterpret_cast<const Actor*>(actor)->id,32);return true;}
    return false;
}
// net_object_resolve.h over the fixture's actors: a player by its slot, the rest by its NetId; found live (or present).
bool EncodeNetRef(const ObjRef& object,unsigned char out[32]) noexcept {
    std::memset(out,0,32);
    for(const auto& p:playerSlots)if(p.first==object.obj)return netref::MakePlayer(p.second,out);
    unsigned char id[32]{};
    return ReadNativeObjectId(static_cast<const unsigned char*>(object.obj),id) && netref::MakeNative(id,out);
}
bool EncodeNetRef(const void* object,unsigned char out[32]) noexcept {return EncodeNetRef(ObjRef::Of(object),out);}
bool Here(const unsigned char* actor,NetLife life) noexcept {
    return !(actor[0x18]&4) && At<LONG>(At<const void*>(actor,kSelfCtrl),8)>0 && (life==NetLife::present || !actor[kDead]);
}
bool ResolveNetRef(const unsigned char* ref,NetLife life,NetResolved* out) noexcept {
    ++resolveCalls;*out=NetResolved{};
    if(netref::Classify(ref,&out->slot)==netref::Kind::invalid)return false;
    unsigned matches=0;
    for(const auto* actor:actors) {
        unsigned char name[32]{};
        if(!EncodeNetRef(ObjRef::Of(actor),name) || std::memcmp(name,ref,32) || !Here(actor,life))continue;
        if(!matches++)out->obj=ObjRef::Of(actor);
    }
    if(matches==1)out->found=true;
    else {out->obj={};out->ambiguous=matches>1;}
    return true;
}
bool StillNetRef(const unsigned char* ref,NetLife life,const NetResolved& r) noexcept {
    unsigned char name[32]{};
    const auto* actor=static_cast<const unsigned char*>(r.obj.obj);
    return r.found && Here(actor,life) && EncodeNetRef(r.obj,name) && !std::memcmp(name,ref,32);
}
bool MapRayReady() noexcept {return mapReady;}
float MapRay(const float*,const float* to,float* hit) noexcept {if(hit)std::memcpy(hit,to,12);return mapDistance;}
float ObjectRay(const float* from,const float* to,float* hit,std::uint32_t* body) noexcept {
    if(!directBody)return -1;
    if(body)*body=directBody;
    if(hit)for(unsigned i=0;i<3;++i)hit[i]=from[i]+(to[i]-from[i])*0.5f;
    return 20.0f;
}
}
int main() {
    using namespace crew;using namespace crew::enemy_target;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    refusedSignature=kNativeSet;CHECK(!Install());refusedSignature=0;CHECK(Install());Hooks();Teams();
    Actor owner(1,1,0,0,-5),controlled(2,1,0,0,0),enemy(3,2,0,0,100),friendActor(4,1,0,0,50),off(5,2,10,0,100);
    controlled.vtable[37]=reinterpret_cast<void*>(&Serialize);controlled.network[14]=reinterpret_cast<void*>(&Send);
    Ray ray{{0,0,0},{0,0,1},6000};Selection selected{};
    CHECK(Pick(owner.Ref(),controlled.Ref(),ray,&selected));CHECK(Same(selected.actor,enemy.Ref()));CHECK(selected.lockIndex==0);
    CHECK(selected.lock.obj==enemy.point && selected.lock.ctrl==enemy.pointCtrl);CHECK(!std::memcmp(selected.ref,enemy.id,32));CHECK(Valid(owner.Ref(),controlled.Ref(),ray,selected));
    // Offline actors keep real weak identity, without inventing network IDs.
    nativeIds=false;CHECK(Pick(owner.Ref(),controlled.Ref(),ray,&selected));CHECK(Same(selected.actor,enemy.Ref()));
    CHECK(Zero(selected.ref));CHECK(Valid(owner.Ref(),controlled.Ref(),ray,selected));nativeIds=true;
    // Full native ID includes bytes24..31; low-ID equality cannot choose another actor.
    unsigned char id[32]{};std::memcpy(id,enemy.id,32);id[24]^=1;CHECK(Normalize(owner.Ref(),controlled.Ref(),id,ray,&selected));CHECK(!selected.actor);CHECK(!Canonical(id));
    std::memcpy(id,enemy.id,32);id[20]=1;CHECK(Normalize(owner.Ref(),controlled.Ref(),id,ray));CHECK(!Canonical(id));
    std::memcpy(id,enemy.id,32);CHECK(Normalize(owner.Ref(),controlled.Ref(),id,ray,&selected));CHECK(selected.actor.obj==enemy.bytes);
    // Weak identity and native eligibility are rechecked, not just addresses.
    const Selection borrowed=selected;Selection retained{};Assign(retained,selected);
    CHECK(At<LONG>(enemy.ctrl,12)==2 && At<LONG>(enemy.pointCtrl,12)==2);
    ObjRef outLock{};CHECK(CopyLock(retained,&outLock));CHECK(At<LONG>(enemy.pointCtrl,12)==3);Drop(outLock);
    const auto sourceBefore=owner;const auto controlledBefore=controlled;
    enemy.bytes[kDead]=1;std::memcpy(id,enemy.id,32);CHECK(Normalize(owner.Ref(),controlled.Ref(),id,ray));CHECK(!Canonical(id));CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));enemy.bytes[kDead]=0;
    Put<LONG>(enemy.ctrl,8,0);CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));Put<LONG>(enemy.ctrl,8,1);
    Put<void*>(enemy.bytes,kSelfCtrl,friendActor.ctrl);CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));Put<void*>(enemy.bytes,kSelfCtrl,enemy.ctrl);
    enemy.bytes[0x18]=4;CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));enemy.bytes[0x18]=0;
    Put<unsigned>(enemy.bytes,kObjectFlags,0xC0);CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));Put<unsigned>(enemy.bytes,kObjectFlags,0x40);
    Put<unsigned>(enemy.bytes,kObjectFlags,0);CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));Put<unsigned>(enemy.bytes,kObjectFlags,0x40);
    teams.relations[1][2]=1;CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));teams.relations[1][2]=2;
    enemy.point[0x29]=0;CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));enemy.point[0x29]=1;
    enemy.point[0x2A]=0;CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));enemy.point[0x2A]=1;
    Put<void*>(enemy.point,8,off.bytes);CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));Put<void*>(enemy.point,8,enemy.bytes);
    Put<LONG>(enemy.pointCtrl,8,0);CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));Put<LONG>(enemy.pointCtrl,8,1);
    enemy.points[0].ctrl=off.pointCtrl;CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));enemy.points[0].ctrl=enemy.pointCtrl;
    opaque=enemy.pointCtrl;CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));opaque=nullptr;
    mapDistance=20;CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));mapDistance=-1;
    bodyObjects[1]=friendActor.bytes;directBody=1;CHECK(!Valid(owner.Ref(),controlled.Ref(),ray,retained));directBody=0;
    CHECK(!std::memcmp(&owner,&sourceBefore,sizeof(owner)));CHECK(!std::memcmp(&controlled,&controlledBefore,sizeof(controlled)));
    // The real surface hit supports a large actor with an off-center locator.
    bodyObjects[2]=off.bytes;directBody=2;CHECK(Pick(owner.Ref(),controlled.Ref(),ray,&selected));CHECK(selected.actor.obj==off.bytes);CHECK(Valid(owner.Ref(),controlled.Ref(),ray,selected));directBody=0;
    // Empty/invalid aims never fall back to P1 or the previous AI target.
    Ray backward{{0,0,0},{0,0,-1},6000};CHECK(Pick(owner.Ref(),controlled.Ref(),backward,&selected));CHECK(!selected.actor);
    Ray invalid=ray;invalid.direction[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!Pick(owner.Ref(),controlled.Ref(),invalid,&selected));CHECK(!selected.actor);
    mapReady=false;CHECK(!Pick(owner.Ref(),controlled.Ref(),ray,&selected));mapReady=true;
    CHECK(!Pick(owner.Ref(),owner.Ref(),ray,&selected));Put<LONG>(owner.ctrl,8,0);CHECK(!Pick(owner.Ref(),controlled.Ref(),ray,&selected));Put<LONG>(owner.ctrl,8,1);
    // Native closed source team uses its previous real +318 team; targets with
    // team=-1 remain unavailable even when an old native ID still resolves.
    Put<int>(controlled.bytes,0x318,1);Put<int>(controlled.bytes,kTeam,-1);
    CHECK(Pick(owner.Ref(),controlled.Ref(),ray,&selected));CHECK(selected.actor.obj==enemy.bytes);
    Put<int>(controlled.bytes,kTeam,1);Put<int>(enemy.bytes,kTeam,-1);
    CHECK(Pick(owner.Ref(),controlled.Ref(),ray,&selected));CHECK(!selected.actor);Put<int>(enemy.bytes,kTeam,2);
    // An actor with no native locator is an optional capture target, never a
    // fabricated homing weak reference.
    Actor physical(9,2,0,0,25,false);CHECK(Pick(owner.Ref(),controlled.Ref(),ray,&selected));CHECK(selected.actor.obj==physical.bytes);
    CHECK(selected.lockIndex==-1 && !selected.lock.obj && !selected.lock.ctrl);CHECK(!CopyLock(selected,&outLock));
    physical.bytes[kDead]=1;
    // A second local source uses its own real team and ray.
    Actor secondOwner(6,3,100,0,-5),secondBody(7,3,100,0,0),secondEnemy(8,4,100,0,200);
    Ray secondRay{{100,0,0},{0,0,1},6000};CHECK(Pick(secondOwner.Ref(),secondBody.Ref(),secondRay,&selected));CHECK(selected.actor.obj==secondEnemy.bytes);
    // Duplicate full identities are rejected even when the second registered
    // actor is outside this ray's cone, rather than selecting by iteration order.
    Actor duplicate(3,2,1000,0,100);std::memcpy(id,enemy.id,32);
    CHECK(Normalize(owner.Ref(),controlled.Ref(),id,ray,&selected));CHECK(Zero(id) && !selected.actor);duplicate.bytes[kDead]=1;
    // An enemy player (PvP) is its mission slot on the wire, not its NetId (another on every machine): picked, it is named
    // by its slot; a focus naming the slot finds its live player; dead, gone or two of the slot: no focus.
    {
        playerSlots.push_back({enemy.bytes,2});
        unsigned char slot2[32]{};CHECK(netref::MakePlayer(2,slot2));
        CHECK(Pick(owner.Ref(),controlled.Ref(),ray,&selected));CHECK(selected.actor.obj==enemy.bytes);
        CHECK(!std::memcmp(selected.ref,slot2,32));CHECK(Valid(owner.Ref(),controlled.Ref(),ray,selected));
        std::memcpy(id,slot2,32);resolveCalls=0;
        CHECK(Normalize(owner.Ref(),controlled.Ref(),id,ray,&selected));CHECK(selected.actor.obj==enemy.bytes);
        CHECK(!std::memcmp(id,slot2,32) && resolveCalls==1);
        CHECK(Resolve(owner.Ref(),controlled.Ref(),slot2,ray,&selected));CHECK(selected.actor.obj==enemy.bytes);
        // Its NetId names nothing any more: the player is found by its slot alone.
        std::memcpy(id,enemy.id,32);CHECK(Normalize(owner.Ref(),controlled.Ref(),id,ray,&selected));CHECK(Zero(id) && !selected.actor);
        enemy.bytes[kDead]=1;
        CHECK(Resolve(owner.Ref(),controlled.Ref(),slot2,ray,&selected));CHECK(!selected.actor);
        enemy.bytes[kDead]=0;
        playerSlots.push_back({off.bytes,2});
        CHECK(!Resolve(owner.Ref(),controlled.Ref(),slot2,ray,&selected));CHECK(!selected.actor);
        std::memcpy(id,slot2,32);CHECK(Normalize(owner.Ref(),controlled.Ref(),id,ray,&selected));CHECK(Zero(id));
        off.bytes[kDead]=1;   // one of the slot fallen (a body and its respawn): the live one
        CHECK(Resolve(owner.Ref(),controlled.Ref(),slot2,ray,&selected));CHECK(selected.actor.obj==enemy.bytes);
        off.bytes[kDead]=0;playerSlots.clear();
        unsigned char gone[32]{};CHECK(netref::MakePlayer(7,gone));   // no player of that slot here
        CHECK(Resolve(owner.Ref(),controlled.Ref(),gone,ray,&selected));CHECK(!selected.actor);
    }
    // Native current/forced are saved separately, exact counts return on restore.
    ObjRef seedCurrent{},seedForced{};
    CHECK(crew::enemy_target::Store(controlled.bytes,0x518,friendActor.Ref(),seedCurrent));
    CHECK(crew::enemy_target::Store(controlled.bytes,0x528,off.Ref(),seedForced));
    const LONG friendCount=At<LONG>(friendActor.ctrl,12),offCount=At<LONG>(off.ctrl,12),enemyCount=At<LONG>(enemy.ctrl,12);
    NativeLease lease{};CHECK(Begin(controlled.Ref(),&lease));CHECK(!Begin(controlled.Ref(),&lease));
    CHECK(Pick(owner.Ref(),controlled.Ref(),ray,&selected));CHECK(selected.actor.obj==enemy.bytes);
    CHECK(Apply(lease,&selected));CHECK(At<const void*>(controlled.bytes,0x518)==enemy.bytes && At<const void*>(controlled.bytes,0x528)==enemy.bytes);
    CHECK(At<LONG>(enemy.ctrl,12)==enemyCount+3);CHECK(notifyCalls==1 && !std::strcmp(notification,"CBSND") && notifiedTarget==enemy.bytes);
    CHECK(Apply(lease,&selected));CHECK(notifyCalls==1 && At<LONG>(enemy.ctrl,12)==enemyCount+3);
    CHECK(Apply(lease,nullptr));CHECK(!At<const void*>(controlled.bytes,0x518) && !At<const void*>(controlled.bytes,0x528));CHECK(notifyCalls==2 && notifiedTarget==nullptr);CHECK(At<LONG>(enemy.ctrl,12)==enemyCount);
    CHECK(Restore(lease));CHECK(!lease.active);CHECK(At<const void*>(controlled.bytes,0x518)==friendActor.bytes && At<const void*>(controlled.bytes,0x528)==off.bytes);
    CHECK(At<LONG>(friendActor.ctrl,12)==friendCount && At<LONG>(off.ctrl,12)==offCount);
    CHECK(Restore(lease));CHECK(At<LONG>(controlled.ctrl,12)==1);
    Clear(retained);CHECK(At<LONG>(enemy.ctrl,12)==1 && At<LONG>(enemy.pointCtrl,12)==1);
    // Native ownership is the same send gate on every peer. Guest applications
    // update their local weak pairs without emitting a duplicate target message.
    CHECK(Begin(controlled.Ref(),&lease));CHECK(Pick(owner.Ref(),controlled.Ref(),ray,&selected));
    const unsigned sent=notifyCalls;controlled.bytes[0x128]=1;
    CHECK(Apply(lease,&selected));CHECK(notifyCalls==sent);CHECK(Restore(lease));CHECK(notifyCalls==sent);controlled.bytes[0x128]=0;
    CHECK(Begin(controlled.Ref(),&lease));nativeSender=false;
    CHECK(Apply(lease,&selected));CHECK(notifyCalls==sent);CHECK(Restore(lease));CHECK(notifyCalls==sent);nativeSender=true;
    // Actual read-only control pages must preserve all weak ownership, even
    // while the fixture's Readable stub deliberately still reports true.
    auto* pinPage=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    auto* lockPage=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    auto* objectPage=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    auto* rootPage=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!pinPage || !lockPage || !objectPage || !rootPage)return 2;
    for(auto* block:{pinPage,lockPage,rootPage}) {Put<void*>(block,0,controlVtable);Put<LONG>(block,8,1);Put<LONG>(block,12,1);}
    Selection pageSource=borrowed;pageSource.actor.ctrl=pinPage;pageSource.lock.ctrl=lockPage;
    Selection pageRetained{};CHECK(TryAssign(pageRetained,pageSource));
    CHECK(At<LONG>(pinPage,12)==2 && At<LONG>(lockPage,12)==2);
    DWORD priorProtect=0,restoreProtect=0;
    CHECK(VirtualProtect(lockPage,0x1000,PAGE_READONLY,&priorProtect)!=0);
    const Selection pageBefore=pageRetained;
    CHECK(!TryClear(pageRetained));CHECK(!TryAssign(pageRetained,Selection{}));Clear(pageRetained);
    CHECK(!std::memcmp(&pageRetained,&pageBefore,sizeof(pageBefore)));
    CHECK(At<LONG>(pinPage,12)==2 && At<LONG>(lockPage,12)==2);
    CHECK(VirtualProtect(lockPage,0x1000,priorProtect,&restoreProtect)!=0);
    CHECK(TryClear(pageRetained));CHECK(!pageRetained.actor && !pageRetained.lock);
    CHECK(At<LONG>(pinPage,12)==1 && At<LONG>(lockPage,12)==1);
    CHECK(TryAssign(pageRetained,pageSource));Hold(pageSource.actor);pageRetained.retiredActor=pageSource.actor;
    CHECK(VirtualProtect(pinPage,0x1000,PAGE_READONLY,&priorProtect)!=0);
    const Selection retiredBefore=pageRetained;CHECK(!TryClear(pageRetained));
    CHECK(!std::memcmp(&pageRetained,&retiredBefore,sizeof(retiredBefore)) && At<LONG>(pinPage,12)==3);
    CHECK(VirtualProtect(pinPage,0x1000,priorProtect,&restoreProtect)!=0);
    CHECK(TryClear(pageRetained));CHECK(At<LONG>(pinPage,12)==1 && At<LONG>(lockPage,12)==1);
    // A retained native root and target use independent real pages. Refusing
    // any one counter or native field page must leave the entire lease intact.
    Actor pagedTarget(10,2,0,0,100);Put<void*>(pagedTarget.bytes,kSelfCtrl,pinPage);
    Selection pageTarget{};pageTarget.actor={pagedTarget.bytes,pinPage};std::memcpy(pageTarget.ref,pagedTarget.id,32);
    std::memcpy(objectPage,controlled.bytes,sizeof(controlled.bytes));Put<void*>(objectPage,kSelf,objectPage);Put<void*>(objectPage,kSelfCtrl,rootPage);
    Put<void*>(objectPage,0x518,nullptr);Put<void*>(objectPage,0x520,nullptr);Put<void*>(objectPage,0x528,nullptr);Put<void*>(objectPage,0x530,nullptr);
    NativeLease protectedLease{};CHECK(Begin(ObjRef{objectPage,rootPage},&protectedLease));CHECK(Apply(protectedLease,&pageTarget));
    const NativeLease leaseBefore=protectedLease;unsigned char nativeBefore[0x600]{};std::memcpy(nativeBefore,objectPage,sizeof(nativeBefore));
    const LONG rootPins=At<LONG>(rootPage,12),targetPins=At<LONG>(pinPage,12);const unsigned sentBefore=notifyCalls;
    opaque=objectPage;CHECK(!Restore(protectedLease));CHECK(!std::memcmp(&protectedLease,&leaseBefore,sizeof(leaseBefore)));opaque=nullptr;
    for(auto* page:{rootPage,pinPage,objectPage}) {
        CHECK(VirtualProtect(page,0x1000,PAGE_READONLY,&priorProtect)!=0);
        CHECK(!Restore(protectedLease));
        CHECK(!std::memcmp(&protectedLease,&leaseBefore,sizeof(leaseBefore)));
        CHECK(!std::memcmp(objectPage,nativeBefore,sizeof(nativeBefore)));
        CHECK(At<LONG>(rootPage,12)==rootPins && At<LONG>(pinPage,12)==targetPins && notifyCalls==sentBefore);
        CHECK(VirtualProtect(page,0x1000,priorProtect,&restoreProtect)!=0);
    }
    CHECK(Restore(protectedLease));CHECK(!protectedLease.active && At<LONG>(rootPage,12)==1 && At<LONG>(pinPage,12)==1);
    CHECK(!At<const void*>(objectPage,0x518) && !At<const void*>(objectPage,0x528));
    VirtualFree(pinPage,0,MEM_RELEASE);VirtualFree(lockPage,0,MEM_RELEASE);VirtualFree(objectPage,0,MEM_RELEASE);VirtualFree(rootPage,0,MEM_RELEASE);
    // A dead/reused root is never written through. Pins are still released.
    CHECK(Begin(controlled.Ref(),&lease));Put<LONG>(controlled.ctrl,8,0);const auto rootBefore=controlled;
    CHECK(!Apply(lease,&borrowed));CHECK(Restore(lease));CHECK(!std::memcmp(controlled.bytes,rootBefore.bytes,sizeof(controlled.bytes)));
    std::printf("enemy target production/native fixture: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
