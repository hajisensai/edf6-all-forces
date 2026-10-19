// Execute production typed aim hooks and the actual generated x64 inline code.
// The image, native leaf calls, admitted profile list and Core lease-point query are fixtures.
#include "../src/enemy_control_weapon_aim.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace fixture {
using namespace crew;
using namespace crew::enemy_weapon_aim;
using namespace crew::enemy_native;
constexpr NativeProfile profiles[]={
#include "../src/enemy_control_native_profiles.inc"
};
int checks=0,failures=0;
void Check(bool value,const char* expression,int line) {
    ++checks;if(!value){++failures;if(failures<=20)std::fprintf(stderr,"line %d: %s\n",line,expression);else if(failures==21)std::fputs("additional failures omitted\n",stderr);}
}
#define CHECK(value) Check((value),#value,__LINE__)
const void* controlled=nullptr;bool allowed=true;float canonical[3]={110,120,30};
unsigned coreQueries=0,walkingQueries=0;bool walkingEnabled=false;const void* walkingControl=nullptr;
unsigned acceptedQueries=0;bool acceptedEnabled=false,deiroiReady=true;const void* acceptedOwner=nullptr;float acceptedPoint[3]={-210,220,-230};
unsigned lockQueries=0;unsigned lockReply=0;const void* lockOwner=nullptr;const void* lockObject=nullptr;unsigned char* lockControl=nullptr;bool lockSawEmpty=false;
using ScribbleFn=void(*)();ScribbleFn scribble=nullptr;
unsigned nativeCalls=0;void* nativeA=nullptr;void* nativeB=nullptr;const void* nativeC=nullptr;void* nativeD=nullptr;
float nativePoint[4]{};float nativeMatrix[16]{};unsigned nativeFlags=0;bool nativeFault=false,nativeEnable=false;
float* nativeResult=nullptr;
std::array<unsigned char,0x400>* activeUnit=nullptr;

const NativeProfile& ProfileNamed(const char* name) {
    for(const auto& p:profiles)if(!std::strcmp(p.name,name))return p;
    std::fprintf(stderr,"missing profile %s\n",name);std::exit(2);
}
struct Object {
    alignas(16) unsigned char bytes[0x4000]{};
    unsigned char weak[32]{};
    Object(const char* name) {
        const auto& p=ProfileNamed(name);Put<void*>(bytes,0,image+p.vtableRva);
        Put<void*>(bytes,0x30,weak);Put<LONG>(weak,8,1);Put<void*>(weak,16,bytes);
    }
};
void NativeUnit(void* unit,void* dt) {
    ++nativeCalls;nativeA=unit;nativeB=dt;nativeFlags=At<unsigned>(unit,0x2C0);
    std::memcpy(nativePoint,static_cast<unsigned char*>(unit)+0x2D0,16);
    if(nativeFault)RaiseException(0xE0011234,0,0,nullptr);
}
float* NativeLarge(void* control,float* out) {
    ++nativeCalls;nativeA=control;nativeB=out;
    out[0]=-1;out[1]=-2;out[2]=-3;out[3]=-4;return out;
}
float* NativeLocator(void* locator,float* out) {
    ++nativeCalls;nativeA=locator;nativeB=out;
    auto* root=static_cast<unsigned char*>(locator)-0x6D8;
    if(nativeEnable)root[0x6A0]=1;
    for(unsigned i=0;i<16;++i)out[i]=static_cast<float>(i+1);
    return nativeResult ? nativeResult : out;
}
void* NativeSpawn(void* manager,void* out,const float* matrix,void* params) {
    ++nativeCalls;nativeA=manager;nativeB=out;nativeC=matrix;nativeD=params;
    std::memcpy(nativeMatrix,matrix,64);return out;
}
bool CatchUnit(void* unit,void* dt) {
    __try {UnitPoint(unit,dt);return false;}
    __except(GetExceptionCode()==0xE0011234 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH){return true;}
}
void CodeAt(unsigned at,const Bytes& code) {std::memcpy(image+at,code.data(),code.size());FlushInstructionCache(GetCurrentProcess(),image+at,code.size());}
void Leaf(unsigned at,const void* target) {Bytes b;Jump(b,target);CodeAt(at,b);}
void* Executable(const Bytes& code) {
    void* memory=VirtualAlloc(nullptr,code.size(),MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if(!memory)std::exit(2);std::memcpy(memory,code.data(),code.size());FlushInstructionCache(GetCurrentProcess(),memory,code.size());return memory;
}
bool Near(float a,float b,float eps=0.0002f) {return std::fabs(a-b)<eps;}
bool Vec(const float* a,const float* b) {for(unsigned i=0;i<4;++i)if(!Near(a[i],b[i]))return false;return true;}
float Length(const float* a) {return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}

void TypedRuntimeCases() {
    Object heavy("EDF6_Berserker_Large");controlled=heavy.bytes;
    unsigned char control[0x20]{},entries[16]{},componentWeak[16]{};float out[16]{};
    Put<void*>(control,0x10,heavy.bytes);Put<void*>(entries,0,control);Put<void*>(entries,8,componentWeak);Put<LONG>(componentWeak,8,1);
    Put<void*>(heavy.bytes,0x1AA8,entries);Put<std::size_t>(heavy.bytes,0x1AB8,1);
    nativeCalls=0;CHECK(LargePoint(control,out)==out);const float wanted[4]={110,120,30,1};
    CHECK(Vec(out,wanted) && nativeCalls==0);
    Put<void*>(entries,0,nullptr);CHECK(LargePoint(control,out)==out && nativeCalls==1 && out[0]==-1);Put<void*>(entries,0,control);
    Put<LONG>(componentWeak,8,0);CHECK(LargePoint(control,out)==out && nativeCalls==2);Put<LONG>(componentWeak,8,1);
    Put<std::size_t>(heavy.bytes,0x1AB8,257);CHECK(LargePoint(control,out)==out && nativeCalls==3);Put<std::size_t>(heavy.bytes,0x1AB8,1);
    allowed=false;CHECK(LargePoint(control,out)==out && nativeCalls==4);allowed=true;
    canonical[0]=std::numeric_limits<float>::quiet_NaN();CHECK(LargePoint(control,out)==out && nativeCalls==5);canonical[0]=110;

    Object ant("GiantAntEX");controlled=ant.bytes;std::array<unsigned char,0x400> unit{};activeUnit=&unit;
    Put<void*>(unit.data(),0x2B0,ant.bytes);Put<void*>(unit.data(),0x2B8,ant.weak);Put<unsigned>(unit.data(),0x2C0,0xA5);
    const float old[4]={-10,-20,-30,1},fixed[4]={1,2,3,1};std::memcpy(unit.data()+0x2D0,old,16);std::memcpy(unit.data()+0x250,fixed,16);
    unsigned char dt[16]{};nativeCalls=0;UnitPoint(unit.data(),dt);
    CHECK(nativeCalls==1 && nativeA==unit.data() && nativeB==dt && nativeFlags==0xA7 && Vec(nativePoint,wanted));
    CHECK(At<unsigned>(unit.data(),0x2C0)==0xA5 && Vec(reinterpret_cast<const float*>(unit.data()+0x2D0),old));
    CHECK(!std::memcmp(unit.data()+0x250,fixed,16));
    allowed=false;UnitPoint(unit.data(),dt);CHECK(nativeFlags==0xA5 && Vec(nativePoint,old) && nativeA==unit.data() && nativeB==dt);allowed=true;
    Put<LONG>(ant.weak,8,0);UnitPoint(unit.data(),dt);CHECK(nativeFlags==0xA5 && Vec(nativePoint,old));Put<LONG>(ant.weak,8,1);
    unsigned char foreignWeak[16]{};Put<LONG>(foreignWeak,8,1);Put<void*>(unit.data(),0x2B8,foreignWeak);UnitPoint(unit.data(),dt);
    CHECK(nativeFlags==0xA5 && Vec(nativePoint,old));Put<void*>(unit.data(),0x2B8,ant.weak);
    nativeFault=true;CHECK(CatchUnit(unit.data(),dt));nativeFault=false;
    CHECK(At<unsigned>(unit.data(),0x2C0)==0xA5 && Vec(reinterpret_cast<const float*>(unit.data()+0x2D0),old));

    Object monster("Monster501");controlled=monster.bytes;nativeCalls=0;nativeEnable=true;nativeResult=out+2;
    CHECK(MonsterLocator(monster.bytes+0x6D8,out)==out+2 && nativeCalls==1 && nativeA==monster.bytes+0x6D8 && nativeB==out);
    CHECK(monster.bytes[0x6A0]==1 && monster.bytes[0x6A1]==1 && Vec(reinterpret_cast<const float*>(monster.bytes+0x6B0),wanted));
    for(unsigned i=0;i<16;++i)CHECK(out[i]==static_cast<float>(i+1));
    nativeEnable=false;monster.bytes[0x6A0]=monster.bytes[0x6A1]=0;std::memcpy(monster.bytes+0x6B0,old,16);
    MonsterLocator(monster.bytes+0x6D8,out);CHECK(!monster.bytes[0x6A1] && Vec(reinterpret_cast<const float*>(monster.bytes+0x6B0),old));
    monster.bytes[0x6A0]=1;allowed=false;MonsterLocator(monster.bytes+0x6D8,out);
    CHECK(!monster.bytes[0x6A1] && Vec(reinterpret_cast<const float*>(monster.bytes+0x6B0),old));allowed=true;nativeResult=nullptr;

    controlled=ant.bytes;const float s=std::sqrt(0.5f);
    alignas(16) const float matrix[16]={2*s,2*s,0,0,-3*s,3*s,0,0,0,0,4,0,10,20,30,1};
    unsigned char manager[16]{},shared[16]{};auto* params=ant.bytes+0x1680;const auto before=std::array<unsigned char,64>{};
    std::memcpy(params,before.data(),before.size());nativeCalls=0;
    CHECK(AntSpawn(manager,shared,matrix,params)==shared && nativeCalls==1 && nativeA==manager && nativeB==shared && nativeD==params);
    CHECK(nativeC!=matrix && !std::memcmp(matrix+12,nativeMatrix+12,16));
    CHECK(Near(Length(nativeMatrix),2) && Near(Length(nativeMatrix+4),3) && Near(Length(nativeMatrix+8),4));
    CHECK(Near(nativeMatrix[0],0) && Near(nativeMatrix[1],0) && Near(nativeMatrix[2],-2));
    CHECK(Near(nativeMatrix[4],-3*s) && Near(nativeMatrix[5],3*s) && Near(nativeMatrix[6],0));
    CHECK(Near(nativeMatrix[8],4*s) && Near(nativeMatrix[9],4*s) && Near(nativeMatrix[10],0));
    CHECK(!std::memcmp(params,before.data(),before.size()));
    allowed=false;AntSpawn(manager,shared,matrix,params);CHECK(nativeC==matrix && !std::memcmp(nativeMatrix,matrix,64));allowed=true;
    canonical[0]=10;canonical[1]=20;canonical[2]=30;AntSpawn(manager,shared,matrix,params);CHECK(nativeC==matrix);canonical[0]=110;canonical[1]=120;
    Object ordinaryAnt("GiantAnt");controlled=ordinaryAnt.bytes;AntSpawn(manager,shared,matrix,ordinaryAnt.bytes+0x1680);
    CHECK(nativeC!=matrix && nativeD==ordinaryAnt.bytes+0x1680 && Near(nativeMatrix[2],-2));
    canonical[0]=10;canonical[1]=20;canonical[2]=-70;AntSpawn(manager,shared,matrix,ordinaryAnt.bytes+0x1680);
    CHECK(Near(nativeMatrix[8],0) && Near(nativeMatrix[9],0) && Near(nativeMatrix[10],-4));
    CHECK(Near(Length(nativeMatrix),2) && Near(Length(nativeMatrix+4),3) && Near(Length(nativeMatrix+8),4));
    canonical[0]=110;canonical[1]=120;canonical[2]=30;
}

struct alignas(16) RegisterInput {
    std::uint64_t gpr[7]{};std::uint64_t gprPadding=0;alignas(16) unsigned char xmm[6][16]{};
    const void* owner=nullptr;const void* target=nullptr;std::uint64_t flags=0;std::uint64_t tailPadding=0;
    float stackPoint[4]={-200,-300,-400,1};unsigned char xmm6[16]{},xmm7[16]{};
    const void* auxiliary=nullptr;std::uint64_t specialPadding=0;unsigned char xmm9[16]{};
};
struct alignas(16) RegisterOutput {
    std::uint64_t gpr[7]{};std::uint64_t gprPadding=0;alignas(16) unsigned char xmm[6][16]{};
    std::uint64_t flags=0;float pointerPoint[4]{};unsigned path=99;unsigned tailPadding=0;
    float stack30[4]{},stack50[4]{},framePoint[4]{},xmm6[4]{};std::uintptr_t nonvolatile[6]{};std::uintptr_t frameBase=0;std::uint64_t specialPadding=0;
    float slot20[4]{},bossPoint[4]{},saved6[4]{},saved7[4]{};std::uintptr_t savedRdi=0,extraPointerPadding=0;unsigned char xmm7[16]{};
    float partsSource[4]{},partsCopy[4]{};std::uintptr_t partsOwner=0,partsPadding=0;
    std::uint64_t gateGpr[7]{};std::uint64_t gatePadding=0;unsigned char gateXmm[6][16]{};std::uint64_t gateFlags=0;
    unsigned gateCalls=0,sendCalls=0,localCalls=0,delegateQueries=0;float sentDirection[4]{},localDirection[4]{},meteorOrigin[4]{};
    std::uintptr_t meteorOwner=0,meteorOriginPointer=0,meteorDirectionPointer=0;
};
constexpr unsigned regs[]={0,1,2,8,9,10,11};
void Disp(Bytes& b,std::size_t offset) {Dword(b,static_cast<unsigned>(offset));}
void MemReg(Bytes& b,bool load,unsigned reg,unsigned base,std::size_t offset) {
    Append(b,{static_cast<unsigned char>(0x48 | (reg>=8 ? 4 : 0) | (base>=8 ? 1 : 0)),static_cast<unsigned char>(load ? 0x8B : 0x89),static_cast<unsigned char>(0x80 | ((reg&7)<<3) | (base&7))});if((base&7)==4)Append(b,{0x24});Disp(b,offset);
}
void VectorMemory(Bytes& b,bool load,unsigned reg,unsigned base,std::size_t offset) {
    Append(b,{0xF3,static_cast<unsigned char>(0x40 | (reg>=8 ? 4 : 0) | (base>=8 ? 1 : 0)),0x0F,static_cast<unsigned char>(load ? 0x6F : 0x7F),static_cast<unsigned char>(0x80 | ((reg&7)<<3) | (base&7))});if((base&7)==4)Append(b,{0x24});Disp(b,offset);
}
void* Snapshot(unsigned path,bool pointerPoint,const void* finish) {
    Bytes b;
    for(unsigned i=0;i<7;++i)MemReg(b,false,regs[i],15,offsetof(RegisterOutput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,false,i,15,offsetof(RegisterOutput,xmm)+i*16);
    Append(b,{0x9C,0x41,0x8F,0x87});Disp(b,offsetof(RegisterOutput,flags)); // POP m64 preserves the incoming flags
    if(pointerPoint) {Append(b,{0xF3,0x0F,0x6F,0x07});VectorMemory(b,false,0,15,offsetof(RegisterOutput,pointerPoint));}
    Append(b,{0x41,0xC7,0x87});Disp(b,offsetof(RegisterOutput,path));Dword(b,path);Jump(b,finish);return Executable(b);
}
using RunFn=void(*)(RegisterInput*,RegisterOutput*);
RunFn InlineHarness(const InlineSite& site) {
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!memory)std::exit(2);
    Bytes b;Append(b,{0x53,0x56,0x57,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0xA8,0,0,0});
    Append(b,{0x49,0x89,0xCE,0x49,0x89,0xD7}); // nonvolatile R14=input, R15=output
    MemReg(b,true,site.ownerRegister,14,offsetof(RegisterInput,owner));
    MemReg(b,true,3,14,offsetof(RegisterInput,target));
    for(unsigned i=0;i<7;++i)MemReg(b,true,regs[i],14,offsetof(RegisterInput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,true,i,14,offsetof(RegisterInput,xmm)+i*16);
    Append(b,{0x41,0xFF,0xB6});Disp(b,offsetof(RegisterInput,flags));Append(b,{0x9D});
    Jump(b,image+site.at);
    void* finish=memory+b.size();
    Append(b,{0x48,0x81,0xC4,0xA8,0,0,0,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5D,0x5F,0x5E,0x5B,0xC3});
    std::memcpy(memory,b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),memory,b.size());
    void* point=Snapshot(1,site.pointPointer,finish);void* nullCase=Snapshot(0,false,finish);
    void* body=Snapshot(2,false,finish);void* lock=Snapshot(3,false,finish);
    Leaf(site.pointPath,point);Leaf(site.nullPath,nullCase);
    Bytes comparison;Append(comparison,{0x74,0x0E});Jump(comparison,lock);Jump(comparison,body);
    void* compare=Executable(comparison);Leaf(site.at+17,compare);
    return reinterpret_cast<RunFn>(memory);
}
void InlineCases() {
    constexpr unsigned selectedSites[]={0x40DD1A,0x4246EC,0x33AF42,0x5351ED,0x52C2A3};
    for(unsigned at:selectedSites) {
        unsigned index=0;while(index<std::size(kInlineSites) && kInlineSites[index].at!=at)++index;
        CHECK(index<std::size(kInlineSites));if(index>=std::size(kInlineSites))continue;
        const auto& site=kInlineSites[index];
        const char* name=at==0x33AF42 ? "DragonSmall" : at==0x4246EC ? "GiantBee" : at==0x5351ED ? "UfoSmall515" : at==0x52C2A3 ? "UfoSmall507" : "GiantAnt";
        Object object(name);controlled=object.bytes;
        std::memcpy(image+site.at,site.original,sizeof(site.original));RunFn run=InlineHarness(site);CHECK(InstallInline(index));
        RegisterInput in{};RegisterOutput out{};in.owner=object.bytes;in.flags=0xA97;
        for(unsigned i=0;i<7;++i)in.gpr[i]=0x1122334455660000ull+i*0x101;
        for(unsigned i=0;i<6;++i)for(unsigned k=0;k<16;++k)in.xmm[i][k]=static_cast<unsigned char>(0x20+i*17+k);
        if(site.targetRegister==0)in.gpr[0]=0;in.target=nullptr;allowed=true;run(&in,&out);
        CHECK(out.path==1 && (out.flags&0x8D5)==(in.flags&0x8D5));
        for(unsigned i=0;i<7;++i)CHECK(out.gpr[i]==in.gpr[i]);
        for(unsigned i=0;i<6;++i)if(site.pointPointer || i!=2)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));
        const float wanted[4]={110,120,30,1};
        CHECK(Vec(site.pointPointer ? out.pointerPoint : reinterpret_cast<const float*>(out.xmm[2]),wanted));
        in.flags=0xAD7;out={};run(&in,&out);CHECK(out.path==1 && (out.flags&0x8D5)==(in.flags&0x8D5));
        for(unsigned i=0;i<7;++i)CHECK(out.gpr[i]==in.gpr[i]);
        in.flags=0xA97;
        allowed=false;out={};run(&in,&out);CHECK(out.path==0 && (out.flags&0x8C5)==0x44);
        for(unsigned i=0;i<7;++i)CHECK(out.gpr[i]==in.gpr[i]);for(unsigned i=0;i<6;++i)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));
        unsigned char target[0x400]{};in.target=target;if(site.targetRegister==0)in.gpr[0]=reinterpret_cast<std::uintptr_t>(target);
        out={};run(&in,&out);CHECK(out.path==2 && (out.flags&0x8C5)==0x44);
        Put<std::uint64_t>(target,0x378,1);out={};run(&in,&out);CHECK(out.path==3 && (out.flags&0x8C5)==0);
        for(unsigned i=0;i<7;++i)CHECK(out.gpr[i]==in.gpr[i]);for(unsigned i=0;i<6;++i)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));
        allowed=true;
    }
}

