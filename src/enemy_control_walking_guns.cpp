#include "enemy_control_walking_guns.h"
#include "enemy_control.h"
#include "enemy_control_weapon_aim.h"
#include "enemy_control_action_native.h"
#include "enemy_control_walking_net.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace crew::enemy_native {
namespace walking_guns {
constexpr unsigned kMaxGuns=256,kMaxRoots=8;
constexpr unsigned kStart=0x34E180,kBusy=0x34E640,kStop=0x6D3EC0;
struct Print { unsigned at;unsigned char bytes[16]; };
constexpr Print kPrints[]={
    {0x34E180,{0x48,0x89,0x5C,0x24,0x08,0x55,0x48,0x8D,0x6C,0x24,0xA9,0x48,0x81,0xEC,0x00,0x01}},
    {0x34E640,{0x83,0x79,0x38,0x00,0x7F,0x27,0x48,0x8B,0x01,0xF6,0x80,0xC0,0x02,0x00,0x00,0x01}},
    {0x6D3EC0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x83,0xA1,0xC0,0x02,0x00,0x00,0xFE,0x48,0x8B,0xD9}},
    {0xD7480,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0x4D,0x8B,0xD0}},
    {0x53060,{0x48,0x8B,0x11,0x44,0x8B,0x49,0x08,0x4C,0x39,0x4A,0x18,0x76,0x26,0x48,0x8B,0x42}},
    {0x52DE0,{0x4C,0x8B,0x09,0x44,0x8B,0x51,0x08,0x44,0x8B,0xC2,0x4E,0x8D,0x04,0x85,0x00,0x00}},
    {0x52F30,{0x48,0x83,0xEC,0x28,0xE8,0x17,0x00,0x00,0x00,0xF2,0x0F,0x5A,0xC0,0x48,0x83,0xC4}},
};
struct Ref { unsigned char* object=nullptr;unsigned char* control=nullptr; };
struct Gun {
    Ref ref{};std::uint64_t sequence=0,seed=0;float point[3]{};
    std::uint64_t acceptedSerial=0,acceptedCommand=0,acceptedSeed=0;float acceptedPoint[3]{};unsigned acceptedPhase=0;
    bool owned=false,result=false,inFlight=false;
};
struct Root { Ref ref{};const NativeProfile* profile=nullptr;Gun guns[kMaxGuns]{};bool faulted=false,restoring=false; };
Root roots[kMaxRoots]{};
unsigned char* admittedImage=nullptr;
bool installed=false;
using StartFn=void(__fastcall*)(void*);
using BusyFn=bool(__fastcall*)(const void*);
struct Handle { void* tree;unsigned index,pad; };
using ChildFn=Handle*(__fastcall*)(const Handle*,Handle*,const wchar_t*);
using CountFn=int(__fastcall*)(const Handle*);
using IndexFn=unsigned(__fastcall*)(const Handle*,unsigned);
using FloatFn=float(__fastcall*)(const Handle*);
struct Label { char id[32]{};wchar_t name[48]{}; };
const Label* Name(unsigned ordinal) noexcept {
    struct Names {
        Label values[kMaxGuns]{};
        Names() noexcept {
            for(unsigned i=0;i<kMaxGuns;++i) {
                std::snprintf(values[i].id,sizeof(values[i].id),"walking_gun_%u",i);
                _snwprintf_s(values[i].name,_TRUNCATE,L"炮位 %u：独立开火",i+1);
            }
        }
    };
    static const Names names;
    return ordinal<kMaxGuns ? names.values+ordinal : nullptr;
}
bool Native() noexcept { return installed && image && admittedImage==image; }
bool Large(const NativeLease& lease) noexcept {
    if(!lease.profile || !lease.profile->name || lease.profile->kind!=Kind::walking ||
       std::strcmp(lease.profile->name,"EDF6_Berserker_Large"))return false;
    std::size_t count=0;const auto profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(lease.profile==profiles+i)return true;
    return false;
}
bool Current(const Ref& ref,bool alive=true) noexcept {
    if(!ref.object || !Readable(ref.control,16) || At<LONG>(ref.control,8)<=0 ||
       !Readable(ref.object,0x1AC0) || At<const void*>(ref.object,0x30)!=ref.control)return false;
    return !alive || (!(ref.object[0x18]&4) && !ref.object[0x2E8]);
}
Ref Reference(const NativeLease& lease) noexcept {
    return {lease.object,static_cast<unsigned char*>(const_cast<void*>(lease.control))};
}
bool Retain(Ref ref) noexcept {
    if(!Readable(ref.control,16,true) || At<LONG>(ref.control,8)<=0 ||
       At<LONG>(ref.control,12)<=0 || At<LONG>(ref.control,12)==0x7FFFFFFF)return false;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(ref.control+12));return true;
}
bool Release(Ref& ref) noexcept {
    if(!ref.control){ref={};return true;}
    if(!Readable(ref.control,16,true) || At<LONG>(ref.control,12)<=0)return false;
    const auto table=At<void* const*>(ref.control,0);
    if(At<LONG>(ref.control,12)==1 && (!Readable(table,16) || !table[1]))return false;
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ref.control+12))==0)
        reinterpret_cast<StartFn>(table[1])(ref.control);
    ref={};return true;
}
bool Pin(const Ref& ref) noexcept {
    if(!Readable(ref.control,16,true))return false;
    auto* uses=reinterpret_cast<volatile LONG*>(ref.control+8);LONG was=*uses;
    while(was>0 && was<0x7FFFFFFF) {
        const LONG actual=InterlockedCompareExchange(uses,was+1,was);
        if(actual==was)return true;was=actual;
    }
    return false;
}
void Unpin(const Ref& ref) noexcept {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ref.control+8))!=0)return;
    auto* const* table=At<void* const*>(ref.control,0);
    reinterpret_cast<StartFn>(table[0])(ref.control);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ref.control+12))==0)
        reinterpret_cast<StartFn>(table[1])(ref.control);
}
bool Child(const Handle& parent,const wchar_t* key,Handle* out) noexcept {
    *out={nullptr,0xFFFFFFFFu,0};
    reinterpret_cast<ChildFn>(image+0xD7480)(&parent,out,key);
    return out->tree!=nullptr;
}
// Only the existing Hector marker identifies its non-firing shield placeholder.
// A generic native/custom weapon with a zero range is never inferred to be a shield.
bool Shields(const NativeLease& lease,std::size_t count,bool* mask) noexcept {
    std::memset(mask,0,kMaxGuns*sizeof(bool));
    Handle root{lease.object+0xA8,0,0},marker{},ranges{};
    if(!Child(root,L"edf41_hector",&marker))return true;
    if(count!=2 || !Child(marker,L"ranges",&ranges) || reinterpret_cast<CountFn>(image+0x53060)(&ranges)!=2)return false;
    for(unsigned i=0;i<2;++i) {
        Handle value{ranges.tree,reinterpret_cast<IndexFn>(image+0x52DE0)(&ranges,i),0};
        const float range=reinterpret_cast<FloatFn>(image+0x52F30)(&value);
        if(!std::isfinite(range) || range<0)return false;
        mask[i]=range==0;
    }
    return true;
}
bool RawRoster(const NativeLease& lease,const unsigned char** rows,std::size_t* count,bool alive=true) noexcept {
    if(!Native() || !Large(lease) || !Current(Reference(lease),alive))return false;
    *rows=At<const unsigned char*>(lease.object,0x1AA8);*count=At<std::size_t>(lease.object,0x1AB8);
    const auto capacity=At<std::size_t>(lease.object,0x1AB0);
    return *count && *count<=kMaxGuns && *count<=capacity && Readable(*rows,*count*16);
}
bool Roster(const NativeLease& lease,const unsigned char** rows,std::size_t* count,bool* shields,bool alive=true) noexcept {
    return RawRoster(lease,rows,count,alive) && Shields(lease,*count,shields);
}
bool Entry(const NativeLease& lease,const unsigned char* row,Ref* ref) noexcept {
    auto* gun=At<unsigned char*>(row,0);auto* control=At<unsigned char*>(row,8);
    if(!control || !Readable(control,16) || At<LONG>(control,8)<=0)return false;
    if(gun!=control+0x10 || !Readable(gun,0x90) || At<const void*>(gun,0x10)!=lease.object)return false;
    const auto battery=At<const unsigned char*>(gun,0),batteryControl=At<const unsigned char*>(gun,8);
    if(!batteryControl || !Readable(batteryControl,16) || At<LONG>(batteryControl,8)<=0 ||
       battery!=batteryControl+0x10 || !Readable(battery,0x6C0) ||
       At<const void*>(battery,0x2B0)!=lease.object || At<const void*>(battery,0x2B8)!=lease.control)return false;
    *ref={gun,control};return true;
}
bool ResolveRaw(const NativeLease& lease,unsigned ordinal,Ref* ref) noexcept {
    const unsigned char* rows=nullptr;std::size_t count=0;
    return ordinal<kMaxGuns && RawRoster(lease,&rows,&count) && ordinal<count &&
        Entry(lease,rows+static_cast<std::size_t>(ordinal)*16,ref);
}

