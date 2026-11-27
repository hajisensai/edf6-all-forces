// The room / HQ screen frame's pure side (src/room_ui.cpp), offline-testable (tests/room_ui_test.cpp):
//   names       vcRoot, the marker tools/room_layout.py adds to both screens' layouts (our layout installed: the native
//               整备 button is created only then; tests/room_layout_test.py checks both sides agree)
//   input       the keyboard and the XInput pad mapped onto room_menu.h Keys; the toggle's pad combination
//   gate        when the screen's own input is taken away (整备 / the 对战 pages up, and after they close until every
//               key is up: the key that closed them must not reach the room as a cancel)
//   screens     which room / HQ object the menu follows when two run at once (the old one retired until it ends)
//   texts       the native button's label and description in the HUD's language (hudtext.h)
//   catalog     the units 整备's support pages list, from the live support catalog (support_call.h)
//   crews       a crew's soldier kind (SupportVehicleCrewWeapon / SupportAircraftCrewWeapon), the colours it wears
//   preview     the crew / vehicle the 整备 support pages' 3D view shows (room_preview.h)
//   save        the changed units' (field, unit, value), zipped from room_menu.h CollectSave
// 2026-11-24 (docs/plan-2026-11-24-ui-rewrite.md): the old overlay (unit / row / status columns, chips, icons, help
// lines) and its loadout dialog box are gone with the menu they drew; 整备 (armory.h) draws the loadout screen.
// No game memory, no Windows API beyond the types loadout_file.h brings.
#pragma once
#include "hudtext.h"
#include "loadout_file.h"
#include "loadout_ini.h"
#include "room_menu.h"
#include "room_preview.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace crew::roomui {

using hudtext::Tx;

// The rows a page of the menu model holds (room_menu.h Menu::pageRows).
inline constexpr int kRows=12;
static_assert(kRows<=roommenu::kPageMost,"a page shows at most the snapshot's rows");
// tools/room_layout.py ROOT: at most 7 characters (the game's std::wstring holds it in place, room_ui.cpp Wide).
inline constexpr const wchar_t* kRootName=L"vcRoot";

// --- Input ---
// XInput button bits (XINPUT_GAMEPAD_*), the pad map.cpp MapPadButtons reads.
inline constexpr std::uint16_t kPadUp=0x0001,kPadDown=0x0002,kPadLeft=0x0004,kPadRight=0x0008,kPadBack=0x0020,
    kPadA=0x1000,kPadB=0x2000,kPadX=0x4000,kPadY=0x8000;
