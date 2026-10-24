// The Wing Diver energy probe (test only, part of EDF6Autopilot): asked by the autopilot command "probe wdenergy".
//
// A weapon's charge (the shared weapon update 0x6934F0) takes energy from its owner each frame through the owner's
// vtable slot 31 (call 0x693E34, `call [rax+0F8h]`, skipped when EnergyChargeRequire[1] (+0x234) is below 0.0011);
// the Wing Diver's slot 31 is 0x5804B0 (amount x +0x2384 off her energy +0x308, max +0x304; +0x2FC = 1 emergency
// charge). Two slots of her vtable (0x17D0FF8) are swapped for logging wrappers that call the originals:
//   slot 5  (0x5808B0, her frame): remembers the soldier, so the tick can read her;
//   slot 31 (0x5804B0, the draw):  counts each draw, its amount and what it took.
// The tick logs, every 250 ms, her energy and each weapon's (h+0x1950 array, h+0x1960 count) AmmoCount +0x248, rounds
// +0xBE8, EnergyChargeRequire as read (+0x22C curve at the star, +0x230 its base, +0x234 element 1), charge left
// +0xE7C, reload frames left +0xE68, ReloadType +0x208, ReloadTime +0x20C. Nothing about the game's own run changes.
#include "wdenergy_probe.h"
#include "edf/memory.h"
#include <intrin.h>
#include <cstdint>
#include <cstring>

namespace autopilot {
namespace {
constexpr std::size_t kVtable=0x17D0FF8,kFrame=0x5808B0,kDraw=0x5804B0,kFrameSlot=5,kDrawSlot=31;
constexpr std::size_t kEnergy=0x308,kEnergyMax=0x304,kEmergency=0x2FC,kDrawScale=0x2384,kWeapons=0x1950,kWeaponCount=0x1960;
constexpr std::size_t kAmmoCount=0x248,kRounds=0xBE8,kNeed=0x22C,kNeedBase=0x230,kNeedGate=0x234,kChargeLeft=0xE7C,
                      kReloadLeft=0xE68,kReloadType=0x208,kReloadTime=0x20C;

using FrameFn=void(__fastcall*)(void*,void*,void*,void*);
using DrawFn=float(__fastcall*)(void*,float,int);

unsigned char* image=nullptr;
LogFn say=nullptr;
FrameFn realFrame=nullptr;
DrawFn realDraw=nullptr;
unsigned char* volatile soldier=nullptr;
volatile LONG draws=0;
volatile LONG drawsLogged=0;
ULONGLONG nextTick=0;

std::size_t Rva(const void* p) noexcept {
    const auto at=reinterpret_cast<std::uintptr_t>(p),base=reinterpret_cast<std::uintptr_t>(image);
    return at>=base && at<base+0x22CE000 ? at-base : 0;
}

template<class T> T At(const unsigned char* p,std::size_t off) noexcept {
    T v{};std::memcpy(&v,p+off,sizeof(T));return v;
}

void __fastcall Frame(void* self,void* a,void* b,void* c) noexcept {
    soldier=static_cast<unsigned char*>(self);
    realFrame(self,a,b,c);
}

float __fastcall Draw(void* self,float amount,int mode) noexcept {
    const auto s=static_cast<const unsigned char*>(self);
    const float before=At<float>(s,kEnergy);
    const float took=realDraw(self,amount,mode);
    const LONG n=InterlockedIncrement(&draws);
    if(InterlockedIncrement(&drawsLogged)<=40)
        say("WDENERGY draw #%ld from EDF+%#zx: amount %.4f mode %d scale %.3f -> took %.4f, energy %.3f -> %.3f",n,
            Rva(_ReturnAddress()),amount,mode,At<float>(s,kDrawScale),took,before,At<float>(s,kEnergy));
    return took;
}

bool Swap(std::size_t slot,std::size_t expect,void* with,void** original) noexcept {
    auto** const entry=reinterpret_cast<void**>(image+kVtable+slot*8);
    if(*entry!=image+expect)return false;
    DWORD old=0;
    if(!VirtualProtect(entry,8,PAGE_READWRITE,&old))return false;
    *original=*entry;
    *entry=with;
    VirtualProtect(entry,8,old,&old);
    return true;
}
}  // namespace

bool InstallWdEnergyProbe(unsigned char* img,LogFn log) noexcept {
    image=img;say=log;
    void* frame=nullptr;void* draw=nullptr;
    if(!Swap(kFrameSlot,kFrame,reinterpret_cast<void*>(&Frame),&frame))return false;
    realFrame=reinterpret_cast<FrameFn>(frame);
    if(!Swap(kDrawSlot,kDraw,reinterpret_cast<void*>(&Draw),&draw))return false;
    realDraw=reinterpret_cast<DrawFn>(draw);
    return true;
}

void WdEnergyProbeTick() noexcept {
    unsigned char* const s=soldier;
    if(!s || !say)return;
    const ULONGLONG now=GetTickCount64();
    if(now<nextTick)return;
    nextTick=now+250;
    __try {
        say("WDENERGY soldier %p energy %.3f / %.3f emergency %d scale %.3f draws %ld",s,At<float>(s,kEnergy),
            At<float>(s,kEnergyMax),At<int>(s,kEmergency),At<float>(s,kDrawScale),draws);
        const auto weapons=At<unsigned char**>(s,kWeapons);
        const auto count=At<unsigned long long>(s,kWeaponCount);
        for(unsigned long long i=0;i<count && i<8 && weapons;++i) {
            const unsigned char* w=weapons[i];
            if(!w || !edf::Readable(w,0xF00))continue;
            say("WDENERGY  weapon %llu %p ammo %d/%d need %.4f base %.4f gate %.4f left %.4f reload %d type %d time %d",i,w,
                At<int>(w,kRounds),At<int>(w,kAmmoCount),At<float>(w,kNeed),At<float>(w,kNeedBase),At<float>(w,kNeedGate),
                At<float>(w,kChargeLeft),At<int>(w,kReloadLeft),At<int>(w,kReloadType),At<int>(w,kReloadTime));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){say("WDENERGY soldier %p unreadable",s);soldier=nullptr;}
}
}  // namespace autopilot
