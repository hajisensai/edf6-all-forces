// Executes the production installed machine-code boundary, including replayed
// native loads, stack center/rotation writes, register and flags restoration.
#include "../src/enemy_control_radar.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <cstddef>
namespace crew { unsigned char* image=nullptr; }
namespace {
using namespace crew;
using namespace crew::enemy_radar;
unsigned checks=0,failures=0;
void Check(bool pass,const char* text,int line) {++checks;if(!pass){++failures;std::fprintf(stderr,"line %d: %s\n",line,text);}}
#define CHECK(x) Check((x),#x,__LINE__)
struct alignas(16) Result {
    float center[4]{},rotation[16]{};
    std::uint64_t regs[7]{};
    std::uint64_t flags=0;
    alignas(16) float xmm[6][4]{};
};
struct Actor {alignas(16) unsigned char bytes[0x320]{};};
struct Hud {alignas(16) unsigned char bytes[0x790]{};};
constexpr std::uint64_t kRegisters[]={0x1020304050607080ull,0x2131415161718191ull,0x32425262728292A2ull,
    0x435363738393A3B3ull,0x5464748494A4B4C4ull,0x65758595A5B5C5D5ull,0x768696A6B6C6D6E6ull};
void AbsoluteRegister(Bytes& b,unsigned index,std::uint64_t value) {
    constexpr unsigned char op[]={0xB8,0xB9,0xBA,0xB8,0xB9,0xBA,0xBB};
    Add(b,{static_cast<unsigned char>(index<3 ? 0x48 : 0x49),op[index]});Address(b,reinterpret_cast<const void*>(value));
}
void StoreRegister(Bytes& b,unsigned index,unsigned offset) {
    constexpr unsigned char modrm[]={0x83,0x8B,0x93,0x83,0x8B,0x93,0x9B};
    Add(b,{static_cast<unsigned char>(index<3 ? 0x48 : 0x4C),0x89,modrm[index]});U32(b,offset);
}
using Invoke=void(__fastcall*)(const void*,const void*,Result*);
Invoke MakeHarness(unsigned index) {
    Bytes b;Add(b,{0x53,0x56,0x41,0x56,0x48,0x81,0xEC,0xA0,0,0,0});
    Add(b,{0x4C,0x89,0xC3,0x49,0x89,0xCE,0x48,0x89,0xD6}); // RBX=result, R14=HUD, RSI=real source
    for(unsigned i=0;i<4;++i) {
        Add(b,{0x0F,0x10,0x83});U32(b,static_cast<unsigned>(offsetof(Result,rotation))+i*16);
        Add(b,{0x0F,0x11,0x84,0x24});U32(b,0x60+i*16);
    }
    for(unsigned i=0;i<6;++i) {Add(b,{0x0F,0x10,static_cast<unsigned char>(0x83+i*8)});U32(b,static_cast<unsigned>(offsetof(Result,xmm))+i*16);}
    Add(b,{0x31,0xC0}); // known arithmetic flags; movs and native replay must preserve them
    for(unsigned i=0;i<7;++i)AbsoluteRegister(b,i,kRegisters[i]);
    const auto site=index ? kHostileSites[index-1].at : kCenter;
    Jump(b,image+site);const auto continuation=b.size();
    for(unsigned i=0;i<7;++i)StoreRegister(b,i,static_cast<unsigned>(offsetof(Result,regs))+i*8);
    for(unsigned i=0;i<6;++i) {Add(b,{0x0F,0x11,static_cast<unsigned char>(0x83+i*8)});U32(b,static_cast<unsigned>(offsetof(Result,xmm))+i*16);}
    Add(b,{0x9C,0x58});StoreRegister(b,0,static_cast<unsigned>(offsetof(Result,flags)));
    Add(b,{0x0F,0x10,0x44,0x24,0x50,0x0F,0x11,0x03});
    for(unsigned i=0;i<4;++i) {
        Add(b,{0x0F,0x10,0x84,0x24});U32(b,0x60+i*16);
        Add(b,{0x0F,0x11,0x83});U32(b,static_cast<unsigned>(offsetof(Result,rotation))+i*16);
    }
    Add(b,{0x48,0x81,0xC4,0xA0,0,0,0,0x41,0x5E,0x5E,0x5B,0xC3});
    auto* page=static_cast<unsigned char*>(AllocateNearCode(image+site,b.data(),b.size()));if(!page)std::exit(2);
    const auto length=index ? sizeof(kHostileSites[0].bytes) : sizeof(kCenterBytes);
    Bytes jump;Jump(jump,page+continuation);std::memcpy(image+site+length,jump.data(),jump.size());
    return reinterpret_cast<Invoke>(page);
}
void Seed(Result& out) {
    out={};for(unsigned i=0;i<16;++i)out.rotation[i]=100+static_cast<float>(i);
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<4;++j)out.xmm[i][j]=static_cast<float>(i*20+j+1);
}
bool Close(float a,float b) {return std::fabs(a-b)<0.00001f;}
void Preserved(const Result& after,const Result& before,const Actor& source,unsigned index,int selected) {
    for(unsigned i=0;i<7;++i)CHECK(after.regs[i]==(index && i==2 ? static_cast<std::uint64_t>(selected) : kRegisters[i]));
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<4;++j)
        CHECK(after.xmm[i][j]==(!index && !i ? At<float>(source.bytes,0x90+j*4) : before.xmm[i][j]));
    CHECK((after.flags&0x8D5)==0x44);
}
void SetHud(Hud& hud,std::uintptr_t object,std::uintptr_t control) {Put<std::uintptr_t>(hud.bytes,0x778,object);Put<std::uintptr_t>(hud.bytes,0x780,control);}
void Native(const Invoke* invoke,Hud& hud,const Actor& source,const Row* expected) {
    const Actor sourceBefore=source;const Hud hudBefore=hud;
    for(unsigned index=0;index<3;++index) {
        Result out{};Seed(out);const Result before=out;invoke[index](hud.bytes,source.bytes,&out);
        const int team=expected ? expected->team : At<int>(source.bytes,0x314);
        Preserved(out,before,source,index,team);
        for(unsigned i=0;i<4;++i)CHECK(Close(out.center[i],!index && expected ? expected->center[i] : (!index ? At<float>(source.bytes,0x90+i*4) : 0)));
        for(unsigned i=0;i<16;++i)CHECK(Close(out.rotation[i],!index && expected ? expected->rotation[i] : before.rotation[i]));
    }
    CHECK(!std::memcmp(&source,&sourceBefore,sizeof(source)));CHECK(!std::memcmp(&hud,&hudBefore,sizeof(hud)));
}
}
int main() {
    using namespace crew;using namespace crew::enemy_radar;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    for(const auto& p:kPrints)std::memcpy(image+p.at,p.bytes,p.size);
    std::memcpy(image+kCenter,kCenterBytes,sizeof(kCenterBytes));
    for(const auto& s:kHostileSites)std::memcpy(image+s.at,s.bytes,sizeof(s.bytes));
    image[kPrints[0].at]^=1;CHECK(!InstallEnemyControlRadar());CHECK(!std::memcmp(image+kCenter,kCenterBytes,sizeof(kCenterBytes)));image[kPrints[0].at]^=1;
    CHECK(InstallEnemyControlRadar());CHECK(InstallEnemyControlRadar());
    Invoke invoke[3]={MakeHarness(0),MakeHarness(1),MakeHarness(2)};
    Actor source{};for(unsigned i=0;i<4;++i)Put<float>(source.bytes,0x90+i*4,static_cast<float>(10+i));Put<int>(source.bytes,0x314,2);
    Hud first{},second{},ordinary{},reused{};
    SetHud(first,0x1000,0x2000);SetHud(second,0x3000,0x4000);SetHud(ordinary,0x5000,0x6000);SetHud(reused,0x1000,0x9000);
    View views[2]={{0x1000,0x2000,{10000,400,-8000},{0,0,-1},{1,0,0},1},
                   {0x3000,0x4000,{-7000,-200,9000},{1,0,0},{0,0,-1},2}};
    Row rows[2]{};CHECK(Build(views[0],rows[0]));CHECK(Build(views[1],rows[1]));EnemyControlRadarPublish(views,2);
    Native(invoke,first,source,rows);Native(invoke,second,source,rows+1);
    Native(invoke,ordinary,source,nullptr);Native(invoke,reused,source,nullptr);
    int selected=99;CHECK(EnemyControlRadarObserverTeam(first.bytes,&selected) && selected==1);CHECK(!EnemyControlRadarObserverTeam(ordinary.bytes,&selected));
    // Real body heading, including the native steep-pitch side-vector fallback.
    CHECK(Close(rows[0].rotation[0],0) && Close(rows[0].rotation[1],1));
    View vertical=views[0];vertical.forward[0]=0;vertical.forward[1]=1;vertical.forward[2]=0;vertical.side[0]=1;vertical.side[2]=0;
    Row pitched{};CHECK(Build(vertical,pitched));CHECK(Close(pitched.rotation[1],-1));
    vertical.forward[1]=-1;CHECK(Build(vertical,pitched));CHECK(Close(pitched.rotation[1],1));
    EnemyControlRadarPublish(nullptr,0);Native(invoke,first,source,nullptr);
    EnemyControlRadarPublish(views,2);published.at=GetTickCount64()-kFreshMs-1;Native(invoke,first,source,nullptr);
    View invalid=views[0];invalid.center[0]=std::numeric_limits<float>::quiet_NaN();EnemyControlRadarPublish(&invalid,1);Native(invoke,first,source,nullptr);
    invalid=views[0];invalid.forward[0]=invalid.forward[1]=invalid.forward[2]=0;CHECK(!Build(invalid,pitched));
    invalid=views[0];invalid.team=7;CHECK(!Build(invalid,pitched));
    View duplicate[2]={views[0],views[0]};EnemyControlRadarPublish(duplicate,2);Native(invoke,first,source,nullptr);
    EnemyControlRadarPublish(views,enemy_control::kMaxControllers+1);Native(invoke,first,source,nullptr);
    auto* nativeImage=image;image=nullptr;Native(invoke,first,source,nullptr);image=nativeImage;
    std::printf("enemy radar production fixture: %u checks, %u failures; native stack/ABI, split views, real teams, fallback and source immutability\n",checks,failures);
    return failures ? 1 : 0;
}
