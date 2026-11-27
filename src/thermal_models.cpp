#include "edf_layout.h"
#include "memory.h"
#include "thermal_models.h"
#include <array>
#include <limits>

namespace crew::thermal {
namespace {
// Native constructors are audited by tests/thermal_model_native_audit.py. These are
// base-class contracts, matched through MSVC RTTI/PMD, not guesses from enemy IDs.
struct Rule { const char* type; std::size_t model; bool array; };
constexpr Rule kRules[]={
    {".?AVHumanoidBase@@",0x6C0,true}, // manager: models +8, count +0x18, stride 0x710
    {".?AVInsectBase@@",0x630,false},
    {".?AVHumanBase@@",0x860,false},
    {".?AVVehicleBase@@",0xE40,false},
    {".?AVBasicAnimationCharacter@@",0x8A0,false},
    {".?AVMonsterBase@@",0x8C0,false},
    {".?AVDeiroi@@",0x600,false},
    {".?AVNephila@@",0x620,false},
    {".?AVUfoSmall507@@",0xC00,false},
    {".?AVUfoSmall515@@",0x620,false},
    {".?AVUfoCarrier@@",0x650,false},
    {".?AVMovingFortress@@",0x5E0,false},
    {".?AVMovingFortress_Battery@@",0x610,false},
    {".?AVMovingFortress_Debris@@",0x5C0,false},
    {".?AVUfoMother511PartsBase@@",0x5E0,false},
    {".?AVHumanoid_BigGreyBoss_PartsBase@@",0x5C0,false},
    {".?AVHumanoid_BigGreyBoss_Meteor@@",0x5C0,false},
    {".?AVAlienTrailer@@",0x5C0,false},
    {".?AVGiantAntEgg@@",0x5C0,false},
    {".?AVAntHill@@",0x5C0,false},
    {".?AVBigNest@@",0x5C0,false},
    {".?AVBombingPlane@@",0x5C0,false},
    {".?AVShootingTarget@@",0x5C0,false},
    {".?AVEventAnimationObject@@",0x5C0,false},
    {".?AVFarEventObject@@",0x5C0,false},
    {".?AVTransporter_Container@@",0x5C0,false},
    {".?AVGeneratorPoll@@",0x7C0,false}
};
constexpr std::size_t kHumanRule=2,kVehicleRule=3;   // kRules' HumanBase, VehicleBase
static_assert(kRules[kHumanRule].model==0x860 && kRules[kVehicleRule].model==0xE40,"HumanBase, VehicleBase");
constexpr std::size_t kHumanoidStride=0x710,kModelBytes=0x4E0,kMostModels=128;
constexpr unsigned kAnimationVtable=0x17C4030;
struct Col { std::uint32_t signature,offset,ctorOffset,type,hierarchy,self; };
struct Hierarchy { std::uint32_t signature,attributes,count,bases; };
struct Base { std::uint32_t type,contained; std::int32_t member,vbase,vdisp; std::uint32_t attributes,hierarchy; };
struct Binding { const void* vtable=nullptr; std::size_t complete=0,member=0; int rule=-1; std::size_t extra=0; };
// A model vtable's verdict (IsModel's RTTI walk), once per vtable.
struct ModelVt { const void* vtable=nullptr; bool model=false; };
struct Cache {
    const unsigned char* base=nullptr;
    std::size_t size=0,count=0,modelCount=0;
    std::array<Binding,256> bindings{};
    std::array<ModelVt,64> models{};
};
// This accessor is explicitly game-thread-only. It never caches a live object/model
// pointer: an object's array may relocate or its parts may be removed between frames.
thread_local Cache cache;

// live: the caller holds the object live and reads under __try (EnemyModelsLive): Readable is not asked.
template<class T> bool Read(const void* p,T* out,bool live=false) noexcept {
    if(!live && !Readable(p,sizeof(T)))return false;
    std::memcpy(out,p,sizeof(T));return true;
}
bool Profile() noexcept {
    if(cache.base==image && cache.size)return true;
    cache=Cache{};cache.base=image;
    IMAGE_DOS_HEADER dos{};
    if(!Read(image,&dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 || dos.e_lfanew>0x1000)return false;
    IMAGE_NT_HEADERS64 nt{};
    if(!Read(image+dos.e_lfanew,&nt) || nt.Signature!=IMAGE_NT_SIGNATURE ||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt.FileHeader.TimeDateStamp!=0x678CCB46 ||
       nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.OptionalHeader.SizeOfImage<=kAnimationVtable+24)return false;
    cache.size=nt.OptionalHeader.SizeOfImage;return true;
}
const unsigned char* Rva(std::uint32_t rva,std::size_t n) noexcept {
    return rva<cache.size && n<=cache.size-rva && Readable(image+rva,n) ? image+rva : nullptr;
}
bool InImage(const void* p,std::size_t n) noexcept {
    const auto begin=reinterpret_cast<std::uintptr_t>(image),at=reinterpret_cast<std::uintptr_t>(p);
    return at>=begin && at-begin<cache.size && n<=cache.size-(at-begin) && Readable(p,n);
}
bool TypeIs(std::uint32_t rva,const char* expected) noexcept {
    const std::size_t n=std::strlen(expected)+1;
    const auto p=Rva(rva,16+n);
    return p && std::memcmp(p+16,expected,n)==0;
}
bool Metadata(const void* object,Col* col,Hierarchy* hierarchy) noexcept {
    const unsigned char* vt=nullptr;
    // EDF4.1 custom classes clone their root vtable outside EDF.dll, retaining
    // the native [-1] COL. The trusted boundary is that complete locator and all
    // its RVA-based type/PMD data in the supported image, not the table allocation.
    if(!Read(object,&vt) || reinterpret_cast<std::uintptr_t>(vt)<8 || !Readable(vt-8,24))return false;
    const void* locator=nullptr;
    if(!Read(vt-8,&locator) || !InImage(locator,sizeof(Col)) || !Read(locator,col) || col->signature!=1 ||
       Rva(col->self,sizeof(Col))!=locator || col->ctorOffset || col->offset>0x10000)return false;
    const auto h=Rva(col->hierarchy,sizeof(Hierarchy));
    if(!h || !Read(h,hierarchy) || !hierarchy->count || hierarchy->count>256 ||
       !Rva(hierarchy->bases,static_cast<std::size_t>(hierarchy->count)*4))return false;
    return true;
}
bool BaseAt(const Hierarchy& h,std::uint32_t i,Base* base) noexcept {
    std::uint32_t rva=0;
    return Read(Rva(h.bases+i*4,4),&rva) && Read(Rva(rva,sizeof(Base)),base);
}
Binding Resolve(const void* object,const void* vt) noexcept {
    Binding result{};result.vtable=vt;
    Col col{};Hierarchy h{};
    if(!Metadata(object,&col,&h))return result;
    result.complete=col.offset;
    // Choose the most specific supported family, independent of the RTTI's order.
    for(std::size_t r=0;r<std::size(kRules);++r) {
        for(std::uint32_t i=0;i<h.count;++i) {
            Base b{};
            if(!BaseAt(h,i,&b))return Binding{vt,0,0,-1,0};
            if(!TypeIs(b.type,kRules[r].type))continue;
            if(b.vbase!=-1 || b.member<0 || b.member>0x10000)return Binding{vt,0,0,-1,0};
            result.rule=static_cast<int>(r);result.member=static_cast<std::size_t>(b.member);break;
        }
        if(result.rule>=0)break;
    }
    // Additional native models on these known descendants are separate from
    // their inherited/main model; do not miss the weak point or trailer barrier.
    for(std::uint32_t i=0;i<h.count;++i) {
        Base b{};
        if(!BaseAt(h,i,&b))return Binding{vt,0,0,-1,0};
        if(b.vbase!=-1 || b.member!=0)continue;
        if(TypeIs(b.type,".?AVEDF6_GrandMotherParts_Weak@@"))result.extra=0x1160;
        if(TypeIs(b.type,".?AVAlienTrailer@@"))result.extra=0x10E0;
        if(TypeIs(b.type,".?AVHumanoid_BigGreyBoss_Meteor@@"))result.extra=0xF00;
    }
    return result;
}
// The RTTI verdict of a model's vtable: an AnimationModel at offset 0. -1: its metadata could not be read (not kept).
int ModelByRtti(const void* model) noexcept {
    Col col{};Hierarchy h{};
    if(!Metadata(model,&col,&h))return -1;
    if(col.offset)return 0;
    for(std::uint32_t i=0;i<h.count;++i) {
        Base b{};
        if(!BaseAt(h,i,&b))return -1;
        if(TypeIs(b.type,".?AVAnimationModel@@"))return b.member==0 && b.vbase==-1;
    }
    return 0;
}
bool IsModel(const void* model,bool live) noexcept {
    if(!live && !Readable(model,kModelBytes))return false;
    const void* vt=nullptr;const void* registered=nullptr;
    if(!Read(model,&vt,live) || !Read(static_cast<const unsigned char*>(model)+0x10,&registered,live) || !registered)
        return false;
    // FP chains slot 1 in place. RTTI/vtable identity survives that chain.
    if(vt==image+kAnimationVtable)return true;
    // The verdict depends on the vtable only (its [-1] locator and the image's RTTI): walked once per vtable.
    for(std::size_t i=0;i<cache.modelCount;++i)if(cache.models[i].vtable==vt)return cache.models[i].model;
    const int verdict=ModelByRtti(model);
    if(verdict>=0 && cache.modelCount<cache.models.size())cache.models[cache.modelCount++]=ModelVt{vt,verdict==1};
    return verdict==1;
}
// The object's binding (resolved once per vtable, up to the cache's size; past it into `uncached`), null when its vtable
// cannot be read.
const Binding* BindingOf(const void* object,Binding& uncached,bool live) noexcept {
    const void* vt=nullptr;
    if(!Read(object,&vt,live))return nullptr;
    for(std::size_t i=0;i<cache.count;++i)if(cache.bindings[i].vtable==vt)return &cache.bindings[i];
    uncached=Resolve(object,vt);
    if(cache.count<cache.bindings.size()){cache.bindings[cache.count]=uncached;return &cache.bindings[cache.count++];}
    return &uncached;
}

std::size_t Models(const void* object,const void** out,std::size_t capacity,bool live) noexcept {
    if(!object || !out || !capacity || !Profile())return 0;
    Binding uncached{};
    const Binding* const binding=BindingOf(object,uncached,live);
    if(!binding || binding->rule<0)return 0;
    const auto raw=reinterpret_cast<std::uintptr_t>(object);
    if(raw<binding->complete || binding->member>(std::numeric_limits<std::uintptr_t>::max)()-(raw-binding->complete))return 0;
    const auto owner=reinterpret_cast<const unsigned char*>(raw-binding->complete+binding->member);
    const Rule& rule=kRules[static_cast<std::size_t>(binding->rule)];
    std::size_t n=0;
    auto add=[&](const void* model) noexcept {
        if(n==capacity || !IsModel(model,live))return;
        for(std::size_t i=0;i<n;++i)if(out[i]==model)return;
        out[n++]=model;
    };
    if(rule.array) {
        const unsigned char* models=nullptr;std::size_t count=0;
        if(!Read(owner+rule.model+8,&models,live) || !Read(owner+rule.model+0x18,&count,live) ||
           count>kMostModels || (count && !models) || (!live && !Readable(models,count*kHumanoidStride)))return 0;
        for(std::size_t i=0;i<count && n<capacity;++i)add(models+i*kHumanoidStride);
    } else add(owner+rule.model);
    if(binding->extra)add(owner+binding->extra);
    return n;
}
Family FamilyOf(const void* object,bool live) noexcept {
    if(!object)return Family::kNone;
    Binding uncached{};
    const Binding* const binding=BindingOf(object,uncached,live);
    if(!binding || binding->rule<0)return Family::kNone;
    const auto rule=static_cast<std::size_t>(binding->rule);
    if(rule==kHumanRule)return Family::kHuman;
    if(rule==kVehicleRule)return Family::kVehicle;
    return Family::kOther;
}
}  // namespace

std::size_t EnemyModels(const void* object,const void** out,std::size_t capacity) noexcept {
    return Models(object,out,capacity,false);
}
const void* EnemyModel(const void* object) noexcept {
    const void* model=nullptr;EnemyModels(object,&model,1);return model;
}
Family EnemyFamily(const void* object) noexcept {
    return FamilyOf(object,false);
}
std::size_t EnemyModelsLive(const void* object,const void** out,std::size_t capacity) noexcept {
    return Models(object,out,capacity,true);
}
const void* EnemyModelLive(const void* object) noexcept {
    const void* model=nullptr;Models(object,&model,1,true);return model;
}
Family EnemyFamilyLive(const void* object) noexcept {
    return FamilyOf(object,true);
}
bool IsAnimationModel(const void* model) noexcept {
    return model && Profile() && IsModel(model,false);
}
}  // namespace crew::thermal