unsigned ballisticCalls=0;void* ballisticContext=nullptr;const float* ballisticMuzzle=nullptr;float ballisticSpeed=0;
float ballisticTarget[4]{};constexpr float ballisticAnswer[4]={6,7,8,1};
float* NativeBallistic(void* context,float* out,const float* target,const float* muzzle,float speed) {
    ++ballisticCalls;ballisticContext=context;ballisticMuzzle=muzzle;ballisticSpeed=speed;
    std::memcpy(ballisticTarget,target,16);std::memcpy(out,ballisticAnswer,16);
    Put<float>(context,0x14,123.25f);return out; // a native-solver-owned side effect, not plugin state
}
void* StackSnapshot(const void* finish) {
    Bytes b;
    for(unsigned i=0;i<7;++i)MemReg(b,false,regs[i],15,offsetof(RegisterOutput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,false,i,15,offsetof(RegisterOutput,xmm)+i*16);
    Append(b,{0x9C,0x41,0x8F,0x87});Disp(b,offsetof(RegisterOutput,flags));
    MemReg(b,false,5,15,offsetof(RegisterOutput,nonvolatile));
    MemReg(b,false,7,15,offsetof(RegisterOutput,nonvolatile)+8);
    MemReg(b,false,12,15,offsetof(RegisterOutput,nonvolatile)+16);
    MemReg(b,false,13,15,offsetof(RegisterOutput,frameBase));
    VectorMemory(b,false,6,15,offsetof(RegisterOutput,xmm6));
    VectorMemory(b,true,0,13,0x30);VectorMemory(b,false,0,15,offsetof(RegisterOutput,stack30));
    VectorMemory(b,true,0,13,0x50);VectorMemory(b,false,0,15,offsetof(RegisterOutput,stack50));
    VectorMemory(b,true,0,13,0x100);VectorMemory(b,false,0,15,offsetof(RegisterOutput,framePoint));
    Append(b,{0x41,0xC7,0x87});Disp(b,offsetof(RegisterOutput,path));Dword(b,4);Jump(b,finish);return Executable(b);
}
RunFn StackHarness(const StackSite& site) {
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!memory)std::exit(2);
    Bytes b;Append(b,{0x53,0x56,0x57,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x28,0x02,0,0});
    // R13 holds the true native RSP across stolen writes to RBP/RDI/R12; save the caller's nonvolatile XMM6.
    Append(b,{0x49,0x89,0xE5,0x49,0x89,0xCE,0x49,0x89,0xD7});
    VectorMemory(b,false,6,13,0x210);
    VectorMemory(b,true,0,14,offsetof(RegisterInput,stackPoint));
    VectorMemory(b,false,0,13,0x30);VectorMemory(b,false,0,13,0x50);VectorMemory(b,false,0,13,0x100);
    Append(b,{0x49,0x8D,0xAD});Dword(b,0x180); // actual RBP-80 destination is original RSP+100
    Append(b,{0x48,0xBF});Address(b,reinterpret_cast<const void*>(0x11224466));
    Append(b,{0x49,0xBC});Address(b,reinterpret_cast<const void*>(0x12481248));
    if(!site.humanoid)MemReg(b,true,site.ownerRegister,14,offsetof(RegisterInput,owner));
    for(unsigned i=0;i<7;++i)MemReg(b,true,regs[i],14,offsetof(RegisterInput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,true,i,14,offsetof(RegisterInput,xmm)+i*16);
    VectorMemory(b,true,6,14,offsetof(RegisterInput,xmm6));
    Append(b,{0x41,0xFF,0xB6});Disp(b,offsetof(RegisterInput,flags));Append(b,{0x9D});
    if(site.humanoid)MemReg(b,true,14,14,offsetof(RegisterInput,owner));
    Jump(b,image+site.at);void* finish=memory+b.size();
    VectorMemory(b,true,6,13,0x210);
    Append(b,{0x48,0x81,0xC4,0x28,0x02,0,0,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5D,0x5F,0x5E,0x5B,0xC3});
    std::memcpy(memory,b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),memory,b.size());
    Leaf(site.at+site.length,StackSnapshot(finish));return reinterpret_cast<RunFn>(memory);
}
RegisterInput StackInput(const void* owner) {
    RegisterInput in{};in.owner=owner;in.flags=0xAD7;
    for(unsigned i=0;i<7;++i)in.gpr[i]=0x1122334455660000ull+i*0x101;
    for(unsigned i=0;i<6;++i)for(unsigned k=0;k<16;++k)in.xmm[i][k]=static_cast<unsigned char>(0x30+i*17+k);
    const float initialXmm6[4]={9,8,7,1};std::memcpy(in.xmm6,initialXmm6,16);return in;
}
void StolenState(const StackSite& site,const RegisterInput& in,const RegisterOutput& out,const void* root,const void* component,const void* world) {
    CHECK(out.path==4);
    for(unsigned i=0;i<7;++i) {
        std::uint64_t expected=in.gpr[i];
        if(i==0 && site.at==0x43613B)expected=At<std::uint64_t>(root,0x3E8);
        if(i==0 && site.gravityGlobal)expected=reinterpret_cast<std::uintptr_t>(world);
        if(i==0 && site.mode==3)expected=reinterpret_cast<std::uintptr_t>(component)+0x120;
        CHECK(out.gpr[i]==expected);
    }
    for(unsigned i=0;i<6;++i) {
        unsigned char expected[16];std::memcpy(expected,in.xmm[i],16);
        if(site.mode==2 && i==0) {std::memset(expected,0,16);std::memcpy(expected,static_cast<const unsigned char*>(root)+0x18A0,4);}
        if(site.mode==3 && i==3) {std::memset(expected,0,16);std::memcpy(expected,static_cast<const unsigned char*>(component)+0x154,4);}
        CHECK(!std::memcmp(out.xmm[i],expected,16));
    }
    if(site.gravityGlobal)CHECK((out.flags&0x8C5)==0x04); // native CMP world+B00(9),0
    else if(site.humanoid)CHECK((out.flags&0xC5)==0x04); // native SHL positive roster size by four
    else CHECK((out.flags&0x8D5)==(in.flags&0x8D5));
    if(site.at==0x43613B)CHECK(out.nonvolatile[0]==0x5D588B656C078965ull);
    else CHECK(out.nonvolatile[0]==out.frameBase+0x180);
    if(site.humanoid) {
        CHECK(out.nonvolatile[1]==At<std::uintptr_t>(root,0x1120));CHECK(out.nonvolatile[2]==48);
    } else {
        CHECK(out.nonvolatile[1]==(site.ownerRegister==7 ? reinterpret_cast<std::uintptr_t>(root) : 0x11224466));
        CHECK(out.nonvolatile[2]==0x12481248);
    }
}
void StackCases() {
    constexpr unsigned selectedSites[]={0x43613B,0x4E3714,0x46F572,0x431CFC,0x3E729A};
    auto* world=image+0x2100000;Put<void*>(image,0x20B2958,world);Put<unsigned>(world,0xB00,9);
    Leaf(0x2312A0,reinterpret_cast<const void*>(&NativeBallistic));
    for(unsigned at:selectedSites) {
        unsigned index=0;while(index<std::size(kStackSites) && kStackSites[index].at!=at)++index;
        CHECK(index<std::size(kStackSites));if(index>=std::size(kStackSites))continue;
        const auto& site=kStackSites[index];
        const char* name=at==0x43613B ? "GiantSpider" : at==0x4E3714 ? "Nephila" : at==0x46F572 ? "Humanoid_Basic" : at==0x431CFC ? "GiantDango" : "EDF6_SquidSmall";
        Object root(name);controlled=root.bytes;Put<std::uint64_t>(root.bytes,0x3E8,0x0BADF00DAB1234ull);
        Put<float>(root.bytes,0x18A0,5.25f);unsigned char roster[16]{};Put<void*>(root.bytes,0x1120,roster);Put<std::size_t>(root.bytes,0x1130,3);
        alignas(16) unsigned char component[0x200]{};unsigned char componentWeak[24]{},components[16]{};
        Put<void*>(component,0,root.bytes);Put<LONG>(componentWeak,8,1);Put<void*>(componentWeak,16,component);
        Put<void*>(components,0,component);Put<void*>(components,8,componentWeak);Put<void*>(root.bytes,0x13A8,components);Put<std::size_t>(root.bytes,0x13B8,1);
        const float muzzle[4]={10,20,30,1};std::memcpy(component+0xF0,muzzle,16);Put<float>(component,0x154,6.25f);
        const bool ballistic=at==0x43613B || at==0x4E3714;
        const std::size_t context=at==0x43613B ? 0x1AD0 : 0x1100,muzzleAt=at==0x43613B ? 0x1CC0 : 0x10A0,speedAt=at==0x43613B ? 0x1CD4 : 0x10C0;
        if(ballistic) {std::memcpy(root.bytes+muzzleAt,muzzle,16);Put<float>(root.bytes,speedAt,16.0f);}
        const void* nativeOwner=site.humanoid ? root.bytes+0x5C0 : site.mode==3 ? component : root.bytes;
        RegisterInput in=StackInput(nativeOwner);RegisterOutput out{};
        std::memcpy(image+site.at,site.original,site.length);RunFn run=StackHarness(site);CHECK(InstallStack(index));
        std::array<unsigned char,0x4000> actorBefore{};std::memcpy(actorBefore.data(),root.bytes,actorBefore.size());
        allowed=true;ballisticCalls=0;run(&in,&out);StolenState(site,in,out,root.bytes,component,world);
        const float wanted[4]={110,120,30,1},direction[4]={100,100,0,1};
        if(ballistic) {
            CHECK(Vec(out.stack30,ballisticAnswer) && ballisticCalls==1 && ballisticContext==root.bytes+context);
            CHECK(ballisticMuzzle==reinterpret_cast<const float*>(root.bytes+muzzleAt) && ballisticSpeed==16 && Vec(ballisticTarget,wanted));
            CHECK(At<float>(root.bytes+context,0x14)==123.25f);
            Put<float>(actorBefore.data()+context,0x14,123.25f);CHECK(!std::memcmp(actorBefore.data(),root.bytes,actorBefore.size()));
            CHECK(Vec(out.stack50,in.stackPoint) && Vec(out.framePoint,in.stackPoint));
        } else if(site.humanoid)CHECK(Vec(out.framePoint,wanted) && Vec(out.stack30,in.stackPoint) && Vec(out.stack50,in.stackPoint));
        else if(site.mode==2)CHECK(Vec(out.xmm6,wanted) && Vec(reinterpret_cast<const float*>(root.bytes+0x1780),wanted) && Vec(out.stack50,wanted));
        else CHECK(Vec(out.stack50,direction) && Vec(out.stack30,in.stackPoint) && Vec(out.framePoint,in.stackPoint));
        if(site.mode!=2)CHECK(!std::memcmp(out.xmm6,in.xmm6,16));
        allowed=false;out={};const unsigned previous=ballisticCalls;run(&in,&out);StolenState(site,in,out,root.bytes,component,world);
        CHECK(ballisticCalls==previous && Vec(out.stack30,in.stackPoint) && Vec(out.framePoint,in.stackPoint));
        if(site.mode==2)CHECK(!std::memcmp(out.xmm6,in.xmm6,16) && !std::memcmp(root.bytes+0x1780,in.xmm6,16) && !std::memcmp(out.stack50,in.xmm6,16));
        else CHECK(Vec(out.stack50,in.stackPoint));
        allowed=true;
        if(ballistic) {
            std::array<unsigned char,0xC0> nativeBefore{};std::memcpy(nativeBefore.data(),root.bytes+context,nativeBefore.size());
            Put<float>(root.bytes,speedAt,0);out={};run(&in,&out);
            CHECK(Vec(out.stack30,in.stackPoint) && ballisticCalls==previous && !std::memcmp(nativeBefore.data(),root.bytes+context,nativeBefore.size()));
            Put<float>(root.bytes,speedAt,16);canonical[0]=std::numeric_limits<float>::quiet_NaN();out={};run(&in,&out);
            CHECK(Vec(out.stack30,in.stackPoint) && ballisticCalls==previous);canonical[0]=110;
        }
        if(site.mode==3) {
            Put<LONG>(componentWeak,8,0);out={};run(&in,&out);StolenState(site,in,out,root.bytes,component,world);CHECK(Vec(out.stack50,in.stackPoint));Put<LONG>(componentWeak,8,1);
            Put<void*>(components,0,nullptr);out={};run(&in,&out);StolenState(site,in,out,root.bytes,component,world);CHECK(Vec(out.stack50,in.stackPoint));Put<void*>(components,0,component);
            Object wrong("GiantAnt");Put<void*>(component,0,wrong.bytes);controlled=wrong.bytes;out={};run(&in,&out);StolenState(site,in,out,root.bytes,component,world);CHECK(Vec(out.stack50,in.stackPoint));Put<void*>(component,0,root.bytes);controlled=root.bytes;
            Put<float>(component,0xF0,std::numeric_limits<float>::quiet_NaN());out={};run(&in,&out);StolenState(site,in,out,root.bytes,component,world);CHECK(Vec(out.stack50,in.stackPoint));
        }
    }
}


