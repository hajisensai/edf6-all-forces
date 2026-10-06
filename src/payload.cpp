// The weapons of the local player's seat on a stock vehicle (payload.h, docs/stock-payload-re.md), and the stock
// helicopters' switchable stores (the user, 2026-10-06: "原版载具也补充上可切换载荷的设定").
//  - The readout: every frame the player sits in a stock vehicle (no plugin body: the plugin's aircraft have their
//    cockpit, playerjet.cpp), the seat's weapons (seat +0xC8 holders, docs/aim-line-re.md) in holder order, the fuel tank
//    every seat lists (V_FUEL01) left out: the weapon's own name (weapon +0x1B0, the game's language), its rounds and
//    full load, and its reload as the game's own weapon gauge reckons it (0x692100, §3): loaded while rounds are left,
//    else 1 - frames left (+0xE68) / ReloadTime (+0x20C), or an energy weapon's charge (+0xE7C over
//    EnergyChargeRequire +0x22C); a ReloadTime below 0 reloads never. Which control fires each one is the vehicle
//    class's input as read (§2): the 506 (N9 Eros, the 602) fires holders 0 and 1 on the primary trigger and holder 2
//    on the secondary button (fire bytes +0x2020 / +0x2021, slot 57 0x61B710), the 409 holder 0 / holder 1 (slot 57
//    0x64AF90), the 403 seat i's primary holder i (0x5FEC38), the Titan 404 seat i's primary holder i and its left
//    trigger holder i + 3 (0x5FFCA8), the flak 603 and the 503 bike holders 0 and 1 together (0x6214A4, 0x618239);
//    another class's seat with one weapon fires it on the primary trigger (M: every class read does), with more the
//    plugin has not read which (`other`: a mech's arms, each on a control of its own).
//  - The stores (ini StockHeliStores; tools/make_stock_stores.py gives the stock 506 helis' requests the jets' rocket pod
//    and Hellfires as holders 4 and on, after the fuel tank): the secondary button fires the store picked, and the
//    jets' switch (PlayerJetSwitchKey R, pad LB) goes round the stock secondary weapon and the stores, as the player
//    jets' does (playerjet.cpp Stores): its press to the next with rounds, one spent for good (no reload coming) to the
//    next by itself. The 506's own fire byte is taken (as the jets take it) and the picked weapon's trigger pulled as
//    the stock 0x62C000 pulls one (weapon +0x139, its holder alive). A store's lock beeps in the cockpit's tone
//    (jetaudio.cpp LockTone: jetsound.cpp keeps the stores' stock lock beeps quiet), the stock missile's are its own;
//    helisight.cpp's mark follows the pick (PayloadPicked). A seat with one secondary weapon is left to the stock input.
//    Only the 506: its weapon build is the loop stores.cpp lets build every holder; the 409 builds three, unrolled.
#include "crew.h"
#include "jetaudio.h"
#include "layout.h"
#include "memory.h"
#include "lockon.h"
#include "stores.h"
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
constexpr std::size_t kWeaponName=0x1B0,kWeaponTrigger=0x139,kWeaponCapacity=0x248,kWeaponReloadTime=0x20C,
                      kWeaponChargeNeed=0x22C,kWeaponReloadLeft=0xE68,kWeaponCharge=0xE7C;
constexpr std::size_t kFireSecondary=0x2021;      // the 506's secondary fire byte (docs/heli-input-re.md §2b)
constexpr std::size_t kSeatPad=0x2B0,kSeatButtons=0x2E8;
constexpr std::uint16_t kButtonLB=0x10;
constexpr std::uint64_t kHolder506Stores=4;       // the 506's holders past the fuel tank (pylib/vcobjects.py FUEL_AT 3)
constexpr ULONGLONG kFreshMs=200;                 // a readout this old (game ms) is gone: the player got out
constexpr int kTracked=8;

