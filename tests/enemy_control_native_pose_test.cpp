// Executes the production pose adapter with real synthetic native layouts and recording physics functions.
#define ENEMY_CONTROL_POSE_TEST
#include "../src/enemy_control_native_pose.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

namespace crew {unsigned char* image=nullptr;}
namespace {
using crew::At;using crew::Put;
using crew::enemy_native::NativeLease;using crew::enemy_native::NativeProfile;
using crew::enemy_native::PoseReady;using crew::enemy_native::CapturePose;using crew::enemy_native::ApplyPose;
using crew::enemy_native::RestorePose;using crew::enemy_native::pose::Kind;using crew::enemy_native::pose::State;
using Wire=std::array<unsigned char,crew::enemy_native::pose::kWireBytes>;
int currentKind=0,checks=0,failures=0,mutations=0,modelBuilds=0,ragReads=0,ragWrites=0,managerFlushes=0;
void Check(bool good,const char* expression,int line) {
    ++checks;if(!good){++failures;std::fprintf(stderr,"kind %d line %d: %s\n",currentKind,line,expression);}
}
#define CHECK(good) Check((good),#good,__LINE__)
bool Close(float a,float b){return std::fabs(a-b)<0.003f;}
bool Close3(const float* a,const float* b){return Close(a[0],b[0])&&Close(a[1],b[1])&&Close(a[2],b[2]);}
void Identity(float* matrix,float x=0,float y=0,float z=0) {
    std::memset(matrix,0,64);matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;matrix[12]=x;matrix[13]=y;matrix[14]=z;
}
struct Physical {
    alignas(16) float matrix[16]{};unsigned reserved[4]{};std::uint32_t motionId=0;unsigned reserved2[3]{};
    float linear[4]{},angular[4]{};
};
static_assert(offsetof(Physical,motionId)==0x50 && offsetof(Physical,linear)==0x60 && offsetof(Physical,angular)==0x70);
// Real backing pages are independent of their body header; queries expose these native offsets.
struct PhysicalStorage {
    std::unordered_map<void*,Physical*> values;
    Physical* find(void* body) const {const auto i=values.find(body);return i==values.end() ? nullptr : i->second;}
    Physical& at(void* body) const {return *values.at(body);}
    void emplace(void* body,Physical* state){values.emplace(body,state);}
    void erase(void* body){values.erase(body);}
    bool empty() const {return values.empty();}
} physical;
std::unordered_map<std::uint32_t,void*> bodyIds;
std::unordered_map<std::uint32_t,unsigned char*> descriptors;
std::unordered_map<void*,unsigned char*> managers;
enum class Fault {none,onceTransform,persistentTransform,onceLinear,persistentLinear};
Fault fault=Fault::none;
Physical* Find(void* body) {
    auto* state=physical.find(body);if(!state){CHECK(false);return nullptr;}return state;
}
void SetTransform(void* body,const float* matrix) {
    ++mutations;auto* state=Find(body);if(!state)return;
    if(fault==Fault::onceTransform){fault=Fault::none;return;}
    std::memcpy(state->matrix,matrix,64);
    if(fault==Fault::persistentTransform)state->matrix[12]+=777.0f;
    for(const auto& entry:bodyIds)if(entry.second==body)std::memcpy(descriptors.at(entry.first),state->matrix,64);
}
void SetLinear(void* body,const float* vector) {
    ++mutations;auto* state=Find(body);if(!state)return;
    if(fault==Fault::onceLinear){fault=Fault::none;return;}
    std::memcpy(state->linear,vector,12);state->linear[3]=0;
    if(fault==Fault::persistentLinear)state->linear[0]+=777.0f;
}
void __fastcall Quaternion(float* out,const float* matrix) {
    const float trace=matrix[0]+matrix[5]+matrix[10];
    if(trace>=0) {
        const float scalar=2.0f*std::sqrt(trace+1.0f);
        out[0]=(matrix[6]-matrix[9])/scalar;out[1]=(matrix[8]-matrix[2])/scalar;
        out[2]=(matrix[1]-matrix[4])/scalar;out[3]=scalar*0.25f;
    } else {out[0]=out[1]=out[2]=0;out[3]=1;}
}
void __fastcall Rotation(float* matrix,const float* q) {
    Identity(matrix);const float x=q[0],y=q[1],z=q[2],w=q[3];
    matrix[0]=1-2*(y*y+z*z);matrix[1]=2*(x*y+z*w);matrix[2]=2*(x*z-y*w);
    matrix[4]=2*(x*y-z*w);matrix[5]=1-2*(x*x+z*z);matrix[6]=2*(y*z+x*w);
    matrix[8]=2*(x*z+y*w);matrix[9]=2*(y*z-x*w);matrix[10]=1-2*(x*x+y*y);
}
void __fastcall CharacterMatrix(void* wrapper,float* out) {
    if(auto* state=Find(At<void*>(wrapper,0)))std::memcpy(out,state->matrix,64);
}
void __fastcall CharacterLinear(void* wrapper,float* out) {
    if(auto* state=Find(At<void*>(wrapper,0)))std::memcpy(out,state->linear,16);
}
void __fastcall CharacterWarp(void* wrapper,const float* matrix){SetTransform(At<void*>(wrapper,0),matrix);}
void __fastcall CharacterSetLinear(void* wrapper,const float* vector){SetLinear(At<void*>(wrapper,0),vector);}
void __fastcall WorldSetTransform(void*,std::uint32_t id,const float* matrix,const double* position,int) {
    const auto found=bodyIds.find(id);CHECK(found!=bodyIds.end());if(found==bodyIds.end())return;
    const bool noWrite=fault==Fault::onceTransform;alignas(16) float nativeMatrix[16]{};std::memcpy(nativeMatrix,matrix,64);
    for(unsigned i=0;i<3;++i)nativeMatrix[12+i]=static_cast<float>(position[i]);
    SetTransform(found->second,nativeMatrix);if(noWrite)return;
    auto* raw=descriptors.at(id);for(unsigned i=0;i<3;++i)Put<float>(raw,0x40+i*4,static_cast<float>(position[i]-At<float>(raw,0x30+i*4)));
}
const void* __fastcall WorldDescriptor(void*,std::uint32_t id) {
    const auto found=descriptors.find(id);if(found==descriptors.end()){CHECK(false);return nullptr;}return found->second;
}
const void* __fastcall WorldMatrix(void*,std::uint32_t id) {
    const auto it=bodyIds.find(id);if(it==bodyIds.end()){CHECK(false);return nullptr;}
    return descriptors.at(id);
}
const void* __fastcall WorldVelocity(void*,std::uint32_t id) {
    const auto it=bodyIds.find(id);if(it==bodyIds.end()){CHECK(false);return nullptr;}auto* state=Find(it->second);return state ? state->linear : nullptr;
}
const void* __fastcall WorldMotion(void*,std::uint32_t id) {
    const auto it=bodyIds.find(id);if(it==bodyIds.end()){CHECK(false);return nullptr;}return Find(it->second);
}
void __fastcall MatrixCopy(const void* source,float* out){std::memcpy(out,source,64);}
const float* __fastcall RigidLinear(void* body){auto* state=Find(body);return state ? state->linear : nullptr;}
void __fastcall RigidAngular(void* body,float* out){if(auto* state=Find(body))std::memcpy(out,state->angular,16);}
void __fastcall RigidWarpWrapper(void* wrapper,const float* matrix){SetTransform(At<void*>(wrapper,0),matrix);}
void __fastcall RigidWarp(void* body,const float* matrix,bool){SetTransform(body,matrix);}
void __fastcall RigidSetLinear(void* body,const float* vector){SetLinear(body,vector);}
void __fastcall RigidSetAngular(void* body,const float* vector) {
    ++mutations;if(auto* state=Find(body)){std::memcpy(state->angular,vector,12);state->angular[3]=0;}
}
void __fastcall BuildModel(void* object,const float* matrix) {
    ++mutations;++modelBuilds;auto* model=static_cast<unsigned char*>(object);model[0xB0]=1;
    auto* nodes=At<unsigned char*>(model,0x10);const auto count=At<std::uint64_t>(model,0x20);
    for(std::size_t i=0;i<count;++i) {
        std::memcpy(nodes+i*0x110+0xB0,matrix,64);
        Put<float>(nodes+i*0x110,0xE0,matrix[12]+static_cast<float>(i));
    }
}
void __fastcall StageShip(void* object) {
    ++mutations;auto* o=static_cast<unsigned char*>(object);std::memcpy(o+0x860,o+0x60,64);
    Put<float>(o,0x898,At<float>(o,0x898)+At<float>(o,0xFD0));
}
void __fastcall RagTarget(void* object,float* out) {
    ++ragReads;auto* record=static_cast<unsigned char*>(object);
    auto* bind=At<unsigned char*>(record,8);auto* link=At<unsigned char*>(bind,0x40);
    auto* bone=At<unsigned char*>(link,0);std::memcpy(out,bone+0xB0,64);
}
void __fastcall RagSet(void* object,const float* matrix) {
    ++mutations;++ragWrites;auto* record=static_cast<unsigned char*>(object);
    if(At<unsigned>(record,0x9C)==2)SetTransform(At<void*>(record,0x50),matrix);
    else {
        auto* node=At<unsigned char*>(record,0xB0);std::memcpy(node+0x40,matrix,64);
        std::memset(node+0x80,0x62,32);node[0xA0]=1;
    }
}
void __fastcall ManagerFlush(void* object) {
    ++mutations;++managerFlushes;const auto found=managers.find(object);CHECK(found!=managers.end());
    if(found==managers.end())return;auto* rag=found->second;
    auto* records=At<unsigned char*>(rag,0x78);const auto count=At<std::uint64_t>(rag,0x88);
    for(std::size_t i=0;i<count;++i) {
        auto* record=records+i*0xC0;
        if(At<unsigned>(record,0x9C)!=1 || !record[0xA4])continue;
        auto* node=At<unsigned char*>(record,0xB0);
        SetTransform(At<void*>(node,0x10),reinterpret_cast<const float*>(node+0x40));node[0xA0]=0;
    }
}
struct Allocation {unsigned char* memory=nullptr;std::size_t size=0;};
struct Snapshot {
    std::vector<std::vector<unsigned char>> memory;
    std::vector<std::pair<void*,Physical>> physics;
};
struct Fixture {
    std::vector<Allocation> allocations;
    std::vector<void*> bodies;
    std::vector<unsigned char*> rags;
    unsigned char* object=nullptr;unsigned char* control=nullptr;unsigned char* character=nullptr;
    unsigned char* rootNodes=nullptr;unsigned char* child=nullptr;
    NativeProfile profile{};NativeLease lease{};Kind kind;
    explicit Fixture(Kind selected):kind(selected) {
        currentKind=static_cast<int>(selected);
        object=Allocate(0x4000);control=Allocate();Put<LONG>(control,8,1);Put<LONG>(control,12,1);Put<void*>(object,0x30,control);
        const unsigned primary=selected==Kind::deiroi ? 0x17AA648u : selected==Kind::large ? 0x17AFEE0u :
            selected==Kind::timeShip ? 0x17B93C0u : selected==Kind::squid ? 0x17B8898u : 0x17CC838u;
        const unsigned secondary=selected==Kind::deiroi ? 0x17AA7B8u : selected==Kind::large ? 0x17B00A0u :
            selected==Kind::timeShip ? 0x17B9580u : selected==Kind::squid ? 0x17B8A58u : 0x17CC9A8u;
        profile.name="pose fixture";profile.vtableRva=primary;profile.profileHash=0xBA000000u+static_cast<unsigned>(selected);
        auto* clone=Allocate();Put<void*>(object,0,clone+8); // Actual controller primary is cloned, not the stock table.
        auto* net=Allocate();std::memcpy(net,crew::image+secondary-8,9*8);Put<void*>(object,0x120,net+8);
        lease.object=object;lease.control=control;lease.profile=&profile;
        lease.savedPad=object+0x300;lease.savedPlayer=7;lease.previousHeld=0x55;
        Identity(reinterpret_cast<float*>(object+0x60),10,20,30);
        if(selected==Kind::large || selected==Kind::squid) {
            const unsigned off=selected==Kind::large ? 0xFF0u : 0xF90u;
            character=Allocate();Put<void*>(object,off,character);
            BuildPhysical(character,reinterpret_cast<const float*>(object+0x60),true);
        }
        if(selected==Kind::large) {
            Put<float>(object,0xFB0,0.25f);Put<float>(object,0xFB4,0.5f);
            Put<float>(object,0xFC0,0.125f);Put<float>(object,0xFC4,0.375f);
            Put<float>(object,0x1020,1);Put<float>(object,0x1024,2);Put<float>(object,0x1028,3);
            for(unsigned off:{0x1440u,0x1460u,0x1470u})Put<float>(object,off+12,1);
        } else if(selected==Kind::squid) {
            for(unsigned i=0;i<3;++i){Put<float>(object,0x12C0+i*4,static_cast<float>(i+1)*0.125f);Put<float>(object,0x12D0+i*4,static_cast<float>(i+1)*0.25f);Put<float>(object,0xFB0+i*4,static_cast<float>(i+4));}
        } else if(selected==Kind::ufo515) {
            auto* body=Allocate();BuildPhysical(body,reinterpret_cast<const float*>(object+0x60));Put<void*>(object,0xEA0,body);
            std::memcpy(object+0x5C0,object+0x60,64);Put<float>(object,0x600,1);Put<float>(object,0x604,2);Put<float>(object,0x608,3);
            Put<float>(object,0x10A0,0.125f);Put<float>(object,0x10B0,0.25f);Put<float>(object,0x10B4,0.375f);
        } else if(selected==Kind::timeShip) {
            std::memcpy(object+0x860,object+0x60,64);Put<float>(object,0xFBC,1);Put<float>(object,0xFCC,1);
            Put<float>(object,0xF90,1);Put<float>(object,0xF94,2);Put<float>(object,0xF98,3);
            Put<float>(object,0xFA0,4);Put<float>(object,0xFA4,5);Put<float>(object,0xFA8,6);
            Put<float>(object,0xFD0,0.25f);Put<float>(object,0xFD8,1);Identity(reinterpret_cast<float*>(object+0x1430));
            rootNodes=Skeleton(object+0x940,reinterpret_cast<const float*>(object+0x860));Rag(object+0xD80,rootNodes);
        } else {
            Put<float>(object,0xF40,0.25f);Put<float>(object,0xF44,0.5f);Put<float>(object,0x1610,1);Put<float>(object,0x1614,2);Put<float>(object,0x1618,3);
            auto* points=Allocate();auto* history=Allocate();Put<void*>(object,0xB08,points);Put<void*>(object,0xB28,history);
            Put<std::uint64_t>(object,0xB18,2);Put<std::uint64_t>(object,0xB38,2);
            for(unsigned i=0;i<2;++i) {IdentityPoint(points+i*0x30,12+static_cast<float>(i),20,30);IdentityPoint(history+i*0x30,10,21,33+static_cast<float>(i));}
            rootNodes=Skeleton(object+0x6A0,reinterpret_cast<const float*>(object+0x60));Rag(object+0xB60,rootNodes);
            auto* list=Allocate();Put<void*>(object,0xF60,list);Put<std::uint64_t>(object,0xF70,1);
            child=Allocate();auto* childControl=Allocate();Put<LONG>(childControl,8,1);Put<LONG>(childControl,12,1);Put<void*>(child,0x30,childControl);Put<void*>(child,0x5C0,object);
            Put<void*>(list,0,child);Put<void*>(list,8,childControl);Identity(reinterpret_cast<float*>(child+0x60),14,20,30);
            IdentityPoint(child+0x610,14,20,30);Put<float>(child,0x62C,1);
            auto* model=Allocate();auto* rag=Allocate();auto* collision=Allocate();Put<void*>(child,0x7A0,model);Put<void*>(child,0x790,rag);Put<void*>(child,0x7B0,collision);
            auto* nodes=Skeleton(model+0xA0,reinterpret_cast<const float*>(child+0x60));Rag(rag,nodes);
        }
    }
    ~Fixture() {
        CHECK(RestorePose(lease));
        for(void* body:bodies) {
            physical.erase(body);
            for(auto it=bodyIds.begin();it!=bodyIds.end();) {if(it->second==body){descriptors.erase(it->first);it=bodyIds.erase(it);}else ++it;}
        }
        for(auto* rag:rags)managers.erase(rag+0x110);
        for(const auto& allocation:allocations)VirtualFree(allocation.memory,0,MEM_RELEASE);
    }
    unsigned char* Allocate(std::size_t size=0x1000) {
        auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!block)std::exit(2);allocations.push_back({block,size});return block;
    }
    static void IdentityPoint(unsigned char* out,float x,float y,float z) {Put<float>(out,0,x);Put<float>(out,4,y);Put<float>(out,8,z);Put<float>(out,12,1);}
    void BuildPhysical(unsigned char* body,const float* matrix,bool characterBody=false) {
        Physical state;std::memcpy(state.matrix,matrix,64);state.linear[0]=0.5f;state.linear[1]=1;state.linear[2]=1.5f;state.angular[1]=0.125f;
        auto* backing=reinterpret_cast<Physical*>(Allocate());std::memcpy(backing,&state,sizeof state);physical.emplace(body,backing);bodies.push_back(body);auto* table=Allocate();
        for(unsigned i=0;i<32;++i)Put<void*>(table,i*8,reinterpret_cast<void*>(&WorldMatrix));
        Put<void*>(table,0x80,reinterpret_cast<void*>(&WorldDescriptor));Put<void*>(table,0xA0,reinterpret_cast<void*>(&WorldVelocity));Put<void*>(table,0xF8,reinterpret_cast<void*>(&WorldMotion));
        auto* writeTable=Allocate();for(unsigned i=0;i<32;++i)Put<void*>(writeTable,i*8,reinterpret_cast<void*>(&WorldSetTransform));
        auto* world=Allocate();auto* service=Allocate();Put<void*>(body,0x100,world);Put<void*>(world,0x58,service);
        Put<void*>(service,0x18,writeTable);Put<void*>(service,0x20,table);
        const auto id=static_cast<std::uint32_t>(bodyIds.size()+1);bodyIds[id]=body;Put<std::uint32_t>(body,0xF0,id);
        auto* descriptor=Allocate();std::memcpy(descriptor,backing->matrix,64);Put<std::uint32_t>(descriptor,0x80,id);descriptors[id]=descriptor;
        if(characterBody) {Put<std::uint32_t>(body,0x18,id);auto* query=Allocate();auto* write=Allocate();Put<void*>(query,0,table);Put<void*>(write,0,writeTable);Put<void*>(body,0x28,query);Put<void*>(body,0x30,write);}
    }
    unsigned char* Skeleton(unsigned char* model,const float* matrix) {
        auto* nodes=Allocate();Put<void*>(model,0x10,nodes);Put<std::uint64_t>(model,0x20,2);
        for(unsigned i=0;i<2;++i){std::memcpy(nodes+i*0x110+0xB0,matrix,64);Put<float>(nodes+i*0x110,0xE0,matrix[12]+static_cast<float>(i));}
        return nodes;
    }
    void Rag(unsigned char* rag,unsigned char* nodes) {
        rags.push_back(rag);managers[rag+0x110]=rag;auto* records=Allocate();Put<void*>(rag,0x78,records);Put<std::uint64_t>(rag,0x88,2);
        for(unsigned i=0;i<2;++i) {
            auto* record=records+i*0xC0;auto* bind=Allocate();auto* link=Allocate();auto* body=Allocate();
            Put<void*>(record,8,bind);Put<void*>(bind,0x40,link);Put<void*>(link,0,nodes+i*0x110);record[0xA4]=1;
            BuildPhysical(body,reinterpret_cast<const float*>(nodes+i*0x110+0xB0));
            Put<unsigned>(record,0x9C,i+1);
            if(i==0){auto* node=Allocate();Put<void*>(record,0xB0,node);Put<void*>(record,0xB8,rag+0x110);Put<void*>(node,0x10,body);std::memcpy(node+0x40,nodes+0xB0,64);}
            else Put<void*>(record,0x50,body);
        }
    }
    Snapshot Before() const {
        Snapshot snapshot;
        for(const auto& allocation:allocations)snapshot.memory.emplace_back(allocation.memory,allocation.memory+allocation.size);
        for(void* body:bodies)snapshot.physics.emplace_back(body,physical.at(body));
        return snapshot;
    }
    bool Unchanged(const Snapshot& snapshot) const {
        if(snapshot.memory.size()!=allocations.size())return false;
        for(std::size_t i=0;i<allocations.size();++i)if(std::memcmp(allocations[i].memory,snapshot.memory[i].data(),allocations[i].size))return false;
        for(const auto& pair:snapshot.physics) {const auto* found=physical.find(pair.first);if(!found || std::memcmp(found,&pair.second,sizeof(Physical)))return false;}
        return true;
    }
    State Captured() const {
        Wire wire{};std::size_t used=0;CHECK(CapturePose(lease,wire.data(),wire.size(),&used));CHECK(used==wire.size());
        State state;CHECK(crew::enemy_native::pose::Decode(wire.data(),used,&state));return state;
    }
    State Incoming() const {
        State state=Captured();state.position[0]=100;state.position[1]=200;state.position[2]=300;
        state.rotation[0]=state.rotation[2]=0;state.rotation[1]=state.rotation[3]=std::sqrt(0.5f);
        for(unsigned i=0;i<3;++i)state.linearVelocity[i]=static_cast<float>(i+7);
        switch(kind) {
        case Kind::deiroi:state.targetAngles[0]=0.625f;state.targetAngles[1]=0.875f;break;
        case Kind::large:state.currentAngles[0]=0.625f;state.currentAngles[1]=0.875f;state.targetAngles[0]=1.125f;state.targetAngles[1]=1.375f;
            for(unsigned i=0;i<3;++i)state.accumulatedVelocity[i]=static_cast<float>(i+10);break;
        case Kind::timeShip:state.phase=0.75f;state.scale=1.25f;state.deformation[12]=2;
            for(unsigned i=0;i<3;++i)state.accumulatedVelocity[i]=static_cast<float>(i+10);break;
        case Kind::squid:for(unsigned i=0;i<3;++i){state.currentAngles[i]=static_cast<float>(i+1)*0.5f;state.targetAngles[i]=static_cast<float>(i+1)*0.75f;state.accumulatedVelocity[i]=static_cast<float>(i+10);}break;
        case Kind::ufo515:state.currentAngles[0]=0.625f;state.currentAngles[1]=0.875f;state.bank=1.125f;
            for(unsigned i=0;i<3;++i)state.accumulatedVelocity[i]=static_cast<float>(i+10);break;
        }
        return state;
    }
    Wire WireFor(const State& state) const {Wire wire{};CHECK(crew::enemy_native::pose::Encode(state,wire.data(),wire.size()));return wire;}
    void* WritableLeaf() const {
        if(kind==Kind::large || kind==Kind::squid)return character;
        if(kind==Kind::ufo515)return At<void*>(object,0xEA0);
        if(kind==Kind::timeShip)return rootNodes;
        return At<void*>(static_cast<unsigned char*>(At<void*>(child,0x7A0))+0xA0,0x10);
    }
    void BreakDeepReference() {
        if(kind==Kind::large || kind==Kind::squid){Put<void*>(character,0x28,nullptr);return;}
        if(kind==Kind::ufo515) {
            auto* body=At<unsigned char*>(object,0xEA0);auto* world=At<unsigned char*>(body,0x100);
            auto* service=At<unsigned char*>(world,0x58);Put<void*>(service,0x20,nullptr);return;
        }
        auto* rag=object+(kind==Kind::deiroi ? 0xB60 : 0xD80);
        auto* records=At<unsigned char*>(rag,0x78);auto* bind=At<unsigned char*>(records,8);Put<void*>(bind,0x40,nullptr);
    }
    void BreakReference() {
        switch(kind) {
        case Kind::deiroi:Put<void*>(object,0xB08,nullptr);break;
        case Kind::large:Put<void*>(object,0xFF0,nullptr);break;
        case Kind::squid:Put<void*>(object,0xF90,nullptr);break;
        case Kind::ufo515:Put<void*>(At<void*>(object,0xEA0),0x100,nullptr);break;
        case Kind::timeShip:Put<void*>(object,0x950,nullptr);break;
        }
    }
};
void BuildImage() {
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!crew::image)std::exit(2);
    for(const auto& signature:crew::enemy_native::kSigs)std::memcpy(crew::image+signature.rva,signature.bytes,sizeof signature.bytes);
    unsigned index=0;
    for(unsigned secondary:{0x17AA7B8u,0x17B00A0u,0x17B9580u,0x17B8A58u,0x17CC9A8u}) {
        Put<void*>(crew::image+secondary-8,0,crew::image+0x10000+index*0x100);
        Put<void*>(crew::image+secondary,5*8,crew::image+0x2000+index*0x100);
        Put<void*>(crew::image+secondary,6*8,crew::image+0x3000+index*0x100);++index;
    }
}
void DrainCheck() {for(const auto& transaction:crew::enemy_native::transactions)CHECK(!transaction.used && transaction.stageRag==nullptr);}
void Success(Kind kind) {
    Fixture fixture(kind);CHECK(PoseReady(fixture.lease));const State incoming=fixture.Incoming();const Wire wire=fixture.WireFor(incoming);
    const int builds=modelBuilds,reads=ragReads,writes=ragWrites,flushes=managerFlushes;
    CHECK(ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(fixture.lease.restoreGate==nullptr);CHECK(crew::enemy_native::Existing(fixture.lease)==nullptr);
    const State captured=fixture.Captured();CHECK(Close3(captured.position,incoming.position));CHECK(Close3(captured.linearVelocity,incoming.linearVelocity));
    CHECK(Close(captured.rotation[1],incoming.rotation[1]) && Close(captured.rotation[3],incoming.rotation[3]));
    if(kind==Kind::deiroi) {
        auto* points=At<float*>(fixture.object,0xB08);CHECK(Close(points[0],100) && Close(points[1],200) && Close(points[2],298));
        CHECK(Close(At<float>(fixture.child,0x610),100) && Close(At<float>(fixture.child,0x618),296));
        CHECK(modelBuilds>=builds+2 && ragReads>=reads+4 && ragWrites>=writes+4 && managerFlushes>=flushes+2);
    } else if(kind==Kind::timeShip)CHECK(modelBuilds>builds && ragReads>=reads+2 && ragWrites>=writes+2 && managerFlushes>flushes);
    else if(kind==Kind::large || kind==Kind::squid)CHECK(Close3(physical.at(fixture.character).matrix+12,incoming.position));
    else CHECK(Close3(physical.at(At<void*>(fixture.object,0xEA0)).matrix+12,incoming.position));
    CHECK(RestorePose(fixture.lease));DrainCheck();
}
void PreflightCases(Kind kind) {
    {
        Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());fixture.BreakDeepReference();const Snapshot before=fixture.Before();const int was=mutations;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was && fixture.Unchanged(before));CHECK(!fixture.lease.restoreGate);DrainCheck();
    }
    {
        Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot before=fixture.Before();const int was=mutations;DWORD previous=0;
        void* const leaf=fixture.WritableLeaf();CHECK(VirtualProtect(leaf,0x1000,PAGE_READONLY,&previous)!=0);Sleep(20);
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was && fixture.Unchanged(before));CHECK(!fixture.lease.restoreGate);
        DWORD discarded=0;CHECK(VirtualProtect(leaf,0x1000,previous,&discarded)!=0);Sleep(20);DrainCheck();
    }
    {
        Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());fixture.BreakReference();const Snapshot before=fixture.Before();const int was=mutations;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was);CHECK(fixture.Unchanged(before));CHECK(!fixture.lease.restoreGate);DrainCheck();
    }
    {
        Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot before=fixture.Before();const int was=mutations;DWORD previous=0;
        CHECK(VirtualProtect(fixture.object,0x4000,PAGE_READONLY,&previous)!=0);Sleep(20);
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was);CHECK(fixture.Unchanged(before));
        DWORD discarded=0;CHECK(VirtualProtect(fixture.object,0x4000,previous,&discarded)!=0);Sleep(20);DrainCheck();
    }
    {
        Fixture fixture(kind);State incoming=fixture.Incoming();incoming.profile^=1u;const Wire wire=fixture.WireFor(incoming);const Snapshot before=fixture.Before();const int was=mutations;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was && fixture.Unchanged(before));DrainCheck();
    }
}
void RollbackCases(Kind kind) {
    {
        Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot before=fixture.Before();fault=Fault::onceTransform;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));fault=Fault::none;
        CHECK(fixture.Unchanged(before));CHECK(fixture.lease.restoreGate==nullptr);DrainCheck();
    }
    {
        Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot before=fixture.Before();fault=Fault::persistentTransform;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));
        auto* journal=crew::enemy_native::Existing(fixture.lease);CHECK(journal && journal->used && journal->pending && journal->writes && journal->bodies);
        CHECK(fixture.lease.restoreGate==&RestorePose);CHECK(!RestorePose(fixture.lease));CHECK(fixture.lease.restoreGate==&RestorePose);
        const unsigned entries=journal ? journal->writes : 0;const int was=mutations;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was);
        CHECK(journal && journal->writes==entries && fixture.lease.object==fixture.object && fixture.lease.control==fixture.control && fixture.lease.profile==&fixture.profile);
        CHECK(fixture.lease.savedPlayer==7 && fixture.lease.previousHeld==0x55 && fixture.lease.savedPad==fixture.object+0x300);
        fault=Fault::none;CHECK(RestorePose(fixture.lease));CHECK(fixture.lease.restoreGate==nullptr);CHECK(fixture.Unchanged(before));DrainCheck();
    }
}
void ReplacementLifecycles() {
    {
        Fixture fixture(Kind::deiroi);const Wire wire=fixture.WireFor(fixture.Incoming());fault=Fault::persistentTransform;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));auto* journal=crew::enemy_native::Existing(fixture.lease);
        CHECK(journal && journal->pending);auto* oldControl=At<unsigned char*>(fixture.child,0x30);
        const LONG held=At<LONG>(oldControl,12);CHECK(held==2);
        std::vector<std::pair<void*,Physical>> oldBodies;
        if(journal)for(unsigned i=0;i<journal->bodies;++i)if(journal->body[i].owner)oldBodies.emplace_back(journal->body[i].object,physical.at(journal->body[i].object));
        Put<LONG>(oldControl,8,0);auto* replacementControl=fixture.Allocate();Put<LONG>(replacementControl,8,1);Put<LONG>(replacementControl,12,1);
        std::memset(fixture.child,0x3C,0x1000);fixture.child[0x18]=fixture.child[0x2E8]=0;Put<void*>(fixture.child,0x30,replacementControl);
        const std::vector<unsigned char> replacement(fixture.child,fixture.child+0x1000);
        fault=Fault::none;CHECK(RestorePose(fixture.lease));CHECK(fixture.lease.restoreGate==nullptr);
        CHECK(std::memcmp(fixture.child,replacement.data(),replacement.size())==0);
        CHECK(At<LONG>(oldControl,12)==held-1 && At<LONG>(replacementControl,12)==1);
        for(const auto& body:oldBodies)CHECK(std::memcmp(&physical.at(body.first),&body.second,sizeof(Physical))==0);
        DrainCheck();
    }
    for(unsigned replacement=0;replacement<2;++replacement) {
        Fixture fixture(Kind::deiroi);const Wire wire=fixture.WireFor(fixture.Incoming());fault=Fault::persistentTransform;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));auto* journal=crew::enemy_native::Existing(fixture.lease);
        CHECK(journal && journal->pending && journal->bodies>=2);if(!journal){fault=Fault::none;continue;}
        auto& victim=journal->body[0];CHECK(victim.owner!=0 && !victim.character);
        auto* body=static_cast<unsigned char*>(victim.object);auto* rag=At<unsigned char*>(fixture.child,0x790);
        auto* oldRecords=At<unsigned char*>(rag,0x78);auto* proxy=At<unsigned char*>(oldRecords,0xB0);
        auto* newRecords=fixture.Allocate();std::memcpy(newRecords,oldRecords,0x180);Put<void*>(rag,0x78,newRecords);
        Physical recycled=physical.at(body);Identity(recycled.matrix,999,321,654);recycled.linear[0]=99;physical.at(body)=recycled;
        std::memset(proxy+0x40,0x47,0x61);const std::vector<unsigned char> proxyReplacement(proxy+0x40,proxy+0xA1);
        if(replacement==0) {
            // Same body pointer is now another native body ID/world, still present in the moved record container.
            Put<unsigned>(body,0xF0,victim.id+0x10000000u);Put<void*>(body,0x100,fixture.Allocate());
        } else {
            // Body ID/world remained the same, but neither old body belongs to the owner's new current rag records.
            std::memset(newRecords,0,0x180);newRecords[0xA4]=1;Put<unsigned>(newRecords,0x9C,2);Put<void*>(newRecords,0x50,fixture.Allocate());Put<std::uint64_t>(rag,0x88,1);
        }
        const std::vector<unsigned char> replacementRecords(newRecords,newRecords+0x180);
        fault=Fault::none;CHECK(RestorePose(fixture.lease));CHECK(fixture.lease.restoreGate==nullptr);
        CHECK(std::memcmp(&physical.at(body),&recycled,sizeof(Physical))==0);
        CHECK(std::memcmp(proxy+0x40,proxyReplacement.data(),proxyReplacement.size())==0);
        CHECK(std::memcmp(newRecords,replacementRecords.data(),replacementRecords.size())==0);
        CHECK(At<LONG>(At<void*>(fixture.child,0x30),12)==1);DrainCheck();
    }
    {
        Fixture fixture(Kind::deiroi);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot baseline=fixture.Before();fault=Fault::persistentTransform;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));auto* childControl=At<unsigned char*>(fixture.child,0x30);CHECK(At<LONG>(childControl,12)==2);
        DWORD previous=0;CHECK(VirtualProtect(childControl,0x1000,PAGE_READONLY,&previous)!=0);Sleep(20);fault=Fault::none;
        CHECK(!RestorePose(fixture.lease));CHECK(fixture.lease.restoreGate==&RestorePose);CHECK(crew::enemy_native::Existing(fixture.lease)!=nullptr);
        CHECK(At<LONG>(childControl,12)==2);
        DWORD discarded=0;CHECK(VirtualProtect(childControl,0x1000,previous,&discarded)!=0);Sleep(20);
        CHECK(RestorePose(fixture.lease));CHECK(fixture.lease.restoreGate==nullptr && fixture.Unchanged(baseline));DrainCheck();
    }
}
void PendingUnreadableRoot(Kind kind) {
    Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot baseline=fixture.Before();fault=Fault::persistentTransform;
    CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(crew::enemy_native::Existing(fixture.lease)!=nullptr);
    DWORD previous=0;CHECK(VirtualProtect(fixture.object,0x4000,PAGE_NOACCESS,&previous)!=0);Sleep(20);fault=Fault::none;
    const int was=mutations;CHECK(!RestorePose(fixture.lease));CHECK(mutations==was);
    CHECK(fixture.lease.restoreGate==&RestorePose && crew::enemy_native::Existing(fixture.lease)!=nullptr);
    DWORD discarded=0;CHECK(VirtualProtect(fixture.object,0x4000,previous,&discarded)!=0);Sleep(20);
    CHECK(RestorePose(fixture.lease));CHECK(fixture.lease.restoreGate==nullptr && fixture.Unchanged(baseline));DrainCheck();
}
void PreflightUnreadablePart() {
    for(unsigned which=0;which<2;++which) {
        Fixture fixture(Kind::deiroi);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot baseline=fixture.Before();
        auto* childControl=At<unsigned char*>(fixture.child,0x30);void* inaccessible=which ? static_cast<void*>(childControl) : fixture.child;
        const int was=mutations;DWORD previous=0;CHECK(VirtualProtect(inaccessible,0x1000,PAGE_NOACCESS,&previous)!=0);Sleep(20);
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was);CHECK(!fixture.lease.restoreGate);DrainCheck();
        DWORD discarded=0;CHECK(VirtualProtect(inaccessible,0x1000,previous,&discarded)!=0);Sleep(20);
        CHECK(fixture.Unchanged(baseline));CHECK(At<LONG>(childControl,12)==1);
    }
}
void RetiredPoseStorage() {
    {
        Fixture fixture(Kind::deiroi);const Wire wire=fixture.WireFor(fixture.Incoming());fault=Fault::persistentTransform;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(crew::enemy_native::Existing(fixture.lease)!=nullptr);
        auto* oldPoints=At<unsigned char*>(fixture.object,0xB08);auto* oldHistory=At<unsigned char*>(fixture.object,0xB28);
        auto* newPoints=fixture.Allocate();auto* newHistory=fixture.Allocate();Put<void*>(fixture.object,0xB08,newPoints);Put<void*>(fixture.object,0xB28,newHistory);
        std::memset(oldPoints,0x71,0x60);std::memset(oldHistory,0x72,0x60);std::memset(newPoints,0x73,0x60);std::memset(newHistory,0x74,0x60);
        fault=Fault::none;CHECK(RestorePose(fixture.lease));
        for(unsigned i=0;i<0x60;++i)CHECK(oldPoints[i]==0x71 && oldHistory[i]==0x72 && newPoints[i]==0x73 && newHistory[i]==0x74);
        CHECK(At<void*>(fixture.object,0xB08)==newPoints && At<void*>(fixture.object,0xB28)==newHistory);DrainCheck();
    }
    for(unsigned retirement=0;retirement<2;++retirement) {
        Fixture fixture(Kind::deiroi);const Wire wire=fixture.WireFor(fixture.Incoming());fault=Fault::persistentTransform;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));auto* journal=crew::enemy_native::Existing(fixture.lease);CHECK(journal!=nullptr);
        if(!journal){fault=Fault::none;continue;}
        if(retirement==0)Put<void*>(fixture.child,0x5C0,fixture.Allocate());else Put<std::uint64_t>(fixture.object,0xF70,0);
        std::vector<std::pair<unsigned char*,std::vector<unsigned char>>> retained;
        std::vector<std::pair<void*,Physical>> retiredBodies;
        for(unsigned i=0;i<journal->writes;++i)if(journal->write[i].owner) {
            const auto& w=journal->write[i];retained.emplace_back(w.at,std::vector<unsigned char>(w.at,w.at+w.size));
        }
        for(unsigned i=0;i<journal->bodies;++i)if(journal->body[i].owner)retiredBodies.emplace_back(journal->body[i].object,physical.at(journal->body[i].object));
        fault=Fault::none;CHECK(RestorePose(fixture.lease));CHECK(fixture.lease.restoreGate==nullptr);
        for(const auto& entry:retained)CHECK(!std::memcmp(entry.first,entry.second.data(),entry.second.size()));
        for(const auto& entry:retiredBodies)CHECK(!std::memcmp(&physical.at(entry.first),&entry.second,sizeof(Physical)));
        CHECK(At<LONG>(At<void*>(fixture.child,0x30),12)==1);DrainCheck();
    }
    {
        Fixture fixture(Kind::timeShip);const Wire wire=fixture.WireFor(fixture.Incoming());fault=Fault::persistentTransform;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(crew::enemy_native::Existing(fixture.lease)!=nullptr);
        Put<std::uint64_t>(fixture.object+0x940,0x20,1);std::memset(fixture.rootNodes+0x110+0xB0,0x69,64);
        const std::vector<unsigned char> retired(fixture.rootNodes+0x110+0xB0,fixture.rootNodes+0x110+0xF0);
        fault=Fault::none;CHECK(RestorePose(fixture.lease));CHECK(!std::memcmp(fixture.rootNodes+0x110+0xB0,retired.data(),retired.size()));
        CHECK(At<std::uint64_t>(fixture.object+0x940,0x20)==1);DrainCheck();
    }
}
void ReadonlyRigidTransformBacking(Kind kind) {
    for(unsigned pending=0;pending<2;++pending) {
        Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot baseline=fixture.Before();
        void* body=kind==Kind::ufo515 ? At<void*>(fixture.object,0xEA0) : fixture.bodies.front();
        auto* descriptor=descriptors.at(At<std::uint32_t>(body,0xF0));
        if(pending){fault=Fault::persistentTransform;CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));fault=Fault::none;}
        DWORD previous=0;CHECK(VirtualProtect(descriptor,0x1000,PAGE_READONLY,&previous)!=0);Sleep(20);const int was=mutations;
        if(pending) {
            Put<float>(fixture.object,0x90,999);CHECK(!RestorePose(fixture.lease));CHECK(mutations==was && At<float>(fixture.object,0x90)==999);
            CHECK(fixture.lease.restoreGate==&RestorePose && crew::enemy_native::Existing(fixture.lease)!=nullptr);
        } else {CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was && fixture.Unchanged(baseline));CHECK(!fixture.lease.restoreGate);}
        DWORD discarded=0;CHECK(VirtualProtect(descriptor,0x1000,previous,&discarded)!=0);Sleep(20);
        CHECK(RestorePose(fixture.lease));CHECK(!fixture.lease.restoreGate && fixture.Unchanged(baseline));DrainCheck();
    }
}
void ReadonlyPhysicalBacking(Kind kind) {
    {
        Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot baseline=fixture.Before();const int was=mutations;
        void* body=kind==Kind::large || kind==Kind::squid ? fixture.character : kind==Kind::ufo515 ? At<void*>(fixture.object,0xEA0) : fixture.bodies.front();
        auto* backing=&physical.at(body);DWORD previous=0;CHECK(VirtualProtect(backing,0x1000,PAGE_READONLY,&previous)!=0);Sleep(20);
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was && fixture.Unchanged(baseline));CHECK(!fixture.lease.restoreGate);
        DWORD discarded=0;CHECK(VirtualProtect(backing,0x1000,previous,&discarded)!=0);Sleep(20);DrainCheck();
    }
    {
        Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot baseline=fixture.Before();fault=Fault::persistentTransform;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(crew::enemy_native::Existing(fixture.lease)!=nullptr);
        void* body=kind==Kind::large || kind==Kind::squid ? fixture.character : kind==Kind::ufo515 ? At<void*>(fixture.object,0xEA0) : fixture.bodies.front();
        auto* backing=&physical.at(body);DWORD previous=0;CHECK(VirtualProtect(backing,0x1000,PAGE_READONLY,&previous)!=0);Sleep(20);fault=Fault::none;
        Put<float>(fixture.object,0x90,888);const int was=mutations;CHECK(!RestorePose(fixture.lease));CHECK(mutations==was && At<float>(fixture.object,0x90)==888);
        CHECK(fixture.lease.restoreGate==&RestorePose && crew::enemy_native::Existing(fixture.lease)!=nullptr);
        DWORD discarded=0;CHECK(VirtualProtect(backing,0x1000,previous,&discarded)!=0);Sleep(20);
        CHECK(RestorePose(fixture.lease));CHECK(fixture.lease.restoreGate==nullptr && fixture.Unchanged(baseline));DrainCheck();
    }
}
void ReadonlyPositionResidual(Kind kind) {
    Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());void* body=fixture.bodies.front();
    const auto id=At<std::uint32_t>(body,0xF0);auto* storage=fixture.Allocate(0x2000);auto* raw=storage+0xFC0;
    std::memcpy(raw,descriptors.at(id),0xB0);descriptors[id]=raw;const Snapshot baseline=fixture.Before();
    DWORD previous=0;CHECK(VirtualProtect(storage+0x1000,0x1000,PAGE_READONLY,&previous)!=0);Sleep(20);const int was=mutations;
    // The first 64B transform is writable; precision residual at +40 is in the next, read-only page.
    CHECK(crew::Readable(raw,64,true) && !crew::Readable(raw,0xB0,true));
    CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was && fixture.Unchanged(baseline));CHECK(!fixture.lease.restoreGate);
    DWORD discarded=0;CHECK(VirtualProtect(storage+0x1000,0x1000,previous,&discarded)!=0);Sleep(20);DrainCheck();
}
void PrecisionRollback(Kind kind) {
    Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());
    for(void* body:fixture.bodies) {
        const auto id=At<std::uint32_t>(body,0xF0);auto* raw=descriptors.at(id);
        Put<float>(raw,0x40,1.0e-7f);Put<float>(raw,0x44,-1.0e-7f);Put<float>(raw,0x48,3.0e-7f);
    }
    const Snapshot baseline=fixture.Before();fault=Fault::onceTransform;
    CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));fault=Fault::none;
    CHECK(fixture.lease.restoreGate==nullptr && fixture.Unchanged(baseline));DrainCheck();
}
void ReusedFailedRagTransaction() {
    {
        Fixture fixture(Kind::deiroi);const Wire wire=fixture.WireFor(fixture.Incoming());fixture.BreakDeepReference();const Snapshot before=fixture.Before();const int was=mutations;
        CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));CHECK(mutations==was && fixture.Unchanged(before));DrainCheck();
    }
    Fixture fixture(Kind::ufo515);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot baseline=fixture.Before();fault=Fault::persistentTransform;
    CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));auto* journal=crew::enemy_native::Existing(fixture.lease);
    CHECK(journal && journal->pending && journal->bodies==1 && journal->body[0].rag==nullptr);
    CHECK(fixture.lease.restoreGate==&RestorePose);fault=Fault::none;
    CHECK(RestorePose(fixture.lease));CHECK(fixture.lease.restoreGate==nullptr && fixture.Unchanged(baseline));DrainCheck();
}
void VelocityFailures(Kind kind) {
    Fixture fixture(kind);const Wire wire=fixture.WireFor(fixture.Incoming());const Snapshot before=fixture.Before();fault=Fault::onceLinear;
    CHECK(!ApplyPose(fixture.lease,wire.data(),wire.size()));fault=Fault::none;
    CHECK(fixture.Unchanged(before));CHECK(fixture.lease.restoreGate==nullptr);DrainCheck();
}
} // namespace

