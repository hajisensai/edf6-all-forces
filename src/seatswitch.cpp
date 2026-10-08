// The player moving to another seat of the vehicle they are in (payload.h, docs/stock-payload-re.md §5; the user,
// 2026-10-06: "载具内多位的情况可切换至空座"; ini SeatSwitch / SeatNextKey / SeatNumberKeys / SeatButton / SeatPilot /
// SeatSwitchOnline). The stock game has no such move: a rider gets off and boards again. The move here is the stock
// board button's own sequence (0x56D700 -> visitor 0x572610 -> FindSeat) without the walk to the door and its reach
// test, onto a seat of the vehicle the player is already in:
//  1. the seat they leave forgets them (0x634940, the clear SeatKick 0x62E1A0 ends with, without its get-off message:
//     that message 0x10000015 would stand them at the door), and its input block is zeroed (the constructor's values):
//     no rider writes it any more, and a vehicle reading it (the 502 copies seat 0's without asking who sits there)
//     must not go on with the player's last stick and trigger;
//  2. the seat they take is reserved for them (0x633FE0, as FindSeat 0x633BFE reserves the one it finds);
//  3. human +0x1540 = that seat, +0x15B0 / +0x15B8 = 0, +0x15BC = 1.0 (the visitor, 0x57268F..0x572745);
//  4. the ride announced to the room (0x5763E0, the board button's 0x56D799: online, message 6 with the vehicle's id
//     and the seat's index, which every other machine's copy of the player takes by RideVehicle 0x5765E0; nothing
//     offline);
//  5. the riding action begun again (0x551C30(human +0x1150, {0x56C9F0, 0}, 0), 0x56D7CC: the new seat's pose and
//     camera) and human +0x3F0 = (seat +0x2B4 == 3) (0x56D7D1; the leave 0x56FE80 clears it).
// The next-seat key (F, pad B) takes the next empty seat round the vehicle; a number key takes that seat, and an NPC in
// it changes places with the player (crew.cpp MoveRider, the bump's move: as the gunship's crew do, the pilot to the
// gun, the gunner to the stick). Another player's seat, one their class may not sit in (the seat's class mask
// +0x30 & +0x34 against human +0x31C, CanRideSeat 0x6346FC) are refused, shown a moment. Out of a stock helicopter's
// pilot seat the stock RideAi seats an NPC pilot (ini SeatPilot; heli.cpp then flies it with the player aboard: it
// fights round itself), as the gunship's gunner gets one (playerjet_crew.inc EnsurePilot); out of a ground vehicle's
// with the stock driving AI an NPC driver (Pilot: the map can then send it off). The plugin's aircraft: only
// the gunship's two seats, its stick taken only where it may be boarded (PlayerJetBoardable: on the ground, or come
// down for the player); playerjet.cpp tells a move from getting out (no ejection). Offline unless SeatSwitchOnline.
// The prompt (hud.cpp SeatLine): the seats and who holds them. With SeatList (the user, 2026-10-07: "能看到同载具席位
// 情况", not only switch) the whole ride in a vehicle with more than one seat, SeatSwitch off or online too: reading the
// seats moves nothing. The keys that move them (or the online lock) ride along only for kPromptMs after boarding and
// after a move, while the key is held, and kRefusedMs after a refusal; SeatList off, the line is shown only then.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "online_authority.h"
#include "npcai.h"
#include "stores.h"
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
constexpr std::size_t kHumanSeat=0x1540,kHumanRideBlend=0x15B0,kHumanRideBlend2=0x15B8,kHumanRideWeight=0x15BC;
constexpr std::size_t kHumanActions=0x1150,kHumanSeatFlag=0x3F0,kHumanClass=0x31C;
constexpr std::size_t kSeatClassMask=0x30,kSeatClassOn=0x34,kSeatKeyRow=0x2B4,kSeatPad=0x2B0,kSeatButtons=0x2E8;
// The seat's input block (docs/heli-input-re.md §4): left stick +0x2C0 (x, y, 0, 1), right stick +0x2D0, the analog
// triggers +0x2E0 / +0x2E4, the buttons' word +0x2E8.
constexpr std::size_t kSeatLeft=0x2C0,kSeatRight=0x2D0,kSeatTriggers=0x2E0;
constexpr unsigned kAnnounce=0x5763E0,kSetAction=0x551C30,kRideAction=0x56C9F0,kReserve=0x633FE0,kClear=0x634940;
constexpr unsigned kBoardTail=0x56D796,kVisitorBlend=0x572734,kClassTest=0x6346FC;
constexpr ULONGLONG kPromptMs=4000,kRefusedMs=1500,kFreshMs=200;
constexpr int kKeyRowSpecial=3;   // 0x56D7D8: a seat of this key row sets human +0x3F0
constexpr int kNoneFree=-2;       // SeatPrompt::refused: the next-seat key found no free seat

