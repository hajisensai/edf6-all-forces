// The new classes' save (src/soldier_save.cpp; docs/plan-2026-11-28-new-classes-impl.md 模块 D, docs/newclass-re/
// inject.md §4, architecture.md §3.3): the pure part. No Windows, no game.
//
// The save block (MAIN.GST) is GS+0x6E68, 0xDCE8 bytes (src/weapon_records_logic.h kBlock / kBlockSize: the weapon
// records' relocation moves their live table past the object, the block keeps its layout and its length). Its two
// local players' records follow its ten armour counts, 0x3E60 bytes each (GS constructor 0xDEDA0: two passes,
// r10 += 0x3E60 at 0xDEF20): +0 the current class (GS+0x6E90+p*0x3E60), then ten classes x six equipment slots.
// Class 4 / 5 are real classes while the plugin runs (SoldierInit's elements 4 and 5); the stock game indexes
// SoldierInit with the current class unchecked, so the file never holds one past 3: the save job's copy gets a stock
// class (the stand-in, Ranger) and the real one goes into a slot no code reads, class 9's last equipment slot of the
// same player record (its slots are reached only through indices below the class count, 0xE34D0; class 9 is never
// given to a class). On load, the real class comes back only while the class is still the stand-in (the player did not
// pick another one in a stock game meanwhile); the slot is cleared either way. One file, written as one: never two
// copies out of step.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::soldier_save {
constexpr std::size_t kBlockSize=0xDCE8;              // the save block, as weapon_records_logic.h kBlockSize
constexpr std::size_t kLocalPlayers=2;                // this machine's players: two records in the block
constexpr std::size_t kPlayerStride=0x3E60;           // one player record
constexpr std::size_t kClassAt=0x28;                  // GS+0x6E90 - GS+0x6E68: the record's current class
constexpr std::size_t kSlotsAt=0x30;                  // GS+0x6E98: class 0's slot 0 (docs/loadout-re.md)
constexpr int kClassSlots=6,kReservedClasses=10;      // per class, per record (0xDEDA0: mov ecx,0xA; 0x18 bytes each)
constexpr int kRecordClass=9,kRecordSlot=5;           // the slot the real class is kept in
constexpr std::size_t kRecordAt=kSlotsAt+4u*(std::size_t{kRecordClass}*kClassSlots+kRecordSlot);   // 0x11C
constexpr int kStockClasses=4;                        // 0 Ranger, 1 Wing Diver, 2 Air Raider, 3 Fencer
constexpr int kStandIn=0;                             // what the file says for a class past the stock ones
constexpr std::uint32_t kNoRecord=0xFFFFFFFFu;        // the slot's own initial value (0xDEDA0 sets every slot to -1)
constexpr std::uint32_t kMagic=0x5A570000u,kMagicMask=0xFFFF0000u;   // "ZW" | real class << 8 | stand-in

static_assert(kRecordAt==0x11C,"class 9 slot 5: GS+0x6F84 (player 0)");
// The ten classes' slots end where the record's armour counts begin (0xDEDA0: r10 = GS+0x6E90, r10+0xF8).
static_assert(kSlotsAt+4u*std::size_t{kReservedClasses}*kClassSlots==kClassAt+0xF8,"ten classes x six slots");
static_assert(kRecordAt+4==kSlotsAt+4u*std::size_t{kReservedClasses}*kClassSlots,"the last slot of the last class");
static_assert(kRecordAt+4<=kPlayerStride && kClassAt<kRecordAt,"inside one player record");
static_assert((kLocalPlayers-1)*kPlayerStride+kRecordAt+4<=kBlockSize,"player 1's slot is inside the saved block");
static_assert(0x6E68+kClassAt+kLocalPlayers*kPlayerStride==0xEB50,"two records end at the block's weapon records");

constexpr std::size_t ClassOffset(std::size_t p) noexcept { return p*kPlayerStride+kClassAt; }
constexpr std::size_t RecordOffset(std::size_t p) noexcept { return p*kPlayerStride+kRecordAt; }

