// The All Forces mission packs' identity (tools/make_edf5_campaign.py, docs/mod-room-isolation.md "Mission pack
// identity"). A room publishes only its content id (DLC) and the row (MISSION_NO); both are the same on every machine
// since 2026-10-18: the seven packs have fixed content ids (kBase..), and a mission a machine lacks keeps its row as a
// placeholder. What can still differ is the pack's definition (another All Forces version) and which rows a machine
// lacks: the installer writes both to EDF6VehicleCrew.packs.ini ([Pack<id>] Tag, Digest, Rows, Missing), the host
// publishes its pack's line with the room (kAttribute) and a guest checks it before the native JoinLobby. The same
// file gives each pack's save layout (Layout, StableRows, Legacy, Older / From<layout>): every save of a pack is
// marked with the layout its rows are in and moved to the rows now by its own marker and content (MigrateModeSave).
// Pure, no game: tests/mod_room_test.cpp and tests/edf5_campaign_native_test.cpp run it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "unlock_ported.h"

namespace crew::packs {
// Fixed: CONTENT_BASE + the pack's place in SLOTS (tools/make_edf5_campaign.py). EDF.dll knows 0..2; other mods take
// 3, 4, ... after the highest id; every read of the id is a 32-bit int (docs/mission-list-re.md §7).
constexpr int kBase=65,kCount=7;
constexpr int kEdf5=65,kRange=68,kEdf41=69;   // the ini's EDF5CampaignContent, TestRangeContent, EDF41Content
constexpr int kMaxRows=512;                   // a pack's save holds 512 missions (the installer refuses more)
constexpr char kAttribute[]="AF_PACK";        // the room's pack line (EOS string attribute), Format / Parse
constexpr bool Reserved(std::int64_t id) noexcept { return id>=kBase && id<kBase+kCount; }
static_assert(Reserved(65) && Reserved(71) && !Reserved(64) && !Reserved(72) && !Reserved(0) && !Reserved(2));

// Each pack's name for the player (the dialogs), by content id.
inline const wchar_t* Name(std::int64_t id) noexcept {
    static const wchar_t* const names[kCount]={L"EDF5 本篇",L"EDF5 任务包１",L"EDF5 任务包２",L"EDF6VehicleCrew 测试场",
        L"EDF4.1 本篇",L"EDF4.1 任务包１",L"EDF4.1 任务包２"};
    return Reserved(id) ? names[id-kBase] : L"（未知任务包）";
}

// An older layout of a pack's rows, as a save written with it has them: each of its rows' row now (-1: that mission is
// no longer in the pack). `from` is that layout's marker; 0 the saves from before the markers (2026-10-18).
struct LayoutMap {
    std::uint32_t from=0;
    int rows=0;
    std::int16_t row[kMaxRows]{};
};
constexpr int kMaxFrom=4;      // the older layouts a pack moves saves from (the installer keeps the most recent ones)
struct Pack {
    int id=0;                  // 0: no pack
    char tag[16]{};
    char digest[17]{};         // 16 hex digits: the definition (tag, every row's mission, the shipped scripts' hashes)
    int rows=0;
    std::uint8_t missing[kMaxRows/8]{};   // the placeholder rows: bit (row % 8) of byte row / 8
    // The save marker of these rows (MigrateModeSave): the rows' mission keys in order, hashed by the installer
    // (tools/make_edf5_campaign.py layout_id). Only the order of the missions: a script's change keeps it. Never 0.
    std::uint32_t layout=0;
    // Rows 0..stableRows-1 sat where they sit now in every layout an older install could have written (a mission an
    // install left out was always at or past this row): an unmarked save using only these rows needs no moving.
    int stableRows=0;
    int fromCount=0;
    LayoutMap from[kMaxFrom]{};   // the layouts a save is moved from; from 0: this machine's record of the unmarked one
    bool Missing(int row) const noexcept { return row>=0 && row<rows && (missing[row/8]>>(row%8)&1); }
    int MissingCount() const noexcept { int n=0; for(int i=0;i<rows;++i)n+=Missing(i); return n; }
    const LayoutMap* From(std::uint32_t marker) const noexcept {
        for(int i=0;i<fromCount;++i)if(from[i].from==marker)return &from[i];
        return nullptr;
    }
};
struct Table {
    Pack packs[kCount]{};
    const Pack* Find(std::int64_t id) const noexcept { return Reserved(id) && packs[id-kBase].id==id ? &packs[id-kBase] : nullptr; }
};

inline int HexDigit(char c) noexcept {
    return c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : c>='A' && c<='F' ? c-'A'+10 : -1;
}
// `hex`: the Missing mask as the installer writes it (row i: bit i % 4 of digit i / 4), exactly (rows + 3) / 4 digits
// and no bit past the last row. False on anything else.
inline bool ParseMask(const char* hex,int rows,std::uint8_t (&out)[kMaxRows/8]) noexcept {
    std::memset(out,0,sizeof(out));
    if(!hex || rows<1 || rows>kMaxRows || std::strlen(hex)!=static_cast<std::size_t>((rows+3)/4))return false;
    for(int k=0;hex[k];++k) {
        const int d=HexDigit(hex[k]);
        if(d<0)return false;
        for(int b=0;b<4;++b) {
            if(!(d>>b&1))continue;
            const int row=k*4+b;
            if(row>=rows)return false;
            out[row/8]|=static_cast<std::uint8_t>(1u<<(row%8));
        }
    }
    return true;
}
inline void FormatMask(const Pack& p,char* out,std::size_t size) noexcept {
    std::size_t n=0;
    for(int k=0;k<(p.rows+3)/4 && n+1<size;++k) {
        int d=0;
        for(int b=0;b<4;++b)d|=(p.Missing(k*4+b) ? 1 : 0)<<b;
        out[n++]="0123456789abcdef"[d];
    }
    if(size)out[n<size ? n : size-1]=0;
}
inline bool Token(const char* s,std::size_t max) noexcept {   // a tag or digest: [A-Za-z0-9], 1..max-1 characters
    const std::size_t n=s ? std::strlen(s) : 0;
    if(n==0 || n>=max)return false;
    for(std::size_t i=0;i<n;++i)if(!((s[i]>='0' && s[i]<='9') || (s[i]>='a' && s[i]<='z') || (s[i]>='A' && s[i]<='Z')))return false;
    return true;
}
// One installed pack from its [Pack<id>] values; false (nothing kept) on any malformed value. Its save layout is
// SetLayout's (a room's line carries none: a room compares definitions, not saves).
inline bool Make(int id,const char* tag,const char* digest,int rows,const char* mask,Pack& out) noexcept {
    Pack p{};
    if(!Reserved(id) || !Token(tag,sizeof(p.tag)) || !Token(digest,sizeof(p.digest)) || std::strlen(digest)!=16
       || !ParseMask(mask,rows,p.missing))return false;
    for(int i=0;i<16;++i)if(HexDigit(digest[i])<0)return false;
    p.id=id;p.rows=rows;
    std::memcpy(p.tag,tag,std::strlen(tag)+1);
    std::memcpy(p.digest,digest,17);
    out=p;
    return true;
}
// A layout id as the installer writes it: exactly 8 hex digits, not 0.
inline bool ParseLayout(const char* hex,std::uint32_t& out) noexcept {
    if(!hex || std::strlen(hex)!=8)return false;
    std::uint32_t v=0;
    for(int i=0;i<8;++i) {
        const int d=HexDigit(hex[i]);
        if(d<0)return false;
        v=v<<4|static_cast<std::uint32_t>(d);
    }
    if(!v)return false;
    out=v;
    return true;
}
// Layout=<8 hex digits> and StableRows=<0..rows>: the pack's save layout. False (nothing changed) on anything else.
inline bool SetLayout(Pack& p,const char* hex,int stableRows) noexcept {
    std::uint32_t v=0;
    if(!ParseLayout(hex,v) || stableRows<0 || stableRows>p.rows || p.From(v))return false;
    p.layout=v;p.stableRows=stableRows;
    return true;
}
// "<old row count>:<each old row's row now, -1 none>" (tools/make_edf5_campaign.py packs_ini) as the map from layout
// `marker`: 0 the unmarked saves' (ini Legacy), else an older marked layout's (ini Older, From<marker>). An empty text
// adds none; anything malformed (a count off, a row past the pack, a marker twice or the pack's own, one too many) is
// false and adds none.
inline bool AddFrom(Pack& p,std::uint32_t marker,const char* text) noexcept {
    if(!text || !*text)return true;
    if(p.fromCount>=kMaxFrom || p.From(marker) || (marker && marker==p.layout))return false;
    char* end=nullptr;
    const long n=std::strtol(text,&end,10);
    if(end==text || *end!=':' || n<1 || n>kMaxRows)return false;
    LayoutMap& m=p.from[p.fromCount];
    m=LayoutMap{};
    const char* at=end+1;
    for(long i=0;i<n;++i) {
        const long v=std::strtol(at,&end,10);
        if(end==at || v<-1 || v>=p.rows || (i+1<n ? *end!=',' : *end!=0)){m=LayoutMap{};return false;}
        m.row[i]=static_cast<std::int16_t>(v);
        at=end+1;
    }
    m.from=marker;m.rows=static_cast<int>(n);
    ++p.fromCount;
    return true;
}

// A mode's save as the game holds it while that mode is set (GS+0x57A4, 0x16C4 bytes: the save job reads DEFP_<MST>
// into it and writes it back from there, 0x7C0C72 / 0x7C3542 -> 0x7BC310 / 0x7BD0A0; docs/mission-list-re.md §0, §1):
// +0 the current row, +8 clear[10 classes][512 rows] (a byte per row, bit per difficulty), +0x14A0 a byte per row.
// Every one is indexed by the list's row. Rows 508..511 of the byte-per-row block hold this plugin's marker: the
// layout its rows are in (no pack of ours has that many rows; nothing reads a row past the list's).
constexpr std::size_t kModeSave=0x57A4,kModeSaveSize=0x16C4,kClear=8,kClearClasses=10,kRowBytes=0x14A0,kMarkerRow=508;
constexpr std::size_t kMarker=kRowBytes+kMarkerRow;
static_assert(kMarker+4<=kRowBytes+kMaxRows && kClear+kClearClasses*kMaxRows<=kRowBytes && kRowBytes+kMaxRows<=kModeSaveSize);
// kUndetermined: an unmarked save with progress whose older layout cannot be told (no record of this machine's older
// list, or one the save does not fit, and progress past the stable rows). kForeign: a marker of a layout this version
// has no map from (another version's), or a save its map does not fit. Both are left exactly as they are, unmarked
// or under their own marker, so a guess is never made permanent.
enum class Migration { kNone,kAlready,kMarked,kRemapped,kForeign,kUndetermined };
inline const char* MigrationName(Migration m) noexcept {
    switch(m) {
    case Migration::kNone: return "none";
    case Migration::kAlready: return "already";
    case Migration::kMarked: return "marked";
    case Migration::kRemapped: return "rows-moved";
    case Migration::kForeign: return "other-layout";
    case Migration::kUndetermined: return "layout-unknown";
    }
    return "?";
}
// The highest row the save holds anything for (a clear of any class, its row byte, the current row); -1: none. The
// row bytes from kMarkerRow on are the marker's, never a row of ours.
inline int HighestUsedRow(const unsigned char* save) noexcept {
    int high=-1;
    for(int r=0;r<static_cast<int>(kMarkerRow);++r) {
        bool used=save[kRowBytes+static_cast<std::size_t>(r)]!=0;
        for(std::size_t c=0;c<kClearClasses && !used;++c)used=save[kClear+c*kMaxRows+static_cast<std::size_t>(r)]!=0;
        if(used)high=r;
    }
    for(int r=static_cast<int>(kMarkerRow);r<kMaxRows;++r)
        for(std::size_t c=0;c<kClearClasses;++c)if(save[kClear+c*kMaxRows+static_cast<std::size_t>(r)])high=r;
    std::uint32_t row=0;
    std::memcpy(&row,save,4);
    if(row>0)high=row>=static_cast<std::uint32_t>(kMaxRows) ? kMaxRows : static_cast<int>(row)>high ? static_cast<int>(row) : high;
    return high;
}
// The save's rows moved by `m` (its old row r to m.row[r]), the current row with them (to 0 when its mission is gone).
inline void MoveRows(unsigned char* save,const LayoutMap& m) noexcept {
    std::uint8_t clear[kClearClasses][kMaxRows]{},bytes[kMarkerRow]{};
    for(std::size_t c=0;c<kClearClasses;++c)
        for(int r=0;r<m.rows;++r)
            if(m.row[r]>=0)clear[c][m.row[r]]=save[kClear+c*kMaxRows+static_cast<std::size_t>(r)];
    for(int r=0;r<m.rows && r<static_cast<int>(kMarkerRow);++r)
        if(m.row[r]>=0 && m.row[r]<static_cast<int>(kMarkerRow))bytes[m.row[r]]=save[kRowBytes+static_cast<std::size_t>(r)];
    std::memcpy(save+kClear,clear,sizeof(clear));
    std::memcpy(save+kRowBytes,bytes,sizeof(bytes));
    std::uint32_t row=0;
    std::memcpy(&row,save,4);
    row=row<static_cast<std::uint32_t>(m.rows) && m.row[row]>=0 ? static_cast<std::uint32_t>(m.row[row]) : 0;
    std::memcpy(save,&row,4);
}
// Once per save, decided by the save itself (its marker and what it holds), never by the install's history alone:
//  - marked with these rows' layout: left alone (a load without a save in between moves nothing twice);
//  - marked with an older layout the installer gave a map from: moved to the rows now and marked (every older layout
//    has its own map to now, so a save skips any number of versions); a save past that map's rows: kForeign;
//  - unmarked (from before 2026-10-18, or new): holding nothing, or nothing past the stable rows (where every older
//    layout had its rows), marked as it is; else moved by this machine's record of its older list (Legacy, from 0)
//    when the save fits it (nothing past its rows), else kUndetermined.
// kNone: no layout, or a pack the marker does not fit (more rows than kMarkerRow).
inline Migration MigrateModeSave(unsigned char* save,const Pack& p) noexcept {
    if(!save || !p.layout || p.rows<1 || p.rows>static_cast<int>(kMarkerRow))return Migration::kNone;
    std::uint32_t marker=0;
    std::memcpy(&marker,save+kMarker,4);
    if(marker==p.layout)return Migration::kAlready;
    const int high=HighestUsedRow(save);
    const LayoutMap* const m=p.From(marker);
    Migration done=Migration::kMarked;
    if(!marker && high<p.stableRows) {
        // nothing in it, or only rows that every older layout had where they are now
    } else if(m && high<m->rows) {
        MoveRows(save,*m);
        done=Migration::kRemapped;
    } else {
        return marker ? Migration::kForeign : Migration::kUndetermined;
    }
    std::memcpy(save+kMarker,&p.layout,4);
    return done;
}

// The room's line: "1;<id>;<tag>;<digest>;<rows>;<missing mask>" (version 1). At most 192 bytes (EOS attribute
// strings may hold 1000).
constexpr std::size_t kLineSize=192;
inline bool Format(const Pack& p,char (&out)[kLineSize]) noexcept {
    char mask[kMaxRows/4+1]{};
    FormatMask(p,mask,sizeof(mask));
    const int n=std::snprintf(out,sizeof(out),"1;%d;%s;%s;%d;%s",p.id,p.tag,p.digest,p.rows,mask);
    return n>0 && static_cast<std::size_t>(n)<sizeof(out);
}
inline bool Parse(const char* line,Pack& out) noexcept {
    if(!line || std::strlen(line)>=kLineSize)return false;
    char copy[kLineSize]{};
    std::memcpy(copy,line,std::strlen(line));
    char* parts[6]{};
    int n=0;
    for(char* p=copy;n<6;) {
        parts[n++]=p;
        char* semi=std::strchr(p,';');
        if(!semi)break;
        *semi=0;p=semi+1;
    }
    if(n!=6 || std::strcmp(parts[0],"1"))return false;
    char* end=nullptr;
    const long id=std::strtol(parts[1],&end,10);
    if(end==parts[1] || *end)return false;
    const long rows=std::strtol(parts[4],&end,10);
    if(end==parts[4] || *end || rows<1 || rows>kMaxRows)return false;
    return Make(static_cast<int>(id),parts[2],parts[3],static_cast<int>(rows),parts[5],out);
}

// What a guest makes of a room (Admit). kOk also for every room whose content is not one of ours (stock 0..2,
// another mod's): those are none of this check's business.
enum class Verdict { kOk,kNotInstalled,kNoIdentity,kMalformed,kOtherPack,kOtherVersion,kRowsMissing };
inline const char* Reason(Verdict v) noexcept {
    switch(v) {
    case Verdict::kOk: return "ok";
    case Verdict::kNotInstalled: return "pack-not-installed";
    case Verdict::kNoIdentity: return "pack-identity-missing";
    case Verdict::kMalformed: return "pack-identity-malformed";
    case Verdict::kOtherPack: return "pack-identity-other-pack";
    case Verdict::kOtherVersion: return "pack-definition-differs";
    case Verdict::kRowsMissing: return "pack-rows-missing-here";
    }
    return "?";
}
struct Admission {
    Verdict verdict=Verdict::kOk;
    Pack host{};                 // the room's line, parsed (when it was)
    int missingHere=0;           // rows the host can choose that this machine lacks
    int firstMissingHere=-1;
};
// `dlc` the room's content id, `line` its kAttribute (nullptr when absent), `local` this machine's pack of that id
// (nullptr when not installed). The guest needs the same definition and every row the host can choose (a host
// switches the room's mission after the guest is in: a row the guest lacks would load a mission it does not have).
inline Admission Admit(std::int64_t dlc,const char* line,const Pack* local) noexcept {
    Admission a{};
    if(!Reserved(dlc))return a;
    if(!local){a.verdict=Verdict::kNotInstalled;return a;}
    if(!line){a.verdict=Verdict::kNoIdentity;return a;}
    if(!Parse(line,a.host)){a.verdict=Verdict::kMalformed;return a;}
    if(a.host.id!=dlc || std::strcmp(a.host.tag,local->tag)){a.verdict=Verdict::kOtherPack;return a;}
    if(std::strcmp(a.host.digest,local->digest) || a.host.rows!=local->rows){a.verdict=Verdict::kOtherVersion;return a;}
    for(int r=0;r<local->rows;++r)
        if(local->Missing(r) && !a.host.Missing(r)){if(a.firstMissingHere<0)a.firstMissingHere=r;++a.missingHere;}
    if(a.missingHere)a.verdict=Verdict::kRowsMissing;
    return a;
}

// The dialog a refused guest sees: which pack, and what differs. False for kOk.
inline bool Explain(std::int64_t dlc,const Admission& a,const Pack* local,wchar_t* out,std::size_t size) noexcept {
    const wchar_t* name=Name(dlc);
    switch(a.verdict) {
    case Verdict::kOk: return false;
    case Verdict::kNotInstalled:
        std::swprintf(out,size,L"房间的任务包是「%ls」（内容编号 %lld），本机没有安装它。\n\n请运行全军出击安装器"
            L"（EDF4.1 的任务包需要本机有 EDF4.1 或 EDF4.1 素材包）。",name,static_cast<long long>(dlc));
        return true;
    case Verdict::kNoIdentity:
        std::swprintf(out,size,L"房间的任务包是「%ls」，但房主的全军出击没有发布任务包身份（旧版本），"
            L"无法确认两边的关卡列表一致。\n\n请双方更新到同一版本的全军出击。",name);
        return true;
    case Verdict::kMalformed: case Verdict::kOtherPack:
        std::swprintf(out,size,L"房间的任务包信息与它的内容编号对不上（「%ls」），无法核对。\n\n请双方更新到同一版本的全军出击。",name);
        return true;
    case Verdict::kOtherVersion:
        std::swprintf(out,size,L"房间的「%ls」任务包与本机的版本不同（房主 %hs / %d 关，本机 %hs / %d 关）："
            L"同一个关号在两边会是不同的关。\n\n请双方使用同一版本的全军出击，并重新运行安装器。",
            name,a.host.digest,a.host.rows,local ? local->digest : "-",local ? local->rows : 0);
        return true;
    case Verdict::kRowsMissing:
        std::swprintf(out,size,L"本机的「%ls」任务包有 %d 关缺资源（第 %d 关等），房主可以选这些关。\n\n"
            L"请补齐资源后重新运行安装器（EDF4.1 的任务包需要 EDF4.1 或 EDF4.1 素材包）。",
            name,a.missingHere,a.firstMissingHere+1);
        return true;
    }
    return false;
}

constexpr std::int32_t kLockedShown=3;   // a mission select row's state: shown, not selectable (unlock_ported.h ListRow)
// The mission select's rows of `pack` (src/edf5campaign.cpp, docs/mission-list-re.md §7): every placeholder row
// "locked but shown" (3): the select refuses a row above 2 (0x8A8629: MissionSelectNotOpen), and the scan stops only
// at a row still 4 (0x8A2330), so the missions after it stay reachable. How many rows were locked.
inline int LockPlaceholders(ported::ListRow* rows,std::size_t n,const Pack& pack) noexcept {
    int locked=0;
    for(std::size_t i=0;i<n && i<static_cast<std::size_t>(pack.rows);++i)
        if(pack.Missing(static_cast<int>(i))){rows[i].state=kLockedShown;++locked;}
    return locked;
}
}  // namespace crew::packs
namespace crew {
// This machine's installed pack of a room's content id (src/edf5campaign.cpp), nullptr when none.
const packs::Pack* InstalledMissionPack(std::int64_t id) noexcept;
}
