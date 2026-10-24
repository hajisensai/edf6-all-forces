// Native pose fallback: canonical semantic wire, preflighted writes, and retained rollback journals.
#include "enemy_control_native_pose.h"
#include "enemy_control_pose_codec.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>

namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_POSE_TEST
void* PoseTestFunction(unsigned rva) noexcept;
#endif
namespace {
using PoseKind=pose::Kind;using pose::State;
constexpr unsigned kMostWrites=16384,kMostBodies=1024,kTransactions=8,kMostPoints=512,kMostParts=256;
struct Sig {unsigned rva;unsigned char bytes[16];};
constexpr Sig kSigs[]={
    {0x63DA0u,{0x40,0x53,0x57,0x48,0x83,0xEC,0x58,0x0F,0x29,0x74,0x24,0x40,0x48,0x8B,0x05,0xA5}},
    {0x63F90u,{0x48,0x83,0xEC,0x68,0xF3,0x0F,0x10,0x52,0x0C,0x33,0xC0,0xF3,0x0F,0x10,0x25,0xF1}},
    {0x11AF6F0u,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0x48,0x8B,0x11,0x48,0x8B,0x4A,0x28}},
    {0x11AF7E0u,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0x48,0x8B,0x11,0x48,0x8B,0x4A,0x28}},
    {0x11AFA70u,{0x48,0x83,0xEC,0x48,0x48,0x8B,0x05,0xDD,0xF5,0xE3,0x00,0x48,0x33,0xC4,0x48,0x89}},
    {0x11B9870u,{0x48,0x8B,0x09,0xE9,0xF8,0x6B,0x02,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {0x97D870u,{0x0F,0x28,0x15,0xE9,0x79,0xEA,0x00,0x0F,0x10,0x59,0x30,0x0F,0x10,0x01,0x0F,0x10}},
    {0x6CE820u,{0x48,0x8B,0x09,0x45,0x33,0xC0,0xE9,0x25,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {0x6CE850u,{0x40,0x55,0x48,0x81,0xEC,0x80,0x00,0x00,0x00,0x48,0x8D,0x6C,0x24,0x50,0x48,0x83}},
    {0x11B1300u,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x48,0x8B,0xD9}},
    {0x11B1060u,{0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0x05,0xE7,0xDF,0xE3}},
    {0x11B18F0u,{0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00}},
    {0x11B1760u,{0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00}},
    {0x3F2CB0u,{0x48,0x8B,0xC4,0x53,0x48,0x81,0xEC,0xC0,0x00,0x00,0x00,0x0F,0x29,0x70,0xE8,0x0F}},
    {0x1100B90u,{0xC6,0x81,0xB0,0x00,0x00,0x00,0x01,0x0F,0x28,0x02,0x48,0x8B,0x41,0x10,0x0F,0x29}},
    {0x6E9130u,{0x48,0x8B,0xC4,0x53,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x4C,0x8B,0x41,0x08,0x48}},
    {0x6ED630u,{0x44,0x8B,0x81,0x9C,0x00,0x00,0x00,0x41,0x8D,0x40,0xFE,0xA9,0xFD,0xFF,0xFF,0xFF}},
    {0x109B80u,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x55}},
};
struct Write {unsigned char* at=nullptr;unsigned size=0,owner=0,scope=0;void* container=nullptr;void* expected=nullptr;void* body=nullptr;bool active=true;unsigned char before[16]{};};
struct Owner {const void* object=nullptr;const void* control=nullptr;bool retained=false,current=true;};
struct Body {void* object=nullptr;void* wrapper=nullptr;bool character=false,current=true;unsigned owner=0,id=0;void* world=nullptr;void* rag=nullptr;float matrix[16]{},linear[4]{},angular[4]{},expected[16]{};double position[4]{};bool expectedSet=false;};
struct Transaction {
    const void* object=nullptr;const void* control=nullptr;unsigned writes=0,bodies=0,owners=0,owner=0;
    bool used=false,changed=false,pending=false;PoseKind kind=PoseKind::deiroi;void* stageRag=nullptr;
    Write write[kMostWrites]{};Body body[kMostBodies]{};Owner retained[kMostParts+1]{};
};
Transaction transactions[kTransactions]{};
struct Part {unsigned char* object=nullptr;void* control=nullptr;void* model=nullptr;void* rag=nullptr;void* collision=nullptr;};
struct DeiroiData {unsigned char* points=nullptr;unsigned char* previous=nullptr;std::size_t count=0,oldCount=0,parts=0;Part part[kMostParts]{};};

template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_POSE_TEST
    return reinterpret_cast<T>(PoseTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool Identity(const NativeLease& l) noexcept {
    return l.object && l.profile && l.control && Readable(l.profile,sizeof(*l.profile)) && Readable(l.object,0x358) &&
        Readable(l.control,16) && At<LONG>(l.control,8)>0 && At<const void*>(l.object,0x30)==l.control &&
        !(l.object[0x18]&4) && !l.object[0x2E8];
}
bool AliveIdentity(const void* object,const void* control) noexcept {
    return object && control && Readable(control,16) && At<LONG>(control,8)>0 && Readable(object,0x38) &&
        At<const void*>(object,0x30)==control && !(At<unsigned char>(object,0x18)&4);
}
bool KindOf(const NativeLease& l,PoseKind* kind,unsigned* secondary=nullptr) noexcept {
    if(!l.profile)return false;unsigned net=0;
    switch(l.profile->vtableRva) {
    case 0x17AA648:*kind=PoseKind::deiroi;net=0x17AA7B8;break;
    case 0x17AFEE0:*kind=PoseKind::large;net=0x17B00A0;break;
    case 0x17B93C0:*kind=PoseKind::timeShip;net=0x17B9580;break;
    case 0x17B8898:*kind=PoseKind::squid;net=0x17B8A58;break;
    case 0x17CC838:*kind=PoseKind::ufo515;net=0x17CC9A8;break;
    default:return false;
    }
    if(secondary)*secondary=net;return true;
}
bool Codes() noexcept {
    if(!image)return false;
    for(const auto& s:kSigs)if(!Readable(image+s.rva,sizeof(s.bytes)) || std::memcmp(image+s.rva,s.bytes,sizeof(s.bytes)))return false;
    return true;
}
Transaction* Existing(const NativeLease& l) noexcept {
    for(auto& t:transactions)if(t.used && t.object==l.object && t.control==l.control)return &t;return nullptr;
}
Transaction* Start(const NativeLease& l) noexcept {
    for(auto& t:transactions)if(!t.used){t.writes=t.bodies=0;t.owners=1;t.owner=0;t.stageRag=nullptr;t.retained[0]={l.object,l.control,false,true};t.used=true;t.changed=t.pending=false;t.object=l.object;t.control=l.control;return &t;}return nullptr;
}
bool Clear(Transaction& t) noexcept {
    for(unsigned i=0;i<t.owners;++i)if(t.retained[i].retained && !Readable(t.retained[i].control,16,true)){t.pending=true;return false;}
    for(unsigned i=0;i<t.owners;++i)if(t.retained[i].retained) {
        auto* ctrl=const_cast<unsigned char*>(static_cast<const unsigned char*>(t.retained[i].control));
        t.retained[i].retained=false;
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+12))==0) {
            const auto table=At<void* const*>(ctrl,0);reinterpret_cast<void(__fastcall*)(void*)>(table[1])(ctrl);
        }
    }
    t.used=t.changed=t.pending=false;t.writes=t.bodies=t.owners=0;t.object=t.control=t.stageRag=nullptr;return true;
}
bool Own(Transaction& t,const void* object,const void* control) noexcept {
    for(unsigned i=0;i<t.owners;++i)if(t.retained[i].object==object && t.retained[i].control==control){t.owner=i;return true;}
    if(t.owners>=kMostParts+1 || !AliveIdentity(object,control) || !Readable(control,16,true))return false;
    auto* c=const_cast<unsigned char*>(static_cast<const unsigned char*>(control));
    if(At<LONG>(c,12)<=0 || At<LONG>(c,12)==LONG_MAX)return false;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(c+12));t.owner=t.owners;
    t.retained[t.owners++]={object,control,true,true};return true;
}
bool PointerFact(Transaction&,const void* at,unsigned size=8) noexcept {
    return (size==4 || size==8) && Readable(at,size);
}
bool CurrentOwners(Transaction& t) noexcept {
    for(unsigned i=0;i<t.owners;++i) {
        auto& o=t.retained[i];if(!Readable(o.control,16))return false;
        if(At<LONG>(o.control,8)<=0){o.current=false;continue;}
        if(!Readable(o.object,0x38))return false;
        o.current=At<const void*>(o.object,0x30)==o.control && !(At<unsigned char>(o.object,0x18)&4);
        if(i && o.current) {
            if(!Readable(o.object,0x5C8) || !Readable(t.object,0xF78))return false;
            if(At<const void*>(o.object,0x5C0)!=t.object){o.current=false;continue;}
            const auto* list=At<const unsigned char*>(t.object,0xF60);const auto count=At<std::uint64_t>(t.object,0xF70);
            if(count>kMostParts || (count && !Readable(list,static_cast<std::size_t>(count)*16)))return false;
            bool member=false;for(std::size_t j=0;j<count;++j)member=member ||
                (At<const void*>(list+j*16,0)==o.object && At<const void*>(list+j*16,8)==o.control);
            if(!member)o.current=false;
        }
    }
    return true;
}
unsigned char* CurrentRag(const Transaction& t,unsigned owner) noexcept {
    auto* object=const_cast<unsigned char*>(static_cast<const unsigned char*>(t.retained[owner].object));
    if(owner){if(!Readable(object,0x798))return nullptr;return At<unsigned char*>(object,0x790);}
    return object+(t.kind==PoseKind::deiroi ? 0xB60 : 0xD80);
}
unsigned char* CurrentModel(const Transaction& t,unsigned owner) noexcept {
    auto* object=const_cast<unsigned char*>(static_cast<const unsigned char*>(t.retained[owner].object));
    if(owner){if(!Readable(object,0x7A8))return nullptr;auto* model=At<unsigned char*>(object,0x7A0);return model ? model+0xA0 : nullptr;}
    return object+(t.kind==PoseKind::deiroi ? 0x6A0 : 0x940);
}
bool BoundBody(Transaction& t,Body& b,void** node=nullptr) noexcept {
    b.current=t.retained[b.owner].current;if(!b.current)return true;
    auto* owner=const_cast<unsigned char*>(static_cast<const unsigned char*>(t.retained[b.owner].object));
    if(b.character || !b.rag) {
        auto* wrapper=b.character ? static_cast<unsigned char*>(b.wrapper) : owner+0xEA0;
        if(!Readable(wrapper,8))return false;
        if(At<void*>(wrapper,0)!=b.object){b.current=false;return true;}
    } else {
        auto* rag=CurrentRag(t,b.owner);if(!Readable(rag,0x90))return false;
        auto* records=At<unsigned char*>(rag,0x78);const auto count=At<std::uint64_t>(rag,0x88);
        if(count>kMostBodies || (count && !Readable(records,static_cast<std::size_t>(count)*0xC0)))return false;
        bool found=false;
        for(std::size_t i=0;i<count;++i) {
            auto* r=records+i*0xC0;const auto mode=At<unsigned>(r,0x9C);if(!r[0xA4])continue;
            if(mode==2 && At<void*>(r,0x50)==b.object)found=true;
            else if(mode==1) {
                auto* proxy=At<unsigned char*>(r,0xB0);if(!Readable(proxy,0xA8))return false;
                if(At<void*>(proxy,0x10)==b.object){found=true;if(node)*node=proxy;}
            }
        }
        if(!found){b.current=false;return true;} // old identity left the actual owner's current container
    }
    if(b.character) {
        if(!Readable(b.object,0x38))return false;
        if(At<unsigned>(b.object,0x18)!=b.id || At<void*>(b.object,0x28)!=b.world)b.current=false;
        return true;
    }
    if(!Readable(b.object,0x108))return false;
    if(At<unsigned>(b.object,0xF0)!=b.id || At<void*>(b.object,0x100)!=b.world)b.current=false;
    return true;
}
bool CurrentWrites(Transaction& t) noexcept {
    for(unsigned i=0;i<t.bodies;++i)if(!BoundBody(t,t.body[i]))return false;
    for(unsigned i=0;i<t.writes;++i) {
        auto& w=t.write[i];w.active=t.retained[w.owner].current;if(!w.active)continue;
        if(w.scope==4) {
            if(!Readable(w.container,0x18))return false;
            auto* data=At<unsigned char*>(w.container,0);const auto count=At<std::uint64_t>(w.container,0x10);
            if(data!=w.expected){w.active=false;continue;}
            const auto offset=static_cast<std::size_t>(w.at-data);
            if(count>kMostPoints)return false;
            if(offset>=static_cast<std::size_t>(count)*0x30){w.active=false;continue;}
        } else if(w.scope==1) {
            auto* model=CurrentModel(t,w.owner);if(!Readable(model,0x28))return false;
            if(model!=w.container || At<void*>(model,0x10)!=w.expected){w.active=false;continue;}
            const auto count=At<std::uint64_t>(model,0x20);if(count>2048)return false;
            if(w.at!=model+0xB0) {
                auto* nodes=static_cast<unsigned char*>(w.expected);
                const auto offset=static_cast<std::size_t>(w.at-nodes);
                if(offset>=static_cast<std::size_t>(count)*0x110){w.active=false;continue;}
            }
        } else if(w.scope==3) {
            auto* rag=CurrentRag(t,w.owner);if(!Readable(rag,0x90))return false;
            if(rag!=w.container || At<void*>(rag,0x78)!=w.expected){w.active=false;continue;}
        } else if(w.scope==2) {
            bool found=false;
            for(unsigned j=0;j<t.bodies;++j)if(t.body[j].owner==w.owner && t.body[j].object==w.body) {
                void* node=nullptr;if(!BoundBody(t,t.body[j],&node))return false;
                found=t.body[j].current && node==w.expected;break;
            }
            if(!found){w.active=false;continue;}
        }
        if(!Readable(w.at,w.size,true))return false;
    }
    return true;
}
bool Remember(Transaction& t,void* address,unsigned size,unsigned scope=0,void* container=nullptr,void* expected=nullptr,void* body=nullptr) noexcept {
    auto* at=static_cast<unsigned char*>(address);
    if(!at || !size || !Readable(at,size,true))return false;
    while(size) {
        const unsigned n=size<16 ? size : 16;
        if(t.writes>=kMostWrites)return false;auto& w=t.write[t.writes++];w.at=at;w.size=n;w.owner=t.owner;w.scope=scope;w.container=container;w.expected=expected;w.body=body;w.active=true;std::memcpy(w.before,at,n);
        at+=n;size-=n;
    }
    return true;
}
bool Field(Transaction& t,unsigned char* o,unsigned off,unsigned size) noexcept {return Remember(t,o+off,size);}
void Get3(const unsigned char* o,unsigned off,float* out) noexcept {std::memcpy(out,o+off,12);}
void Set3(unsigned char* o,unsigned off,const float* in) noexcept {std::memcpy(o+off,in,12);}
bool Normalize(float* q) noexcept {
    double length=0;for(unsigned i=0;i<4;++i){if(!std::isfinite(q[i]))return false;length+=static_cast<double>(q[i])*q[i];}
    if(length<1.0e-12 || length>1.0e12)return false;
    const float scale=static_cast<float>(1.0/std::sqrt(length));for(unsigned i=0;i<4;++i)q[i]*=scale;return true;
}
bool GetQ(const unsigned char* o,unsigned off,float* out) noexcept {std::memcpy(out,o+off,16);return Normalize(out);}
bool RigidTransform(const float* m) noexcept {
    for(unsigned i=0;i<16;++i)if(!std::isfinite(m[i]) || std::fabs(m[i])>1.0e6f)return false;
    if(std::fabs(m[3])>1.0e-4f || std::fabs(m[7])>1.0e-4f || std::fabs(m[11])>1.0e-4f || std::fabs(m[15]-1)>1.0e-4f)return false;
    for(unsigned a=0;a<3;++a)for(unsigned b=a;b<3;++b){float dot=0;for(unsigned c=0;c<3;++c)dot+=m[a*4+c]*m[b*4+c];if(std::fabs(dot-(a==b ? 1.0f : 0.0f))>0.05f)return false;}
    return true;
}
bool MatrixState(const float* m,State& state) noexcept {
    if(!RigidTransform(m))return false;
    std::memcpy(state.position,m+12,12);
    Fn<void(__fastcall*)(float*,const float*)>(0x63DA0)(state.rotation,m);return Normalize(state.rotation);
}
void Matrix(const State& s,float* m) noexcept {
    Fn<void(__fastcall*)(float*,const float*)>(0x63F90)(m,s.rotation);
    m[3]=m[7]=m[11]=0;m[15]=1;std::memcpy(m+12,s.position,12);
}
bool RigidLinks(const void* body) noexcept {
    if(!Readable(body,0x108,true))return false;const auto world=At<const unsigned char*>(body,0x100);
    if(!Readable(world,0x60))return false;const auto service=At<const unsigned char*>(world,0x58);
    return Readable(service,0x30) && Readable(At<const void*>(service,0x18),0xB8) && Readable(At<const void*>(service,0x20),0x88);
}
bool MotionStorage(void* readService,unsigned id) noexcept {
    if(!Readable(readService,8))return false;const auto table=At<void* const*>(readService,0);
    if(!Readable(table,0x100))return false;
    const auto query=reinterpret_cast<const unsigned char*(__fastcall*)(void*,std::uint32_t)>(table[0x80/8]);
    const auto motionQuery=reinterpret_cast<const unsigned char*(__fastcall*)(void*,std::uint32_t)>(table[0xF8/8]);
    const auto* raw=query(readService,id);if(!Readable(raw,0x84))return false;
    const auto* motion=motionQuery(readService,At<std::uint32_t>(raw,0x80));
    // Native 11B1300 returns motion+60; DF68A0 reads its quaternion and angular velocity at +70.
    return Readable(motion,0x80,true);
}
bool CharacterStorage(void* wrapper) noexcept {
    if(!Readable(wrapper,8))return false;auto* body=At<unsigned char*>(wrapper,0);
    if(!Readable(body,0x38,true))return false;auto* readService=At<void*>(body,0x28);const auto writeService=At<const void*>(body,0x30);
    if(!Readable(readService,8) || !Readable(writeService,8))return false;
    const auto table=At<void* const*>(readService,0);
    if(!Readable(table,0x100) || !Readable(At<const void*>(writeService,0),0xA8))return false;
    const auto matrix=reinterpret_cast<const void*(__fastcall*)(void*,std::uint32_t)>(table[0x88/8]);
    const auto velocity=reinterpret_cast<const void*(__fastcall*)(void*,std::uint32_t)>(table[0xA0/8]);
    const unsigned id=At<unsigned>(body,0x18);
    return Readable(matrix(readService,id),0xB0,true) && Readable(velocity(readService,id),16,true) && MotionStorage(readService,id);
}
bool RigidStorage(void* body) noexcept {
    if(!RigidLinks(body))return false;
    auto* world=At<unsigned char*>(body,0x100);auto* service=At<unsigned char*>(world,0x58)+0x20;
    const auto table=At<void* const*>(service,0);if(!Readable(table,0x88))return false;
    const auto query=reinterpret_cast<const void*(__fastcall*)(void*,std::uint32_t)>(table[0x80/8]);
    const unsigned id=At<unsigned>(body,0xF0);
    return Readable(query(service,id),0xB0,true) && MotionStorage(service,id);
}
const unsigned char* BodyStorage(const Body& b) noexcept {
    void* readService=nullptr;
    if(b.character)readService=At<void*>(b.object,0x28);
    else {auto* world=At<unsigned char*>(b.object,0x100);readService=At<unsigned char*>(world,0x58)+0x20;}
    if(!Readable(readService,8))return nullptr;const auto table=At<void* const*>(readService,0);
    if(!Readable(table,0x88))return nullptr;
    const auto query=reinterpret_cast<const unsigned char*(__fastcall*)(void*,std::uint32_t)>(table[0x80/8]);
    const auto* raw=query(readService,b.id);return Readable(raw,0xB0,true) ? raw : nullptr;
}
bool PhysicsPosition(const Body& b,double* out) noexcept {
    const auto* raw=BodyStorage(b);if(!raw)return false;
    for(unsigned i=0;i<3;++i){out[i]=static_cast<double>(At<float>(raw,0x30+i*4))+At<float>(raw,0x40+i*4);if(!std::isfinite(out[i]))return false;}
    out[3]=0;return true;
}
bool RestoreTransform(const Body& b,const float* matrix) noexcept {
    void* writer=nullptr;
    if(b.character)writer=At<void*>(b.object,0x30);
    else {auto* world=At<unsigned char*>(b.object,0x100);writer=At<unsigned char*>(world,0x58)+0x18;}
    if(!Readable(writer,8))return false;const auto table=At<void* const*>(writer,0);if(!Readable(table,0x90))return false;
    alignas(32) double position[4]{};std::memcpy(position,b.position,sizeof position);
    // Havok keeps XYZ as a float high part plus a float residual. The native writer accepts their double sum.
    reinterpret_cast<void(__fastcall*)(void*,std::uint32_t,const float*,const double*,int)>(table[0x88/8])(writer,b.id,matrix,position,0);
    double restored[4]{};if(!PhysicsPosition(b,restored))return false;
    for(unsigned i=0;i<3;++i)if(std::fabs(restored[i]-b.position[i])>1.0e-9+std::fabs(b.position[i])*2.0e-15)return false;
    return true;
}
bool RigidMatrix(void* body,float* out) noexcept {
    if(!RigidLinks(body))return false;
    const auto world=At<const unsigned char*>(body,0x100);auto* service=const_cast<unsigned char*>(At<const unsigned char*>(world,0x58))+0x20;
    const auto table=At<void* const*>(service,0);
    const auto read=reinterpret_cast<const void*(__fastcall*)(void*,std::uint32_t)>(table[0x80/8]);
    const void* raw=read(service,At<std::uint32_t>(body,0xF0));if(!Readable(raw,64,true))return false;
    Fn<void(__fastcall*)(const void*,float*)>(0x97D870)(raw,out);return true;
}
bool Same3(const float* a,const float* b) noexcept {
    for(unsigned i=0;i<3;++i)if(!std::isfinite(a[i]) || !std::isfinite(b[i]) || std::fabs(a[i]-b[i])>0.002f+std::fabs(b[i])*1.0e-5f)return false;
    return true;
}
bool SameMatrix(const float* a,const float* b) noexcept {
    for(unsigned i=0;i<16;++i)if(!std::isfinite(a[i]) || !std::isfinite(b[i]) || std::fabs(a[i]-b[i])>0.002f+std::fabs(b[i])*1.0e-5f)return false;
    return true;
}
bool AddRigid(Transaction& t,void* body) noexcept {
    for(unsigned i=0;i<t.bodies;++i)if(t.body[i].object==body)return true;
    if(t.bodies>=kMostBodies || !RigidStorage(body))return false;
    Body b;b.object=body;b.owner=t.owner;b.id=At<unsigned>(body,0xF0);b.world=At<void*>(body,0x100);b.rag=t.stageRag;
    if(!PointerFact(t,static_cast<unsigned char*>(body)+0x100) || !PointerFact(t,static_cast<unsigned char*>(body)+0xF0,4))return false;
    if(!RigidMatrix(body,b.matrix) || !RigidTransform(b.matrix) || !PhysicsPosition(b,b.position))return false;
    const auto* v=Fn<const float*(__fastcall*)(void*)>(0x11B1300)(body);if(!Readable(v,32,true))return false;std::memcpy(b.linear,v,16);
    alignas(16) float angular[4]{};Fn<void(__fastcall*)(void*,float*)>(0x11B1060)(body,angular);std::memcpy(b.angular,angular,16);
    t.body[t.bodies++]=b;return true;
}
bool AddCharacter(Transaction& t,void* wrapper) noexcept {
    if(t.bodies>=kMostBodies || !CharacterStorage(wrapper))return false;
    Body b;b.character=true;b.wrapper=wrapper;b.object=At<void*>(wrapper,0);b.owner=t.owner;
    auto* inner=static_cast<unsigned char*>(b.object);b.id=At<unsigned>(inner,0x18);b.world=At<void*>(inner,0x28);
    if(!Readable(At<void*>(inner,0x28),8) || !Readable(At<void*>(inner,0x30),8) ||
       !Readable(At<const void*>(At<void*>(inner,0x28),0),0xA8) || !Readable(At<const void*>(At<void*>(inner,0x30),0),0xA8) ||
       !PointerFact(t,wrapper) || !PointerFact(t,inner+0x28) || !PointerFact(t,inner+0x30) || !PointerFact(t,inner+0x18,4))return false;
    Fn<void(__fastcall*)(void*,float*)>(0x11AF6F0)(wrapper,b.matrix);
    Fn<void(__fastcall*)(void*,float*)>(0x11AF7E0)(wrapper,b.linear);
    if(!RigidTransform(b.matrix) || !PhysicsPosition(b,b.position))return false;
    t.body[t.bodies++]=b;return true;
}
bool Rollback(Transaction& t) noexcept {
    if(!Readable(t.control,16)){t.pending=true;return false;}
    if(At<LONG>(t.control,8)<=0)return Clear(t);
    if(!Readable(t.object,0x38)){t.pending=true;return false;}
    if(At<const void*>(t.object,0x30)!=t.control || (At<unsigned char>(t.object,0x18)&4))return Clear(t);
    __try {
        if(!CurrentOwners(t) || !CurrentWrites(t)){t.pending=true;return false;}
        for(unsigned i=0;i<t.bodies;++i)if(t.body[i].current &&
            !(t.body[i].character ? CharacterStorage(t.body[i].wrapper) : RigidStorage(t.body[i].object))){t.pending=true;return false;}
        // Restore native cache first, then the true body transforms and velocity that those caches describe.
        for(unsigned i=t.writes;i>0;--i){const auto& w=t.write[i-1];if(w.active)std::memcpy(w.at,w.before,w.size);}
        for(unsigned i=0;i<t.bodies;++i) {
            const auto& b=t.body[i];if(!b.current)continue;alignas(16) float got[16]{},before[16]{};std::memcpy(before,b.matrix,64);
            if(b.character) {
                if(!Readable(b.wrapper,8) || At<void*>(b.wrapper,0)!=b.object){t.pending=true;return false;}
                if(!RestoreTransform(b,before)){t.pending=true;return false;}
                Fn<void(__fastcall*)(void*,const float*)>(0x11AFA70)(b.wrapper,b.linear);
                Fn<void(__fastcall*)(void*,float*)>(0x11AF6F0)(b.wrapper,got);
            } else {
                if(!RigidLinks(b.object)){t.pending=true;return false;}
                if(!RestoreTransform(b,before)){t.pending=true;return false;}
                Fn<void(__fastcall*)(void*,const float*)>(0x11B18F0)(b.object,b.linear);
                Fn<void(__fastcall*)(void*,const float*)>(0x11B1760)(b.object,b.angular);
                if(!RigidMatrix(b.object,got)){t.pending=true;return false;}
            }
            if(!SameMatrix(got,b.matrix)){t.pending=true;return false;}
            alignas(16) float velocity[4]{},angular[4]{};
            if(b.character)Fn<void(__fastcall*)(void*,float*)>(0x11AF7E0)(b.wrapper,velocity);
            else {
                const float* linear=Fn<const float*(__fastcall*)(void*)>(0x11B1300)(b.object);if(!Readable(linear,16)){t.pending=true;return false;}
                std::memcpy(velocity,linear,16);Fn<void(__fastcall*)(void*,float*)>(0x11B1060)(b.object,angular);
            }
            if(!Same3(velocity,b.linear) || (!b.character && !Same3(angular,b.angular))){t.pending=true;return false;}
        }
        return Clear(t);
    } __except(EXCEPTION_EXECUTE_HANDLER){t.pending=true;return false;}
}
bool Skeleton(Transaction& t,unsigned char* model) noexcept {
    if(!Readable(model,0xB1))return false;
    auto* nodes=At<unsigned char*>(model,0x10);const auto count=At<std::uint64_t>(model,0x20);
    if(!count || count>2048 || !Readable(nodes,static_cast<std::size_t>(count)*0x110) || !PointerFact(t,model+0x10) || !PointerFact(t,model+0x20))return false;
    if(!Remember(t,model+0xB0,1,1,model,nodes))return false;
    for(std::size_t i=0;i<count;++i)if(!Remember(t,nodes+i*0x110+0xB0,64,1,model,nodes))return false;
    return true;
}
bool Rag(Transaction& t,unsigned char* rag) noexcept {
    if(!Readable(rag,0x1B8))return false;
    const auto n=At<std::uint64_t>(rag,0x88);auto* records=At<unsigned char*>(rag,0x78);
    if(!n || n>kMostBodies || !Readable(records,static_cast<std::size_t>(n)*0xC0) || !PointerFact(t,rag+0x78) || !PointerFact(t,rag+0x88))return false;
    if(!Remember(t,rag+0x190,32,3,rag,records) || !Remember(t,rag+0x1C4,1,3,rag,records))return false;
    t.stageRag=rag;
    for(std::size_t i=0;i<n;++i) {
        auto* r=records+i*0xC0;if(!r[0xA4])continue;const auto mode=At<unsigned>(r,0x9C);
        if(mode!=1 && mode!=2)continue; // Detached/dead dynamic debris (mode 4) is not part of a living lease.
        const auto bind=At<const unsigned char*>(r,8);
        if(!Readable(bind,0x48) || !Readable(At<const void*>(bind,0x40),8))return false;
        const auto bone=At<const void*>(At<const void*>(bind,0x40),0);if(!Readable(bone,0xF0) || !PointerFact(t,r+8) || !PointerFact(t,bind+0x40) || !PointerFact(t,At<const void*>(bind,0x40)))return false;
        if(mode==2){if(!PointerFact(t,r+0x50) || !AddRigid(t,At<void*>(r,0x50)))return false;}
        else {
            auto* node=At<unsigned char*>(r,0xB0);auto* manager=At<unsigned char*>(r,0xB8);
            if(!Readable(node,0xA8) || manager!=rag+0x110 || !PointerFact(t,r+0xB0) || !PointerFact(t,r+0xB8) || !PointerFact(t,node+0x10) || !Remember(t,node+0x40,96,2,rag,node,At<void*>(node,0x10)) || !Remember(t,node+0xA0,1,2,rag,node,At<void*>(node,0x10)))return false;
            if(!AddRigid(t,At<void*>(node,0x10)))return false;
        }
    }
    t.stageRag=nullptr;return true;
}
bool FlushRag(Transaction& t,unsigned char* rag) noexcept {
    const auto n=At<std::uint64_t>(rag,0x88);auto* records=At<unsigned char*>(rag,0x78);
    for(std::size_t i=0;i<n;++i) {
        auto* r=records+i*0xC0;const auto mode=At<unsigned>(r,0x9C);if(!r[0xA4] || (mode!=1 && mode!=2))continue;
        alignas(16) float target[16]{};
        Fn<void(__fastcall*)(void*,float*)>(0x6E9130)(r,target);
        Fn<void(__fastcall*)(void*,const float*)>(0x6ED630)(r,target);
        void* body=mode==2 ? At<void*>(r,0x50) : At<void*>(At<void*>(r,0xB0),0x10);
        for(unsigned j=0;j<t.bodies;++j)if(t.body[j].object==body){std::memcpy(t.body[j].expected,target,64);t.body[j].expectedSet=true;break;}
    }
    Fn<void(__fastcall*)(void*)>(0x109B80)(rag+0x110);
    for(unsigned i=0;i<t.bodies;++i)if(t.body[i].rag==rag && t.body[i].expectedSet) {
        alignas(16) float actual[16]{};if(!RigidMatrix(t.body[i].object,actual) || !SameMatrix(actual,t.body[i].expected))return false;
    }
    return true;
}
bool Parts(Transaction& t,unsigned char* o,DeiroiData& d) noexcept {
    d.points=At<unsigned char*>(o,0xB08);d.count=static_cast<std::size_t>(At<std::uint64_t>(o,0xB18));
    d.previous=At<unsigned char*>(o,0xB28);d.oldCount=static_cast<std::size_t>(At<std::uint64_t>(o,0xB38));
    if(!PointerFact(t,o+0xB08) || !PointerFact(t,o+0xB18) || !PointerFact(t,o+0xB28) || !PointerFact(t,o+0xB38) || !PointerFact(t,o+0xF60) || !PointerFact(t,o+0xF70))return false;
    if(!d.count || d.count>kMostPoints || !d.oldCount || d.oldCount>kMostPoints ||
       !Readable(d.points,d.count*0x30) || !Readable(d.previous,d.oldCount*0x30))return false;
    for(std::size_t i=0;i<d.count;++i)if(!Remember(t,d.points+i*0x30,16,4,o+0xB08,d.points))return false;
    for(std::size_t i=0;i<d.oldCount;++i)if(!Remember(t,d.previous+i*0x30,16,4,o+0xB28,d.previous))return false;
    const auto count=At<std::uint64_t>(o,0xF70);const auto list=At<const unsigned char*>(o,0xF60);
    if(count>kMostParts || (count && !Readable(list,static_cast<std::size_t>(count)*16)))return false;
    for(std::size_t i=0;i<count;++i) {
        auto* part=At<unsigned char*>(list+i*16,0);void* control=At<void*>(list+i*16,8);
        if(!part && !control)continue;
        if(!part || !control || !Readable(control,16))return false;
        if(At<LONG>(control,8)<=0)continue;
        // An unreadable live part is unknown, not retired: preflight must not commit a partial body.
        if(!Readable(part,0x7B8))return false;
        if(At<void*>(part,0x30)!=control || (part[0x18]&4) || At<void*>(part,0x5C0)!=o || part[0x2E8])continue;
        Part p{part,control,At<void*>(part,0x7A0),At<void*>(part,0x790),At<void*>(part,0x7B0)};
        if(!Own(t,part,control) || !PointerFact(t,part+0x7A0) || !PointerFact(t,part+0x790))return false;
        if(!Readable(p.model,0x1A0) || !Skeleton(t,static_cast<unsigned char*>(p.model)+0xA0) ||
           !Field(t,part,0x60,64) || !Field(t,part,0x610,32) || !Rag(t,static_cast<unsigned char*>(p.rag)))return false;
        d.part[d.parts++]=p;
    }
    t.owner=0;return true;
}
void TurnVector(const float* oldM,const float* newM,const float* v,float* out) noexcept {
    float local[3]{};for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)local[j]+=v[k]*oldM[j*4+k];
    for(unsigned k=0;k<3;++k){out[k]=0;for(unsigned j=0;j<3;++j)out[k]+=local[j]*newM[j*4+k];}
}
void TurnPoint(const float* oldM,const float* newM,float* v) noexcept {
    const float relative[3]={v[0]-oldM[12],v[1]-oldM[13],v[2]-oldM[14]};float rotated[3]{};TurnVector(oldM,newM,relative,rotated);
    for(unsigned i=0;i<3;++i)v[i]=newM[12+i]+rotated[i];
}
bool Preflight(const NativeLease& l,PoseKind kind,Transaction& t,DeiroiData& d) noexcept {
    auto* o=l.object;t.kind=kind;
    if(!Field(t,o,0x60,64))return false;
    switch(kind) {
    case PoseKind::large:
        return AddCharacter(t,o+0xFF0) && Field(t,o,0xFB0,8) && Field(t,o,0xFC0,8) && Field(t,o,0x1020,12) && Field(t,o,0x1440,64);
    case PoseKind::squid:
        return AddCharacter(t,o+0xF90) && Field(t,o,0x12C0,12) && Field(t,o,0x12D0,12) && Field(t,o,0xFB0,12);
    case PoseKind::ufo515:
        return Readable(o+0xEA0,8) && PointerFact(t,o+0xEA0) && AddRigid(t,At<void*>(o,0xEA0)) && Field(t,o,0x5C0,64) &&
            Field(t,o,0x600,12) && Field(t,o,0x10A0,4) && Field(t,o,0x10B0,8);
    case PoseKind::timeShip:
        return Field(t,o,0xF90,64) && Field(t,o,0xFD0,4) && Field(t,o,0xFD8,4) && Field(t,o,0x1430,64) &&
            Field(t,o,0x860,64) && Skeleton(t,o+0x940) && Rag(t,o+0xD80);
    case PoseKind::deiroi:
        return Field(t,o,0xF40,8) && Field(t,o,0x1610,12) && Parts(t,o,d) && Skeleton(t,o+0x6A0) && Rag(t,o+0xB60);
    }
    return false;
}
bool MoveSkeleton(unsigned char* model,const float* matrix) noexcept {
    Fn<void(__fastcall*)(void*,const float*)>(0x1100B90)(model,matrix);
    auto* nodes=At<unsigned char*>(model,0x10);
    return Readable(nodes,0xF0) && SameMatrix(reinterpret_cast<const float*>(nodes+0xB0),matrix);
}
bool Apply(const NativeLease& l,const State& s,Transaction& t,const DeiroiData& d) noexcept {
    auto* o=l.object;alignas(16) float m[16]{};Matrix(s,m);t.changed=true;
    switch(s.kind) {
    case PoseKind::large:
        Fn<void(__fastcall*)(void*,const float*)>(0x11B9870)(o+0xFF0,m);
        {alignas(16) float velocity[4]={s.linearVelocity[0],s.linearVelocity[1],s.linearVelocity[2],0};Fn<void(__fastcall*)(void*,const float*)>(0x11AFA70)(o+0xFF0,velocity);}
        std::memcpy(o+0xFB0,s.targetAngles,8);std::memcpy(o+0xFC0,s.currentAngles,8);Set3(o,0x1020,s.accumulatedVelocity);
        std::memcpy(o+0x1470,s.auxiliaryRotation,16);std::memcpy(o+0x1460,s.rotationStep,16);std::memcpy(o+0x1440,s.rotationDelta,16);
        std::memcpy(o+0x60,m,64);break;
    case PoseKind::squid:
        Fn<void(__fastcall*)(void*,const float*)>(0x11B9870)(o+0xF90,m);
        {alignas(16) float velocity[4]={s.linearVelocity[0],s.linearVelocity[1],s.linearVelocity[2],0};Fn<void(__fastcall*)(void*,const float*)>(0x11AFA70)(o+0xF90,velocity);}
        Set3(o,0x12C0,s.targetAngles);Set3(o,0x12D0,s.currentAngles);Set3(o,0xFB0,s.accumulatedVelocity);std::memcpy(o+0x60,m,64);break;
    case PoseKind::ufo515: {
        Fn<void(__fastcall*)(void*,const float*)>(0x6CE820)(o+0xEA0,m);
        alignas(16) float velocity[4]={s.linearVelocity[0],s.linearVelocity[1],s.linearVelocity[2],0};
        Fn<void(__fastcall*)(void*,const float*)>(0x11B18F0)(At<void*>(o,0xEA0),velocity);
        std::memcpy(o+0x5C0,m,64);Set3(o,0x600,s.accumulatedVelocity);std::memcpy(o+0x10B0,s.currentAngles,8);Put<float>(o,0x10A0,s.bank);
        std::memcpy(o+0x60,m,64);break;
    }
    case PoseKind::timeShip:
        std::memcpy(o+0x60,m,64);std::memcpy(o+0xFB0,s.rotation,16);std::memcpy(o+0xFC0,s.targetRotation,16);
        Set3(o,0xF90,s.linearVelocity);Set3(o,0xFA0,s.accumulatedVelocity);Put<float>(o,0xFD0,s.phase);Put<float>(o,0xFD8,s.scale);
        std::memcpy(o+0x1430,s.deformation,64);Fn<void(__fastcall*)(void*)>(0x3F2CB0)(o);
        if(!MoveSkeleton(o+0x940,reinterpret_cast<const float*>(o+0x860)))return false;
        if(!FlushRag(t,o+0xD80))return false;break;
    case PoseKind::deiroi: {
        alignas(16) float old[16];std::memcpy(old,o+0x60,64);
        for(std::size_t i=0;i<d.count;++i)TurnPoint(old,m,reinterpret_cast<float*>(d.points+i*0x30));
        for(std::size_t i=0;i<d.oldCount;++i)TurnPoint(old,m,reinterpret_cast<float*>(d.previous+i*0x30));
        std::memcpy(o+0x60,m,64);std::memcpy(o+0xF40,s.targetAngles,8);Set3(o,0x1610,s.linearVelocity);
        for(std::size_t i=0;i<d.parts;++i) {
            const auto& p=d.part[i];TurnPoint(old,m,reinterpret_cast<float*>(p.object+0x610));
            alignas(16) float partM[16]{};Fn<void(__fastcall*)(float*,const float*)>(0x63F90)(partM,reinterpret_cast<const float*>(p.object+0x620));
            for(unsigned row=0;row<3;++row){float result[3]{};TurnVector(old,m,partM+row*4,result);std::memcpy(partM+row*4,result,12);}
            Fn<void(__fastcall*)(float*,const float*)>(0x63DA0)(reinterpret_cast<float*>(p.object+0x620),partM);
            Fn<void(__fastcall*)(float*,const float*)>(0x63F90)(reinterpret_cast<float*>(p.object+0x60),reinterpret_cast<const float*>(p.object+0x620));
            std::memcpy(p.object+0x90,p.object+0x610,12);
            if(!MoveSkeleton(static_cast<unsigned char*>(p.model)+0xA0,reinterpret_cast<const float*>(p.object+0x60)))return false;
            if(!FlushRag(t,static_cast<unsigned char*>(p.rag)))return false;
        }
        if(!MoveSkeleton(o+0x6A0,reinterpret_cast<const float*>(o+0x60)))return false;if(!FlushRag(t,o+0xB60))return false;break;
    }
    }
    if(s.kind==PoseKind::large || s.kind==PoseKind::squid){alignas(16) float got[16]{};Fn<void(__fastcall*)(void*,float*)>(0x11AF6F0)(o+(s.kind==PoseKind::large ? 0xFF0 : 0xF90),got);if(!SameMatrix(got,m))return false;alignas(16) float v[4]{};Fn<void(__fastcall*)(void*,float*)>(0x11AF7E0)(o+(s.kind==PoseKind::large ? 0xFF0 : 0xF90),v);if(!Same3(v,s.linearVelocity))return false;}
    if(s.kind==PoseKind::ufo515){alignas(16) float got[16]{};if(!RigidMatrix(At<void*>(o,0xEA0),got) || !SameMatrix(got,m))return false;const float* v=Fn<const float*(__fastcall*)(void*)>(0x11B1300)(At<void*>(o,0xEA0));if(!Readable(v,12) || !Same3(v,s.linearVelocity))return false;}
    return true;
}
} // namespace

