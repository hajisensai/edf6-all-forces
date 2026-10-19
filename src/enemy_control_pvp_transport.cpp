// EDF.dll 678CCB46: typed TimeShip parts and ObjectGenerator/SpawnerObject ownership.
#include "enemy_control_pvp_transport.h"
#include "enemy_control_pvp_native.h"
#include "enemy_control_pvp_tree.h"
#include "memory.h"
#include <cstring>
#include <cwchar>
#include <limits>

namespace crew {
bool EnemyControlNativePvpSpawnProof(const wchar_t*,controlled_pvp::SpawnProof*) noexcept;
bool EnemyControlOwnsTargetSelector(const void*,const void*) noexcept;
namespace transport_pvp_detail {
using controlled_pvp::Emitter;using controlled_pvp::EmitterKind;using controlled_pvp::Life;
constexpr unsigned kTime=0x17B93C0,kDrop=0x17C94D8,kCarrier=0x17C9DC0;
constexpr unsigned kAnchor=0x17B9DB8,kCapsule=0x17BA800,kBarrier=0x17BA488,kSpawner=0x17E7658;
constexpr unsigned kCreate=0x11945E0,kCreateSpawner=0x6DDD80,kDelete=0x118A1B0;
constexpr unsigned kCreateSpawnerSite=0x6E257F,kCreateFinalSite=0x6E32D1,kTeamFinalSite=0x6E3370,kCreatePartSite=0x3ED493,kCapsuleCrueSite=0x1CAF7D,kCapsuleCrueCreate=0x1D8900;
constexpr unsigned kBornLimit=4096,kMirrorLimit=2048;
struct AnchorBytes {unsigned rva;unsigned char bytes[16];unsigned size;};
constexpr AnchorBytes anchors[]={
    {0x3E93E0,{0x48,0x8B,0x51,0x50,0x4C,0x8B,0xC1},7},
    {0x3EEA81,{0x48,0x8B,0xB9,0x28,0x11,0x00,0x00},7},
    {0x3EEAFD,{0x41,0x89,0xAE,0x60,0x11,0x00,0x00},7},
    {0x3EC8B3,{0x8B,0x9E,0x14,0x03,0x00,0x00},6},
    {0x3EC8D1,{0x41,0xB0,0x01,0x8B,0xD3,0xE8,0x95,0x25,0x16,0x00},10},
    {0x6DF940,{0x48,0x89,0x91,0xE0,0x00,0x00,0x00},7},
    {0x6E16DF,{0x41,0x8B,0x46,0x40,0x89,0x45,0x07},7},
    {0x6E1770,{0x89,0x43,0x68},3},
    {0x6E1892,{0x89,0x83,0xC0,0x00,0x00,0x00},6},
    {0x6E2361,{0x8B,0x86,0xC0,0x00,0x00,0x00},6},
    {0x6E24AD,{0x48,0x89,0x95,0x98,0x01,0x00,0x00},7},
    {0x6E2686,{0x4C,0x89,0x60,0x10,0x48,0x8B,0x4C,0x24,0x20,0x48,0x89,0x48,0x18},13},
    {0x6E1F25,{0x48,0x8D,0x57,0x28},4},
    {0x6E13E5,{0x48,0x83,0xE9,0x80,0xE8,0xB2,0xE0,0xFF,0xFF},9},
    {0x6DF4F5,{0x8B,0x47,0x40,0x89,0x43,0x40},6},
    {0x6E3367,{0x8B,0x93,0x20,0x06,0x00,0x00},6},
    {0x6E331C,{0x48,0x89,0xB3,0x10,0x07,0x00,0x00},7},
    {0x6E332A,{0x48,0x89,0x83,0x18,0x07,0x00,0x00},7},
    {0x6E119A,{0x49,0xC1,0xE7,0x05,0x4D,0x03,0x7D,0x10},8},
    {0x1CAF70,{0x4C,0x8D,0x45,0x90,0x48,0x8D,0x54,0x24,0x70,0x49,0x8B,0x4E,0x18},13},
    {0x1CAE1D,{0xC7,0x45,0x18,0x01,0x00,0x00,0x00},7},
    {0x1BC3F0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0x48,0x8B,0x51,0x08,0x48,0x8B,0xCB},16},
    {0x1D8A60,{0x41,0xB0,0x01,0x41,0x8B,0x96,0x88,0x00,0x00,0x00,0x48,0x8B,0xCE},13},
    {0x3FAEA7,{0x8B,0x81,0x7C,0x10,0x00,0x00,0x48,0x3B,0x81,0x98,0x10,0x00,0x00},13},
    {0x3FAF3D,{0x48,0x89,0x4E,0x30,0x48,0x8B,0x4E,0x38,0x4C,0x89,0x46,0x38},12},
    {0x3F7428,{0x48,0x8D,0x9F,0xB0,0x10,0x00,0x00,0x48,0x8D,0x73,0x40},11},
    {0x3F768D,{0xE8,0xCE,0xA6,0x2E,0x00},5},
    {0x3F8F72,{0xE8,0x49,0xC2,0x2D,0x00},5},
    {0x3F8953,{0x41,0x8B,0x86,0x18,0x03,0x00,0x00,0x89,0x45,0x94},10},
    {0x3F5B04,{0x8B,0x97,0x18,0x03,0x00,0x00,0x48,0x8B,0xCE,0xE8,0x5E,0x93,0x15,0x00},14},
    {0x3F99DA,{0x8B,0x83,0x18,0x03,0x00,0x00,0x89,0x83,0x98,0x0F,0x00,0x00},12},
    {0x3F9A00,{0x89,0x91,0x98,0x0F,0x00,0x00,0xB0,0x01,0xC3},9},
    {0x3FBE5A,{0x4C,0x8B,0xB3,0x88,0x10,0x00,0x00,0x4C,0x8B,0xBB,0x98,0x10,0x00,0x00},14},
    {0x3FBE88,{0x48,0x8B,0x16,0x48,0x85,0xD2,0x74,0x22},8},
    // Carrier death deliberately emits the stock team=-1 hazard; no invented owner/team rewrite.
    {0x4F9CEF,{0x89,0x7D,0xCB},3},
};
bool StaticProof() noexcept {
    if(!image)return false;
    for(const auto& anchor:anchors)if(!Readable(image+anchor.rva,anchor.size) || std::memcmp(image+anchor.rva,anchor.bytes,anchor.size))return false;
    return true;
}
unsigned Setter(unsigned kind) noexcept {return kind==kTime ? 0x3EEA60u : kind==kBarrier ? 0x3F9A00u : 0x54ABD0u;}
struct Born {ObjRef parent{},child{};int team=-1,last=-1,cache=-1;bool spawner=false,adopted=false;unsigned parentIndex=kBornLimit;};
struct Mirror {ObjRef owner{};const unsigned char* field=nullptr;const void* control=nullptr;int baseline=-1;};
Born born[kBornLimit]{};Mirror mirrors[kMirrorLimit]{};
bool installed=false,attempted=false,incomplete=false;
using CreateFn=unsigned char*(__fastcall*)(void*,const float*,const wchar_t*,void*);
using SpawnerFn=ObjRef*(__fastcall*)(void*,ObjRef*,const float*,void*);
using TeamFn=void(__fastcall*)(void*,int,bool);
using DeleteFn=void(__fastcall*)(void*);
bool Same(const ObjRef& a,const ObjRef& b) noexcept {return a.obj==b.obj && a.ctrl==b.ctrl;}
bool Team(int value) noexcept {return value>=-1 && value<7;}
bool Scalar(EmitterKind kind) noexcept {return kind==EmitterKind::ownerTeamMirror || kind==EmitterKind::ownerTeamCache || kind==EmitterKind::ownerLastTeamCache;}
Life Identity(const ObjRef& ref) noexcept {
    if(!ref.obj || !ref.ctrl || !Readable(ref.ctrl,16))return Life::invalid;
    if(At<LONG>(ref.ctrl,8)<=0)return Life::gone;
    if(!Readable(ref.obj,0x560))return Life::invalid;
    if(At<const void*>(ref.obj,kSelfCtrl)!=ref.ctrl || (At<unsigned char>(ref.obj,0x18)&4u))return Life::gone;
    return At<const void*>(ref.obj,kSelf)==ref.obj ? Life::live : Life::invalid;
}
bool KnownCurrent(const ObjRef& ref) noexcept {
    return ref.obj && ref.ctrl && Readable(ref.ctrl,16) && At<LONG>(ref.ctrl,8)>0 && Readable(ref.obj,0x560) &&
        At<const void*>(ref.obj,kSelf)==ref.obj && At<const void*>(ref.obj,kSelfCtrl)==ref.ctrl;
}
bool Hold(const ObjRef& ref,bool acceptedDeleted=false) noexcept {
    if((acceptedDeleted ? !KnownCurrent(ref) : Identity(ref)!=Life::live) || !Readable(ref.ctrl,16,true))return false;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(const_cast<void*>(ref.ctrl))+12));return true;
}
void Drop(const ObjRef& ref) noexcept {
    if(!ref.ctrl)return;
    auto* control=static_cast<unsigned char*>(const_cast<void*>(ref.ctrl));
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+12))==0)
        reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(control,0)[1])(control);
}
struct NativeType {unsigned vtable,slots;const char* name;};
constexpr NativeType types[]={{kTime,55,"EDF6_TimeShip"},{kDrop,49,"DropBoat512"},{kCarrier,49,"UfoCarrier508"},
    {kAnchor,55,"EDF6_TimeShip_Anchor"},{kCapsule,55,"EDF6_TimeShip_Capsule"},{kBarrier,55,"EDF6_TimeShip_Anchor_Barrier"},{kSpawner,45,"SpawnerObject"}};
