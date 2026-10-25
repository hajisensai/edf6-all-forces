// Steam 678CCB46: typed BigGreyBoss parts and the independent meteor publication edge.
// Never infer ownership from a team, an SGO string, proximity or a network-id hash.
#include "enemy_control_pvp_boss.h"
#include "enemy_control_pvp_native.h"
#include "enemy_control.h"
#include "enemy_control_pvp_tree.h"
#include "memory.h"
#include "team_native_logic.h"
#include <cmath>
#include <cstring>

#ifdef EDF6_ENEMY_BOSS_TEST
namespace crew { void* EnemyBossTestFunction(unsigned) noexcept; }
#endif
namespace crew { namespace boss_pvp_detail {
using controlled_pvp::Emitter;using controlled_pvp::EmitterKind;using controlled_pvp::Life;
constexpr unsigned kPublishCall=0x44884F,kPublish=0x474F90,kDelete=0x118A1B0;
constexpr unsigned kRootLimit=64,kMeteorLimit=2048,kBindingLimit=8192;
struct Type { unsigned table,slots;const char* name;bool root,meteor; };
constexpr Type kTypes[]={
    {0x17BEC68,86,"Humanoid_BigGreyBoss",true,false},
    {0x17C02A0,47,"Humanoid_BigGreyBoss_PartsBase",false,false},
    {0x17BF4F0,47,"Humanoid_BigGreyBoss_EffectFire",false,false},
    {0x17BF7E8,47,"Humanoid_BigGreyBoss_EffectModel",false,false},
    {0x17BFAE0,47,"Humanoid_BigGreyBoss_HandMuzzle",false,false},
    {0x17C0610,47,"Humanoid_BigGreyBoss_Ring",false,false},
    {0x17C0918,47,"Humanoid_BigGreyBoss_Ring_Large",false,false},
    {0x17BFE58,45,"Humanoid_BigGreyBoss_Meteor",false,true},
};
struct Anchor {unsigned rva,size;unsigned char bytes[32];};
constexpr Anchor kAnchors[]={
    {0x470ACB,14,{0x8B,0x96,0x18,0x03,0,0,0x48,0x8B,0xCE,0xE8,0x17,0xD9,0x0D,0}},
    {0x4A0392,19,{0x48,0x8B,0x87,0xB0,0x05,0,0,0x8B,0x88,0x18,0x03,0,0,0x89,0x8F,0x10,0x02,0,0}},
    {0x6CF93A,14,{0x44,0x8B,0x88,0x14,0x03,0,0,0x44,0x89,0x89,0xB8,0x02,0,0}},
    {0x6CFEF0,8,{0x48,0x8D,0x81,0xA0,0,0,0,0xC3}},
    {0x6CFF00,8,{0x48,0x8D,0x81,0,0x03,0,0,0xC3}},
    {0x44DB76,18,{0x4D,0x8B,0xB7,0x60,0x0B,0,0,0x4D,0x8B,0xA7,0x70,0x0B,0,0,0x49,0xC1,0xE4,0x04}},
    {0x54ABD0,3,{0xB0,0x01,0xC3}}, // slot 22 approves only; SetTeam is not recursive
    {0x444F1D,16,{0x49,0x8B,0xBE,0xE8,0x17,0,0,0x48,0x8B,0x1F,0x0F,0x57,0xF6,0x48,0x3B,0xDF}},
    {0x444FC2,16,{0x48,0x8B,0x53,0x48,0x48,0x85,0xD2,0x74,0x19,0x8B,0x42,0x08,0x85,0xC0,0x74,0x12}},
    {0x448DF0,8,{0x48,0x8B,0x81,0xD0,0x16,0,0,0xC3}}, // secondary-this +120: root count +17F0
    {0x44C5D1,21,{0x48,0x89,0x8F,0xE8,0x0A,0,0,0x48,0x8B,0x8F,0xF0,0x0A,0,0,0x48,0x89,0x97,0xF0,0x0A,0,0}},
    {0x44CF4B,7,{0x4D,0x8D,0x85,0xE8,0x0A,0,0}},
    {0x44D30C,21,{0x48,0x89,0x8E,0,0x02,0,0,0x48,0x8B,0x8E,0x08,0x02,0,0,0x48,0x89,0x96,0x08,0x02,0,0}},
    {0x44D354,21,{0x48,0x89,0x8E,0x10,0x02,0,0,0x48,0x8B,0x8E,0x18,0x02,0,0,0x48,0x89,0x96,0x18,0x02,0,0}},
    {0x44D5A1,17,{0x48,0x8D,0x4E,0x20,0x48,0x8D,0x94,0x24,0x80,0,0,0,0xE8,0x5E,0x29,0x28,0}},
    {0x6D0677,17,{0x48,0x8B,0x4C,0x24,0x30,0x8B,0x91,0x14,0x03,0,0,0x89,0x90,0x90,0,0,0}},
    {0x44BAF5,12,{0x8B,0x96,0x14,0x03,0,0,0x89,0x96,0x44,0x14,0,0}},
    {0x448801,12,{0x41,0x8B,0x95,0x14,0x03,0,0,0x48,0x8B,0x4C,0x24,0x30}},
    {0x44883D,18,{0x44,0x89,0x74,0x24,0x20,0x41,0xB8,0x02,0,0,0,0x48,0x8D,0x55,0x80,0x49,0x8B,0xCD}},
};
constexpr unsigned char kPublishBytes[]={0xE8,0x3C,0xC7,0x02,0};
bool installed=false,globalUnsafe=false;unsigned char installedCall[5]{};std::uint64_t serial=0;
struct Root {ObjRef ref{};bool unsafe=false;std::uint64_t token=0;unsigned wave=0,total=0,next=0;};
struct Meteor {ObjRef root{},ref{};int team=-1,last=-1;bool adopted=false;};
struct Binding {ObjRef root{},part{};const unsigned char* launcher=nullptr;const void* context=nullptr;const void* maker=nullptr;std::uint64_t serial=0;};
Root roots[kRootLimit]{};Meteor meteors[kMeteorLimit]{};Binding bindings[kBindingLimit]{};
bool Same(const ObjRef& a,const ObjRef& b) noexcept {return a.obj==b.obj && a.ctrl==b.ctrl;}
bool Team(int v) noexcept {return team_native::TeamOrClosing(v);}
bool Cache(EmitterKind kind) noexcept {return kind==EmitterKind::ownerTeamCache || kind==EmitterKind::ownerLastTeamCache;}
Life Live(const ObjRef& r) noexcept {
    if(!r.obj || !r.ctrl || !Readable(r.ctrl,16))return Life::invalid;
    if(At<LONG>(r.ctrl,8)<=0)return Life::gone;
    if(!Readable(r.obj,kSelfCtrl+8))return Life::invalid;
    if(At<const void*>(r.obj,kSelfCtrl)!=r.ctrl || (At<unsigned char>(r.obj,0x18)&4u))return Life::gone;
    return Life::live;
}
bool PinControl(const void* value) noexcept {
    if(!value || !Readable(value,16) || At<LONG>(value,8)<=0)return false;
    auto* c=const_cast<unsigned char*>(static_cast<const unsigned char*>(value));
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(c+12));return true;
}
void DropControl(const void* value) noexcept {
    if(!value)return;
    auto* c=const_cast<unsigned char*>(static_cast<const unsigned char*>(value));
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(c+12))==0) {
        const auto* table=At<const unsigned char*>(c,0);
        reinterpret_cast<void(__fastcall*)(void*)>(At<const void*>(table,8))(c);
    }
}
bool Pin(const ObjRef& r) noexcept {return Live(r)==Life::live && PinControl(r.ctrl);}
void Drop(ObjRef& r) noexcept {DropControl(r.ctrl);r={};}
template<class T>T Native(unsigned rva) noexcept {
#ifdef EDF6_ENEMY_BOSS_TEST
    return reinterpret_cast<T>(EnemyBossTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool NativeType(const Type& t) noexcept {
    if(!image || !Readable(image+t.table-8,(static_cast<std::size_t>(t.slots)+1)*8))return false;
    const auto* col=At<const unsigned char*>(image+t.table-8,0);
    if(!Readable(col,24) || At<unsigned>(col,0)!=1 || At<unsigned>(col,4)!=0 || At<unsigned>(col,8)!=0 ||
       image+At<unsigned>(col,20)!=col)return false;
    const auto* name=image+At<unsigned>(col,12)+16;const auto n=std::strlen(t.name);
    return Readable(name,n+7) && std::memcmp(name,".?AV",4)==0 && std::memcmp(name+4,t.name,n)==0 && std::memcmp(name+4+n,"@@",3)==0;
}
const Type* TypeOf(const ObjRef& r) noexcept {
    if(Live(r)!=Life::live || !Readable(r.obj,8))return nullptr;
    const auto* table=At<const unsigned char*>(r.obj,0);
    if(reinterpret_cast<std::uintptr_t>(table)<8 || !Readable(table-8,8))return nullptr;
    const void* col=At<const void*>(table-8,0);
    for(const auto& t:kTypes)if(NativeType(t) && col==At<const void*>(image+t.table-8,0)) {
        if(!Readable(table,static_cast<std::size_t>(t.slots)*8))return nullptr;
        for(unsigned i=0;i<t.slots;++i)if(i!=4 && i!=5 && i!=7) {
            const void* actual=At<const void*>(table,i*8);
            if(actual!=At<const void*>(image+t.table,i*8) &&
               (i!=14 || !EnemyControlOwnsTargetSelector(r.obj,actual)))return nullptr;
        }
        return &t;
    }
    return nullptr;
}
Root* FindRoot(const ObjRef& r) noexcept {for(auto& x:roots)if(x.ref && Same(x.ref,r))return &x;return nullptr;}
Root* RememberRoot(const ObjRef& r) noexcept {
    if(auto* x=FindRoot(r))return x;
    for(auto& x:roots)if(!x.ref && Pin(r)){x={};x.ref=r;return &x;}
    globalUnsafe=true;return nullptr;
}
void Unsafe(const ObjRef& r) noexcept {if(auto* x=RememberRoot(r))x->unsafe=true;else globalUnsafe=true;}
bool Sweep() noexcept {
    for(auto& x:meteors)if(x.ref) {
        const Life life=Live(x.ref);if(life==Life::invalid)return false;
        if(life==Life::gone){Drop(x.ref);Drop(x.root);x={};}
    }
    for(auto& b:bindings)if(b.serial) {
        if(!Readable(b.context,16) || !Readable(b.maker,16))return false;
        if(At<LONG>(b.context,8)<=0 || At<LONG>(b.maker,8)<=0 || Live(b.part)==Life::gone) {
            DropControl(b.context);DropControl(b.maker);Drop(b.part);Drop(b.root);b={};
        }
    }
    for(auto& x:roots)if(x.ref && !x.unsafe && !x.token && Live(x.ref)==Life::gone) {
        bool held=false;for(const auto& m:meteors)held=held || (m.ref && Same(m.root,x.ref));
        for(const auto& b:bindings)held=held || (b.serial && Same(b.root,x.ref));
        if(!held){Drop(x.ref);x={};}
    }
    return true;
}
unsigned MeteorRoom() noexcept {unsigned n=0;for(const auto& m:meteors)n+=m.ref ? 0u : 1u;return n;}
Meteor* FindMeteor(const ObjRef& r) noexcept {for(auto& x:meteors)if(x.ref && Same(x.ref,r))return &x;return nullptr;}
bool Weak(const unsigned char* p,std::size_t at,const ObjRef& r) noexcept {
    return Readable(p+at,16) && At<const void*>(p,at)==r.obj && At<const void*>(p,at+8)==r.ctrl;
}
bool PartRoot(const ObjRef& part,ObjRef* out) noexcept {
    const auto* p=TypeOf(part);if(!out || !p || p->root || p->meteor || !Readable(part.obj,0xAF8))return false;
    const ObjRef root{At<const void*>(part.obj,0xAE8),At<const void*>(part.obj,0xAF0)};
    const auto* rt=TypeOf(root);if(!rt || !rt->root)return false;*out=root;return true;
}
Binding* Bind(const ObjRef& root,const ObjRef& part,const unsigned char* launcher,const void* context,const void* maker) noexcept {
    for(auto& b:bindings)if(b.serial && Same(b.root,root) && Same(b.part,part) && b.launcher==launcher && b.context==context && b.maker==maker)return &b;
    if(serial==UINT64_MAX)return nullptr;
    for(auto& b:bindings)if(!b.serial) {
        if(!Pin(root))return nullptr;
        if(!Pin(part)){auto copy=root;Drop(copy);return nullptr;}
        if(!PinControl(context)){auto a=root,z=part;Drop(a);Drop(z);return nullptr;}
        if(!PinControl(maker)){DropControl(context);auto a=root,z=part;Drop(a);Drop(z);return nullptr;}
        b={root,part,launcher,context,maker,++serial};return &b;
    }
    Unsafe(root);return nullptr;
}
bool Emit(const ObjRef& member,const void* object,EmitterKind kind,std::uint64_t token,Emitter* out,unsigned cap,unsigned& used) noexcept {
    if(!object || !token || used==cap)return false;
    for(unsigned i=0;i<used;++i)if(out[i].object==object)return false;
    out[used++]={kind,member,object,token};return true;
}
bool PartEmitters(const ObjRef& part,Emitter* out,unsigned cap,unsigned& used) noexcept {
    ObjRef root{};if(!PartRoot(part,&root) || !Readable(part.obj,0xB78))return false;
    const auto* data=At<const unsigned char*>(part.obj,0xB60);const auto n=At<std::size_t>(part.obj,0xB70),reserved=At<std::size_t>(part.obj,0xB68);
    if(n>reserved || n>cap-used || (n && (!data || !Readable(data,n*16))))return false;
    for(std::size_t i=0;i<n;++i) {
        const auto* ctx=At<const unsigned char*>(data,i*16);const auto* control=At<const void*>(data,i*16+8);
        if(!ctx || !control || !Readable(control,16) || At<LONG>(control,8)<=0 || !Readable(ctx,0x220) ||
           !Weak(ctx,0x200,root) || !Weak(ctx,0x210,part))return false;
        const auto* launcher=ctx+0x20;const auto* maker=At<const unsigned char*>(launcher,0x20);const auto* makerControl=At<const void*>(launcher,0x28);
        if(!Weak(launcher,0x10,root) || !maker || !makerControl || !Readable(makerControl,16) || At<LONG>(makerControl,8)<=0 || !Readable(maker,8))return false;
        const auto* vt=At<const void*>(maker,0);
        if(vt!=image+0x17E6EE0 && vt!=image+0x17E6F08)return false;
        auto* b=Bind(root,part,launcher,control,makerControl);
        if(!b || !Emit(root,launcher,EmitterKind::liveOwnerTeam,b->serial,out,cap,used))return false;
    }
    return true;
}
bool Parts(const ObjRef&,ObjRef*,unsigned,unsigned&) noexcept;
bool RootEmitters(const ObjRef& root,Emitter* out,unsigned cap,unsigned& used) noexcept {
    // Share the ordinary Humanoid typed proof, including its retained laser map.
    if(!EnemyControlNativePvpCollectHumanoidWeapons(root,out,cap,&used))return false;
    ObjRef parts[controlled_pvp::kMaxMembers]{};unsigned count=0;
    if(!Parts(root,parts,controlled_pvp::kMaxMembers,count))return false;
    for(unsigned i=0;i<count;++i)if(!PartEmitters(parts[i],out,cap,used))return false;
    // Humanoid contact/knockback reads the root's +318 at 470ACB before 54E3F0.
    return Emit(root,root.obj,EmitterKind::liveOwnerLastTeam,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(root.ctrl)),out,cap,used);
}
bool CurrentEmitters(const ObjRef& member,Emitter* out,unsigned cap,unsigned& used) noexcept {
    const auto* type=TypeOf(member);if(!type)return false;
    if(type->root)return RootEmitters(member,out,cap,used);
    if(type->meteor)return Readable(member.obj,0x1448) && Emit(member,member.obj,EmitterKind::liveOwnerTeam,
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(member.ctrl)),out,cap,used);
    // A part owns containers, but their actual firing owner is the root.
    // Only the root collector publishes those derived bindings.
    ObjRef owner{};return PartRoot(member,&owner);
}
bool Add(const ObjRef& r,ObjRef* out,unsigned cap,unsigned& used) noexcept {
    if(Live(r)!=Life::live)return false;
    for(unsigned i=0;i<used;++i)if(out[i].obj==r.obj)return Same(out[i],r);
    if(used==cap)return false;out[used++]=r;return true;
}
bool Parts(const ObjRef& root,ObjRef* out,unsigned cap,unsigned& used) noexcept {
    if(!Readable(root.obj,0x17F8))return false;
    const auto* head=At<const unsigned char*>(root.obj,0x17E8);const auto count=At<std::size_t>(root.obj,0x17F0);
    if(count>cap-used || !Readable(head,16))return false;
    const auto* node=At<const unsigned char*>(head,0);const auto* previous=head;std::size_t walked=0;
    while(node!=head) {
        if(walked>=count || !Readable(node,0x50) || At<const void*>(node,8)!=previous)return false;
        const ObjRef part{At<const void*>(node,0x40),At<const void*>(node,0x48)};
        if(!part.obj && !part.ctrl)return false;
        const Life life=Live(part);if(life==Life::invalid)return false;
        if(life==Life::live) {
            const auto* t=TypeOf(part);ObjRef parent{};
            if(!t || t->root || t->meteor || !PartRoot(part,&parent) || !Same(parent,root) || !Add(part,out,cap,used))return false;
        }
        ++walked;previous=node;node=At<const unsigned char*>(node,0);
    }
    return walked==count && At<const void*>(head,8)==previous;
}
using PublishFn=void(__fastcall*)(void*,const ObjRef*,unsigned,unsigned,int,int);
using DeleteFn=void(__fastcall*)(void*);
bool SpawnCollect(void*,const ObjRef& root,ObjRef* m,unsigned mc,unsigned* mn,Emitter* e,unsigned ec,unsigned* en) noexcept {
    return EnemyControlBossPvpCollect(root,m,mc,mn,e,ec,en);
}
Life SpawnRead(void*,const Emitter& e,int* t) noexcept {return EnemyControlBossPvpRead(e,t);}
bool SpawnWrite(void*,const Emitter& e,int t) noexcept {return EnemyControlBossPvpWrite(e,t);}
bool SpawnMemberBaseline(void*,const ObjRef& r,const ObjRef& m,int t,int l,int* a,int* b) noexcept {return EnemyControlBossPvpBaseline(r,m,t,l,a,b);}
bool SpawnEmitterBaseline(void*,const Emitter& e,int t,int l,int* a) noexcept {return EnemyControlBossPvpBaseline(e,t,l,a);}
controlled_pvp::SpawnProof Proof() noexcept {
    return {{nullptr,SpawnCollect,SpawnRead,SpawnWrite,SpawnMemberBaseline,SpawnEmitterBaseline},1,1};
}
bool SetBaseline(const ObjRef& r,int team,int last) noexcept {
    if(!Team(team) || !team_native::TeamRow(last) || Live(r)!=Life::live || !Readable(r.obj,0x384))return false;
    auto* p=const_cast<void*>(r.obj);const bool registered=(At<unsigned>(p,0x380)&0x40u)!=0 && team!=-1;
    Native<void(__fastcall*)(void*,int,bool)>(kSetTeam)(p,team,registered);
    if(Live(r)!=Life::live || At<int>(p,kTeam)!=team)return false;
    Put<int>(p,0x318,last);return true;
}
void Deny(const ObjRef& root,const ObjRef& child,const ObjRef* argument) noexcept {
    Unsafe(root);
    ObjRef consumed{};
    if(argument && Readable(argument,sizeof(ObjRef),true)) {
        auto* temporary=const_cast<ObjRef*>(argument);consumed=*temporary;*temporary={};
    }
    // Clear the caller-owned by-value temporary before any operation can fault.
    // An outer SEH retry must never consume the same weak reference twice.
    if(consumed.ctrl)DropControl(consumed.ctrl);
    if(Live(child)==Life::live && !controlled_pvp::OwnsMember(child))Native<DeleteFn>(kDelete)(const_cast<void*>(child.obj));
    controlled_pvp::FaultForMember(root,controlled_pvp::Result::capacity);
}
void __fastcall OnMeteor(void* raw,const ObjRef* argument,unsigned kind,unsigned wave,int index,int total) noexcept {
    ObjRef root{},child{};bool owned=false;
    __try {
        root=ObjRef::Of(raw);if(argument && Readable(argument,sizeof(ObjRef)))child=*argument;
        owned=controlled_pvp::OwnsMember(root);
        const auto* type=TypeOf(root);const auto* born=TypeOf(child);
        Root* state=nullptr;
        bool ok=type && type->root && born && born->meteor && kind==2 && total>=20 && total<=40 && index>=0 && index<total && Sweep();
        if(ok){state=RememberRoot(root);ok=state && !state->unsafe && !globalUnsafe;}
        if(ok && owned) {
            if(index==0) {
                if(state->token){controlled_pvp::FinishSpawns(state->token);state->token=0;}
                ok=MeteorRoom()>=static_cast<unsigned>(total) && controlled_pvp::PrepareSpawns(root,Proof(),static_cast<unsigned>(total),&state->token);
                if(ok){state->wave=wave;state->total=static_cast<unsigned>(total);state->next=0;}
            }
            ok=ok && state->token && state->wave==wave && state->total==static_cast<unsigned>(total) && state->next==static_cast<unsigned>(index);
        }
        Meteor* record=nullptr;
        if(ok) {
            record=FindMeteor(child);if(record && !Same(record->root,root))ok=false;
            if(ok && !record)for(auto& m:meteors)if(!m.ref) {
                int team=At<int>(child.obj,kTeam),last=At<int>(child.obj,0x318);
                if(owned && !controlled_pvp::ReadBaselineForMember(root,&team,&last)){ok=false;break;}
                if(!Team(team) || !team_native::TeamRow(last) || !Pin(root)){ok=false;break;}
                if(!Pin(child)){auto copy=root;Drop(copy);ok=false;break;}
                m={root,child,team,last};record=&m;break;
            }
            ok=ok && record;
        }
        if(ok && owned) {
            // Native 44880D has already inherited the parent's temporary PVP team.
            // Establish the real birth baseline BEFORE generic AdoptSpawn captures it.
            record->adopted=true; // SpawnGroup must be the sole collector during AdoptSpawn
            ok=SetBaseline(child,record->team,record->last) && controlled_pvp::AdoptSpawn(state->token,root,child);
            if(!ok && !controlled_pvp::OwnsMember(child))record->adopted=false;
            if(ok && ++state->next==state->total){controlled_pvp::FinishSpawns(state->token);state->token=0;}
        }
        if(!ok) {
            Unsafe(root);
            if(owned){Deny(root,child,argument);return;}
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        globalUnsafe=true;
        if(owned){__try {Deny(root,child,argument);} __except(EXCEPTION_EXECUTE_HANDLER){}return;}
    }
    Native<PublishFn>(kPublish)(raw,argument,kind,wave,index,total);
}
} // boss_pvp_detail

