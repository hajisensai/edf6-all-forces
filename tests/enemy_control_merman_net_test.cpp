// Real Merman provider/codec and real common projectile helper in disjoint peers.
#define ENEMY_CONTROL_MERMAN_NET_TEST
#define ENEMY_CONTROL_PROJECTILE_NATIVE_TEST
#include "../src/enemy_control_merman_net.cpp"
#include "../src/enemy_control_projectile_native.cpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::enemy_native;
namespace mn=crew::enemy_native::merman_net;using Result=crew::enemy_control::ActionResult;
constexpr NativeProfile profile{"EDF6_Merman",0x17B5370,0x3A4AF0,0x3A4340,0x1122,55,0xFFFF,Kind::monster,15,4};
unsigned checks=0,failures=0,creates=0,callbacks=0,registered=0,deletions=0,destructors=0;
unsigned copies=0,drops=0,faultDestructors=0,reentries=0;
bool failFactory=false,raisePost=false,role=true,closing=false,readiness=true,resourceMismatch=false;
bool raiseCopy=false,raiseDrop=false,raiseUnpin=false,protectOnCreate=false,replayOnCallback=false,withinCallback=false;
const std::vector<unsigned char>* callbackPacket=nullptr;Result reentrantResult=Result::pending;
NativeActionExtension extension{};NativeUnitFactoryProvider provider{};NativeBeamRefreshProvider beamRefresh{};NativeBeamStepProvider beamStep{};bool continuous=false,reenterStepAbort=false,reenterRetiredBind=false;unsigned stepReentries=0;void RebindDuringRetiredStep();
void Check(bool ok,const char* text,int line){++checks;if(!ok){++failures;std::fprintf(stderr,"%d: %s\n",line,text);}}
#define CHECK(x) Check((x),#x,__LINE__)
void __fastcall Destroy(void*){++destructors;if(raiseUnpin){++faultDestructors;RaiseException(0xE0427414,0,0,nullptr);}}
void __fastcall FreeControl(void*){}
void* controlVt[]={reinterpret_cast<void*>(&Destroy),reinterpret_cast<void*>(&FreeControl)};
void Identity(float* m){std::memset(m,0,64);m[0]=m[5]=m[10]=m[15]=1;}
void NextMemoryProbeTick(){const auto tick=GetTickCount64();while(GetTickCount64()==tick)Sleep(1);}
struct World;
World* current=nullptr;World* stepRebindTarget=nullptr;std::vector<World*> worlds;
struct World {
    unsigned char* data=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* root=nullptr;unsigned char* weak=nullptr;NativeLease lease{};bool host=true,retain=false;
    unsigned char* lastBullet=nullptr;float lastMatrix[16]{};std::uint32_t lastSeed=0,lastShot=0;void* lastFactory=nullptr;
    unsigned nextBullet=0;unsigned marker=0;
    explicit World(bool authority=true):host(authority) {
        if(!data)std::exit(2);root=data;weak=data+0x3000;InitControl(weak);
        Put<void*>(root,0,image+profile.vtableRva);Put<void*>(root,0x28,root);Put<void*>(root,0x30,weak);
        lease.object=root;lease.control=weak;lease.profile=&profile;
        for(unsigned i=0;i<2;++i) {
            auto* ownerRef=data+0x4000+i*0x2000;auto* owner=ownerRef+16;InitControl(ownerRef);
            Put<void*>(root,0x1C10+i*16,owner);Put<void*>(root,0x1C18+i*16,ownerRef);
            auto* batteryRef=data+0x8000+i*0x2000;auto* battery=batteryRef+16;InitControl(batteryRef);
            Put<void*>(owner,i ? 0xF0 : 0,battery);Put<void*>(owner,i ? 0xF8 : 8,batteryRef);
            Put<void*>(battery,0x2B0,root);Put<void*>(battery,0x2B8,weak);
            auto* factory=data+0xC000+i*0x100;Put<void*>(factory,0,factoryVt);Put<unsigned>(factory,8,i);
            Put<void*>(battery,0x2F0,factory);
            auto* muzzle=data+0xD000+i*0x400;Put<void*>(battery,0x278,muzzle);Put<std::size_t>(battery,0x280,2);Put<std::size_t>(battery,0x288,2);
            for(unsigned n=0;n<2;++n){Identity(reinterpret_cast<float*>(muzzle+n*0xD0+0x70));}
            auto* head=data+0xE000+i*0x100;Put<void*>(head,0,head);Put<void*>(head,8,head);Put<void*>(battery,0x290,head);
            auto* callback=data+0xF000+i*0x100;Put<void*>(callback,0,callbackVt);Put<void*>(battery,0x650,callback);
            auto* param=battery+0x300;Put<void*>(param,0,image+0x1769028);Put<std::uint16_t>(param,0xF8,0xFFFF);Put<std::uint16_t>(param,0x1C0,0xFFFF);
        }
        worlds.push_back(this);
    }
    static void InitControl(unsigned char* p){Put<void*>(p,0,controlVt);Put<LONG>(p,8,1);Put<LONG>(p,12,1);}
    unsigned char* Owner(unsigned i){return At<unsigned char*>(root,0x1C10+i*16);}
    unsigned char* Battery(unsigned i){return At<unsigned char*>(Owner(i),i ? 0xF0 : 0);}
    void Select(){current=this;}
    void Bind(){Select();Input input{};CHECK(extension.Before(lease,host,&input));}
    ~World(){
        Select();
        if(retain){CHECK(!extension.Abort(lease));CHECK(!extension.Empty(lease));CHECK(!extension.Restore(lease));}
        else {bool closed=extension.Abort(lease);if(!closed){auto* stream=mn::Find(lease);if(stream && !stream->cleanupUncertain && NativeProjectileJournalEmpty(stream->journal)){float nativeDelta=0.015625f;for(auto& b:stream->beams)if(b.held && !b.stopped)beamStep.Invoke(b.ref.object,&nativeDelta);closed=extension.Abort(lease);}}CHECK(closed);CHECK(At<LONG>(weak,8)==1);VirtualFree(data,0,MEM_RELEASE);}
        // An intentionally uncertain journal keeps native memory until process
        // exit, just as production keeps its pins and role instead of retrying.
        for(auto*& w:worlds)if(w==this)w=nullptr;
    }
    static void* factoryVt[3];static void* callbackVt[3];
};
World* OwnerOf(const void* descriptor){auto* owner=At<void*>(descriptor,0x38);for(auto* w:worlds)if(w && w->root==owner)return w;return nullptr;}
void* __fastcall Create(void*,const float* matrix,void* factory,void* descriptor) {
    ++creates;auto* w=OwnerOf(descriptor);CHECK(w && current==w);if(!w || failFactory)return nullptr;
    CHECK(At<LONG>(w->weak,8)>=2);w->lastFactory=factory;std::memcpy(w->lastMatrix,matrix,64);
    w->lastSeed=At<std::uint32_t>(descriptor,0x1A0);w->lastShot=At<std::uint32_t>(descriptor,0x1A4);
    auto* b=w->data+0x10000+(w->nextBullet++)*0x1000;auto* c=b+0xF00;World::InitControl(c);
    Put<void*>(b,0x28,b);Put<void*>(b,0x30,c);Put<unsigned>(b,0x128,UINT32_MAX);Put<unsigned>(b,0xC30,continuous ? 1u : 0u);Put<unsigned>(b,0xC34,0x2000);Put<void*>(b,0xB20,At<void*>(descriptor,0x70));Put<void*>(b,0xD30,b+0xD80);Put<void*>(b,0xAE8,At<void*>(descriptor,0x38));Put<void*>(b,0xAF0,At<void*>(descriptor,0x40));std::memcpy(b+0xD80,matrix,64);
    w->lastBullet=b;if(protectOnCreate){DWORD old=0;CHECK(VirtualProtect(w->weak,0x1000,PAGE_READONLY,&old));NextMemoryProbeTick();}return b;
}
void __fastcall Callback(void*,void* p,void*){
    ++callbacks;CHECK(current && *static_cast<void**>(p)==current->lastBullet);CHECK(At<LONG>(current->weak,8)>=2);CHECK(At<LONG>(current->lastBullet+0xF00,8)>=2);
    if(replayOnCallback && !withinCallback && callbackPacket){
        withinCallback=true;++reentries;
        reentrantResult=extension.Apply(current->lease,callbackPacket->data(),callbackPacket->size());
        withinCallback=false;
    }
    if(raisePost)RaiseException(0xE0427411,0,0,nullptr);
}
void* World::factoryVt[3]={nullptr,nullptr,reinterpret_cast<void*>(&Create)};
void* World::callbackVt[3]={nullptr,nullptr,reinterpret_cast<void*>(&Callback)};
void* __fastcall Allocate(std::size_t n){return std::calloc(1,n);}
void __fastcall ReleaseMemory(void* p,std::size_t){std::free(p);}
void __fastcall Delete(void* p){++deletions;static_cast<unsigned char*>(p)[0x18]|=4;}
void* __fastcall NetManager(void*,void* out){auto** p=static_cast<void**>(out);p[0]=image+0x200;p[1]=nullptr;return out;}
void __fastcall Register(void*,void* net,unsigned low,unsigned high){++registered;Put<unsigned>(net,8,(high<<8)|low);}
void* __fastcall Copy(void* to,const void* from){
    ++copies;std::memcpy(to,from,0x1E0);for(unsigned o:{0x38u,0x60u,0x78u,0x1D0u})if(auto* c=At<unsigned char*>(to,o+8))InterlockedIncrement(reinterpret_cast<volatile LONG*>(c+12));
    if(raiseCopy)RaiseException(0xE0427412,0,0,nullptr);return to;
}
void __fastcall Drop(void* p){
    ++drops;for(unsigned o:{0x38u,0x60u,0x78u,0x1D0u})if(auto* c=At<unsigned char*>(p,o+8)){
        InterlockedDecrement(reinterpret_cast<volatile LONG*>(c+12));if(raiseDrop)RaiseException(0xE0427413,0,0,nullptr);
    }
}
void __fastcall Move(void* to,projectile_native::Ref* source){
    auto old=At<projectile_native::Ref>(to,0);std::memcpy(to,source,16);*source={};if(old.control)InterlockedDecrement(reinterpret_cast<volatile LONG*>(old.control+12));
}
projectile_native::Ref* __fastcall Lock(void*,projectile_native::Ref* out,int){*out={};return out;}
void Descriptor(World& w,unsigned part,unsigned muzzle,unsigned char* out,std::uint32_t seed,std::uint32_t shot){
    auto* b=w.Battery(part);std::memcpy(out,b+0x300,0x1E0);Put<void*>(out,0x38,w.root);Put<void*>(out,0x40,w.weak);
    Put<void*>(out,0x70,At<unsigned char*>(b,0x278)+muzzle*0xD0+0x70);Put<unsigned>(out,0x1A0,seed);Put<unsigned>(out,0x1A4,shot);
    Put<unsigned>(out,0x94,0x42480000);Put<unsigned>(out,0x9C,0x42C60000);
}
std::vector<unsigned char> Take(World& w) {
    w.Select();std::vector<unsigned char> packet(512);std::size_t used=0;CHECK(extension.Capture(w.lease,packet.data(),packet.size(),&used));packet.resize(used);return packet;
}
Result Apply(World& w,const std::vector<unsigned char>& p){w.Select();return extension.Apply(w.lease,p.data(),p.size());}
void* FireDirect(World& w,unsigned part,unsigned muzzle,unsigned seed,unsigned shot,unsigned id) {
    w.Select();alignas(16) unsigned char descriptor[0x1E0]{};Descriptor(w,part,muzzle,descriptor,seed,shot);
    alignas(16) float matrix[16];Identity(matrix);matrix[12]=17.25f+part;matrix[13]=-11;matrix[14]=99.75f;
    auto* factory=At<void*>(w.Battery(part),0x2F0);CHECK(provider.Owns(factory,descriptor));
    void* bullet=provider.Invoke(image+0x100,matrix,factory,descriptor);
    // The real 6D2640 caller performs optional native ID registration AFTER
    // its factory returns. Sampling early would serialize UINT32_MAX instead.
    if(bullet)Put<unsigned>(bullet,0x128,id);
    return bullet;
}
unsigned pendingPart=0,pendingMuzzle=0,pendingSeed=0,pendingShot=0,pendingId=0;void* pendingBullet=nullptr;
void __fastcall NativeUpdate(void* battery,void*) {
    CHECK(current && current->Battery(pendingPart)==battery);
    pendingBullet=FireDirect(*current,pendingPart,pendingMuzzle,pendingSeed,pendingShot,pendingId);if(pendingBullet)if(auto* generator=At<void*>(battery,0x658))Put<unsigned>(generator,0,At<unsigned>(generator,0)+1);
    // Reentrant transport in a pre/post-create callback must observe no complete
    // event until the admitted original battery update has returned.
    if(pendingBullet && mn::Find(current->lease)->count==1){std::size_t used=99;unsigned char packet[512]{};CHECK(extension.Capture(current->lease,packet,sizeof(packet),&used) && !used);}
}
void* Fire(World& w,unsigned part,unsigned muzzle,unsigned seed,unsigned shot,unsigned id) {
    w.Select();pendingPart=part;pendingMuzzle=muzzle;pendingSeed=seed;pendingShot=shot;pendingId=id;pendingBullet=nullptr;
    mn::ObserveUpdate(w.Battery(part),nullptr);return pendingBullet;
}
void ActualAndDuplicates() {
    World host(true),guest(false);host.Bind();guest.Bind();for(auto* w:{&host,&guest}){auto* generator=w->data+0xE300;Put<void*>(w->Battery(1),0x658,generator);Put<unsigned>(generator,0,0xCDEEF5);Put<unsigned>(generator,4,0x12);}const auto before=creates;
    CHECK(Fire(host,1,1,0xF1234567,0xAABBCCDD,0xCDEEF512));CHECK(creates==before+1);
    CHECK(At<LONG>(host.lastBullet+0xF00,8)==2);auto packet=Take(host);CHECK(packet.size()==mn::kWireBytes);CHECK(At<LONG>(host.lastBullet+0xF00,8)==1);
    CHECK(!Fire(guest,1,1,123,7,0));CHECK(creates==before+1); // guest native state may run; factory cannot
    CHECK(Apply(guest,packet)==Result::applied);CHECK(creates==before+2);CHECK(guest.lastSeed==0xF1234567 && guest.lastShot==0xAABBCCDD);
    CHECK(guest.lastFactory==At<void*>(guest.Battery(1),0x2F0));CHECK(guest.lastMatrix[12]==18.25f && guest.lastMatrix[14]==99.75f);
    CHECK(At<unsigned>(guest.lastBullet,0x128)==0xCDEEF512);CHECK(At<unsigned>(guest.data+0xE300,0)==0xCDEEF6);CHECK(registered>0 && callbacks>0);
    auto* muzzle=At<unsigned char*>(guest.Battery(1),0x278)+0xD0;CHECK(At<void*>(muzzle,0xB0)==nullptr);
    CHECK(At<std::size_t>(guest.Battery(1),0x298)==1);CHECK(Apply(guest,packet)==Result::applied && creates==before+2);
    packet.back()^=1;CHECK(Apply(guest,packet)==Result::failed && creates==before+2); // receipt payload conflict
}
void BothOrdinalsAndClose() {
    World host(true),guest(false);host.Bind();guest.Bind();CHECK(Fire(host,0,0,1,2,UINT32_MAX));CHECK(Fire(host,1,0,3,4,0x88776655));
    CHECK(!extension.Restore(host.lease));CHECK(extension.Quiesce(host.lease));CHECK(!Fire(host,0,0,4,5,0));
    auto first=Take(host),second=Take(host);CHECK(extension.Empty(host.lease));CHECK(extension.Restore(host.lease));
    CHECK(mn::Find(host.lease) && mn::Find(host.lease)->retired);CHECK(!Fire(host,0,0,8,9,0)); // residual role, no stream: still suppressed
    CHECK(extension.Quiesce(guest.lease));CHECK(Apply(guest,first)==Result::applied);CHECK(Apply(guest,second)==Result::applied);
    CHECK(extension.Restore(guest.lease));const auto done=creates;closing=true;Input input{};CHECK(extension.Before(guest.lease,false,&input));CHECK(Apply(guest,second)==Result::applied && creates==done);closing=false;
}
void PreflightAndFailure() {
    World host(true),guest(false);host.Bind();guest.Bind();CHECK(Fire(host,0,0,99,100,7));auto packet=Take(host);const auto before=creates;
    resourceMismatch=true;CHECK(Apply(guest,packet)==Result::pending && creates==before);resourceMismatch=false;
    // Definition mismatch is rejected before the irreversible native factory.
    CHECK(extension.Abort(guest.lease));guest.Bind();auto corrupt=packet;corrupt[24]^=1; // accepted ID changes, still typed
    auto invalid=packet;invalid[8]=2;CHECK(Apply(guest,invalid)==Result::failed && creates==before);
    raisePost=true;CHECK(Apply(guest,packet)==Result::failed);raisePost=false;CHECK(creates==before+1 && deletions>0);
    CHECK(Apply(guest,packet)==Result::failed && creates==before+1);CHECK(!guest.lastBullet || (guest.lastBullet[0x18]&4));
}
void GeneratorBurstAndPending() {
    World host(true),guest(false);host.Bind();guest.Bind();
    for(auto* w:{&host,&guest}) {auto* generator=w->data+0xE300;Put<void*>(w->Battery(0),0x658,generator);Put<unsigned>(generator,0,0x1234);Put<unsigned>(generator,4,4);}
    CHECK(Fire(host,0,0,1001,1,0x123404));CHECK(Fire(host,0,1,1002,2,0x123504));
    auto one=Take(host),two=Take(host);mn::Event first{},second{};CHECK(mn::Decode(one.data(),one.size(),&first));CHECK(mn::Decode(two.data(),two.size(),&second));
    CHECK(first.nextCounter==0x1235 && second.nextCounter==0x1236);
    CHECK(Apply(guest,one)==Result::applied);CHECK(At<unsigned>(guest.data+0xE300,0)==0x1235);
    CHECK(Apply(guest,two)==Result::applied);CHECK(At<unsigned>(guest.data+0xE300,0)==0x1236);
    const auto made=creates;auto missing=second;missing.serial=3;missing.spawn=3;missing.sample.target[0]=1;unsigned char wire[512]{};std::size_t used=0;
    CHECK(mn::Encode(missing,wire,sizeof(wire),&used));guest.Select();CHECK(extension.Apply(guest.lease,wire,used)==Result::pending && creates==made);
    missing.sample.target[20]=1;CHECK(!mn::Encode(missing,wire,sizeof(wire),&used));
}
void NullCreationAndBackpressure() {
    World host(true);host.Bind();const auto before=creates;failFactory=true;CHECK(!Fire(host,0,0,1,2,UINT32_MAX));failFactory=false;
    CHECK(creates==before+1 && Take(host).empty() && mn::Find(host.lease)->next==1);
    auto* stream=mn::Find(host.lease);stream->count=mn::kQueued;CHECK(!Fire(host,0,0,3,4,UINT32_MAX));CHECK(creates==before+1 && stream->faulted);
    stream->count=0;CHECK(extension.Abort(host.lease));
}
void PostReentryIsNotASecondShot() {
    World host(true),guest(false);host.Bind();guest.Bind();CHECK(Fire(host,0,0,21,22,UINT32_MAX));auto packet=Take(host);
    const auto made=creates,called=callbacks,entered=reentries;callbackPacket=&packet;replayOnCallback=true;
    CHECK(Apply(guest,packet)==Result::applied);replayOnCallback=false;callbackPacket=nullptr;
    CHECK(creates==made+1 && callbacks==called+1 && reentries==entered+1 && reentrantResult==Result::failed);
    CHECK(!mn::Find(guest.lease)->faulted && mn::Find(guest.lease)->applied==1);
    CHECK(Apply(guest,packet)==Result::applied && creates==made+1);
}
void RecoverableJournalCleanup() {
    World host(true),guest(false);host.Bind();guest.Bind();CHECK(Fire(host,0,0,31,32,UINT32_MAX));auto packet=Take(host);
    const auto made=creates,dropped=drops,deleted=deletions;protectOnCreate=true;
    CHECK(Apply(guest,packet)==Result::failed);protectOnCreate=false;auto* stream=mn::Find(guest.lease);
    CHECK(stream && stream->faulted && stream->held && !stream->retired && !stream->journal.uncertain);
    CHECK(stream->journal.made && stream->journal.created==guest.lastBullet && stream->journal.heldMask==1);
    CHECK(creates==made+1 && drops==dropped && deletions==deleted && At<LONG>(guest.weak,8)==3);
    CHECK(!extension.Empty(guest.lease) && !extension.Restore(guest.lease));
    CHECK(Apply(guest,packet)==Result::failed && creates==made+1 && drops==dropped);
    DWORD old=0;CHECK(VirtualProtect(guest.weak,0x1000,PAGE_READWRITE,&old));NextMemoryProbeTick();
    // Core RestoreNativeActions checks Empty before invoking Restore. Empty must
    // therefore finish this safe descriptor-only cleanup once its pins are writable.
    CHECK(extension.Empty(guest.lease) && NativeProjectileJournalEmpty(stream->journal));
    CHECK(drops==dropped+1 && creates==made+1 && deletions==deleted && At<LONG>(guest.weak,8)==2);
    CHECK(Apply(guest,packet)==Result::failed && creates==made+1);CHECK(extension.Restore(guest.lease));
    CHECK(stream->retired && !stream->held && At<LONG>(guest.weak,8)==1);
}
void UncertainCopyKeepsOwnership() {
    World host(true),guest(false);host.Bind();guest.Bind();CHECK(Fire(host,0,0,41,42,UINT32_MAX));auto packet=Take(host);
    const auto made=creates,copied=copies,dropped=drops;guest.retain=true;raiseCopy=true;
    CHECK(Apply(guest,packet)==Result::failed);raiseCopy=false;auto* stream=mn::Find(guest.lease);
    CHECK(stream && stream->faulted && stream->journal.uncertain && stream->journal.copyEntered);
    CHECK(!stream->journal.made && !stream->journal.created && stream->journal.heldMask==1 && At<LONG>(guest.weak,8)==3);
    CHECK(copies==copied+1 && drops==dropped && creates==made);
    CHECK(!extension.Empty(guest.lease) && !extension.Abort(guest.lease) && !extension.Restore(guest.lease));
    CHECK(Apply(guest,packet)==Result::failed && copies==copied+1 && creates==made && drops==dropped);
    CHECK(stream->held && !stream->retired && At<LONG>(guest.weak,8)==3);
}
void UncertainCleanupDoesNotRetryOrDeleteCreated() {
    World host(true),guest(false);host.Bind();guest.Bind();CHECK(Fire(host,0,0,51,52,UINT32_MAX));auto packet=Take(host);
    const auto made=creates,dropped=drops,deleted=deletions;guest.retain=true;raiseDrop=true;
    CHECK(Apply(guest,packet)==Result::failed);raiseDrop=false;auto* stream=mn::Find(guest.lease);
    CHECK(stream && stream->journal.uncertain && stream->journal.cleanupEntered && stream->journal.made);
    CHECK(stream->journal.created==guest.lastBullet && stream->journal.heldMask==1 && At<LONG>(guest.weak,8)==3);
    CHECK(creates==made+1 && drops==dropped+1 && deletions==deleted && !(guest.lastBullet[0x18]&4));
    CHECK(!extension.Empty(guest.lease) && !extension.Abort(guest.lease) && !extension.Restore(guest.lease));
    CHECK(Apply(guest,packet)==Result::failed && creates==made+1 && drops==dropped+1 && deletions==deleted);
    CHECK(stream->held && !stream->retired && At<LONG>(guest.weak,8)==3);
}
void ReleaseSehDoesNotDecrementTwice() {
    World guest(false);guest.Bind();guest.retain=true;auto* stream=mn::Find(guest.lease);auto* control=guest.Battery(0)-16;
    Put<LONG>(control,8,1);const auto destroyed=destructors;raiseUnpin=true;
    CHECK(!extension.Abort(guest.lease));raiseUnpin=false;
    CHECK(stream->cleanupUncertain && At<LONG>(control,8)==0 && destructors==destroyed+1);
    CHECK(!extension.Empty(guest.lease) && !extension.Abort(guest.lease) && !extension.Restore(guest.lease));
    CHECK(At<LONG>(control,8)==0 && destructors==destroyed+1 && stream->held && !stream->retired && At<LONG>(guest.weak,8)==2);
}
void RootReleaseSehRetainsItsStream() {
    World guest(false);guest.Bind();guest.retain=true;auto* stream=mn::Find(guest.lease);
    Put<LONG>(guest.weak,8,1);const auto destroyed=destructors;raiseUnpin=true;
    CHECK(!extension.Abort(guest.lease));raiseUnpin=false;
    CHECK(stream->cleanupUncertain && !stream->held && !stream->retired && At<LONG>(guest.weak,8)==0 && destructors==destroyed+1);
    CHECK(mn::Find(guest.lease)==stream && !extension.Empty(guest.lease) && !extension.Abort(guest.lease) && !extension.Restore(guest.lease));
    {World next(false);next.Bind();CHECK(mn::Find(guest.lease)==stream && stream->cleanupUncertain);}
    CHECK(At<LONG>(guest.weak,8)==0 && destructors==destroyed+1);
}
void CaptureSehDoesNotDecrementTwice() {
    World host(true);host.Bind();host.retain=true;CHECK(Fire(host,0,0,61,62,UINT32_MAX));
    auto* stream=mn::Find(host.lease);auto* control=host.lastBullet+0xF00;Put<LONG>(control,8,1);
    const auto destroyed=destructors;unsigned char packet[512]{};std::size_t used=0;raiseUnpin=true;
    CHECK(!extension.Capture(host.lease,packet,sizeof(packet),&used));raiseUnpin=false;
    CHECK(stream->cleanupUncertain && At<LONG>(control,8)==0 && destructors==destroyed+1);
    CHECK(!extension.Empty(host.lease) && !extension.Abort(host.lease) && !extension.Restore(host.lease));
    used=0;CHECK(!extension.Capture(host.lease,packet,sizeof(packet),&used));
    CHECK(At<LONG>(control,8)==0 && destructors==destroyed+1 && stream->held && !stream->retired);
}