struct Sig { unsigned rva; const unsigned char* bytes; std::size_t size; };
const unsigned char kAnnounceSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x81};
const unsigned char kSetActionSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x20,0x4C,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55};
const unsigned char kRideActionSig[]={0x48,0x8B,0x01,0xFF,0xA0,0x78,0x02,0x00,0x00};   // jmp [rax+0x278]: the human's own
const unsigned char kReserveSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
const unsigned char kClearSig[]={0x48,0x85,0xD2,0x0F,0x84,0x94,0x00,0x00,0x00,0x48,0x89,0x5C,0x24,0x08,0x57,0x48};
// The board button's tail: call 0x5763E0(human); the riding action {0x56C9F0, 0} set on human +0x1150 by 0x551C30;
// seat = human +0x1540; seat +0x2B4 == 3 sets human +0x3F0.
const unsigned char kBoardTailSig[]={0x48,0x8B,0xCB,0xE8,0x42,0x8C,0x00,0x00,0x48,0x8D,0x8B,0x50,0x11,0x00,0x00,0x48,0x8D,0x05,0x44,0xF2,
    0xFF,0xFF,0x48,0x89,0x44,0x24,0x30,0xC7,0x44,0x24,0x38,0x00,0x00,0x00,0x00,0x0F,0x28,0x44,0x24,0x30,0x66,0x0F,0x7F,0x44,0x24,0x30,
    0x45,0x33,0xC0,0x48,0x8D,0x54,0x24,0x30,0xE8,0x5F,0x44,0xFE,0xFF,0x48,0x8B,0x83,0x40,0x15,0x00,0x00,0x83,0xB8,0xB4,0x02,0x00,0x00,
    0x03,0x75,0x07,0xC6,0x83,0xF0,0x03,0x00,0x00,0x01};
const unsigned char kVisitorBlendSig[]={0x48,0x8B,0x46,0x08,0x48,0x89,0xA8,0xB0,0x15,0x00,0x00,0x89,0xA8,0xB8,0x15,0x00,0x00,0xC7,0x80,
    0xBC,0x15,0x00,0x00,0x00,0x00,0x80,0x3F};
const unsigned char kClassTestSig[]={0x8B,0x82,0x1C,0x03,0x00,0x00,0x41,0x23,0x40,0x34,0x41,0x85,0x40,0x30,0x0F,0x84};
const Sig kSigs[]={
    {kAnnounce,kAnnounceSig,sizeof(kAnnounceSig)},{kSetAction,kSetActionSig,sizeof(kSetActionSig)},
    {kRideAction,kRideActionSig,sizeof(kRideActionSig)},{kReserve,kReserveSig,sizeof(kReserveSig)},{kClear,kClearSig,sizeof(kClearSig)},
    {kBoardTail,kBoardTailSig,sizeof(kBoardTailSig)},
    {kVisitorBlend,kVisitorBlendSig,sizeof(kVisitorBlendSig)},{kClassTest,kClassTestSig,sizeof(kClassTestSig)},
};
bool ok=false;

using AnnounceFn=void(__fastcall*)(void*);
using SetActionFn=void(__fastcall*)(void*,const void*,std::int64_t);
using ReserveFn=void(__fastcall*)(void*,void*,void*);
using ClearFn=void(__fastcall*)(void*,void*);

// Per local player (split screen: two): the vehicle they sit in, the keys down last frame, what the prompt shows.
constexpr int kNumberKeys=9;
struct Rider_ {
    ObjRef human,vehicle;
    bool nextHeld,numberHeld[kNumberKeys];
    ULONGLONG promptUntil,refusedUntil,seen;
    int refused;
};
Rider_ riders[2]{};
SeatPrompt latest{};
ULONGLONG latestMs=0,shownUntil=0;

Rider_* RiderFor(const unsigned char* human,ULONGLONG ms) noexcept {
    Rider_* slot=nullptr;
    for(auto& r:riders) {
        if(r.human.Is(human))return &r;
        if(!slot && (!r.human || r.human.obj==human || ms-r.seen>kFreshMs*10))slot=&r;
    }
    if(slot){*slot=Rider_{};slot->human=ObjRef::Of(human);slot->refused=-1;}
    return slot;
}

