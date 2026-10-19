// Execute the actual provider against native-layout fixtures, including Core clones.
#include "../src/enemy_control_pvp_native.cpp"
#include "../src/pvp_controlled.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {unsigned char* image=nullptr;}
namespace native_pvp_fixture {
using namespace crew;using namespace crew::native_pvp_detail;
struct Region {const void* pointer;std::size_t size;};
Region regions[128]{};unsigned regionCount=0;
const unsigned char* denied=nullptr;std::size_t deniedSize=0;
const unsigned char* watched=nullptr;std::size_t watchedSize=0;unsigned watchedReads=0;
int checks=0,failures=0,integrationOwnerTeam=1;unsigned nativeWrites=0,controlDeletes=0;
void __fastcall DeleteControl(void*) noexcept {++controlDeletes;}
void* controlVtable[2]={nullptr,reinterpret_cast<void*>(&DeleteControl)};
bool partsReady=true,partsInstallAllowed=true;unsigned partsInstalls=0;
bool bossReady=false,transportReady=false,berserkerReady=false,selectorOwned=true,familyExtra=false;unsigned familyCollects=0,familyReads=0,familyWrites=0,familyInstalls=0,familyResets=0;
void Check(bool ok,const char* why,int line) {++checks;if(!ok){++failures;std::fprintf(stderr,"%d: %s\n",line,why);}}
#define CHECK(x) native_pvp_fixture::Check(!!(x),#x,__LINE__)
void RegionOf(const void* p,std::size_t size) {regions[regionCount++]={p,size};}
bool Overlap(const void* p,std::size_t n,const void* q,std::size_t m) {
    if(!p || !q || !n || !m)return false;
    const auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(q);
    return a<=b ? b-a<n : a-b<m;
}
struct Object {
    alignas(16) unsigned char bytes[0x4000]{};
    alignas(16) unsigned char control[0x20]{};
    unsigned char head[0x18]{};
    unsigned char clone[87*8]{};
};
Object objects[5]{};
alignas(16) unsigned char batteries[3*0x1B0]{},replacement[3*0x1B0]{};
alignas(16) unsigned char makers[4][0x40]{},makerControls[4][0x20]{};
unsigned char nodes[2][0x18]{},gunHead[16]{},gunNodes[2][0x48]{};
unsigned char legRows[0x40]{},legController[0x28]{},partEntries[0x10]{},partNode[0x28]{},sceneEntries[16]{},footContactControl[16]{};
const NativeProfile& Find(std::uint32_t vtable) {for(const auto& p:kProfiles)if(p.vtableRva==vtable)return p;std::exit(3);}
ObjRef Ref(unsigned i=0) {return {objects[i].bytes,objects[i].control};}
void OriginTables() {
    for(std::size_t i=0;i<sizeof(kProfiles)/sizeof(kProfiles[0]);++i) {
        const auto& p=kProfiles[i];auto* col=image+0x2050000+i*0x100;auto* td=col+0x30;
        Put<const void*>(image,p.vtableRva-8,col);Put<std::uint32_t>(col,0,1);
        Put<std::uint32_t>(col,12,static_cast<std::uint32_t>(td-image));
        Put<std::uint32_t>(col,20,static_cast<std::uint32_t>(col-image));
        std::memcpy(td+16,".?AV",4);std::memcpy(td+20,p.name,std::strlen(p.name));
        std::memcpy(td+20+std::strlen(p.name),"@@",3);
        for(unsigned s=0;s<p.slots;++s)Put<const void*>(image,p.vtableRva+s*8,image+0x10000+s*0x100);
        Put<const void*>(image,p.vtableRva+p.inputSlot*8,image+p.inputRva);
        Put<const void*>(image,p.vtableRva+7*8,image+p.aiRva);
    }
}
void FootOrigin() {
    for(std::size_t n=0;n<sizeof(kFeet)/sizeof(kFeet[0]);++n) {
        const auto& type=kFeet[n];auto* col=image+0x2057000+n*0x100;auto* td=col+0x30;Put<void*>(image,type.vtable-8,col);
        Put<std::uint32_t>(col,0,1);Put<std::uint32_t>(col,12,static_cast<std::uint32_t>(td-image));Put<std::uint32_t>(col,20,static_cast<std::uint32_t>(col-image));
        std::memcpy(td+16,".?AV",4);std::memcpy(td+20,type.name,std::strlen(type.name));std::memcpy(td+20+std::strlen(type.name),"@@",3);
        for(unsigned i=0;i<48;++i)Put<void*>(image,type.vtable+i*8,image+0x30000+i*0x100);
        Put<void*>(image,type.vtable+5*8,image+type.input);Put<void*>(image,type.vtable+7*8,image+0x54A000);
    }
    Put<void*>(image,0x17E6EE0+8,image+0x6CF7D0);Put<void*>(image,0x17E6EE0+0x10,image+0x6CFEF0);
    Put<void*>(image,0x17E6F08+8,image+0x6CF910);Put<void*>(image,0x17E6F08+0x10,image+0x6CFF00);
}

void BulletOrigin() {
    auto* col=image+0x2058000;auto* hierarchy=col+0x30;auto* array=col+0x50;auto* base=col+0x70;
    Put<void*>(image,0x179E048-8,col);Put<std::uint32_t>(col,0,1);Put<std::uint32_t>(col,12,0x2022E80);Put<std::uint32_t>(col,16,static_cast<std::uint32_t>(hierarchy-image));Put<std::uint32_t>(col,20,static_cast<std::uint32_t>(col-image));
    Put<std::uint32_t>(hierarchy,8,1);Put<std::uint32_t>(hierarchy,12,static_cast<std::uint32_t>(array-image));Put<std::uint32_t>(array,0,static_cast<std::uint32_t>(base-image));
    Put<std::uint32_t>(base,0,0x2022E80);Put<std::int32_t>(base,12,-1);
}
void SetClass(unsigned i,std::uint32_t vtable,bool clone=false) {
    const auto& p=Find(vtable);
    if(clone) {
        std::memcpy(objects[i].clone,image+p.vtableRva-8,(static_cast<std::size_t>(p.slots)+1)*8);
        for(unsigned s:{4u,5u,7u,14u})Put<const void*>(objects[i].clone,(s+1)*8,image+0x20000+s);
        Put<const void*>(objects[i].bytes,0,objects[i].clone+8);
    } else Put<const void*>(objects[i].bytes,0,image+vtable);
}
void Reset() {
    partsReady=true;partsInstallAllowed=true;partsInstalls=0;
    regionCount=0;denied=watched=nullptr;deniedSize=watchedSize=watchedReads=0;bossReady=transportReady=berserkerReady=false;selectorOwned=true;familyExtra=false;familyCollects=familyReads=familyWrites=familyInstalls=familyResets=0;
    RegionOf(image,0x2100000);RegionOf(kProfiles,sizeof(kProfiles));RegionOf(controlVtable,sizeof(controlVtable));controlDeletes=0;
    for(const auto& p:kProfiles)RegionOf(p.name,std::strlen(p.name)+1);
    for(const auto& a:kAnchors)std::memcpy(image+a.rva,a.bytes,a.size);
    for(auto& o:objects) {
        o=Object{};RegionOf(&o,sizeof(o));Put<void*>(o.bytes,kSelf,o.bytes);Put<void*>(o.bytes,kSelfCtrl,o.control);
        Put<void*>(o.control,0,controlVtable);Put<LONG>(o.control,8,1);Put<LONG>(o.control,12,1);Put<int>(o.bytes,kTeam,5);Put<int>(o.bytes,0x318,5);
        Put<void*>(o.bytes,0x550,o.head);Put<void*>(o.head,0,o.head);Put<void*>(o.head,8,o.head);
    }
    std::memset(batteries,0,sizeof(batteries));std::memset(replacement,0,sizeof(replacement));
    RegionOf(batteries,sizeof(batteries));RegionOf(replacement,sizeof(replacement));
    RegionOf(makers,sizeof(makers));RegionOf(makerControls,sizeof(makerControls));RegionOf(nodes,sizeof(nodes));RegionOf(gunHead,sizeof(gunHead));RegionOf(gunNodes,sizeof(gunNodes));
    std::memset(gunHead,0,sizeof(gunHead));std::memset(gunNodes,0,sizeof(gunNodes));
    RegionOf(legRows,sizeof(legRows));RegionOf(legController,sizeof(legController));RegionOf(partEntries,sizeof(partEntries));RegionOf(partNode,sizeof(partNode));RegionOf(sceneEntries,sizeof(sceneEntries));RegionOf(footContactControl,sizeof(footContactControl));
    std::memset(legRows,0,sizeof(legRows));std::memset(legController,0,sizeof(legController));std::memset(partEntries,0,sizeof(partEntries));std::memset(partNode,0,sizeof(partNode));std::memset(sceneEntries,0,sizeof(sceneEntries));std::memset(footContactControl,0,sizeof(footContactControl));
    for(auto& c:makerControls){std::memset(c,0,sizeof(c));Put<LONG>(c,8,1);}
    for(auto& maker:makers)Put<void*>(maker,0,image+0x17E6EE0);
}
NativeLease Lease(std::uint32_t vtable,bool clone=false) {
    SetClass(0,vtable,clone);NativeLease lease{};lease.object=objects[0].bytes;lease.control=objects[0].control;lease.profile=&Find(vtable);return lease;
}
void Ufo507(unsigned i=0) {
    SetClass(i,0x17CC2A8);Put<void*>(objects[i].bytes,0x6F0,makers[0]);
    Put<void*>(objects[i].bytes,0x738,objects[i].bytes);Put<void*>(objects[i].bytes,0x740,objects[i].control);
}
void Battery(unsigned char* data,std::size_t i,unsigned owner=0) {
    auto* battery=data+i*0x1B0+0x20;
    Put<void*>(battery,0x10,objects[owner].bytes);Put<void*>(battery,0x18,objects[owner].control);
    Put<void*>(battery,0x20,makers[i]);Put<void*>(battery,0x28,makerControls[i]);
}
void Ufo515(std::size_t count=2,unsigned char* data=batteries) {
    SetClass(0,0x17CC838);Put<void*>(objects[0].bytes,0x1298,data);Put<std::size_t>(objects[0].bytes,0x12A8,count);
    for(std::size_t i=0;i<count && i<3;++i)Battery(data,i);
}
bool Collect(Emitter* emitters,unsigned* emitted,unsigned capacity=8) {
    ObjRef members[8]{};unsigned count=99;
    const bool ok=EnemyControlNativePvpCollect(Ref(),members,8,&count,emitters,capacity,emitted);
    CHECK(ok ? count>0 : count==0);return ok;
}
void RejectCollect() {Emitter e[8]{};unsigned emitted=99;CHECK(!Collect(e,&emitted));CHECK(emitted==0);}
void TestUfo507() {
    Reset();Ufo507();auto lease=Lease(0x17CC2A8);controlled_pvp::Hooks hooks{};
    CHECK(EnemyControlNativePvpHooks(lease,&hooks));CHECK(hooks.user==&lease && hooks.collect && hooks.readEmitter && hooks.writeEmitter && hooks.memberBaseline && hooks.emitterBaseline);
    Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted));CHECK(emitted==1 && e[0].object==objects[0].bytes+0x700 && e[0].kind==EmitterKind::liveOwnerTeam);
    unsigned char saved[sizeof(objects[0].bytes)]{};std::memcpy(saved,objects[0].bytes,sizeof(saved));
    int team=99;CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==5);
    CHECK(EnemyControlNativePvpWrite(e[0],5));CHECK(!EnemyControlNativePvpWrite(e[0],2));CHECK(!EnemyControlNativePvpWrite(e[0],7));
    CHECK(std::memcmp(saved,objects[0].bytes,sizeof(saved))==0);
    Put<int>(objects[0].bytes,kTeam,-1);CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==-1);CHECK(EnemyControlNativePvpWrite(e[0],-1));
    Put<int>(objects[0].bytes,kTeam,2);CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==2);
    CHECK(EnemyControlNativePvpBaseline(e[0],5,4,&team) && team==5);
    int last=99;CHECK(EnemyControlNativePvpBaseline(Ref(),Ref(),3,4,&team,&last) && team==3 && last==4);
    CHECK(!EnemyControlNativePvpBaseline(Ref(),Ref(1),3,4,&team,&last));
    Put<void*>(objects[0].bytes,0x738,objects[1].bytes);RejectCollect();CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::invalid);
    Put<void*>(objects[0].bytes,0x738,objects[0].bytes);Put<void*>(objects[0].bytes,0x740,objects[1].control);RejectCollect();
    Put<void*>(objects[0].bytes,0x740,objects[0].control);Put<void*>(objects[0].bytes,0x6F0,nullptr);RejectCollect();
    CHECK(!EnemyControlNativePvpHooks(lease,nullptr));
}
void TestAlienBarrier() {
    Reset();auto lease=Lease(0x17A9650);auto* root=objects[0].bytes;
    Put<void*>(root,0x10C0,makers[0]);Put<void*>(root,0x10D0,root);Put<void*>(root,0x10D8,makers[0]);
    controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(lease,&hooks));
    Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted));CHECK(emitted==1 && e[0].object==root+0x10D0);
    int team=99;CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==5);
    Put<int>(root,kTeam,2);CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==2);
    unsigned char saved[sizeof(objects[0].bytes)]{};std::memcpy(saved,root,sizeof(saved));
    CHECK(EnemyControlNativePvpWrite(e[0],2));CHECK(!EnemyControlNativePvpWrite(e[0],1));
    CHECK(std::memcmp(saved,root,sizeof(saved))==0);
    Put<void*>(root,0x10D0,objects[1].bytes);RejectCollect();CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::invalid);
    Put<void*>(root,0x10D0,root);Put<void*>(root,0x10D8,objects[0].control);RejectCollect();
    Put<void*>(root,0x10D8,makers[0]);Put<void*>(root,kSelf,objects[1].bytes);RejectCollect();
}
void TestCloneAndContext() {
    Reset();Ufo507();auto lease=Lease(0x17CC2A8,true);controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(lease,&hooks));
    ObjRef m[8]{};Emitter e[8]{};unsigned count=99,emitted=99;
    CHECK(hooks.collect(hooks.user,Ref(),m,8,&count,e,8,&emitted) && count==1 && emitted==1);
    CHECK(!hooks.collect(hooks.user,Ref(1),m,8,&count,e,8,&emitted) && count==0 && emitted==0);
    Put<void*>(objects[0].clone,(2+1)*8,image+0xBAD);CHECK(!EnemyControlNativePvpHooks(lease,&hooks));CHECK(hooks.user==nullptr);
    SetClass(0,0x17CC2A8,true);Put<void*>(objects[0].clone,15*8,image+0xBAD);CHECK(!EnemyControlNativePvpHooks(lease,&hooks));
    SetClass(0,0x17CC2A8,true);selectorOwned=false;CHECK(!EnemyControlNativePvpHooks(lease,&hooks));selectorOwned=true;
    SetClass(0,0x17CC2A8,true);Put<void*>(objects[0].clone,0,image+0xBAD);CHECK(!EnemyControlNativePvpHooks(lease,&hooks));
    SetClass(0,0x17CC2A8);lease.control=objects[1].control;CHECK(!EnemyControlNativePvpHooks(lease,&hooks));
    lease.control=objects[0].control;lease.profile=&Find(0x17CC838);CHECK(!EnemyControlNativePvpHooks(lease,&hooks));
    lease.profile=&Find(0x17CC2A8);image[0x52C6E5]^=1;CHECK(!EnemyControlNativePvpHooks(lease,&hooks));
}
void TestUfo515AndDetached() {
    Reset();Ufo515();Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted));CHECK(emitted==2);
    CHECK(e[0].object==batteries+0x20 && e[1].object==batteries+0x1D0);int team=99;
    CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==5);
    unsigned char saved[sizeof(batteries)]{};std::memcpy(saved,batteries,sizeof(saved));
    CHECK(EnemyControlNativePvpWrite(e[0],5));CHECK(std::memcmp(saved,batteries,sizeof(saved))==0);
    // Replaced container proves removal without querying an obsolete emitter page.
    Battery(replacement,0);Put<void*>(objects[0].bytes,0x1298,replacement);Put<std::size_t>(objects[0].bytes,0x12A8,1);
    watched=batteries;watchedSize=sizeof(batteries);denied=watched;deniedSize=watchedSize;
    CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::gone);CHECK(watchedReads==0);
    Put<void*>(replacement+0x20,0x10,objects[1].bytes);CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::invalid);CHECK(watchedReads==0);
    Put<void*>(replacement+0x20,0x10,objects[0].bytes);Put<std::size_t>(objects[0].bytes,0x12A8,0);
    watched=replacement;watchedSize=sizeof(replacement);denied=watched;deniedSize=watchedSize;watchedReads=0;
    CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::gone && watchedReads==0);
    Reset();Ufo515();CHECK(Collect(e,&emitted));Put<void*>(batteries+0x20,0x28,makerControls[3]);
    CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::gone);
    Put<void*>(batteries+0x20,0x28,makerControls[0]);Put<LONG>(makerControls[0],8,0);
    CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::invalid);
}
void TestHumanoidWeapons() {
    for(auto vt:{0x17C4D70u,0x17C0BD0u,0x17C24B0u}) {
        Reset();auto lease=Lease(vt,true);auto* root=objects[0].bytes;auto* weapon=objects[1].bytes;
        Put<void*>(root,0x1120,batteries);Put<std::size_t>(root,0x1130,1);
        Put<void*>(batteries,0,weapon);Put<void*>(batteries,8,objects[1].control);Put<void*>(weapon,0x5B0,root);
        Put<float>(weapon,0x214,3.25f);Put<int>(root,kTeam,-1);Put<int>(root,0x318,2);
        controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(lease,&hooks));
        Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted));CHECK(emitted==1 && e[0].kind==EmitterKind::liveOwnerLastTeam && e[0].object==weapon);
        int team=99;CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==2);
        CHECK(EnemyControlNativePvpWrite(e[0],2));CHECK(At<float>(weapon,0x214)==3.25f);
        CHECK(EnemyControlNativePvpBaseline(e[0],-1,5,&team) && team==5);
        Put<void*>(weapon,0x5B0,objects[2].bytes);CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::invalid);RejectCollect();
        Put<void*>(weapon,0x5B0,root);Put<LONG>(objects[1].control,8,0);watched=weapon;watchedSize=sizeof(objects[1].bytes);
        CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::gone);CHECK(watchedReads==0);
        CHECK(Collect(e,&emitted) && emitted==0);CHECK(watchedReads==0);
    }
}
void TestMonsterAndDeiroi() {
    for(auto vt:{0x17C5628u,0x17C5A60u}) {
        Reset();auto lease=Lease(vt);auto* root=objects[0].bytes;const std::size_t init=vt==0x17C5628 ? 0x1B30u : 0x1A70u;
        Put<void*>(root,init-0x10,makers[0]);Put<void*>(root,init+0x38,root);Put<void*>(root,init+0x40,objects[0].control);
        controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(lease,&hooks));
        Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted) && emitted==1 && e[0].object==root+init);
        int team=99;CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==5);
        Put<void*>(root,init+0x40,objects[1].control);RejectCollect();
    }
    Reset();auto lease=Lease(0x17AA648);auto* root=objects[0].bytes;
    // One actual stride-290 record fits in the deliberately separate array.
    Put<void*>(root,0x1638,batteries);Put<std::size_t>(root,0x1648,1);auto* battery=batteries+0x100;
    Put<void*>(battery,0x10,root);Put<void*>(battery,0x18,objects[0].control);Put<void*>(battery,0x20,makers[0]);Put<void*>(battery,0x28,makerControls[0]);
    controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(lease,&hooks));
    Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted) && emitted==1 && e[0].object==battery);
    int team=99;CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==5);
    Put<void*>(battery,0x10,objects[1].bytes);RejectCollect();
}
void OwnedTemplate(std::size_t init) {
    Put<void*>(objects[0].bytes,init+0x38,objects[0].bytes);Put<void*>(objects[0].bytes,init+0x40,objects[0].control);
    Put<float>(objects[0].bytes,init-0x10,3.25f); // actual native fields here can be numeric
}
void CommonContact() {Put<void*>(objects[0].bytes,0xF40,objects[0].bytes);Put<void*>(objects[0].bytes,0xF48,objects[0].control);}
void Contact(unsigned char* component) {
    Put<void*>(component,0,image+0x17E8028);Put<void*>(component,8,objects[0].bytes);Put<void*>(component,0x40,objects[0].bytes);Put<void*>(component,0x48,objects[0].control);
}
void ContactPointer(std::size_t at) {
    auto* component=batteries+0x400;Contact(component);Put<void*>(objects[0].bytes,at,component);Put<void*>(objects[0].bytes,at+8,makerControls[3]);
}
void BigUnit(unsigned which=1) {
    auto* unit=objects[which].bytes;Put<void*>(unit,0x2B0,objects[0].bytes);Put<void*>(unit,0x2B8,objects[0].control);
    Put<void*>(unit,0x278,replacement);Put<std::size_t>(unit,0x280,1);Put<std::size_t>(unit,0x288,1);
    Put<float>(unit,0x10,3.25f);Put<float>(replacement,0x10,7.25f);
}
void SpiderMap(bool populated=true) {
    Put<void*>(objects[0].bytes,0x23D8,gunHead);Put<std::size_t>(objects[0].bytes,0x23E0,populated ? 1 : 0);
    Put<void*>(gunHead,0,populated ? gunNodes[0] : gunHead);Put<void*>(gunHead,8,populated ? gunNodes[0] : gunHead);
    if(populated) {
        BigUnit();Put<void*>(gunNodes[0],0,gunHead);Put<void*>(gunNodes[0],8,gunHead);
        Put<void*>(gunNodes[0],0x38,objects[1].bytes);Put<void*>(gunNodes[0],0x40,makerControls[1]);
    }
}
void TestInsects() {
    struct Case {std::uint32_t vt;std::size_t init,extra;unsigned expected;};
    const Case cases[]={{0x17ABA40,0x15B0,0,2},{0x17BB798,0x1680,0,2},{0x17BC648,0x1680,0x2140,3},
        {0x17BCEE0,0x16D0,0,2},{0x17BD638,0x16D0,0x2240,3},{0x17BE120,0x1AA0,0,2},{0x17BE7E0,0x1AA0,0x23C0,4},
        {0x17BDAC0,0,0,2},{0x17C84C8,0x10D0,0,1}};
    for(const auto& c:cases) {
        Reset();auto lease=Lease(c.vt,true);if(c.init)OwnedTemplate(c.init);if(c.vt!=0x17C84C8)CommonContact();
        if(c.extra)ContactPointer(c.extra);if(c.vt==0x17BE7E0)SpiderMap();
        controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(lease,&hooks));CHECK(EnemyControlNativePvpProfileReady(lease.profile->name));
        Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted));CHECK(emitted==c.expected);
        for(unsigned i=0;i<emitted;++i){int team=99;CHECK(EnemyControlNativePvpRead(e[i],&team)==Life::live && team==5);CHECK(EnemyControlNativePvpWrite(e[i],5));}
        if(c.init)CHECK(At<float>(objects[0].bytes,c.init-0x10)==3.25f);
        if(c.init){Put<void*>(objects[0].bytes,c.init+0x40,objects[1].control);RejectCollect();}
        else {Put<void*>(objects[0].bytes,0xF48,objects[1].control);RejectCollect();}
    }
    CHECK(!EnemyControlNativePvpProfileReady("EDF6_TimeShip"));CHECK(!EnemyControlNativePvpProfileReady(nullptr));
}
void TestBigBatteryBoundaries() {
    Reset();auto lease=Lease(0x17BE7E0);OwnedTemplate(0x1AA0);CommonContact();ContactPointer(0x23C0);SpiderMap();
    Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted) && emitted==4);const auto old=e[3];int team=99;
    CHECK(old.object==objects[1].bytes && old.kind==EmitterKind::liveOwnerLastTeam);
    Put<int>(objects[0].bytes,kTeam,-1);Put<int>(objects[0].bytes,0x318,2);
    CHECK(EnemyControlNativePvpRead(old,&team)==Life::live && team==2);CHECK(EnemyControlNativePvpWrite(old,2));
    CHECK(At<float>(objects[1].bytes,0x10)==3.25f && At<float>(replacement,0x10)==7.25f);
    Put<void*>(objects[1].bytes,0x2B0,objects[2].bytes);CHECK(EnemyControlNativePvpRead(old,&team)==Life::invalid);RejectCollect();
    Put<void*>(objects[1].bytes,0x2B0,objects[0].bytes);Put<std::size_t>(objects[1].bytes,0x288,2);RejectCollect();
    Put<std::size_t>(objects[1].bytes,0x288,1);Put<void*>(gunNodes[0],0,gunNodes[0]);RejectCollect();
    SpiderMap(false);watched=objects[1].bytes;watchedSize=sizeof(objects[1].bytes);denied=watched;deniedSize=watchedSize;
    CHECK(EnemyControlNativePvpRead(old,&team)==Life::gone);CHECK(watchedReads==0);
    Reset();lease=Lease(0x17AFEE0);Contact(objects[0].bytes+0x11C0);BigUnit();
    Put<void*>(objects[0].bytes,0x1628,objects[1].bytes);Put<void*>(objects[0].bytes,0x1630,makerControls[1]);
    CHECK(Collect(e,&emitted) && emitted==3);
    Reset();lease=Lease(0x17B6270);Put<void*>(objects[0].bytes,0x12C0,objects[0].bytes);Put<void*>(objects[0].bytes,0x12C8,makers[0]);
    BigUnit(1);BigUnit(2);Put<void*>(objects[0].bytes,0x1460,batteries);Put<std::size_t>(objects[0].bytes,0x1470,1);
    Put<void*>(batteries,0,objects[1].bytes);Put<void*>(batteries,8,makerControls[1]);
    Put<void*>(objects[0].bytes,0x1498,objects[2].bytes);Put<void*>(objects[0].bytes,0x14A0,makerControls[2]);
    CHECK(Collect(e,&emitted) && emitted==3);CHECK(e[0].object==objects[0].bytes+0x12C0);
    Put<void*>(objects[0].bytes,0x12C0,objects[1].bytes);RejectCollect();
    (void)lease;
}
void Foot() {
    auto* root=objects[0].bytes;auto* child=objects[1].bytes;
    Put<void*>(child,0,image+0x17AB118);Put<void*>(child,0x38,root);Put<void*>(child,0x5C0,root);
    Contact(batteries+0x400);Put<void*>(batteries+0x400,8,child);Put<void*>(batteries+0x400,0x40,child);Put<void*>(batteries+0x400,0x48,objects[1].control);
    Put<LONG>(footContactControl,0,1);Put<LONG>(footContactControl,4,1);Put<void*>(footContactControl,8,batteries+0x400);
    Put<void*>(child,0x7A8,footContactControl);Put<void*>(child,0x7B0,batteries+0x400);
    Put<void*>(child,0xBA0,child);Put<void*>(child,0xBA8,objects[1].control);Put<void*>(child,0xBB0,makers[0]);Put<void*>(child,0xBB8,makerControls[0]);
    Put<void*>(root,0xAE8,legRows);Put<std::size_t>(root,0xAF0,1);Put<std::size_t>(root,0xAF8,1);
    Put<void*>(legRows,0x28,legController);Put<void*>(legController,0,root);Put<void*>(legController,0x10,partEntries);Put<std::size_t>(legController,0x18,1);Put<std::size_t>(legController,0x20,1);
    Put<void*>(partEntries,8,partNode);Put<void*>(partNode,8,child);Put<void*>(root,0x48,sceneEntries);Put<std::size_t>(root,0x50,1);Put<std::size_t>(root,0x58,1);Put<void*>(sceneEntries,0,child);
}
void TestDeiroiParts() {
    Reset();auto native=Lease(0x17AA648);Foot();ObjRef members[8]{};Emitter e[8]{};unsigned count=0,emitted=0;
    CHECK(EnemyControlNativePvpCollect(Ref(),members,8,&count,e,8,&emitted));CHECK(count==2 && members[1].obj==objects[1].bytes && emitted==2 && e[1].member.obj==objects[1].bytes && e[1].object==objects[1].bytes+0xB90);
    int team=99;CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==5);
    Put<void*>(objects[1].bytes,0xBA0,objects[0].bytes);CHECK(EnemyControlNativePvpRead(e[1],&team)==Life::invalid);RejectCollect();
    Put<void*>(objects[1].bytes,0xBA0,objects[1].bytes);Put<void*>(objects[1].bytes,0x38,objects[2].bytes);RejectCollect();
    Put<void*>(objects[1].bytes,0x38,objects[0].bytes);Put<void*>(legController,0,objects[2].bytes);RejectCollect();
    Put<void*>(legController,0,objects[0].bytes);Put<std::size_t>(objects[0].bytes,0xAF8,33);RejectCollect();
    Put<std::size_t>(objects[0].bytes,0xAF8,1);Put<int>(objects[1].bytes,0xB30,2);RejectCollect();
    for(const auto& type:kFeet) {
        Reset();native=Lease(0x17AA648);Foot();Put<void*>(objects[1].bytes,0,image+type.vtable);
        CHECK(EnemyControlNativePvpCollect(Ref(),members,8,&count,e,8,&emitted));
        CHECK(count==2 && emitted==(type.vtable==0x17AB118 || type.vtable==0x17AB770 ? 2u : 1u));
        for(unsigned i=0;i<emitted;++i)CHECK(EnemyControlNativePvpRead(e[i],&team)==Life::live && team==5);
        Put<void*>(footContactControl,8,objects[2].bytes);RejectCollect();
    }
    (void)native;
}
void __fastcall NativeSetTeam(void* object,int team,bool registered) noexcept {
    ++nativeWrites;
    const auto* type=native_pvp_detail::Class(ObjRef::Of(object));
    if(type && type->vtableRva==0x17AA648 && At<std::size_t>(object,0x58)==1)
        NativeSetTeam(At<void*>(At<void*>(object,0x48),0),team,false);
    Put<int>(object,kTeam,team);
    if(team>=0) {
        Put<int>(object,0x318,team);const auto flags=At<unsigned>(object,kObjectFlags);
        Put<unsigned>(object,kObjectFlags,registered ? flags|0x40u : flags&~0x40u);
    }
}
void TestLeaseIntegration() {
    Reset();Ufo507();auto native=Lease(0x17CC2A8,true);controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(native,&hooks));
    Put<int>(objects[0].bytes,0x790,12345); // cached IP team is native-owned and must remain untouched
    unsigned char before[sizeof(objects[0].bytes)]{};std::memcpy(before,objects[0].bytes,sizeof(before));
    integrationOwnerTeam=1;nativeWrites=0;controlled_pvp::Lease lease{};
    CHECK(controlled_pvp::Probe(Ref(),hooks));CHECK(nativeWrites==0);
    CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));CHECK(lease.id!=0);
    CHECK(At<int>(objects[0].bytes,kTeam)==1 && At<int>(objects[0].bytes,0x318)==1 && At<LONG>(objects[0].control,12)==2);
    CHECK(At<int>(objects[0].bytes,0x790)==12345);
    integrationOwnerTeam=2;CHECK(controlled_pvp::Frame(lease));CHECK(At<int>(objects[0].bytes,kTeam)==2);
    controlled_pvp::Status status{};CHECK(controlled_pvp::ReadStatus(lease,&status));
    CHECK(status.followsOwnerTeam && status.members==1 && status.emitters==1);
    integrationOwnerTeam=0;CHECK(controlled_pvp::Frame(lease));CHECK(At<int>(objects[0].bytes,kTeam)==5 && At<int>(objects[0].bytes,0x318)==5);
    integrationOwnerTeam=1;CHECK(controlled_pvp::Frame(lease));CHECK(controlled_pvp::Restore(&lease));CHECK(lease.id==0);
    CHECK(At<LONG>(objects[0].control,12)==1 && std::memcmp(before,objects[0].bytes,sizeof(before))==0);
    // Real provider and real lease engine retain rollback across invalid owners.
    Reset();Ufo515();native=Lease(0x17CC838);CHECK(EnemyControlNativePvpHooks(native,&hooks));integrationOwnerTeam=1;
    CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));Put<void*>(batteries+0x20,0x10,objects[1].bytes);
    CHECK(!controlled_pvp::Frame(lease));CHECK(!controlled_pvp::Restore(&lease));CHECK(lease.id!=0);
    Put<void*>(batteries+0x20,0x10,objects[0].bytes);CHECK(controlled_pvp::Restore(&lease));CHECK(lease.id==0);
    CHECK(At<int>(objects[0].bytes,kTeam)==5 && At<LONG>(objects[0].control,12)==1);
    // Root SetTeam recursively changes real SceneObject foot parts. Every part
    // must have been captured before that write, then restore its own baseline.
    Reset();native=Lease(0x17AA648,true);Foot();Put<int>(objects[1].bytes,kTeam,4);Put<int>(objects[1].bytes,0x318,4);
    CHECK(EnemyControlNativePvpHooks(native,&hooks));CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));
    CHECK(At<int>(objects[0].bytes,kTeam)==1 && At<int>(objects[1].bytes,kTeam)==1 && At<LONG>(objects[1].control,12)==2);
    integrationOwnerTeam=2;CHECK(controlled_pvp::Frame(lease));CHECK(At<int>(objects[1].bytes,kTeam)==2);
    CHECK(controlled_pvp::Restore(&lease));CHECK(At<int>(objects[0].bytes,kTeam)==5 && At<int>(objects[1].bytes,kTeam)==4 && At<int>(objects[1].bytes,0x318)==4 && At<LONG>(objects[1].control,12)==1);
    integrationOwnerTeam=1;
    // Replaced emitter storage is never queried during Sync or final Restore.
    Reset();Ufo515();native=Lease(0x17CC838);CHECK(EnemyControlNativePvpHooks(native,&hooks));
    CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));Battery(replacement,0);
    Put<void*>(objects[0].bytes,0x1298,replacement);Put<std::size_t>(objects[0].bytes,0x12A8,1);
    watched=batteries;watchedSize=sizeof(batteries);denied=watched;deniedSize=watchedSize;
    CHECK(controlled_pvp::Frame(lease));CHECK(controlled_pvp::Restore(&lease));CHECK(watchedReads==0 && lease.id==0);
}
void TestShellAndSpinner() {
    for(auto vt:{0x17B6CA0u,0x17B8100u}) {
        Reset();auto native=Lease(vt);BigUnit();auto* helper=objects[2].bytes;
        Put<void*>(objects[0].bytes,vt==0x17B6CA0 ? 0x1550 : 0x11A8,helper);
        Put<void*>(objects[0].bytes,vt==0x17B6CA0 ? 0x1558 : 0x11B0,makerControls[0]);
        if(vt==0x17B6CA0) {
            Put<void*>(helper,0xE8,batteries);Put<void*>(helper,0xF0,makerControls[2]);
            Put<void*>(batteries,0,objects[1].bytes);Put<void*>(batteries,8,makerControls[1]);
        } else {
            Put<void*>(helper,0x78,batteries);Put<std::size_t>(helper,0x80,1);Put<std::size_t>(helper,0x88,1);
            Put<void*>(batteries,0,objects[1].bytes);Put<void*>(batteries,8,makerControls[1]);
        }
        controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(native,&hooks));
        Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted) && emitted==1 && e[0].kind==EmitterKind::liveOwnerLastTeam);
        int team=99;CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==5);
        Put<void*>(objects[1].bytes,0x2B8,objects[2].control);RejectCollect();
    }
}
void TestSmallMakerTypes() {
    Reset();Ufo515(1);Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted));
    Put<void*>(makers[0],0,image+0x17E6F08);CHECK(Collect(e,&emitted));
    Put<void*>(makers[0],0,image+0xBAD);RejectCollect();
}
void TestFamilyDelegation() {
    for(auto vt:{0x17BEC68u,0x17B93C0u,0x17ADA78u}) {
        Reset();auto native=Lease(vt,true);const bool boss=vt==0x17BEC68;bossReady=boss;transportReady=vt==0x17B93C0;berserkerReady=vt==0x17ADA78;
        controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(native,&hooks));CHECK(familyCollects>0);
        CHECK(EnemyControlNativePvpProfileReady(native.profile->name));
        Emitter mirror{EmitterKind::ownerTeamMirror,Ref(),objects[0].bytes+0x1200,1};int team=99;
        Put<int>(objects[0].bytes,0x1200,2);CHECK(EnemyControlNativePvpRead(mirror,&team)==Life::live && team==2 && familyReads==1);
        CHECK(EnemyControlNativePvpWrite(mirror,1) && At<int>(objects[0].bytes,0x1200)==1 && familyWrites==1);
        CHECK(EnemyControlNativePvpBaseline(mirror,5,4,&team) && team==5);
    }
}
void TestMermanAndSquid() {
    Reset();auto native=Lease(0x17B5370);BigUnit(1);BigUnit(2);auto* weapon=objects[3].bytes;auto* poison=objects[4].bytes;
    Put<void*>(objects[0].bytes,0x1C10,weapon);Put<void*>(objects[0].bytes,0x1C18,makerControls[0]);
    Put<void*>(objects[0].bytes,0x1C20,poison);Put<void*>(objects[0].bytes,0x1C28,makerControls[3]);
    Put<void*>(weapon,0,objects[1].bytes);Put<void*>(weapon,8,makerControls[1]);Put<void*>(poison,0xF0,objects[2].bytes);Put<void*>(poison,0xF8,makerControls[2]);
    controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(native,&hooks));Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted) && emitted==2);
    Put<void*>(objects[2].bytes,0x2B8,objects[1].control);RejectCollect();
    Reset();native=Lease(0x17B8898);auto* arm=objects[1].bytes;Put<void*>(arm,0,objects[0].bytes);
    Put<void*>(objects[0].bytes,0x13A8,batteries);Put<std::size_t>(objects[0].bytes,0x13B0,1);Put<std::size_t>(objects[0].bytes,0x13B8,1);
    Put<void*>(batteries,0,arm);Put<void*>(batteries,8,makerControls[1]);Put<int>(objects[0].bytes,kTeam,-1);Put<int>(objects[0].bytes,0x318,2);
    CHECK(EnemyControlNativePvpHooks(native,&hooks));CHECK(Collect(e,&emitted) && emitted==1 && e[0].object==arm && e[0].kind==EmitterKind::liveOwnerLastTeam);
    int team=99;CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::live && team==2);CHECK(EnemyControlNativePvpWrite(e[0],2));
    Put<void*>(arm,0,objects[2].bytes);RejectCollect();Put<void*>(arm,0,objects[0].bytes);
    Put<std::size_t>(objects[0].bytes,0x13B8,0);watched=arm;watchedSize=0x1A0;denied=watched;deniedSize=watchedSize;
    CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::gone);CHECK(watchedReads==0);
}
void TestMixedFamilyTree() {
    Reset();Ufo507();auto native=Lease(0x17CC2A8,true);SetClass(1,0x17B93C0);Ufo507(2);transportReady=true;familyExtra=true;
    Put<void*>(objects[0].head,0,nodes[0]);Put<void*>(objects[0].head,8,nodes[0]);Put<void*>(nodes[0],0,objects[0].head);Put<void*>(nodes[0],8,objects[0].head);Put<void*>(nodes[0],0x10,objects[1].bytes);Put<std::size_t>(objects[0].bytes,0x558,1);
    Put<int>(objects[1].bytes,0x1200,5);ObjRef members[8]{};Emitter e[8]{};unsigned count=0,emitted=0;
    CHECK(EnemyControlNativePvpCollect(Ref(),members,8,&count,e,8,&emitted));CHECK(count==3 && emitted==3 && familyCollects>0);
    controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(native,&hooks));controlled_pvp::Lease lease{};integrationOwnerTeam=1;
    CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));CHECK(At<int>(objects[0].bytes,kTeam)==1 && At<int>(objects[1].bytes,kTeam)==1 && At<int>(objects[2].bytes,kTeam)==1);
    CHECK(At<int>(objects[1].bytes,0x1200)==1);CHECK(!EnemyControlNativePvpReset());
    CHECK(controlled_pvp::Restore(&lease));CHECK(At<int>(objects[1].bytes,0x1200)==5 && At<LONG>(objects[2].control,12)==1);
}
void TestInstallResetGate() {
    Reset();partsReady=false;
    CHECK(!EnemyControlNativePvpProfileReady("EDF6_Berserker_TypeA"));
    CHECK(EnemyControlNativePvpProfileReady("UfoSmall507"));
    CHECK(EnemyControlNativePvpInstall());CHECK(familyInstalls==3 && partsInstalls==1 && partsReady);
    CHECK(EnemyControlNativePvpProfileReady("EDF6_Berserker_TypeA"));
    CHECK(EnemyControlNativePvpReset());CHECK(familyResets==3);
    Reset();partsReady=false;partsInstallAllowed=false;CHECK(!EnemyControlNativePvpInstall());
    CHECK(berserkerReady && !partsReady && partsInstalls==1);
    CHECK(!EnemyControlNativePvpProfileReady("EDF6_Berserker_TypeA"));
    CHECK(EnemyControlNativePvpProfileReady("UfoSmall507"));
}
void TestAllProfileReadiness() {
    // Family stubs exercise only dispatch and isolation; each family's real
    // executable anchors and startup hooks are verified in its own fixture.
    Reset();CHECK(sizeof(kProfiles)/sizeof(kProfiles[0])==34);CHECK(EnemyControlNativePvpInstall());
    for(const auto& p:kProfiles)CHECK(EnemyControlNativePvpProfileReady(p.name));
    for(unsigned disabled=0;disabled<3;++disabled) {
        bossReady=disabled!=0;transportReady=disabled!=1;berserkerReady=disabled!=2;
        for(const auto& p:kProfiles) {
            const bool boss=p.vtableRva==0x17BEC68;
            const bool transport=p.vtableRva==0x17B93C0 || p.vtableRva==0x17C94D8 || p.vtableRva==0x17C9DC0;
            const bool berserker=p.inputRva==0x343DD0;
            const bool expected=!(disabled==0 ? boss : disabled==1 ? transport : berserker);
            CHECK(EnemyControlNativePvpProfileReady(p.name)==expected);
        }
    }
    bossReady=transportReady=berserkerReady=true;partsReady=false;
    for(const auto& p:kProfiles)CHECK(EnemyControlNativePvpProfileReady(p.name)==(p.inputRva!=0x343DD0));
    partsReady=true;
    image[kAnchors[0].rva]^=1;
    for(const auto& p:kProfiles)CHECK(!EnemyControlNativePvpProfileReady(p.name));
    image[kAnchors[0].rva]^=1;
    CHECK(EnemyControlNativePvpReset());
}
void AliveBeam(int hit=4,int damage=3) {
    auto* bullet=objects[3].bytes;Put<void*>(bullet,0,image+0x179E048);Put<int>(bullet,0xC30,1);
    Put<void*>(bullet,0xAE8,objects[0].bytes);Put<void*>(bullet,0xAF0,objects[0].control);Put<void*>(bullet,0x880,objects[0].bytes);Put<void*>(bullet,0x888,objects[0].control);
    Put<int>(bullet,0xB40,hit);Put<int>(bullet,0x894,damage);Put<void*>(replacement,0xB0,bullet);Put<void*>(replacement,0xB8,objects[3].control);
}
void TestPersistentBeamCaches() {
    Reset();auto native=Lease(0x17BE7E0);OwnedTemplate(0x1AA0);CommonContact();ContactPointer(0x23C0);SpiderMap();AliveBeam();
    Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted) && emitted==6);
    CHECK(e[4].kind==EmitterKind::ownerLastTeamCache && e[4].object==objects[3].bytes+0xB40 && e[5].object==objects[3].bytes+0x894);
    int team=99;CHECK(EnemyControlNativePvpRead(e[4],&team)==Life::live && team==4);CHECK(EnemyControlNativePvpRead(e[5],&team)==Life::live && team==3);
    controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(native,&hooks));controlled_pvp::Lease lease{};integrationOwnerTeam=1;
    CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));CHECK(At<int>(objects[3].bytes,0xB40)==1 && At<int>(objects[3].bytes,0x894)==1);
    integrationOwnerTeam=2;CHECK(controlled_pvp::Frame(lease));CHECK(At<int>(objects[3].bytes,0xB40)==2 && At<int>(objects[3].bytes,0x894)==2);
    CHECK(controlled_pvp::Restore(&lease));CHECK(At<int>(objects[3].bytes,0xB40)==4 && At<int>(objects[3].bytes,0x894)==3);
    Put<void*>(objects[3].bytes,0x888,objects[2].control);CHECK(EnemyControlNativePvpRead(e[4],&team)==Life::invalid);RejectCollect();
    Put<void*>(objects[3].bytes,0x888,objects[0].control);Put<void*>(replacement,0xB0,nullptr);Put<void*>(replacement,0xB8,nullptr);
    watched=objects[3].bytes;watchedSize=sizeof(objects[3].bytes);denied=watched;deniedSize=watchedSize;
    CHECK(EnemyControlNativePvpRead(e[4],&team)==Life::gone);CHECK(watchedReads==0);CHECK(!EnemyControlNativePvpWrite(e[4],2));
    Reset();native=Lease(0x17BE7E0);OwnedTemplate(0x1AA0);CommonContact();ContactPointer(0x23C0);SpiderMap();CHECK(EnemyControlNativePvpHooks(native,&hooks));integrationOwnerTeam=1;
    CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));AliveBeam(1,1);CHECK(controlled_pvp::Frame(lease));CHECK(controlled_pvp::Restore(&lease));
    CHECK(At<int>(objects[3].bytes,0xB40)==5 && At<int>(objects[3].bytes,0x894)==5);
    // A retained lease in stable mode-off must snapshot task-modified 4/3 independently.
    integrationOwnerTeam=1;CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));integrationOwnerTeam=0;CHECK(controlled_pvp::Frame(lease));
    AliveBeam(4,3);CHECK(controlled_pvp::Frame(lease));CHECK(Collect(e,&emitted) && emitted==6);
    CHECK(EnemyControlNativePvpBaseline(e[4],5,5,&team) && team==4);
    integrationOwnerTeam=1;CHECK(controlled_pvp::Frame(lease));CHECK(At<int>(objects[3].bytes,0xB40)==1 && At<int>(objects[3].bytes,0x894)==1);
    integrationOwnerTeam=0;CHECK(controlled_pvp::HasTemporaryTeam(Ref()));
    CHECK(EnemyControlNativePvpBaseline(e[4],5,5,&team) && team==4);
    CHECK(controlled_pvp::Restore(&lease));CHECK(At<int>(objects[3].bytes,0xB40)==4 && At<int>(objects[3].bytes,0x894)==3);
    AliveBeam(4,3);Put<int>(objects[3].bytes,0xC30,0);CHECK(Collect(e,&emitted) && emitted==4);CHECK(At<int>(objects[3].bytes,0xB40)==4 && At<int>(objects[3].bytes,0x894)==3);
}
void TestHumanoidPersistentMap() {
    Reset();auto native=Lease(0x17C4D70,true);auto* root=objects[0].bytes;auto* weapon=objects[1].bytes;
    Put<void*>(root,0x1120,batteries);Put<std::size_t>(root,0x1130,1);Put<void*>(batteries,0,weapon);Put<void*>(batteries,8,objects[1].control);Put<void*>(weapon,0x5B0,root);
    AliveBeam(4,3);Put<void*>(weapon,0x368,gunHead);Put<std::size_t>(weapon,0x370,1);
    Put<void*>(gunHead,0,gunNodes[0]);Put<void*>(gunHead,8,gunNodes[0]);Put<void*>(gunNodes[0],0,gunHead);Put<void*>(gunNodes[0],8,gunHead);
    Put<void*>(gunNodes[0],0x18,objects[3].bytes);Put<void*>(gunNodes[0],0x20,objects[3].control);
    Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted) && emitted==3 && e[1].kind==EmitterKind::ownerLastTeamCache);
    Emitter exported[8]{};unsigned exportedCount=0;CHECK(EnemyControlNativePvpCollectHumanoidWeapons(Ref(),exported,8,&exportedCount) && exportedCount==3);
    unsigned tooSmall=0;CHECK(!EnemyControlNativePvpCollectHumanoidWeapons(Ref(),exported,1,&tooSmall) && tooSmall==0);
    controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(native,&hooks));controlled_pvp::Lease lease{};integrationOwnerTeam=1;
    CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));CHECK(At<int>(objects[3].bytes,0xB40)==1 && At<int>(objects[3].bytes,0x894)==1);
    CHECK(controlled_pvp::Restore(&lease));CHECK(At<int>(objects[3].bytes,0xB40)==4 && At<int>(objects[3].bytes,0x894)==3);
    CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));
    Put<std::uint32_t>(objects[3].bytes,0xC34,1u<<14);CHECK(controlled_pvp::Frame(lease));
    CHECK(Collect(e,&emitted) && emitted==1);CHECK(At<int>(objects[3].bytes,0xB40)==4 && At<int>(objects[3].bytes,0x894)==3);
    CHECK(At<LONG>(objects[3].control,12)==1);CHECK(controlled_pvp::Restore(&lease));
    Put<std::uint32_t>(objects[3].bytes,0xC34,0);CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));
    Put<void*>(gunHead,0,gunHead);Put<void*>(gunHead,8,gunHead);Put<std::size_t>(weapon,0x370,0);
    CHECK(controlled_pvp::Frame(lease));CHECK(At<int>(objects[3].bytes,0xB40)==4 && At<int>(objects[3].bytes,0x894)==3);
    CHECK(At<LONG>(objects[3].control,12)==1);CHECK(controlled_pvp::Restore(&lease));
    Put<void*>(gunHead,0,gunNodes[0]);Put<void*>(gunHead,8,gunNodes[0]);Put<std::size_t>(weapon,0x370,1);
    Put<void*>(gunNodes[0],0,gunNodes[0]);RejectCollect();
    Put<void*>(gunHead,0,gunHead);Put<void*>(gunHead,8,gunHead);Put<std::size_t>(weapon,0x370,0);
    watched=objects[3].bytes;watchedSize=sizeof(objects[3].bytes);denied=watched;deniedSize=watchedSize;int team=99;
    CHECK(EnemyControlNativePvpRead(e[1],&team)==Life::gone);CHECK(watchedReads==0);
}
void TestRetainedBeamLifecycle() {
    for(unsigned detach=0;detach<3;++detach) {
        Reset();auto native=Lease(0x17BE7E0,true);OwnedTemplate(0x1AA0);CommonContact();ContactPointer(0x23C0);SpiderMap();AliveBeam();
        Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted) && emitted==6);
        CHECK(e[4].storage.obj==objects[3].bytes && e[4].storage.ctrl==objects[3].control);
        CHECK(!EnemyControlNativePvpCacheRetained(e[4]));
        Put<LONG>(objects[0].control,8,0);int unresolved=99;
        CHECK(EnemyControlNativePvpRead(e[4],&unresolved)==Life::invalid);
        Put<LONG>(objects[0].control,8,1);
        controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(native,&hooks));controlled_pvp::Lease lease{};integrationOwnerTeam=1;
        CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));
        CHECK(At<LONG>(objects[3].control,12)==3 && EnemyControlNativePvpCacheRetained(e[4]));
        if(detach==0){Put<void*>(replacement,0xB0,nullptr);Put<void*>(replacement,0xB8,nullptr);}
        if(detach==1)Put<int>(objects[3].bytes,0xC30,0);
        if(detach==2){Put<void*>(gunNodes[0],0x38,nullptr);Put<void*>(gunNodes[0],0x40,nullptr);}
        CHECK(controlled_pvp::Frame(lease));
        CHECK(At<int>(objects[3].bytes,0xB40)==4 && At<int>(objects[3].bytes,0x894)==3);
        CHECK(At<LONG>(objects[3].control,12)==1 && !EnemyControlNativePvpCacheRetained(e[4]));
        CHECK(controlled_pvp::Restore(&lease));CHECK(At<LONG>(objects[0].control,12)==1);
    }
    // The owner can be destroyed while its independently retained beam lives.
    Reset();auto native=Lease(0x17BE7E0,true);OwnedTemplate(0x1AA0);CommonContact();ContactPointer(0x23C0);SpiderMap();AliveBeam();
    Emitter e[8]{};unsigned emitted=0;CHECK(Collect(e,&emitted) && emitted==6);
    controlled_pvp::Hooks hooks{};CHECK(EnemyControlNativePvpHooks(native,&hooks));controlled_pvp::Lease lease{};integrationOwnerTeam=1;
    CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));
    Put<LONG>(objects[0].control,8,0);InterlockedDecrement(reinterpret_cast<volatile LONG*>(objects[0].control+12));
    watched=objects[0].bytes;watchedSize=sizeof(objects[0].bytes);denied=watched;deniedSize=watchedSize;
    int team=99;CHECK(EnemyControlNativePvpRead(e[4],&team)==Life::live && team==1);
    CHECK(controlled_pvp::Restore(&lease));CHECK(watchedReads==0);
    CHECK(At<int>(objects[3].bytes,0xB40)==4 && At<int>(objects[3].bytes,0x894)==3);
    CHECK(At<LONG>(objects[3].control,12)==1 && At<LONG>(objects[0].control,12)==0 && controlDeletes==1);
    denied=watched=nullptr;deniedSize=watchedSize=0;
    // Reusing object memory with a NEW control is legitimate. The old control
    // cannot be reused until both saved field bindings release their weak pins.
    Reset();native=Lease(0x17BE7E0);OwnedTemplate(0x1AA0);CommonContact();ContactPointer(0x23C0);SpiderMap();AliveBeam();
    CHECK(EnemyControlNativePvpHooks(native,&hooks));CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));
    CHECK(At<LONG>(objects[3].control,12)==3);
    Put<LONG>(objects[3].control,8,0);InterlockedDecrement(reinterpret_cast<volatile LONG*>(objects[3].control+12));
    CHECK(At<LONG>(objects[3].control,12)==2 && controlDeletes==0);
    AliveBeam(1,1);Put<void*>(objects[3].bytes,kSelfCtrl,objects[4].control);Put<void*>(replacement,0xB8,objects[4].control);
    CHECK(controlled_pvp::Frame(lease));
    CHECK(At<LONG>(objects[3].control,12)==0 && controlDeletes==1 && At<LONG>(objects[4].control,12)==3);
    CHECK(controlled_pvp::Restore(&lease));
    CHECK(At<int>(objects[3].bytes,0xB40)==5 && At<int>(objects[3].bytes,0x894)==5 && At<LONG>(objects[4].control,12)==1);
    // A destroyed storage control is checked before any old object memory read.
    Reset();native=Lease(0x17BE7E0);OwnedTemplate(0x1AA0);CommonContact();ContactPointer(0x23C0);SpiderMap();AliveBeam();
    CHECK(EnemyControlNativePvpHooks(native,&hooks));CHECK(controlled_pvp::Acquire(Ref(),"owner1",hooks,&lease));
    CHECK(Collect(e,&emitted) && emitted==6);Put<LONG>(objects[3].control,8,0);
    InterlockedDecrement(reinterpret_cast<volatile LONG*>(objects[3].control+12));Put<void*>(replacement,0xB0,nullptr);Put<void*>(replacement,0xB8,nullptr);
    watched=objects[3].bytes;watchedSize=sizeof(objects[3].bytes);denied=watched;deniedSize=watchedSize;
    CHECK(EnemyControlNativePvpReadRetainedCache(e[4],&team)==Life::gone);
    CHECK(controlled_pvp::Restore(&lease));CHECK(watchedReads==0 && controlDeletes==1 && At<LONG>(objects[3].control,12)==0);
}
void TestFailureAndWholeTree() {
    Reset();Ufo515();Emitter e[8]{};unsigned emitted=99;
    CHECK(!Collect(e,&emitted,1) && emitted==0);Put<std::size_t>(objects[0].bytes,0x12A8,(std::numeric_limits<std::size_t>::max)());RejectCollect();
    Put<std::size_t>(objects[0].bytes,0x12A8,2);Put<void*>(objects[0].bytes,0x1298,nullptr);RejectCollect();
    Reset();Ufo507();Ufo507(1);Put<void*>(objects[0].head,0,nodes[0]);Put<void*>(objects[0].head,8,nodes[0]);
    Put<void*>(nodes[0],0,objects[0].head);Put<void*>(nodes[0],8,objects[0].head);Put<void*>(nodes[0],0x10,objects[1].bytes);Put<std::size_t>(objects[0].bytes,0x558,1);
    CHECK(Collect(e,&emitted) && emitted==2);
    SetClass(1,0x17B93C0);RejectCollect(); // unknown native child paths fail the entire operation
    for(auto v:{0x17B93C0u,0x17C94D8u,0x17C9DC0u,0x17BEC68u}) {
        Reset();auto lease=Lease(v);controlled_pvp::Hooks hooks{};CHECK(!EnemyControlNativePvpHooks(lease,&hooks));
    }
    Reset();Ufo507();CHECK(Collect(e,&emitted));int team=99;
    Put<LONG>(objects[0].control,8,0);watched=objects[0].bytes;watchedSize=sizeof(objects[0].bytes);
    CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::gone);CHECK(watchedReads==0);
    Put<LONG>(objects[0].control,8,1);denied=objects[0].bytes;deniedSize=sizeof(objects[0].bytes);
    CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::invalid);
    denied=nullptr;Put<void*>(objects[0].bytes,kSelfCtrl,objects[1].control);
    CHECK(EnemyControlNativePvpRead(e[0],&team)==Life::gone);
    CHECK(EnemyControlNativePvpRead(e[0],nullptr)==Life::invalid);
    CHECK(!EnemyControlNativePvpWrite({},2));
}
}
namespace edf {
bool Matches(const unsigned char* base,std::size_t rva,const unsigned char*,std::size_t) noexcept {return base==crew::image && rva==crew::kSetTeam;}
bool Readable(const void* pointer,std::size_t bytes,bool) noexcept {
    using namespace native_pvp_fixture;
    if(Overlap(pointer,bytes,watched,watchedSize))++watchedReads;
    if(!pointer || Overlap(pointer,bytes,denied,deniedSize))return false;
    const auto start=reinterpret_cast<std::uintptr_t>(pointer);
    for(unsigned i=0;i<regionCount;++i) {
        const auto base=reinterpret_cast<std::uintptr_t>(regions[i].pointer);
        if(start>=base && start-base<=regions[i].size && bytes<=regions[i].size-(start-base))return true;
    }
    return false;
}
}
namespace crew::enemy_native {
bool WalkingPartsControlReady() noexcept {return native_pvp_fixture::partsReady;}
bool InstallWalkingPartsAcceptedActions() noexcept {
    using namespace native_pvp_fixture;++partsInstalls;CHECK(berserkerReady);
    partsReady=berserkerReady && partsInstallAllowed;return partsReady;
}
}
namespace crew {
int PvpSupportTeam(const char*) noexcept {return native_pvp_fixture::integrationOwnerTeam;}
namespace {
bool FixtureFamily(const ObjRef& r,unsigned vtable) noexcept {
    if(!r.obj || !Readable(r.obj,8))return false;const auto* table=At<const unsigned char*>(r.obj,0);
    return reinterpret_cast<std::uintptr_t>(table)>=8 && Readable(table-8,8) && At<const void*>(table-8,0)==At<const void*>(image+vtable-8,0);
}
bool FixtureCollect(const ObjRef& r,ObjRef* m,unsigned mc,unsigned* mn,controlled_pvp::Emitter* e,unsigned ec,unsigned* en) noexcept {
    using namespace native_pvp_fixture;
    ++familyCollects;if(mn)*mn=0;if(en)*en=0;if(!m || !mc || !mn || !en)return false;
    if(familyExtra && (mc<2 || ec<2 || !e))return false;
    m[0]=r;*mn=1;
    if(familyExtra) {
        m[1]=Ref(2);*mn=2;*en=2;e[0]={controlled_pvp::EmitterKind::ownerTeamMirror,r,objects[1].bytes+0x1200,1};
        e[1]={controlled_pvp::EmitterKind::liveOwnerTeam,Ref(2),objects[2].bytes+0x700,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(makers[0]))};
    }
    return true;
}
}
bool EnemyControlOwnsTargetSelector(const void* root,const void* function) noexcept {
    using namespace native_pvp_fixture;
    if(!selectorOwned || function!=image+0x20000+14)return false;
    for(const auto& o:objects)if(root==o.bytes)return At<const void*>(root,0)==o.clone+8;
    return false;
}
bool EnemyControlBossPvpInstall() noexcept {++native_pvp_fixture::familyInstalls;return native_pvp_fixture::bossReady=true;}
bool EnemyControlTransportPvpInstall() noexcept {++native_pvp_fixture::familyInstalls;return native_pvp_fixture::transportReady=true;}
bool EnemyControlBossPvpReset() noexcept {++native_pvp_fixture::familyResets;return true;}
void EnemyControlTransportPvpReset() noexcept {++native_pvp_fixture::familyResets;}
bool EnemyControlBerserkerPvpInstall() noexcept {++native_pvp_fixture::familyInstalls;return native_pvp_fixture::berserkerReady=true;}
bool EnemyControlBerserkerPvpReset() noexcept {++native_pvp_fixture::familyResets;return true;}
bool EnemyControlBerserkerPvpReady() noexcept {return native_pvp_fixture::berserkerReady;}
bool EnemyControlBerserkerPvpSupports(const ObjRef& r) noexcept {
    for(auto vt:{0x17ADA78u,0x17AE088u,0x17AE968u,0x17AED50u,0x17AF200u,0x17AF9F8u})if(FixtureFamily(r,vt))return true;
    return false;
}
bool EnemyControlBerserkerPvpCollect(const ObjRef& r,ObjRef* m,unsigned mc,unsigned* mn,controlled_pvp::Emitter* e,unsigned ec,unsigned* en) noexcept {
    return EnemyControlBerserkerPvpReady() && FixtureCollect(r,m,mc,mn,e,ec,en);
}
controlled_pvp::Life EnemyControlBerserkerPvpRead(const controlled_pvp::Emitter& e,int* team) noexcept {return EnemyControlBossPvpRead(e,team);}
bool EnemyControlBerserkerPvpWrite(const controlled_pvp::Emitter& e,int team) noexcept {return EnemyControlBossPvpWrite(e,team);}
bool EnemyControlBerserkerPvpBaseline(const ObjRef& r,const ObjRef& m,int team,int last,int* out,int* savedLast) noexcept {return EnemyControlBossPvpBaseline(r,m,team,last,out,savedLast);}
bool EnemyControlBerserkerPvpBaseline(const controlled_pvp::Emitter& e,int team,int last,int* out) noexcept {return EnemyControlBossPvpBaseline(e,team,last,out);}
bool EnemyControlBossPvpReady() noexcept {return native_pvp_fixture::bossReady;}
bool EnemyControlTransportPvpReady() noexcept {return native_pvp_fixture::transportReady;}
bool EnemyControlBossPvpSupports(const ObjRef& r) noexcept {return FixtureFamily(r,0x17BEC68);}
bool EnemyControlTransportPvpSupports(const ObjRef& r) noexcept {return FixtureFamily(r,0x17B93C0) || FixtureFamily(r,0x17C94D8) || FixtureFamily(r,0x17C9DC0);}
bool EnemyControlBossPvpCollect(const ObjRef& r,ObjRef* m,unsigned mc,unsigned* mn,controlled_pvp::Emitter* e,unsigned ec,unsigned* en) noexcept {
    return EnemyControlBossPvpReady() && FixtureCollect(r,m,mc,mn,e,ec,en);
}
bool EnemyControlTransportPvpCollect(const ObjRef& r,ObjRef* m,unsigned mc,unsigned* mn,controlled_pvp::Emitter* e,unsigned ec,unsigned* en) noexcept {
    return EnemyControlTransportPvpReady() && FixtureCollect(r,m,mc,mn,e,ec,en);
}
controlled_pvp::Life EnemyControlBossPvpRead(const controlled_pvp::Emitter& e,int* team) noexcept {++native_pvp_fixture::familyReads;*team=At<int>(e.object,0);return controlled_pvp::Life::live;}
controlled_pvp::Life EnemyControlTransportPvpRead(const controlled_pvp::Emitter& e,int* team) noexcept {return EnemyControlBossPvpRead(e,team);}
bool EnemyControlBossPvpWrite(const controlled_pvp::Emitter& e,int team) noexcept {++native_pvp_fixture::familyWrites;Put<int>(const_cast<void*>(e.object),0,team);return true;}
bool EnemyControlTransportPvpWrite(const controlled_pvp::Emitter& e,int team) noexcept {return EnemyControlBossPvpWrite(e,team);}
bool EnemyControlBossPvpBaseline(const ObjRef&,const ObjRef&,int team,int last,int* out,int* savedLast) noexcept {*out=team;*savedLast=last;return true;}
bool EnemyControlTransportPvpBaseline(const ObjRef& r,const ObjRef& m,int team,int last,int* out,int* savedLast) noexcept {return EnemyControlBossPvpBaseline(r,m,team,last,out,savedLast);}
bool EnemyControlBossPvpBaseline(const controlled_pvp::Emitter&,int team,int,int* out) noexcept {*out=team;return true;}
bool EnemyControlTransportPvpBaseline(const controlled_pvp::Emitter& e,int team,int last,int* out) noexcept {return EnemyControlBossPvpBaseline(e,team,last,out);}
}
int main() {
    using namespace native_pvp_fixture;
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!crew::image)return 3;
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto target=reinterpret_cast<std::uintptr_t>(&NativeSetTeam);std::memcpy(jump+2,&target,8);
    std::memcpy(crew::image+crew::kSetTeam,jump,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),crew::image+crew::kSetTeam,sizeof(jump));
    OriginTables();FootOrigin();BulletOrigin();TestUfo507();TestAlienBarrier();TestCloneAndContext();TestUfo515AndDetached();TestHumanoidWeapons();TestMonsterAndDeiroi();TestInsects();TestBigBatteryBoundaries();TestFailureAndWholeTree();TestDeiroiParts();TestShellAndSpinner();TestSmallMakerTypes();TestLeaseIntegration();TestFamilyDelegation();TestMermanAndSquid();TestMixedFamilyTree();TestInstallResetGate();TestAllProfileReadiness();TestPersistentBeamCaches();TestHumanoidPersistentMap();TestRetainedBeamLifecycle();
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("enemy_control_pvp_native_test: %d checks, %d failed\n",checks,failures);return failures ? 1 : 0;
}
