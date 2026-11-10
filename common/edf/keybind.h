// The plugins' keys kept in the game's own key store (docs/keyconfig-re.md, docs/plan-2026-10-25-keybinds.md §4): one
// table both plugins read (EDF6VehicleCrew src/keyconfig.cpp, EDF6AutoTurret autoturret/src/plugin.cpp), the game's key
// codes and their Windows virtual keys, and the store's layout. No Windows, no game: tests/keybind_test.cpp runs it.
//
// The store (EDF.dll TimeDateStamp 0x678CCB46, tests/keyconfig_native_audit.py checks every number here):
//   - the option object [0x20B2890] holds it at +0x318: per local player (+0x2800 each) ten sets of 0xFF int32 slots
//     at +0x28 (set * 0x3FC + action * 4), one slot per action byte. The game's actions use 0x00..0x4D; 0x4E ends a
//     page's list; 0x4F..0xFE are slots no code of the game reads (keyconfig-re.md §3).
//   - the whole store, 0x548C bytes from +0x318, is COMMON.CFG's body, written and read raw (0x7BBC30 / 0x7BC9E0):
//     every slot persists, the spare ones too (keyconfig-re.md §6).
//   - the menu writes the common page (row 2) and the vehicle pages (rows 5..10) to sets 0..3 at once (0xD5470), the
//     class pages to one set; the plugin's pages are made all-sets pages (src/keyconfig_menu.h), so set 0 is enough to
//     read and the four classes never disagree.
// A slot holds a game key code (below), 0 = unassigned.
//
// The plugins' slots are action bytes 0x4F.. in the order of kSlots: COMMON.CFG keeps them, so a slot's byte never
// changes and a new key only ever gets the next byte. Each owner seeds each of its slots once from its ini (the marker
// slot says up to which byte it did, so a slot added later is seeded alone, the player's keys left as they are): from
// then on the game's key config (HQ -> settings -> key config -> the 「插件」 pages) is where they are set, the ini value
// is no longer read. Only while EDF6VehicleCrew offers those pages (kPagesExport): without them the ini stays the keys.
#pragma once
#include <cstddef>
#include <cstdint>