// The classes whose fire was read (§2); the vtables as crew.cpp kClasses has them.
enum class Class { other, heli506, heli409, tank403, tank404, flak603, bike503 };
constexpr unsigned kVt403=0x17D8FA0,kVt404=0x17D9458,kVt603=0x17DC620,kVt503=0x17DA508;
Class ClassOf(const void* v) noexcept {
    const auto vt=At<std::uintptr_t>(v,0)-reinterpret_cast<std::uintptr_t>(image);
    return vt==kVt506 ? Class::heli506 : vt==kVt409 ? Class::heli409 : vt==kVt403 ? Class::tank403 : vt==kVt404 ? Class::tank404 :
           vt==kVt603 ? Class::flak603 : vt==kVt503 ? Class::bike503 : Class::other;
}

// The switch's state per vehicle the player sits in: the weapon picked, the switch down last frame.
struct Pick { ObjRef ref; unsigned char* weapon; bool held,listed; ULONGLONG seen; };
Pick picks[kTracked]{};
PayloadReadout latest{};
ULONGLONG latestMs=0;

Pick* PickFor(const void* v,ULONGLONG ms) noexcept {
    Pick* slot=nullptr;
    for(auto& p:picks) {
        if(p.ref.Is(v))return &p;
        if(!slot && (!p.ref || p.ref.obj==v || ms-p.seen>kFreshMs*10))slot=&p;
    }
    if(slot)*slot=Pick{ObjRef::Of(v),nullptr,false,false,ms};
    return slot;
}
Pick* FindPick(const void* v) noexcept {
    for(auto& p:picks)if(p.ref.Is(v))return &p;
    return nullptr;
}

