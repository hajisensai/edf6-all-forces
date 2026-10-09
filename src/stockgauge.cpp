// The stock weapon gauges where our HUD lists the weapons (the user, 2026-10-06: "删掉原版挂载和油料显示。选择挂载等改成
// 我们自己的hud显示"; ini HideStockGauges), and the fuel tank they showed (docs/hud-re.md §9).
//
// The stock vehicle gauge is a HUiHudWeapon of its own (layout lyt_HudWeaponGuageVehicle: a pool of 5 "WeaponGuage"
// panels, each a name, rounds and magazine; the instance's +0xC60 set by its constructor, 0x82DBEF). Its update (vtable
// slot 1, 0x832B30) lists the player's seat's holders once a ride, the first update with a vehicle (+0xC61 latched): an
// index a holder that has a weapon, into +0xC68 (a buffer: data +0xC70, size +0xC80). Its layout (slot 2, 0x831A40, run
// when +0x770 is set) shows a pool panel for each listed index and hides the rest (panel +0x1F8 = 0, 0x832348). Nothing
// else reads the list (the displacement scan finds +0xC68..+0xC80 only in the constructor and these two), and firing
// never looks at the HUD. So: with the list's size 0 the layout hides every panel, through its own code; set back to
// unlisted (+0xC61 0, size 0) the next update lists the seat again. Both are done here around the stock update, on its
// own thread, the dirty flag +0x770 set so the layout runs: no draw call skipped, no widget touched by hand.
//
// The same latch held a bug of its own: the list is made once a ride, so a seat switch (seatswitch.cpp: the player stays
// in the vehicle) kept the old seat's indices and the layout read the new seat's holders with them, past its holder
// count where the new seat has fewer. The seat a list was made for is kept; another seat relists.
//
// Which vehicle is covered is HudPublish's (SetStockGaugeCover, game thread): the one the player is in while the HUD it
// publishes lists that vehicle's weapons (and can draw text). The gauge's own player (its owner, cast to SoldierBase as
// the update casts it) must be in that vehicle: a second local player's gauge is left alone.
//
// The armor gauge (the user, 2026-10-09: "上了载具以后，可以把原版的左上角的血条hud隐藏吧，我们已经有自制的hud了";
// docs/hud-re.md §9.3): HUiHudPowerGuage (vtable 0x17FCC98; the player's armor and the vehicle's durability, top left)
// is hidden as one layout while the same cover holds for its player (the HUD of ours lists the vehicle they ride, and
// draws its durability), by its root node's shown flag: stock_armor_hud.h. Its update (slot 1, 0x827010) is chained and
// the flag written after it, on its own thread: no draw call skipped, nothing else in the layout touched. The player's
// own armor goes with it while they ride (one layout: the vehicle's bar is drawn on the same root); on foot it is back.
//
// The fuel tank: every heli (HelicopterBase slot 46 0x6530E0: +0x1690), the 503 bike (+0x29B0) and the 511 (+0x2B00)
// have a FuelTank {+0 on, +4 capacity, +8 left, +0xC burn a unit of rotor or throttle} (made by 0x5EFA70, burnt by
// 0x5EF8E0: left -= |input| x burn a frame); the fuel weapon v_fuel01 (AmmoCount 1) is how the stock gauge shows it.
#include "crew.h"
#include "memory.h"
#include "stock_armor_hud.h"
#include <cmath>
#include <cwchar>
#include <iterator>

