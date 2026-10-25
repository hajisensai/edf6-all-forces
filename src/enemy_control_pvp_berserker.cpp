// Steam 678CCB46: scene parts, followers and live held BulletBase beams are distinct native graphs.
#include "enemy_control_pvp_berserker.h"
#include "enemy_control_pvp_native.h"
#include "enemy_control_pvp_tree.h"
#include "enemy_control_gun_factory.h"
#include "enemy_control_walking_parts.h"
#include "memory.h"
#include "team_native_logic.h"
#include <cstring>
namespace crew {
bool EnemyControlOwnsTargetSelector(const void*,const void*) noexcept;
#ifdef EDF6_ENEMY_BERSERKER_TEST
void* EnemyBerserkerTestFunction(unsigned) noexcept;
#endif
namespace berserker_pvp_detail {
using controlled_pvp::Emitter;using controlled_pvp::EmitterKind;using controlled_pvp::Life;
enum class Kind:unsigned {large,middle,typeA,bommer,grapple,shooter,rocket,bomb,wire};
struct Type {unsigned table,slots;const char* name;Kind kind;};
constexpr Type kTypes[]={
 {0x17ADA78,79,"EDF6_Berserker_Large",Kind::large},{0x17AE088,79,"EDF6_Berserker_Middle",Kind::middle},
 {0x17AE968,79,"EDF6_Berserker_TypeA",Kind::typeA},{0x17AED50,79,"EDF6_Berserker_TypeBommer",Kind::bommer},
 {0x17AF200,79,"EDF6_Berserker_TypeGrapple",Kind::grapple},{0x17AF9F8,79,"EDF6_Berserker_TypeShooter",Kind::shooter},
 {0x17AE460,45,"EDF6_Berserker_Equipment_RocketPunch",Kind::rocket},{0x17AD518,55,"EDF6_Berserker_Equipment_Bomb",Kind::bomb},
 {0x17AD8F8,12,"EDF6_Berserker_Equipment_GrappleWire",Kind::wire},
};
constexpr unsigned kRootLimit=256,kBornLimit=4096,kBindingLimit=8192,kMaxActions=256,kMaxMuzzles=256;
constexpr unsigned kShoot=0x33FCB0,kRocketCtor=0x351940,kBombCtor=0x348830,kFactory=0x1194280,kDefaultFactory=0x351660,kDelete=0x118A1B0;
struct Anchor {unsigned at,size;unsigned char bytes[32];};
constexpr Anchor kAnchors[]={
 {0x119436F,12,{0x48,0x8B,0x8B,0xA8,0x0C,0x00,0x00,0x48,0x85,0xC9,0x74,0x2A}},
 {0x11943A2,3,{0xFF,0x50,0x10}},
 {0x1195A30,14,{0x48,0x8B,0x81,0xD0,0x03,0x00,0x00,0x4C,0x8B,0x02,0x49,0x89,0x40,0x20}},
 {0x1195A50,18,{0x48,0x8D,0x8B,0xB0,0x03,0x00,0x00,0x4C,0x8B,0xC7,0x48,0x8B,0x11,0xE8,0x8E,0xAB,0xFF,0xFF}},
 {0x1197050,19,{0x48,0x83,0xEC,0x28,0x48,0x8B,0xC1,0x48,0x8B,0x89,0xA8,0x0C,0x00,0x00,0x48,0x85,0xC9,0x74,0x1F}},
 {0x1197063,31,{0x8B,0x80,0xB0,0x0C,0x00,0x00,0x4C,0x8D,0x44,0x24,0x30,0x48,0x89,0x54,0x24,0x40,0x48,0x8D,0x54,0x24,0x40,0x89,0x44,0x24,0x30,0x48,0x8B,0x01,0xFF,0x50,0x10}},
 {0x1197082,5,{0x48,0x83,0xC4,0x28,0xC3}},
 {0x1194927,12,{0x48,0x8B,0x8F,0xA8,0x0C,0x00,0x00,0x48,0x85,0xC9,0x74,0x22}},
 {0x1194952,8,{0xFF,0x50,0x10,0x48,0x8B,0x4C,0x24,0x38}},
 {0x3528A4,18,{0x48,0x8B,0x4D,0xD0,0x48,0x81,0xC1,0xA0,0x0D,0x00,0x00,0x48,0x8D,0x95,0xE0,0x03,0x00,0x00}},
 {0x35292F,18,{0x48,0x8B,0x4D,0xD0,0x48,0x81,0xC1,0xE0,0x0D,0x00,0x00,0x48,0x8D,0x95,0x20,0x04,0x00,0x00}},
 {0x34045F,11,{0xF6,0x43,0x18,0x04,0x74,0x4A,0x49,0x8B,0xDE,0x48,0x8B}},
 {0x118A273,12,{0x80,0x4B,0x18,0x04,0x4C,0x8B,0x05,0xDA,0x86,0xF2,0x00,0x0F}},
 {0x1194966,11,{0xF6,0x41,0x18,0x04,0x49,0x8B,0xFC,0x48,0x0F,0x44,0xF9}},
 {0x3574FE,7,{0x4C,0x8B,0xC0,0x48,0x85,0xC0,0x75}},
 {0x348649,11,{0xB9,0xE0,0x14,0x00,0x00,0xE8,0x5D,0xFF,0xF8,0x00,0x48}},
 {0x6D34BE,7,{0x83,0xBB,0x30,0x0C,0x00,0x00,0x01}},
 {0x6D34E4,7,{0x49,0x89,0x8D,0xB0,0x00,0x00,0x00}},
 {0x6D34F2,7,{0x49,0x89,0x85,0xB8,0x00,0x00,0x00}},
 {0x232015,20,{0x8B,0x83,0x00,0x0A,0x00,0x00,0xF3,0x0F,0x10,0x9B,0x38,0x0A,0x00,0x00,0x89,0x83,0x54,0x07,0x00,0x00}},
 {0x232D20,12,{0x48,0x63,0x8E,0x00,0x0A,0x00,0x00,0x83,0xF9,0xFF,0x74,0x37}},
 {0x352683,21,{0x4C,0x89,0xBF,0xD0,0x05,0x00,0x00,0x48,0x8B,0x8F,0xD8,0x05,0x00,0x00,0x4C,0x89,0xA7,0xD8,0x05,0x00,0x00}},
 {0x348AFD,21,{0x48,0x8B,0x43,0x38,0x8B,0xCA,0x48,0x85,0xC0,0x74,0x0B,0x48,0x8B,0x4B,0x30,0xF0,0xFF,0x40,0x0C,0x48,0x8B}},
 {0x34D280,4,{0x48,0x89,0x57,0x10}}, {0x34D322,11,{0x48,0x89,0x07,0x48,0x8B,0x5F,0x08,0x48,0x89,0x77,0x08}},
 {0x35022A,21,{0x48,0x89,0x87,0x60,0x1A,0,0,0x48,0x8B,0x9F,0x68,0x1A,0,0,0x48,0x89,0xB7,0x68,0x1A,0,0}},
 {0x6D107B,21,{0x48,0x89,0x8E,0xB0,0x02,0,0,0x48,0x8B,0x8E,0xB8,0x02,0,0,0x48,0x89,0x96,0xB8,0x02,0,0}},
 {0x6D2707,18,{0x4C,0x8B,0xAE,0x78,0x02,0,0,0x4C,0x69,0xB6,0x88,0x02,0,0,0xD0,0,0,0}},
 {0x6D2A46,12,{0x8B,0x83,0x18,0x03,0,0,0x89,0x85,0x10,0x02,0,0}},
 {0x33FCF6,14,{0x4C,0x63,0xEA,0x49,0xC1,0xE5,0x08,0x4C,0x03,0xA9,0xF0,0x17,0,0}},
 {0x351EA8,6,{0x8B,0x90,0x18,0x03,0,0}}, {0x351FDC,13,{0x41,0x8B,0x80,0x14,0x03,0,0,0x89,0x85,0xF0,0x04,0,0}},
 {0x34A2C3,9,{0x8B,0x87,0x18,0x03,0,0,0x89,0x45,0xA4}}, {0x34A330,9,{0x8B,0x80,0x18,0x03,0,0,0x89,0x45,0xA4}},
 {0x34DE17,9,{0x8B,0x86,0x18,0x03,0,0,0x89,0x45,0x84}},
 {0x118AF35,12,{0x48,0x89,0x5F,0x38,0x4C,0x8B,0x43,0x58,0x48,0x8B,0x53,0x50}},
};
struct HookSite {unsigned at,target;unsigned char after[5]{};bool changed=false;const void* function=nullptr;};
HookSite sites[]={
 {0x340424,kRocketCtor},{0x351EB1,kSetTeam},{0x3525A3,kFactory},{0x3526B9,kDefaultFactory},{0x348663,kBombCtor},{0x340441,0x1195A20},{0x34044E,0x1197050},{0x3574F9,0x11945E0},{0x3528B6,0x353290},{0x352941,0x353420},{0x3516E6,0x1197050},
};
struct Root {ObjRef ref{};bool unsafe=false;};
struct Born {ObjRef parent{},child{};int team=-1,last=-1,parentTeam=-1,parentLast=-1;bool adopted=false,controlledBirth=false,denied=false;};
struct Binding {ObjRef owner{},holder{},bullet{};const void* unit=nullptr;const void* unitControl=nullptr;std::size_t slot=0;int filter=-1,damage=-1;std::uint64_t serial=0;bool lastSource=false;};
struct BombBirth {ObjRef root{};std::uint64_t token=0;bool owned=false,allowed=false,failed=false;};
struct Notification {ObjRef object{};void* callback=nullptr;void* function=nullptr;unsigned metadata=0;};
struct ShotChild {ObjRef ref{},bullet{};void* manager=nullptr;bool complete=false,activate=false;Notification notice{};};
struct Shot {ObjRef root{};std::uint64_t token=0;unsigned expected=0,created=0,tracked=0;bool owned=false,failed=false;ObjRef denied{};ShotChild children[controlled_pvp::kMaxMembers]{};};
struct Construction {void* memory=nullptr;void* result=nullptr;bool entered=false,completed=false,rejected=false,creatingDamage=false;};
Root roots[kRootLimit]{};Born born[kBornLimit]{};Binding bindings[kBindingLimit]{};
unsigned char bulletPublicationCode[28]{};const unsigned char* bulletPublicationRelay=nullptr;unsigned char bulletPublicationAfter[13]{};bool bulletPublicationChanged=false;
unsigned char publicationCode[24]{};const unsigned char* publicationRelay=nullptr;const void* constructingRocket=nullptr;Construction* construction=nullptr;Shot* active=nullptr;BombBirth* activeBomb=nullptr;ObjRef pendingDenied{};unsigned char publicationAfter[9]{};bool publicationChanged=false;bool installed=false,attempted=false,globalUnsafe=false;unsigned char* admittedImage=nullptr;std::uint64_t serial=0;
bool Same(const ObjRef& a,const ObjRef& b) noexcept {return a.obj==b.obj && a.ctrl==b.ctrl;}
bool Team(int t) noexcept {return team_native::TeamOrClosing(t);}
bool RootKind(Kind k) noexcept {return k<=Kind::shooter;}
bool Body(Kind k) noexcept {return k!=Kind::wire;}
Life Identity(const ObjRef& r) noexcept {
 if(!r.obj || !r.ctrl || !Readable(r.ctrl,16))return Life::invalid;
 if(At<LONG>(r.ctrl,8)<=0)return Life::gone;
 if(!Readable(r.obj,0x38))return Life::invalid;
 if(At<const void*>(r.obj,kSelfCtrl)!=r.ctrl || (At<unsigned char>(r.obj,0x18)&4u))return Life::gone;
 return At<const void*>(r.obj,kSelf)==r.obj ? Life::live : Life::invalid;
}
bool HoldControl(const void* p) noexcept {
 if(!p || !Readable(p,16,true) || At<LONG>(p,8)<=0 || At<LONG>(p,12)<=0 || At<LONG>(p,12)==0x7FFFFFFF)return false;
 InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(const_cast<void*>(p))+12));return true;
}
bool DropControl(const void* p) noexcept {
 if(!p)return true;if(!Readable(p,16,true) || At<LONG>(p,12)<=0)return false;
 auto* c=static_cast<unsigned char*>(const_cast<void*>(p));const auto* vt=At<void* const*>(c,0);
 if(At<LONG>(p,12)==1 && (!Readable(vt,16) || !vt[1]))return false;
 if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(c+12))==0)reinterpret_cast<void(__fastcall*)(void*)>(vt[1])(c);return true;
}
bool Hold(const ObjRef& r) noexcept {return Identity(r)==Life::live && HoldControl(r.ctrl);}
bool Drop(ObjRef& r) noexcept {if(!DropControl(r.ctrl))return false;r={};return true;}
bool HoldStrong(const ObjRef& r) noexcept {
 if(Identity(r)!=Life::live || !Readable(r.ctrl,16,true))return false;
 const auto* vt=At<void* const*>(r.ctrl,0);if(!Readable(vt,16) || !vt[0] || !vt[1])return false;
 auto* uses=reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(const_cast<void*>(r.ctrl))+8);LONG value=*uses;
 while(value>0 && value<0x7FFFFFFF){const LONG was=InterlockedCompareExchange(uses,value+1,value);if(was==value)return true;value=was;}return false;
}
bool DropStrong(ObjRef& ref) {
 if(!ref)return true;if(!Readable(ref.ctrl,16,true) || At<LONG>(ref.ctrl,8)<=0)return false;
 const auto* vt=At<void* const*>(ref.ctrl,0);if(!Readable(vt,16) || !vt[0] || !vt[1])return false;
 const ObjRef old=ref;ref={};
 if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(const_cast<void*>(old.ctrl))+8))==0) {
  reinterpret_cast<void(__fastcall*)(const void*)>(vt[0])(old.ctrl);return DropControl(old.ctrl);
 }
 return true;
}
template<class T>T Native(unsigned at) noexcept {
#ifdef EDF6_ENEMY_BERSERKER_TEST
 return reinterpret_cast<T>(EnemyBerserkerTestFunction(at));
#else
 return reinterpret_cast<T>(image+at);
#endif
}
bool NativeType(const Type& t) noexcept {
 if(!image || !Readable(image+t.table-8,(static_cast<std::size_t>(t.slots)+1)*8))return false;
 const auto* c=At<const unsigned char*>(image+t.table-8,0);
 if(!Readable(c,24) || At<unsigned>(c,0)!=1 || At<unsigned>(c,4)!=0 || At<unsigned>(c,8)!=0 || image+At<unsigned>(c,20)!=c)return false;
 const auto* n=image+At<unsigned>(c,12)+16;const auto length=std::strlen(t.name);
 return Readable(n,length+7) && !std::memcmp(n,".?AV",4) && !std::memcmp(n+4,t.name,length) && !std::memcmp(n+4+length,"@@",3);
}
const Type* TypeOf(const ObjRef& r) noexcept {
 if(Identity(r)!=Life::live)return nullptr;
 const auto* v=At<const unsigned char*>(r.obj,0);if(reinterpret_cast<std::uintptr_t>(v)<8 || !Readable(v-8,8))return nullptr;
 for(const auto& t:kTypes)if(NativeType(t) && At<const void*>(v-8,0)==At<const void*>(image+t.table-8,0)) {
  if(!Readable(v,t.slots*8))return nullptr;
  for(unsigned j=0;j<t.slots;++j)if(At<const void*>(v,j*8)!=At<const void*>(image+t.table,j*8)) {
   if(RootKind(t.kind) && (j==4 || j==5 || j==7))continue;
   if(RootKind(t.kind) && j==14 && EnemyControlOwnsTargetSelector(r.obj,At<const void*>(v,j*8)))continue;
   return nullptr;
  }
  return &t;
 }
 return nullptr;
}
bool BulletType(const ObjRef& b) noexcept {
 if(Identity(b)!=Life::live || !Readable(b.obj,0xB44))return false;
 const auto* v=At<const unsigned char*>(b.obj,0);if(reinterpret_cast<std::uintptr_t>(v)<8 || !Readable(v-8,8))return false;
 const auto* c=At<const unsigned char*>(v-8,0);
 if(!Readable(c,24) || At<unsigned>(c,0)!=1 || At<unsigned>(c,4)!=0 || image+At<unsigned>(c,20)!=c)return false;
 const auto* h=image+At<unsigned>(c,16);if(!Readable(h,16))return false;
 const unsigned count=At<unsigned>(h,8);if(!count || count>256)return false;
 const auto* list=image+At<unsigned>(h,12);if(!Readable(list,count*4))return false;
 for(unsigned i=0;i<count;++i) {
  const auto* descriptor=image+At<unsigned>(list,i*4);if(!Readable(descriptor,24))return false;
  if(At<unsigned>(descriptor,0)==0x2022E80 && At<int>(descriptor,8)==0 && At<int>(descriptor,12)==-1)return true;
 }
 return false;
}
bool Weak(const void* data,std::size_t at,ObjRef* out) noexcept {
 if(!out || !Readable(static_cast<const unsigned char*>(data)+at,16))return false;
 *out={At<const void*>(data,at),At<const void*>(data,at+8)};
 if(!out->obj && !out->ctrl)return true;
 const Life life=Identity(*out);if(life==Life::gone){*out={};return true;}return life==Life::live;
}
bool Owner(const void* object,std::size_t at,const ObjRef& owner) noexcept {
 return Readable(static_cast<const unsigned char*>(object)+at,16) && At<const void*>(object,at)==owner.obj && At<const void*>(object,at+8)==owner.ctrl;
}

