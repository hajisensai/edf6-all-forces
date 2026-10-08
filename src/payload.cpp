// The weapons of the local player's seat on a stock vehicle (payload.h, docs/stock-payload-re.md), and the stock
// vehicles' switchable stores (the user, 2026-10-06: "原版载具也补充上可切换载荷的设定"; 2026-10-07: "给载具应有的多种挂载
// 增加多种挂载。例如原版坦克、aa车、直升机等" "应该有的都得有，比如导弹车").
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
//  - The stores (ini StockVehicleStores, or the older StockHeliStores; tools/make_stock_stores.py hangs them on the
//    requests of the tanks, the flak, the missile launcher, the Grape, the bikes and the helicopters, holders after the
//    stock ones that stores.cpp builds): catalogued stores (IsStoreWeapon) on the seat ride one stock
//    control, the seat's second one when a stock weapon is on it (the helicopters' missile, the Titan's gatling), else
//    its first (the gun). The jets' switch (PlayerJetSwitchKey R, pad LB) goes round that control's stock weapon and the
//    stores as the player jets' does (playerjet.cpp Stores): its press to the next with rounds, one spent for good (no
//    reload coming) to the next by itself. With a store picked the control still pulls its stock weapons' triggers
//    (every stock vehicle fires through the holder pull 0x62C000, in whichever slot it fires: 55, 57, the bike's 5);
//    the pull is taken over (PullHook) and lands on the store picked instead: the trigger latch (weapon +0x139) the
//    weapon's own update reads and clears (0x6935B2). A store's lock beeps in the cockpit's tone (jetaudio.cpp
//    LockTone: jetsound.cpp keeps the stores' stock lock beeps quiet), the stock missile's are its own; helisight.cpp's
//    mark follows the pick (PayloadPicked). A seat with no store is left to the stock input, and so is a seat whose
//    stores ride no stock weapon of a control read for its class.
#include "crew.h"
#include "jetaudio.h"
#include "layout.h"
#include "memory.h"
#include "lockon.h"
#include "stores.h"
#include "retired_loadout.h"
#include "edf/weapon.h"
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
constexpr std::size_t kWeaponName=0x1B0,kWeaponTrigger=0x139,kWeaponCapacity=0x248,kWeaponReloadTime=0x20C,
                      kWeaponChargeNeed=0x22C,kWeaponReloadLeft=0xE68,kWeaponCharge=0xE7C;
constexpr std::size_t kSeatPad=0x2B0,kSeatButtons=0x2E8;
constexpr std::uint16_t kButtonLB=0x10;
constexpr ULONGLONG kFreshMs=200;                 // a readout this old (game ms) is gone: the player got out
constexpr int kTracked=64;

// The classes whose fire was read (§2); the vtables as crew.cpp kClasses has them.
enum class Class { other, heli506, heli409, tank403, tank404, flak603, bike503 };
constexpr unsigned kVt403=0x17D8FA0,kVt404=0x17D9458,kVt603=0x17DC620,kVt503=0x17DA508;
Class ClassOf(const void* v) noexcept {
    const auto vt=At<std::uintptr_t>(v,0)-reinterpret_cast<std::uintptr_t>(image);
    return vt==kVt506 ? Class::heli506 : vt==kVt409 ? Class::heli409 : vt==kVt403 ? Class::tank403 : vt==kVt404 ? Class::tank404 :
           vt==kVt603 ? Class::flak603 : vt==kVt503 ? Class::bike503 : Class::other;
}

// The switch's state per vehicle the player sits in: the weapon picked, the switch down last frame.
struct Pick { ObjRef ref; unsigned seat; unsigned char* weapon; bool held,listed,npc; ULONGLONG seen; unsigned char* from[kMostPayload]; int redirects; };
Pick picks[kTracked]{};
// One tracked local player's fire-control view. Store selection remains independent and keeps its old contract.
struct SightPick { ObjRef vehicle,human; unsigned seat; PayloadFire control=PayloadFire::primary; bool primaryHeld,secondaryHeld; };
SightPick sightPick{};
PayloadReadout latest{};
ULONGLONG latestMs=0;

