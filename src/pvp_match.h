// A PvP match as data (docs/plan-2026-10-25-pvp-factions.md): named factions, each on its own native team; the
// relations between factions (a preset or the host's own table); every member's faction, role and unit. No game access:
// pvp_protocol.h carries it, pvp_native.cpp applies it (team_native.h writes the relations), tests/pvp_match_test.cpp
// checks it.
#pragma once
#include "team_native_logic.h"
#include <cstddef>
#include <cstdint>

namespace crew::pvp_match {
using team_native::kEnemy;
using team_native::kFriend;
using team_native::kNeutral;

// Which kind of army a faction is: what roles and units it may take.
enum class Side : std::uint8_t { edf, monster, primer, foreigner };
// A faction's native team is fixed (one team each, never shared): the stock EDF NPCs (team 2) are the first EDF
// faction's, the mission's own monsters (team 1) the monster faction's; the others take the rows team_native adds.
struct Faction { const char* id; Side side; std::uint8_t team; std::uint32_t color; };
inline constexpr Faction kFactions[]={
    {"edf",Side::edf,2,0xFF3C8CFFu},         // EDF (the stock friendly NPCs are on it)
    {"edf2",Side::edf,7,0xFFF0A030u},        // EDF 2
    {"edf3",Side::edf,8,0xFF40C060u},        // EDF 3
    {"edf4",Side::edf,9,0xFFC060E0u},        // EDF 4
    {"monster",Side::monster,1,0xFFE04040u}, // the monsters, every kind (the mission's own are on it)
    {"primer",Side::primer,10,0xFF40D0D0u},  // the Primers (EDF5 / EDF6 aliens)
    {"foreigner",Side::foreigner,11,0xFFD0D040u},  // the Foreigners (EDF4.1's)
};
constexpr int kFactionCount=static_cast<int>(sizeof(kFactions)/sizeof(kFactions[0]));
constexpr int kMaxFactions=16;   // the wire's room (pvp_protocol.h)
static_assert(kFactionCount<=kMaxFactions);

constexpr bool FactionValid(int f) noexcept { return f>=0 && f<kFactionCount; }
constexpr int FactionTeam(int f) noexcept { return FactionValid(f) ? kFactions[f].team : -1; }
// The faction a native team belongs to, -1 for a team no faction has.
constexpr int TeamFaction(int team) noexcept {
    for(int f=0;f<kFactionCount;++f)if(kFactions[f].team==team)return f;
    return -1;
}
// A team a PvP member may be on.
constexpr bool PvpTeam(int team) noexcept { return TeamFaction(team)>=0; }
constexpr bool Alien(Side s) noexcept { return s!=Side::edf; }

// What a member plays: a soldier of their class (EDF), the commander (the M map's view: the faction's NPCs and support),
// or one unit of their faction's army they spawn as and control (the aliens).
enum class Role : std::uint8_t { soldier, commander, unit, count };
constexpr int kRoleCount=static_cast<int>(Role::count);
constexpr bool RoleAllowed(int faction,int role) noexcept {
    if(!FactionValid(faction) || role<0 || role>=kRoleCount)return false;
    const Role r=static_cast<Role>(role);
    if(r==Role::commander)return true;
    return Alien(kFactions[faction].side) ? r==Role::unit : r==Role::soldier;
}
// The role a faction starts with (and falls back to when a member's role does not fit their new faction).
constexpr int DefaultRole(int faction) noexcept {
    return FactionValid(faction) && Alien(kFactions[faction].side) ? static_cast<int>(Role::unit) : static_cast<int>(Role::soldier);
}
constexpr std::uint16_t kNoUnit=0xFFFF;

// How the factions see each other.
enum class Preset : std::uint8_t {
    freeForAll,      // every faction against every other
    humansVsAliens,  // the EDF factions together against the monsters, Primers and Foreigners together
    edfCivilWar,     // the EDF factions against each other; the aliens together against every EDF faction
    aliensWar,       // the monsters, Primers and Foreigners against each other; the EDF factions together against all
    custom,          // the host's own table
    count
};
constexpr int kPresetCount=static_cast<int>(Preset::count);
// The preset after `p` in the host's cycle (custom is only ever the host's own table, never cycled to).
constexpr int NextPreset(int p) noexcept {
    const int next=p+1>=static_cast<int>(Preset::custom) ? 0 : p+1;
    return next;
}
constexpr std::uint8_t PresetRelation(Preset p,int a,int b) noexcept {
    if(a==b)return kFriend;
    const bool alienA=Alien(kFactions[a].side),alienB=Alien(kFactions[b].side);
    switch(p) {
    case Preset::freeForAll:return kEnemy;
    case Preset::humansVsAliens:return alienA==alienB ? kFriend : kEnemy;
    case Preset::edfCivilWar:return alienA && alienB ? kFriend : kEnemy;
    case Preset::aliensWar:return !alienA && !alienB ? kFriend : kEnemy;
    default:return kEnemy;
    }
}

// The match's relations between factions (row a, column b: how a sees b): a preset's, or the host's custom table.
struct Relations {
    std::uint8_t preset=static_cast<std::uint8_t>(Preset::freeForAll);
    std::uint8_t rel[kMaxFactions][kMaxFactions]{};
    void FromPreset(Preset p) noexcept {
        preset=static_cast<std::uint8_t>(p);
        for(int a=0;a<kMaxFactions;++a)for(int b=0;b<kMaxFactions;++b)
            rel[a][b]=a==b ? kFriend : (a<kFactionCount && b<kFactionCount ? PresetRelation(p,a,b) : kNeutral);
    }
    // Same diagonal, both ways the same, values the manager's, nothing past the catalogue, a preset's table its own.
    bool Valid() const noexcept {
        if(preset>=kPresetCount)return false;
        for(int a=0;a<kMaxFactions;++a)for(int b=0;b<kMaxFactions;++b) {
            const std::uint8_t v=rel[a][b];
            if(v>kEnemy || v!=rel[b][a])return false;
            if(a==b ? v!=kFriend : ((a>=kFactionCount || b>=kFactionCount) && v!=kNeutral))return false;
            if(preset!=static_cast<std::uint8_t>(Preset::custom) && a!=b && a<kFactionCount && b<kFactionCount &&
               v!=PresetRelation(static_cast<Preset>(preset),a,b))return false;
        }
        return true;
    }
    bool Same(const Relations& o) const noexcept {
        if(preset!=o.preset)return false;
        for(int a=0;a<kMaxFactions;++a)for(int b=0;b<kMaxFactions;++b)if(rel[a][b]!=o.rel[a][b])return false;
        return true;
    }
};

// The native relations a match writes (team_native.h SetTeamOverlay): stock, then every pair of faction teams from the
// faction table; team 0 (the stock players' other team, friend of 2) follows the first EDF faction (team 2). Teams 3..6
// keep their stock rows.
inline team_native::Overlay TeamOverlay(const Relations& r) noexcept {
    team_native::Overlay o{};
    o.Stock();
    for(int a=0;a<kFactionCount;++a)for(int b=0;b<kFactionCount;++b) {
        const int ta=kFactions[a].team,tb=kFactions[b].team;
        o.rel[ta][tb]=r.rel[a][b];
    }
    const int edf=kFactions[0].team;
    for(int f=0;f<kFactionCount;++f) {
        const int t=kFactions[f].team;
        if(t==edf)continue;
        o.rel[0][t]=o.rel[edf][t];
        o.rel[t][0]=o.rel[t][edf];
    }
    return o;
}

// A member's choice. `unit`: the spawn catalogue row (src/spawn_catalog.inc) of the unit role, kNoUnit otherwise.
struct Pick { std::uint8_t faction=0,role=0; std::uint16_t unit=kNoUnit; };
constexpr bool PickValid(const Pick& p) noexcept {
    if(!RoleAllowed(p.faction,p.role))return false;
    return (p.role==static_cast<std::uint8_t>(Role::unit))==(p.unit!=kNoUnit);
}
}  // namespace crew::pvp_match