Root* FindRoot(const ObjRef& r) noexcept {for(auto& x:roots)if(x.ref && Same(x.ref,r))return &x;return nullptr;}
Root* RememberRoot(const ObjRef& r) noexcept {
 if(auto* x=FindRoot(r))return x;
 for(auto& x:roots)if(!x.ref && Hold(r)){x.ref=r;return &x;}globalUnsafe=true;return nullptr;
}
void Fault(const ObjRef& r,controlled_pvp::Result why=controlled_pvp::Result::nativeFailure) noexcept {
 if(auto* x=RememberRoot(r))x->unsafe=true;else globalUnsafe=true;controlled_pvp::FaultForMember(r,why);
}
Born* Recorded(const ObjRef& r) noexcept {for(auto& x:born)if(x.child && Same(x.child,r))return &x;return nullptr;}
bool Original(const ObjRef& r,int* t,int* l) noexcept {
 if(!controlled_pvp::HasTemporaryTeam(r) && Identity(r)==Life::live && Readable(r.obj,0x31C)) {
  *t=At<int>(r.obj,kTeam);*l=At<int>(r.obj,0x318);return Team(*t) && Team(*l);
 }
 if(controlled_pvp::ReadBaselineForMember(r,t,l))return Team(*t) && Team(*l);
 if(const auto* b=Recorded(r)){*t=b->team;*l=b->last;return Team(*t) && Team(*l);}
 if(controlled_pvp::OwnsMember(r) || Identity(r)!=Life::live || !Readable(r.obj,0x31C))return false;
 *t=At<int>(r.obj,kTeam);*l=At<int>(r.obj,0x318);return Team(*t) && Team(*l);
}
bool Sweep() noexcept {
 for(auto& x:bindings)if(x.serial) {
  const Life b=Identity(x.bullet);
  if(b==Life::invalid)return false;
  if(b==Life::gone) {
   if(!Drop(x.owner) || !Drop(x.holder) || !Drop(x.bullet) || !DropControl(x.unitControl))return false;x={};
  }
 }
 for(auto& x:born)if(x.child && Identity(x.child)==Life::gone && !controlled_pvp::OwnsMember(x.child)) {
  if(!Drop(x.child) || !Drop(x.parent))return false;x={};
 }
 for(auto& x:roots)if(x.ref && Identity(x.ref)==Life::gone && !x.unsafe) {
  bool named=false;for(const auto& b:born)named=named || (b.child && Same(b.parent,x.ref));
  if(!named){if(!Drop(x.ref))return false;x={};}
 }
 return true;
}
unsigned Room() noexcept {if(!Sweep())return 0;unsigned n=0;for(const auto& b:born)n+=b.child ? 0u : 1u;return n;}
bool Record(const ObjRef& parent,const ObjRef& child,int bodyTeam,int bodyLast,int parentTeam,int parentLast,bool controlled) noexcept {
 if(Identity(parent)!=Life::live || Identity(child)!=Life::live || Same(parent,child) || !Team(bodyTeam) || !Team(bodyLast) || !Team(parentTeam) || !Team(parentLast))return false;
 if(auto* b=Recorded(child))return Same(b->parent,parent);
 if(!Sweep())return false;Born* free=nullptr;
 for(auto& b:born){if(b.child.obj==child.obj && b.child.ctrl!=child.ctrl)return false;if(!b.child && !free)free=&b;}
 if(!free || !Hold(parent))return false;if(!Hold(child)){ObjRef copy=parent;Drop(copy);return false;}
 *free={parent,child,bodyTeam,bodyLast,parentTeam,parentLast,false,controlled,false};return true;
}
bool PartParent(const ObjRef& child,ObjRef* out) noexcept {
 const auto* type=TypeOf(child);if(!out || !type || RootKind(type->kind) || type->kind==Kind::wire)return false;
 const auto* old=Recorded(child);const auto* sceneParent=At<const void*>(child.obj,0x38);
 if(type->kind==Kind::bomb) {
  if(!Readable(static_cast<const unsigned char*>(child.obj)+0xFC0,16))return false;
  *out={At<const void*>(child.obj,0xFC0),At<const void*>(child.obj,0xFC8)};
  if(!out->obj && !out->ctrl && old)*out=old->parent;
 } else if(old)*out=old->parent;
 else {if(!sceneParent || !Readable(sceneParent,0x38))return false;*out=ObjRef::Of(sceneParent);}
 if(old && !Same(old->parent,*out))return false;
 if(sceneParent && sceneParent!=out->obj)return false;
 const Life life=Identity(*out);
 if(life==Life::gone)return old && Same(old->parent,*out);
 if(life!=Life::live)return false;
 const auto* root=TypeOf(*out);return root && RootKind(root->kind);
}
bool Add(const ObjRef& r,ObjRef* out,unsigned cap,unsigned& used,bool main=true) noexcept {
 if(Identity(r)!=Life::live)return false;
 if(main && used)if(const auto* b=Recorded(r))if(b->adopted && controlled_pvp::OwnsMember(r))return true;
 for(unsigned i=0;i<used;++i)if(out[i].obj==r.obj)return Same(out[i],r);
 if(used==cap)return false;out[used++]=r;return true;
}
bool Emit(const Emitter& e,Emitter* out,unsigned cap,unsigned& used) noexcept {
 for(unsigned i=0;i<used;++i)if(out[i].object==e.object)return out[i].kind==e.kind && Same(out[i].member,e.member) && out[i].incarnation==e.incarnation;
 if(used==cap)return false;out[used++]=e;return true;
}
bool Derived(const ObjRef& owner,const void* object,EmitterKind kind,const void* token,Emitter* out,unsigned cap,unsigned& used) noexcept {
 return object && token && Emit({kind,owner,object,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(token))},out,cap,used);
}
Binding* Bind(const ObjRef& owner,const ObjRef& holder,const ObjRef& bullet,const void* unit,const void* unitControl,std::size_t slot,bool lastSource) noexcept {
 for(auto& b:bindings)if(b.serial && Same(b.owner,owner) && Same(b.holder,holder) && Same(b.bullet,bullet) && b.unit==unit && b.unitControl==unitControl && b.slot==slot && b.lastSource==lastSource) {
  if(!controlled_pvp::HasTemporaryTeam(owner)){b.filter=At<int>(bullet.obj,0xB40);b.damage=At<int>(bullet.obj,0x894);if(!Team(b.filter) || !Team(b.damage))return nullptr;}
  return &b;
 }
 if(serial==UINT64_MAX || !Sweep())return nullptr;
 int filter=At<int>(bullet.obj,0xB40),damage=At<int>(bullet.obj,0x894);
 if(controlled_pvp::HasTemporaryTeam(owner)) {
  int team=-1,last=-1;if(!Original(owner,&team,&last))return nullptr;filter=damage=lastSource ? last : team;
 }
 if(!Team(filter) || !Team(damage))return nullptr;
 Binding* target=nullptr;for(auto& b:bindings)if(!b.serial){target=&b;break;}if(!target)return nullptr;
 if(!Hold(owner))return nullptr;if(!Hold(holder)){ObjRef r=owner;Drop(r);return nullptr;}
 if(!Hold(bullet)){ObjRef a=owner,z=holder;Drop(a);Drop(z);return nullptr;}
 if(unitControl && !HoldControl(unitControl)){ObjRef a=owner,z=holder,q=bullet;Drop(a);Drop(z);Drop(q);return nullptr;}
 *target={owner,holder,bullet,unit,unitControl,slot,filter,damage,++serial,lastSource};return target;
}
bool HeldBullet(const ObjRef& owner,const ObjRef& holder,const ObjRef& bullet,const void* unit,const void* control,std::size_t slot,bool lastSource,Emitter* out,unsigned cap,unsigned& used) noexcept {
 if(!BulletType(bullet) || !Owner(bullet.obj,0xAE8,owner) || !Owner(bullet.obj,0x880,owner))return false;
 auto* b=Bind(owner,holder,bullet,unit,control,slot,lastSource);if(!b)return false;
 const auto kind=lastSource ? EmitterKind::ownerLastTeamCache : EmitterKind::ownerTeamCache;
 return Emit({kind,owner,static_cast<const unsigned char*>(bullet.obj)+0xB40,b->serial,bullet},out,cap,used) &&
  Emit({kind,owner,static_cast<const unsigned char*>(bullet.obj)+0x894,b->serial,bullet},out,cap,used);
}
bool Battery(const ObjRef& owner,const unsigned char* battery,const void* control,Emitter* out,unsigned cap,unsigned& used) noexcept {
 if(!battery && !control)return true;
 if(!control || !Readable(control,16) || At<LONG>(control,8)<=0 || battery!=static_cast<const unsigned char*>(control)+16 || !Readable(battery,0x6C0) || !Owner(battery,0x2B0,owner))return false;
 const auto count=At<std::size_t>(battery,0x288),allocated=At<std::size_t>(battery,0x280);const auto* data=At<const unsigned char*>(battery,0x278);
 if(count>allocated || count>kMaxMuzzles || (count && (!data || !Readable(data,count*0xD0))))return false;
 if(!Derived(owner,battery,EmitterKind::liveOwnerLastTeam,control,out,cap,used))return false;
 for(std::size_t i=0;i<count;++i) {
  ObjRef bullet{};if(!Weak(data+i*0xD0,0xB0,&bullet))return false;
  if(bullet && (!Readable(static_cast<const unsigned char*>(bullet.obj)+0xC30,4) || (At<int>(bullet.obj,0xC30)==1 && !HeldBullet(owner,owner,bullet,battery,control,i,true,out,cap,used))))return false;
 }
 return true;
}
bool RootEmitters(const ObjRef& root,const Type& type,Emitter* out,unsigned cap,unsigned& used) noexcept {
 const auto* p=static_cast<const unsigned char*>(root.obj);if(!Readable(p,0x1A60))return false;
 if(type.kind==Kind::large) {
  if(!Readable(p,0x1AC0))return false;const auto n=At<std::size_t>(p,0x1AB8),capacity=At<std::size_t>(p,0x1AB0);const auto* data=At<const unsigned char*>(p,0x1AA8);
  if(n>capacity || n>256 || (n && (!data || !Readable(data,n*16))))return false;
  for(std::size_t i=0;i<n;++i) {
   const auto* gun=At<const unsigned char*>(data,i*16);const auto* control=At<const unsigned char*>(data,i*16+8);
   if(!control || !Readable(control,16) || At<LONG>(control,8)<=0 || gun!=control+16 || !Readable(gun,0x90) || At<const void*>(gun,0x10)!=root.obj ||
      !Battery(root,At<const unsigned char*>(gun,0),At<const void*>(gun,8),out,cap,used))return false;
  }
 } else if(type.kind==Kind::middle || type.kind==Kind::shooter) {
  if(!Readable(p+0x1A60,16) || !Battery(root,At<const unsigned char*>(p,0x1A60),At<const void*>(p,0x1A68),out,cap,used))return false;
 }
 // PartsShootList inherits actual parent+314; native stomps/damage fallbacks read +318.
 const auto actions=At<std::size_t>(p,0x1800),capacity=At<std::size_t>(p,0x17F8);const auto* table=At<const unsigned char*>(p,0x17F0);
 if(actions>capacity || actions>kMaxActions || (actions && (!table || !Readable(table,actions*0x100))))return false;
 return Derived(root,p+0x17E8,EmitterKind::liveOwnerTeam,root.ctrl,out,cap,used) && Derived(root,p,EmitterKind::liveOwnerLastTeam,root.ctrl,out,cap,used);
}
bool SceneParts(const ObjRef& root,ObjRef* out,unsigned cap,unsigned& used) noexcept {
 if(!Readable(root.obj,0x60))return false;const auto n=At<std::size_t>(root.obj,0x58),capacity=At<std::size_t>(root.obj,0x50);const auto* data=At<const unsigned char*>(root.obj,0x48);
 if(n>capacity || n>kBornLimit || (n && (!data || !Readable(data,n*8))))return false;
 for(std::size_t i=0;i<n;++i) {
  const auto* object=At<const void*>(data,i*8);if(!object || !Readable(object,0x38))return false;
  ObjRef child=ObjRef::Of(object);const Life life=Identity(child);if(life==Life::gone)continue;const auto* type=TypeOf(child);if(!type || At<const void*>(child.obj,0x38)!=root.obj)return false;
  if(type->kind==Kind::wire)continue;
  if(type->kind!=Kind::rocket && type->kind!=Kind::bomb)return false;
  ObjRef parent{};if(!PartParent(child,&parent) || !Same(parent,root))return false;
  if(!Recorded(child)) {
   if(controlled_pvp::OwnsMember(root))return false;
   int pt=-1,pl=-1;if(!Original(root,&pt,&pl) || !Record(root,child,At<int>(child.obj,kTeam),At<int>(child.obj,0x318),pt,pl,false))return false;
  }
  if(!Add(child,out,cap,used))return false;
 }
 return true;
}
bool RootParts(const ObjRef& root,const Type& t,ObjRef* out,unsigned cap,unsigned& used) noexcept {
 if(!SceneParts(root,out,cap,used))return false;
 if(t.kind==Kind::bommer) {
  if(!Readable(root.obj,0x1A80))return false;const auto n=At<std::size_t>(root.obj,0x1A78),capacity=At<std::size_t>(root.obj,0x1A70);const auto* data=At<const unsigned char*>(root.obj,0x1A68);
  if(n>capacity || n>kBornLimit || (n && (!data || !Readable(data,n*16))))return false;
  for(std::size_t i=0;i<n;++i) {ObjRef child{};if(!Weak(data,i*16,&child))return false;if(!child)continue;
   const auto* ct=TypeOf(child);ObjRef parent{};if(!ct || ct->kind!=Kind::bomb || !PartParent(child,&parent) || !Same(parent,root) || !Add(child,out,cap,used))return false;
  }
 }
 for(const auto& b:born)if(b.child && Same(b.parent,root) && Identity(b.child)==Life::live && !b.denied && !Add(b.child,out,cap,used))return false;
 return true;
}
bool CurrentEmitters(const ObjRef& member,Emitter* out,unsigned cap,unsigned& used) noexcept {
 const auto* t=TypeOf(member);if(!t || !Body(t->kind))return false;
 if(!RootKind(t->kind))return true; // Held bullets belong to the real parent, not to the logical part.
 if(!RootEmitters(member,*t,out,cap,used))return false;
 for(auto& b:born)if(b.child && Same(b.parent,member) && Identity(b.child)==Life::live && !b.denied) {
  const auto* ct=TypeOf(b.child);if(!ct)return false;
  if(ct->kind==Kind::rocket) {
   ObjRef bullet{};if(!Weak(b.child.obj,0x5D0,&bullet))return false;
   if(bullet && !HeldBullet(member,b.child,bullet,nullptr,nullptr,0,false,out,cap,used))return false;
  }
 }
 return true;
}