void* SpecialSnapshot(unsigned path,const void* finish) {
    Bytes b;
    for(unsigned i=0;i<7;++i)MemReg(b,false,regs[i],15,offsetof(RegisterOutput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,false,i,15,offsetof(RegisterOutput,xmm)+i*16);
    Append(b,{0x9C,0x41,0x8F,0x87});Disp(b,offsetof(RegisterOutput,flags));
    constexpr unsigned nativeRegs[]={5,7,12,6,14,3};
    for(unsigned i=0;i<6;++i)MemReg(b,false,nativeRegs[i],15,offsetof(RegisterOutput,nonvolatile)+i*8);
    MemReg(b,false,13,15,offsetof(RegisterOutput,frameBase));
    VectorMemory(b,false,6,15,offsetof(RegisterOutput,xmm6));VectorMemory(b,false,7,15,offsetof(RegisterOutput,xmm7));
    constexpr unsigned slots[]={0x20,0x147,0x90,0x80};
    constexpr std::size_t outputs[]={offsetof(RegisterOutput,slot20),offsetof(RegisterOutput,bossPoint),offsetof(RegisterOutput,saved6),offsetof(RegisterOutput,saved7)};
    for(unsigned i=0;i<4;++i){VectorMemory(b,true,0,13,slots[i]);VectorMemory(b,false,0,15,outputs[i]);}
    MemReg(b,true,0,13,0xB0);MemReg(b,false,0,15,offsetof(RegisterOutput,savedRdi));
    Append(b,{0x41,0xC7,0x87});Disp(b,offsetof(RegisterOutput,path));Dword(b,path);Jump(b,finish);return Executable(b);
}
struct SpecialRun {RunFn run;void* finish;};
SpecialRun SpecialHarness(unsigned entry,bool shell) {
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!memory)std::exit(2);
    Bytes b;Append(b,{0x53,0x56,0x57,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x48,0x02,0,0});
    Append(b,{0x49,0x89,0xE5,0x49,0x89,0xCE,0x49,0x89,0xD7});
    VectorMemory(b,false,6,13,0x210);VectorMemory(b,false,7,13,0x220);VectorMemory(b,false,9,13,0x230);
    VectorMemory(b,true,0,14,offsetof(RegisterInput,stackPoint));VectorMemory(b,false,0,13,0x20);VectorMemory(b,false,0,13,0x147);
    Append(b,{0x49,0x8D,0xAD});Dword(b,0x180);
    Append(b,{0x48,0xBE});Address(b,reinterpret_cast<const void*>(0x55667788));
    Append(b,{0x49,0xBC});Address(b,reinterpret_cast<const void*>(0x12481248));
    if(shell) {MemReg(b,true,3,14,offsetof(RegisterInput,owner));Append(b,{0x48,0xBF});Address(b,reinterpret_cast<const void*>(0x11224466));}
    else {MemReg(b,true,3,14,offsetof(RegisterInput,auxiliary));MemReg(b,true,7,14,offsetof(RegisterInput,owner));}
    for(unsigned i=0;i<7;++i)MemReg(b,true,regs[i],14,offsetof(RegisterInput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,true,i,14,offsetof(RegisterInput,xmm)+i*16);
    VectorMemory(b,true,6,14,offsetof(RegisterInput,xmm6));VectorMemory(b,true,7,14,offsetof(RegisterInput,xmm7));VectorMemory(b,true,9,14,offsetof(RegisterInput,xmm9));
    Append(b,{0x41,0xFF,0xB6});Disp(b,offsetof(RegisterInput,flags));Append(b,{0x9D});
    if(!shell)MemReg(b,true,14,14,offsetof(RegisterInput,target));
    Jump(b,image+entry);void* finish=memory+b.size();
    VectorMemory(b,true,6,13,0x210);VectorMemory(b,true,7,13,0x220);VectorMemory(b,true,9,13,0x230);
    Append(b,{0x48,0x81,0xC4,0x48,0x02,0,0,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5D,0x5F,0x5E,0x5B,0xC3});
    std::memcpy(memory,b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),memory,b.size());return {reinterpret_cast<RunFn>(memory),finish};
}
void VolatileSame(const RegisterInput& in,const RegisterOutput& out,int changedGpr=-1) {
    for(unsigned i=0;i<7;++i)if(static_cast<int>(i)!=changedGpr)CHECK(out.gpr[i]==in.gpr[i]);
    for(unsigned i=0;i<6;++i)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));
    CHECK(!std::memcmp(out.xmm6,in.xmm6,16) && !std::memcmp(out.xmm7,in.xmm7,16));
}
void ShellCases() {
    constexpr unsigned at=0x3C466C;constexpr unsigned char original[]={0x48,0x85,0xD2,0x75,0x1D,0x33,0xC0,0xC7,0x41,0x18,0,0,0x80,0x3F};
    Object root("EDF6_ShellFish");controlled=root.bytes;alignas(16) unsigned char unit[0x300]{},control[0x100]{};
    Put<void*>(unit,0x2B0,root.bytes);Put<void*>(unit,0x2B8,root.weak);Put<void*>(control,0,unit);
    auto h=SpecialHarness(at,true);Leaf(0x3C46C7,SpecialSnapshot(1,h.finish));Leaf(0x3C468E,SpecialSnapshot(2,h.finish));
    // Remaining native null-target branch stores (0,0,1,1) before its original epilogue.
    Bytes nullNative;Append(nullNative,{0x48,0x89,0x41,0x10,0xC7,0x41,0x1C,0,0,0x80,0x3F});Jump(nullNative,SpecialSnapshot(0,h.finish));
    Leaf(at+sizeof(original),Executable(nullNative));std::memcpy(image+at,original,sizeof(original));CHECK(InstallShell());
    RegisterInput in=StackInput(control);RegisterOutput out{};in.gpr[1]=reinterpret_cast<std::uintptr_t>(control);in.gpr[2]=0;
    for(unsigned i=0;i<16;++i)in.xmm7[i]=static_cast<unsigned char>(0x70+i);
    allowed=true;h.run(&in,&out);const float wanted[4]={110,120,30,1};
    CHECK(out.path==1 && Vec(out.slot20,wanted) && out.nonvolatile[1]==out.frameBase+0x20);
    CHECK(out.savedRdi==0x11224466 && !std::memcmp(out.saved6,in.xmm6,16) && !std::memcmp(out.saved7,in.xmm7,16));
    CHECK((out.flags&0x8D5)==(in.flags&0x8D5));VolatileSame(in,out);
    allowed=false;out={};h.run(&in,&out);CHECK(out.path==0 && out.gpr[0]==0 && (out.flags&0x8C5)==0x44);VolatileSame(in,out,0);
    const float nullDirection[4]={0,0,1,1};CHECK(Vec(reinterpret_cast<const float*>(control+0x10),nullDirection));
    unsigned char target[0x400]{};in.gpr[2]=reinterpret_cast<std::uintptr_t>(target);out={};h.run(&in,&out);
    CHECK(out.path==2 && (out.flags&0xC1)==0);VolatileSame(in,out);
    allowed=true;Put<LONG>(root.weak,8,0);in.gpr[2]=0;out={};h.run(&in,&out);
    CHECK(out.path==0 && out.gpr[0]==0);VolatileSame(in,out,0);Put<LONG>(root.weak,8,1);
    Object wrong("GiantAnt");Put<void*>(unit,0x2B0,wrong.bytes);Put<void*>(unit,0x2B8,wrong.weak);controlled=wrong.bytes;
    out={};h.run(&in,&out);CHECK(out.path==0 && out.gpr[0]==0);Put<void*>(unit,0x2B0,root.bytes);Put<void*>(unit,0x2B8,root.weak);controlled=root.bytes;
}
struct BossFixture {
    Object root{"Humanoid_BigGreyBoss"};alignas(16) unsigned char child[0xC00]{},gun[0x300]{};
    unsigned char childWeak[32]{},gunWeak[32]{},head[0x50]{},node[0x50]{},guns[16]{};
    BossFixture() {
        Put<void*>(child,0x30,childWeak);Put<LONG>(childWeak,8,1);Put<void*>(childWeak,16,child);
        Put<LONG>(gunWeak,8,1);Put<void*>(gunWeak,16,gun);
        Put<void*>(child,0xAE8,root.bytes);Put<void*>(child,0xAF0,root.weak);Put<void*>(root.bytes,0x17E8,head);
        Put<void*>(head,0,node);Put<void*>(node,0,head);Put<void*>(node,0x40,child);Put<void*>(node,0x48,childWeak);
        Put<void*>(child,0xB60,guns);Put<std::size_t>(child,0xB70,1);Put<void*>(guns,0,gun);Put<void*>(guns,8,gunWeak);
        Put<void*>(gun,0x200,root.bytes);Put<void*>(gun,0x208,root.weak);Put<void*>(gun,0x210,child);Put<void*>(gun,0x218,childWeak);
    }
};
unsigned weakCalls=0;const void* weakTarget=nullptr;void* weakOutput=nullptr;int weakMode=-1;
void* NativeWeak(const void* target,void* out,int mode) {
    ++weakCalls;weakTarget=target;weakOutput=out;weakMode=mode;
    Put<const void*>(out,0,target);Put<std::uint64_t>(out,8,0x12345678);return static_cast<unsigned char*>(out)+8;
}