// What is held now: the keyboard's keys (the game window in front) and the first XInput pad.
struct Raw {
    bool foreground=false;
    bool toggleKey=false,up=false,down=false,left=false,right=false,enter=false,escape=false,insert=false,del=false,home=false;
    bool padOk=false;
    std::uint16_t pad=0;
};
// The menu's keys held now. `combo`: the pad buttons that together toggle (0: none); while all are held they toggle and
// do nothing else. Nothing is held while the game is not in front.
inline roommenu::Keys KeysDown(const Raw& r,std::uint16_t combo) noexcept {
    if(!r.foreground)return roommenu::Keys{};
    const std::uint16_t pad=r.padOk ? r.pad : 0;
    const bool comboHeld=combo && (pad&combo)==combo;
    const std::uint16_t b=comboHeld ? static_cast<std::uint16_t>(pad&~combo) : pad;
    return roommenu::Keys{r.toggleKey || comboHeld,r.up || (b&kPadUp),r.down || (b&kPadDown),r.left || (b&kPadLeft),
                          r.right || (b&kPadRight),r.enter || (b&kPadA),r.escape || (b&kPadB),r.insert || (b&kPadX),
                          r.del || (b&kPadY),r.home || (b&kPadBack)};
}
inline constexpr std::uint16_t kPadKnownBits=0xF3FF;
inline constexpr bool PadToggleAvailable(bool padOk,std::uint16_t combo) noexcept {
    return padOk && combo && !(combo & static_cast<std::uint16_t>(~kPadKnownBits));
}
// The pad combination's name for the hint ("L3+R3"); empty for none.
inline void PadComboName(std::uint16_t combo,wchar_t* out,std::size_t n) noexcept {
    struct Bit { std::uint16_t bit; const wchar_t* name; };
    static constexpr Bit kBits[]={{0x0100,L"LB"},{0x0200,L"RB"},{0x0040,L"L3"},{0x0080,L"R3"},{0x0020,L"Back"},{0x0010,L"Start"},
        {0x1000,L"A"},{0x2000,L"B"},{0x4000,L"X"},{0x8000,L"Y"},{0x0001,L"Up"},{0x0002,L"Down"},{0x0004,L"Left"},{0x0008,L"Right"}};
    if(!out || !n)return;
    out[0]=0;
    std::size_t used=0;
    for(const Bit& b:kBits) {
        if(!(combo&b.bit))continue;
        const int w=_snwprintf_s(out+used,n-used,_TRUNCATE,L"%ls%ls",used ? L"+" : L"",b.name);
        if(w<0)return;
        used+=static_cast<std::size_t>(w);
    }
}
// The game's own pad table (EDF+0x2136530, two slots 0x4A0 apart, a pointer per channel; the channel's +0x4C held,
// +0x4D suppressed: 0x1182A00 / 0x1188270): which channel is which XInput button (the writer reads wButtons into them).
struct PadChannel { std::uint32_t offset; std::uint16_t bit; };
inline constexpr PadChannel kPadChannels[]={
    {0x50,kPadUp},{0x58,kPadDown},{0x60,kPadLeft},{0x68,kPadRight},{0x70,kPadA},{0x78,kPadB},{0x80,kPadX},{0x88,kPadY},
    {0x90,kPadBack},{0x98,0x0010},{0xA0,0x0100},{0xA8,0x0200},{0xB0,0x0040},{0xB8,0x0080}};
inline constexpr std::uint32_t kPadSlotSize=0x4A0;
inline constexpr std::size_t kChannelHeld=0x4C,kChannelSuppressed=0x4D;
// The XInput-style button bits held on either slot; `channel(slot, offset)` gives that channel's object (nullptr none).
template<class Channel> std::uint16_t PadBits(Channel channel) noexcept {
    std::uint16_t bits=0;
    for(int slot=0;slot<2;++slot)
        for(const PadChannel& c:kPadChannels) {
            const unsigned char* ch=channel(slot,c.offset);
            if(ch && ch[kChannelHeld] && !ch[kChannelSuppressed])bits=static_cast<std::uint16_t>(bits|c.bit);
        }
    return bits;
}
inline bool AnyDown(const roommenu::Keys& k) noexcept {
    return k.toggle || k.up || k.down || k.left || k.right || k.confirm || k.cancel || k.add || k.remove || k.reset;
}

// A native menu button queues one open after the game's Click dispatch returns. The native weak-screen callback
// supplies the screen identity; covered / closing / replaced / disabled screens cannot leave a delayed request behind.
struct NativeEntry {
    bool attempted=false,ready=false,enabled=false,requested=false;
};
inline void NativeEntryInput(NativeEntry& e,bool canUse) noexcept {
    e.enabled=e.ready && canUse;
    if(!e.enabled)e.requested=false;
}
inline bool NativeEntryClick(NativeEntry& e,bool currentScreen) noexcept {
    if(!currentScreen || !e.enabled || e.requested)return false;
    e.requested=true;
    return true;
}
inline bool TakeNativeEntry(NativeEntry& e,bool currentScreen) noexcept {
    const bool take=currentScreen && e.enabled && e.requested;
    e.requested=false;
    return take;
}

// --- The input gate ---
// The screen's own input (ctx+8) is taken away while the menu is open, and, once it closes, until every menu key is up:
// the key that closed it (toggle, cancel = the room's own cancel, confirm on save) is still down then, and the room would
// read it the next frame (docs/room-ui-re.md §1).
struct Gate { bool latched=false; };
inline bool TakeInput(Gate& g,bool open,bool anyDown) noexcept {
    if(open){g.latched=true;return true;}
    if(g.latched && anyDown)return true;
    g.latched=false;
    return false;
}