void Step(World& world,void* bullet) {world.Select();float nativeDelta=0.03125f;CHECK(beamStep.Owns(bullet));beamStep.Invoke(bullet,&nativeDelta);}
void MoveMuzzle(World& world,unsigned part,unsigned muzzle,float x) {
    auto* matrix=reinterpret_cast<float*>(At<unsigned char*>(world.Battery(part),0x278)+muzzle*0xD0+0x70);matrix[12]=x;
}
void NativeContinuousOutcomes() {
    continuous=true;
    World host(true),guest(false);host.Bind();guest.Bind();const auto made=creates;
    CHECK(Fire(host,0,0,81,82,UINT32_MAX));auto birth=Take(host);CHECK(Apply(guest,birth)==Result::applied);
    auto* source=host.lastBullet;auto* peer=guest.lastBullet;CHECK(creates==made+2);
    CHECK(At<void*>(peer,0xB20)==mn::BeamAt(*mn::Find(guest.lease),1)->persistent);CHECK(At<void*>(peer,0xB20)!=At<unsigned char*>(guest.Battery(0),0x278)+0x70);
    MoveMuzzle(host,0,0,50);host.Select();CHECK(beamRefresh.Owns(source));beamRefresh.Invoke(source);Step(host,source);
    auto refresh=Take(host);CHECK(refresh.size()==164);mn::Event actual{};CHECK(mn::Decode(refresh.data(),refresh.size(),&actual));
    CHECK(actual.kind==mn::EventKind::refresh && actual.spawn==1 && actual.sample.muzzleMatrix[12]==50 && actual.sample.matrix[12]==50);
    CHECK(Apply(guest,refresh)==Result::pending && creates==made+2);CHECK(!mn::Find(guest.lease)->waitingReady);
    Step(guest,peer);CHECK(At<float>(At<void*>(peer,0xD30),48)==50);CHECK(Apply(guest,refresh)==Result::applied);
    CHECK(Apply(guest,refresh)==Result::applied && creates==made+2);
    // A peer's local MAB/RNG can modify the shared persistent pointer. The next
    // actual consumer restores the accepted source matrix before native use.
    for(unsigned i=0;i<9;++i){MoveMuzzle(guest,0,0,9000.0f+i);guest.Select();beamRefresh.Invoke(peer);Step(guest,peer);CHECK(!(At<unsigned>(peer,0xC34)&0x4000u));CHECK(At<float>(At<void*>(peer,0xD30),48)==50);}
    // Keepalive is local at the true native step cadence, not tied to packets.
    CHECK(extension.Quiesce(guest.lease));Step(guest,peer);CHECK(!(At<unsigned>(peer,0xC34)&0x4000u));
    host.Select();beamRefresh.Invoke(source);Step(host,source);CHECK(Take(host).empty());
    MoveMuzzle(host,0,0,75);host.Select();beamRefresh.Invoke(source);Step(host,source);
    MoveMuzzle(host,0,0,80);host.Select();beamRefresh.Invoke(source);Step(host,source);
    auto latest=Take(host);CHECK(mn::Decode(latest.data(),latest.size(),&actual));CHECK(actual.serial==3 && actual.sample.matrix[12]==80);
    CHECK(Take(host).empty());CHECK(Apply(guest,latest)==Result::pending);Step(guest,peer);CHECK(Apply(guest,latest)==Result::applied);
    CHECK(extension.Quiesce(host.lease));CHECK(!extension.Empty(host.lease));Step(host,source);CHECK(At<unsigned>(source,0xC34)&0x4000u);
    auto stop=Take(host);CHECK(stop.size()==37);CHECK(Apply(guest,stop)==Result::pending);
    Step(guest,peer);CHECK(At<unsigned>(peer,0xC34)&0x4000u);CHECK(Apply(guest,stop)==Result::applied);CHECK(At<void*>(peer,0xB20)==At<void*>(peer,0xD30));
    CHECK(Apply(guest,stop)==Result::applied && creates==made+2);CHECK(extension.Empty(host.lease) && extension.Empty(guest.lease));
    CHECK(extension.Restore(host.lease));CHECK(extension.Restore(guest.lease));continuous=false;
}
void RefreshMarkIsNotConsumptionAck() {
    continuous=true;
    World host(true),guest(false);host.Bind();guest.Bind();CHECK(Fire(host,1,1,91,92,UINT32_MAX));auto birth=Take(host);CHECK(Apply(guest,birth)==Result::applied);
    MoveMuzzle(host,1,1,222);host.Select();beamRefresh.Invoke(host.lastBullet);Step(host,host.lastBullet);auto refresh=Take(host);
    CHECK(Apply(guest,refresh)==Result::pending);auto* peer=guest.lastBullet;auto* guard=guest.data+0xE500;Put<LONG>(guard,8,0);Put<void*>(peer,0xB30,guard);Put<std::uint64_t>(peer,0xB38,1);
    Step(guest,peer);CHECK(!mn::Find(guest.lease)->waitingReady);CHECK(Apply(guest,refresh)==Result::pending); // void mark succeeded, copy gate did not
    Put<std::uint64_t>(peer,0xB38,0);Step(guest,peer);CHECK(Apply(guest,refresh)==Result::applied);
    // An already expired local beam cannot be revived or falsely acknowledged.
    MoveMuzzle(host,1,1,333);host.Select();beamRefresh.Invoke(host.lastBullet);Step(host,host.lastBullet);auto next=Take(host);
    Put<unsigned>(peer,0xC34,At<unsigned>(peer,0xC34)|0x4000u);CHECK(Apply(guest,next)==Result::failed);
    CHECK(mn::Find(guest.lease)->faulted);const auto made=creates;CHECK(Apply(guest,next)==Result::failed && creates==made);continuous=false;
}
void TerminalBeamAndTrustedAbort() {
    continuous=true;
    World host(true),guest(false);host.Bind();guest.Bind();CHECK(Fire(host,0,1,101,102,UINT32_MAX));auto birth=Take(host);CHECK(Apply(guest,birth)==Result::applied);
    host.Select();Delete(host.lastBullet);Input input{};CHECK(extension.Before(host.lease,true,&input));auto terminal=Take(host);
    mn::Event e{};CHECK(mn::Decode(terminal.data(),terminal.size(),&e) && e.kind==mn::EventKind::stop && e.sample.flags==1);
    const auto erased=deletions;CHECK(Apply(guest,terminal)==Result::applied && deletions==erased+1);
    CHECK(guest.lastBullet[0x18]&4);CHECK(Apply(guest,terminal)==Result::applied && deletions==erased+1);
    CHECK(extension.Restore(host.lease));CHECK(extension.Restore(guest.lease));continuous=false;
}