unsigned bossSolveCalls=0;void* bossContext=nullptr;float* bossOutput=nullptr;const float* bossInput=nullptr;const float* bossMuzzle=nullptr;float bossSpeed=0;float bossTarget[4]{};
float* NativeBossBallistic(void* context,float* out,const float* target,const float* muzzle,float speed) {
    ++bossSolveCalls;bossContext=context;bossOutput=out;bossInput=target;bossMuzzle=muzzle;bossSpeed=speed;
    std::memcpy(bossTarget,target,16);std::memcpy(out,ballisticAnswer,16);return out;
}
void BossCases() {
    BossFixture f;controlled=f.root.bytes;const void* root=nullptr;
    CHECK(BossPartOwner(f.child,&root) && root==f.root.bytes);CHECK(BossGunOwner(f.gun,&root) && root==f.root.bytes);
    unsigned char target[0x600]{},lockpoint[0x30]{};void* lockList[]={lockpoint};
    Put<void*>(target,0x368,lockList);const float wanted[4]={110,120,30,1};
    Leaf(0x548EF0,reinterpret_cast<const void*>(&NativeWeak));unsigned char pair[16]{};std::memset(pair,0xEE,16);weakCalls=0;
    CHECK(BossLockWeak(target,pair,f.gun)==pair && weakCalls==0 && At<std::uint64_t>(pair,0)==0 && At<std::uint64_t>(pair,8)==0);
    allowed=false;CHECK(BossLockWeak(target,pair,f.gun)==pair+8 && weakCalls==1 && weakTarget==target && weakOutput==pair && weakMode==0);
    CHECK(BossLockWeak(nullptr,pair,f.gun)==pair+8 && weakCalls==2 && weakTarget==nullptr && weakOutput==pair && weakMode==0);allowed=true;
    Put<LONG>(f.childWeak,8,0);CHECK(!BossGunOwner(f.gun,&root));CHECK(BossLockWeak(target,pair,f.gun)==pair+8 && weakCalls==3);Put<LONG>(f.childWeak,8,1);
    unsigned char other[16]{};Put<void*>(f.gun,0x208,other);CHECK(!BossGunOwner(f.gun,&root));Put<void*>(f.gun,0x208,f.root.weak);
    Put<void*>(f.guns,0,nullptr);CHECK(!BossGunOwner(f.gun,&root));Put<void*>(f.guns,0,f.gun);
    Put<void*>(f.node,0x40,nullptr);CHECK(!BossPartOwner(f.child,&root));Put<void*>(f.node,0x40,f.child);

    constexpr unsigned gate=0x44E1E6;constexpr unsigned char gateBytes[]={0x48,0x8B,0x93,0x20,0x05,0,0,0xBE,0xFF,0xFF,0xFF,0xFF,0x48,0x85,0xD2};
    auto g=SpecialHarness(gate,false);Leaf(0x44E25E,SpecialSnapshot(1,g.finish));
    Bytes gateNative;Append(gateNative,{0x74,0x0E});Jump(gateNative,SpecialSnapshot(2,g.finish));Jump(gateNative,SpecialSnapshot(0,g.finish));
    Leaf(gate+sizeof(gateBytes),Executable(gateNative));std::memcpy(image+gate,gateBytes,sizeof(gateBytes));CHECK(InstallBoss(0));
    RegisterInput in=StackInput(f.gun);RegisterOutput out{};in.auxiliary=f.root.bytes;in.target=nullptr;
    for(unsigned i=0;i<16;++i)in.xmm7[i]=static_cast<unsigned char>(0x90+i);
    allowed=true;g.run(&in,&out);CHECK(out.path==1 && out.nonvolatile[4]==0 && out.nonvolatile[3]==0xFFFFFFFFull);
    CHECK((out.flags&0x8D5)==(in.flags&0x8D5));VolatileSame(in,out);CHECK(Vec(out.bossPoint,in.stackPoint));
    allowed=false;Put<void*>(f.root.bytes,0x520,nullptr);out={};g.run(&in,&out);
    CHECK(out.path==0 && out.gpr[2]==0 && out.nonvolatile[3]==0xFFFFFFFFull && out.nonvolatile[4]==0 && (out.flags&0x8C5)==0x44);VolatileSame(in,out,2);
    Put<void*>(f.root.bytes,0x520,f.childWeak);in.target=target;out={};g.run(&in,&out);
    CHECK(out.path==2 && out.gpr[2]==reinterpret_cast<std::uintptr_t>(f.childWeak) && out.nonvolatile[4]==reinterpret_cast<std::uintptr_t>(target));VolatileSame(in,out,2);
    Put<LONG>(f.gunWeak,8,0);allowed=true;in.target=nullptr;Put<void*>(f.root.bytes,0x520,nullptr);out={};g.run(&in,&out);
    CHECK(out.path==0 && out.gpr[2]==0);Put<LONG>(f.gunWeak,8,1);

    constexpr unsigned point=0x44E36F;constexpr unsigned char pointBytes[]={0x49,0x83,0xBE,0x78,0x03,0,0,0,0x75,0x09,0x4D,0x8D,0x86,0x90,0,0,0};
    auto h=SpecialHarness(point,false);Leaf(0x44E390,SpecialSnapshot(4,h.finish));
    // Execute the exact native body/lockpoint continuation, including ADD's flag side effects.
    const unsigned char continuation[]={0xEB,0x0E,0x49,0x8B,0x86,0x68,0x03,0,0,0x4C,0x8B,0,0x49,0x83,0xC0,0x10};
    std::memcpy(image+0x44E380,continuation,sizeof(continuation));std::memcpy(image+point,pointBytes,sizeof(pointBytes));CHECK(InstallBoss(1));
    in.target=nullptr;out={};allowed=true;h.run(&in,&out);
    CHECK(out.path==4 && Vec(out.bossPoint,wanted) && out.gpr[3]==out.frameBase+0x147);CHECK((out.flags&0x8D5)==(in.flags&0x8D5));VolatileSame(in,out,3);
    // The native gate does not enter this point block without a real target. Its null branch was tested above.
    in.target=target;allowed=false;Put<std::uint64_t>(target,0x378,0);out={};h.run(&in,&out);
    CHECK(out.path==4 && out.gpr[3]==reinterpret_cast<std::uintptr_t>(target)+0x90 && Vec(out.bossPoint,in.stackPoint));CHECK((out.flags&0x8C5)==0x44);VolatileSame(in,out,3);
    Put<std::uint64_t>(target,0x378,1);out={};h.run(&in,&out);
    CHECK(out.path==4 && out.gpr[3]==reinterpret_cast<std::uintptr_t>(lockpoint)+0x10 && out.gpr[0]==reinterpret_cast<std::uintptr_t>(lockList));
    CHECK(Vec(out.bossPoint,in.stackPoint));for(unsigned i=1;i<7;++i)if(i!=3)CHECK(out.gpr[i]==in.gpr[i]);
    for(unsigned i=0;i<6;++i)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));allowed=true;
    // Run the real continuation's 231110 call ABI after the already-verified raw checkpoint.
    Bytes solver;Append(solver,{0xF3,0x44,0x0F,0x11,0x4C,0x24,0x20,0x48,0x8D,0x55,0xC7,0xE8});
    Dword(solver,static_cast<unsigned>(0x231110-(0x44E390+16)));CodeAt(0x44E390,solver);
    Leaf(0x44E3A0,SpecialSnapshot(5,h.finish));Leaf(0x231110,reinterpret_cast<const void*>(&NativeBossBallistic));
    in.gpr[1]=reinterpret_cast<std::uintptr_t>(f.gun+0x40);in.gpr[4]=reinterpret_cast<std::uintptr_t>(f.gun+0x1E0);
    const float speed=16;std::memcpy(in.xmm9,&speed,4);in.target=nullptr;out={};bossSolveCalls=0;h.run(&in,&out);
    CHECK(out.path==5 && bossSolveCalls==1 && bossContext==f.gun+0x40 && bossMuzzle==reinterpret_cast<const float*>(f.gun+0x1E0));
    CHECK(bossSpeed==16 && Vec(bossTarget,wanted) && Vec(out.bossPoint,ballisticAnswer));
    CHECK(bossInput==bossOutput && reinterpret_cast<std::uintptr_t>(bossInput)==out.frameBase+0x147);
}


