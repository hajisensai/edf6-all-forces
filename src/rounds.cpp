// A vehicle weapon's round as the game will fly it, read off the live weapon (vhud.h, rounds.h; docs/hud-re.md §7). The
// user, 2026-10-06: "the rockets' point is worked out along a straight line: it is off from where they land; fix it".
//  - The round's class: the weapon's AmmoClass factory (weapon+0x7F8, filled at 0x68D4D0 / 0x68D53A) is a Factory@<class>
//    object; its vtable names the class (the RTTI is checked at load: kClasses).
//  - Ammo_CustomParameter: the weapon keeps the SGO's value at +0x8E8 (0x68D9DB), inside the block it hands every round it
//    fires (weapon+0x830, 0x69712F; MissileBullet01 reads its copy at +0xB98, 0x267AB6). It is a variant whose tag 2 is a
//    node of the SGO document; the missile's constructor reads its entries through three tables by tag (0x179EAF0 the
//    child, 0x179EAA8 the number, 0x179EEF0 the count: 0x267CD1, 0x267CFE, 0x268143), and so do we, with their tag-2
//    functions (checked to be what the tables hold).
//  - MissileBullet01's motion (rounds.h Motor): its update 0x26A880, checked at the instructions that read its state.
// Off (any check fails): every round is flown as an arc and every missile counted homing by its LockonType, as before.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "rounds.h"
#include "edf/weapon.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kAmmoFactory=0x7F8,kWeaponCustom=0x8E8;   // (LockonType: layout.h kWeaponLockon)
constexpr std::size_t kImageSize=0x22CE000;
constexpr int kSegment=15;   // frames a map ray covers (launcher.cpp RoundImpact's)

// The variant: 16 bytes and the tag (a word at +0x10; 0xFFFF: empty). Tag 2: {document, node index}.
struct Variant { unsigned char data[16]; std::uint16_t tag; unsigned char pad[6]; };
struct Pick { Variant* out; std::int32_t index; };
constexpr std::uint16_t kTagNode=2,kTagNone=0xFFFF;
constexpr unsigned kChildTable=0x179EAF0,kNumberTable=0x179EAA8,kCountTable=0x179EEF0;
constexpr unsigned kChild=0x2390D0,kNumber=0x2390B0,kCount=0x240AF0;
using ChildFn=void(__fastcall*)(const Variant*,Pick*);
using NumberFn=void(__fastcall*)(const Variant*,double*);
using CountFn=void(__fastcall*)(const Variant*,std::int32_t*);

struct Sig { unsigned rva; unsigned char bytes[12]; std::size_t size; };
const Sig kSigs[]={
    {0x68D9DB,{0x48,0x8D,0x8E,0xE8,0x08,0x00,0x00},7},              // the SGO read: lea rcx,[rsi+0x8E8]
    {0x69712F,{0x49,0x8D,0x97,0x30,0x08,0x00,0x00},7},              // fire: lea rdx,[r15+0x830] (the round's block)
    {0x267AB6,{0x48,0x8D,0xB7,0x98,0x0B,0x00,0x00},7},              // the missile reads its copy: lea rsi,[rdi+0xB98]
    {0x267CD1,{0x4D,0x8B,0x84,0xC6,0xF0,0xEA,0x79,0x01},8},         // ...through the child table
    {0x267CFE,{0x4D,0x8B,0x84,0xC6,0xA8,0xEA,0x79,0x01},8},         // ...the number table
    {0x268143,{0x4D,0x8B,0x84,0xC0,0xF0,0xEE,0x79,0x01},8},         // ...the count table
    {kChild,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0x1A,0x48,0x8B,0x01},12},
    {kNumber,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0xE8},10},
    {kCount,{0x4C,0x8B,0x01,0x4C,0x8B,0xCA,0x8B,0x51,0x08,0x49,0x39,0x50},12},
    // MissileBullet01's update (0x26A880): the state rounds.h Motor steps.
    {0x26A92E,{0x8B,0xBB,0x00,0x14,0x00,0x00,0x3B,0xBB,0xAC,0x13,0x00,0x00},12},   // age < CP[7][0]
    {0x26A948,{0xF3,0x0F,0x10,0x83,0xB0,0x13,0x00,0x00},8},         // inh x CP[7][1]
    {0x26A97D,{0xF3,0x0F,0x10,0x83,0xB4,0x13,0x00,0x00},8},         // own x CP[7][2]
    {0x26A9B2,{0x0F,0x10,0x8B,0xF0,0x13,0x00,0x00},7},              // + the frame's gravity
    {0x26AA26,{0x0F,0x59,0x0D,0x53,0x73,0x53,0x01},7},              // inh x 0.9
    {0x17A1D80,{0x66,0x66,0x66,0x3F,0x66,0x66,0x66,0x3F},8},        // (0.9f)
    {0x26AA59,{0x8B,0x8B,0x80,0x13,0x00,0x00},6},                   // the guidance type (CP[0]): 0 straight
    {0x26AACC,{0xF3,0x0F,0x58,0x83,0xA0,0x13,0x00,0x00},8},         // |own| + CP[4]
    {0x26AAE3,{0xF3,0x0F,0x10,0x8B,0xA8,0x13,0x00,0x00},8},         // at most CP[6]
    {0x268F5E,{0x0F,0x11,0x87,0xF0,0x13,0x00,0x00},7},              // the gravity a frame: the core's (0x231C10)
};

