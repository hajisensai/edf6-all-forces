// HUiHudRader keeps its real SoldierBase owner. Replace only its per-update
// observer values: native stack center/rotation and native hostile-walk team.
#include "enemy_control_radar.h"
#include "enemy_control_types.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace crew {
namespace enemy_radar {
constexpr ULONGLONG kFreshMs=500;
constexpr unsigned kCenter=0x82B7A5;
constexpr unsigned char kCenterBytes[]={0x0F,0x10,0x86,0x90,0,0,0,0x66,0x0F,0x7F,0x44,0x24,0x50};
struct Site { unsigned at;unsigned char bytes[6]; };
constexpr Site kHostileSites[]={
    {0x82B89A,{0x8B,0x96,0x14,0x03,0,0}},
    {0x82BB80,{0x8B,0x96,0x14,0x03,0,0}},
};
struct Print { unsigned at;unsigned char bytes[16];unsigned size; };
constexpr Print kPrints[]={
    {0x82B4D7,{0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57},11},
    {0x82B4E2,{0x48,0x8D,0xA8,0xA8,0xFE,0xFF,0xFF,0x48,0x81,0xEC,0x20,0x02,0,0},14},
    {0x82B4F8,{0x4C,0x8B,0xEA,0x4C,0x8B,0xF1},6},
    {0x82B541,{0x48,0x8B,0x91,0x80,0x07,0,0,0x48,0x85,0xD2,0x74,0xCF,0x83,0x7A,0x08,0},16},
    {0x82B553,{0x4C,0x8D,0xB9,0x78,0x07,0,0},7},
    {0x82B671,{0x0F,0x10,0x76,0x70,0x0F,0x10,0x86,0x80,0,0,0},11},
    {0x82B6D3,{0x48,0x8D,0x55,0xF0,0x48,0x8D,0x4D,0xB0,0xE8,0x20,0x2A,0x82,0xFF},13},
    {0x82B767,{0xF3,0x0F,0x11,0x44,0x24,0x60,0xF3,0x0F,0x11,0x7C,0x24,0x64},12},
    {0x82B788,{0xF3,0x0F,0x11,0x44,0x24,0x74,0xC7,0x44,0x24,0x78,0,0,0,0},14},
    {0x82B8C3,{0x48,0x8B,0x0D,0xAE,0x70,0x88,0x01,0xE8,0x51,0x56,0xDB,0xFF},12},
    {0x82BBA9,{0x48,0x8B,0x0D,0xC8,0x6D,0x88,0x01,0xE8,0x6B,0x53,0xDB,0xFF},12},
    {0x82C69A,{0x41,0x0F,0x10,0x10,0x48,0x8B,0xDA},7},
    {0x82C6A9,{0x41,0x0F,0x5C,0x19},4},
};
struct Row { std::uintptr_t object=0,control=0;float center[4]{},rotation[16]{};int team=-1; };
struct Published { Row rows[enemy_control::kMaxControllers]{};unsigned count=0;ULONGLONG at=0; };
SRWLOCK viewLock=SRWLOCK_INIT;
Published published{};
unsigned char* admittedImage=nullptr;
bool ready=false;
void* code[3]{};
unsigned char patches[3][sizeof(kCenterBytes)]{};

bool Finite(const float* values,unsigned count) noexcept {
    for(unsigned i=0;i<count;++i)if(!std::isfinite(values[i]))return false;
    return true;
}
bool Build(const View& in,Row& out) noexcept {
    if(!in.ownerObject || !in.ownerControl || in.team<0 || in.team>6 ||
       !Finite(in.center,3) || !Finite(in.side,3) || !Finite(in.forward,3))return false;
    const float horizontal=std::hypot(in.forward[0],in.forward[2]);
    const float pitch=-std::atan2(in.forward[1],horizontal);
    const float* heading=in.forward;
    float reversed[3];
    // Exactly the native 82B6E5/82B6FD fallback: a near-vertical forward
    // vector uses +side above pitch .75, -side below pitch -.75.
    if(pitch>0.75f)heading=in.side;
    else if(pitch<-.75f) {for(unsigned i=0;i<3;++i)reversed[i]=-in.side[i];heading=reversed;}
    if(std::hypot(heading[0],heading[2])<0.000001f)return false;
    const float yaw=std::atan2(heading[0],heading[2]);
    const float sine=std::sin(yaw),cosine=std::cos(yaw);
    out={};out.object=in.ownerObject;out.control=in.ownerControl;out.team=in.team;
    std::memcpy(out.center,in.center,12);out.center[3]=1;
    out.rotation[0]=cosine;out.rotation[1]=sine;
    out.rotation[4]=-sine;out.rotation[5]=cosine;
    out.rotation[10]=out.rotation[15]=1;
    return true;
}
bool Find(const void* hud,Row& out) noexcept {
    if(!ready || image!=admittedImage || !hud)return false;
    // These are this native HUD's own weak keys, already locked/cast by
    // 82B541..82B624. Never follow them to read a render-thread GameObject.
    std::uintptr_t object=0,control=0;
    __try {object=At<std::uintptr_t>(hud,0x778);control=At<std::uintptr_t>(hud,0x780);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    if(!object || !control)return false;
    const auto now=GetTickCount64();
    bool found=false;
    AcquireSRWLockShared(&viewLock);
    if(now>=published.at && now-published.at<=kFreshMs)
        for(unsigned i=0;i<published.count;++i)if(published.rows[i].object==object && published.rows[i].control==control) {
            out=published.rows[i];found=true;break;
        }
    ReleaseSRWLockShared(&viewLock);return found;
}
void __fastcall ObserverValues(const void* hud,float* center,float* rotation) noexcept {
    Row row{};if(!Find(hud,row))return;
    // Only the native update's value storage is written; its real owner,
    // body/physics, layout, scale and team registry remain untouched.
    std::memcpy(center,row.center,sizeof(row.center));
    std::memcpy(rotation,row.rotation,sizeof(row.rotation));
}
int __fastcall ObserverTeam(const void* hud,int fallback) noexcept {
    Row row{};return Find(hud,row) ? row.team : fallback;
}
using Bytes=std::vector<unsigned char>;
void Add(Bytes& b,std::initializer_list<unsigned char> bytes) {b.insert(b.end(),bytes.begin(),bytes.end());}
void U32(Bytes& b,unsigned value) {for(unsigned s=0;s<32;s+=8)b.push_back(static_cast<unsigned char>(value>>s));}
void Address(Bytes& b,const void* ptr) {
    const auto value=reinterpret_cast<std::uintptr_t>(ptr);
    for(unsigned s=0;s<64;s+=8)b.push_back(static_cast<unsigned char>(value>>s));
}
void Jump(Bytes& b,const void* ptr) {Add(b,{0xFF,0x25,0,0,0,0});Address(b,ptr);}
void Volatiles(Bytes& b,bool load) {
    constexpr unsigned char regs[]={0x44,0x4C,0x54,0x44,0x4C,0x54,0x5C};
    for(unsigned i=0;i<7;++i)Add(b,{static_cast<unsigned char>(i<3 ? 0x48 : 0x4C),static_cast<unsigned char>(load ? 0x8B : 0x89),regs[i],0x24,static_cast<unsigned char>(0x30+i*8)});
    for(unsigned i=0;i<6;++i) {Add(b,{0xF3,0x0F,static_cast<unsigned char>(load ? 0x6F : 0x7F),static_cast<unsigned char>(0x84+i*8),0x24});U32(b,0x70+i*16);}
}
Bytes Stub(unsigned index,const void* continuation) {
    Bytes b;
    const auto* original=index ? kHostileSites[index-1].bytes : kCenterBytes;
    const auto length=index ? sizeof(kHostileSites[0].bytes) : sizeof(kCenterBytes);
    b.insert(b.end(),original,original+length);
    // Native RSP is 16-byte aligned here. Flags + E8h produce an aligned
    // helper call, 20h shadow space, disjoint GPR and XMM0..5 save areas.
    Add(b,{0x9C,0x48,0x81,0xEC,0xE8,0,0,0});Volatiles(b,false);
    Add(b,{0x4C,0x89,0xF1}); // this HUiHudRader is R14 throughout its update
    if(!index) {
        Add(b,{0x48,0x8D,0x94,0x24});U32(b,0x140); // original RSP+50: center float4
        Add(b,{0x4C,0x8D,0x84,0x24});U32(b,0x150); // original RSP+60: rotation float4x4
    }
    Add(b,{0x48,0xB8});Address(b,index ? reinterpret_cast<const void*>(&ObserverTeam) : reinterpret_cast<const void*>(&ObserverValues));
    Add(b,{0xFF,0xD0});
    if(index)Add(b,{0x89,0x44,0x24,0x40}); // replace only native EDX, retaining all other incoming registers
    Volatiles(b,true);Add(b,{0x48,0x81,0xC4,0xE8,0,0,0,0x9D});Jump(b,continuation);return b;
}
bool Contract() noexcept {
    if(!image)return false;
    for(const auto& p:kPrints)if(!Matches(p.at,p.bytes,p.size))return false;
    if(!Matches(kCenter,kCenterBytes,sizeof(kCenterBytes)))return false;
    for(const auto& s:kHostileSites)if(!Matches(s.at,s.bytes,sizeof(s.bytes)))return false;
    return true;
}
bool written[3]{};
void Rollback() noexcept {
    for(unsigned reverse=3;reverse;--reverse) {
        const unsigned i=reverse-1,at=i ? kHostileSites[i-1].at : kCenter;
        const auto length=i ? sizeof(kHostileSites[0].bytes) : sizeof(kCenterBytes);
        const auto* original=i ? kHostileSites[i-1].bytes : kCenterBytes;
        if(written[i] && !edf::PatchCode(image+at,patches[i],original,length))continue;
        written[i]=false;if(code[i])VirtualFree(code[i],0,MEM_RELEASE);code[i]=nullptr;
    }
}
bool Install(unsigned index) {
    const unsigned at=index ? kHostileSites[index-1].at : kCenter;
    const auto length=index ? sizeof(kHostileSites[0].bytes) : sizeof(kCenterBytes);
    const auto* original=index ? kHostileSites[index-1].bytes : kCenterBytes;
    Bytes b=Stub(index,image+at+length);
    code[index]=AllocateNearCode(image+at,b.data(),b.size());if(!code[index])return false;
    auto* patch=patches[index];std::memset(patch,0x90,length);patch[0]=0xE9;
    const auto delta=static_cast<unsigned char*>(code[index])-(image+at+5);
    if(delta>0x7FFFFFFF || delta<-0x7FFFFFFF)return false;
    const auto relative=static_cast<std::int32_t>(delta);std::memcpy(patch+1,&relative,4);
    written[index]=edf::PatchCode(image+at,original,patch,length);return written[index];
}
}
void EnemyControlRadarPublish(const enemy_radar::View* views,unsigned count) noexcept {
    enemy_radar::Published next{};next.at=GetTickCount64();
    if(views && count<=enemy_control::kMaxControllers)for(unsigned i=0;i<count;++i) {
        enemy_radar::Row row{};if(!enemy_radar::Build(views[i],row))continue;
        bool duplicate=false;for(unsigned j=0;j<count;++j)if(i!=j && views[j].ownerObject==row.object)duplicate=true;
        if(!duplicate)next.rows[next.count++]=row;
    }
    AcquireSRWLockExclusive(&enemy_radar::viewLock);enemy_radar::published=next;ReleaseSRWLockExclusive(&enemy_radar::viewLock);
}
bool EnemyControlRadarObserverTeam(const void* hud,int* team) noexcept {
    if(!team)return false;enemy_radar::Row row{};if(!enemy_radar::Find(hud,row))return false;*team=row.team;return true;
}
bool InstallEnemyControlRadar() noexcept {
    using namespace enemy_radar;
    if(ready)return image==admittedImage;
    __try {
        if(!Contract())return false;
        for(unsigned i=0;i<3;++i)if(!Install(i)){Rollback();return false;}
        admittedImage=image;ready=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){Rollback();return false;}
}
}