void FinishedBitCannotAcknowledgeOldMatrix() {
    continuous=true;World host(true),guest(false);host.Bind();guest.Bind();CHECK(Fire(host,0,0,111,112,UINT32_MAX));
    auto birth=Take(host);CHECK(Apply(guest,birth)==Result::applied);
    host.Select();beamRefresh.Invoke(host.lastBullet);Step(host,host.lastBullet);auto refresh=Take(host);mn::Event e{};CHECK(mn::Decode(refresh.data(),refresh.size(),&e));
    CHECK(Apply(guest,refresh)==Result::pending);auto* peer=guest.lastBullet;
    std::memcpy(At<void*>(peer,0xD30),e.sample.matrix,64);Put<unsigned>(peer,0xC34,At<unsigned>(peer,0xC34)|1u);
    Step(guest,peer);CHECK(!mn::Find(guest.lease)->waitingReady && mn::Find(guest.lease)->faulted);
    CHECK(Apply(guest,refresh)==Result::failed);continuous=false;
}
void PrivateGetterAndInFlightStop() {
    continuous=true;World host(true),guest(false);host.Bind();guest.Bind();CHECK(Fire(host,0,0,121,122,UINT32_MAX));
    auto birth=Take(host);CHECK(Apply(guest,birth)==Result::applied);MoveMuzzle(host,0,0,777);host.Select();beamRefresh.Invoke(host.lastBullet);Step(host,host.lastBullet);
    auto refresh=Take(host);CHECK(Apply(guest,refresh)==Result::pending);auto* peer=guest.lastBullet;Step(guest,peer);CHECK(Apply(guest,refresh)==Result::applied);
    Put<unsigned>(peer,0xE74,1);Step(guest,peer);CHECK(At<unsigned>(peer,0xC34)&0x2000u);
    auto* privateMatrix=At<const float*>(peer,0xB20);CHECK(privateMatrix==mn::BeamAt(*mn::Find(guest.lease),1)->persistent);
    MoveMuzzle(guest,0,0,-999);CHECK(privateMatrix[12]==777); // 2319F0's bit13 getter reads this stable accepted buffer outside Step
    CHECK(extension.Quiesce(host.lease));reenterStepAbort=true;const auto before=stepReentries;Step(host,host.lastBullet);reenterStepAbort=false;
    CHECK(stepReentries==before+1 && mn::Find(host.lease)->beamDepth==0);CHECK(extension.Abort(host.lease));
    // Explicit terminal outcome rehomes before private slot release, too.
    guest.Select();CHECK(!extension.Abort(guest.lease));Step(guest,peer);CHECK(extension.Abort(guest.lease));
    CHECK(At<void*>(peer,0xB20)==At<void*>(peer,0xD30));continuous=false;
}

