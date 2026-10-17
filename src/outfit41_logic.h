// The EDF4.1 outfit's pure logic (src/outfit41.cpp is the game glue; docs/edf41-outfits.md): which files a class's
// EDF4.1 outfit is, the per-class choice as the plugin stores it, and the layout of the HQ outfit popup it adds an item
// to. No game memory, no Windows: tests/outfit41_test.cpp runs it offline.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>

namespace crew::outfit41 {

// CONFIG.SGO SoldierInit's class numbers: 0 Ranger, 1 Wing Diver, 2 Air Raider, 3 Fencer.
inline constexpr int kClasses=4;
constexpr bool ValidClass(int c) noexcept { return c>=0 && c<kClasses; }

// The files tools/make_edf41_outfits.py ships (edf41port/outfits.json; tools/edf41_outfits.py installs them under Mods):
// the battle SGO the game builds the player from, its menu SGO (the HQ / room 3D view), and the model archive both name.
struct Files {
    const wchar_t* key;          // the state file's key
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

// --- The choice, per class, for this machine's first local player (the plugin's own file; the save keeps the stock
// outfit number -1..3 untouched, so the save, other players and an uninstall see the stock outfit).
struct State { bool on[kClasses]{}; };
inline constexpr const char* kSection="[Outfit41]";

inline std::string Format(const State& s) {
    std::string out=std::string(kSection)+"\r\n";
    for(int c=0;c<kClasses;++c) {
        const wchar_t* k=kFiles[c].key;
        while(*k)out+=static_cast<char>(*k++);
        out+=s.on[c] ? "=1\r\n" : "=0\r\n";
    }
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
            for(int c=0;c<kClasses;++c) {
                std::size_t n=0;
                const wchar_t* k=kFiles[c].key;
                bool same=true;
                for(;k[n];++n)if(n>=key.size() || key[n]!=static_cast<char>(k[n])){same=false;break;}
                if(same && n==key.size())s.on[c]=value=="1";
            }
        }
        at=end+1;
    }
    return s;
}

// --- Whose model it is
// The player creation's description (0x5A3F90's rcx): +0 is the local player's slot (0x595C80: the offline slot,
// 0 or 1 split screen; 0x595960 online: 0 this machine's, -1 another's, from the session member's remote bit).
inline constexpr std::size_t kDescPlayer=0x0,kDescClass=0x8,kDescOutfit=0xC;
// Only this machine's first player wears it: split screen's second player and every remote player keep the stock one.
constexpr bool OwnPlayer(std::int32_t descPlayer) noexcept { return descPlayer==0; }
// A session member (0x59DC90's shared_ptr: the object at +0): +0x10 bit 1 is remote (0x12AC420).
inline constexpr std::size_t kMemberFlags=0x10;
constexpr bool RemoteMember(std::uint32_t flags) noexcept { return ((flags>>1)&1u)!=0; }

// --- The HQ outfit popup (built in 0x8AB310, shown by 0x119C600(system, out, type, request)): its request object
// holds a vector of 0x90-byte items {wstring text, std::function<void(ctx*)> picked, ...}; five stock items (the
// mission's outfit -1, then 2, 3, 1, 0), the cursor index, and the dialog type.
inline constexpr std::size_t kReqVector=0x150,kVecData=0x8,kVecCapacity=0x10,kVecSize=0x18;
inline constexpr std::size_t kReqCursor=0x148,kReqType=0x128;
inline constexpr std::int32_t kOutfitDialog=6;
inline constexpr std::size_t kItemSize=0x90,kItemText=0x0,kItemImpl=0x58;
inline constexpr std::size_t kStockItems=5;
inline constexpr std::int32_t kItemEdf41=5;           // ours: after the five stock items
inline constexpr const wchar_t kItemLabel[]=L"EDF4.1";
// The vector's growth when full, as the game's own loop does it (0x8ABAF3): reserve(capacity * 2 + 2).
constexpr std::size_t Grown(std::size_t capacity) noexcept { return capacity*2+2; }
// The stock items' std::function implementation: {vtable, the screen}; the screen's player slot and outfit.
inline constexpr std::size_t kLambdaScreen=0x8;
inline constexpr std::size_t kScreenPlayer=0x798,kScreenOutfit=0x7A0;
// std::_Func_impl's vtable (MSVC): _Copy, _Move, _Do_call, _Target_type, _Delete_this, _Get.
inline constexpr int kFuncSlots=6,kSlotCopy=0,kSlotMove=1,kSlotCall=2;

// The cursor the popup opens on: ours when the EDF4.1 outfit is on, else the game's own choice.
constexpr std::int32_t Cursor(bool on,std::int32_t stock) noexcept { return on ? kItemEdf41 : stock; }

}  // namespace crew::outfit41