// Whether the virtual key `vk` is down while the game has the foreground (0: never).
bool KeyDown(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

SeatHolder HolderOf(const unsigned char* seat,const unsigned char* human) noexcept {
    switch(SeatRider(seat)) {
        case Rider::none: return SeatHolder::empty;
        case Rider::dummy: return SeatHolder::npc;
        case Rider::player: return At<const unsigned char*>(seat,kSeatRider)==human ? SeatHolder::you : SeatHolder::other;
        default: return NpcCanYieldSeat(seat) ? SeatHolder::npc : SeatHolder::other;
    }
}

// A weapon of the seat's own (the fuel tank, which every seat of a vehicle with one lists, is none).
bool HasGun(const unsigned char* seat) noexcept {
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(n>16 || !Readable(holders,n*8))return false;
    for(std::uint64_t i=0;i<n;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        std::size_t len=0;
        const wchar_t* f=WeaponFile(At<const unsigned char*>(holders[i],kHolderWeapon),&len);
        if(f && !(len>=6 && _wcsnicmp(f,L"V_FUEL",6)==0))return true;
    }
    return false;
}

// The seat's class mask lets the human sit there (CanRideSeat's own test, 0x6346FC).
bool ClassMay(const unsigned char* seat,const unsigned char* human) noexcept {
    return (At<std::uint32_t>(human,kHumanClass)&At<std::uint32_t>(seat,kSeatClassOn)&At<std::uint32_t>(seat,kSeatClassMask))!=0;
}

// The seat's input block as the vehicle's constructor leaves it (0x6291B0): sticks centred, triggers and buttons off.
void ZeroInput(unsigned char* seat) noexcept {
    const float stick[4]={0.0f,0.0f,0.0f,1.0f};
    std::memcpy(seat+kSeatLeft,stick,16);std::memcpy(seat+kSeatRight,stick,16);
    Put<float>(seat,kSeatTriggers,0.0f);Put<float>(seat,kSeatTriggers+4,0.0f);
    Put<std::uint16_t>(seat,kSeatButtons,0);
}

// The player into `seat` (steps 2-5 of the top).
void Take(unsigned char* v,unsigned char* human,unsigned char* seat) noexcept {
    reinterpret_cast<ReserveFn>(image+kReserve)(v,human,seat);
    Put<unsigned char*>(human,kHumanSeat,seat);
    Put<std::uint64_t>(human,kHumanRideBlend,0);Put<std::int32_t>(human,kHumanRideBlend2,0);Put<float>(human,kHumanRideWeight,1.0f);
    reinterpret_cast<AnnounceFn>(image+kAnnounce)(human);
    alignas(16) const std::uintptr_t action[2]={reinterpret_cast<std::uintptr_t>(image+kRideAction),0};
    reinterpret_cast<SetActionFn>(image+kSetAction)(human+kHumanActions,action,0);
    human[kHumanSeatFlag]=At<std::int32_t>(seat,kSeatKeyRow)==kKeyRowSpecial ? 1 : 0;
}

// The move (the top's steps), the NPC in `to` (when `npc`) first moved to the seat the player leaves. False: the player
// is where they were (the NPC would not move, or a step faulted and they were seated again, logged).
bool Move(unsigned char* v,unsigned char* human,unsigned from,unsigned to,bool npc) noexcept {
    unsigned char* const fromSeat=SeatAt(v,from);
    unsigned char* const toSeat=SeatAt(v,to);
    bool moved=false;
    __try {
        reinterpret_cast<ClearFn>(image+kClear)(v,fromSeat);
        if(!npc || MoveRider(v,to,from)) {
            ZeroInput(fromSeat);
            Take(v,human,toSeat);
            moved=true;
        } else reinterpret_cast<ReserveFn>(image+kReserve)(v,human,fromSeat);   // the NPC stays: the player in their own seat again
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Never seatless: the seat already theirs (the new one reserved), else their own if still free, else the new one.
        unsigned char* back=nullptr;
        __try {
            back=SeatRider(toSeat)==Rider::player && At<const unsigned char*>(toSeat,kSeatRider)==human ? toSeat :
                 SeatRider(fromSeat)==Rider::none ? fromSeat : SeatRider(toSeat)==Rider::none ? toSeat : nullptr;
            if(back)Take(v,human,back);
        } __except(EXCEPTION_EXECUTE_HANDLER){back=nullptr;}
        Log("SEAT v=%p the move %u -> %u faulted: the player %s",v,from,to,back==toSeat ? "in the new seat" : back ? "back in their own" :
            "could not be seated again");
        moved=back==toSeat;
    }
    return moved;
}

// The driver's seat left empty in the move: the stock RideAi seats an NPC there (once). A stock helicopter: heli.cpp
// flies it. A ground vehicle with the stock driving AI (NpcDrivable; the user 2026-10-07: "也可以开车，也就是通过m地图
// 指引以后可以让内部的npc开走"): the stock AI drives and fights, npcpost.cpp keeps it on its post (where it is now), and
// the map lists it with the tanks, so a guard order drives it away with the player aboard. Crew() never seats one while
// a player rides (anyPlayer), so this is the only way a driver comes. Others (a truck, the Proteus) stand.
void Pilot(unsigned char* v) noexcept {
    if(!Cfg().seatPilot || BodyOf(v)!=PluginBody::none || SeatRider(SeatAt(v,0))!=Rider::none)return;
    const bool heli=IsHelicopter(v);
    if(!heli && !NpcDrivable(v))return;
    if(!SeatNpcRider(v,false)){Log("SEAT v=%p the driver seat empty: no NPC driver here (the room host seats one)",v);return;}
    Log("SEAT v=%p the %s seat empty: %s",v,heli ? "pilot" : "driver",SeatRider(SeatAt(v,0))==Rider::dummy ?
        (heli ? "an NPC pilot seated" : "an NPC driver seated (the map's tanks)") : "the stock RideAi seated no one");
}

// Whether a move to `to` may be made now (see the top), the seat's holder `h`.
bool MayTake(unsigned char* v,const unsigned char* human,unsigned to,SeatHolder h) noexcept {
    if(h!=SeatHolder::empty && h!=SeatHolder::npc)return false;
    if(!ClassMay(SeatAt(v,to),human))return false;
    return BodyOf(v)==PluginBody::none || to!=0 || PlayerJetBoardable(v);   // the gunship's stick: where it may be boarded
}

// The seat a press asks for, -1 none: `number` 0..8 a number key, -1 the next-seat key.
int Wanted(unsigned char* v,const unsigned char* human,unsigned at,int number,unsigned count) noexcept {
    if(number>=0)return static_cast<unsigned>(number)<count && static_cast<unsigned>(number)!=at ? number : -1;
    for(unsigned k=1;k<count;++k) {
        const unsigned to=(at+k)%count;
        if(HolderOf(SeatAt(v,to),human)==SeatHolder::empty && MayTake(v,human,to,SeatHolder::empty))return static_cast<int>(to);
    }
    return -1;
}

// The seat `human` sits in now, -1 none.
int SeatOf(unsigned char* v,const unsigned char* human) noexcept {
    const unsigned count=SeatCount(v);
    for(unsigned i=0;i<count;++i)if(HolderOf(SeatAt(v,i),human)==SeatHolder::you)return static_cast<int>(i);
    return -1;
}

void Publish(unsigned char* v,const unsigned char* human,bool keys,bool locked,const Rider_& r,ULONGLONG ms) noexcept {
    SeatPrompt p{};
    const unsigned count=SeatCount(v);
    p.seats=static_cast<int>(count);p.at=SeatOf(v,human);p.keys=keys;p.locked=locked;
    p.aircraft=IsHelicopter(v) || BodyOf(v)!=PluginBody::none;
    p.refused=ms<r.refusedUntil ? r.refused : -1;
    p.hints=ms<r.promptUntil || p.refused!=-1;
    for(unsigned i=0;i<count && i<static_cast<unsigned>(kMostSeatsShown);++i) {
        const unsigned char* s=SeatAt(v,i);
        p.holder[i]=HolderOf(s,human);
        p.gun[i]=HasGun(s);
    }
    latest=p;latestMs=ms;
    const ULONGLONG until=Cfg().seatList ? ms+kFreshMs : r.promptUntil>r.refusedUntil ? r.promptUntil : r.refusedUntil;
    if(until>shownUntil)shownUntil=until;
}

// One local player in seat `at` of `v` (see the top). True when they moved.
bool Frame(unsigned char* v,unsigned char* human,unsigned at,ULONGLONG ms) noexcept {
    Rider_* const r=RiderFor(human,ms);
    if(!r)return false;
    r->seen=ms;
    const bool may=ok && Cfg().seatSwitch;   // moves at all (else the seats are only listed, SeatList)
    if(!may) {
        if(!r->vehicle.Is(v)){r->vehicle=ObjRef::Of(v);r->promptUntil=0;r->refused=-1;r->refusedUntil=0;}
        Publish(v,human,false,false,*r,ms);
        return false;
    }
    const bool locked=InSession() && !Cfg().seatSwitchOnline;
    if(!r->vehicle.Is(v)){r->vehicle=ObjRef::Of(v);r->promptUntil=locked ? 0 : ms+kPromptMs;r->refused=-1;r->refusedUntil=0;}
    const unsigned count=SeatCount(v);
    const unsigned char* const seat=SeatAt(v,at);
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    const bool nextDown=keys ? KeyDown(Cfg().seatNextKey) : Cfg().seatButton>0 && (At<std::uint16_t>(seat,kSeatButtons)&Cfg().seatButton)!=0;
    const bool next=nextDown && !r->nextHeld;
    r->nextHeld=nextDown;
    int number=-1;
    for(int k=0;k<kNumberKeys;++k) {
        const bool down=keys && Cfg().seatNumberKeys && static_cast<unsigned>(k)<count && KeyDown('1'+k);
        if(down && !r->numberHeld[k] && number<0)number=k;
        r->numberHeld[k]=down;
    }
    if(nextDown && r->promptUntil<ms+kPromptMs/2)r->promptUntil=ms+kPromptMs/2;   // held: the seats shown
    bool moved=false;
    if(next || number>=0) {
        const int to=locked ? -1 : Wanted(v,human,at,number,count);
        const SeatHolder h=to>=0 ? HolderOf(SeatAt(v,static_cast<unsigned>(to)),human) : SeatHolder::other;
        if(to>=0 && MayTake(v,human,static_cast<unsigned>(to),h)) {
            moved=Move(v,human,at,static_cast<unsigned>(to),h==SeatHolder::npc);
            Log("SEAT v=%p the player %s seat %u -> %d%s",v,moved ? "moved" : "could not move:",at,to,
                h==SeatHolder::npc ? (moved ? " (the NPC there took the seat they left)" : " (the NPC there would not move)") : "");
            if(moved && at==0)Pilot(v);
        }
        if(moved){r->promptUntil=ms+kPromptMs;r->refused=-1;r->refusedUntil=0;}
        else {
            r->refused=number>=0 ? number : kNoneFree;r->refusedUntil=ms+kRefusedMs;
            Log("SEAT v=%p seat %d refused (%s)",v,r->refused,locked ? "online, SeatSwitchOnline=0" :
                number>=0 ? "taken, not this class's, or the stick not to be taken here" : "no free seat");
        }
    }
    Publish(v,human,keys,locked,*r,ms);
    return moved;
}
}  // namespace