// UI request transport: only immutable numbers cross threads. Native identities are
// kept on the game thread and compared again before a queued selection is applied.
struct ChoiceIdentity { const void* weapon; const void* holder; const void* ctrl; const void* spec; PayloadFire fire; bool selectable; };
struct ChoiceContext {
    ObjRef vehicle,human; const void* seatObject; int seat,count; Class kind; bool enabled,aircraft;
    ChoiceIdentity entry[kMostPayload]; std::uint64_t token;
};
ChoiceContext choiceContext{};
PayloadReadout selectableLatest{};
ULONGLONG selectableAt=0; // wall time; protected with the complete readout by choiceLock
std::uint64_t nextChoiceToken=0; // never reset across missions: delayed UI clicks cannot alias a new snapshot
struct ChoiceRequest { std::uint64_t token; int seat,entry; ULONGLONG posted; };
struct ChoicePublication { std::uint64_t token; int seat,count; bool selectable[kMostPayload]; ULONGLONG at; };
SRWLOCK choiceLock=SRWLOCK_INIT;
ChoiceRequest choiceRequest{};
ChoicePublication choicePublication{};
void ClearChoiceContext(const void* vehicle=nullptr) noexcept {
    if(vehicle && choiceContext.vehicle.obj!=vehicle)return;
    choiceContext=ChoiceContext{};
    AcquireSRWLockExclusive(&choiceLock);
    selectableLatest=PayloadReadout{};selectableAt=0;
    choiceRequest=ChoiceRequest{};choicePublication=ChoicePublication{};
    ReleaseSRWLockExclusive(&choiceLock);
}
void PublishChoices(unsigned char* v,unsigned char* const* ws,PayloadReadout& r,bool aircraft=false,const Store* stores=nullptr) noexcept {
    ChoiceContext next{};
    next.aircraft=aircraft;
    next.vehicle=ObjRef::Of(v);next.human=ObjRef::Of(PlayerHuman());next.seat=r.seat;
    next.seatObject=SeatAt(v,static_cast<unsigned>(r.seat));next.count=r.count;next.kind=ClassOf(v);
    const auto holders=At<unsigned char* const*>(next.seatObject,kSeatWeapons);
    const auto count=At<std::uint64_t>(next.seatObject,kSeatWeaponCount);
    bool same=choiceContext.vehicle.Is(v) && choiceContext.human.Is(PlayerHuman()) && choiceContext.seat==next.seat &&
              choiceContext.seatObject==next.seatObject && choiceContext.count==next.count && choiceContext.kind==next.kind && choiceContext.aircraft==next.aircraft;
    for(int i=0;i<r.count;++i) {
        auto& e=next.entry[i];e.weapon=ws[i];e.spec=stores ? stores[i].spec : nullptr;e.fire=r.entry[i].fire;e.selectable=r.entry[i].selectable;
        next.enabled=next.enabled || e.selectable;
        if(count<=16 && Readable(holders,count*8))for(std::uint64_t h=0;h<count;++h)
            if(Readable(holders[h],kHolderWeapon+8) && At<const void*>(holders[h],kHolderWeapon)==ws[i]) {
                e.holder=holders[h];e.ctrl=At<const void*>(holders[h],kHolderCtrl);break;
            }
        const auto& old=choiceContext.entry[i];
        same=same && e.weapon==old.weapon && e.holder==old.holder && e.ctrl==old.ctrl && e.spec==old.spec && e.fire==old.fire && e.selectable==old.selectable;
    }
    same=same && next.enabled==choiceContext.enabled;
    if(!same){if(++nextChoiceToken==0)++nextChoiceToken;next.token=nextChoiceToken;}
    else next.token=choiceContext.token;
    choiceContext=next;
    r.selectionToken=next.enabled ? next.token : 0;
}
// Publish only after the selection result is final. Request admission metadata and
// the complete UI readout share one lock/epoch; the draw thread sees no game pointers.
void PublishSelectableSnapshot(const PayloadReadout& r) noexcept {
    AcquireSRWLockExclusive(&choiceLock);
    selectableLatest=r;selectableAt=GetTickCount64();
    choicePublication=ChoicePublication{};choicePublication.token=r.selectionToken;choicePublication.seat=r.seat;
    choicePublication.count=r.count;choicePublication.at=selectableAt;
    for(int i=0;i<r.count;++i)choicePublication.selectable[i]=r.entry[i].selectable;
    ReleaseSRWLockExclusive(&choiceLock);
}

int ConsumeChoice(const PayloadReadout& r) noexcept {
    AcquireSRWLockExclusive(&choiceLock);
    const auto request=choiceRequest;choiceRequest=ChoiceRequest{};
    ReleaseSRWLockExclusive(&choiceLock);
    return request.token && request.token==r.selectionToken && request.seat==r.seat && request.entry>=0 && request.entry<r.count &&
        GetTickCount64()-request.posted<=kFreshMs && r.entry[request.entry].selectable ? request.entry : -1;
}

