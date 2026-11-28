// The tables the game indexes by the soldier type, made to hold types 4 and 5 (src/soldier_type_patches.h). The sites are
// src/soldier_sites.inc (tests/soldier_native_audit.py checks them against EDF.dll).
#include "soldier_type_patches.h"
#include "soldier_type_logic.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "memory.h"
#include "edf/patch.h"

#include <atomic>
#include <cstring>

namespace crew {
namespace {
namespace st=soldier_type;

bool Print(std::size_t rva,const char* tag) noexcept {
    const st::Site* site=st::FindPrint(rva);
    unsigned char bytes[st::kMostSiteBytes];
    const std::size_t n=site ? st::Decode(site->hex,bytes,sizeof(bytes)) : 0;
    if(n && Readable(image+rva,n) && Matches(rva,bytes,n))return true;
    Log("ZWTYPE the code at EDF.dll+0x%zX (%s) is not what this was written for: %s off",rva,site ? site->what : "unlisted",tag);
    return false;
}

// ---- the nameplate table ----
constexpr std::size_t kPlateCall=0x804875,kPlateFill=0x806FE0,kAssign=0x3D450;
using PlateFillFn=void(__fastcall*)(void*);
using AssignFn=void*(__fastcall*)(void*,const wchar_t*,std::size_t);
std::atomic<int> platesFilled{0};

// After the stock fill (entries 0..3; the patched reserve and resize made 4 and 5 default ones): the new classes' letters and
// colours, the alpha the stock entries use (entry 0's).
void __fastcall PlateFillHook(void* table) noexcept {
    reinterpret_cast<PlateFillFn>(image+kPlateFill)(table);
    char* t=static_cast<char*>(table);
    if(!t || !Readable(t+8,0x18)) return;
    char* data=*reinterpret_cast<char**>(t+8);
    const std::size_t size=*reinterpret_cast<const std::size_t*>(t+0x18);
    if(size<static_cast<std::size_t>(st::kTypes) || !Readable(data,st::kPlateStride*st::kTypes,true)) {
        Log("ZWTYPE nameplate table %p holds %zu entries, not %d: entries 4 / 5 not filled",table,size,st::kTypes);
        return;
    }
    float alpha;
    std::memcpy(&alpha,data+st::kPlateAlpha,4);
    for(const st::Plate& p:st::kPlates) {
        char* e=data+st::kPlateStride*static_cast<std::size_t>(p.type);
        const wchar_t letter[2]={p.letter,0};
        reinterpret_cast<AssignFn>(image+kAssign)(e,letter,1);
        const float rgba[4]={p.r,p.g,p.b,alpha};
        std::memcpy(e+st::kPlateColour,rgba,sizeof(rgba));
    }
    if(platesFilled.fetch_add(1)<2)Log("ZWTYPE nameplate table %p: entries 4 (V) and 5 (S) filled",table);
}

bool PatchAll(bool forward) noexcept {
    for(const st::Patch& p:st::kPlatePatches) {
        unsigned char from[st::kMostSiteBytes],to[st::kMostSiteBytes];
        const std::size_t n=st::Decode(p.from,from,sizeof(from));
        if(!n || st::Decode(p.to,to,sizeof(to))!=n)return false;
        if(!edf::PatchCode(image+p.rva,forward ? from : to,forward ? to : from,n))return false;
    }
    return true;
}

bool InstallPlates() noexcept {
    if(!Print(kPlateCall,"the nameplates for types 4 / 5") || !Print(kPlateFill,"the nameplates for types 4 / 5")
       || !Print(0x8073A4,"the nameplates for types 4 / 5") || !Print(kAssign,"the nameplates for types 4 / 5"))return false;
    // All of the reserve / resize immediates or none: a capacity of 6 over a 4-entry allocation would be written past.
    for(const st::Patch& p:st::kPlatePatches) {
        unsigned char from[st::kMostSiteBytes];
        const std::size_t n=st::Decode(p.from,from,sizeof(from));
        if(!n || !Readable(image+p.rva,n) || !Matches(p.rva,from,n)) {
            Log("ZWTYPE the code at EDF.dll+0x%zX (%s) is not what this was written for: the nameplates for types 4 / 5 off",p.rva,p.what);
            return false;
        }
    }
    if(!PatchAll(true)) {
        const bool back=PatchAll(false);
        Log("ZWTYPE the nameplate table's reserve could not be written (%s): the nameplates for types 4 / 5 off",
            back ? "put back" : "NOT put back whole");
        return false;
    }
    bool changed=false;
    if(!RedirectCall(image+kPlateCall,image+kPlateFill,reinterpret_cast<void*>(&PlateFillHook),changed)) {
        Log("ZWTYPE the nameplate fill call was not redirected: the table holds 6 entries, 4 / 5 left blank");
        return false;
    }
    return true;
}

// ---- the damage number colour ----
alignas(16) constexpr st::DamageRows kDamageRows=st::MakeDamageRows();

bool InstallDamageColour() noexcept {
    constexpr const char* tag="the damage colour for types 4 / 5";
    if(!Print(0x81F837,tag) || !Print(st::kDamageSite,tag) || !Print(st::kDamageBack,tag))return false;
    unsigned char stock[st::kMostSiteBytes];
    if(st::Decode(st::FindPrint(st::kDamageSite)->hex,stock,sizeof(stock))!=st::kDamageSiteSize)return false;
    unsigned char code[st::kDamageStubSize];
    const std::size_t n=st::BuildDamageStub(code,sizeof(code),stock,reinterpret_cast<std::uint64_t>(&kDamageRows),
                                            reinterpret_cast<std::uint64_t>(image+st::kDamageBack));
    if(n!=st::kDamageStubSize)return false;
    const unsigned char* page=edf::RedirectToCode(image+st::kDamageSite,stock,st::kDamageSiteSize,code,n,edf::Branch::kJump);
    if(!page)Log("ZWTYPE the damage colour site was not redirected: %s off",tag);
    return page!=nullptr;
}

// ---- the script boundary ----
using TypeFn=int(__fastcall*)(void*);
// In place of `call [rax+0x298]` (rcx the soldier, rax its vptr): the type through the same slot, as the scripts see it.
int __fastcall ScriptSoldierType(void* soldier) noexcept {
    const int type=(*static_cast<TypeFn* const*>(soldier))[st::kSlotType](soldier);
    return st::ScriptType(type);
}

bool InstallScriptType() noexcept {
    constexpr const char* tag="the script-side type of types 4 / 5";
    if(!Print(0x1B5092,tag) || !Print(0x222810,tag))return false;
    int done=0;
    for(const std::size_t site:st::kScriptSites) {
        void* thunk=AllocateNearThunk(image+site,reinterpret_cast<void*>(&ScriptSoldierType));
        if(thunk && edf::RedirectToThunk(image+site,st::kTypeCall,st::kScriptSiteSize,thunk,edf::Branch::kCall))++done;
        else Log("ZWTYPE the script type call at EDF.dll+0x%zX was not redirected",site);
    }
    return done==2;
}
}  // namespace

bool InstallSoldierTypePatches() noexcept {
    const bool plates=InstallPlates();
    const bool damage=InstallDamageColour();
    const bool script=InstallScriptType();
    Log("ZWTYPE nameplates=%d damage colour=%d script type=%d",plates,damage,script);
    return plates && damage && script;
}
}  // namespace crew
