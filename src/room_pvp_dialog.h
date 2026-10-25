// The room menu's 对战 pages (the game's own dialogs, room_dialog.h, as the loadout pages): the pure side, so
// tests/room_pvp_test.cpp walks them. From what the room says now (View: the host's settings, this member's pick, every
// member's pick and whether it acknowledged the host's lock), the page to show (Build); a button picked (Apply) moves the
// pages and says what to change (Action), which room_pvp.cpp publishes.
//   main     the host's mode and sides (host only), this member's faction, role and unit, then every member's line
//   faction  the factions          role  the roles the faction takes          unit  the side's units, kPerPage a page
// No game memory, no Windows API.
#pragma once
#include "room_dialog_logic.h"
#include "room_pvp_logic.h"
#include "pvp_units.h"

namespace crew::roompvp {
namespace rd=roomdialog;
using hudtext::Tx;

enum class Page : std::uint8_t { main, faction, role, unit };
struct Nav { Page page=Page::main; int first=0; };

// One member's line of the main page.
struct Line { int ordinal=0; bool host=false,self=false,hasPick=false,acked=false; pm::Pick pick{}; };
struct View {
    bool host=false,online=false;
    Settings settings{};
    pm::Pick mine{};
    bool locking=false;   // the host holds the start until every member acknowledged the lock
    int count=0;
    Line lines[kMaxMembers]{};
};

// What a pick asks room_pvp.cpp to change.
enum class Change : std::uint8_t { none, mode, preset, pick };
struct Action { Change change=Change::none; bool enabled=false; int preset=0; pm::Pick pick{}; };
enum class Result : std::uint8_t { show, back };   // back: leave the 对战 pages (the loadout menu's units page)

// Button codes (roomdialog::Choice: `what` the code, `arg` its value).
enum class Code : std::uint8_t { none, mode, preset, faction, role, unit, setFaction, setRole, setUnit, prev, next, back };
constexpr rd::Choice C(Code c,std::int32_t arg=0) noexcept { return {static_cast<rd::Do>(c),arg}; }
constexpr Code CodeOf(rd::Choice c) noexcept { return static_cast<Code>(c.what); }

inline Tx FactionText(int f) noexcept {
    constexpr Tx kNames[]={Tx::pvpFactionEdf,Tx::pvpFactionEdf2,Tx::pvpFactionEdf3,Tx::pvpFactionEdf4,
                           Tx::pvpFactionMonster,Tx::pvpFactionPrimer,Tx::pvpFactionForeigner};
    static_assert(sizeof(kNames)/sizeof(kNames[0])==pm::kFactionCount,"a name a faction");
    return pm::FactionValid(f) ? kNames[f] : Tx::pvpFactionEdf;
}
inline Tx RoleText(int r) noexcept {
    return r==static_cast<int>(pm::Role::commander) ? Tx::pvpRoleCommander : r==static_cast<int>(pm::Role::unit) ? Tx::pvpRoleUnit
                                                                                                                 : Tx::pvpRoleSoldier;
}
inline Tx PresetText(int p) noexcept {
    constexpr Tx kNames[]={Tx::pvpPresetFfa,Tx::pvpPresetHva,Tx::pvpPresetCivil,Tx::pvpPresetAliens,Tx::pvpPresetCustom};
    static_assert(sizeof(kNames)/sizeof(kNames[0])==pm::kPresetCount,"a name a preset");
    return p>=0 && p<pm::kPresetCount ? kNames[p] : Tx::pvpPresetFfa;
}
inline const wchar_t* UnitText(const pm::Pick& p) noexcept {
    if(p.role!=static_cast<std::uint8_t>(pm::Role::unit) || p.unit>=debugspawn::kEntryCount)return L"-";
    return debugspawn::kEntries[p.unit].name;
}
// A pick with `faction`: its role kept when the faction takes it, else the faction's default; a unit kept when it is the
// faction side's, else the side's default (pvp.h PvpPickForFaction does the same in the mission).
inline pm::Pick WithFaction(pm::Pick p,int faction) noexcept {
    if(!pm::FactionValid(faction))return p;
    p.faction=static_cast<std::uint8_t>(faction);
    if(!pm::RoleAllowed(faction,p.role))p.role=static_cast<std::uint8_t>(pm::DefaultRole(faction));
    const auto side=pm::kFactions[faction].side;
    if(p.role!=static_cast<std::uint8_t>(pm::Role::unit))p.unit=pm::kNoUnit;
    else if(!pvp_units::Playable(p.unit,side)){const int r=pvp_units::DefaultRow(side);p.unit=r>=0 ? static_cast<std::uint16_t>(r) : pm::kNoUnit;}
    return p;
}
inline pm::Pick WithRole(pm::Pick p,int role) noexcept {
    if(!pm::RoleAllowed(p.faction,role))return p;
    p.role=static_cast<std::uint8_t>(role);
    const auto side=pm::kFactions[p.faction].side;
    if(p.role!=static_cast<std::uint8_t>(pm::Role::unit))p.unit=pm::kNoUnit;
    else if(!pvp_units::Playable(p.unit,side)){const int r=pvp_units::DefaultRow(side);p.unit=r>=0 ? static_cast<std::uint16_t>(r) : pm::kNoUnit;}
    return p;
}

inline void MemberLine(const Line& l,wchar_t* out,std::size_t n) noexcept {
    const pm::Pick p=l.hasPick ? l.pick : DefaultPick();
    wchar_t who[32];
    rd::detail::Put(who,32,hudtext::Tr(l.self ? Tx::pvpRoomYou : l.host ? Tx::pvpRoomHostN : Tx::pvpRoomMemberN),l.ordinal+1);
    rd::detail::Put(out,n,hudtext::Tr(Tx::pvpRoomMemberLine),who,hudtext::Tr(FactionText(p.faction)),hudtext::Tr(RoleText(p.role)),
                    p.role==static_cast<std::uint8_t>(pm::Role::unit) ? UnitText(p) : L"",
                    l.acked ? hudtext::Tr(Tx::pvpRoomAcked) : l.hasPick ? L"" : hudtext::Tr(Tx::pvpRoomDefault));
}

inline rd::Dialog Build(const View& v,const Nav& nav) noexcept {
    rd::Dialog d;
    const pm::Pick mine=v.mine;
    switch(nav.page) {
    case Page::main: {
        rd::detail::Put(d.title,rd::kTitleMost,hudtext::Tr(Tx::pvpRoomTitle),hudtext::Tr(v.settings.enabled ? Tx::pvpOn : Tx::pvpOff),
                        hudtext::Tr(PresetText(v.settings.relations.preset)),hudtext::Tr(v.locking ? Tx::pvpRoomLocking : v.host ? Tx::pvpRoomHostHint : Tx::pvpRoomGuestHint));
        if(v.host) {
            rd::detail::AddF(d,C(Code::mode),hudtext::Tr(Tx::pvpRoomMode),hudtext::Tr(v.settings.enabled ? Tx::pvpOn : Tx::pvpOff));
            rd::detail::AddF(d,C(Code::preset),hudtext::Tr(Tx::pvpPresetRow),hudtext::Tr(PresetText(v.settings.relations.preset)));
        }
        rd::detail::AddF(d,C(Code::faction),hudtext::Tr(Tx::pvpRoomFaction),hudtext::Tr(FactionText(mine.faction)));
        rd::detail::AddF(d,C(Code::role),hudtext::Tr(Tx::pvpRoomRole),hudtext::Tr(RoleText(mine.role)));
        if(mine.role==static_cast<std::uint8_t>(pm::Role::unit))rd::detail::AddF(d,C(Code::unit),hudtext::Tr(Tx::pvpRoomUnit),UnitText(mine));
        for(int i=0;i<v.count && d.count<rd::kItemsMost-1;++i) {
            wchar_t line[rd::kTextMost];MemberLine(v.lines[i],line,rd::kTextMost);
            rd::detail::Add(d,C(Code::none),line);
        }
        rd::detail::Add(d,C(Code::back),hudtext::Tr(Tx::roomDlgBack));
        break;
    }
    case Page::faction:
        rd::detail::Put(d.title,rd::kTitleMost,L"%ls",hudtext::Tr(Tx::pvpRoomPickFaction));
        for(int f=0;f<pm::kFactionCount;++f) {
            rd::detail::Add(d,C(Code::setFaction,f),hudtext::Tr(FactionText(f)));
            if(f==mine.faction)d.cursor=d.count-1;
        }
        rd::detail::Add(d,C(Code::back),hudtext::Tr(Tx::roomDlgBack));
        break;
    case Page::role:
        rd::detail::Put(d.title,rd::kTitleMost,L"%ls",hudtext::Tr(Tx::pvpRoomPickRole));
        for(int r=0;r<pm::kRoleCount;++r)if(pm::RoleAllowed(mine.faction,r)) {
            rd::detail::Add(d,C(Code::setRole,r),hudtext::Tr(RoleText(r)));
            if(r==mine.role)d.cursor=d.count-1;
        }
        rd::detail::Add(d,C(Code::back),hudtext::Tr(Tx::roomDlgBack));
        break;
    case Page::unit: {
        const auto side=pm::kFactions[mine.faction].side;
        const int total=pvp_units::Count(side);
        rd::detail::Put(d.title,rd::kTitleMost,hudtext::Tr(Tx::pvpRoomPickUnit),hudtext::Tr(FactionText(mine.faction)),
                        nav.first/rd::kPerPage+1,(total+rd::kPerPage-1)/rd::kPerPage);
        if(nav.first>0)rd::detail::Add(d,C(Code::prev),hudtext::Tr(Tx::roomDlgPrev));
        for(int k=nav.first;k<total && k<nav.first+rd::kPerPage;++k) {
            const int row=pvp_units::NthRow(side,k);
            rd::detail::Add(d,C(Code::setUnit,row),debugspawn::kEntries[row].name);
            if(row==mine.unit)d.cursor=d.count-1;
        }
        if(nav.first+rd::kPerPage<total)rd::detail::Add(d,C(Code::next),hudtext::Tr(Tx::roomDlgNext));
        rd::detail::Add(d,C(Code::back),hudtext::Tr(Tx::roomDlgBack));
        break;
    }
    }
    // The description beside the list: a button's own words (the loadout pages' Build does the same for theirs).
    for(int i=0;i<d.count;++i)if(!d.item[i].desc[0])rd::detail::Put(d.item[i].desc,rd::kDescMost,L"%ls",d.item[i].text);
    return d;
}

// The unit page that shows the member's unit.
inline int UnitPageFirst(const pm::Pick& p) noexcept {
    const auto side=pm::kFactions[p.faction].side;
    for(int k=0;k<pvp_units::Count(side);++k)if(pvp_units::NthRow(side,k)==p.unit)return k/rd::kPerPage*rd::kPerPage;
    return 0;
}

inline Result Apply(const View& v,Nav& nav,rd::Choice c,Action* out) noexcept {
    *out=Action{};
    switch(CodeOf(c)) {
    case Code::mode:
        if(v.host){out->change=Change::mode;out->enabled=!v.settings.enabled;}
        return Result::show;
    case Code::preset:
        if(v.host){out->change=Change::preset;out->preset=pm::NextPreset(v.settings.relations.preset);}
        return Result::show;
    case Code::faction:nav={Page::faction,0};return Result::show;
    case Code::role:nav={Page::role,0};return Result::show;
    case Code::unit:nav={Page::unit,UnitPageFirst(v.mine)};return Result::show;
    case Code::setFaction:
        out->change=Change::pick;out->pick=WithFaction(v.mine,c.arg);
        nav={Page::main,0};return Result::show;
    case Code::setRole:
        out->change=Change::pick;out->pick=WithRole(v.mine,c.arg);
        // Choosing to be a unit goes straight on to which one.
        nav=out->pick.role==static_cast<std::uint8_t>(pm::Role::unit) ? Nav{Page::unit,UnitPageFirst(out->pick)} : Nav{Page::main,0};
        return Result::show;
    case Code::setUnit: {
        pm::Pick p=v.mine;
        if(p.role==static_cast<std::uint8_t>(pm::Role::unit) && pvp_units::Playable(c.arg,pm::kFactions[p.faction].side)) {
            p.unit=static_cast<std::uint16_t>(c.arg);out->change=Change::pick;out->pick=p;
        }
        nav={Page::main,0};return Result::show;
    }
    case Code::prev:nav.first=nav.first>=rd::kPerPage ? nav.first-rd::kPerPage : 0;return Result::show;
    case Code::next:nav.first+=rd::kPerPage;return Result::show;
    case Code::back:
        if(nav.page==Page::main)return Result::back;
        nav={Page::main,0};return Result::show;
    case Code::none:return Result::show;
    }
    return Result::show;
}
// The dialog's own cancel: one page up, out of the 对战 pages from the main one.
inline Result Cancel(Nav& nav) noexcept {
    if(nav.page==Page::main)return Result::back;
    nav={Page::main,0};return Result::show;
}
}  // namespace crew::roompvp