void WalkingCases() {
    Object root("EDF6_Berserker_Large");controlled=root.bytes;unsigned char control[0x20]{},roster[16]{},weak[16]{};float point[4]{};
    Put<void*>(control,0x10,root.bytes);Put<void*>(roster,0,control);Put<void*>(roster,8,weak);Put<LONG>(weak,8,1);
    Put<void*>(root.bytes,0x1AA8,roster);Put<std::size_t>(root.bytes,0x1AB8,1);
    walkingControl=control;walkingEnabled=true;allowed=false;walkingQueries=0;const unsigned beforeCore=coreQueries;
    CHECK(LargePoint(control,point)==point && walkingQueries==1 && coreQueries==beforeCore);
    const float walkingPoint[4]={310,320,330,1};CHECK(Vec(point,walkingPoint));
    Put<void*>(roster,0,nullptr);const unsigned beforeWalking=walkingQueries;nativeCalls=0;
    CHECK(LargePoint(control,point)==point && nativeCalls==1 && walkingQueries==beforeWalking);Put<void*>(roster,0,control);
    walkingEnabled=false;allowed=true;const float corePoint[4]={110,120,30,1};
    CHECK(LargePoint(control,point)==point && Vec(point,corePoint));walkingControl=nullptr;
}
void* PartsSnapshot(const void* finish) {
    Bytes b;for(unsigned i=0;i<7;++i)MemReg(b,false,regs[i],12,offsetof(RegisterOutput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,false,i,12,offsetof(RegisterOutput,xmm)+i*16);
    Append(b,{0x9C,0x41,0x8F,0x84,0x24});Disp(b,offsetof(RegisterOutput,flags));
    MemReg(b,false,13,12,offsetof(RegisterOutput,frameBase));MemReg(b,false,15,12,offsetof(RegisterOutput,partsOwner));
    MemReg(b,false,5,12,offsetof(RegisterOutput,nonvolatile));
    VectorMemory(b,true,0,13,0x70);VectorMemory(b,false,0,12,offsetof(RegisterOutput,partsSource));
    VectorMemory(b,true,0,13,0x170);VectorMemory(b,false,0,12,offsetof(RegisterOutput,partsCopy));
    Append(b,{0x41,0xC7,0x84,0x24});Disp(b,offsetof(RegisterOutput,path));Dword(b,6);Jump(b,finish);return Executable(b);
}
RunFn PartsHarness(const StackSite& site) {
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!memory)std::exit(2);
    Bytes b;Append(b,{0x53,0x56,0x57,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x28,0x02,0,0});
    Append(b,{0x49,0x89,0xE5,0x49,0x89,0xCE,0x49,0x89,0xD4}); // R12=output; R15 must remain the real native root
    VectorMemory(b,false,6,13,0x210);VectorMemory(b,true,0,14,offsetof(RegisterInput,stackPoint));VectorMemory(b,false,0,13,0x70);
    MemReg(b,true,0,14,offsetof(RegisterInput,auxiliary));MemReg(b,false,0,13,0x150);
    Append(b,{0x49,0x8D,0xAD});Dword(b,0x180);
    for(unsigned i=0;i<7;++i)MemReg(b,true,regs[i],14,offsetof(RegisterInput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,true,i,14,offsetof(RegisterInput,xmm)+i*16);
    Append(b,{0x41,0xFF,0xB6});Disp(b,offsetof(RegisterInput,flags));Append(b,{0x9D});
    MemReg(b,true,15,14,offsetof(RegisterInput,owner));Jump(b,image+site.at);void* finish=memory+b.size();
    VectorMemory(b,true,6,13,0x210);Append(b,{0x48,0x81,0xC4,0x28,0x02,0,0,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5D,0x5F,0x5E,0x5B,0xC3});
    std::memcpy(memory,b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),memory,b.size());Leaf(site.at+site.length,PartsSnapshot(finish));return reinterpret_cast<RunFn>(memory);
}
void PartsCases() {
    unsigned index=0;while(index<std::size(kStackSites) && kStackSites[index].at!=0x3403D1)++index;
    CHECK(index<std::size(kStackSites));if(index>=std::size(kStackSites))return;
    const auto& site=kStackSites[index];Object root("EDF6_Berserker_TypeBommer");controlled=root.bytes;
    RegisterInput in=StackInput(root.bytes);in.auxiliary=reinterpret_cast<const void*>(0x12349876);RegisterOutput out{};
    std::memcpy(image+site.at,site.original,site.length);RunFn run=PartsHarness(site);CHECK(InstallStack(index));
    const float wanted[4]={110,120,30,1};
    for(bool enabled:{true,false}) {
        allowed=enabled;out={};run(&in,&out);const float* expected=enabled ? wanted : in.stackPoint;
        CHECK(out.path==6 && out.partsOwner==reinterpret_cast<std::uintptr_t>(root.bytes));
        CHECK(Vec(out.partsSource,expected) && Vec(out.partsCopy,expected) && Vec(reinterpret_cast<const float*>(out.xmm[0]),expected));
        CHECK(out.gpr[0]==reinterpret_cast<std::uintptr_t>(in.auxiliary) && out.nonvolatile[0]==out.frameBase+0x180);
        for(unsigned i=1;i<7;++i)CHECK(out.gpr[i]==in.gpr[i]);for(unsigned i=1;i<6;++i)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));
        CHECK((out.flags&0x8D5)==(in.flags&0x8D5));
    }
    allowed=true;
}
unsigned inverseCalls=0,spinnerCalls=0,spinnerFallbacks=0,mermanFallbacks=0;
void* fanControl=nullptr;const void* fanTarget=nullptr;float fanWorld[4]{},inverseInput[16]{};
constexpr float fanInverse[16]={0,0,1,0,0,1,0,0,-1,0,0,0,30,-20,-10,1};
void NativeInverse(float* matrix) {++inverseCalls;std::memcpy(inverseInput,matrix,64);std::memcpy(matrix,fanInverse,64);}
void NativeSpinner(void* control,const void* target) {
    ++spinnerCalls;fanControl=control;fanTarget=target;std::memcpy(fanWorld,target,16);Put<unsigned>(control,0x9C,0x5AA5);
}
void NativeSpinnerFallback(void* control,const void* target) {++spinnerFallbacks;fanControl=control;fanTarget=target;}
void NativeMermanFallback(void* control,const void* target) {++mermanFallbacks;fanControl=control;fanTarget=target;}
void FanCases() {
    Leaf(0x71810,reinterpret_cast<const void*>(&NativeInverse));Leaf(0x3DA890,reinterpret_cast<const void*>(&NativeSpinner));
    Leaf(0x3DB140,reinterpret_cast<const void*>(&NativeSpinnerFallback));Leaf(0x3A9CF0,reinterpret_cast<const void*>(&NativeMermanFallback));
    Object spinner("EDF6_SpinnerUfo");controlled=spinner.bytes;alignas(16) unsigned char control[0xA0]{},unit1[0x300]{},unit2[0x300]{},list[0x60]{};
    const float world[16]={0,0,-1,0,0,1,0,0,1,0,0,0,10,20,30,1};
    std::memcpy(control+0x30,world,64);Put<void*>(control,0x78,list);Put<std::size_t>(control,0x88,2);Put<void*>(spinner.bytes,0x11A8,control);
    for(auto* unit:{unit1,unit2}) {Put<void*>(unit,0x2B0,spinner.bytes);Put<void*>(unit,0x2B8,spinner.weak);}
    Put<void*>(list,0,unit1);Put<void*>(list,0x30,unit2);unsigned char target[0x400]{};
    const auto before=std::array<unsigned char,0xA0>{};std::array<unsigned char,0xA0> expected=before;std::memcpy(expected.data(),control,expected.size());
    const float local[4]={0,100,100,1},wanted[4]={110,120,30,1};std::memcpy(expected.data()+0x20,local,16);Put<unsigned>(expected.data(),0x9C,0x5AA5);
    inverseCalls=spinnerCalls=spinnerFallbacks=0;SpinnerTarget(control,nullptr);
    CHECK(spinnerCalls==1 && spinnerFallbacks==0 && inverseCalls==1 && fanControl==control && Vec(fanWorld,wanted));
    CHECK(Vec(reinterpret_cast<const float*>(control+0x20),local) && !std::memcmp(inverseInput,world,64));
    CHECK(!std::memcmp(control,expected.data(),expected.size()));
    allowed=false;const unsigned invBefore=inverseCalls;SpinnerTarget(control,target);
    CHECK(spinnerFallbacks==1 && fanControl==control && fanTarget==target && inverseCalls==invBefore);allowed=true;
    Put<LONG>(spinner.weak,8,0);SpinnerTarget(control,target);CHECK(spinnerFallbacks==2 && inverseCalls==invBefore);Put<LONG>(spinner.weak,8,1);
    Object foreign("EDF6_SpinnerUfo");Put<void*>(unit2,0x2B0,foreign.bytes);Put<void*>(unit2,0x2B8,foreign.weak);SpinnerTarget(control,target);
    CHECK(spinnerFallbacks==3 && inverseCalls==invBefore);Put<void*>(unit2,0x2B0,spinner.bytes);Put<void*>(unit2,0x2B8,spinner.weak);
    Put<void*>(spinner.bytes,0x11A8,nullptr);SpinnerTarget(control,target);CHECK(spinnerFallbacks==4 && fanTarget==target);Put<void*>(spinner.bytes,0x11A8,control);

    Object merman("EDF6_Merman");controlled=merman.bytes;alignas(16) unsigned char mermanControl[0x80]{},mermanUnit[0x300]{};
    for(unsigned i=0;i<sizeof(mermanControl);++i)mermanControl[i]=static_cast<unsigned char>(i+1);
    Put<void*>(mermanControl,0,mermanUnit);Put<void*>(mermanUnit,0x2B0,merman.bytes);Put<void*>(mermanUnit,0x2B8,merman.weak);Put<void*>(merman.bytes,0x1C10,mermanControl);
    std::array<unsigned char,0x80> mermanExpected{};std::memcpy(mermanExpected.data(),mermanControl,mermanExpected.size());std::memcpy(mermanExpected.data()+0x10,wanted,16);
    mermanFallbacks=0;MermanTarget(mermanControl,nullptr);
    CHECK(mermanFallbacks==0 && !std::memcmp(mermanControl,mermanExpected.data(),mermanExpected.size()));
    allowed=false;MermanTarget(mermanControl,target);CHECK(mermanFallbacks==1 && fanControl==mermanControl && fanTarget==target);allowed=true;
    Put<LONG>(merman.weak,8,0);MermanTarget(mermanControl,nullptr);CHECK(mermanFallbacks==2 && fanTarget==nullptr);Put<LONG>(merman.weak,8,1);
    Put<void*>(merman.bytes,0x1C10,nullptr);MermanTarget(mermanControl,target);CHECK(mermanFallbacks==3 && fanTarget==target);

    Object dyno("EDF6_FlyingDyno");controlled=dyno.bytes;alignas(16) unsigned char mainUnit[0x300]{},verticalUnit[0x300]{};unsigned char dt[16]{};
    const float old[4]={-10,-20,-30,1};
    for(auto* unit:{mainUnit,verticalUnit}) {Put<void*>(unit,0x2B0,dyno.bytes);Put<void*>(unit,0x2B8,dyno.weak);Put<unsigned>(unit,0x2C0,0xA5);std::memcpy(unit+0x2D0,old,16);}
    Put<void*>(dyno.bytes,0x1498,verticalUnit);UnitPoint(mainUnit,dt);
    CHECK(nativeFlags==0xA7 && Vec(nativePoint,wanted) && At<unsigned>(mainUnit,0x2C0)==0xA5 && Vec(reinterpret_cast<const float*>(mainUnit+0x2D0),old));
    UnitPoint(verticalUnit,dt);CHECK(nativeFlags==0xA5 && Vec(nativePoint,old) && nativeA==verticalUnit && nativeB==dt);
    CHECK(At<unsigned>(verticalUnit,0x2C0)==0xA5 && Vec(reinterpret_cast<const float*>(verticalUnit+0x2D0),old));
}