void RebindDuringRetiredStep() {
    auto* oldWorld=current;auto* old=mn::Find(oldWorld->lease);Input input{};
    CHECK(old && old->retired && !old->held && old->beamDepth==1);
    CHECK(!extension.Before(oldWorld->lease,oldWorld->host,&input));
    CHECK(mn::Find(oldWorld->lease)==old && old->beamDepth==1);
    stepRebindTarget->Select();CHECK(extension.Before(stepRebindTarget->lease,stepRebindTarget->host,&input));
    auto* next=mn::Find(stepRebindTarget->lease);CHECK(next && next!=old && !next->beamDepth);
    CHECK(old->retired && old->beamDepth==1);oldWorld->Select();
}
void RetiredStepCannotReuseStream() {
    continuous=true;World host(true),next(false);host.Bind();CHECK(Fire(host,0,0,131,132,UINT32_MAX));
    CHECK(!Take(host).empty());CHECK(extension.Quiesce(host.lease));Step(host,host.lastBullet);CHECK(Take(host).size()==37);
    CHECK(extension.Empty(host.lease) && extension.Restore(host.lease));auto* old=mn::Find(host.lease);
    CHECK(old && old->retired && !old->held && !old->beamDepth);
    stepRebindTarget=&next;reenterRetiredBind=true;Step(host,host.lastBullet);reenterRetiredBind=false;stepRebindTarget=nullptr;
    CHECK(mn::Find(host.lease)==old && !old->beamDepth);CHECK(mn::Find(next.lease) && !mn::Find(next.lease)->beamDepth);continuous=false;
}
void OwnershipAndPins() {
    World host(true);host.Bind();const auto uses=At<LONG>(host.weak,8);CHECK(uses==2);
    for(unsigned i=0;i<2;++i){CHECK(At<LONG>(host.Owner(i)-16,8)==2);CHECK(At<LONG>(host.Battery(i)-16,8)==2);}
    alignas(16) unsigned char descriptor[0x1E0]{};Descriptor(host,0,0,descriptor,1,2);CHECK(provider.Owns(nullptr,descriptor));
    role=false;CHECK(!provider.Owns(nullptr,descriptor));role=true;
    CHECK(extension.Quiesce(host.lease));closing=true;CHECK(!Fire(host,0,0,1,2,0));CHECK(extension.Restore(host.lease));closing=false;
    CHECK(At<LONG>(host.weak,8)==1);CHECK(provider.Owns(nullptr,descriptor)); // ownership survives extension stream release
    provider.Fault(nullptr,descriptor);CHECK(mn::Find(host.lease) && mn::Find(host.lease)->retired);
}
}
namespace crew {
bool ResolveMarkIdentities(const unsigned char[32],const unsigned char[32],MarkIdentities* out) noexcept{*out={};return true;}
bool ReadNativeObjectId(const unsigned char*,unsigned char*) noexcept{return false;}
}
namespace crew::enemy_native {
const NativeProfile* Profiles(std::size_t* count) noexcept{*count=1;return &profile;}
bool NativeActionRole(const void* root,bool* authority) noexcept{if(!role)return false;for(auto* w:worlds)if(w && w->root==root){*authority=w->host;return true;}return false;}
bool NativeActionQuiescing(const void*) noexcept{return closing;}
bool RegisterNativeActionExtension(const NativeActionExtension& e) noexcept{extension=e;return true;}
bool RegisterNativeUnitFactoryProvider(const NativeUnitFactoryProvider& p) noexcept{provider=p;return readiness;}
bool RegisterNativeUnitFactoryCallsite(std::uint32_t rva) noexcept{return rva==0x6D331D && readiness;}
bool InstallNativeUnitFactoryDispatcher() noexcept{return readiness;}
bool NativeUnitFactoryProviderReady(const NativeUnitFactoryProvider&) noexcept{return readiness;}
bool NativeUnitFactoryCallsiteReady(std::uint32_t rva) noexcept{return rva==0x6D331D && readiness;}
bool RegisterNativeBeamRefreshProvider(const NativeBeamRefreshProvider& p) noexcept{beamRefresh=p;return readiness;}
bool InstallNativeBeamRefreshDispatcher() noexcept{return readiness;}
bool NativeBeamRefreshProviderReady(const NativeBeamRefreshProvider&) noexcept{return readiness;}
bool RegisterNativeBeamStepProvider(const NativeBeamStepProvider& p) noexcept{beamStep=p;return readiness;}
bool InstallNativeBeamStepDispatcher() noexcept{return readiness;}
bool NativeBeamStepProviderReady(const NativeBeamStepProvider&) noexcept{return readiness;}
void InvokeOriginalNativeBeamRefresh(void* bullet){if(At<unsigned>(bullet,0xC30)==1 && !(At<unsigned>(bullet,0xC34)&0x4000u))Put<unsigned>(bullet,0xC34,At<unsigned>(bullet,0xC34)|0x2000u);}
void InvokeOriginalNativeBeamStep(void* bullet,void*){
 auto flags=At<unsigned>(bullet,0xC34);
 if(!(flags&0x2000u))flags|=0x4000u;
 if(!(flags&0x4001u) && (flags&0x2000u) && mn::ConsumerGate(bullet))std::memcpy(At<void*>(bullet,0xD30),At<void*>(bullet,0xB20),64);
 if(!(At<unsigned>(bullet,0xE74)&1u))flags&=~0x2000u;
 Put<unsigned>(bullet,0xC34,flags);if(reenterRetiredBind)RebindDuringRetiredStep();if(reenterStepAbort && (flags&0x4000u)){++stepReentries;auto* stream=mn::Find(current->lease);CHECK(stream && stream->beamDepth>0);const auto held=At<LONG>(static_cast<unsigned char*>(bullet)+0xF00,8);CHECK(!extension.Abort(current->lease));CHECK(!extension.Empty(current->lease));CHECK(!extension.Restore(current->lease));CHECK(At<LONG>(static_cast<unsigned char*>(bullet)+0xF00,8)==held);}
}
void* InvokeOriginalNativeUnitFactory(void* manager,const float* matrix,void* factory,void* descriptor){return Create(manager,matrix,factory,descriptor);}
bool MermanNetTestDefinition(const void*,unsigned part,std::uint64_t* out) noexcept{*out=0xAABB0011+part+(resourceMismatch && current && !current->host ? 100u : 0u);return true;}
void* MermanNetTestFunction(unsigned rva) noexcept{
    switch(rva){case 0x6D51C0:return reinterpret_cast<void*>(&NativeUpdate);case 0x12D85B0:return reinterpret_cast<void*>(&Allocate);case 0x12D85EC:return reinterpret_cast<void*>(&ReleaseMemory);
    case 0x118A1B0:return reinterpret_cast<void*>(&Delete);case 0x784510:return reinterpret_cast<void*>(&NetManager);case 0x7639B0:return reinterpret_cast<void*>(&Register);default:std::abort();}
}
void* ProjectileNativeTestFunction(unsigned rva) noexcept{
    switch(rva){case 0x32EF10:return reinterpret_cast<void*>(&Copy);case 0x100C90:return reinterpret_cast<void*>(&Drop);
    case 0x58EB40:return reinterpret_cast<void*>(&Move);case 0x548EF0:return reinterpret_cast<void*>(&Lock);case 0x1194280:return reinterpret_cast<void*>(&Create);default:std::abort();}
}
}