// --- One frame's plan ---
// `hasInput`: the screen would read input this frame (0x8DAD00 with the real ctx: not covered by a sub-screen, a dialog
// or a transition). Covered, the menu is hidden and frozen (no key steps it: not even the toggle), its edits kept.
// The closed page draws nothing of ours (2026-10-25: the old entry line sat in HUiMainFrame's guide band, over EDF6Coop's
// label): the entry is the native menu button (vcLoad), its shortcut written into the screen's own MenuDescText on focus.
struct Plan { bool step,show; };
inline Plan PlanFrame(bool hasInput,bool open) noexcept {
    return Plan{hasInput,hasInput && open};
}

// --- The HQ opens its own mission select by itself (docs/worldmap-hq-open-re.md §5.1) ---
// Once per HQ screen object (`tried`: this object was tried already, opened or not; a new object, even at a dead one's
// address, is untried): Enabled && HqAutoMissionSelect (`on`), only while the HQ itself would read input (`hasInput`:
// 0x8DAD00 with the real ctx, nothing of the game's over it: the opening, a notice, a sub-screen, a transition) and never
// while the support loadout menu (F2) is up or its closing key still held (`menuBusy`): that frame is simply waited out.
inline bool AutoOpenDue(bool on,bool tried,bool hasInput,bool menuBusy) noexcept {
    return on && !tried && hasInput && !menuBusy;
}

// --- Which screen object the menu follows (room_ui.cpp Follow) ---
// Two screen objects of the hooked classes can run OnUpdate in the same frames: the HQ's pack switch (pack_switch.cpp)
// closes the HQ and the script builds the next HQ at once, and the old one keeps updating through its close (the
// 2026-11-27 run: ~0.27 s of frames from both after every switch). The menu follows one object; a newer one replaces it
// and the replaced one is retired: its frames pass straight through (nothing of the menu run, nothing created on it)
// until its destructor runs (Unretire). Without that every frame alternated between the two, forgetting the menu and
// creating the native 整备 button anew on each (dozens of orphan buttons left in the new HQ's list). When the followed
// object itself ends first (its destructor), every retired one may be followed again (ClearRetired): an older object
// still alive is then the only screen.
// The destructor hooks may run on another thread than the frames: the retired slots are atomic; the ring index is the
// frames' (the UI thread's) alone.
inline constexpr int kRetiredMost=4;
struct ScreenTrack {
    std::atomic<const void*> retired[kRetiredMost]{};
    int next=0;
};
inline bool IsRetired(const ScreenTrack& t,const void* p) noexcept {
    if(!p)return false;
    for(const auto& r:t.retired)if(r.load()==p)return true;
    return false;
}
inline void Retire(ScreenTrack& t,const void* p) noexcept {
    if(!p || IsRetired(t,p))return;
    t.retired[t.next].store(p);
    t.next=(t.next+1)%kRetiredMost;
}
// The object's destructor: true when it was retired (now forgotten: a new object at its address is a new screen).
inline bool Unretire(ScreenTrack& t,const void* p) noexcept {
    bool was=false;
    for(auto& r:t.retired) {
        const void* expect=p;
        if(p && r.compare_exchange_strong(expect,nullptr))was=true;
    }
    return was;
}
inline void ClearRetired(ScreenTrack& t) noexcept {
    for(auto& r:t.retired)r.store(nullptr);
}
enum class Track { Same, First, Replaced, Stale };
// `self`'s frame with `current` followed (nullptr: none, or the followed one ended and was forgotten). Replaced:
// `current` is retired here; the caller forgets the menu and follows `self`. Stale: a retired object, passed through.
inline Track TrackScreen(ScreenTrack& t,const void* current,const void* self) noexcept {
    if(self==current)return Track::Same;
    if(IsRetired(t,self))return Track::Stale;
    if(!current)return Track::First;
    Retire(t,current);
    return Track::Replaced;
}

