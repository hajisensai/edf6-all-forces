// Weapon records past 2048: the pure part (layout, the patched constants, the sidecar file). No Windows, no game.
//
// Ported from (and cross-checked against) "EDF6 WeaponLimit" v1.0, Copyright (c) 2026 AnonymousHello, MIT License
// (the full notice: third_party/EDF6WeaponLimit/LICENSE): its relocation of the game status's weapon records to +0x150A8, the
// constant groups A-D, the object size. Our own: the instruction-exact site list (src/weapon_records_sites.inc,
// every row the whole instruction, never a byte scan over a function range), the weapon select screen through two
// redirected reads instead of a widened copy, and persistence at the game's own save / load / delete instead of a
// polling thread (src/weapon_records.cpp, docs/loadout-re.md 8.8).
//
// EDF6 keeps per weapon-table row a 12-byte record (u32 flags: bit0 owned, bit1 used, bit2 NEW, bit3 UP; 8 star
// bytes) at GS+0xEB50+id*12, 0x800 of them: the tail of the save block GS+0x6E68 (0xDCE8 bytes, MAIN.GST), followed
// by other members at GS+0x14B50. The plugin moves every access to GS+0x150A8 (past the whole 0x150A0-byte object,
// whose allocation grows to hold kSlots records). The native block keeps its old table as the save's copy of rows
// 0..0x7FF: mirrored from the live table before every copy out of the block (save, status records), read back after
// every copy into it (load, record restore, new game). Rows 0x800.. live only in the relocated table and in a sidecar
// file next to MAIN.GST.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::wrec {
constexpr std::uint32_t kRecord=12;
constexpr std::uint32_t kNativeSlots=0x800;                  // what the save block holds
constexpr std::uint32_t kSlots=0x2000;                       // what the relocated table holds (tools/call_weapons.py SAVE_WEAPONS)
constexpr std::size_t kBlock=0x6E68,kBlockSize=0xDCE8;       // GS: the save block (MAIN.GST)
constexpr std::size_t kOldTable=0xEB50;                      // GS: the native table (the block's tail)
constexpr std::size_t kOldInBlock=kOldTable-kBlock;          // 0x7CE8
constexpr std::size_t kObjectSize=0x150A0;                   // the game status as the game allocates it
constexpr std::size_t kNewTable=0x150A8;                     // the first multiple of 12 past it
constexpr std::size_t kNewInBlock=kNewTable-kBlock;          // 0xE240
constexpr std::size_t kNewObjectSize=kNewTable+std::size_t{kSlots}*kRecord;
constexpr std::size_t kNativeBytes=std::size_t{kNativeSlots}*kRecord;            // 0x6000
constexpr std::size_t kExtBytes=std::size_t{kSlots-kNativeSlots}*kRecord;        // rows 0x800..kSlots-1
constexpr std::size_t kTableBytes=std::size_t{kSlots}*kRecord;
static_assert(kOldTable+kNativeBytes==kBlock+kBlockSize,"the native table is the block's tail");
// The game folds (id + K) * 12 = id*12 + offset (0x139C for GS+0xEB50, 0xA69 for block+0x7CEC): the new offsets must
// be multiples of 12 too, or no K exists.
static_assert(kNewTable%kRecord==0 && (kNewInBlock+4)%kRecord==0,"folded forms need offsets divisible by 12");
static_assert(kNewTable>=kObjectSize,"the table lies past every member of the object");

// What one of the game's constants becomes (0: not one of ours).
constexpr std::uint32_t Relocated(std::uint32_t v) noexcept {
    switch(v) {
    case 0xEB50: return static_cast<std::uint32_t>(kNewTable);            // GS-based flags
    case 0xEB54: return static_cast<std::uint32_t>(kNewTable+4);          // GS-based stars
    case 0xEB44: return static_cast<std::uint32_t>(kNewTable-kRecord);    // a loop pointer one record behind (0xAECFB)
    case 0x7CE8: return static_cast<std::uint32_t>(kNewInBlock);          // block-based flags
    case 0x7CEC: return static_cast<std::uint32_t>(kNewInBlock+4);        // block-based stars
    case 0x7CF4: return static_cast<std::uint32_t>(kNewInBlock+kRecord); // record 0's stars' end (0xDF660: memset bound)
    case 0x139C: return static_cast<std::uint32_t>(kNewTable/kRecord);    // (id + 0x139C) * 12 = GS+0xEB50 + id*12
    case 0xA69:  return static_cast<std::uint32_t>((kNewInBlock+4)/kRecord);   // (id + 0xA69) * 12 = block+0x7CEC + id*12
    case 0x800:  return kSlots;                                           // the helpers' record count (0xDF130..)
    case 0x7FF:  return kSlots-1;                                         // 0xDF170: the last record
    case 0x150A0: return static_cast<std::uint32_t>(kNewObjectSize);      // the game status's allocation (12 sites)
    default: return 0;
    }
}

