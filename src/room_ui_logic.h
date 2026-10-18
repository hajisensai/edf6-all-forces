// The room / HQ support loadout screen's pure side (docs/feature-2026-10-12-room-loadout-menu.md stage 4): what
// src/room_ui.cpp decides each frame without touching the game, offline-testable (tests/room_ui_test.cpp):
//   names       the layout elements tools/room_layout.py adds (tests/room_layout_test.py checks both sides agree)
//   input       the keyboard and the XInput pad mapped onto room_menu.h Keys; the toggle's pad combination
//   gate        when the screen's own input is taken away (the menu open, and after it closes until every menu key is up:
//               the key that closed it must not reach the room as a cancel)
//   looks       per row: which of text / chips / icon / note show, the chips' RGBA, the icon's path (docs/room-ui-re.md §3);
//               the panel and the chips are the layout's texture skins showing kWhiteTexture tinted (nothing without the
//               plugin: tools/room_layout.py)
//   crews       a crew row's soldier kind (SupportVehicleCrewWeapon / SupportAircraftCrewWeapon), the colours it wears
//   texts       every line in the HUD's language (hudtext.h)
//   catalog     the units the menu lists, from the live support catalog (support_call.h)
//   preview     which soldier the 3D view shows (room_preview.h)
//   save        the changed units' (field, unit, value), zipped from room_menu.h CollectSave
//   status      the status lines' order
// No game memory, no Windows API beyond the types loadout_file.h brings.
#pragma once
#include "hudtext.h"
#include "loadout_file.h"
#include "loadout_ini.h"
#include "room_menu.h"
#include "room_preview.h"
#include "room_sync.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace crew::roomui {

using hudtext::Tx;
using roommenu::ViewRow;
using roommenu::RowText;