Pick* PickFor(const void* v,unsigned seat,ULONGLONG ms) noexcept {
    Pick* slot=nullptr;
    for(auto& p:picks) {
        if(p.ref.Is(v) && p.seat==seat)return &p;
        if(!slot && (!p.ref || (p.ref.obj==v && !p.ref.Is(v)) || ms-p.seen>kFreshMs*10))slot=&p;
    }
    if(slot){*slot=Pick{};slot->ref=ObjRef::Of(v);slot->seat=seat;slot->seen=ms;}
    return slot;
}
Pick* FindPick(const void* v) noexcept {
    for(auto& p:picks)if(p.ref.Is(v) && !p.npc && p.seat<SeatCount(static_cast<const unsigned char*>(v)) &&
        SeatRider(SeatAt(static_cast<unsigned char*>(const_cast<void*>(v)),p.seat))==Rider::player &&
        At<const void*>(SeatAt(static_cast<unsigned char*>(const_cast<void*>(v)),p.seat),kSeatRider)==PlayerHuman())return &p;
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

// `weapons`: the seat's stock weapons (its stores left out).
PayloadFire FireOf(Class c,unsigned seat,std::int64_t holder,const unsigned char* w,int weapons) noexcept {
    const std::int64_t s=static_cast<std::int64_t>(seat);
    // EDF6VC_ also names native drill/Katyusha/Proteus weapons. Only catalogued add-on stores are redirectable.
    if(IsStoreWeapon(w))return PayloadFire::store;
    switch(c) {
        case Class::heli506: return holder==0 || holder==1 ? PayloadFire::primary : holder==2 ? PayloadFire::secondary : PayloadFire::other;
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
    for(unsigned i=0;i<count;++i)if(SeatRider(SeatAt(v,i))==Rider::player &&
        At<const void*>(SeatAt(v,i),kSeatRider)==PlayerHuman())return static_cast<int>(i);
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
        const auto ctrl=At<const unsigned char*>(holders[i],kHolderCtrl);
        if(!Readable(ctrl,12) || At<std::int32_t>(ctrl,8)<=0)continue;
        unsigned char* const w=At<unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponCharge+4) || IsFuelTank(w))continue;
        if(RetiredLoadout(w)){ReportRetiredLoadout();continue;}
        index[count]=HolderIndex(v,holders[i]);
        ws[count++]=w;
    }
    int stock=0;
    for(int i=0;i<count;++i)stock+=IsStoreWeapon(ws[i]) ? 0 : 1;
    for(int i=0;i<count;++i) {
        PayloadEntry& e=r.entry[i];
        e=PayloadEntry{};
        CopyName(ws[i],e.name,_countof(e.name));
        const std::int32_t rounds=At<std::int32_t>(ws[i],kWeaponAmmo),cap=At<std::int32_t>(ws[i],kWeaponCapacity);
        e.rounds=rounds>0 ? rounds : 0;
        e.capacity=cap>0 ? cap : 0;
        Reload(ws[i],rounds,&e.ready,&e.reloadSec);
        e.fire=FireOf(c,seat,index[i],ws[i],stock);
        e.homing=At<std::int32_t>(ws[i],kWeaponLockon)==kHoming;
    }
    return count;
}

void Lock(unsigned char* w) noexcept {
    if(!IsStoreWeapon(w))return;   // the stock missile beeps itself
    float at[3],progress=0.0f;
    const int lock=WeaponLock(w,at,&progress);
    audio::LockTone(lock,progress);
}

