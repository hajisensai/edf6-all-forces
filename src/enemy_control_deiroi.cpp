// Source/runtime contracts and independent instruction audit: docs/enemy-control-deiroi.md.
#include "enemy_control_deiroi.h"
#include "enemy_control_identity.h"
#include "enemy_control.h"
#include "enemy_control_deiroi_net.h"
#include "enemy_control_deiroi_entity_aim.h"
#include "enemy_control_action_native.h"
#include "crew.h"
#include "edf41classes.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace crew::enemy_native {
namespace deiroi_control {
constexpr unsigned kBatteryVtable=0x17AB118,kMaxLegs=32,kMaxParts=128,kMaxBatteries=256,kMaxRoots=8;
constexpr unsigned kMaxMainGuns=64,kMainGroup=32,kMainStride=0x290,kMainSet=0x3211C0,kMainFire=0x31CB60,kMainIdle=0x31CDB0;
constexpr unsigned kSet=0x32C310,kIsState=0x32A890,kIdle=0x32D0C0,kCharge=0x32C750,kFire=0x32C880,kClose=0x32C6A0;
constexpr unsigned kAutoCall=0x32D286,kPoseCall=0x32B422,kWeaponCall=0x32CFE8;
constexpr std::size_t kMachine=0xAD0,kHolder=0xB90;
struct Desc { const void* function;std::int32_t adjustor=0,pad=0; };
static_assert(sizeof(Desc)==16,"native member-callback descriptor");
struct Print { unsigned at;unsigned char bytes[16]; };
constexpr Print kPrints[]={
    {0x3211C0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x31CB60,{0x40,0x57,0x48,0x81,0xEC,0xE0,0x00,0x00,0x00,0x48,0x8B,0xF9,0x85,0xD2,0x75,0x38}},
    {0x31CDB0,{0xC2,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {0x32B210,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x70,0x48}},
    {0x32D0C0,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x85,0xD2,0x0F,0x85,0x98,0x00,0x00}},
    {0x32B790,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x54,0x41,0x56}},
    {0x32BA80,{0x48,0x89,0x5C,0x24,0x18,0x55,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x48,0x81,0xC1}},
    {0x31D830,{0x49,0x63,0x55,0x08,0x49,0x03,0xD5,0x48,0x63,0xC3,0x48,0x8D,0x0C,0x40,0x48,0x8D}},
    {0x324760,{0x49,0x63,0x57,0x08,0x49,0x03,0xD7,0x4A,0x8D,0x04,0x22,0x48,0x63,0x48,0x08,0x48}},
    {0x3248B6,{0x49,0x8B,0x56,0x08,0x48,0x8B,0x0F,0xE8,0x5E,0x66,0xE6,0x00,0x4C,0x89,0x75,0x9F}},
    {0x32C310,{0x48,0x89,0x5C,0x24,0x08,0x4C,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55}},
    {0x32A890,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x32C750,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x85,0xD2,0x75,0x4C,0x48,0x8B,0x81}},
    {0x32C880,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x55,0x48,0x8D,0x68,0xE8}},
    {0x6BB5A0,{0x48,0x8B,0xC4,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x4C,0x89,0x70,0x20,0x55}},
    {0x6CFC60,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x55}},
    {0x3296E0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x20,0x55,0x48,0x8D,0x68,0xA1,0x48,0x81,0xEC,0xE0}},
};
struct Ref { unsigned char* object=nullptr;unsigned char* control=nullptr; };
struct Entry { Ref ref{};unsigned key=0; };
struct Gun {
    Ref ref{};unsigned key=0;
    std::uint64_t sequence=0,seed=0,acceptedSequence=0;
    unsigned acceptedPhase=0;std::uint64_t acceptedSeed=0;float acceptedPoint[3]{};float confirmedPoint[4]{};bool pointValid=false;
    bool manual=false,owned=false,lastAutomatic=false,lastResult=false;
};
struct MainGun {
    unsigned char* row=nullptr;std::uint64_t acceptedSequence=0,acceptedSeed=0;
    unsigned acceptedPhase=0;float acceptedPoint[3]{};bool owned=false;
};
struct Root {
    Ref ref{};const NativeProfile* profile=nullptr;float yaw=0,elevation=0;bool faulted=false;unsigned nativeInFlight=0;
    Gun guns[kMaxBatteries]{};MainGun main[kMaxMainGuns]{};
};
Root roots[kMaxRoots]{};
unsigned char* admittedImage=nullptr;
thread_local const void* explicitChild=nullptr;
bool redirected[3]{},ready=false;
using SetFn=void(__fastcall*)(void*,const Desc*,void*);
using MainSetFn=void(__fastcall*)(void*,const void*,void*);
using PoseFn=void(__fastcall*)(void*,const float*,float);
using WeaponFn=void(__fastcall*)(void*,float*,std::uint32_t);
using MatrixFn=float*(__fastcall*)(void*,float*);