bool EnemyControlBossPvpReady() noexcept {
    using namespace boss_pvp_detail;
    __try {
        if(!image)return false;
        for(const auto& a:kAnchors)if(!Readable(image+a.rva,a.size) || std::memcmp(image+a.rva,a.bytes,a.size)!=0)return false;
        for(const auto& t:kTypes)if(!NativeType(t) || At<const void*>(image+t.table,22*8)!=image+0x54ABD0)return false;
        if(!Readable(image+0x17E6EE0,32) || !Readable(image+0x17E6F08,32) ||
           At<const void*>(image+0x17E6EE0,8)!=image+0x6CF7D0 || At<const void*>(image+0x17E6EE0,16)!=image+0x6CFEF0 ||
           At<const void*>(image+0x17E6F08,8)!=image+0x6CF910 || At<const void*>(image+0x17E6F08,16)!=image+0x6CFF00)return false;
        return !installed || (Readable(image+kPublishCall,5) && std::memcmp(image+kPublishCall,installedCall,5)==0);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBossPvpSupports(const ObjRef& r) noexcept {
    __try {return boss_pvp_detail::TypeOf(r)!=nullptr;} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBossPvpInstall() noexcept {
    using namespace boss_pvp_detail;if(installed)return true;
    __try {
        if(!EnemyControlBossPvpReady() || !Readable(image+kPublishCall,sizeof(kPublishBytes)) ||
           std::memcmp(image+kPublishCall,kPublishBytes,sizeof(kPublishBytes))!=0)return false;
        bool changed=false;installed=RedirectCall(image+kPublishCall,reinterpret_cast<void*>(image+kPublish),reinterpret_cast<void*>(OnMeteor),changed);
        if(installed)std::memcpy(installedCall,image+kPublishCall,5);
        return installed;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBossPvpReset() noexcept {
    using namespace boss_pvp_detail;
    __try {
        for(const auto& x:roots)if(x.ref && controlled_pvp::OwnsMember(x.ref))return false;
        for(const auto& x:meteors)if(x.ref && controlled_pvp::OwnsMember(x.ref))return false;
        for(auto& x:roots){if(x.token)controlled_pvp::FinishSpawns(x.token);Drop(x.ref);x={};}
        for(auto& x:meteors){Drop(x.root);Drop(x.ref);x={};}
        for(auto& x:bindings){Drop(x.root);Drop(x.part);DropControl(x.context);DropControl(x.maker);x={};}
        globalUnsafe=false;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBossPvpPreflight(const ObjRef& root,unsigned count) noexcept {
    using namespace boss_pvp_detail;
    __try {
        const auto* t=TypeOf(root);if(!t || !t->root)return false;
        if(!controlled_pvp::OwnsMember(root))return true; // ordinary AI is never limited
        if(!EnemyControlBossPvpReady() || !installed || !Sweep() || globalUnsafe)return false;
        if(const auto* state=FindRoot(root))if(state->unsafe)return false;
        if(!count) {
            if(!Readable(root.obj,0x26B4))return false;
            const float stage=At<float>(root.obj,0x26B0);if(!std::isfinite(stage))return false;
            const float fraction=(stage-3.0f)*0.0625f;
            count=fraction<0 ? 20u : static_cast<unsigned>((fraction<1.0f ? fraction : 1.0f)*20.0f+20.0f);
        }
        return count>0 && count<=40 && MeteorRoom()>=count && controlled_pvp::CanPrepareSpawns(root,Proof(),count);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBossPvpCollect(const ObjRef& root,ObjRef* members,unsigned cap,unsigned* count,controlled_pvp::Emitter* emitters,unsigned ec,unsigned* emitted) noexcept {
    using namespace boss_pvp_detail;if(count)*count=0;if(emitted)*emitted=0;
    if(!count || !emitted || !members || !emitters || !cap || cap>controlled_pvp::kMaxMembers || ec>controlled_pvp::kMaxEmitters)return false;
    __try {
        const auto* t=TypeOf(root);if(!t || (!t->root && !t->meteor) || !EnemyControlBossPvpReady() || !installed || !Sweep())return false;
        unsigned used=0,eu=0;if(!Add(root,members,cap,used))return false;
        if(t->root) {
            const auto* state=FindRoot(root);
            // Faulted leases still need their previously proven write set to restore.
            if(((globalUnsafe || (state && state->unsafe)) && !controlled_pvp::OwnsMember(root)) || !Parts(root,members,cap,used))return false;
            for(const auto& m:meteors)if(m.ref && Same(m.root,root) && Live(m.ref)==Life::live && !(m.adopted && controlled_pvp::OwnsMember(m.ref)))
                if(!Add(m.ref,members,cap,used))return false;
        }
        // Native ordinary GOB children and the family-specific weak children are distinct trees.
        for(unsigned i=0;i<used;++i) {
            ObjRef ordinary[controlled_pvp::kMaxMembers]{};unsigned n=0;
            if(!enemy_pvp_native::CollectTree(members[i],ordinary,controlled_pvp::kMaxMembers,&n))return false;
            for(unsigned j=1;j<n;++j)if(!Add(ordinary[j],members,cap,used))return false;
        }
        bool done[controlled_pvp::kMaxMembers]{};
        for(unsigned i=0;i<used;++i) {
            if(done[i])continue;
            if(TypeOf(members[i])){if(!CurrentEmitters(members[i],emitters,ec,eu))return false;}
            else {
                ObjRef other[controlled_pvp::kMaxMembers]{};Emitter extra[controlled_pvp::kMaxEmitters]{};unsigned n=0,e=0;
                if(!EnemyControlNativePvpCollect(members[i],other,controlled_pvp::kMaxMembers,&n,extra,controlled_pvp::kMaxEmitters,&e) || e>ec-eu)return false;
                for(unsigned j=0;j<n;++j) {
                    if(!Add(other[j],members,cap,used))return false;
                    for(unsigned k=0;k<used;++k)if(Same(members[k],other[j]))done[k]=true;
                }
                for(unsigned j=0;j<e;++j)emitters[eu++]=extra[j];
            }
        }
        *count=used;*emitted=eu;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
controlled_pvp::Life EnemyControlBossPvpRead(const controlled_pvp::Emitter& e,int* team) noexcept {
    using namespace boss_pvp_detail;
    if(!team || !e.object || !e.incarnation || (e.kind!=EmitterKind::liveOwnerTeam && e.kind!=EmitterKind::liveOwnerLastTeam && !Cache(e.kind)))return Life::invalid;
    __try {
        if(EnemyControlNativePvpCacheRetained(e))return EnemyControlNativePvpReadRetainedCache(e,team);
        const Life life=Live(e.member);
        if(life!=Life::live) {
            if(Cache(e.kind) && e.storage.obj && e.storage.ctrl) {
                if(!Readable(e.storage.ctrl,16))return Life::invalid;
                return At<LONG>(e.storage.ctrl,8)<=0 ? Life::gone : Life::invalid;
            }
            return life;
        }
        if(!EnemyControlBossPvpReady())return Life::invalid;
        const auto* memberType=TypeOf(e.member);if(!memberType)return Life::invalid;
        if(!memberType->root && !memberType->meteor) {
            if(!Readable(e.member.obj,0xAF8))return Life::invalid;
            const ObjRef parent{At<const void*>(e.member.obj,0xAE8),At<const void*>(e.member.obj,0xAF0)};
            const Life parentLife=Live(parent);if(parentLife!=Life::live)return parentLife;
        }
        Emitter current[controlled_pvp::kMaxEmitters]{};unsigned used=0;
        if(!CurrentEmitters(e.member,current,controlled_pvp::kMaxEmitters,used))return Life::invalid;
        for(unsigned i=0;i<used;++i)if(current[i].object==e.object && current[i].incarnation==e.incarnation && current[i].kind==e.kind &&
            Same(current[i].member,e.member) && Same(current[i].storage,e.storage)) {
            ObjRef owner=e.member;const auto* t=TypeOf(e.member);if(!t)return Life::invalid;
            if(!t->root && !t->meteor && !PartRoot(e.member,&owner))return Life::invalid;
            if(Cache(e.kind) && !Readable(e.object,sizeof(int)))return Life::invalid;
            const int value=Cache(e.kind) ? At<int>(e.object,0) : At<int>(owner.obj,e.kind==EmitterKind::liveOwnerLastTeam ? 0x318 : kTeam);
            if(!Team(value))return Life::invalid;*team=value;return Life::live;
        }
        return Life::gone;
    } __except(EXCEPTION_EXECUTE_HANDLER){return Life::invalid;}
}
bool EnemyControlBossPvpWrite(const controlled_pvp::Emitter& e,int team) noexcept {
    __try {
        if(EnemyControlNativePvpCacheRetained(e))return EnemyControlNativePvpWriteRetainedCache(e,team);
        int actual=-1;if(!boss_pvp_detail::Team(team) || EnemyControlBossPvpRead(e,&actual)!=controlled_pvp::Life::live)return false;
        if(!boss_pvp_detail::Cache(e.kind))return actual==team;
        if(actual!=team) {
            if(!Readable(e.object,sizeof(int),true))return false;
            Put<int>(const_cast<void*>(e.object),0,team);
        }
        return EnemyControlBossPvpRead(e,&actual)==controlled_pvp::Life::live && actual==team;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBossPvpBaseline(const ObjRef& root,const ObjRef& member,int rootTeam,int rootLast,int* team,int* last) noexcept {
    using namespace boss_pvp_detail;
    if(!team || !last || !Team(rootTeam) || !team_native::TeamRow(rootLast))return false;
    __try {
        const auto* t=TypeOf(member);if(!t)return false;
        if(t->meteor)if(const auto* m=FindMeteor(member)) {
            if(Same(m->root,root) || Same(member,root)){*team=m->team;*last=m->last;return true;}return false;
        }
        if(Same(root,member)){*team=rootTeam;*last=rootLast;return true;}
        ObjRef owner{};if(!t->root && !t->meteor && PartRoot(member,&owner) && Same(owner,root)) {
            *team=rootTeam;*last=rootLast;return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBossPvpBaseline(const controlled_pvp::Emitter& e,int ownerTeam,int ownerLast,int* team) noexcept {
    using namespace boss_pvp_detail;if(!team)return false;
    __try {
        if(EnemyControlNativePvpCacheRetained(e)) {
            int actual=-1;
            return EnemyControlNativePvpReadRetainedCache(e,&actual)==Life::live && controlled_pvp::ReadEmitterBaseline(e,team);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    if(!Team(ownerTeam) || !team_native::TeamRow(ownerLast))return false;
    int actual=-1;if(EnemyControlBossPvpRead(e,&actual)!=Life::live)return false;
    if(Cache(e.kind) && !controlled_pvp::HasTemporaryTeam(e.member)){*team=actual;return true;}
    __try {
        const auto* t=TypeOf(e.member);if(!t)return false;
        if(!t->root && !t->meteor) {
            ObjRef root{};int original=-1,last=-1;
            if(!PartRoot(e.member,&root) || !controlled_pvp::ReadBaselineForMember(root,&original,&last))return false;
            *team=original;return Team(original);
        }
        *team=(e.kind==EmitterKind::liveOwnerLastTeam || e.kind==EmitterKind::ownerLastTeamCache) ? ownerLast : ownerTeam;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace crew