// Redirects belong to one live vehicle and seat; NPC and local-player seats coexist.
void ClearRedirect(const void* v) noexcept {
    for(auto& p:picks)if(!v || p.ref.obj==v)p.redirects=0;
}
void __fastcall PullHook(unsigned char* holder) {
    const auto ctrl=At<const unsigned char*>(holder,kHolderCtrl);
    if(!ctrl || At<std::int32_t>(ctrl,8)==0)return;
    unsigned char* w=At<unsigned char*>(holder,kHolderWeapon);
    const ULONGLONG ms=GameMs();
    __try {
    if(Cfg().enabled && Cfg().stockStores)for(const auto& p:picks) {
        if(!p.redirects || ms-p.seen>kFreshMs || !Readable(p.ref.obj,kSelfCtrl+8) || !p.ref.Is(p.ref.obj))continue;
        const auto v=static_cast<unsigned char*>(const_cast<void*>(p.ref.obj));
        if(p.seat>=SeatCount(v) || v[kDead])continue;
        const auto seat=SeatAt(v,p.seat);
        if(p.npc ? !AiGunner(v,seat) : SeatRider(seat)!=Rider::player)continue;
        const auto list=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
        bool installed=false;
        if(count<=16 && Readable(list,count*8))for(std::uint64_t i=0;i<count;++i) {
            if(!Readable(list[i],kHolderWeapon+8))continue;
            const auto targetCtrl=At<const unsigned char*>(list[i],kHolderCtrl);
            if(Readable(targetCtrl,12) && At<std::int32_t>(targetCtrl,8)>0 && At<unsigned char*>(list[i],kHolderWeapon)==p.weapon)installed=true;
        }
        if(!installed)continue;
        for(int i=0;i<p.redirects;++i)if(p.from[i]==w){w=p.weapon;break;}
    }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if(Readable(w,kWeaponTrigger+1))w[kWeaponTrigger]=1;
}
constexpr unsigned kPull=0x62C000;
const unsigned char kPullCode[]={0x48,0x8B,0x41,0x08,0x48,0x85,0xC0,0x74,0x11,0x83,0x78,0x08,0x00,0x74,0x0B,0x48,0x8B,0x41,0x10,
                                 0xC6,0x80,0x39,0x01,0x00,0x00,0x01,0xC3};
bool pullOk=false;

// The store switch on the player's seat (see the top). `ws` the seat's weapons, `r` their entries.
bool Switch(unsigned char* v,const unsigned char* seat,Pick& p,unsigned char* const* ws,PayloadReadout& r,bool* uiChoice) noexcept {
    *uiChoice=false;
    PayloadFire ride=PayloadFire::primary;
    for(int i=0;i<r.count;++i)if(r.entry[i].fire==PayloadFire::secondary)ride=PayloadFire::secondary;
    int stock[kMostPayload],ns=0,list[kMostPayload],n=0;
    for(int i=0;i<r.count;++i)if(r.entry[i].fire==ride)stock[ns++]=i;
    if(ns)list[n++]=stock[0];   // that control's stock weapon(s): one choice (the 603's pair fire together)
    for(int i=0;i<r.count;++i)if(r.entry[i].fire==PayloadFire::store)list[n++]=i;
    const bool enabled=Cfg().enabled && pullOk && Cfg().stockStores && ns>0 && n>=2;
    if(enabled)for(int k=0;k<n;++k)r.entry[list[k]].selectable=!Spent(ws[list[k]]);
    PublishChoices(v,ws,r);
    const int requested=ConsumeChoice(r);
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    // During a map hold follow the physical key only to drain its edge; do not replay it on close.
    const int vk=Cfg().playerJetSwitchKey;
    const bool down=keys ? (MapHoldsKeys() ? vk>0 && (GetAsyncKeyState(vk)&0x8000)!=0 : KeyDown(vk)) :
        (At<std::uint16_t>(seat,kSeatButtons)&kButtonLB)!=0;
    const bool press=!MapHoldsKeys() && down && !p.held;
    p.held=down;
    if(!enabled){p.weapon=nullptr;return Cfg().enabled && pullOk && Cfg().stockStores && ns>0 && press;}
    int at=0;
    for(int k=0;k<n;++k)if(ws[list[k]]==p.weapon)at=k;
    if(!p.weapon || ws[list[at]]!=p.weapon)at=0;
    if(!p.listed) {
        p.listed=true;
        for(int k=0;k<n;++k)Log("PAYLOAD v=%p %s %d: %ls (%d rounds)%s",v,ride==PayloadFire::secondary ? "secondary" : "primary",k,
                                r.entry[list[k]].name,r.entry[list[k]].rounds,k==at ? " [picked]" : "");
    }
    int requestedAt=-1;
    for(int k=0;k<n;++k)if(list[k]==requested)requestedAt=k;
    if(requestedAt>=0) {
        if(at!=requestedAt){ClearWeaponLock(ws[list[at]]);ClearWeaponLock(ws[list[requestedAt]]);}
        at=requestedAt;*uiChoice=true;
    } else if(press || Spent(ws[list[at]])) {
        const int was=at;
        for(int k=1;k<=n;++k) {
            const int next=(at+k)%n;
            if(!Spent(ws[list[next]]) || k==n){at=next;break;}
        }
        if(at!=was){ClearWeaponLock(ws[list[was]]);ClearWeaponLock(ws[list[at]]);}
        if(press)Log("PAYLOAD v=%p picked: %ls (%d rounds)",v,r.entry[list[at]].name,r.entry[list[at]].rounds);
    }
    p.weapon=ws[list[at]];
    r.entry[list[at]].picked=true;
    r.picked=list[at];r.choices=n;r.switchButton=kButtonLB;
    if(at>0)for(int k=0;k<ns && p.redirects<kMostPayload;++k)p.from[p.redirects++]=ws[stock[k]];
    Lock(p.weapon);
    return press || *uiChoice;
}
}  // namespace