using ShootFn=void(__fastcall*)(void*,int);using CtorFn=unsigned char*(__fastcall*)(void*,void*);using TeamFn=void(__fastcall*)(void*,int,bool);
using FactoryFn=unsigned char*(__fastcall*)(void*,const float*,const wchar_t*,void*);
using DefaultFn=ObjRef*(__fastcall*)(void*,ObjRef*,const float*,void*);using DeleteFn=void(__fastcall*)(void*);
bool SpawnCollect(void*,const ObjRef& r,ObjRef* m,unsigned mc,unsigned* mn,Emitter* e,unsigned ec,unsigned* en) noexcept {return EnemyControlBerserkerPvpCollect(r,m,mc,mn,e,ec,en);}
Life SpawnRead(void*,const Emitter& e,int* t) noexcept {return EnemyControlBerserkerPvpRead(e,t);}
bool SpawnWrite(void*,const Emitter& e,int t) noexcept {return EnemyControlBerserkerPvpWrite(e,t);}
bool SpawnMember(void*,const ObjRef& r,const ObjRef& m,int a,int b,int* t,int* l) noexcept {return EnemyControlBerserkerPvpBaseline(r,m,a,b,t,l);}
bool SpawnEmitter(void*,const Emitter& e,int a,int b,int* t) noexcept {return EnemyControlBerserkerPvpBaseline(e,a,b,t);}
controlled_pvp::SpawnProof Proof(unsigned emitters=2) noexcept {return {{nullptr,SpawnCollect,SpawnRead,SpawnWrite,SpawnMember,SpawnEmitter},1,emitters};}
bool ActionCount(const ObjRef& r,int index,unsigned* out) noexcept {
 if(!out || !Readable(r.obj,0x1808))return false;*out=0;
 const auto n=At<std::size_t>(r.obj,0x1800),capacity=At<std::size_t>(r.obj,0x17F8);const auto* table=At<const unsigned char*>(r.obj,0x17F0);
 if(n>capacity || n>kMaxActions || (n && (!table || !Readable(table,n*0x100))))return false;
 for(std::size_t i=0;i<n;++i)if(index<0 || i==static_cast<unsigned>(index)) {
  const auto* row=table+i*0x100;const auto count=At<std::size_t>(row,0x18),allocated=At<std::size_t>(row,0x10);const auto* data=At<const void*>(row,8);
  if(count>allocated || count>controlled_pvp::kMaxMembers || (count && (!data || !Readable(data,count*0x28))))return false;
  if(count>*out)*out=static_cast<unsigned>(count);
 }
 return index<0 || static_cast<std::size_t>(index)<n;
}
ShotChild* Tracked(Shot& state,const ObjRef& child) noexcept {
 for(unsigned i=0;i<state.tracked;++i)if(Same(state.children[i].ref,child))return &state.children[i];return nullptr;
}
ShotChild* TrackChild(const ObjRef& child) noexcept {
 if(!active)return nullptr;if(auto* p=Tracked(*active,child))return p;
 if(active->tracked>=controlled_pvp::kMaxMembers || !Hold(child)){active->failed=true;return nullptr;}
 auto* p=&active->children[active->tracked++];p->ref=child;return p;
}
bool CaptureCompleted(ShotChild& tracked,const ObjRef& parent) noexcept {
 const auto* type=TypeOf(tracked.ref);if(!type || type->kind!=Kind::rocket)return false;
 tracked.complete=true;ObjRef bullet{};if(!Weak(tracked.ref.obj,0x5D0,&bullet))return false;
 if(bullet && (!BulletType(bullet) || !Owner(bullet.obj,0xAE8,parent) || !Owner(bullet.obj,0x880,parent) || !Hold(bullet)))return false;
 tracked.bullet=bullet;return true;
}
bool CancelBullet(const ObjRef& bullet,const ObjRef& parent) noexcept {
 if(!bullet)return true;const Life life=Identity(bullet);if(life==Life::gone)return true;
 if(life!=Life::live || !BulletType(bullet) || !Owner(bullet.obj,0xAE8,parent) || !Owner(bullet.obj,0x880,parent))return false;
 Native<DeleteFn>(kDelete)(const_cast<void*>(bullet.obj));return true;
}
bool CancelChild(const ObjRef& child,const ObjRef& parent,const ObjRef* captured=nullptr) noexcept {
 if(auto* row=Recorded(child))row->denied=true;
 const Life body=Identity(child);bool clean=body!=Life::invalid;ObjRef bullet{};
 if(captured)bullet=*captured;
 else if(body==Life::live) {
  const auto* type=TypeOf(child);if(!type || type->kind!=Kind::rocket)return false;
  if(!Weak(child.obj,0x5D0,&bullet))clean=false;
 }
 if(!CancelBullet(bullet,parent))clean=false;
 if(body==Life::live)Native<DeleteFn>(kDelete)(const_cast<void*>(child.obj));return clean;
}
void Rollback(Shot& state) noexcept {
 for(unsigned i=0;i<state.tracked;++i) {
  auto& p=state.children[i];
  if(p.complete && !CancelChild(p.ref,state.root,&p.bullet))Fault(state.root);
  // A factory notification can precede a later constructor exception or a missing 5D0 attachment.
  if(p.notice.object && !CancelBullet(p.notice.object,state.root))Fault(state.root);
 }
}
bool ActivateGroup(Shot& state) {
 for(unsigned i=0;i<state.tracked;++i) {
  const auto& p=state.children[i];const auto* type=TypeOf(p.ref);
  if(!p.complete || !p.activate || !type || type->kind!=Kind::rocket || (Recorded(p.ref) && Recorded(p.ref)->denied))return false;
  if(p.notice.object && (!Same(p.notice.object,p.bullet) || !BulletType(p.notice.object) || !Owner(p.notice.object.obj,0xAE8,state.root) || !Owner(p.notice.object.obj,0x880,state.root)))return false;
 }
 for(unsigned i=0;i<state.tracked;++i) {
  const auto& p=state.children[i];
  if(p.notice.object) {
   const void* object=p.notice.object.obj;const unsigned metadata=p.notice.metadata;
   reinterpret_cast<void(__fastcall*)(void*,const void*,const void*)>(p.notice.function)(p.notice.callback,&object,&metadata);
   // The native factories check flag4 after notifying. A rejecting observer still rejects the whole group.
   if(Identity(p.notice.object)!=Life::live)return false;
  }
  Native<void(__fastcall*)(void*,void*)>(0x1197050)(p.manager,const_cast<void*>(p.ref.obj));
  if(Identity(p.ref)!=Life::live || (p.bullet && Identity(p.bullet)!=Life::live))return false;
 }
 return true;
}
bool ExecuteParts(void* raw,int index,ShootFn actualOriginal,bool(*validate)(void*) noexcept=nullptr,void* context=nullptr) noexcept {
 if(!actualOriginal)return false;ObjRef candidate{};bool controlledCall=false;
 __try {if(raw && Readable(raw,0x38)){candidate=ObjRef::Of(raw);controlledCall=controlled_pvp::OwnsMember(candidate);}}
 __except(EXCEPTION_EXECUTE_HANDLER){}
 if(!controlledCall && !validate){actualOriginal(raw,index);return true;}
 Shot state{};Shot* previous=active;bool completed=false;
 __try {
  __try {
   state.root=candidate;state.owned=controlledCall;const auto* t=TypeOf(state.root);
   if(!t || !RootKind(t->kind)){Fault(state.root,controlled_pvp::Result::unsupported);return false;}
   if(!EnemyControlBerserkerPvpReady() || globalUnsafe || !ActionCount(state.root,index,&state.expected) || Room()<state.expected ||
      (state.owned && state.expected && !controlled_pvp::PrepareSpawns(state.root,Proof(),state.expected,&state.token))) {Fault(state.root,controlled_pvp::Result::capacity);return false;}
   active=&state;actualOriginal(raw,index);
   completed=!state.failed && state.created==state.expected && state.tracked==state.expected;
   if(completed && validate)completed=validate(context);
   if(completed)completed=ActivateGroup(state);
   if(completed && validate)completed=validate(context);
   if(!completed)Fault(state.root);
  } __except(EXCEPTION_EXECUTE_HANDLER){Fault(state.root);completed=false;}
 } __finally {
  __try {if(!completed)Rollback(state);}
  __finally {
   __try {
    for(unsigned i=0;i<state.tracked;++i) {
     auto& p=state.children[i];if(!Drop(p.ref) || !Drop(p.bullet) || !DropStrong(p.notice.object))Fault(state.root);
    }
   } __finally {active=previous;controlled_pvp::FinishSpawns(state.token);}
  }
 }
 return completed;
}
void __fastcall RocketTeam(void* object,int team,bool registered) noexcept {
 ObjRef ordinaryParent{},ordinaryChild{};bool typed=false,leasedCall=false;
 __try {
  ordinaryChild=ObjRef::Of(object);const auto* ct=TypeOf(ordinaryChild);
  typed=ct && ct->kind==Kind::rocket && PartParent(ordinaryChild,&ordinaryParent);
  leasedCall=typed && controlled_pvp::OwnsMember(ordinaryParent);
 } __except(EXCEPTION_EXECUTE_HANDLER){}
 if(!leasedCall && !active) {
  Native<TeamFn>(kSetTeam)(object,team,registered);
  __try {if(typed){int pt=-1,pl=-1;if(!Original(ordinaryParent,&pt,&pl) || !Record(ordinaryParent,ordinaryChild,At<int>(object,kTeam),At<int>(object,0x318),pt,pl,false))globalUnsafe=true;}}
  __except(EXCEPTION_EXECUTE_HANDLER){globalUnsafe=true;}
  return;
 }
 __try {
  const ObjRef child=ObjRef::Of(object);const auto* type=TypeOf(child);ObjRef parent{};
  if(!type || type->kind!=Kind::rocket || !PartParent(child,&parent)) {if(active){active->failed=true;Fault(active->root);}Native<TeamFn>(kSetTeam)(object,team,registered);return;}
  if(active && !TrackChild(child)){active->failed=true;Fault(active->root);}
  const bool leased=controlled_pvp::OwnsMember(parent),temporary=controlled_pvp::HasTemporaryTeam(parent);int pt=-1,pl=-1;
  if(!Original(parent,&pt,&pl)){if(active)active->failed=true;Native<TeamFn>(kSetTeam)(object,team,registered);if(leased)Fault(parent);return;}
  Native<TeamFn>(kSetTeam)(object,temporary ? pl : team,registered);
  const bool recorded=Record(parent,child,At<int>(object,kTeam),At<int>(object,0x318),pt,pl,temporary);
  if(!leased && !active){if(!recorded)globalUnsafe=true;return;}
  if(!recorded || !active || active->failed || !Same(active->root,parent) || (leased && !active->token) || active->created>=active->expected) {
   if(active)active->failed=true;if(auto* b=Recorded(child))b->denied=true;Fault(parent);return;
  }
  Born* b=Recorded(child);b->adopted=leased;++active->created;
  if(leased && !controlled_pvp::AdoptSpawn(active->token,parent,child)){b->denied=true;active->failed=true;Fault(parent);}
 } __except(EXCEPTION_EXECUTE_HANDLER){if(active){active->failed=true;Fault(active->root);}}
}
void RejectRocket() noexcept {
 if(construction)construction->rejected=true;
 if(!active)return;active->failed=true;
 if(constructingRocket && Readable(constructingRocket,0x38))if(auto* b=Recorded(ObjRef::Of(constructingRocket)))b->denied=true;
 Fault(active->root);
}
bool EmptyRejectedRocket(const void* target,std::uintptr_t offset) noexcept {
 if(reinterpret_cast<std::uintptr_t>(target)!=offset || !constructingRocket || !Readable(constructingRocket,0x5E0) ||
    !((active && active->failed) || (construction && construction->rejected)))return false;
 const auto ref=ObjRef::Of(constructingRocket);const auto* row=Recorded(ref);
 return Identity(ref)==Life::live && (!row || row->denied) && !At<const void*>(constructingRocket,0x5D0) && !At<const void*>(constructingRocket,0x5D8);
}
using BindFn=void(__fastcall*)(void*,const void*);
void __fastcall BindDamage(void* target,const void* argument) noexcept {
 if(!EmptyRejectedRocket(target,0xDA0))Native<BindFn>(0x353290)(target,argument);
}
void __fastcall BindFinish(void* target,const void* argument) noexcept {
 if(!EmptyRejectedRocket(target,0xDE0))Native<BindFn>(0x353420)(target,argument);
}
void ConstructionFailure() {
 RejectRocket();RaiseException(0xE0425352,EXCEPTION_NONCONTINUABLE,0,nullptr);
}
void* __fastcall OriginalRocketConstructor(void* memory,const void* init) {
 if(construction) {
  if(construction->memory!=memory || construction->entered){ConstructionFailure();return nullptr;}
  construction->entered=true;
 }
 void* result=Native<CtorFn>(kRocketCtor)(memory,const_cast<void*>(init));
 if(construction){construction->result=result;construction->completed=true;}return result;
}
unsigned char* __fastcall RocketCtor(void* memory,void* init) noexcept {
 const void* previous=constructingRocket;Construction state{};state.memory=memory;Construction* previousConstruction=construction;
 unsigned char* result=nullptr;if(active)active->denied={};constructingRocket=memory;construction=&state;
 __try {
  // This shallow local copy keeps the original caller resources alive for the synchronous rejection constructor.
  // The native caller at 34045F dereferences the returned body without a null check.
  alignas(16) unsigned char original[0x148];
  if(!Readable(init,sizeof(original))){ConstructionFailure();return nullptr;}
  std::memcpy(original,init,sizeof(original));
  void* bridge=enemy_native::InvokeWalkingPartConstructor(memory,init,&OriginalRocketConstructor);
  if(!state.entered) {RejectRocket();OriginalRocketConstructor(memory,original);}
  if(!state.completed || !state.result || state.result!=memory){ConstructionFailure();return nullptr;}
  result=static_cast<unsigned char*>(state.result);
  if(bridge!=result)RejectRocket();
  const ObjRef child=ObjRef::Of(result);const auto* row=Recorded(child);
  if(active) {auto* p=TrackChild(child);if(!p || !CaptureCompleted(*p,active->root))ConstructionFailure();}
  if(state.rejected || (row && row->denied) || (active && active->failed)) {
   if(active)active->denied=child;
   ObjRef parent{};if(!PartParent(child,&parent) || !CancelChild(child,parent))ConstructionFailure();
  }
 } __finally {constructingRocket=previous;construction=previousConstruction;}
 return result;
}
using PublishFn=void(__fastcall*)(void*,const ObjRef*);using ActivateFn=void(__fastcall*)(void*,void*);
void __fastcall Publish(void* manager,const ObjRef* ref) noexcept {
 if(ref && Readable(ref,sizeof(ObjRef))) {
  const auto* row=Recorded(*ref);if((active && active->denied && Same(active->denied,*ref)) || (row && row->denied))return;
 }
 Native<PublishFn>(0x1195A20)(manager,ref);
}
void __fastcall Activate(void* manager,void* object) noexcept {
 if(object && Readable(object,0x38)) {
  const ObjRef child=ObjRef::Of(object);const auto* row=Recorded(child);
  if((active && active->denied && Same(active->denied,child)) || (row && row->denied))return;
  if(active) {
   if(auto* p=Tracked(*active,child)){if(p->activate){active->failed=true;return;}p->manager=manager;p->activate=true;return;}
   active->failed=true;return;
  }
 }
 Native<ActivateFn>(0x1197050)(manager,object);
}
bool CanEmit(void* init) noexcept {
 if(construction && construction->rejected)return false;
 if(!init || !Readable(init,0x48))return false;const ObjRef owner{At<const void*>(init,0x38),At<const void*>(init,0x40)};
 if(active && Same(active->root,owner) && active->failed)return false;
 if(!controlled_pvp::OwnsMember(owner))return true;
 return active && Same(active->root,owner) && active->token && !active->failed;
}
unsigned char* __fastcall Factory(void* manager,const float* matrix,void* factory,void* init) noexcept {
 if(!CanEmit(init)){RejectRocket();return nullptr;}
 unsigned char* result=nullptr;Construction* state=construction;const bool previous=state && state->creatingDamage;
 if(state)state->creatingDamage=true;
 __try {result=static_cast<unsigned char*>(enemy_native::DispatchNativeUnitFactory(manager,matrix,factory,init));}
 __finally {if(state)state->creatingDamage=previous;}
 if(!result && active)RejectRocket();return result;
}
void* __fastcall OriginalFallbackFactory(void* manager,void* out,const float* matrix,void* init) {return Native<DefaultFn>(kDefaultFactory)(manager,static_cast<ObjRef*>(out),matrix,init);}
ObjRef* __fastcall DefaultFactory(void* manager,ObjRef* out,const float* matrix,void* init) noexcept {
 // Native 3526AB passes an uninitialized out pair; every rejected bridge must still initialize it.
 if(!out || !Readable(out,sizeof(*out),true)){ConstructionFailure();return out;}
 *out={};
 if(!CanEmit(init)){RejectRocket();return out;}
 ObjRef* result=nullptr;Construction* state=construction;const bool previous=state && state->creatingDamage;
 if(state)state->creatingDamage=true;
 __try {result=static_cast<ObjRef*>(enemy_native::InvokeWalkingPartFallback(manager,out,matrix,init,&OriginalFallbackFactory));}
 __finally {if(state)state->creatingDamage=previous;}
 if(result!=out || !out->obj || !out->ctrl){*out={};if(active)RejectRocket();}
 return out;
}
using NotificationFn=void(__fastcall*)(void*,const void*,const void*);
bool DeferDamageNotice(void* callback,const void* argument,const void* metadata,void* function) noexcept {
 if(!active || !construction || !construction->creatingDamage || construction->completed)return false;
 if(!argument || !Readable(argument,8) || !metadata || !Readable(metadata,4) || !function){RejectRocket();return true;}
 const void* raw=At<const void*>(argument,0);
 if(!raw || !Readable(raw,0x38)){RejectRocket();return true;}
 const ObjRef bullet=ObjRef::Of(raw);const ObjRef holder=ObjRef::Of(construction->memory);auto* p=Tracked(*active,holder);
 if(!p || p->notice.object || !BulletType(bullet) || !Owner(bullet.obj,0xAE8,active->root) || !Owner(bullet.obj,0x880,active->root) || !HoldStrong(bullet)) {RejectRocket();return true;}
 p->notice={bullet,callback,function,At<unsigned>(metadata,0)};return true;
}
void __fastcall BulletPublication(void* callback,const void* argument,const void* metadata,void* function) {
 if(!DeferDamageNotice(callback,argument,metadata,function))reinterpret_cast<NotificationFn>(function)(callback,argument,metadata);
}
void __fastcall DamageActivate(void* manager,void* object) {
 if(active && construction && construction->creatingDamage && !construction->completed) {
  if(!manager || !Readable(static_cast<const unsigned char*>(manager)+0xCA8,16)){RejectRocket();return;}
  void* callback=At<void*>(manager,0xCA8);if(!callback)return;
  const auto* vt=At<void* const*>(callback,0);if(!Readable(vt,24) || !vt[2]){RejectRocket();return;}
  const unsigned metadata=At<unsigned>(manager,0xCB0);const void* argument=object;
  if(DeferDamageNotice(callback,&argument,&metadata,vt[2]))return;
 }
 Native<ActivateFn>(0x1197050)(manager,object);
}
// The common factory publishes only after this constructor returns. It is not null-safe on an empty out pair.
// Rejected children finish construction, are deleted, and are suppressed at its exact publication edge.
void KeepDenied(const ObjRef& child) noexcept {
 if(pendingDenied && !Drop(pendingDenied))return;
 if(Hold(child))pendingDenied=child;else globalUnsafe=true;
}
unsigned char* __fastcall BombCtor(void* memory,void* init) noexcept {
 BombBirth local{};BombBirth* previous=activeBomb;bool ownsLocal=false;unsigned char* result=nullptr;
 __try {
   ObjRef parent{};
   if(Weak(init,0x30,&parent) && parent) {
    const auto* type=TypeOf(parent);
    if(type && RootKind(type->kind) && controlled_pvp::OwnsMember(parent) && (!activeBomb || !Same(activeBomb->root,parent))) {
     local.root=parent;local.owned=true;ownsLocal=true;
     local.allowed=Room()>0 && controlled_pvp::PrepareSpawns(parent,Proof(0),1,&local.token);
     activeBomb=&local;
    }
   }
   result=Native<CtorFn>(kBombCtor)(memory,init);
   if(!result)return result;
   const ObjRef child=ObjRef::Of(result);ObjRef owner{};
   if(!PartParent(child,&owner))return result;
   const bool leased=controlled_pvp::OwnsMember(owner);int pt=-1,pl=-1;
   const bool captured=Original(owner,&pt,&pl) && Record(owner,child,At<int>(child.obj,kTeam),At<int>(child.obj,0x318),pt,pl,controlled_pvp::HasTemporaryTeam(owner));
   if(!leased){if(!captured)globalUnsafe=true;return result;}
   Born* row=captured ? Recorded(child) : nullptr;
   bool admitted=row && activeBomb && Same(activeBomb->root,owner) && activeBomb->allowed && activeBomb->token;
   if(admitted){row->adopted=true;admitted=controlled_pvp::AdoptSpawn(activeBomb->token,owner,child);}
   if(!admitted) {
    if(row)row->denied=true;if(activeBomb)activeBomb->failed=true;
    KeepDenied(child);Fault(owner);Native<DeleteFn>(kDelete)(result);
   }
 } __finally {if(ownsLocal){activeBomb=previous;controlled_pvp::FinishSpawns(local.token);}}
 return result;
}
unsigned char* __fastcall CreateBomb(void* manager,const float* matrix,const wchar_t* path,void* init) noexcept {
 BombBirth birth{};BombBirth* previous=activeBomb;unsigned char* result=nullptr;
 bool guarded=false;
 __try {if(Weak(init,0x30,&birth.root) && birth.root){const auto* type=TypeOf(birth.root);guarded=type && type->kind==Kind::bommer && controlled_pvp::OwnsMember(birth.root);}}
 __except(EXCEPTION_EXECUTE_HANDLER){}
 if(!guarded)return Native<FactoryFn>(0x11945E0)(manager,matrix,path,init);
 __try {
  __try {
   if(!Weak(init,0x30,&birth.root) || !birth.root)return Native<FactoryFn>(0x11945E0)(manager,matrix,path,init);
   const auto* type=TypeOf(birth.root);birth.owned=controlled_pvp::OwnsMember(birth.root);
   if(!type || type->kind!=Kind::bommer || !birth.owned)return Native<FactoryFn>(0x11945E0)(manager,matrix,path,init);
   birth.allowed=EnemyControlBerserkerPvpReady() && Room()>0 && controlled_pvp::PrepareSpawns(birth.root,Proof(0),1,&birth.token);
   if(!birth.allowed){Fault(birth.root,controlled_pvp::Result::capacity);return nullptr;}
   activeBomb=&birth;result=Native<FactoryFn>(0x11945E0)(manager,matrix,path,init);
   if(birth.failed)return nullptr;
  } __except(EXCEPTION_EXECUTE_HANDLER){if(birth.owned)Fault(birth.root);}
 } __finally {activeBomb=previous;controlled_pvp::FinishSpawns(birth.token);}
 return result;
}
using PublicationFn=void(__fastcall*)(void*,const void*,const void*);
void __fastcall ScenePublication(void* callback,const void* argument,const void* metadata,void* original) {
 bool deny=false;
 __try {
  if(argument && Readable(argument,sizeof(void*))) {
   const auto* object=At<const void*>(argument,0);
   if(object && Readable(object,0x38)) {
    const ObjRef child=ObjRef::Of(object);const auto* row=Recorded(child);
    deny=(pendingDenied && Same(pendingDenied,child)) || (row && row->denied);
    if(deny && pendingDenied && Same(pendingDenied,child))Drop(pendingDenied);
   }
  }
 } __except(EXCEPTION_EXECUTE_HANDLER){if(activeBomb){activeBomb->failed=true;Fault(activeBomb->root);}}
 // The original callback remains outside our SEH filter. Unrelated native errors keep their original propagation.
 if(!deny)reinterpret_cast<PublicationFn>(original)(callback,argument,metadata);
}
constexpr unsigned kPublication=0x1194949;
constexpr unsigned char kPublicationBytes[]={0x4C,0x8D,0x44,0x24,0x48,0x48,0x8D,0x55,0xB7};
const void* publicationTable[3]={nullptr,nullptr,reinterpret_cast<void*>(&ScenePublication)};
bool PatchPublication() noexcept {
 if(!Readable(image+kPublication,9) || std::memcmp(image+kPublication,kPublicationBytes,9))return false;
 // Replace only the argument LEAs. Native null-callback JE ->1194955 and CALL [RAX+10] stay intact.
 // This is a leaf: the real C++ guard is called from the original native instruction/pdata frame.
 unsigned char code[]={0x4C,0x8B,0x48,0x10,0x4C,0x8D,0x44,0x24,0x50,0x48,0x8D,0x55,0xB7,0x48,0xB8,0,0,0,0,0,0,0,0,0xC3};
 const auto table=reinterpret_cast<std::uintptr_t>(publicationTable);std::memcpy(code+15,&table,8);
 const auto* thunk=static_cast<const unsigned char*>(AllocateNearCode(image+kPublication,code,sizeof(code)));if(!thunk)return false;
 const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(image+kPublication+5);if(delta<INT32_MIN || delta>INT32_MAX)return false;
 unsigned char patch[9]={0xE8,0,0,0,0,0x90,0x90,0x90,0x90};const auto relative=static_cast<std::int32_t>(delta);std::memcpy(patch+1,&relative,4);
 DWORD old=0;if(!VirtualProtect(image+kPublication,9,PAGE_EXECUTE_READWRITE,&old))return false;
 std::memcpy(image+kPublication,patch,9);FlushInstructionCache(GetCurrentProcess(),image+kPublication,9);DWORD ignored=0;
 if(!VirtualProtect(image+kPublication,9,old,&ignored))return false;
 std::memcpy(publicationAfter,patch,9);std::memcpy(publicationCode,code,sizeof(code));publicationRelay=thunk;publicationChanged=true;return true;
}

