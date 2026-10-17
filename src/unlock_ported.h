// The ported content unlocked for the player who asks (ini PortedMissionsUnlocked, PortedWeaponsUnlocked; both 0 by
// default): the parts that need no game, checked offline (tools/unlock_ported_check.cpp). The hooks that use them:
//  - missions: src/edf5campaign.cpp InstallPortedMissions, the mission select's row states (docs/mission-list-re.md §3, §7);
//  - weapons: src/airstrike.cpp UnlockDlcHook, the save load's "grant the installed DLC weapons" step (docs/loadout-re.md §8).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace crew::ported {
constexpr int kPacks5=3;    // EDF5's story, DLC1, DLC2: content ids EDF5CampaignContent .. +2
constexpr int kPacks41=3;   // EDF4.1's story, Mission Pack 1, Mission Pack 2: EDF41Content .. +2

// One of the six ported story packs (each id 0: not installed). Not the test range, not EDF6's own (0..2).
constexpr bool StoryPack(int id,int edf5,int edf41) noexcept {
    return (edf5>0 && id>=edf5 && id<edf5+kPacks5) || (edf41>0 && id>=edf41 && id<edf41+kPacks41);
}
static_assert(StoryPack(3,3,0) && StoryPack(5,3,0) && !StoryPack(6,3,0) && !StoryPack(0,3,7) && !StoryPack(2,3,7));
static_assert(StoryPack(7,3,7) && StoryPack(9,3,7) && !StoryPack(10,3,7) && !StoryPack(6,3,7) && !StoryPack(3,0,0));

// A row of the mission select's list (0x8A1530, built at 0x8A2160..0x8A241A): 0x14 bytes. +0 the list row, +4 the
// shown number (field 4), +8 the state, +0xC field 5 (both filled while the scan reaches the row), +0x10 a byte.
// States: 0 cleared, 1 open, 2 open (a branch row: field 5 is 1), 3 locked but shown, 4 locked and hidden.
struct ListRow {
    std::int32_t row;
    std::int32_t number;
    std::int32_t state;
    std::int32_t branch;
    std::uint8_t flag;
    std::uint8_t pad[3];
};
static_assert(sizeof(ListRow)==0x14);
constexpr std::int32_t kCleared=0,kOpen=1,kOpenBranch=2,kHidden=4;

// The rows as the list's first loop leaves them (0x8A2160..0x8A219D): row i at i, row 0 open, every other hidden.
// Anything else is not the list this was read from: false, nothing touched.
inline bool Fresh(const ListRow* rows,std::size_t n) noexcept {
    if(!rows || n==0 || rows[0].row!=0 || rows[0].state!=kOpen)return false;
    for(std::size_t i=1;i<n;++i)
        if(rows[i].row!=static_cast<std::int32_t>(i) || rows[i].state!=kHidden)return false;
    return true;
}
// Every hidden row open, as the scan opens a cleared row's successor (0x8A2222..0x8A2262: 2 when its field 5 is 1,
// else 1), before the scan: it then never meets a hidden row it has not cleared (0x8A2330), so it never stops, and
// a row cleared before keeps its 0 (the scan writes it). Nothing is written to the save. How many were opened; -1
// when the rows are not Fresh. branch(row) = the list's field 5.
template<class Branch> int OpenAll(ListRow* rows,std::size_t n,Branch branch) noexcept {
    if(!Fresh(rows,n))return -1;
    int opened=0;
    for(std::size_t i=1;i<n;++i) {
        rows[i].state=branch(static_cast<unsigned>(i))==1 ? kOpenBranch : kOpen;
        ++opened;
    }
    return opened;
}

// Our ported weapons' rows (tools/ported_weapons.py GAMES: EDF5's and EDF4.1's, every registry). A placeholder
// (EDF6VC_RETIRED_*: uninstalled, or waiting for its game) is not one, nor the call weapons (EDF6VC_CALL_*).
constexpr wchar_t kPrefix5[]=L"EDF6VC_E5_";
constexpr wchar_t kPrefix41[]=L"EDF6VC_E41_";
constexpr std::size_t kLongestPrefix=sizeof(kPrefix41)/sizeof(wchar_t)-1;
inline bool PortedWeapon(const wchar_t* name) noexcept {
    if(!name)return false;
    return std::wcsncmp(name,kPrefix5,sizeof(kPrefix5)/sizeof(wchar_t)-1)==0
        || std::wcsncmp(name,kPrefix41,kLongestPrefix)==0;
}

// A weapon record of the game status (GS+0xEB50 + row*12: u32 flags, bit0 owned, bit2 NEW; 8 star bytes) owned, as
// 0xDC550 gives an installed DLC's weapon: the first time NEW and 0 stars, after that the stars left alone. Whether
// it was not owned yet. Never clears a bit.
inline bool Own(unsigned char* record) noexcept {
    std::uint32_t flags;
    std::memcpy(&flags,record,4);
    const bool fresh=!(flags&1);
    if(fresh) {
        flags|=4;
        std::memset(record+4,0,8);
    }
    flags|=1;
    std::memcpy(record,&flags,4);
    return fresh;
}
}  // namespace crew::ported
