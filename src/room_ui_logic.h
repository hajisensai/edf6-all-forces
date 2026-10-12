// The room / HQ support loadout screen's pure side (docs/feature-2026-10-12-room-loadout-menu.md stage 4): what
// src/room_ui.cpp decides each frame without touching the game, offline-testable (tests/room_ui_test.cpp):
//   names       the layout elements tools/room_layout.py adds (tests/room_layout_test.py checks both sides agree)
//   input       the keyboard and the XInput pad mapped onto room_menu.h Keys; the toggle's pad combination
//   gate        when the screen's own input is taken away (the menu open, and after it closes until every menu key is up:
//               the key that closed it must not reach the room as a cancel)
//   looks       per row: which of text / chips / icon / note show, the chips' RGBA, the icon's path (docs/room-ui-re.md §3)
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
inline constexpr int kRows=14;          // rows a column shows: room_menu.h Menu::pageRows
inline constexpr int kStatusLines=4;
static_assert(kRows<=roommenu::kPageMost,"a column shows at most the snapshot's rows");
// Every name is at most 7 characters: the game's std::wstring holds it in place (no heap pointer to hand over).
inline constexpr std::size_t kNameMost=8;
inline constexpr const wchar_t* kRootName=L"vcRoot";
inline constexpr const wchar_t* kBackName=L"vcBack";
inline constexpr const wchar_t* kHeadName=L"vcHead";
inline constexpr const wchar_t* kHintName=L"vcHint";
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
struct Plan { bool step,show; };
inline Plan PlanFrame(bool hasInput,bool open) noexcept { return Plan{hasInput,hasInput && open}; }

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
    if(r->text==RowText::soldier) {
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
namespace detail {
template<class... A> void Put(wchar_t* out,std::size_t n,const wchar_t* format,A... args) noexcept {
    if(!out || !n)return;
    if(_snwprintf_s(out,n,_TRUNCATE,format,args...)<0 && n)out[n-1]=0;
}
}  // namespace detail
// The unit column's line: the cursor, the label, edited / refused.
inline void UnitLine(const ViewRow& r,wchar_t* out,std::size_t n) noexcept {
    wchar_t label[kLineMost];
    if(r.text==RowText::save)detail::Put(label,kLineMost,L"%ls",hudtext::Tr(Tx::roomSave));
    else if(r.refused && !r.changed)detail::Put(label,kLineMost,hudtext::Tr(Tx::roomUnitRefused),r.label ? r.label : L"?");
    else if(r.changed)detail::Put(label,kLineMost,hudtext::Tr(Tx::roomUnitEdited),r.label ? r.label : L"?");
    else detail::Put(label,kLineMost,L"%ls",r.label ? r.label : L"?");
    detail::Put(out,n,L"%ls%ls",r.cursor ? kCursor : kNoCursor,label);
}
// The row column's text: soldier / gun / pylon / the stock rows.
inline void RowLine(const ViewRow& r,wchar_t* out,std::size_t n) noexcept {
    wchar_t body[kLineMost];
    switch(r.text) {
    case RowText::soldier:
        detail::Put(body,kLineMost,r.leader ? hudtext::Tr(Tx::roomNumberedLeader) : kNumbered,r.index+1,KindLabel(r.kind));
        break;
    case RowText::pylon: detail::Put(body,kLineMost,kNumbered,r.index+1,StoreLabel(r.store)); break;
    case RowText::gunHE: detail::Put(body,kLineMost,L"%ls",hudtext::Tr(Tx::roomGunHe)); break;
    case RowText::gunAP: detail::Put(body,kLineMost,L"%ls",hudtext::Tr(Tx::roomGunAp)); break;
    case RowText::guns: detail::Put(body,kLineMost,L"%ls",hudtext::Tr(Tx::roomGuns)); break;
    case RowText::ownLoad: detail::Put(body,kLineMost,L"%ls",hudtext::Tr(Tx::roomOwnLoad)); break;
    case RowText::stockVehicle: detail::Put(body,kLineMost,L"%ls",hudtext::Tr(Tx::roomStockVehicle)); break;
    default: body[0]=0; break;
    }
    detail::Put(out,n,L"%ls%ls",r.cursor ? kCursor : kNoCursor,body);
}
// The note beside a row: the field being changed, else a pylon's rounds or a soldier's stock colours; empty for none.
inline void NoteLine(const ViewRow& r,wchar_t* out,std::size_t n) noexcept {
    if(!out || !n)return;
    out[0]=0;
    if(r.editing && r.text==RowText::soldier) {
        const Tx t=r.field==0 ? Tx::roomFieldKind : r.field==1 ? Tx::roomFieldPrimary : Tx::roomFieldSecondary;
        detail::Put(out,n,L"%ls",hudtext::Tr(t));
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

// --- The units: the live catalog's entries a preset or a loadout belongs to ---
using KeyOf=const wchar_t*(*)(int index) noexcept;
using SeatsOf=int(*)(int index) noexcept;
using NameOf=const wchar_t*(*)(int index) noexcept;
// Every catalog entry with seats (a squad: SupportPreset_<key>) or a vehicle key (SupportVehicle_<key>), in catalog
// order, labelled in the HUD's language. Returns how many (at most `capacity`).
inline int CatalogUnits(int count,KeyOf keyOf,SeatsOf seatsOf,NameOf nameOf,roommenu::UnitSpec* out,int capacity) noexcept {
    int n=0;
    for(int i=0;keyOf && i<count && n<capacity;++i) {
        const wchar_t* key=keyOf(i);
        if(!key)continue;
        const int seats=seatsOf ? seatsOf(i) : 0;
        if(seats<=0 && VehicleBodyOfKey(key)==VehicleBody::none)continue;
        out[n++]=roommenu::UnitSpec{key,UnitLabel(key,nameOf ? nameOf(i) : key),seats>0 ? seats : 0};
    }
    return n;
}

// --- The preview: the soldier under the cursor, in a squad's rows / fields ---
inline bool PreviewFor(const roommenu::Menu& m,roompreview::PreviewSpec* out) noexcept {
    if(!m.open || m.level==roommenu::Level::units || m.unitAt<0 || m.unitAt>=m.unitCount)return false;
    const roommenu::Unit& u=m.unit[m.unitAt];
    if(!roommenu::IsSquad(u) || m.rowAt<0 || m.rowAt>=u.squad.count)return false;
    if(out)*out=roompreview::PreviewSpec{u.squad.kind[m.rowAt],m.rowAt%4==0,u.squad.look[m.rowAt]};
    return true;
}
inline bool SamePreview(const roompreview::PreviewSpec& a,const roompreview::PreviewSpec& b) noexcept {
    return a.kind==b.kind && a.leader==b.leader && a.look==b.look;
}

// --- Saving: what loadout_store.h SaveSupportLoadout is called with, per changed unit ---
struct SaveItem { LoadoutField field; const wchar_t* unit; const wchar_t* value; };
// `set`: CollectSave's (true) for `m`; one item per line, in the same order (the changed units). -1 when they do not pair.
inline int SaveItems(const roommenu::Menu& m,const roommenu::SaveSet& set,SaveItem* out,int capacity) noexcept {
    int n=0;
    for(int i=0;i<m.unitCount;++i) {
        const roommenu::Unit& u=m.unit[i];
        if(!roommenu::UnitChanged(u))continue;
        if(n>=set.count || n>=capacity)return -1;
        const LoadoutField field=roommenu::IsSquad(u) ? LoadoutField::preset : LoadoutField::vehicle;
        if(LoadoutKey(field,u.key)!=set.line[n].key)return -1;
        out[n]=SaveItem{field,u.key,set.line[n].value};
        ++n;
    }
    return n==set.count ? n : -1;
}

}  // namespace crew::roomui
