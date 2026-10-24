// The room / HQ support loadout menu as the game's own dialogs (2026-10-19, docs/room-native-dialog-re.md): every page is
// one native HUiDialogBox (APP:/UI/LYT_HUIDIALOGBOX.SGO, the window the soldier screen's outfit popup is) with a title
// and a list of buttons; the game draws it, moves the focus with the mouse, the keyboard and the pad, confirms with its own
// keys and closes it with its own cancel. The plugin only says what the page holds and what each button does.
//
// This header is the pure side (tests/room_dialog_test.cpp): from room_menu.h's Menu (the same model, the same edits, the
// same save) and where the player is (Nav), the page to show (Build); a button picked (Apply) or the dialog cancelled
// (Cancel) edits the Menu and moves Nav. Pages:
//   units      every support unit (edited ones marked), then save / close
//   unit       a squad's soldiers (add one), a vehicle's gun, pylons (add one) and crew row, a crew unit's two colours;
//              reset the unit
//   soldier    a soldier's class, colours and backpack weapon; copy it after itself; remove it
//   pylon      a pylon's store and rounds; remove it
//   crew       a crew's two colours
//   kind / primary / secondary / pack / gun / store / rounds / crewPrimary / crewSecondary: pick one value
//   discard    closing with unsaved edits: discard them or keep editing
// A long list is shown kPerPage at a time with "previous" / "next" buttons; every page ends in "back" (the units page in
// "close"), and the dialog's own cancel does the same.
// No game memory, no Windows API.
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

enum class Page : std::uint8_t { units, unit, soldier, pylon, crew, kind, primary, secondary, pack, gun, store, rounds,
                                 crewPrimary, crewSecondary, discard };
// Where the player is: the page, its unit and row (a soldier / a pylon: the unit's row as room_menu.h numbers them), and
// the first entry a long page shows.
struct Nav { Page page=Page::units; int unit=0,row=0,first=0; };

// What a button does; its argument (a unit, a row, a value) beside it. Packed into the button's 8 bytes (Pack / Unpack).
enum class Do : std::uint8_t { none, open, save, close, discard, keep, back, prev, next, row, add, copy, remove, reset,
                               customise, field, set };
struct Choice { Do what=Do::none; std::int32_t arg=0; };
constexpr std::uint64_t Pack(Choice c) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint8_t>(c.what))<<32)|static_cast<std::uint32_t>(c.arg);
}
constexpr Choice Unpack(std::uint64_t v) noexcept {
    return Choice{static_cast<Do>(static_cast<std::uint8_t>(v>>32)),static_cast<std::int32_t>(static_cast<std::uint32_t>(v))};
}
// A field of a soldier / pylon / crew page (Do::field's argument): the page it opens.
constexpr std::int32_t FieldArg(Page p) noexcept { return static_cast<std::int32_t>(p); }

inline constexpr int kPerPage=10;            // entries a page shows before "previous" / "next"
inline constexpr int kItemsMost=kPerPage+4;  // + previous, next, an action, back
inline constexpr std::size_t kTextMost=96;
inline constexpr std::size_t kTitleMost=512;
struct Item { wchar_t text[kTextMost]{}; Choice choice{}; };
struct Dialog { wchar_t title[kTitleMost]{}; int count=0; Item item[kItemsMost]{}; int cursor=0; };

// What the units page's title says under its name: the client's note, the last save, the generator, the room's sync
// (room_ui_logic.h ComposeStatus's lines, already in the HUD's language).
struct Context { const wchar_t* status[4]{}; int statusCount=0; };

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
    Tx::roomKindCannon,Tx::roomKindMidCannon,Tx::roomKindPileBanker,Tx::roomKindShotgunFencer};
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