namespace crew::enemy_native {
void* PoseTestFunction(unsigned rva) noexcept {
    switch(rva) {
    case 0x63DA0:return reinterpret_cast<void*>(&Quaternion);
    case 0x63F90:return reinterpret_cast<void*>(&Rotation);
    case 0x11AF6F0:return reinterpret_cast<void*>(&CharacterMatrix);
    case 0x11AF7E0:return reinterpret_cast<void*>(&CharacterLinear);
    case 0x11AFA70:return reinterpret_cast<void*>(&CharacterSetLinear);
    case 0x11B9870:return reinterpret_cast<void*>(&CharacterWarp);
    case 0x97D870:return reinterpret_cast<void*>(&MatrixCopy);
    case 0x6CE820:return reinterpret_cast<void*>(&RigidWarpWrapper);
    case 0x6CE850:return reinterpret_cast<void*>(&RigidWarp);
    case 0x11B1300:return reinterpret_cast<void*>(&RigidLinear);
    case 0x11B1060:return reinterpret_cast<void*>(&RigidAngular);
    case 0x11B18F0:return reinterpret_cast<void*>(&RigidSetLinear);
    case 0x11B1760:return reinterpret_cast<void*>(&RigidSetAngular);
    case 0x3F2CB0:return reinterpret_cast<void*>(&StageShip);
    case 0x1100B90:return reinterpret_cast<void*>(&BuildModel);
    case 0x6E9130:return reinterpret_cast<void*>(&RagTarget);
    case 0x6ED630:return reinterpret_cast<void*>(&RagSet);
    case 0x109B80:return reinterpret_cast<void*>(&ManagerFlush);
    default:std::fprintf(stderr,"unmapped pose function %X\n",rva);std::exit(2);
    }
}
} // namespace crew::enemy_native

int main() {
    BuildImage();
    for(Kind kind:{Kind::deiroi,Kind::large,Kind::timeShip,Kind::squid,Kind::ufo515}) {Success(kind);PreflightCases(kind);RollbackCases(kind);}
    for(Kind kind:{Kind::large,Kind::squid,Kind::ufo515})VelocityFailures(kind);
    for(Kind kind:{Kind::deiroi,Kind::large,Kind::timeShip,Kind::squid,Kind::ufo515}){PendingUnreadableRoot(kind);ReadonlyPhysicalBacking(kind);PrecisionRollback(kind);ReadonlyPositionResidual(kind);}
    for(Kind kind:{Kind::deiroi,Kind::timeShip,Kind::ufo515})ReadonlyRigidTransformBacking(kind);
    ReusedFailedRagTransaction();PreflightUnreadablePart();RetiredPoseStorage();ReplacementLifecycles();DrainCheck();CHECK(physical.empty() && bodyIds.empty() && managers.empty() && descriptors.empty());VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    if(failures){std::fprintf(stderr,"%d failures / %d pose runtime checks\n",failures,checks);return 1;}
    std::printf("%d pose runtime checks passed; five adapters, native physics/cache journal rollback and retained lease gates\n",checks);return 0;
}