namespace edf::keybind {
// --- The store
inline constexpr std::size_t kOptions=0x20B2890;   // the option object (the game's settings: COMMON.CFG)
inline constexpr std::size_t kStore=0x318;         // the key store inside it
inline constexpr std::size_t kStoreSize=0x548C;    // what COMMON.CFG saves of it
inline constexpr std::size_t kSlotsAt=0x28;        // the first slot
inline constexpr std::size_t kPlayerStride=0x2800; // a local player's sets
inline constexpr std::size_t kSetStride=0x3FC;     // a set: 0xFF slots
inline constexpr std::size_t kReady=0x5488;        // 1 once the store is built (0xD3F90) or loaded
inline constexpr int kSets=10,kSyncedSets=4,kActions=0xFF,kPlayers=2;
inline constexpr int kStockLastAction=0x4D,kListEnd=0x4E;

// --- The game's key codes (the menu's key names 0x11856F0 / 0x1F56050, the keyboard's virtual keys 0x11852C0)
inline constexpr int kNoAssign=0;
inline constexpr int kPadFirst=0x01,kPadLast=0x0B;   // A B X Y LT RT LB RB L3 R3 Back
inline constexpr int kMouseL=0x0C,kMouseM=0x0D,kMouseR=0x0E,kMouseWheel=0x0F,kMouseSide1=0x10,kMouseSide2=0x11;
inline constexpr int kKeyFirst=0x12,kKeyLast=0x78;
// The keyboard codes' virtual keys, kKeyFirst on (0x11852C0's jump table, in the order of the menu's key names A ..
// Kana; tests/keyconfig_native_audit.py reads it from EDF.dll).
inline constexpr unsigned char kKeyVk[kKeyLast-kKeyFirst+1]={
    0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,
    0x58,0x59,0x5A,                                                  // A .. Z
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,               // 0 .. 9
    0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,               // Num0 .. Num9
    0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,     // F1 .. F12
    0xBD,0xDE,0xDC,0xC0,0xBB,0xBA,0xDB,0xDD,0xBC,0xBE,               // - ^ \ @ ; : [ ] , .  (the JIS layout's names)
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0x5B,0x5C,                         // ShiftL ShiftR ControlL ControlR AltL AltR WinL WinR
    0x26,0x28,0x25,0x27,                                             // Up Down Left Right
    0x0D,0x20,0x08,0x09,0x2D,0x2E,0x24,0x23,0x22,0x21,0x2F,0x1B,0x2A,0x13,0x90,   // Return Space BackSpace Tab Insert
                                                                     // Delete Home End PageDown PageUp Help Escape Print Pause NumLock
    0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,                                   // Num * + Separator - . /
    0x5F,0x15};                                                      // Sleep Kana
static_assert(sizeof(kKeyVk)==0x67,"0x11852C0 maps 0x67 keyboard codes");

// The virtual key the plugins read (GetAsyncKeyState) for a game code; 0 for one they cannot read held: unassigned,
// a pad button (the plugins' pad buttons stay their ini's), the mouse wheel (no held state). The left Shift / Control /
// Alt are read as either side's (VK_SHIFT / VK_CONTROL / VK_MENU), as the plugins' Shift and Control keys always were.
constexpr int VkOfCode(int code) noexcept {
    switch(code) {
    case kMouseL: return 0x01;
    case kMouseR: return 0x02;
    case kMouseM: return 0x04;
    case kMouseSide1: return 0x05;
    case kMouseSide2: return 0x06;
    default: break;
    }
    if(code<kKeyFirst || code>kKeyLast)return 0;
    const int vk=kKeyVk[code-kKeyFirst];
    return vk==0xA0 ? 0x10 : vk==0xA2 ? 0x11 : vk==0xA4 ? 0x12 : vk;
}
// The game code for an ini's virtual key; kNoAssign for 0 and for a key the game has no code for. Either side's
// Shift / Control / Alt (VK_SHIFT / VK_CONTROL / VK_MENU) is the left one's code.
constexpr int CodeOfVk(int vk) noexcept {
    switch(vk) {
    case 0x01: return kMouseL;
    case 0x02: return kMouseR;
    case 0x04: return kMouseM;
    case 0x05: return kMouseSide1;
    case 0x06: return kMouseSide2;
    case 0x10: return CodeOfVk(0xA0);
    case 0x11: return CodeOfVk(0xA2);
    case 0x12: return CodeOfVk(0xA4);
    default: break;
    }
    for(int c=kKeyFirst;c<=kKeyLast;++c)if(kKeyVk[c-kKeyFirst]==vk)return c;
    return kNoAssign;
}
// A code the menu can show (0xD4290 indexes its name tables with it unchecked).
constexpr bool ValidCode(std::int32_t code) noexcept { return code>=kNoAssign && code<=kKeyLast; }
// A code the plugins can be bound to from the menu: a key or a mouse button they read held.
constexpr bool BindableCode(std::int32_t code) noexcept { return ValidCode(code) && VkOfCode(code)!=0; }

// --- The stock actions the plugins follow
inline constexpr int kSpotAction=0x1B;   // the common page's 「标记」 (OptionPlayer_KeyboardSpot): the plugin's Q mark and
                                         // EDF6AutoTurret's lock key are this key (NpcMarkKey / LockKey 0: off)

// --- The plugins' slots
enum class Owner : unsigned char { crew, turret };
inline constexpr int kFirstPage=11,kPages=6;   // the key config's pages 11..16 (the stock ones are 0..10)
inline constexpr int kMaxRows=12;              // a page's list: 13 bytes, the 0x4E end included (0x7CD2EA)
struct Slot {
    unsigned char action;   // the store's slot, fixed forever (COMMON.CFG keeps it)
    unsigned char page;     // kFirstPage..
    unsigned char row;      // its row on the page
    Owner owner;
    const wchar_t* ini;     // the ini key it was seeded from (the owner's section)
    int defaultVk;          // the owner's default (its Config's, its shipped ini's): seeded when the ini has none,
                            // written back by the page's 「恢复默认」
};
inline constexpr Slot kSlots[]={
    // page 11: on foot and anywhere
    {0x4F,11,0,Owner::crew,L"NpcFormationCycleKey",0x55},
    {0x50,11,1,Owner::crew,L"NpcPickupKey",0x59},
    {0x51,11,2,Owner::crew,L"PlayerJetHailKey",0x48},
    {0x52,11,3,Owner::crew,L"FirstPersonKey",0x56},
    {0x53,11,4,Owner::crew,L"MapKey",0x4D},
    {0x54,11,5,Owner::crew,L"DamageStatsKey",0x49},
    {0x55,11,6,Owner::crew,L"RoomLoadoutKey",0x71},
    {0x56,11,7,Owner::crew,L"PlayerJetChuteCutKey",0x58},
    {0x57,11,8,Owner::turret,L"AutoAimKey",0x4A},
    // page 12: every vehicle
    {0x58,12,0,Owner::crew,L"FreeLookKey",0x43},   // C since 2026-11-10 (kSteps; the middle button is the Grape's horn)
    {0x59,12,1,Owner::crew,L"HighCamKey",0x4C},    // L since 2026-11-10 (C went to free look)
    {0x5A,12,2,Owner::crew,L"SightZoomKey",0x5A},
    {0x5B,12,3,Owner::crew,L"SightRangeUpKey",0x21},
    {0x5C,12,4,Owner::crew,L"SightRangeDownKey",0x22},
    {0x5D,12,5,Owner::crew,L"VehicleThermalKey",0x4E},
    {0x5E,12,6,Owner::crew,L"SeatNextKey",0x46},
    {0x5F,12,7,Owner::crew,L"PlayerJetSwitchKey",0x52},
    {0x60,12,8,Owner::crew,L"VehicleSecondarySwitchKey",0x54},
    {0x61,12,9,Owner::crew,L"GunshipGunnerKey",0x56},
    {0x62,12,10,Owner::crew,L"GunshipGatlingKey",0x02},
    // page 13: the jets
    {0x63,13,0,Owner::crew,L"PlayerJetBoostKey",0x10},
    {0x64,13,1,Owner::crew,L"PlayerJetBrakeKey",0x11},
    {0x65,13,2,Owner::crew,L"PlayerJetTargetKey",0x51},
    {0x66,13,3,Owner::crew,L"PlayerJetFlareKey",0x58},
    {0x67,13,4,Owner::crew,L"PlayerJetGearKey",0x47},
    // page 14: the drill tank, the Proteus (its stance on G, the jets' gear key: the 形态 family, plan §3.3 A; so not
    // on the jets' page, where one key is one row)
    {0x68,14,0,Owner::crew,L"DrillLaunchKey",0x52},
    {0x69,14,1,Owner::crew,L"ProteusStanceKey",0x47},
    {0x6A,14,2,Owner::crew,L"ProteusShieldKey",0x42},
    {0x6B,14,3,Owner::crew,L"ProteusSalvoKey",0x02},
    // page 15: the Dreadnought
    {0x6C,15,0,Owner::crew,L"DreadnoughtBoostKey",0x10},
    {0x6D,15,1,Owner::crew,L"DreadnoughtDescendKey",0x11},
    {0x6E,15,2,Owner::crew,L"DreadnoughtStrafeLeftKey",0x51},
    {0x6F,15,3,Owner::crew,L"DreadnoughtStrafeRightKey",0x45},
    {0x70,15,4,Owner::crew,L"DreadnoughtHangarKey",0x48},
    {0x71,15,5,Owner::crew,L"DreadnoughtAheadKey",0x02},
    // page 16: the Sazabi
    {0x72,16,0,Owner::crew,L"SazabiLockKey",0x04},
    {0x73,16,1,Owner::crew,L"SazabiDashKey",0x10},
    {0x74,16,2,Owner::crew,L"SazabiDescendKey",0x11},
    {0x75,16,3,Owner::crew,L"SazabiFunnelKey",0x52},
    {0x76,16,4,Owner::crew,L"SazabiMegaCannonKey",0x58},
    {0x77,16,5,Owner::crew,L"SazabiMeleeKey",0x56},
    {0x78,16,6,Owner::crew,L"SazabiGuardKey",0x42},
    // page 14 again: every ground vehicle's own fire keys and its auto turrets (src/payload.cpp OwnKey, AutoShot)
    {0x79,14,4,Owner::crew,L"VehicleFire3Key",0x58},
    {0x7A,14,5,Owner::crew,L"VehicleFire4Key",0x05},
    {0x7B,14,6,Owner::crew,L"VehicleFire5Key",0x06},
    {0x7C,14,7,Owner::crew,L"VehicleFire6Key",0x48},
    {0x7D,14,8,Owner::crew,L"VehicleAutoTurretKey",0x59},
    // ...and the stock ground vehicles' cruise gears, two keys of their own that W / S override (src/drivegear.cpp)
    {0x7E,14,9,Owner::crew,L"VehicleCruiseUpKey",0x09},
    {0x7F,14,10,Owner::crew,L"VehicleCruiseDownKey",0x70},
};
inline constexpr int kSlotCount=static_cast<int>(sizeof(kSlots)/sizeof(kSlots[0]));
inline constexpr int kFirstAction=0x4F;
// Each owner's marker: a slot of set 0 no page lists, holding kMarkerTag | the default steps taken << 8 | the end of what
// the owner seeded (one past its highest slot byte then). The slots are only ever added at the next byte, so the owner's
// slots from that end on are the ones added since: those alone are seeded (MarkerEnd). Anything else in the slot (0 in a
// fresh store): nothing seeded. A marker written before the steps existed has 0 there: no step taken.
inline constexpr unsigned char kMarkerAction[2]={0xFE,0xFD};   // Owner::crew, Owner::turret
inline constexpr std::uint32_t kMarkerTag=0x4B420000u,kMarkerTagMask=0xFFFF0000u;   // "BK" in the high half
constexpr std::int32_t MarkerOf(int end,int steps=0) noexcept {
    return static_cast<std::int32_t>(kMarkerTag|(static_cast<std::uint32_t>(steps)&0xFFu)<<8|(static_cast<std::uint32_t>(end)&0xFFu));
}
constexpr int MarkerEnd(std::int32_t marker) noexcept {
    const std::uint32_t m=static_cast<std::uint32_t>(marker);
    return (m&kMarkerTagMask)==kMarkerTag ? static_cast<int>(m&0xFFu) : kFirstAction;
}
constexpr int MarkerSteps(std::int32_t marker) noexcept {
    const std::uint32_t m=static_cast<std::uint32_t>(marker);
    return (m&kMarkerTagMask)==kMarkerTag ? static_cast<int>(m>>8&0xFFu) : 0;
}

// --- Default steps: a slot's default changed after stores were seeded. COMMON.CFG keeps what a seeded slot holds, so a
// new default reaches a store seeded before it only through a step (the installer's ini steps, tools/installer.py
// NEW_DEFAULTS_*, are the same for the ini). A move: the slot still holding its old default gets the new one, if no
// other slot holds that key then (a move freeing a key lets the one waiting for it go: HighCamKey off C, then
// FreeLookKey onto it). A key the player set is kept. A store seeded after a step (its ini has the new defaults) skips it.
struct Move { unsigned char action; int fromVk; int toVk; };
struct Step { const Move* moves; int count; };
// 2026-11-10 (docs/feedback-2026-11-10-freeview-key.md): free look off the middle button, which on the drive page is the
// stock row that sounds the Grape's horn (docs/keyconfig-re.md §8), onto C; the high view off C onto L.
inline constexpr Move kMoves1110[]={{0x59,0x43,0x4C},{0x58,0x04,0x43}};
inline constexpr Step kCrewSteps[]={{kMoves1110,2}};
constexpr int StepsOf(Owner owner) noexcept {
    return owner==Owner::crew ? static_cast<int>(sizeof(kCrewSteps)/sizeof(kCrewSteps[0])) : 0;
}
// EDF6VehicleCrew's export (looked up by name, common/edf/aimlink.h Resolve): true once its 「插件」 pages are in the game's
// key config. The store's slots are the keys only then; EDF6AutoTurret without it (alone, or the pages not offered)
// neither seeds nor reads the store, its ini is its keys.
using PagesOfferedFn=bool(__cdecl*)();
inline constexpr char kPagesExport[]="EDF6VehicleCrew_KeyConfigPagesV1";

constexpr int RowsOf(int page) noexcept {
    int n=0;
    for(const Slot& s:kSlots)n+=s.page==page ? 1 : 0;
    return n;
}
constexpr bool PluginPage(int page) noexcept { return page>=kFirstPage && page<kFirstPage+kPages; }
constexpr const Slot* SlotAt(int page,int row) noexcept {
    for(const Slot& s:kSlots)if(s.page==page && s.row==row)return &s;
    return nullptr;
}
constexpr const Slot* SlotOf(int action) noexcept {
    for(const Slot& s:kSlots)if(s.action==action)return &s;
    return nullptr;
}
constexpr int IndexOf(const Slot* s) noexcept { return s ? static_cast<int>(s-kSlots) : -1; }
constexpr int SlotsOf(Owner owner) noexcept {
    int n=0;
    for(const Slot& s:kSlots)n+=s.owner==owner ? 1 : 0;
    return n;
}
// One past the owner's highest slot byte: what its marker says once all of its slots are seeded.
constexpr int OwnerEnd(Owner owner) noexcept {
    int end=kFirstAction;
    for(const Slot& s:kSlots)if(s.owner==owner && s.action>=end)end=s.action+1;
    return end;
}

// The catalog's own rules (also tests/keybind_test.cpp's): bytes in order from kFirstAction (a slot added later takes
// the next byte, whatever its page: COMMON.CFG keeps the bytes), each page's rows 0.. in the order of its slots, every
// page used and a list the stock page code can hold, no slot on a marker.
constexpr bool CatalogValid() noexcept {
    int rows[kPages]{};
    for(int i=0;i<kSlotCount;++i) {
        const Slot& s=kSlots[i];
        if(s.action!=kFirstAction+i || !PluginPage(s.page))return false;
        int& row=rows[s.page-kFirstPage];
        if(s.row!=row || ++row>kMaxRows)return false;
        if(s.action==kMarkerAction[0] || s.action==kMarkerAction[1])return false;
    }
    for(const int n:rows)if(!n)return false;
    return true;
}
static_assert(CatalogValid(),"the slots' bytes, pages and rows are in order and fit the stock page lists");
// Each step's moves: slots of the crew, from another key to the slot's default now, at most 8 to a step (ApplyStep).
constexpr bool StepsValid() noexcept {
    for(const Step& st:kCrewSteps) {
        if(st.count<1 || st.count>8)return false;
        for(int i=0;i<st.count;++i) {
            const Slot* s=SlotOf(st.moves[i].action);
            if(!s || s->owner!=Owner::crew || st.moves[i].fromVk==st.moves[i].toVk)return false;
        }
    }
    // the last step's moves end on the defaults (an earlier step's may have been moved on by a later one)
    const Step& last=kCrewSteps[sizeof(kCrewSteps)/sizeof(kCrewSteps[0])-1];
    for(int i=0;i<last.count;++i)if(SlotOf(last.moves[i].action)->defaultVk!=last.moves[i].toVk)return false;
    return true;
}
static_assert(StepsValid(),"the default steps move crew slots onto their defaults");

// The store's slot `action` of `player`'s set `set` (store = the option object + kStore).
inline std::int32_t* SlotPtr(unsigned char* store,int player,int set,int action) noexcept {
    return reinterpret_cast<std::int32_t*>(store+kSlotsAt+static_cast<std::size_t>(player)*kPlayerStride+
                                           static_cast<std::size_t>(set)*kSetStride+static_cast<std::size_t>(action)*4);
}
inline std::int32_t Read(unsigned char* store,int player,int action) noexcept { return *SlotPtr(store,player,0,action); }
// As the menu writes an all-sets page (0xD5470): the four class sets.
inline void WriteSynced(unsigned char* store,int player,int action,std::int32_t code) noexcept {
    for(int set=0;set<kSyncedSets;++set)*SlotPtr(store,player,set,action)=code;
}
// Where the owner's marker says its seeding got to (kFirstAction: nothing seeded).
inline int SeededEnd(unsigned char* store,Owner owner) noexcept { return MarkerEnd(Read(store,0,kMarkerAction[static_cast<int>(owner)])); }
inline int SeededSteps(unsigned char* store,Owner owner) noexcept { return MarkerSteps(Read(store,0,kMarkerAction[static_cast<int>(owner)])); }
inline bool Seeded(unsigned char* store,Owner owner) noexcept {
    return SeededEnd(store,owner)>=OwnerEnd(owner) && SeededSteps(store,owner)>=StepsOf(owner);
}
// Does a slot other than `except` hold `code` (player 0's set 0, as the plugins read them)?
inline bool CodeHeld(unsigned char* store,std::int32_t code,int except) noexcept {
    for(const Slot& s:kSlots)if(s.action!=except && Read(store,0,s.action)==code)return true;
    return false;
}
// One step on a seeded store (player 0's, as seeded): the moves whose slot still holds the old default and whose new key
// is free, until none is left to take. The slots moved.
inline int ApplyStep(unsigned char* store,const Step& step) noexcept {
    bool done[8]{};
    int moved=0;
    for(bool again=true;again;) {
        again=false;
        for(int i=0;i<step.count && i<8;++i) {
            const Move& m=step.moves[i];
            if(done[i] || Read(store,0,m.action)!=CodeOfVk(m.fromVk) || CodeHeld(store,CodeOfVk(m.toVk),m.action))continue;
            WriteSynced(store,0,m.action,CodeOfVk(m.toVk));
            done[i]=again=true;
            ++moved;
        }
    }
    return moved;
}
// The owner's slots its marker does not cover yet from its ini (`iniVk(slot)`: the ini's virtual key, or the slot's
// default when the ini has none), then its marker; the slots it covers keep what they hold (the player's keys). Player
// 0's: the keyboard is the first local player's (the menu's keyboard pages are player 0's). The slots written.
// A store the owner seeded before (some of) its default steps takes those steps on the slots it keeps (`moved`: the slots
// they moved); a store seeded now takes none (the ini has the new defaults).
template<class IniVk> int Seed(unsigned char* store,Owner owner,IniVk iniVk,int* moved=nullptr) noexcept {
    const int from=SeededEnd(store,owner),steps=from>kFirstAction ? SeededSteps(store,owner) : StepsOf(owner);
    int written=0,stepped=0;
    for(const Slot& s:kSlots)
        if(s.owner==owner && s.action>=from){WriteSynced(store,0,s.action,CodeOfVk(iniVk(s)));++written;}
    if(owner==Owner::crew)
        for(int i=steps;i<StepsOf(owner);++i)stepped+=ApplyStep(store,kCrewSteps[i]);
    if(from<OwnerEnd(owner) || steps<StepsOf(owner))
        *SlotPtr(store,0,0,kMarkerAction[static_cast<int>(owner)])=MarkerOf(OwnerEnd(owner),StepsOf(owner));
    if(moved)*moved=stepped;
    return written;
}
// The virtual key a slot holds now for the plugins (0: unassigned, or a code they cannot read: see VkOfCode).
inline int SlotVk(unsigned char* store,int action) noexcept {
    const std::int32_t code=Read(store,0,action);
    return ValidCode(code) ? VkOfCode(code) : 0;
}
}  // namespace edf::keybind