// --- The lists a value page picks from ---
// Packs a class carries: none, then its own in the table's order (npc_pack_table.h CyclePack's ring).
inline int PackList(int soldierClass,int* out,int capacity) noexcept {
    int n=0;
    if(n<capacity)out[n++]=0;
    for(const auto& p:kPackWeapons)if(p.soldierClass==soldierClass && n<capacity)out[n++]=p.code;
    return n;
}
// The page's whole list of entries (before paging): count, and each entry's choice / text through `put`.
template<class Put> int Entries(const rm::Menu& m,const Nav& nav,Put put) noexcept {
    const rm::Unit* u=nav.unit>=0 && nav.unit<m.unitCount ? &m.unit[nav.unit] : nullptr;
    wchar_t text[kTextMost];
    int n=0;
    const auto emit=[&](Choice c,const wchar_t* t) noexcept { put(n++,c,t); };
    switch(nav.page) {
    case Page::units:
        for(int i=0;i<m.unitCount;++i){UnitLine(m.unit[i],text,kTextMost);emit({Do::open,i},text);}
        break;
    case Page::kind:
        for(int k=0;k<kSupportWeaponCount;++k)emit({Do::set,k},KindName(k));
        break;
    case Page::primary: case Page::secondary: case Page::crewSecondary:
        ColourName(m.palette,kStockColour,text,kTextMost);emit({Do::set,kStockColour},text);
        for(int i=0;i<m.palette.count;++i){ColourName(m.palette,m.palette.rgb[i],text,kTextMost);emit({Do::set,m.palette.rgb[i]},text);}
        break;
    case Page::crewPrimary: {
        const bool own=u && OwnDefault(*u);
        emit({Do::set,-2},hudtext::Tr(own ? Tx::roomStockColour : Tx::roomCrewDefault));   // not set
        if(!own){ColourName(m.palette,kStockColour,text,kTextMost);emit({Do::set,kStockColour},text);}
        for(int i=0;i<m.palette.count;++i){ColourName(m.palette,m.palette.rgb[i],text,kTextMost);emit({Do::set,m.palette.rgb[i]},text);}
        break;
    }
    case Page::pack: {
        if(!u || !rm::IsSquad(*u) || nav.row<0 || nav.row>=u->squad.count)break;
        int list[kPackWeaponCount+1];
        const int count=PackList(SupportSoldierClass(u->squad.kind[nav.row]),list,kPackWeaponCount+1);
        for(int i=0;i<count;++i)emit({Do::set,list[i]},PackName(list[i]));
        break;
    }
    case Page::store: {   // the body's stores of which at least one round still fits its weight (rm::PylonFits)
        if(!u || nav.row<1 || nav.row>u->vehicle.count)break;
        std::uint8_t allowed[kLoadStoreCount];
        const int count=rm::AllowedStores(*u,allowed);
        for(int i=0;i<count;++i)if(rm::MostRoundsFitting(*u,nav.row-1,allowed[i])>=0)emit({Do::set,allowed[i]},StoreName(allowed[i]));
        break;
    }
    case Page::rounds:    // the counts this pylon's store may hold within the weight
        if(!u || nav.row<1 || nav.row>u->vehicle.count)break;
        for(int r=0;r<16;++r) {
            if(!rm::PylonFits(*u,nav.row-1,u->vehicle.store[nav.row-1],static_cast<std::uint8_t>(r)))continue;
            detail::Put(text,kTextMost,L"×%d",kRoundCounts[r]);emit({Do::set,r},text);
        }
        break;
    case Page::gun:
        emit({Do::set,0},hudtext::Tr(Tx::roomGunHe));
        emit({Do::set,1},hudtext::Tr(Tx::roomGunAp));
        break;
    case Page::unit:
        if(!u)break;
        if(rm::IsSquad(*u)) {
            for(int i=0;i<u->squad.count;++i){SoldierLine(*u,i,text,kTextMost);emit({Do::row,i},text);}
        } else if(u->kind==rm::UnitKind::vehicle) {
            if(!u->vehicle.On())emit({Do::customise,0},hudtext::Tr(Tx::roomDlgCustomise));
            else {
                if(rm::IsTank(*u))emit({Do::field,FieldArg(Page::gun)},hudtext::Tr(u->vehicle.apGun ? Tx::roomGunAp : Tx::roomGunHe));
                for(int i=0;i<u->vehicle.count;++i) {
                    detail::Put(text,kTextMost,hudtext::Tr(Tx::roomDlgPylon),i+1,StoreName(u->vehicle.store[i]),
                                kRoundCounts[u->vehicle.rounds[i]&0xF]);
                    emit({Do::row,i+1},text);
                }
            }
        }
        if(u->hasCrew && !rm::IsCrewUnit(*u))emit({Do::row,rm::CrewRow(*u)},hudtext::Tr(Tx::roomDlgCrew));
        break;
    default: break;
    }
    return n;
}