// The round classes by their factory's vtable (RTTI ".?AVFactory@<class>@@", checked at load) and how the HUD shows them.
enum class Cls : std::uint8_t { arc, lobbed, missile, homing };
struct ClassInfo { unsigned vtable; const char* rtti; const char* label; Cls cls; };
const ClassInfo kClasses[]={
    {0x17A3E90,".?AVFactory@SolidBullet01@@","GUN",Cls::arc},
    {0x17A3ED0,".?AVFactory@SolidBullet01Rail@@","CANNON",Cls::arc},
    {0x17A3878,".?AVFactory@RocketBullet01@@","CANNON",Cls::arc},
    {0x17A4440,".?AVFactory@SolidExpBullet01@@","CANNON",Cls::arc},
    {0x17A1688,".?AVFactory@GrenadeBullet01@@","GREN",Cls::lobbed},
    {0x17A16C8,".?AVFactory@GrenadeBullet01_MapNoDamage@@","GREN",Cls::lobbed},
    {0x17A1BD0,".?AVFactory@MissileBullet01@@","MSL",Cls::missile},
    {0x17A1DA8,".?AVFactory@MissileBullet02@@","MSL",Cls::homing},
    {0x179FC60,".?AVFactory@LaserBullet01@@","LASER",Cls::arc},
    {0x179F9D8,".?AVFactory@HomingLaserBullet01@@","HLASER",Cls::homing},
    {0x179E2D0,".?AVFactory@EfsBullet@@","BEAM",Cls::arc},
    {0x179ECA8,".?AVFactory@EfsExposureBullet@@","BEAM",Cls::arc},
    {0x17A12F0,".?AVFactory@FlameBullet02@@","FLAME",Cls::arc},
    {0x17A20A0,".?AVFactory@AcidBullet01@@","ACID",Cls::arc},
    {0x17A14A0,".?AVFactory@NapalmBullet01@@","NAPALM",Cls::lobbed},
};
constexpr int kClassCount=static_cast<int>(sizeof(kClasses)/sizeof(kClasses[0]));
bool classOk[kClassCount]{};
bool customOk=false,statusOk=false;
// The weapon status the stock gauge reads (0x692100), whose fields vhud.cpp shows: rounds, ReloadTime, the cooldown, the
// charge time and its count, the reload's count, the magazine (and the SGO read that fills it, 0x68C55C).
const Sig kStatusSigs[]={
    {0x692114,{0x8B,0xA9,0xE8,0x0B,0x00,0x00},6},{0x69214A,{0x8B,0xB1,0x0C,0x02,0x00,0x00},6},
    {0x69216E,{0xF3,0x0F,0x10,0x83,0x0C,0x0E,0x00,0x00},8},{0x692189,{0xF3,0x0F,0x10,0xA3,0x2C,0x02,0x00,0x00},8},
    {0x6921A2,{0x0F,0x10,0x83,0x7C,0x0E,0x00,0x00},7},{0x6921C2,{0x66,0x0F,0x6E,0x8B,0x68,0x0E,0x00,0x00},8},
    {0x69220F,{0x8B,0x83,0x48,0x02,0x00,0x00},6},{0x68C55C,{0x4C,0x8D,0xAE,0x48,0x02,0x00,0x00},7},
};