bool IsDeiroi(const NativeLease& lease) noexcept {
    if(!lease.profile || lease.profile->kind!=Kind::deiroi || !lease.profile->name ||
       std::strcmp(lease.profile->name,"Deiroi")!=0)return false;
    std::size_t count=0;const auto profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(lease.profile==profiles+i)return true;
    return false;
}
bool Current(const Ref& r,bool alive=true) noexcept {
    if(!r.object || !Readable(r.control,16) || At<LONG>(r.control,8)<=0 ||
       !Readable(r.object,0x355) || At<const void*>(r.object,0x30)!=r.control)return false;
    return !alive || (!(r.object[0x18]&4) && !r.object[0x2E8]);
}
Ref Reference(const void* object) noexcept {
    auto* body=static_cast<unsigned char*>(const_cast<void*>(object));
    return {body,Readable(body,0x38) ? At<unsigned char*>(body,0x30) : nullptr};
}
bool Keep(Ref r) noexcept {
    if(!Current(r,false) || !Readable(r.control,16,true))return false;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(r.control+12));return true;
}
bool Drop(Ref& r) noexcept {
    if(r.control && (!Readable(r.control,16,true) || At<LONG>(r.control,12)<=0))return false;
    if(r.control &&
       InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12))==0) {
        const auto table=At<void* const*>(r.control,0);
        reinterpret_cast<void(__fastcall*)(void*)>(table[1])(r.control);
    }
    r={};return true;
}
bool Strong(const Ref& r) noexcept {
    if(!Current(r,false) || !Readable(r.control,16,true))return false;
    auto* uses=reinterpret_cast<volatile LONG*>(r.control+8);
    LONG was=*uses;
    while(was>0 && was<0x7FFFFFFF) {
        const LONG actual=InterlockedCompareExchange(uses,was+1,was);
        if(actual==was)return true;
        was=actual;
    }
    return false;
}
void Unstrong(const Ref& r) noexcept {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+8))!=0)return;
    auto* const* table=At<void* const*>(r.control,0);
    reinterpret_cast<void(__fastcall*)(void*)>(table[0])(r.control);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12))==0)
        reinterpret_cast<void(__fastcall*)(void*)>(table[1])(r.control);
}
bool Native() noexcept { return ready && image && image==admittedImage; }
bool Battery(const Ref& child,const Ref& owner,bool ownerAlive=true) noexcept {
    if(!Current(owner,ownerAlive) || !Current(child) || !Readable(child.object,0xD22,true) ||
       At<const void*>(child.object,0)!=image+kBatteryVtable ||
       At<const void*>(image+kBatteryVtable,5*8)!=image+0x32B210 ||
       At<const void*>(image+kBatteryVtable,7*8)!=image+0x54A000 ||
       At<const void*>(child.object,0x38)!=owner.object || At<const void*>(child.object,0x5C0)!=owner.object)return false;
    const int pattern=At<int>(child.object,0xB30);if(pattern!=0 && pattern!=1)return false;
    const auto col=At<const unsigned char*>(image+kBatteryVtable-8,0);
    if(!Readable(col,24) || At<unsigned>(col,0)!=1 || At<unsigned>(col,4)!=0 ||
       image+At<unsigned>(col,20)!=col)return false;
    const auto td=image+At<unsigned>(col,12);
    constexpr char name[]=".?AVDeiroiFootParts_Battery@@";
    return Readable(td+16,sizeof(name)) && std::memcmp(td+16,name,sizeof(name))==0;
}
bool Contains(const unsigned char* owner,const void* child) noexcept {
    const auto entries=At<const void* const*>(owner,0x48);
    const auto count=At<std::size_t>(owner,0x58),capacity=At<std::size_t>(owner,0x50);
    if(count>4096 || count>capacity || (count && !Readable(entries,count*sizeof(void*))))return false;
    for(std::size_t i=0;i<count;++i)if(entries[i]==child)return true;
    return false;
}
// These are the constructor-owned, ordered native SGO leg/part tables, not an AI
// list or a heap scan. Child registration is independently proved by SceneObject's
// actual parent-child registry and both native owner fields (3248B6/118AF20).
// Missing/dead nodes do not renumber surviving guns: key=(SGO leg<<16)|SGO part.
bool Enumerate(const NativeLease& lease,Entry* out,unsigned capacity,unsigned* total) noexcept {
    *total=0;
    if(!Native() || !IsDeiroi(lease))return false;
    const Ref owner{lease.object,static_cast<unsigned char*>(const_cast<void*>(lease.control))};
    if(!Current(owner) || !Readable(owner.object,0xB00))return false;
    const auto rows=At<const unsigned char*>(owner.object,0xAE8);
    const auto legs=At<std::size_t>(owner.object,0xAF8),legCapacity=At<std::size_t>(owner.object,0xAF0);
    if(!legs || legs>kMaxLegs || legs>legCapacity || !Readable(rows,legs*0x40))return false;
    for(unsigned leg=0;leg<legs;++leg) {
        const auto controller=At<const unsigned char*>(rows+static_cast<std::size_t>(leg)*0x40,0x28);
        if(!Readable(controller,0x28) || At<const void*>(controller,0)!=owner.object)return false;
        const auto nodes=At<const unsigned char*>(controller,0x10);
        const auto parts=At<std::size_t>(controller,0x20),partCapacity=At<std::size_t>(controller,0x18);
        if(parts>kMaxParts || parts>partCapacity || (parts && !Readable(nodes,parts*0x10)))return false;
        for(unsigned part=0;part<parts;++part) {
            const auto node=At<const unsigned char*>(nodes+static_cast<std::size_t>(part)*0x10,8);
            if(!Readable(node,0x28))return false;
            auto* child=At<unsigned char*>(node,8);
            if(!child || !Contains(owner.object,child))continue;
            if(!Readable(child,0x38))return false;
            const auto table=At<const void*>(child,0);
            if(table==image+0x17AB4B0 || table==image+0x17AB770)continue; // original joint/toe, no weapon
            if(table!=image+kBatteryVtable)return false; // never silently drop an unknown configured part
            const Ref ref=Reference(child);
            const auto identity=enemy_identity::Read(ref.object,ref.control,0x355);
            if(identity==enemy_identity::State::unknown)return false;
            if(identity==enemy_identity::State::gone || (ref.object[0x18]&4) || ref.object[0x2E8])continue;
            if(!Battery(ref,owner))return false;
            if(*total>=kMaxBatteries)return false;
            if(out && *total<capacity)out[*total]={ref,(leg<<16)|part};
            ++*total;
        }
    }
    return true;
}
// 31F192/31F199/31F235 iterate this constructor-owned vector. A row is
// plain storage with its owning root at +0, never an independently weak actor.
// Logical death is allowed only so a strong accepted context can drain outcomes.
bool MainRows(const NativeLease& lease,unsigned char** rows,unsigned* count,bool writable=false) noexcept {
    *rows=nullptr;*count=0;
    if(!Native() || !IsDeiroi(lease))return false;
    const Ref owner{lease.object,static_cast<unsigned char*>(const_cast<void*>(lease.control))};
    if(!Current(owner,false) || !Readable(owner.object,0x1650))return false;
    auto* table=At<unsigned char*>(owner.object,0x1638);
    const auto size=At<std::size_t>(owner.object,0x1648),capacity=At<std::size_t>(owner.object,0x1640);
    if(size>kMaxMainGuns || size>capacity || (size && !Readable(table,size*kMainStride,writable)))return false;
    for(unsigned i=0;i<size;++i) {
        const auto* row=table+static_cast<std::size_t>(i)*kMainStride;
        if(At<const void*>(row,0)!=owner.object || At<const void*>(row,0xB8)!=row+0x10 ||
           At<const void*>(row,0xC0)!=row)return false;
    }
    *rows=table;*count=static_cast<unsigned>(size);return true;
}
bool MainRow(const NativeLease& lease,unsigned key,unsigned char** row,bool writable=false) noexcept {
    *row=nullptr;if((key>>16)!=kMainGroup)return false;
    unsigned char* rows=nullptr;unsigned count=0;
    const unsigned index=key&0xFFFFu;
    if(!MainRows(lease,&rows,&count,writable) || index>=count)return false;
    *row=rows+static_cast<std::size_t>(index)*kMainStride;return true;
}
void MainSet(Root& root,unsigned char* row,unsigned callback) noexcept {
    // This registered machine accepts a DIRECT callback in RDX. Unlike the foot
    // machine's 16-byte descriptor, passing &Desc here calls unrelated storage.
    ++root.nativeInFlight;
    __try {reinterpret_cast<MainSetFn>(image+kMainSet)(row+0xB0,image+callback,nullptr);}
    __finally {--root.nativeInFlight;}
}
bool CancelMain(const NativeLease& lease,Root& root,unsigned index) noexcept {
    auto& gun=root.main[index];if(!gun.owned)return true;
    unsigned char* row=nullptr;
    if(!MainRow(lease,(kMainGroup<<16)|index,&row,true) || row!=gun.row)return false;
    const auto state=At<const void*>(row,0xC8);
    if(state==image+kMainFire)MainSet(root,row,kMainIdle);
    else if(state!=image+kMainIdle)return false;
    gun.owned=false;return true;
}
bool State(const Gun& gun,unsigned callback) noexcept {
    Desc desc{image+callback};
    return reinterpret_cast<bool(__fastcall*)(void*,const Desc*)>(image+kIsState)(gun.ref.object+kMachine,&desc);
}
void Set(Gun& gun,unsigned callback) noexcept {
    Desc desc{image+callback};
    const void* previous=explicitChild;explicitChild=gun.ref.object;Root* root=nullptr;
    for(auto& candidate:roots)for(auto& found:candidate.guns)if(&found==&gun){root=&candidate;break;}
    if(root)++root->nativeInFlight;
    __try {reinterpret_cast<SetFn>(image+kSet)(gun.ref.object+kMachine,&desc,nullptr);}
    __finally {if(root)--root->nativeInFlight;explicitChild=previous;}
}
bool BindingGone(const Root& root,const Gun& gun,bool* gone) noexcept {
    *gone=false;
    // A retained weak control proves destruction without touching old storage.
    if(!Readable(gun.ref.control,16))return false;
    const LONG uses=At<LONG>(gun.ref.control,8);
    if(uses==0){*gone=true;return true;}
    if(uses<0 || !Readable(gun.ref.object,0x5C8))return false;
    if(At<const void*>(gun.ref.object,0x30)!=gun.ref.control ||
       At<const void*>(gun.ref.object,0x38)!=root.ref.object ||
       At<const void*>(gun.ref.object,0x5C0)!=root.ref.object ||
       (gun.ref.object[0x18]&4) || gun.ref.object[0x2E8]) {*gone=true;return true;}
    return true;
}
bool Cancel(Root& root,Gun& gun) noexcept {
    if(!gun.owned)return true;
    bool gone=false;
    if(!BindingGone(root,gun,&gone))return false;
    if(gone){gun.owned=false;return true;}
    // A live same-binding but read-only/corrupt/replaced native object is NOT
    // destruction. Keep owned state and weak controls so restoration can retry.
    if(!Battery(gun.ref,root.ref,false))return false;
    if(!Strong(gun.ref))return false;
    bool ok=false;
    __try {
        if(State(gun,kCharge) || State(gun,kFire) || State(gun,kClose))Set(gun,kIdle);
        ok=true;gun.owned=false;
    } __finally { Unstrong(gun.ref); }
    return ok;
}
bool Clear(Root& root) noexcept {
    bool dropped=true;
    for(auto& gun:root.guns) {
        if(Drop(gun.ref))gun={};
        else dropped=false;
    }
    if(!dropped || !Drop(root.ref))return false;
    root={};return true;
}
Root* Owner(const NativeLease& lease,bool create) noexcept {
    Root* free=nullptr;
    for(auto& root:roots) {
        // Restoration must also find a retained control whose actor was destroyed
        // or whose page is temporarily inaccessible. Never forget a failed Drop.
        if(!create && root.ref.object==lease.object && root.ref.control==lease.control)return &root;
        if(root.ref.object && Readable(root.ref.control,16) && At<LONG>(root.ref.control,8)==0)Clear(root);
        if(root.ref.object==lease.object && root.ref.control==lease.control && Current(root.ref,false))return &root;
        if(!root.ref.object && !free)free=&root;
    }
    Ref ref{lease.object,static_cast<unsigned char*>(const_cast<void*>(lease.control))};
    if(!create || !free || !Current(ref) || !Keep(ref))return nullptr;
    free->ref=ref;free->profile=lease.profile;return free;
}
Gun* Controlled(const void* object,Root** owner=nullptr) noexcept {
    if(!Native())return nullptr;
    for(auto& root:roots)if(Current(root.ref))for(auto& gun:root.guns)
        if(gun.manual && gun.ref.object==object && Battery(gun.ref,root.ref)) {
            if(owner)*owner=&root;return &gun;
        }
    return nullptr;
}
bool Direction(const float* in,float* out) noexcept {
    const double sum=static_cast<double>(in[0])*in[0]+static_cast<double>(in[1])*in[1]+static_cast<double>(in[2])*in[2];
    if(!std::isfinite(sum) || sum<1e-18)return false;
    const double length=std::sqrt(sum);
    for(unsigned i=0;i<3;++i)out[i]=static_cast<float>(in[i]/length);
    return true;
}
void Cross(const float* a,const float* b,float* out) noexcept {
    out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0];
}
float Dot(const float* a,const float* b) noexcept {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
// Rotate the WHOLE native burst pattern by one shortest-arc world rotation. Never
// rebuild from its final forward: that collapses the original fan/ring pattern.
bool Rotate(float* matrix,const float* source,const float* desired,const float* locatorRight) noexcept {
    float a[3],b[3];
    if(!Direction(source,a) || !Direction(desired,b))return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return false;
    float v[3];Cross(a,b,v);const float c=(std::max)(-1.0f,(std::min)(1.0f,Dot(a,b)));
    float u[3]{};
    if(c<-0.99999f) {
        const float projection=Dot(locatorRight,a);float axis[3];
        for(unsigned i=0;i<3;++i)axis[i]=locatorRight[i]-a[i]*projection;
        if(!Direction(axis,u)) {
            const float basis[3]={std::fabs(a[0])<0.8f ? 1.0f : 0.0f,std::fabs(a[0])<0.8f ? 0.0f : 1.0f,0};
            Cross(a,basis,axis);if(!Direction(axis,u))return false;
        }
    }
    float copy[16];std::memcpy(copy,matrix,sizeof(copy));
    for(unsigned row=0;row<3;++row) {
        const float* r=matrix+row*4;
        if(c<-0.99999f) {
            const float projection=Dot(u,r);
            for(unsigned i=0;i<3;++i)copy[row*4+i]=2*u[i]*projection-r[i];
        } else {
            float first[3],second[3];Cross(v,r,first);Cross(v,first,second);
            for(unsigned i=0;i<3;++i)copy[row*4+i]=r[i]+first[i]+second[i]/(1+c);
        }
    }
    for(float value:copy)if(!std::isfinite(value))return false;
    std::memcpy(matrix,copy,sizeof(copy));return true;
}
bool AimPoint(const Root& root,float* point) noexcept {
    if(!Current(root.ref) || !std::isfinite(root.yaw) || !std::isfinite(root.elevation))return false;
    if(EnemyControlWeaponAim(root.ref.object,point)) {
        for(unsigned i=0;i<3;++i)if(!std::isfinite(point[i]))return false;
        point[3]=1;return true;
    }
    // An active core lease never substitutes a different point for an absent or
    // stale canonical frame. The fallback is for independently used adapters.
    if(EnemyControlTargetActive(root.ref.object))return false;
    float pivot[3];std::memcpy(pivot,root.ref.object+0x90,sizeof(pivot));
    edf41c::AimPoint(root.ref.object,pivot);
    const float direction[3]={std::sin(root.yaw)*std::cos(root.elevation),std::sin(root.elevation),std::cos(root.yaw)*std::cos(root.elevation)};
    // Same native lock-point pivot and semantic direction as EnemyControlCamera.
    // Keep one common target for all muzzles, rather than per-part parallel rays.
    for(unsigned i=0;i<3;++i){if(!std::isfinite(pivot[i]))return false;point[i]=pivot[i]+direction[i]*10000.0f;}
    point[3]=1;return true;
}
bool GunPoint(Root& root,Gun& gun,float* out) noexcept {
    if(AimPoint(root,out)) {
        std::memcpy(gun.confirmedPoint,out,sizeof(gun.confirmedPoint));gun.pointValid=true;return true;
    }
    // Once an attack is accepted, stale input holds its last confirmed canonical
    // point. It never turns the active weapon back toward an unrelated AI target.
    if(gun.pointValid){std::memcpy(out,gun.confirmedPoint,sizeof(gun.confirmedPoint));return true;}
    return false;
}
void __fastcall Auto(void* machine,const Desc* desc,void* data) {
    bool manual=false;
    __try {
        if(Readable(machine,0x30) && Readable(desc,sizeof(*desc)) && desc->function==image+kCharge) {
            const auto child=At<const void*>(machine,0x28);
            manual=Controlled(child)!=nullptr && static_cast<const unsigned char*>(child)+kMachine==machine;
            NativeLease root{};unsigned ordinal=0;bool authority=true;
            if(!manual && ResolveDeiroiControlledGun(child,&root,&ordinal) &&
               NativeActionRole(root.object,&authority) && (!authority || DeiroiAcceptedQuiescing(root.object)))manual=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { manual=false; }
    if(!manual)reinterpret_cast<SetFn>(image+kSet)(machine,desc,data);
}
void __fastcall Pose(void* child,const float* was,float gain) {
    alignas(16) float point[4];bool use=false;
    __try {Root* root=nullptr;if(auto* gun=Controlled(child,&root))use=GunPoint(*root,*gun,point);}
    __except(EXCEPTION_EXECUTE_HANDLER) {use=false;}
    reinterpret_cast<PoseFn>(image+0x3296E0)(child,use ? point : was,gain);
}
void __fastcall Weapon(void* holder,float* matrix,std::uint32_t seed) {
    __try {
        auto* child=static_cast<unsigned char*>(holder)-kHolder;Root* root=nullptr;
        if(auto* gun=Controlled(child,&root);gun && Readable(matrix,64,true)) {
            alignas(16) float locator[16]{};float point[4],source[3],desired[3];
            if(GunPoint(*root,*gun,point) && Readable(child+0xB80,16) && At<const void*>(child,0xB80)) {
                reinterpret_cast<MatrixFn>(image+0x6BB5A0)(child+0xB80,locator);
                const int pattern=At<int>(child,0xB30);
                if(pattern==0 || pattern==1) {
                    std::memcpy(source,pattern==0 ? reinterpret_cast<const float*>(child+0xB40) : locator+8,sizeof(source));
                    for(unsigned i=0;i<3;++i)desired[i]=point[i]-matrix[12+i];
                    Rotate(matrix,source,desired,locator);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    // The native network sender at32CFF8 reads this SAME caller matrix next.
    reinterpret_cast<WeaponFn>(image+0x6CFC60)(holder,matrix,seed);
}
bool Select(const NativeLease& lease,unsigned key,Entry* entry) noexcept {
    Entry all[kMaxBatteries]{};unsigned count=0;
    if(!Enumerate(lease,all,kMaxBatteries,&count))return false;
    for(unsigned i=0;i<count;++i)if(all[i].key==key){*entry=all[i];return true;}
    return false;
}
Gun* GunFor(Root& root,const Entry& entry) noexcept {
    Gun* free=nullptr;
    for(auto& gun:root.guns) {
        if(gun.ref.object) {
            const auto identity=enemy_identity::Read(gun.ref.object,gun.ref.control,0x355);
            if(identity==enemy_identity::State::unknown)return nullptr;
            if(identity==enemy_identity::State::gone){if(!Drop(gun.ref))return nullptr;gun={};}
        }
        if(gun.ref.object==entry.ref.object && gun.ref.control==entry.ref.control && gun.key==entry.key)return &gun;
        if(gun.ref.object && gun.key==entry.key)return nullptr; // reused ordinal never adopts another identity
        if(!gun.ref.object && !free)free=&gun;
    }
    if(!free || !Keep(entry.ref))return nullptr;
    free->ref=entry.ref;free->key=entry.key;return free;
}
bool Execute(const NativeLease& lease,unsigned key,bool automatic,std::uint64_t sequence,std::uint64_t seed) noexcept {
    if(!sequence || !seed || !DeiroiWeaponAimReady() || !IsDeiroi(lease) || DeiroiAcceptedNativeInFlight(lease))return false;
    Entry entry{};if(!Select(lease,key,&entry))return false;
    Root* root=Owner(lease,true);if(!root || root->faulted || root->nativeInFlight || !Current(root->ref))return false;
    bool authority=true;const bool networked=NativeActionRole(root->ref.object,&authority);
    if(!networked) {
        bool online=false;
        // A missing native role is not proof of offline play. The immutable
        // admitted core Session is the trusted fallback, including closing/fault.
        if(!EnemyControlNetworkContext(root->ref.object,&online) || online)return false;
    }
    Gun* gun=GunFor(*root,entry);if(!gun)return false;
    if(sequence<=gun->sequence)return sequence==gun->sequence && seed==gun->seed && automatic==gun->lastAutomatic && gun->lastResult;
    gun->sequence=sequence;gun->seed=seed;gun->lastAutomatic=automatic;gun->lastResult=false;
    if(!Strong(root->ref))return false;
    bool childHeld=false;
    __try {
        childHeld=Strong(gun->ref);
        if(!childHeld || !Battery(gun->ref,root->ref))return false;
        // Semantic input is intent on guests. The host-accepted stream alone
        // starts/cancels child native states and replays actual factory outcomes.
        if(networked && !authority){gun->lastResult=true;return true;}
        if(automatic) {
            if(!SetSeed(lease,seed))return false;
            Put<std::uint64_t>(gun->ref.object,0x3E8,seed);
            if(!Cancel(*root,*gun))return false;
            gun->manual=false;
            float point[3]{};if(gun->pointValid)std::memcpy(point,gun->confirmedPoint,sizeof(point));
            if(networked && !QueueDeiroiAcceptedControl(lease,key,1,sequence,seed,point)){root->faulted=true;return false;}
            gun->lastResult=true;return true;
        }
        // B00 is AI random selection delay, not a reload clock. Do not change it
        // or require a live AI target: manual blind fire is a normal native burst.
        // Idle, disabled/dead gates and the real B90 weapon lifecycle remain.
        enemy_control::Input input{};
        if(EnemyControlTargetActive(root->ref.object) &&
           (!EnemyControlCurrentInput(root->ref.object,&input) || !input.absoluteAim || input.sequence!=sequence))return false;
        alignas(16) float point[4];
        if(gun->ref.object[0x639] || !AimPoint(*root,point) || !State(*gun,kIdle) ||
           !Readable(gun->ref.object+kHolder,0x140,true))return false;
        if(!SetSeed(lease,seed))return false;
        Put<std::uint64_t>(gun->ref.object,0x3E8,seed);
        // Publish manual ownership only after a successful native transition.
        // A refused first press must not silently suppress automatic control.
        gun->owned=true;Set(*gun,kCharge);gun->manual=true;
        std::memcpy(gun->confirmedPoint,point,sizeof(point));gun->pointValid=true;
        if(networked && !QueueDeiroiAcceptedControl(lease,key,0,sequence,seed,point)){root->faulted=true;return false;}
        gun->lastResult=true;return true;
    } __finally {
        if(childHeld)Unstrong(gun->ref);
        Unstrong(root->ref);
    }
}
void Rollback(const NativeLease& lease,unsigned key,bool wasManual) noexcept {
    Root* root=Owner(lease,false);if(!root)return;
    for(auto& gun:root->guns)if(gun.ref.object && gun.key==key) {
        bool canceled=false;
        __try {canceled=Cancel(*root,gun);} __except(EXCEPTION_EXECUTE_HANDLER) {canceled=false;}
        gun.lastResult=false;
        if(canceled)gun.manual=wasManual;
        else root->faulted=true; // UpdateAim asks the core to retain/retry restoration
        return;
    }
}
} // namespace deiroi_control

bool DeiroiWeaponAimReady() noexcept {
    using namespace deiroi_control;
    __try {
        if(!Native() || !DeiroiEntityFactoryAimReady() || !DeiroiAcceptedActionsReady())return false;
        constexpr unsigned sites[]={kAutoCall,kPoseCall,kWeaponCall};
        const void* hooks[]={reinterpret_cast<const void*>(&Auto),reinterpret_cast<const void*>(&Pose),reinterpret_cast<const void*>(&Weapon)};
        for(unsigned i=0;i<3;++i) {
            const auto* at=image+sites[i];
            if(!Readable(at,5) || at[0]!=0xE8)return false;
            const auto target=at+5+At<std::int32_t>(at,1);
            if(target==hooks[i])continue;
            if(!Readable(target,12) || target[0]!=0x48 || target[1]!=0xB8 ||
               At<const void*>(target,2)!=hooks[i] || target[10]!=0xFF || target[11]!=0xE0)return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool InstallDeiroiControl() noexcept {
    using namespace deiroi_control;
    __try {
        if(Native())return DeiroiWeaponAimReady();
        if(!image || (admittedImage && image!=admittedImage))return false;
        for(const auto& p:kPrints)if(!Readable(image+p.at,sizeof(p.bytes)) || std::memcmp(image+p.at,p.bytes,sizeof(p.bytes)))return false;
        if(!InstallDeiroiEntityFactoryAim())return false;
        constexpr unsigned sites[]={kAutoCall,kPoseCall,kWeaponCall};
        constexpr unsigned targets[]={kSet,0x3296E0,0x6CFC60};
        void* const hooks[]={reinterpret_cast<void*>(&Auto),reinterpret_cast<void*>(&Pose),reinterpret_cast<void*>(&Weapon)};
        for(unsigned i=0;i<3;++i)if(!redirected[i]) {
            bool changed=false;
            const bool ok=RedirectCall(image+sites[i],image+targets[i],hooks[i],changed);
            redirected[i]=changed;
            if(!ok)return false;
        }
        admittedImage=image;ready=true;
        if(!InstallDeiroiAcceptedActions()){ready=false;return false;}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ReadDeiroiGunCount(const NativeLease& lease,unsigned* count) noexcept {
    if(!count)return false;*count=0;
    __try {unsigned total=0;if(!deiroi_control::Enumerate(lease,nullptr,0,&total))return false;*count=total;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
unsigned DeiroiGunCount(const NativeLease& lease) noexcept {
    unsigned count=0;
    return ReadDeiroiGunCount(lease,&count) ? count : ~0u; // failure is not an empty, complete skill catalogue
}
bool DeiroiGunOrdinal(const NativeLease& lease,unsigned index,unsigned* ordinal) noexcept {
    if(!ordinal)return false;
    __try {
        deiroi_control::Entry entries[deiroi_control::kMaxBatteries]{};unsigned count=0;
        if(!deiroi_control::Enumerate(lease,entries,deiroi_control::kMaxBatteries,&count) || index>=count)return false;
        *ordinal=entries[index].key;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool UpdateDeiroiAim(const NativeLease& lease,float yaw,float elevation) noexcept {
    using namespace deiroi_control;
    if(!IsDeiroi(lease))return true;
    __try {
        if(!DeiroiWeaponAimReady() || !std::isfinite(yaw) || !std::isfinite(elevation) || std::fabs(elevation)>1.5707963268f)return false;
        Root* root=Owner(lease,true);if(!root || root->faulted || !Current(root->ref))return false;
        root->yaw=std::remainder(yaw,6.2831853072f);root->elevation=elevation;
        Entry entries[kMaxBatteries]{};unsigned count=0;
        if(!Enumerate(lease,entries,kMaxBatteries,&count))return false;
        for(unsigned i=0;i<count;++i)if(!GunFor(*root,entries[i]))return false;
        if(!BindDeiroiAcceptedContext(lease)){root->faulted=true;return false;}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ExecuteDeiroiAbility(const NativeLease& lease,unsigned ordinal,bool automatic,std::uint64_t sequence,std::uint64_t seed) noexcept {
    bool wasManual=false;
    __try {
        if(auto* root=deiroi_control::Owner(lease,false))for(const auto& gun:root->guns)
            if(gun.ref.object && gun.key==ordinal){wasManual=gun.manual;break;}
        return deiroi_control::Execute(lease,ordinal,automatic,sequence,seed);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        deiroi_control::Rollback(lease,ordinal,wasManual);return false;
    }
}
bool ReadDeiroiAbilityState(const NativeLease& lease,unsigned key,bool automatic,enemy_control::Ability* out) noexcept {
    using namespace deiroi_control;
    if(!out || !IsDeiroi(lease))return false;
    out->state=enemy_control::AbilityState::unavailable;out->cooldownKnown=false;
    __try {
        Entry entry{};if(!Select(lease,key,&entry) || entry.ref.object[0x639])return true;
        bool manual=false,owned=false;
        // Read-only: do not call Owner(), whose dead-root sweep releases leases.
        for(const auto& root:roots)if(root.ref.object==lease.object && root.ref.control==lease.control && Current(root.ref))
            for(const auto& gun:root.guns)if(gun.key==key && gun.ref.object==entry.ref.object && gun.ref.control==entry.ref.control) {
                manual=gun.manual;owned=gun.owned;break;
            }
        if(automatic) {
            out->state=manual ? enemy_control::AbilityState::native : enemy_control::AbilityState::active;
            return true;
        }
        Gun gun{};gun.ref=entry.ref;
        if(manual && owned && (State(gun,kCharge) || State(gun,kFire) || State(gun,kClose)))out->state=enemy_control::AbilityState::active;
        else out->state=State(gun,kIdle) ? enemy_control::AbilityState::native : enemy_control::AbilityState::cooldown;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ResolveDeiroiControlledGun(const void* child,NativeLease* lease,unsigned* key) noexcept {
    using namespace deiroi_control;
    if(!child || !lease || !key)return false;
    __try {
        if(!Readable(child,0x5C8))return false;
        const auto parent=At<const void*>(child,0x5C0);
        for(const auto& root:roots)if(root.ref.object==parent && Current(root.ref)) {
            NativeLease candidate{};candidate.object=root.ref.object;candidate.control=root.ref.control;candidate.profile=root.profile;
            Entry entries[kMaxBatteries]{};unsigned count=0;
            if(!Enumerate(candidate,entries,kMaxBatteries,&count))return false;
            for(unsigned i=0;i<count;++i)if(entries[i].ref.object==child) {
                *lease=candidate;*key=entries[i].key;return true;
            }
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ResolveDeiroiControlledGunKey(const NativeLease& lease,unsigned key,void** child) noexcept {
    if(!child)return false;*child=nullptr;
    __try {deiroi_control::Entry entry{};if(!deiroi_control::Select(lease,key,&entry))return false;*child=entry.ref.object;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ResolveDeiroiRetainedGunKey(const NativeLease& lease,unsigned key,void** child) noexcept {
    using namespace deiroi_control;
    if(!child)return false;*child=nullptr;
    __try {
        if(ResolveDeiroiControlledGunKey(lease,key,child))return true;
        Root* root=Owner(lease,false);
        if(!root || !Current(root->ref,false))return false;
        for(const auto& gun:root->guns)if(gun.key==key && Current(gun.ref,false) &&
            Readable(gun.ref.object,0xD22) && At<const void*>(gun.ref.object,0)==image+kBatteryVtable &&
            (!Current(root->ref) || !Current(gun.ref))) {
            // Previously proved ordered node/Scene/dual-owner membership. A
            // strong accepted-stream context keeps its real native factory alive
            // after logical death solely to finish already accepted emissions.
            *child=gun.ref.object;return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ReadDeiroiMainGunCount(const NativeLease& lease,unsigned* count) noexcept {
    if(!count)return false;*count=0;
    __try {unsigned char* rows=nullptr;return deiroi_control::MainRows(lease,&rows,count);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ResolveDeiroiControlledHolder(const void* holder,NativeLease* lease,unsigned* key) noexcept {
    using namespace deiroi_control;
    if(!holder || !lease || !key)return false;
    __try {
        for(const auto& root:roots)if(Current(root.ref)) {
            NativeLease candidate{};candidate.object=root.ref.object;candidate.control=root.ref.control;candidate.profile=root.profile;
            Entry entries[kMaxBatteries]{};unsigned count=0;
            if(!Enumerate(candidate,entries,kMaxBatteries,&count))return false;
            for(unsigned i=0;i<count;++i)if(entries[i].ref.object+kHolder==holder) {
                *lease=candidate;*key=entries[i].key;return true;
            }
            unsigned char* rows=nullptr;
            if(!MainRows(candidate,&rows,&count))return false;
            for(unsigned i=0;i<count;++i)if(rows+static_cast<std::size_t>(i)*kMainStride+0x100==holder) {
                *lease=candidate;*key=(kMainGroup<<16)|i;return true;
            }
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ResolveDeiroiControlledHolderKey(const NativeLease& lease,unsigned key,void** actor,void** holder) noexcept {
    using namespace deiroi_control;
    if(!actor || !holder)return false;*actor=nullptr;*holder=nullptr;
    __try {
        if((key>>16)==kMainGroup) {
            Root* root=Owner(lease,false);unsigned char* row=nullptr;
            if(!root || !Current(root->ref,false) || !MainRow(lease,key,&row))return false;
            auto& retained=root->main[key&0xFFFFu];
            if(retained.row && retained.row!=row)return false;
            retained.row=row;*actor=root->ref.object;*holder=row+0x100;return true;
        }
        void* child=nullptr;if(!ResolveDeiroiRetainedGunKey(lease,key,&child))return false;
        *actor=child;*holder=static_cast<unsigned char*>(child)+kHolder;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool DeiroiGunManual(const NativeLease& lease,unsigned key) noexcept {
    using namespace deiroi_control;
    if(!IsDeiroi(lease))return false;
    __try {
        for(const auto& root:roots)if(root.ref.object==lease.object && root.ref.control==lease.control && Current(root.ref)) {
            if((key>>16)==kMainGroup) {unsigned char* row=nullptr;return MainRow(lease,key,&row);}
            for(const auto& gun:root.guns)if(gun.key==key && gun.manual && Battery(gun.ref,root.ref))return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool DeiroiNativeTransitionActive(const NativeLease& lease) noexcept {
    for(const auto& root:deiroi_control::roots)if(root.ref.object==lease.object && root.ref.control==lease.control)return root.nativeInFlight!=0;
    return false;
}
bool DeiroiExplicitTransition(const void* child) noexcept {return child && deiroi_control::explicitChild==child;}
bool ApplyDeiroiAcceptedControl(const NativeLease& lease,unsigned key,unsigned phase,std::uint64_t acceptedSequence,
                                std::uint64_t seed,const float point[3]) noexcept {
    using namespace deiroi_control;
    if(phase>7 || !acceptedSequence || (phase<2 && !seed) || !point || ((phase>=6)!=((key>>16)==kMainGroup)))return false;
    __try {
        for(unsigned i=0;i<3;++i)if(!std::isfinite(point[i]))return false;
        if(phase>=6) {
            Root* root=Owner(lease,false);unsigned char* row=nullptr;
            if(!root || root->faulted || root->nativeInFlight || !MainRow(lease,key,&row,true))return false;
            auto& gun=root->main[key&0xFFFFu];
            if(gun.row && gun.row!=row)return false;
            gun.row=row;
            if(acceptedSequence<=gun.acceptedSequence)return acceptedSequence==gun.acceptedSequence && phase==gun.acceptedPhase &&
                seed==gun.acceptedSeed && std::memcmp(point,gun.acceptedPoint,12)==0;
            if(!Strong(root->ref))return false;
            __try {
                // A retained dead root consumes accepted phases but never starts
                // a new action; previously accepted actual shots drain separately.
                if(Current(root->ref)) {
                    if(phase==6)gun.owned=true; // retain partial native entry for rollback
                    MainSet(*root,row,phase==6 ? kMainFire : kMainIdle);
                    if(phase==7)gun.owned=false;
                }
                // Main entry/idle consume no RNG. Do not rewind root RNG shared
                // with the independent root action producer.
                gun.acceptedSequence=acceptedSequence;gun.acceptedPhase=phase;gun.acceptedSeed=seed;
                std::memcpy(gun.acceptedPoint,point,12);return true;
            } __finally {Unstrong(root->ref);}
        }
        Entry entry{};
        if(!Select(lease,key,&entry)) {
            void* retained=nullptr;
            // Dead actors do not enter new animation states. Their already
            // accepted actual shots are replayed independently by the codec.
            return ResolveDeiroiRetainedGunKey(lease,key,&retained);
        }
        Root* root=Owner(lease,true);if(!root || root->faulted || root->nativeInFlight)return false;
        Gun* gun=GunFor(*root,entry);if(!gun)return false;
        if(acceptedSequence<=gun->acceptedSequence)return acceptedSequence==gun->acceptedSequence && phase==gun->acceptedPhase && seed==gun->acceptedSeed && std::memcmp(point,gun->acceptedPoint,12)==0;
        if(!Strong(root->ref))return false;
        bool childHeld=false;
        __try {
            childHeld=Strong(gun->ref);if(!childHeld || !Battery(gun->ref,root->ref))return false;
            if(phase<=1 && !SetSeed(lease,seed))return false;
            // Idle consumes exactly one native LCG draw (32D114..32D156).
            // State observations carry its post-entry RNG value, so reconstruct
            // the pre-entry value rather than consuming a second random sample.
            const auto nativeSeed=phase==5 ? (seed-0x269EC3ull)*0xDEDCEDAE9638806Dull : seed;
            Put<std::uint64_t>(gun->ref.object,0x3E8,nativeSeed);
            if(phase==1) {
                if(!Cancel(*root,*gun))return false;
                gun->manual=false;
            } else {
                const unsigned state=phase==0 || phase==2 ? kCharge : phase==3 ? kFire : phase==4 ? kClose : kIdle;
                gun->owned=true;Set(*gun,state);if(phase==5)gun->owned=false;
                if(phase==0) {
                    gun->manual=true;std::memcpy(gun->confirmedPoint,point,12);gun->confirmedPoint[3]=1;gun->pointValid=true;
                }
            }
            gun->acceptedSequence=acceptedSequence;gun->acceptedPhase=phase;gun->acceptedSeed=seed;std::memcpy(gun->acceptedPoint,point,12);return true;
        } __finally {if(childHeld)Unstrong(gun->ref);Unstrong(root->ref);}
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool RestoreDeiroiControl(const NativeLease& lease) noexcept {
    using namespace deiroi_control;
    if(!IsDeiroi(lease))return true;
    __try {
        Root* root=Owner(lease,false);if(!root)return true;
        if(root->nativeInFlight || DeiroiAcceptedNativeInFlight(lease))return false;
        if(!Readable(root->ref.control,16))return false;
        if(At<LONG>(root->ref.control,8)==0)return RestoreDeiroiAcceptedActions(lease) && Clear(*root);
        if(!Current(root->ref,false))return false;
        for(auto& gun:root->guns)if(gun.ref.object && !Cancel(*root,gun))return false;
        for(unsigned i=0;i<kMaxMainGuns;++i)if(!CancelMain(lease,*root,i))return false;
        if(!RestoreDeiroiAcceptedActions(lease))return false;
        return Clear(*root);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
} // namespace crew::enemy_native