// --- The switches over one screen frame: Enabled && RoomLoadout (`on`) and RoomPreSync ---
// Off: the screen's own update is chained with its own ctx and nothing else runs (no key read, no input taken, no
// pre-sync, no preview; only what the plugin drew on this screen earlier is taken back). The room pre-sync runs only on
// the online room's screen with both switches on; whenever it stops running (a switch turned off, the HQ, the screen
// gone) this machine's published keys are withdrawn exactly once (room_sync.h LeaveRoomSync).
struct Switches { bool syncing=false; };   // the pre-sync ran since the last withdrawal
struct SwitchPlan { bool pass,tick,leave; };
inline SwitchPlan PlanSwitches(Switches& s,bool on,bool preSync,bool room) noexcept {
    const bool tick=on && preSync && room;
    const SwitchPlan p{!on,tick,s.syncing && !tick};
    s.syncing=tick;
    return p;
}
// The screen object went (another room, the HQ, a mission): withdrawn when the pre-sync ran, once.
inline bool LeaveOnForget(Switches& s) noexcept {
    const bool leave=s.syncing;
    s.syncing=false;
    return leave;
}

// A text's font colour (HUiTextField +0x2B0, RGBA; room_ui.cpp SetText), the layout's default white, as the game's
// float4 (RGBA 0..1).
inline constexpr std::uint32_t kWhite=0xFFFFFFFFu;
inline void RgbaFloat(std::uint32_t rgba,float out[4]) noexcept {
    for(int i=0;i<4;++i)out[i]=static_cast<float>((rgba>>(24-8*i))&0xFF)/255.0f;
}

// --- Labels in the HUD's language ---
struct UnitText { const wchar_t* key; Tx text; };
inline constexpr UnitText kUnitTexts[]={
    {L"SQUAD",Tx::roomUnitSquad},{L"PLATOON",Tx::roomUnitPlatoon},{L"TRANSPORT_CREWED",Tx::roomUnitTransportCrewed},
    {L"TRUCK_CREWED",Tx::roomUnitTruckCrewed},{L"SQUAD_HELI",Tx::roomUnitSquadHeli},{L"PLATOON_HELI",Tx::roomUnitPlatoonHeli},
    {L"SQUAD_AIRDROP",Tx::roomUnitSquadAirdrop},{L"PLATOON_AIRDROP",Tx::roomUnitPlatoonAirdrop},
    {L"TANK_CREWED",Tx::roomUnitTankCrewed},{L"TANK_DELIVERY",Tx::roomUnitTankDelivery},{L"STRIKE",Tx::roomUnitStrike},
    {L"STRIKE_F",Tx::roomUnitStrikeFollow},{L"FIGHTER",Tx::roomUnitFighter},{L"FIGHTER_F",Tx::roomUnitFighterFollow},
    {L"INTERCEPTOR",Tx::roomUnitInterceptor},{L"INTERCEPTOR_F",Tx::roomUnitInterceptorFollow},
    {L"MULTIROLE",Tx::roomUnitMultirole},{L"MULTIROLE_F",Tx::roomUnitMultiroleFollow},
    {L"OWN_PFIGHTER",Tx::roomUnitOwnPFighter},{L"OWN_PSTRIKE",Tx::roomUnitOwnPStrike},{L"OWN_FIGHTER",Tx::roomUnitOwnFighter},
    {L"OWN_INTERCEPTOR",Tx::roomUnitOwnInterceptor},{L"OWN_MULTIROLE",Tx::roomUnitOwnMultirole},{L"OWN_MECH",Tx::roomUnitOwnMech}};
// The unit's label: the table's in the HUD's language, else `fallback` (the catalog's own name).
inline const wchar_t* UnitLabel(const wchar_t* key,const wchar_t* fallback) noexcept {
    if(key && !*key)return hudtext::Tr(Tx::roomUnitCrewDefault);     // SupportCrewColour
    if(key && !_wcsicmp(key,kCrewMissionKey))return hudtext::Tr(Tx::roomUnitCrewMission);
    for(const auto& u:kUnitTexts)if(key && !_wcsicmp(u.key,key))return hudtext::Tr(u.text);
    return fallback ? fallback : key;
}