// --- The layout's elements (tools/room_layout.py ROWS, STATUS_LINES, the names) ---
inline constexpr int kRows=12;          // rows a column shows: room_menu.h Menu::pageRows
inline constexpr int kStatusLines=4;
static_assert(kRows<=roommenu::kPageMost,"a column shows at most the snapshot's rows");
// Every name is at most 7 characters: the game's std::wstring holds it in place (no heap pointer to hand over).
inline constexpr std::size_t kNameMost=8;
inline constexpr const wchar_t* kRootName=L"vcRoot";
inline constexpr const wchar_t* kBackName=L"vcBack";
inline constexpr const wchar_t* kHeadName=L"vcHead";
inline constexpr const wchar_t* kHintName=L"vcHint";
inline constexpr const wchar_t* kHelp2Name=L"vcHelp2";
inline constexpr const wchar_t* kHelp3Name=L"vcHelp3";
inline constexpr const wchar_t* kEntryName=L"vcEntry";
// The plain white texture the panel and the chips show, tinted (tools/room_layout.py WHITE: written to Mods/UI by the
// installer; Root.cpk has no plain one). Without the plugin nothing binds it: the skins draw nothing.
inline constexpr const wchar_t* kWhiteTexture=L"app:/UI/EDF6VC_WHITE.dds";
inline constexpr std::uint32_t kPanelColour=0x0C1622D8u;   // the panel: dark blue-grey, 85 % opaque (RGBA)
inline constexpr std::uint32_t kIconColour=0xFFFFFFFFu;    // an icon as drawn
enum class Part : wchar_t { box=0, text=L'T', primary=L'A', secondary=L'B', icon=L'I', note=L'N' };
namespace detail {
inline bool Name(wchar_t (&out)[kNameMost],wchar_t kind,int i,int digits,wchar_t part) noexcept {
    out[0]=0;
    if(i<0 || (digits==2 && i>99) || (digits==1 && i>9))return false;
    std::size_t n=0;
    out[n++]=L'v';out[n++]=L'c';out[n++]=kind;
    if(digits==2)out[n++]=static_cast<wchar_t>(L'0'+i/10);
    out[n++]=static_cast<wchar_t>(L'0'+i%10);
    if(part)out[n++]=part;
    out[n]=0;
    return true;
}
}  // namespace detail
// vcU00..: the unit column's lines.
inline bool UnitName(int i,wchar_t (&out)[kNameMost]) noexcept { return i<kRows && detail::Name(out,L'U',i,2,0); }
// vcR00 (the row's box), vcR00T / A / B / I / N its parts.
inline bool RowName(int i,Part p,wchar_t (&out)[kNameMost]) noexcept {
    return i<kRows && detail::Name(out,L'R',i,2,static_cast<wchar_t>(p));
}
// vcS0..: the status lines.
inline bool StatusName(int i,wchar_t (&out)[kNameMost]) noexcept { return i<kStatusLines && detail::Name(out,L'S',i,1,0); }

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
struct Plan { bool step,show,entry; };
// The closed page advertises its real toggle without stealing focus. A covered page, an unavailable binding, or the
// closing key still held hides the entry: the prompt must never promise an action that the page cannot take now.
inline Plan PlanFrame(bool hasInput,bool open,bool canToggle=true,bool inputReleased=true) noexcept {
    return Plan{hasInput,hasInput && open,hasInput && !open && canToggle && inputReleased};
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

// --- Icons (docs/room-ui-re.md §3: the AI weapons' own WeaponIcon; the vehicle stores borrow players' icons) ---
inline constexpr const wchar_t* kKindIcons[kSupportWeaponCount]={
    L"app:/weapon/icon/AF14.dds",L"app:/weapon/icon/flamethrower.dds",L"app:/weapon/icon/stingray.dds",
    L"app:/weapon/icon/buffalo.dds",L"app:/weapon/icon/srMMF.dds",
    L"app:/weapon/icon/lazerlance01.dds",L"app:/weapon/icon/LAZR.dds",L"app:/weapon/icon/monster01.dds",
    L"app:/weapon/icon/izuna.dds",L"app:/weapon/icon/thunderbow.dds",
    L"app:/weapon/icon/canon.dds",L"app:/weapon/icon/canon.dds",L"app:/weapon/icon/pilebunker.dds",
    L"app:/weapon/icon/canon.dds"};
// kLoadStores codes 1..10.
inline constexpr const wchar_t* kStoreIcons[kLoadStoreCount]={
    L"app:/weapon/icon/s_missile_617_falcon01.dds",L"app:/weapon/icon/s_missile_emeraude02.dds",
    L"app:/weapon/icon/missile_titania.dds",L"app:/weapon/icon/s_missile_airtortoise02.dds",
    L"app:/weapon/icon/missile_arm.dds",L"app:/weapon/icon/pd609_drone_bombbig.dds",L"app:/weapon/icon/hornet01.dds",
    L"app:/weapon/icon/h_cannon_apfsds01.dds",L"app:/weapon/icon/tank01.dds",L"app:/weapon/icon/stingray.dds"};
inline const wchar_t* KindIcon(int kind) noexcept { return kind>=0 && kind<kSupportWeaponCount ? kKindIcons[kind] : nullptr; }
inline const wchar_t* StoreIcon(int code) noexcept { return code>=1 && code<=kLoadStoreCount ? kStoreIcons[code-1] : nullptr; }

// --- A row's looks ---
struct Look {
    bool shown=false;                       // the row's box
    bool primary=false,secondary=false;     // the chips (a coloured soldier's; a stock colour: no chip)
    std::uint32_t primaryRgba=0,secondaryRgba=0;
    const wchar_t* icon=nullptr;            // the texture, nullptr: the icon hidden
};
inline Look LookOf(const ViewRow* r) noexcept {
    Look l;
    if(!r)return l;
    l.shown=true;
    if(r->text==RowText::soldier || r->text==RowText::crew) {
        l.primary=r->primary!=0;l.secondary=r->secondary!=0;
        l.primaryRgba=r->primary;l.secondaryRgba=r->secondary;
        l.icon=KindIcon(r->kind);
    } else if(r->text==RowText::pylon)l.icon=StoreIcon(r->store);
    else if(r->text==RowText::gunHE)l.icon=StoreIcon(9);
    else if(r->text==RowText::gunAP)l.icon=StoreIcon(8);
    return l;
}
// A text's colour (HUiTextField +0x2B0 font colour, RGBA): the cursor's row yellow, a unit whose ini value was refused
// orange, a warning red, the rest white (the layout's default).
inline constexpr std::uint32_t kWhite=0xFFFFFFFFu,kCursorColour=0xFFD84CFFu,kRefusedColour=0xFF9A40FFu,kWarnColour=0xFF6A5AFFu;
inline std::uint32_t RowColour(const ViewRow* r) noexcept {
    if(!r)return kWhite;
    if(r->cursor)return kCursorColour;
    return r->refused && !r->changed ? kRefusedColour : kWhite;
}
inline std::uint32_t StatusColour(bool warn) noexcept { return warn ? kWarnColour : kWhite; }
// A chip's colour as the game's float4 (RGBA 0..1).
inline void ChipColour(std::uint32_t rgba,float out[4]) noexcept {
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
    {L"MULTIROLE",Tx::roomUnitMultirole},{L"MULTIROLE_F",Tx::roomUnitMultiroleFollow}};
// The unit's label: the table's in the HUD's language, else `fallback` (the catalog's own name).
inline const wchar_t* UnitLabel(const wchar_t* key,const wchar_t* fallback) noexcept {
    if(key && !*key)return hudtext::Tr(Tx::roomUnitCrewDefault);     // SupportCrewColour
    if(key && !_wcsicmp(key,kCrewMissionKey))return hudtext::Tr(Tx::roomUnitCrewMission);
    for(const auto& u:kUnitTexts)if(key && !_wcsicmp(u.key,key))return hudtext::Tr(u.text);
    return fallback ? fallback : key;
}
inline constexpr Tx kKindTexts[kSupportWeaponCount]={
    Tx::roomKindRifle,Tx::roomKindFlame,Tx::roomKindRocket,Tx::roomKindShotgun,Tx::roomKindSniper,
    Tx::roomKindLance,Tx::roomKindLaser,Tx::roomKindMonster,Tx::roomKindIzuna,Tx::roomKindThunderBow,
    Tx::roomKindCannon,Tx::roomKindMidCannon,Tx::roomKindPileBanker,Tx::roomKindShotgunFencer};
inline constexpr Tx kStoreTexts[kLoadStoreCount]={
    Tx::roomStoreAamS,Tx::roomStoreAamM,Tx::roomStoreAamL,Tx::roomStoreAgm,Tx::roomStoreAgmL,Tx::roomStoreMk82,
    Tx::roomStoreRkt,Tx::roomStoreAp,Tx::roomStoreHe,Tx::roomStoreGlm};
inline const wchar_t* KindLabel(int kind) noexcept { return kind>=0 && kind<kSupportWeaponCount ? hudtext::Tr(kKindTexts[kind]) : L"?"; }
inline const wchar_t* StoreLabel(int code) noexcept { return code>=1 && code<=kLoadStoreCount ? hudtext::Tr(kStoreTexts[code-1]) : L"?"; }

// --- The lines ---
inline constexpr std::size_t kLineMost=200;
inline constexpr const wchar_t* kCursor=L"> ";
inline constexpr const wchar_t* kNoCursor=L"   ";
// Numbers and symbols, the same in every language (hudtext.h: not translated).
inline constexpr const wchar_t* kNumbered=L"%d. %ls";
inline constexpr const wchar_t* kRounds=L"x%d";
inline constexpr const wchar_t* kFieldRounds=L"< x%d >";
inline constexpr const wchar_t* kFieldGun=L"< HE / AP >";
inline constexpr const wchar_t* kPackSuffix=L" + %ls";
namespace detail {
template<class... A> void Put(wchar_t* out,std::size_t n,const wchar_t* format,A... args) noexcept {
    if(!out || !n)return;
    if(_snwprintf_s(out,n,_TRUNCATE,format,args...)<0 && n)out[n-1]=0;
}
}  // namespace detail
// The unit column's line: the cursor, the label, edited / refused.
// A crew unit of a catalog entry (not the default's or the mission crews': their labels say it) reads "<label>: crew".
inline bool CrewOfEntry(const ViewRow& r) noexcept {
    return r.text==RowText::crewUnit && r.key && *r.key && _wcsicmp(r.key,kCrewMissionKey)!=0;
}
inline void UnitLine(const ViewRow& r,wchar_t* out,std::size_t n) noexcept {
    wchar_t label[kLineMost],named[kLineMost];
    detail::Put(named,kLineMost,CrewOfEntry(r) ? hudtext::Tr(Tx::roomUnitCrewOf) : L"%ls",r.label ? r.label : L"?");
    if(r.text==RowText::save)detail::Put(label,kLineMost,L"%ls",hudtext::Tr(Tx::roomSave));
    else if(r.refused && !r.changed)detail::Put(label,kLineMost,hudtext::Tr(Tx::roomUnitRefused),named);
    else if(r.changed)detail::Put(label,kLineMost,hudtext::Tr(Tx::roomUnitEdited),named);
    else detail::Put(label,kLineMost,L"%ls",named);
    detail::Put(out,n,L"%ls%ls",r.cursor ? kCursor : kNoCursor,label);
}
// The row column's text: soldier / gun / pylon / the stock rows.
inline void RowLine(const ViewRow& r,wchar_t* out,std::size_t n) noexcept {
    wchar_t body[kLineMost];
    switch(r.text) {
    case RowText::soldier:
        detail::Put(body,kLineMost,r.leader ? hudtext::Tr(Tx::roomNumberedLeader) : kNumbered,r.index+1,KindLabel(r.kind));
        if(const PackWeapon* pack=r.pack ? PackOf(r.pack) : nullptr) {   // its backpack weapon (the game's English name)
            const std::size_t used=std::wcslen(body);
            detail::Put(body+used,kLineMost-used,kPackSuffix,pack->label);
        } else if(r.editing && r.field==roommenu::kPackField) {
            const std::size_t used=std::wcslen(body);
            detail::Put(body+used,kLineMost-used,kPackSuffix,hudtext::Tr(Tx::roomPackNone));
        }
        break;
    case RowText::pylon: detail::Put(body,kLineMost,kNumbered,r.index+1,StoreLabel(r.store)); break;
    case RowText::gunHE: detail::Put(body,kLineMost,L"%ls",hudtext::Tr(Tx::roomGunHe)); break;
    case RowText::gunAP: detail::Put(body,kLineMost,L"%ls",hudtext::Tr(Tx::roomGunAp)); break;
    case RowText::guns: detail::Put(body,kLineMost,L"%ls",hudtext::Tr(Tx::roomGuns)); break;
    case RowText::ownLoad: detail::Put(body,kLineMost,L"%ls",hudtext::Tr(Tx::roomOwnLoad)); break;
    case RowText::stockVehicle: detail::Put(body,kLineMost,L"%ls",hudtext::Tr(Tx::roomStockVehicle)); break;
    case RowText::crew: detail::Put(body,kLineMost,hudtext::Tr(Tx::roomCrewRow),KindLabel(r.kind)); break;
    default: body[0]=0; break;
    }
    detail::Put(out,n,L"%ls%ls",r.cursor ? kCursor : kNoCursor,body);
}
// The note beside a row: the field being changed, else a pylon's rounds, a soldier's or a crew's stock colours, a crew
// wearing the default's; empty for none.
inline void NoteLine(const ViewRow& r,wchar_t* out,std::size_t n) noexcept {
    if(!out || !n)return;
    out[0]=0;
    if(r.editing && r.text==RowText::soldier) {
        const Tx t=r.field==0 ? Tx::roomFieldKind : r.field==1 ? Tx::roomFieldPrimary : r.field==2 ? Tx::roomFieldSecondary :
                   Tx::roomFieldPack;
        detail::Put(out,n,L"%ls",hudtext::Tr(t));
    } else if(r.editing && r.text==RowText::crew) {
        detail::Put(out,n,L"%ls",hudtext::Tr(r.field==0 ? Tx::roomFieldPrimary : Tx::roomFieldSecondary));
    } else if(r.text==RowText::crew) {
        const bool ownDefault=!r.key || !*r.key;   // the default's own row: not set is stock
        if(r.inherit && !ownDefault)detail::Put(out,n,L"%ls",hudtext::Tr(Tx::roomCrewDefault));
        else if(!r.primary && !r.secondary)detail::Put(out,n,L"%ls",hudtext::Tr(Tx::roomStockColour));
    } else if(r.editing && r.text==RowText::pylon) {
        if(r.field==1)detail::Put(out,n,kFieldRounds,r.rounds);
        else detail::Put(out,n,L"%ls",hudtext::Tr(Tx::roomFieldStore));
    } else if(r.editing && (r.text==RowText::gunHE || r.text==RowText::gunAP))detail::Put(out,n,L"%ls",kFieldGun);
    else if(r.text==RowText::pylon)detail::Put(out,n,kRounds,r.rounds);
    else if(r.text==RowText::soldier && !r.primary && !r.secondary)detail::Put(out,n,L"%ls",hudtext::Tr(Tx::roomStockColour));
}
inline const wchar_t* Title(bool dirty) noexcept { return hudtext::Tr(dirty ? Tx::roomTitleUnsaved : Tx::roomTitle); }
inline void HintLine(const wchar_t* toggleName,wchar_t* out,std::size_t n) noexcept {
    detail::Put(out,n,hudtext::Tr(Tx::roomHint),toggleName && *toggleName ? toggleName : L"-");
}
// Three bounded, contextual lines instead of the old all-controls sentence. The second line explains the operation
// available at this level; the last always says that toggling discards edits (save has its own explicit row).
inline void HelpLine(roommenu::Level level,int line,const wchar_t* toggleName,wchar_t* out,std::size_t n) noexcept {
    if(line==0)detail::Put(out,n,L"%ls",hudtext::Tr(level==roommenu::Level::fields ? Tx::roomHintFields : Tx::roomHintNavigate));
    else if(line==1)detail::Put(out,n,L"%ls",hudtext::Tr(level==roommenu::Level::units ? Tx::roomHintSave :
                                                      level==roommenu::Level::rows ? Tx::roomHintTools : Tx::roomHintFinish));
    else if(line==2)detail::Put(out,n,hudtext::Tr(Tx::roomHintClose),toggleName && *toggleName ? toggleName : L"-");
    else if(out && n)out[0]=0;
}
inline void EntryLine(const wchar_t* toggleName,wchar_t* out,std::size_t n) noexcept {
    detail::Put(out,n,hudtext::Tr(Tx::roomEntry),toggleName && *toggleName ? toggleName : L"-");
}
// The generator's state as a line; false when there is nothing to say (idle).
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

// --- The status lines, in order: the client's note, the last save's result, the generator, the room's sync ---
struct Status {
    int count=0;
    wchar_t text[kStatusLines][kLineMost]{};
    bool warn[kStatusLines]{};
    void Add(const wchar_t* line,bool isWarn) noexcept {
        if(count>=kStatusLines || !line || !*line)return;
        detail::Put(text[count],kLineMost,L"%ls",line);
        warn[count++]=isWarn;
    }
};
inline Status ComposeStatus(bool clientNote,const wchar_t* saveNote,bool saveWarn,const GenerateStatus* gen,
                            const roomsync::SyncLine* sync,int syncCount) noexcept {
    Status s;
    if(clientNote)s.Add(hudtext::Tr(Tx::roomClientNote),false);
    s.Add(saveNote,saveWarn);
    wchar_t line[kLineMost];
    if(gen && GenerateLine(*gen,line,kLineMost))
        s.Add(line,gen->state==GenerateState::failed || gen->state==GenerateState::missing);
    for(int i=0;sync && i<syncCount;++i)s.Add(sync[i].text,sync[i].warn);
    return s;
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
// The snapshot's crew rows dressed for drawing: the crew's kind, and a row wearing the default its colours (the chips
// show what the crew will wear; `inherit` stays set, the note says so).
inline void DressCrewRows(roommenu::View& v,const roommenu::Menu& m,const CrewKinds& k) noexcept {
    const SupportLook fallback=CrewLookOf(DefaultCrew(m),CrewColour{});
    for(int i=0;i<v.rows.count;++i) {
        ViewRow& r=v.rows.row[i];
        if(r.text!=RowText::crew)continue;
        const SupportWeapon kind=CrewKindOf(r.key,k);
        r.kind=static_cast<int>(kind);r.soldierClass=SupportSoldierClass(kind);
        if(r.inherit && r.key && *r.key){r.primary=roommenu::Rgba(fallback.primary);r.secondary=roommenu::Rgba(fallback.secondary);}
    }
}

// --- The preview: the soldier under the cursor, in a squad's rows / fields; a crew row's crew (a member, not a leader) ---
inline bool PreviewFor(const roommenu::Menu& m,roompreview::PreviewSpec* out,const CrewKinds& kinds=CrewKinds{}) noexcept {
    if(!m.open || m.level==roommenu::Level::units || m.unitAt<0 || m.unitAt>=m.unitCount)return false;
    const roommenu::Unit& u=m.unit[m.unitAt];
    if(m.rowAt>=0 && m.rowAt==roommenu::CrewRow(u)) {
        if(out)*out=roompreview::PreviewSpec{CrewKindOf(u.key,kinds),false,CrewLookOf(u.crew,u.key && *u.key ? DefaultCrew(m) : CrewColour{})};
        return true;
    }
    if(!roommenu::IsSquad(u) || m.rowAt<0 || m.rowAt>=u.squad.count)return false;
    if(out)*out=roompreview::PreviewSpec{u.squad.kind[m.rowAt],m.rowAt%4==0,u.squad.look[m.rowAt]};
    return true;
}
inline bool SamePreview(const roompreview::PreviewSpec& a,const roompreview::PreviewSpec& b) noexcept {
    return a.kind==b.kind && a.leader==b.leader && a.look==b.look;
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
