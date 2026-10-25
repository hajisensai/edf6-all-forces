// The native TeamManager's capacity and the relation overlay, as data (docs/team-manager-re.md). No game access:
// team_native.cpp installs it, tests/team_native_test.cpp and tests/team_native_sig_audit.py check it.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew::team_native {
// Teams 0..6 keep their stock meaning (1 enemy, 2 EDF, 3 neutral, 4 enemy of 1 only, 5 empty vehicles, 6 the
// mission's environment). The PvP factions take 7..kTeamCapacity-1.
constexpr int kNativeTeams=7,kTeamCapacity=16,kFirstFactionTeam=kNativeTeams;
constexpr std::size_t kRowStride=0x38;
// A team row once the capacity is installed (the native team field +0x314 and its kept last team +0x318), and the same
// or -1: a closing object's team (SetTeam -1 keeps +0x318, docs/enemy-control-target-re.md).
constexpr bool TeamRow(int t) noexcept { return t>=0 && t<kTeamCapacity; }
constexpr bool TeamOrClosing(int t) noexcept { return t==-1 || TeamRow(t); }
// Relation values as TeamManager stores them (row a, column b: how a sees b).
constexpr std::uint8_t kNeutral=0,kFriend=1,kEnemy=2;

// The manager's constructor 0x5E03C0 sizes its rows and every row's relation vector with the constant 7, and the
// enumeration of every team 0x5E0C80 walks 7 rows (0x188 = 7 * 0x38) where everything else reads the row count
// (+0x48) or the vector's own size. Each site, its stock bytes and the bytes that make it kTeamCapacity.
struct Site { std::uint32_t rva; std::uint8_t size; std::uint8_t stock[8]; std::uint8_t patched[8]; };
constexpr std::uint8_t kCap=static_cast<std::uint8_t>(kTeamCapacity);
constexpr std::uint32_t kCapRows=static_cast<std::uint32_t>(kTeamCapacity*kRowStride);
constexpr std::uint8_t kRowsLo=static_cast<std::uint8_t>(kCapRows&0xFF),kRowsHi=static_cast<std::uint8_t>(kCapRows>>8);
constexpr Site kSites[]={
    {0x5E0409,5,{0x41,0x8D,0x54,0x24,0x07},{0x41,0x8D,0x54,0x24,kCap}},              // lea edx,[r12+7]: reserve
    {0x5E045D,5,{0x48,0x83,0x7E,0x40,0x07},{0x48,0x83,0x7E,0x40,kCap}},              // cmp [rsi+40h],7: capacity
    {0x5E0464,5,{0x41,0x8D,0x54,0x24,0x07},{0x41,0x8D,0x54,0x24,kCap}},              // lea edx,[r12+7]: reserve
    {0x5E047A,5,{0xBA,0x07,0x00,0x00,0x00},{0xBA,kCap,0x00,0x00,0x00}},              // mov edx,7: rows
    {0x5E04FB,5,{0x48,0x83,0x79,0x10,0x07},{0x48,0x83,0x79,0x10,kCap}},              // row: relation capacity
    {0x5E0502,5,{0xBA,0x07,0x00,0x00,0x00},{0xBA,kCap,0x00,0x00,0x00}},              // row: reserve
    {0x5E0514,5,{0x48,0x83,0x7B,0x20,0x07},{0x48,0x83,0x7B,0x20,kCap}},              // row: capacity
    {0x5E051F,4,{0x48,0x83,0xF9,0x07},{0x48,0x83,0xF9,kCap}},                        // row: fill loop
    {0x5E053B,4,{0x48,0x83,0xF9,0x07},{0x48,0x83,0xF9,kCap}},
    {0x5E0543,5,{0xBA,0x07,0x00,0x00,0x00},{0xBA,kCap,0x00,0x00,0x00}},
    {0x5E0559,4,{0x48,0x83,0xF9,0x07},{0x48,0x83,0xF9,kCap}},
    {0x5E056B,4,{0x48,0x83,0xF9,0x07},{0x48,0x83,0xF9,kCap}},
    {0x5E0571,8,{0x48,0xC7,0x43,0x28,0x07,0x00,0x00,0x00},{0x48,0xC7,0x43,0x28,kCap,0x00,0x00,0x00}},   // row size
    {0x5E057D,7,{0x49,0x81,0xFE,0x88,0x01,0x00,0x00},{0x49,0x81,0xFE,kRowsLo,kRowsHi,0x00,0x00}},       // rows loop
    {0x5E0D28,7,{0x48,0x81,0xFE,0x88,0x01,0x00,0x00},{0x48,0x81,0xFE,kRowsLo,kRowsHi,0x00,0x00}},       // every team
};
constexpr std::size_t kSiteCount=sizeof(kSites)/sizeof(kSites[0]);

// The stock defaults 0x5E1E90 (mission start / reload) writes back, and the identity 0x5E1F90: every relation the
// overlay set is gone after either, so the overlay is written again right after them (these call sites).
constexpr std::uint32_t kManager=0x20B2978,kRows=0x38,kRowCount=0x48,kRelData=0x18,kRelSize=0x28,kLock=0x08;
constexpr std::uint32_t kResetDefaults=0x5E1E90,kResetIdentity=0x5E1F90,kLockEnter=0x50BC0,kLockLeave=0x50BE0;
struct ResetSite { std::uint32_t rva; std::uint32_t target; };
constexpr ResetSite kResetSites[]={
    {0x1DEF15,kResetDefaults},{0x210332,kResetDefaults},{0x228607,kResetDefaults},
    {0x21041F,kResetIdentity},{0x228F17,kResetIdentity},
};

// Stock relations (ctor 0x5E0600..0x5E06D6 = 0x5E1E90), extended with neutral rows for the faction teams.
constexpr std::uint8_t StockRelation(int a,int b) noexcept {
    if(a==b)return kFriend;
    const auto pair=[a,b](int x,int y){return (a==x && b==y) || (a==y && b==x);};
    if(pair(0,1) || pair(1,2) || pair(1,4))return kEnemy;
    if(pair(0,2))return kFriend;
    return kNeutral;
}

// The relations a PvP match wants. Only the rows/columns a match uses differ from stock; a team's own diagonal stays
// friendly (the native walks and the bullets assume it).
struct Overlay {
    std::uint8_t rel[kTeamCapacity][kTeamCapacity]{};
    void Stock() noexcept { for(int a=0;a<kTeamCapacity;++a)for(int b=0;b<kTeamCapacity;++b)rel[a][b]=StockRelation(a,b); }
    bool Valid() const noexcept {
        for(int a=0;a<kTeamCapacity;++a)for(int b=0;b<kTeamCapacity;++b) {
            if(rel[a][b]>kEnemy)return false;
            if(a==b && rel[a][b]!=kFriend)return false;
        }
        // Teams 5 (empty vehicles) and 6 (environment) keep their stock rows: the boarding prompt, the map's empty
        // vehicles and the spider webs read them as stock.
        for(int b=0;b<kTeamCapacity;++b)for(int t=5;t<=6;++t)
            if(rel[t][b]!=StockRelation(t,b) || rel[b][t]!=StockRelation(b,t))return false;
        return true;
    }
};
}  // namespace crew::team_native