bool PoseRequired(const NativeLease& l) noexcept {PoseKind k{};return KindOf(l,&k);}
bool PoseReady(const NativeLease& l) noexcept {
    __try {
        PoseKind k{};unsigned secondary=0;if(!Identity(l) || !KindOf(l,&k,&secondary) || !Codes() || Existing(l))return false;
        // The primary table is intentionally cloned by the controller. Its retained profile and weak identity admit it;
        // the secondary native writer/reader must remain the captured class's, including copied tables with original COL.
        const auto net=At<const void* const*>(l.object,0x120);const auto stock=reinterpret_cast<const void* const*>(image+secondary);
        if(!Readable(net-1,9*8) || net[-1]!=stock[-1] || net[5]!=stock[5] || net[6]!=stock[6])return false;
        const unsigned extent=k==PoseKind::deiroi ? 0x161C : k==PoseKind::large ? 0x1480 : k==PoseKind::timeShip ? 0x1470 : k==PoseKind::squid ? 0x12DC : 0x1618;
        return Readable(l.object,extent,true);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool CapturePose(const NativeLease& l,void* out,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;if(!out || !used || capacity<pose::kWireBytes || !PoseReady(l))return false;
    __try {
        State s;KindOf(l,&s.kind);s.profile=l.profile->profileHash;auto* o=l.object;
        alignas(16) float m[16]{};
        if(s.kind==PoseKind::large || s.kind==PoseKind::squid)Fn<void(__fastcall*)(void*,float*)>(0x11AF6F0)(o+(s.kind==PoseKind::large ? 0xFF0 : 0xF90),m);
        else if(s.kind==PoseKind::ufo515){if(!Readable(o+0xEA0,8) || !RigidMatrix(At<void*>(o,0xEA0),m))return false;}
        else std::memcpy(m,o+0x60,64);
        if(!MatrixState(m,s))return false;
        switch(s.kind) {
        case PoseKind::large:
            std::memcpy(s.currentAngles,o+0xFC0,8);std::memcpy(s.targetAngles,o+0xFB0,8);Get3(o,0x1020,s.accumulatedVelocity);
            {alignas(16) float v[4]{};Fn<void(__fastcall*)(void*,float*)>(0x11AF7E0)(o+0xFF0,v);std::memcpy(s.linearVelocity,v,12);}
            if(!GetQ(o,0x1470,s.auxiliaryRotation) || !GetQ(o,0x1460,s.rotationStep) || !GetQ(o,0x1440,s.rotationDelta))return false;break;
        case PoseKind::squid:
            Get3(o,0x12C0,s.targetAngles);Get3(o,0x12D0,s.currentAngles);Get3(o,0xFB0,s.accumulatedVelocity);
            {alignas(16) float v[4]{};Fn<void(__fastcall*)(void*,float*)>(0x11AF7E0)(o+0xF90,v);std::memcpy(s.linearVelocity,v,12);}break;
        case PoseKind::ufo515: {
            std::memcpy(s.currentAngles,o+0x10B0,8);s.bank=At<float>(o,0x10A0);Get3(o,0x600,s.accumulatedVelocity);
            const auto* velocity=Fn<const float*(__fastcall*)(void*)>(0x11B1300)(At<void*>(o,0xEA0));if(!Readable(velocity,12))return false;std::memcpy(s.linearVelocity,velocity,12);break;
        }
        case PoseKind::timeShip:
            if(!GetQ(o,0xFB0,s.rotation) || !GetQ(o,0xFC0,s.targetRotation))return false;
            Get3(o,0x90,s.position);Get3(o,0xF90,s.linearVelocity);Get3(o,0xFA0,s.accumulatedVelocity);
            std::memcpy(s.deformation,o+0x1430,64);s.phase=At<float>(o,0xFD0);s.scale=At<float>(o,0xFD8);break;
        case PoseKind::deiroi:std::memcpy(s.targetAngles,o+0xF40,8);Get3(o,0x1610,s.linearVelocity);break;
        }
        if(!pose::Encode(s,out,pose::kWireBytes))return false;*used=pose::kWireBytes;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ApplyPose(NativeLease& l,const void* bytes,std::size_t size) noexcept {
    State state;PoseKind kind{};if(!pose::Decode(bytes,size,&state) || !KindOf(l,&kind) || kind!=state.kind ||
        !l.profile || l.profile->profileHash!=state.profile || !PoseReady(l))return false;
    l.restoreGate=&RestorePose;auto* t=Start(l);if(!t){l.restoreGate=nullptr;return false;}bool ok=false;
    __try {DeiroiData d;if(Preflight(l,kind,*t,d))ok=Apply(l,state,*t,d);}
    __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    if(ok){if(!Clear(*t))return false;l.restoreGate=nullptr;return true;}
    if(!t->changed)Clear(*t);else {t->pending=true;Rollback(*t);}if(!t->used)l.restoreGate=nullptr;return false;
}
bool RestorePose(NativeLease& l) noexcept {
    if(auto* t=Existing(l)){if(!Rollback(*t))return false;}l.restoreGate=nullptr;return true;
}
} // namespace crew::enemy_native