bool Resolve(const NativeLease& lease,unsigned ordinal,Ref* ref,bool alive=true) noexcept {
    const unsigned char* rows=nullptr;std::size_t count=0;bool shields[kMaxGuns]{};
    return ordinal<kMaxGuns && Roster(lease,&rows,&count,shields,alive) && ordinal<count && !shields[ordinal] &&
        Entry(lease,rows+static_cast<std::size_t>(ordinal)*16,ref);
}
Root* Find(const Ref& root) noexcept {
    for(auto& entry:roots)if(entry.ref.object==root.object && entry.ref.control==root.control)return &entry;
    return nullptr;
}
Root* Acquire(const NativeLease& lease) noexcept {
    if(auto* root=Find(Reference(lease)))return root;
    for(auto& root:roots)if(!root.ref.control) {
        const auto ref=Reference(lease);if(!Retain(ref))return nullptr;
        root.ref=ref;root.profile=lease.profile;return &root;
    }
    return nullptr;
}
struct Definition {
    const unsigned char* tree=nullptr;const unsigned char* raw=nullptr;
    unsigned nodes=0,visited=0,depth=0;unsigned path[32]{};
    std::size_t bytes=0;std::uint64_t hash=14695981039346656037ULL;
};
constexpr std::size_t kMaxDefinitionBytes=4*1024*1024,kMaxRawOffset=16*1024*1024;
bool Mix(Definition& definition,const void* bytes,std::size_t size) noexcept {
    if(size>kMaxDefinitionBytes-definition.bytes || (size && !Readable(bytes,size)))return false;
    const auto data=static_cast<const unsigned char*>(bytes);
    for(std::size_t i=0;i<size;++i){definition.hash^=data[i];definition.hash*=1099511628211ULL;}
    definition.bytes+=size;return true;
}
bool Number(Definition& definition,unsigned value) noexcept {return Mix(definition,&value,sizeof(value));}
const unsigned char* RawAt(const Definition& definition,std::int64_t offset,std::size_t bytes) noexcept {
    if(offset<0 || static_cast<std::uint64_t>(offset)>kMaxRawOffset || bytes>kMaxRawOffset-static_cast<std::size_t>(offset))return nullptr;
    const auto base=reinterpret_cast<std::uintptr_t>(definition.raw);
    if(base>UINTPTR_MAX-static_cast<std::uintptr_t>(offset))return nullptr;
    const auto pointer=reinterpret_cast<const unsigned char*>(base+static_cast<std::uintptr_t>(offset));
    return Readable(pointer,bytes) ? pointer : nullptr;
}
std::int64_t RawOffset(const Definition& definition,const unsigned char* pointer) noexcept {
    return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(pointer)-reinterpret_cast<std::uintptr_t>(definition.raw));
}
bool NotOverridden(const Definition& definition,unsigned index) noexcept {
    if(index>=definition.nodes || !Readable(definition.tree,0x58))return false;
    const auto count=At<std::size_t>(definition.tree,0x18);
    if(count>1048576)return false;
    if(index>=count)return true;
    const auto map=At<const unsigned char*>(definition.tree,8);
    return Readable(map,static_cast<std::size_t>(index)*4+4) && At<std::uint32_t>(map,static_cast<std::size_t>(index)*4)==UINT32_MAX;
}
const unsigned char* RawNode(const Definition& definition,unsigned index) noexcept {
    if(!NotOverridden(definition,index))return nullptr;
    return RawAt(definition,16+static_cast<std::int64_t>(index)*16,16);
}
bool RawText(Definition& definition,std::int64_t offset) noexcept {
    unsigned count=0;
    for(;count<=1024;++count) {
        const auto word=RawAt(definition,offset+static_cast<std::int64_t>(count)*2,2);
        if(!word)return false;
        if(!At<std::uint16_t>(word,0))break;
    }
    return count<=1024 && Number(definition,count) && Mix(definition,RawAt(definition,offset,static_cast<std::size_t>(count)*2),static_cast<std::size_t>(count)*2);
}
bool HashNode(Definition& definition,unsigned index) noexcept {
    if(++definition.visited>4096 || definition.depth>=32)return false;
    for(unsigned i=0;i<definition.depth;++i)if(definition.path[i]==index)return false;
    const auto node=RawNode(definition,index);if(!node)return false;
    const unsigned type=At<unsigned>(node,8);if(type>4 || !Number(definition,type))return false;
    if(type==0)return Mix(definition,node,8); // exact literal double, never expression evaluation
    const auto blockOffset=RawOffset(definition,node)+At<std::int32_t>(node,0);
    if(type==1)return RawText(definition,blockOffset);
    if(type==2 || type==4) {
        const auto block=RawAt(definition,blockOffset,8);if(!block)return false;
        const unsigned bytes=At<unsigned>(block,0);if(bytes>1024*1024 || !Number(definition,bytes))return false;
        const auto data=RawAt(definition,blockOffset+At<std::int32_t>(block,4),bytes);
        return data && Mix(definition,data,bytes); // type 4 VM bytes, not difficulty-dependent result
    }
    const auto block=RawAt(definition,blockOffset,16);if(!block)return false;
    const unsigned count=At<unsigned>(block,12),names=At<unsigned>(block,4);
    if(count>1024 || names>count || !Number(definition,count))return false;
    const auto children=RawAt(definition,blockOffset+At<std::int32_t>(block,8),static_cast<std::size_t>(count)*4);
    const auto nameTable=RawAt(definition,blockOffset+At<std::int32_t>(block,0),static_cast<std::size_t>(names)*8);
    if((count && !children) || (names && !nameTable))return false;
    for(unsigned n=0;n<names;++n)if(At<unsigned>(nameTable+static_cast<std::size_t>(n)*8,4)>=count)return false;
    definition.path[definition.depth++]=index;bool valid=true;
    for(unsigned child=0;child<count && valid;++child) {
        const unsigned char* name=nullptr;
        for(unsigned n=0;n<names;++n) {
            const auto entry=nameTable+static_cast<std::size_t>(n)*8;
            if(At<unsigned>(entry,4)==child){if(name){valid=false;break;}name=entry;}
        }
        if(!valid)break;
        if(name)valid=Number(definition,1) && RawText(definition,RawOffset(definition,name)+At<std::int32_t>(name,0));
        else valid=Number(definition,0);
        if(valid)valid=HashNode(definition,At<unsigned>(children,static_cast<std::size_t>(child)*4));
    }
    --definition.depth;return valid;
}

