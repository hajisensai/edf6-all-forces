// Six player classes (docs/plan-2026-11-28-new-classes-impl.md, docs/newclass-re/inject.md / count.md): the game reads
// its classes from CONFIG.SGO's SoldierInit when it loads the mode data (0xE2AD0, from SetMode 0xDC460, the first time
// inside the game status's constructor 0xD62C0), and the class count everywhere after from that node
// (0xE34D0 = [[cfg+0x20]+4]).
//
// - The mode data load's two reads (0xE2C17 SoldierInit, 0xE2C2A SoldierWeaponCategory, both 0x528A0(doc, key)) are
//   redirected: the first reads both stock nodes, copies them (sgo_build_logic.h) with the classes 4 / 5 and their five
//   weapon categories appended (soldier_classes_logic.h), and returns the SoldierInit copy; the second returns the
//   category copy made with it (one or the other alone would leave the category map with nameless rows). The copies are
//   resident and never freed: the category map keeps pointers into their strings. Copies of the same content are one.
// - Code with room for the stock four only reads min(count, 4): the armor statistics (0xDCD63, 0xDDB25), the key config
//   menu (0x893066, whose switch has no case past 3), and the play counts' loop (0x70FC2D: a class past 3 goes to the
//   next row instead of the switch on it).
//
// Everything is checked before anything is written; the writes go in an order where each prefix is harmless with four
// classes (the clamps, the play count stub, the category read, the SoldierInit read last).
#include "soldier_classes.h"
#include "soldier_classes_logic.h"
#include "sgo_build_logic.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "memory.h"
#include "edf/patch.h"
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