// --- The lines ---
inline constexpr std::size_t kLineMost=200;
namespace detail {
template<class... A> void Put(wchar_t* out,std::size_t n,const wchar_t* format,A... args) noexcept {
    if(!out || !n)return;
    if(_snwprintf_s(out,n,_TRUNCATE,format,args...)<0 && n)out[n-1]=0;
}
}  // namespace detail
inline const wchar_t* Title(bool dirty) noexcept { return hudtext::Tr(dirty ? Tx::roomTitleUnsaved : Tx::roomTitle); }
inline void EntryLine(const wchar_t* toggleName,wchar_t* out,std::size_t n) noexcept {
    detail::Put(out,n,hudtext::Tr(Tx::roomEntry),toggleName && *toggleName ? toggleName : L"-");
}
// The generator's state as a line (armory.cpp FillStatus, the 整备 status strip); false when there is nothing to say (idle).
inline bool GenerateLine(const GenerateStatus& s,wchar_t* out,std::size_t n) noexcept {
    if(!out || !n)return false;
    out[0]=0;
    switch(s.state) {
    case GenerateState::running: detail::Put(out,n,L"%ls",hudtext::Tr(Tx::roomGenRunning)); return true;
    case GenerateState::ok: detail::Put(out,n,hudtext::Tr(Tx::roomGenOk),s.message); return true;
    case GenerateState::failed: detail::Put(out,n,hudtext::Tr(Tx::roomGenFailed),s.message); return true;
    case GenerateState::missing: detail::Put(out,n,L"%ls",hudtext::Tr(Tx::roomGenMissing)); return true;
    default: return false;
    }
}

// --- The units: the live catalog's entries a preset, a loadout or a crew's colours belong to ---
using KeyOf=const wchar_t*(*)(int index) noexcept;
using SeatsOf=int(*)(int index) noexcept;
using NameOf=const wchar_t*(*)(int index) noexcept;
// Every catalog entry with seats (a squad: SupportPreset_<key>), a vehicle key (SupportVehicle_<key>) or a crew
// (support_loadout.h HasCrew: SupportCrewColour_<key>), in catalog order, labelled in the HUD's language; then the
// default every crew wears (key "") and the mission vehicles' support crews (kCrewMissionKey), as room_menu.h
// kDefaultUnits ends. Returns how many (at most `capacity`).
inline int CatalogUnits(int count,KeyOf keyOf,SeatsOf seatsOf,NameOf nameOf,roommenu::UnitSpec* out,int capacity) noexcept {
    int n=0;
    for(int i=0;keyOf && i<count && n<capacity;++i) {
        const wchar_t* key=keyOf(i);
        if(!key || !*key)continue;
        const int seats=seatsOf ? seatsOf(i) : 0;
        const bool crew=HasCrew(key);
        if(seats<=0 && VehicleBodyOfKey(key)==VehicleBody::none && !crew)continue;
        out[n++]=roommenu::UnitSpec{key,UnitLabel(key,nameOf ? nameOf(i) : key),seats>0 ? seats : 0,crew};
    }
    // The vehicles this player summons with a vehicle request (support_loadout.h kPlayerVehicles): not catalog entries.
    for(const auto& v:kPlayerVehicles)
        if(n<capacity)out[n++]=roommenu::UnitSpec{v.key,UnitLabel(v.key,v.key),0,false};
    for(const wchar_t* key:{L"",kCrewMissionKey})
        if(n<capacity)out[n++]=roommenu::UnitSpec{key,UnitLabel(key,key),0,true};
    return n;
}
// room_menu.h kDefaultUnits (the menu without a live catalog) labelled in the HUD's language. Returns how many.
inline int DefaultUnits(roommenu::UnitSpec* out,int capacity) noexcept {
    int n=0;
    for(int i=0;i<roommenu::kDefaultUnitCount && n<capacity;++i) {
        out[n]=roommenu::kDefaultUnits[i];
        out[n].label=UnitLabel(out[n].key,out[n].label);
        ++n;
    }
    return n;
}