void PayloadFrame(unsigned char* v) noexcept {
    if(!Cfg().enabled){ClearRedirect(v);ClearChoiceContext(v);return;}
    if(v[kDead]){ClearRedirect(v);ClearChoiceContext(v);return;}
    if(BodyOf(v)!=PluginBody::none){ClearRedirect(v);if(!choiceContext.aircraft)ClearChoiceContext(v);return;}
    const int seat=PlayerSeatOf(v);
    if(seat<0){if(sightPick.vehicle.Is(v))sightPick=SightPick{};ClearChoiceContext(v);return;}
    const ULONGLONG ms=GameMs();
    Pick* const p=PickFor(v,static_cast<unsigned>(seat),ms);
    if(!p)return;
    const bool takeover=p->npc;
    p->seen=ms;p->npc=false;
    const Class c=ClassOf(v);
    PayloadReadout r{};
    unsigned char* ws[kMostPayload]{};
    r.seat=seat;r.seats=static_cast<int>(SeatCount(v));r.picked=-1;
    r.count=ReadSeat(v,static_cast<unsigned>(seat),c,ws,r);
    const unsigned char* const s=SeatAt(v,static_cast<unsigned>(seat));
    r.keys=At<unsigned char>(s,kSeatPad)==0;
    const bool primary=At<float>(s,0x2E4)>=0.8f;
    const bool secondary=(c==Class::heli506 || c==Class::heli409) ?
        (At<std::uint16_t>(s,kSeatButtons)&0x20)!=0 : At<float>(s,0x2E0)>=0.8f;
    const bool fresh=!sightPick.vehicle.Is(v) || !sightPick.human.Is(PlayerHuman()) || sightPick.seat!=static_cast<unsigned>(seat) || takeover;
    if(fresh) {
        sightPick={ObjRef::Of(v),ObjRef::Of(PlayerHuman()),static_cast<unsigned>(seat),PayloadFire::primary,primary,secondary};
        p->held=true; // a switch/trigger already held while entering is not a new action
        if(takeover)p->weapon=nullptr;
    }
    p->redirects=0;
    bool uiChoice=false;
    const bool chose=Switch(v,s,*p,ws,r,&uiChoice);
    if(uiChoice && MapHoldsKeys())sightPick.control=PayloadFire::store;
    if(!MapHoldsKeys()) {
        // Same-frame trigger edges prefer the primary. Explicit R/LB selection aims the selected payload without firing.
        if(primary && !sightPick.primaryHeld)sightPick.control=PayloadFire::primary;
        else if(secondary && !sightPick.secondaryHeld)sightPick.control=PayloadFire::secondary;
        else if(chose)sightPick.control=PayloadFire::store;
    }
    sightPick.primaryHeld=primary;sightPick.secondaryHeld=secondary;
    latest=r;latestMs=ms;PublishSelectableSnapshot(r);
}

