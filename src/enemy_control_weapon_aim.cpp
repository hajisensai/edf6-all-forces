// Native skill aim points for admitted, actively leased enemy roots. Full instruction contract:
// tests/enemy_control_weapon_aim_native_audit.py. Non-controlled calls preserve their original ABI and arguments.
#include "enemy_control_weapon_aim.h"
#include "enemy_control.h"
#include "enemy_control_native.h"
#include "enemy_control_deiroi.h"
#include "enemy_control_walking_guns.h"
#include "enemy_control_walking_net.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace crew {
namespace enemy_weapon_aim {
struct Print { unsigned rva;unsigned char bytes[16]; };
constexpr Print kPrints[]={
    {0x450920,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0xFA}},
    {0x4A1D40,{0x0F,0x10,0x02,0x0F,0x11,0x81,0x20,0x07,0x00,0x00,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {0x4054E0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57}},
    {0x6BB5A0,{0x48,0x8B,0xC4,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x4C,0x89,0x70,0x20,0x55}},
    {0x2312A0,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x50,0x41,0x0F,0x10,0x00,0x48,0x8B}},
    {0x71810,{0x48,0x8B,0xC4,0x48,0x81,0xEC,0xF8,0x00,0x00,0x00,0xF3,0x0F,0x10,0x1A,0xF3,0x0F}},
    {0x6D1800,{0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x18,0x55,0x57,0x41,0x56,0x49,0x8D,0xAB,0x48,0xFF}},
    {0x34E470,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xF9,0x48,0x8B,0xDA}},
    {0x34E480,{0x48,0x8B,0x49,0x10,0xE8,0x67,0xDC,0xFB,0xFF,0x48,0x85,0xC0,0x74,0x48,0x48,0x83}},
    {0x6D51FD,{0x4C,0x8B,0x81,0xB8,0x02,0x00,0x00,0x4D,0x85,0xC0,0x74,0x31,0x41,0x8B,0x40,0x08}},
    {0x6D1980,{0xF6,0x83,0xC0,0x02,0x00,0x00,0x02,0x74,0x34,0x0F,0x10,0x93,0xD0,0x02,0x00,0x00}},
    {0x3DB140,{0x40,0x53,0x48,0x83,0xEC,0x60,0x48,0x8B,0xD9,0x48,0x85,0xD2,0x75,0x1A,0x33,0xC0}},
    {0x3DA890,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56}},
    {0x3A9CF0,{0x48,0x85,0xD2,0x75,0x15,0x33,0xC0,0xC7,0x41,0x18,0x00,0x00,0xC8,0x42,0x48,0x89}},
    {0x44E640,{0x0F,0x28,0x02,0x0F,0x29,0x81,0x10,0x0B,0x00,0x00,0x0F,0x28,0x4A,0x10,0x0F,0x29}},
    {0x44DE70,{0x0F,0x28,0x02,0x0F,0x29,0x81,0x10,0x0B,0x00,0x00,0x0F,0x28,0x4A,0x10,0x0F,0x29}},
    {0x448230,{0x48,0x8B,0xC4,0x4C,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x57,0x41}},
    {0x548EF0,{0x4D,0x63,0xC8,0x4C,0x3B,0x89,0x78,0x03,0x00,0x00,0x73,0x34,0x45,0x85,0xC0,0x78}},
    {0x231110,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x40,0x41,0x0F,0x10,0x00,0x48,0x8B}},
    {0x6CFBC0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x83,0xB9,0x00,0x01,0x00,0x00,0x00,0x48,0x8B}},
    {0x7748F0,{0x48,0x8B,0x05,0x99,0xDF,0x93,0x01,0x8B,0x48,0x38,0x83,0xF9,0xFF,0x75,0x03,0x32}},
};
unsigned char* admittedImage=nullptr;
constexpr unsigned kTypedCount=19;
bool ready=false,redirected[kTypedCount]{};
unsigned char callBytes[kTypedCount][5]{};
using PointFn=float*(__fastcall*)(void*,float*);
using UnitFn=void(__fastcall*)(void*,void*);
const enemy_native::NativeProfile* Family(const void* owner) noexcept {
    if(!Readable(owner,0x38))return nullptr;
    const auto table=At<const unsigned char*>(owner,0);
    if(!Readable(table-8,8))return nullptr;
    const auto col=At<const void*>(table-8,0);
    std::size_t count=0;const auto profiles=enemy_native::Profiles(&count);
    for(std::size_t i=0;i<count;++i) {
        const auto native=image+profiles[i].vtableRva;
        if(Readable(native-8,8) && At<const void*>(native-8,0)==col)return profiles+i;
    }
    return nullptr;
}
bool Point(const void* owner,float* out) noexcept {
    if(!ready || image!=admittedImage || !out || !Family(owner))return false;
    float point[3];
    // Accepted native bursts retain their host-frozen point; future actions still use the current Core reticle.
    if(!enemy_native::WalkingAcceptedAimPoint(owner,point) && !EnemyControlWeaponAim(owner,point))return false;
    for(float value:point)if(!std::isfinite(value))return false;
    std::memcpy(out,point,12);out[3]=1.0f;return true;
}
// At each native inline emitter the target point precedes its ORIGINAL gravity/speed solver.
// The callback bypasses only old AI-target coordinate selection, including the null-target forward fallback.
struct InlineSite { unsigned at,pointPath,nullPath,ownerRegister;unsigned char original[17];unsigned targetRegister=3;bool pointPointer=false; };
constexpr InlineSite kInlineSites[]={
    {0x40DD1A,0x40DD47,0x40DFA7,6,{0x48,0x85,0xDB,0x0F,0x84,0x84,0x02,0x00,0x00,0x48,0x83,0xBB,0x78,0x03,0x00,0x00,0x00}},
    {0x4246EC,0x424719,0x424980,6,{0x48,0x85,0xDB,0x0F,0x84,0x8B,0x02,0x00,0x00,0x48,0x83,0xBB,0x78,0x03,0x00,0x00,0x00}},
    {0x33AF42,0x33AF6F,0x33B1CA,7,{0x48,0x85,0xDB,0x0F,0x84,0x7F,0x02,0x00,0x00,0x48,0x83,0xBB,0x78,0x03,0x00,0x00,0x00}},
    {0x5351ED,0x535217,0x53537B,6,{0x48,0x85,0xC0,0x0F,0x84,0x85,0x01,0x00,0x00,0x48,0x83,0xB8,0x78,0x03,0x00,0x00,0x00},0,true},
    {0x52C2A3,0x52C2CF,0x52C537,6,{0x48,0x85,0xC0,0x0F,0x84,0x8B,0x02,0x00,0x00,0x48,0x83,0xB8,0x78,0x03,0x00,0x00,0x00},0,false},
};
constexpr unsigned kInlineCount=sizeof(kInlineSites)/sizeof(kInlineSites[0]);
void* inlineCode[kInlineCount]{};
bool __fastcall CachedPoint(const void* root,float* out) noexcept {
    __try {return Point(root,out);} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
using Bytes=std::vector<unsigned char>;
Bytes inlineBytes[kInlineCount];
void Append(Bytes& b,std::initializer_list<unsigned char> bytes) {b.insert(b.end(),bytes.begin(),bytes.end());}
void Address(Bytes& b,const void* address) {
    const auto value=reinterpret_cast<std::uintptr_t>(address);
    for(unsigned shift=0;shift<64;shift+=8)b.push_back(static_cast<unsigned char>(value>>shift));
}
void Dword(Bytes& b,unsigned value) {for(unsigned shift=0;shift<32;shift+=8)b.push_back(static_cast<unsigned char>(value>>shift));}
void Jump(Bytes& b,const void* destination) {Append(b,{0xFF,0x25,0,0,0,0});Address(b,destination);}
void Volatiles(Bytes& b,bool load) {
    // All volatile integer registers and all six volatile vector registers. Native nonvolatiles obey the x64 ABI.
    constexpr unsigned char modrm[]={0x44,0x4C,0x54,0x44,0x4C,0x54,0x5C};
    for(unsigned i=0;i<7;++i)Append(b,{static_cast<unsigned char>(i<3 ? 0x48 : 0x4C),static_cast<unsigned char>(load ? 0x8B : 0x89),modrm[i],0x24,static_cast<unsigned char>(0x30+i*8)});
    for(unsigned i=0;i<6;++i) {
        Append(b,{0xF3,0x0F,static_cast<unsigned char>(load ? 0x6F : 0x7F),static_cast<unsigned char>(0x84+i*8),0x24});Dword(b,0x70+i*16);
    }
}
void RestoreFrame(Bytes& b,bool point) {
    Volatiles(b,true);
    if(point)Append(b,{0x0F,0x10,0x54,0x24,0x20}); // actual point replaces ONLY xmm2 at the original point-consumer boundary
    Append(b,{0x48,0x81,0xC4,0xE8,0,0,0,0x9D}); // balance local frame and restore incoming flags
}
bool InstallInline(unsigned index) {
    if(inlineCode[index])return true;
    const auto& site=kInlineSites[index];
    if(!Readable(image+site.at,sizeof(site.original)) || std::memcmp(image+site.at,site.original,sizeof(site.original)))return false;
    Bytes b;Append(b,{0x9C,0x48,0x81,0xEC,0xE8,0,0,0});Volatiles(b,false);
    Append(b,{0x48,0x89,static_cast<unsigned char>(site.ownerRegister==6 ? 0xF1 : 0xF9),0x48,0x8D,0x54,0x24,0x20,0x48,0xB8});
    Address(b,reinterpret_cast<const void*>(&CachedPoint));Append(b,{0xFF,0xD0,0x84,0xC0,0x0F,0x84});
    const auto branch=b.size();Dword(b,0);
    if(site.pointPointer) {
        Append(b,{0x0F,0x10,0x44,0x24,0x20,0x0F,0x11,0x84,0x24});Dword(b,0x170); // emitter's real rsp+80 float4
        Volatiles(b,true);Append(b,{0x48,0x8D,0xBC,0x24});Dword(b,0x170); // rdi is a float4 pointer, never a GameObject
        Append(b,{0x48,0x81,0xC4,0xE8,0,0,0,0x9D});
    } else RestoreFrame(b,true);
    Jump(b,image+site.pointPath);
    const auto fallback=b.size();RestoreFrame(b,false);
    Append(b,{0x48,0x85,static_cast<unsigned char>(site.targetRegister==3 ? 0xDB : 0xC0),0x75,0x0E});Jump(b,image+site.nullPath); // exact native test rbx; je nullPath
    Append(b,{0x48,0x83,static_cast<unsigned char>(site.targetRegister==3 ? 0xBB : 0xB8),0x78,0x03,0,0,0});Jump(b,image+site.at+17); // native lockpoint comparison, its original JNE follows
    const auto relative=static_cast<unsigned>(fallback-(branch+4));
    for(unsigned i=0;i<4;++i)b[branch+i]=static_cast<unsigned char>(relative>>(8*i));
    void* code=edf::AllocateNearCode(image+site.at,b.data(),b.size());
    if(!code)return false;
    Bytes patch;Jump(patch,code);patch.resize(sizeof(site.original),0x90);
    if(!edf::PatchCode(image+site.at,site.original,patch.data(),patch.size())) {VirtualFree(code,0,MEM_RELEASE);return false;}
    inlineCode[index]=code;inlineBytes[index]=b;return true;
}

using BallisticFn=float*(__fastcall*)(void*,float*,const float*,const float*,float);
bool __fastcall BallisticPoint(const void* owner,float* out) noexcept {
    __try {
        const auto profile=Family(owner);if(!profile)return false;
        std::size_t context=0,muzzle=0,speed=0;
        if(std::strcmp(profile->name,"GiantSpider")==0 || std::strcmp(profile->name,"GiantSpiderEX")==0) {context=0x1AD0;muzzle=0x1CC0;speed=0x1CD4;}
        else if(std::strcmp(profile->name,"Nephila")==0) {context=0x1100;muzzle=0x10A0;speed=0x10C0;}
        else return false;
        const auto root=static_cast<const unsigned char*>(owner);alignas(16) float point[4];
        if(!Readable(root+context,0xC0,true) || !Readable(root+muzzle,16) || !Readable(root+speed,4) || !Point(owner,point))return false;
        const float velocity=At<float>(root,speed);
        if(!std::isfinite(velocity) || velocity<=0)return false;
        reinterpret_cast<BallisticFn>(image+0x2312A0)(const_cast<unsigned char*>(root)+context,out,point,
            reinterpret_cast<const float*>(root+muzzle),velocity);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool __fastcall SquidDirection(const void* control,float* out) noexcept {
    __try {
        if(!Readable(control,0x100))return false;
        const auto root=At<const unsigned char*>(control,0);
        const auto profile=Family(root);
        if(!profile || std::strcmp(profile->name,"EDF6_SquidSmall") || !Readable(root,0x13C0))return false;
        const auto list=At<const unsigned char*>(root,0x13A8);const auto count=At<std::size_t>(root,0x13B8);
        if(count>128 || !Readable(list,count*16))return false;
        bool member=false;
        for(std::size_t i=0;i<count;++i)if(At<const void*>(list+i*16,0)==control) {
            const auto weak=At<const unsigned char*>(list+i*16,8);member=Readable(weak,12) && At<LONG>(weak,8)>0;break;
        }
        float point[4];if(!member || !Point(root,point))return false;
        const auto muzzle=reinterpret_cast<const float*>(static_cast<const unsigned char*>(control)+0xF0);
        for(unsigned i=0;i<3;++i) {if(!std::isfinite(muzzle[i]))return false;out[i]=point[i]-muzzle[i];}
        out[3]=1;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

struct StackSite { unsigned at,ownerRegister;unsigned char original[24];unsigned length;bool gravityGlobal;bool humanoid=false;unsigned mode=0; };
constexpr StackSite kStackSites[]={
    {0x43613B,3,{0x48,0x8B,0x83,0xE8,0x03,0,0,0x48,0xBD,0x65,0x89,0x07,0x6C,0x65,0x8B,0x58,0x5D},17,false},
    {0x4E3714,6,{0x48,0x8B,0x05,0x3D,0xF2,0xBC,0x01,0x83,0xB8,0,0x0B,0,0,0},14,true},
    {0x46F572,14,{0x49,0x8B,0xBE,0x60,0x0B,0x00,0x00,0x4D,0x8B,0xA6,0x70,0x0B,0x00,0x00,0x49,0xC1,0xE4,0x04},18,false,true},
    {0x431CFC,7,{0x0F,0x11,0xB7,0x80,0x17,0x00,0x00,0x0F,0x29,0x74,0x24,0x50,0xF3,0x0F,0x10,0x87,0xA0,0x18,0x00,0x00},20,false,false,2},
    {0x3E729A,3,{0x48,0x8D,0x83,0x20,0x01,0x00,0x00,0x48,0x89,0x44,0x24,0x20,0xF3,0x0F,0x10,0x9B,0x54,0x01,0x00,0x00},20,false,false,3},
    {0x3403D1,15,{0x0F,0x28,0x44,0x24,0x70,0x66,0x0F,0x7F,0x45,0xF0,0x48,0x8B,0x45,0xD0},14,false,false,4},
};
constexpr unsigned kStackCount=sizeof(kStackSites)/sizeof(kStackSites[0]);
void* stackCode[kStackCount]{};
Bytes stackBytes[kStackCount];
bool InstallStack(unsigned index) {
    if(stackCode[index])return true;
    const auto& site=kStackSites[index];
    if(!Readable(image+site.at,site.length) || std::memcmp(image+site.at,site.original,site.length))return false;
    Bytes b;Append(b,{0x9C,0x48,0x81,0xEC,0xE8,0,0,0});Volatiles(b,false);
    if(site.mode==4)Append(b,{0x4C,0x89,0xF9}); // R15 is the admitted Berserker root during native detached-part construction
    else if(site.humanoid)Append(b,{0x49,0x8D,0x8E,0x40,0xFA,0xFF,0xFF}); // rcx = actual root+5C0 callback owner minus 5C0
    else Append(b,{0x48,0x89,static_cast<unsigned char>(site.ownerRegister==3 ? 0xD9 : site.ownerRegister==7 ? 0xF9 : 0xF1)});
    Append(b,{0x48,0x8D,0x54,0x24,0x20,0x48,0xB8});
    Address(b,site.mode==3 ? reinterpret_cast<const void*>(&SquidDirection) : site.humanoid || site.mode==2 || site.mode==4 ? reinterpret_cast<const void*>(&CachedPoint) : reinterpret_cast<const void*>(&BallisticPoint));Append(b,{0xFF,0xD0,0x84,0xC0,0x0F,0x84});
    const auto branch=b.size();Dword(b,0);
    // Copy the new native-solved vector to the ORIGINAL emitter's stack float4, before restoring xmm0.
    Append(b,{0x0F,0x10,0x44,0x24,0x20});
    if(site.humanoid)Append(b,{0x0F,0x11,0x45,0x80}); // original [rbp-80] common point, before every hand's own angle solver
    else if(site.mode==2)Append(b,{0x0F,0x28,0xF0}); // original rolling target is carried in nonvolatile xmm6
    else {Append(b,{0x0F,0x11,0x84,0x24});Dword(b,site.mode==3 ? 0x140 : site.mode==4 ? 0x160 : 0x120);}
    const auto fallback=b.size();RestoreFrame(b,false);
    if(site.gravityGlobal) {
        Append(b,{0x48,0xB8});Address(b,image+0x20B2958);Append(b,{0x48,0x8B,0x00,0x83,0xB8,0,0x0B,0,0,0});
    } else b.insert(b.end(),site.original,site.original+site.length);
    Jump(b,image+site.at+site.length);
    const auto relative=static_cast<unsigned>(fallback-(branch+4));
    for(unsigned i=0;i<4;++i)b[branch+i]=static_cast<unsigned char>(relative>>(8*i));
    void* code=edf::AllocateNearCode(image+site.at,b.data(),b.size());if(!code)return false;
    Bytes patch;Jump(patch,code);patch.resize(site.length,0x90);
    if(!edf::PatchCode(image+site.at,site.original,patch.data(),patch.size())) {VirtualFree(code,0,MEM_RELEASE);return false;}
    stackCode[index]=code;stackBytes[index]=b;return true;
}

bool LargeOwner(const void* control,const void** owner) noexcept {
    if(!Readable(control,0x18))return false;
    const auto root=At<const unsigned char*>(control,0x10);
    const auto profile=Family(root);
    if(!profile || std::strcmp(profile->name,"EDF6_Berserker_Large") || !Readable(root,0x1AC0))return false;
    const auto entries=At<const unsigned char*>(root,0x1AA8);
    const auto count=At<std::size_t>(root,0x1AB8);
    if(count>256 || !Readable(entries,count*16))return false;
    for(std::size_t i=0;i<count;++i)if(At<const void*>(entries+i*16,0)==control) {
        const auto weak=At<const unsigned char*>(entries+i*16,8);
        if(!Readable(weak,16) || At<LONG>(weak,8)<=0)return false;
        *owner=root;return true;
    }
    return false;
}
float* __fastcall LargePoint(void* control,float* out) {
    __try {
        const void* owner=nullptr;
        if(Readable(out,16,true) && LargeOwner(control,&owner)) {
            if(enemy_native::WalkingGunAimPoint(control,out)) {out[3]=1.0f;return out;}
            if(Point(owner,out))return out;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return reinterpret_cast<PointFn>(image+0x34E470)(control,out);
}
bool Normalize(const float* in,float* out) noexcept {
    const float length=std::sqrt(in[0]*in[0]+in[1]*in[1]+in[2]*in[2]);
    if(!std::isfinite(length) || length<0.00001f)return false;
    for(unsigned i=0;i<3;++i)out[i]=in[i]/length;return true;
}
void Cross(const float* a,const float* b,float* out) noexcept {
    out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0];
}
float Dot(const float* a,const float* b) noexcept {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool AimMatrix(const void* owner,float* matrix) noexcept {
    if(!Readable(matrix,64,true))return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return false;
    float point[4],a[3],b[3],delta[3];
    if(!Point(owner,point))return false;
    for(unsigned i=0;i<3;++i)delta[i]=point[i]-matrix[12+i];
    if(!Normalize(matrix+8,a) || !Normalize(delta,b))return false;
    const float dot=Dot(a,b),c=dot<-1 ? -1 : dot>1 ? 1 : dot;float v[3],axis[3]{};Cross(a,b,v);
    if(c<-0.99999f) {
        const float projection=Dot(matrix,a);float perpendicular[3];
        for(unsigned i=0;i<3;++i)perpendicular[i]=matrix[i]-projection*a[i];
        if(!Normalize(perpendicular,axis))return false;
    }
    float copy[16];std::memcpy(copy,matrix,64);
    for(unsigned row=0;row<3;++row) {
        const auto r=matrix+row*4;
        if(c<-0.99999f) {const float projection=Dot(axis,r);for(unsigned i=0;i<3;++i)copy[row*4+i]=2*axis[i]*projection-r[i];}
        else {float first[3],second[3];Cross(v,r,first);Cross(v,first,second);for(unsigned i=0;i<3;++i)copy[row*4+i]=r[i]+first[i]+second[i]/(1+c);}
    }
    std::memcpy(matrix,copy,64);return true;
}
using SpawnFn=void*(__fastcall*)(void*,void*,const float*,void*);
void* __fastcall AntSpawn(void* manager,void* out,const float* was,void* params) {
    alignas(16) float matrix[16];bool use=false;
    __try {
        const auto root=static_cast<unsigned char*>(params)-0x1680;
        const auto profile=Family(root);
        if(profile && (std::strcmp(profile->name,"GiantAnt")==0 || std::strcmp(profile->name,"GiantAntEX")==0) && Readable(was,64)) {
            std::memcpy(matrix,was,64);use=AimMatrix(root,matrix);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {use=false;}
    return reinterpret_cast<SpawnFn>(image+0x4054E0)(manager,out,use ? matrix : was,params);
}
using LocatorFn=float*(__fastcall*)(void*,float*);
float* __fastcall MonsterLocator(void* locator,float* out) {
    float* result=reinterpret_cast<LocatorFn>(image+0x6BB5A0)(locator,out);
    __try {
        auto* root=static_cast<unsigned char*>(locator)-0x6D8;
        const auto profile=Family(root);alignas(16) float point[4];
        if(profile && profile->kind==enemy_native::Kind::monster && Readable(root+0x6A0,0x20,true) && root[0x6A0] && Point(root,point)) {
            std::memcpy(root+0x6B0,point,16);root[0x6A1]=1;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

bool BossPartOwner(const void* child,const void** owner) noexcept {
    if(!Readable(child,0xAF8))return false;
    const auto root=At<const unsigned char*>(child,0xAE8);const auto rootControl=At<const unsigned char*>(child,0xAF0);
    const auto family=Family(root);
    if(!family || std::strcmp(family->name,"Humanoid_BigGreyBoss") || !Readable(root,0x17F0) ||
       !Readable(rootControl,24) || At<LONG>(rootControl,8)<=0 || At<const void*>(root,0x30)!=rootControl)return false;
    const auto self=At<const void*>(child,0x30);const auto head=At<const unsigned char*>(root,0x17E8);
    if(!Readable(head,0x50))return false;
    const auto* node=At<const unsigned char*>(head,0);
    for(unsigned n=0;node!=head && n<512;++n) {
        if(!Readable(node,0x50))return false;
        if(At<const void*>(node,0x40)==child) {
            const auto weak=At<const unsigned char*>(node,0x48);
            if(weak!=self || !Readable(weak,24) || At<LONG>(weak,8)<=0 || At<const void*>(weak,16)!=child)return false;
            *owner=root;return true;
        }
        node=At<const unsigned char*>(node,0);
    }
    return false;
}
bool BossGunOwner(const void* gun,const void** owner) noexcept {
    if(!Readable(gun,0x228))return false;
    const auto child=At<const unsigned char*>(gun,0x210);const auto childControl=At<const unsigned char*>(gun,0x218);
    const void* root=nullptr;
    if(!BossPartOwner(child,&root) || !Readable(child,0xB78) || childControl!=At<const void*>(child,0x30) ||
       At<const void*>(gun,0x200)!=root || At<const void*>(gun,0x208)!=At<const void*>(root,0x30))return false;
    const auto list=At<const unsigned char*>(child,0xB60);const auto count=At<std::size_t>(child,0xB70);
    if(count>128 || !Readable(list,count*16))return false;
    for(std::size_t i=0;i<count;++i)if(At<const void*>(list+i*16,0)==gun) {
        const auto weak=At<const unsigned char*>(list+i*16,8);
        if(!Readable(weak,12) || At<LONG>(weak,8)<=0)return false;
        *owner=root;return true;
    }
    return false;
}
bool __fastcall BossPoint(const void* gun,float* out) noexcept {
    __try {const void* owner=nullptr;return BossGunOwner(gun,&owner) && Point(owner,out);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
using MatrixSetFn=void(__fastcall*)(void*,const float*);
template<unsigned Original> void __fastcall BossMatrix(void* child,const float* was) {
    alignas(16) float matrix[16];bool use=false;
    __try {const void* owner=nullptr;if(BossPartOwner(child,&owner) && Readable(was,64)) {std::memcpy(matrix,was,64);use=AimMatrix(owner,matrix);}}
    __except(EXCEPTION_EXECUTE_HANDLER){use=false;}
    reinterpret_cast<MatrixSetFn>(image+Original)(child,use ? matrix : was);
}
using MeteorFn=void(__fastcall*)(void*,const float*,const float*);
bool __fastcall BossMeteorPoint(void* owner,const float* origin,float* direction) noexcept {
    __try {
        float point[4];const auto family=Family(owner);
        if(!family || std::strcmp(family->name,"Humanoid_BigGreyBoss") || !Readable(origin,16) ||
           !Readable(direction,16,true) || !Point(owner,point))return false;
        float delta[3],unit[3];for(unsigned i=0;i<3;++i)delta[i]=point[i]-origin[i];
        const float length=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
        if(!std::isfinite(length) || !Normalize(delta,unit))return false;
        // The same original stack direction is serialized to native peers and then passed to the local factory.
        for(unsigned i=0;i<3;++i)direction[i]=unit[i]*length;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void __fastcall BossMeteor(void* owner,const float* origin,const float* direction) {
    // Already adjusted before native sender/serialization. Re-aiming here would diverge host and guest shots.
    reinterpret_cast<MeteorFn>(image+0x448230)(owner,origin,direction);
}
void* meteorCode=nullptr;Bytes meteorBytes;
bool InstallMeteor() {
    if(meteorCode)return true;
    constexpr unsigned at=0x443A93;
    constexpr unsigned char original[]={0x48,0x8D,0x8E,0x20,0x01,0,0,0xE8,0x51,0x0E,0x33,0,0x84,0xC0};
    if(!Readable(image+at,sizeof(original)) || std::memcmp(image+at,original,sizeof(original)))return false;
    Bytes b;Append(b,{0x9C,0x48,0x81,0xEC,0xE8,0,0,0});Volatiles(b,false);
    Append(b,{0x48,0x89,0xF1,0x48,0x8D,0x94,0x24});Dword(b,0x120); // actual original rsp+30 origin
    Append(b,{0x4C,0x8D,0x84,0x24});Dword(b,0x110); // actual original rsp+20 direction, also the native packet payload
    Append(b,{0x48,0xB8});Address(b,reinterpret_cast<const void*>(&BossMeteorPoint));Append(b,{0xFF,0xD0});
    RestoreFrame(b,false);
    Append(b,{0x48,0x8D,0x8E,0x20,0x01,0,0}); // original native sender connection, after restoring all registers/flags
    Append(b,{0xFF,0x15,0x02,0,0,0,0xEB,0x08});Address(b,image+0x7748F0); // absolute call, no relocated rel32 or input-register clobber
    Append(b,{0x84,0xC0});Jump(b,image+0x443AA1); // exact native AL gate and host/guest/offline flags
    void* code=edf::AllocateNearCode(image+at,b.data(),b.size());if(!code)return false;
    Bytes patch;Jump(patch,code);
    if(!edf::PatchCode(image+at,original,patch.data(),patch.size())) {VirtualFree(code,0,MEM_RELEASE);return false;}
    meteorCode=code;meteorBytes=b;return true;
}
using LockWeakFn=void*(__fastcall*)(const void*,void*,int);
void* __fastcall BossLockWeak(const void* target,void* out,const void* gun) {
    float point[4];bool use=false;const void* owner=nullptr;
    __try {use=BossGunOwner(gun,&owner) && Point(owner,point);}
    __except(EXCEPTION_EXECUTE_HANDLER){use=false;}
    if(use && Readable(out,16,true)) {
        // The native receiver releases this genuine copied LockPoint weak reference. Empty free aim is valid.
        // Never send the canonical point through a GameObject ABI or fall back to an old AI homing target.
        std::memset(out,0,16);EnemyControlWeaponLock(owner,out);return out;
    }
    return reinterpret_cast<LockWeakFn>(image+0x548EF0)(target,out,0);
}
void* bossCode[2]{};
Bytes bossBytes[2];
bool InstallBoss(unsigned index) {
    if(bossCode[index])return true;
    constexpr unsigned sites[]={0x44E1E6,0x44E36F};constexpr unsigned lengths[]={15,17};
    constexpr unsigned char original[2][17]={
        {0x48,0x8B,0x93,0x20,0x05,0x00,0x00,0xBE,0xFF,0xFF,0xFF,0xFF,0x48,0x85,0xD2},
        {0x49,0x83,0xBE,0x78,0x03,0x00,0x00,0x00,0x75,0x09,0x4D,0x8D,0x86,0x90,0x00,0x00,0x00},
    };
    const unsigned at=sites[index],length=lengths[index];
    if(!Readable(image+at,length) || std::memcmp(image+at,original[index],length))return false;
    Bytes b;Append(b,{0x9C,0x48,0x81,0xEC,0xE8,0,0,0});Volatiles(b,false);
    Append(b,{0x48,0x89,0xF9,0x48,0x8D,0x54,0x24,0x20,0x48,0xB8});Address(b,reinterpret_cast<const void*>(&BossPoint));
    Append(b,{0xFF,0xD0,0x84,0xC0,0x0F,0x84});const auto branch=b.size();Dword(b,0);
    if(index==1)Append(b,{0x0F,0x10,0x44,0x24,0x20,0x0F,0x11,0x45,0xC7}); // true native float4 at rbp-39, not an object
    RestoreFrame(b,false);
    if(index==0) {
        Append(b,{0x49,0xC7,0xC6,0,0,0,0,0xBE,0xFF,0xFF,0xFF,0xFF});Jump(b,image+0x44E25E);
    } else {Append(b,{0x4C,0x8D,0x45,0xC7});Jump(b,image+0x44E390);}
    const auto fallback=b.size();RestoreFrame(b,false);
    if(index==0) {b.insert(b.end(),original[0],original[0]+length);Jump(b,image+at+length);}
    else {
        b.insert(b.end(),original[1],original[1]+8);Append(b,{0x74,0x0E});Jump(b,image+0x44E382);
        b.insert(b.end(),original[1]+10,original[1]+17);Jump(b,image+0x44E380);
    }
    const auto relative=static_cast<unsigned>(fallback-(branch+4));for(unsigned i=0;i<4;++i)b[branch+i]=static_cast<unsigned char>(relative>>(8*i));
    void* code=edf::AllocateNearCode(image+at,b.data(),b.size());if(!code)return false;
    Bytes patch;Jump(patch,code);patch.resize(length,0x90);
    if(!edf::PatchCode(image+at,original[index],patch.data(),patch.size())) {VirtualFree(code,0,MEM_RELEASE);return false;}
    bossCode[index]=code;bossBytes[index]=b;return true;
}
bool bossContext=false;
bool InstallBossContext() noexcept {
    if(bossContext)return true;
    constexpr unsigned char original[]={0x45,0x33,0xC0},replacement[]={0x4C,0x8B,0xC7}; // same-width explicit gun context; wrapper restores original zero on native fallback
    if(!edf::PatchCode(image+0x44E400,original,replacement,3))return false;
    bossContext=true;return true;
}

bool HumanoidOwner(const void* weapon,const void** owner) noexcept {
    if(!Readable(weapon,0x738))return false;
    const auto root=At<const unsigned char*>(weapon,0x5B0);
    const auto profile=Family(root);
    if(!profile || profile->kind!=enemy_native::Kind::humanoid || !Readable(root,0x1138))return false;
    const auto entries=At<const unsigned char*>(root,0x1120);
    const auto count=At<std::size_t>(root,0x1130);
    if(count>64 || !Readable(entries,count*16))return false;
    const auto self=At<const void*>(weapon,0x30);
    for(std::size_t i=0;i<count;++i)if(At<const void*>(entries+i*16,0)==weapon) {
        const auto weak=At<const unsigned char*>(entries+i*16,8);
        if(weak!=self || !Readable(weak,24) || At<LONG>(weak,8)<=0 || At<const void*>(weak,16)!=weapon)return false;
        *owner=root;return true;
    }
    return false;
}
using HumanoidPointFn=bool(__fastcall*)(void*,float*);
using HumanoidSetFn=void(__fastcall*)(void*,const float*);
bool __fastcall HumanoidPoint(void* weapon,float* out) {
    __try {
        const void* owner=nullptr;
        if(Readable(out,16,true) && HumanoidOwner(weapon,&owner) && Point(owner,out))return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return reinterpret_cast<HumanoidPointFn>(image+0x450920)(weapon,out);
}
void __fastcall HumanoidSet(void* weapon,const float* was) {
    alignas(16) float point[4];bool use=false;
    __try {const void* owner=nullptr;use=HumanoidOwner(weapon,&owner) && Point(owner,point);}
    __except(EXCEPTION_EXECUTE_HANDLER) {use=false;}
    reinterpret_cast<HumanoidSetFn>(image+0x4A1D40)(weapon,use ? point : was);
}

bool OwnerOfUnit(const void* unit,const void** owner) noexcept {
    if(!Readable(unit,0x2E0))return false;
    const auto root=At<const unsigned char*>(unit,0x2B0);const auto weak=At<const unsigned char*>(unit,0x2B8);
    if(!Readable(root,0x38) || !Readable(weak,16) || At<LONG>(weak,8)<=0 || At<const void*>(root,0x30)!=weak || !Family(root))return false;
    *owner=root;return true;
}
bool __fastcall ShellPoint(const void* control,float* out) noexcept {
    __try {
        if(!Readable(control,0x60))return false;
        const void* owner=nullptr;
        if(!OwnerOfUnit(At<const void*>(control,0),&owner))return false;
        const auto family=Family(owner);
        return family && std::strcmp(family->name,"EDF6_ShellFish")==0 && Point(owner,out);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
using ControlFn=void(__fastcall*)(void*,const void*);
void __fastcall SpinnerTarget(void* control,const void* target) {
    alignas(16) float point[4],inverse[16];bool use=false;
    __try {
        if(Readable(control,0xA0,true)) {
            const auto list=At<const unsigned char*>(control,0x78);const auto count=At<std::size_t>(control,0x88);
            if(count && count<=128 && Readable(list,count*0x30)) {
                const void* owner=nullptr;
                if(OwnerOfUnit(At<const void*>(list,0),&owner)) {
                    const auto family=Family(owner);
                    if(family && std::strcmp(family->name,"EDF6_SpinnerUfo")==0 && Readable(owner,0x11B0) &&
                       At<const void*>(owner,0x11A8)==control && Point(owner,point)) {
                        bool all=true;
                        for(std::size_t i=1;i<count;++i) {const void* other=nullptr;if(!OwnerOfUnit(At<const void*>(list+i*0x30,0),&other) || other!=owner)all=false;}
                        if(all) {
                            std::memcpy(inverse,static_cast<unsigned char*>(control)+0x30,64);
                            reinterpret_cast<void(__fastcall*)(float*)>(image+0x71810)(inverse);
                            float local[4]{};
                            for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i)local[j]+=point[i]*inverse[i*4+j];
                            std::memcpy(static_cast<unsigned char*>(control)+0x20,local,16);use=true;
                        }
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {use=false;}
    // Preserve native per-gun sweep construction and timeline interpolation; substitute only its original base point.
    if(use)reinterpret_cast<ControlFn>(image+0x3DA890)(control,point);
    else reinterpret_cast<ControlFn>(image+0x3DB140)(control,target);
}
void __fastcall MermanTarget(void* control,const void* target) {
    alignas(16) float point[4];bool use=false;
    __try {
        const void* owner=nullptr;
        if(Readable(control,0x80,true) && OwnerOfUnit(At<const void*>(control,0),&owner)) {
            const auto family=Family(owner);
            use=family && !std::strcmp(family->name,"EDF6_Merman") && Readable(owner,0x1C18) &&
                At<const void*>(owner,0x1C10)==control && Point(owner,point);
        }
        if(use)std::memcpy(static_cast<unsigned char*>(control)+0x10,point,16);
    } __except(EXCEPTION_EXECUTE_HANDLER) {use=false;}
    if(!use)reinterpret_cast<ControlFn>(image+0x3A9CF0)(control,target);
}

void* shellCode=nullptr;
Bytes shellBytes;
bool InstallShell() {
    if(shellCode)return true;
    constexpr unsigned at=0x3C466C;
    constexpr unsigned char original[]={0x48,0x85,0xD2,0x75,0x1D,0x33,0xC0,0xC7,0x41,0x18,0,0,0x80,0x3F};
    if(!Readable(image+at,sizeof(original)) || std::memcmp(image+at,original,sizeof(original)))return false;
    Bytes b;Append(b,{0x9C,0x48,0x81,0xEC,0xE8,0,0,0});Volatiles(b,false);
    Append(b,{0x48,0x89,0xD9,0x48,0x8D,0x54,0x24,0x20,0x48,0xB8});Address(b,reinterpret_cast<const void*>(&ShellPoint));
    Append(b,{0xFF,0xD0,0x84,0xC0,0x0F,0x84});const auto branch=b.size();Dword(b,0);
    Append(b,{0x0F,0x10,0x44,0x24,0x20,0x0F,0x11,0x84,0x24});Dword(b,0x110); // original rsp+20 world float4, outside original inverse-matrix slots
    Volatiles(b,true);
    Append(b,{0x48,0x89,0xBC,0x24});Dword(b,0x1A0); // replay native save old rdi at original rsp+B0
    Append(b,{0x0F,0x29,0xB4,0x24});Dword(b,0x180); // save original xmm6 at rsp+90
    Append(b,{0x0F,0x29,0xBC,0x24});Dword(b,0x170); // save original xmm7 at rsp+80
    Append(b,{0x48,0x8D,0xBC,0x24});Dword(b,0x110);
    Append(b,{0x48,0x81,0xC4,0xE8,0,0,0,0x9D});Jump(b,image+0x3C46C7);
    const auto fallback=b.size();RestoreFrame(b,false);
    Append(b,{0x48,0x85,0xD2,0x74,0x0E});Jump(b,image+0x3C468E);
    b.insert(b.end(),original+5,original+sizeof(original));Jump(b,image+at+sizeof(original));
    const auto relative=static_cast<unsigned>(fallback-(branch+4));for(unsigned i=0;i<4;++i)b[branch+i]=static_cast<unsigned char>(relative>>(8*i));
    void* code=edf::AllocateNearCode(image+at,b.data(),b.size());if(!code)return false;
    Bytes patch;Jump(patch,code);
    if(!edf::PatchCode(image+at,original,patch.data(),patch.size())) {VirtualFree(code,0,MEM_RELEASE);return false;}
    shellCode=code;shellBytes=b;return true;
}

bool UnitOwner(const void* unit,const void** owner) noexcept {
    if(!Readable(unit,0x2E0))return false;
    const auto root=At<const unsigned char*>(unit,0x2B0);
    const auto weak=At<const unsigned char*>(unit,0x2B8);
    if(!Readable(root,0x38) || !Readable(weak,16) || At<LONG>(weak,8)<=0 || At<const void*>(root,0x30)!=weak)return false;
    const auto profile=Family(root);
    // Only the proven direct-target EX controllers use this boundary. Other families build authored sweep/fan
    // points before calling the shared unit and must be intercepted at their own base-point consumer instead.
    if(!profile)return false;
    if(!std::strcmp(profile->name,"EDF6_FlyingDyno")) {
        // The main fireballs and horizontal flame copy a direct target. The separate vertical unit at +1498 uses
        // authored cached bone matrices (3B8... ->3B908F) and has no AI target coordinate; preserve that whole pattern.
        if(!Readable(root,0x14A0) || At<const void*>(root,0x1498)==unit)return false;
    } else if(std::strcmp(profile->name,"GiantAntEX") && std::strcmp(profile->name,"GiantBeeEX") &&
              std::strcmp(profile->name,"GiantSpiderEX") && std::strcmp(profile->name,"EDF6_Berserker_Middle") &&
              std::strcmp(profile->name,"EDF6_Berserker_TypeShooter") && std::strcmp(profile->name,"EDF6_GiantDangoLarge"))return false;
    *owner=root;return true;
}
void __fastcall UnitPoint(void* unit,void* dt) {
    bool changed=false;unsigned flags=0;alignas(16) float saved[4],point[4];
    __try {
        const void* owner=nullptr;
        if(UnitOwner(unit,&owner) && Readable(static_cast<unsigned char*>(unit)+0x2C0,0x20,true) && Point(owner,point)) {
            flags=At<unsigned>(unit,0x2C0);std::memcpy(saved,static_cast<unsigned char*>(unit)+0x2D0,16);
            Put<unsigned>(unit,0x2C0,flags|2u);std::memcpy(static_cast<unsigned char*>(unit)+0x2D0,point,16);changed=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {changed=false;}
    // Native per-muzzle matrices, native angle constraints, accuracy/spread and subsequent weapon state stay native.
    // This helper reads no target object/velocity: the only target coordinate consumed is unit+2D0 in point mode.
    __try { reinterpret_cast<UnitFn>(image+0x6D1800)(unit,dt); }
    __finally {
        if(changed) {
            Put<unsigned>(unit,0x2C0,flags);std::memcpy(static_cast<unsigned char*>(unit)+0x2D0,saved,16);
        }
    }
}
void __fastcall DeiroiMainMatrix(void* holder,const float* was);
void* const* TypedHooks() noexcept {
    static void* const hooks[]={reinterpret_cast<void*>(&LargePoint),reinterpret_cast<void*>(&LargePoint),reinterpret_cast<void*>(&UnitPoint),
            reinterpret_cast<void*>(&HumanoidPoint),reinterpret_cast<void*>(&HumanoidPoint),reinterpret_cast<void*>(&HumanoidSet),reinterpret_cast<void*>(&HumanoidSet),reinterpret_cast<void*>(&HumanoidSet),reinterpret_cast<void*>(&AntSpawn),reinterpret_cast<void*>(&MonsterLocator),reinterpret_cast<void*>(&SpinnerTarget),reinterpret_cast<void*>(&SpinnerTarget),reinterpret_cast<void*>(&BossMatrix<0x44E640>),reinterpret_cast<void*>(&BossMatrix<0x44DE70>),reinterpret_cast<void*>(&BossMeteor),reinterpret_cast<void*>(&BossLockWeak),reinterpret_cast<void*>(&MermanTarget),reinterpret_cast<void*>(&MermanTarget),reinterpret_cast<void*>(&DeiroiMainMatrix)};
    return hooks;
}
bool CodeMatches(const void* code,const Bytes& expected) noexcept {
    return code && !expected.empty() && Readable(code,expected.size()) && !std::memcmp(code,expected.data(),expected.size());
}
bool DeiroiMainOwner(const void* row,const void** owner) noexcept {
    if(!Readable(row,0x290))return false;
    const auto root=At<const unsigned char*>(row,0);const auto profile=Family(root);
    if(!profile || std::strcmp(profile->name,"Deiroi") || !Readable(root,0x1650))return false;
    const auto control=At<const unsigned char*>(root,0x30);
    if(!Readable(control,24) || At<LONG>(control,8)<=0 || At<const void*>(control,16)!=root)return false;
    const auto list=At<const unsigned char*>(root,0x1638);const auto count=At<std::size_t>(root,0x1648);
    if(count>128 || !Readable(list,count*0x290))return false;
    for(std::size_t i=0;i<count;++i)if(list+i*0x290==row) {*owner=root;return true;}
    return false;
}
bool __fastcall DeiroiMainPoint(const void* row,const float* muzzle,float* out) noexcept {
    __try {
        const void* root=nullptr;alignas(16) float point[4];
        if(!DeiroiMainOwner(row,&root) || !Readable(muzzle,16) || !Readable(out,16,true) || !Point(root,point))return false;
        const auto parameterObject=At<const unsigned char*>(row,0x120);
        if(!Readable(parameterObject,8))return false;
        const auto table=At<const unsigned char*>(parameterObject,0);
        if(!Readable(table,24))return false;
        using ParamsFn=void*(__fastcall*)(const void*);
        auto* params=reinterpret_cast<ParamsFn>(At<const void*>(table,16))(parameterObject);
        const float speed=At<float>(row,0x280);
        if(!Readable(params,0xF0,true) || !std::isfinite(speed) || speed<=0)return false;
        // The original gravity solver is executed once with the real params/muzzle/speed, before native spread.
        reinterpret_cast<BallisticFn>(image+0x2312A0)(static_cast<unsigned char*>(params)+0x30,out,point,muzzle,speed);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void __fastcall DeiroiMainMatrix(void* holder,const float* was) {
    alignas(16) float matrix[16];bool use=false;
    __try {
        const void* owner=nullptr;const auto row=static_cast<unsigned char*>(holder)-0x100;
        if(DeiroiMainOwner(row,&owner) && Readable(was,64)) {std::memcpy(matrix,was,64);use=AimMatrix(owner,matrix);}
    } __except(EXCEPTION_EXECUTE_HANDLER){use=false;}
    reinterpret_cast<MatrixSetFn>(image+0x6CFBC0)(holder,use ? matrix : was);
}
void* deiroiMainCode=nullptr;Bytes deiroiMainBytes;
bool InstallDeiroiMain() {
    if(deiroiMainCode)return true;
    constexpr unsigned at=0x31CC4B;
    constexpr unsigned char original[]={0x48,0x85,0xC0,0x74,0x5A,0x48,0x8B,0x8F,0x20,0x01,0,0,0x48,0x8B,0x11};
    if(!Readable(image+at,sizeof(original)) || std::memcmp(image+at,original,sizeof(original)))return false;
    Bytes b;Append(b,{0x9C,0x48,0x81,0xEC,0xE8,0,0,0});Volatiles(b,false);
    Append(b,{0x48,0x89,0xF9,0x48,0x8D,0x94,0x24});Dword(b,0x170); // row=RDI; actual original rsp+80 muzzle
    Append(b,{0x4C,0x8D,0x44,0x24,0x20,0x48,0xB8});Address(b,reinterpret_cast<const void*>(&DeiroiMainPoint));
    Append(b,{0xFF,0xD0,0x84,0xC0,0x0F,0x84});const auto branch=b.size();Dword(b,0);
    Append(b,{0x0F,0x10,0x44,0x24,0x20,0x0F,0x11,0x84,0x24});Dword(b,0x120); // native direction at original rsp+30
    RestoreFrame(b,false);Jump(b,image+0x31CCAA); // retain native cone, original RNG and actual holder emission
    const auto fallback=b.size();RestoreFrame(b,false);
    Append(b,{0x48,0x85,0xC0,0x75,0x0E});Jump(b,image+0x31CCAA);
    b.insert(b.end(),original+5,original+sizeof(original));Jump(b,image+at+sizeof(original));
    const auto relative=static_cast<unsigned>(fallback-(branch+4));for(unsigned i=0;i<4;++i)b[branch+i]=static_cast<unsigned char>(relative>>(8*i));
    void* code=edf::AllocateNearCode(image+at,b.data(),b.size());if(!code)return false;
    Bytes patch;Jump(patch,code);patch.resize(sizeof(original),0x90);
    if(!edf::PatchCode(image+at,original,patch.data(),patch.size())) {VirtualFree(code,0,MEM_RELEASE);return false;}
    deiroiMainCode=code;deiroiMainBytes=b;return true;
}
bool Integrity() {
    Bytes meteor;Jump(meteor,meteorCode);
    if(!CodeMatches(meteorCode,meteorBytes) || std::memcmp(image+0x443A93,meteor.data(),meteor.size()))return false;
    Bytes main;Jump(main,deiroiMainCode);main.resize(15,0x90);
    if(!CodeMatches(deiroiMainCode,deiroiMainBytes) || std::memcmp(image+0x31CC4B,main.data(),main.size()))return false;
    for(const auto& p:kPrints)if(!Readable(image+p.rva,sizeof(p.bytes)) || std::memcmp(image+p.rva,p.bytes,sizeof(p.bytes)))return false;
    constexpr unsigned bossSites[]={0x44E1E6,0x44E36F},bossLengths[]={15,17};
    for(unsigned i=0;i<2;++i) {Bytes patch;Jump(patch,bossCode[i]);patch.resize(bossLengths[i],0x90);if(!CodeMatches(bossCode[i],bossBytes[i]) || std::memcmp(image+bossSites[i],patch.data(),patch.size()))return false;}
    constexpr unsigned char context[]={0x4C,0x8B,0xC7};if(!bossContext || std::memcmp(image+0x44E400,context,3))return false;
    Bytes shell;Jump(shell,shellCode);if(!CodeMatches(shellCode,shellBytes) || std::memcmp(image+0x3C466C,shell.data(),shell.size()))return false;
    constexpr unsigned sites[]={0x34E231,0x34F978,0x6D550B,0x45448D,0x455B62,0x4545B7,0x455B96,0x46F96C,0x40B13C,0x4BA0B5,0x3D87AD,0x3D87DF,0x44500D,0x447703,0x443B5E,0x44E40A,0x3A027D,0x3A85DE,0x31CB9A};
    const auto hooks=TypedHooks();
    for(unsigned i=0;i<kTypedCount;++i) {
        const auto* at=image+sites[i];
        if(!redirected[i] || std::memcmp(at,callBytes[i],5))return false;
        const auto* destination=at+5+At<std::int32_t>(at,1);
        if(destination==hooks[i])continue;
        if(!Readable(destination,12) || destination[0]!=0x48 || destination[1]!=0xB8 ||
           At<const void*>(destination,2)!=hooks[i] || destination[10]!=0xFF || destination[11]!=0xE0)return false;
    }
    for(unsigned i=0;i<kInlineCount;++i) {
        Bytes wanted;Jump(wanted,inlineCode[i]);wanted.resize(17,0x90);
        if(!CodeMatches(inlineCode[i],inlineBytes[i]) || std::memcmp(image+kInlineSites[i].at,wanted.data(),wanted.size()))return false;
    }
    for(unsigned i=0;i<kStackCount;++i) {
        Bytes stack;Jump(stack,stackCode[i]);stack.resize(kStackSites[i].length,0x90);
        if(!CodeMatches(stackCode[i],stackBytes[i]) || std::memcmp(image+kStackSites[i].at,stack.data(),stack.size()))return false;
    }
    return true;
}
} // namespace enemy_weapon_aim
bool InstallEnemyControlWeaponAim() noexcept {
    using namespace enemy_weapon_aim;
    __try {
        if(ready && image==admittedImage)return true;
        if(!image || (admittedImage && admittedImage!=image))return false;
        for(const auto& p:kPrints)if(!Readable(image+p.rva,sizeof(p.bytes)) || std::memcmp(image+p.rva,p.bytes,sizeof(p.bytes)))return false;
        constexpr unsigned sites[]={0x34E231,0x34F978,0x6D550B,0x45448D,0x455B62,0x4545B7,0x455B96,0x46F96C,0x40B13C,0x4BA0B5,0x3D87AD,0x3D87DF,0x44500D,0x447703,0x443B5E,0x44E40A,0x3A027D,0x3A85DE,0x31CB9A};
        constexpr unsigned originals[]={0x34E470,0x34E470,0x6D1800,0x450920,0x450920,0x4A1D40,0x4A1D40,0x4A1D40,0x4054E0,0x6BB5A0,0x3DB140,0x3DB140,0x44E640,0x44DE70,0x448230,0x548EF0,0x3A9CF0,0x3A9CF0,0x6CFBC0};
        auto hooks=TypedHooks();
        for(unsigned i=0;i<kTypedCount;++i)if(!redirected[i]) {
            bool changed=false;
            if(!RedirectCall(image+sites[i],image+originals[i],hooks[i],changed))return false;
            std::memcpy(callBytes[i],image+sites[i],5);redirected[i]=true;
        }
        for(unsigned i=0;i<kInlineCount;++i)if(!InstallInline(i))return false;
        for(unsigned i=0;i<kStackCount;++i)if(!InstallStack(i))return false;
        if(!InstallShell() || !InstallDeiroiMain() || !InstallMeteor())return false;
        for(unsigned i=0;i<2;++i)if(!InstallBoss(i))return false;
        if(!InstallBossContext())return false;
        admittedImage=image;ready=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool EnemyControlAimMatrix(const void* root,float matrix[16]) noexcept {
    __try {return enemy_weapon_aim::AimMatrix(root,matrix);} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlWeaponAimReady(const enemy_native::NativeProfile* profile) noexcept {
    using namespace enemy_weapon_aim;
    __try {
        if(!ready || image!=admittedImage || !profile)return false;
        std::size_t count=0;const auto profiles=enemy_native::Profiles(&count);bool admitted=false;
        for(std::size_t i=0;i<count;++i)if(profile==profiles+i)admitted=true;
        if(!admitted)return false;
        if(!Integrity())return false;
        // Explicit real routes are recorded per family in tests/enemy_control_weapon_aim_contract.json.
        // AlienTrailer/TimeShip/DropBoat/Carrier and the Bommer's hand bombs are physical/barrier/transport
        // skills with no target-point projectile. Shields and body-contact skills keep their native collision path.
        // Deiroi additionally requires its separate whole-foot pose/weapon hooks to remain owned and intact.
        constexpr const char* complete[]={
            "AlienTrailer",
            "Deiroi",
            "DragonSmall",
            "EDF6_Berserker_Large",
            "EDF6_Berserker_Middle",
            "EDF6_Berserker_TypeA",
            "EDF6_Berserker_TypeBommer",
            "EDF6_Berserker_TypeGrapple",
            "EDF6_Berserker_TypeShooter",
            "EDF6_GiantDangoLarge",
            "EDF6_Merman",
            "EDF6_FlyingDyno",
            "EDF6_ShellFish",
            "EDF6_SpinnerUfo",
            "EDF6_SquidSmall",
            "EDF6_TimeShip",
            "GiantAnt",
            "GiantAntEX",
            "GiantBee",
            "GiantBeeEX",
            "GiantDango",
            "GiantSpider",
            "GiantSpiderEX",
            "Humanoid_BigGreyBoss",
            "EDF6_Martian",
            "EDF6_Venus",
            "Humanoid_Basic",
            "Monster501",
            "Monster504",
            "Nephila",
            "DropBoat512",
            "UfoCarrier508",
            "UfoSmall507",
            "UfoSmall515",
        };
        for(const char* name:complete)if(std::strcmp(profile->name,name)==0)
            return std::strcmp(name,"Deiroi")!=0 || enemy_native::DeiroiWeaponAimReady();
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
} // namespace crew