struct Site { std::uint32_t rva; std::uint8_t size,field; std::uint32_t value; std::uint8_t code[12]; };
inline constexpr Site kSites[]={
#include "weapon_records_sites.inc"
};
constexpr std::size_t kSiteCount=sizeof(kSites)/sizeof(kSites[0]);

// The other code src/weapon_records.cpp writes (EDF.dll RVAs): the block copies, the save / load / delete calls it
// redirects, and the weapon select screen's two reads of its snapshot's table, each replaced whole by a jump.
constexpr unsigned kMemcpy=0x12DA77A;                     // jmp [memcpy]
constexpr unsigned kBlockInit=0xDEDA0,kBlockInitCall=0xD6468;   // the game status's constructor 0xD62C0
constexpr unsigned kOutCalls[]={0x70AF25,0x70B079,0x70B61C,0x70B79C};   // memcpy(record, GS block, 0xDCE8)
constexpr unsigned kInCalls[]={0x70B23A,0x70B421,0x70B956};            // memcpy(GS block, record, 0xDCE8)
constexpr unsigned kNewGameCall=0xDB96D;                  // 0xDB920: memcpy(GS block, template, 0xDCE8)
// A load of the block goes through two copies (docs/loadout-re.md 8.8, 2026-10-26). The load job 0x7C1670 (save
// thread) decodes the file into ITS OWN result buffer: 0x7C1AD1 memcpy([[job+0xB0]+8], decoded, size), never the block.
// The request's completion 0x7BE970 then copies that buffer into the request's destination: [req+0x90] the block
// (0x7C05F6 asks with GS+0x6E68), [req+0x98] its size 0xDCE8 (0x7BD7A6 / 0x7BD7AD), only when the status is not an
// error and the sizes agree; its memcpy is a tail jump (E9 at 0x7BE99D) with the size from the request, so no list
// of "memcpy calls with 0xDCE8" finds it. Unwrapped, every load filled only the block's old table and the game went
// on with the relocated table it had: an empty one after the constructor, so EDF6's own weapons were gone and only
// what the plugin and the DLC grant re-own showed; the next save mirrored that into MAIN.GST.
constexpr unsigned kLoadCopyCall=0x7C1AD1;                // the load job 0x7C1670: memcpy(its buffer, decoded, size)
constexpr unsigned kResultCopy=0x7BE970,kResultJump=0x7BE99D;   // the completion: jmp memcpy([req+0x90], buffer, [req+0x98])
constexpr unsigned kUiSnapCall=0x8B49B1;                  // HUiHQWeaponSelect 0x8B4280: memcpy(screen+0x800, block, 0xDCE8)
constexpr unsigned kSaveCall=0x7C2F66,kSave=0x7BCD40;     // MAIN.GST's save request (0x7C2E30), 9 arguments
constexpr unsigned kWriteCall=0x7C228B,kWrite=0x7B9990;   // the save job 0x7C1CB0 writes a file
constexpr unsigned kReadOpen=0x7B90CD,kWriteOpen=0x7B9C28;   // call [CreateFileW] in 0x7B8DE0 / 0x7B9990
constexpr unsigned kDeleteCalls[]={0x7B69F1,0x7B7FEB};    // call [DeleteFileW] in 0x7B61B0 / 0x7B7EC0
// The screen's two reads of its snapshot's table (screen+0x800 = the block's copy: +0x7CE8 / +0x84E8 = block+0x7CE8):
// both go to a copy of the relocated table (UiStub). Neither is a constant site: relocating 0x8B4F70's 0x7CE8 would
// read past the 0xDCE8-byte snapshot, and it would change the bytes the jump expects there, so the jump's write fails
// and the whole relocation is rolled back (build 471bfc5, 2026-10-25: "writing the code failed at the weapon select
// screen", then the game's writes to rows 0x800.. land on GS+0x14B50 and the boot crashes at EDF+0xCDAE6).
constexpr unsigned kUiOwned=0x8B4F70,kUiStatus=0x8B51A0;
constexpr std::uint8_t kUiReadSize=8;