void AcceptedPointCases() {
    Object root("EDF6_Berserker_TypeShooter");controlled=root.bytes;acceptedOwner=root.bytes;float point[4]={-1,-2,-3,-4};
    acceptedEnabled=true;const unsigned queries=coreQueries;
    CHECK(Point(root.bytes,point) && coreQueries==queries);const float frozen[4]={-210,220,-230,1};CHECK(Vec(point,frozen));
    acceptedPoint[0]=std::numeric_limits<float>::quiet_NaN();const float untouched[4]={4,3,2,1};std::memcpy(point,untouched,16);
    CHECK(!Point(root.bytes,point) && Vec(point,untouched) && coreQueries==queries);acceptedPoint[0]=-210;
    acceptedEnabled=false;const float live[4]={110,120,30,1};CHECK(Point(root.bytes,point) && Vec(point,live) && coreQueries==queries+1);
    acceptedEnabled=true;Put<void*>(root.bytes,0,nullptr);const unsigned acceptedBefore=acceptedQueries;
    CHECK(!Point(root.bytes,point) && acceptedQueries==acceptedBefore);acceptedEnabled=false;acceptedOwner=nullptr;
}
void SeedCall(unsigned at,unsigned target) {Bytes b;Append(b,{0xE8});Dword(b,target-(at+5));CodeAt(at,b);}
void AllReady(bool expected) {for(const auto& p:profiles)CHECK(EnemyControlWeaponAimReady(&p)==expected);}
void ReadinessCases() {
    // A second image holds exact original byte contracts; typed runtime leaves in the first image stay isolated.
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)std::exit(2);
    admittedImage=nullptr;ready=false;crew::enemy_weapon_aim::bossContext=false;shellCode=nullptr;deiroiMainCode=nullptr;meteorCode=nullptr;
    for(void*& code:inlineCode)code=nullptr;for(void*& code:stackCode)code=nullptr;for(void*& code:bossCode)code=nullptr;
    for(bool& flag:redirected)flag=false;std::memset(callBytes,0,sizeof(callBytes));
    for(const auto& print:kPrints)std::memcpy(image+print.rva,print.bytes,sizeof(print.bytes));
    constexpr unsigned callPairs[][2]={
        {0x34E231,0x34E470},{0x34F978,0x34E470},{0x6D550B,0x6D1800},{0x45448D,0x450920},
        {0x455B62,0x450920},{0x4545B7,0x4A1D40},{0x455B96,0x4A1D40},{0x46F96C,0x4A1D40},
        {0x40B13C,0x4054E0},{0x4BA0B5,0x6BB5A0},{0x3D87AD,0x3DB140},{0x3D87DF,0x3DB140},
        {0x44500D,0x44E640},{0x447703,0x44DE70},{0x443B5E,0x448230},{0x44E40A,0x548EF0},
        {0x3A027D,0x3A9CF0},{0x3A85DE,0x3A9CF0},{0x31CB9A,0x6CFBC0},
    };
    CHECK(std::size(callPairs)==std::size(callBytes));if constexpr(std::size(callPairs)!=std::size(callBytes))return;for(const auto& pair:callPairs)SeedCall(pair[0],pair[1]);
    for(const auto& site:kInlineSites)std::memcpy(image+site.at,site.original,sizeof(site.original));
    for(const auto& site:kStackSites)std::memcpy(image+site.at,site.original,site.length);
    constexpr unsigned char shell[]={0x48,0x85,0xD2,0x75,0x1D,0x33,0xC0,0xC7,0x41,0x18,0,0,0x80,0x3F};std::memcpy(image+0x3C466C,shell,sizeof(shell));
    constexpr unsigned char gate[]={0x48,0x8B,0x93,0x20,0x05,0,0,0xBE,0xFF,0xFF,0xFF,0xFF,0x48,0x85,0xD2};std::memcpy(image+0x44E1E6,gate,sizeof(gate));
    constexpr unsigned char bossPointBytes[]={0x49,0x83,0xBE,0x78,0x03,0,0,0,0x75,0x09,0x4D,0x8D,0x86,0x90,0,0,0};std::memcpy(image+0x44E36F,bossPointBytes,sizeof(bossPointBytes));
    constexpr unsigned char context[]={0x45,0x33,0xC0};std::memcpy(image+0x44E400,context,sizeof(context));
    constexpr unsigned char mainOriginal[]={0x48,0x85,0xC0,0x74,0x5A,0x48,0x8B,0x8F,0x20,0x01,0,0,0x48,0x8B,0x11};std::memcpy(image+0x31CC4B,mainOriginal,sizeof(mainOriginal));
    constexpr unsigned char meteorOriginal[]={0x48,0x8D,0x8E,0x20,0x01,0,0,0xE8,0x51,0x0E,0x33,0,0x84,0xC0};std::memcpy(image+0x443A93,meteorOriginal,sizeof(meteorOriginal));
    deiroiReady=true;const bool installed=InstallEnemyControlWeaponAim();CHECK(installed);if(!installed)return;CHECK(std::size(profiles)==34);AllReady(true);
    CHECK(InstallEnemyControlWeaponAim());AllReady(true);CHECK(!EnemyControlWeaponAimReady(nullptr));
    NativeProfile foreign=profiles[0];CHECK(!EnemyControlWeaponAimReady(&foreign));
    deiroiReady=false;for(const auto& p:profiles)CHECK(EnemyControlWeaponAimReady(&p)==(std::strcmp(p.name,"Deiroi")!=0));deiroiReady=true;
    auto foreignByte=[&](unsigned at){image[at]^=1;AllReady(false);image[at]^=1;};
    for(const auto& pair:callPairs)foreignByte(pair[0]+1);
    for(const auto& site:kInlineSites)foreignByte(site.at+6);
    for(const auto& site:kStackSites)foreignByte(site.at+6);
    foreignByte(0x443A93+6);foreignByte(0x31CC4B+6);foreignByte(0x3C466C+6);foreignByte(0x44E1E6+6);foreignByte(0x44E36F+6);foreignByte(0x44E400);
    for(const auto& print:kPrints)foreignByte(print.rva); // native helper fingerprints remain part of readiness after installation
    auto foreignAddressByte=[&](unsigned char* address) {
        DWORD previous=0;const bool writable=VirtualProtect(address,1,PAGE_EXECUTE_READWRITE,&previous)!=0;CHECK(writable);if(!writable)return;
        *address^=1;FlushInstructionCache(GetCurrentProcess(),address,1);AllReady(false);
        *address^=1;FlushInstructionCache(GetCurrentProcess(),address,1);DWORD ignored=0;
        CHECK(VirtualProtect(address,1,previous,&ignored)!=0);AllReady(true);
    };
    unsigned relayCount=0;const auto hooks=crew::enemy_weapon_aim::TypedHooks();
    for(unsigned i=0;i<std::size(callPairs);++i) {
        auto* call=image+callPairs[i][0];auto* destination=call+5+At<std::int32_t>(call,1);
        if(destination==hooks[i])continue;
        ++relayCount;CHECK(destination[0]==0x48 && destination[1]==0xB8 && At<const void*>(destination,2)==hooks[i]);
        unsigned char unchangedCall[5];std::memcpy(unchangedCall,call,5);
        for(unsigned offset:{2u,10u,11u}) {foreignAddressByte(destination+offset);CHECK(!std::memcmp(unchangedCall,call,5));}
    }
    CHECK(relayCount>0); // actual far-target installation above must exercise the near relay contract
    auto foreignStub=[&](void* code,const Bytes& expected,unsigned callsite,std::size_t width) {
        CHECK(code && expected.size()>32);if(!code || expected.size()<=32)return;
        Bytes original(image+callsite,image+callsite+width);auto* bytes=static_cast<unsigned char*>(code);
        foreignAddressByte(bytes+expected.size()/2);CHECK(!std::memcmp(image+callsite,original.data(),width));
        foreignAddressByte(bytes+expected.size()-1);CHECK(!std::memcmp(image+callsite,original.data(),width));
    };
    for(unsigned i=0;i<std::size(kInlineSites);++i)foreignStub(inlineCode[i],inlineBytes[i],kInlineSites[i].at,17);
    for(unsigned i=0;i<std::size(kStackSites);++i)foreignStub(stackCode[i],stackBytes[i],kStackSites[i].at,kStackSites[i].length);
    foreignStub(meteorCode,meteorBytes,0x443A93,14);foreignStub(deiroiMainCode,deiroiMainBytes,0x31CC4B,15);foreignStub(shellCode,shellBytes,0x3C466C,14);foreignStub(bossCode[0],bossBytes[0],0x44E1E6,15);foreignStub(bossCode[1],bossBytes[1],0x44E36F,17);
    AllReady(true);
}


