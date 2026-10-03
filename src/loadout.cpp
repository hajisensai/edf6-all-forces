// Forced loadout for the test range (testrange/ writes EDF6TestRange.loadout.ini next to the plugin).
// The local player's class and weapons live in GameStatus; the game reads them in the per-player
// preload (0x59DE50) and in offline player creation (0x591410). Both calls are wrapped: the wanted
// loadout is written just before the call and the old values put back right after it, so nothing
// forced is left for the menus or the autosave to see. Layout: docs/loadout-re.md.
// Refill=1 also fills every weapon of the new player at once (the developer command weapon_reload's
// per-soldier call), which makes an Air Raider's vehicles callable without earning points first.
#include "crew.h"
#include "memory.h"
#include <cwchar>
#include <iterator>

namespace crew {
namespace {
using Fn=std::uintptr_t(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);

constexpr unsigned kGameStatus=0x20B2890,kPreloadPlayer=0x59DE50,kCreateLocal=0x591410;
constexpr unsigned kReloadAll=0x5A1060;   // (SoldierBase*): reload-complete on each weapon in +0x1950[+0x1960]
constexpr std::size_t kWeaponList=0x1950,kWeaponCount=0x1960;
constexpr unsigned kPreloadCalls[]={0x1B8F52,0x225FB5};
// Online (GameStatus+0x38 != -1) the scripts' PreloadPlayerResource (0x1B8CC0) preloads each session
// player through 0x59DC90 instead and never reaches 0x1B8F52; the jets are preloaded there too, or an
// airstrike takeover finds nothing preloaded online and the stock bombers come.
constexpr unsigned kPreloadSession=0x59DC90;
constexpr unsigned kSessionCalls[]={0x1B8E98,0x225FAC};
constexpr unsigned kCreateCalls[]={0xA153A,0xA1879,0x1DC539,0x22AE3D,0x5A51C5};
// GameStatus: per local player (stride 0x3E60) the class, then 6 weapon ids per class;
// per weapon id (stride 12) a flags word and 8 star bytes.
constexpr std::size_t kSlotStride=0x3E60,kClass=0x6E90,kWeapons=0x6E98,kClassStride=0x18;
constexpr std::size_t kStars=0xEB54,kRecordStride=12,kStarCount=8;
constexpr int kSlotsPerClass[4]={4,4,5,6};
constexpr int kMaxWeapon=0x800;   // GameStatus has room for this many weapon records

wchar_t loadoutPath[MAX_PATH]{};
Fn preloadOrig=nullptr,createOrig=nullptr,sessionOrig=nullptr;
void(__fastcall* reloadAll)(void*)=nullptr;

struct Loadout { int cls=-1; int weapon[6]{-1,-1,-1,-1,-1,-1}; int stars=-1; };

// What a call overwrote, to put back afterwards.
struct Undo {
    unsigned char* player=nullptr;
    unsigned char* gs=nullptr;
    std::int32_t cls=0,target=0;
    std::int32_t weapon[6]{};
    int starIds[6]{-1,-1,-1,-1,-1,-1};
    unsigned char stars[6][kStarCount]{};
};

bool ReadLoadout(Loadout& l) noexcept {
    if(!GetPrivateProfileIntW(L"Loadout",L"Enabled",0,loadoutPath))return false;
    l.cls=static_cast<int>(GetPrivateProfileIntW(L"Loadout",L"Class",static_cast<UINT>(-1),loadoutPath));
    if(l.cls<0 || l.cls>3)return false;
    for(int i=0;i<kSlotsPerClass[l.cls];++i) {
        wchar_t key[8];swprintf_s(key,L"Slot%d",i);
        const int id=static_cast<int>(GetPrivateProfileIntW(L"Loadout",key,static_cast<UINT>(-1),loadoutPath));
        l.weapon[i]=id>=0 && id<kMaxWeapon ? id : -1;   // -1: leave the slot as the save has it
    }
    l.stars=static_cast<int>(GetPrivateProfileIntW(L"Loadout",L"Stars",static_cast<UINT>(-1),loadoutPath));
    if(l.stars>10)l.stars=10;
    return true;
}

bool Apply(std::uintptr_t slotArg,Undo& u,const char* where) noexcept {
    if(static_cast<std::int32_t>(slotArg)!=0)return false;   // 1P only; split-screen 2P keeps its own
    Loadout l;
    if(!ReadLoadout(l))return false;
    auto gs=At<unsigned char*>(image,kGameStatus);
    if(!Readable(gs,kStars+kMaxWeapon*kRecordStride,true))return false;
    auto p=gs;   // slot 0
    u.player=p;u.gs=gs;u.target=l.cls;
    u.cls=At<std::int32_t>(p,kClass);
    for(int i=0;i<6;++i)u.weapon[i]=At<std::int32_t>(p,kWeapons+l.cls*kClassStride+i*4);
    Put<std::int32_t>(p,kClass,l.cls);
    for(int i=0;i<6;++i) {
        if(l.weapon[i]<0)continue;
        Put<std::int32_t>(p,kWeapons+l.cls*kClassStride+i*4,l.weapon[i]);
        if(l.stars<0)continue;
        auto stars=gs+kStars+l.weapon[i]*kRecordStride;
        u.starIds[i]=l.weapon[i];
        std::memcpy(u.stars[i],stars,kStarCount);
        std::memset(stars,l.stars,kStarCount);
    }
    if(cfg.debug)Log("LOADOUT %s class=%d weapons=%d,%d,%d,%d,%d,%d stars=%d",where,l.cls,
                     l.weapon[0],l.weapon[1],l.weapon[2],l.weapon[3],l.weapon[4],l.weapon[5],l.stars);
    return true;
}

void Restore(const Undo& u) noexcept {
    for(int i=5;i>=0;--i)   // reverse: a weapon listed twice gets its first saved stars back
        if(u.starIds[i]>=0)std::memcpy(u.gs+kStars+u.starIds[i]*kRecordStride,u.stars[i],kStarCount);
    for(int i=0;i<6;++i)Put<std::int32_t>(u.player,kWeapons+u.target*kClassStride+i*4,u.weapon[i]);
    Put<std::int32_t>(u.player,kClass,u.cls);
}

std::uintptr_t __fastcall PreloadHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    Undo u;
    const bool applied=Apply(a,u,"preload");
    const auto result=preloadOrig(a,b,c,d);
    if(applied)Restore(u);
    PreloadJets();   // the airstrike takeovers' jets (jet.cpp), with the mission's own resources
    PreloadSub();    // ...and the submarine carrier (subcarrier.cpp)
    return result;
}

std::uintptr_t __fastcall SessionPreloadHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    const auto result=sessionOrig(a,b,c,d);
    PreloadJets();
    PreloadSub();
    return result;
}