// --- Crews: the soldier a crew row stands for and the colours it wears ---
// What the real crews carry (support_config.h SupportVehicleCrewWeapon / SupportAircraftCrewWeapon).
struct CrewKinds { SupportWeapon ground=SupportWeapon::rifle,air=SupportWeapon::rifle; };
// The crew of `key`: a ground vehicle's (support_loadout.h IsGroundCrewKey) the ground kind, every other entry's the air
// kind; the mission vehicles' support crews are always riflemen (mission_crew_support.cpp: N601_COMMON_RANGER_AF[_LEADER]);
// the default's row ("" : every crew) shows the ground kind.
inline SupportWeapon CrewKindOf(const wchar_t* key,const CrewKinds& k) noexcept {
    if(key && !_wcsicmp(key,kCrewMissionKey))return SupportWeapon::rifle;
    if(!key || !*key || IsGroundCrewKey(key))return k.ground;
    return k.air;
}
// The default every crew without its own colours wears (the menu's working copy of SupportCrewColour); not set: stock.
inline CrewColour DefaultCrew(const roommenu::Menu& m) noexcept {
    for(int i=0;i<m.unitCount;++i)
        if(roommenu::IsCrewUnit(m.unit[i]) && m.unit[i].key && !*m.unit[i].key)return m.unit[i].crew;
    return CrewColour{};
}

// --- The preview (整备's support pages, support_pages.h): a crew row's crew (a member, not a leader), a vehicle's rows
// its vehicle (room_preview.h PreviewSpec::body) ---
inline roompreview::PreviewSpec VehiclePreview(const roommenu::Unit& u) noexcept {
    roompreview::PreviewSpec p{};
    p.body=u.body;
    if(u.vehicle.On() && u.vehicle.body==u.body)p.load=u.vehicle;
    return p;
}
inline roompreview::PreviewSpec CrewPreview(const roommenu::Menu& m,const roommenu::Unit& u,const CrewKinds& kinds) noexcept {
    return roompreview::PreviewSpec{CrewKindOf(u.key,kinds),false,CrewLookOf(u.crew,u.key && *u.key ? DefaultCrew(m) : CrewColour{})};
}

// --- Saving: what loadout_store.h SaveSupportLoadout is called with, one per line of room_menu.h CollectSave ---
// `unit` its ini key's unit name; `unitIndex` the menu's unit it came from (MarkItemSaved).
struct SaveItem { LoadoutField field; const wchar_t* unit; const wchar_t* value; int unitIndex; };
// `set`: CollectSave's (true) for `m`; one item per line, in the same order (each changed unit's preset / loadout, then
// its crew's colours). -1 when they do not pair (a line's key is not the unit's key for that field).
inline int SaveItems(const roommenu::Menu& m,const roommenu::SaveSet& set,SaveItem* out,int capacity) noexcept {
    int n=0;
    const auto add=[&](LoadoutField field,const roommenu::Unit& u,int index) noexcept {
        if(n>=set.count || n>=capacity || LoadoutKey(field,u.key)!=set.line[n].key)return false;
        out[n]=SaveItem{field,u.key,set.line[n].value,index};
        ++n;
        return true;
    };
    for(int i=0;i<m.unitCount;++i) {
        const roommenu::Unit& u=m.unit[i];
        if(roommenu::PartChanged(u) && !add(roommenu::IsSquad(u) ? LoadoutField::preset : LoadoutField::vehicle,u,i))return -1;
        if(roommenu::CrewChanged(u) && !add(LoadoutField::crew,u,i))return -1;
    }
    return n==set.count ? n : -1;
}
// One item written: that part of its unit now holds what the ini holds (the rest of the unit stays edited until its
// own item is written).
inline void MarkItemSaved(roommenu::Menu& m,const SaveItem& item) noexcept {
    if(item.unitIndex<0 || item.unitIndex>=m.unitCount)return;
    roommenu::Unit& u=m.unit[item.unitIndex];
    if(item.field==LoadoutField::crew){u.crewLoaded=u.crew;u.crewRefused=false;return;}
    u.refused=false;
    u.squadLoaded=u.squad;u.vehicleLoaded=u.vehicle;
}

}  // namespace crew::roomui
