// Execute the production inline x64 code with original native branch checkpoints.
#include "../src/enemy_control_deiroi_entity_aim.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace crew {unsigned char* image=nullptr;}
namespace {
int checks=0,failed=0;unsigned queries=0;const void* trusted=nullptr;const void* lastParameter=nullptr;
unsigned mode=0;bool fault=false;using ScribbleFn=void(*)();ScribbleFn scribble=nullptr;
void Check(bool ok,const char* what,int line){++checks;if(!ok){++failed;std::printf("FAIL %d %s\n",line,what);}}
#define CHECK(x) Check((x),#x,__LINE__)
using Bytes=std::vector<unsigned char>;
void Add(Bytes& b,std::initializer_list<unsigned char> v){b.insert(b.end(),v);}
void U32(Bytes& b,std::size_t v){const auto x=static_cast<std::uint32_t>(v);const auto* p=reinterpret_cast<const unsigned char*>(&x);b.insert(b.end(),p,p+4);}
void Ptr(Bytes& b,const void* p){const auto* v=reinterpret_cast<const unsigned char*>(&p);b.insert(b.end(),v,v+8);}
void Jump(Bytes& b,const void* p){Add(b,{0xFF,0x25,0,0,0,0});Ptr(b,p);}
void Reg(Bytes& b,bool load,unsigned reg,unsigned base,std::size_t off){
    Add(b,{static_cast<unsigned char>(0x48|(reg>=8 ? 4 : 0)|(base>=8 ? 1 : 0)),
        static_cast<unsigned char>(load ? 0x8B : 0x89),static_cast<unsigned char>(0x80|((reg&7)<<3)|(base&7))});
    if((base&7)==4)Add(b,{0x24});U32(b,off);
}
void Xmm(Bytes& b,bool load,unsigned reg,unsigned base,std::size_t off){
    Add(b,{0xF3,static_cast<unsigned char>(0x40|(base>=8 ? 1 : 0)),0x0F,static_cast<unsigned char>(load ? 0x6F : 0x7F),
        static_cast<unsigned char>(0x80|(reg<<3)|(base&7))});
    if((base&7)==4)Add(b,{0x24});U32(b,off);
}
void* Exec(const Bytes& b){
    auto* p=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);if(!p)std::exit(2);
    std::memcpy(p,b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),p,b.size());return p;
}
void Leaf(unsigned rva,const void* fn){Bytes b;Jump(b,fn);std::memcpy(crew::image+rva,b.data(),b.size());}
constexpr unsigned vol[]={0,1,2,8,9,10,11},nonvol[]={3,6,7,12,13};
struct alignas(16) In {std::uint64_t gpr[7]{};std::uint64_t gprPadding=0;alignas(16) unsigned char xmm[6][16]{};const void* parameter=nullptr;std::uint64_t flags=0;std::uint64_t nv[5]{};std::uint64_t tailPadding=0;};
struct alignas(16) Out {std::uint64_t gpr[7]{};std::uint64_t gprPadding=0;alignas(16) unsigned char xmm[6][16]{};std::uint64_t flags=0,nv[5]{},parameter=0,stack=0,expectedStack=0;unsigned path=0,tailPadding=0;};
using RunFn=void(*)(const In*,Out*);
void* Snapshot(unsigned path,const void* finish){
    Bytes b;for(unsigned i=0;i<7;++i)Reg(b,false,vol[i],15,offsetof(Out,gpr)+i*8);
    for(unsigned i=0;i<6;++i)Xmm(b,false,i,15,offsetof(Out,xmm)+i*16);
    Add(b,{0x9C,0x41,0x8F,0x87});U32(b,offsetof(Out,flags));
    for(unsigned i=0;i<5;++i)Reg(b,false,nonvol[i],15,offsetof(Out,nv)+i*8);
    Reg(b,false,5,15,offsetof(Out,parameter));Reg(b,false,4,15,offsetof(Out,stack));
    Add(b,{0x41,0xC7,0x87});U32(b,offsetof(Out,path));U32(b,path);Jump(b,finish);return Exec(b);
}
RunFn Harness(){
    auto* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!p)std::exit(2);
    Bytes b;Add(b,{0x53,0x56,0x57,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x28,
        0x49,0x89,0xCE,0x49,0x89,0xD7}); // R14=input,R15=output; native RSP mod16=0
    Reg(b,false,4,15,offsetof(Out,expectedStack));
    for(unsigned i=0;i<5;++i)Reg(b,true,nonvol[i],14,offsetof(In,nv)+i*8);
    Reg(b,true,5,14,offsetof(In,parameter));
    for(unsigned i=0;i<7;++i)Reg(b,true,vol[i],14,offsetof(In,gpr)+i*8);
    for(unsigned i=0;i<6;++i)Xmm(b,true,i,14,offsetof(In,xmm)+i*16);
    Add(b,{0x41,0xFF,0xB6});U32(b,offsetof(In,flags));Add(b,{0x9D});Jump(b,crew::image+0x6CF95A);
    void* finish=p+b.size();
    Add(b,{0xFC,0x48,0x83,0xC4,0x28,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5D,0x5F,0x5E,0x5B,0xC3});
    std::memcpy(p,b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),p,b.size());
    Leaf(0x6CFA71,Snapshot(1,finish));Leaf(0x6CF96E,Snapshot(2,finish));
    return reinterpret_cast<RunFn>(p);
}
void Same(const In& in,const Out& out,unsigned path,bool native){
    CHECK(out.path==path);CHECK(out.stack==out.expectedStack&&(out.stack&15)==0);
    CHECK(out.parameter==reinterpret_cast<std::uintptr_t>(in.parameter));
    for(unsigned i=0;i<5;++i)CHECK(out.nv[i]==in.nv[i]);
    for(unsigned i=0;i<7;++i)if(!native || !in.gpr[2] || i!=0)CHECK(out.gpr[i]==in.gpr[i]);
    for(unsigned i=0;i<6;++i)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));
    if(!native)CHECK((out.flags&0xCD5)==(in.flags&0xCD5));
}
}
namespace crew::enemy_native {
bool DeiroiAcceptedEntityFactoryScope(const void* parameter) noexcept {
    ++queries;lastParameter=parameter;if(scribble)scribble();
    if(fault){fault=false;RaiseException(0xE0421010,0,0,nullptr);}
    return parameter==trusted&&(mode==1||mode==2);
}
}
int main(){
    using namespace crew;using namespace crew::enemy_native;using namespace deiroi_entity_aim;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    std::memcpy(image+kSite,kOriginal,sizeof(kOriginal));constexpr unsigned char tail[]={0x0F,0x84,0x03,0x01,0,0};
    std::memcpy(image+kSite+sizeof(kOriginal),tail,sizeof(tail));RunFn run=Harness();
    alignas(16) unsigned char parameter[0x4E0]{},other[0x4E0]{};for(unsigned i=0;i<sizeof(parameter);++i)parameter[i]=static_cast<unsigned char>(i*13+7);
    std::array<unsigned char,sizeof(parameter)> before{};std::memcpy(before.data(),parameter,sizeof(parameter));trusted=parameter;
    std::uint64_t control[2]={0x1234567890ABCDEF,1};In in{};in.parameter=parameter;in.flags=0xED7;
    for(unsigned i=0;i<7;++i)in.gpr[i]=0x1234567890123400ull+i*19;
    for(unsigned i=0;i<5;++i)in.nv[i]=0x9080706050403000ull+i*37;
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<16;++j)in.xmm[i][j]=static_cast<unsigned char>(i*23+j);
    Out baseline[3]{};
    in.gpr[2]=0;run(&in,&baseline[0]);Same(in,baseline[0],1,true);CHECK(queries==0);
    in.gpr[2]=reinterpret_cast<std::uintptr_t>(control);control[1]=0;run(&in,&baseline[1]);Same(in,baseline[1],1,true);CHECK(baseline[1].gpr[0]==0);
    control[1]=0x7FFFFFFE;run(&in,&baseline[2]);Same(in,baseline[2],2,true);CHECK(baseline[2].gpr[0]==0x7FFFFFFE);
    image[kSite+3]^=1;CHECK(!InstallDeiroiEntityFactoryAim());CHECK(!thunk);image[kSite+3]^=1;
    CHECK(InstallDeiroiEntityFactoryAim());CHECK(InstallDeiroiEntityFactoryAim());CHECK(DeiroiEntityFactoryAimReady());
    Bytes trash;for(unsigned i=0;i<6;++i)Add(trash,{0x0F,0x57,static_cast<unsigned char>(0xC0|(i<<3)|i)});
    for(unsigned reg:vol){Add(trash,{static_cast<unsigned char>(0x48|(reg>=8 ? 1 : 0)),static_cast<unsigned char>(0xB8|(reg&7))});Ptr(trash,reinterpret_cast<const void*>(0xDEADBEEF11223344ull));}
    Add(trash,{0xC3});scribble=reinterpret_cast<ScribbleFn>(Exec(trash));
    for(unsigned which=0;which<3;++which){
        mode=0;in.gpr[2]=which ? reinterpret_cast<std::uintptr_t>(control) : 0;control[1]=which==1 ? 0 : 0x7FFFFFFE;
        Out out{};run(&in,&out);Same(in,out,which==2 ? 2 : 1,true);
        CHECK((out.flags&0xCC5)==(baseline[which].flags&0xCC5));if(which)CHECK(out.gpr[0]==baseline[which].gpr[0]);
        CHECK(lastParameter==parameter);
    }
    auto* inaccessible=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);CHECK(inaccessible!=nullptr);
    in.gpr[2]=reinterpret_cast<std::uintptr_t>(inaccessible);
    for(unsigned selected=1;selected<=2;++selected){mode=selected;Out out{};run(&in,&out);Same(in,out,1,false);CHECK(lastParameter==parameter);}
    mode=1;in.parameter=other;in.gpr[2]=reinterpret_cast<std::uintptr_t>(control);control[1]=1;
    Out out{};run(&in,&out);Same(in,out,2,true);CHECK(out.gpr[0]==1);in.parameter=parameter;
    fault=true;out={};run(&in,&out);Same(in,out,2,true);CHECK(out.gpr[0]==1);
    CHECK(!std::memcmp(parameter,before.data(),before.size()));CHECK(control[0]==0x1234567890ABCDEFull&&control[1]==1);
    image[kSite+6]^=1;CHECK(!DeiroiEntityFactoryAimReady());CHECK(!InstallDeiroiEntityFactoryAim());image[kSite+6]^=1;CHECK(DeiroiEntityFactoryAimReady());
    DWORD old=0;CHECK(VirtualProtect(thunk,installed.size,PAGE_EXECUTE_READWRITE,&old)!=0);
    static_cast<unsigned char*>(thunk)[installed.size-1]^=1;CHECK(!DeiroiEntityFactoryAimReady());CHECK(!InstallDeiroiEntityFactoryAim());
    static_cast<unsigned char*>(thunk)[installed.size-1]^=1;DWORD ignored=0;CHECK(VirtualProtect(thunk,installed.size,old,&ignored)!=0);CHECK(DeiroiEntityFactoryAimReady());
    auto* saved=image;image+=0x1000;CHECK(!DeiroiEntityFactoryAimReady());CHECK(!InstallDeiroiEntityFactoryAim());image=saved;CHECK(DeiroiEntityFactoryAimReady());
    std::printf("Deiroi TypeEntity inline aim: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