// Select only loaded weapons already attached to this seat. The native caller owns
// aiming/firing; no shot or weapon object is fabricated here.
unsigned char* NpcPayloadSelect(unsigned char* v,unsigned seat,float distance,bool airborne,PayloadFire* fire) noexcept {
    if(fire)*fire=PayloadFire::other;
    if(!pullOk || !Cfg().enabled || !Cfg().stockStores || !Cfg().npcGunners || !v || v[kDead] ||
       BodyOf(v)!=PluginBody::none || seat>=SeatCount(v))return nullptr;
    const auto s=SeatAt(v,seat);
    if(!AiGunner(v,s))return nullptr;
    Pick* const p=PickFor(v,seat,GameMs());
    if(!p)return nullptr;
    const auto previous=p->weapon;
    p->seen=GameMs();p->npc=true;p->redirects=0;p->weapon=nullptr;
    if(!std::isfinite(distance) || distance<=0.0f)return nullptr;
    PayloadReadout r{};unsigned char* ws[kMostPayload]{};
    r.count=ReadSeat(v,seat,ClassOf(v),ws,r);
    PayloadFire ride=PayloadFire::primary;
    for(int i=0;i<r.count;++i)if(r.entry[i].fire==PayloadFire::secondary)ride=PayloadFire::secondary;
    int best=-1;float score=-1.0f;
    for(int i=0;i<r.count;++i) {
        if(r.entry[i].fire==PayloadFire::other || r.entry[i].rounds<=0)continue;
        const auto w=ws[i];
        if(!(At<float>(w,0x89C)>0.0f))continue; // healers never target enemies
        float muzzle[3],dir[3];RoundModel m{};
        if(!edf::MeanMuzzle(w,64,muzzle,dir) || !ReadRound(w,&m) || m.kind==RoundKind::none)continue;
        // Native NPC engagement reach. Guided stores also need a reachable lock; an
        // accelerating rocket must not be limited to its initial speed times lifetime.
        float reach=At<float>(w,0x224);
        if(!std::isfinite(reach) || reach<=0.0f)continue;
        if(m.kind==RoundKind::homing) {
            const float lockReach=At<float>(w,0x6D0);
            if(!std::isfinite(lockReach) || lockReach<=0.0f)continue;
            reach=std::fmin(reach,lockReach);
        } else if(m.kind==RoundKind::arc) {
            if(!std::isfinite(m.speed) || m.speed<=0.0f || m.alive<=0)continue;
            reach=std::fmin(reach,m.speed*static_cast<float>(m.alive));
        }
        if(!std::isfinite(reach) || reach<distance)continue;
        const float blast=At<float>(w,0x8B0);
        if(!std::isfinite(blast) || (blast>0.0f && distance<=blast*2.0f))continue;
        const StoreSpec* const spec=StoreOf(w);
        const auto mark=At<std::int32_t>(w,edf::kWeaponMark);
        if(spec && ((airborne && (spec->role==StoreRole::ground || spec->role==StoreRole::bomb || spec->role==StoreRole::rocket)) ||
                    (!airborne && spec->role==StoreRole::air)))continue;
        if(airborne && (m.lobbed || mark==edf::kMarkGround || mark==edf::kMarkLofted))continue;
        if(!airborne && mark==edf::kMarkAir)continue;
        // Guided fire at long range, direct fire close up, splash against ground targets.
        float rank=m.kind==RoundKind::homing ? (distance>150.0f ? 4.0f : 2.0f) : airborne ? 3.0f : blast>0.0f ? 3.5f : 2.5f;
        if(ws[i]==previous)rank+=0.1f;
        if(rank>score){score=rank;best=i;}
    }
    if(best<0)return nullptr;
    p->weapon=ws[best];
    if(r.entry[best].fire==PayloadFire::store)
        for(int i=0;i<r.count;++i)if(r.entry[i].fire==ride)p->from[p->redirects++]=ws[i];
    if(r.entry[best].fire==PayloadFire::store && !p->redirects){p->weapon=nullptr;return nullptr;}
    if(previous && previous!=p->weapon && Readable(previous,kWeaponCharge+4))ClearWeaponLock(previous);
    if(fire)*fire=r.entry[best].fire==PayloadFire::store ? ride : r.entry[best].fire;
    return p->weapon;
}

bool PlayerPayload(PayloadReadout* out) noexcept {
    if(!latestMs || GameMs()-latestMs>kFreshMs)return false;
    *out=latest;
    return true;
}

bool RequestPayloadSelection(std::uint64_t token,int seat,int entry) noexcept {
    if(!token || seat<0 || entry<0 || entry>=kMostPayload)return false;
    const ULONGLONG now=GetTickCount64();
    AcquireSRWLockExclusive(&choiceLock);
    const bool valid=token==choicePublication.token && seat==choicePublication.seat && entry<choicePublication.count &&
        choicePublication.selectable[entry] && now-choicePublication.at<=kFreshMs;
    if(valid)choiceRequest=ChoiceRequest{token,seat,entry,now};
    ReleaseSRWLockExclusive(&choiceLock);
    return valid;
}

bool PlayerSelectablePayload(PayloadReadout* out) noexcept {
    if(!out)return false;
    AcquireSRWLockShared(&choiceLock);
    const bool fresh=selectableAt && GetTickCount64()-selectableAt<=kFreshMs;
    if(fresh)*out=selectableLatest;
    ReleaseSRWLockShared(&choiceLock);
    return fresh;
}