namespace {
struct TraceScenario {const char* name;const char* asset;unsigned part,count,interval;bool beam,refused;};
unsigned WriteTrace(World& world,const TraceScenario& scenario,unsigned tick,FILE* out) {
    world.Select();unsigned packets=0;
    for(;;) {
        unsigned char wire[512]{};std::size_t size=0;CHECK(extension.Capture(world.lease,wire,sizeof(wire),&size));if(!size)break;
        mn::Event e{};CHECK(mn::Decode(wire,size,&e));++packets;
        const char* kind=e.kind==mn::EventKind::birth ? "birth" : e.kind==mn::EventKind::refresh ? "active" : "stop";
        std::fprintf(out,"{\"record\":\"packet\",\"scenario\":\"%s\",\"provider\":\"EMR1\",\"nativeUpdateTick\":%u,\"actualUnits\":%u,\"kind\":\"%s\",\"serial\":%llu,\"spawnSerial\":%llu,\"partOrdinal\":%u,\"muzzleOrdinal\":%u,\"seed\":%u,\"shot\":%u,\"encodedBytes\":%zu,\"hex\":\"",
            scenario.name,tick,e.kind==mn::EventKind::birth ? 1u : 0u,kind,static_cast<unsigned long long>(e.serial),static_cast<unsigned long long>(e.spawn),e.part,e.muzzle,e.sample.random,e.sample.shot,size);
        for(std::size_t n=0;n<size;++n)std::fprintf(out,"%02x",static_cast<unsigned>(wire[n]));std::fprintf(out,"\"}\n");
    }
    return packets;
}
int DumpTrace(const char* path) {
    FILE* out=nullptr;if(fopen_s(&out,path,"wb") || !out)return 2;
    const TraceScenario scenarios[]={
        {"merman_gun","E603_MERMAN.SGO",0,30,1,false,false},
        {"merman_mist","E603_MERMAN.SGO",1,15,15,false,false},
        {"merman_light_gun","E603_MERMAN_LIGHT.SGO",0,30,1,false,false},
        {"merman_light_mist","E603_MERMAN_LIGHT.SGO",1,5,45,false,false},
        {"merman_m063_gun","E603_MERMAN_M063.SGO",0,30,1,false,false},
        {"merman_m063_mist","E603_MERMAN_M063.SGO",1,15,15,false,false},
        {"merman_refused_first_factory","E603_MERMAN.SGO",0,30,1,false,true},
        {"merman_continuous_contract","fixture_EfsExposureBullet_contract",0,30,1,true,false}
    };
    unsigned totalPackets=0,totalUnits=0;
    for(const auto& scenario:scenarios) {
        continuous=scenario.beam;World host(true);host.Select();
        for(unsigned i=0;i<2;++i){Put<std::size_t>(host.Battery(i),0x280,1);Put<std::size_t>(host.Battery(i),0x288,1);}
        host.Bind();const auto beforeCreates=creates;unsigned attempts=0,successful=0,packets=0,cycle=0;
        const unsigned last=1+(scenario.count-1)*(scenario.interval+1);
        std::fprintf(out,"{\"record\":\"scenario\",\"scenario\":\"%s\",\"asset\":\"%s\",\"source\":\"Merman.root.slot5-to-3AA2D0/3AA430-to-6D51C0-to-6D2640-to-6D331D\",\"muzzleCount\":1,\"partOrdinal\":%u,\"warmupUpdates\":0,\"totalEmitOpportunities\":%u,\"intervalUpdates\":%u,\"scattered\":1,\"birthBytes\":%u,\"activeBytes\":164,\"stopBytes\":37,\"clock\":\"native battery UpdateContext count after the accepted fire gate; no Hz assumption\",\"allocation\":\"native factory ABI fixture seam; production provider/Capture/Encode\",\"schedule\":\"fixture simulation from audited resource parameters; native battery cadence machine code is not executed\",\"scope\":\"poison warning VM precedes this fire gate and is not guessed as a fixed delay\"}\n",
            scenario.name,scenario.asset,scenario.part,scenario.count,scenario.interval,mn::kWireBytes);
        for(unsigned tick=1;tick<=last;++tick) {
            host.Select();const auto beforeSuccess=successful;unsigned frameAttempts=0;
            if((tick-1)%(scenario.interval+1)==0) {
                ++attempts;++frameAttempts;
                if(!scenario.beam || !cycle) {
                    failFactory=scenario.refused && !cycle;
                    void* result=Fire(host,scenario.part,0,0xF0001000u+cycle,cycle,UINT32_MAX);if(result)++successful;
                    if(scenario.beam && result)Put<unsigned>(result,0xE74,1);failFactory=false;
                } else beamRefresh.Invoke(host.lastBullet);
                ++cycle;
            }
            if(scenario.beam) {MoveMuzzle(host,scenario.part,0,20.0f+static_cast<float>(tick)*0.25f);Step(host,host.lastBullet);}
            const auto emitted=WriteTrace(host,scenario,tick,out);packets+=emitted;
            std::fprintf(out,"{\"record\":\"frame\",\"scenario\":\"%s\",\"nativeUpdateTick\":%u,\"factoryAttempts\":%u,\"actualUnits\":%u,\"packets\":%u}\n",
                scenario.name,tick,scenario.beam && cycle>1 ? 0u : frameAttempts,successful-beforeSuccess,emitted);
        }
        if(scenario.beam) {
            CHECK(extension.Quiesce(host.lease));Step(host,host.lastBullet);packets+=WriteTrace(host,scenario,last+1,out);
        }
        CHECK(extension.Empty(host.lease));CHECK(extension.Quiesce(host.lease) && extension.Restore(host.lease));
        totalPackets+=packets;totalUnits+=successful;
        std::fprintf(out,"{\"record\":\"end\",\"scenario\":\"%s\",\"nativeUpdates\":%u,\"emitOpportunities\":%u,\"actualSuccessfulUnits\":%u,\"actualFactoryCalls\":%u,\"falseFactoryOutcomes\":%u,\"packets\":%u}\n",
            scenario.name,last+(scenario.beam ? 1u : 0u),attempts,successful,creates-beforeCreates,scenario.refused ? 1u : 0u,packets);
    }
    continuous=false;std::fclose(out);std::printf("Merman production codec trace: %u actual Unit births, %u typed packets written to %s\n",totalUnits,totalPackets,path);return failures ? 1 : 0;
}
} // namespace

