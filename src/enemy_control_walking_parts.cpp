// EDF.dll 678CCB46: defer complete 33FCB0 groups, never leave a null bullet link.
#include "enemy_control_walking_parts.h"
#include "enemy_control_action_native.h"
#include "enemy_control.h"
#include "enemy_control_gun_factory.h"
#include "enemy_control_projectile_native.h"
#include "enemy_control_walking_net.h"
#include "enemy_control_pvp_berserker.h"
#include "command_identity.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_WALKING_PARTS_TEST
void* WalkingPartsTestFunction(unsigned) noexcept;
bool WalkingPartsTestDefinition(const NativeLease&,unsigned,std::uint64_t*) noexcept;
#endif
namespace walking_parts {
using Result=enemy_control::ActionResult;
constexpr std::uint32_t kTag=0x31505745u; // EWP1
constexpr unsigned kRoots=8,kParts=256,kQueued=1024,kReceipts=2048,kEntry=0x33FCB0;
constexpr unsigned kCtorWords[]={0x48,0x4C,0x50,0xF0,0xF4,0xF8,0xFC,0x100,0x104,0x108,0x10C};
constexpr unsigned kCtorBytes[]={0x28,0x110};
struct Ref {unsigned char* object=nullptr;unsigned char* control=nullptr;};
struct Handle {const unsigned char* tree=nullptr;unsigned index=UINT32_MAX,pad=0;};
struct Ctor {std::uint32_t words[11]{};unsigned char bytes[2]{};float values[40]{};};
struct Meta {
    std::uint64_t transaction=0,definition=0,pre3=0,pre4=0,post3=0,post4=0;
    unsigned group=0,count=0;unsigned char first=0,last=0;float point[3]{};
};
struct Record {
    Ctor ctor{};NativeProjectileSample sample{};NativeProjectileJournal journal{};
    alignas(16) float muzzle[16]{};Ref child{},bullet{};
    bool childHeld=false,bulletHeld=false,ctorCaptured=false,sampleReady=false,shotCaptured=false,fallback=false,complete=false,cleanupEntered=false,cleanupUncertain=false;
};
struct Pending {Meta meta{};Record record{};std::uint64_t serial=0;unsigned ordinal=0;unsigned char phase=0;};
struct Receipt {std::uint64_t serial=0,hash=0;};
struct Stream {
    Ref root{};const NativeProfile* profile=nullptr;
    bool held=false,retired=false,authority=false,faulted=false,quiescing=false,executing=false,uncertain=false;
    std::uint64_t next=1,applied=0;unsigned head=0,count=0;
    Pending queue[kQueued]{};Receipt receipts[kReceipts]{};
    Meta stage{};Record parts[kParts]{};unsigned staged=0;bool finalStaged=false;std::uint64_t finalSerial=0,finalHash=0;
};
Stream streams[kRoots]{};unsigned char* admittedImage=nullptr;bool installed=false;
using ShootFn=void(__fastcall*)(void*,int);using VoidFn=void(__fastcall*)(void*);
ShootFn originalShoot=nullptr;unsigned char entryPatch[32]{};void* trampoline=nullptr;
struct Scope {Stream* stream=nullptr;Meta meta{};unsigned queueBase=0,cursor=0,current=kParts;bool replay=false,failed=false;Scope* previous=nullptr;};
thread_local Scope* scope=nullptr;
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_WALKING_PARTS_TEST
    return reinterpret_cast<T>(WalkingPartsTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool Family(const NativeLease& l) noexcept {
    if(!l.profile || l.profile->kind!=Kind::walking)return false;
    std::size_t count=0;const auto* profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(l.profile==profiles+i) {
        const auto v=l.profile->vtableRva;return v==0x17ADA78 || v==0x17AE088 || v==0x17AE968 || v==0x17AED50 || v==0x17AF200 || v==0x17AF9F8;
    }
    return false;
}
bool Current(Ref r,bool alive=true) noexcept {
    return r.object && Readable(r.object,0x1941,true) && Readable(r.control,16,true) && At<LONG>(r.control,8)>0 &&
        At<const void*>(r.object,0x30)==r.control && (!alive || (!(r.object[0x18]&4) && !r.object[0x2E8]));
}
bool Gob(Ref r) noexcept {
    return r.object && Readable(r.object,0x38,true) && Readable(r.control,16,true) && At<LONG>(r.control,8)>0 && At<const void*>(r.object,0x30)==r.control;
}
bool CanUnpin(Ref r) noexcept {
    if(!Readable(r.control,16,true) || At<LONG>(r.control,8)<=0)return false;
    const auto* vt=At<void* const*>(r.control,0);return Readable(vt,16) && vt[0] && vt[1];
}
bool Pin(Ref r) noexcept {
    if(!CanUnpin(r))return false;auto* uses=reinterpret_cast<volatile LONG*>(r.control+8);LONG value=*uses;
    while(value>0 && value<(std::numeric_limits<LONG>::max)()) {const LONG was=InterlockedCompareExchange(uses,value+1,value);if(was==value)return true;value=was;}return false;
}
void Unpin(Ref r) {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+8)))return;
    const auto* vt=At<void* const*>(r.control,0);reinterpret_cast<VoidFn>(vt[0])(r.control);
    if(!InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12)))reinterpret_cast<VoidFn>(vt[1])(r.control);
}
NativeLease Lease(const Stream& s) noexcept {NativeLease l{};l.object=s.root.object;l.control=s.root.control;l.profile=s.profile;return l;}
Stream* Find(const NativeLease& l) noexcept {
    for(auto& s:streams)if((s.held || s.retired || s.uncertain) && s.root.object==l.object && s.root.control==l.control && s.profile==l.profile)return &s;return nullptr;
}
Stream* FindRoot(const void* root) noexcept {for(auto& s:streams)if(s.held && s.root.object==root)return &s;return nullptr;}
Stream* Bind(const NativeLease& l,bool authority) noexcept {
    if(!installed || image!=admittedImage || !Family(l) || !Current({l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))}))return nullptr;
    if(auto* s=Find(l))return !s->retired && s->authority==authority ? s : nullptr;
    for(auto& s:streams)if(!s.held && !s.retired && !s.uncertain) {
        if(!Pin({l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))}))return nullptr;
        s.root={l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))};s.profile=l.profile;s.held=true;s.authority=authority;return &s;
    }
    return nullptr;
}
bool Finite(const float* v,unsigned count) noexcept {for(unsigned i=0;i<count;++i)if(!std::isfinite(v[i]))return false;return true;}
bool Matrix(const float* m) noexcept {
    if(!Finite(m,16) || std::fabs(m[15]-1.0f)>0.0001f)return false;
    double n[3]{};for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)n[i]+=static_cast<double>(m[i*4+j])*m[i*4+j];
    const double det=static_cast<double>(m[0])*(m[5]*m[10]-m[6]*m[9])-static_cast<double>(m[1])*(m[4]*m[10]-m[6]*m[8])+static_cast<double>(m[2])*(m[4]*m[9]-m[5]*m[8]);
    return n[0]>0 && n[1]>0 && n[2]>0 && std::isfinite(det) && std::fabs(det)>std::sqrt(n[0]*n[1]*n[2])*1.0e-8;
}
bool Group(const NativeLease& l,unsigned group,const unsigned char** entries,unsigned* count) noexcept {
    if(!Current({l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))}))return false;
    const auto total=At<std::size_t>(l.object,0x1800),capacity=At<std::size_t>(l.object,0x17F8);const auto* rows=At<const unsigned char*>(l.object,0x17F0);
    if(total>4096 || total>capacity || group>=total || !Readable(rows,total*0x100))return false;
    const auto* row=rows+static_cast<std::size_t>(group)*0x100;
    const auto n=At<std::size_t>(row,0x18),cap=At<std::size_t>(row,0x10);const auto* parts=At<const unsigned char*>(row,8);
    if(!n || n>kParts || n>cap || !Readable(parts,n*0x28))return false;
    for(unsigned i=0;i<n;++i)if(!Readable(At<const void*>(parts+i*0x28,0),0xF0,true))return false;
    *entries=parts;*count=static_cast<unsigned>(n);return true;
}
// Hash only the actual immutable PartsShootList SGO subtree, including VM bytes.
struct Definition {const unsigned char* tree=nullptr;const unsigned char* raw=nullptr;unsigned nodes=0,visited=0,depth=0,path[32]{};std::size_t bytes=0;std::uint64_t hash=14695981039346656037ull;};
bool Mix(Definition& d,const void* p,std::size_t n) noexcept {
    if(n>4*1024*1024-d.bytes || (n && !Readable(p,n)))return false;for(std::size_t i=0;i<n;++i){d.hash^=static_cast<const unsigned char*>(p)[i];d.hash*=1099511628211ull;}d.bytes+=n;return true;
}
bool Number(Definition& d,unsigned n) noexcept {return Mix(d,&n,4);}
const unsigned char* Raw(const Definition& d,std::int64_t offset,std::size_t bytes) noexcept {
    if(offset<0 || offset>16*1024*1024 || bytes>16*1024*1024-static_cast<std::size_t>(offset))return nullptr;
    const auto base=reinterpret_cast<std::uintptr_t>(d.raw);if(base>UINTPTR_MAX-static_cast<std::uintptr_t>(offset))return nullptr;
    const auto* p=reinterpret_cast<const unsigned char*>(base+static_cast<std::uintptr_t>(offset));return Readable(p,bytes) ? p : nullptr;
}
std::int64_t Offset(const Definition& d,const unsigned char* p) noexcept {return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(p)-reinterpret_cast<std::uintptr_t>(d.raw));}
bool Text(Definition& d,std::int64_t at) noexcept {
    unsigned n=0;for(;n<=1024;++n){const auto* p=Raw(d,at+n*2,2);if(!p)return false;if(!At<std::uint16_t>(p,0))break;}
    return n<=1024 && Number(d,n) && Mix(d,Raw(d,at,n*2),n*2);
}
bool Node(Definition& d,unsigned index) noexcept {
    if(index>=d.nodes || ++d.visited>4096 || d.depth==32)return false;for(unsigned i=0;i<d.depth;++i)if(d.path[i]==index)return false;
    const auto mapped=At<std::size_t>(d.tree,0x18);if(mapped>1048576)return false;
    if(index<mapped){const auto* map=At<const unsigned char*>(d.tree,8);if(!Readable(map,index*4+4) || At<unsigned>(map,index*4)!=UINT32_MAX)return false;}
    const auto* node=Raw(d,16+static_cast<std::int64_t>(index)*16,16);if(!node)return false;
    const auto type=At<unsigned>(node,8);if(type>4 || !Number(d,type))return false;if(!type)return Mix(d,node,8);
    const auto at=Offset(d,node)+At<std::int32_t>(node,0);if(type==1)return Text(d,at);
    const auto* block=Raw(d,at,type==3 ? 16u : 8u);if(!block)return false;
    if(type!=3){const auto n=At<unsigned>(block,0);return n<=1024*1024 && Number(d,n) && Mix(d,Raw(d,at+At<std::int32_t>(block,4),n),n);}
    const auto count=At<unsigned>(block,12),names=At<unsigned>(block,4);if(count>1024 || names>count || !Number(d,count))return false;
    const auto* children=Raw(d,at+At<std::int32_t>(block,8),count*4);const auto* keys=Raw(d,at+At<std::int32_t>(block,0),names*8);
    if((count && !children) || (names && !keys))return false;for(unsigned i=0;i<names;++i)if(At<unsigned>(keys+i*8,4)>=count)return false;
    d.path[d.depth++]=index;bool okay=true;
    for(unsigned i=0;i<count && okay;++i){const unsigned char* key=nullptr;for(unsigned n=0;n<names;++n)if(At<unsigned>(keys+n*8,4)==i){if(key){okay=false;break;}key=keys+n*8;}
        if(okay)okay=Number(d,key ? 1u : 0u) && (!key || Text(d,Offset(d,key)+At<std::int32_t>(key,0))) && Node(d,At<unsigned>(children,i*4));}
    --d.depth;return okay;
}
bool DefinitionOf(const NativeLease& l,unsigned group,std::uint64_t* digest) noexcept {
#ifdef ENEMY_CONTROL_WALKING_PARTS_TEST
    return WalkingPartsTestDefinition(l,group,digest);
#else
    Handle root{l.object+0xA8,0,0},settings{},list{};using Lookup=Handle*(__fastcall*)(const Handle*,Handle*,const wchar_t*);
    Fn<Lookup>(0xD7480)(&root,&settings,L"BerserkerSettings");if(!settings.tree)return false;
    Fn<Lookup>(0xD7480)(&settings,&list,L"PartsShootList");if(!Readable(list.tree,0x58))return false;
    Definition d{};d.tree=list.tree;d.raw=At<const unsigned char*>(d.tree,0x50);
    if(!Readable(d.raw,16) || At<unsigned>(d.raw,0)!=0x4F475344 || At<unsigned>(d.raw,4)!=16 || At<unsigned>(d.raw,12)!=16)return false;
    d.nodes=At<unsigned>(d.raw,8);constexpr char domain[]="EDF6.Walking.PartsShoot.1";
    if(!d.nodes || d.nodes>65536 || !Mix(d,domain,sizeof(domain)) || !Number(d,group) || !Node(d,list.index) || !d.hash)return false;*digest=d.hash;return true;
#endif
}
bool CtorValid(const Ctor& c) noexcept {
    if(c.bytes[0]>1 || c.bytes[1]>1 || !Finite(c.values,40) || !Matrix(c.values) || !Matrix(c.values+24))return false;
    for(unsigned i=0;i<11;++i)if(i!=4){float f=0;std::memcpy(&f,c.words+i,4);if(!std::isfinite(f))return false;}return true;
}
bool MetaValid(const Meta& m) noexcept {return m.transaction && m.definition && m.group<4096 && m.count && m.count<=kParts && m.first<16 && m.last<16 && Finite(m.point,3);}
bool SameMeta(const Meta& a,const Meta& b) noexcept {
    return a.transaction==b.transaction && a.definition==b.definition && a.pre3==b.pre3 && a.pre4==b.pre4 && a.post3==b.post3 && a.post4==b.post4 &&
        a.group==b.group && a.count==b.count && a.first==b.first && a.last==b.last && !std::memcmp(a.point,b.point,12);
}
Record& AtRecord(Scope& s,unsigned i) noexcept {return s.replay ? s.stream->parts[i] : s.stream->queue[(s.queueBase+i)%kQueued].record;}
bool CaptureCtor(const void* input,Ctor* out) noexcept {
    if(!Readable(input,0x148) || !Readable(At<const void*>(input,8),64))return false;Ctor c{};
    for(unsigned i=0;i<11;++i)c.words[i]=At<std::uint32_t>(input,kCtorWords[i]);for(unsigned i=0;i<2;++i)c.bytes[i]=At<unsigned char>(input,kCtorBytes[i]);
    std::memcpy(c.values,At<const void*>(input,8),64);std::memcpy(c.values+16,static_cast<const unsigned char*>(input)+0x60,32);std::memcpy(c.values+24,static_cast<const unsigned char*>(input)+0x80,64);
    if(!CtorValid(c))return false;*out=c;return true;
}
bool RestoreRecord(Record& p,bool remove) noexcept {
    if(p.cleanupEntered || p.cleanupUncertain)return false;
    __try {
        if(!RestoreNativeProjectileJournal(p.journal))return false;
        if((p.childHeld && !CanUnpin(p.child)) || (p.bulletHeld && !CanUnpin(p.bullet)))return false;
        if(remove) {
            const Ref refs[]={p.child,p.bullet};const bool held[]={p.childHeld,p.bulletHeld};
            for(unsigned i=0;i<2;++i)if(held[i] && Gob(refs[i]) && !(refs[i].object[0x18]&4)) {
                p.cleanupEntered=true;Fn<VoidFn>(0x118A1B0)(refs[i].object);p.cleanupEntered=false;
            }
        }
        if(p.childHeld){p.childHeld=false;p.cleanupEntered=true;Unpin(p.child);p.cleanupEntered=false;}
        if(p.bulletHeld){p.bulletHeld=false;p.cleanupEntered=true;Unpin(p.bullet);p.cleanupEntered=false;}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){p.cleanupUncertain=true;return false;}
}
bool Linked(const Stream& s,const Record& p) noexcept {
    if(!p.childHeld || !p.bulletHeld || !Gob(p.child) || !Gob(p.bullet) || (p.child.object[0x18]&4) || (p.bullet.object[0x18]&4) ||
       !Readable(p.child.object,0x5E0) || At<const void*>(p.child.object,0x38)!=s.root.object || At<const void*>(p.child.object,0x5C0)!=s.root.object+0x8A0 ||
       At<const void*>(p.child.object,0x5D0)!=p.bullet.object || At<const void*>(p.child.object,0x5D8)!=p.bullet.control)return false;
    const auto count=At<std::size_t>(s.root.object,0x1820),cap=At<std::size_t>(s.root.object,0x1818);const auto* rows=At<const unsigned char*>(s.root.object,0x1810);
    if(count>4096 || count>cap || !Readable(rows,count*16))return false;unsigned found=0;
    for(std::size_t i=0;i<count;++i)if(At<const void*>(rows+i*16,0)==p.child.object && At<const void*>(rows+i*16,8)==p.child.control)++found;return found==1;
}
}
namespace walking_parts {
constexpr unsigned kHeaderBytes=24,kCtorPacketBytes=kHeaderBytes+58+44+2+160;
constexpr unsigned kSampleBytes=128+kProjectileWords*4+kProjectileBytes+2+12+64+4+3;
constexpr unsigned kShotPacketBytes=kHeaderBytes+1+kSampleBytes;
static_assert(kCtorPacketBytes<=512 && kShotPacketBytes<=512,"bounded accepted parts packets");
struct Packet {unsigned char kind=0;std::uint64_t serial=0,transaction=0;unsigned ordinal=0;Meta meta{};Ctor ctor{};NativeProjectileSample sample{};bool fallback=false;};
void PutWire(unsigned char*& p,std::uint64_t v,unsigned n) noexcept {for(unsigned i=0;i<n;++i)*p++=static_cast<unsigned char>(v>>(8*i));}
std::uint64_t GetWire(const unsigned char*& p,unsigned n) noexcept {std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(*p++)<<(8*i);return v;}
void PutFloats(unsigned char*& p,const float* v,unsigned n) noexcept {for(unsigned i=0;i<n;++i){std::uint32_t bits=0;std::memcpy(&bits,v+i,4);PutWire(p,bits,4);}}
void GetFloats(const unsigned char*& p,float* v,unsigned n) noexcept {for(unsigned i=0;i<n;++i){const auto bits=static_cast<std::uint32_t>(GetWire(p,4));std::memcpy(v+i,&bits,4);}}
void PutSample(unsigned char*& p,const NativeProjectileSample& v) noexcept {
    PutFloats(p,v.matrix,16);PutFloats(p,v.muzzleMatrix,16);for(auto n:v.scalar)PutWire(p,n,4);for(auto n:v.bytes)*p++=n;
    PutWire(p,v.scalar178,2);PutWire(p,v.random,4);PutWire(p,v.shot,4);PutWire(p,v.flags,4);
    for(auto n:v.target)*p++=n;for(auto n:v.extra)*p++=n;PutWire(p,v.lock,4);*p++=v.hasMuzzle ? 1 : 0;*p++=v.extraObject ? 1 : 0;*p++=v.resourceMask;
}
bool GetSample(const unsigned char*& p,NativeProjectileSample* v) noexcept {
    GetFloats(p,v->matrix,16);GetFloats(p,v->muzzleMatrix,16);for(auto& n:v->scalar)n=static_cast<unsigned>(GetWire(p,4));for(auto& n:v->bytes)n=*p++;
    v->scalar178=static_cast<std::uint16_t>(GetWire(p,2));v->random=static_cast<unsigned>(GetWire(p,4));v->shot=static_cast<unsigned>(GetWire(p,4));v->flags=static_cast<unsigned>(GetWire(p,4));
    for(auto& n:v->target)n=*p++;for(auto& n:v->extra)n=*p++;v->lock=static_cast<unsigned>(GetWire(p,4));const auto muzzle=*p++,extra=*p++;v->resourceMask=*p++;
    if(muzzle>1 || extra>1)return false;v->hasMuzzle=muzzle!=0;v->extraObject=extra!=0;return ValidNativeProjectileSample(*v);
}
bool Valid(const Packet& e) noexcept {
    if(!e.serial || !e.transaction || e.ordinal>=kParts)return false;
    return e.kind==1 ? MetaValid(e.meta) && e.meta.transaction==e.transaction && e.ordinal<e.meta.count && CtorValid(e.ctor) :
        e.kind==2 && !e.sample.hasMuzzle && ValidNativeProjectileSample(e.sample);
}
bool Encode(const Packet& e,void* out,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;if(!used || !out || !Valid(e))return false;const unsigned bytes=e.kind==1 ? kCtorPacketBytes : kShotPacketBytes;if(capacity<bytes)return false;
    auto* p=static_cast<unsigned char*>(out);PutWire(p,kTag,4);*p++=1;*p++=e.kind;PutWire(p,e.serial,8);PutWire(p,e.transaction,8);PutWire(p,e.ordinal,2);
    if(e.kind==1) {
        const auto& m=e.meta;PutWire(p,m.definition,8);PutWire(p,m.pre3,8);PutWire(p,m.pre4,8);PutWire(p,m.post3,8);PutWire(p,m.post4,8);
        PutWire(p,m.group,2);PutWire(p,m.count,2);*p++=m.first;*p++=m.last;PutFloats(p,m.point,3);
        for(auto n:e.ctor.words)PutWire(p,n,4);for(auto n:e.ctor.bytes)*p++=n;PutFloats(p,e.ctor.values,40);
    } else {*p++=e.fallback ? 1 : 0;PutSample(p,e.sample);}
    *used=static_cast<std::size_t>(p-static_cast<unsigned char*>(out));return *used==bytes;
}
bool Decode(const void* in,std::size_t bytes,Packet* e) noexcept {
    if(!in || !e || !Readable(in,bytes) || (bytes!=kCtorPacketBytes && bytes!=kShotPacketBytes))return false;
    const auto* p=static_cast<const unsigned char*>(in);if(GetWire(p,4)!=kTag || *p++!=1)return false;e->kind=*p++;
    if((e->kind==1 ? kCtorPacketBytes : e->kind==2 ? kShotPacketBytes : 0)!=bytes)return false;
    e->serial=GetWire(p,8);e->transaction=GetWire(p,8);e->ordinal=static_cast<unsigned>(GetWire(p,2));
    if(e->kind==1) {
        auto& m=e->meta;m.transaction=e->transaction;m.definition=GetWire(p,8);m.pre3=GetWire(p,8);m.pre4=GetWire(p,8);m.post3=GetWire(p,8);m.post4=GetWire(p,8);
        m.group=static_cast<unsigned>(GetWire(p,2));m.count=static_cast<unsigned>(GetWire(p,2));m.first=*p++;m.last=*p++;GetFloats(p,m.point,3);
        for(auto& n:e->ctor.words)n=static_cast<unsigned>(GetWire(p,4));for(auto& n:e->ctor.bytes)n=*p++;GetFloats(p,e->ctor.values,40);
    } else {const auto fallback=*p++;if(fallback>1)return false;e->fallback=fallback!=0;if(!GetSample(p,&e->sample))return false;}
    return p==static_cast<const unsigned char*>(in)+bytes && Valid(*e);
}
std::uint64_t Hash(const Packet& e) noexcept {
    unsigned char bytes[512]{};std::size_t used=0;if(!Encode(e,bytes,sizeof(bytes),&used))return 0;
    std::uint64_t h=14695981039346656037ull;for(std::size_t i=0;i<used;++i){h^=bytes[i];h*=1099511628211ull;}return h;
}
bool PinBullet(Record& p,void* bullet) noexcept {
    if(!bullet || !Readable(bullet,0x38))return false;Ref r{static_cast<unsigned char*>(bullet),At<unsigned char*>(bullet,0x30)};
    if(!Gob(r) || !Pin(r))return false;p.bullet=r;p.bulletHeld=true;return true;
}
void* TrackUnit(Scope& active,Record& p,void* manager,const float* matrix,void* factory,void* descriptor) {
    if(active.failed || p.shotCaptured || !Current(active.stream->root) || !Readable(descriptor,0x1E0) ||
       At<const void*>(descriptor,0x38)!=At<const void*>(active.stream->root.object,0x28) || At<const void*>(descriptor,0x40)!=active.stream->root.control) {active.failed=true;return nullptr;}
    const auto l=Lease(*active.stream);void* bullet=nullptr;
    if(!active.replay) {
        if(!CaptureNativeProjectileSample(l,descriptor,matrix,descriptor,&p.sample) || p.sample.hasMuzzle){active.failed=true;return nullptr;}
        bullet=InvokeOriginalNativeUnitFactory(manager,matrix,factory,descriptor);p.fallback=false;
    } else {
        if(p.fallback || !CanReplayNativeProjectileSample(l,factory,descriptor,p.muzzle,p.sample)){active.failed=true;return nullptr;}
        struct Context {void* manager;void* factory;} context{manager,factory};
        const auto create=[](void* value,const float* m,void* d) noexcept ->void* {auto* c=static_cast<Context*>(value);return InvokeOriginalNativeUnitFactory(c->manager,m,c->factory,d);};
        bullet=WithNativeProjectileSampleInPlace(p.journal,l,descriptor,p.muzzle,p.sample,create,&context);
        if(!bullet && p.journal.created){bullet=p.journal.created;active.failed=true;active.stream->faulted=true;}
    }
    if(!bullet){active.failed=true;return nullptr;}
    if(!PinBullet(p,bullet)) {
        p.bullet={static_cast<unsigned char*>(bullet),Readable(bullet,0x38) ? At<unsigned char*>(bullet,0x30) : nullptr};
        p.cleanupUncertain=true;active.failed=true;active.stream->faulted=true;active.stream->uncertain=true;return bullet;
    }
    p.shotCaptured=true;p.sampleReady=true;return bullet;
}
bool OwnsFactory(const void*,const void* descriptor) noexcept {
    if(!scope)return false;
    if(scope->current<scope->meta.count)return true;
    return Readable(descriptor,0x48) && At<const void*>(descriptor,0x38)==At<const void*>(scope->stream->root.object,0x28) &&
        At<const void*>(descriptor,0x40)==scope->stream->root.control;
}
void FactoryFault(const void*,const void*) noexcept {if(scope){scope->failed=true;scope->stream->faulted=true;}}
void* InvokeFactory(void* manager,const float* matrix,void* factory,void* descriptor) {
    if(!scope)return nullptr;if(scope->current>=scope->meta.count){scope->failed=true;return nullptr;}
    return TrackUnit(*scope,AtRecord(*scope,scope->current),manager,matrix,factory,descriptor);
}
bool ValidateGroup(void* context) noexcept {
    __try {
        auto* active=static_cast<Scope*>(context);if(!active || !active->stream)return false;auto* s=active->stream;bool role=false;
        if(!NativeActionRole(s->root.object,&role) || role!=s->authority || active->failed || active->cursor!=active->meta.count || !Current(s->root))return false;
        for(unsigned i=0;i<active->meta.count;++i) {
            const auto& p=AtRecord(*active,i);
            if(!p.ctorCaptured || !p.shotCaptured || !Linked(*s,p) || p.cleanupEntered || p.cleanupUncertain || !NativeProjectileJournalEmpty(p.journal))return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool OriginalGroup(Stream* s,unsigned group,bool replay,Meta* meta,unsigned queueBase) {
    Scope active{};active.stream=s;active.meta=*meta;active.queueBase=queueBase;active.replay=replay;active.previous=scope;
    bool completed=false,okay=false;
    __try {
        scope=&active;s->executing=true;
        okay=crew::EnemyControlBerserkerPvpExecutePartsValidated(s->root.object,static_cast<int>(group),originalShoot,ValidateGroup,&active);
        completed=true;
    } __finally {scope=active.previous;s->executing=false;if(!completed){s->uncertain=true;s->faulted=true;}}
    if(!okay)return false;
    for(unsigned i=0;i<meta->count;++i)AtRecord(active,i).complete=true;
    meta->post3=At<std::uint64_t>(s->root.object,0x3E8);meta->post4=At<std::uint64_t>(s->root.object,0x490);meta->last=s->root.object[0x1940];
    return true;
}
void __fastcall Shoot(void* root,int index) {
    bool authority=false;
    if(!NativeActionRole(root,&authority)) {crew::EnemyControlBerserkerPvpExecuteParts(root,index,originalShoot);return;}
    auto* s=FindRoot(root);if(!s || s->authority!=authority || !Current(s->root)){if(s)s->faulted=true;return;}
    if(s->quiescing || NativeActionQuiescing(root) || s->retired)return;
    // Only the final, fully staged accepted outcome may enter the guest creator.
    if(!authority)return;
    if(s->executing || s->faulted || scope || index<0){s->faulted=true;return;}
    const auto l=Lease(*s);const unsigned char* parts=nullptr;unsigned count=0;Meta meta{};
    if(!Group(l,static_cast<unsigned>(index),&parts,&count) || count>kQueued-s->count || !s->next || s->next>UINT64_MAX-count*2 ||
       !DefinitionOf(l,static_cast<unsigned>(index),&meta.definition) || (!WalkingAcceptedAimPoint(root,meta.point) && !EnemyControlWeaponAim(root,meta.point))) {s->faulted=true;return;}
    meta.transaction=s->next;meta.group=static_cast<unsigned>(index);meta.count=count;meta.first=s->root.object[0x1940];
    meta.pre3=At<std::uint64_t>(root,0x3E8);meta.pre4=At<std::uint64_t>(root,0x490);meta.post3=meta.pre3;meta.post4=meta.pre4;meta.last=meta.first;
    if(!MetaValid(meta)){s->faulted=true;return;}const unsigned base=(s->head+s->count)%kQueued;
    for(unsigned i=0;i<count;++i) {
        auto& p=s->queue[(base+i)%kQueued];
        if(p.record.childHeld || p.record.bulletHeld || !NativeProjectileJournalEmpty(p.record.journal)){s->faulted=true;return;}
        p={};p.ordinal=i;
    }
    bool okay=false;
    __try {okay=OriginalGroup(s,meta.group,false,&meta,base);}
    __finally {
        if(!okay) {
            s->faulted=true;
            // Retain all partial native references until restoration succeeds.
            for(unsigned i=0;i<count;++i){auto& p=s->queue[(base+i)%kQueued];p.meta=meta;p.ordinal=i;}
            s->count+=count;
        }
    }
    if(!okay)return;
    for(unsigned i=0;i<count;++i){auto& p=s->queue[(base+i)%kQueued];p.meta=meta;p.serial=s->next;p.ordinal=i;s->next+=2;}
    s->count+=count;
}
bool EntryHook() noexcept {
#ifdef ENEMY_CONTROL_WALKING_PARTS_TEST
    originalShoot=Fn<ShootFn>(kEntry);return originalShoot!=nullptr;
#else
    constexpr unsigned char original[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xA8,0x98,0xFD,0xFF,0xFF,0x48,0x81,0xEC,0x30,0x03,0x00,0x00};
    if(trampoline)return Readable(image+kEntry,32) && !std::memcmp(image+kEntry,entryPatch,32);
    if(!Readable(image+kEntry,32) || std::memcmp(image+kEntry,original,32))return false;
    unsigned char code[46]{};std::memcpy(code,original,32);code[32]=0xFF;code[33]=0x25;const void* next=image+kEntry+32;std::memcpy(code+38,&next,8);
    void* allocated=edf::AllocateNearCode(image+kEntry,code,sizeof(code));if(!allocated)return false;
    unsigned char patch[32];std::memset(patch,0x90,sizeof(patch));patch[0]=0xFF;patch[1]=0x25;std::memset(patch+2,0,4);const void* hook=reinterpret_cast<const void*>(&Shoot);std::memcpy(patch+6,&hook,8);
    if(!edf::PatchCode(image+kEntry,original,patch,32)){VirtualFree(allocated,0,MEM_RELEASE);return false;}
    trampoline=allocated;originalShoot=reinterpret_cast<ShootFn>(allocated);std::memcpy(entryPatch,patch,32);return true;
#endif
}
}
void* InvokeWalkingPartConstructor(void* storage,const void* descriptor,WalkingPartCtorFn original) {
    using namespace walking_parts;
    if(!scope)return original(storage,descriptor);
    auto& active=*scope;auto* s=active.stream;const auto l=Lease(*s);const unsigned char* entries=nullptr;unsigned count=0;
    if(!original || active.failed || active.current!=kParts || active.cursor>=active.meta.count || !Current(s->root) ||
       !Group(l,active.meta.group,&entries,&count) || count!=active.meta.count || !Readable(descriptor,0x148) ||
       At<const void*>(descriptor,0x30)!=s->root.object || At<const void*>(descriptor,0x38)!=s->root.object+0x8A0 ||
       At<const void*>(descriptor,0x40)!=At<const void*>(entries+active.cursor*0x28,0) || !Readable(At<const void*>(descriptor,0x20),16,true)) {active.failed=true;return nullptr;}
    const unsigned ordinal=active.cursor++;auto& p=AtRecord(active,ordinal);
    alignas(16) unsigned char local[0x148];std::memcpy(local,descriptor,sizeof(local));alignas(16) float world[16];
    if(active.replay) {
        if(!p.ctorCaptured || !CtorValid(p.ctor)){active.failed=true;return nullptr;}
        for(unsigned i=0;i<11;++i)Put<std::uint32_t>(local,kCtorWords[i],p.ctor.words[i]);for(unsigned i=0;i<2;++i)Put<unsigned char>(local,kCtorBytes[i],p.ctor.bytes[i]);
        std::memcpy(world,p.ctor.values,64);Put<float*>(local,8,world);std::memcpy(local+0x60,p.ctor.values+16,32);std::memcpy(local+0x80,p.ctor.values+24,64);
    } else {
        // The original inline selector may still contain an AI weak target.
        // Use the one accepted camera point without a second raycast.
        std::memcpy(local+0x60,active.meta.point,12);
        if(!CaptureCtor(local,&p.ctor)){active.failed=true;return nullptr;}p.ctorCaptured=true;
    }
    void* child=nullptr;active.current=ordinal;bool returned=false;
    __try {child=original(storage,local);returned=true;}
    __finally {active.current=kParts;if(!returned){active.failed=true;s->uncertain=true;}}
    if(!child || !Readable(child,0x38)){active.failed=true;return child;}
    Ref ref{static_cast<unsigned char*>(child),At<unsigned char*>(child,0x30)};
    if(!Gob(ref) || !Pin(ref)){p.child=ref;p.cleanupUncertain=true;active.failed=true;s->uncertain=true;return child;}p.child=ref;p.childHeld=true;
    if(!p.shotCaptured || !p.bulletHeld || !Readable(child,0x5E0) || At<const void*>(child,0x5D0)!=p.bullet.object || At<const void*>(child,0x5D8)!=p.bullet.control)active.failed=true;
    return child;
}
void* InvokeWalkingPartFallback(void* manager,void* outWeak,const float* matrix,void* descriptor,WalkingPartFallbackFn original) {
    using namespace walking_parts;
    if(!scope)return original(manager,outWeak,matrix,descriptor);
    auto& active=*scope;
    if(Readable(outWeak,16,true))std::memset(outWeak,0,16);
    if(active.current>=active.meta.count || !original || !Readable(outWeak,16,true)){active.failed=true;return outWeak;}
    auto& p=AtRecord(active,active.current);const auto l=Lease(*active.stream);void* bullet=nullptr;
    if(active.failed || p.shotCaptured || !Current(active.stream->root)){active.failed=true;return outWeak;}
    if(!active.replay) {
        if(!CaptureNativeProjectileSample(l,descriptor,matrix,descriptor,&p.sample) || p.sample.hasMuzzle){active.failed=true;return outWeak;}
        original(manager,outWeak,matrix,descriptor);bullet=At<void*>(outWeak,0);p.fallback=true;
    } else {
        if(!p.fallback || !CanPrepareNativeProjectileSample(l,descriptor,p.muzzle,p.sample)){active.failed=true;return outWeak;}
        struct Context {void* manager;void* out;WalkingPartFallbackFn original;} context{manager,outWeak,original};
        const auto create=[](void* value,const float* m,void* d) noexcept ->void* {auto* c=static_cast<Context*>(value);c->original(c->manager,c->out,m,d);return At<void*>(c->out,0);};
        bullet=WithNativeProjectileSampleInPlace(p.journal,l,descriptor,p.muzzle,p.sample,create,&context);
        if(!bullet && p.journal.created){bullet=p.journal.created;active.failed=true;active.stream->faulted=true;}
    }
    if(!bullet)active.failed=true;
    else if(!PinBullet(p,bullet)){p.bullet={static_cast<unsigned char*>(bullet),Readable(bullet,0x38) ? At<unsigned char*>(bullet,0x30) : nullptr};p.cleanupUncertain=true;active.failed=true;active.stream->faulted=true;active.stream->uncertain=true;}
    else {p.shotCaptured=true;p.sampleReady=true;}return outWeak;
}
namespace walking_parts {
bool Needs(const NativeLease& l) noexcept {return Family(l);}
bool StageEmpty(const Stream& s) noexcept {
    if(s.stage.transaction || s.staged || s.finalStaged)return false;
    for(const auto& p:s.parts)if(p.childHeld || p.bulletHeld || p.cleanupEntered || p.cleanupUncertain || !NativeProjectileJournalEmpty(p.journal))return false;return true;
}
bool Release(Stream& s) {
    if(s.retired)return true;if(s.executing || s.uncertain || s.count || !StageEmpty(s) || (s.held && !CanUnpin(s.root)))return false;
    if(s.held){s.held=false;s.uncertain=true;Unpin(s.root);s.uncertain=false;}s.retired=true;return true;
}
bool ClearStage(Stream& s,bool remove) {
    for(auto& p:s.parts)if(!RestoreRecord(p,remove))return false;
    for(auto& p:s.parts)p={};s.stage={};s.staged=0;s.finalStaged=false;s.finalSerial=s.finalHash=0;return true;
}
void ReceiptFor(Stream& s,const Packet& e,std::uint64_t hash) noexcept {s.receipts[e.serial%kReceipts]={e.serial,hash};s.applied=e.serial;}
Result ReplayStage(Stream& s) {
    if(!s.finalStaged || s.staged!=s.stage.count || s.executing || scope)return Result::pending;
    const auto l=Lease(s);const unsigned char* entries=nullptr;unsigned count=0;std::uint64_t digest=0;
    if(!Current(s.root) || !Group(l,s.stage.group,&entries,&count) || count!=s.stage.count || !DefinitionOf(l,s.stage.group,&digest) || digest!=s.stage.definition)return Result::failed;
    if(!crew::EnemyControlBerserkerPvpPreflight({s.root.object,s.root.control},count))return Result::pending;
    for(unsigned i=0;i<count;++i)if(!NativeProjectileDependenciesReady(l,s.parts[i].sample))return Result::pending;
    for(unsigned i=0;i<count;++i) {
        auto& p=s.parts[i];if(!p.ctorCaptured || !p.sampleReady || !CtorValid(p.ctor) || !ValidNativeProjectileSample(p.sample) ||
            p.childHeld || p.bulletHeld || !NativeProjectileJournalEmpty(p.journal))return Result::failed;p.shotCaptured=false;
    }
    // Seeds and binding ordinal belong to this host-accepted complete group.
    Put<std::uint64_t>(s.root.object,0x3E8,s.stage.pre3);Put<std::uint64_t>(s.root.object,0x490,s.stage.pre4);s.root.object[0x1940]=s.stage.first;
    Meta actual=s.stage;
    if(!OriginalGroup(&s,s.stage.group,true,&actual,0)){s.faulted=true;return Result::failed;}
    // Native SGO expression evaluation may differ in timing; both the physical
    // bullet and the continuing RNG/identity cursors use the actual host values.
    Put<std::uint64_t>(s.root.object,0x3E8,s.stage.post3);Put<std::uint64_t>(s.root.object,0x490,s.stage.post4);s.root.object[0x1940]=s.stage.last;
    if(!ClearStage(s,false)){s.faulted=true;return Result::failed;}return Result::applied;
}
}
bool WalkingPartsControlReady() noexcept {
    using namespace walking_parts;
    if(!installed || !image || image!=admittedImage || !crew::EnemyControlBerserkerPvpReady())return false;
#ifndef ENEMY_CONTROL_WALKING_PARTS_TEST
    if(!trampoline || !Readable(image+kEntry,32) || std::memcmp(image+kEntry,entryPatch,32))return false;
#endif
    const NativeUnitFactoryProvider provider{kTag,OwnsFactory,InvokeFactory,FactoryFault};return NativeUnitFactoryProviderReady(provider);
}
bool BeforeWalkingParts(const NativeLease& l,bool authority,Input*) noexcept {
    using namespace walking_parts;
    __try {
        if(NativeActionQuiescing(l.object)){if(auto* old=Find(l))old->quiescing=true;return true;}
        if(!WalkingPartsControlReady())return false;
        if(auto* old=Find(l))if(old->retired){std::memset(old,0,sizeof(*old));old->next=1;}auto* s=Bind(l,authority);return s && !s->faulted && !s->quiescing && !s->uncertain;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
bool AfterWalkingParts(const NativeLease& l) noexcept {
    __try {auto* s=walking_parts::Find(l);return s && !s->faulted && !s->uncertain && !s->executing;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool CaptureWalkingParts(const NativeLease& l,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    using namespace walking_parts;if(used)*used=0;
    __try {
        auto* s=Find(l);if(!s)return Family(l);if(s->retired)return true;
        if(!used || !s->authority || s->executing || (s->faulted && !s->quiescing))return false;
        // Preserve the entry prefix when the shared extension scheduler visits
        // Parts first: yielding never consumes or releases a native outcome.
        if(WalkingAcceptedEntryPending(l))return true;
        while(s->count) {
            auto& p=s->queue[s->head];
            if(!p.record.complete || !p.serial) {
                if(!s->quiescing || !RestoreRecord(p.record,true))return false;
                p={};s->head=(s->head+1)%kQueued;--s->count;continue;
            }
            Packet e{};e.kind=p.phase ? 2 : 1;e.serial=p.serial+p.phase;e.transaction=p.meta.transaction;e.ordinal=p.ordinal;
            e.meta=p.meta;e.ctor=p.record.ctor;e.sample=p.record.sample;e.fallback=p.record.fallback;
            if(!Encode(e,bytes,capacity,used))return false;
            if(!p.phase){p.phase=1;return true;}
            if(!RestoreRecord(p.record,false)){*used=0;return false;}p={};s->head=(s->head+1)%kQueued;--s->count;return true;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
bool OwnsWalkingParts(const void* bytes,std::size_t size) noexcept {
    return bytes && size>=4 && Readable(bytes,4) && At<std::uint32_t>(bytes,0)==walking_parts::kTag;
}
enemy_control::ActionResult ApplyWalkingParts(const NativeLease& l,const void* bytes,std::size_t size) noexcept {
    using namespace walking_parts;
    __try {
        Packet e{};if(!Decode(bytes,size,&e))return Result::failed;const auto hash=Hash(e);bool authority=false;
        if(!NativeActionRole(l.object,&authority) || authority)return Result::failed;auto* s=Find(l);
        if(s && s->retired){const auto& r=s->receipts[e.serial%kReceipts];return r.serial==e.serial && r.hash==hash ? Result::applied : Result::failed;}
        s=Bind(l,false);if(!s || s->faulted || s->uncertain)return Result::failed;
        if(e.serial<=s->applied){const auto& r=s->receipts[e.serial%kReceipts];return r.serial==e.serial && r.hash==hash ? Result::applied : Result::failed;}
        if(e.serial!=s->applied+1 || s->executing)return Result::failed;
        if(s->finalStaged) {
            if(e.serial!=s->finalSerial || hash!=s->finalHash)return Result::failed;
            const auto result=ReplayStage(*s);if(result==Result::applied)ReceiptFor(*s,e,hash);return result;
        }
        if(e.kind==1) {
            const unsigned char* entries=nullptr;unsigned count=0;std::uint64_t digest=0;
            if(!Group(l,e.meta.group,&entries,&count) || !DefinitionOf(l,e.meta.group,&digest))return Result::pending;
            if(digest!=e.meta.definition || count!=e.meta.count || e.ordinal!=s->staged)return Result::failed;
            if(!s->stage.transaction) {if(e.ordinal || !StageEmpty(*s))return Result::failed;s->stage=e.meta;}
            else if(!SameMeta(s->stage,e.meta))return Result::failed;
            auto& p=s->parts[e.ordinal];if(p.ctorCaptured || p.sampleReady || p.childHeld || p.bulletHeld || !NativeProjectileJournalEmpty(p.journal))return Result::failed;
            p.ctor=e.ctor;p.ctorCaptured=true;ReceiptFor(*s,e,hash);return Result::applied;
        }
        if(!s->stage.transaction || e.transaction!=s->stage.transaction || e.ordinal!=s->staged || e.ordinal>=s->stage.count)return Result::failed;
        auto& p=s->parts[e.ordinal];if(!p.ctorCaptured || p.sampleReady)return Result::failed;
        p.sample=e.sample;p.sampleReady=true;p.fallback=e.fallback;++s->staged;
        if(s->staged<s->stage.count){ReceiptFor(*s,e,hash);return Result::applied;}
        s->finalStaged=true;s->finalSerial=e.serial;s->finalHash=hash;
        const auto result=ReplayStage(*s);if(result==Result::applied)ReceiptFor(*s,e,hash);return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return Result::failed;}
}
bool QuiesceWalkingParts(const NativeLease& l) noexcept {if(auto* s=walking_parts::Find(l))s->quiescing=true;return true;}
bool WalkingPartsEmpty(const NativeLease& l) noexcept {
    const auto* s=walking_parts::Find(l);return !s || (!s->count && !s->executing && !s->uncertain && walking_parts::StageEmpty(*s));
}
bool RestoreWalkingParts(const NativeLease& l) noexcept {
    __try {auto* s=walking_parts::Find(l);return !s || walking_parts::Release(*s);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool AbortWalkingParts(const NativeLease& l) noexcept {
    using namespace walking_parts;
    __try {
        auto* s=Find(l);if(!s)return true;if(s->retired){std::memset(s,0,sizeof(*s));s->next=1;return true;}if(s->executing || s->uncertain)return false;s->quiescing=true;
        for(unsigned i=0;i<s->count;++i)if(!RestoreRecord(s->queue[(s->head+i)%kQueued].record,true))return false;
        for(unsigned i=0;i<s->count;++i)s->queue[(s->head+i)%kQueued]={};s->head=s->count=0;
        if(!ClearStage(*s,true))return false;return Release(*s);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool InstallWalkingPartsAcceptedActions() noexcept {
    using namespace walking_parts;
    __try {
        if(!image || (admittedImage && image!=admittedImage) || !crew::EnemyControlBerserkerPvpReady())return false;
#ifndef ENEMY_CONTROL_WALKING_PARTS_TEST
        constexpr unsigned char ctor[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
        if(!Readable(image+0x351940,16) || std::memcmp(image+0x351940,ctor,16))return false;
#endif
        const NativeUnitFactoryProvider provider{kTag,OwnsFactory,InvokeFactory,FactoryFault};
        if(!RegisterNativeUnitFactoryProvider(provider) || !NativeUnitFactoryProviderReady(provider))return false;
        const NativeActionExtension extension{kTag,Needs,BeforeWalkingParts,AfterWalkingParts,CaptureWalkingParts,OwnsWalkingParts,ApplyWalkingParts,QuiesceWalkingParts,WalkingPartsEmpty,AbortWalkingParts,RestoreWalkingParts};
        if(!EntryHook() || !RegisterNativeActionExtension(extension))return false;admittedImage=image;installed=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