// --- The page ---
namespace detail {
inline void UnitsTitle(const rm::Menu& m,const Context& c,wchar_t* out,std::size_t n) noexcept {
    Put(out,n,L"%ls",hudtext::Tr(rm::Dirty(m) ? Tx::roomTitleUnsaved : Tx::roomTitle));
    for(int i=0;i<c.statusCount && i<4;++i) {
        if(!c.status[i] || !*c.status[i])continue;
        const std::size_t used=std::wcslen(out);
        if(used<n)Put(out+used,n-used,L"\n%ls",c.status[i]);
    }
}
// The fixed buttons a page has (after its entries): the actions on what it shows.
inline void Actions(Dialog& d,const rm::Menu& m,const Nav& nav) noexcept {
    const rm::Unit* u=nav.unit>=0 && nav.unit<m.unitCount ? &m.unit[nav.unit] : nullptr;
    wchar_t name[kTextMost];
    switch(nav.page) {
    case Page::units:
        if(rm::Dirty(m))Add(d,{Do::save,0},hudtext::Tr(Tx::roomSave));
        break;
    case Page::unit:
        if(!u)break;
        if(rm::IsSquad(*u) && u->squad.count<u->seats)AddF(d,{Do::add,0},hudtext::Tr(Tx::roomDlgAddSoldier),u->squad.count,u->seats);
        if(u->kind==rm::UnitKind::vehicle && u->vehicle.On() && rm::CanAddPylon(*u))
            AddF(d,{Do::add,0},hudtext::Tr(Tx::roomDlgAddPylon),u->vehicle.count,rm::MostRows(*u));
        if(rm::IsCrewUnit(*u)) {
            CrewPrimaryName(m.palette,u->crew,OwnDefault(*u),name,kTextMost);
            AddF(d,{Do::field,FieldArg(Page::crewPrimary)},hudtext::Tr(Tx::roomDlgPrimary),name);
            ColourName(m.palette,u->crew.set ? u->crew.look.secondary : kStockColour,name,kTextMost);
            AddF(d,{Do::field,FieldArg(Page::crewSecondary)},hudtext::Tr(Tx::roomDlgSecondary),name);
        }
        if(u->squad.count>0 || u->vehicle.On() || u->crew.set)   // anything of its own: back to the stock unit
            Add(d,{Do::reset,0},hudtext::Tr(rm::IsSquad(*u) ? Tx::roomDlgResetSquad : rm::IsCrewUnit(*u) ? Tx::roomDlgResetCrew :
                                                                Tx::roomDlgResetVehicle));
        break;
    case Page::soldier: {
        if(!u || nav.row<0 || nav.row>=u->squad.count)break;
        const SupportLook& l=u->squad.look[nav.row];
        AddF(d,{Do::field,FieldArg(Page::kind)},hudtext::Tr(Tx::roomDlgKind),KindName(static_cast<int>(u->squad.kind[nav.row])));
        ColourName(m.palette,l.primary,name,kTextMost);
        AddF(d,{Do::field,FieldArg(Page::primary)},hudtext::Tr(Tx::roomDlgPrimary),name);
        ColourName(m.palette,l.secondary,name,kTextMost);
        AddF(d,{Do::field,FieldArg(Page::secondary)},hudtext::Tr(Tx::roomDlgSecondary),name);
        if(rm::CarriesPack(u->squad.kind[nav.row]))AddF(d,{Do::field,FieldArg(Page::pack)},hudtext::Tr(Tx::roomDlgPack),PackName(l.pack));
        if(u->squad.count<u->seats)Add(d,{Do::copy,0},hudtext::Tr(Tx::roomDlgCopySoldier));
        Add(d,{Do::remove,0},hudtext::Tr(Tx::roomDlgRemoveSoldier));
        break;
    }
    case Page::pylon: {
        if(!u || nav.row<1 || nav.row>u->vehicle.count)break;
        AddF(d,{Do::field,FieldArg(Page::store)},hudtext::Tr(Tx::roomDlgStore),StoreName(u->vehicle.store[nav.row-1]));
        AddF(d,{Do::field,FieldArg(Page::rounds)},hudtext::Tr(Tx::roomDlgRounds),kRoundCounts[u->vehicle.rounds[nav.row-1]&0xF]);
        Add(d,{Do::remove,0},hudtext::Tr(Tx::roomDlgRemovePylon));
        break;
    }
    case Page::crew:
        if(!u)break;
        CrewPrimaryName(m.palette,u->crew,OwnDefault(*u),name,kTextMost);
        AddF(d,{Do::field,FieldArg(Page::crewPrimary)},hudtext::Tr(Tx::roomDlgPrimary),name);
        ColourName(m.palette,u->crew.set ? u->crew.look.secondary : kStockColour,name,kTextMost);
        AddF(d,{Do::field,FieldArg(Page::crewSecondary)},hudtext::Tr(Tx::roomDlgSecondary),name);
        break;
    case Page::discard:
        Add(d,{Do::discard,0},hudtext::Tr(Tx::roomDlgDiscard));
        Add(d,{Do::save,0},hudtext::Tr(Tx::roomSave));
        Add(d,{Do::keep,0},hudtext::Tr(Tx::roomDlgKeepEditing));
        break;
    default: break;
    }
}
// A weighed vehicle's pages say what its pylons weigh against what it may carry (rm::UnitMassKg / UnitBudgetKg): the
// choices the store / rounds pages offer are the ones that keep it within.
inline void PayloadLine(Dialog& d,const rm::Menu& m,const Nav& nav) noexcept {
    const rm::Unit* u=nav.unit>=0 && nav.unit<m.unitCount ? &m.unit[nav.unit] : nullptr;
    const bool vehiclePage=nav.page==Page::unit || nav.page==Page::pylon || nav.page==Page::store || nav.page==Page::rounds;
    if(!u || !vehiclePage || u->kind!=rm::UnitKind::vehicle || !u->vehicle.On() || rm::UnitBudgetKg(*u)<=0)return;
    const std::size_t used=std::wcslen(d.title);
    if(used+1<kTitleMost)Put(d.title+used,kTitleMost-used,hudtext::Tr(Tx::roomDlgPayload),rm::UnitMassKg(*u),rm::UnitBudgetKg(*u));
}
inline void TitleText(Dialog& d,const rm::Menu& m,const Nav& nav,const Context& c) noexcept;
inline void Title(Dialog& d,const rm::Menu& m,const Nav& nav,const Context& c) noexcept {
    TitleText(d,m,nav,c);
    PayloadLine(d,m,nav);
}
inline void TitleText(Dialog& d,const rm::Menu& m,const Nav& nav,const Context& c) noexcept {
    const rm::Unit* u=nav.unit>=0 && nav.unit<m.unitCount ? &m.unit[nav.unit] : nullptr;
    wchar_t name[kTextMost];
    switch(nav.page) {
    case Page::units: UnitsTitle(m,c,d.title,kTitleMost); return;
    case Page::discard: Put(d.title,kTitleMost,L"%ls",hudtext::Tr(Tx::roomDlgDiscardTitle)); return;
    default: break;
    }
    if(!u){d.title[0]=0;return;}
    UnitLine(*u,name,kTextMost);
    switch(nav.page) {
    case Page::unit: Put(d.title,kTitleMost,L"%ls",name); return;
    case Page::soldier: case Page::kind: case Page::primary: case Page::secondary: case Page::pack: {
        wchar_t soldier[kTextMost];
        if(nav.row>=0 && nav.row<u->squad.count)SoldierLine(*u,nav.row,soldier,kTextMost);else soldier[0]=0;
        const Tx what=nav.page==Page::kind ? Tx::roomDlgPickKind : nav.page==Page::primary ? Tx::roomDlgPickPrimary :
                      nav.page==Page::secondary ? Tx::roomDlgPickSecondary : nav.page==Page::pack ? Tx::roomDlgPickPack : Tx::roomDlgSoldier;
        Put(d.title,kTitleMost,L"%ls\n%ls\n%ls",name,soldier,hudtext::Tr(what));
        return;
    }
    case Page::pylon: case Page::store: case Page::rounds: {
        const Tx what=nav.page==Page::store ? Tx::roomDlgPickStore : nav.page==Page::rounds ? Tx::roomDlgPickRounds : Tx::roomDlgPylonTitle;
        Put(d.title,kTitleMost,L"%ls\n%ls",name,hudtext::Tr(what));
        if(nav.page==Page::pylon && nav.row>=1) {   // "Pylon %d"
            wchar_t pylon[kTextMost];
            Put(pylon,kTextMost,hudtext::Tr(what),nav.row);
            Put(d.title,kTitleMost,L"%ls\n%ls",name,pylon);
        }
        return;
    }
    case Page::gun: Put(d.title,kTitleMost,L"%ls\n%ls",name,hudtext::Tr(Tx::roomDlgPickGun)); return;
    case Page::crew: case Page::crewPrimary: case Page::crewSecondary:
        Put(d.title,kTitleMost,L"%ls\n%ls",name,hudtext::Tr(nav.page==Page::crewPrimary ? Tx::roomDlgPickPrimary :
                                                            nav.page==Page::crewSecondary ? Tx::roomDlgPickSecondary : Tx::roomDlgCrew));
        return;
    default: Put(d.title,kTitleMost,L"%ls",name); return;
    }
}
// The entry a value page opens on: the current value's place.
inline int Current(const rm::Menu& m,const Nav& nav) noexcept {
    const rm::Unit* u=nav.unit>=0 && nav.unit<m.unitCount ? &m.unit[nav.unit] : nullptr;
    if(!u)return -1;
    std::int32_t want=0;bool any=true;
    switch(nav.page) {
    case Page::kind: if(nav.row<0 || nav.row>=u->squad.count)return -1; want=static_cast<std::int32_t>(u->squad.kind[nav.row]); break;
    case Page::primary: if(nav.row<0 || nav.row>=u->squad.count)return -1; want=u->squad.look[nav.row].primary; break;
    case Page::secondary: if(nav.row<0 || nav.row>=u->squad.count)return -1; want=u->squad.look[nav.row].secondary; break;
    case Page::pack: if(nav.row<0 || nav.row>=u->squad.count)return -1; want=u->squad.look[nav.row].pack; break;
    case Page::gun: want=u->vehicle.apGun ? 1 : 0; break;
    case Page::store: if(nav.row<1 || nav.row>u->vehicle.count)return -1; want=u->vehicle.store[nav.row-1]; break;
    case Page::rounds: if(nav.row<1 || nav.row>u->vehicle.count)return -1; want=u->vehicle.rounds[nav.row-1]&0xF; break;
    case Page::crewPrimary: want=u->crew.set ? u->crew.look.primary : -2; break;
    case Page::crewSecondary: want=u->crew.set ? u->crew.look.secondary : kStockColour; break;
    default: any=false; break;
    }
    if(!any)return -1;
    int at=-1;
    Entries(m,nav,[&](int i,Choice c,const wchar_t*) noexcept { if(at<0 && c.what==Do::set && c.arg==want)at=i; });
    return at;
}
}  // namespace detail

