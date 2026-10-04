// The jets' stores (stores.h, docs/stores-re.md). All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
//  - The 506 builds its weapons in slot 46 (0x61B770) for exactly four holders (`cmp ebx,4` at 0x61B85F; one
//    SetWeaponObject 0x633330 a holder, no bound check): a jet's fifth store and on had no weapon. The loop's end is
//    redirected to a cave that compares with the vehicle's holder count (veh +0x648, vehicle_weapon_setting's rows,
//    which pylib/vcobjects.py jet_sgo makes as many as mission_setup[3]'s entries). The stock 506s have four and four.
//    Without the plugin the game builds the first four: every jet SGO has at least four holders, so none reads an
//    entry that is not there.
//  - A weapon's SGO: weapon +0x08 is its resource node (shared by every weapon from that file), node +0x20 its key,
//    an MSVC std::wstring of the upper-cased path (inline under 8 characters' capacity, +0x38).
#include "stores.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>

namespace crew {
namespace {
#include "stores.inc"
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr std::size_t kWeaponNode=0x08,kNodeKey=0x20,kKeyLength=0x30,kKeyCapacity=0x38;
constexpr std::size_t kWeaponTrigger=0x139,kWeaponLockRange=0x6D0,kWeaponAmmo=0xBE8,kWeaponLocked=0xC68;
constexpr std::size_t kLoopEnd=0x61B85D,kLoopTop=0x61B7F0,kLoopOut=0x61B864;
const unsigned char kLoopCode[]={0xFF,0xC3,0x83,0xFB,0x04,0x7C,0x8C,0x0F,0xB7,0x45,0xD8};   // inc ebx; cmp ebx,4; jl
const unsigned char kBuildSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8B,0xEC};   // 0x61B770
const unsigned char kSetWeaponSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};          // 0x633330
bool storesOk=false;

// The file name of the weapon's SGO (after the last separator of its key), or nullptr.
const wchar_t* FileOf(const unsigned char* weapon,std::size_t* length) noexcept {
    const auto node=At<const unsigned char*>(weapon,kWeaponNode);
    if(!Readable(node,kKeyCapacity+8))return nullptr;
    const std::size_t n=At<std::size_t>(node,kKeyLength),cap=At<std::size_t>(node,kKeyCapacity);
    if(n==0 || n>260 || cap<n)return nullptr;
    const wchar_t* key=cap>=8 ? At<const wchar_t*>(node,kNodeKey) : reinterpret_cast<const wchar_t*>(node+kNodeKey);
    if(!Readable(key,n*sizeof(wchar_t)))return nullptr;
    std::size_t at=n;
    while(at>0 && key[at-1]!=L'\\' && key[at-1]!=L'/')--at;
    *length=n-at;
    return key+at;
}

// The store whose files `name` (`length` characters) is one of: its prefix, then digits, then ".SGO".
const StoreSpec* SpecOf(const wchar_t* name,std::size_t length) noexcept {
    for(const auto& s:kStores) {
        const std::size_t p=std::wcslen(s.prefix);
        if(length<=p+4 || _wcsnicmp(name,s.prefix,p)!=0 || _wcsnicmp(name+length-4,L".SGO",4)!=0)continue;
        bool digits=true;
        for(std::size_t i=p;i<length-4;++i)digits=digits && std::iswdigit(name[i]);
        if(digits)return &s;
    }
    return nullptr;
}

void* BuildLoopCave() noexcept {
    unsigned char cave[]={
        0xFF,0xC3,                                   // inc ebx
        0x3B,0x9E,0x48,0x06,0x00,0x00,               // cmp ebx,[rsi+0x648]   (rsi: the vehicle, 0x61B794)
        0x7D,0x0E,                                   // jge out
        0xFF,0x25,0,0,0,0, 0,0,0,0,0,0,0,0,          // jmp [rip] -> the loop's top
        0xFF,0x25,0,0,0,0, 0,0,0,0,0,0,0,0};         // out: jmp [rip] -> after the loop
    const auto top=reinterpret_cast<std::uintptr_t>(image+kLoopTop),out=reinterpret_cast<std::uintptr_t>(image+kLoopOut);
    std::memcpy(cave+16,&top,8);std::memcpy(cave+30,&out,8);
    return edf::AllocateNearCode(image+kLoopEnd,cave,sizeof(cave));
}
}  // namespace

int ReadStores(unsigned char* v,Store* out,int most) noexcept {
    if(SeatCount(v)==0)return 0;   // without the loop patch the first four holders still have their weapons
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>16 || !Readable(holders,count*8))return 0;
    int n=0;
    for(std::uint64_t i=0;i<count && n<most;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponLocked+8))continue;
        std::size_t length=0;
        const wchar_t* name=FileOf(w,&length);
        const StoreSpec* spec=name ? SpecOf(name,length) : nullptr;
        if(!spec)continue;
        const std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        const auto locked=At<std::uint64_t>(w,kWeaponLocked);
        const float range=At<float>(w,kWeaponLockRange);
        out[n++]=Store{w,spec,ammo>0 ? ammo : 0,locked<64 ? static_cast<std::int32_t>(locked) : 0,
                       std::isfinite(range) && range>0.0f ? range : 0.0f};
    }
    return n;
}

void TriggerStore(const Store& s) noexcept {
    if(s.weapon && s.ammo>0 && Readable(s.weapon+kWeaponTrigger,1,true))s.weapon[kWeaponTrigger]=1;
}

Burden BurdenOf(float mark,const Store* stores,int count) noexcept {
    float clean=0.0f;
    for(const auto& m:kJetMasses)if(m.mark==mark)clean=m.mass;
    Burden b{1.0f,0.0f};
    if(clean<=0.0f)return b;
    float kg=0.0f;
    for(int i=0;i<count;++i){kg+=stores[i].spec->mass*static_cast<float>(stores[i].ammo);b.drag+=stores[i].spec->drag*static_cast<float>(stores[i].ammo);}
    b.mass=(clean+kg)/clean;
    return b;
}

bool InstallStores() noexcept {
    __try {
        if(!Matches(0x61B770,kBuildSig,sizeof(kBuildSig)) || !Matches(0x633330,kSetWeaponSig,sizeof(kSetWeaponSig)) ||
           !Matches(kLoopEnd,kLoopCode,sizeof(kLoopCode))) {
            Log("HOOK stores=0 (unexpected EDF.dll code: a jet's fifth store and on have no weapon)");
            return false;
        }
        void* cave=BuildLoopCave();
        if(!cave){Log("HOOK stores=0 (no memory near the 506's weapon build)");return false;}
        const auto rel=static_cast<std::int32_t>(static_cast<unsigned char*>(cave)-(image+kLoopEnd+5));
        unsigned char jump[7]={0xE9,0,0,0,0,0x90,0x90};
        std::memcpy(jump+1,&rel,4);
        storesOk=edf::PatchCode(image+kLoopEnd,kLoopCode,jump,sizeof(jump));
        if(!storesOk)VirtualFree(cave,0,MEM_RELEASE);
        Log("HOOK stores=%d (the 506 builds a weapon for every holder; %zu store kinds)",storesOk,sizeof(kStores)/sizeof(kStores[0]));
        return storesOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
