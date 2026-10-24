// Typed host-accepted Walking action states and actual native factory outcomes.
// Game-thread only. No native addresses, shared pointers or padded objects are serialized.
#include "enemy_control_walking_net.h"
#include "enemy_control_walking.h"
#include "enemy_control_walking_guns.h"
#include "enemy_control_walking_parts.h"
#include "enemy_control_action_native.h"
#include "enemy_control_projectile_native.h"
#include "enemy_control_gun_factory.h"
#include "enemy_control.h"
#include "crew.h"
#include "memory.h"
#include "command_identity.h"
#include "support_soldier.h"
#include <cmath>
#include <cstring>

namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_WALKING_NET_TEST
void* WalkingNetTestFunction(unsigned) noexcept;
#endif
namespace walking_net {
using Result=enemy_control::ActionResult;
constexpr std::uint32_t kTag=0x314B4C57u; // WLK1
constexpr unsigned kRoots=8,kGuns=256,kMuzzles=16,kQueued=4096,kBatch=8;
constexpr std::size_t kWireMax=512,kHeader=8;
constexpr unsigned kScalar32[]={0x30,0x50,0x54,0x58,0x5C,0x90,0x94,0x98,0x9C,0xA0,0xA4,0xA8,0xAC,0xB0,0xC0,0xC4,0xC8,0xCC,0xD0,0xD4,0xD8,0xDC,0xE0,0x108,0x10C,0x110,0x114,0x118,0x170,0x174};
constexpr unsigned kScalar8[]={0x28,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0x11C};
constexpr unsigned kWords=sizeof(kScalar32)/sizeof(kScalar32[0]),kBytes=sizeof(kScalar8)/sizeof(kScalar8[0]);
enum class EventKind:unsigned char { action=1,gun=2,shot=3,refresh=4 };
struct Event {
    EventKind kind{};unsigned char phase=0;unsigned key=0,muzzle=0;bool hasMuzzle=false;unsigned char resourceMask=0;std::uint64_t serial=0,digest=0,command=0,seed3=0,seed4=0;
    float point[3]{},aim[12]{},matrix[16]{},muzzleMatrix[16]{};int delay=0;std::uint64_t gunRng=0;
    std::uint32_t scalar[kWords]{},random=0,shot=0,flags=0;unsigned char bytes[kBytes]{};std::uint16_t scalar178=0;unsigned char target[32]{},extra[32]{};std::uint32_t lock=0;std::uint64_t value88=0;
};
struct Ref {unsigned char* object=nullptr;unsigned char* control=nullptr;};
struct Beam {Ref scene{};void* bullet=nullptr;std::uint64_t shot=0;float matrix[16]{};bool held=false,active=false,stopEntered=false,uncertain=false;};
struct Preparation {
    Ref cache[kMuzzles]{},scene[kMuzzles]{};bool held[kMuzzles]{},deleteEntered[kMuzzles]{};
    bool complete=false,entered=false,uncertain=false;unsigned count=0;
};
struct Part {Ref gun{},battery{};std::uint64_t digest=0;bool gunHeld=false,batteryHeld=false;Beam beams[kMuzzles]{};Preparation prep{};};
struct Replay {NativeProjectileJournal native{};void* node=nullptr;Ref result{};bool resultHeld=false,nodeLinked=false,uncertain=false,freeEntered=false;std::uint64_t serial=0;};
struct Receipt {std::uint64_t serial=0,hash=0;};
struct Stream {
    Ref root{};const NativeProfile* profile=nullptr;bool rootHeld=false,authority=false,faulted=false,quiescing=false,pointOwned=false,pendingAction=false,preparing=false;Event pending{};
    float point[3]{};unsigned action=0;std::uint64_t next=1,applied=0;unsigned head=0,count=0;
    Part guns[kGuns]{};Event queue[kQueued]{};Receipt receipts[kQueued]{};Replay replay{};
};
Stream streams[kRoots]{};unsigned char* admittedImage=nullptr;bool installed=false;
using VoidFn=void(__fastcall*)(void*);
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_WALKING_NET_TEST
    return reinterpret_cast<T>(WalkingNetTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool Family(const NativeLease& l) noexcept {
    if(!l.profile || !Readable(l.profile,sizeof(*l.profile)) || l.profile->kind!=Kind::walking)return false;
    std::size_t count=0;const auto profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(l.profile==profiles+i) {
        const auto v=l.profile->vtableRva;
        return v==0x17ADA78 || v==0x17AE088 || v==0x17AE968 || v==0x17AED50 || v==0x17AF200 || v==0x17AF9F8;
    }
    return false;
}
bool Current(Ref r,bool alive=true) noexcept {
    return r.object && Readable(r.control,16) && At<LONG>(r.control,8)>0 && Readable(r.object,0x355) &&
        At<const void*>(r.object,0x30)==r.control && (!alive || (!(r.object[0x18]&4) && !r.object[0x2E8]));
}
Ref Reference(const NativeLease& l) noexcept {return {l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))};}
bool Pin(Ref r) noexcept {
    if(!Readable(r.control,16,true))return false;
    auto* uses=reinterpret_cast<volatile LONG*>(r.control+8);LONG was=*uses;
    while(was>0 && was<0x7FFFFFFF) {const LONG actual=InterlockedCompareExchange(uses,was+1,was);if(actual==was)return true;was=actual;}
    return false;
}
bool CanUnpin(Ref r) noexcept {
    if(!Readable(r.control,16,true) || At<LONG>(r.control,8)<=0)return false;
    const auto table=At<void* const*>(r.control,0);
    return Readable(table,16) && table[0] && table[1];
}
void Unpin(Ref r) noexcept {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+8))!=0)return;
    const auto table=At<void* const*>(r.control,0);reinterpret_cast<VoidFn>(table[0])(r.control);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12))==0)reinterpret_cast<VoidFn>(table[1])(r.control);
}
NativeLease Lease(const Stream& s) noexcept {NativeLease l{};l.object=s.root.object;l.control=s.root.control;l.profile=s.profile;return l;}
Stream* Find(const NativeLease& l) noexcept {
    for(auto& s:streams)if(s.root.object==l.object && s.root.control==l.control)return &s;return nullptr;
}
Stream* Bind(const NativeLease& l,bool authority) noexcept {
    if(!installed || image!=admittedImage || !Family(l) || !Current(Reference(l),false))return nullptr;
    if(auto* s=Find(l))return s->authority==authority ? s : nullptr;
    for(auto& s:streams)if(!s.rootHeld) {
        if(!Pin(Reference(l)))return nullptr;s.root=Reference(l);s.rootHeld=true;s.profile=l.profile;s.authority=authority;return &s;
    }
    return nullptr;
}
bool Finite(const float* values,unsigned count) noexcept {for(unsigned i=0;i<count;++i)if(!std::isfinite(values[i]))return false;return true;}
bool Matrix(const float* v) noexcept {
    if(!Finite(v,16) || std::fabs(v[15]-1.0f)>0.0001f)return false;
    double norms[3]{};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)norms[i]+=static_cast<double>(v[i*4+j])*v[i*4+j];
    if(!(norms[0]>0 && norms[1]>0 && norms[2]>0))return false;
    const double d=static_cast<double>(v[0])*(v[5]*v[10]-v[6]*v[9])-static_cast<double>(v[1])*(v[4]*v[10]-v[6]*v[8])+static_cast<double>(v[2])*(v[4]*v[9]-v[5]*v[8]);
    return std::isfinite(d) && std::fabs(d)>std::sqrt(norms[0]*norms[1]*norms[2])*1.0e-8;
}
bool Valid(const Event& e) noexcept {
    if(!e.serial || !e.digest)return false;
    if(e.kind==EventKind::action)return e.key<4096 && e.command && Finite(e.point,3);
    if(e.key>=kGuns)return false;
    if(e.kind==EventKind::gun)return e.command && e.phase<2 && (e.phase || e.delay>=0) && Finite(e.point,3) && Finite(e.aim,12);
    if(e.muzzle>=kMuzzles)return false;
    if(e.kind==EventKind::refresh)return e.command && e.phase<2 && Matrix(e.muzzleMatrix);
    if(e.kind!=EventKind::shot || e.value88>1)return false;
    NativeProjectileSample sample{};std::memcpy(sample.matrix,e.matrix,64);std::memcpy(sample.muzzleMatrix,e.muzzleMatrix,64);
    std::memcpy(sample.scalar,e.scalar,sizeof(e.scalar));std::memcpy(sample.bytes,e.bytes,sizeof(e.bytes));
    sample.random=e.random;sample.shot=e.shot;sample.flags=e.flags;sample.scalar178=e.scalar178;sample.lock=e.lock;
    std::memcpy(sample.target,e.target,32);std::memcpy(sample.extra,e.extra,32);sample.hasMuzzle=e.hasMuzzle;sample.extraObject=e.value88!=0;sample.resourceMask=e.resourceMask;
    return ValidNativeProjectileSample(sample);
}
std::size_t Bytes(const Event& e) noexcept {
    if(!Valid(e))return 0;
    if(e.kind==EventKind::action)return 56;
    if(e.kind==EventKind::gun)return 108;
    if(e.kind==EventKind::refresh)return 94;
    return 19u+4u+128u+kWords*4u+kBytes+2u+12u+64u+4u;
}
void PutWire(unsigned char*& p,std::uint64_t value,unsigned size) noexcept {for(unsigned i=0;i<size;++i)*p++=static_cast<unsigned char>(value>>(i*8));}
std::uint64_t GetWire(const unsigned char*& p,unsigned size) noexcept {std::uint64_t value=0;for(unsigned i=0;i<size;++i)value|=std::uint64_t(*p++)<<(i*8);return value;}
void PutFloats(unsigned char*& p,const float* values,unsigned count) noexcept {
    for(unsigned i=0;i<count;++i){std::uint32_t bits=0;std::memcpy(&bits,values+i,4);PutWire(p,bits,4);}
}
void GetFloats(const unsigned char*& p,float* values,unsigned count) noexcept {
    for(unsigned i=0;i<count;++i){const auto bits=static_cast<std::uint32_t>(GetWire(p,4));std::memcpy(values+i,&bits,4);}
}
bool Encode(const Event* events,unsigned count,void* out,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;if(!events || !out || !used || !count || count>kBatch)return false;
    std::size_t size=kHeader;std::uint64_t previous=0;
    for(unsigned i=0;i<count;++i){const auto n=Bytes(events[i]);if(!n || events[i].serial<=previous)return false;previous=events[i].serial;size+=n;}
    if(size>capacity || size>kWireMax)return false;
    auto* p=static_cast<unsigned char*>(out);PutWire(p,kTag,4);*p++=2;*p++=static_cast<unsigned char>(count);*p++=0;*p++=0;
    for(unsigned i=0;i<count;++i) {
        const auto& e=events[i];*p++=static_cast<unsigned char>(e.kind);PutWire(p,e.serial,8);PutWire(p,e.key,2);PutWire(p,e.digest,8);
        if(e.kind==EventKind::action){PutWire(p,e.command,8);PutWire(p,e.seed3,8);PutWire(p,e.seed4,8);PutFloats(p,e.point,3);*p++=e.phase;}
        else if(e.kind==EventKind::gun){*p++=e.phase;PutWire(p,e.command,8);PutWire(p,e.seed3,8);PutFloats(p,e.point,3);PutWire(p,static_cast<unsigned>(e.delay),4);PutFloats(p,e.aim,12);PutWire(p,e.gunRng,8);}
        else if(e.kind==EventKind::refresh){PutWire(p,e.muzzle,2);*p++=e.phase;PutWire(p,e.command,8);PutFloats(p,e.muzzleMatrix,16);}
        else {PutWire(p,e.muzzle,2);*p++=static_cast<unsigned char>((e.hasMuzzle ? 1u : 0u)|(e.value88 ? 2u : 0u));*p++=e.resourceMask;
            PutFloats(p,e.matrix,16);PutFloats(p,e.muzzleMatrix,16);for(auto v:e.scalar)PutWire(p,v,4);for(auto v:e.bytes)*p++=v;
            PutWire(p,e.scalar178,2);PutWire(p,e.random,4);PutWire(p,e.shot,4);PutWire(p,e.flags,4);for(auto v:e.target)*p++=v;for(auto v:e.extra)*p++=v;PutWire(p,e.lock,4);}
    }
    *used=size;return static_cast<std::size_t>(p-static_cast<unsigned char*>(out))==size;
}
bool Decode(const void* input,std::size_t size,Event* events,unsigned* count) noexcept {
    if(count)*count=0;if(!input || !events || !count || size<kHeader || size>kWireMax)return false;
    const auto* p=static_cast<const unsigned char*>(input);const auto* end=p+size;
    if(GetWire(p,4)!=kTag || *p++!=2)return false;const unsigned total=*p++;
    if(!total || total>kBatch || *p++ || *p++)return false;
    Event pending[kBatch]{};std::uint64_t previous=0;
    for(unsigned i=0;i<total;++i) {
        if(end-p<19)return false;auto& e=pending[i];e.kind=static_cast<EventKind>(*p++);e.serial=GetWire(p,8);e.key=static_cast<unsigned>(GetWire(p,2));e.digest=GetWire(p,8);
        const auto needed=e.kind==EventKind::action ? 37u : e.kind==EventKind::gun ? 89u : e.kind==EventKind::refresh ? 75u : e.kind==EventKind::shot ? (4u+128u+kWords*4u+kBytes+2u+12u+64u+4u) : 0u;
        if(!needed || static_cast<std::size_t>(end-p)<needed)return false;
        if(e.kind==EventKind::action){e.command=GetWire(p,8);e.seed3=GetWire(p,8);e.seed4=GetWire(p,8);GetFloats(p,e.point,3);e.phase=*p++;}
        else if(e.kind==EventKind::gun){e.phase=*p++;e.command=GetWire(p,8);e.seed3=GetWire(p,8);GetFloats(p,e.point,3);e.delay=static_cast<int>(static_cast<std::uint32_t>(GetWire(p,4)));GetFloats(p,e.aim,12);e.gunRng=GetWire(p,8);}
        else if(e.kind==EventKind::refresh){e.muzzle=static_cast<unsigned>(GetWire(p,2));e.phase=*p++;e.command=GetWire(p,8);GetFloats(p,e.muzzleMatrix,16);}
        else {e.muzzle=static_cast<unsigned>(GetWire(p,2));const unsigned attributes=*p++;if(attributes&~3u)return false;e.hasMuzzle=(attributes&1)!=0;e.value88=(attributes&2)!=0;e.resourceMask=*p++;
            GetFloats(p,e.matrix,16);GetFloats(p,e.muzzleMatrix,16);for(auto& v:e.scalar)v=static_cast<std::uint32_t>(GetWire(p,4));for(auto& v:e.bytes)v=*p++;
            e.scalar178=static_cast<std::uint16_t>(GetWire(p,2));e.random=static_cast<std::uint32_t>(GetWire(p,4));e.shot=static_cast<std::uint32_t>(GetWire(p,4));e.flags=static_cast<std::uint32_t>(GetWire(p,4));for(auto& v:e.target)v=*p++;for(auto& v:e.extra)v=*p++;e.lock=static_cast<std::uint32_t>(GetWire(p,4));}
        if(!Valid(e) || e.serial<=previous)return false;previous=e.serial;
    }
    if(p!=end)return false;for(unsigned i=0;i<total;++i)events[i]=pending[i];*count=total;return true;
}
std::uint64_t Hash(const Event& e) noexcept {
    unsigned char wire[kWireMax]{};std::size_t used=0;if(!Encode(&e,1,wire,sizeof(wire),&used))return 0;
    std::uint64_t value=14695981039346656037ull;for(std::size_t i=0;i<used;++i){value^=wire[i];value*=1099511628211ull;}return value;
}
bool Push(Stream& s,Event e,bool committed=false) noexcept {
    if((!committed && (s.faulted || s.quiescing)) || s.count==kQueued || !s.next){s.faulted=true;return false;}
    e.serial=s.next;if(!Valid(e)){s.faulted=true;return false;}s.queue[(s.head+s.count)%kQueued]=e;++s.count;
    if(++s.next==0){s.faulted=true;return false;}
    // A prior producer fault does not invalidate an already-real outcome that
    // was successfully appended for authenticated normal drain.
    return true;
}
bool PartReady(const Part& p) noexcept {
    if(!p.batteryHeld || !Readable(p.battery.control,16) || At<LONG>(p.battery.control,8)<=0 ||
       p.battery.object!=p.battery.control+0x10 || !Readable(p.battery.object,0x6C0,true))return false;
    if(!p.gunHeld)return true;
    return Readable(p.gun.control,16) && At<LONG>(p.gun.control,8)>0 && p.gun.object==p.gun.control+0x10 && Readable(p.gun.object,0x90,true) &&
        At<const void*>(p.gun.object,0)==p.battery.object && At<const void*>(p.gun.object,8)==p.battery.control;
}
bool Muzzle(const Part& p,unsigned index,unsigned char** out) noexcept {
    if(!out || !PartReady(p))return false;
    const auto count=At<std::size_t>(p.battery.object,0x288);const auto rows=At<unsigned char*>(p.battery.object,0x278);
    if(!count || count>kMuzzles || index>=count || !Readable(rows,count*0xD0,true))return false;
    *out=rows+static_cast<std::size_t>(index)*0xD0;return true;
}
bool KeepBattery(Stream& s,const NativeLease& l,unsigned key,Ref gun,Ref battery,std::uint64_t digest) noexcept {
    if(key>=kGuns || !digest || !Readable(battery.object,0x6C0) || !Readable(battery.control,16) || At<LONG>(battery.control,8)<=0 ||
       At<const void*>(battery.object,0x2B8)!=s.root.control || At<const void*>(battery.object,0x2B0)!=At<const void*>(l.object,0x28))return false;
    auto& p=s.guns[key];
    if(p.batteryHeld)return p.gun.object==gun.object && p.gun.control==gun.control && p.battery.object==battery.object && p.battery.control==battery.control && p.digest==digest && PartReady(p);
    if(gun.control && !p.gunHeld){if(!Pin(gun))return false;p.gun=gun;p.gunHeld=true;}
    if(!Pin(battery))return false;p.battery=battery;p.batteryHeld=true;p.digest=digest;
    unsigned char* muzzle=nullptr;return Muzzle(p,0,&muzzle);
}
bool KeepGuns(Stream& s,const NativeLease& l) noexcept {
    if(l.profile->vtableRva==0x17AE088 || l.profile->vtableRva==0x17AF9F8) {
        if(!Readable(l.object,0x1A70))return false;std::uint64_t digest=0;
        const Ref battery{At<unsigned char*>(l.object,0x1A60),At<unsigned char*>(l.object,0x1A68)};
        return WalkingBatteryDefinition(l,0,&digest) && KeepBattery(s,l,0,{},battery,digest);
    }
    if(l.profile->vtableRva!=0x17ADA78)return true;
    unsigned count=0;if(!WalkingGunPreparationCount(l,&count) || count>kGuns)return false;
    // Preparation owns the complete physical roster, including passive Hector
    // shield slots that intentionally do not appear as selectable attacks.
    for(unsigned key=0;key<count;++key) {
        void* gun=nullptr;std::uint64_t digest=0;
        if(!ResolveWalkingGunPreparationKey(l,key,&gun,&digest) || !Readable(gun,0x90))return false;
        const Ref actualGun{static_cast<unsigned char*>(gun),static_cast<unsigned char*>(gun)-0x10};
        const Ref battery{At<unsigned char*>(gun,0),At<unsigned char*>(gun,8)};
        if(!KeepBattery(s,l,key,actualGun,battery,digest))return false;
    }
    return true;
}
bool TickBeams(Stream&,bool stop) noexcept;
bool PrepareSources(Stream&) noexcept;
bool RestorePreparation(Part&) noexcept;
bool Before(const NativeLease& l,bool authority,Input* input) noexcept {
    __try {
        auto* s=Bind(l,authority);if(!s || s->faulted || s->quiescing || !KeepGuns(*s,l) || !PrepareSources(*s) || !TickBeams(*s,false))return false;
        if(authority && input && !WalkingPartsEmpty(l))input->pressed&=~0x30u;
        // Actual vanilla tag10 still transports the host's generic/contextual
        // root attack. A guest must not independently re-trigger those edges.
        if(!authority && input){input->held&=~0x30u;input->pressed&=~0x30u;}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
bool After(const NativeLease& l) noexcept {const auto* s=Find(l);return !s || !s->faulted;}
bool Needs(const NativeLease& l) noexcept {return Family(l);}
void Sample(const Event&,NativeProjectileSample*) noexcept;
bool Preflight(Stream& s,const NativeLease& l,const Event& e) noexcept {
    if(!Current(s.root,false))return false;
    if(e.kind==EventKind::action){std::uint64_t digest=0;return WalkingActionDefinition(l,e.key,&digest) && digest==e.digest && Readable(l.object+0x3E8,8,true) && Readable(l.object+0x490,8,true) && Readable(l.object+0x1940,1,true);}
    if(e.key>=kGuns || !PartReady(s.guns[e.key]) || s.guns[e.key].digest!=e.digest)return false;
    const auto& part=s.guns[e.key];
    if(e.kind==EventKind::gun)return part.gunHeld;
    unsigned char* muzzle=nullptr;if(!Muzzle(part,e.muzzle,&muzzle))return false;
    if(e.kind==EventKind::refresh)return Readable(At<const void*>(muzzle,0xB8),16) && At<LONG>(At<const void*>(muzzle,0xB8),8)>0 && Readable(At<const void*>(muzzle,0xB0),0xC38);
    NativeProjectileSample sample{};Sample(e,&sample);
    return CanReplayNativeProjectileSample(l,part.battery.object+0x2F0,part.battery.object+0x300,reinterpret_cast<float*>(muzzle+0x70),sample);
}
bool Emit(Stream&,const Event&) noexcept;
bool CleanReplay(Stream&) noexcept;
bool ReleaseBeams(Stream&) noexcept;
bool Release(Stream& s) noexcept {
    if(s.preparing || !CleanReplay(s) || !ReleaseBeams(s) || (s.rootHeld && !CanUnpin(s.root)))return false;
    for(auto& p:s.guns)if(!RestorePreparation(p))return false;
    for(const auto& p:s.guns)if((p.gunHeld && !CanUnpin(p.gun)) || (p.batteryHeld && !CanUnpin(p.battery)))return false;
    for(auto& p:s.guns){if(p.batteryHeld){Unpin(p.battery);p.batteryHeld=false;}if(p.gunHeld){Unpin(p.gun);p.gunHeld=false;}p={};}
    if(s.rootHeld){Unpin(s.root);s.rootHeld=false;}std::memset(&s,0,sizeof(s));s.next=1;return true;
}

using MoveWeakFn=void(__fastcall*)(void*,void*);
using CastFn=void*(__fastcall*)(void*,long,const void*,const void*,int);
using AllocFn=void*(__fastcall*)(std::size_t);
using FreeFn=void(__fastcall*)(void*,std::size_t);
struct NativePrint {unsigned at;unsigned char bytes[16];};
constexpr NativePrint kNativePrints[]={
    {0x6D3EC0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x83,0xA1,0xC0,0x02,0x00,0x00,0xFE,0x48,0x8B,0xD9}},
    {0x6D4040,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x22F390,{0x83,0xB9,0x30,0x0C,0x00,0x00,0x01,0x75,0x19,0x8B,0x91,0x34,0x0C,0x00,0x00,0x8B}},
    {0x58EB40,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x02,0x48,0x8B,0xD9,0x4C,0x8B,0x42,0x08}},
    {0x118A1B0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48}},
    {0x12D85B0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xEB,0x0F,0x48,0x8B,0xCB,0xE8,0x47}},
    {0x12D85EC,{0xE9,0xB7,0x0B,0x00,0x00,0xCC,0xCC,0xCC,0x48,0x83,0xEC,0x28,0x4D,0x8B,0x41,0x38}},
};
void Sample(const Event& e,NativeProjectileSample* out) noexcept {
    *out={};std::memcpy(out->matrix,e.matrix,64);std::memcpy(out->muzzleMatrix,e.muzzleMatrix,64);
    std::memcpy(out->scalar,e.scalar,sizeof(e.scalar));std::memcpy(out->bytes,e.bytes,sizeof(e.bytes));out->scalar178=e.scalar178;
    out->random=e.random;out->shot=e.shot;out->flags=e.flags;out->lock=e.lock;out->hasMuzzle=e.hasMuzzle;out->extraObject=e.value88!=0;out->resourceMask=e.resourceMask;
    std::memcpy(out->target,e.target,32);std::memcpy(out->extra,e.extra,32);
}
Event Shot(unsigned key,unsigned muzzle,std::uint64_t digest,const NativeProjectileSample& sample) noexcept {
    Event e{};e.kind=EventKind::shot;e.key=key;e.muzzle=muzzle;e.digest=digest;e.hasMuzzle=sample.hasMuzzle;e.resourceMask=sample.resourceMask;e.value88=sample.extraObject ? 1u : 0u;
    std::memcpy(e.matrix,sample.matrix,64);std::memcpy(e.muzzleMatrix,sample.muzzleMatrix,64);std::memcpy(e.scalar,sample.scalar,sizeof(e.scalar));std::memcpy(e.bytes,sample.bytes,sizeof(e.bytes));
    e.scalar178=sample.scalar178;e.random=sample.random;e.shot=sample.shot;e.flags=sample.flags;e.lock=sample.lock;
    std::memcpy(e.target,sample.target,32);std::memcpy(e.extra,sample.extra,32);return e;
}
bool RootForFactory(const void* factory,NativeLease* out) noexcept {
    if(!factory || !out)return false;const auto value=reinterpret_cast<std::uintptr_t>(factory);if(value<0x2F0)return false;
    const auto battery=reinterpret_cast<const unsigned char*>(value-0x2F0);
    if(!Readable(battery,0x6C0))return false;
    auto* root=At<unsigned char*>(battery,0x2B0);const auto control=At<const void*>(battery,0x2B8);
    if(!root || !Readable(root,0x38) || At<const void*>(root,0x30)!=control || !Readable(control,16) || At<LONG>(control,8)<=0)return false;
    const auto table=At<const unsigned char*>(root,0);if(!table || !Readable(table-8,8))return false;
    const auto col=At<const void*>(table-8,0);std::size_t count=0;const auto profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(profiles[i].kind==Kind::walking && Readable(image+profiles[i].vtableRva-8,8) && At<const void*>(image+profiles[i].vtableRva-8,0)==col) {
        NativeLease l{};l.object=root;l.control=control;l.profile=profiles+i;if(!Family(l))return false;*out=l;return true;
    }
    return false;
}
Stream* FactoryStream(const void* factory,unsigned* key) noexcept {
    for(auto& s:streams)if(s.rootHeld)for(unsigned i=0;i<kGuns;++i)if(s.guns[i].batteryHeld && factory==s.guns[i].battery.object+0x2F0){if(key)*key=i;return &s;}
    return nullptr;
}
bool FindMuzzle(const Part& p,const void* descriptor,unsigned* index) noexcept {
    if(!descriptor || !index || !Readable(descriptor,0x1E0))return false;
    const auto point=At<const void*>(descriptor,0x70);
    for(unsigned i=0;i<kMuzzles;++i){unsigned char* muzzle=nullptr;if(!Muzzle(p,i,&muzzle))break;if(point==muzzle+0x70){*index=i;return true;}}
    return false;
}
bool ListReady(const Part& p) noexcept {
    if(!PartReady(p) || At<const void*>(p.battery.object,0x650) || At<const void*>(p.battery.object,0x658))return false;
    const auto head=At<unsigned char*>(p.battery.object,0x290);
    const auto count=At<std::uint64_t>(p.battery.object,0x298);
    if(!Readable(head,32,true) || count>=0x7FFFFFFFFFFFFFFull)return false;
    const auto last=At<unsigned char*>(head,8);
    return Readable(last,16,true) && At<const void*>(last,0)==head;
}
bool DropBeam(Beam& b) noexcept {
    if(b.uncertain || (b.held && !CanUnpin(b.scene)))return false;
    if(b.held)Unpin(b.scene);b={};return true;
}
bool BeamAlive(const Beam& b) noexcept {
    return b.held && Readable(b.scene.control,16) && At<LONG>(b.scene.control,8)>0 && Readable(b.scene.object,0x1C) && !(b.scene.object[0x18]&4) &&
        Readable(b.bullet,0xC38) && At<unsigned>(b.bullet,0xC30)==1 && !(At<unsigned>(b.bullet,0xC34)&0x4000u);
}
bool BeamMethod(const void* bullet) noexcept {
    if(!Readable(bullet,8))return false;const auto table=At<const void* const*>(bullet,0);
    return Readable(table,13*8) && table[12]==image+0x22F390;
}
// The prepare/activate boundary cancels an NPC's pre-existing native burst.
// Its old cached beam has no accepted shot serial and must never be adopted as
// a new player command. All cache identities are retained before native work.
bool ClearableWeak(Ref r) noexcept {
    if(!r.control)return !r.object;
    if(!r.object || !Readable(r.control,16,true) || At<LONG>(r.control,8)<0 || At<LONG>(r.control,12)<=0)return false;
    const auto table=At<void* const*>(r.control,0);return Readable(table,16) && table[1];
}
bool PreflightPreparation(Part& p) noexcept {
    auto& prep=p.prep;if(prep.uncertain)return false;if(prep.complete)return true;
    if(prep.entered || !PartReady(p))return false;
    const auto count=At<std::size_t>(p.battery.object,0x288);
    if(!count || count>kMuzzles || (p.gunHeld && !Readable(p.gun.object+0x38,4,true)))return false;
    if(prep.count && prep.count!=count)return false;prep.count=static_cast<unsigned>(count);
    for(unsigned i=0;i<prep.count;++i) {
        unsigned char* muzzle=nullptr;if(!Muzzle(p,i,&muzzle))return false;
        const Ref cache{At<unsigned char*>(muzzle,0xB0),At<unsigned char*>(muzzle,0xB8)};
        if(prep.held[i]) {
            if(prep.cache[i].object!=cache.object || prep.cache[i].control!=cache.control || !Current(prep.scene[i],false))return false;
            continue;
        }
        if(!ClearableWeak(cache))return false;prep.cache[i]=cache;
        if(!cache.control || !At<LONG>(cache.control,8))continue; // expired weak: never dereference its old object
        // Match the native weak lock order: acquire the control before reading
        // its aliased object. A positive use-count read alone is not ownership.
        if(!Pin(cache))return false;prep.scene[i]=cache;prep.held[i]=true;
        if(!Readable(cache.object,0xC38) || At<unsigned>(cache.object,0xC30)!=1 || !BeamMethod(cache.object))return false;
        const Ref scene{At<unsigned char*>(cache.object,0x28),At<unsigned char*>(cache.object,0x30)};
        if(scene.control!=cache.control || !Current(scene,false))return false;
        prep.scene[i]=scene;
    }
    return true;
}
bool ClearPreparedCache(Part& p,unsigned index) noexcept {
    unsigned char* muzzle=nullptr;if(!Muzzle(p,index,&muzzle))return false;
    auto& prep=p.prep;const Ref current{At<unsigned char*>(muzzle,0xB0),At<unsigned char*>(muzzle,0xB8)};
    if(!current.object && !current.control)return true;
    if(current.object!=prep.cache[index].object || current.control!=prep.cache[index].control || !ClearableWeak(current))return false;
    void* empty[2]{};Fn<MoveWeakFn>(0x58EB40)(muzzle+0xB0,empty);
    return !At<const void*>(muzzle,0xB0) && !At<const void*>(muzzle,0xB8);
}
bool PreparedCacheIdentity(const Part& p,unsigned index) noexcept {
    unsigned char* muzzle=nullptr;if(!Muzzle(p,index,&muzzle))return false;
    const auto& prep=p.prep;const Ref current{At<unsigned char*>(muzzle,0xB0),At<unsigned char*>(muzzle,0xB8)};
    if(current.object==prep.cache[index].object && current.control==prep.cache[index].control)return true;
    // Only our explicit clearing of an expired/deleted snapshot may be empty.
    return !current.object && !current.control && (!prep.held[index] || (prep.scene[index].object[0x18]&4));
}
bool RestorePreparation(Part& p) noexcept {
    auto& prep=p.prep;if(prep.uncertain || (prep.entered && !prep.complete))return false;
    for(unsigned i=0;i<prep.count;++i)if(prep.held[i] && !CanUnpin(prep.scene[i]))return false;
    __try {
        for(unsigned i=0;i<prep.count;++i)if(prep.held[i]){prep.held[i]=false;Unpin(prep.scene[i]);}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){prep.uncertain=true;return false;}
}
bool PrepareOne(Part& p) noexcept {
    auto& prep=p.prep;if(prep.uncertain)return false;if(prep.complete)return true;if(prep.entered)return false;
    __try {
        prep.entered=true;
        if(p.gunHeld)Put<int>(p.gun.object,0x38,-1);
        Fn<VoidFn>(0x6D3EC0)(p.battery.object);
        if((At<unsigned>(p.battery.object,0x2C0)&1) || At<unsigned>(p.battery.object,0x2E0) || At<unsigned>(p.battery.object,0x2E8)) {prep.uncertain=true;return false;}
        // Stop/native destruction may reenter and replace another cache. The
        // original 6D4040 merely locks each current weak, calls 118A1B0 and drops
        // that lock. We already retained every real snapshot: use exactly those
        // objects rather than deleting a foreign replacement from a later loop.
        for(unsigned i=0;i<prep.count;++i)if(!PreparedCacheIdentity(p,i)){prep.uncertain=true;return false;}
        for(unsigned i=0;i<prep.count;++i) {
            for(unsigned j=0;j<prep.count;++j)if(!PreparedCacheIdentity(p,j)){prep.uncertain=true;return false;}
            if(prep.held[i] && !(prep.scene[i].object[0x18]&4)) {
                if(prep.deleteEntered[i]){prep.uncertain=true;return false;}
                prep.deleteEntered[i]=true;Fn<VoidFn>(0x118A1B0)(prep.scene[i].object);
                if(!(prep.scene[i].object[0x18]&4)){prep.uncertain=true;return false;}
            }
            if(!ClearPreparedCache(p,i)){prep.uncertain=true;return false;}
        }
        if(p.gunHeld && At<int>(p.gun.object,0x38)!=-1){prep.uncertain=true;return false;}
        prep.complete=true;return RestorePreparation(p);
    } __except(EXCEPTION_EXECUTE_HANDLER){prep.uncertain=true;return false;}
}
bool PrepareSources(Stream& s) noexcept {
    if(s.preparing)return false;
    // Validate the entire physical roster before stopping the first battery.
    for(auto& p:s.guns)if(p.batteryHeld && !PreflightPreparation(p)){s.faulted=true;return false;}
    s.preparing=true;
    __try {for(auto& p:s.guns)if(p.batteryHeld && !PrepareOne(p)){s.faulted=true;return false;}return true;}
    __finally {s.preparing=false;}
}
bool PreparedBeam(const void* bullet,Stream** owner=nullptr) noexcept {
    for(auto& s:streams)if(s.rootHeld)for(auto& p:s.guns)for(unsigned i=0;i<p.prep.count;++i)
        if(p.prep.held[i] && p.prep.cache[i].object==bullet){if(owner)*owner=&s;return true;}
    return false;
}
bool HoldBeam(Part& p,unsigned index,const Ref& scene,void* bullet,std::uint64_t serial,const float matrix[16]) noexcept {
    if(index>=kMuzzles || !Readable(bullet,0xC38))return false;
    auto& b=p.beams[index];
    if(At<unsigned>(bullet,0xC30)!=1)return true;
    if(!BeamMethod(bullet) || !Pin(scene))return false;
    if(!DropBeam(b)){Unpin(scene);return false;}
    b.scene=scene;b.bullet=bullet;b.shot=serial;b.held=b.active=true;std::memcpy(b.matrix,matrix,64);return true;
}
bool CleanReplay(Stream& s) noexcept {
    auto& r=s.replay;
    __try {
        if(r.uncertain || r.freeEntered || !RestoreNativeProjectileJournal(r.native))return false;
        if(r.node && !r.nodeLinked){r.freeEntered=true;Fn<FreeFn>(0x12D85EC)(r.node,32);r.node=nullptr;r.freeEntered=false;}
        if(r.resultHeld){if(!CanUnpin(r.result))return false;r.resultHeld=false;Unpin(r.result);}
        r={};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){r.uncertain=true;return false;}
}
bool ReleaseBeams(Stream& s) noexcept {
    for(auto& part:s.guns)for(auto& beam:part.beams)if((beam.active && BeamAlive(beam)) || !DropBeam(beam))return false;return true;
}
bool RetainResult(Stream& s,void* raw,void** bullet) noexcept {
    if(!bullet || !Readable(raw,0x38) || (At<unsigned char>(raw,0x18)&4))return false;
    const Ref scene{At<unsigned char*>(raw,0x28),At<unsigned char*>(raw,0x30)};
    if(!scene.object || !Readable(scene.object,0x38) || At<const void*>(scene.object,0x30)!=scene.control || !Pin(scene))return false;
    s.replay.result=scene;s.replay.resultHeld=true;
    *bullet=Fn<CastFn>(0x12DA7AA)(scene.object,0,image+0x2006450,image+0x2022E80,0);
    return *bullet && Readable(*bullet,0xC38);
}
bool PublishPending(Stream& s,const NativeLease& l) noexcept {
    if(!s.pendingAction)return true;
    if(!WalkingActionEntered(l,s.pending.key))return true; // another already-running native emitter, before the new row enters
    Event pending=s.pending;if(!Push(s,pending,true))return false;
    s.pendingAction=false;s.pending={};std::memcpy(s.point,pending.point,sizeof(s.point));s.action=pending.key;s.pointOwned=true;return true;
}
bool Post(Stream& s,Part& p,unsigned index,void* bullet,const Event& e) noexcept {
    auto& r=s.replay;unsigned char* muzzle=nullptr;
    if(!r.resultHeld || !Muzzle(p,index,&muzzle) || !ListReady(p) || !Readable(r.node,32,true))return false;
    if(At<unsigned>(bullet,0xC30)==1) {
        if(!Readable(r.result.control,16,true))return false;
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(r.result.control+12));
        void* weak[2]={bullet,r.result.control};Fn<MoveWeakFn>(0x58EB40)(muzzle+0xB0,weak);
        if(!HoldBeam(p,index,r.result,bullet,e.serial,e.muzzleMatrix))return false;
    }
    const auto head=At<unsigned char*>(p.battery.object,0x290);const auto last=At<unsigned char*>(head,8);
    if(!Readable(r.result.control,16,true))return false;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(r.result.control+12));
    Put<const void*>(r.node,0,head);Put<const void*>(r.node,8,last);Put<const void*>(r.node,16,bullet);Put<const void*>(r.node,24,r.result.control);
    Put<std::uint64_t>(p.battery.object,0x298,At<std::uint64_t>(p.battery.object,0x298)+1);
    Put<const void*>(head,8,r.node);Put<const void*>(last,0,r.node);r.nodeLinked=true;r.node=nullptr;
    return true;
}
bool DeleteBeam(Beam& b) noexcept {
    __try {
        if(b.uncertain || !Readable(b.scene.object,0x1C))return false;
        if(b.scene.object[0x18]&4)return true;
        if(b.stopEntered){b.uncertain=true;return false;}
        b.stopEntered=true;Fn<VoidFn>(0x118A1B0)(b.scene.object);
        return (b.scene.object[0x18]&4)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER){b.uncertain=true;return false;}
}
bool ApplyRefresh(Stream& s,const Event& e) noexcept {
    auto& p=s.guns[e.key];auto& b=p.beams[e.muzzle];unsigned char* muzzle=nullptr;
    if(!b.held || b.shot!=e.command || !Muzzle(p,e.muzzle,&muzzle))return false;
    if(e.phase) {
        if(!DeleteBeam(b))return false;
        b.active=false;return DropBeam(b);
    }
    if(!BeamAlive(b) || !BeamMethod(b.bullet))return false;
    std::memcpy(muzzle+0x70,e.muzzleMatrix,64);std::memcpy(b.matrix,e.muzzleMatrix,64);
    InvokeOriginalNativeBeamRefresh(b.bullet);
    return (At<unsigned>(b.bullet,0xC34)&0x2000u)!=0;
}
bool Emit(Stream& s,const Event& e) noexcept {
    if(e.kind==EventKind::refresh)return ApplyRefresh(s,e);
    auto& p=s.guns[e.key];unsigned char* muzzle=nullptr;
    if(!Muzzle(p,e.muzzle,&muzzle) || !ListReady(p) || !CleanReplay(s))return false;
    auto& r=s.replay;r.serial=e.serial;r.node=Fn<AllocFn>(0x12D85B0)(32);if(!Readable(r.node,32,true))return false;
    NativeProjectileSample sample{};Sample(e,&sample);
    const auto l=Lease(s);
    void* raw=ReplayNativeProjectileSampleInPlace(r.native,l,p.battery.object+0x2F0,p.battery.object+0x300,reinterpret_cast<float*>(muzzle+0x70),sample);
    if(!raw) {if(r.native.created && !r.resultHeld){void* partial=nullptr;RetainResult(s,r.native.created,&partial);}return false;}
    void* bullet=nullptr;
    if(!RetainResult(s,raw,&bullet) || !Post(s,p,e.muzzle,bullet,e)){r.uncertain=true;return false;}
    return CleanReplay(s);
}
bool OwnFactory(const void* factory,const void*) noexcept {
    if(!installed || admittedImage!=image)return false;
    __try {
        if(FactoryStream(factory,nullptr))return true;
        NativeLease l{};bool online=false;return RootForFactory(factory,&l) && EnemyControlNetworkContext(l.object,&online) && online;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void FactoryFault(const void* factory,const void*) noexcept {
    if(auto* s=FactoryStream(factory,nullptr))s->faulted=true;
}
void* UnitFactory(void* manager,const float* matrix,void* factory,void* descriptor) {
    Stream* s=nullptr;
    __try {
        unsigned key=0;s=FactoryStream(factory,&key);
        if(!s)return nullptr; // retained online context without a binding is never offline permission
        const auto l=Lease(*s);bool authority=false,online=false;
        if(!EnemyControlNetworkContext(l.object,&online) || !online || !NativeActionRole(l.object,&authority) || authority!=s->authority){s->faulted=true;return nullptr;}
        unsigned index=0;if(!Current(s->root) || !KeepGuns(*s,l) || !FindMuzzle(s->guns[key],descriptor,&index)){s->faulted=true;return nullptr;}
        auto& p=s->guns[key];auto& beam=p.beams[index];unsigned char* muzzle=nullptr;if(!Muzzle(p,index,&muzzle)){s->faulted=true;return nullptr;}
        if(s->preparing)return nullptr;
        if(!s->authority || s->quiescing || NativeActionQuiescing(l.object)) {
            if(beam.held && beam.active && Matrix(beam.matrix))std::memcpy(muzzle+0x70,beam.matrix,64);
            return nullptr;
        }
        if(s->faulted || s->count+(s->pendingAction ? 2u : 1u)>kQueued || !ListReady(p) || !Readable(descriptor,0x1E0,true) || !CleanReplay(*s)){s->faulted=true;return nullptr;}
        // The Core cached exactly the selected LockPoint, including an empty
        // selection. Consuming that owned weak replaces the old actor/index0.
        void* lock[2]{};if(!EnemyControlWeaponLock(l.object,lock)){s->faulted=true;return nullptr;}
        Fn<MoveWeakFn>(0x58EB40)(static_cast<unsigned char*>(descriptor)+0x60,lock);
        NativeProjectileSample sample{};
        if(!CaptureNativeProjectileSample(l,p.battery.object+0x300,matrix,descriptor,&sample)){s->faulted=true;return nullptr;}
        void* raw=InvokeOriginalNativeUnitFactory(manager,matrix,factory,descriptor);
        if(!raw)return nullptr;
        Event e=Shot(key,index,p.digest,sample);e.serial=s->next;
        void* bullet=nullptr;
        if(!RetainResult(*s,raw,&bullet) || !PublishPending(*s,l)){s->replay.uncertain=true;s->faulted=true;return raw;}
        e.serial=s->next;
        if(!Push(*s,e,true) || !HoldBeam(p,index,s->replay.result,bullet,e.serial,sample.muzzleMatrix) || !CleanReplay(*s)){s->faulted=true;return raw;}
        return raw; // its original native caller owns the original post-create path
    } __except(EXCEPTION_EXECUTE_HANDLER){if(s){s->faulted=true;s->replay.uncertain=true;}return nullptr;}
}
bool FindBeam(const void* bullet,Stream** stream,Part** part,unsigned* key,unsigned* index) noexcept {
    for(auto& s:streams)if(s.rootHeld)for(unsigned k=0;k<kGuns;++k)if(s.guns[k].batteryHeld)for(unsigned m=0;m<kMuzzles;++m) {
        auto& b=s.guns[k].beams[m];if(b.held && b.bullet==bullet){if(stream)*stream=&s;if(part)*part=&s.guns[k];if(key)*key=k;if(index)*index=m;return true;}
    }
    return false;
}
bool OwnBeam(const void* bullet) noexcept {
    __try {return installed && admittedImage==image && (PreparedBeam(bullet) || FindBeam(bullet,nullptr,nullptr,nullptr,nullptr));}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void BeamFault(const void* bullet) noexcept {Stream* s=nullptr;if(PreparedBeam(bullet,&s) || FindBeam(bullet,&s,nullptr,nullptr,nullptr))s->faulted=true;}
void Refresh(void* bullet) {
    Stream* s=nullptr;
    __try {
        if(PreparedBeam(bullet,&s))return;
        Part* p=nullptr;unsigned key=0,index=0;if(!FindBeam(bullet,&s,&p,&key,&index))return;
        auto& b=p->beams[index];unsigned char* muzzle=nullptr;if(!Muzzle(*p,index,&muzzle)){s->faulted=true;return;}
        if(!s->authority) {if(b.active)std::memcpy(muzzle+0x70,b.matrix,64);return;}
        if(s->faulted || s->quiescing)return;
        if(!BeamMethod(bullet)){s->faulted=true;return;}
        // A cached native weak may survive its beam's expiry until the next
        // manager sweep. The original refresh is a legitimate no-op then.
        if(!BeamAlive(b)){InvokeOriginalNativeBeamRefresh(bullet);return;}
        if(s->count==kQueued){s->faulted=true;return;}
        InvokeOriginalNativeBeamRefresh(bullet);
        if(!(At<unsigned>(bullet,0xC34)&0x2000u)){s->faulted=true;return;}
        Event e{};e.kind=EventKind::refresh;e.key=key;e.muzzle=index;e.digest=p->digest;e.command=b.shot;std::memcpy(e.muzzleMatrix,muzzle+0x70,64);e.serial=s->next;
        if(!Push(*s,e,true)){s->faulted=true;return;}std::memcpy(b.matrix,e.muzzleMatrix,64);
    } __except(EXCEPTION_EXECUTE_HANDLER){if(s)s->faulted=true;}
}
void Step(void* bullet,void* dt) {
    Stream* s=nullptr;
    __try {
        if(PreparedBeam(bullet,&s))return;
        Part* p=nullptr;unsigned index=0;if(!FindBeam(bullet,&s,&p,nullptr,&index))return;
        auto& b=p->beams[index];unsigned char* muzzle=nullptr;
        if(!Muzzle(*p,index,&muzzle)){s->faulted=true;return;}
        // The authority's original expired/ending step still owns native FX/SE
        // cleanup. Only a guest's accepted active latch requires a live beam.
        if(!s->authority && !BeamAlive(b)){s->faulted=true;return;}
        if(!s->authority && b.active && !s->faulted) {
            std::memcpy(muzzle+0x70,b.matrix,64);InvokeOriginalNativeBeamRefresh(bullet);
        }
        InvokeOriginalNativeBeamStep(bullet,dt);
    } __except(EXCEPTION_EXECUTE_HANDLER){if(s)s->faulted=true;}
}
bool TickBeams(Stream& s,bool stop) noexcept {
    unsigned needed=0;
    for(auto& p:s.guns)for(auto& b:p.beams)if(b.held && b.active && (stop || !BeamAlive(b)))++needed;
    if(s.authority && needed>kQueued-s.count)return false;
    for(unsigned key=0;key<kGuns;++key)for(unsigned index=0;index<kMuzzles;++index) {
        auto& p=s.guns[key];auto& b=p.beams[index];if(!b.held || !b.active)continue;
        unsigned char* muzzle=nullptr;if(!Muzzle(p,index,&muzzle) || b.uncertain)return false;
        if(stop || !BeamAlive(b)) {
            if(!s.authority && !stop){s.faulted=true;return false;}
            if(s.authority) {
                Event e{};e.kind=EventKind::refresh;e.phase=1;e.key=key;e.muzzle=index;e.digest=p.digest;e.command=b.shot;e.serial=s.next;std::memcpy(e.muzzleMatrix,b.matrix,64);
                if(!DeleteBeam(b) || !Push(s,e,true))return false;
            } else if(stop && !DeleteBeam(b))return false;
            b.active=false;if(!DropBeam(b))return false;
        } else if(!s.authority) {
            // An accepted active cycle owns the borrowed cache matrix and its
            // native keepalive. Local MAB cadence cannot expire or redirect it.
            std::memcpy(muzzle+0x70,b.matrix,64);InvokeOriginalNativeBeamRefresh(b.bullet);
            if(!(At<unsigned>(b.bullet,0xC34)&0x2000u))return false;
        }
    }
    return true;
}
const NativeUnitFactoryProvider kUnitProvider{kTag,&OwnFactory,&UnitFactory,&FactoryFault};
const NativeBeamRefreshProvider kRefreshProvider{kTag,&OwnBeam,&Refresh,&BeamFault};
const NativeBeamStepProvider kStepProvider{kTag,&OwnBeam,&Step,&BeamFault};

} // namespace walking_net

bool WalkingAcceptedCanQueue(const NativeLease& l) noexcept {
    using namespace walking_net;
    __try {bool authority=false;if(!NativeActionRole(l.object,&authority) || !authority || NativeActionQuiescing(l.object))return false;
        auto* s=Bind(l,true);return s && !s->faulted && !s->quiescing && s->count<kQueued && !s->preparing && WalkingPartsEmpty(l) && KeepGuns(*s,l) && PrepareSources(*s);}
    __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
bool WalkingAcceptedEntryPending(const NativeLease& l) noexcept {
    __try {
        const auto* s=walking_net::Find(l);if(!s)return false;
        if(s->pendingAction)return true;
        for(unsigned i=0;i<s->count;++i)if(s->queue[(s->head+i)%walking_net::kQueued].kind==walking_net::EventKind::action)return true;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return true;}
}
bool BeginWalkingAcceptedAction(const NativeLease& l,unsigned key,std::uint64_t command,std::uint64_t seed3,std::uint64_t seed4) noexcept {
    using namespace walking_net;
    __try {
        if(!command || !WalkingAcceptedCanQueue(l))return false;auto* s=Find(l);if(s->pendingAction)return false;
        Event e{};e.kind=EventKind::action;e.key=key;e.command=command;e.seed3=seed3;e.seed4=seed4;e.serial=s->next;e.phase=l.object[0x1940];
        if(!WalkingActionDefinition(l,key,&e.digest) || !EnemyControlWeaponAim(l.object,e.point) || !Valid(e))return false;
        s->pending=e;s->pendingAction=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
bool CancelWalkingAcceptedAction(const NativeLease& l) noexcept {
    __try {auto* s=walking_net::Find(l);if(s){s->faulted=true;s->pendingAction=false;s->pending={};}return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool QueueWalkingAcceptedAction(const NativeLease& l,unsigned key,std::uint64_t command,std::uint64_t seed3,std::uint64_t seed4) noexcept {
    using namespace walking_net;
    __try {
        auto* s=Find(l);if(!s || !s->authority || !WalkingActionCommitted(l,key))return false;Event e{};
        if(s->pendingAction) {
            e=s->pending;
            if(e.key!=key || e.command!=command || e.seed3!=seed3 || e.seed4!=seed4)return false;
        } else {
            // Idempotent completion after a synchronous native emitter already
            // published this accepted row before its first real shot.
            for(unsigned i=0;i<s->count;++i){const auto& prior=s->queue[(s->head+i)%kQueued];if(prior.kind==EventKind::action && prior.key==key && prior.command==command && prior.seed3==seed3 && prior.seed4==seed4)return true;}
            return false;
        }
        if(!Push(*s,e,true))return false;s->pendingAction=false;s->pending={};
        std::memcpy(s->point,e.point,sizeof(e.point));s->action=key;s->pointOwned=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
bool QueueWalkingAcceptedGun(const NativeLease& l,unsigned key,unsigned phase,std::uint64_t command,std::uint64_t seed,const float point[3]) noexcept {
    using namespace walking_net;
    __try {
        if(!point || phase>1 || !WalkingAcceptedCanQueue(l) || key>=kGuns)return false;auto* s=Find(l);const auto& p=s->guns[key];
        if(!PartReady(p) || (!phase && !WalkingGunCommitted(l,key)))return false;
        Event e{};e.kind=EventKind::gun;e.key=key;e.phase=static_cast<unsigned char>(phase);e.command=command;e.seed3=seed;e.digest=p.digest;
        std::memcpy(e.point,point,sizeof(e.point));e.delay=At<int>(p.gun.object,0x38);std::memcpy(e.aim,p.gun.object+0x50,sizeof(e.aim));e.gunRng=At<std::uint64_t>(p.gun.object,0x80);
        return Push(*s,e);
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
bool WalkingAcceptedAimPoint(const void* root,float out[3]) noexcept {
    using namespace walking_net;if(!out)return false;
    __try {
        bool authority=false;if(!NativeActionRole(root,&authority))return false;
        for(auto& s:streams)if(s.root.object==root && s.authority==authority && (s.pointOwned || s.pendingAction) && !s.quiescing && !s.faulted && Current(s.root)) {
            const auto l=Lease(s);if(!Family(l))return false;
            if(s.pendingAction && Finite(s.pending.point,3)){std::memcpy(out,s.pending.point,sizeof(s.pending.point));return true;}
            if(!s.pointOwned || !WalkingActionCommitted(l,s.action) || !Finite(s.point,3))return false;
            std::memcpy(out,s.point,sizeof(s.point));return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return false;
}
bool OwnsWalkingAcceptedAction(const void* bytes,std::size_t size) noexcept {
    __try {return bytes && size>=4 && Readable(bytes,4) && At<std::uint32_t>(bytes,0)==walking_net::kTag;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool CaptureWalkingAcceptedAction(const NativeLease& l,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    using namespace walking_net;if(used)*used=0;if(!used || !bytes)return false;
    __try {
        auto* s=Find(l);if(!s)return Family(l);if(!s->authority || (s->faulted && !s->quiescing))return false;
        if(s->pendingAction)return false;if(!s->count)return true;Event batch[kBatch]{};unsigned count=0;std::size_t needed=kHeader;const auto limit=capacity<kWireMax ? capacity : kWireMax;
        while(count<kBatch && count<s->count){const auto& e=s->queue[(s->head+count)%kQueued];const auto n=Bytes(e);if(needed+n>limit)break;batch[count++]=e;needed+=n;}
        if(!count || !Encode(batch,count,bytes,limit,used))return false;s->head=(s->head+count)%kQueued;s->count-=count;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
enemy_control::ActionResult ApplyWalkingAcceptedAction(const NativeLease& l,const void* bytes,std::size_t size) noexcept {
    using namespace walking_net;
    __try {
        bool authority=false;if(!bytes || !Readable(bytes,size) || !NativeActionRole(l.object,&authority) || authority)return Result::failed;
        auto* s=Bind(l,false);if(!s || s->faulted || !KeepGuns(*s,l) || !PrepareSources(*s))return Result::failed;
        Event events[kBatch]{};unsigned count=0;if(!Decode(bytes,size,events,&count))return Result::failed;
        std::uint64_t expected=s->applied;
        // Every definition, identity and write destination is validated before
        // the first native mutation. A missing destination yields pending.
        for(unsigned i=0;i<count;++i) {
            const auto& e=events[i];if(e.serial<=s->applied){const auto& receipt=s->receipts[e.serial%kQueued];if(receipt.serial!=e.serial || receipt.hash!=Hash(e))return Result::failed;continue;}
            if(e.serial!=expected+1)return Result::failed;expected=e.serial;
            if(!Preflight(*s,l,e))return Result::pending;
        }
        for(unsigned i=0;i<count;++i) {
            const auto& e=events[i];if(e.serial<=s->applied)continue;bool accepted=false;
            if(e.kind==EventKind::action) {
                if(!WalkingActionReady(l,e.key))return Result::pending;
                s->pending=e;s->pendingAction=true;accepted=CommitWalkingAction(l,e.key,e.command,e.seed3,e.seed4,e.phase) && WalkingActionCommitted(l,e.key);
                if(accepted){std::memcpy(s->point,e.point,sizeof(e.point));s->action=e.key;s->pointOwned=true;s->pendingAction=false;s->pending={};}
            } else if(e.kind==EventKind::gun) {
                accepted=ApplyWalkingGunAcceptedControl(l,e.key,e.phase,e.serial,e.command,e.seed3,e.point);
                if(accepted && !e.phase){auto* gun=s->guns[e.key].gun.object;Put<int>(gun,0x38,e.delay);std::memcpy(gun+0x50,e.aim,sizeof(e.aim));Put<std::uint64_t>(gun,0x80,e.gunRng);}
            } else accepted=Emit(*s,e);
            if(!accepted){s->faulted=true;return Result::failed;}
            s->receipts[e.serial%kQueued]={e.serial,Hash(e)};s->applied=e.serial;
        }
        return Result::applied;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return Result::failed;}
}
bool QuiesceWalkingAcceptedActions(const NativeLease& l) noexcept {
    __try {auto* s=walking_net::Find(l);if(s){
        s->quiescing=true;
        // A reserved native command is still in its synchronous producer call.
        // It must publish its real outcome before normal close can forget it.
        // Drain can arrive before previously accepted refresh/Stop packets.
        // A guest retains the accepted cycle until the authority's Stop arrives.
        if(s->pendingAction || !walking_net::TickBeams(*s,s->authority))return false;
    }return RestoreWalkingGunControl(l);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool WalkingAcceptedActionsEmpty(const NativeLease& l) noexcept {
    __try {
        const auto* s=walking_net::Find(l);if(!s)return true;
        if(s->preparing || s->count || s->pendingAction || !NativeProjectileJournalEmpty(s->replay.native) || s->replay.node || s->replay.resultHeld || s->replay.uncertain || s->replay.freeEntered)return false;
        for(const auto& part:s->guns) {
            if(part.prep.uncertain || (part.prep.entered && !part.prep.complete))return false;
            for(unsigned i=0;i<part.prep.count;++i)if(part.prep.held[i])return false;
            for(const auto& beam:part.beams)if(beam.held || beam.uncertain || beam.stopEntered)return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool RestoreWalkingAcceptedActions(const NativeLease& l) noexcept {
    __try {auto* s=walking_net::Find(l);if(!s)return true;if(s->count || s->pendingAction)return false;return walking_net::Release(*s);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool AbortWalkingAcceptedActions(const NativeLease& l) noexcept {
    __try {
        auto* s=walking_net::Find(l);
        // This path is called only by the trusted retired epoch/link-loss gate.
        // It can terminate a guest cycle even when normal drain is faulted.
        if(s){s->quiescing=true;if(s->preparing || s->pendingAction || !walking_net::TickBeams(*s,true))return false;}
        if(!RestoreWalkingGunControl(l))return false;if(!s)return true;
        s->head=s->count=0;return walking_net::Release(*s);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace crew::enemy_native

namespace crew::enemy_native {
bool InstallWalkingAcceptedActions() noexcept {
    using namespace walking_net;
    __try {
        if(installed && image==admittedImage)return NativeUnitFactoryProviderReady(kUnitProvider) && NativeUnitFactoryCallsiteReady(0x6D331D) && NativeBeamRefreshProviderReady(kRefreshProvider) && NativeBeamStepProviderReady(kStepProvider);
        if(!image || (admittedImage && admittedImage!=image))return false;
        for(const auto& p:kNativePrints)if(!Readable(image+p.at,16) || std::memcmp(image+p.at,p.bytes,16))return false;
        const NativeActionExtension extension{kTag,&Needs,&Before,&After,&CaptureWalkingAcceptedAction,&OwnsWalkingAcceptedAction,&ApplyWalkingAcceptedAction,
            &QuiesceWalkingAcceptedActions,&WalkingAcceptedActionsEmpty,&AbortWalkingAcceptedActions,&RestoreWalkingAcceptedActions};
        if(!RegisterNativeActionExtension(extension) || !RegisterNativeUnitFactoryProvider(kUnitProvider) || !RegisterNativeUnitFactoryCallsite(0x6D331D) ||
           !RegisterNativeBeamRefreshProvider(kRefreshProvider) || !RegisterNativeBeamStepProvider(kStepProvider) || !InstallNativeUnitFactoryDispatcher() ||
           !InstallNativeBeamRefreshDispatcher() || !InstallNativeBeamStepDispatcher())return false;
        admittedImage=image;installed=true;
        return NativeUnitFactoryProviderReady(kUnitProvider) && NativeUnitFactoryCallsiteReady(0x6D331D) && NativeBeamRefreshProviderReady(kRefreshProvider) && NativeBeamStepProviderReady(kStepProvider);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