// Whether `p` lists values (picking one sets it and goes back).
constexpr bool ValuePage(Page p) noexcept {
    return p==Page::kind || p==Page::primary || p==Page::secondary || p==Page::pack || p==Page::gun || p==Page::store ||
           p==Page::rounds || p==Page::crewPrimary || p==Page::crewSecondary;
}
// The page to show: its title, the entries from nav.first (kPerPage of them), previous / next, its actions, back / close.
// `cursor` the button the dialog opens on: a value page's current value, else `wantCursor` when it is on the page.
inline Dialog Build(const rm::Menu& m,const Nav& nav,const Context& c,int wantCursor=0) noexcept {
    Dialog d;
    detail::Title(d,m,nav,c);
    const int total=Entries(m,nav,[](int,Choice,const wchar_t*) noexcept {});
    int first=nav.first<0 ? 0 : nav.first>=total ? (total>0 ? (total-1)/kPerPage*kPerPage : 0) : nav.first;
    first=first/kPerPage*kPerPage;
    const int current=detail::Current(m,nav);
    Entries(m,nav,[&](int i,Choice ch,const wchar_t* t) noexcept {
        if(i<first || i>=first+kPerPage)return;
        if(i==current)d.cursor=d.count;
        detail::Add(d,ch,t);
    });
    if(first>0)detail::Add(d,{Do::prev,0},hudtext::Tr(Tx::roomDlgPrev));
    if(first+kPerPage<total)detail::Add(d,{Do::next,0},hudtext::Tr(Tx::roomDlgNext));
    detail::Actions(d,m,nav);
    detail::Add(d,{nav.page==Page::units ? Do::close : Do::back,0},
                hudtext::Tr(nav.page==Page::units ? (rm::Dirty(m) ? Tx::roomDlgCloseDiscard : Tx::roomDlgClose) : Tx::roomDlgBack));
    if(current<0 && wantCursor>=0 && wantCursor<d.count)d.cursor=wantCursor;
    return d;
}

