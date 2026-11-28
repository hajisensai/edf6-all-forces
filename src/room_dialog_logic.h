// The game's own dialog pages' pure side (docs/room-native-dialog-re.md): a page is one native HUiDialogBox
// (APP:/UI/LYT_HUIDIALOGBOX.SGO) with a title and a list of buttons, each carrying a Choice packed into its 8 bytes. The
// room's 对战 pages (room_pvp_dialog.h) are built as such pages; room_dialog.h opens them.
// 2026-11-24 (docs/plan-2026-11-24-ui-rewrite.md 阶段 2): the support loadout pages that were built here (units / unit /
// soldier / pylon / crew / value pages) are gone, replaced by 整备 (armory.h); the unit and soldier lines stay (整备's
// placeholder lists and room_ui_logic.h use them).
// No game memory, no Windows API. tests/room_dialog_test.cpp.
#pragma once
#include "hudtext.h"
#include "room_menu.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace crew::roomdialog {

using hudtext::Tx;
namespace rm=roommenu;

// What a button does; its argument (a unit, a row, a value) beside it. Packed into the button's 8 bytes (Pack / Unpack).
enum class Do : std::uint8_t { none, open, save, close, discard, keep, back, prev, next, row, add, copy, remove, reset,
                               customise, field, set, pvp };
struct Choice { Do what=Do::none; std::int32_t arg=0; };
constexpr std::uint64_t Pack(Choice c) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint8_t>(c.what))<<32)|static_cast<std::uint32_t>(c.arg);
}
constexpr Choice Unpack(std::uint64_t v) noexcept {
    return Choice{static_cast<Do>(static_cast<std::uint8_t>(v>>32)),static_cast<std::int32_t>(static_cast<std::uint32_t>(v))};
}

inline constexpr int kPerPage=10;            // entries a page shows before "previous" / "next"
inline constexpr int kItemsMost=kPerPage+5;  // + the 对战 entry, previous, next, an action, back
inline constexpr std::size_t kTextMost=96;
inline constexpr std::size_t kTitleMost=512;
inline constexpr std::size_t kDescMost=400;
// `desc`: what the description dialog shows beside the list while the button has the focus (Describe).
struct Item { wchar_t text[kTextMost]{}; Choice choice{}; wchar_t desc[kDescMost]{}; };
struct Dialog { wchar_t title[kTitleMost]{}; int count=0; Item item[kItemsMost]{}; int cursor=0; };


namespace detail {
template<class... A> void Put(wchar_t* out,std::size_t n,const wchar_t* format,A... args) noexcept {
    if(!out || !n)return;
    if(_snwprintf_s(out,n,_TRUNCATE,format,args...)<0)out[n-1]=0;
}
inline void Add(Dialog& d,Choice c,const wchar_t* text) noexcept {
    if(d.count>=kItemsMost)return;
    Item& it=d.item[d.count++];
    Put(it.text,kTextMost,L"%ls",text ? text : L"");
    it.choice=c;
}
template<class... A> void AddF(Dialog& d,Choice c,const wchar_t* format,A... args) noexcept {
    wchar_t text[kTextMost];
    Put(text,kTextMost,format,args...);
    Add(d,c,text);
}
}  // namespace detail

// --- Names ---
// The kind / store labels room_ui_logic.h uses (one table): repeated here so this header stands alone.
inline constexpr Tx kKindTexts[kSupportWeaponCount]={
    Tx::roomKindRifle,Tx::roomKindFlame,Tx::roomKindRocket,Tx::roomKindShotgun,Tx::roomKindSniper,
    Tx::roomKindLance,Tx::roomKindLaser,Tx::roomKindMonster,Tx::roomKindIzuna,Tx::roomKindThunderBow,
    Tx::roomKindCannon,Tx::roomKindMidCannon,Tx::roomKindPileBanker,Tx::roomKindShotgunFencer,Tx::roomKindXmInfantry,
    Tx::roomKindZwVehicler,Tx::roomKindZwRuneBlade};
inline constexpr Tx kStoreTexts[kLoadStoreCount]={
    Tx::roomStoreAamS,Tx::roomStoreAamM,Tx::roomStoreAamL,Tx::roomStoreAgm,Tx::roomStoreAgmL,Tx::roomStoreMk82,
    Tx::roomStoreRkt,Tx::roomStoreAp,Tx::roomStoreHe,Tx::roomStoreGlm};