void BossLockCases() {
    BossFixture f;controlled=f.root.bytes;lockOwner=f.root.bytes;unsigned char oldTarget[0x400]{},selectedLock[0x80]{},selectedControl[32]{},pair[16]{};
    Put<LONG>(selectedControl,8,1);Put<LONG>(selectedControl,12,1);Put<void*>(selectedControl,16,selectedLock);
    lockObject=selectedLock;lockControl=selectedControl;weakCalls=lockQueries=0;allowed=true;
    std::memset(pair,0xEE,16);lockReply=2;
    CHECK(BossLockWeak(oldTarget,pair,f.gun)==pair && weakCalls==0 && lockQueries==1 && lockSawEmpty);
    CHECK(At<const void*>(pair,0)==selectedLock && At<const void*>(pair,8)==selectedControl);
    CHECK(At<LONG>(selectedControl,12)==2 && At<LONG>(selectedControl,8)==1); // genuine weak retain, no strong retain and no premature release
    CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(selectedControl+12))==1);std::memset(pair,0,16); // native recipient releases its retained pair
    std::memset(pair,0xEE,16);lockReply=1;
    CHECK(BossLockWeak(oldTarget,pair,f.gun)==pair && weakCalls==0 && lockQueries==2 && lockSawEmpty);
    CHECK(At<std::uint64_t>(pair,0)==0 && At<std::uint64_t>(pair,8)==0 && At<LONG>(selectedControl,12)==1);
    std::memset(pair,0xEE,16);lockReply=0;
    CHECK(BossLockWeak(oldTarget,pair,f.gun)==pair && weakCalls==0 && lockQueries==3 && lockSawEmpty);
    CHECK(At<std::uint64_t>(pair,0)==0 && At<std::uint64_t>(pair,8)==0); // failed owned cache never resurrects old AI homing
    lockReply=2;CHECK(BossLockWeak(nullptr,pair,f.gun)==pair && weakCalls==0 && lockQueries==4 && At<const void*>(pair,0)==selectedLock);
    CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(selectedControl+12))==1);std::memset(pair,0,16);
    alignas(16) unsigned char foreignGun[0x300]{};std::memcpy(foreignGun,f.gun,sizeof(foreignGun));const unsigned before=lockQueries;
    CHECK(BossLockWeak(oldTarget,pair,foreignGun)==pair+8 && weakCalls==1 && lockQueries==before);
    CHECK(weakTarget==oldTarget && weakOutput==pair && weakMode==0);
    CHECK(BossLockWeak(nullptr,pair,foreignGun)==pair+8 && weakCalls==2 && weakTarget==nullptr && lockQueries==before);
    Put<LONG>(f.root.weak,8,0);CHECK(BossLockWeak(oldTarget,pair,f.gun)==pair+8 && weakCalls==3 && lockQueries==before);Put<LONG>(f.root.weak,8,1);
    canonical[0]=std::numeric_limits<float>::quiet_NaN();CHECK(BossLockWeak(oldTarget,pair,f.gun)==pair+8 && weakCalls==4 && lockQueries==before);canonical[0]=110;
    lockReply=0;lockOwner=lockObject=nullptr;lockControl=nullptr;
}


unsigned mainGetterCalls=0,mainMatrixCalls=0;const void* mainGetterObject=nullptr;void* mainParams=nullptr;const void* mainMatrixHolder=nullptr;const float* mainMatrixPointer=nullptr;
void* MainParamsGetter(const void* object) {++mainGetterCalls;mainGetterObject=object;return mainParams;}
void NativeMainMatrix(void* holder,const float* matrix) {++mainMatrixCalls;mainMatrixHolder=holder;mainMatrixPointer=matrix;std::memcpy(nativeMatrix,matrix,64);}
void* MainSnapshot(unsigned path,const void* finish) {
    Bytes b;for(unsigned i=0;i<7;++i)MemReg(b,false,regs[i],15,offsetof(RegisterOutput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,false,i,15,offsetof(RegisterOutput,xmm)+i*16);
    Append(b,{0x9C,0x41,0x8F,0x87});Disp(b,offsetof(RegisterOutput,flags));MemReg(b,false,13,15,offsetof(RegisterOutput,frameBase));MemReg(b,false,7,15,offsetof(RegisterOutput,nonvolatile)+8);
    VectorMemory(b,true,0,13,0x30);VectorMemory(b,false,0,15,offsetof(RegisterOutput,stack30));
    VectorMemory(b,true,0,13,0x80);VectorMemory(b,false,0,15,offsetof(RegisterOutput,saved7));
    Append(b,{0x41,0xC7,0x87});Disp(b,offsetof(RegisterOutput,path));Dword(b,path);Jump(b,finish);return Executable(b);
}
RunFn MainHarness() {
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!memory)std::exit(2);
    Bytes b;Append(b,{0x53,0x56,0x57,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x28,0x02,0,0});
    Append(b,{0x49,0x89,0xE5,0x49,0x89,0xCE,0x49,0x89,0xD7});VectorMemory(b,false,6,13,0x210);
    VectorMemory(b,true,0,14,offsetof(RegisterInput,stackPoint));VectorMemory(b,false,0,13,0x30);
    VectorMemory(b,true,0,14,offsetof(RegisterInput,xmm6));VectorMemory(b,false,0,13,0x80);
    MemReg(b,true,7,14,offsetof(RegisterInput,owner));
    for(unsigned i=0;i<7;++i)MemReg(b,true,regs[i],14,offsetof(RegisterInput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,true,i,14,offsetof(RegisterInput,xmm)+i*16);
    Append(b,{0x41,0xFF,0xB6});Disp(b,offsetof(RegisterInput,flags));Append(b,{0x9D});Jump(b,image+0x31CC4B);void* finish=memory+b.size();
    VectorMemory(b,true,6,13,0x210);Append(b,{0x48,0x81,0xC4,0x28,0x02,0,0,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5D,0x5F,0x5E,0x5B,0xC3});
    std::memcpy(memory,b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),memory,b.size());Leaf(0x31CCAA,MainSnapshot(7,finish));Leaf(0x31CC5A,MainSnapshot(8,finish));return reinterpret_cast<RunFn>(memory);
}
void DeiroiMainCases() {
    Object root("Deiroi");controlled=root.bytes;alignas(16) unsigned char rows[0x520]{},parameterObject[16]{},parameters[0xF0]{};void* table[3]={nullptr,nullptr,reinterpret_cast<void*>(&MainParamsGetter)};
    auto* row=rows+0x290;Put<void*>(rows,0,root.bytes);Put<void*>(row,0,root.bytes);Put<void*>(root.bytes,0x1638,rows);Put<std::size_t>(root.bytes,0x1648,2);
    Put<void*>(parameterObject,0,table);Put<void*>(row,0x120,parameterObject);Put<float>(row,0x280,16);mainParams=parameters;
    const void* owner=nullptr;CHECK(DeiroiMainOwner(row,&owner) && owner==root.bytes);
    alignas(16) unsigned char foreignRow[0x290]{};Put<void*>(foreignRow,0,root.bytes);CHECK(!DeiroiMainOwner(foreignRow,&owner));
    Put<LONG>(root.weak,8,0);CHECK(!DeiroiMainOwner(row,&owner));Put<LONG>(root.weak,8,1);
    Leaf(0x2312A0,reinterpret_cast<const void*>(&NativeBallistic));Leaf(0x6CFBC0,reinterpret_cast<const void*>(&NativeMainMatrix));
    const float muzzle[4]={10,20,30,1},wanted[4]={110,120,30,1};float point[4]{};mainGetterCalls=ballisticCalls=0;allowed=true;
    CHECK(DeiroiMainPoint(row,muzzle,point));CHECK(mainGetterCalls==1 && mainGetterObject==parameterObject && ballisticCalls==1);
    CHECK(ballisticContext==parameters+0x30 && ballisticMuzzle==muzzle && ballisticSpeed==16 && Vec(ballisticTarget,wanted) && Vec(point,ballisticAnswer));
    CHECK(At<float>(parameters,0x44)==123.25f);
    const float s=std::sqrt(0.5f);alignas(16) const float matrix[16]={2*s,2*s,0,0,-3*s,3*s,0,0,0,0,4,0,10,20,30,1};
    mainMatrixCalls=0;DeiroiMainMatrix(row+0x100,matrix);
    CHECK(mainMatrixCalls==1 && mainMatrixHolder==row+0x100 && mainMatrixPointer!=matrix && Near(nativeMatrix[2],-2));
    CHECK(!std::memcmp(matrix+12,nativeMatrix+12,16) && Near(Length(nativeMatrix),2) && Near(Length(nativeMatrix+4),3) && Near(Length(nativeMatrix+8),4));
    allowed=false;DeiroiMainMatrix(row+0x100,matrix);CHECK(mainMatrixCalls==2 && mainMatrixPointer==matrix && !std::memcmp(nativeMatrix,matrix,64));allowed=true;
    DeiroiMainMatrix(foreignRow+0x100,matrix);CHECK(mainMatrixCalls==3 && mainMatrixHolder==foreignRow+0x100 && mainMatrixPointer==matrix);

    constexpr unsigned char original[]={0x48,0x85,0xC0,0x74,0x5A,0x48,0x8B,0x8F,0x20,0x01,0,0,0x48,0x8B,0x11};
    std::memcpy(image+0x31CC4B,original,sizeof(original));RunFn run=MainHarness();CHECK(InstallDeiroiMain());
    RegisterInput in=StackInput(row);std::memcpy(in.xmm6,muzzle,16);in.gpr[0]=0;RegisterOutput out{};mainGetterCalls=ballisticCalls=0;
    std::array<unsigned char,0x520> rowsBefore{};std::memcpy(rowsBefore.data(),rows,rowsBefore.size());run(&in,&out);
    CHECK(out.path==7 && Vec(out.stack30,ballisticAnswer) && Vec(out.saved7,muzzle) && out.nonvolatile[1]==reinterpret_cast<std::uintptr_t>(row));
    CHECK(ballisticCalls==1 && mainGetterCalls==1 && mainGetterObject==parameterObject && ballisticContext==parameters+0x30);
    CHECK(ballisticMuzzle==reinterpret_cast<const float*>(out.frameBase+0x80) && ballisticSpeed==16 && Vec(ballisticTarget,wanted));
    CHECK(!std::memcmp(rowsBefore.data(),rows,rowsBefore.size()));
    for(unsigned i=0;i<7;++i)CHECK(out.gpr[i]==in.gpr[i]);for(unsigned i=0;i<6;++i)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));CHECK((out.flags&0x8D5)==(in.flags&0x8D5));
    allowed=false;out={};run(&in,&out);CHECK(out.path==7 && Vec(out.stack30,in.stackPoint) && ballisticCalls==1 && mainGetterCalls==1);
    CHECK((out.flags&0x8C5)==0x44);for(unsigned i=0;i<7;++i)CHECK(out.gpr[i]==in.gpr[i]);for(unsigned i=0;i<6;++i)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));
    unsigned char target[0x400]{};in.gpr[0]=reinterpret_cast<std::uintptr_t>(target);out={};run(&in,&out);
    CHECK(out.path==8 && Vec(out.stack30,in.stackPoint) && out.gpr[1]==reinterpret_cast<std::uintptr_t>(parameterObject) && out.gpr[2]==reinterpret_cast<std::uintptr_t>(table));
    for(unsigned i=0;i<7;++i)if(i!=1 && i!=2)CHECK(out.gpr[i]==in.gpr[i]);for(unsigned i=0;i<6;++i)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));
    allowed=true;in.gpr[0]=0;mainParams=nullptr;out={};run(&in,&out);CHECK(out.path==7 && Vec(out.stack30,in.stackPoint) && ballisticCalls==1);mainParams=parameters;
    Put<float>(row,0x280,0);out={};run(&in,&out);CHECK(Vec(out.stack30,in.stackPoint) && ballisticCalls==1);Put<float>(row,0x280,16);
    Put<LONG>(root.weak,8,0);const unsigned getters=mainGetterCalls;out={};run(&in,&out);CHECK(Vec(out.stack30,in.stackPoint) && mainGetterCalls==getters);Put<LONG>(root.weak,8,1);
}


