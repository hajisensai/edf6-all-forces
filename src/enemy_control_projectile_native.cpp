// Supported EDF.dll 678CCB46 BulletCreateInfo value/weak/resource contract.
// References remain local; only declared numbers, matrices and full IDs cross codecs.
#include "enemy_control_projectile_native.h"
#include "crew.h"
#include "memory.h"
#include "command_identity.h"
#include "support_soldier.h"
#include <cmath>
#include <cstring>
namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_PROJECTILE_NATIVE_TEST
void* ProjectileNativeTestFunction(unsigned) noexcept;
#endif
namespace projectile_native {
struct Print {unsigned rva;unsigned char bytes[16];};
constexpr Print prints[]={
    {0x32EF10,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x4C,0x24,0x08,0x55,0x56,0x57,0x48,0x83,0xEC}},
    {0x100C90,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48}},
    {0x58EB40,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x02,0x48,0x8B,0xD9,0x4C,0x8B,0x42,0x08}},
    {0x548EF0,{0x4D,0x63,0xC8,0x4C,0x3B,0x89,0x78,0x03,0x00,0x00,0x73,0x34,0x45,0x85,0xC0,0x78}},
    {0x75C910,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48}},
    {0x777410,{0x48,0x8B,0x41,0x18,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {0x1194280,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x40,0x49,0x8B,0xF1,0x48,0x8B}},
};
unsigned char* admittedImage=nullptr;NativeProjectileJournal journals[32]{};
struct Ref {void* object=nullptr;unsigned char* control=nullptr;};
using VoidFn=void(__fastcall*)(void*);
using CopyFn=void*(__fastcall*)(void*,const void*);
using MoveFn=void(__fastcall*)(void*,Ref*);
using LockFn=Ref*(__fastcall*)(void*,Ref*,int);
using CreateFn=void*(__fastcall*)(void*,const float*,void*,void*);
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_PROJECTILE_NATIVE_TEST
    return reinterpret_cast<T>(ProjectileNativeTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool Ready() noexcept {
    if(!image)return false;if(admittedImage)return image==admittedImage;
    for(const auto& p:prints)if(!Readable(image+p.rva,16) || std::memcmp(image+p.rva,p.bytes,16))return false;
    admittedImage=image;return true;
}
bool Zero(const unsigned char* id) noexcept {unsigned any=0;for(unsigned i=0;i<32;++i)any|=id[i];return !any;}
bool Canonical(const unsigned char* id) noexcept {return !Zero(id) && !At<std::uint32_t>(id,20);}
bool Matrix(const float* matrix) noexcept {
    if(!Readable(matrix,64))return false;for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return false;
    if(std::fabs(matrix[15]-1.0f)>0.0001f)return false;
    double n[3]{};for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)n[i]+=static_cast<double>(matrix[i*4+j])*matrix[i*4+j];
    const double d=static_cast<double>(matrix[0])*(matrix[5]*matrix[10]-matrix[6]*matrix[9])-static_cast<double>(matrix[1])*(matrix[4]*matrix[10]-matrix[6]*matrix[8])+static_cast<double>(matrix[2])*(matrix[4]*matrix[9]-matrix[5]*matrix[8]);
    return n[0]>0 && n[1]>0 && n[2]>0 && std::isfinite(d) && std::fabs(d)>std::sqrt(n[0]*n[1]*n[2])*1.0e-8;
}
float Scalar(const NativeProjectileSample& s,unsigned index) noexcept {
    float value=0;std::memcpy(&value,s.scalar+index,sizeof(value));return value;
}
bool Velocity(const NativeProjectileSample& s) noexcept {
    // 231DC3..231E4B: speed is descriptor94 (CoreParam64). Native first
    // multiplies the actual forward row, then adds optional descriptor50/54/58
    // only if their native ordered sum is not NaN. NaN here is a real sentinel.
    static_assert(kProjectileScalar32[6]==0x94 && kProjectileScalar32[1]==0x50 && kProjectileScalar32[2]==0x54 && kProjectileScalar32[3]==0x58,"native velocity scalar map");
    const float speed=Scalar(s,6);if(!std::isfinite(speed))return false;
    float velocity[3]{};for(unsigned i=0;i<3;++i){velocity[i]=s.matrix[8+i]*speed;if(!std::isfinite(velocity[i]))return false;}
    const float inherited[3]={Scalar(s,1),Scalar(s,2),Scalar(s,3)};
    const float sum=(inherited[1]+inherited[0])+inherited[2];
    if(!std::isnan(sum))for(unsigned i=0;i<3;++i)if(!std::isfinite(velocity[i]+inherited[i]))return false;
    return true;
}
bool Weak(const Ref& r) noexcept {
    return (!r.object && !r.control) || (Readable(r.control,16,true) && At<LONG>(r.control,8)>=0 &&
        At<LONG>(r.control,12)>0 && At<LONG>(r.control,12)<0x7FFFFFFF);
}
bool Actor(const Ref& r) noexcept {
    return r.object && Weak(r) && At<LONG>(r.control,8)>0 && Readable(r.object,0x380) && At<const void*>(r.object,0x30)==r.control;
}
bool Root(const NativeLease& l) noexcept {return Actor({l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))});}
bool Pin(Ref r) noexcept {
    if(!Readable(r.control,16,true))return false;auto* p=reinterpret_cast<volatile LONG*>(r.control+8);LONG was=*p;
    while(was>0 && was<0x7FFFFFFF){const LONG actual=InterlockedCompareExchange(p,was+1,was);if(actual==was)return true;was=actual;}return false;
}
void Unpin(Ref r) noexcept {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+8))!=0)return;
    const auto table=At<void* const*>(r.control,0);reinterpret_cast<VoidFn>(table[0])(r.control);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12))==0)reinterpret_cast<VoidFn>(table[1])(r.control);
}
bool Variant(const void* a,const void* b) noexcept {
    const auto type=At<std::uint16_t>(a,16);if(type!=At<std::uint16_t>(b,16))return false;
    if(type==0xFFFF)return true;if(type>2)return false;return !std::memcmp(a,b,type==2 ? 16u : 8u);
}
bool Template(const void* p) noexcept {
    if(!Readable(p,0x1E0) || At<const void*>(p,0)!=image+0x1769028)return false;
    if(!Variant(static_cast<const unsigned char*>(p)+0xE8,static_cast<const unsigned char*>(p)+0xE8) ||
       !Variant(static_cast<const unsigned char*>(p)+0x1B0,static_cast<const unsigned char*>(p)+0x1B0))return false;
    for(unsigned offset:{0x38u,0x60u,0x78u,0x1D0u})if(!Weak(At<Ref>(p,offset)))return false;
    for(unsigned offset:{0x180u,0x190u}) {
        const auto* control=At<const unsigned char*>(p,offset);
        if(control && (!Readable(control,16,true) || At<LONG>(control,0)<=0 || At<LONG>(control,4)<=0 ||
           At<LONG>(control,0)==0x7FFFFFFF || At<LONG>(control,4)==0x7FFFFFFF))return false;
    }
    return true;
}
unsigned char Resources(const void* p) noexcept {return static_cast<unsigned char>((At<const void*>(p,0x180) ? 1 : 0)|(At<const void*>(p,0x190) ? 2 : 0));}
bool Compatible(const void* local,const void* actual) noexcept {
    if(!Template(local) || !Template(actual))return false;
    constexpr unsigned pointers[]={0x100,0x120,0x130,0x140,0x150,0x160,0x1C8,0x1D0,0x1D8};
    for(unsigned offset:pointers)if(At<std::uint64_t>(actual,offset)!=At<std::uint64_t>(local,offset))return false;
    for(unsigned offset:{0x128u,0x138u,0x148u,0x158u,0x168u})if(At<std::uint32_t>(actual,offset)!=At<std::uint32_t>(local,offset))return false;
    if(!Variant(static_cast<const unsigned char*>(local)+0xE8,static_cast<const unsigned char*>(actual)+0xE8) ||
       !Variant(static_cast<const unsigned char*>(local)+0x1B0,static_cast<const unsigned char*>(actual)+0x1B0))return false;
    for(unsigned offset:{0x180u,0x190u}) {
        const auto* control=At<const unsigned char*>(local,offset);
        if(At<const void*>(actual,offset)!=control || At<const void*>(actual,offset+8)!=(control ? At<const void*>(control,8) : nullptr))return false;
    }
    return true;
}
bool ReadActorId(const Ref& r,unsigned char* id) noexcept {
    return Actor(r) && ReadNativeObjectId(static_cast<const unsigned char*>(r.object),id) && Canonical(id);
}
bool CaptureLock(const Ref& lock,unsigned char* id,std::uint32_t* index) noexcept {
    if(!Weak(lock))return false;if(!lock.control || At<LONG>(lock.control,8)==0)return true;
    if(!Readable(lock.object,16))return false;
    auto* actor=At<unsigned char*>(lock.object,8);if(!Readable(actor,0x380))return false;
    const Ref owner{actor,At<unsigned char*>(actor,0x30)};if(!ReadActorId(owner,id))return false;
    const auto count=At<std::size_t>(actor,0x378),capacity=At<std::size_t>(actor,0x370);const auto* rows=At<const unsigned char*>(actor,0x368);
    if(!count || count>1024 || count>capacity || !Readable(rows,count*16))return false;
    for(unsigned i=0;i<count;++i)if(At<const void*>(rows+i*16,0)==lock.object && At<const void*>(rows+i*16,8)==lock.control){*index=i;return true;}return false;
}
// Native 75C910 uses this exact bucket walk, but its equality ignores the
// +24 suffix. Compare every semantic ID field ourselves before reading its
// weak record. This also recognizes an exact retained expired weak without
// dereferencing the already destroyed body. Missing is deliberately pending.
bool NativeActor(const unsigned char* id,Ref* out) noexcept {
    // Game-thread read-only lookup: no engine callbacks or temporary strong
    // ownership are acquired here. A returned strong reference is acquired only
    // at the public handoff (or directly into the persistent replay journal).
    const auto* view=At<const unsigned char*>(image,0x20B2AC0);
    if(!view || reinterpret_cast<std::uintptr_t>(view)<0x98)return false;
    const auto* session=view-0x98;if(!Readable(session,0xE0))return false;
    const Ref owner{At<void*>(session,0xD0),At<unsigned char*>(session,0xD8)};
    if(!Readable(owner.object,0x58) || !Weak(owner) || At<LONG>(owner.control,8)<=0)return false;
    const auto* registry=At<const unsigned char*>(owner.object,0x50);if(!Readable(registry,0xF8))return false;
    const auto mask=At<std::size_t>(registry,0xF0);const auto* buckets=At<const unsigned char*>(registry,0xD8);const auto* sentinel=At<const unsigned char*>(registry,0xC8);
    if(mask>1048575 || !Readable(sentinel,16) || !Readable(buckets,(mask+1)*16))return false;
    const auto stable=[&]() noexcept {
        return At<const void*>(image,0x20B2AC0)==view && At<const void*>(session,0xD0)==owner.object && At<const void*>(session,0xD8)==owner.control &&
            Weak(owner) && At<LONG>(owner.control,8)>0 && At<const void*>(owner.object,0x50)==registry &&
            At<std::size_t>(registry,0xF0)==mask && At<const void*>(registry,0xD8)==buckets && At<const void*>(registry,0xC8)==sentinel;
    };
    const auto hash=At<std::uint64_t>(id,24);const auto* bucket=buckets+(hash&mask)*16;
    const auto* first=At<const unsigned char*>(bucket,0);const auto* node=At<const unsigned char*>(bucket,8);
    if(node==sentinel)return false;
    for(unsigned visited=0;visited<1048576;++visited) {
        if(node==sentinel || !Readable(node,0x38))return false;
        if(!std::memcmp(node+0x10,id,20) && !std::memcmp(node+0x28,id+24,8)) {
            const auto* record=At<const unsigned char*>(node,0x30);if(!Readable(record,0x20))return false;
            const Ref actor{At<void*>(record,0x10),At<unsigned char*>(record,0x18)};
            const auto sameRecord=[&]() noexcept {
                return At<const void*>(node,0x30)==record && At<const void*>(record,0x10)==actor.object && At<const void*>(record,0x18)==actor.control &&
                    !std::memcmp(node+0x10,id,20) && !std::memcmp(node+0x28,id+24,8);
            };
            if(!actor.control || !Weak(actor))return false; // placeholder is not proven expired
            if(At<LONG>(actor.control,8)==0){if(!stable() || !sameRecord())return false;*out={};return true;}
            unsigned char actual[32]{};
            if(!ReadActorId(actor,actual) || std::memcmp(actual,id,32) || !stable() || !sameRecord() || !Actor(actor))return false;
            *out=actor;return true;
        }
        if(node==first)return false;node=At<const unsigned char*>(node,8);
    }
    return false;
}

