// Production NPC adapter + actual controlled-PVP core, with native SetTeam as the only gameplay stand-in.
#include "../src/pvp_controlled_npc.cpp"
#include "../src/pvp_controlled.cpp"
#include <cstdio>
#include <cstdlib>

namespace npc_fixture {
using namespace crew;using namespace crew::controlled_pvp;
const void* certifiedTable=nullptr;const void* certifiedThink=nullptr;bool originalChain=true;
unsigned checks=0,failed=0,nativeWrites=0;int ownerTeam=1;const void* denied=nullptr;const void* watched=nullptr;unsigned watchedReads=0;
void Check(bool pass,const char* expression,int line){++checks;if(!pass){++failed;std::fprintf(stderr,"%d: %s\n",line,expression);}}
#define CHECK(x) npc_fixture::Check(!!(x),#x,__LINE__)
void __fastcall DeleteControl(void*) noexcept{}
void* controlTable[2]={nullptr,reinterpret_cast<void*>(&DeleteControl)};
struct Object {
    alignas(16) unsigned char b[0x4000]{};alignas(16) unsigned char c[0x20]{};
    ObjRef Ref()const noexcept{return {b,c};}
};
Object actor{},parentActor{},sceneModel{},weapons[2]{},bullets[32]{};
unsigned char gobHead[0x20]{},heldHeads[2][0x20]{},nodes[2][16][0x20]{};
unsigned char combos[2][0xC0]{};
const void* roster[2]{};const void* scene[2]{};unsigned char identities[0x100]{};
void Identity(Object& object) {
    object=Object{};Put<void*>(object.b,kSelf,object.b);Put<void*>(object.b,kSelfCtrl,object.c);
    Put<void*>(object.c,0,controlTable);Put<LONG>(object.c,8,1);Put<LONG>(object.c,12,1);
}
void Type(const npc_pvp_detail::Type& t,unsigned slot,unsigned baseDescriptor=0x2006450) {
    const unsigned col=0x2060000+slot*0x100,descriptor=col+0x40;
    Put<void*>(image+t.table-8,0,image+col);Put<unsigned>(image+col,0,1);Put<unsigned>(image+col,12,descriptor);Put<unsigned>(image+col,20,col);
    auto* n=image+descriptor+16;std::memcpy(n,".?AV",4);const auto size=std::strlen(t.name);std::memcpy(n+4,t.name,size);std::memcpy(n+4+size,"@@",3);
    const unsigned h=0x2070000+slot*0x100,array=h+0x20,base=h+0x40;Put<unsigned>(image+col,16,h);Put<unsigned>(image+h,8,1);Put<unsigned>(image+h,12,array);Put<unsigned>(image+array,0,base);
    Put<unsigned>(image+base,0,baseDescriptor);Put<int>(image+base,12,-1);
    for(unsigned i=0;i<t.count;++i)Put<void*>(image+t.table,i*8,image+t.slots[i]);
}
void Origin() {
    for(const auto& anchor:npc_pvp_detail::kAnchors)std::memcpy(image+anchor.rva,anchor.bytes,anchor.size);
    for(unsigned i=0;i<3;++i)Type(npc_pvp_detail::kActors[i],i);for(unsigned i=0;i<4;++i)Type(npc_pvp_detail::kWeapons[i],3+i,0x2045768);Type(npc_pvp_detail::kShieldBullet,7,0x2022E80);
    for(unsigned i=0;i<sizeof(npc_pvp_detail::kSharedWeapons)/sizeof(npc_pvp_detail::kSharedWeapons[0]);++i)Type(npc_pvp_detail::kSharedWeapons[i],10+i,0x2045768);
    constexpr unsigned col=0x2000A00,h=col+0x30,array=col+0x50,base=col+0x70;
    Put<void*>(image+0x179E048-8,0,image+col);Put<unsigned>(image+col,0,1);Put<unsigned>(image+col,16,h);Put<unsigned>(image+col,20,col);
    Put<unsigned>(image+h,8,1);Put<unsigned>(image+h,12,array);Put<unsigned>(image+array,0,base);
    Put<unsigned>(image+base,0,0x2022E80);Put<int>(image+base,12,-1);
}
void __fastcall SetTeam(void* o,int team,bool registered) noexcept {
    ++nativeWrites;CHECK(o==actor.b || o==parentActor.b);Put<int>(o,kTeam,team);if(team!=-1)Put<int>(o,0x318,team);
    const unsigned f=At<unsigned>(o,0x380);Put<unsigned>(o,0x380,registered ? f|0x40u : f&~0x40u);
}
void Reset(unsigned actorClass=0,unsigned fireCount=1) {
    CHECK(!HasLeases());ownerTeam=1;nativeWrites=0;denied=watched=nullptr;watchedReads=0;
    Identity(actor);Identity(parentActor);Identity(sceneModel);for(auto& w:weapons)Identity(w);for(auto& b:bullets)Identity(b);
    std::memset(heldHeads,0,sizeof(heldHeads));std::memset(nodes,0,sizeof(nodes));std::memset(gobHead,0,sizeof(gobHead));
    Put<void*>(actor.b,0,image+npc_pvp_detail::kActors[actorClass].table);Put<int>(actor.b,kTeam,5);Put<int>(actor.b,0x318,6);Put<unsigned>(actor.b,0x380,0xA040u);
    Put<void*>(actor.b,0x550,gobHead);Put<void*>(gobHead,0,gobHead);Put<void*>(gobHead,8,gobHead);
    Put<void*>(actor.b,0x1950,roster);Put<std::size_t>(actor.b,0x1958,2);Put<std::size_t>(actor.b,0x1960,1);
    Put<void*>(actor.b,0x48,scene);Put<std::size_t>(actor.b,0x50,2);Put<std::size_t>(actor.b,0x58,1);
    std::memset(identities,0x5A,sizeof(identities));Put<void*>(actor.b,0x1ED0,identities);Put<void*>(actor.b,0x1ED8,identities+0x40);
    for(unsigned i=0;i<2;++i) {
        auto& w=weapons[i];roster[i]=scene[i]=w.b;Put<void*>(w.b,0,image+npc_pvp_detail::kBasic.table);Put<void*>(w.b,0x120,actor.b);Put<void*>(w.b,0x38,actor.b);Put<void*>(w.b,0x838,actor.b);Put<void*>(w.b,0x840,actor.c);
        Put<unsigned>(w.b,0x208,0);Put<int>(w.b,0x214,4);Put<int>(w.b,0x368,static_cast<int>(fireCount));Put<int>(w.b,kTeam,0x44444444);
        Put<void*>(w.b,0xF20,heldHeads[i]);Put<void*>(heldHeads[i],0,heldHeads[i]);Put<void*>(heldHeads[i],8,heldHeads[i]);
    }
    Type({"FixtureModel",0x1760048},8);Put<void*>(sceneModel.b,0,image+0x1760048);
    for(auto& b:bullets) {
        Put<void*>(b.b,0,image+0x179E048);Put<int>(b.b,0xC30,1);Put<int>(b.b,0xB40,3);Put<int>(b.b,0x894,2);
        for(unsigned at:{0xAE8u,0x880u}){Put<void*>(b.b,at,actor.b);Put<void*>(b.b,at+8,actor.c);}
        Put<int>(b.b,kTeam,0x55555555);
    }
}
void Held(unsigned count,unsigned which=0) {
    auto* head=heldHeads[which];auto* ownNodes=nodes[which];Put<std::size_t>(weapons[which].b,0xF28,count);
    Put<void*>(head,0,count ? ownNodes[0] : head);Put<void*>(head,8,count ? ownNodes[count-1] : head);
    for(unsigned i=0;i<count;++i) {
        Put<void*>(ownNodes[i],0,i+1<count ? ownNodes[i+1] : head);Put<void*>(ownNodes[i],8,i ? ownNodes[i-1] : head);
        Put<void*>(ownNodes[i],0x10,bullets[which*16+i].b);Put<void*>(ownNodes[i],0x18,bullets[which*16+i].c);
    }
}
SpawnProof Proof(const char* name="AssultSoldier",unsigned held=1,unsigned weaponsCount=1) {
    SpawnProof p{};CHECK(PvpControlledNpcProof(name,weaponsCount,held,&p));CHECK(p.maxMembers==1);CHECK(p.maxEmitters==weaponsCount*(1+2*held));return p;
}
bool Collect(const SpawnProof& p,Emitter* out,unsigned cap,unsigned* emitted) {
    ObjRef members[kMaxMembers]{};unsigned count=99;*emitted=99;
    const bool okay=p.hooks.collect(p.hooks.user,actor.Ref(),members,kMaxMembers,&count,out,cap,emitted);
    CHECK(okay ? count==1 && npc_pvp_detail::Same(members[0],actor.Ref()) : count==0 && *emitted==0);return okay;
}
void BoundsAndTypedProbe() {
    Reset();SpawnProof p{};CHECK(!PvpControlledNpcProof("Ranger",1,1,&p));CHECK(!p.maxMembers && !p.hooks.user);
    CHECK(!PvpControlledNpcProof("AssultSoldier",0,1,&p));CHECK(!PvpControlledNpcProof("AssultSoldier",3,1,&p));CHECK(!PvpControlledNpcProof("AssultSoldier",1,0,&p));CHECK(!PvpControlledNpcProof("AssultSoldier",1,16,&p));
    p=Proof();Emitter out[kMaxEmitters]{};unsigned n=0;CHECK(Collect(p,out,kMaxEmitters,&n) && n==1);CHECK(out[0].kind==EmitterKind::ownerTeamCache && npc_pvp_detail::Same(out[0].storage,weapons[0].Ref()));
    const auto initial=actor;const auto weapon=weapons[0];for(unsigned i=0;i<10;++i)CHECK(Probe(actor.Ref(),p.hooks));
    CHECK(!std::memcmp(&initial,&actor,sizeof(actor)) && !std::memcmp(&weapon,&weapons[0],sizeof(weapon)) && nativeWrites==0);
    Held(1);CHECK(Collect(p,out,kMaxEmitters,&n) && n==3);CHECK(!Collect(p,out,2,&n));CHECK(At<LONG>(bullets[0].c,12)==1);
    const auto snapshot=bullets[0];CHECK(Probe(actor.Ref(),p.hooks));CHECK(!std::memcmp(&snapshot,&bullets[0],sizeof(snapshot)));
    Put<void*>(bullets[0].b,0xAF0,weapons[1].c);CHECK(!Probe(actor.Ref(),p.hooks));Put<void*>(bullets[0].b,0xAF0,actor.c);
    Put<void*>(weapons[0].b,0x120,weapons[1].b);CHECK(!Probe(actor.Ref(),p.hooks));Put<void*>(weapons[0].b,0x120,actor.b);
    Put<void*>(weapons[0].b,0x840,weapons[1].c);CHECK(!Probe(actor.Ref(),p.hooks));Put<void*>(weapons[0].b,0x840,actor.c);
    Put<void*>(weapons[0].b,0x38,weapons[1].b);CHECK(!Probe(actor.Ref(),p.hooks));Put<void*>(weapons[0].b,0x38,actor.b);
    Emitter stale=out[0];Put<void*>(weapons[0].b,kSelfCtrl,weapons[1].c);int staleTeam=-1;CHECK(p.hooks.readEmitter(p.hooks.user,stale,&staleTeam)==Life::gone);Put<void*>(weapons[0].b,kSelfCtrl,weapons[0].c);
    Put<std::size_t>(actor.b,0x558,1);CHECK(!Probe(actor.Ref(),p.hooks));Put<std::size_t>(actor.b,0x558,0);
    Put<std::size_t>(actor.b,0x1A10,1);CHECK(!Probe(actor.Ref(),p.hooks));Put<std::size_t>(actor.b,0x1A10,0);
    Put<std::size_t>(actor.b,0x58,2);CHECK(!Probe(actor.Ref(),p.hooks));scene[1]=sceneModel.b;CHECK(Probe(actor.Ref(),p.hooks));scene[1]=weapons[1].b;Put<std::size_t>(actor.b,0x58,1);
    Put<unsigned>(weapons[0].b,0x208,1);CHECK(!Probe(actor.Ref(),p.hooks));Put<unsigned>(weapons[0].b,0x208,0);
    Put<void*>(nodes[0][0],0,nodes[0][0]);CHECK(!Collect(p,out,kMaxEmitters,&n));Held(1);
    Put<unsigned char>(bullets[0].b,0x18,4);CHECK(Collect(p,out,kMaxEmitters,&n) && n==1);Put<unsigned char>(bullets[0].b,0x18,0);
    Put<LONG>(bullets[0].c,8,0);CHECK(Collect(p,out,kMaxEmitters,&n) && n==1);Put<LONG>(bullets[0].c,8,1);
    Emitter forged=out[0];forged.incarnation++;int team=-1;CHECK(p.hooks.readEmitter(p.hooks.user,forged,&team)==Life::invalid);
    Reset(1,15);p=Proof("PaleWing",15);Held(15);CHECK(Collect(p,out,kMaxEmitters,&n) && n==31);CHECK(Probe(actor.Ref(),p.hooks));
    Put<int>(weapons[0].b,0x368,16);CHECK(!Probe(actor.Ref(),p.hooks));Put<int>(weapons[0].b,0x368,15);Held(16);CHECK(!Probe(actor.Ref(),p.hooks));
    Reset(0,10);p=Proof("AssultSoldier",10);Held(10);CHECK(Collect(p,out,kMaxEmitters,&n) && n==21);CHECK(Probe(actor.Ref(),p.hooks));
}
void StrictSlotsAndKnownThink() {
    Reset();auto p=Proof();auto* actorTable=image+npc_pvp_detail::kActors[0].table;auto* weaponTable=image+npc_pvp_detail::kWeapons[0].table;
    const void* actorLast=At<const void*>(actorTable,93*8);Put<const void*>(actorTable,93*8,image+0xABCD);CHECK(!Probe(actor.Ref(),p.hooks));Put<const void*>(actorTable,93*8,actorLast);
    const void* weaponLast=At<const void*>(weaponTable,40*8);Put<const void*>(weaponTable,40*8,image+0xABCD);CHECK(!Probe(actor.Ref(),p.hooks));Put<const void*>(weaponTable,40*8,weaponLast);
    const void* nativeThink=At<const void*>(actorTable,7*8);Put<const void*>(actorTable,7*8,image+0xAABB);
    CHECK(!Probe(actor.Ref(),p.hooks));certifiedTable=actorTable;certifiedThink=image+0xAABB;CHECK(Probe(actor.Ref(),p.hooks));
    Lease lease{};CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));ownerTeam=2;CHECK(Frame(lease));CHECK(Restore(&lease));
    originalChain=false;CHECK(!Probe(actor.Ref(),p.hooks));originalChain=true;Put<const void*>(actorTable,7*8,image+0xAABC);CHECK(!Probe(actor.Ref(),p.hooks));
    Put<const void*>(actorTable,7*8,nativeThink);certifiedTable=certifiedThink=nullptr;CHECK(Probe(actor.Ref(),p.hooks));
}
void HeavyWeapon(unsigned index,unsigned kind) {
    Put<void*>(weapons[index].b,0,image+npc_pvp_detail::kWeapons[kind].table);
    if(kind==2) {
        std::memset(combos,0,sizeof(combos));Put<void*>(weapons[index].b,0x1638,combos);Put<std::size_t>(weapons[index].b,0x1640,2);Put<std::size_t>(weapons[index].b,0x1648,2);Put<int>(weapons[index].b,0x1650,-1);
    }
}
void HeavyLoadoutsAndShieldFilter() {
    Reset(2);auto p=Proof("HeavyArmor",1,2);Put<std::size_t>(actor.b,0x1960,2);Put<std::size_t>(actor.b,0x58,2);HeavyWeapon(0,1);HeavyWeapon(1,2);Held(1);Held(1,1);
    Emitter out[6]{};unsigned count=0;CHECK(Collect(p,out,6,&count) && count==6);CHECK(Probe(actor.Ref(),p.hooks));
    Put<unsigned char>(combos[1],0x28,1);CHECK(!Probe(actor.Ref(),p.hooks));Put<unsigned char>(combos[1],0x28,0);
    Lease lease{};CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));ownerTeam=2;CHECK(Frame(lease));CHECK(At<int>(weapons[1].b,0x214)==2 && At<int>(bullets[16].b,0x894)==2);CHECK(Restore(&lease));
    CHECK(At<int>(weapons[1].b,0x214)==4 && At<int>(bullets[16].b,0xB40)==3 && At<int>(bullets[16].b,0x894)==2);
    Reset(2);p=Proof("HeavyArmor",1,2);Put<std::size_t>(actor.b,0x1960,2);Put<std::size_t>(actor.b,0x58,2);HeavyWeapon(0,2);HeavyWeapon(1,3);Held(1);Held(1,1);
    Put<void*>(bullets[16].b,0,image+npc_pvp_detail::kShieldBullet.table);Put<void*>(bullets[16].b,0xE98,actor.b);Put<void*>(bullets[16].b,0xEA0,actor.c);Put<int>(bullets[16].b,0xEF0,6);
    Put<int>(bullets[16].b,0xB40,0x66666666);Put<int>(bullets[16].b,0x894,0x77777777);Put<void*>(bullets[16].b,0xAF0,nullptr);Put<void*>(bullets[16].b,0x888,nullptr);
    CHECK(Collect(p,out,6,&count) && count==5);CHECK(Probe(actor.Ref(),p.hooks));
    Put<void*>(bullets[16].b,0xEA0,weapons[0].c);CHECK(!Probe(actor.Ref(),p.hooks));Put<void*>(bullets[16].b,0xEA0,actor.c);
    CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));CHECK(At<int>(bullets[16].b,0xEF0)==1 && At<int>(bullets[16].b,0xB40)==0x66666666 && At<int>(bullets[16].b,0x894)==0x77777777);
    ownerTeam=2;CHECK(Frame(lease));CHECK(At<int>(bullets[16].b,0xEF0)==2);Put<LONG>(actor.c,8,0);denied=actor.b;CHECK(Restore(&lease));denied=nullptr;
    CHECK(At<int>(bullets[16].b,0xEF0)==6 && At<int>(weapons[1].b,0x214)==4 && At<int>(bullets[16].b,0xB40)==0x66666666);
    Reset(2);p=Proof("HeavyArmor",1,2);Put<std::size_t>(actor.b,0x1960,2);Put<std::size_t>(actor.b,0x58,2);HeavyWeapon(0,1);HeavyWeapon(1,3);
    CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));Put<void*>(bullets[16].b,0,image+npc_pvp_detail::kShieldBullet.table);Put<void*>(bullets[16].b,0xE98,actor.b);Put<void*>(bullets[16].b,0xEA0,actor.c);Put<int>(bullets[16].b,0xEF0,1);Held(1,1);
    CHECK(Frame(lease));CHECK(Restore(&lease));CHECK(At<int>(bullets[16].b,0xEF0)==4); // exact source weapon baseline
}
void NeutralTypedApi() {
    Reset();Held(1);Emitter out[4]{};unsigned n=0;ownerTeam=-1;
    CHECK(PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && n==3 && !HasLeases());int team=-1;
    CHECK(PvpNativeReadWeaponEmitter(out[0],&team)==Life::live && team==4);
    const unsigned writes=nativeWrites;for(unsigned i=0;i<n;++i)CHECK(PvpNativeWriteWeaponEmitter(out[i],2));CHECK(nativeWrites==writes && !HasLeases());
    CHECK(At<int>(weapons[0].b,0x214)==2 && At<int>(bullets[0].b,0x894)==2);
    Put<LONG>(actor.c,8,0);denied=actor.b;CHECK(PvpNativeWriteWeaponEmitter(out[0],4));CHECK(PvpNativeWriteWeaponEmitter(out[1],3));CHECK(PvpNativeWriteWeaponEmitter(out[2],6));denied=nullptr;
    CHECK(At<int>(weapons[0].b,0x214)==4 && At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==6);
    Reset();Held(1);n=1;CHECK(!PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,3,&n) && n==1);n=0;
    Put<void*>(weapons[0].b,0x840,weapons[1].c);CHECK(!PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && n==0);Put<void*>(weapons[0].b,0x840,actor.c);
    Put<std::size_t>(weapons[0].b,0xF28,513);CHECK(!PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && n==0);Held(1);
    // All exact original WeaponBase primaries share this native list, regardless of inheritance from BasicShoot.
    for(const auto& type:npc_pvp_detail::kSharedWeapons) {
        Put<void*>(weapons[0].b,0,image+type.table);Put<unsigned>(weapons[0].b,0x208,1);Put<int>(weapons[0].b,0x368,1000);Put<int>(bullets[0].b,0xC30,0);n=0;
        CHECK(PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && n==1);
        CHECK(PvpNativeReadWeaponEmitter(out[0],&team)==Life::live);
        CHECK(PvpNativeWriteWeaponEmitter(out[0],1) && PvpNativeWriteWeaponEmitter(out[0],2) && PvpNativeWriteWeaponEmitter(out[0],4));
        CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);
        const void* last=At<const void*>(image+type.table,40*8);Put<void*>(image+type.table,40*8,image+0xABCD);unsigned rejected=0;
        CHECK(!PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&rejected) && rejected==0);Put<const void*>(image+type.table,40*8,last);
    }
    Reset();Put<void*>(weapons[0].b,0,image+0x123456);n=0;CHECK(!PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && !n);
    Reset();HeavyWeapon(0,3);Put<void*>(bullets[0].b,0,image+npc_pvp_detail::kShieldBullet.table);Put<void*>(bullets[0].b,0xE98,actor.b);Put<void*>(bullets[0].b,0xEA0,actor.c);Put<int>(bullets[0].b,0xEF0,6);Put<int>(bullets[0].b,0xC30,0);Held(1);n=0;
    CHECK(PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && n==1);
    CHECK(PvpNativeWriteWeaponEmitter(out[0],1) && PvpNativeWriteWeaponEmitter(out[0],2) && PvpNativeWriteWeaponEmitter(out[0],4));CHECK(At<int>(bullets[0].b,0xEF0)==6);
    Put<int>(bullets[0].b,0xC30,1);n=0;CHECK(PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && n==2);CHECK(out[1].object==bullets[0].b+0xEF0);CHECK(PvpNativeWriteWeaponEmitter(out[1],2));
    Put<LONG>(weapons[0].c,8,0);Put<LONG>(actor.c,8,0);denied=actor.b;CHECK(PvpNativeWriteWeaponEmitter(out[1],6));denied=nullptr;CHECK(At<int>(bullets[0].b,0xEF0)==6 && !HasLeases());
}
void OnlyNativeKeepaliveFollows() {
    Reset();Held(1);auto p=Proof();Emitter out[4]{};unsigned n=0;Put<int>(bullets[0].b,0xC30,0);
    CHECK(Collect(p,out,4,&n) && n==1);Lease lease{};CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));ownerTeam=2;CHECK(Frame(lease));
    CHECK(At<int>(weapons[0].b,0x214)==2 && At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);CHECK(Restore(&lease));
    // A stopped mode-1 object is also outside the native keepalive path.
    Reset();Held(1);p=Proof();Put<unsigned>(bullets[0].b,0xC34,1u<<14);CHECK(Collect(p,out,4,&n) && n==1);
    CHECK(!PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && n==1); // duplicate Weapon214 is rejected without publishing a partial append
    n=0;CHECK(PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && n==1);
    CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));ownerTeam=2;CHECK(Frame(lease));CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);CHECK(Restore(&lease));
    // Non-followed nodes still require their complete typed source identity.
    Put<int>(bullets[0].b,0xC30,0);Put<void*>(bullets[0].b,0xAF0,weapons[1].c);n=0;CHECK(!PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && n==0);
    Reset();Held(1);n=0;CHECK(PvpNativeWeaponEmitters(actor.Ref(),weapons[0].b,out,4,&n) && n==3);
    CHECK(PvpNativeWriteWeaponEmitter(out[1],1) && PvpNativeWriteWeaponEmitter(out[2],1));CHECK(PvpNativeWriteWeaponEmitter(out[1],2) && PvpNativeWriteWeaponEmitter(out[2],2));
    CHECK(At<int>(bullets[0].b,0xB40)==2 && At<int>(bullets[0].b,0x894)==2);CHECK(PvpNativeWriteWeaponEmitter(out[1],3) && PvpNativeWriteWeaponEmitter(out[2],6));
    CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==6);
}
void RestoreDistinctFields() {
    Reset();Held(1);auto p=Proof();Lease lease{};unsigned char identityCopy[sizeof(identities)]{};std::memcpy(identityCopy,identities,sizeof(identities));
    CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));CHECK(At<int>(actor.b,kTeam)==1 && At<int>(weapons[0].b,0x214)==1 && At<int>(bullets[0].b,0xB40)==1 && At<int>(bullets[0].b,0x894)==1);
    CHECK(At<int>(weapons[0].b,kTeam)==0x44444444 && At<int>(bullets[0].b,kTeam)==0x55555555);
    CHECK(At<LONG>(actor.c,12)==2 && At<LONG>(weapons[0].c,12)==2 && At<LONG>(bullets[0].c,12)==3);
    ownerTeam=2;CHECK(Frame(lease));CHECK(At<int>(actor.b,kTeam)==2 && At<int>(weapons[0].b,0x214)==2 && At<int>(bullets[0].b,0x894)==2);
    CHECK(Restore(&lease));CHECK(At<int>(actor.b,kTeam)==5 && At<int>(actor.b,0x318)==6 && At<int>(weapons[0].b,0x214)==4);
    CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2 && At<unsigned>(actor.b,0x380)==0xA040u);
    CHECK(At<LONG>(actor.c,12)==1 && At<LONG>(weapons[0].c,12)==1 && At<LONG>(bullets[0].c,12)==1);
    CHECK(!std::memcmp(identityCopy,identities,sizeof(identities)) && At<const void*>(actor.b,0x1ED0)==identities && At<const void*>(actor.b,0x1ED8)==identities+0x40);
}
void DynamicBulletAndIndependentLifetime() {
    Reset();auto p=Proof();Lease lease{};CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));
    Put<int>(bullets[0].b,0xB40,1);Put<int>(bullets[0].b,0x894,1);Held(1);CHECK(Frame(lease));CHECK(Restore(&lease));
    CHECK(At<int>(bullets[0].b,0xB40)==4 && At<int>(bullets[0].b,0x894)==4); // weapon baseline 4, body baseline 5
    Reset();Held(1);p=Proof();CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));
    Put<std::size_t>(actor.b,0x1960,0);Put<std::size_t>(actor.b,0x58,0);CHECK(Frame(lease));
    CHECK(At<int>(weapons[0].b,0x214)==4 && At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);CHECK(Restore(&lease));
    Reset();Held(1);p=Proof();CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));
    Emitter out[3]{};unsigned count=0;CHECK(Collect(p,out,3,&count));CHECK(count==3);
    Put<LONG>(actor.c,8,0);denied=actor.b;watched=actor.b;watchedReads=0;CHECK(Restore(&lease));
    CHECK(watchedReads==0 && At<int>(weapons[0].b,0x214)==4 && At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);denied=watched=nullptr;
    Reset();Held(1);p=Proof();CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));
    Put<LONG>(weapons[0].c,8,0);Put<std::size_t>(actor.b,0x1960,0);Put<std::size_t>(actor.b,0x58,0);CHECK(Restore(&lease));CHECK(At<int>(bullets[0].b,0xB40)==3 && At<int>(bullets[0].b,0x894)==2);
    Reset();Held(1);p=Proof();CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));
    denied=bullets[0].b+0x894;CHECK(!Restore(&lease) && lease.id!=0);denied=nullptr;CHECK(Restore(&lease));CHECK(At<int>(bullets[0].b,0x894)==2);
}
bool ParentCollect(void*,const ObjRef& root,ObjRef* members,unsigned cap,unsigned* n,Emitter*,unsigned,unsigned* e) noexcept {
    if(n)*n=0;if(e)*e=0;if(!cap || !n || !e || !npc_pvp_detail::Same(root,parentActor.Ref()))return false;members[0]=root;*n=1;return true;
}
void ActualSpawnAdoption() {
    Reset();auto p=Proof();Put<int>(parentActor.b,kTeam,5);Put<int>(parentActor.b,0x318,5);Put<unsigned>(parentActor.b,0x380,0x40u);
    Hooks parent{};parent.collect=&ParentCollect;Lease lease{};CHECK(Acquire(parentActor.Ref(),"owner1",parent,&lease));
    CHECK(CanPrepareSpawns(parentActor.Ref(),p,20));std::uint64_t token=0;CHECK(PrepareSpawns(parentActor.Ref(),p,1,&token));
    const auto identitySnapshot=actor;CHECK(AdoptSpawn(token,parentActor.Ref(),actor.Ref()));FinishSpawns(token);
    CHECK(OwnsMember(actor.Ref()) && At<int>(actor.b,kTeam)==1 && At<int>(weapons[0].b,0x214)==1);
    Put<int>(bullets[0].b,0xB40,1);Put<int>(bullets[0].b,0x894,1);Held(1);CHECK(Frame(lease));
    ownerTeam=2;CHECK(Frame(lease));CHECK(At<int>(parentActor.b,kTeam)==2 && At<int>(actor.b,kTeam)==2 && At<int>(bullets[0].b,0x894)==2);
    CHECK(Restore(&lease));CHECK(At<int>(actor.b,kTeam)==5 && At<int>(weapons[0].b,0x214)==4 && At<int>(bullets[0].b,0xB40)==4);
    CHECK(At<const void*>(actor.b,0x1ED0)==At<const void*>(identitySnapshot.b,0x1ED0) && At<const void*>(actor.b,0x1ED8)==At<const void*>(identitySnapshot.b,0x1ED8));
}
void SpawnBudgetAndModeOff() {
    Reset();auto p=Proof();Lease lease{};CHECK(Acquire(actor.Ref(),"owner1",p.hooks,&lease));
    CHECK(CanPrepareSpawns(actor.Ref(),p,1));std::uint64_t token=0;CHECK(PrepareSpawns(actor.Ref(),p,1,&token) && token);FinishSpawns(token);
    auto huge=p;huge.maxEmitters=kMaxEmitters;CHECK(!CanPrepareSpawns(actor.Ref(),huge,1));
    ownerTeam=0;CHECK(Frame(lease));CHECK(At<int>(weapons[0].b,0x214)==4);Put<int>(weapons[0].b,0x214,6);CHECK(Frame(lease));
    ownerTeam=2;CHECK(Frame(lease));CHECK(At<int>(weapons[0].b,0x214)==2);CHECK(Restore(&lease));CHECK(At<int>(weapons[0].b,0x214)==6);
}
}
namespace crew {bool NpcOwnsNativeThink(const void* table,const void* function) noexcept{return npc_fixture::originalChain && table==npc_fixture::certifiedTable && function==npc_fixture::certifiedThink;}unsigned char* image=nullptr;void Log(const char*,...) noexcept{}int PvpSupportTeam(const char*) noexcept{return npc_fixture::ownerTeam;}}
namespace edf {
bool Readable(const void* p,std::size_t size,bool write) noexcept {
    if(p==npc_fixture::watched)++npc_fixture::watchedReads;
    if(!p || p==npc_fixture::denied || reinterpret_cast<std::uintptr_t>(p)+size<reinterpret_cast<std::uintptr_t>(p))return false;
    auto cursor=reinterpret_cast<std::uintptr_t>(p),end=cursor+size;
    while(cursor<end) {
        MEMORY_BASIC_INFORMATION v{};if(!VirtualQuery(reinterpret_cast<const void*>(cursor),&v,sizeof(v)) || v.State!=MEM_COMMIT || (v.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        if(write && !(v.Protect&(PAGE_READWRITE|PAGE_EXECUTE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_WRITECOPY)))return false;
        const auto next=reinterpret_cast<std::uintptr_t>(v.BaseAddress)+v.RegionSize;if(next<=cursor)return false;cursor=next;
    }
    return true;
}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept{return true;}
}
int main() {
    using namespace npc_fixture;image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)return 2;Origin();
    unsigned char thunk[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};const auto target=reinterpret_cast<std::uintptr_t>(&SetTeam);std::memcpy(thunk+2,&target,8);std::memcpy(image+kSetTeam,thunk,sizeof(thunk));FlushInstructionCache(GetCurrentProcess(),image+kSetTeam,sizeof(thunk));
    BoundsAndTypedProbe();StrictSlotsAndKnownThink();HeavyLoadoutsAndShieldFilter();NeutralTypedApi();OnlyNativeKeepaliveFollows();RestoreDistinctFields();DynamicBulletAndIndependentLifetime();ActualSpawnAdoption();SpawnBudgetAndModeOff();
    VirtualFree(image,0,MEM_RELEASE);std::printf("pvp_controlled_npc_test: %u checks, %u failed\n",checks,failed);return failed ? 1 : 0;
}