constexpr unsigned kBulletPublication=0x1194395;
constexpr unsigned char kBulletPublicationBytes[]={0x4C,0x8D,0x44,0x24,0x70,0x48,0x8D,0x94,0x24,0x80,0,0,0};
const void* bulletPublicationTable[3]={nullptr,nullptr,reinterpret_cast<void*>(&BulletPublication)};
bool PatchBulletPublication() noexcept {
 if(!Readable(image+kBulletPublication,13) || std::memcmp(image+kBulletPublication,kBulletPublicationBytes,13))return false;
 // A leaf replaces only argument LEAs; native null-callback branch and indirect CALL remain in their original frame.
 unsigned char code[]={0x4C,0x8B,0x48,0x10,0x4C,0x8D,0x44,0x24,0x78,0x48,0x8D,0x94,0x24,0x88,0,0,0,0x48,0xB8,0,0,0,0,0,0,0,0,0xC3};
 const auto table=reinterpret_cast<std::uintptr_t>(bulletPublicationTable);std::memcpy(code+19,&table,8);
 const auto* thunk=static_cast<const unsigned char*>(AllocateNearCode(image+kBulletPublication,code,sizeof(code)));if(!thunk)return false;
 const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(image+kBulletPublication+5);if(delta<INT32_MIN || delta>INT32_MAX)return false;
 unsigned char patch[13]={0xE8,0,0,0,0,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};const auto relative=static_cast<std::int32_t>(delta);std::memcpy(patch+1,&relative,4);
 DWORD old=0;if(!VirtualProtect(image+kBulletPublication,13,PAGE_EXECUTE_READWRITE,&old))return false;
 std::memcpy(image+kBulletPublication,patch,13);FlushInstructionCache(GetCurrentProcess(),image+kBulletPublication,13);DWORD ignored=0;
 if(!VirtualProtect(image+kBulletPublication,13,old,&ignored))return false;
 std::memcpy(bulletPublicationAfter,patch,13);std::memcpy(bulletPublicationCode,code,sizeof(code));bulletPublicationRelay=thunk;bulletPublicationChanged=true;return true;
}