// Whether the virtual key `vk` is down while the game has the foreground (0: never).
bool KeyDown(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

// The reload as the stock gauge 0x692100 reckons it (see the top).
void Reload(const unsigned char* w,std::int32_t rounds,float* ready,float* sec) noexcept {
    *ready=1.0f;*sec=0.0f;
    if(rounds>0)return;
    const std::int32_t time=At<std::int32_t>(w,kWeaponReloadTime),left=At<std::int32_t>(w,kWeaponReloadLeft);
    const float need=At<float>(w,kWeaponChargeNeed),charge=At<float>(w,kWeaponCharge);
    float r=0.0f;
    if(std::isfinite(need) && need>0.0f)r=std::isfinite(charge) ? 1.0f-charge/need : 0.0f;
    else if(time>0)r=1.0f-static_cast<float>(left)/static_cast<float>(time);
    *ready=r<0.0f ? 0.0f : r>1.0f ? 1.0f : r;
    if(time>=0 && left>0)*sec=static_cast<float>(left)/60.0f;
}

// A weapon spent for good: no rounds and no reload coming (ReloadTime < 0, 0x692152).
bool Spent(const unsigned char* w) noexcept {
    return At<std::int32_t>(w,kWeaponAmmo)<=0 && At<std::int32_t>(w,kWeaponReloadTime)<0;
}

void CopyName(const unsigned char* w,wchar_t* out,std::size_t size) noexcept {
    out[0]=L'\0';
    const auto name=At<const wchar_t*>(w,kWeaponName);
    if(name && Readable(name,2)) {
        std::size_t n=0;
        while(n+1<size && Readable(name+n,2) && name[n])++n;
        std::memcpy(out,name,n*sizeof(wchar_t));out[n]=L'\0';
    }
    if(out[0])return;
    std::size_t n=0;
    const wchar_t* f=WeaponFile(w,&n);
    if(!f)return;
    if(n>=size)n=size-1;
    std::memcpy(out,f,n*sizeof(wchar_t));out[n]=L'\0';
}

// The vehicle holder index of seat holder `h` (veh +0x638 array), or -1.
std::int64_t HolderIndex(const unsigned char* v,const unsigned char* h) noexcept {
    const auto base=At<const unsigned char*>(v,kHolders);
    const auto count=At<std::uint64_t>(v,kHolderCount);
    if(!base || h<base || count>64)return -1;
    const auto off=static_cast<std::uint64_t>(h-base);
    return off%kHolderStride==0 && off/kHolderStride<count ? static_cast<std::int64_t>(off/kHolderStride) : -1;
}

PayloadFire FireOf(Class c,unsigned seat,std::int64_t holder,const unsigned char* w,int weapons) noexcept {
    const std::int64_t s=static_cast<std::int64_t>(seat);
    switch(c) {
        case Class::heli506:
            if(holder==0 || holder==1)return PayloadFire::primary;
            if(holder==2)return PayloadFire::secondary;
            return holder>=static_cast<std::int64_t>(kHolder506Stores) && IsStoreWeapon(w) ? PayloadFire::store : PayloadFire::other;
        case Class::heli409: return holder==0 ? PayloadFire::primary : holder==1 ? PayloadFire::secondary : PayloadFire::other;
        case Class::tank403: return holder==s ? PayloadFire::primary : PayloadFire::other;
        case Class::tank404: return holder==s ? PayloadFire::primary : holder==s+3 ? PayloadFire::secondary : PayloadFire::other;
        case Class::flak603: case Class::bike503: return holder==0 || holder==1 ? PayloadFire::primary : PayloadFire::other;
        default: return weapons==1 ? PayloadFire::primary : PayloadFire::other;
    }
}

// The seat the first local player sits in, -1 with none.
int PlayerSeatOf(unsigned char* v) noexcept {
    const unsigned count=SeatCount(v);
    for(unsigned i=0;i<count;++i)if(SeatRider(SeatAt(v,i))==Rider::player)return static_cast<int>(i);
    return -1;
}

// The seat's weapons (`ws`, at most kMostPayload, the fuel tank left out) and their entries.
int ReadSeat(unsigned char* v,unsigned seat,Class c,unsigned char** ws,PayloadReadout& r) noexcept {
    const auto s=SeatAt(v,seat);
    const auto holders=At<unsigned char* const*>(s,kSeatWeapons);
    const auto n=At<std::uint64_t>(s,kSeatWeaponCount);
    if(n>16 || !Readable(holders,n*8))return 0;
    std::int64_t index[kMostPayload];
    int count=0;
    for(std::uint64_t i=0;i<n && count<kMostPayload;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        unsigned char* const w=At<unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponCharge+4) || IsFuelTank(w))continue;
        index[count]=HolderIndex(v,holders[i]);
        ws[count++]=w;
    }
    for(int i=0;i<count;++i) {
        PayloadEntry& e=r.entry[i];
        e=PayloadEntry{};
        CopyName(ws[i],e.name,_countof(e.name));
        const std::int32_t rounds=At<std::int32_t>(ws[i],kWeaponAmmo),cap=At<std::int32_t>(ws[i],kWeaponCapacity);
        e.rounds=rounds>0 ? rounds : 0;
        e.capacity=cap>0 ? cap : 0;
        Reload(ws[i],rounds,&e.ready,&e.reloadSec);
        e.fire=FireOf(c,seat,index[i],ws[i],count);
        e.homing=At<std::int32_t>(ws[i],kWeaponLockon)==kHoming;
    }
    return count;
}

// The holder of weapon `w` alive (0x62C000's test: its control block's use count): its trigger may be pulled.
bool HolderAlive(const unsigned char* v,const unsigned char* w) noexcept {
    const auto base=At<const unsigned char*>(v,kHolders);
    const auto count=At<std::uint64_t>(v,kHolderCount);
    if(!base || count>64 || !Readable(base,count*kHolderStride))return false;
    for(std::uint64_t i=0;i<count;++i) {
        const unsigned char* h=base+i*kHolderStride;
        if(At<const unsigned char*>(h,kHolderWeapon)!=w)continue;
        const auto ctrl=At<const unsigned char*>(h,kHolderCtrl);
        return ctrl && Readable(ctrl,0x10) && At<std::int32_t>(ctrl,8)>0;
    }
    return false;
}

void Lock(unsigned char* w) noexcept {
    if(!IsStoreWeapon(w))return;   // the stock missile beeps itself
    float at[3],progress=0.0f;
    const int lock=WeaponLock(w,at,&progress);
    audio::LockTone(lock,progress);
}

