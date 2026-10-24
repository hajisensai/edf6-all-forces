// The EDF4.1 outfit's pure logic (src/outfit41.cpp is the game glue; docs/edf41-outfits.md): which files a class's
// EDF4.1 outfit is, the per-player choice as the plugin stores it, who wears it (the choice, the EDF4.1 packs' mission
// default), what a member tells the room about it, and the layout of the HQ outfit popup it adds an item to. No game
// memory, no Windows: tests/outfit41_test.cpp runs it offline.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>
#include "game_dialog.h"
#include "ini_text.h"
#include "unlock_ported.h"

namespace crew::outfit41 {

// CONFIG.SGO SoldierInit's class numbers: 0 Ranger, 1 Wing Diver, 2 Air Raider, 3 Fencer.
inline constexpr int kClasses=4;
constexpr bool ValidClass(int c) noexcept { return c>=0 && c<kClasses; }

// The files tools/make_edf41_outfits.py ships (edf41port/outfits.json; tools/edf41_outfits.py installs them under Mods):
// the battle SGO the game builds the player from, its menu SGO (the HQ / room 3D view), and the model archive both name.
struct Files {
    const wchar_t* key;          // the class's name in logs
    const wchar_t* battle;       // what 0xE34E0 hands back for the player (CONFIG.SGO SoldierInit[class][2][outfit]'s form)
    const wchar_t* menu;         // what 0xE3420 hands back for the 3D view (SoldierInit[class][5][1][outfit]'s form)
    const wchar_t* onDisk[3];    // Mods-relative: all three must be there before the outfit is offered or used
};
inline constexpr Files kFiles[kClasses]={
    {L"Ranger",L"app:/object/EDF41_P_RANGER.SGO",L"app:/MENUOBJECT/EDF41_P_RANGER.SGO",
     {L"OBJECT\\EDF41_P_RANGER.SGO",L"OBJECT\\EDF41_P_RANGER.MRAB",L"MENUOBJECT\\EDF41_P_RANGER.SGO"}},
    {L"WingDiver",L"app:/object/EDF41_P_WINGDIVER.SGO",L"app:/MENUOBJECT/EDF41_P_WINGDIVER.SGO",
     {L"OBJECT\\EDF41_P_WINGDIVER.SGO",L"OBJECT\\EDF41_P_WINGDIVER.MRAB",L"MENUOBJECT\\EDF41_P_WINGDIVER.SGO"}},
    {L"AirRaider",L"app:/object/EDF41_P_AIRRAIDER.SGO",L"app:/MENUOBJECT/EDF41_P_AIRRAIDER.SGO",
     {L"OBJECT\\EDF41_P_AIRRAIDER.SGO",L"OBJECT\\EDF41_P_AIRRAIDER.MRAB",L"MENUOBJECT\\EDF41_P_AIRRAIDER.SGO"}},
    {L"Fencer",L"app:/object/EDF41_P_FENCER.SGO",L"app:/MENUOBJECT/EDF41_P_FENCER.SGO",
     {L"OBJECT\\EDF41_P_FENCER.SGO",L"OBJECT\\EDF41_P_FENCER.MRAB",L"MENUOBJECT\\EDF41_P_FENCER.SGO"}},
};

// --- The choice, per local player (the save's outfit is one number a player for every class, GS +0x6E94 +
// player*0x3E60: so is ours). The plugin's own file; the save keeps the stock outfit number -1..3 untouched (0xE34E0 /
// 0xE3420 do not check it: a 4 would read past SoldierInit once the plugin is gone).
inline constexpr int kLocalPlayers=2;   // 0 the first player, 1 split screen's second (0xAC650 / 0xAC9E0: slot < 2)
constexpr bool ValidLocal(int p) noexcept { return p>=0 && p<kLocalPlayers; }
// --- The EDF4.1 outfit's own colours (the user, 2026-10-19: 「4.1等各个种类的皮肤颜色可单独保留自定义，不受其他皮肤颜色影响」)
// The game keeps a player's colours per class and per stock outfit: GS +0x6FB0 + player*0x3E60 + (outfit + class*4) *
// 0x188, a block of the primary's 12 HQ chips (float4) and its picked index (+0xC0), then the secondary's (+0xC4, index
// +0x184) (0xDF1B0 fills it; read by the battle 0x5914C0, the HQ view 0x93B320, the room's member record 0x78E9A0, the
// colour screens 0x881060 / 0x87D790 ...; docs/room-3d-preview-re.md §1). Every stock outfit already has its own block;
// the EDF4.1 outfit has none (the save's outfit stays the stock -1..3) and so wore, and edited, the block of the stock
// outfit under it. Now it has its own block per local player and class, kept by the plugin: while a player wears it,
// the block of the stock outfit under it (the class's, at the outfit 0xD8700 resolves) holds the EDF4.1 colours and the
// stock ones wait in the plugin's backup; every reader of the game's (the battle, the views, the colour screens, the
// room's member record: what the others see) reads the right colours with nothing of theirs hooked. Taken off, the
// block goes back. The save file's format is untouched; a block the player edits while swapped is the EDF4.1 one.
inline constexpr std::size_t kColourBlock=0x188,kGsColours=0x6FB0;
inline constexpr int kStockOutfits=4;
constexpr std::size_t ColourOffset(int player,int cls,int outfit) noexcept {
    return kGsColours+static_cast<std::size_t>(player)*0x3E60+static_cast<std::size_t>(outfit+cls*kStockOutfits)*kColourBlock;
}
struct Swap {
    int cls=-1,outfit=-1;                  // the block holding the EDF4.1 colours now; -1 none
    unsigned char backup[kColourBlock]{};  // that stock outfit's own colours
    std::uint64_t others=0;                // the player's other blocks' hash when swapped (a save loaded since: changed)
};
struct State {
    bool on[kLocalPlayers]{};
    bool has[kLocalPlayers][kClasses]{};                      // the EDF4.1 colours known (first worn: the stock ones then)
    unsigned char own[kLocalPlayers][kClasses][kColourBlock]{};
    Swap swap[kLocalPlayers];
};

// FNV-1a over bytes.
inline std::uint64_t Hash(const unsigned char* p,std::size_t n,std::uint64_t h=0xCBF29CE484222325ull) noexcept {
    for(std::size_t i=0;i<n;++i){h^=p[i];h*=0x100000001B3ull;}
    return h;
}
// A player's colour blocks but one (the swapped one's neighbours: the game rewrites them all when it loads a save).
inline std::uint64_t OthersHash(const unsigned char* playerColours,int cls,int outfit) noexcept {
    std::uint64_t h=0xCBF29CE484222325ull;
    for(int b=0;b<kClasses*kStockOutfits;++b)
        if(b!=outfit+cls*kStockOutfits)h=Hash(playerColours+static_cast<std::size_t>(b)*kColourBlock,kColourBlock,h);
    return h;
}
// The block swapped for player `p` (`colours` its 16 blocks) still holds the EDF4.1 colours as the plugin left them
// or as the player edited them since; false when there is no swap, or the block is the game's own again (a save
// loaded: the other blocks changed too, or the block is exactly the stock backup). Reconcile's and the save's test.
inline bool SwappedHere(const State& s,int p,const unsigned char* colours) noexcept {
    if(!ValidLocal(p) || !colours)return false;
    const Swap& w=s.swap[p];
    if(w.cls<0)return false;
    const unsigned char* g=colours+static_cast<std::size_t>(w.outfit+w.cls*kStockOutfits)*kColourBlock;
    if(std::memcmp(g,s.own[p][w.cls],kColourBlock)==0)return true;
    return OthersHash(colours,w.cls,w.outfit)==w.others && std::memcmp(g,w.backup,kColourBlock)!=0;
}
// The save (outfit41.cpp SaveJobHook): the game's copy of MAIN.GST (`mainCopy`: GS +kMainBlock, kMainSize bytes) with
// every block still swapped given its stock colours back (the swap's backup), so the file never holds the EDF4.1
// colours; GS itself is not touched. Returns how many blocks were put back.
inline constexpr std::size_t kMainBlock=0x6E68,kMainSize=0xDCE8;
inline int RestoreSavedCopy(const State& s,unsigned char* mainCopy) noexcept {
    int put=0;
    for(int p=0;p<kLocalPlayers && mainCopy;++p) {
        const Swap& w=s.swap[p];
        unsigned char* colours=mainCopy+(ColourOffset(p,0,0)-kMainBlock);
        if(!SwappedHere(s,p,colours))continue;
        std::memcpy(colours+static_cast<std::size_t>(w.outfit+w.cls*kStockOutfits)*kColourBlock,w.backup,kColourBlock);
        ++put;
    }
    return put;
}
static_assert(ColourOffset(kLocalPlayers-1,kClasses-1,kStockOutfits-1)+kColourBlock<=kMainBlock+kMainSize,
              "every colour block is inside the save's main block");

// What Reconcile did (the caller persists and logs).
enum class ColourAct : std::uint8_t { none, swapped, restored, mirrored, dropped };
// Makes the game's colour blocks of player `p` (`colours`: GS +0x6FB0 + p*0x3E60, all 16 blocks) agree with what it wears:
// `wantCls` / `wantOutfit` the block the EDF4.1 colours belong in now (-1: it does not wear the EDF4.1 outfit). Returns
// the last thing it did, `changed` whether the state needs saving.
//   - swapped and still wanted there: an edit (the colour screen writes the block) is the EDF4.1 colours, mirrored;
//     unless the other blocks changed too (a save was loaded): the swap is the old save's, dropped without writing GS
//     (the block is the loaded save's own), or, when the block is exactly the backup, the save never had the swap.
//   - swapped and not wanted there: the EDF4.1 colours kept, the backup written back.
//   - wanted and not swapped: the stock block saved as the backup, the EDF4.1 colours written (the first time: the
//     stock ones, so nothing changes until the player edits them).
inline ColourAct Reconcile(State& s,int p,unsigned char* colours,int wantCls,int wantOutfit,bool& changed) noexcept {
    changed=false;
    if(!ValidLocal(p) || !colours)return ColourAct::none;
    if(!ValidClass(wantCls) || wantOutfit<0 || wantOutfit>=kStockOutfits)wantCls=wantOutfit=-1;
    Swap& w=s.swap[p];
    ColourAct act=ColourAct::none;
    if(w.cls>=0) {
        unsigned char* g=colours+static_cast<std::size_t>(w.outfit+w.cls*kStockOutfits)*kColourBlock;
        const bool atOwn=std::memcmp(g,s.own[p][w.cls],kColourBlock)==0;
        if(!SwappedHere(s,p,colours)) {   // not our block any more: left as the game has it
            w=Swap{};changed=true;act=ColourAct::dropped;
        } else if(w.cls!=wantCls || w.outfit!=wantOutfit) {
            std::memcpy(s.own[p][w.cls],g,kColourBlock);s.has[p][w.cls]=true;
            std::memcpy(g,w.backup,kColourBlock);
            w=Swap{};changed=true;act=ColourAct::restored;
        } else if(!atOwn) {
            std::memcpy(s.own[p][w.cls],g,kColourBlock);s.has[p][w.cls]=true;
            changed=true;act=ColourAct::mirrored;
        }
    }
    if(wantCls>=0 && w.cls<0) {
        unsigned char* g=colours+static_cast<std::size_t>(wantOutfit+wantCls*kStockOutfits)*kColourBlock;
        std::memcpy(w.backup,g,kColourBlock);
        if(!s.has[p][wantCls]){std::memcpy(s.own[p][wantCls],g,kColourBlock);s.has[p][wantCls]=true;}
        std::memcpy(g,s.own[p][wantCls],kColourBlock);
        w.cls=wantCls;w.outfit=wantOutfit;w.others=OthersHash(colours,wantCls,wantOutfit);
        changed=true;act=ColourAct::swapped;
    }
    return act;
}

// --- The file (EDF6VehicleCrew.outfit41.ini; ini_text.h reads and writes it key by key, as the room menu's ini)
inline constexpr char kHexDigits[]="0123456789ABCDEF";
inline std::string Hex(const unsigned char* p,std::size_t n) {
    std::string out;out.reserve(n*2);
    for(std::size_t i=0;i<n;++i){out+=kHexDigits[p[i]>>4];out+=kHexDigits[p[i]&15];}
    return out;
}
inline bool Unhex(std::string_view v,unsigned char* out,std::size_t n) noexcept {
    if(v.size()!=n*2)return false;
    const auto nib=[](char c) noexcept -> int { return c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : c>='a' && c<='f' ? c-'a'+10 : -1; };
    for(std::size_t i=0;i<n;++i) {
        const int a=nib(v[2*i]),b=nib(v[2*i+1]);
        if(a<0 || b<0)return false;
        out[i]=static_cast<unsigned char>(a*16+b);
    }
    return true;
}
inline constexpr const char* kSectionName="Outfit41";
inline constexpr const char* kSection="[Outfit41]";
// The keys, per local player (initext::Key): Player<P>=1 the choice; per local player and class Colours<P>_<class>=<the
// block in hex> (known ones); a swap Swap<P>=<class>,<outfit>,<others hash hex> with Backup<P>=<the block in hex>.
inline std::string PlayerKey(int p) { return initext::Key("Player",p); }
inline std::string ColoursKey(int p,int c) { return initext::Key("Colours",p,c); }
inline std::string SwapKey(int p) { return initext::Key("Swap",p); }
inline std::string BackupKey(int p) { return initext::Key("Backup",p); }

// Every key of `s` written into `was` (the file as it is: every other line, comment and key kept), a key the state no
// longer has (colours unknown, no swap) removed. False only for a UTF-16 file.
inline bool Store(const std::string& was,const State& s,std::string* out) {
    std::string text=was,next;
    const auto set=[&](const std::string& key,const std::string& value) {
        if(!initext::Rewrite(text,kSectionName,key,value,&next))return false;
        text.swap(next);
        return true;
    };
    for(int p=0;p<kLocalPlayers;++p)if(!set(PlayerKey(p),s.on[p] ? "1" : "0"))return false;
    for(int p=0;p<kLocalPlayers;++p) {
        for(int c=0;c<kClasses;++c)if(!set(ColoursKey(p,c),s.has[p][c] ? Hex(s.own[p][c],kColourBlock) : std::string{}))return false;
        const Swap& w=s.swap[p];
        std::string swap,backup;
        if(w.cls>=0) {
            unsigned char h[8];
            for(int i=0;i<8;++i)h[i]=static_cast<unsigned char>(w.others>>(56-8*i));
            swap=std::to_string(w.cls)+","+std::to_string(w.outfit)+","+Hex(h,8);
            backup=Hex(w.backup,kColourBlock);
        }
        if(!set(SwapKey(p),swap) || !set(BackupKey(p),backup))return false;
    }
    *out=std::move(text);
    return true;
}
// A new file of `s` alone.
inline std::string Format(const State& s) {
    std::string out;
    Store(std::string{},s,&out);
    return out;
}

namespace detail {
inline bool Digit(std::string_view v,int most,int& out) noexcept {
    if(v.size()!=1 || v[0]<'0' || v[0]-'0'>most)return false;
    out=v[0]-'0';
    return true;
}
}  // namespace detail
// The keys in any section (the file only ever had [Outfit41]); a missing or malformed one: off / unknown / no swap. A
// swap is kept only with its backup: without it the game's block is left as it is.
inline State Parse(std::string_view text) {
    State s;
    std::string v;
    for(int p=0;p<kLocalPlayers;++p) {
        s.on[p]=initext::Get(text,{},PlayerKey(p),&v) && v=="1";
        for(int c=0;c<kClasses;++c)
            if(initext::Get(text,{},ColoursKey(p,c),&v) && Unhex(v,s.own[p][c],kColourBlock))s.has[p][c]=true;
        Swap w;int c=0,o=0;unsigned char h[8];
        if(initext::Get(text,{},SwapKey(p),&v) && v.size()==4+16 && v[1]==',' && v[3]==',' &&
           detail::Digit(std::string_view(v).substr(0,1),kClasses-1,c) && detail::Digit(std::string_view(v).substr(2,1),kStockOutfits-1,o) &&
           Unhex(std::string_view(v).substr(4),h,8) && initext::Get(text,{},BackupKey(p),&v) && Unhex(v,w.backup,kColourBlock)) {
            w.cls=c;w.outfit=o;w.others=0;
            for(int i=0;i<8;++i)w.others=(w.others<<8)|h[i];
            s.swap[p]=w;
        }
    }
    return s;
}

// --- Who wears it (the user, 2026-10-18: a fifth outfit "绝望阴影再袭"; in the EDF4.1 mission packs it is the default
// when the outfit is "配合任务", or when the outfit change is not unlocked yet).
// The stock outfit change is unlocked by GS +0x300 (a byte of the SYSTEM block, every mode's): the soldier screen copies it
// to +0x7A4 (0x8A9B15) and opens the outfit popup only when it is set (0x8AB59C); the game sets it when the last row of
// the current mission list is cleared by any class (0x70D933 after 0xDF990). A new save's outfit is -1 (0xDEDD8).
constexpr bool MissionDefault(std::int32_t outfit,bool unlocked) noexcept { return outfit<0 || !unlocked; }
// `chosen`: the player picked the EDF4.1 item; `missionDefault`: MissionDefault of the player's save; `pack41`: the
// current mode is one of the EDF4.1 packs (story, Mission Pack 1, 2: unlock_ported.h StoryPack's 4.1 half). The outfit's
// files must be installed too (the caller asks).
constexpr bool Wears(bool chosen,bool missionDefault,bool pack41) noexcept { return chosen || (pack41 && missionDefault); }
constexpr bool Pack41(int content,int edf41) noexcept { return ported::Edf41Pack(content,edf41); }

// --- What a member tells the room (EDF6Coop member attribute, src/coop_member_attribute_api.h). Every member of a build
// with this feature publishes it, whatever it wears: "1;" then its chosen flag and its MissionDefault flag, '0' or '1'.
// The pack is the room's (every member plays the same mission), so a reader decides with its own Pack41. A member with no
// value (an older All Forces, Coop without the member attributes) or another version's value wears the stock outfit on
// this machine; the room's version notice names the feature (support_protocol.h kExtOutfit41).
inline constexpr char kAttrKey[]="EDF6X_OUTFIT41";
struct Wire { bool chosen=false,missionDefault=false; };
inline constexpr std::size_t kWireSize=5;   // "1;cd" and the NUL
inline void FormatWire(const Wire& w,char (&out)[kWireSize]) noexcept {
    out[0]='1';out[1]=';';out[2]=w.chosen ? '1' : '0';out[3]=w.missionDefault ? '1' : '0';out[4]=0;
}
inline bool ParseWire(std::string_view v,Wire& out) noexcept {
    if(v.size()!=4 || v[0]!='1' || v[1]!=';')return false;
    const auto bit=[](char c,bool& b) noexcept { if(c!='0' && c!='1')return false; b=c=='1'; return true; };
    Wire w;
    if(!bit(v[2],w.chosen) || !bit(v[3],w.missionDefault))return false;
    out=w;
    return true;
}

// --- Whose model it is
// The player creation's description (0x5A3F90's rcx): +0 is the local player's slot (0x595C80: the offline slot,
// 0 or 1 split screen; 0x595960 online: 0 this machine's, -1 another's, from the session member's remote bit).
inline constexpr std::size_t kDescPlayer=0x0,kDescClass=0x8,kDescOutfit=0xC;
// A session member (0x59DC90's / 0x595960's shared_ptr: the object at +0): +0x10 bit 1 is remote (0x12AC420); +0x48 is
// its mission index, the record GS +0x14C78 + index*0xD4 it is built from (0x59DCD9, 0x5959DD; EDF6Coop's
// EDF6Coop_ResolveMissionPlayerPuid maps the same number to the member's EOS id).
inline constexpr std::size_t kMemberFlags=0x10,kMemberIndex=0x48;
constexpr bool RemoteMember(std::uint32_t flags) noexcept { return ((flags>>1)&1u)!=0; }
inline constexpr int kMissionPlayers=1024;   // the game takes 0..3; EDF6Coop's [Mission] Extend widens it to 1024 (multislot patches.h)
constexpr bool ValidIndex(int i) noexcept { return i>=0 && i<kMissionPlayers; }

// --- What the preloads took (outfit41.cpp): per local slot (offline) and per mission index (online), -1 or the class
// whose EDF4.1 outfit was preloaded; a creation takes the outfit only for that class (its resource must be preloaded).
// This mission's only: a creation of a slot or index this mission's preloads never reached (a member joining mid-mission,
// an index EDF6Coop's Extend adds) gets the stock outfit, never the last mission's decision for that number. The record
// is cleared at the first preload after the last mission ended (MissionOver: a player was created, or an out-of-mission
// screen ran), not at MissionStart (mission.cpp), which runs after each player's preload and would drop the ones before.
// The preloads and the creations may run on different threads: each entry is an atomic.
struct PreloadRecord {
    std::atomic<int> local[kLocalPlayers];
    std::atomic<int> online[kMissionPlayers];
    std::atomic<bool> over{true};   // the next preload starts a new mission's record
    PreloadRecord() noexcept { Clear(); }
    void Clear() noexcept {
        for(auto& v:local)v.store(-1,std::memory_order_relaxed);
        for(auto& v:online)v.store(-1,std::memory_order_relaxed);
    }
    void MissionOver() noexcept { over.store(true,std::memory_order_release); }
    void BeginPreload() noexcept { if(over.exchange(false,std::memory_order_acq_rel))Clear(); }
    std::atomic<int>* Local(int p) noexcept { return ValidLocal(p) ? &local[p] : nullptr; }
    std::atomic<int>* Online(int i) noexcept { return ValidIndex(i) ? &online[i] : nullptr; }
    bool LocalTook(int p,int cls) const noexcept { return ValidLocal(p) && ValidClass(cls) && local[p].load()==cls; }
    bool OnlineTook(int i,int cls) const noexcept { return ValidIndex(i) && ValidClass(cls) && online[i].load()==cls; }
};

// The plugin's live switch (Enabled=0, crew.h): no item in the popup, the choice left as stored, every model the stock
// one, and the room told the stock flags, as every other module passes through while it is off.
constexpr Wire Effective(bool enabled,const Wire& w) noexcept { return enabled ? w : Wire{}; }

// --- The HQ outfit popup (built in 0x8AB310, shown by 0x119C600(system, out, type, request)): its request object
// holds a vector of 0x90-byte items {wstring text, std::function<void(ctx*)> picked, ...}; five stock items (the
// mission's outfit -1, then 2, 3, 1, 0), the cursor index, and the dialog type.
inline constexpr std::size_t kReqVector=0x150;
// The items vector and an item's layout, its growth and the std::function vtable: game_dialog.h (shared with the room
// menu's dialogs).
using gamedialog::kVecData;using gamedialog::kVecCapacity;using gamedialog::kVecSize;
inline constexpr std::size_t kReqCursor=0x148,kReqType=0x128;
inline constexpr std::int32_t kOutfitDialog=6;
using gamedialog::kItemSize;using gamedialog::kItemText;using gamedialog::kItemImpl;
inline constexpr std::size_t kStockItems=5;
inline constexpr std::int32_t kItemEdf41=5;           // ours: after the five stock items
// The vector's growth when full, as the game's own loop does it (0x8ABAF3): reserve(capacity * 2 + 2).
using gamedialog::Grown;
// The stock items' std::function implementation: {vtable, the screen}; the screen's player slot and outfit.
inline constexpr std::size_t kLambdaScreen=0x8;
inline constexpr std::size_t kScreenPlayer=0x798,kScreenOutfit=0x7A0;
// std::_Func_impl's vtable (MSVC): _Copy, _Move, _Do_call, _Target_type, _Delete_this, _Get.
using gamedialog::kFuncSlots;using gamedialog::kSlotCopy;using gamedialog::kSlotMove;using gamedialog::kSlotCall;

// The cursor the popup opens on: ours (at `ours`) when the EDF4.1 outfit is chosen, else the game's own choice.
constexpr std::int32_t Cursor(bool on,std::int32_t stock,std::int32_t ours=kItemEdf41) noexcept { return on ? ours : stock; }

// --- The outfit change still locked (GS +0x300 == 0: the last row of a mission list not cleared yet, docs/edf41-outfits.md
// §1.1). The stock soldier screen then never opens the popup (0x8AB59C reads its copy +0x7A4) and its help names no
// outfit key (0x8A9B31). The EDF4.1 outfit is ours, not the game's progression: the user (2026-10-19) asked why it could
// not be chosen before clearing EDF4.1, and it was never meant to wait for any clear. So while locked the screen is given
// the popup (+0x7A4 set, the help the unlocked screen shows: 0x8A9B37..0x8A9CA6) and the popup offers only what the
// lock allows plus ours: the mission's outfit (item 0, -1, what a locked save always wears) and the EDF4.1 outfit; the
// four stock outfits stay behind the game's own unlock.
inline constexpr std::size_t kScreenUnlocked=0x7A4,kScreenHelp=0x738,kScreenHelpDirty=0x6F2;
inline constexpr std::size_t kLockedKeep=1;           // the stock items kept while locked: item 0, the mission's outfit
// The popup's EDF4.1 item offered to a locked screen: the plugin on, the change locked, and the class's files there.
constexpr bool OfferLocked(bool enabled,bool unlocked,bool installed) noexcept { return enabled && !unlocked && installed; }
// The help the unlocked screen picks (0x8A9B37): the keyboard one for the first player on the keyboard, else the pad's.
constexpr bool KeyboardHelp(std::int32_t player,bool padMode) noexcept { return player==0 && !padMode; }
// The popup's cursor while locked: ours when chosen, else the mission's outfit (the only stock item left).
constexpr std::int32_t LockedCursor(bool on) noexcept { return on ? static_cast<std::int32_t>(kLockedKeep) : 0; }

// --- The HQ / room 3D view (UiHQ_3DModelView, vtable 0x180FA60): OnMessage (slot 3, 0x93B320) asks 0xE3420 for the
// menu model path (0x93B426 msg 3, 0x93BD03 msg 6) of the view's own player, view +0x2AC.
inline constexpr std::size_t kViewPlayer=0x2AC;
inline constexpr int kViewSlotMessage=3;

}  // namespace crew::outfit41
