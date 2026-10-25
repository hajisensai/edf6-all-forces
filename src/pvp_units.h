// The PvP unit catalogue: which rows of the debug spawn catalogue (debug_spawn.h kEntries) a player can be in a PvP match
// (spawned as and controlled by enemy control), and the faction side each belongs to. The rows come from pvp_units.inc,
// written by tools/gen_pvp_units.py (the playable ones only; docs/plan-2026-10-25-pvp-factions.md section 4); no game access,
// so tests/pvp_units_test.cpp checks the helpers off it.
//
// The user, 2026-10-25: monsters are one faction (every monster of any game), the Primers (EDF5 / EDF6) another, the
// Foreigners (EDF4.1's) a third. Side::edf has no unit here: the EDF's members are soldiers and vehicles, not enemy rows.
#pragma once
#include "debug_spawn.h"
#include "pvp_match.h"

namespace crew {
namespace pvp_units {
using pvp_match::Side;

struct Unit { const char* id; Side side; };
inline constexpr Unit kUnits[]={
#include "pvp_units.inc"
};
constexpr int kUnitCount=static_cast<int>(sizeof(kUnits)/sizeof(kUnits[0]));
constexpr int kSideCount=4;

constexpr bool SameId(const char* a,const char* b) noexcept {
    if(!a || !b)return false;
    while(*a && *a==*b){++a;++b;}
    return *a==*b;
}
// The index into debugspawn::kEntries of the row named `id` (its log id), -1 none.
constexpr int Row(const char* id) noexcept {
    if(!id)return -1;
    for(int i=0;i<debugspawn::kEntryCount;++i)if(SameId(debugspawn::kEntries[i].id,id))return i;
    return -1;
}

namespace detail {
// kUnits is in catalogue order, so one walk over kEntries finds every unit's row (a unit absent from it or out of order
// leaves `found` short: the static_assert below).
struct Rows { int row[kUnitCount]; int found; };
constexpr Rows Resolve() noexcept {
    Rows r{};
    int u=0;
    for(int i=0;i<debugspawn::kEntryCount && u<kUnitCount;++i)
        if(SameId(debugspawn::kEntries[i].id,kUnits[u].id)){ r.row[u]=i; ++u; }
    r.found=u;
    return r;
}
inline constexpr Rows kRows=Resolve();
constexpr bool SideValid(Side s) noexcept { return static_cast<int>(s)>=0 && static_cast<int>(s)<kSideCount; }
}

// How many rows a side has.
constexpr int Count(Side side) noexcept {
    int n=0;
    for(const Unit& u:kUnits)if(u.side==side)++n;
    return n;
}
// The `n`th (0-based) playable row of `side`, in catalogue order; -1 none.
constexpr int NthRow(Side side,int n) noexcept {
    if(n<0)return -1;
    for(int i=0;i<kUnitCount;++i)
        if(kUnits[i].side==side && n--==0)return detail::kRows.row[i];
    return -1;
}
// Can a player of `side` be the debug spawn row `row`?
constexpr bool Playable(int row,Side side) noexcept {
    if(row<0 || row>=debugspawn::kEntryCount)return false;
    for(int i=0;i<kUnitCount;++i)if(detail::kRows.row[i]==row)return kUnits[i].side==side;
    return false;
}
// The row a side starts on: a plain, typical one (the giant ant; a Martian; a Hector), else the side's first. -1 when the
// side has none (the EDF).
constexpr int DefaultRow(Side side) noexcept {
    const char* want=nullptr;
    switch(side) {
    case Side::monster: want="GIANTANT01"; break;
    case Side::primer: want="E601_MARTIAN_GS"; break;
    case Side::foreigner: want="HECTOR301CC"; break;
    default: break;
    }
    const int row=Row(want);
    return Playable(row,side) ? row : NthRow(side,0);
}
// The row `step` places along the side's list from `row`, wrapping at both ends. A row that is not the side's starts from
// its first (step>0) or last (step<0); step 0 on it gives the default. -1 when the side has none.
constexpr int Next(int row,Side side,int step) noexcept {
    const int n=Count(side);
    if(n==0)return -1;
    int at=-1;
    for(int k=0;k<n;++k)if(NthRow(side,k)==row){at=k;break;}
    if(at<0)return step>0 ? NthRow(side,0) : step<0 ? NthRow(side,n-1) : DefaultRow(side);
    const long long to=((static_cast<long long>(at)+step)%n+n)%n;
    return NthRow(side,static_cast<int>(to));
}

constexpr bool UnitsValid() noexcept {
    for(const Unit& u:kUnits) {
        const int row=Row(u.id);
        if(row<0 || !detail::SideValid(u.side))return false;
        const debugspawn::Category c=debugspawn::kEntries[row].category;
        if(c!=debugspawn::Category::enemy && c!=debugspawn::Category::enemy41)return false;
    }
    return true;
}
static_assert(kUnitCount>0);
static_assert(UnitsValid(),"every pvp_units.inc id is an enemy / enemy41 row of the debug spawn catalogue");
static_assert(detail::kRows.found==kUnitCount,"pvp_units.inc is in the catalogue's order, each id once");
static_assert(Count(Side::monster)>0 && Count(Side::primer)>0 && Count(Side::foreigner)>0,"every enemy side has a unit");
static_assert(Count(Side::monster)+Count(Side::primer)+Count(Side::foreigner)==kUnitCount,"no unit is the EDF's");
static_assert(DefaultRow(Side::monster)>=0 && DefaultRow(Side::primer)>=0 && DefaultRow(Side::foreigner)>=0);
}
}