inline const wchar_t* KindName(int kind) noexcept {
    return kind>=0 && kind<kSupportWeaponCount ? hudtext::Tr(kKindTexts[kind]) : L"?";
}
inline const wchar_t* StoreName(int code) noexcept {
    return code>=1 && code<=kLoadStoreCount ? hudtext::Tr(kStoreTexts[code-1]) : L"?";
}
inline const wchar_t* PackName(int code) noexcept {
    const PackWeapon* p=code ? PackOf(code) : nullptr;
    return p ? p->label : hudtext::Tr(Tx::roomPackNone);
}
// A colour as its place in the HQ palette ("colour 7 (#RRGGBB)"); stock "stock colours"; one written by hand in the ini
// (not in the palette) by its hex alone.
inline void ColourName(const rm::Palette& p,std::int32_t colour,wchar_t* out,std::size_t n) noexcept {
    if(colour==kStockColour){detail::Put(out,n,L"%ls",hudtext::Tr(Tx::roomStockColour));return;}
    for(int i=0;i<p.count;++i)
        if(p.rgb[i]==colour){detail::Put(out,n,hudtext::Tr(Tx::roomDlgColourN),i+1,static_cast<unsigned>(colour&0xFFFFFF));return;}
    detail::Put(out,n,L"#%06X",static_cast<unsigned>(colour&0xFFFFFF));
}
// A crew's primary: not set (the default's), else a colour.
inline void CrewPrimaryName(const rm::Palette& p,const CrewColour& c,bool ownDefault,wchar_t* out,std::size_t n) noexcept {
    if(!c.set){detail::Put(out,n,L"%ls",hudtext::Tr(ownDefault ? Tx::roomStockColour : Tx::roomCrewDefault));return;}
    ColourName(p,c.look.primary,out,n);
}
// A mech row's name ("Right arm") and a value of it in the HUD's language (the parts tables' own four labels; -1 the
// request's own frame / core or an empty slot).
inline const wchar_t* MechRowName(int row) noexcept {
    static constexpr Tx kRows[kMechFields]={Tx::roomMechRequest,Tx::roomMechFrame,Tx::roomMechRightArm,Tx::roomMechLeftArm,
                                            Tx::roomMechRightShoulder,Tx::roomMechLeftShoulder,Tx::roomMechCore};
    return row>=0 && row<kMechFields ? hudtext::Tr(kRows[row]) : L"?";
}
inline const wchar_t* MechValueName(int row,int v) noexcept {
    if(const wchar_t* label=rm::MechValueLabel(row,v,static_cast<int>(hudtext::InUse())))return label;
    return v==-1 ? hudtext::Tr(row==rm::kMechRowFrame || row==rm::kMechRowCore ? Tx::roomMechOwn : Tx::roomMechEmpty) : L"?";
}
// "Right arm: Machine Gun".
inline void MechLine(const MechLoadout& m,int row,wchar_t* out,std::size_t n) noexcept {
    detail::Put(out,n,hudtext::Tr(Tx::roomMechPart),MechRowName(row),MechValueName(row,rm::MechRowValue(m,row)));
}
inline const wchar_t* UnitName(const rm::Unit& u) noexcept { return u.label ? u.label : u.key ? u.key : L"?"; }
inline bool OwnDefault(const rm::Unit& u) noexcept { return !u.key || !*u.key; }   // the default's own crew row
// A unit as the units page lists it: its name, "(edited)" / "(ini value invalid)".
inline void UnitLine(const rm::Unit& u,wchar_t* out,std::size_t n) noexcept {
    wchar_t name[kTextMost];
    if(rm::IsCrewUnit(u) && u.key && *u.key && _wcsicmp(u.key,kCrewMissionKey)!=0)
        detail::Put(name,kTextMost,hudtext::Tr(Tx::roomUnitCrewOf),UnitName(u));
    else detail::Put(name,kTextMost,L"%ls",UnitName(u));
    if(rm::UnitChanged(u))detail::Put(out,n,hudtext::Tr(Tx::roomUnitEdited),name);
    else if(u.refused || u.crewRefused)detail::Put(out,n,hudtext::Tr(Tx::roomUnitRefused),name);
    else detail::Put(out,n,L"%ls",name);
}
// A soldier of a squad: "1. [Leader] Ranger, rifle + MG11".
inline void SoldierLine(const rm::Unit& u,int i,wchar_t* out,std::size_t n) noexcept {
    const int kind=static_cast<int>(u.squad.kind[i]);
    detail::Put(out,n,i%4==0 ? hudtext::Tr(Tx::roomNumberedLeader) : L"%d. %ls",i+1,KindName(kind));
    if(u.squad.look[i].pack) {
        const std::size_t used=std::wcslen(out);
        if(used<n)detail::Put(out+used,n-used,L" + %ls",PackName(u.squad.look[i].pack));
    }
}

}  // namespace crew::roomdialog
