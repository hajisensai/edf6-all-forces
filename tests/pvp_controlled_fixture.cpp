// Execute the controlled-GameObject PVP lease core against a small native ABI.
// Typed adapters stay explicit: emitters are never passed to GameObject SetTeam.
#include "../src/pvp_controlled.cpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace controlled_fixture {
using namespace crew::controlled_pvp;
struct Object {
    alignas(16) unsigned char bytes[0x2100]{};
    alignas(16) unsigned char control[0x20]{};
    int baselineTeam=5,baselineLast=5,refuseTeamA=-999,refuseTeamB=-999;
    bool baselineKnown=true;
};
struct NativeEmitter {
    alignas(16) unsigned char bytes[0x300]{};
    EmitterKind kind=EmitterKind::weaponBase;
    Object* member=nullptr;
    NativeEmitter* storage=nullptr; // GunBattery and its private owner-team mirror share one native object
    Object* scene=nullptr;crew::ObjRef sealedOwner{},sealedStorage{};
    std::uint64_t incarnation=1;
    Life life=Life::live;
    int baseline=5,refuseTeam=-999;
    bool listed=true,baselineKnown=true,retiredIncarnationsGone=false;
    unsigned writes=0,reads=0,validations=0;
    bool Derived() const noexcept {return kind==EmitterKind::liveOwnerTeam || kind==EmitterKind::liveOwnerLastTeam;}
    unsigned Offset() const noexcept {
        switch(kind) {
        case EmitterKind::weaponBase:return 0x214;
        case EmitterKind::indirectFireControl:return 0xD0;
        case EmitterKind::gunBattery:return 0x148;
        case EmitterKind::ownerTeamMirror:return 0x10;
        case EmitterKind::liveOwnerTeam:case EmitterKind::liveOwnerLastTeam:return 0;
        case EmitterKind::ownerTeamCache:return 0x80;case EmitterKind::ownerLastTeamCache:return 0x84;
        }
        return 0;
    }
    const unsigned char* Data() const noexcept {return scene ? scene->bytes : storage ? storage->bytes : bytes;}
    unsigned char* Data() noexcept {return scene ? scene->bytes : storage ? storage->bytes : bytes;}
    int Team() const noexcept {return Derived() ? edf::At<int>(member->bytes,kind==EmitterKind::liveOwnerTeam ? edf::kTeam : 0x318) : edf::At<int>(Data(),Offset());}
    void Team(int team) noexcept {if(!Derived())edf::Put<int>(Data(),Offset(),team);}
};
struct World {
    Object root{},part{},newPart{};
    Object* members[3]{};
    unsigned memberCount=2;
    NativeEmitter emitters[8]{};
    unsigned emitterCount=8;
    bool collectOk=true,memberHook=true,emitterHook=true,cascadeNative=false;
    unsigned collects=0,memberBaselines=0,emitterBaselines=0,initializations=0;
    bool initializerResult=true,initializerFinish=false,initializerRestore=false,initializerGrow=false,initializerAcquire=false;
    std::uint64_t initializerToken=0;Lease* initializerLease=nullptr;
    int baselineRootTeam=-999,baselineRootLast=-999,baselineOwnerTeam=-999,baselineOwnerLast=-999;
    unsigned char sourceUser[0x100]{},sourcePuid[65]{};
};
World* contexts[12]{};unsigned contextCount=0;
int ownerTeam=1,checks=0,failures=0;
unsigned nativeWrites=0,controlDeletes=0,deadReads=0;
const void* watchedDead=nullptr;const void* denyReadable=nullptr;
const void* watchNativeWrite=nullptr;unsigned watchedNativeWrites=0;
const void* dieOnControlRead=nullptr;World* dyingWorld=nullptr;unsigned controlReadCount=0,dieAtRead=0;
void Check(bool value,const char* why,int line) {
    ++checks;if(!value){++failures;std::fprintf(stderr,"line %d: %s\n",line,why);}
}
#define CHECK(value) controlled_fixture::Check(!!(value),#value,__LINE__)
void __fastcall DeleteControl(void*) noexcept {++controlDeletes;}
void InitObject(Object& object,int team=5,int last=5,bool registered=true) {
    object=Object{};
    static void* table[2]={nullptr,reinterpret_cast<void*>(&DeleteControl)};
    edf::Put<void*>(object.control,0,table);edf::Put<LONG>(object.control,8,1);edf::Put<LONG>(object.control,12,1);
    edf::Put<void*>(object.bytes,edf::kSelf,object.bytes);edf::Put<void*>(object.bytes,edf::kSelfCtrl,object.control);
    edf::Put<int>(object.bytes,edf::kTeam,team);edf::Put<int>(object.bytes,0x318,last);
    edf::Put<unsigned>(object.bytes,0x380,0xA000u|(registered ? 0x40u : 0u));
    for(unsigned i=0;i<32;++i)object.bytes[0x108+i]=static_cast<unsigned char>(0x20u+i);
    edf::Put<unsigned short>(object.bytes,0x128,2);
    object.baselineTeam=team;object.baselineLast=last;
}
void Init(World& world,int team=5,int last=5,bool registered=true,bool fresh=true) {
        world=World{};
    if(fresh){contextCount=0;ownerTeam=1;nativeWrites=controlDeletes=deadReads=0;watchedDead=nullptr;denyReadable=nullptr;watchNativeWrite=nullptr;watchedNativeWrites=0;dieOnControlRead=nullptr;dyingWorld=nullptr;controlReadCount=dieAtRead=0;}
    CHECK(contextCount<12);if(contextCount<12)contexts[contextCount++]=&world;
    InitObject(world.root,team,last,registered);InitObject(world.part,team,last,registered);InitObject(world.newPart,team,last,registered);
    world.members[0]=&world.root;world.members[1]=&world.part;
    std::memset(world.sourceUser,0x39,sizeof(world.sourceUser));std::memcpy(world.sourcePuid,"authenticated-owner1",21);
    for(Object* object:world.members)if(object) {
        edf::Put<void*>(object->bytes,0x1ED0,world.sourceUser);edf::Put<void*>(object->bytes,0x1ED8,world.sourcePuid);
    }
    world.emitters[3].storage=&world.emitters[2];
    const EmitterKind kinds[8]={EmitterKind::weaponBase,EmitterKind::indirectFireControl,EmitterKind::gunBattery,EmitterKind::ownerTeamMirror,EmitterKind::liveOwnerTeam,EmitterKind::liveOwnerLastTeam,EmitterKind::ownerTeamCache,EmitterKind::ownerLastTeamCache};
    for(unsigned i=0;i<8;++i) {
        auto& emitter=world.emitters[i];emitter.kind=kinds[i];emitter.member=i==0 ? &world.root : &world.part;
        emitter.baseline=emitter.kind==EmitterKind::ownerTeamCache ? team : emitter.kind==EmitterKind::ownerLastTeamCache ? last : team==-1 ? last : team;emitter.Team(emitter.baseline);
    }
}
Object* ObjectOf(const void* pointer) noexcept {

    for(unsigned i=0;i<contextCount;++i)for(auto* object:{&contexts[i]->root,&contexts[i]->part,&contexts[i]->newPart})if(object->bytes==pointer)return object;
    return nullptr;
}
void RawTeam(void* pointer,int team,bool registered) noexcept {
    edf::Put<int>(pointer,edf::kTeam,team);
    if(team>=0)edf::Put<int>(pointer,0x318,team);
    const auto flags=edf::At<unsigned>(pointer,0x380);
    edf::Put<unsigned>(pointer,0x380,registered ? flags|0x40u : flags&~0x40u);
}
void __fastcall NativeSetTeam(void* pointer,int team,bool registered) noexcept {
    ++nativeWrites;if(pointer==watchNativeWrite)++watchedNativeWrites;Object* object=ObjectOf(pointer);CHECK(object);CHECK(team!=-1 || !registered);if(!object)return;
    for(unsigned i=0;i<contextCount;++i)if(contexts[i]->root.bytes==pointer && contexts[i]->cascadeNative) {
        // Native slot 22 overrides may recurse into children even before this root's own update fails.
        RawTeam(contexts[i]->part.bytes,team,registered);RawTeam(contexts[i]->newPart.bytes,team,registered);
        contexts[i]->emitters[3].Team(team); // native slot-22 private GunBattery owner mirror propagation
    }
    if(team==object->refuseTeamA || team==object->refuseTeamB)return;
    RawTeam(pointer,team,registered);
}
bool Collect(void* user,const crew::ObjRef& root,crew::ObjRef* members,unsigned capacity,unsigned* count,
             Emitter* emitters,unsigned emitterCapacity,unsigned* emitterCount) noexcept {
    auto& world=*static_cast<World*>(user);++world.collects;
    if(!world.collectOk || root.obj!=world.root.bytes || root.ctrl!=world.root.control || world.memberCount>capacity)return false;
    *count=world.memberCount;for(unsigned i=0;i<world.memberCount;++i)members[i]=crew::ObjRef::Of(world.members[i]->bytes);
    *emitterCount=0;
    for(unsigned i=0;i<world.emitterCount;++i) {
        auto& native=world.emitters[i];if(!native.listed)continue;
        if(*emitterCount==emitterCapacity)return false;
        emitters[(*emitterCount)++]={native.kind,crew::ObjRef::Of(native.member->bytes),native.Data(),native.incarnation,native.scene ? native.sealedStorage : crew::ObjRef{}};
    }
    return true;
}
NativeEmitter* EmitterOf(World& world,const Emitter& emitter) noexcept {
    for(auto& native:world.emitters)if(native.Data()==emitter.object && native.kind==emitter.kind)return &native;
    return nullptr;
}
Life ReadEmitter(void* user,const Emitter& emitter,int* team) noexcept {
    auto& world=*static_cast<World*>(user);auto* native=EmitterOf(world,emitter);if(!native)return Life::invalid;
    ++native->reads;if(native->life!=Life::live)return native->life;
    if(native->incarnation!=emitter.incarnation && native->retiredIncarnationsGone)return Life::gone;
    if(native->scene) {
        // Fullweak binding is sealed when this cache is enumerated. A container may detach or its owner may die;
        // neither proves the independent SceneObject cache has ceased to exist.
        if(native->kind!=emitter.kind || native->incarnation!=emitter.incarnation ||
           emitter.member.obj!=native->sealedOwner.obj || emitter.member.ctrl!=native->sealedOwner.ctrl ||
           emitter.storage.obj!=native->sealedStorage.obj || emitter.storage.ctrl!=native->sealedStorage.ctrl)return Life::invalid;
        if(!emitter.storage.ctrl || edf::At<LONG>(emitter.storage.ctrl,8)<=0)return Life::gone;
        if(!emitter.storage.Is(native->scene->bytes))return Life::invalid;
        if(team)*team=native->Team();return Life::live;
    }
    if(native->kind!=emitter.kind || native->incarnation!=emitter.incarnation ||
       native->member->bytes!=emitter.member.obj || native->member->control!=emitter.member.ctrl)return Life::invalid;
    if(edf::At<LONG>(native->member->control,8)<=0)return Life::gone;
    if(!emitter.member.Is(native->member->bytes))return Life::invalid;
    if(team)*team=native->Team();return Life::live;
}
bool WriteEmitter(void* user,const Emitter& emitter,int team) noexcept {
    auto& world=*static_cast<World*>(user);auto* native=EmitterOf(world,emitter);int before=0;
    if(!native || ReadEmitter(user,emitter,&before)!=Life::live || native->refuseTeam==team)return false;
    if(native->Derived()){++native->validations;return before==team;}
    native->Team(team);++native->writes;return true;
}
bool MemberBaseline(void* user,const crew::ObjRef&,const crew::ObjRef& member,int rootTeam,int rootLast,int* team,int* last) noexcept {
    auto& world=*static_cast<World*>(user);++world.memberBaselines;world.baselineRootTeam=rootTeam;world.baselineRootLast=rootLast;
    auto* object=ObjectOf(member.obj);if(!world.memberHook || !object || !object->baselineKnown)return false;
    *team=object->baselineTeam;*last=object->baselineLast;return true;
}
bool EmitterBaseline(void* user,const Emitter& emitter,int ownerTeamValue,int ownerLast,int* team) noexcept {
    auto& world=*static_cast<World*>(user);++world.emitterBaselines;world.baselineOwnerTeam=ownerTeamValue;world.baselineOwnerLast=ownerLast;auto* native=EmitterOf(world,emitter);
    if(!world.emitterHook || !native || !native->baselineKnown)return false;
    *team=native->baseline;return true;
}
Hooks Adapter(World& world) noexcept {return {&world,&Collect,&ReadEmitter,&WriteEmitter,&MemberBaseline,&EmitterBaseline};}
struct SpawnSet {World* worlds[2]{};};
World* SpawnRoot(SpawnSet& set,const crew::ObjRef& root) noexcept {
    for(auto* world:set.worlds)if(world && world->root.bytes==root.obj && world->root.control==root.ctrl)return world;
    return nullptr;
}
World* SpawnEmitter(SpawnSet& set,const Emitter& emitter) noexcept {
    for(auto* world:set.worlds)if(world && EmitterOf(*world,emitter))return world;
    return nullptr;
}
bool SpawnCollect(void* user,const crew::ObjRef& root,crew::ObjRef* members,unsigned capacity,unsigned* count,
                  Emitter* emitters,unsigned emitterCapacity,unsigned* emitterCount) noexcept {
    auto* world=SpawnRoot(*static_cast<SpawnSet*>(user),root);
    return world && Collect(world,root,members,capacity,count,emitters,emitterCapacity,emitterCount);
}
Life SpawnRead(void* user,const Emitter& emitter,int* team) noexcept {
    auto* world=SpawnEmitter(*static_cast<SpawnSet*>(user),emitter);return world ? ReadEmitter(world,emitter,team) : Life::invalid;
}
bool SpawnWrite(void* user,const Emitter& emitter,int team) noexcept {
    auto* world=SpawnEmitter(*static_cast<SpawnSet*>(user),emitter);return world && WriteEmitter(world,emitter,team);
}
bool SpawnMemberBaseline(void* user,const crew::ObjRef& root,const crew::ObjRef& member,int rootTeam,int rootLast,int* team,int* last) noexcept {
    auto* world=SpawnRoot(*static_cast<SpawnSet*>(user),root);return world && MemberBaseline(world,root,member,rootTeam,rootLast,team,last);
}
bool SpawnEmitterBaseline(void* user,const Emitter& emitter,int ownerTeamValue,int ownerLast,int* team) noexcept {
    auto* world=SpawnEmitter(*static_cast<SpawnSet*>(user),emitter);return world && EmitterBaseline(world,emitter,ownerTeamValue,ownerLast,team);
}
Hooks SpawnAdapter(SpawnSet& set) noexcept {return {&set,&SpawnCollect,&SpawnRead,&SpawnWrite,&SpawnMemberBaseline,&SpawnEmitterBaseline};}
int Team(const Object& object) noexcept {return edf::At<int>(object.bytes,edf::kTeam);}
int Last(const Object& object) noexcept {return edf::At<int>(object.bytes,0x318);}
bool Registered(const Object& object) noexcept {return (edf::At<unsigned>(object.bytes,0x380)&0x40u)!=0;}
LONG Weak(const Object& object) noexcept {return edf::At<LONG>(object.control,12);}
void RequireRestore(Lease& lease) {CHECK(Restore(&lease));CHECK(lease.id==0);}
void CheckUntouchedIdentity(const World& world) {
    unsigned char expectedUser[sizeof(world.sourceUser)]{};std::memset(expectedUser,0x39,sizeof(expectedUser));
    CHECK(std::memcmp(expectedUser,world.sourceUser,sizeof(expectedUser))==0);
    CHECK(std::strcmp(reinterpret_cast<const char*>(world.sourcePuid),"authenticated-owner1")==0);
    for(const auto* object:{&world.root,&world.part}) {
        CHECK(edf::At<const void*>(object->bytes,0x1ED0)==world.sourceUser);
        CHECK(edf::At<const void*>(object->bytes,0x1ED8)==world.sourcePuid);
        CHECK(edf::At<unsigned short>(object->bytes,0x128)==2);
        bool identity=true;for(unsigned i=0;i<32;++i)identity=identity && object->bytes[0x108+i]==static_cast<unsigned char>(0x20u+i);
        CHECK(identity);
    }
}
void TestProbeAndTypedApply() {
    World world;Init(world);auto hooks=Adapter(world);const auto root=crew::ObjRef::Of(world.root.bytes);
    unsigned char before[sizeof(world.root.bytes)]{};std::memcpy(before,world.root.bytes,sizeof(before));
    CHECK(Probe(root,hooks));CHECK(nativeWrites==0 && Weak(world.root)==1 && Weak(world.part)==1);
    CHECK(std::memcmp(before,world.root.bytes,sizeof(before))==0);for(const auto& emitter:world.emitters)CHECK(emitter.writes==0);
    Lease lease{};CHECK(Acquire(root,"owner1",hooks,&lease));CHECK(lease.id!=0);
    CHECK(Team(world.root)==1 && Team(world.part)==1 && Last(world.root)==1);
    for(const auto& emitter:world.emitters)CHECK(emitter.Team()==1 && (emitter.Derived() ? emitter.writes==0 : emitter.writes>0));
    CHECK(world.emitters[2].Data()==world.emitters[3].Data());
    CHECK(edf::At<int>(world.emitters[2].bytes,0x148)==1 && edf::At<int>(world.emitters[2].bytes,0x10)==1);
    CHECK(edf::At<int>(world.emitters[3].bytes,0x10)==0); // only the GunBattery's actual mirror was written
    Status status{};CHECK(ReadStatus(lease,&status));CHECK(status.controlledTeam==1 && status.followsOwnerTeam && !status.restoring);
    CHECK(status.members==2 && status.emitters==8);CheckUntouchedIdentity(world);
    int applied=0,baseline=0,last=0;
    CHECK(ReadAppliedTeam(root,&applied) && applied==1);
    CHECK(ReadBaseline(root,crew::ObjRef::Of(world.part.bytes),&baseline,&last) && baseline==5 && last==5);
    ownerTeam=-1;CHECK(!ReadAppliedTeam(root,&applied));ownerTeam=1;
    ownerTeam=2;CHECK(!ReadAppliedTeam(root,&applied));CHECK(Frame(lease));CHECK(Team(world.root)==2 && Team(world.part)==2);
    CHECK(ReadAppliedTeam(root,&applied) && applied==2);
    for(const auto& emitter:world.emitters)CHECK(emitter.Team()==2);
    ownerTeam=0;CHECK(Frame(lease));CHECK(Team(world.root)==5 && Team(world.part)==5);CHECK(!ReadAppliedTeam(root,&applied));
    for(const auto& emitter:world.emitters)CHECK(emitter.Team()==5);
    CHECK(ReadStatus(lease,&status));CHECK(status.controlledTeam==5 && !status.followsOwnerTeam);CheckUntouchedIdentity(world);
    RequireRestore(lease);CHECK(Weak(world.root)==1 && Weak(world.part)==1);
}
void TestClosedAndDynamicRegistration() {
    World world;Init(world,-1,5,false);auto hooks=Adapter(world);Lease lease{};
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    CHECK(Team(world.root)==-1 && Last(world.root)==1 && !Registered(world.root));
    Status status{};CHECK(ReadStatus(lease,&status));CHECK(status.controlledTeam==-1 && status.followsOwnerTeam);
    edf::Put<unsigned>(world.root.bytes,0x380,edf::At<unsigned>(world.root.bytes,0x380)|0x40u);
    edf::Put<int>(world.root.bytes,edf::kTeam,1); // the native hatch opens while control is active
    ownerTeam=2;CHECK(Frame(lease));CHECK(Team(world.root)==2 && Last(world.root)==2 && Registered(world.root));
    RequireRestore(lease);CHECK(Team(world.root)==5 && Last(world.root)==5 && Registered(world.root));
    CHECK(Team(world.part)==-1 && Last(world.part)==5 && !Registered(world.part));
    Init(world,5,5,true);hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    edf::Put<unsigned>(world.root.bytes,0x380,edf::At<unsigned>(world.root.bytes,0x380)&~0x40u);
    ownerTeam=2;CHECK(Frame(lease));CHECK(!Registered(world.root));RequireRestore(lease);CHECK(!Registered(world.root));
    Init(world,5,4,true);hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    edf::Put<int>(world.root.bytes,edf::kTeam,-1);
    edf::Put<unsigned>(world.root.bytes,0x380,edf::At<unsigned>(world.root.bytes,0x380)&~0x40u);
    RequireRestore(lease);CHECK(Team(world.root)==-1 && Last(world.root)==4 && !Registered(world.root));
}
void TestDynamicBaselineAndPending() {
    World world;Init(world);auto hooks=Adapter(world);Lease lease{};CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    InitObject(world.newPart,1,1,true);world.newPart.baselineTeam=world.newPart.baselineLast=5;
    world.members[world.memberCount++]=&world.newPart;
    CHECK(Frame(lease));CHECK(world.memberBaselines>0 && world.baselineRootTeam==5 && world.baselineRootLast==5);
    CHECK(Team(world.newPart)==1);ownerTeam=2;CHECK(Frame(lease));CHECK(Team(world.newPart)==2);
    ownerTeam=-1;CHECK(!Frame(lease));Status status{};CHECK(ReadStatus(lease,&status));CHECK(!status.followsOwnerTeam);
    ownerTeam=0;CHECK(Frame(lease));CHECK(Team(world.newPart)==5);RequireRestore(lease);CHECK(Weak(world.newPart)==1);
    Init(world);hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    InitObject(world.newPart,1,1,true);world.newPart.baselineKnown=false;world.members[world.memberCount++]=&world.newPart;
        CHECK(!Frame(lease));CHECK(lease.id!=0);CHECK(!Restore(&lease));CHECK(lease.id!=0);
    int unknownTeam=123,unknownLast=456;
    CHECK(!ReadBaseline(crew::ObjRef::Of(world.root.bytes),crew::ObjRef::Of(world.newPart.bytes),&unknownTeam,&unknownLast));
    CHECK(unknownTeam==123 && unknownLast==456);CHECK(!ReadAppliedTeam(crew::ObjRef::Of(world.root.bytes),&unknownTeam));
    world.newPart.baselineKnown=true;world.newPart.baselineTeam=world.newPart.baselineLast=5;
    RequireRestore(lease);CHECK(Team(world.newPart)==5 && Weak(world.newPart)==1);
    CHECK(Weak(world.root)==1 && Weak(world.part)==1);
}
void TestDynamicEmitterBaseline() {
    World world;Init(world);world.emitterCount=3;auto hooks=Adapter(world);Lease lease{};
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    world.emitterCount=4;world.emitters[3].Team(1);world.emitters[3].baseline=5;
    CHECK(Frame(lease));CHECK(world.emitterBaselines>0 && world.baselineOwnerTeam==5 && world.baselineOwnerLast==5);
    CHECK(world.emitters[3].Team()==1);ownerTeam=2;CHECK(Frame(lease));CHECK(world.emitters[3].Team()==2);
    RequireRestore(lease);CHECK(world.emitters[3].Team()==5);
    Init(world);world.emitterCount=3;hooks=Adapter(world);hooks.emitterBaseline=nullptr;
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    world.emitterCount=4;world.emitters[3].Team(1);const auto writes=world.emitters[3].writes;
    CHECK(!Frame(lease));Status status{};CHECK(ReadStatus(lease,&status));CHECK(status.restoring && !status.followsOwnerTeam);
        CHECK(world.emitters[3].writes==writes);CHECK(!Restore(&lease));CHECK(lease.id!=0);
    world.emitters[3].life=Life::gone;world.emitters[3].listed=false;RequireRestore(lease);
}
void TestProvenDeadEmitterOwner() {
    World world;Init(world);auto hooks=Adapter(world);Lease lease{};
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    world.emitters[0].life=Life::invalid;const auto reads=world.emitters[0].reads;
    edf::Put<LONG>(world.root.control,8,0);CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(world.root.control+12))==1);
    watchedDead=world.root.bytes;deadReads=0;RequireRestore(lease);
    CHECK(world.emitters[0].reads==reads && deadReads==0 && controlDeletes==1);
    CHECK(Team(world.part)==5 && Weak(world.part)==1);watchedDead=nullptr;
}
void TestInheritedPartialSyncAndDirectRestore() {
    World world;Init(world);world.emitterCount=3;auto hooks=Adapter(world);Lease lease{};
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    InitObject(world.newPart,1,1,true);world.newPart.baselineTeam=world.newPart.baselineLast=5;
    world.members[world.memberCount++]=&world.newPart;
    world.emitterCount=4;world.emitters[3].Team(1);world.emitters[3].baselineKnown=false;
    CHECK(!Frame(lease));CHECK(Team(world.newPart)==5); // typed baseline was already acquired before the later emitter failed
    CHECK(!Restore(&lease));CHECK(lease.id!=0 && Weak(world.newPart)>1);
    world.emitters[3].baselineKnown=true;RequireRestore(lease);
    CHECK(Team(world.newPart)==5 && world.emitters[3].Team()==5 && Weak(world.newPart)==1);
    Init(world);world.emitterCount=3;hooks=Adapter(world);
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    InitObject(world.newPart,1,1,true);world.newPart.baselineTeam=world.newPart.baselineLast=5;
    world.members[world.memberCount++]=&world.newPart;
    world.emitterCount=4;world.emitters[3].Team(1); // native skill produced these after the last Frame, immediately before control exit
    RequireRestore(lease);CHECK(Team(world.newPart)==5 && world.emitters[3].Team()==5);
    CHECK(world.memberBaselines>0 && world.emitterBaselines>0 && Weak(world.newPart)==1);
}
void TestDerivedOwnerEmitters() {
    World world;Init(world,-1,5,false);auto hooks=Adapter(world);Lease lease{};
    unsigned char originalDerived[2][0x300]{};
    std::memcpy(originalDerived[0],world.emitters[4].bytes,sizeof(originalDerived[0]));
    std::memcpy(originalDerived[1],world.emitters[5].bytes,sizeof(originalDerived[1]));
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    CHECK(world.emitters[4].Team()==-1 && world.emitters[5].Team()==1);
    CHECK(world.emitters[4].writes==0 && world.emitters[5].writes==0 && nativeWrites==0);
    int applied=0;CHECK(ReadAppliedTeam(crew::ObjRef::Of(world.root.bytes),&applied) && applied==1);
    ownerTeam=2;CHECK(Frame(lease));CHECK(world.emitters[4].Team()==-1 && world.emitters[5].Team()==2);
    RequireRestore(lease);CHECK(world.emitters[4].Team()==-1 && world.emitters[5].Team()==5);
    CHECK(std::memcmp(originalDerived[0],world.emitters[4].bytes,sizeof(originalDerived[0]))==0);
    CHECK(std::memcmp(originalDerived[1],world.emitters[5].bytes,sizeof(originalDerived[1]))==0);
    Init(world);world.emitterCount=4;hooks=Adapter(world);hooks.emitterBaseline=nullptr;
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    InitObject(world.newPart,-1,1,false);world.newPart.baselineTeam=-1;world.newPart.baselineLast=5;
    world.members[world.memberCount++]=&world.newPart;
    world.emitters[4].member=world.emitters[5].member=&world.newPart;world.emitterCount=6;
    CHECK(Frame(lease));CHECK(world.memberBaselines>0 && world.emitterBaselines==0);
    CHECK(world.emitters[4].Team()==-1 && world.emitters[5].Team()==1);
    int baseline=0,last=0;CHECK(ReadBaseline(crew::ObjRef::Of(world.root.bytes),crew::ObjRef::Of(world.newPart.bytes),&baseline,&last));
    CHECK(baseline==-1 && last==5);CHECK(ReadAppliedTeam(crew::ObjRef::Of(world.newPart.bytes),&applied) && applied==1);
    ownerTeam=2;CHECK(Frame(lease));CHECK(world.emitters[4].Team()==-1 && world.emitters[5].Team()==2);
    RequireRestore(lease);CHECK(Team(world.newPart)==-1 && Last(world.newPart)==5 && Weak(world.newPart)==1);
    CHECK(world.emitters[4].writes==0 && world.emitters[5].writes==0);
}
void TestPartialFailureRetainsLease() {
    World world;Init(world);auto hooks=Adapter(world);world.part.refuseTeamA=1;world.root.refuseTeamA=5;
    Lease lease{};CHECK(!Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));CHECK(lease.id!=0);
    CHECK(Weak(world.root)==2 && Weak(world.part)==2);Status status{};CHECK(ReadStatus(lease,&status));CHECK(status.restoring && !status.followsOwnerTeam);
    CHECK(!Restore(&lease));CHECK(lease.id!=0 && Weak(world.root)>1);
    world.part.refuseTeamA=world.root.refuseTeamA=-999;RequireRestore(lease);
    CHECK(Team(world.root)==5 && Team(world.part)==5 && Weak(world.root)==1 && Weak(world.part)==1);
    Init(world);hooks=Adapter(world);world.emitters[2].refuseTeam=1;world.emitters[0].refuseTeam=5;
    CHECK(!Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));CHECK(lease.id!=0);
    world.emitters[2].refuseTeam=world.emitters[0].refuseTeam=-999;RequireRestore(lease);
    for(const auto& emitter:world.emitters)CHECK(emitter.Team()==5);
}
void TestEmitterInvalidAndGone() {
    World world;Init(world);auto hooks=Adapter(world);Lease lease{};CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    world.emitters[2].life=Life::invalid;CHECK(!Frame(lease));CHECK(!Restore(&lease));CHECK(lease.id!=0);
    world.emitters[2].life=Life::live;RequireRestore(lease);CHECK(world.emitters[2].Team()==5);
    Init(world);hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    const auto writes=world.emitters[2].writes;world.emitters[2].life=Life::gone;world.emitters[2].listed=false;
    CHECK(Frame(lease));RequireRestore(lease);CHECK(world.emitters[2].writes==writes);
}
void TestSpawnReservationBounds() {
    World main,child;Init(main);Init(child,6,6,true,false);Lease lease{};
    const auto parent=crew::ObjRef::Of(main.root.bytes),born=crew::ObjRef::Of(child.root.bytes);
    CHECK(Acquire(parent,"owner1",Adapter(main),&lease));const SpawnProof proof{Adapter(child),3,8};
    unsigned char before[sizeof(main.root.bytes)]{};std::memcpy(before,main.root.bytes,sizeof(before));
    const auto writes=nativeWrites;std::uint64_t token=99;
    CHECK(!CanPrepareSpawns(parent,proof,4096));CHECK(!PrepareSpawns(parent,proof,4096,&token) && token==0);
    auto oversized=proof;oversized.maxEmitters=512;CHECK(!PrepareSpawns(parent,oversized,1,&token) && token==0);
    oversized=proof;oversized.maxMembers=256;CHECK(!CanPrepareSpawns(parent,oversized,1));
    const SpawnProof unknown{};CHECK(!PrepareSpawns(parent,unknown,1,&token) && token==0);
    CHECK(nativeWrites==writes && std::memcmp(before,main.root.bytes,sizeof(before))==0 && Weak(child.root)==1);
    CHECK(CanPrepareSpawns(parent,proof,2));ownerTeam=-1;CHECK(!CanPrepareSpawns(parent,proof,1));ownerTeam=1;
    SpawnProof groupBound{Adapter(child),1,0};CHECK(PrepareSpawns(parent,groupBound,128,&token));
    CHECK(!CanPrepareSpawns(parent,groupBound,1));FinishSpawns(token);CHECK(CanPrepareSpawns(parent,proof,2));
    auto tooSmall=proof;tooSmall.maxMembers=1;CHECK(PrepareSpawns(parent,tooSmall,1,&token));
    CHECK(!AdoptSpawn(token,parent,born));CHECK(Weak(child.root)==1 && Weak(child.part)==1 && Team(child.root)==6);FinishSpawns(token);
    tooSmall=proof;tooSmall.maxEmitters=5;CHECK(PrepareSpawns(parent,tooSmall,1,&token));
    CHECK(!AdoptSpawn(token,parent,born));CHECK(Weak(child.root)==1 && nativeWrites==writes);FinishSpawns(token);
    CHECK(PrepareSpawns(parent,proof,1,&token));CHECK(!AdoptSpawn(token+1000,parent,born));
    CHECK(!AdoptSpawn(token,crew::ObjRef::Of(main.part.bytes),born));CHECK(Weak(child.root)==1);
    CHECK(AdoptSpawn(token,parent,born));CHECK(Team(child.root)==1 && Weak(child.root)==2);
    FinishSpawns(token);CHECK(Team(child.root)==1 && Weak(child.root)==2);CHECK(!AdoptSpawn(token,parent,born));
    RequireRestore(lease);CHECK(Team(child.root)==6 && Weak(child.root)==1 && Weak(child.part)==1);
}
bool PrepareChildInstance(void* user,const crew::ObjRef& child) noexcept {
    auto& world=*static_cast<World*>(user);++world.initializations;
    CHECK(child.obj==world.root.bytes && child.ctrl==world.root.control);
    CHECK(Weak(world.root)==1 && Weak(world.part)==1); // no generic capture before the native producer's real setup
    for(unsigned i=0;i<world.memberCount;++i) {
        RawTeam(world.members[i]->bytes,1,Registered(*world.members[i]));
        world.members[i]->baselineTeam=world.members[i]->baselineLast=1;
    }
    for(auto& emitter:world.emitters){emitter.Team(1);emitter.baseline=1;}
    if(world.initializerGrow)world.members[world.memberCount++]=&world.newPart;
    if(world.initializerFinish)FinishSpawns(world.initializerToken);
    if(world.initializerRestore){CHECK(world.initializerLease);if(world.initializerLease)CHECK(Restore(world.initializerLease));}
    if(world.initializerAcquire){CHECK(world.initializerLease);if(world.initializerLease)CHECK(Acquire(child,"owner1",Adapter(world),world.initializerLease));}
    return world.initializerResult;
}
void TestSpawnProducerInitializationCommit() {
    World main,child;Init(main);Init(child,6,6,true,false);ownerTeam=2;Lease lease{};
    const auto parent=crew::ObjRef::Of(main.root.bytes),born=crew::ObjRef::Of(child.root.bytes);
    const SpawnProof proof{Adapter(child),3,8,&PrepareChildInstance};
    CHECK(Probe(born,proof.hooks));CHECK(child.initializations==0 && Team(child.root)==6);
    CHECK(Acquire(parent,"owner1",Adapter(main),&lease));const auto writes=nativeWrites;std::uint64_t token=0;
    CHECK(CanPrepareSpawns(parent,proof,1));CHECK(CanPrepareAcceptedSpawns(parent,proof,1));
    CHECK(PrepareSpawns(parent,proof,1,&token));CHECK(child.initializations==0 && nativeWrites==writes && Team(child.root)==6);
    CHECK(!AdoptSpawn(token+1000,parent,born));CHECK(!AdoptSpawn(token,crew::ObjRef::Of(main.part.bytes),born));
    CHECK(child.initializations==0 && Team(child.root)==6 && Weak(child.root)==1);
    CHECK(AdoptSpawn(token,parent,born));CHECK(child.initializations==1 && Team(child.root)==2 && Team(child.part)==2);
    int team=-1,last=-1;CHECK(ReadBaselineForMember(born,&team,&last) && team==1 && last==1);
    CHECK(child.initializations==1 && Weak(child.root)==2 && Weak(child.part)==2);
    FinishSpawns(token);RequireRestore(lease);CHECK(Team(child.root)==1 && Last(child.root)==1 && Team(child.part)==1);
    for(const auto& emitter:child.emitters)CHECK(emitter.Team()==1);
    CHECK(Weak(child.root)==1 && Weak(child.part)==1);CheckUntouchedIdentity(child);

    Init(main);Init(child,6,6,true,false);ownerTeam=2;CHECK(Acquire(crew::ObjRef::Of(main.root.bytes),"owner1",Adapter(main),&lease));
    child.initializerResult=false;CHECK(PrepareSpawns(crew::ObjRef::Of(main.root.bytes),proof,1,&token));
    CHECK(!AdoptSpawn(token,crew::ObjRef::Of(main.root.bytes),crew::ObjRef::Of(child.root.bytes)));
    CHECK(child.initializations==1 && Team(child.root)==1 && Weak(child.root)==1 && Weak(child.part)==1);
    CHECK(!ReadBaselineForMember(crew::ObjRef::Of(child.root.bytes),&team,&last) && !OwnsMember(crew::ObjRef::Of(child.root.bytes)));
    child.initializerResult=true;CHECK(AdoptSpawn(token,crew::ObjRef::Of(main.root.bytes),crew::ObjRef::Of(child.root.bytes)));
    CHECK(child.initializations==2 && Team(child.root)==2);FinishSpawns(token);RequireRestore(lease);CHECK(Team(child.root)==1);
}
void TestSpawnInitializerIdentityAndReentrancy() {
    World main,child;Init(main);Init(child,6,6,true,false);ownerTeam=2;Lease lease{},other{};
    CHECK(Acquire(crew::ObjRef::Of(main.root.bytes),"owner1",Adapter(main),&lease));
    const SpawnProof proof{Adapter(child),3,8,&PrepareChildInstance};std::uint64_t token=0;
    auto narrow=proof;narrow.maxMembers=1;CHECK(PrepareSpawns(crew::ObjRef::Of(main.root.bytes),narrow,1,&token));
    CHECK(!AdoptSpawn(token,crew::ObjRef::Of(main.root.bytes),crew::ObjRef::Of(child.root.bytes)));
    CHECK(child.initializations==0 && Weak(child.root)==1 && Team(child.root)==6);FinishSpawns(token);
    CHECK(Acquire(crew::ObjRef::Of(child.root.bytes),"owner1",Adapter(child),&other));
    CHECK(PrepareSpawns(crew::ObjRef::Of(main.root.bytes),proof,1,&token));
    CHECK(!AdoptSpawn(token,crew::ObjRef::Of(main.root.bytes),crew::ObjRef::Of(child.root.bytes)));
    CHECK(child.initializations==0 && Weak(child.root)==2);FinishSpawns(token);RequireRestore(other);RequireRestore(lease);

    for(unsigned action=0;action<4;++action) {
        Init(main);Init(child,6,6,true,false);ownerTeam=2;
        CHECK(Acquire(crew::ObjRef::Of(main.root.bytes),"owner1",Adapter(main),&lease));
        auto bounded=proof;bounded.maxMembers=2;CHECK(PrepareSpawns(crew::ObjRef::Of(main.root.bytes),bounded,1,&token));
        child.initializerToken=token;child.initializerLease=action==3 ? &other : &lease;
        child.initializerFinish=action==0;child.initializerRestore=action==1;child.initializerGrow=action==2;child.initializerAcquire=action==3;
        CHECK(!AdoptSpawn(token,crew::ObjRef::Of(main.root.bytes),crew::ObjRef::Of(child.root.bytes)));
        CHECK(child.initializations==1);
        if(action==3) {
            CHECK(other.id && OwnsMember(crew::ObjRef::Of(child.root.bytes)) && Weak(child.root)==2);
            RequireRestore(other);CHECK(Team(child.root)==1);
        } else {
            CHECK(Weak(child.root)==1 && Weak(child.part)==1 && Team(child.root)==1);
            CHECK(!OwnsMember(crew::ObjRef::Of(child.root.bytes)));
        }
        FinishSpawns(token);RequireRestore(lease);CHECK(Weak(child.root)==1 && Weak(child.part)==1);
    }
}
void TestSpawnGroupsFollowAndRestore() {
    World main,first,second;Init(main);Init(first,6,6,true,false);Init(second,4,4,false,false);
    SpawnSet set{{&first,&second}};const SpawnProof proof{SpawnAdapter(set),3,8};Lease lease{};
    const auto parent=crew::ObjRef::Of(main.root.bytes);
    CHECK(Acquire(parent,"owner1",Adapter(main),&lease));
    unsigned char firstBefore[2][sizeof(first.root.bytes)]{},secondBefore[2][sizeof(second.root.bytes)]{};
    std::memcpy(firstBefore[0],first.root.bytes,sizeof(first.root.bytes));std::memcpy(firstBefore[1],first.part.bytes,sizeof(first.part.bytes));
    std::memcpy(secondBefore[0],second.root.bytes,sizeof(second.root.bytes));std::memcpy(secondBefore[1],second.part.bytes,sizeof(second.part.bytes));
    std::uint64_t token=0;CHECK(PrepareSpawns(parent,proof,2,&token));
    CHECK(AdoptSpawn(token,parent,crew::ObjRef::Of(first.root.bytes)));
    CHECK(Team(first.root)==1 && Team(first.part)==1 && Team(second.root)==4);
    CHECK(AdoptSpawn(token,parent,crew::ObjRef::Of(second.root.bytes)));FinishSpawns(token);
    CHECK(Team(second.root)==1 && Team(second.part)==1 && !Registered(second.root));
    CHECK(first.emitters[0].reads>0 && second.emitters[0].reads>0);Status status{};CHECK(ReadStatus(lease,&status));
    CHECK(status.members==6 && status.emitters==24 && status.followsOwnerTeam);
    ownerTeam=2;CHECK(Frame(lease));CHECK(Team(first.root)==2 && Team(second.root)==2);
    InitObject(first.newPart,2,2,true);first.newPart.baselineTeam=first.newPart.baselineLast=6;
    first.members[first.memberCount++]=&first.newPart;CHECK(Frame(lease));
    CHECK(first.memberBaselines>0 && first.baselineRootTeam==6 && first.baselineRootLast==6 && main.memberBaselines==0);
    int original=0,last=0;CHECK(ReadBaselineForMember(crew::ObjRef::Of(first.newPart.bytes),&original,&last));CHECK(original==6 && last==6);
    ownerTeam=0;CHECK(Frame(lease));CHECK(Team(first.root)==6 && Team(first.newPart)==6 && Team(second.root)==4 && Team(main.root)==5);
    ownerTeam=1;CHECK(Frame(lease));CHECK(Team(first.newPart)==1 && Team(second.part)==1);
    RequireRestore(lease);CHECK(Team(first.newPart)==6 && Weak(first.newPart)==1);
    CHECK(std::memcmp(firstBefore[0],first.root.bytes,sizeof(first.root.bytes))==0);
    CHECK(std::memcmp(firstBefore[1],first.part.bytes,sizeof(first.part.bytes))==0);
    CHECK(std::memcmp(secondBefore[0],second.root.bytes,sizeof(second.root.bytes))==0);
    CHECK(std::memcmp(secondBefore[1],second.part.bytes,sizeof(second.part.bytes))==0);
    CHECK(Weak(first.root)==1 && Weak(second.root)==1 && controlDeletes==0);
    for(const auto& emitter:first.emitters)CHECK(emitter.Team()==6);
    for(const auto& emitter:second.emitters)CHECK(emitter.Team()==4);
}
void TestSpawnFinishAndPartialFailure() {
    World main,child;Init(main);Init(child,6,6,true,false);Lease lease{};
    const auto parent=crew::ObjRef::Of(main.root.bytes),born=crew::ObjRef::Of(child.root.bytes);const SpawnProof proof{Adapter(child),3,8};
    CHECK(Acquire(parent,"owner1",Adapter(main),&lease));std::uint64_t token=0;
    CHECK(PrepareSpawns(parent,proof,2,&token));CHECK(AdoptSpawn(token,parent,born));FinishSpawns(token);
    CHECK(Team(child.root)==1 && Weak(child.root)==2);CHECK(CanPrepareSpawns(parent,proof,1));RequireRestore(lease);
    CHECK(Team(child.root)==6 && Weak(child.root)==1);
    Init(main);Init(child,6,6,true,false);CHECK(Acquire(crew::ObjRef::Of(main.root.bytes),"owner1",Adapter(main),&lease));
    child.root.refuseTeamA=6;child.part.refuseTeamA=1;
    CHECK(PrepareSpawns(crew::ObjRef::Of(main.root.bytes),SpawnProof{Adapter(child),3,8},1,&token));
    CHECK(!AdoptSpawn(token,crew::ObjRef::Of(main.root.bytes),crew::ObjRef::Of(child.root.bytes)));
    CHECK(lease.id!=0 && Weak(child.root)==2 && Weak(child.part)==2);Status status{};CHECK(ReadStatus(lease,&status));
    CHECK(status.restoring && !status.followsOwnerTeam);FinishSpawns(token);
    CHECK(!Restore(&lease) && lease.id!=0 && Weak(child.root)==2);
    child.root.refuseTeamA=child.part.refuseTeamA=-999;RequireRestore(lease);
    CHECK(Team(child.root)==6 && Team(child.part)==6 && Weak(child.root)==1 && Weak(child.part)==1 && controlDeletes==0);
}
void TestRecursiveNativeModesAndPartialRestore() {
    World world;Init(world);InitObject(world.part,-1,5,false);InitObject(world.newPart,6,6,false);
    world.members[2]=&world.newPart;world.memberCount=3;world.cascadeNative=true;ownerTeam=2;
    Lease lease{};auto hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    CHECK(Team(world.root)==2 && Registered(world.root));
    CHECK(Team(world.part)==-1 && Last(world.part)==2 && !Registered(world.part));
    CHECK(Team(world.newPart)==2 && Last(world.newPart)==2 && !Registered(world.newPart));
    RequireRestore(lease);CHECK(Team(world.part)==-1 && Last(world.part)==5 && !Registered(world.part));
    CHECK(Team(world.newPart)==6 && Last(world.newPart)==6 && !Registered(world.newPart));
    Init(world);InitObject(world.part,-1,5,false);InitObject(world.newPart,6,6,false);
    world.members[2]=&world.newPart;world.memberCount=3;world.cascadeNative=true;ownerTeam=2;hooks=Adapter(world);
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    RawTeam(world.part.bytes,2,false);RawTeam(world.newPart.bytes,-1,false); // skill transitions are live state, not control-owned originals
    RequireRestore(lease);CHECK(Team(world.part)==5 && Last(world.part)==5 && !Registered(world.part));
    CHECK(Team(world.newPart)==-1 && Last(world.newPart)==6 && !Registered(world.newPart));
    Init(world);InitObject(world.part,-1,5,false);InitObject(world.newPart,6,6,false);
    world.members[2]=&world.newPart;world.memberCount=3;world.cascadeNative=true;ownerTeam=2;hooks=Adapter(world);
    world.root.refuseTeamA=5;world.newPart.refuseTeamA=2;
    CHECK(!Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    CHECK(lease.id!=0 && Weak(world.root)==2 && Weak(world.part)==2 && Weak(world.newPart)==2);
    CHECK(Team(world.root)==2 && Team(world.part)==-1 && Last(world.part)==5 && !Registered(world.part));
    CHECK(Team(world.newPart)==6 && Last(world.newPart)==6 && !Registered(world.newPart));
    CHECK(!Restore(&lease) && lease.id!=0);
    CHECK(Team(world.part)==-1 && Last(world.part)==5 && !Registered(world.part));
    CHECK(Team(world.newPart)==6 && Last(world.newPart)==6 && !Registered(world.newPart));
    world.root.refuseTeamA=world.newPart.refuseTeamA=-999;RequireRestore(lease);
    CHECK(Team(world.root)==5 && Team(world.part)==-1 && Last(world.part)==5 && !Registered(world.part));
    CHECK(Team(world.newPart)==6 && Last(world.newPart)==6 && !Registered(world.newPart));
    CHECK(Weak(world.root)==1 && Weak(world.part)==1 && Weak(world.newPart)==1);
}
void TestSpawnRootPinnedBeforePartialCapture() {
    World main,child;Init(main);Init(child,6,6,true,false);Lease lease{};
    const auto parent=crew::ObjRef::Of(main.root.bytes),born=crew::ObjRef::Of(child.root.bytes);
    CHECK(Acquire(parent,"owner1",Adapter(main),&lease));
    child.members[0]=&child.part;child.members[1]=&child.root; // the typed collector is not required to put its root first
    std::uint64_t token=0;CHECK(PrepareSpawns(parent,SpawnProof{Adapter(child),3,8},1,&token));
    dieOnControlRead=child.part.control;dyingWorld=&child;controlReadCount=0;dieAtRead=2;
    CHECK(!AdoptSpawn(token,parent,born));dieOnControlRead=nullptr;dyingWorld=nullptr;
    CHECK(lease.id!=0 && Weak(child.root)==2 && Weak(child.part)==0 && controlDeletes==1);
    Status status{};CHECK(ReadStatus(lease,&status));CHECK(status.restoring && !status.followsOwnerTeam);
    FinishSpawns(token);CHECK(Weak(child.root)==2);CHECK(!Restore(&lease) && lease.id!=0);
    child.collectOk=true;child.members[0]=&child.root;child.memberCount=1;child.emitterCount=1;
    RequireRestore(lease);CHECK(Team(child.root)==6 && Weak(child.root)==1 && controlDeletes==1);
}
void TestPermanentDisabledAndOwnershipQueries() {
    World world;Init(world,-1,-1,false);auto hooks=Adapter(world);Lease lease{};
    const auto root=crew::ObjRef::Of(world.root.bytes),part=crew::ObjRef::Of(world.part.bytes);
    CHECK(Probe(root,hooks));CHECK(!OwnsMember(root) && !FaultForMember(root));CHECK(nativeWrites==0);
    CHECK(Acquire(root,"owner1",hooks,&lease));CHECK(Team(world.root)==-1 && Last(world.root)==1 && !Registered(world.root));
    CHECK(OwnsMember(root) && OwnsMember(part));ownerTeam=2;CHECK(Frame(lease));CHECK(Last(world.root)==2 && Last(world.part)==2);
    ownerTeam=0;CHECK(Frame(lease));CHECK(Team(world.root)==-1 && Last(world.root)==-1 && !Registered(world.root));
    CHECK(OwnsMember(root));ownerTeam=1;CHECK(Frame(lease));RequireRestore(lease);
    CHECK(Team(world.root)==-1 && Last(world.root)==-1 && Team(world.part)==-1 && Last(world.part)==-1);
    CHECK(!OwnsMember(root) && !FaultForMember(root));
    Init(world);hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    InitObject(world.newPart,-1,1,false);world.newPart.baselineTeam=world.newPart.baselineLast=-1;
    world.members[world.memberCount++]=&world.newPart;CHECK(Frame(lease));
    CHECK(Team(world.newPart)==-1 && Last(world.newPart)==1 && !Registered(world.newPart));
    RequireRestore(lease);CHECK(Team(world.newPart)==-1 && Last(world.newPart)==-1 && Weak(world.newPart)==1);
    Init(world,5,-1,true);CHECK(!Probe(crew::ObjRef::Of(world.root.bytes),Adapter(world)));
    Init(world);hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    ownerTeam=-1;CHECK(!Frame(lease));CHECK(OwnsMember(crew::ObjRef::Of(world.root.bytes)));
    CHECK(FaultForMember(crew::ObjRef::Of(world.part.bytes),Result::unsupported));Status status{};CHECK(ReadStatus(lease,&status));
    CHECK(status.restoring && OwnsMember(crew::ObjRef::Of(world.part.bytes)) && !Frame(lease));
    RequireRestore(lease);CHECK(!OwnsMember(crew::ObjRef::Of(world.part.bytes)));
    Init(world,-1,-1,false);hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    RawTeam(world.root.bytes,1,false);CHECK(!Restore(&lease) && lease.id!=0 && Weak(world.root)==2);
    RawTeam(world.root.bytes,-1,false);RequireRestore(lease);
    CHECK(Team(world.root)==-1 && Last(world.root)==-1 && Weak(world.root)==1);
}
void TestEmitterBaselineQueriesAfterNativeRetag() {
    World world;Init(world);world.cascadeNative=true;world.members[2]=&world.newPart;world.memberCount=3;
    auto hooks=Adapter(world);Lease lease{};
    Emitter mirror{EmitterKind::ownerTeamMirror,crew::ObjRef::Of(world.part.bytes),world.emitters[3].Data(),world.emitters[3].incarnation};
    int baseline=777;CHECK(!ReadEmitterBaseline(mirror,&baseline) && baseline==777);
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    CHECK(world.emitters[3].Team()==1);CHECK(ReadEmitterBaseline(mirror,&baseline) && baseline==5);
    auto unknown=mirror;++unknown.incarnation;CHECK(!ReadEmitterBaseline(unknown,&baseline));
    ownerTeam=-1;CHECK(!Frame(lease));CHECK(ReadEmitterBaseline(mirror,&baseline) && baseline==5);
    CHECK(FaultForMember(crew::ObjRef::Of(world.root.bytes)));world.emitters[3].Team(2);
    CHECK(ReadEmitterBaseline(mirror,&baseline) && baseline==5 && world.emitters[3].Team()==2);
    RequireRestore(lease);CHECK(world.emitters[3].Team()==5 && !ReadEmitterBaseline(mirror,&baseline));
    Init(world);world.emitterCount=3;hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    world.emitterCount=4;world.emitters[3].Team(1);world.emitters[3].baselineKnown=false;
    mirror={EmitterKind::ownerTeamMirror,crew::ObjRef::Of(world.part.bytes),world.emitters[3].Data(),world.emitters[3].incarnation};
    CHECK(!Frame(lease));baseline=777;CHECK(!ReadEmitterBaseline(mirror,&baseline) && baseline==777);
    world.emitters[3].baselineKnown=true;RequireRestore(lease);CHECK(world.emitters[3].Team()==5);
}
void TestOwnerCachesAndLeasePresence() {
    CHECK(!HasLeases());World world;Init(world,-1,5,false);ownerTeam=2;
    world.emitters[6].Team(6);world.emitters[6].baseline=6;world.emitters[7].Team(4);world.emitters[7].baseline=4;
    auto hooks=Adapter(world);Lease lease{};CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    CHECK(HasLeases());CHECK(world.emitters[6].Team()==-1 && world.emitters[7].Team()==2 && world.emitters[3].Team()==2);
    CHECK(world.emitters[6].writes>0 && world.emitters[7].writes>0);
    RawTeam(world.part.bytes,2,false);CHECK(Frame(lease));CHECK(world.emitters[6].Team()==2 && world.emitters[7].Team()==2);
    ownerTeam=-1;CHECK(!Frame(lease));CHECK(HasLeases());ownerTeam=2;RequireRestore(lease);
    CHECK(!HasLeases() && world.emitters[6].Team()==6 && world.emitters[7].Team()==4 && world.emitters[3].Team()==5);
    Init(world);world.emitterCount=6;hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    world.emitters[6].Team(1);world.emitters[6].baseline=6;world.emitters[7].Team(1);world.emitters[7].baseline=4;world.emitterCount=8;
    CHECK(Frame(lease));CHECK(world.emitterBaselines>=2 && world.emitters[6].Team()==1 && world.emitters[7].Team()==1);
    RequireRestore(lease);CHECK(world.emitters[6].Team()==6 && world.emitters[7].Team()==4 && !HasLeases());
    Init(world);world.emitterCount=6;hooks=Adapter(world);hooks.emitterBaseline=nullptr;
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    world.emitters[6].Team(1);world.emitters[7].Team(1);world.emitterCount=8;CHECK(!Frame(lease));
    CHECK(!Restore(&lease) && HasLeases());world.emitters[6].life=world.emitters[7].life=Life::gone;
    world.emitters[6].listed=world.emitters[7].listed=false;RequireRestore(lease);CHECK(!HasLeases());
    Init(world);hooks=Adapter(world);world.emitters[7].refuseTeam=1;world.emitters[6].refuseTeam=5;
    CHECK(!Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));CHECK(lease.id!=0 && HasLeases());
    Status status{};CHECK(ReadStatus(lease,&status));CHECK(status.restoring && world.emitters[6].Team()==1);
    CHECK(!Restore(&lease) && HasLeases());world.emitters[6].refuseTeam=world.emitters[7].refuseTeam=-999;
    RequireRestore(lease);CHECK(!HasLeases() && world.emitters[6].Team()==5 && world.emitters[7].Team()==5);
    Init(world);world.emitterCount=7;hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    auto& cache=world.emitters[6];Emitter old{cache.kind,crew::ObjRef::Of(cache.member->bytes),cache.Data(),cache.incarnation};
    cache.retiredIncarnationsGone=true;++cache.incarnation;cache.baseline=6;cache.Team(1);
    CHECK(Frame(lease));Emitter replacement{cache.kind,crew::ObjRef::Of(cache.member->bytes),cache.Data(),cache.incarnation};
    int original=0;CHECK(!ReadEmitterBaseline(old,&original));CHECK(ReadEmitterBaseline(replacement,&original) && original==6);
    RequireRestore(lease);CHECK(cache.Team()==6 && !HasLeases());
}
void TestTemporaryTagSeparateFromLease() {
    World world;Init(world);ownerTeam=0;Lease lease{};const auto root=crew::ObjRef::Of(world.root.bytes);
    CHECK(!HasTemporaryTeam(root));CHECK(Acquire(root,"owner1",Adapter(world),&lease));
    CHECK(HasLeases() && OwnsMember(root) && !HasTemporaryTeam(root));
    ownerTeam=1;CHECK(Frame(lease));CHECK(HasTemporaryTeam(root));
    ownerTeam=-1;CHECK(!Frame(lease));CHECK(HasTemporaryTeam(root) && OwnsMember(root));
    ownerTeam=0;CHECK(Frame(lease));CHECK(!HasTemporaryTeam(root) && HasLeases());
    ownerTeam=2;CHECK(Frame(lease));CHECK(FaultForMember(root));CHECK(HasTemporaryTeam(root) && HasLeases());
    RequireRestore(lease);CHECK(!HasTemporaryTeam(root) && !HasLeases());
}
void SceneCache(World& world,unsigned index,Object& scene,int baseline) {
    auto& cache=world.emitters[index];cache.scene=&scene;cache.storage=nullptr;cache.kind=EmitterKind::ownerTeamCache;
    cache.sealedOwner=crew::ObjRef::Of(cache.member->bytes);cache.sealedStorage=crew::ObjRef::Of(scene.bytes);
    cache.baseline=baseline;cache.Team(baseline);
}
void TestIndependentStorageLifetimeAndSealedRead() {
    World world;Object scene;Init(world);InitObject(scene,5,5,false);world.emitterCount=7;SceneCache(world,6,scene,6);
    Lease lease{};auto hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    CHECK(Weak(scene)==2 && world.emitters[6].Team()==1);
    const auto readBefore=world.emitters[6].reads;world.emitters[6].listed=false;CHECK(Frame(lease));
    CHECK(world.emitters[6].reads>readBefore && world.emitters[6].Team()==6 && Weak(scene)==1);
    RequireRestore(lease);CHECK(Weak(scene)==1);
    Init(world);InitObject(scene,5,5,false);world.emitterCount=7;SceneCache(world,6,scene,6);
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    world.memberCount=1;for(unsigned i=1;i<world.emitterCount;++i)world.emitters[i].listed=false;
    edf::Put<LONG>(world.part.control,8,0);CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(world.part.control+12))==1);
    watchedDead=world.part.bytes;deadReads=0;CHECK(Frame(lease));CHECK(deadReads==0);
    CHECK(world.emitters[6].Team()==6 && Weak(scene)==1 && Weak(world.part)==0 && controlDeletes==1);
    watchedDead=nullptr;RequireRestore(lease);
    Init(world);InitObject(scene,5,5,false);world.emitterCount=7;SceneCache(world,6,scene,6);
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    world.emitters[6].listed=false;edf::Put<void*>(scene.bytes,edf::kSelf,nullptr);
    CHECK(!Frame(lease));CHECK(!Restore(&lease) && lease.id!=0 && Weak(scene)==2);
    edf::Put<void*>(scene.bytes,edf::kSelf,scene.bytes);RequireRestore(lease);CHECK(world.emitters[6].Team()==6 && Weak(scene)==1);
    Init(world);InitObject(scene,5,5,false);world.emitterCount=7;SceneCache(world,6,scene,6);
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    world.emitters[6].listed=false;edf::Put<void*>(scene.bytes,edf::kSelf,nullptr);
    CHECK(!Frame(lease));CHECK(!Restore(&lease) && Weak(scene)==2);
    edf::Put<LONG>(scene.control,8,0);CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(scene.control+12))==1);
    watchedDead=scene.bytes;deadReads=0;RequireRestore(lease);CHECK(deadReads==0 && Weak(scene)==0 && controlDeletes==1);watchedDead=nullptr;
}
void TestStoragePartialRestoreAndReplacement() {
    World world;Object first,second;Init(world);InitObject(first,5,5,false);InitObject(second,5,5,false);
    world.emitterCount=7;SceneCache(world,6,first,6);Lease lease{};
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    world.emitters[6].listed=false;world.emitters[6].refuseTeam=6;CHECK(!Frame(lease));
    CHECK(!Restore(&lease) && lease.id!=0 && Weak(first)==2 && world.emitters[6].Team()==1);
    world.emitters[6].refuseTeam=-999;RequireRestore(lease);CHECK(Weak(first)==1 && world.emitters[6].Team()==6);
    Init(world);InitObject(first,5,5,false);InitObject(second,5,5,false);world.emitterCount=7;SceneCache(world,6,first,6);
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    world.emitters[6].listed=false;SceneCache(world,7,second,4);world.emitters[7].Team(1);world.emitterCount=8;
    CHECK(Frame(lease));CHECK(world.emitters[6].Team()==6 && Weak(first)==1 && world.emitters[7].Team()==1 && Weak(second)==2);
    RequireRestore(lease);CHECK(world.emitters[7].Team()==4 && Weak(second)==1);
    Init(world);InitObject(first,5,5,false);InitObject(second,5,5,false);world.emitterCount=7;SceneCache(world,6,first,6);
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    const auto incarnation=world.emitters[6].incarnation;
    edf::Put<LONG>(first.control,8,0);CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(first.control+12))==1);
    edf::Put<void*>(first.bytes,edf::kSelfCtrl,second.control);world.emitters[6].sealedStorage={first.bytes,second.control};
    world.emitters[6].baseline=4;world.emitters[6].Team(1);CHECK(Frame(lease));
    CHECK(world.emitters[6].incarnation==incarnation && Weak(first)==0 && Weak(second)==2 && controlDeletes==1);
    RequireRestore(lease);CHECK(world.emitters[6].Team()==4 && Weak(second)==1 && controlDeletes==1);
}
void TestSpawnStoragePinAndModeOff() {
    World main,child;Object scene;Init(main);Init(child,6,6,true,false);InitObject(scene,5,5,false);
    child.emitterCount=7;SceneCache(child,6,scene,4);Lease lease{};
    const auto parent=crew::ObjRef::Of(main.root.bytes);CHECK(Acquire(parent,"owner1",Adapter(main),&lease));
    std::uint64_t token=0;CHECK(PrepareSpawns(parent,SpawnProof{Adapter(child),3,8},1,&token));
    CHECK(Weak(scene)==1);CHECK(AdoptSpawn(token,parent,crew::ObjRef::Of(child.root.bytes)));FinishSpawns(token);
    CHECK(Weak(scene)==2 && child.emitters[6].Team()==1);
    CHECK(Team(scene)==5 && Last(scene)==5 && !Registered(scene)); // storage itself is never a SetTeam/GameObject member
    ownerTeam=0;CHECK(Frame(lease));CHECK(Weak(scene)==2 && child.emitters[6].Team()==4 && Team(child.root)==6);
    ownerTeam=2;CHECK(Frame(lease));CHECK(child.emitters[6].Team()==2 && Weak(scene)==2);
    RequireRestore(lease);CHECK(child.emitters[6].Team()==4 && Weak(scene)==1 && Weak(child.root)==1 && Weak(child.part)==1);
    CHECK(Team(scene)==5 && Last(scene)==5 && !Registered(scene));
}
void TestLiveStorageCannotRetireFromTypedGone() {
    World world;Object scene;Init(world);InitObject(scene,5,5,false);world.emitterCount=7;SceneCache(world,6,scene,6);
    Lease lease{};CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    world.emitters[6].listed=false;world.emitters[6].life=Life::gone; // erroneous adapter inference from a detached owner/container
    CHECK(!Frame(lease));CHECK(!Restore(&lease) && lease.id!=0 && Weak(scene)==2 && world.emitters[6].Team()==1);
    world.emitters[6].life=Life::live;RequireRestore(lease);CHECK(world.emitters[6].Team()==6 && Weak(scene)==1);
    Init(world);InitObject(scene,5,5,false);world.emitterCount=7;SceneCache(world,6,scene,6);
    CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    world.emitters[6].listed=false;world.emitters[6].life=Life::gone;CHECK(!Frame(lease));
    CHECK(lease.id!=0 && Weak(scene)==2);edf::Put<LONG>(scene.control,8,0);
    CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(scene.control+12))==1);
    watchedDead=scene.bytes;deadReads=0;RequireRestore(lease);
    CHECK(deadReads==0 && Weak(scene)==0 && controlDeletes==1);watchedDead=nullptr;
}
void TestReadControlRootBorrowedIdentity() {
    World world;Init(world);Lease lease{};const auto root=crew::ObjRef::Of(world.root.bytes),part=crew::ObjRef::Of(world.part.bytes);
    crew::ObjRef found{world.part.bytes,world.part.control};CHECK(!ReadControlRoot(part,&found) && !found);
    CHECK(!ReadControlRoot(root,nullptr));CHECK(Acquire(root,"owner1",Adapter(world),&lease));
    CHECK(ReadControlRoot(part,&found) && found.obj==root.obj && found.ctrl==root.ctrl);
    CHECK(!ReadControlRoot({part.obj,world.newPart.control},&found) && !found);
    ownerTeam=-1;CHECK(!Frame(lease));CHECK(ReadControlRoot(part,&found) && found.obj==root.obj && found.ctrl==root.ctrl);
    CHECK(FaultForMember(part));CHECK(ReadControlRoot(part,&found) && found.obj==root.obj && found.ctrl==root.ctrl);
    edf::Put<LONG>(world.root.control,8,0);CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(world.root.control+12))==1);
    watchedDead=world.root.bytes;deadReads=0;CHECK(ReadControlRoot(part,&found) && found.obj==root.obj && found.ctrl==root.ctrl && deadReads==0);
    RequireRestore(lease);CHECK(!ReadControlRoot(part,&found) && !found);CHECK(deadReads==0);watchedDead=nullptr;
}
void TestUnreadableStrongMembersRetainOriginals() {
    World world;Init(world);Lease lease{};CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    denyReadable=world.root.bytes;CHECK(!Restore(&lease));CHECK(lease.id!=0 && Weak(world.root)==2 && Weak(world.part)==2);
    CHECK(OwnsMember(crew::ObjRef{world.root.bytes,world.root.control}));denyReadable=nullptr;RequireRestore(lease);
    CHECK(Team(world.root)==5 && Last(world.root)==5 && Team(world.part)==5 && Weak(world.root)==1 && Weak(world.part)==1);
    Init(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    denyReadable=world.part.bytes;CHECK(!Restore(&lease));CHECK(lease.id!=0 && Weak(world.root)==2 && Weak(world.part)==2);
    CHECK(OwnsMember(crew::ObjRef{world.part.bytes,world.part.control}));denyReadable=nullptr;RequireRestore(lease);
    CHECK(Team(world.root)==5 && Team(world.part)==5 && Last(world.part)==5 && Weak(world.root)==1 && Weak(world.part)==1);
    Init(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",Adapter(world),&lease));
    edf::Put<LONG>(world.root.control,8,0);CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(world.root.control+12))==1);
    denyReadable=world.root.bytes;RequireRestore(lease);denyReadable=nullptr;
    CHECK(Weak(world.root)==0 && Weak(world.part)==1 && controlDeletes==1);
}
void TestAcceptedLateGroupsFromClosingRoot() {
    World main,first,second;Init(main);Init(first,5,5,true,false);Init(second,5,5,true,false);ownerTeam=2;
    Lease lease{};const auto parent=crew::ObjRef::Of(main.root.bytes);CHECK(Acquire(parent,"owner1",Adapter(main),&lease));
    main.root.bytes[0x18]|=4u;watchNativeWrite=main.root.bytes;watchedNativeWrites=0;
    unsigned char oldRoot[sizeof(main.root.bytes)]{};std::memcpy(oldRoot,main.root.bytes,sizeof(oldRoot));
    SpawnSet set{{&first,&second}};const SpawnProof proof{SpawnAdapter(set),3,8};std::uint64_t token=99;const auto before=nativeWrites;
    CHECK(!CanPrepareSpawns(parent,proof,2));CHECK(!PrepareSpawns(parent,proof,2,&token) && token==0);
    CHECK(CanPrepareAcceptedSpawns(parent,proof,2));CHECK(nativeWrites==before && watchedNativeWrites==0);
    ownerTeam=-1;CHECK(!PrepareAcceptedSpawns(parent,proof,1,&token) && token==0);ownerTeam=2;
    CHECK(!PrepareAcceptedSpawns(parent,proof,4096,&token) && token==0);
    auto oversized=proof;oversized.maxEmitters=512;CHECK(!PrepareAcceptedSpawns(parent,oversized,1,&token) && token==0);
    CHECK(PrepareAcceptedSpawns(parent,proof,2,&token));CHECK(!AdoptSpawn(token,crew::ObjRef::Of(main.part.bytes),crew::ObjRef::Of(first.root.bytes)));
    CHECK(Weak(first.root)==1 && Team(first.root)==5);
    CHECK(AdoptSpawn(token,parent,crew::ObjRef::Of(first.root.bytes)));
    CHECK(Team(first.root)==2 && Team(first.part)==2 && Weak(first.root)==2 && Team(second.root)==5);
    CHECK(AdoptSpawn(token,parent,crew::ObjRef::Of(second.root.bytes)));FinishSpawns(token);
    CHECK(Team(second.root)==2 && Team(second.part)==2 && Weak(second.root)==2);
    CHECK(watchedNativeWrites==0 && std::memcmp(oldRoot,main.root.bytes,sizeof(oldRoot))==0);
    Status status{};CHECK(ReadStatus(lease,&status));CHECK(!status.followsOwnerTeam && !status.restoring);
    RequireRestore(lease);CHECK(Team(first.root)==5 && Team(first.part)==5 && Team(second.root)==5 && Weak(first.root)==1 && Weak(second.root)==1);
    CHECK(watchedNativeWrites==0 && std::memcmp(oldRoot,main.root.bytes,sizeof(oldRoot))==0);watchNativeWrite=nullptr;
    Init(main);Init(first,6,4,false,false);ownerTeam=0;CHECK(Acquire(crew::ObjRef::Of(main.root.bytes),"owner1",Adapter(main),&lease));
    main.root.bytes[0x18]|=4u;watchNativeWrite=main.root.bytes;watchedNativeWrites=0;
    unsigned char modeOffBirth[sizeof(first.root.bytes)]{};std::memcpy(modeOffBirth,first.root.bytes,sizeof(modeOffBirth));
    CHECK(PrepareAcceptedSpawns(crew::ObjRef::Of(main.root.bytes),SpawnProof{Adapter(first),3,8},1,&token));
    CHECK(AdoptSpawn(token,crew::ObjRef::Of(main.root.bytes),crew::ObjRef::Of(first.root.bytes)));FinishSpawns(token);
    CHECK(std::memcmp(modeOffBirth,first.root.bytes,sizeof(modeOffBirth))==0 && Weak(first.root)==2 && watchedNativeWrites==0);
    RequireRestore(lease);CHECK(std::memcmp(modeOffBirth,first.root.bytes,sizeof(modeOffBirth))==0 && Weak(first.root)==1);watchNativeWrite=nullptr;
}
void TestAcceptedLateGroupFailureRetainsProof() {
    World main,child;Init(main);Init(child,5,5,true,false);ownerTeam=2;Lease lease{};
    CHECK(Acquire(crew::ObjRef::Of(main.root.bytes),"owner1",Adapter(main),&lease));
    const auto parent=crew::ObjRef::Of(main.root.bytes);main.root.bytes[0x18]|=4u;
    child.part.refuseTeamA=2;child.root.refuseTeamA=5;std::uint64_t token=0;
    CHECK(PrepareAcceptedSpawns(parent,SpawnProof{Adapter(child),3,8},1,&token));
    watchNativeWrite=main.root.bytes;watchedNativeWrites=0;CHECK(!AdoptSpawn(token,parent,crew::ObjRef::Of(child.root.bytes)));
    CHECK(lease.id!=0 && Weak(child.root)==2 && Weak(child.part)==2 && watchedNativeWrites==0);FinishSpawns(token);
    CHECK(!CanPrepareAcceptedSpawns(parent,SpawnProof{Adapter(child),3,8},1));CHECK(!Restore(&lease) && lease.id!=0);
    child.root.refuseTeamA=child.part.refuseTeamA=-999;RequireRestore(lease);
    CHECK(Team(child.root)==5 && Team(child.part)==5 && Weak(child.root)==1 && watchedNativeWrites==0);watchNativeWrite=nullptr;
    Init(main);Init(child,5,5,true,false);CHECK(!CanPrepareAcceptedSpawns(crew::ObjRef::Of(main.root.bytes),SpawnProof{Adapter(child),3,8},1));
    CHECK(Acquire(crew::ObjRef::Of(main.root.bytes),"owner1",Adapter(main),&lease));
    InitObject(main.newPart,1,1,true);main.newPart.baselineKnown=false;main.members[main.memberCount++]=&main.newPart;CHECK(!Frame(lease));
    CHECK(!PrepareAcceptedSpawns(crew::ObjRef::Of(main.newPart.bytes),SpawnProof{Adapter(child),3,8},1,&token) && token==0);
    main.newPart.baselineKnown=true;main.newPart.baselineTeam=main.newPart.baselineLast=5;RequireRestore(lease);
    Init(main);Init(child,5,5,true,false);CHECK(Acquire(crew::ObjRef::Of(main.root.bytes),"owner1",Adapter(main),&lease));
    main.root.bytes[0x18]|=4u;edf::Put<LONG>(main.root.control,8,0);
    CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(main.root.control+12))==1);
    CHECK(!PrepareAcceptedSpawns(crew::ObjRef{main.root.bytes,main.root.control},SpawnProof{Adapter(child),3,8},1,&token) && token==0);
    RequireRestore(lease);CHECK(Weak(child.root)==1 && Team(child.root)==5);
}
void TestDeadIdentityAndAba() {
    World world;Init(world);auto hooks=Adapter(world);Lease lease{};CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    world.emitterCount=0;world.memberCount=1; // old child/emitter containers are proven gone
    for(auto& emitter:world.emitters)emitter.life=Life::gone;
    edf::Put<LONG>(world.root.control,8,0);CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(world.root.control+12))>=1);
    watchedDead=world.root.bytes;deadReads=0;RequireRestore(lease);CHECK(deadReads==0 && controlDeletes==1);watchedDead=nullptr;
    Init(world);hooks=Adapter(world);CHECK(Acquire(crew::ObjRef::Of(world.root.bytes),"owner1",hooks,&lease));
    Object replacement{};InitObject(replacement);edf::Put<LONG>(world.root.control,8,0);
    CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(world.root.control+12))>=1);
    edf::Put<void*>(world.root.bytes,edf::kSelfCtrl,replacement.control);
    for(auto& emitter:world.emitters)emitter.life=Life::gone;
    const int replacementTeam=Team(world.root);RequireRestore(lease);CHECK(Team(world.root)==replacementTeam && Weak(replacement)==1);
}
}
namespace edf {
bool Readable(const void* pointer,std::size_t,bool) noexcept {
    if(pointer && pointer==controlled_fixture::denyReadable)return false;
    if(pointer && pointer==controlled_fixture::watchedDead)++controlled_fixture::deadReads;
    if(pointer && pointer==controlled_fixture::dieOnControlRead && ++controlled_fixture::controlReadCount==controlled_fixture::dieAtRead) {
        auto* control=const_cast<void*>(pointer);edf::Put<LONG>(control,8,0);
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(control)+12))==0)controlled_fixture::DeleteControl(control);
        if(controlled_fixture::dyingWorld)controlled_fixture::dyingWorld->collectOk=false;
    }
    return pointer!=nullptr;
}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept {return true;}
}
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
int PvpSupportTeam(const char*) noexcept {return controlled_fixture::ownerTeam;}
}
int main() {
    using namespace controlled_fixture;
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x550000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    CHECK(crew::image);if(!crew::image)return 1;
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto target=reinterpret_cast<std::uintptr_t>(&NativeSetTeam);std::memcpy(jump+2,&target,8);
    std::memcpy(crew::image+crew::kSetTeam,jump,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),crew::image+crew::kSetTeam,sizeof(jump));
    TestProbeAndTypedApply();TestClosedAndDynamicRegistration();TestDynamicBaselineAndPending();
    TestDynamicEmitterBaseline();TestInheritedPartialSyncAndDirectRestore();TestDerivedOwnerEmitters();TestPartialFailureRetainsLease();TestEmitterInvalidAndGone();TestProvenDeadEmitterOwner();TestDeadIdentityAndAba();TestSpawnReservationBounds();TestSpawnProducerInitializationCommit();TestSpawnInitializerIdentityAndReentrancy();TestSpawnGroupsFollowAndRestore();TestSpawnFinishAndPartialFailure();TestSpawnRootPinnedBeforePartialCapture();TestRecursiveNativeModesAndPartialRestore();TestPermanentDisabledAndOwnershipQueries();TestEmitterBaselineQueriesAfterNativeRetag();TestOwnerCachesAndLeasePresence();TestTemporaryTagSeparateFromLease();TestIndependentStorageLifetimeAndSealedRead();TestStoragePartialRestoreAndReplacement();TestSpawnStoragePinAndModeOff();TestLiveStorageCannotRetireFromTypedGone();TestReadControlRootBorrowedIdentity();TestUnreadableStrongMembersRetainOriginals();TestAcceptedLateGroupsFromClosingRoot();TestAcceptedLateGroupFailureRetainsProof();
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("pvp_controlled_fixture: %d checks, %d failed\n",checks,failures);return failures ? 1 : 0;
}