namespace crew {
namespace {
constexpr unsigned kGameStatus=0x20B2890;
constexpr unsigned kSgoNode=0x528A0;        // (doc*, const wchar_t* key) -> node* or null
constexpr unsigned kClassCount=0xE34D0;     // (mode config*) -> [[cfg+0x20]+4]

// 0xE2C0D: lea rdx,L"SoldierInit"; mov rcx,r12; call 0x528A0; mov [rsi+0x20],rax;
//          lea rdx,L"SoldierWeaponCategory"; mov rcx,r12; call 0x528A0; mov rdi,rax
constexpr unsigned kModeLoad=0xE2C0D;
constexpr unsigned char kModeLoadSig[]={0x48,0x8D,0x15,0x8C,0x56,0x68,0x01,0x49,0x8B,0xCC,0xE8,0x84,0xFC,0xF6,0xFF,0x48,0x89,0x46,0x20,
                                        0x48,0x8D,0x15,0x91,0x56,0x68,0x01,0x49,0x8B,0xCC,0xE8,0x71,0xFC,0xF6,0xFF,0x48,0x8B,0xF8};
constexpr unsigned kInitCall=0xE2C17;
constexpr unsigned kCategoryCall=0xE2C2A;

struct CountSite {
    unsigned rva;
    unsigned char code[8];   // call 0xE34D0, then what reads its result
    std::size_t size;
};
constexpr CountSite kCountSites[]={
    {0xDCD63,{0xE8,0x68,0x67,0x00,0x00,0x85,0xC0},7},          // armor statistics: test eax,eax
    {0xDDB25,{0xE8,0xA6,0x59,0x00,0x00,0x44,0x3B,0xE8},8},     // armor statistics: cmp r13d,eax (the loop's end)
    {0x893066,{0xE8,0x65,0x04,0x85,0xFF,0x44,0x8B,0xE0},8},    // key config menu: mov r12d,eax
};

constexpr unsigned kPlayCount=0x70FC2D;     // mov edx,[rcx+rdx+0x6E90]; test edx,edx
constexpr unsigned char kPlayCountSig[]={0x8B,0x94,0x11,0x90,0x6E,0x00,0x00,0x85,0xD2};
constexpr unsigned kPlayCountBack=0x70FC36;  // je (the switch on the class)
constexpr unsigned char kPlayCountBackSig[]={0x74,0x36};
constexpr unsigned kPlayCountSkip=0x70FD82;  // inc ebx: the loop's next row
constexpr unsigned char kPlayCountSkipSig[]={0xFF,0xC3};

using SgoNodeFn=const unsigned char* (*)(void* doc,const wchar_t* key);
using CountFn=int (*)(void* config);

std::atomic<bool> installed{false};
std::atomic<bool> refused{false};   // a mode data load kept the stock four (logged)

std::mutex lock;   // the copies and the pending category copy
struct Block {
    const unsigned char* at;
    std::size_t size;
};
std::vector<Block> resident;
const unsigned char* pendingFrom=nullptr;   // the stock category node read with the SoldierInit just copied
const unsigned char* pendingTo=nullptr;     // and its copy

// `bytes` in memory that is never freed: the copy made before if it has the same content (the block is position
// independent), a new one otherwise. Null: out of memory.
const unsigned char* Resident(const std::vector<unsigned char>& bytes) {
    for(const Block& b:resident)
        if(b.size==bytes.size() && std::memcmp(b.at,bytes.data(),bytes.size())==0)return b.at;
    auto* at=static_cast<unsigned char*>(std::malloc(bytes.size()));
    if(!at)return nullptr;
    std::memcpy(at,bytes.data(),bytes.size());
    resident.push_back({at,bytes.size()});
    return at;
}

void Refuse(const char* why) noexcept {
    refused.store(true);
    Log("NEWCLASS REFUSED: %s; this mode data keeps the four stock classes",why);
}

const unsigned char* Copies(const unsigned char* init,const unsigned char* cats) {
    sgob::Value initStock,catsStock,initOut,catsOut;
    if(!sgob::Read(init,initStock) || !sgob::Read(cats,catsStock)) {
        Refuse("SoldierInit / SoldierWeaponCategory are not SGO nodes as the game reads them");
        return init;
    }
    if(const char* why=zwclass::ExtendSoldierInit(initStock,initOut)) {
        Refuse(why);
        return init;
    }
    if(const char* why=zwclass::ExtendCategories(catsStock,catsOut)) {
        Refuse(why);
        return init;
    }
    const std::vector<unsigned char> initBytes=sgob::Write(initOut),catsBytes=sgob::Write(catsOut);
    if(initBytes.empty() || catsBytes.empty()) {
        Refuse("the copies could not be laid out");
        return init;
    }
    const unsigned char* const initCopy=Resident(initBytes);
    const unsigned char* const catsCopy=Resident(catsBytes);
    if(!initCopy || !catsCopy) {
        Refuse("out of memory for the copies");
        return init;
    }
    pendingFrom=cats;
    pendingTo=catsCopy;
    static bool logged=false;
    if(!logged) {
        logged=true;
        Log("NEWCLASS SoldierInit %u -> %u classes, SoldierWeaponCategory %u -> %u rows (%zu + %zu resident bytes)",
            static_cast<unsigned>(initStock.items.size()),static_cast<unsigned>(initOut.items.size()),
            static_cast<unsigned>(catsStock.items.size()),static_cast<unsigned>(catsOut.items.size()),initBytes.size(),catsBytes.size());
    }
    return initCopy;
}

// 0xE2C17: SoldierInit (and SoldierWeaponCategory with it) as the copies.
const unsigned char* SoldierInitHook(void* doc,const wchar_t* key) noexcept {
    const auto read=reinterpret_cast<SgoNodeFn>(image+kSgoNode);
    const unsigned char* const init=read(doc,key);
    std::lock_guard<std::mutex> hold(lock);
    pendingFrom=pendingTo=nullptr;
    const unsigned char* const cats=read(doc,L"SoldierWeaponCategory");
    if(!init || !cats) {
        Refuse("CONFIG.SGO has no SoldierInit or no SoldierWeaponCategory");
        return init;
    }
    try {
        return Copies(init,cats);
    } catch(const std::bad_alloc&) {
        Refuse("out of memory for the copies");
        return init;
    }
}

// 0xE2C2A: SoldierWeaponCategory as the copy made with the SoldierInit copy.
const unsigned char* CategoryHook(void* doc,const wchar_t* key) noexcept {
    const unsigned char* const cats=reinterpret_cast<SgoNodeFn>(image+kSgoNode)(doc,key);
    std::lock_guard<std::mutex> hold(lock);
    const unsigned char* out=cats;
    if(pendingFrom) {
        if(cats==pendingFrom)out=pendingTo;
        else Log("NEWCLASS: SoldierWeaponCategory is not the node read with SoldierInit; the new classes' categories have no names");
    }
    pendingFrom=pendingTo=nullptr;
    return out;
}

// The class count, as code with room for the stock four reads it.
int StockClassCountHook(void* config) noexcept {
    return zwclass::StockClassesOf(reinterpret_cast<CountFn>(image+kClassCount)(config));
}

unsigned char* Gs() noexcept { return At<unsigned char*>(image,kGameStatus); }

bool Redirect(unsigned site,unsigned target,void* hook) noexcept {
    bool changed=false;
    if(RedirectCall(image+site,image+target,hook,changed))return true;
    Log("NEWCLASS the call at EDF+0x%X (to 0x%X) could not be redirected (another hook?)",site,target);
    return false;
}
}  // namespace

const wchar_t* SoldierClassInternalName(int type) noexcept {
    const zwclass::ClassDef* c=zwclass::Find(type);
    return c?c->internalName:nullptr;
}
const wchar_t* SoldierClassNameKey(int type) noexcept {
    const zwclass::ClassDef* c=zwclass::Find(type);
    return c?c->nameKey:nullptr;
}

bool SoldierClassesActive() noexcept { return installed.load() && !refused.load(); }

bool InstallSoldierClasses() noexcept {
    if(Gs()) {
        Log("NEWCLASS REFUSED: the game status exists already (the mode data was read with the four stock classes)");
        return false;
    }
    // 1. Everything checked, nothing written.
    if(!Matches(kModeLoad,kModeLoadSig,sizeof(kModeLoadSig))) {
        Log("NEWCLASS REFUSED: the mode data load's SoldierInit / SoldierWeaponCategory reads at EDF+0x%X are not the expected code",kModeLoad);
        return false;
    }
    for(const CountSite& s:kCountSites) {
        if(!Matches(s.rva,s.code,s.size)) {
            Log("NEWCLASS REFUSED: the class count read at EDF+0x%X is not the expected code",s.rva);
            return false;
        }
    }
    if(!Matches(kPlayCount,kPlayCountSig,sizeof(kPlayCountSig)) || !Matches(kPlayCountBack,kPlayCountBackSig,sizeof(kPlayCountBackSig)) ||
       !Matches(kPlayCountSkip,kPlayCountSkipSig,sizeof(kPlayCountSkipSig))) {
        Log("NEWCLASS REFUSED: the play counts' class switch at EDF+0x%X is not the expected code",kPlayCount);
        return false;
    }
    // 2. The writes, each prefix harmless with four classes.
    for(const CountSite& s:kCountSites)
        if(!Redirect(s.rva,kClassCount,reinterpret_cast<void*>(&StockClassCountHook))) {
            Log("NEWCLASS REFUSED: the class counts are not all clamped; SoldierInit not redirected");
            return false;
        }
    unsigned char stub[zwclass::kPlayCountStubSize];
    zwclass::PlayCountStub(reinterpret_cast<std::uint64_t>(image+kPlayCountBack),reinterpret_cast<std::uint64_t>(image+kPlayCountSkip),stub);
    if(!edf::RedirectToCode(image+kPlayCount,kPlayCountSig,sizeof(kPlayCountSig),stub,sizeof(stub),edf::Branch::kJump)) {
        Log("NEWCLASS REFUSED: no stub for the play counts' class switch at EDF+0x%X; SoldierInit not redirected",kPlayCount);
        return false;
    }
    if(!Redirect(kCategoryCall,kSgoNode,reinterpret_cast<void*>(&CategoryHook)) ||
       !Redirect(kInitCall,kSgoNode,reinterpret_cast<void*>(&SoldierInitHook))) {
        Log("NEWCLASS REFUSED: SoldierInit / SoldierWeaponCategory reads not redirected; the game keeps four classes");
        return false;
    }
    installed.store(true);
    Log("NEWCLASS installed: SoldierInit read at EDF+0x%X gives %d classes (+%ls, %ls)",kInitCall,zwclass::kClassCount,
        zwclass::kClasses[0].internalName,zwclass::kClasses[1].internalName);
    return true;
}
}  // namespace crew
