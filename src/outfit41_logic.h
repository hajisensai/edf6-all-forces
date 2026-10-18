// The EDF4.1 outfit's pure logic (src/outfit41.cpp is the game glue; docs/edf41-outfits.md): which files a class's
// EDF4.1 outfit is, the per-player choice as the plugin stores it, who wears it (the choice, the EDF4.1 packs' mission
// default), what a member tells the room about it, and the layout of the HQ outfit popup it adds an item to. No game
// memory, no Windows: tests/outfit41_test.cpp runs it offline.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>
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
struct State { bool on[kLocalPlayers]{}; };
inline constexpr const char* kSection="[Outfit41]";
inline constexpr const char* kPlayerKeys[kLocalPlayers]={"Player1","Player2"};

inline std::string Format(const State& s) {
    std::string out=std::string(kSection)+"\r\n";
    for(int p=0;p<kLocalPlayers;++p)out+=std::string(kPlayerKeys[p])+(s.on[p] ? "=1\r\n" : "=0\r\n");
    return out;
}

// Lines "Key=1" under any section; an unknown key, a value other than 1, a missing file: off.
inline State Parse(std::string_view text) noexcept {
    State s;
    std::size_t at=text.substr(0,3)=="\xEF\xBB\xBF" ? 3 : 0;
    while(at<text.size()) {
        std::size_t end=text.find('\n',at);
        if(end==std::string_view::npos)end=text.size();
        std::string_view line=text.substr(at,end-at);
        while(!line.empty() && (line.back()=='\r' || line.back()==' '))line.remove_suffix(1);
        const std::size_t eq=line.find('=');
        if(eq!=std::string_view::npos) {
            const std::string_view key=line.substr(0,eq),value=line.substr(eq+1);
            for(int p=0;p<kLocalPlayers;++p)if(key==kPlayerKeys[p])s.on[p]=value=="1";
        }
        at=end+1;
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
inline constexpr std::size_t kReqVector=0x150,kVecData=0x8,kVecCapacity=0x10,kVecSize=0x18;
inline constexpr std::size_t kReqCursor=0x148,kReqType=0x128;
inline constexpr std::int32_t kOutfitDialog=6;
inline constexpr std::size_t kItemSize=0x90,kItemText=0x0,kItemImpl=0x58;
inline constexpr std::size_t kStockItems=5;
inline constexpr std::int32_t kItemEdf41=5;           // ours: after the five stock items
// The vector's growth when full, as the game's own loop does it (0x8ABAF3): reserve(capacity * 2 + 2).
constexpr std::size_t Grown(std::size_t capacity) noexcept { return capacity*2+2; }
// The stock items' std::function implementation: {vtable, the screen}; the screen's player slot and outfit.
inline constexpr std::size_t kLambdaScreen=0x8;
inline constexpr std::size_t kScreenPlayer=0x798,kScreenOutfit=0x7A0;
// std::_Func_impl's vtable (MSVC): _Copy, _Move, _Do_call, _Target_type, _Delete_this, _Get.
inline constexpr int kFuncSlots=6,kSlotCopy=0,kSlotMove=1,kSlotCall=2;

// The cursor the popup opens on: ours when the EDF4.1 outfit is chosen, else the game's own choice.
constexpr std::int32_t Cursor(bool on,std::int32_t stock) noexcept { return on ? kItemEdf41 : stock; }

// --- The HQ / room 3D view (UiHQ_3DModelView, vtable 0x180FA60): OnMessage (slot 3, 0x93B320) asks 0xE3420 for the
// menu model path (0x93B426 msg 3, 0x93BD03 msg 6) of the view's own player, view +0x2AC.
inline constexpr std::size_t kViewPlayer=0x2AC;
inline constexpr int kViewSlotMessage=3;

}  // namespace crew::outfit41