// Whether the vtable at `rva` belongs to the class named `rtti` (its complete object locator, at the vtable's -8: the
// type descriptor's image offset at +0xC, the name at the descriptor's +0x10).
bool VtableNamed(unsigned rva,const char* rtti) noexcept {
    const auto col=At<const unsigned char*>(image,rva-8);
    if(!Readable(col,0x10))return false;
    const std::uint32_t td=At<std::uint32_t>(col,0xC);
    const std::size_t n=std::strlen(rtti)+1;
    return td>0 && td+0x10+n<kImageSize && std::memcmp(image+td+0x10,rtti,n)==0;
}

const ClassInfo* ClassOf(const unsigned char* w) noexcept {
    const auto factory=At<const unsigned char*>(w,kAmmoFactory);
    if(!Readable(factory,8))return nullptr;
    const auto vt=At<const unsigned char*>(factory,0);
    for(int i=0;i<kClassCount;++i)if(classOk[i] && vt==image+kClasses[i].vtable)return &kClasses[i];
    return nullptr;
}

// Ammo_CustomParameter's entries as the missile's constructor reads them: `v` a tag-2 variant, the entry `index` into
// `out` (false: none such).
bool Child(const Variant& v,int index,Variant* out) noexcept {
    if(v.tag!=kTagNode || index<0)return false;
    std::int32_t n=0;
    reinterpret_cast<CountFn>(image+kCount)(&v,&n);
    if(index>=n)return false;
    *out=Variant{};out->tag=kTagNone;
    Pick pick{out,index};
    reinterpret_cast<ChildFn>(image+kChild)(&v,&pick);
    return out->tag==kTagNode;
}
// The number at `path` (one or two indices), or `fallback` with no such entry.
double Number(const Variant& custom,int first,int second,double fallback) noexcept {
    Variant a{},b{};
    if(!Child(custom,first,&a))return fallback;
    const Variant* at=&a;
    if(second>=0){if(!Child(a,second,&b))return fallback;at=&b;}
    double d=fallback;
    reinterpret_cast<NumberFn>(image+kNumber)(at,&d);
    return std::isfinite(d) ? d : fallback;
}
// The int conversion the constructor makes (0x239130: rounded half away from zero).
std::int32_t Whole(double d) noexcept {
    const double r=std::floor(std::fabs(d)+0.5);
    return static_cast<std::int32_t>(d<0.0 ? -r : r);
}

// The shooter's velocity a round takes on (m/frame): 0x691FA0's weapon+0x190 x AmmoOwnerMove / 60.
void Inherited(const unsigned char* w,float* out) noexcept {
    const float share=At<float>(w,edf::kWeaponAmmoOwnerMove);
    const float* v=reinterpret_cast<const float*>(w+edf::kWeaponOwnerVel);
    for(int i=0;i<3;++i)out[i]=std::isfinite(share) && std::isfinite(v[i]) ? v[i]*share/60.0f : 0.0f;
}

float Ray(const float* a,const float* b,float* hit) noexcept { return MapRay(a,b,hit); }
}  // namespace