constexpr std::uint32_t Encode(int real,int standIn) noexcept {
    return kMagic|(static_cast<std::uint32_t>(real)&0xFFu)<<8|(static_cast<std::uint32_t>(standIn)&0xFFu);
}
// A record of ours: a real class past the stock ones, a stock stand-in.
constexpr bool Decode(std::uint32_t v,int* real,int* standIn) noexcept {
    if((v&kMagicMask)!=kMagic)return false;
    const int r=static_cast<int>(v>>8&0xFFu),s=static_cast<int>(v&0xFFu);
    if(r<kStockClasses || s>=kStockClasses)return false;
    *real=r;*standIn=s;
    return true;
}

// --- the save job's copy ---
struct Saved { int cls; std::uint32_t record; bool standIn; };
// What one player's record in the copy becomes: a stock class (or anything not a class) stays, the slot says "none";
// a class past the stock ones is written as the stand-in, the slot keeps it (a class too big for the record: the
// stand-in all the same, no record; a file with it would crash the stock game).
constexpr Saved ForSave(int cls) noexcept {
    if(cls<kStockClasses)return {cls,kNoRecord,false};
    if(cls>0xFF)return {kStandIn,kNoRecord,true};
    return {kStandIn,Encode(cls,kStandIn),true};
}

// --- after a load into the game status ---
enum class Loaded {
    kNone,          // no record: the class as the file says
    kRestored,      // the class was the stand-in: the real one back
    kChanged,       // a record, but the class is not its stand-in any more (a stock game picked another): the file's
    kUnavailable,   // a record whose class this session does not have (the new classes not injected): the file's
};
struct Restore { int cls; Loaded what; int real; };
// `classes`: the class count of this session's SoldierInit (0xE34D0), 0 when unknown.
constexpr Restore ForLoad(int cls,std::uint32_t record,int classes) noexcept {
    int real=-1,standIn=-1;
    if(!Decode(record,&real,&standIn))return {cls,Loaded::kNone,-1};
    if(cls!=standIn)return {cls,Loaded::kChanged,real};
    if(real>=classes)return {cls,Loaded::kUnavailable,real};
    return {real,Loaded::kRestored,real};
}

inline std::int32_t ReadInt(const std::uint8_t* block,std::size_t at) noexcept {
    std::int32_t v;
    std::memcpy(&v,block+at,4);
    return v;
}
inline void WriteInt(std::uint8_t* block,std::size_t at,std::uint32_t v) noexcept { std::memcpy(block+at,&v,4); }

// The save job's copy of the block (kBlockSize bytes), both players; `out` (kLocalPlayers) what each was and became.
struct SaveResult { int was; Saved now; };
inline int PrepareSaveCopy(std::uint8_t* copy,SaveResult* out) noexcept {
    int standIns=0;
    for(std::size_t p=0;p<kLocalPlayers;++p) {
        const int was=ReadInt(copy,ClassOffset(p));
        const Saved s=ForSave(was);
        WriteInt(copy,ClassOffset(p),static_cast<std::uint32_t>(s.cls));
        WriteInt(copy,RecordOffset(p),s.record);
        out[p]={was,s};
        standIns+=s.standIn;
    }
    return standIns;
}
// The game status's block right after a load filled it, both players; the slot cleared for each. Returns how many
// players got their real class back.
struct LoadResult { int was; Restore now; };
inline int RestoreLoaded(std::uint8_t* block,int classes,LoadResult* out) noexcept {
    int restored=0;
    for(std::size_t p=0;p<kLocalPlayers;++p) {
        const int was=ReadInt(block,ClassOffset(p));
        const Restore r=ForLoad(was,static_cast<std::uint32_t>(ReadInt(block,RecordOffset(p))),classes);
        WriteInt(block,ClassOffset(p),static_cast<std::uint32_t>(r.cls));
        WriteInt(block,RecordOffset(p),kNoRecord);
        out[p]={was,r};
        restored+=r.what==Loaded::kRestored;
    }
    return restored;
}
}  // namespace crew::soldier_save
