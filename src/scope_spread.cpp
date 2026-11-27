// The scoped weapons' spread for the player's own rounds: scope_spread.h (the rule: scope_spread_logic.h; the reading:
// docs/scope-accuracy-re.md; why: docs/feedback-2026-11-21-scope-accuracy.md).
#include "scope_spread.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "heli.h"
#include "host_settings.h"
#include "memory.h"
#include <cstdint>
#include <cstring>

namespace crew {
namespace {
// EDF.dll 0x678CCB46 RVAs (tests/scope_spread_native_audit.py pins every one of them):
//  - the player creation 0x5A3F90's loadout loop: 0x5A4330 mov esi,[r13+rbx*4] (the weapon table row), 0x5A434A GetRow,
//    and at 0x5A4479 lea r8,[rbp-8] (stars); lea rdx,[rbp] (the path); mov rcx,r14 (the human); 0x5A4484 call 0x58F360
//    (the weapon made and appended to human+0x1950, its count +0x1960). esi is not written between: the row at the call.
//    NPCs make theirs through 0x5944A9 (support_soldier.cpp), never here.
//  - the weapon table: GameStatus (EDF+0x20B2890) +0x130, its rows at +0x188 (null: none yet); RowCount 0xE23F0,
//    GetRow 0xE1CF0(table, out row, id), the row's category at +0x10 (docs/loadout-re.md §8.1).
//  - every round's cone (0x691560, from the shot entry 0x696FD0 only): 0x691AFA movss xmm3,[rdi+0xE14];
//    0x691B02 mulss xmm3,[rdi+0x378] (FireAccuracy), the polar angle's upper bound for 0x4E820.
constexpr unsigned kMakeCtx=0x5A4479,kMakeSite=0x5A4484,kMakeWeapon=0x58F360,kConeRead=0x691AFA;
constexpr unsigned kRowCount=0xE23F0,kGetRow=0xE1CF0;
constexpr std::size_t kGameStatus=0x20B2890,kWeaponTable=0x130,kTableRows=0x188,kRowCategory=0x10;
const unsigned char kMakeCode[]={0x4C,0x8D,0x45,0xF8,0x48,0x8D,0x55,0x00,0x49,0x8B,0xCE,0xE8,0xD7,0xAE,0xFE,0xFF};
const unsigned char kConeCode[]={0xF3,0x0F,0x10,0x9F,0x14,0x0E,0x00,0x00,0xF3,0x0F,0x59,0x9F,0x78,0x03,0x00,0x00};
// The weapon: +0x120 its holder (the soldier), +0x378 FireAccuracy, +0x690 SecondaryFire_Type (1: a scope), +0x780 the
// scope open (toggled by 0x696310, the soldier's frame 0x59ABB0 reads it), +0xE14 the cone's other factor.
constexpr std::size_t kWeaponOwner=0x120,kWeaponAccuracy=0x378,kWeaponScope=0x690,kWeaponZoomed=0x780,kWeaponCone=0xE14;
constexpr std::size_t kHumanWeapons=0x1950,kHumanWeaponCount=0x1960;
constexpr std::uint64_t kWeaponsMost=16;

using MakeFn=void*(__fastcall*)(void*,const void*,const void*);
using RowCountFn=std::uint32_t(__fastcall*)(void*);
using GetRowFn=void*(__fastcall*)(void*,void*,std::uint32_t);

bool installed=false;

// The player weapons made (each its creation's human, row and category). Not cleared at a mission's start: the player
// creation may run after MissionStart's preload or before it, so a round checks the entry against the weapon's holder
// and the holder's list instead; a weapon made again at the same address overwrites its entry.
struct Made {
    const unsigned char* weapon=nullptr;
    const unsigned char* human=nullptr;
    std::int32_t row=-1,category=-1;
    std::int8_t zoomLogged=-1;   // the zoom state last logged for it (-1: none yet)
};
constexpr int kMadeMost=64;
Made made[kMadeMost];
int madeNext=0;
SRWLOCK lock=SRWLOCK_INIT;

// The row's category, -1 when the table is not there or the row past its end.
std::int32_t CategoryOf(std::uint32_t row) noexcept {
    const auto gs=At<unsigned char*>(image+kGameStatus,0);
    if(!gs || !Readable(gs+kWeaponTable+kTableRows,8) || !At<void*>(gs+kWeaponTable,kTableRows))return -1;
    void* const table=gs+kWeaponTable;
    if(row>=reinterpret_cast<RowCountFn>(image+kRowCount)(table))return -1;
    alignas(8) unsigned char buffer[0x100]{};
    reinterpret_cast<GetRowFn>(image+kGetRow)(table,buffer,row);
    return At<std::int32_t>(buffer,kRowCategory);
}

void Note(unsigned char* human,std::uint64_t before,std::uint32_t row) noexcept {
    if(before>=kWeaponsMost || !Readable(human+kHumanWeapons,0x18))return;
    const std::uint64_t now=At<std::uint64_t>(human,kHumanWeaponCount);
    const auto list=At<unsigned char* const*>(human,kHumanWeapons);
    if(now!=before+1 || !Readable(list,now*8))return;   // no weapon appended (a path the game refused): nothing to note
    unsigned char* const weapon=list[before];
    if(!Readable(weapon,kWeaponCone+4))return;
    const std::int32_t category=CategoryOf(row);
    if(category<0){Log("SCOPESPREAD row %d out of table (weapon %p): stock spread for it",static_cast<int>(row),weapon);return;}
    AcquireSRWLockExclusive(&lock);
    Made* slot=nullptr;
    for(auto& m:made)if(m.weapon==weapon){slot=&m;break;}
    if(!slot){slot=&made[madeNext];madeNext=(madeNext+1)%kMadeMost;}
    *slot=Made{weapon,human,static_cast<std::int32_t>(row),category,-1};
    ReleaseSRWLockExclusive(&lock);
    Log("SCOPESPREAD weapon %p row %d cat %d sniper %d scope %d accuracy %.4f",weapon,static_cast<int>(row),category,
        scopespread::SniperCategory(category),At<std::int32_t>(weapon,kWeaponScope)==1,At<float>(weapon,kWeaponAccuracy));
}

// 0x5A4484's call, through the near stub (mov r9d,esi: the row): the stock creation, then the weapon it appended noted.
void* __fastcall MakeHook(unsigned char* human,const void* path,const void* stars,std::uint32_t row) {
    std::uint64_t before=~0ull;
    __try { if(Readable(human+kHumanWeaponCount,8))before=At<std::uint64_t>(human,kHumanWeaponCount); }
    __except(EXCEPTION_EXECUTE_HANDLER){before=~0ull;}
    void* const result=reinterpret_cast<MakeFn>(image+kMakeWeapon)(human,path,stars);
    __try { Note(human,before,row); } __except(EXCEPTION_EXECUTE_HANDLER){}
    return result;
}

bool Holds(const unsigned char* human,const unsigned char* weapon) noexcept {
    if(!Readable(human+kHumanWeapons,0x18))return false;
    const std::uint64_t n=At<std::uint64_t>(human,kHumanWeaponCount);
    const auto list=At<const unsigned char* const*>(human,kHumanWeapons);
    if(n==0 || n>kWeaponsMost || !Readable(list,n*8))return false;
    for(std::uint64_t i=0;i<n;++i)if(list[i]==weapon)return true;
    return false;
}

void Shot(unsigned char* weapon,ScopeShot* out) noexcept {
    if(!Readable(weapon,kWeaponCone+4,true) || At<std::int32_t>(weapon,kWeaponScope)!=1)return;
    const auto owner=At<const unsigned char*>(weapon,kWeaponOwner);
    if(!IsPlayer(owner) || !Holds(owner,weapon))return;   // this machine's player (not an NPC, not another machine's)
    const scopespread::Params params=ScopeSpreadInEffect();
    if(!params.on)return;
    const bool zoomed=weapon[kWeaponZoomed]!=0;
    std::int32_t category=-1;
    bool logZoom=false;
    AcquireSRWLockExclusive(&lock);
    for(auto& m:made) {
        if(m.weapon!=weapon || m.human!=owner)continue;
        category=m.category;
        logZoom=m.zoomLogged!=(zoomed ? 1 : 0);
        m.zoomLogged=zoomed ? 1 : 0;
        break;
    }
    ReleaseSRWLockExclusive(&lock);
    if(category<0)return;   // not made by the player creation (or its row was not in the table): stock
    const float e14=At<float>(weapon,kWeaponCone),accuracy=At<float>(weapon,kWeaponAccuracy);
    const float stock=e14*accuracy;
    const bool sniper=scopespread::SniperCategory(category);
    const float want=scopespread::Cone(sniper,zoomed,stock,params);
    if(logZoom)Log("SCOPESPREAD %p zoom %d cone %.4f -> %.4f (cat %d)",weapon,zoomed,stock,want,category);
    const scopespread::Write write=scopespread::Plan(e14,accuracy,want);
    if(!write.any)return;
    out->cone=e14;out->accuracy=accuracy;out->weapon=weapon;
    std::memcpy(weapon+kWeaponCone,&write.cone,4);
    std::memcpy(weapon+kWeaponAccuracy,&write.accuracy,4);
}
}  // namespace

void ScopeSpreadShot(unsigned char* weapon,bool replay,ScopeShot* out) noexcept {
    out->weapon=nullptr;
    // A replayed round (another machine's, drawn here: 0x696FD0's fifth argument 1) keeps the cone the stock draws.
    if(!installed || replay || !weapon)return;
    __try { Shot(weapon,out); } __except(EXCEPTION_EXECUTE_HANDLER){out->weapon=nullptr;}
}

void ScopeSpreadRestore(const ScopeShot& shot) noexcept {
    if(!shot.weapon)return;
    __try {
        std::memcpy(shot.weapon+kWeaponCone,&shot.cone,4);
        std::memcpy(shot.weapon+kWeaponAccuracy,&shot.accuracy,4);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

bool InstallScopeSpread() noexcept {
    bool make=false,cone=false;
    __try {
        make=Matches(kMakeCtx,kMakeCode,sizeof(kMakeCode));
        cone=Matches(kConeRead,kConeCode,sizeof(kConeCode));
    } __except(EXCEPTION_EXECUTE_HANDLER){make=cone=false;}
    if(!make){Log("SCOPESPREAD REFUSED call site 0x%X bytes differ: scoped weapons keep the stock spread",kMakeSite);return false;}
    if(!cone){Log("SCOPESPREAD REFUSED the cone read 0x%X bytes differ: scoped weapons keep the stock spread",kConeRead);return false;}
    if(!InstallShotEntry()){Log("SCOPESPREAD REFUSED the shot entry hook (0x696FD0) not installed: scoped weapons keep the stock spread");return false;}
    // mov r9d,esi (the row, kept in esi since 0x5A4330); mov rax,MakeHook; jmp rax: the call's return address and shadow
    // space stay the stock creation's.
    unsigned char stub[]={0x41,0x89,0xF1,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto target=reinterpret_cast<std::uintptr_t>(&MakeHook);
    std::memcpy(stub+5,&target,8);
    void* const page=AllocateNearCode(image+kMakeSite,stub,sizeof(stub));
    if(!page){Log("SCOPESPREAD REFUSED no code page near 0x%X: scoped weapons keep the stock spread",kMakeSite);return false;}
    bool changed=false;
    if(!RedirectCall(image+kMakeSite,image+kMakeWeapon,page,changed)) {
        Log("SCOPESPREAD REFUSED call site 0x%X not redirected%s: scoped weapons keep the stock spread",kMakeSite,changed ? " (half patched)" : "");
        if(!changed)VirtualFree(page,0,MEM_RELEASE);
        return false;
    }
    installed=true;
    Log("SCOPESPREAD installed (player weapon creation 0x%X, the shot entry's cone 0x%X)",kMakeSite,kConeRead);
    return true;
}
}  // namespace crew