void SeatSwitchFrame(unsigned char* v) noexcept {
    if(!(ok && Cfg().seatSwitch) && !Cfg().seatList)return;
    if(v[kDead])return;
    const unsigned count=SeatCount(v);
    if(count<2)return;
    if(BodyOf(v)!=PluginBody::none && !GunshipCrewSeats(v))return;   // the plugin's other aircraft: one seat, or its own
    const ULONGLONG ms=GameMs();
    for(unsigned i=0;i<count;++i) {
        unsigned char* const seat=SeatAt(v,i);
        if(SeatRider(seat)!=Rider::player)continue;
        if(Frame(v,At<unsigned char*>(seat,kSeatRider),i,ms))return;   // moved: the seats changed under the loop
    }
}

bool PlayerSeatPrompt(SeatPrompt* out) noexcept {
    const ULONGLONG ms=GameMs();
    if(!latestMs || ms-latestMs>kFreshMs || ms>=shownUntil)return false;
    *out=latest;
    return true;
}

bool InstallSeatSwitch() noexcept {
    __try {
        ok=true;
        for(const auto& s:kSigs)ok=ok && Matches(s.rva,s.bytes,s.size);
    } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    Log("HOOK seat switch=%d (%s)",ok,ok ? "the stock board button's steps, within the vehicle" :
        "unexpected EDF.dll code: no moves, the seats line (SeatList) still");
    return ok;
}

void ResetSeatSwitch() noexcept {
    for(auto& r:riders)r=Rider_{};
    latest=SeatPrompt{};latestMs=shownUntil=0;
}
}  // namespace crew