// What the caller does after Apply / Cancel.
enum class Result : std::uint8_t { show, save, close };

namespace detail {
// The page above `p` (where back / cancel go).
constexpr Page Parent(Page p) noexcept {
    switch(p) {
    case Page::unit: case Page::discard: return Page::units;
    case Page::soldier: case Page::pylon: case Page::crew: case Page::gun: return Page::unit;
    case Page::kind: case Page::primary: case Page::secondary: case Page::pack: return Page::soldier;
    case Page::store: case Page::rounds: return Page::pylon;
    case Page::crewPrimary: case Page::crewSecondary: return Page::crew;
    default: return Page::units;
    }
}
// A crew unit's page is its unit page (its only row is the crew): its value pages come back there.
inline Page Up(const rm::Menu& m,const Nav& nav) noexcept {
    const Page p=Parent(nav.page);
    if(p==Page::crew && nav.unit>=0 && nav.unit<m.unitCount && rm::IsCrewUnit(m.unit[nav.unit]))return Page::unit;
    return p;
}
// room_menu.h's cursor and level kept on what the player is editing: its preview (room_ui_logic.h PreviewFor) follows.
inline void Sync(rm::Menu& m,const Nav& nav) noexcept {
    m.unitAt=nav.unit;
    m.rowAt=nav.row<0 ? 0 : nav.row;
    m.fieldAt=0;
    m.level=nav.page==Page::units || nav.page==Page::discard ? rm::Level::units :
            nav.page==Page::unit ? rm::Level::rows : rm::Level::fields;
    if(nav.page==Page::unit && nav.unit>=0 && nav.unit<m.unitCount && rm::IsCrewUnit(m.unit[nav.unit]))m.rowAt=0;
}
inline void Go(rm::Menu& m,Nav& nav,Page p,int row) noexcept { nav.page=p;nav.row=row;nav.first=0;Sync(m,nav); }
inline void Set(rm::Menu& m,const Nav& nav,std::int32_t v) noexcept {
    if(nav.unit<0 || nav.unit>=m.unitCount)return;
    rm::Unit& u=m.unit[nav.unit];
    switch(nav.page) {
    case Page::kind:
        if(nav.row<0 || nav.row>=u.squad.count || v<0 || v>=kSupportWeaponCount)return;
        u.squad.kind[nav.row]=static_cast<SupportWeapon>(v);
        if(!PackFits(u.squad.look[nav.row].pack,SupportSoldierClass(u.squad.kind[nav.row])))u.squad.look[nav.row].pack=0;
        return;
    case Page::primary: if(nav.row>=0 && nav.row<u.squad.count)u.squad.look[nav.row].primary=v; return;
    case Page::secondary: if(nav.row>=0 && nav.row<u.squad.count)u.squad.look[nav.row].secondary=v; return;
    case Page::pack:
        if(nav.row>=0 && nav.row<u.squad.count && PackFits(v,SupportSoldierClass(u.squad.kind[nav.row])))
            u.squad.look[nav.row].pack=static_cast<std::uint8_t>(v);
        return;
    case Page::gun: if(u.vehicle.On())u.vehicle.apGun=v!=0; return;
    case Page::store: {   // a store whose rounds would not fit the weight takes as many as do
        if(nav.row<1 || nav.row>u.vehicle.count)return;
        std::uint8_t allowed[kLoadStoreCount];
        const int n=rm::AllowedStores(u,allowed);
        for(int i=0;i<n;++i) {
            if(allowed[i]!=v)continue;
            const int most=rm::MostRoundsFitting(u,nav.row-1,allowed[i]);
            if(most<0)return;
            const int at=nav.row-1;
            if(!rm::PylonFits(u,at,allowed[i],u.vehicle.rounds[at]))u.vehicle.rounds[at]=static_cast<std::uint8_t>(most);
            u.vehicle.store[at]=allowed[i];
        }
        return;
    }
    case Page::rounds:
        if(nav.row>=1 && nav.row<=u.vehicle.count && v>=0 && v<16 &&
           rm::PylonFits(u,nav.row-1,u.vehicle.store[nav.row-1],static_cast<std::uint8_t>(v)))
            u.vehicle.rounds[nav.row-1]=static_cast<std::uint8_t>(v);
        return;
    case Page::crewPrimary:
        if(v==-2){u.crew=CrewColour{};return;}
        if(!u.crew.set){u.crew.set=true;u.crew.look=SupportLook{};}
        u.crew.look.primary=v;
        return;
    case Page::crewSecondary:
        if(!u.crew.set){u.crew.set=true;u.crew.look=SupportLook{};}
        u.crew.look.secondary=v;
        return;
    default: return;
    }
}
}  // namespace detail