bool HookReady(const HookSite& h) noexcept {
 if(!Readable(image+h.at,5) || std::memcmp(image+h.at,h.after,5) || !h.function)return false;
 const auto* relay=image+h.at+5+At<std::int32_t>(image+h.at,1);
 return Readable(relay,12) && relay[0]==0x48 && relay[1]==0xB8 && relay[10]==0xFF && relay[11]==0xE0 && At<const void*>(relay,2)==h.function;
}

bool CallIs(unsigned at,unsigned target) noexcept {return Readable(image+at,5) && image[at]==0xE8 && image+at+5+At<std::int32_t>(image+at,1)==image+target;}
} // namespace berserker_pvp_detail
bool EnemyControlBerserkerPvpReady() noexcept {
 using namespace berserker_pvp_detail;
 __try {
  if(!image || (admittedImage && image!=admittedImage))return false;
  for(const auto& a:kAnchors)if(!Readable(image+a.at,a.size) || std::memcmp(image+a.at,a.bytes,a.size))return false;
  for(const auto& h:sites)if(h.changed ? !HookReady(h) : !CallIs(h.at,h.target))return false;
  if(publicationChanged ? std::memcmp(image+kPublication,publicationAfter,9)!=0 : std::memcmp(image+kPublication,kPublicationBytes,9)!=0)return false;
  if(publicationChanged && (!publicationRelay || !Readable(publicationRelay,sizeof(publicationCode)) || std::memcmp(publicationRelay,publicationCode,sizeof(publicationCode)) || publicationTable[2]!=reinterpret_cast<void*>(&ScenePublication)))return false;
  if(bulletPublicationChanged ? std::memcmp(image+kBulletPublication,bulletPublicationAfter,13)!=0 : std::memcmp(image+kBulletPublication,kBulletPublicationBytes,13)!=0)return false;
  if(bulletPublicationChanged && (!bulletPublicationRelay || !Readable(bulletPublicationRelay,sizeof(bulletPublicationCode)) || std::memcmp(bulletPublicationRelay,bulletPublicationCode,sizeof(bulletPublicationCode)) || bulletPublicationTable[2]!=reinterpret_cast<void*>(&BulletPublication)))return false;
  for(const auto& t:kTypes)if(!NativeType(t))return false;return true;
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBerserkerPvpSupports(const ObjRef& r) noexcept {
 __try {const auto* t=berserker_pvp_detail::TypeOf(r);return t && berserker_pvp_detail::Body(t->kind);}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBerserkerPvpInstall() noexcept {
 using namespace berserker_pvp_detail;if(attempted)return installed;attempted=true;
 __try {
  if(!EnemyControlBerserkerPvpReady())return false;
  void* targets[]={reinterpret_cast<void*>(&RocketCtor),reinterpret_cast<void*>(&RocketTeam),reinterpret_cast<void*>(&Factory),reinterpret_cast<void*>(&DefaultFactory),reinterpret_cast<void*>(&BombCtor),reinterpret_cast<void*>(&Publish),reinterpret_cast<void*>(&Activate),reinterpret_cast<void*>(&CreateBomb),reinterpret_cast<void*>(&BindDamage),reinterpret_cast<void*>(&BindFinish),reinterpret_cast<void*>(&DamageActivate)};
  for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);++i) {
   auto& h=sites[i];bool changed=false;if(!RedirectCall(image+h.at,image+h.target,targets[i],changed))return false;
   h.changed=true;h.function=targets[i];std::memcpy(h.after,image+h.at,5);
  }
  if(!PatchPublication() || !PatchBulletPublication())return false;
  admittedImage=image;installed=true;return true;
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBerserkerPvpReset() noexcept {
 using namespace berserker_pvp_detail;
 __try {
  if(active || activeBomb || constructingRocket || construction)return false;
  for(const auto& r:roots)if(r.ref && controlled_pvp::OwnsMember(r.ref))return false;
  for(const auto& b:born)if(b.child && (controlled_pvp::OwnsMember(b.child) || controlled_pvp::OwnsMember(b.parent)))return false;
  for(const auto& b:bindings)if(b.serial && (controlled_pvp::OwnsMember(b.owner) || controlled_pvp::OwnsMember(b.holder)))return false;
  for(auto& b:bindings)if(b.serial){if(!Drop(b.owner) || !Drop(b.holder) || !Drop(b.bullet) || !DropControl(b.unitControl))return false;b={};}
  for(auto& b:born)if(b.child){if(!Drop(b.child) || !Drop(b.parent))return false;b={};}
  for(auto& r:roots){if(!Drop(r.ref))return false;r={};}if(!Drop(pendingDenied))return false;globalUnsafe=false;return true;
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBerserkerPvpExecuteParts(void* root,int index,void(__fastcall* original)(void*,int)) noexcept {return berserker_pvp_detail::ExecuteParts(root,index,original);}
bool EnemyControlBerserkerPvpExecutePartsValidated(void* root,int index,void(__fastcall* original)(void*,int),bool(*validate)(void*) noexcept,void* context) noexcept {
 if(!validate)return false;return berserker_pvp_detail::ExecuteParts(root,index,original,validate,context);
}
bool EnemyControlBerserkerPvpPreflight(const ObjRef& root,unsigned count) noexcept {
 using namespace berserker_pvp_detail;
 __try {
  const auto* t=TypeOf(root);if(!t || !RootKind(t->kind) || !installed || !EnemyControlBerserkerPvpReady())return false;
  if(!controlled_pvp::OwnsMember(root))return true;
  if(globalUnsafe)return false;if(const auto* r=FindRoot(root))if(r->unsafe)return false;
  if(!count && !ActionCount(root,-1,&count))return false;
  return !count || (Room()>=count && controlled_pvp::CanPrepareSpawns(root,Proof(),count));
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBerserkerPvpCollect(const ObjRef& root,ObjRef* members,unsigned cap,unsigned* count,controlled_pvp::Emitter* emitters,unsigned ec,unsigned* emitted) noexcept {
 using namespace berserker_pvp_detail;if(count)*count=0;if(emitted)*emitted=0;
 if(!count || !emitted || !members || !emitters || !cap || cap>controlled_pvp::kMaxMembers || ec>controlled_pvp::kMaxEmitters)return false;
 __try {
  const auto* t=TypeOf(root);if(!t || !Body(t->kind) || !installed || !EnemyControlBerserkerPvpReady() || !Sweep() || (globalUnsafe && !controlled_pvp::OwnsMember(root)))return false;
  unsigned used=0,em=0;if(!Add(root,members,cap,used,false))return false;
  if(RootKind(t->kind)) {
   if(!RememberRoot(root))return false;
   if(const auto* r=FindRoot(root))if(r->unsafe && !controlled_pvp::OwnsMember(root))return false;
   if(!RootParts(root,*t,members,cap,used))return false;
  } else {ObjRef parent{};if(!PartParent(root,&parent) || !Recorded(root))return false;}
  for(unsigned i=0;i<used;++i) {
   const auto* type=TypeOf(members[i]);if(!type || !Body(type->kind))return false;
   ObjRef tree[controlled_pvp::kMaxMembers]{};unsigned n=0;
   if(!enemy_pvp_native::CollectTree(members[i],tree,controlled_pvp::kMaxMembers,&n))return false;
   for(unsigned j=1;j<n;++j)if(!Add(tree[j],members,cap,used))return false;
   if(!CurrentEmitters(members[i],emitters,ec,em))return false;
  }
  *count=used;*emitted=em;return true;
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
controlled_pvp::Life EnemyControlBerserkerPvpRead(const controlled_pvp::Emitter& e,int* team) noexcept {
 using namespace berserker_pvp_detail;if(!team || !e.object || !e.incarnation)return Life::invalid;
 __try {
  if(!installed || !EnemyControlBerserkerPvpReady())return Life::invalid;
  if(e.kind==EmitterKind::ownerTeamCache || e.kind==EmitterKind::ownerLastTeamCache) {
   const Life storageLife=Identity(e.storage);if(storageLife!=Life::live)return storageLife;
   // Only Collect can seal a binding from a current typed holder. A detached live store still owns restoration.
   for(const auto& b:bindings)if(b.serial==e.incarnation && Same(b.owner,e.member) && Same(b.bullet,e.storage)) {
    const auto expected=b.lastSource ? EmitterKind::ownerLastTeamCache : EmitterKind::ownerTeamCache;
    if(e.kind!=expected || (e.object!=static_cast<const unsigned char*>(b.bullet.obj)+0xB40 && e.object!=static_cast<const unsigned char*>(b.bullet.obj)+0x894))return Life::invalid;
    const Life life=Identity(b.bullet);if(life!=Life::live)return life;
    if(!BulletType(b.bullet) || !Owner(b.bullet.obj,0xAE8,b.owner) || !Owner(b.bullet.obj,0x880,b.owner))return Life::invalid;
    *team=At<int>(e.object,0);return Team(*team) ? Life::live : Life::invalid;
   }
   return Life::invalid;
  }
  const Life life=Identity(e.member);if(life!=Life::live)return life;
  Emitter current[controlled_pvp::kMaxEmitters]{};unsigned n=0;if(!CurrentEmitters(e.member,current,controlled_pvp::kMaxEmitters,n))return Life::invalid;
  for(unsigned i=0;i<n;++i)if(current[i].object==e.object && current[i].incarnation==e.incarnation && current[i].kind==e.kind && Same(current[i].member,e.member)) {
   *team=At<int>(e.member.obj,e.kind==EmitterKind::liveOwnerLastTeam ? 0x318 : kTeam);return Team(*team) ? Life::live : Life::invalid;
  }
  return Life::gone;
 } __except(EXCEPTION_EXECUTE_HANDLER){return Life::invalid;}
}
bool EnemyControlBerserkerPvpWrite(const controlled_pvp::Emitter& e,int team) noexcept {
 using namespace berserker_pvp_detail;if(!Team(team))return false;
 __try {
  int value=-1;if(EnemyControlBerserkerPvpRead(e,&value)!=Life::live)return false;
  if(e.kind!=EmitterKind::ownerTeamCache && e.kind!=EmitterKind::ownerLastTeamCache)return value==team;
  if(!Readable(e.object,4,true))return false;Put<int>(const_cast<void*>(e.object),0,team);return At<int>(e.object,0)==team;
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBerserkerPvpBaseline(const ObjRef& root,const ObjRef& member,int rootTeam,int rootLast,int* team,int* last) noexcept {
 using namespace berserker_pvp_detail;if(!team || !last || !Team(rootTeam) || !Team(rootLast))return false;
 __try {
  const auto* t=TypeOf(member);if(!t || !Body(t->kind))return false;
  if(const auto* b=Recorded(member)) {if(!Same(member,root) && !Same(b->parent,root))return false;*team=b->team;*last=b->last;return true;}
  if(Same(root,member)){*team=rootTeam;*last=rootLast;return true;}return false;
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBerserkerPvpBaseline(const controlled_pvp::Emitter& e,int ownerTeam,int ownerLast,int* team) noexcept {
 using namespace berserker_pvp_detail;if(!team || !Team(ownerTeam) || !Team(ownerLast))return false;
 int current=-1;if(EnemyControlBerserkerPvpRead(e,&current)!=Life::live)return false;
 if(e.kind==EmitterKind::ownerTeamCache || e.kind==EmitterKind::ownerLastTeamCache) {
  for(const auto& b:bindings)if(b.serial==e.incarnation && Same(b.owner,e.member)) {
   if(e.object==static_cast<const unsigned char*>(b.bullet.obj)+0xB40){*team=b.filter;return true;}
   if(e.object==static_cast<const unsigned char*>(b.bullet.obj)+0x894){*team=b.damage;return true;}
  }
  return false;
 }
 *team=e.kind==EmitterKind::liveOwnerLastTeam ? ownerLast : ownerTeam;return true;
}
} // namespace crew
