// The HQ's mission pack switch (src/pack_switch.cpp, docs/pack-switch-re.md): its pure side, no game
// (tests/pack_switch_test.cpp).
//  - Step: which mode Q / E (LB / RB) go to: the next offline story mode (ModeList item 8 = 0 and item 10 = 0, the ones
//    the mission-pack dialog lists) the player may select, in the ModeList's order, round the end;
//  - the script routine that switches to it: `string AF_PackSwitch<mode>()`, compiled into MAINSCRIPT's module once,
//    which calls the script's own ApplyModeNo (the lobby's switch of mode inside a session) and goes back to HQMain.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cwchar>

namespace crew::packswitch {
constexpr int kMaxModes=64;   // more than any ModeList the installer writes (20 with every pack)

struct ModeView {
    bool offlineStory=false;   // item 8 (online) 0 and item 10 (type) 0: GAME_OFFLINE_SCENARIO
    bool selectable=false;     // the dialog's ownership answer for its content id (0xD92B0, the plugin's hook)
};

// The mode `dir` (-1: Q / LB, +1: E / RB) steps to from `current`, or -1 when there is no other one to go to.
inline int Step(const ModeView* modes,int count,int current,int dir) noexcept {
    if(!modes || count<=0 || count>kMaxModes || (dir!=1 && dir!=-1))return -1;
    const int from=current>=0 && current<count ? current : (dir>0 ? count-1 : 0);
    for(int k=1;k<=count;++k) {   // k == count: `from` itself (only when there is no current mode)
        const int i=((from+dir*k)%count+count)%count;
        if(i!=current && modes[i].offlineStory && modes[i].selectable)return i;
    }
    return -1;
}

// The routine's declaration (createCoRoutine's argument, as MAINSCRIPT's main() builds it) and its code. False when
// the mode is out of range or a buffer too small.
inline bool Declaration(int mode,wchar_t* out,std::size_t size) noexcept {
    if(mode<0 || mode>=kMaxModes || !out)return false;
    const int n=std::swprintf(out,size,L"string AF_PackSwitch%d()",mode);
    return n>0 && static_cast<std::size_t>(n)<size;
}
inline bool NarrowDeclaration(int mode,char* out,std::size_t size) noexcept {
    if(mode<0 || mode>=kMaxModes || !out)return false;
    const int n=std::snprintf(out,size,"string AF_PackSwitch%d()",mode);
    return n>0 && static_cast<std::size_t>(n)<size;
}
// ApplyModeNo (MAINSCRIPT.AS 1895): SetMode, the mode's save loaded, UnlockDownloadContents, the main block kept
// (ModeChangeFixup); false after its own dialog (no such content / the load failed): back to the title as the stock
// HQ's 回到标题 would.
inline bool Code(int mode,char* out,std::size_t size) noexcept {
    if(mode<0 || mode>=kMaxModes || !out)return false;
    const int n=std::snprintf(out,size,
        "string AF_PackSwitch%d() { if( ApplyModeNo(%d) ) return \"HQMain\"; return \"StartToTitle\"; }",mode,mode);
    return n>0 && static_cast<std::size_t>(n)<size;
}

// Edge of the two keys: a press only on the frame one goes down while the other is up (both down: nothing).
struct Keys { bool prev=false,next=false; };
inline int Pressed(const Keys& was,const Keys& now) noexcept {
    if(now.prev && now.next)return 0;
    if(now.prev && !was.prev)return -1;
    if(now.next && !was.next)return 1;
    return 0;
}

// The mode the HQ was closed for: the UI thread asks it (Ask), the script's thread takes it at the next routine start
// (Take, createCoRoutine: whatever routine that is, so a switch never outlives the HQ it was asked from); -1: none.
struct Pending { std::atomic<int> mode{-1}; };
inline bool Ask(Pending& p,int mode) noexcept {
    int none=-1;
    return mode>=0 && p.mode.compare_exchange_strong(none,mode);
}
inline int Take(Pending& p) noexcept { return p.mode.exchange(-1); }

// One HQ frame (pack_switch.cpp HqUpdate): the keys' edge, `was` advanced every frame whatever follows (a key held down
// while something covered the HQ does not fire when it uncovers); the direction to switch when the HQ owned the input
// before and after its own update (`owned`) and no switch is pending, else 0.
inline int HqFrame(Keys& was,const Keys& now,bool owned,const Pending& p) noexcept {
    const int dir=Pressed(was,now);
    was=now;
    return dir && owned && p.mode.load()<0 ? dir : 0;
}
}  // namespace crew::packswitch