bool ResolveActor(const unsigned char* id,Ref* out) noexcept {
    *out={};if(Zero(id))return true;if(!Canonical(id))return false;
#ifdef ENEMY_CONTROL_PROJECTILE_NATIVE_TEST
    // Existing boundary fixtures can omit the game registry entirely. Lifecycle
    // fixtures install the real native-layout registry and exercise the path above.
    if(!At<const void*>(image,0x20B2AC0)) {
        MarkIdentities found{};if(!ResolveMarkIdentities(nullptr,id,&found) || !found.target)return false;
        Ref result{const_cast<void*>(found.target.obj),static_cast<unsigned char*>(const_cast<void*>(found.target.ctrl))};
        unsigned char actual[32]{};if(!ReadActorId(result,actual) || std::memcmp(actual,id,32))return false;*out=result;return true;
    }
#endif
    return NativeActor(id,out);
}

bool ResolveLock(const Ref& actor,unsigned index,Ref* out) noexcept {
    *out={};if(!actor.object)return true; // an exact expired native target has no live lock point
    const auto count=At<std::size_t>(actor.object,0x378),capacity=At<std::size_t>(actor.object,0x370);const auto* rows=At<const unsigned char*>(actor.object,0x368);
    if(count>1024 || count>capacity || index>=count || !Readable(rows,count*16))return false;
    const Ref lock=At<Ref>(rows+static_cast<std::size_t>(index)*16,0);
    if(!Weak(lock) || !lock.object || At<LONG>(lock.control,8)<=0 || !Readable(lock.object,16) || At<const void*>(lock.object,8)!=actor.object)return false;
    *out=lock;return true;
}
void SetWeak(void* target,Ref source) noexcept {
    if(source.control)InterlockedIncrement(reinterpret_cast<volatile LONG*>(source.control+12));
    Fn<MoveFn>(0x58EB40)(target,&source);
}
struct Unit {void* manager;void* factory;};
void* UnitCreate(void* opaque,const float* matrix,void* descriptor) noexcept {
    auto* unit=static_cast<Unit*>(opaque);return Fn<CreateFn>(0x1194280)(unit->manager,matrix,unit->factory,descriptor);
}
} // namespace projectile_native
bool CaptureNativeProjectileLock(const void* weak,unsigned char id[32],std::uint32_t* ordinal) noexcept {
    using namespace projectile_native;
    if(id)std::memset(id,0,32);if(ordinal)*ordinal=0;
    __try {
        unsigned char pending[32]{};std::uint32_t index=0;
        if(!id || !ordinal || !Ready() || !Readable(weak,16) || !CaptureLock(At<Ref>(weak,0),pending,&index))return false;
        std::memcpy(id,pending,32);*ordinal=index;return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER){if(id)std::memset(id,0,32);if(ordinal)*ordinal=0;return false;}
}
bool ResolveNativeProjectileActor(const unsigned char id[32],void** object,const void** control) noexcept {
    using namespace projectile_native;if(object)*object=nullptr;if(control)*control=nullptr;
    __try {
        if(!object || !control || !id || !Readable(id,32) || !Ready())return false;
        Ref actor{};if(!ResolveActor(id,&actor))return false;if(!actor.object)return true;
        if(!Pin(actor))return false;*object=actor.object;*control=actor.control;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ResolveNativeProjectileLock(const unsigned char id[32],std::uint32_t ordinal,void** object,const void** control) noexcept {
    using namespace projectile_native;if(object)*object=nullptr;if(control)*control=nullptr;
    __try {
        if(!object || !control || !id || !Readable(id,32) || !Ready() || (Zero(id) ? ordinal!=0 : !Canonical(id)) || ordinal>=1024)return false;
        Ref actor{},lock{};if(!ResolveActor(id,&actor))return false;
        if(!actor.object)return true;
        if(!ResolveLock(actor,ordinal,&lock) || !Pin(lock))return false;
        // Only the reference handed to the caller is acquired. No temporary
        // actor release or native destructor can fail after this publication.
        *object=lock.object;*control=lock.control;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ReleaseNativeProjectileLock(void* object,const void* control,bool* consumed) noexcept {
    using namespace projectile_native;if(consumed)*consumed=false;
    if(!object && !control)return true;
    __try {
        const Ref r{object,static_cast<unsigned char*>(const_cast<void*>(control))};
        if(!object || !Readable(control,16,true) || At<LONG>(control,8)<=0)return false;
        const auto table=At<void* const*>(control,0);if(!Readable(table,16) || !table[0] || !table[1])return false;
        // All operations before the decrement are admitted. A subsequent native
        // last-use destructor failure has consumed this strong reference once.
        if(consumed)*consumed=true;Unpin(r);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ValidNativeProjectileSample(const NativeProjectileSample& s) noexcept {
    using namespace projectile_native;
    return Matrix(s.matrix) && Velocity(s) && (!s.hasMuzzle || Matrix(s.muzzleMatrix)) && s.resourceMask<=3 &&
        (Zero(s.target) ? s.lock==0 : Canonical(s.target)) && (Zero(s.extra) ? !s.extraObject : Canonical(s.extra));
}
bool CaptureNativeProjectileSample(const NativeLease& l,const void* local,const float* matrix,const void* actual,NativeProjectileSample* out) noexcept {
    using namespace projectile_native;
    __try {
        if(!out || !Ready() || !Root(l) || !Matrix(matrix) || !Compatible(local,actual) ||
           At<const void*>(actual,0x38)!=At<const void*>(l.object,0x28) || At<const void*>(actual,0x40)!=l.control)return false;
        NativeProjectileSample s{};std::memcpy(s.matrix,matrix,64);const auto* muzzle=At<const float*>(actual,0x70);s.hasMuzzle=muzzle!=nullptr;
        if(s.hasMuzzle){if(!Matrix(muzzle))return false;std::memcpy(s.muzzleMatrix,muzzle,64);}
        for(unsigned i=0;i<kProjectileWords;++i)s.scalar[i]=At<std::uint32_t>(actual,kProjectileScalar32[i]);
        for(unsigned i=0;i<kProjectileBytes;++i)s.bytes[i]=At<unsigned char>(actual,kProjectileScalar8[i]);
        s.scalar178=At<std::uint16_t>(actual,0x178);s.random=At<std::uint32_t>(actual,0x1A0);s.shot=At<std::uint32_t>(actual,0x1A4);s.flags=At<std::uint32_t>(actual,0x1A8);s.resourceMask=Resources(actual);
        if(!CaptureLock(At<Ref>(actual,0x60),s.target,&s.lock))return false;
        const Ref extra=At<Ref>(actual,0x78);if(!Weak(extra))return false;
        const auto* raw=At<const void*>(actual,0x88);if(raw && raw!=extra.object)return false;
        if(extra.control && At<LONG>(extra.control,8)>0){if(!ReadActorId(extra,s.extra))return false;s.extraObject=raw!=nullptr;}
        else if(raw)return false;
        if(!ValidNativeProjectileSample(s))return false;*out=s;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeProjectileDependenciesReady(const NativeLease& l,const NativeProjectileSample& s) noexcept {
    using namespace projectile_native;
    __try {
        if(!Ready() || !Root(l) || !ValidNativeProjectileSample(s))return false;
        Ref target{},lock{},extra{};return ResolveActor(s.target,&target) && ResolveLock(target,s.lock,&lock) && ResolveActor(s.extra,&extra);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool CanPrepareNativeProjectileSample(const NativeLease& l,const void* local,float* muzzle,const NativeProjectileSample& s) noexcept {
    using namespace projectile_native;
    __try {
        if(!Ready() || !Root(l) || !ValidNativeProjectileSample(s) || !Template(local) || Resources(local)!=s.resourceMask ||
           (s.hasMuzzle && !Readable(muzzle,64,true)))return false;
        Ref target{},lock{},extra{};return ResolveActor(s.target,&target) && ResolveLock(target,s.lock,&lock) && ResolveActor(s.extra,&extra);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool CanReplayNativeProjectileSample(const NativeLease& l,void* factory,const void* local,float* muzzle,const NativeProjectileSample& s) noexcept {
    __try {const auto table=Readable(factory,8) ? At<void* const*>(factory,0) : nullptr;
        return Readable(table,24) && table[2] && CanPrepareNativeProjectileSample(l,local,muzzle,s);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeProjectileJournalEmpty(const NativeProjectileJournal& j) noexcept {
    return !j.ownerObject && !j.ownerControl && !j.heldMask && !j.made && !j.busy && !j.copyEntered && !j.createEntered && !j.cleanupEntered && !j.uncertain;
}
bool RestoreNativeProjectileJournal(NativeProjectileJournal& j) noexcept {
    using namespace projectile_native;
    if(NativeProjectileJournalEmpty(j))return true;
    if(j.busy || j.uncertain || j.copyEntered || j.createEntered || j.cleanupEntered)return false;
    __try {
        // Preflight every remaining reference before starting a native cleanup.
        // A read-only control block can become writable and be retried safely;
        // an exception after cleanup started is permanently uncertain instead.
        if(j.made && (!Ready() || !Template(j.descriptor)))return false;
        for(unsigned i=0;i<3;++i)if(j.heldMask&(1u<<i)) {
            const auto* control=static_cast<const unsigned char*>(j.controls[i]);
            if(!Readable(control,16,true) || At<LONG>(control,8)<=0)return false;
            const auto table=At<void* const*>(control,0);if(!Readable(table,16) || !table[0] || !table[1])return false;
        }
        if(j.made) {j.cleanupEntered=true;Fn<VoidFn>(0x100C90)(j.descriptor);j.made=false;j.cleanupEntered=false;std::memset(j.descriptor,0,sizeof(j.descriptor));}
        for(unsigned i=0;i<3;++i)if(j.heldMask&(1u<<i)) {
            // Writable admission above guarantees the decrement itself. Clear
            // ownership before a possible last-use native destructor callback;
            // an exception then cannot cause a second decrement on retry.
            j.heldMask&=~(1u<<i);j.cleanupEntered=true;
            Unpin({j.objects[i],static_cast<unsigned char*>(const_cast<void*>(j.controls[i]))});j.cleanupEntered=false;
        }
        j={};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){j.uncertain=true;j.busy=false;return false;}
}
NativeProjectileOutcome WithNativeProjectileOutcomeInPlace(NativeProjectileJournal& j,const NativeLease& l,const void* local,float* muzzle,const NativeProjectileSample& s,NativeProjectileCreateFn create,void* user) noexcept {
    using namespace projectile_native;
    __try {
        if(!NativeProjectileJournalEmpty(j) || !create || !CanPrepareNativeProjectileSample(l,local,muzzle,s))return {};
        Ref refs[3]={{l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))},{},{}};Ref lock{};
        if(!ResolveActor(s.target,refs+1) || !ResolveLock(refs[1],s.lock,&lock) || !ResolveActor(s.extra,refs+2))return {};
        j.ownerObject=l.object;j.ownerControl=l.control;j.busy=true;
        for(unsigned i=0;i<3;++i)if(refs[i].object) {
            if(!Pin(refs[i])){j.busy=false;RestoreNativeProjectileJournal(j);return {};}
            j.objects[i]=refs[i].object;j.controls[i]=refs[i].control;j.heldMask|=1u<<i;
        }
        j.copyEntered=true;Fn<CopyFn>(0x32EF10)(j.descriptor,local);j.made=true;j.copyEntered=false;
        SetWeak(j.descriptor+0x38,{At<void*>(l.object,0x28),static_cast<unsigned char*>(const_cast<void*>(l.control))});
        SetWeak(j.descriptor+0x60,lock);SetWeak(j.descriptor+0x78,refs[2]);
        Put<void*>(j.descriptor,0x88,s.extraObject ? refs[2].object : nullptr);
        if(s.hasMuzzle){std::memcpy(muzzle,s.muzzleMatrix,64);Put<float*>(j.descriptor,0x70,muzzle);}else Put<float*>(j.descriptor,0x70,nullptr);
        for(unsigned i=0;i<kProjectileWords;++i)Put<std::uint32_t>(j.descriptor,kProjectileScalar32[i],s.scalar[i]);
        for(unsigned i=0;i<kProjectileBytes;++i)Put<unsigned char>(j.descriptor,kProjectileScalar8[i],s.bytes[i]);
        Put<std::uint16_t>(j.descriptor,0x178,s.scalar178);Put<std::uint32_t>(j.descriptor,0x1A0,s.random);Put<std::uint32_t>(j.descriptor,0x1A4,s.shot);Put<std::uint32_t>(j.descriptor,0x1A8,s.flags);
        std::memcpy(j.matrix,s.matrix,64);Put<float*>(j.descriptor,8,j.matrix);j.createEntered=true;j.created=create(user,j.matrix,j.descriptor);j.createEntered=false;j.busy=false;
        void* result=j.created;const bool cleaned=RestoreNativeProjectileJournal(j);return {result,cleaned};
    } __except(EXCEPTION_EXECUTE_HANDLER){j.uncertain=true;j.busy=false;return {j.created,false};}
}
void* WithNativeProjectileSampleInPlace(NativeProjectileJournal& j,const NativeLease& l,const void* local,float* muzzle,const NativeProjectileSample& s,NativeProjectileCreateFn create,void* user) noexcept {
    const auto outcome=WithNativeProjectileOutcomeInPlace(j,l,local,muzzle,s,create,user);
    return outcome.cleanupComplete ? outcome.created : nullptr;
}
bool NativeProjectileJournalsEmpty(const NativeLease& l) noexcept {
    for(const auto& j:projectile_native::journals)if(j.ownerObject==l.object && j.ownerControl==l.control && !NativeProjectileJournalEmpty(j))return false;return true;
}
bool RestoreNativeProjectileJournals(const NativeLease& l) noexcept {
    bool okay=true;for(auto& j:projectile_native::journals)if(j.ownerObject==l.object && j.ownerControl==l.control)okay=RestoreNativeProjectileJournal(j) && okay;return okay;
}
void* WithNativeProjectileSample(const NativeLease& l,const void* local,float* muzzle,const NativeProjectileSample& s,NativeProjectileCreateFn create,void* user) noexcept {
    if(!NativeProjectileJournalsEmpty(l))return nullptr;
    for(auto& j:projectile_native::journals)if(NativeProjectileJournalEmpty(j))return WithNativeProjectileSampleInPlace(j,l,local,muzzle,s,create,user);return nullptr;
}
void* ReplayNativeProjectileSampleInPlace(NativeProjectileJournal& j,const NativeLease& l,void* factory,const void* local,float* muzzle,const NativeProjectileSample& s) noexcept {
    const auto outcome=ReplayNativeProjectileOutcomeInPlace(j,l,factory,local,muzzle,s);
    return outcome.cleanupComplete ? outcome.created : nullptr;
}
NativeProjectileOutcome ReplayNativeProjectileOutcomeInPlace(NativeProjectileJournal& j,const NativeLease& l,void* factory,const void* local,float* muzzle,const NativeProjectileSample& s) noexcept {
    using namespace projectile_native;
    __try {if(!CanReplayNativeProjectileSample(l,factory,local,muzzle,s) || !Readable(image+0x20B2958,8))return {};
        Unit unit{At<void*>(image+0x20B2958,0),factory};if(!Readable(unit.manager,0xCB4))return {};
        return WithNativeProjectileOutcomeInPlace(j,l,local,muzzle,s,&UnitCreate,&unit);}
    __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
void* ReplayNativeProjectileSample(const NativeLease& l,void* factory,const void* local,float* muzzle,const NativeProjectileSample& s) noexcept {
    using namespace projectile_native;
    __try {if(!CanReplayNativeProjectileSample(l,factory,local,muzzle,s) || !Readable(image+0x20B2958,8))return nullptr;
        Unit unit{At<void*>(image+0x20B2958,0),factory};if(!Readable(unit.manager,0xCB4))return nullptr;
        return WithNativeProjectileSample(l,local,muzzle,s,&UnitCreate,&unit);}
    __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
} // namespace crew::enemy_native