// The store switch on a 506's pilot seat (see the top). `ws` the seat's weapons, `r` their entries.
void Switch(unsigned char* v,const unsigned char* seat,Pick& p,unsigned char* const* ws,PayloadReadout& r) noexcept {
    int list[kMostPayload],n=0;
    for(int i=0;i<r.count;++i)
        if(r.entry[i].fire==PayloadFire::secondary || r.entry[i].fire==PayloadFire::store)list[n++]=i;
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    const bool down=keys ? KeyDown(Cfg().playerJetSwitchKey) : (At<std::uint16_t>(seat,kSeatButtons)&kButtonLB)!=0;
    const bool press=down && !p.held;
    p.held=down;
    if(n<2 || !Cfg().stockHeliStores){p.weapon=nullptr;return;}   // one secondary weapon: the stock input fires it
    int at=0;
    for(int k=0;k<n;++k)if(ws[list[k]]==p.weapon)at=k;
    if(!p.weapon || ws[list[at]]!=p.weapon)at=0;
    if(!p.listed) {
        p.listed=true;
        for(int k=0;k<n;++k)Log("PAYLOAD v=%p secondary %d: %ls (%d rounds)%s",v,k,r.entry[list[k]].name,r.entry[list[k]].rounds,k==at ? " [picked]" : "");
    }
    if(press || Spent(ws[list[at]])) {
        const int was=at;
        for(int k=1;k<=n;++k) {
            const int next=(at+k)%n;
            if(!Spent(ws[list[next]]) || k==n){at=next;break;}
        }
        if(at!=was){ClearWeaponLock(ws[list[was]]);ClearWeaponLock(ws[list[at]]);}
        if(press)Log("PAYLOAD v=%p secondary: %ls (%d rounds)",v,r.entry[list[at]].name,r.entry[list[at]].rounds);
    }
    p.weapon=ws[list[at]];
    r.entry[list[at]].picked=true;
    r.picked=list[at];r.choices=n;
    const bool fire=v[kFireSecondary]!=0;
    v[kFireSecondary]=0;   // the stock slot 57 would fire holder 2 itself
    if(fire && HolderAlive(v,p.weapon) && Readable(p.weapon+kWeaponTrigger,1,true))p.weapon[kWeaponTrigger]=1;
    Lock(p.weapon);
}
}  // namespace

void PayloadFrame(unsigned char* v) noexcept {
    if(v[kDead] || BodyOf(v)!=PluginBody::none)return;
    const int seat=PlayerSeatOf(v);
    if(seat<0)return;
    const ULONGLONG ms=GameMs();
    Pick* const p=PickFor(v,ms);
    if(!p)return;
    p->seen=ms;
    const Class c=ClassOf(v);
    PayloadReadout r{};
    unsigned char* ws[kMostPayload]{};
    r.seat=seat;r.seats=static_cast<int>(SeatCount(v));r.picked=-1;
    r.count=ReadSeat(v,static_cast<unsigned>(seat),c,ws,r);
    const unsigned char* const s=SeatAt(v,static_cast<unsigned>(seat));
    r.keys=At<unsigned char>(s,kSeatPad)==0;
    if(c==Class::heli506 && seat==0)Switch(v,s,*p,ws,r);
    else p->weapon=nullptr;
    latest=r;latestMs=ms;
}

bool PlayerPayload(PayloadReadout* out) noexcept {
    if(!latestMs || GameMs()-latestMs>kFreshMs)return false;
    *out=latest;
    return true;
}

unsigned char* PayloadPicked(const void* vehicle) noexcept {
    const Pick* const p=FindPick(vehicle);
    return p && p->weapon && GameMs()-p->seen<=kFreshMs ? p->weapon : nullptr;
}

void ResetPayload() noexcept {
    for(auto& p:picks)p=Pick{};
    latest=PayloadReadout{};latestMs=0;
}
}  // namespace crew