int main(int argc,char** argv){
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!image)return 2;
    for(const auto& sig:crew::enemy_native::projectile_native::prints)std::memcpy(image+sig.rva,sig.bytes,16);
    Put<void*>(image,0x20B2958,image+0x100);Put<void*>(image,0x20B2AC8,image+0x200);Put<void*>(image,0x208,image+0x600);
    Put<void*>(image,profile.vtableRva-8,image+0x400);
    readiness=false;CHECK(!InstallMermanAcceptedProjectiles());readiness=true;CHECK(InstallMermanAcceptedProjectiles());if(argc==3 && !std::strcmp(argv[1],"--dump-trace"))return DumpTrace(argv[2]);
    ActualAndDuplicates();BothOrdinalsAndClose();PreflightAndFailure();GeneratorBurstAndPending();NullCreationAndBackpressure();OwnershipAndPins();NativeContinuousOutcomes();RefreshMarkIsNotConsumptionAck();TerminalBeamAndTrustedAbort();FinishedBitCannotAcknowledgeOldMatrix();PrivateGetterAndInFlightStop();RetiredStepCannotReuseStream();
    CHECK(destructors==0);PostReentryIsNotASecondShot();RecoverableJournalCleanup();UncertainCopyKeepsOwnership();
    UncertainCleanupDoesNotRetryOrDeleteCreated();ReleaseSehDoesNotDecrementTwice();RootReleaseSehRetainsItsStream();CaptureSehDoesNotDecrementTwice();
    CHECK(destructors==faultDestructors && faultDestructors==3);VirtualFree(image,0,MEM_RELEASE);
    std::printf("Merman actual factory: %u checks, %u failures\n",checks,failures);return failures ? 1 : 0;
}
