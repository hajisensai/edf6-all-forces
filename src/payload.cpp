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
#include "twin_cadence.h"
#include "retired_loadout.h"
#include "edf/weapon.h"
#include "online_authority.h"
#include "seat_net_transport.h"
#include "plugin_net.h"
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
constexpr float kCoaxNpcM=900.0f;                 // an NPC crew's coaxial gun: targets this close (NpcCoaxFire; a 7.62 mm
                                                  // coax's point-target reach, where its tracer burns out)
constexpr float kCoaxCone=0.05f;                  // ...within this of its line (rad, ~3 degrees)
// A coax belt is linked four ball to one tracer (CoaxBelt). The round's colour is the weapon's AmmoColor, four floats
// at +0x8D0 in the round template (+0x830) fire copies into each round it makes (docs/boarding-re.md §2: 0x68D978
// writes it there, 0x69712F copies the block; only the drawing reads the round's copy). Keep these equal to
// pylib/vcobjects.py COAX_TRACER / COAX_BALL / COAX_TRACER_EVERY (tests/test_stock_store_loadouts.py).
constexpr std::size_t kWeaponAmmoColor=0x8D0;
constexpr int kCoaxTracerEvery=5;
constexpr float kCoaxTracer[4]={3.0f,1.0f,0.35f,1.0f};   // red-orange (NATO M62 / M276); the weapon file's own colour
constexpr float kCoaxBall[4]={0.3f,0.28f,0.26f,0.1f};    // ball: a faint streak (the game draws every round)
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
// `remote`: another machine's choice for a seat it runs (RemotePick), applied here so this copy's pulls land on it too.
// `secondary`: the intrinsic weapon the seat's own secondary fires (IndependentSecondary: the BMPT's Ataka or 9M120F),
// chosen apart from `weapon` (the primary's belt): the two controls fire at once, each its own pick.
struct Pick { ObjRef ref; unsigned seat; unsigned char* weapon; unsigned char* secondary; bool held,ownHeld,listed,npc,nativeControl,remote; ULONGLONG seen; unsigned char* from[kMostPayload]; unsigned char* to[kMostPayload]; int redirects; };
Pick picks[kTracked]{};
// Online (seat_net.h Pick): the pick of a seat another machine runs (its player's, or the NPC gunner of the machine that
// runs it), as its last message said: the weapon by its holder in the vehicle's holder array. And what this machine last
// sent for a seat it runs (sent again on a change, else every kPickRefreshMs).
struct RemotePick { ObjRef ref; unsigned seat; std::int32_t holder; bool npc; ULONGLONG at; };
RemotePick remotes[kTracked]{};
// Each seat's sender watermarks, apart from the records (seat_net.h StreamGates): a record dropped when stale or taken
// for another seat keeps its seat's gate, so a delayed older pick is never admitted again. Dropped with the vehicle,
// the room's members (plugin_net::RoomGeneration) and the mission (ResetPayload).
seat_net::StreamGates<kTracked*2,64> pickGates;
std::uint32_t pickGatesRoom=0;
struct SentPick { ObjRef ref; unsigned seat; std::int32_t holder; ULONGLONG at; };
SentPick sentPicks[kTracked]{};
bool pickNetOk=false;
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
    for(auto& p:picks)if(p.ref.Is(v) && !p.npc && !p.remote && p.seat<SeatCount(static_cast<const unsigned char*>(v)) &&
        SeatRider(SeatAt(static_cast<unsigned char*>(const_cast<void*>(v)),p.seat))==Rider::player &&
        At<const void*>(SeatAt(static_cast<unsigned char*>(const_cast<void*>(v)),p.seat),kSeatRider)==PlayerHuman())return &p;
    return nullptr;
}