namespace crew {
namespace {
// HUiHudWeapon (EDF.dll TimeDateStamp 0x678CCB46).
constexpr unsigned kGaugeVtable=0x17FD150,kUpdateSlot=kGaugeVtable+1*8,kUpdate=0x832B30;
constexpr unsigned kCast=0x12DA7AA,kSceneObjectType=0x2006450,kSoldierType=0x2006428;   // __RTDynamicCast and its two types
constexpr std::size_t kGaugeDirty=0x770,kGaugeOwner=0x778,kGaugeOwnerCtrl=0x780;
constexpr std::size_t kGaugeVehicle=0xC60,kGaugeListed=0xC61,kGaugeListSize=0xC80;
constexpr std::size_t kSoldierSeat=0x1540,kSoldierVehicle=0x1548,kSoldierVehicleCtrl=0x1550,kUses=0x8;
struct Sig { unsigned rva; const unsigned char* bytes; std::size_t size; };
const unsigned char kUpdateSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
// The update's owner cast (0x832BAE): __RTDynamicCast(owner, 0, SceneObject, SoldierBase, 0).
const unsigned char kCastSig[]={0x89,0x74,0x24,0x20,0x4C,0x8D,0x0D,0x6F,0x38,0x7D,0x01,0x4C,0x8D,0x05,0x90,0x38,0x7D,0x01,0x33,0xD2,
    0x49,0x8B,0xCA,0xE8,0xE0,0x7B,0xAA,0x00};
// The vehicle gauge's branch (0x832C48): +0xC60, the soldier's vehicle +0x1550, the latch +0xC61; its seat +0x1540 and
// the seat's holder count (0x832C85); the list's buffer +0xC68 (0x832CB2); off the vehicle: unlatched, size 0 (0x832D00).
const unsigned char kBranchSig[]={0x80,0xBF,0x60,0x0C,0x00,0x00,0x00,0x0F,0x84,0xBE,0x00,0x00,0x00,0x49,0x8B,0x8D,0x50,0x15,0x00,0x00,
    0x0F,0xB6,0x87,0x61,0x0C,0x00,0x00,0x48,0x85,0xC9};
const unsigned char kSeatSig[]={0x4D,0x8B,0xAD,0x40,0x15,0x00,0x00,0x4D,0x63,0xBD,0xD8,0x00,0x00,0x00};
const unsigned char kListSig[]={0x48,0x8D,0x9F,0x68,0x0C,0x00,0x00};
const unsigned char kUnlistSig[]={0xC6,0x87,0x61,0x0C,0x00,0x00,0x00,0x48,0x89,0xB7,0x80,0x0C,0x00,0x00};
// The layout (slot 2): run when +0x770 (0x831A96); the vehicle gauge's panels from the list's size (0x831BB4); the
// panels past it hidden (0x832348).
const unsigned char kDirtySig[]={0x80,0xB9,0x70,0x07,0x00,0x00,0x00,0x0F,0x84,0x08,0x10,0x00,0x00};
const unsigned char kPanelsSig[]={0x80,0xBF,0x60,0x0C,0x00,0x00,0x00,0x0F,0x84,0xC4,0x07,0x00,0x00,0x80,0xBF,0x61,0x0C,0x00,0x00,0x00,
    0x0F,0x84,0xDD,0x0E,0x00,0x00,0x8B,0x9F,0x80,0x0C,0x00,0x00};
const unsigned char kHideSig[]={0xC6,0x81,0xF8,0x01,0x00,0x00,0x00};
const Sig kGaugeSigs[]={{kUpdate,kUpdateSig,sizeof(kUpdateSig)},{0x832BAE,kCastSig,sizeof(kCastSig)},
    {0x832C48,kBranchSig,sizeof(kBranchSig)},{0x832C85,kSeatSig,sizeof(kSeatSig)},{0x832CB2,kListSig,sizeof(kListSig)},
    {0x832D00,kUnlistSig,sizeof(kUnlistSig)},{0x831A96,kDirtySig,sizeof(kDirtySig)},{0x831BB4,kPanelsSig,sizeof(kPanelsSig)},
    {0x832348,kHideSig,sizeof(kHideSig)}};

// HUiHudPowerGuage: its update (slot 1) and what the hold reads. Its constructor (0x825250) sets the vtable (0x825290)
// and looks up Guage_Root through the HUiHud base (0x825438: call 0x816080, which stores the node at +0x788 (0x816101),
// a weak reference: its control block at +0x790); the update casts the same owner the weapon gauge's does (+0x778,
// 0x827082); a layout node's constructor shows it (+0x1F8 = 1, 0x7E999C).
constexpr unsigned kArmorVtable=0x17FCC98,kArmorUpdateSlot=kArmorVtable+1*8,kArmorUpdate=0x827010;
constexpr std::size_t kHudRoot=0x788,kHudRootCtrl=0x790,kNodeShown=0x1F8;
const unsigned char kArmorUpdateSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x55};
const unsigned char kArmorVtableSig[]={0x48,0x8D,0x05,0x01,0x7A,0xFD,0x00};   // 0x825290: lea rax, [0x17FCC98]
const unsigned char kArmorRootCallSig[]={0xE8,0x43,0x0C,0xFF,0xFF};           // 0x825438: call 0x816080
const unsigned char kRootNameSig[]={0x48,0x8D,0x15,0x2A,0x25,0xFE,0x00};      // 0x8160C7: lea rdx, L"Guage_Root"
const unsigned char kRootStoreSig[]={0x48,0x89,0x93,0x88,0x07,0x00,0x00};     // 0x816101: mov [rbx+0x788], rdx
const unsigned char kArmorOwnerSig[]={0x4C,0x8B,0x97,0x78,0x07,0x00,0x00};    // 0x827082: mov r10, [rdi+0x778]
const unsigned char kNodeShowSig[]={0xC6,0x87,0xF8,0x01,0x00,0x00,0x01};      // 0x7E999C: mov byte [rdi+0x1F8], 1
const Sig kArmorSigs[]={{kArmorUpdate,kArmorUpdateSig,sizeof(kArmorUpdateSig)},{0x825290,kArmorVtableSig,sizeof(kArmorVtableSig)},
    {0x825438,kArmorRootCallSig,sizeof(kArmorRootCallSig)},{0x8160C7,kRootNameSig,sizeof(kRootNameSig)},
    {0x816101,kRootStoreSig,sizeof(kRootStoreSig)},{0x827082,kArmorOwnerSig,sizeof(kArmorOwnerSig)},
    {0x7E999C,kNodeShowSig,sizeof(kNodeShowSig)}};

// The FuelTank: made (0x5EFA70: +8 = +4 = capacity, +0xC = burn, +0 = 1), burnt (0x5EF8E0), and where each class keeps it.
constexpr std::size_t kTankOn=0x0,kTankCapacity=0x4,kTankLeft=0x8;
constexpr std::size_t kHeliTank=0x1690,kBike503Tank=0x29B0,kBike511Tank=0x2B00;
constexpr unsigned kVt503=0x17DA508,kVt511=0x17DBDF8;
const unsigned char kMakeSig[]={0xF3,0x0F,0x11,0x49,0x08,0xF3,0x0F,0x11,0x49,0x04,0xF3,0x0F,0x11,0x51,0x0C,0xC6,0x01,0x01,0xC3};
const unsigned char kBurnSig[]={0x80,0x39,0x00,0x0F,0x28,0xD1,0x74,0x63,0xF3,0x0F,0x10,0x49,0x08,0x0F,0x57,0xDB};
const unsigned char kHeliMakeSig[]={0x48,0x8D,0x8B,0x90,0x16,0x00,0x00,0xE8,0xB4,0xC7,0xF9,0xFF};   // 0x6532B0 (slot 46)
const unsigned char kHeliBurnSig[]={0x48,0x8D,0x97,0x90,0x16,0x00,0x00};                           // 0x651A0E (the rotor)
const unsigned char k503MakeSig[]={0x48,0x8D,0x8E,0xB0,0x29,0x00,0x00,0xE8,0x12,0x7C,0xFD,0xFF};    // 0x617E52
const unsigned char k503BurnSig[]={0x48,0x8D,0x8B,0xB0,0x29,0x00,0x00,0xE8,0x01,0x7F,0xFD,0xFF};    // 0x6179D3
const unsigned char k511MakeSig[]={0x48,0x8D,0x8B,0x00,0x2B,0x00,0x00,0xE8,0xDB,0x03,0xFD,0xFF};    // 0x61F689
const unsigned char k511BurnSig[]={0x48,0x8D,0x8B,0x00,0x2B,0x00,0x00,0xE8,0x61,0x06,0xFD,0xFF};    // 0x61F273
const Sig kFuelSigs[]={{0x5EFA70,kMakeSig,sizeof(kMakeSig)},{0x5EF8E0,kBurnSig,sizeof(kBurnSig)},
    {0x6532B0,kHeliMakeSig,sizeof(kHeliMakeSig)},{0x651A0E,kHeliBurnSig,sizeof(kHeliBurnSig)},
    {0x617E52,k503MakeSig,sizeof(k503MakeSig)},{0x6179D3,k503BurnSig,sizeof(k503BurnSig)},
    {0x61F689,k511MakeSig,sizeof(k511MakeSig)},{0x61F273,k511BurnSig,sizeof(k511BurnSig)}};
constexpr float kBurnSmoothSec=2.0f;   // the burn's smoothing (a frame's decrement is one float step at a full tank)

using UpdateFn=void(__fastcall*)(unsigned char*,void*);
using CastFn=void*(__cdecl*)(void*,long,void*,void*,int);
UpdateFn nextUpdate=nullptr,nextArmor=nullptr;
bool gaugeOk=false,fuelOk=false,armorOk=false;

// The armor gauges' holds (stock_armor_hud.h), in the mission `armorGeneration` (ResetStockGauges begins another).
maphud::Record armorHolds[armorhud::kGauges]{};
std::uint64_t armorGeneration=1;

// What the hidden armor gauge showed (stock_armor_hud.h Readout), for hud.cpp: written by ArmorHook (the gauge's thread)
// while it holds the HUD's player's gauge hidden, its soldier and the wall time with it.
constexpr std::size_t kObjectHp=0x2F8,kObjectHpMost=0x2F4;   // a soldier's armor, a vehicle's durability (layout.h kHp)
constexpr ULONGLONG kArmorFreshMs=250;
SRWLOCK armorLock=SRWLOCK_INIT;
struct ArmorShown { const void* soldier; ULONGLONG at; armorhud::Readout r; } armorShown{};

// The vehicle whose weapons our HUD lists (SetStockGaugeCover), by its object and its weak-this control block.
SRWLOCK coverLock=SRWLOCK_INIT;
struct Cover { const void* vehicle; const void* ctrl; } cover{};

// A vehicle gauge (one a local player): the seat its list was made for, whether its panels are hidden here.
struct Gauge { const unsigned char* gauge; const void* seat; bool hidden; };
Gauge gauges[4]{};

// The gauge's player in a vehicle: their seat and the vehicle (object, control block).
struct Ride { const void* seat; const void* vehicle; const void* ctrl; const unsigned char* soldier; };

Gauge* GaugeOf(const unsigned char* g) noexcept {
    Gauge* free=nullptr;
    for(auto& e:gauges) {
        if(e.gauge==g)return &e;
        if(!free && !e.gauge)free=&e;
    }
    if(free)*free=Gauge{g,nullptr,false};
    return free;
}

bool RideOf(const unsigned char* g,Ride* r) noexcept {
    __try {
        const auto ownerCtrl=At<const unsigned char*>(g,kGaugeOwnerCtrl);
        void* const owner=At<void*>(g,kGaugeOwner);
        if(!ownerCtrl || !owner || At<std::int32_t>(ownerCtrl,kUses)==0)return false;
        const auto soldier=static_cast<const unsigned char*>(
            reinterpret_cast<CastFn>(image+kCast)(owner,0,image+kSceneObjectType,image+kSoldierType,0));
        if(!soldier)return false;
        const auto ctrl=At<const unsigned char*>(soldier,kSoldierVehicleCtrl);
        if(!ctrl || At<std::int32_t>(ctrl,kUses)==0)return false;
        *r=Ride{At<const void*>(soldier,kSoldierSeat),At<const void*>(soldier,kSoldierVehicle),ctrl,soldier};
        return r->vehicle!=nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool Covered(const Ride& r) noexcept {
    if(!Cfg().enabled || !Cfg().hideStockGauges)return false;
    AcquireSRWLockShared(&coverLock);
    const bool on=cover.vehicle==r.vehicle && cover.ctrl==r.ctrl;
    ReleaseSRWLockShared(&coverLock);
    return on;
}

// The vehicle gauge's update, the list kept to the seat and emptied while covered (see the top).
void __fastcall UpdateHook(unsigned char* g,void* context) {
    if(!g[kGaugeVehicle]){nextUpdate(g,context);return;}
    Ride r{};
    const bool rides=RideOf(g,&r);
    const bool hide=rides && Covered(r);
    Gauge* const e=GaugeOf(g);
    bool relist=false;
    if(e && g[kGaugeListed]) {
        const bool moved=rides && e->seat && r.seat!=e->seat;
        if(moved || (e->hidden && !hide)) {
            Log("GAUGE %p: the stock weapon gauge listed again (%s)",g,moved ? "another seat" : "our HUD no longer lists it");
            g[kGaugeListed]=0;Put<std::uint64_t>(g,kGaugeListSize,0);e->hidden=false;relist=true;
        }
    }
    const bool listed=g[kGaugeListed]!=0;
    nextUpdate(g,context);
    if(!e)return;
    if(!g[kGaugeListed]){*e=Gauge{};return;}   // off the vehicle (the update unlisted it): its slot free again
    if(!listed){e->seat=rides ? r.seat : nullptr;g[kGaugeDirty]=1;}
    if(hide && At<std::uint64_t>(g,kGaugeListSize)) {
        if(!e->hidden)Log("GAUGE %p: the stock weapon gauge hidden for v=%p (our HUD lists its weapons)",g,r.vehicle);
        Put<std::uint64_t>(g,kGaugeListSize,0);g[kGaugeDirty]=1;e->hidden=true;
    } else if(relist)g[kGaugeDirty]=1;
}

// The armor gauge's update, its layout's root held hidden while covered (see the top).
void __fastcall ArmorHook(unsigned char* g,void* context) {
    nextArmor(g,context);
    Ride r{};
    // Only the HUD's own player's (PlayerHuman: the one whose armor hud.cpp then shows in its place); a second local
    // player's gauge stays, its armor shown nowhere else.
    const bool hide=RideOf(g,&r) && Covered(r) && r.soldier==PlayerHuman();
    __try {
        const auto ctrl=At<const unsigned char*>(g,kHudRootCtrl);
        unsigned char* const root=At<unsigned char*>(g,kHudRoot);
        if(!root || !ctrl || At<std::int32_t>(ctrl,kUses)==0)return;   // no layout (yet, or any more): nothing shown
        const bool was=[&]{ for(const auto& h:armorHolds)if(h.hidden && h.cam==g)return true; return false; }();
        const bool now=armorhud::Step(armorHolds,g,armorGeneration,hide,root+kNodeShown);
        if(now!=was)Log("GAUGE %p: the stock armor gauge %s",g,now ? "hidden (our HUD shows the vehicle the player rides)" : "shown again");
        // Its numbers to our HUD while it is hidden; taken back when it shows again.
        armorhud::Readout shown{};
        const bool give=now && armorhud::Publish(true,At<float>(r.soldier,kObjectHp),At<float>(r.soldier,kObjectHpMost),
            Readable(r.vehicle,kObjectHp+4) ? At<float>(r.vehicle,kObjectHp) : 0.0f,
            Readable(r.vehicle,kObjectHp+4) ? At<float>(r.vehicle,kObjectHpMost) : 0.0f,&shown);
        AcquireSRWLockExclusive(&armorLock);
        if(give)armorShown=ArmorShown{r.soldier,GetTickCount64(),shown};
        else if(armorShown.soldier==r.soldier || (was && !now))armorShown=ArmorShown{};
        ReleaseSRWLockExclusive(&armorLock);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

bool AllMatch(const Sig* sigs,std::size_t n) noexcept {
    for(std::size_t i=0;i<n;++i)if(!Matches(sigs[i].rva,sigs[i].bytes,sigs[i].size))return false;
    return true;
}

std::size_t TankOf(const void* v) noexcept {
    if(IsHelicopter(v))return kHeliTank;   // the plugin's aircraft are 506 bodies: helicopters too
    const void* const vt=At<const void*>(v,0);
    return vt==image+kVt503 ? kBike503Tank : vt==image+kVt511 ? kBike511Tank : 0;
}

// The burn of the tanks read lately (game thread): left a game ms ago, units a second.
struct Burn { ObjRef ref; float left; ULONGLONG ms; float rate; };
Burn burns[4]{};
Burn* BurnOf(const void* v) noexcept {
    Burn* old=&burns[0];
    for(auto& b:burns) {
        if(b.ref.Is(v))return &b;
        if(b.ms<old->ms)old=&b;
    }
    *old=Burn{ObjRef::Of(v),-1.0f,0,-1.0f};
    return old;
}
}  // namespace

bool IsFuelTank(const unsigned char* w) noexcept {
    std::size_t n=0;
    const wchar_t* f=WeaponFile(w,&n);
    return f && n>=6 && _wcsnicmp(f,L"V_FUEL",6)==0;
}

namespace {
// The armor gauge's update chained (its own signatures: off alone when they differ).
bool InstallArmorGauge() noexcept {
    if(!AllMatch(kArmorSigs,std::size(kArmorSigs)) || !Readable(image+kArmorUpdateSlot,8)) {
        Log("HOOK stockgauge armor=0: the stock armor gauge's code is not as expected (it stays as it is)");
        return false;
    }
    void* const current=*reinterpret_cast<void**>(image+kArmorUpdateSlot);
    if(current!=image+kArmorUpdate)Log("HOOK stockgauge: the armor gauge chaining onto %p (another plugin)",current);
    nextArmor=reinterpret_cast<UpdateFn>(current);
    armorOk=PatchVtableSlot(reinterpret_cast<void**>(image+kArmorUpdateSlot),current,reinterpret_cast<void*>(&ArmorHook));
    Log("HOOK stockgauge armor=%d (the stock armor gauge hidden while our HUD shows the vehicle ridden)",armorOk);
    return armorOk;
}
}  // namespace

bool InstallStockGauges() noexcept {
    fuelOk=AllMatch(kFuelSigs,std::size(kFuelSigs));
    Log("HOOK stockgauge fuel=%d (the fuel tanks read where the FUEL gauge showed them)",fuelOk);
    InstallArmorGauge();
    if(!AllMatch(kGaugeSigs,std::size(kGaugeSigs)) || !Readable(image+kUpdateSlot,8)) {
        Log("HOOK stockgauge gauge=0: the stock weapon gauge's code is not as expected (it stays as it is)");
        return false;
    }
    void* const current=*reinterpret_cast<void**>(image+kUpdateSlot);
    if(current!=image+kUpdate)Log("HOOK stockgauge: chaining onto %p (another plugin)",current);
    nextUpdate=reinterpret_cast<UpdateFn>(current);
    gaugeOk=PatchVtableSlot(reinterpret_cast<void**>(image+kUpdateSlot),current,reinterpret_cast<void*>(&UpdateHook));
    Log("HOOK stockgauge gauge=%d",gaugeOk);
    return gaugeOk;
}

void SetStockGaugeCover(bool lists) noexcept {
    Cover c{};
    __try {
        if(const unsigned char* const human=lists ? PlayerHuman() : nullptr) {
            const auto ctrl=At<const unsigned char*>(human,kSoldierVehicleCtrl);
            if(ctrl && At<std::int32_t>(ctrl,kUses)!=0)c=Cover{At<const void*>(human,kSoldierVehicle),ctrl};
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){c=Cover{};}
    AcquireSRWLockExclusive(&coverLock);
    cover=c;
    ReleaseSRWLockExclusive(&coverLock);
}

bool PlayerStockArmor(armorhud::Readout* out) noexcept {
    const void* const human=PlayerHuman();
    AcquireSRWLockShared(&armorLock);
    const ArmorShown a=armorShown;
    ReleaseSRWLockShared(&armorLock);
    if(!armorOk || !human || a.soldier!=human || GetTickCount64()-a.at>kArmorFreshMs)return false;
    *out=a.r;
    return true;
}

void ResetStockGauges() noexcept {
    for(auto& e:gauges)e=Gauge{};   // a gauge gone with the last mission while its player rode
    for(auto& b:burns)b=Burn{};
    ++armorGeneration;
    AcquireSRWLockExclusive(&armorLock);
    armorShown=ArmorShown{};
    ReleaseSRWLockExclusive(&armorLock);   // the armor gauges' holds are the last mission's now (stock_armor_hud.h): never written blind
    SetStockGaugeCover(false);
}

bool FuelGauge(const void* v,FuelReading* out) noexcept {
    *out=FuelReading{false,0.0f,-1.0f};
    if(!fuelOk || !v)return false;
    __try {
        const std::size_t at=TankOf(v);
        if(!at)return false;
        const unsigned char* const tank=static_cast<const unsigned char*>(v)+at;
        const float capacity=At<float>(tank,kTankCapacity),left=At<float>(tank,kTankLeft);
        if(!tank[kTankOn] || !std::isfinite(capacity+left) || capacity<=0.0f)return false;
        Burn* const b=BurnOf(v);
        const ULONGLONG ms=GameMs();
        if(b->ms && ms>b->ms) {
            const float dt=static_cast<float>(ms-b->ms)*0.001f,drop=(b->left-left)/dt;
            if(drop<0.0f)b->rate=-1.0f;   // filled up (a resupply): measured afresh
            else b->rate=b->rate<0.0f ? drop : b->rate+(drop-b->rate)*std::fmin(dt/kBurnSmoothSec,1.0f);
        }
        if(ms!=b->ms){b->left=left;b->ms=ms;}
        out->ok=true;
        out->share=std::fmax(0.0f,std::fmin(left/capacity,1.0f));
        out->sec=b->rate>0.0f ? std::fmax(left,0.0f)/b->rate : -1.0f;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){*out=FuelReading{false,0.0f,-1.0f};return false;}
}
}  // namespace crew
