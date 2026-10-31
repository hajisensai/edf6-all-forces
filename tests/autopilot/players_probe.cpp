// The local players probe (test only, part of EDF6Autopilot): asked by the autopilot command "probe players".
//
// src/enemy_control_pad.h reads two fields of the player's soldier from static disassembly: +0x338, the local pad bound
// to the player (0x54ED40), and +0xD40, the class's key record (each class constructor writes its own: AssultSoldier 0,
// PaleWing 1 at 0x57CD80, Engineer 2 at 0x563706, HeavyArmor 3 at 0x5652C8). Slot 5 (the frame) of each class's
// primary vtable is swapped for a wrapper that remembers the soldier with a controller (+0x340) and calls the original.
// The tick logs, every second, for each class seen: +0x338, +0xD40, the player byte +0x354, and the controller's own
// pad-path field +0x2D68. Nothing about the game's own run changes.
//
// It also logs what the room's state sync (75D2E0) reads of the soldier, to compare classes: the NetworkObject at
// +0x120, its flags +0x128 (bit 0 another machine's, bit 1 ours), +0x138 (its network id's record, 774BD0; null until
// registered online) and +0x150 and slot 3's byte +0x2E8 that its slot 4 (774920, "send this frame") tests, the slot 4
// answer itself, and that record through its control block at +0x130 (+0x30 its bucket, +0x34 the bucket it goes back
// to after a send, 75DB17).
#include "players_probe.h"
#include "edf/memory.h"
#include <cstdint>
#include <cstring>

namespace autopilot {
namespace {
using FrameFn=void(__fastcall*)(void*,void*,void*,void*);
struct Class { const char* name; std::size_t vtable,frame; };
constexpr Class kClasses[4]={{"AssultSoldier",0x17CDF28,0x550C30},{"PaleWing",0x17D0FF8,0x5808B0},
                             {"Engineer",0x17CF100,0x563DC0},{"HeavyArmor",0x17CF5B8,0x567780}};
constexpr std::size_t kFrameSlot=5,kLocalPad=0x338,kPad=0x340,kPlayer=0x354,kClassRecord=0xD40,kPadPath=0x2D68;
constexpr std::size_t kNet=0x120,kNetFlags=0x128,kNetRecord=0x130,kNetId=0x138,kNetForce=0x150,kNetSkip=0x2E8;
constexpr std::size_t kNetSlotSend=4;
using SendFn=bool(__fastcall*)(void*);

unsigned char* image=nullptr;
LogFn say=nullptr;
FrameFn real[4]{};
unsigned char* volatile seen[4]{};
ULONGLONG nextTick=0;

template<class T> T At(const unsigned char* p,std::size_t off) noexcept {
    T v{};std::memcpy(&v,p+off,sizeof(T));return v;
}
template<unsigned I> void __fastcall Frame(void* self,void* a,void* b,void* c) noexcept {
    const auto s=static_cast<unsigned char*>(self);
    __try { if(At<void*>(s,kPad) && s[kPlayer])seen[I]=s; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    real[I](self,a,b,c);
}
constexpr FrameFn kWrappers[4]={&Frame<0>,&Frame<1>,&Frame<2>,&Frame<3>};

bool Swap(const Class& c,void* with,void** original) noexcept {
    auto** const entry=reinterpret_cast<void**>(image+c.vtable+kFrameSlot*8);
    if(*entry!=image+c.frame)return false;
    DWORD old=0;
    if(!VirtualProtect(entry,8,PAGE_READWRITE,&old))return false;
    *original=*entry;
    *entry=with;
    VirtualProtect(entry,8,old,&old);
    return true;
}
}  // namespace

bool InstallPlayersProbe(unsigned char* img,LogFn log) noexcept {
    image=img;say=log;
    for(const auto& c:kClasses)if(*reinterpret_cast<void**>(image+c.vtable+kFrameSlot*8)!=image+c.frame)return false;
    for(unsigned i=0;i<4;++i) {
        void* original=nullptr;
        if(!Swap(kClasses[i],reinterpret_cast<void*>(kWrappers[i]),&original))return false;
        real[i]=reinterpret_cast<FrameFn>(original);
    }
    return true;
}

void PlayersProbeTick() noexcept {
    if(!say)return;
    const ULONGLONG now=GetTickCount64();
    if(now<nextTick)return;
    nextTick=now+1000;
    for(unsigned i=0;i<4;++i) {
        unsigned char* const s=seen[i];
        if(!s)continue;
        if(!edf::Readable(s,kClassRecord+4)){say("PLAYERS %s %p gone",kClasses[i].name,s);seen[i]=nullptr;continue;}
        __try {
            const auto pad=At<const unsigned char*>(s,kPad);
            say("PLAYERS %s %p localPad(+0x338) %d classRecord(+0xD40) %d player(+0x354) %d pad %p padPath(+0x2D68) %d",
                kClasses[i].name,s,At<int>(s,kLocalPad),At<int>(s,kClassRecord),At<unsigned char>(s,kPlayer),pad,
                pad && edf::Readable(pad,kPadPath+4) ? At<int>(pad,kPadPath) : -99);
            void* const net=s+kNet;
            const auto send=(*reinterpret_cast<SendFn* const*>(net))[kNetSlotSend];
            // The record through its control block at +0x130 (uses at +4, the record at +8: 22B058..22B09B).
            const auto ctrl=At<const unsigned char*>(s,kNetRecord);
            const unsigned char* record=ctrl && edf::Readable(ctrl,0x10) && At<int>(ctrl,4) ? At<const unsigned char*>(ctrl,8) : nullptr;
            const bool recordOk=record && edf::Readable(record,0x38);
            say("PLAYERS %s %p net flags(+0x128) %04X id(+0x138) %p force(+0x150) %d skip(+0x2E8) %d send %d record %p bucket %d back %d",
                kClasses[i].name,s,At<unsigned short>(s,kNetFlags),At<void*>(s,kNetId),At<unsigned char>(s,kNetForce),
                At<unsigned char>(s,kNetSkip),send(net) ? 1 : 0,record,recordOk ? At<int>(record,0x30) : -99,
                recordOk ? At<int>(record,0x34) : -99);
        } __except(EXCEPTION_EXECUTE_HANDLER){say("PLAYERS %s %p unreadable",kClasses[i].name,s);seen[i]=nullptr;}
    }
}
}  // namespace autopilot