// Each patch of the other code, [rva, rva+size): none may share a byte with a constant site, or with another of them
// (two writes over one instruction: the second finds the first's bytes and fails).
struct Span { std::uint32_t rva; std::uint8_t size; };
inline constexpr Span kOtherPatches[]={
    {kOutCalls[0],5},{kOutCalls[1],5},{kOutCalls[2],5},{kOutCalls[3],5},{kInCalls[0],5},{kInCalls[1],5},{kInCalls[2],5},
    {kNewGameCall,5},{kLoadCopyCall,5},{kResultJump,5},{kUiSnapCall,5},{kBlockInitCall,5},{kSaveCall,5},{kWriteCall,5},
    {kReadOpen,6},{kWriteOpen,6},{kDeleteCalls[0],6},{kDeleteCalls[1],6},{kUiOwned,kUiReadSize},{kUiStatus,kUiReadSize},
};
constexpr bool Overlap(std::uint32_t a,std::uint32_t an,std::uint32_t b,std::uint32_t bn) noexcept { return a<b+bn && b<a+an; }
constexpr bool PatchesApart() noexcept {
    for(const Span& p:kOtherPatches) {
        for(const Site& s:kSites)if(Overlap(p.rva,p.size,s.rva,s.size))return false;
        for(const Span& q:kOtherPatches)if(&p!=&q && Overlap(p.rva,p.size,q.rva,q.size))return false;
    }
    return true;
}
static_assert(PatchesApart(),"a redirected call or screen read shares bytes with a constant site or another patch");

// The site's own bytes are consistent: the constant sits at +field, it is one we relocate, the instruction fits.
constexpr bool SiteConsistent(const Site& s) noexcept {
    if(s.size<5 || s.size>12 || s.field+4u>s.size || !Relocated(s.value))return false;
    const std::uint32_t v=static_cast<std::uint32_t>(s.code[s.field])|static_cast<std::uint32_t>(s.code[s.field+1])<<8|
                          static_cast<std::uint32_t>(s.code[s.field+2])<<16|static_cast<std::uint32_t>(s.code[s.field+3])<<24;
    return v==s.value;
}
// The instruction with its constant relocated.
inline void Patched(const Site& s,std::uint8_t* out) noexcept {
    std::memcpy(out,s.code,s.size);
    const std::uint32_t v=Relocated(s.value);
    std::memcpy(out+s.field,&v,4);
}

// FNV-1a 64 over bytes: tells a save job's copy of the block's table from another's (src/weapon_records.cpp).
inline std::uint64_t Hash(const void* data,std::size_t size) noexcept {
    std::uint64_t h=0xCBF29CE484222325ull;
    const auto* p=static_cast<const unsigned char*>(data);
    for(std::size_t i=0;i<size;++i){h^=p[i];h*=0x100000001B3ull;}
    return h;
}

// --- Which save request a write of MAIN.GST belongs to (src/weapon_records.cpp SaveHook / WriteHook) ---
// A request remembers rows 0x800.. under the hash of the block's table it copied (rows 0..0x7FF); the write says only
// that hash. Several requests can share one (only rows past 0x800 changed between them: a weapon past 0x800 picked up),
// and a request may never be written: the save job 0x7C1CB0 leaves for 0x7C2318 on an error before its write 0x7C228B.
// Taking the oldest request with the hash then pairs every later write with the request before its own, for as long
// as the table keeps that hash (each save one request behind). The write takes the NEWEST request with its hash: same
// hash, same rows 0..0x7FF, and rows 0x800.. from the latest request; the earlier ones with that hash are done (written
// already, or never to be) and go, the newest stays for its own write still to come (it takes the same one).
// `hashes`: the remembered requests' hashes, oldest first. Returns the index taken, `n` when none has `hash`.
constexpr std::size_t PickSave(const std::uint64_t* hashes,std::size_t n,std::uint64_t hash) noexcept {
    std::size_t at=n;
    for(std::size_t i=0;i<n;++i)if(hashes[i]==hash)at=i;
    return at;
}

// --- Which file a load's copy into the block came from (src/weapon_records.cpp LoadCopyHook / ResultCopyHook) ---
// The load job remembers its result buffer with the path it read (save thread); the completion's copy into the block
// names that buffer as its source. A buffer address can come back for a later load: the newest entry with it.
// `buffers`: the remembered loads' buffers, oldest first. Returns the index taken, `n` when none has `src`.
constexpr std::size_t PickLoad(const void* const* buffers,std::size_t n,const void* src) noexcept {
    std::size_t at=n;
    for(std::size_t i=0;i<n;++i)if(src && buffers[i]==src)at=i;
    return at;
}