RegisterOutput* meteorOutput=nullptr;
void NativeMeteorLocal(void* owner,const float* origin,const float* direction) {
    ++meteorOutput->localCalls;meteorOutput->meteorOwner=reinterpret_cast<std::uintptr_t>(owner);
    meteorOutput->meteorOriginPointer=reinterpret_cast<std::uintptr_t>(origin);meteorOutput->meteorDirectionPointer=reinterpret_cast<std::uintptr_t>(direction);
    std::memcpy(meteorOutput->localDirection,direction,16);std::memcpy(meteorOutput->meteorOrigin,origin,16);
}
void MeteorContinuation(void* owner,unsigned char* originalFrame,RegisterOutput* out,unsigned gate) {
    const auto* direction=reinterpret_cast<const float*>(originalFrame+0x20);const auto* origin=reinterpret_cast<const float*>(originalFrame+0x30);
    if(gate){++out->sendCalls;std::memcpy(out->sentDirection,direction,16);}
    const unsigned queries=coreQueries;const float previous=canonical[0];canonical[0]=-1000; // a new reticle cannot re-aim a packet already frozen above
    meteorOutput=out;BossMeteor(owner,origin,direction);canonical[0]=previous;out->delegateQueries=coreQueries-queries;
}
void* MeteorGateProbe(unsigned response) {
    Bytes b;
    for(unsigned i=0;i<7;++i)MemReg(b,false,regs[i],15,offsetof(RegisterOutput,gateGpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,false,i,15,offsetof(RegisterOutput,gateXmm)+i*16);
    Append(b,{0x9C,0x41,0x8F,0x87});Disp(b,offsetof(RegisterOutput,gateFlags));
    Append(b,{0x41,0xFF,0x87});Disp(b,offsetof(RegisterOutput,gateCalls));
    Append(b,{0xB0,static_cast<unsigned char>(response),0xC3});return Executable(b); // AL is native-owned; no fixture clobber of the other input registers
}
void* MeteorSnapshot(const void* finish) {
    Bytes b;
    for(unsigned i=0;i<7;++i)MemReg(b,false,regs[i],15,offsetof(RegisterOutput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,false,i,15,offsetof(RegisterOutput,xmm)+i*16);
    Append(b,{0x9C,0x41,0x8F,0x87});Disp(b,offsetof(RegisterOutput,flags));MemReg(b,false,13,15,offsetof(RegisterOutput,frameBase));
    Append(b,{0x41,0xC7,0x87});Disp(b,offsetof(RegisterOutput,path));Dword(b,10);
    Append(b,{0x44,0x0F,0xB6,0xC8,0x48,0x89,0xF1,0x4C,0x89,0xEA,0x4D,0x89,0xF8,0x48,0xB8});Address(b,reinterpret_cast<const void*>(&MeteorContinuation));Append(b,{0xFF,0xD0});Jump(b,finish);return Executable(b);
}
RunFn MeteorHarness() {
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!memory)std::exit(2);
    Bytes b;Append(b,{0x53,0x56,0x57,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x28,0x02,0,0});
    Append(b,{0x49,0x89,0xE5,0x49,0x89,0xCE,0x49,0x89,0xD7});VectorMemory(b,false,6,13,0x210);
    VectorMemory(b,true,0,14,offsetof(RegisterInput,stackPoint));VectorMemory(b,false,0,13,0x20);
    VectorMemory(b,true,0,14,offsetof(RegisterInput,xmm6));VectorMemory(b,false,0,13,0x30);MemReg(b,true,6,14,offsetof(RegisterInput,owner));
    for(unsigned i=0;i<7;++i)MemReg(b,true,regs[i],14,offsetof(RegisterInput,gpr)+i*8);
    for(unsigned i=0;i<6;++i)VectorMemory(b,true,i,14,offsetof(RegisterInput,xmm)+i*16);
    Append(b,{0x41,0xFF,0xB6});Disp(b,offsetof(RegisterInput,flags));Append(b,{0x9D});Jump(b,image+0x443A93);void* finish=memory+b.size();
    VectorMemory(b,true,6,13,0x210);Append(b,{0x48,0x81,0xC4,0x28,0x02,0,0,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5D,0x5F,0x5E,0x5B,0xC3});
    std::memcpy(memory,b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),memory,b.size());Leaf(0x443AA1,MeteorSnapshot(finish));return reinterpret_cast<RunFn>(memory);
}
void MeteorCases() {
    Object root("Humanoid_BigGreyBoss");controlled=root.bytes;
    Leaf(0x448230,reinterpret_cast<const void*>(&NativeMeteorLocal));
    constexpr unsigned char original[]={0x48,0x8D,0x8E,0x20,0x01,0,0,0xE8,0x51,0x0E,0x33,0,0x84,0xC0};
    std::memcpy(image+0x443A93,original,sizeof(original));RunFn run=MeteorHarness();CHECK(InstallMeteor());
    RegisterInput in=StackInput(root.bytes);const float direction[4]={0,0,5,7},origin[4]={10,20,30,1};std::memcpy(in.stackPoint,direction,16);std::memcpy(in.xmm6,origin,16);
    const float expected[4]={5*std::sqrt(0.5f),5*std::sqrt(0.5f),0,7};
    for(bool enable:{true,false})for(unsigned gate:{0u,1u}) {
        Leaf(0x7748F0,MeteorGateProbe(gate));allowed=enable;RegisterOutput out{};run(&in,&out);
        CHECK(out.path==10 && out.gateCalls==1 && out.sendCalls==gate && out.localCalls==1 && out.delegateQueries==0);
        const float* wanted=enable ? expected : direction;CHECK(Vec(out.localDirection,wanted) && Vec(out.meteorOrigin,origin));
        CHECK(out.meteorOwner==reinterpret_cast<std::uintptr_t>(root.bytes) && out.meteorOriginPointer==out.frameBase+0x30 && out.meteorDirectionPointer==out.frameBase+0x20);
        if(gate)CHECK(!std::memcmp(out.sentDirection,out.localDirection,12));
        for(unsigned i=0;i<7;++i) {
            const auto before=i==1 ? reinterpret_cast<std::uintptr_t>(root.bytes+0x120) : in.gpr[i];CHECK(out.gateGpr[i]==before);
            const auto after=i==0 ? (in.gpr[0]&~0xFFull)|gate : before;CHECK(out.gpr[i]==after);
        }
        for(unsigned i=0;i<6;++i)CHECK(!std::memcmp(out.gateXmm[i],in.xmm[i],16) && !std::memcmp(out.xmm[i],in.xmm[i],16));
        CHECK((out.gateFlags&0x8D5)==(in.flags&0x8D5));CHECK((out.flags&0x8C5)==(gate ? 0u : 0x44u));
    }
    allowed=true;RegisterOutput guest{};meteorOutput=&guest;const float wire[4]={-4,3,2,99};const unsigned before=coreQueries;
    BossMeteor(root.bytes,origin,wire);CHECK(guest.localCalls==1 && !std::memcmp(guest.localDirection,wire,16) && guest.meteorDirectionPointer==reinterpret_cast<std::uintptr_t>(wire) && coreQueries==before);
    const float save[3]={canonical[0],canonical[1],canonical[2]};std::memcpy(canonical,origin,12);RegisterOutput coincident{};run(&in,&coincident);
    CHECK(!std::memcmp(coincident.localDirection,direction,16));std::memcpy(canonical,save,12);
}

}

namespace crew {
unsigned char* image=nullptr;
bool EnemyControlWeaponLock(const void* owner,void* out16) noexcept {
    ++fixture::lockQueries;fixture::lockSawEmpty=At<std::uint64_t>(out16,0)==0 && At<std::uint64_t>(out16,8)==0;
    if(owner!=fixture::lockOwner || fixture::lockReply==0)return false;
    if(fixture::lockReply==2) {
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(fixture::lockControl+12));
        Put<const void*>(out16,0,fixture::lockObject);Put<const void*>(out16,8,fixture::lockControl);
    }
    return true;
}
bool EnemyControlWeaponAim(const void* owner,float out[3]) noexcept {
    ++fixture::coreQueries;if(!fixture::allowed || owner!=fixture::controlled)return false;
    std::memcpy(out,fixture::canonical,12);if(fixture::scribble)fixture::scribble();return true;
}
namespace enemy_native {
bool DeiroiWeaponAimReady() noexcept {return fixture::deiroiReady;}
bool WalkingAcceptedAimPoint(const void* owner,float out[3]) noexcept {
    ++fixture::acceptedQueries;if(!fixture::acceptedEnabled || owner!=fixture::acceptedOwner)return false;std::memcpy(out,fixture::acceptedPoint,12);return true;
}
bool WalkingGunAimPoint(const void* control,float out[3]) noexcept {
    ++fixture::walkingQueries;if(!fixture::walkingEnabled || control!=fixture::walkingControl)return false;out[0]=310;out[1]=320;out[2]=330;return true;
}
const NativeProfile* Profiles(std::size_t* count) noexcept {if(count)*count=sizeof(fixture::profiles)/sizeof(fixture::profiles[0]);return fixture::profiles;}
}
}
int main() {
    using namespace fixture;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    unsigned i=0;for(const auto& p:profiles)Put<const void*>(image+p.vtableRva-8,0,image+0x2000000+(i++)*0x20);
    admittedImage=image;ready=true;
    Leaf(0x6D1800,reinterpret_cast<const void*>(&NativeUnit));Leaf(0x34E470,reinterpret_cast<const void*>(&NativeLarge));
    Leaf(0x6BB5A0,reinterpret_cast<const void*>(&NativeLocator));Leaf(0x4054E0,reinterpret_cast<const void*>(&NativeSpawn));
    Bytes noise;
    for(unsigned reg:regs) {Append(noise,{static_cast<unsigned char>(reg>=8 ? 0x49 : 0x48),static_cast<unsigned char>(0xB8+(reg&7))});Address(noise,reinterpret_cast<const void*>(0xFEED000000000000ull+reg));}
    for(unsigned reg=0;reg<6;++reg)Append(noise,{0x0F,0x57,static_cast<unsigned char>(0xC0+reg*9)});
    Append(noise,{0xF9,0xC3});scribble=reinterpret_cast<ScribbleFn>(Executable(noise));
    TypedRuntimeCases();InlineCases();StackCases();ShellCases();BossCases();WalkingCases();PartsCases();FanCases();AcceptedPointCases();BossLockCases();DeiroiMainCases();MeteorCases();ReadinessCases();
    std::printf("enemy weapon aim runtime: %d checks, %d failures\n",checks,failures);return failures?1:0;
}