bool InstallRounds() noexcept {
    __try {
        bool custom=true;
        for(const auto& s:kSigs)custom=custom && Matches(s.rva,s.bytes,s.size);
        // The tables' tag-2 entries (relocated pointers): the functions the constructor calls are these.
        custom=custom && At<const void*>(image,kChildTable+16)==image+kChild && At<const void*>(image,kNumberTable+16)==image+kNumber
               && At<const void*>(image,kCountTable+16)==image+kCount;
        customOk=custom;
        bool status=true;
        for(const auto& s:kStatusSigs)status=status && Matches(s.rva,s.bytes,s.size);
        statusOk=status;
        int named=0;
        for(int i=0;i<kClassCount;++i)named+=classOk[i]=VtableNamed(kClasses[i].vtable,kClasses[i].rtti);
        Log("HOOK rounds custom=%d classes=%d/%d status=%d (rockets flown as the game flies them: custom=1 and MissileBullet01's class;"
            " the reload shown: status=1)",customOk,named,kClassCount,statusOk);
        return customOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool WeaponStatusOk() noexcept { return statusOk; }

bool ReadRound(const unsigned char* w,RoundModel* out) noexcept {
    if(!Readable(w,kWeaponCustom+0x18) || !Readable(w+edf::kWeaponMatrix,0x40))return false;
    RoundModel m{};
    m.speed=At<float>(w,edf::kWeaponAmmoSpeed);m.factor=At<float>(w,edf::kWeaponAmmoGravity);m.alive=At<std::int32_t>(w,edf::kWeaponAmmoAlive);
    if(!std::isfinite(m.speed) || m.speed<0.0f || !std::isfinite(m.factor) || m.alive<=0)return false;
    const ClassInfo* c=ClassOf(w);
    m.label=c ? c->label : "WPN";
    m.kind=At<std::int32_t>(w,kWeaponLockon)==kHoming ? RoundKind::homing : RoundKind::arc;
    if(c && c->cls==Cls::homing)m.kind=RoundKind::homing;
    m.lobbed=c && c->cls==Cls::lobbed;
    if(c && c->cls==Cls::missile && customOk) {
        // The guidance type (CP[0]): 0 flies straight on its motor (the rockets), 1 / 2 steer at the lock.
        const Variant& custom=*reinterpret_cast<const Variant*>(w+kWeaponCustom);
        if(Whole(Number(custom,0,-1,0.0))!=0)m.kind=RoundKind::homing;
        else {
            m.kind=RoundKind::rocket;m.label="RKT";
            m.accel=static_cast<float>(Number(custom,4,-1,0.0));
            m.top=static_cast<float>(Number(custom,6,-1,0.0));
            m.ignite=Whole(Number(custom,7,0,0.0));
            m.keepInh=static_cast<float>(Number(custom,7,1,0.0));
            m.keepOwn=static_cast<float>(Number(custom,7,2,1.0));   // not given: 1 (0x2680D5)
        }
    } else if(c && c->cls==Cls::missile && m.kind!=RoundKind::homing) {
        m.kind=RoundKind::homing;   // its motion unread: no path drawn (a lock still is)
    }
    *out=m;
    return true;
}

bool RoundLands(const unsigned char* w,const RoundModel& m,const float* pos,const float* dir,float reach,float* at,float* sec) noexcept {
    float g[3],owner[3],hit[3],took=0.0f;
    if(m.kind==RoundKind::homing || m.kind==RoundKind::none || !edf::WorldGravity(image,g))return false;
    Inherited(w,owner);
    const float drop[3]={g[0]*m.factor/3600.0f,g[1]*m.factor/3600.0f,g[2]*m.factor/3600.0f};
    bool landed=false;
    if(m.kind==RoundKind::rocket) {
        rounds::Motor f{};
        for(int i=0;i<3;++i){f.own[i]=dir[i]*m.speed;f.inh[i]=owner[i];f.drop[i]=drop[i];}
        f.accel=m.accel;f.top=m.top;f.keepInh=m.keepInh;f.keepOwn=m.keepOwn;f.ignite=m.ignite;
        landed=rounds::FirstHit(f,pos,m.alive,kSegment,reach,&Ray,hit,at,&took);
    } else {
        rounds::Arc f{};
        for(int i=0;i<3;++i){f.vel[i]=dir[i]*m.speed+owner[i];f.drop[i]=drop[i];}
        landed=rounds::FirstHit(f,pos,m.alive,kSegment,reach,&Ray,hit,at,&took);
    }
    if(landed)std::memcpy(at,hit,12);
    *sec=took/60.0f;
    return landed;
}
}  // namespace crew