// --- The sidecar: rows kNativeSlots.. of one save (MAIN.GST.edf6vc next to MAIN.GST) ---
// "EDF6VCWR" | u32 version 1 | u32 first row (0x800) | u32 rows | u32 FNV-1a-32 of the records | rows*12 bytes.
constexpr char kMagic[8]={'E','D','F','6','V','C','W','R'};
constexpr std::uint32_t kVersion=1;
constexpr std::size_t kHeader=24;
constexpr std::size_t kSidecarSize=kHeader+kExtBytes;

inline std::uint32_t Hash32(const void* data,std::size_t size) noexcept {
    std::uint32_t h=0x811C9DC5u;
    const auto* p=static_cast<const unsigned char*>(data);
    for(std::size_t i=0;i<size;++i){h^=p[i];h*=0x01000193u;}
    return h;
}
// The rows past the save's last used one are left out (a save with none past 0x800 writes a header only).
inline std::uint32_t UsedRows(const std::uint8_t* ext) noexcept {
    std::uint32_t n=kSlots-kNativeSlots;
    while(n) {
        const std::uint8_t* r=ext+std::size_t{n-1}*kRecord;
        bool zero=true;
        for(std::uint32_t i=0;i<kRecord && zero;++i)zero=r[i]==0;
        if(!zero)break;
        --n;
    }
    return n;
}
// Writes the sidecar for `ext` (kExtBytes) into out (kSidecarSize); returns its length.
inline std::size_t Serialize(const std::uint8_t* ext,std::uint8_t* out) noexcept {
    const std::uint32_t rows=UsedRows(ext);
    const std::size_t bytes=std::size_t{rows}*kRecord;
    const std::uint32_t head[4]={kVersion,kNativeSlots,rows,Hash32(ext,bytes)};
    std::memcpy(out,kMagic,8);
    std::memcpy(out+8,head,sizeof(head));
    std::memcpy(out+kHeader,ext,bytes);
    return kHeader+bytes;
}
enum class Parse { kOk, kShort, kMagic, kVersion, kLayout, kChecksum };
inline const char* ParseName(Parse p) noexcept {
    switch(p) {
    case Parse::kOk: return "ok";
    case Parse::kShort: return "truncated";
    case Parse::kMagic: return "not a weapon record file";
    case Parse::kVersion: return "unknown version";
    case Parse::kLayout: return "unexpected first row";
    case Parse::kChecksum: return "checksum mismatch";
    }
    return "?";
}
// Reads a sidecar into ext (kExtBytes; rows it does not have are zero). Anything but kOk leaves ext all zero.
inline Parse Deserialize(const std::uint8_t* data,std::size_t size,std::uint8_t* ext) noexcept {
    std::memset(ext,0,kExtBytes);
    if(size<kHeader)return Parse::kShort;
    if(std::memcmp(data,kMagic,8))return Parse::kMagic;
    std::uint32_t head[4];
    std::memcpy(head,data+8,sizeof(head));
    if(head[0]!=kVersion)return Parse::kVersion;
    if(head[1]!=kNativeSlots)return Parse::kLayout;
    const std::size_t bytes=std::size_t{head[2]}*kRecord;
    if(size<kHeader+bytes)return Parse::kShort;
    if(Hash32(data+kHeader,bytes)!=head[3])return Parse::kChecksum;
    // A file from a build with more slots: the rows this one has room for. The caller logs the file's row count,
    // since the next save writes this build's rows only.
    std::memcpy(ext,data+kHeader,bytes<kExtBytes ? bytes : kExtBytes);
    return Parse::kOk;
}
inline std::uint32_t SidecarRows(const std::uint8_t* data,std::size_t size) noexcept {
    if(size<kHeader)return 0;
    std::uint32_t rows=0;
    std::memcpy(&rows,data+16,4);
    return rows;
}

// --- The game status's table, both copies (gs = the object; every pointer checked by the caller) ---
// Rows 0..0x7FF of the live table into the block's old table: what a copy out of the block must carry.
inline void MirrorToBlock(std::uint8_t* gs) noexcept { std::memmove(gs+kOldTable,gs+kNewTable,kNativeBytes); }
// The block's old table into rows 0..0x7FF of the live table: after a copy into the block.
inline void ReadFromBlock(std::uint8_t* gs) noexcept { std::memmove(gs+kNewTable,gs+kOldTable,kNativeBytes); }
inline std::uint8_t* Ext(std::uint8_t* gs) noexcept { return gs+kNewTable+kNativeBytes; }
}  // namespace crew::wrec