// (slot, transform, mode): mode 1/2 are the scripts' CreatePlayer_NoWeapon / _InitWeapon; leave those alone.
// The new soldier's weapon list must look like one before the game's own loop walks it.
void Refill(std::uintptr_t soldier) noexcept {
    if(!reloadAll || !GetPrivateProfileIntW(L"Loadout",L"Refill",0,loadoutPath))return;
    auto obj=reinterpret_cast<unsigned char*>(soldier);
    if(!Readable(obj,kWeaponCount+8))return;
    auto list=At<void* const*>(obj,kWeaponList);
    const auto count=At<std::uint64_t>(obj,kWeaponCount);
    if(count==0 || count>16 || !Readable(list,count*sizeof(void*)))return;
    for(std::uint64_t i=0;i<count;++i)if(!Readable(list[i],0xE80))return;
    reloadAll(obj);
    if(cfg.debug)Log("LOADOUT refill weapons=%llu",static_cast<unsigned long long>(count));
}

std::uintptr_t __fastcall CreateHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    Undo u;
    const bool applied=static_cast<std::int32_t>(c)==0 && Apply(a,u,"create");
    const auto result=createOrig(a,b,c,d);
    if(applied) {
        Restore(u);
        Refill(result);
    }
    return result;
}

int Redirect(const unsigned* sites,std::size_t count,unsigned target,void* hook) noexcept {
    int done=0;
    for(std::size_t i=0;i<count;++i) {
        bool changed=false;
        if(RedirectCall(image+sites[i],image+target,hook,changed))++done;
        else if(changed)Log("LOADOUT call site %#x half patched",sites[i]);
    }
    return done;
}
}  // namespace

bool InstallLoadout(const wchar_t* pluginIni) noexcept {
    wcscpy_s(loadoutPath,pluginIni);
    auto slash=wcsrchr(loadoutPath,L'\\');
    if(!slash)return false;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-loadoutPath),L"EDF6TestRange.loadout.ini");
    static const unsigned char session[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x81,0xEC,0x80,0x00,0x00,0x00};
    if(Matches(kPreloadSession,session,sizeof(session))) {
        sessionOrig=reinterpret_cast<Fn>(image+kPreloadSession);
        const int n=Redirect(kSessionCalls,std::size(kSessionCalls),kPreloadSession,reinterpret_cast<void*>(&SessionPreloadHook));
        Log("HOOK online jet preload=%d/%zu",n,std::size(kSessionCalls));
    } else Log("HOOK online jet preload: profile mismatch");
    static const unsigned char preload[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
    static const unsigned char create[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x60,0x41,0x8B,0xD8,0x48,0x8B,0xFA};
    if(!Matches(kPreloadPlayer,preload,sizeof(preload)) || !Matches(kCreateLocal,create,sizeof(create))) {
        Log("LOADOUT profile mismatch, forced loadout off");
        return false;
    }
    static const unsigned char reload[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x99,0x50,0x19,0x00,0x00};
    if(Matches(kReloadAll,reload,sizeof(reload)))reloadAll=reinterpret_cast<void(__fastcall*)(void*)>(image+kReloadAll);
    else Log("LOADOUT reload profile mismatch, Refill off");
    preloadOrig=reinterpret_cast<Fn>(image+kPreloadPlayer);
    createOrig=reinterpret_cast<Fn>(image+kCreateLocal);
    const int p=Redirect(kPreloadCalls,std::size(kPreloadCalls),kPreloadPlayer,reinterpret_cast<void*>(&PreloadHook));
    const int c=Redirect(kCreateCalls,std::size(kCreateCalls),kCreateLocal,reinterpret_cast<void*>(&CreateHook));
    Log("HOOK loadout preload=%d/%zu create=%d/%zu file=%ls",p,std::size(kPreloadCalls),c,std::size(kCreateCalls),loadoutPath);
    return p>0 && c>0;
}
}  // namespace crew