void ForgetAircraftPayload(const void* vehicle) noexcept {
    if(choiceContext.aircraft)ClearChoiceContext(vehicle);
}
int AircraftPayloadChoice(unsigned char* v,const Store* stores,int count,int picked) noexcept {
    if(!Cfg().enabled || !Cfg().playerJet || !v || v[kDead] || SeatCount(v)==0 || count<=0 || count>kMostPayload || !stores ||
       SeatRider(SeatAt(v,0))!=Rider::player || At<const void*>(SeatAt(v,0),kSeatRider)!=PlayerHuman()) {
        ForgetAircraftPayload(v);return -1;
    }
    PayloadReadout r{};r.seat=0;r.seats=static_cast<int>(SeatCount(v));r.count=count;r.picked=picked;
    r.keys=At<unsigned char>(SeatAt(v,0),kSeatPad)==0;r.switchButton=kButtonLB;
    unsigned char* ws[kMostPayload]{};
    const auto holders=At<unsigned char* const*>(SeatAt(v,0),kSeatWeapons);
    const auto n=At<std::uint64_t>(SeatAt(v,0),kSeatWeaponCount);
    for(int i=0;i<count;++i) {
        const auto& st=stores[i];auto& e=r.entry[i];ws[i]=st.weapon;e.fire=PayloadFire::store;e.picked=i==picked;
        if(st.spec && st.spec->name)_snwprintf_s(e.name,_countof(e.name),_TRUNCATE,L"%hs",st.spec->name);
        e.rounds=st.ammo>0 ? st.ammo : 0;e.capacity=e.rounds;e.ready=e.rounds>0 ? 1.0f : 0.0f;
        e.homing=st.spec && (st.spec->role==StoreRole::air || st.spec->role==StoreRole::ground);
        bool installed=false;
        if(st.weapon && n<=16 && Readable(holders,n*8))for(std::uint64_t h=0;h<n;++h) {
            if(!Readable(holders[h],kHolderWeapon+8) || At<unsigned char*>(holders[h],kHolderWeapon)!=st.weapon)continue;
            const auto ctrl=At<const void*>(holders[h],kHolderCtrl);
            installed=Readable(ctrl,12) && At<int>(ctrl,8)>0;break;
        }
        // Spec identity and native ammo are re-read, rather than trusting a cached Store readout.
        if(installed && Readable(st.weapon,kWeaponAmmo+4) && StoreOf(st.weapon)==st.spec && st.spec) {
            const auto rounds=At<std::int32_t>(st.weapon,kWeaponAmmo),capacity=At<std::int32_t>(st.weapon,kWeaponCapacity);
            e.rounds=rounds>0 ? rounds : 0;e.capacity=capacity>0 ? capacity : 0;e.ready=e.rounds>0 ? 1.0f : 0.0f;
            e.selectable=count>1 && e.rounds>0;
        }
        if(e.selectable)++r.choices;
    }
    PublishChoices(v,ws,r,true,stores);
    const int chosen=ConsumeChoice(r);
    if(chosen>=0){r.picked=chosen;for(int i=0;i<count;++i)r.entry[i].picked=i==chosen;}
    PublishSelectableSnapshot(r);
    return chosen;
}

void PumpPayloadUi(unsigned char* human) noexcept {
    if(!MapHoldsKeys() || human!=PlayerHuman())return;
    __try {
        if(!Cfg().enabled || !Readable(human,kHumanVehicleCtrl+8) || !IsPlayer(human) || human[kDead]) {
            ClearChoiceContext();return;
        }
        const auto ctrl=At<const void*>(human,kHumanVehicleCtrl);
        const auto v=At<unsigned char*>(human,kHumanVehicleCtrl-8);
        if(!Readable(ctrl,12) || At<int>(ctrl,8)<=0 || !Readable(v,kSeatCount+8) || At<const void*>(v,kSelfCtrl)!=ctrl || v[kDead]) {
            ClearChoiceContext();return;
        }
        if(BodyOf(v)==PluginBody::none)PayloadFrame(v);
        else PumpAircraftPayloadUi(v);
    } __except(EXCEPTION_EXECUTE_HANDLER) { ClearChoiceContext(); }
}

unsigned char* PayloadPicked(const void* vehicle) noexcept {
    const Pick* const p=FindPick(vehicle);
    return p && p->weapon && GameMs()-p->seen<=kFreshMs ? p->weapon : nullptr;
}

unsigned char* PayloadSightPicked(const void* vehicle,unsigned index) noexcept {
    unsigned char* weapon=nullptr;
    return PayloadSightWeapons(vehicle,index,&weapon,1)>0 ? weapon : nullptr;
}