unsigned Kind(const ObjRef& ref) noexcept {
    if(!ref.obj || !Readable(ref.obj,8) || !image)return 0;
    const auto* table=At<const unsigned char*>(ref.obj,0);
    if(reinterpret_cast<std::uintptr_t>(table)<8 || !Readable(table-8,8))return 0;
    const auto* col=At<const unsigned char*>(table-8,0);
    for(const auto& type:types) {
        const auto* original=image+type.vtable;
        if(!Readable(original-8,(static_cast<std::size_t>(type.slots)+1)*8) || At<const void*>(original-8,0)!=col)continue;
        if(At<const void*>(original,22*8)!=image+Setter(type.vtable))return 0;
        if(!Readable(col,24) || At<unsigned>(col,0)!=1 || At<unsigned>(col,4)!=0 || At<unsigned>(col,8)!=0 || image+At<unsigned>(col,20)!=col)return 0;
        const auto* descriptor=image+At<unsigned>(col,12);const auto n=std::strlen(type.name);
        if(!Readable(descriptor+16,n+7) || std::memcmp(descriptor+16,".?AV",4) || std::memcmp(descriptor+20,type.name,n) ||
           std::memcmp(descriptor+20+n,"@@",3) || !Readable(table,static_cast<std::size_t>(type.slots)*8))return 0;
        for(unsigned slot=0;slot<type.slots;++slot)if(slot!=4 && slot!=5 && slot!=7) {
            const auto* function=At<const void*>(table,static_cast<std::size_t>(slot)*8);
            if(function!=At<const void*>(original,static_cast<std::size_t>(slot)*8) &&
               (slot!=14 || !EnemyControlOwnsTargetSelector(ref.obj,function)))return 0;
        }
        return type.vtable;
    }
    return 0;
}
bool Weak(const unsigned char* at,ObjRef* out) noexcept {
    if(!out || !Readable(at,16))return false;
    *out={At<const void*>(at,0),At<const void*>(at,8)};
    if(!out->obj && !out->ctrl)return true;
    if(!out->ctrl)return false;
    const auto life=Identity(*out);
    if(life==Life::gone){*out={};return true;}
    return life==Life::live;
}
Born* Recorded(const ObjRef& child) noexcept {
    for(auto& row:born)if(row.child.obj==child.obj && row.child.ctrl==child.ctrl)return &row;
    return nullptr;
}
bool HasChildren(const ObjRef& parent) noexcept {
    for(const auto& row:born)if(row.child && Same(row.parent,parent))return true;
    return false;
}
void Sweep() noexcept {
    // Only dead leaves can leave. A bounded queue keeps a deep retained lineage from
    // turning scene cleanup into repeated quadratic scans or freeing an ancestor early.
    unsigned children[kBornLimit]{},queue[kBornLimit]{},queued=0,read=0;
    for(const auto& row:born)if(row.child && row.parentIndex<kBornLimit && Same(born[row.parentIndex].child,row.parent))++children[row.parentIndex];
    for(unsigned i=0;i<kBornLimit;++i)if(born[i].child && !children[i] && Identity(born[i].child)==Life::gone)queue[queued++]=i;
    while(read<queued) {
        const unsigned index=queue[read++];auto& row=born[index];const unsigned parent=row.parentIndex;
        const bool linked=parent<kBornLimit && Same(born[parent].child,row.parent);
        Drop(row.parent);Drop(row.child);row={};
        if(linked && children[parent] && --children[parent]==0 && Identity(born[parent].child)==Life::gone)queue[queued++]=parent;
    }
    for(auto& row:mirrors)if(row.owner.obj &&
        (Identity(row.owner)==Life::gone || (Readable(row.control,16) && At<LONG>(row.control,8)<=0))) {
        Drop(row.owner);Drop({row.field,row.control});row={};
    }
}
bool Room() noexcept {Sweep();for(const auto& row:born)if(!row.child)return true;return false;}
bool Record(const ObjRef& parent,const ObjRef& child,int team,int last,bool spawner=false,int cache=-1,bool acceptedDeletedParent=false) noexcept {
    if(!Team(team) || last<0 || last>=7 || (acceptedDeletedParent ? !KnownCurrent(parent) : Identity(parent)!=Life::live) ||
       Identity(child)!=Life::live || Same(parent,child))return false;
    if(auto* old=Recorded(child))return Same(old->parent,parent) && old->team==team && old->last==last && old->spawner==spawner;
    Sweep();Born* slot=nullptr;
    for(auto& row:born) {
        if(row.child.obj==child.obj && row.child.ctrl!=child.ctrl)return false;
        if(!slot && !row.child)slot=&row;
    }
    if(!slot || !Hold(parent,acceptedDeletedParent))return false;
    if(!Hold(child)){Drop(parent);return false;}
    *slot={parent,child,team,last,cache,spawner};
    if(const auto* ancestor=Recorded(parent))slot->parentIndex=static_cast<unsigned>(ancestor-born);
    const auto index=static_cast<unsigned>(slot-born);
    for(auto& row:born)if(row.child && &row!=slot && row.parentIndex==kBornLimit && Same(row.parent,child))row.parentIndex=index;
    return true;
}
bool AncestorFrom(const ObjRef& root,const Born& child) noexcept {
    const Born* row=&child;
    for(unsigned depth=0;depth<kBornLimit;++depth) {
        if(Same(root,row->child) || Same(root,row->parent))return true;
        if(row->parentIndex>=kBornLimit || !Same(born[row->parentIndex].child,row->parent))return false;
        row=&born[row->parentIndex];
    }
    return false;
}
bool Ancestor(const ObjRef& root,const ObjRef& child) noexcept {
    if(Same(root,child))return true;
    const auto* row=Recorded(child);return row && AncestorFrom(root,*row);
}
bool LeaseOwner(const ObjRef& member,ObjRef* out) noexcept {
    ObjRef current=member;
    for(unsigned depth=0;depth<kBornLimit;++depth) {
        if(controlled_pvp::OwnsMember(current)){ObjRef root{};if(!controlled_pvp::ReadControlRoot(current,&root))return false;if(out)*out=root;return true;}
        const auto* row=Recorded(current);if(!row)return false;
        current=row->parent;
    }
    return false;
}
const void* GeneratorControl(const ObjRef& owner,const unsigned char* generator) noexcept {
    const auto kind=Kind(owner);const auto* object=static_cast<const unsigned char*>(owner.obj);
    if(kind==kDrop || kind==kCarrier)
        return Readable(object+0xD60,16) && At<const void*>(object,0xD60)==generator ? At<const void*>(object,0xD68) : nullptr;
    if(kind==kAnchor && Readable(object+0x10B0,0x40))
        for(unsigned at=0x10B0;at<0x10F0;at+=16)if(At<const void*>(object,at)==generator)return At<const void*>(object,at+8);
    return nullptr;
}
bool Original(const ObjRef& member,int* team,int* last) noexcept {
    if(!team || !last)return false;
    if(controlled_pvp::HasTemporaryTeam(member))
        return controlled_pvp::ReadBaselineForMember(member,team,last) && Team(*team) && *last>=0 && *last<7;
    if(Identity(member)!=Life::live)return false;
    *team=At<int>(member.obj,kTeam);*last=At<int>(member.obj,0x318);return Team(*team) && *last>=0 && *last<7;
}
Mirror* Cache(const ObjRef& owner,const unsigned char* field,const void* control,int baseline,bool remember) noexcept {
    for(auto& row:mirrors)if(Same(row.owner,owner) && row.field==field && row.control==control){if(remember && Team(baseline))row.baseline=baseline;return &row;}
    if(!remember || !Team(baseline) || !control || !Readable(control,16) || At<LONG>(control,8)<=0)return nullptr;
    Sweep();Mirror* slot=nullptr;for(auto& row:mirrors)if(!row.owner.obj){slot=&row;break;}
    if(!slot || !Hold(owner))return nullptr;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(const_cast<void*>(control))+12));
    *slot={owner,field,control,baseline};return slot;
}
bool Append(const ObjRef& ref,ObjRef* out,unsigned capacity,unsigned& count) noexcept {
    if(Identity(ref)!=Life::live)return false;
    if(count)if(auto* row=Recorded(ref))if(row->adopted && !Same(ref,out[0])) {
        if(!controlled_pvp::OwnsMember(ref))row->adopted=false; // Its old independent lease ended; a new probe must include it again.
        else if(controlled_pvp::OwnsMember(out[0]))return true;
    }
    for(unsigned i=0;i<count;++i)if(out[i].obj==ref.obj)return out[i].ctrl==ref.ctrl;
    if(count==capacity)return false;out[count++]=ref;return true;
}
bool Emit(const Emitter& value,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    for(unsigned i=0;i<count;++i)if(out[i].object==value.object)
        return out[i].kind==value.kind && Same(out[i].member,value.member) && out[i].incarnation==value.incarnation && Same(out[i].storage,value.storage);
    if(count==capacity)return false;out[count++]=value;return true;
}
bool MirrorEmit(const ObjRef& owner,const unsigned char* field,const void* control,int baseline,
                Emitter* out,unsigned capacity,unsigned& count,EmitterKind kind=EmitterKind::ownerTeamMirror) noexcept {
    if(!Readable(field,4) || !Team(At<int>(field,0)) || !control || !Readable(control,16) || At<LONG>(control,8)<=0 || !Team(baseline))return false;
    return Emit({kind,owner,field,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control))},out,capacity,count);
}
bool Derived(const ObjRef& owner,const void* object,const void* control,EmitterKind kind,
             Emitter* out,unsigned capacity,unsigned& count) noexcept {
    return control && Emit({kind,owner,object,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control))},out,capacity,count);
}
bool Generator(const ObjRef& owner,const unsigned char* generator,const void* control,ObjRef* members,unsigned memberCapacity,unsigned& used,
               Emitter* emitters,unsigned emitterCapacity,unsigned& emitted) noexcept {
    if(!generator && !control)return true;
    if(!generator || !control || !Readable(control,16) || At<LONG>(control,8)<=0 || !Readable(generator,0x328) ||
       At<const void*>(generator,0xE0)!=owner.obj)return false;
    // 6E1F25/33 resets current config +80 from the original config +28: mirror both team fields.
    const auto* cache=generator+0xC0;
    if(!MirrorEmit(owner,generator+0x68,control,At<int>(generator,0x68),emitters,emitterCapacity,emitted) ||
       !MirrorEmit(owner,cache,control,At<int>(cache,0),emitters,emitterCapacity,emitted))return false;
    const auto total=At<std::size_t>(generator,0x20),reserved=At<std::size_t>(generator,0x18);
    const auto* data=At<const unsigned char*>(generator,0x10);
    if(total>reserved || total>memberCapacity || (total && (!data || !Readable(data,total*0x20))))return false;
    for(std::size_t i=0;i<total;++i)for(unsigned offset:{0u,16u}) {
        ObjRef child{};if(!Weak(data+i*0x20+offset,&child))return false;
        if(child && !Append(child,members,memberCapacity,used))return false;
    }
    for(unsigned at:{0x230u,0x318u}) {
        const auto* head=At<const unsigned char*>(generator,at);const auto expected=At<std::size_t>(generator,at+8);
        if(expected>memberCapacity || !Readable(head,16))return false;
        const auto* node=At<const unsigned char*>(head,0);const auto* previous=head;std::size_t walked=0;
        while(node!=head) {
            if(walked>=expected || !Readable(node,0x20) || At<const void*>(node,8)!=previous)return false;
            ObjRef child{};if(!Weak(node+0x10,&child))return false;
            if(child && !Append(child,members,memberCapacity,used))return false;
            ++walked;previous=node;node=At<const unsigned char*>(node,0);
        }
        if(walked!=expected || At<const void*>(head,8)!=previous)return false;
    }
    return true;
}
bool BigBatteries(const ObjRef& owner,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(owner.obj);
    if(!Readable(object+0x1170,16))return false;
    const auto* array=At<const unsigned char*>(object,0x1170);
    const auto* arrayControl=At<const void*>(object,0x1178);
    if(!array && !arrayControl)return true;
    if(!array || !arrayControl || !Readable(arrayControl,16) || At<LONG>(arrayControl,8)<=0 || !Readable(array,24))return false;
    const auto* begin=At<const unsigned char*>(array,0);const auto* end=At<const unsigned char*>(array,8);
    const auto* limit=At<const unsigned char*>(array,16);
    const auto first=reinterpret_cast<std::uintptr_t>(begin),last=reinterpret_cast<std::uintptr_t>(end),reserved=reinterpret_cast<std::uintptr_t>(limit);
    if(last<first || reserved<last || (last-first)%16 || (last-first)/16>capacity-count || (last>first && !Readable(begin,last-first)))return false;
    for(std::size_t i=0;i<(last-first)/16;++i) {
        const auto* battery=At<const unsigned char*>(begin,i*16);const auto* control=At<const void*>(begin,i*16+8);
        if(!battery && !control)continue;
        if(!battery || !control || !Readable(control,16) || At<LONG>(control,8)<=0 || !Readable(battery,0x2C0) ||
           At<const void*>(battery,0x2B0)!=owner.obj || At<const void*>(battery,0x2B8)!=owner.ctrl)return false;
        const auto n=At<std::size_t>(battery,0x288),allocated=At<std::size_t>(battery,0x280);
        const auto* muzzles=At<const unsigned char*>(battery,0x278);
        if(n>allocated || n>(std::numeric_limits<std::size_t>::max)()/0xD0 || (n && (!muzzles || !Readable(muzzles,n*0xD0))))return false;
        if(!EnemyControlNativePvpCollectBigBattery(owner,battery,control,out,capacity,&count))return false;
    }
    return true;
}
bool Own(const ObjRef& member,ObjRef* members,unsigned memberCapacity,unsigned& used,Emitter* emitters,unsigned emitterCapacity,unsigned& emitted) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);const unsigned kind=Kind(member);
    if(!kind || !enemy_pvp_native::CollectTree(member,members,memberCapacity,&used))return false;
    if(kind==kTime) {
        if(!Readable(object,0x1164))return false;
        const auto* head=At<const unsigned char*>(object,0x1128);const auto expected=At<std::size_t>(object,0x1130);
        if(expected>memberCapacity || !Readable(head,16))return false;
        const auto* node=At<const unsigned char*>(head,0);const auto* previous=head;std::size_t walked=0;
        while(node!=head) {
            if(walked>=expected || !Readable(node,0x20) || At<const void*>(node,8)!=previous)return false;
            const auto* component=At<const unsigned char*>(node,0x10);const auto* control=At<const void*>(node,0x18);
            if(!component || !control || !Readable(control,16) || At<LONG>(control,8)<=0 || !Readable(component,0x58) ||
               At<const void*>(component,0x18)!=member.obj)return false;
            ObjRef child{};if(!Weak(component+0x48,&child))return false;
            if(child && (Kind(child)!=kAnchor && Kind(child)!=kCapsule))return false;
            if(child && !Append(child,members,memberCapacity,used))return false;
            ++walked;previous=node;node=At<const unsigned char*>(node,0);
        }
        if(walked!=expected || At<const void*>(head,8)!=previous)return false;
        return MirrorEmit(member,object+0x1160,member.ctrl,At<int>(object,0x1160),emitters,emitterCapacity,emitted,EmitterKind::ownerTeamCache);
    }
    if(kind==kDrop || kind==kCarrier) {
        if(!Readable(object+0xD60,16))return false;
        return Generator(member,At<const unsigned char*>(object,0xD60),At<const void*>(object,0xD68),members,memberCapacity,used,emitters,emitterCapacity,emitted);
    }
    if(kind==kAnchor) {
        if(!Readable(object,0x1180))return false;
        ObjRef barrier{};if(!Weak(object+0x1088,&barrier))return false;
        if(barrier && (Kind(barrier)!=kBarrier || !Append(barrier,members,memberCapacity,used)))return false;
        for(unsigned at=0x10B0;at<0x10F0;at+=16)
            if(!Generator(member,At<const unsigned char*>(object,at),At<const void*>(object,at+8),members,memberCapacity,used,emitters,emitterCapacity,emitted))return false;
        return BigBatteries(member,emitters,emitterCapacity,emitted) &&
            Derived(member,member.obj,member.ctrl,EmitterKind::liveOwnerLastTeam,emitters,emitterCapacity,emitted);
    }
    if(kind==kCapsule) {
        if(!Readable(object,0x10A0))return false;
        const auto total=At<std::size_t>(object,0x1098),reserved=At<std::size_t>(object,0x1090);
        const auto* data=At<const unsigned char*>(object,0x1088);
        if(total>reserved || total>memberCapacity || (total && (!data || !Readable(data,total*0x40))))return false;
        for(std::size_t i=0;i<total;++i) {
            ObjRef child{};if(!Weak(data+i*0x40+0x30,&child))return false;
            if(child && !Append(child,members,memberCapacity,used))return false;
        }
        return true;
    }
    if(kind==kBarrier) {
        if(!Readable(object,0xFA8))return false;
        int original=At<int>(object,0xF98),last=0;
        if(controlled_pvp::ReadBaselineForMember(member,&last,&original))original=last;
        return MirrorEmit(member,object+0xF98,member.ctrl,original,emitters,emitterCapacity,emitted,EmitterKind::ownerTeamCache);
    }
    if(kind==kSpawner) {
        if(!Readable(object,0x720))return false;
        const ObjRef owner{At<const void*>(object,0x6C8),At<const void*>(object,0x6D0)};
        const auto* record=Recorded(member);
        if(record) {if(!record->spawner || !Same(record->parent,owner))return false;}
        else if(Identity(owner)!=Life::live)return false;
        const int original=record && record->spawner ? record->cache : At<int>(object,0x620);
        // This cache is consumed by the SpawnerObject itself. It can survive its creator:
        // use the actual storage member's lifetime, while validating the captured creator pair.
        return MirrorEmit(member,object+0x620,member.ctrl,original,emitters,emitterCapacity,emitted);
    }
    return false;
}
// Each member contributes its own +550 tree without overwriting a previously accumulated set.
bool OwnAppend(const ObjRef& member,ObjRef* members,unsigned capacity,unsigned& used,Emitter* emitters,unsigned emitCapacity,unsigned& emitted) noexcept {
    ObjRef local[controlled_pvp::kMaxMembers]{};unsigned n=0;
    if(!Own(member,local,capacity,n,emitters,emitCapacity,emitted))return false;
    for(unsigned i=0;i<n;++i)if(!Append(local[i],members,capacity,used))return false;
    return true;
}
bool TimeChildren(const ObjRef& root,ObjRef* children,unsigned capacity,unsigned* count,bool acceptedDeleted=false) noexcept {
    *count=0;
    if(Kind(root)!=kTime || (acceptedDeleted ? !KnownCurrent(root) : Identity(root)!=Life::live) || !Readable(root.obj,0x1138))return false;
    const auto* head=At<const unsigned char*>(root.obj,0x1128);const auto expected=At<std::size_t>(root.obj,0x1130);
    if(expected>capacity || !Readable(head,16))return false;
    const auto* node=At<const unsigned char*>(head,0);const auto* previous=head;std::size_t walked=0;
    while(node!=head) {
        if(walked>=expected || !Readable(node,0x20) || At<const void*>(node,8)!=previous)return false;
        const auto* component=At<const unsigned char*>(node,0x10);const auto* control=At<const void*>(node,0x18);
        if(!component || !control || !Readable(control,16) || At<LONG>(control,8)<=0 || !Readable(component,0x58) || At<const void*>(component,0x18)!=root.obj)return false;
        ObjRef child{};if(!Weak(component+0x48,&child))return false;
        if(child && !Append(child,children,capacity,*count))return false;
        ++walked;previous=node;node=At<const unsigned char*>(node,0);
    }
    return walked==expected && At<const void*>(head,8)==previous;
}
bool CurrentEmitters(const ObjRef& member,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    ObjRef members[controlled_pvp::kMaxMembers]{};unsigned used=0;count=0;
    return Kind(member) && Own(member,members,controlled_pvp::kMaxMembers,used,out,capacity,count);
}
unsigned char* __fastcall CreateFinal(void* manager,const float* matrix,const wchar_t* path,void* init,void* rawSpawner) noexcept {
    auto* spawner=static_cast<unsigned char*>(rawSpawner);ObjRef parent{},leaseOwner{};std::uint64_t token=0;unsigned char* created=nullptr;
    __try {
        if(!spawner || !Readable(spawner,0x720))return reinterpret_cast<CreateFn>(image+kCreate)(manager,matrix,path,init);
        const auto producer=ObjRef::Of(spawner);const auto* pending=Recorded(producer);
        parent={At<const void*>(spawner,0x6C8),At<const void*>(spawner,0x6D0)};
        if(!parent || (pending ? (!pending->spawner || !Same(pending->parent,parent)) : Identity(parent)!=Life::live))
            return reinterpret_cast<CreateFn>(image+kCreate)(manager,matrix,path,init);
        bool leased=LeaseOwner(parent,&leaseOwner);
        if(leased && Identity(leaseOwner)!=Life::live) {
            // A detached pending object can update before the controller's next frame.
            // Restore its cache now, before the native final factory consumes the temporary side.
            controlled_pvp::FaultForMember(leaseOwner,controlled_pvp::Result::invalidIdentity);
            if(!pending || !Team(pending->cache))return nullptr;
            const Emitter cache{EmitterKind::ownerTeamMirror,producer,spawner+0x620,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(producer.ctrl))};
            int current=-99;
            if(EnemyControlTransportPvpRead(cache,&current)!=Life::live || (current!=pending->cache && !EnemyControlTransportPvpWrite(cache,pending->cache)))return nullptr;
            leased=false;
        }
        controlled_pvp::SpawnProof proof{};
        if(leased && (!pending || !pending->spawner || !Team(pending->cache) || !Room() || !EnemyControlNativePvpSpawnProof(path,&proof) ||
            !controlled_pvp::PrepareSpawns(leaseOwner,proof,1,&token))) {
            controlled_pvp::FaultForMember(leaseOwner,controlled_pvp::Result::unsupported);return nullptr;
        }
        auto* child=reinterpret_cast<CreateFn>(image+kCreate)(manager,matrix,path,init);created=child;
        if(!child){controlled_pvp::FinishSpawns(token);return nullptr;}
        if(leased) {
            const auto ref=ObjRef::Of(child);
            reinterpret_cast<TeamFn>(image+kSetTeam)(child,pending->cache,true);
            const bool captured=Record(producer,ref,At<int>(child,kTeam),At<int>(child,0x318));
            if(captured)Recorded(ref)->adopted=true; // Exclude it from the main group before AdoptSpawn synchronously recollects.
            const bool adopted=captured && controlled_pvp::AdoptSpawn(token,leaseOwner,ref);
            controlled_pvp::FinishSpawns(token);
            if(!adopted) {
                controlled_pvp::FaultForMember(leaseOwner,controlled_pvp::Result::nativeFailure);
                reinterpret_cast<DeleteFn>(image+kDelete)(child);created=nullptr;return nullptr;
            }
        }
        return child;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        controlled_pvp::FinishSpawns(token);
        if(created) {
            __try {if(Readable(created,kSelfCtrl+8) && Identity(ObjRef::Of(created))==Life::live)reinterpret_cast<DeleteFn>(image+kDelete)(created);}
            __except(EXCEPTION_EXECUTE_HANDLER){incomplete=true;}
        }
        if(parent && LeaseOwner(parent,&leaseOwner))controlled_pvp::FaultForMember(leaseOwner,controlled_pvp::Result::nativeFailure);
        return nullptr;
    }
}
void __fastcall SetFinalTeam(void* object,int team,bool registered,void* rawSpawner) noexcept {
    ObjRef parent{};int wanted=team;bool skip=false;
    __try {
        const auto* spawner=static_cast<const unsigned char*>(rawSpawner);
        if(spawner && Readable(spawner,0x720)) {
            const auto producer=ObjRef::Of(spawner);const auto* pending=Recorded(producer);
            parent={At<const void*>(spawner,0x6C8),At<const void*>(spawner,0x6D0)};
            if(!parent || (pending ? (!pending->spawner || !Same(pending->parent,parent)) : Identity(parent)!=Life::live))parent={};
            const auto child=ObjRef::Of(object);
            // The baseline SetTeam already ran before AdoptSpawn. A redundant virtual
            // slot-22 cascade would reopen children after their per-member mode commit.
            if(const auto* row=Recorded(child))skip=row->adopted && controlled_pvp::OwnsMember(child);
            ObjRef leaseOwner{};
            if(!skip && parent && LeaseOwner(parent,&leaseOwner) && (Identity(leaseOwner)!=Life::live || !controlled_pvp::ReadAppliedTeam(leaseOwner,&wanted))) {
                if(pending && pending->spawner && Team(pending->cache))wanted=pending->cache;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){incomplete=true;parent={};wanted=team;skip=false;}
    if(skip)return;
    // Keep the original native failure/return contract; metadata faults must never suppress or retry it.
    reinterpret_cast<TeamFn>(image+kSetTeam)(object,wanted,registered);
    __try {
        const auto child=ObjRef::Of(object);
        if(parent && !Recorded(child) && !Record(ObjRef::Of(rawSpawner),child,At<int>(object,kTeam),At<int>(object,0x318)))incomplete=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){incomplete=true;}
}
ObjRef* __fastcall CreatePending(void* manager,ObjRef* out,const float* matrix,void* init,void* rawGenerator) noexcept {
    ObjRef parent{},leaseOwner{};int baseline=-99;
    __try {
        const auto* generator=static_cast<const unsigned char*>(rawGenerator);
        if(generator && Readable(generator,0xE8)) {
            const auto* object=At<const unsigned char*>(generator,0xE0);
            if(object && Readable(object,kSelfCtrl+8))parent=ObjRef::Of(object);
        }
        if(parent && Kind(parent) && Identity(parent)==Life::live) {
            const auto* field=generator+0xC0;baseline=At<int>(field,0);
            bool known=!controlled_pvp::HasTemporaryTeam(parent);
            if(!known) {
                const auto* control=GeneratorControl(parent,generator);
                const Emitter binding{EmitterKind::ownerTeamMirror,parent,field,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control))};
                known=control && controlled_pvp::ReadEmitterBaseline(binding,&baseline);
                if(!known)for(const auto& row:mirrors)if(Same(row.owner,parent) && row.field==field){baseline=row.baseline;known=true;break;}
            }
            if(!known){baseline=-99;LeaseOwner(parent,&leaseOwner);controlled_pvp::FaultForMember(leaseOwner,controlled_pvp::Result::invalidIdentity);}
            if(LeaseOwner(parent,&leaseOwner) && (!Team(baseline) || !Room()))
                controlled_pvp::FaultForMember(leaseOwner,controlled_pvp::Result::capacity);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){incomplete=true;parent={};baseline=-99;}
    // The pending-spawner caller has no null-safe exit. Always call the original
    // exactly once; any leased/budget failure is rejected by safe final creation.
    auto* result=reinterpret_cast<SpawnerFn>(image+kCreateSpawner)(manager,out,matrix,init);
    __try {
        if(parent && Kind(parent) && out && out->obj && !Record(parent,*out,At<int>(out->obj,kTeam),At<int>(out->obj,0x318),true,baseline))incomplete=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){incomplete=true;}
    return result;
}
bool CapsuleBaseline(const ObjRef& parent,int constructorLast,int* team,int* last) noexcept {
    if(!team || !last || Kind(parent)!=kTime || !KnownCurrent(parent) || constructorLast<0 || constructorLast>=7)return false;
    const int current=At<int>(parent.obj,kTeam);if(!Team(current))return false;
    *team=current;*last=constructorLast;
    if(current<0)return true; // A physically closed parent creates a closed pod, preserving its constructor last-team.
    if(controlled_pvp::HasTemporaryTeam(parent)) {
        int original=0,originalLast=0;
        if(!controlled_pvp::ReadBaselineForMember(parent,&original,&originalLast) || !Team(original) || originalLast<0 || originalLast>=7)return false;
        *team=original<0 ? originalLast : original;
    }
    *last=*team;return true;
}
// TimeShip's component factory has no null-safe failure exit. This hook is strictly
// pass-through: capture constructor baselines and retain lineage, never reject creation.
unsigned char* __fastcall CreateTimePart(void* manager,const float* matrix,const wchar_t* path,void* init,void* rawParent) noexcept {
    auto* child=reinterpret_cast<CreateFn>(image+kCreate)(manager,matrix,path,init);
    __try {
        const auto* parentObject=static_cast<const unsigned char*>(rawParent);
        if(!child || !parentObject || !Readable(parentObject,kSelfCtrl+8))return child;
        const auto parent=ObjRef::Of(parentObject),ref=ObjRef::Of(child);
        const unsigned kind=Kind(ref);
        if(Kind(parent)!=kTime || !KnownCurrent(parent) || (kind!=kAnchor && kind!=kCapsule))return child;
        int team=At<int>(child,kTeam),last=At<int>(child,0x318);
        if(kind==kCapsule) {
            if(!CapsuleBaseline(parent,last,&team,&last)){incomplete=true;controlled_pvp::FaultForMember(parent,controlled_pvp::Result::invalidIdentity);return child;}
        }
        // All four generators were configured inside the Anchor constructor, before this object
        // is returned to TimeShip and before its slot-22 team cascade can affect any part.
        if(kind==kAnchor && Readable(child,0x10F0))for(unsigned at=0x10B0;at<0x10F0;at+=16) {
            const auto* generator=At<const unsigned char*>(child,at);const auto* control=At<const void*>(child,at+8);
            if(!generator && !control)continue;
            if(!generator || !control || !Readable(generator,0xE8) || At<const void*>(generator,0xE0)!=child ||
               !Cache(ref,generator+0x68,control,At<int>(generator,0x68),true) || !Cache(ref,generator+0xC0,control,At<int>(generator,0xC0),true))incomplete=true;
        }
        if(!Record(parent,ref,team,last,false,-1,true)) {
            incomplete=true;
            if(controlled_pvp::OwnsMember(parent))controlled_pvp::FaultForMember(parent,controlled_pvp::Result::capacity);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){incomplete=true;}
    return child;
}
using CapsuleCrueFn=ObjRef*(__fastcall*)(void*,ObjRef*,const unsigned char*);
void ReleaseStrong(ObjRef& ref) noexcept {
    if(ref.ctrl) {
        auto* control=static_cast<unsigned char*>(const_cast<void*>(ref.ctrl));
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+8))==0) {
            reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(control,0)[0])(control);
            if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+12))==0)
                reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(control,0)[1])(control);
        }
    }
    ref={};
}
const wchar_t* CapsuleResource(const unsigned char* params) noexcept {
    if(!Readable(params,0x8C) || At<int>(params,0x88)!=1)return nullptr;
    const auto* functor=At<const unsigned char*>(params,0x78);
    if(!Readable(functor,16) || At<const void*>(functor,0)!=image+0x1791E18 ||
       !Readable(image+0x1791E18,24) || At<const void*>(image+0x1791E18,16)!=image+0x1BC3F0)return nullptr;
    const auto* text=At<const unsigned char*>(functor,8);if(!Readable(text,32))return nullptr;
    const auto size=At<std::size_t>(text,16),capacity=At<std::size_t>(text,24);
    if(!size || size>259 || size>capacity)return nullptr;
    const auto* value=capacity<8 ? reinterpret_cast<const wchar_t*>(text) : At<const wchar_t*>(text,0);
    return Readable(value,(size+1)*sizeof(wchar_t)) && value[size]==0 ? value : nullptr;
}
bool CapsuleSeatAvailable(const ObjRef& parent) noexcept {
    if(Kind(parent)!=kCapsule || Identity(parent)!=Life::live || !Readable(parent.obj,0x10A0))return false;
    const auto cursor=At<unsigned>(parent.obj,0x107C);const auto count=At<std::size_t>(parent.obj,0x1098),capacity=At<std::size_t>(parent.obj,0x1090);
    const auto* data=At<const unsigned char*>(parent.obj,0x1088);
    return count<=capacity && count<=controlled_pvp::kMaxMembers && cursor<count && data && Readable(data,count*0x40);
}
ObjRef* __fastcall CreateCapsuleCrue(void* manager,ObjRef* out,const unsigned char* params,const ObjRef* source) noexcept {
    ObjRef parent{},leaseOwner{};unsigned char* created=nullptr;std::uint64_t token=0;
    __try {
        if(source && Readable(source,16))parent=*source;
        const bool typed=parent && Kind(parent)==kCapsule && Identity(parent)==Life::live;
        const bool leased=typed && LeaseOwner(parent,&leaseOwner);
        controlled_pvp::SpawnProof proof{};
        if(leased) {
            const auto* path=CapsuleResource(params);
            if(!out || !CapsuleSeatAvailable(parent) || !Room() || !path || !EnemyControlNativePvpSpawnProof(path,&proof) ||
               !controlled_pvp::PrepareSpawns(leaseOwner,proof,1,&token)) {
                if(out)*out={};controlled_pvp::FaultForMember(leaseOwner,controlled_pvp::Result::unsupported);return out;
            }
        }
        auto* result=reinterpret_cast<CapsuleCrueFn>(image+kCapsuleCrueCreate)(manager,out,params);
        if(!typed || !out || !out->obj){controlled_pvp::FinishSpawns(token);return result;}
        created=static_cast<unsigned char*>(const_cast<void*>(out->obj));
        const bool captured=Identity(*out)==Life::live && At<int>(created,kTeam)==1 && At<int>(created,0x318)==1 && Record(parent,*out,1,1);
        if(leased) {
            if(captured)Recorded(*out)->adopted=true;
            const bool adopted=captured && controlled_pvp::AdoptSpawn(token,leaseOwner,*out);
            controlled_pvp::FinishSpawns(token);token=0;
            if(!adopted) {
                controlled_pvp::FaultForMember(leaseOwner,controlled_pvp::Result::nativeFailure);
                reinterpret_cast<DeleteFn>(image+kDelete)(created);created=nullptr;ReleaseStrong(*out);return out;
            }
        } else if(!captured)incomplete=true;
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        controlled_pvp::FinishSpawns(token);
        if(created) {
            __try {if(Readable(created,kSelfCtrl+8) && Identity(ObjRef::Of(created))==Life::live)reinterpret_cast<DeleteFn>(image+kDelete)(created);}
            __except(EXCEPTION_EXECUTE_HANDLER){incomplete=true;}
            if(out)ReleaseStrong(*out);
        } else if(out)*out={};
        if(parent && LeaseOwner(parent,&leaseOwner))controlled_pvp::FaultForMember(leaseOwner,controlled_pvp::Result::nativeFailure);
        return out;
    }
}
void* CapsuleCrueThunk(const void* site) noexcept {
    // Original AS frame holds its strong Capsule pair at caller RSP+40; call entry adds 8.
    unsigned char code[]={0x4C,0x8D,0x4C,0x24,0x48,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto address=reinterpret_cast<std::uintptr_t>(&CreateCapsuleCrue);std::memcpy(code+7,&address,8);
    return AllocateNearCode(site,code,sizeof(code));
}
void* FiveArgThunk(const void* site,void* target,unsigned sourceRegister) noexcept {
    unsigned char code[]={0x48,0x83,0xEC,0x38,0x48,0x89,0x5C,0x24,0x20,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x83,0xC4,0x38,0xC3};
    code[6]=static_cast<unsigned char>(sourceRegister==6 ? 0x74 : sourceRegister==13 ? 0x6C : 0x5C);
    if(sourceRegister==13)code[4]=0x4C;
    const auto address=reinterpret_cast<std::uintptr_t>(target);std::memcpy(code+11,&address,8);
    return AllocateNearCode(site,code,sizeof(code));
}
void* TeamThunk(const void* site) noexcept {
    unsigned char code[]={0x49,0x89,0xD9,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto address=reinterpret_cast<std::uintptr_t>(&SetFinalTeam);std::memcpy(code+5,&address,8);
    return AllocateNearCode(site,code,sizeof(code));
}
bool CallIs(unsigned site,unsigned target) noexcept {
    if(!Readable(image+site,5) || image[site]!=0xE8)return false;
    return image+site+5+At<std::int32_t>(image+site,1)==image+target;
}
bool CapsuleProofCollect(void*,const ObjRef& root,ObjRef* members,unsigned capacity,unsigned* count,Emitter* emitters,unsigned emitterCapacity,unsigned* emitted) noexcept {
    if(count)*count=0;if(emitted)*emitted=0;
    if(!count || !emitted || !members || !emitters || !capacity || Kind(root)!=kCapsule || Identity(root)!=Life::live)return false;
    unsigned used=0,written=0;
    if(!Own(root,members,capacity,used,emitters,emitterCapacity,written) || used!=1 || written || !Same(members[0],root))return false;
    *count=1;return true;
}
controlled_pvp::SpawnProof CapsuleProof() noexcept {
    controlled_pvp::SpawnProof proof{};proof.hooks.collect=&CapsuleProofCollect;proof.maxMembers=1;return proof;
}
bool CapsuleBirthRoom(unsigned count) noexcept {
    if(!count || count>kBornLimit)return false;
    Sweep();unsigned available=0;for(const auto& row:born)if(!row.child)++available;
    return count<=available;
}
} // namespace transport_pvp_detail

bool EnemyControlTransportPvpInstall() noexcept {
    using namespace transport_pvp_detail;
    if(attempted)return installed;attempted=true;
    __try {
        if(!StaticProof() || !CallIs(kCreateSpawnerSite,kCreateSpawner) || !CallIs(kCreateFinalSite,kCreate) || !CallIs(kTeamFinalSite,kSetTeam) || !CallIs(kCreatePartSite,kCreate) || !CallIs(kCapsuleCrueSite,kCapsuleCrueCreate))return false;
        auto* pending=FiveArgThunk(image+kCreateSpawnerSite,reinterpret_cast<void*>(&CreatePending),6);
        auto* final=FiveArgThunk(image+kCreateFinalSite,reinterpret_cast<void*>(&CreateFinal),3);
        auto* team=TeamThunk(image+kTeamFinalSite);
        auto* part=FiveArgThunk(image+kCreatePartSite,reinterpret_cast<void*>(&CreateTimePart),13);
        auto* capsule=CapsuleCrueThunk(image+kCapsuleCrueSite);
        if(!pending || !final || !team || !part || !capsule)return false;
        bool changed=false;
        if(!RedirectCall(image+kCreateSpawnerSite,image+kCreateSpawner,pending,changed) ||
           !RedirectCall(image+kCreateFinalSite,image+kCreate,final,changed) ||
           !RedirectCall(image+kTeamFinalSite,image+kSetTeam,team,changed) ||
           !RedirectCall(image+kCreatePartSite,image+kCreate,part,changed) ||
           !RedirectCall(image+kCapsuleCrueSite,image+kCapsuleCrueCreate,capsule,changed))return false;
        installed=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void EnemyControlTransportPvpReset() noexcept {
    using namespace transport_pvp_detail;
    for(auto& row:born)if(row.child){Drop(row.parent);Drop(row.child);row={};}
    for(auto& row:mirrors)if(row.owner.obj){Drop(row.owner);Drop({row.field,row.control});row={};}
    incomplete=false;
}
bool EnemyControlTransportPvpReady() noexcept {return transport_pvp_detail::installed && transport_pvp_detail::StaticProof();}
bool EnemyControlTransportPvpSupports(const ObjRef& ref) noexcept {return transport_pvp_detail::Kind(ref)!=0;}
bool EnemyControlTransportPvpCanPrepareCapsules(const ObjRef& parent,unsigned count) noexcept {
    using namespace transport_pvp_detail;
    __try {
        if(Kind(parent)!=kTime || !KnownCurrent(parent) || !count || count>controlled_pvp::kMaxMembers)return false;
        if(!controlled_pvp::OwnsMember(parent))return true; // Ordinary control remains usable when the optional PVP hooks are unavailable.
        return EnemyControlTransportPvpReady() && !incomplete && CapsuleBirthRoom(count) &&
            controlled_pvp::CanPrepareAcceptedSpawns(parent,CapsuleProof(),count);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlTransportPvpPrepareCapsules(const ObjRef& parent,unsigned count,std::uint64_t* token) noexcept {
    using namespace transport_pvp_detail;
    if(token)*token=0;if(!token)return false;
    if(!EnemyControlTransportPvpCanPrepareCapsules(parent,count))return false;
    return !controlled_pvp::OwnsMember(parent) || controlled_pvp::PrepareAcceptedSpawns(parent,CapsuleProof(),count,token);
}
bool EnemyControlTransportPvpAdoptCapsule(std::uint64_t token,const ObjRef& parent,const ObjRef& child) noexcept {
    using namespace transport_pvp_detail;
    __try {
        if(Kind(parent)!=kTime || !KnownCurrent(parent) || Kind(child)!=kCapsule || Identity(child)!=Life::live)return false;
        if(!token) {
            if(controlled_pvp::OwnsMember(parent) || controlled_pvp::OwnsMember(child))return false;
            ObjRef parts[controlled_pvp::kMaxMembers]{};unsigned count=0;
            if(!TimeChildren(parent,parts,controlled_pvp::kMaxMembers,&count,true))return false;
            for(unsigned i=0;i<count;++i)if(Same(parts[i],child))return true;
            return false;
        }
        if(!controlled_pvp::OwnsMember(parent))return false;
        auto* row=Recorded(child);if(!row || !Same(row->parent,parent) || !Team(row->team) || row->last<0 || row->last>=7)return false;
        ObjRef members[1]{};Emitter emitters[1]{};unsigned used=0,emitted=0;
        if(!CapsuleProofCollect(nullptr,child,members,1,&used,emitters,0,&emitted))return false;
        // 3EC8D6 has now copied the parent's temporary current team. Restore both
        // native birth fields before generic captures them; -1 alone preserves the wrong last-team.
        reinterpret_cast<TeamFn>(image+kSetTeam)(const_cast<void*>(child.obj),row->team,row->team>=0);
        if(!Readable(static_cast<const unsigned char*>(child.obj)+0x318,4,true))return false;
        Put<int>(const_cast<void*>(child.obj),0x318,row->last);
        if(At<int>(child.obj,kTeam)!=row->team || At<int>(child.obj,0x318)!=row->last)return false;
        row->adopted=true;
        return controlled_pvp::AdoptSpawn(token,parent,child);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void EnemyControlTransportPvpFinishCapsules(std::uint64_t token) noexcept {controlled_pvp::FinishSpawns(token);}
bool EnemyControlTransportPvpCollect(const ObjRef& root,ObjRef* members,unsigned memberCapacity,unsigned* memberCount,
                                    controlled_pvp::Emitter* emitters,unsigned emitterCapacity,unsigned* emitterCount) noexcept {
    using namespace transport_pvp_detail;
    if(memberCount)*memberCount=0;if(emitterCount)*emitterCount=0;
    if(!memberCount || !emitterCount || !members || !emitters || !memberCapacity || memberCapacity>controlled_pvp::kMaxMembers ||
       emitterCapacity>controlled_pvp::kMaxEmitters || !EnemyControlTransportPvpReady() ||
       (incomplete && !controlled_pvp::OwnsMember(root)))return false;
    __try {
        unsigned used=0,emitted=0;
        if(!Append(root,members,memberCapacity,used))return false;
        for(const auto& row:born)if(row.child && AncestorFrom(root,row) && Identity(row.child)==Life::live &&
            !Append(row.child,members,memberCapacity,used))return false;
        for(unsigned i=0;i<used;++i) {
            if(Kind(members[i])) {
                if(!OwnAppend(members[i],members,memberCapacity,used,emitters,emitterCapacity,emitted))return false;
            } else {
                ObjRef list[controlled_pvp::kMaxMembers]{};Emitter weapons[controlled_pvp::kMaxEmitters]{};unsigned n=0,e=0;
                if(!EnemyControlNativePvpCollect(members[i],list,memberCapacity,&n,weapons,emitterCapacity,&e))return false;
                for(unsigned j=0;j<n;++j)if(!Append(list[j],members,memberCapacity,used))return false;
                for(unsigned j=0;j<e;++j)if(!Emit(weapons[j],emitters,emitterCapacity,emitted))return false;
            }

        }
        *memberCount=used;*emitterCount=emitted;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
controlled_pvp::Life EnemyControlTransportPvpRead(const controlled_pvp::Emitter& emitter,int* team) noexcept {
    using namespace transport_pvp_detail;
    if(!team || !emitter.object || !emitter.incarnation)return Life::invalid;
    if(EnemyControlNativePvpCacheRetained(emitter))return EnemyControlNativePvpReadRetainedCache(emitter,team);
    __try {
        const auto life=Identity(emitter.member);if(life!=Life::live)return life;
        if(!Kind(emitter.member))return EnemyControlNativePvpRead(emitter,team);
        Emitter current[controlled_pvp::kMaxEmitters]{};unsigned count=0;
        if(!CurrentEmitters(emitter.member,current,controlled_pvp::kMaxEmitters,count))return Life::invalid;
        for(unsigned i=0;i<count;++i)if(current[i].kind==emitter.kind && Same(current[i].member,emitter.member) &&
            current[i].object==emitter.object && current[i].incarnation==emitter.incarnation && Same(current[i].storage,emitter.storage)) {
            *team=Scalar(emitter.kind) ? At<int>(emitter.object,0) :
                At<int>(emitter.member.obj,emitter.kind==EmitterKind::liveOwnerLastTeam ? 0x318 : kTeam);
            return Team(*team) ? Life::live : Life::invalid;
        }
        return Life::gone;
    } __except(EXCEPTION_EXECUTE_HANDLER){return Life::invalid;}
}
bool EnemyControlTransportPvpWrite(const controlled_pvp::Emitter& emitter,int team) noexcept {
    using namespace transport_pvp_detail;
    if(!Team(team))return false;
    if(EnemyControlNativePvpCacheRetained(emitter))return EnemyControlNativePvpWriteRetainedCache(emitter,team);
    __try {
        if(!Kind(emitter.member))return EnemyControlNativePvpWrite(emitter,team);
        int current=-1;if(EnemyControlTransportPvpRead(emitter,&current)!=Life::live)return false;
        if(!Scalar(emitter.kind))return current==team;
        if(!Readable(emitter.object,4,true))return false;
        int baseline=current;
        if(controlled_pvp::OwnsMember(emitter.member) && !controlled_pvp::ReadEmitterBaseline(emitter,&baseline))return false;
        if(!Cache(emitter.member,static_cast<const unsigned char*>(emitter.object),reinterpret_cast<const void*>(static_cast<std::uintptr_t>(emitter.incarnation)),baseline,true))return false;
        Put<int>(const_cast<void*>(emitter.object),0,team);return At<int>(emitter.object,0)==team;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlTransportPvpBaseline(const ObjRef& root,const ObjRef& member,int rootTeam,int rootLast,int* team,int* last) noexcept {
    using namespace transport_pvp_detail;
    if(!team || !last)return false;
    __try {
        if(Same(root,member)){*team=rootTeam;*last=rootLast;return Team(*team) && *last>=0 && *last<7;}
        if(const auto* row=Recorded(member))if(Ancestor(root,member)){*team=row->team;*last=row->last;return true;}
        if(controlled_pvp::ReadBaseline(root,member,team,last))return true;
        const unsigned kind=Kind(member);
        // Capsules are the dynamic TimeShip component: 3EC8B3/D6 explicitly applies current root team.
        if(Kind(root)==kTime && kind==kCapsule) {
            ObjRef parts[controlled_pvp::kMaxMembers]{};unsigned count=0;
            if(!TimeChildren(root,parts,controlled_pvp::kMaxMembers,&count))return false;
            for(unsigned i=0;i<count;++i)if(Same(parts[i],member)) {
                *team=rootTeam;*last=rootTeam>=0 ? rootTeam : rootLast;return Team(*team) && *last>=0 && *last<7;
            }
            return false;
        }
        if(kind==kBarrier) {
            ObjRef parts[controlled_pvp::kMaxMembers]{};unsigned count=0;
            if(Kind(root)==kTime && TimeChildren(root,parts,controlled_pvp::kMaxMembers,&count)) {
                for(unsigned i=0;i<count;++i)if(Kind(parts[i])==kAnchor) {
                    ObjRef barrier{};if(!Weak(static_cast<const unsigned char*>(parts[i].obj)+0x1088,&barrier))return false;
                    if(Same(barrier,member)){int parentTeam=0;if(!Original(parts[i],&parentTeam,last))return false;*team=*last;return true;}
                }
            }
            for(const auto& row:born)if(row.child && Kind(row.child)==kAnchor && Ancestor(root,row.child) && Identity(row.child)==Life::live) {
                ObjRef barrier{};if(!Weak(static_cast<const unsigned char*>(row.child.obj)+0x1088,&barrier))return false;
                if(Same(barrier,member)) {int parentTeam=0;if(!Original(row.child,&parentTeam,last))return false;*team=*last;return true;}
            }
            if(Kind(root)==kAnchor) {ObjRef barrier{};if(!Weak(static_cast<const unsigned char*>(root.obj)+0x1088,&barrier))return false;
                if(Same(barrier,member)){*team=rootLast;*last=rootLast;return rootLast>=0 && rootLast<7;}}
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlTransportPvpBaseline(const controlled_pvp::Emitter& emitter,int ownerTeam,int ownerLast,int* team) noexcept {
    using namespace transport_pvp_detail;
    if(!team)return false;
    if(EnemyControlNativePvpCacheRetained(emitter))return EnemyControlNativePvpBaseline(emitter,ownerTeam,ownerLast,team);
    __try {
        if(!Kind(emitter.member))return EnemyControlNativePvpBaseline(emitter,ownerTeam,ownerLast,team);
        if(emitter.kind==EmitterKind::ownerLastTeamCache){*team=ownerLast;return Team(*team);}
        if(emitter.kind==EmitterKind::ownerTeamCache){*team=ownerTeam;return Team(*team);}
        if(emitter.kind==EmitterKind::ownerTeamMirror) {
            if(Kind(emitter.member)==kBarrier){*team=ownerLast;return ownerLast>=0 && ownerLast<7;}
            for(const auto& row:born)if(row.child && row.spawner && Same(row.child,emitter.member) &&
                emitter.object==static_cast<const unsigned char*>(row.child.obj)+0x620 &&
                emitter.incarnation==reinterpret_cast<std::uintptr_t>(row.child.ctrl)){*team=row.cache;return Team(*team);}
            for(const auto& row:mirrors)if(Same(row.owner,emitter.member) && row.field==emitter.object &&
                reinterpret_cast<std::uintptr_t>(row.control)==emitter.incarnation){*team=row.baseline;return Team(*team);}
            return false;
        }
        *team=emitter.kind==EmitterKind::liveOwnerLastTeam ? ownerLast : ownerTeam;return Team(*team);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace crew