// Starts the menu on the units page.
inline void Begin(rm::Menu& m,Nav& nav) noexcept { nav=Nav{};detail::Sync(m,nav); }

// A button picked. Result::save: the caller saves (room_ui.cpp Save) and shows the units page again; close: the caller
// closes the menu (the edits already discarded by Apply where that was the choice).
inline Result Apply(rm::Menu& m,Nav& nav,Choice c) noexcept {
    rm::Unit* u=nav.unit>=0 && nav.unit<m.unitCount ? &m.unit[nav.unit] : nullptr;
    switch(c.what) {
    case Do::open:
        if(c.arg<0 || c.arg>=m.unitCount)return Result::show;
        nav.unit=c.arg;detail::Go(m,nav,Page::unit,0);
        return Result::show;
    case Do::save: detail::Go(m,nav,Page::units,0); return Result::save;
    case Do::close:
        if(rm::Dirty(m)){detail::Go(m,nav,Page::discard,0);return Result::show;}
        rm::Discard(m);
        return Result::close;
    case Do::discard: rm::Discard(m); return Result::close;
    case Do::keep: detail::Go(m,nav,Page::units,0); return Result::show;
    case Do::back: {
        const Page up=detail::Up(m,nav);
        const int row=up==Page::unit || up==Page::units ? 0 : nav.row;
        detail::Go(m,nav,up,row);
        return Result::show;
    }
    case Do::prev: nav.first=nav.first>=kPerPage ? nav.first-kPerPage : 0; return Result::show;
    case Do::next: nav.first+=kPerPage; return Result::show;
    case Do::row:
        if(!u)return Result::show;
        if(rm::IsSquad(*u)){detail::Go(m,nav,Page::soldier,c.arg);return Result::show;}
        if(c.arg==rm::CrewRow(*u)){detail::Go(m,nav,Page::crew,c.arg);return Result::show;}
        detail::Go(m,nav,Page::pylon,c.arg);
        return Result::show;
    case Do::add:
        if(!u)return Result::show;
        if(rm::IsSquad(*u)) {
            m.rowAt=u->squad.count ? u->squad.count-1 : 0;   // after the last soldier, a copy of it
            if(rm::detail::AddSoldier(m,*u)==rm::Act::changed){detail::Go(m,nav,Page::soldier,m.rowAt);}
        } else if(rm::detail::AddPylon(m,*u)==rm::Act::changed)detail::Go(m,nav,Page::pylon,m.rowAt);
        return Result::show;
    case Do::copy:
        if(!u || !rm::IsSquad(*u))return Result::show;
        m.rowAt=nav.row;
        if(rm::detail::AddSoldier(m,*u)==rm::Act::changed)detail::Go(m,nav,Page::soldier,m.rowAt);
        return Result::show;
    case Do::remove:
        if(!u)return Result::show;
        m.rowAt=nav.row;
        if(rm::IsSquad(*u))rm::detail::RemoveSoldier(m,*u);
        else rm::detail::RemovePylon(m,*u);
        detail::Go(m,nav,Page::unit,0);
        return Result::show;
    case Do::reset:
        if(u){u->squad=SupportPreset{};u->vehicle=VehicleLoadout{};u->crew=CrewColour{};}
        detail::Go(m,nav,Page::unit,0);
        return Result::show;
    case Do::customise:
        if(u && u->kind==rm::UnitKind::vehicle)rm::detail::Materialise(*u);
        return Result::show;
    case Do::field: {
        const Page p=static_cast<Page>(c.arg);
        if(!ValuePage(p))return Result::show;
        const int row=nav.row;
        detail::Go(m,nav,p,row);
        const int current=detail::Current(m,nav);   // a long list opens on the page holding the current value
        if(current>0)nav.first=current/kPerPage*kPerPage;
        return Result::show;
    }
    case Do::set: {
        detail::Set(m,nav,c.arg);
        const Page up=detail::Up(m,nav);
        detail::Go(m,nav,up,up==Page::unit ? 0 : nav.row);
        return Result::show;
    }
    default: return Result::show;
    }
}
// The dialog cancelled (the game's own cancel: Esc / B / right click): as "back"; on the units page as "close".
inline Result Cancel(rm::Menu& m,Nav& nav) noexcept {
    if(nav.page==Page::discard){detail::Go(m,nav,Page::units,0);return Result::show;}
    return Apply(m,nav,Choice{nav.page==Page::units ? Do::close : Do::back,0});
}

}  // namespace crew::roomdialog