int PayloadSightWeapons(const void* vehicle,unsigned index,unsigned char** out,int capacity) noexcept {
    if(!out || capacity<=0)return 0;
    __try {
        auto* v=static_cast<unsigned char*>(const_cast<void*>(vehicle));
        if(!v || !Readable(v,kSeatCount+8) || v[kDead] || index>=SeatCount(v))return 0;
        const auto seat=SeatAt(v,index);
        if(SeatRider(seat)!=Rider::player || At<const void*>(seat,kSeatRider)!=PlayerHuman())return 0;
        PayloadReadout r{};unsigned char* ws[kMostPayload]{};
        r.count=ReadSeat(v,index,ClassOf(v),ws,r);
        const bool current=sightPick.vehicle.Is(v) && sightPick.human.Is(PlayerHuman()) && sightPick.seat==index;
        const Pick* pick=nullptr;
        // Selection ownership lasts until identity/seat/weapon changes, not until a HUD timer expires.
        // Every returned weapon still comes from the live ReadSeat set below. Fire redirects retain
        // their separate kFreshMs guard in PullHook; a slow/pause frame never grants a stale shot.
        if(current)for(const auto& p:picks)if(!p.npc && p.ref.Is(v) && p.seat==index){pick=&p;break;}
        const PayloadFire want=current ? sightPick.control : PayloadFire::primary;
        const auto usable=[&](int i) -> bool {return !Spent(ws[i]);}; // reloading guns retain their sight; permanently spent ones do not
        const auto redirected=[&](unsigned char* w) -> unsigned char* {
            if(pick && Cfg().enabled && Cfg().stockStores)for(int k=0;k<pick->redirects;++k)if(pick->from[k]==w) {
                for(int j=0;j<r.count;++j)if(ws[j]==pick->weapon && usable(j))return ws[j];
                return static_cast<unsigned char*>(nullptr);
            }
            return Spent(w) ? nullptr : w;
        };
        PayloadFire group=want==PayloadFire::store ? PayloadFire::secondary : want;
        if(want==PayloadFire::store && pick && pick->weapon && Cfg().enabled && Cfg().stockStores)
            for(int i=0;i<r.count;++i)if(ws[i]==pick->weapon && usable(i)) {
                // A stock choice represents its whole native trigger group (e.g. both 603 guns).
                group=r.entry[i].fire;
                if(group==PayloadFire::store) {
                    // Stores ride the native secondary if present, otherwise the primary (Switch).
                    // Enumerating that group also requires a live source holder before returning a store.
                    group=PayloadFire::primary;
                    for(int j=0;j<r.count;++j)if(r.entry[j].fire==PayloadFire::secondary)group=PayloadFire::secondary;
                }
                break;
            }
        const auto collect=[&](PayloadFire fire) -> int {
            int count=0;
            for(int i=0;i<r.count && count<capacity;++i)if(r.entry[i].fire==fire) {
                // Resolve first: an empty native gun can still pull the selected, loaded store.
                auto* w=redirected(ws[i]);
                if(!w)continue;
                bool duplicate=false;
                for(int j=0;j<count;++j)if(out[j]==w)duplicate=true;
                if(!duplicate)out[count++]=w;
            }
            return count;
        };
        if(const int count=collect(group))return count;
        // Missing/expired/spent secondary falls back to a real usable primary, never to an old weapon pointer.
        if(group!=PayloadFire::primary)if(const int count=collect(PayloadFire::primary))return count;
        // Unknown controls have no proven cofire relationship: keep just their existing optic owner.
        for(int i=0;i<r.count;++i)if(r.entry[i].fire==PayloadFire::other && usable(i)){out[0]=ws[i];return 1;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

bool InstallPayload() noexcept {
    __try {
        if(!Matches(kPull,kPullCode,sizeof(kPullCode))) {
            Log("HOOK payload=0 (unexpected EDF.dll code at the holder pull: the stock vehicles' stores do not switch)");
            return false;
        }
        unsigned char jump[14]={0xFF,0x25,0,0,0,0};
        const auto to=reinterpret_cast<std::uintptr_t>(&PullHook);
        std::memcpy(jump+6,&to,8);
        pullOk=edf::PatchCode(image+kPull,kPullCode,jump,sizeof(jump));
        Log("HOOK payload=%d (the holder pull lands on the store picked)",pullOk);
        return pullOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void ResetPayload() noexcept {
    ClearChoiceContext();
    ClearRedirect(nullptr);
    for(auto& p:picks)p=Pick{};
    latest=PayloadReadout{};latestMs=0;
    sightPick=SightPick{};
}
}  // namespace crew