bool RawGunDefinition(const NativeLease& lease,unsigned ordinal,std::uint64_t* digest) noexcept {
    if(!digest)return false;
    __try {
        Ref ref{};if(!ResolveRaw(lease,ordinal,&ref))return false;
        Handle root{lease.object+0xA8,0,0},guns{};
        if(!Child(root,L"gun_control",&guns) || !Readable(guns.tree,0x58))return false;
        Definition definition{};definition.tree=static_cast<const unsigned char*>(guns.tree);
        definition.raw=At<const unsigned char*>(guns.tree,0x50);
        if(!Readable(definition.raw,16) || At<unsigned>(definition.raw,0)!=0x4F475344 ||
           At<unsigned>(definition.raw,4)!=16 || At<unsigned>(definition.raw,12)!=16)return false;
        definition.nodes=At<unsigned>(definition.raw,8);
        if(!definition.nodes || definition.nodes>65536 || !NotOverridden(definition,0))return false;
        const auto table=RawNode(definition,guns.index);
        if(!table || At<unsigned>(table,8)!=3)return false;
        const auto blockOffset=RawOffset(definition,table)+At<std::int32_t>(table,0);
        const auto block=RawAt(definition,blockOffset,16);if(!block)return false;
        const unsigned count=At<unsigned>(block,12);
        if(count!=At<std::size_t>(lease.object,0x1AB8) || ordinal>=count)return false;
        const auto children=RawAt(definition,blockOffset+At<std::int32_t>(block,8),static_cast<std::size_t>(count)*4);
        if(!children)return false;
        const unsigned selected=At<unsigned>(children,static_cast<std::size_t>(ordinal)*4);
        constexpr char domain[]="EDF6.WalkingGun.Definition.1";
        if(!Mix(definition,domain,sizeof(domain)) || !Mix(definition,lease.profile->name,std::strlen(lease.profile->name)+1) ||
           !Number(definition,count) || !Number(definition,ordinal) || !HashNode(definition,selected) || !definition.hash)return false;
        *digest=definition.hash;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}


bool Busy(const Ref& gun) noexcept {
    return reinterpret_cast<BusyFn>(image+kBusy)(gun.object);
}
bool Active(const Ref& gun) noexcept {
    const auto battery=At<const unsigned char*>(gun.object,0);
    return At<int>(gun.object,0x38)>=0 || (At<unsigned>(battery,0x2C0)&1) ||
        At<unsigned>(battery,0x2E0)>0 || At<unsigned>(battery,0x2E8)>0;
}
bool Cancel(const NativeLease& lease,unsigned ordinal,Gun& gun) noexcept {
    if(!gun.owned)return true;
    if(!Readable(gun.ref.control,16))return false;
    if(At<LONG>(gun.ref.control,8)<=0){gun.owned=false;return true;}
    const auto rootRef=Reference(lease);
    if(Readable(rootRef.control,16) && At<LONG>(rootRef.control,8)<=0){gun.owned=false;return true;}
    Ref current{};
    // A removed/replaced native slot is never followed through the old pointer.
    if(!Current(Reference(lease),false) || !Resolve(lease,ordinal,&current,false) ||
       current.object!=gun.ref.object || current.control!=gun.ref.control)return false;
    auto* battery=At<unsigned char*>(current.object,0);
    if(!Readable(current.object+0x38,4,true) || !Readable(battery+0x2C0,0x2C,true))return false;
    bool pinned=false,done=false;
    __try {
        pinned=Pin(current);if(!pinned)return false;
        // -1 is the constructor's unscheduled state; this adapter only starts an
        // idle gun and owns the delay it scheduled. Native Stop clears pending,
        // charge/fire, effects and firing SE. Cached persistent beam deletion is
        // a separate native path handled by accepted-action preparation.
        Put<int>(current.object,0x38,-1);
        reinterpret_cast<StartFn>(image+kStop)(battery);
        done=!Active(current);
    } __finally {if(pinned)Unpin(current);}
    return done;
}
}
using namespace walking_guns;
bool InstallWalkingGunControl() noexcept {
    if(Native())return true;
    if(!image || (admittedImage && admittedImage!=image))return false;
    __try {
        for(const auto& print:kPrints)if(!Readable(image+print.at,16) || std::memcmp(image+print.at,print.bytes,16))return false;
        admittedImage=image;installed=InstallWalkingAcceptedActions();return installed;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
unsigned ListWalkingGunAbilities(const NativeLease& lease,NativeAbility* out,unsigned capacity) noexcept {
    __try {
        const unsigned char* rows=nullptr;std::size_t count=0;bool shields[kMaxGuns]{};unsigned total=0;
        if(!Roster(lease,&rows,&count,shields))return 0;
        for(unsigned i=0;i<count;++i) {
            Ref ref{};if(shields[i] || !Entry(lease,rows+static_cast<std::size_t>(i)*16,&ref))continue;
            const auto label=Name(i);if(!label)return 0;
            if(out && total<capacity)out[total]={label->id,label->name,0,0,AbilityKind::walkingGun,nullptr,static_cast<std::int32_t>(i)};
            ++total;
        }
        return total;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
bool ExecuteWalkingGun(const NativeLease& lease,unsigned ordinal,std::uint64_t sequence,std::uint64_t seed) noexcept {
    Root* pending=nullptr;
    __try {
        Ref ref{};
        if(!sequence || !Resolve(lease,ordinal,&ref) || !EnemyControlTargetActive(lease.object) ||
           !EnemyControlWeaponAimReady(lease.profile))return false;
        bool authority=false;const bool bound=NativeActionRole(lease.object,&authority);
        // The retained controller context is immutable across closing/fault.
        // Unknown ownership is never interpreted as an offline emitter. A bound
        // accepted-action role wins over any room/session probe.
        bool networked=bound;
        if(!bound && !EnemyControlNetworkContext(lease.object,&networked))return false;
        if(networked) {
            if(!bound)return false;
            if(!authority)return true; // accepted Apply owns all guest mutations/emissions
        }
        auto* root=Acquire(lease);if(!root || root->faulted || root->restoring)return false;pending=root;
        auto& gun=root->guns[ordinal];
        if(gun.inFlight)return false; // do not overwrite an outer native call's command journal
        if(gun.ref.control && (gun.ref.object!=ref.object || gun.ref.control!=ref.control))return false;
        if(sequence<gun.sequence)return false;
        if(sequence==gun.sequence)return gun.seed==seed && gun.result;
        if(!gun.ref.control){if(!Retain(ref))return false;gun.ref=ref;}
        gun.sequence=sequence;gun.seed=seed;gun.result=false;
        if(gun.inFlight || Busy(ref) || Active(ref) || !Readable(ref.object,0x90,true) ||
           (networked && !WalkingAcceptedCanQueue(lease)))return false;
        float point[3]{};if(!EnemyControlWeaponAim(lease.object,point))return false;
        for(float value:point)if(!std::isfinite(value))return false;
        const Ref pinnedRoot=root->ref;bool rootPinned=false,gunPinned=false;
        __try {
            rootPinned=Pin(pinnedRoot);if(!rootPinned)return false;
            gunPinned=Pin(ref);if(!gunPinned)return false;
            Ref current{};if(!Resolve(lease,ordinal,&current) || current.object!=ref.object || current.control!=ref.control || Busy(ref) || Active(ref))return false;
            std::memcpy(gun.point,point,sizeof(point));gun.seed=seed;gun.owned=true;gun.inFlight=true;
            if(!SetSeed(lease,seed) || !Resolve(lease,ordinal,&current) || current.object!=ref.object || current.control!=ref.control ||
               !EnemyControlTargetActive(root->ref.object)){gun.owned=false;return false;}
            // The native gun's own aiming random stream is separate from root
            // +3E8. A semantic accepted seed therefore also enters this stream.
            Put<std::uint64_t>(ref.object,0x80,seed);
            reinterpret_cast<StartFn>(image+kStart)(ref.object);
            gun.result=Current(root->ref) && Resolve(lease,ordinal,&current) && current.object==ref.object &&
                current.control==ref.control && At<int>(ref.object,0x38)>=0;
            if(gun.result && networked && !QueueWalkingAcceptedGun(lease,ordinal,0,sequence,seed,point))gun.result=false;
            if(!gun.result)root->faulted=true;
        } __finally {gun.inFlight=false;if(gunPinned)Unpin(ref);if(rootPinned)Unpin(pinnedRoot);}
        return gun.result;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(pending)pending->faulted=true;return false;}
}
bool ReadWalkingGunState(const NativeLease& lease,unsigned ordinal,enemy_control::Ability* out) noexcept {
    if(!out)return false;
    out->state=enemy_control::AbilityState::unavailable;out->cooldownKnown=false;out->cooldownSeconds=0;
    __try {
        Ref ref{};if(!Resolve(lease,ordinal,&ref))return false;
        const auto* root=Find(Reference(lease));if(root && (root->faulted || root->restoring))return true;
        if(Active(ref))out->state=enemy_control::AbilityState::active;
        else out->state=Busy(ref) ? enemy_control::AbilityState::cooldown : enemy_control::AbilityState::native;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool RestoreWalkingGunControl(const NativeLease& lease) noexcept {
    __try {
        auto* root=Find(Reference(lease));if(!root)return true;
        if(root->restoring)return false;
        for(const auto& gun:root->guns)if(gun.inFlight)return false;
        const Ref pinnedRoot=root->ref;bool pinned=false;
        __try {
            root->restoring=true;
            if(!Readable(pinnedRoot.control,16))return false;
            if(At<LONG>(pinnedRoot.control,8)>0) {
                if(!Current(pinnedRoot,false) || !Pin(pinnedRoot))return false;
                pinned=true;
            }
            for(unsigned i=0;i<kMaxGuns;++i) {
                auto& gun=root->guns[i];
                if(!Cancel(lease,i,gun) || !Release(gun.ref))return false;
                gun={};
            }
            if(!Release(root->ref))return false;
            *root={};return true;
        } __finally {root->restoring=false;if(pinned)Unpin(pinnedRoot);}
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool WalkingGunAimPoint(const void* control,float out[3]) noexcept {
    if(!out)return false;
    __try {
        if(!Native())return false;
        for(auto& root:roots) {
            if(!root.ref.control || !Current(root.ref))continue;
            for(unsigned ordinal=0;ordinal<kMaxGuns;++ordinal) {
                auto& gun=root.guns[ordinal];
                if(!gun.owned || gun.ref.object!=control || !Readable(gun.ref.control,16) || At<LONG>(gun.ref.control,8)<=0)continue;
                NativeLease lease{};lease.object=root.ref.object;lease.control=root.ref.control;lease.profile=root.profile;
                Ref current{};if(!Resolve(lease,ordinal,&current) || current.object!=control || current.control!=gun.ref.control)return false;
                float point[3]{};
                if(!gun.inFlight && EnemyControlTargetActive(root.ref.object) && EnemyControlWeaponAim(root.ref.object,point)) {
                    bool finite=true;for(float value:point)finite=finite && std::isfinite(value);
                    if(finite)std::memcpy(gun.point,point,sizeof(point));
                }
                // Keep the last accepted canonical point while an already-started
                // native burst ends or a failed restore is retried. Never AI aim.
                std::memcpy(out,gun.point,sizeof(gun.point));return true;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return false;
}
bool ResolveWalkingGunKey(const NativeLease& lease,unsigned ordinal,void** out) noexcept {
    if(!out)return false;
    __try {Ref ref{};if(!Resolve(lease,ordinal,&ref))return false;*out=ref.object;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ResolveWalkingControlledGun(const void* control,NativeLease* out,unsigned* ordinal) noexcept {
    if(!out || !ordinal)return false;
    __try {
        if(!Native() || !Readable(control,0x18))return false;
        auto* root=At<unsigned char*>(control,0x10);
        if(!Readable(root,0x1AC0))return false;
        bool authority=false;
        if(!EnemyControlTargetActive(root) && !NativeActionRole(root,&authority))return false;
        std::size_t profilesCount=0;const auto profiles=Profiles(&profilesCount);const NativeProfile* large=nullptr;
        for(std::size_t i=0;i<profilesCount;++i)if(profiles[i].kind==Kind::walking && profiles[i].name &&
            !std::strcmp(profiles[i].name,"EDF6_Berserker_Large")){large=profiles+i;break;}
        if(!large)return false;
        NativeLease lease{};lease.object=root;lease.control=At<const void*>(root,0x30);lease.profile=large;
        const unsigned char* rows=nullptr;std::size_t count=0;bool shields[kMaxGuns]{};
        if(!Roster(lease,&rows,&count,shields))return false;
        for(unsigned i=0;i<count;++i) {
            Ref ref{};if(!shields[i] && Entry(lease,rows+static_cast<std::size_t>(i)*16,&ref) && ref.object==control) {
                *out=lease;*ordinal=i;return true;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return false;
}
bool WalkingGunReady(const NativeLease& lease,unsigned ordinal) noexcept {
    __try {Ref ref{};const auto* root=Find(Reference(lease));return (!root || (!root->faulted && !root->restoring)) && Resolve(lease,ordinal,&ref) && !Active(ref) && !Busy(ref);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool WalkingGunCommitted(const NativeLease& lease,unsigned ordinal) noexcept {
    __try {
        Ref ref{};if(!Resolve(lease,ordinal,&ref))return false;
        const auto* root=Find(Reference(lease));if(!root || root->faulted)return false;
        const auto& gun=root->guns[ordinal];return gun.owned && gun.ref.object==ref.object && gun.ref.control==ref.control && Active(ref);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool WalkingGunDefinition(const NativeLease& lease,unsigned ordinal,std::uint64_t* digest) noexcept {
    if(!digest)return false;
    __try {Ref ref{};return Resolve(lease,ordinal,&ref) && RawGunDefinition(lease,ordinal,digest);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool WalkingGunPreparationCount(const NativeLease& lease,unsigned* out) noexcept {
    if(!out)return false;
    __try {
        const unsigned char* rows=nullptr;std::size_t count=0;
        if(!RawRoster(lease,&rows,&count))return false;
        for(std::size_t i=0;i<count;++i) {Ref ref{};if(!Entry(lease,rows+i*16,&ref))return false;}
        *out=static_cast<unsigned>(count);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ResolveWalkingGunPreparationKey(const NativeLease& lease,unsigned ordinal,void** out,std::uint64_t* digest) noexcept {
    if(!out || !digest)return false;
    __try {
        Ref ref{};std::uint64_t definition=0;
        if(!ResolveRaw(lease,ordinal,&ref) || !RawGunDefinition(lease,ordinal,&definition))return false;
        *out=ref.object;*digest=definition;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool WalkingBatteryDefinition(const NativeLease& lease,unsigned key,std::uint64_t* digest) noexcept {
    if(!digest)return false;
    __try {
        if(Large(lease))return WalkingGunDefinition(lease,key,digest);
        if(!Native() || key || !lease.profile || lease.profile->kind!=Kind::walking)return false;
        bool family=false;std::size_t count=0;const auto profiles=Profiles(&count);
        for(std::size_t i=0;i<count;++i)if(lease.profile==profiles+i) {
            family=profiles[i].vtableRva==0x17AE088 || profiles[i].vtableRva==0x17AF9F8;break;
        }
        if(!family || !lease.object || !Readable(lease.object,0x1A70) || !Readable(lease.control,16) ||
           At<LONG>(lease.control,8)<=0 || At<const void*>(lease.object,0x30)!=lease.control ||
           (lease.object[0x18]&4) || lease.object[0x2E8])return false;
        const auto battery=At<const unsigned char*>(lease.object,0x1A60),control=At<const unsigned char*>(lease.object,0x1A68);
        if(!control || !Readable(control,16) || At<LONG>(control,8)<=0 || battery!=control+0x10 || !Readable(battery,0x6C0) ||
           At<const void*>(battery,0x2B0)!=lease.object || At<const void*>(battery,0x2B8)!=lease.control)return false;
        Handle root{lease.object+0xA8,0,0},selected{};
        if(!Child(root,L"gun_battery_settings",&selected) || !Readable(selected.tree,0x58))return false;
        Definition definition{};definition.tree=static_cast<const unsigned char*>(selected.tree);
        definition.raw=At<const unsigned char*>(selected.tree,0x50);
        if(!Readable(definition.raw,16) || At<unsigned>(definition.raw,0)!=0x4F475344 ||
           At<unsigned>(definition.raw,4)!=16 || At<unsigned>(definition.raw,12)!=16)return false;
        definition.nodes=At<unsigned>(definition.raw,8);
        if(!definition.nodes || definition.nodes>65536 || !NotOverridden(definition,0))return false;
        constexpr char domain[]="EDF6.WalkingBattery.Definition.1";
        if(!Mix(definition,domain,sizeof(domain)) || !Mix(definition,lease.profile->name,std::strlen(lease.profile->name)+1) ||
           !Number(definition,1) || !Number(definition,key) || !HashNode(definition,selected.index) || !definition.hash)return false;
        *digest=definition.hash;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool ApplyWalkingGunAcceptedControl(const NativeLease& lease,unsigned ordinal,unsigned phase,
    std::uint64_t serial,std::uint64_t command,std::uint64_t seed,const float point[3]) noexcept {
    Root* pending=nullptr;
    __try {
        bool authority=false;Ref ref{};
        if(!serial || !command || phase>1 || !point || !NativeActionRole(lease.object,&authority) || authority ||
           !Resolve(lease,ordinal,&ref) || !EnemyControlWeaponAimReady(lease.profile))return false;
        for(unsigned i=0;i<3;++i)if(!std::isfinite(point[i]))return false;
        auto* root=Acquire(lease);if(!root || root->faulted || root->restoring)return false;pending=root;
        auto& gun=root->guns[ordinal];
        if(gun.ref.control && (gun.ref.object!=ref.object || gun.ref.control!=ref.control))return false;
        if(serial<gun.acceptedSerial)return false;
        if(serial==gun.acceptedSerial)return gun.acceptedPhase==phase && gun.acceptedCommand==command &&
            gun.acceptedSeed==seed && !std::memcmp(gun.acceptedPoint,point,sizeof(gun.acceptedPoint));
        if(!gun.ref.control){if(!Retain(ref))return false;gun.ref=ref;}
        if(gun.inFlight)return false;
        const Ref pinnedRoot=root->ref;bool rootPinned=false,gunPinned=false,result=false;
        __try {
            rootPinned=Pin(pinnedRoot);if(!rootPinned)return false;
            gunPinned=Pin(ref);if(!gunPinned)return false;
            gun.inFlight=true;
            // Authority has already admitted this transition. A guest's delayed
            // visual battery state cannot veto it or schedule a second emitter.
            // Native cancellation removes that older local charge/burst first.
            gun.owned=true;
            if(!Cancel(lease,ordinal,gun)){root->faulted=true;return false;}
            if(phase==1){gun.owned=false;result=true;}
            else {
                if(!Readable(ref.object,0x90,true) || !SetSeed(lease,seed))return false;
                Ref still{};
                if(!Resolve(lease,ordinal,&still) || still.object!=ref.object || still.control!=ref.control ||
                   !EnemyControlTargetActive(root->ref.object)){root->faulted=true;return false;}
                std::memcpy(gun.point,point,sizeof(gun.point));gun.inFlight=true;
                Put<std::uint64_t>(ref.object,0x80,seed);
                reinterpret_cast<StartFn>(image+kStart)(ref.object);
                Ref current{};result=Resolve(lease,ordinal,&current) && current.object==ref.object && current.control==ref.control &&
                    At<int>(ref.object,0x38)>=0;
                if(!result)root->faulted=true;
            }
            if(result) {
                gun.acceptedSerial=serial;gun.acceptedCommand=command;gun.acceptedSeed=seed;gun.acceptedPhase=phase;
                std::memcpy(gun.acceptedPoint,point,sizeof(gun.acceptedPoint));
            }
        } __finally {gun.inFlight=false;if(gunPinned)Unpin(ref);if(rootPinned)Unpin(pinnedRoot);}
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(pending)pending->faulted=true;return false;}
}

}