// Whether the virtual key `vk` is down while the game has the foreground (0: never).
bool KeyDown(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (edf::AsyncKey(vk)&0x8000)!=0;
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

// A coaxial machine gun (tools/make_stock_stores.py COAX; the user, 2026-10-11: 「应该有同轴机枪的坦克增加同轴机枪」).
bool IsCoax(const unsigned char* w) noexcept {
    const StoreSpec* const spec=StoreOf(w);
    return spec && spec->role==StoreRole::coax;
}
// The coax's next round as its belt has it (kCoaxTracerEvery): a tracer when the rounds left are a multiple of it,
// else ball. Written before every pull (the player's and the NPC crew's): the weapon fires at most one round a frame
// (FireInterval 4-5), each with the colour of the count it leaves from, so the belt's rhythm holds whichever comes
// first in a frame, this write or the weapon's update. Only on the EDF.dll whose AmmoColor write and fire copy were
// read (coaxBeltOk, InstallPayload); otherwise every round is the file's tracer, as before.
bool coaxBeltOk=false;
void CoaxBelt(unsigned char* w) noexcept {
    if(!coaxBeltOk || !Readable(w,kWeaponAmmoColor+sizeof(kCoaxTracer)))return;
    const std::int32_t left=At<std::int32_t>(w,kWeaponAmmo);
    std::memcpy(w+kWeaponAmmoColor,left%kCoaxTracerEvery==0 ? kCoaxTracer : kCoaxBall,sizeof(kCoaxTracer));
}
constexpr unsigned kAmmoColorInit=0x68D978,kFireCopy=0x69712F;   // the same bytes boarding.cpp checks
const unsigned char kAmmoColorInitCode[]={0x0F,0x28,0x45,0x50,0x0F,0x11,0x86,0xD0,0x08,0x00,0x00};   // movups [rsi+0x8D0]
const unsigned char kFireCopyCode[]={0x49,0x8D,0x97,0x30,0x08,0x00,0x00};                           // lea rdx,[r15+0x830]

// The main gun's firing motion stays the main gun's (the user, 2026-10-12: 「同轴机枪开火的时候主炮会伸缩」;
// docs/feedback-2026-10-12-coax-recoil.md). VehicleBase slot 5 0x630250 calls the class's slot 48 for each holder whose
// gun fired this frame. The Blacker 505's (0x61AEA0) and the Varius 601's (0x620960) slot 48 first run the VehicleBase
// part 0x62EA10 (the camera kick from the gun's recoil strength +0xBD8), then play the class's firing motion
// 0x116BFC0(veh+0x1170, [veh+0x2AC0], [veh+0x2AC8], 1) at 0x61AEE6 / 0x6209A6 -- the motion that runs 'cannon_slide',
// the main gun's recoil travel -- without looking at which holder fired, then push the hull by the mount's BodyRecoil
// (0x5F9FB0). The coax is one more holder (tools/make_stock_stores.py COAX), so every coax round ran the main gun back
// and out. For a coax holder only the VehicleBase part runs: the hull push it skips is the coax mount's BodyRecoil
// [0, 0] (make_stock_stores._params), nothing. The Grape's slot 48 (Car 0x65A740) plays no motion: left alone.
// Installed after gunnerrecoil.cpp (plugin.cpp), which checks these slots are stock and calls them through the vtable.
using FiredFn=void(__fastcall*)(unsigned char*,unsigned char*);
constexpr std::size_t kSlotFired=48;
constexpr unsigned kVehicleFired=0x62EA10,kPlayMotion=0x116BFC0;
struct MotionClass { unsigned vtable,fired,motionCall; const char* name; };
constexpr MotionClass kMotionClasses[]={{0x17DADB0,0x61AEA0,0x61AEE6,"505_Tank"},{0x17DC250,0x620960,0x6209A6,"601_Tank"}};
constexpr int kMotionClassCount=static_cast<int>(sizeof(kMotionClasses)/sizeof(kMotionClasses[0]));
constexpr unsigned kFiredVehicleCallAt=0x10;   // fired+0x10: call 0x62EA10 (both classes)
void* firedNext[kMotionClassCount]{};

bool CoaxHolder(const unsigned char* holder) noexcept {
    __try {
        const unsigned char* const w=holder ? At<const unsigned char*>(holder,kHolderWeapon) : nullptr;
        return w && IsCoax(w);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

template<int I> void __fastcall CoaxAwareFired(unsigned char* v,unsigned char* holder) {
    if(CoaxHolder(holder))reinterpret_cast<FiredFn>(image+kVehicleFired)(v,holder);
    else reinterpret_cast<FiredFn>(firedNext[I])(v,holder);
}
constexpr FiredFn kCoaxAwareFired[]={&CoaxAwareFired<0>,&CoaxAwareFired<1>};
static_assert(sizeof(kCoaxAwareFired)/sizeof(kCoaxAwareFired[0])==kMotionClassCount,"one hook per class");

// Whether a rel32 call at `at` lands on `target` (RVAs). Under __try (the image may not be EDF.dll).
bool CallsTo(unsigned at,unsigned target) noexcept {
    if(image[at]!=0xE8)return false;
    std::int32_t rel=0;
    std::memcpy(&rel,image+at+1,sizeof(rel));
    return static_cast<std::int64_t>(at)+5+rel==static_cast<std::int64_t>(target);
}

int InstallCoaxMotion() noexcept {
    int hooked=0;
    for(int i=0;i<kMotionClassCount;++i) {
        const MotionClass& c=kMotionClasses[i];
        bool read=false;
        __try {
            read=reinterpret_cast<void**>(image+c.vtable)[kSlotFired]==image+c.fired &&
                 CallsTo(c.fired+kFiredVehicleCallAt,kVehicleFired) && CallsTo(c.motionCall,kPlayMotion);
        } __except(EXCEPTION_EXECUTE_HANDLER) { read=false; }
        if(!read){Log("COAX MOTION %s: slot 48 is not the one read (the coax still runs the main gun's motion)",c.name);continue;}
        if(!edf::ChainVtableSlot(reinterpret_cast<void**>(image+c.vtable)+kSlotFired,reinterpret_cast<void*>(kCoaxAwareFired[i]),
                                 &firedNext[i])) {
            Log("COAX MOTION %s: slot 48 patch failed",c.name);
            continue;
        }
        ++hooked;
    }
    return hooked;
}

// Whether class `c`'s stock input leaves the seat's second fire control unread (the 505, the 601, the Grape: their
// slot 55 pulls holder 0 on the primary trigger alone, docs/stock-payload-re.md §2), so a coaxial gun can have it.
constexpr bool FreeSecondary(Class c) noexcept { return c==Class::other; }
// V603 consumes only its primary trigger; the authored BMPT explicitly equips its spare secondary.
// A catalogue flag cannot claim an unread input on another class or replace a native secondary weapon.
bool IndependentSecondary(Class c,const unsigned char* w) noexcept {
    const auto spec=StoreOf(w);
    return c==Class::flak603 && spec && spec->independentSecondary;
}
bool HasIndependentSecondary(Class c,unsigned char* const* ws,const PayloadReadout& r) noexcept {
    for(int i=0;i<r.count;++i)if(IndependentSecondary(c,ws[i]))return true;
    return false;
}
bool SwitchSecondary(Class c,const unsigned char* w,const PayloadEntry& entry) noexcept {
    return entry.fire==PayloadFire::secondary && !entry.coax && !IndependentSecondary(c,w);
}

// `weapons`: the seat's stock weapons (its stores left out). `playerSeat`: the readout of a player's seat (an NPC's
// selection treats a coaxial gun as one more store, fired through the redirected primary pull: NpcPayloadSelect).
PayloadFire FireOf(Class c,unsigned seat,std::int64_t holder,const unsigned char* w,int weapons,bool playerSeat=true) noexcept {
    const std::int64_t s=static_cast<std::int64_t>(seat);
    // EDF6VC_ also names native drill/Katyusha/Proteus weapons. Only catalogued add-on stores are redirectable.
    const auto spec=StoreOf(w);
    if(IndependentSecondary(c,w))return PayloadFire::secondary;
    if(IsStoreWeapon(w) && !(spec && spec->nativeControl))
        return playerSeat && FreeSecondary(c) && IsCoax(w) ? PayloadFire::secondary : PayloadFire::store;
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
int ReadSeat(unsigned char* v,unsigned seat,Class c,unsigned char** ws,PayloadReadout& r,bool playerSeat=true) noexcept {
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
    for(int i=0;i<count;++i) {
        const auto spec=StoreOf(ws[i]);
        stock+=IsStoreWeapon(ws[i]) && !(spec && spec->nativeControl) ? 0 : 1;
    }
    for(int i=0;i<count;++i) {
        PayloadEntry& e=r.entry[i];
        e=PayloadEntry{};
        CopyName(ws[i],e.name,_countof(e.name));
        const std::int32_t rounds=At<std::int32_t>(ws[i],kWeaponAmmo),cap=At<std::int32_t>(ws[i],kWeaponCapacity);
        e.rounds=rounds>0 ? rounds : 0;
        e.capacity=cap>0 ? cap : 0;
        Reload(ws[i],rounds,&e.ready,&e.reloadSec);
        e.fire=FireOf(c,seat,index[i],ws[i],stock,playerSeat);
        e.coax=IsCoax(ws[i]);
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
// A seat another machine runs: no player of this machine in it and no NPC gunner this machine runs; a player's pick
// needs that machine's player there (another machine's player copied here, IsAnyPlayer).
bool RemoteSeat(const unsigned char* v,const unsigned char* seat,bool npc) noexcept {
    if(SeatRider(seat)==Rider::player || AiGunner(v,seat))return false;
    return npc || IsAnyPlayer(At<const unsigned char*>(seat,kSeatRider));
}
void __fastcall PullHook(unsigned char* holder) {
    const auto ctrl=At<const unsigned char*>(holder,kHolderCtrl);
    if(!ctrl || At<std::int32_t>(ctrl,8)==0)return;
    unsigned char* w=At<unsigned char*>(holder,kHolderWeapon);
    const ULONGLONG ms=GameMs();
    __try {
    if(Cfg().enabled)for(const auto& p:picks) {
        if(!Cfg().stockStores && !p.nativeControl)continue;
        if(!p.redirects || ms-p.seen>kFreshMs || !Readable(p.ref.obj,kSelfCtrl+8) || !p.ref.Is(p.ref.obj))continue;
        const auto v=static_cast<unsigned char*>(const_cast<void*>(p.ref.obj));
        if(p.seat>=SeatCount(v) || v[kDead])continue;
        const auto seat=SeatAt(v,p.seat);
        // The seat's pull is this pick's: this machine's player, its NPC gunner, or (a remote pick) the rider another
        // machine runs there, its player (IsAnyPlayer) or its NPC, while this machine runs neither.
        if(p.remote ? !RemoteSeat(v,seat,p.npc) : p.npc ? !AiGunner(v,seat) : SeatRider(seat)!=Rider::player)continue;
        const auto list=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
        for(int k=0;k<p.redirects;++k)if(p.from[k]==w) {
            const auto target=p.to[k];
            if(!target)return; // another native source already owns this single-barrel store
            bool installed=false;
            if(count<=16 && Readable(list,count*8))for(std::uint64_t i=0;i<count;++i) {
                if(!Readable(list[i],kHolderWeapon+8))continue;
                const auto targetCtrl=At<const unsigned char*>(list[i],kHolderCtrl);
                if(Readable(targetCtrl,12) && At<std::int32_t>(targetCtrl,8)>0 && At<unsigned char*>(list[i],kHolderWeapon)==target)installed=true;
            }
            if(installed && !Spent(target))w=target;
            else return; // a removed/expired selected barrel never falls through to a stock shot
            break;
        }
    }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if(Readable(w,kWeaponTrigger+1))w[kWeaponTrigger]=1;
}
constexpr unsigned kPull=0x62C000;
const unsigned char kPullCode[]={0x48,0x8B,0x41,0x08,0x48,0x85,0xC0,0x74,0x11,0x83,0x78,0x08,0x00,0x74,0x0B,0x48,0x8B,0x41,0x10,
                                 0xC6,0x80,0x39,0x01,0x00,0x00,0x01,0xC3};
bool pullOk=false;

// Linked stores share one switch choice while retaining their own native muzzle and firing lifecycle.
bool SamePayloadGroup(const unsigned char* a,const unsigned char* b) noexcept {
    if(a==b)return true;
    const auto sa=StoreOf(a),sb=StoreOf(b);
    return sa && sb && sa->group!=0 && sa->group==sb->group;
}
bool GroupSpent(int choice,unsigned char* const* ws,const PayloadReadout& r) noexcept {
    for(int i=0;i<r.count;++i)if(SamePayloadGroup(ws[choice],ws[i]) && !Spent(ws[i]))return false;
    return true;
}
bool NativeAlternates(unsigned char* const* ws,const int* stock,int count) noexcept {
    for(int i=0;i<count;++i) {const auto spec=StoreOf(ws[stock[i]]);if(spec && spec->nativeControl)return true;}
    return false;
}
std::int64_t NativeHolderIndex(unsigned char* v,unsigned seat,const unsigned char* weapon) noexcept {
    const auto s=SeatAt(v,seat);
    const auto list=At<unsigned char* const*>(s,kSeatWeapons);
    const auto count=At<std::uint64_t>(s,kSeatWeaponCount);
    if(count<=16 && Readable(list,count*8))for(std::uint64_t i=0;i<count;++i)
        if(Readable(list[i],kHolderWeapon+8) && At<const unsigned char*>(list[i],kHolderWeapon)==weapon)return HolderIndex(v,list[i]);
    return -1;
}
void RedirectPayload(unsigned char* v,unsigned seat,Pick& p,unsigned char* const* ws,const PayloadReadout& r,
                     const int* stock,int count,int choice) noexcept;

// --- Online: a seat's pick to the vehicle's copies (seat_net.h Pick). The machine that runs the seat sends it; every
// other one lands that seat's pulls on the same weapon (ApplyRemotePicks -> PullHook). ---
// Send the pick of seat `seat` (`weapon`, null none) this machine runs, when it changed or is due again.
void SendPick(unsigned char* v,unsigned seat,const unsigned char* weapon,bool npc) noexcept {
    if(!pickNetOk || !seat_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)))return;
    seat_net::ChainVehicle(v);   // this class hears its copies' picks too
    const ULONGLONG now=GameMs();
    SentPick* slot=nullptr;
    for(auto& x:sentPicks) {
        if(x.ref.Is(v) && x.seat==seat){slot=&x;break;}
        if(!slot && (!x.ref || !x.ref.Is(x.ref.obj) || now-x.at>seat_net::kPickStaleMs*4))slot=&x;
    }
    if(!slot)return;
    if(!slot->ref.Is(v) || slot->seat!=seat)*slot=SentPick{ObjRef::Of(v),seat,-2,0};
    const std::int64_t index=weapon ? NativeHolderIndex(v,seat,weapon) : -1;
    const std::int32_t holder=index>=0 && index<static_cast<std::int64_t>(seat_net::kMostHolders) ? static_cast<std::int32_t>(index) : -1;
    if(!seat_net::PickDue(holder!=slot->holder,now,slot->at))return;
    seat_net::Pick m;
    m.seat=seat;m.holder=holder;m.flags=npc ? seat_net::kNpc : 0;m.head.controller=seat_net::SeatController(v,seat);
    if(!seat_net::Stamp(&m,sizeof(m)) || !seat_net::Valid(m))return;
    if(seat_net::Send(v,&m,sizeof(m))){slot->holder=holder;slot->at=now;}
}
// A pick from another machine (game thread). Admitted for a seat this machine does not run itself, in the control epoch
// this machine sees there (the rider's ReferenceId, -1 an NPC or none), newer than that sender's last.
void PickReceived(unsigned char* v,const void* block,std::size_t size) noexcept {
    seat_net::Pick m;
    if(!pickNetOk || !Cfg().enabled || !InSession() || !seat_net::Decode(block,size,m))return;
    __try {
        if(!Readable(v,kSeatCount+8) || v[kDead] || m.seat>=SeatCount(v))return;
        const unsigned char* const seat=SeatAt(v,m.seat);
        const bool here=SeatRider(seat)==Rider::player || AiGunner(v,seat);
        const ULONGLONG now=GameMs();
        RemotePick* slot=nullptr;
        for(auto& r:remotes) {
            if(r.ref.Is(v) && r.seat==m.seat){slot=&r;break;}
            if(!slot && (!r.ref || !r.ref.Is(r.ref.obj) || now-r.at>seat_net::kPickStaleMs*4))slot=&r;
        }
        if(!slot)return;
        const std::uint32_t room=plugin_net::RoomGeneration();
        if(room!=pickGatesRoom){pickGates.Reset();pickGatesRoom=room;}   // other members: their watermarks with them
        const ObjRef ref=ObjRef::Of(v);
        auto* const gate=pickGates.For({ref.obj,ref.ctrl,m.seat},[](const void* obj,const void* ctrl) noexcept {
            return Readable(obj,kSelfCtrl+8) && ObjRef{obj,ctrl}.Is(obj);
        });
        if(!gate || !gate->Admit(m.head,InSession(),here,seat_net::SeatController(v,m.seat)))return;
        if(!slot->ref.Is(v) || slot->seat!=m.seat){*slot=RemotePick{};slot->ref=ref;slot->seat=m.seat;slot->holder=-1;}
        slot->holder=m.holder;slot->npc=(m.flags&seat_net::kNpc)!=0;slot->at=now;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// The remote picks of `v` as redirects of its seats' picks (each frame: PullHook takes only fresh ones). A seat this
// machine now runs, a pick not refreshed for kPickStaleMs, a session left: the record is dropped.
void ApplyRemotePicks(unsigned char* v) noexcept {
    const ULONGLONG ms=GameMs();
    const bool session=InSession();
    for(auto& r:remotes) {
        if(!r.ref.Is(v))continue;
        if(!session || ms-r.at>seat_net::kPickStaleMs || r.seat>=SeatCount(v)) {
            for(auto& p:picks)if(p.remote && p.ref.Is(v) && p.seat==r.seat){p.redirects=0;p.weapon=nullptr;p.remote=false;}
            r=RemotePick{};continue;
        }
        unsigned char* const seat=SeatAt(v,r.seat);
        if(!RemoteSeat(v,seat,r.npc))continue;
        Pick* const p=PickFor(v,r.seat,ms);
        if(!p)continue;
        p->remote=true;p->npc=r.npc;p->seen=ms;p->redirects=0;p->weapon=nullptr;p->secondary=nullptr;p->nativeControl=false;
        if(r.holder<0)continue;
        PayloadReadout pr{};unsigned char* ws[kMostPayload]{};
        pr.count=ReadSeat(v,r.seat,ClassOf(v),ws,pr,!r.npc);
        // As NpcPayloadSelect: the coax and the intrinsic secondary are never the primary's pick.
        if(r.npc)for(int i=0;i<pr.count;++i)if(pr.entry[i].coax || IndependentSecondary(ClassOf(v),ws[i]))pr.entry[i].fire=PayloadFire::other;
        int choice=-1;
        for(int i=0;i<pr.count;++i)if(NativeHolderIndex(v,r.seat,ws[i])==r.holder){choice=i;break;}
        if(choice<0 || IndependentSecondary(ClassOf(v),ws[choice]))continue;   // a pick only ever names the primary's weapon
        PayloadFire ride=PayloadFire::primary;   // the stores ride the seat's secondary when a stock weapon is on it (Switch)
        for(int i=0;i<pr.count;++i)if(SwitchSecondary(ClassOf(v),ws[i],pr.entry[i]))ride=PayloadFire::secondary;
        int stock[kMostPayload],count=0;
        for(int i=0;i<pr.count;++i)if(pr.entry[i].fire==ride)stock[count++]=i;
        // As Switch / NpcPayloadPick: the stock pulls are redirected only for a store or among native alternates. The
        // intrinsic secondary makes the pick PullHook's to honour (nativeControl), never a redirect of a stock belt: the
        // BMPT's HE is its native pair, and a redirect of it would map the right barrel's pull to no target (a single
        // mount receives one source, RedirectPayload), dropping it (2026-10-24 review: latches L=1 R=0).
        const bool alternates=NativeAlternates(ws,stock,count);
        p->nativeControl=alternates || HasIndependentSecondary(ClassOf(v),ws,pr);
        p->weapon=ws[choice];
        if(pr.entry[choice].fire==PayloadFire::store || alternates)RedirectPayload(v,r.seat,*p,ws,pr,stock,count,choice);
    }
}

void RedirectPayload(unsigned char* v,unsigned seat,Pick& p,unsigned char* const* ws,const PayloadReadout& r,
                     const int* stock,int count,int choice) noexcept {
    const auto spec=StoreOf(ws[choice]);
    for(int k=0;k<count && p.redirects<kMostPayload;++k) {
        unsigned char* target=nullptr;
        if(spec && spec->group) {
            const auto holder=NativeHolderIndex(v,seat,ws[stock[k]]);
            for(int j=0;j<r.count;++j) {
                const auto other=StoreOf(ws[j]);
                if(other && other->group==spec->group && static_cast<std::int64_t>(other->barrel)==holder && !Spent(ws[j])) {
                    target=ws[j];break;
                }
            }
        } else if(k==0)target=ws[choice]; // a single mount receives exactly one native source, never both 603 pulls
        p.from[p.redirects]=ws[stock[k]];p.to[p.redirects]=target;++p.redirects;
    }
}

// The next choice of a cycle (n entries, the current one `at`) with something left in it (`spent(k)`: the k-th has
// nothing left and none coming), else the current one: a press goes round, a spent pick moves on by itself.
template<class SpentFn> int NextChoice(int at,int n,SpentFn spent) noexcept {
    for(int k=1;k<=n;++k) {
        const int next=(at+k)%n;
        if(!spent(next) || k==n)return next;
    }
    return at;
}

// The store switch on the player's seat (see the top). `ws` the seat's weapons, `r` their entries. Two cycles: the
// primary's (its native belts and the stores riding it: `p.weapon`) and the intrinsic secondary's (IndependentSecondary:
// `p.secondary`, the BMPT's two Ataka rounds on the V603's otherwise unread second trigger). Both fire at once, each on
// its own trigger, and each has its own switch, whatever was fired last (as War Thunder's: the gun's rounds on their own
// keys, the ATGM its own fire and selection; the user, 2026-10-21: 「学学战雷」): R / LB (PlayerJetSwitchKey) goes round the
// primary's cycle only, VehicleSecondarySwitchKey (T) / VehicleSecondarySwitchButton (the d-pad right) the own
// secondary's only; a menu choice (ConsumeChoice) goes to the cycle of the entry chosen. `chosen`: the control a press or
// a menu choice changed (`store` the primary's cycle, the sight following its pick; `secondary` the own secondary's),
// `other` none.
void Switch(unsigned char* v,const unsigned char* seat,Pick& p,unsigned char* const* ws,PayloadReadout& r,
            PayloadFire* chosen) noexcept {
    *chosen=PayloadFire::other;
    const Class c=ClassOf(v);
    PayloadFire ride=PayloadFire::primary;   // the coaxial gun has the secondary to itself: the stores stay on the gun
    for(int i=0;i<r.count;++i)if(SwitchSecondary(c,ws[i],r.entry[i]))ride=PayloadFire::secondary;
    int stock[kMostPayload],ns=0,list[kMostPayload],n=0,own[kMostPayload],no=0;
    for(int i=0;i<r.count;++i)if(r.entry[i].fire==ride && !IndependentSecondary(c,ws[i]))stock[ns++]=i;
    for(int i=0;i<r.count;++i)if(IndependentSecondary(c,ws[i]))own[no++]=i;
    const bool alternatives=NativeAlternates(ws,stock,ns);
    p.nativeControl=alternatives || no>0;
    if(alternatives)for(int k=0;k<ns;++k)list[n++]=stock[k];
    else if(ns)list[n++]=stock[0]; // a normal native pair remains one trigger group
    for(int i=0;i<r.count;++i)if(Cfg().stockStores && r.entry[i].fire==PayloadFire::store) {
        bool linked=false;
        for(int k=0;k<n;++k)if(SamePayloadGroup(ws[i],ws[list[k]]))linked=true;
        if(!linked)list[n++]=i;
    }
    const bool enabled=Cfg().enabled && pullOk && (Cfg().stockStores || alternatives) && ns>0 && n>=2;
    // The intrinsic secondary is the vehicle's own: managed with the optional stores off too (StockVehicleStores=0).
    const bool ownEnabled=Cfg().enabled && pullOk && no>=2;
    const auto spent=[&](int k) -> bool {
        if(!alternatives && ns && list[k]==stock[0]) {
            for(int j=0;j<ns;++j)if(!Spent(ws[stock[j]]))return false;
            return true;
        }
        return GroupSpent(list[k],ws,r);
    };
    const auto ownSpent=[&](int k) -> bool {return Spent(ws[own[k]]);};
    if(enabled)for(int k=0;k<n;++k)r.entry[list[k]].selectable=!spent(k);
    if(ownEnabled)for(int k=0;k<no;++k)r.entry[own[k]].selectable=!ownSpent(k);
    PublishChoices(v,ws,r);
    const int requested=ConsumeChoice(r);
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    // During a map hold follow the physical key only to drain its edge; do not replay it on close.
    const int vk=Cfg().playerJetSwitchKey;
    const bool down=keys ? (MapHoldsKeys() ? vk>0 && (edf::AsyncKey(vk)&0x8000)!=0 : KeyDown(vk)) :
        (At<std::uint16_t>(seat,kSeatButtons)&kButtonLB)!=0;
    const bool press=!MapHoldsKeys() && down && !p.held;
    p.held=down;
    // The own secondary's switch: its key on the keyboard, its pad buttons (XInput: the seat button word has no d-pad).
    const int ownVk=Cfg().vehicleSecondarySwitchKey,ownMask=Cfg().vehicleSecondarySwitchButton;
    std::uint16_t padButtons=0;std::uint8_t lt=0,rt=0;
    const bool ownDown=keys ? (MapHoldsKeys() ? ownVk>0 && (edf::AsyncKey(ownVk)&0x8000)!=0 : KeyDown(ownVk)) :
        ownMask>0 && MapPadState(&padButtons,&lt,&rt) && (padButtons&ownMask)==ownMask;
    const bool pressOwn=ownEnabled && !MapHoldsKeys() && ownDown && !p.ownHeld;
    p.ownHeld=ownDown;
    if(!p.listed) {
        p.listed=true;
        for(int k=0;k<n;++k)Log("PAYLOAD v=%p %s %d: %ls (%d rounds)",v,ride==PayloadFire::secondary ? "secondary" : "primary",k,
                                r.entry[list[k]].name,r.entry[list[k]].rounds);
        for(int k=0;k<no;++k)Log("PAYLOAD v=%p own secondary %d: %ls (%d rounds)",v,k,r.entry[own[k]].name,r.entry[own[k]].rounds);
    }
    // --- the intrinsic secondary's cycle ---
    int ownAt=0;
    if(no) {
        for(int k=0;k<no;++k)if(ws[own[k]]==p.secondary)ownAt=k;
        const int was=ownAt;
        int requestedAt=-1;
        for(int k=0;k<no;++k)if(own[k]==requested)requestedAt=k;
        if(ownEnabled && requestedAt>=0){ownAt=requestedAt;*chosen=PayloadFire::secondary;}
        else if(ownEnabled && (pressOwn || ownSpent(ownAt))) {
            ownAt=NextChoice(ownAt,no,ownSpent);
            if(pressOwn) {
                *chosen=PayloadFire::secondary;
                Log("PAYLOAD v=%p secondary picked: %ls (%d rounds)",v,r.entry[own[ownAt]].name,r.entry[own[ownAt]].rounds);
            }
        }
        if(ownAt!=was && p.secondary==ws[own[was]]){ClearWeaponLock(ws[own[was]]);ClearWeaponLock(ws[own[ownAt]]);}
        p.secondary=ws[own[ownAt]];
        r.entry[own[ownAt]].picked=true;
    } else p.secondary=nullptr;
    r.switchButton=kButtonLB;r.ownSwitchButton=ownMask;
    if(ownEnabled){r.ownPicked=own[ownAt];r.ownChoices=no;}
    if(!enabled) {
        p.weapon=nullptr;
        if(p.secondary)Lock(p.secondary);
        if(*chosen==PayloadFire::other && Cfg().enabled && pullOk && Cfg().stockStores && ns>0 && n>=2 && press)*chosen=PayloadFire::store;
        return;
    }
    // --- the primary's cycle ---
    int at=0;
    for(int k=0;k<n;++k)if(ws[list[k]]==p.weapon)at=k;
    if(!p.weapon || ws[list[at]]!=p.weapon)at=0;
    int requestedAt=-1;
    for(int k=0;k<n;++k)if(list[k]==requested)requestedAt=k;
    if(requestedAt>=0) {
        if(at!=requestedAt){ClearWeaponLock(ws[list[at]]);ClearWeaponLock(ws[list[requestedAt]]);}
        at=requestedAt;*chosen=PayloadFire::store;
    } else if(press || spent(at)) {
        const int was=at;
        at=NextChoice(at,n,spent);
        if(at!=was){ClearWeaponLock(ws[list[was]]);ClearWeaponLock(ws[list[at]]);}
        if(press){*chosen=PayloadFire::store;Log("PAYLOAD v=%p picked: %ls (%d rounds)",v,r.entry[list[at]].name,r.entry[list[at]].rounds);}
    }
    p.weapon=ws[list[at]];
    for(int i=0;i<r.count;++i)if(SamePayloadGroup(p.weapon,ws[i]))r.entry[i].picked=true;
    r.picked=list[at];r.choices=n;   // the primary's cycle (R / LB); the own secondary's: ownPicked / ownChoices
    if(alternatives || r.entry[list[at]].fire==PayloadFire::store)RedirectPayload(v,p.seat,p,ws,r,stock,ns,list[at]);
    // One lock tone: a homing store the primary picked, else the own secondary's missile.
    const bool primaryHoming=At<std::int32_t>(p.weapon,kWeaponLockon)==kHoming;
    Lock(p.secondary && !primaryHoming ? p.secondary : p.weapon);
}
}  // namespace

void PayloadFrame(unsigned char* v) noexcept {
    if(!Cfg().enabled){ClearRedirect(v);ClearChoiceContext(v);return;}
    if(v[kDead]){ClearRedirect(v);ClearChoiceContext(v);return;}
    if(BodyOf(v)!=PluginBody::none){ClearRedirect(v);if(!choiceContext.aircraft)ClearChoiceContext(v);return;}
    if(pickNetOk) {
        if(seat_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)))seat_net::ChainVehicle(v);   // this class hears its copies' picks
        ApplyRemotePicks(v);
    }
    const int seat=PlayerSeatOf(v);
    if(seat<0){if(sightPick.vehicle.Is(v))sightPick=SightPick{};ClearChoiceContext(v);return;}
    const ULONGLONG ms=GameMs();
    Pick* const p=PickFor(v,static_cast<unsigned>(seat),ms);
    if(!p)return;
    const bool takeover=p->npc || p->remote;
    p->seen=ms;p->npc=false;p->remote=false;
    const Class c=ClassOf(v);
    PayloadReadout r{};
    unsigned char* ws[kMostPayload]{};
    r.seat=seat;r.seats=static_cast<int>(SeatCount(v));r.picked=-1;r.ownPicked=-1;
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
        if(takeover){p->weapon=nullptr;p->secondary=nullptr;}
    }
    p->redirects=0;
    PayloadFire chosen=PayloadFire::other;
    Switch(v,s,*p,ws,r,&chosen);
    // An explicit choice aims what it chose without firing: a primary-cycle choice the picked payload (`store`), an
    // own-secondary choice the secondary (whose group is that one pick: SeatFireWeapons).
    if(chosen!=PayloadFire::other && MapHoldsKeys())sightPick.control=chosen;
    if(!MapHoldsKeys()) {
        // Same-frame trigger edges prefer the primary. Explicit R/LB selection aims the selected payload without firing.
        if(primary && !sightPick.primaryHeld)sightPick.control=PayloadFire::primary;
        else if(secondary && !sightPick.secondaryHeld)sightPick.control=PayloadFire::secondary;
        else if(chosen!=PayloadFire::other)sightPick.control=chosen;
    }
    sightPick.primaryHeld=primary;sightPick.secondaryHeld=secondary;
    // The coaxial gun fires while the secondary trigger is held (the stock input of these classes never reads it): its
    // trigger latch, as a store's pull sets it (PullHook), each frame held; its own FireInterval paces the burst. The
    // main gun's choice (R / LB) is untouched, so the gunner keeps a round in the breech while he hoses infantry.
    if(Cfg().stockStores && secondary && !MapHoldsKeys())
        for(int i=0;i<r.count;++i)
            if(r.entry[i].coax && r.entry[i].fire==PayloadFire::secondary && !Spent(ws[i]) && Readable(ws[i],kWeaponTrigger+1)) {
                CoaxBelt(ws[i]);ws[i][kWeaponTrigger]=1;
            }
    // The intrinsic secondary (the BMPT's missiles) on its own trigger, the primary's guns untouched: optional add-on
    // stores off or not, it never redirects a cannon source. Only the one picked (Switch: p->secondary) is pulled.
    if(p->secondary)FireIndependentSecondary(v,static_cast<unsigned>(seat),p->secondary);
    latest=r;latestMs=ms;PublishSelectableSnapshot(r);
    if(r.choices>=2 || p->weapon)SendPick(v,static_cast<unsigned>(seat),p->weapon,false);   // this seat's pick to its copies
}

namespace {
bool NpcWeaponCandidate(const unsigned char* w,float distance,bool airborne,float* rank) noexcept {
    if(!(At<float>(w,0x89C)>0.0f))return false; // healers never target enemies
    float muzzle[3],dir[3];RoundModel m{};
    if(!edf::MeanMuzzle(w,64,muzzle,dir) || !ReadRound(w,&m) || m.kind==RoundKind::none)return false;
    // Native NPC engagement reach. Guided stores also need a reachable lock; an
    // accelerating rocket must not be limited to its initial speed times lifetime.
    float reach=At<float>(w,0x224);
    if(!std::isfinite(reach) || reach<=0.0f)return false;
    if(m.kind==RoundKind::homing) {
        const float lockReach=At<float>(w,0x6D0);
        if(!std::isfinite(lockReach) || lockReach<=0.0f)return false;
        reach=std::fmin(reach,lockReach);
    } else if(m.kind==RoundKind::arc) {
        if(!std::isfinite(m.speed) || m.speed<=0.0f || m.alive<=0)return false;
        reach=std::fmin(reach,m.speed*static_cast<float>(m.alive));
    }
    if(!std::isfinite(reach) || reach<distance)return false;
    // No splash dead zone: a vehicle round's blast hurts only what is hostile to its team (the IndirectFireUnit's
    // +0xD0, docs/carrier-laser-re.md; online_authority.h SparesRide), so the vehicle and its side are never in it.
    // A 2x-blast veto kept the Titan's main cannon (30 m) silent against everything inside 60 m (2026-10-10).
    const float blast=At<float>(w,0x8B0);
    if(!std::isfinite(blast))return false;
    const StoreSpec* const spec=StoreOf(w);
    const edf::GunRole role=edf::RoleOf(At<std::int32_t>(w,edf::kWeaponMark));   // EDF6AutoTurret's table too
    if(spec && ((airborne && (spec->role==StoreRole::ground || spec->role==StoreRole::bomb || spec->role==StoreRole::rocket)) ||
                (!airborne && spec->role==StoreRole::air)))return false;
    // A catalogued gun round (StoreRole::gun, vcobjects.Shell) is a direct-fire round whatever class carries it: the
    // flak's proximity HE flies the stock grenade class (rounds.cpp calls it lobbed) and is meant for aircraft.
    const bool lobbed=m.lobbed && !(spec && spec->role==StoreRole::gun);
    if(airborne && lobbed)return false;   // a lobbed round cannot meet an aircraft
    // Guided fire at long range, direct fire close up, splash against ground targets.
    *rank=m.kind==RoundKind::homing ? (distance>150.0f ? 4.0f : 2.0f) : airborne ? 3.0f : blast>0.0f ? 3.5f : 2.5f;
    // A gun's preferred side (EDF6AutoTurret's role table) ranks it, never forbids it: the Kepler's flak guns are its
    // only guns, and a veto against ground targets kept its NPC driver from ever firing (2026-10-10).
    if((airborne && role.prefer==edf::Prefer::ground) || (!airborne && role.prefer==edf::Prefer::air))*rank-=2.0f;
    return true;
}
} // namespace

// Select only loaded weapons already attached to this seat. The native caller owns
// aiming/firing; no shot or weapon object is fabricated here.
namespace {
unsigned char* NpcPayloadPick(unsigned char* v,unsigned seat,float distance,bool airborne,PayloadFire* fire) noexcept {
    if(fire)*fire=PayloadFire::other;
    if(!pullOk || !Cfg().enabled || !Cfg().npcGunners || !v || v[kDead] ||
       BodyOf(v)!=PluginBody::none || seat>=SeatCount(v))return nullptr;
    const auto s=SeatAt(v,seat);
    if(!AiGunner(v,s))return nullptr;
    Pick* const p=PickFor(v,seat,GameMs());
    if(!p)return nullptr;
    const auto previous=p->remote ? nullptr : p->weapon;
    p->seen=GameMs();p->npc=true;p->remote=false;p->nativeControl=NativePayloadControl(v,seat);p->redirects=0;p->weapon=nullptr;p->secondary=nullptr;
    if(!std::isfinite(distance) || distance<=0.0f)return nullptr;
    PayloadReadout r{};unsigned char* ws[kMostPayload]{};
    r.count=ReadSeat(v,seat,ClassOf(v),ws,r,false);
    if(!Cfg().stockStores) {
        bool native=false;
        for(int i=0;i<r.count;++i) {const auto spec=StoreOf(ws[i]);native=native || (spec && spec->nativeControl);}
        if(!native && !HasIndependentSecondary(ClassOf(v),ws,r))return nullptr;
    }
    // Intrinsic independent weapons are planned alongside the main gun, never selected in its place.
    for(int i=0;i<r.count;++i)if(r.entry[i].coax || IndependentSecondary(ClassOf(v),ws[i]))r.entry[i].fire=PayloadFire::other;
    PayloadFire ride=PayloadFire::primary;
    for(int i=0;i<r.count;++i)if(r.entry[i].fire==PayloadFire::secondary)ride=PayloadFire::secondary;
    int best=-1;float score=-1.0f;
    for(int i=0;i<r.count;++i) {
        if(r.entry[i].fire==PayloadFire::other || r.entry[i].rounds<=0 || (!Cfg().stockStores && r.entry[i].fire==PayloadFire::store))continue;
        float rank=0.0f;
        if(!NpcWeaponCandidate(ws[i],distance,airborne,&rank))continue;
        if(ws[i]==previous)rank+=0.1f;
        if(rank>score){score=rank;best=i;}
    }
    if(best<0)return nullptr;
    p->weapon=ws[best];
    int stock[kMostPayload],count=0;
    for(int i=0;i<r.count;++i)if(r.entry[i].fire==ride)stock[count++]=i;
    p->nativeControl=NativeAlternates(ws,stock,count) || HasIndependentSecondary(ClassOf(v),ws,r);
    if(r.entry[best].fire==PayloadFire::store || NativeAlternates(ws,stock,count))
        RedirectPayload(v,seat,*p,ws,r,stock,count,best);
    if(r.entry[best].fire==PayloadFire::store && !p->redirects){p->weapon=nullptr;return nullptr;}
    if(previous && previous!=p->weapon && Readable(previous,kWeaponCharge+4))ClearWeaponLock(previous);
    if(fire)*fire=r.entry[best].fire==PayloadFire::store ? ride : r.entry[best].fire;
    return p->weapon;
}
}  // namespace

// The NPC gunner's pick runs on the machine that runs the gunner (AiGunner: the host's NPC, IsOnlineAuthority); that
// machine sends it to the vehicle's copies (SendPick), so their pulls land on the same weapon.
unsigned char* NpcPayloadSelect(unsigned char* v,unsigned seat,float distance,bool airborne,PayloadFire* fire) noexcept {
    unsigned char* const picked=NpcPayloadPick(v,seat,distance,airborne,fire);
    __try {
        if(pickNetOk && v && !v[kDead] && seat<SeatCount(v) && AiGunner(v,SeatAt(v,seat)))SendPick(v,seat,picked,true);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return picked;
}

unsigned char* NpcSecondarySelect(unsigned char* v,unsigned seat,float distance,bool airborne) noexcept {
    if(!pullOk || !Cfg().enabled || !Cfg().npcGunners || !v || v[kDead] || BodyOf(v)!=PluginBody::none ||
       seat>=SeatCount(v) || !AiGunner(v,SeatAt(v,seat)) || !std::isfinite(distance) || distance<=0.0f)return nullptr;
    PayloadReadout r{};unsigned char* ws[kMostPayload]{};
    const Class c=ClassOf(v);r.count=ReadSeat(v,seat,c,ws,r,false);
    Pick* const p=PickFor(v,seat,GameMs());
    const unsigned char* const previous=p && p->npc && !p->remote ? p->secondary : nullptr;
    unsigned char* best=nullptr;float score=-1.0f;
    for(int i=0;i<r.count;++i) {
        if(!IndependentSecondary(c,ws[i]) || r.entry[i].rounds<=0)continue;
        float rank=0.0f;
        if(!NpcWeaponCandidate(ws[i],distance,airborne,&rank))continue;
        if(ws[i]==previous)rank+=0.1f;   // stay on the round already locking
        if(rank>score){best=ws[i];score=rank;}
    }
    if(p && p->npc && !p->remote){p->seen=GameMs();p->secondary=best;}
    return best;
}

bool FireIndependentSecondary(unsigned char* v,unsigned seat,const unsigned char* expectedWeapon) noexcept {
    __try {
        if(!pullOk || !Cfg().enabled || !v || v[kDead] || BodyOf(v)!=PluginBody::none || seat>=SeatCount(v))return false;
        const auto s=SeatAt(v,seat);
        const bool local=SeatRider(s)==Rider::player && At<const void*>(s,kSeatRider)==PlayerHuman();
        if((local && MapHoldsKeys()) || (!local && !AiGunner(v,s)) || !(At<float>(s,0x2E0)>=0.8f))return false;
        PayloadReadout r{};unsigned char* ws[kMostPayload]{};
        const Class c=ClassOf(v);r.count=ReadSeat(v,seat,c,ws,r,local);
        bool fired=false;
        for(int i=0;i<r.count;++i)if((!expectedWeapon || ws[i]==expectedWeapon) && IndependentSecondary(c,ws[i]) && r.entry[i].rounds>0 && Readable(ws[i],kWeaponTrigger+1,true)) {
            ws[i][kWeaponTrigger]=1;fired=true;
        }
        return fired;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool NpcCoaxFire(unsigned char* v,unsigned seat,const void* target,LineBlocked blocked) noexcept {
    __try {
        if(!Cfg().enabled || !Cfg().stockStores || !Cfg().npcGunners || !v || v[kDead] || !target || BodyOf(v)!=PluginBody::none ||
           seat>=SeatCount(v) || !AiGunner(v,SeatAt(v,seat)) || !Readable(target,kPosition+12))return false;
        PayloadReadout r{};unsigned char* ws[kMostPayload]{};
        r.count=ReadSeat(v,seat,ClassOf(v),ws,r,false);
        const float* p=reinterpret_cast<const float*>(static_cast<const unsigned char*>(target)+kPosition);
        const float at[3]={p[0],p[1]+0.5f,p[2]};
        bool fired=false;
        for(int i=0;i<r.count;++i) {
            if(!r.entry[i].coax || Spent(ws[i]) || r.entry[i].rounds<=0)continue;
            float muzzle[3],dir[3];
            if(!edf::MeanMuzzle(ws[i],64,muzzle,dir))continue;
            float to[3]={at[0]-muzzle[0],at[1]-muzzle[1],at[2]-muzzle[2]};
            const float d=std::sqrt(to[0]*to[0]+to[1]*to[1]+to[2]*to[2]);
            float reach=At<float>(ws[i],0x224);
            if(!std::isfinite(reach) || reach<=0.0f || reach>kCoaxNpcM)reach=kCoaxNpcM;
            if(!(d>1.0f) || d>reach)continue;
            const float c=(to[0]*dir[0]+to[1]*dir[1]+to[2]*dir[2])/d;
            if(c<std::cos(kCoaxCone) || (blocked && blocked(muzzle,at,v)))continue;   // not laid on it yet, or a friend in the way
            if(Readable(ws[i],kWeaponTrigger+1)){CoaxBelt(ws[i]);ws[i][kWeaponTrigger]=1;fired=true;}
        }
        return fired;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// --- Twin barrels in turn (twin_cadence.h). The weapon fields the cadence reads, as the shot and the weapon update use
// them: +0x139 the trigger latch (0x6935B1 reads and clears it, after every vehicle's input: the pull 0x62C000 sets it,
// so do the coax and the own secondary above), +0x36C FireInterval and +0xE0C the cooldown it sets at a shot
// (0x6981E0 / 0x6981F7), counted down by +0xE10 to FLT_EPSILON by the update (0x693A58..0x693A95). twinOk: those bytes.
constexpr std::size_t kWeaponInterval=0x36C,kWeaponCooldown=0xE0C,kWeaponCooldownStep=0xE10;
constexpr unsigned kShotIntervalAt=0x6981E0,kShotCooldownAt=0x6981F7,kCooldownStepAt=0x693A58,kLatchReadAt=0x6935B1;
const unsigned char kShotIntervalCode[]={0x41,0x8B,0x87,0x6C,0x03,0x00,0x00};             // mov eax,[r15+0x36C]
const unsigned char kShotCooldownCode[]={0xF3,0x41,0x0F,0x11,0x87,0x0C,0x0E,0x00,0x00};   // movss [r15+0xE0C],xmm0
// lea r15,[rsi+0xE0C]; movss xmm0,[r15]; subss xmm0,[rsi+0xE10]
const unsigned char kCooldownStepCode[]={0x4C,0x8D,0xBE,0x0C,0x0E,0x00,0x00,0xF3,0x41,0x0F,0x10,0x07,0xF3,0x0F,0x5C,0x86,0x10,0x0E,0x00,0x00};
const unsigned char kLatchReadCode[]={0x44,0x88,0xAE,0x39,0x01,0x00,0x00};               // mov [rsi+0x139],r13b
bool twinOk=false;

void TwinBarrelFrame(unsigned char* v) noexcept {
    if(!twinOk || !Cfg().enabled)return;
    __try {
        if(!v || v[kDead] || BodyOf(v)!=PluginBody::none)return;
        const auto holders=At<unsigned char*>(v,kHolders);
        const auto count=At<std::uint64_t>(v,kHolderCount);
        if(!holders || !count || count>16 || !Readable(holders,count*kHolderStride))return;
        twin::Barrel b[16]{};unsigned char* w[16]{};int n=0;
        for(std::uint64_t i=0;i<count;++i) {
            const unsigned char* const h=holders+i*kHolderStride;
            const auto ctrl=At<const unsigned char*>(h,kHolderCtrl);
            if(!Readable(ctrl,12) || At<std::int32_t>(ctrl,8)<=0)continue;
            unsigned char* const weapon=At<unsigned char*>(h,kHolderWeapon);
            if(!Readable(weapon,kWeaponCooldownStep+4,true))continue;
            const unsigned pair=TwinPairOf(weapon);
            if(!pair)continue;
            b[n]={pair,static_cast<int>(i),weapon[kWeaponTrigger]!=0,At<float>(weapon,kWeaponCooldown),
                  At<float>(weapon,kWeaponCooldownStep),At<std::int32_t>(weapon,kWeaponInterval),At<std::int32_t>(weapon,kWeaponAmmo)};
            w[n++]=weapon;
        }
        const std::uint32_t held=twin::Withheld(b,n);
        for(int i=0;i<n;++i)if((held>>i)&1u)w[i][kWeaponTrigger]=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
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
    PayloadReadout r{};r.seat=0;r.seats=static_cast<int>(SeatCount(v));r.count=count;r.picked=picked;r.ownPicked=-1;
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
unsigned char* PayloadOwnSecondaryPicked(const void* vehicle) noexcept {
    const Pick* const p=FindPick(vehicle);
    return p && p->secondary && GameMs()-p->seen<=kFreshMs ? p->secondary : nullptr;
}

unsigned char* PayloadSightPicked(const void* vehicle,unsigned index) noexcept {
    unsigned char* weapon=nullptr;
    return PayloadSightWeapons(vehicle,index,&weapon,1)>0 ? weapon : nullptr;
}

namespace {
// PayloadSightWeapons (`both` false) and PayloadTriggerWeapons (`both` true: the primary's group, then the secondary's).
int SeatFireWeapons(const void* vehicle,unsigned index,bool both,unsigned char** out,int capacity) noexcept {
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
        if(current)for(const auto& p:picks)if(!p.npc && !p.remote && p.ref.Is(v) && p.seat==index){pick=&p;break;}
        const PayloadFire want=current ? sightPick.control : PayloadFire::primary;
        const auto usable=[&](int i) -> bool {return !Spent(ws[i]);}; // reloading guns retain their sight; permanently spent ones do not
        const auto redirected=[&](unsigned char* w) -> unsigned char* {
            if(pick && Cfg().enabled && (Cfg().stockStores || pick->nativeControl))for(int k=0;k<pick->redirects;++k)if(pick->from[k]==w) {
                for(int j=0;j<r.count;++j)if(ws[j]==pick->to[k] && usable(j))return ws[j];
                return static_cast<unsigned char*>(nullptr);
            }
            return Spent(w) ? nullptr : w;
        };
        PayloadFire group=want==PayloadFire::store ? PayloadFire::secondary : want;
        if(want==PayloadFire::store && pick && pick->weapon && Cfg().enabled && (Cfg().stockStores || pick->nativeControl))
            for(int i=0;i<r.count;++i)if(ws[i]==pick->weapon && usable(i)) {
                // A stock choice represents its whole native trigger group (e.g. both 603 guns).
                group=r.entry[i].fire;
                if(group==PayloadFire::store) {
                    // Stores ride the native secondary if present, otherwise the primary (Switch).
                    // Enumerating that group also requires a live source holder before returning a store.
                    group=PayloadFire::primary;
                    for(int j=0;j<r.count;++j)if(SwitchSecondary(ClassOf(v),ws[j],r.entry[j]))group=PayloadFire::secondary;
                }
                break;
            }
        // The own secondary's group is the one weapon its trigger pulls: the pick (Switch), else the first left.
        const unsigned char* own=pick && pick->secondary ? pick->secondary : nullptr;
        if(!own)for(int i=0;i<r.count && !own;++i)if(IndependentSecondary(ClassOf(v),ws[i]) && usable(i))own=ws[i];
        const auto collect=[&](PayloadFire fire,int count=0) -> int {
            for(int i=0;i<r.count && count<capacity;++i)if(r.entry[i].fire==fire) {
                if(IndependentSecondary(ClassOf(v),ws[i]) && ws[i]!=own)continue;
                // Resolve first: an empty native gun can still pull the selected, loaded store.
                auto* w=redirected(ws[i]);
                if(!w)continue;
                bool duplicate=false;
                for(int j=0;j<count;++j)if(out[j]==w)duplicate=true;
                if(!duplicate)out[count++]=w;
            }
            return count;
        };
        if(both)return collect(PayloadFire::secondary,collect(PayloadFire::primary));
        if(const int count=collect(group))return count;
        // Missing/expired/spent secondary falls back to a real usable primary, never to an old weapon pointer.
        if(group!=PayloadFire::primary)if(const int count=collect(PayloadFire::primary))return count;
        // Unknown controls have no proven cofire relationship: keep just their existing optic owner.
        for(int i=0;i<r.count;++i)if(r.entry[i].fire==PayloadFire::other && usable(i)){out[0]=ws[i];return 1;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
}  // namespace

int PayloadSightWeapons(const void* vehicle,unsigned index,unsigned char** out,int capacity) noexcept {
    return SeatFireWeapons(vehicle,index,false,out,capacity);
}
int PayloadTriggerWeapons(const void* vehicle,unsigned index,unsigned char** out,int capacity) noexcept {
    return SeatFireWeapons(vehicle,index,true,out,capacity);
}

bool InstallPayload() noexcept {
    __try {
        coaxBeltOk=Matches(kAmmoColorInit,kAmmoColorInitCode,sizeof(kAmmoColorInitCode)) &&
                   Matches(kFireCopy,kFireCopyCode,sizeof(kFireCopyCode));
        Log("HOOK coax belt=%d (a tracer every %d coax rounds)",coaxBeltOk,kCoaxTracerEvery);
        Log("HOOK coax motion=%d/%d (the coax no longer runs the main gun's firing motion)",InstallCoaxMotion(),kMotionClassCount);
        twinOk=Matches(kShotIntervalAt,kShotIntervalCode,sizeof(kShotIntervalCode)) &&
               Matches(kShotCooldownAt,kShotCooldownCode,sizeof(kShotCooldownCode)) &&
               Matches(kCooldownStepAt,kCooldownStepCode,sizeof(kCooldownStepCode)) &&
               Matches(kLatchReadAt,kLatchReadCode,sizeof(kLatchReadCode));
        Log("HOOK twin barrels in turn=%d (the BMPT's 2A42 pair fires left, right, left)",twinOk);
        if(!Matches(kPull,kPullCode,sizeof(kPullCode))) {
            Log("HOOK payload=0 (unexpected EDF.dll code at the holder pull: the stock vehicles' stores do not switch)");
            return false;
        }
        unsigned char jump[14]={0xFF,0x25,0,0,0,0};
        const auto to=reinterpret_cast<std::uintptr_t>(&PullHook);
        std::memcpy(jump+6,&to,8);
        pullOk=edf::PatchCode(image+kPull,kPullCode,jump,sizeof(jump));
        Log("HOOK payload=%d (the holder pull lands on the store picked)",pullOk);
        pickNetOk=pullOk && seat_net::Install() && seat_net::Register(seat_net::kPickMagic,&PickReceived);
        Log("HOOK payload online=%d (a seat's pick to the vehicle's copies, tag 15 'SPCK')",pickNetOk);
        return pullOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool NativePayloadControl(const void* vehicle,unsigned index) noexcept {
    __try {
        const auto* v=static_cast<const unsigned char*>(vehicle);
        if(!v || !Readable(v,kSeatCount+8) || v[kDead] || index>=SeatCount(v))return false;
        const auto seat=SeatAt(const_cast<unsigned char*>(v),index);
        const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(count>16 || !Readable(holders,count*8))return false;
        for(std::uint64_t i=0;i<count;++i) {
            if(!Readable(holders[i],kHolderWeapon+8))continue;
            const auto ctrl=At<const unsigned char*>(holders[i],kHolderCtrl);
            if(!Readable(ctrl,12) || At<int>(ctrl,8)<=0)continue;
            const auto weapon=At<const unsigned char*>(holders[i],kHolderWeapon);
            if(!Readable(weapon,kWeaponCharge+4))continue;
            const auto spec=StoreOf(weapon);
            if(spec && (spec->nativeControl || IndependentSecondary(ClassOf(v),weapon)))return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool PayloadAimWeapon(const void* vehicle,unsigned index,const void* source,const unsigned char** out) noexcept {
    if(!out)return false;
    *out=nullptr;
    __try {
        auto* v=static_cast<unsigned char*>(const_cast<void*>(vehicle));
        if(!Cfg().enabled || !v || !source || !Readable(v,kSeatCount+8) || v[kDead] || index>=SeatCount(v) || BodyOf(v)!=PluginBody::none)return false;
        const auto identity=ObjRef::Of(v);
        if(!identity.Is(v))return false;
        const auto seat=SeatAt(v,index);
        const bool localSeat=SeatRider(seat)==Rider::player && At<const void*>(seat,kSeatRider)==PlayerHuman();
        const bool npc=!localSeat && AiGunner(v,seat);
        if(!localSeat && !npc)return false;
        PayloadReadout r{};unsigned char* ws[kMostPayload]{};
        r.count=ReadSeat(v,index,ClassOf(v),ws,r,localSeat);
        int from=-1;
        for(int i=0;i<r.count;++i)if(ws[i]==source){from=i;break;}
        if(from<0)return false;
        PayloadFire group=r.entry[from].fire;
        if(group==PayloadFire::store) {
            group=PayloadFire::primary;
            for(int i=0;i<r.count;++i)if(SwitchSecondary(ClassOf(v),ws[i],r.entry[i]))group=PayloadFire::secondary;
        }
        if(group==PayloadFire::other)return false;
        // An intrinsic secondary aims as itself: it is no other weapon's redirect and redirects nothing.
        if(IndependentSecondary(ClassOf(v),ws[from])){*out=Spent(ws[from]) ? nullptr : ws[from];return true;}
        const Pick* picked=nullptr;
        for(const auto& p:picks)if(p.ref.Is(v) && p.seat==index && p.npc==npc && !p.remote){picked=&p;break;}
        const bool fresh=picked && GameMs()-picked->seen<=kFreshMs && (Cfg().stockStores || picked->nativeControl);
        bool catalogued=false;
        for(int i=0;i<r.count;++i)catalogued=catalogued || IsStoreWeapon(ws[i]);
        // Only managed payload/native-ammunition controllers opt an unmarked source into the peer's aiming path.
        // Before the first plan, beyond the firing redirect bound, or with this feature disabled the old peer stays native.
        if(!fresh || !(picked->nativeControl || (Cfg().stockStores && catalogued)))return false;
        // A fresh NPC target-loss/no-usable-weapon decision must not resurrect its native guns in the aimer.
        if(npc && fresh && !picked->weapon)return true;
        for(int i=0;i<r.count;++i)if(r.entry[i].fire==group) {
            const unsigned char* target=ws[i];
            if(fresh)for(int k=0;k<picked->redirects;++k)if(picked->from[k]==ws[i]) {target=picked->to[k];break;}
            if(!target)continue;
            // Identity comes from the currently installed, live holder set; stale target pointers are never returned.
            for(int j=0;j<r.count;++j)if(ws[j]==target && !Spent(ws[j])) {*out=ws[j];return true;}
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){*out=nullptr;return false;}
}

void ResetPayload() noexcept {
    ClearChoiceContext();
    ClearRedirect(nullptr);
    for(auto& p:picks)p=Pick{};
    for(auto& r:remotes)r=RemotePick{};
    pickGates.Reset();
    for(auto& x:sentPicks)x=SentPick{};
    latest=PayloadReadout{};latestMs=0;
    sightPick=SightPick{};
}
}  // namespace crew
