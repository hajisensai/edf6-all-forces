// Humanoid_BigGreyBoss (E516) part events (docs/greyboss-parts-re.md). Steam 678CCB46.
//
// The boss keeps its parts (SGO `parts`, built by the constructor 0x441040 at 0x442715) in an
// unordered_map<wstring, weak_ptr<part>> at +0x17E0: the list's head (end node) at +0x17E8, its size at +0x17F0;
// a node holds its key (wstring) at +0x10 and the part's weak_ptr at +0x40 / +0x48. Three CAS events of its
// model ask a part by the event's "name": parts_action (0x443B90), パーツ稼働 (0x443BF0), パーツ停止 (0x443C30).
// Each calls 0x4444B0(root, name) = parts.find(name)->second.lock().get(), and treats null as "no part".
// 0x4444B0 never compares the find with end(): on a name the boss has no part for (0x444600: rbx = the head)
// it reads the head node's never-constructed value (0x444641: mov rdx,[rbx+0x48]) and locks whatever it holds.
//
// E516's own data hits that: its MAB has parts_action name=中心砲台 at frame 113.2 in attack_rollingLazer's (and
// attack_generate's) events, and E516_BIGGREYBOSS.SGO's `parts` has no 中心砲台. What the head holds is whatever
// the allocation left there: a boss built from reused heap (a mid-mission spawn) found stale bytes and the lock
// faulted (EDF.dll+0x44464A, reading 0x7200000067: the crash of 2026-10-24, docs/greyboss-parts-re.md §1).
//
// The three calls go through Lookup: the same find, over the same list, and the native call only for a name
// the boss has; a name it lacks gives null, which every caller already handles. A list this cannot walk
// (not readable, its links or size disagreeing) is left to the native call, unchanged.
#include "greyboss_parts.h"
#include "crew.h"
#include "memory.h"
#include <cwchar>

namespace crew {
namespace greyboss_parts {
constexpr unsigned kLookup=0x4444B0;
constexpr std::size_t kPartsHead=0x17E8,kPartsSize=0x17F0;
constexpr std::size_t kNodeNext=0x0,kNodePrev=0x8,kNodeKey=0x10,kKeySize=0x20,kKeyCapacity=0x28,kNodeBytes=0x50;
constexpr std::size_t kMostParts=4096;
using LookupFn=void*(__fastcall*)(void*,const wchar_t*);

struct Site {
    unsigned rva;unsigned char original[5]{},patched[5]{};void* thunk=nullptr;bool changed=false;
};
// The call of 0x4444B0 in each event handler: parts_action, パーツ稼働, パーツ停止.
Site sites[]={{0x443BB9},{0x443C15},{0x443C55}};
unsigned char* installedImage=nullptr;
volatile LONG misses=0;

enum class Found { yes,no,unknown };
// Whether the boss's parts map has `name`, walked the way the list links it (the native find compares the
// same keys: length, then every UTF-16 unit).
Found Has(const unsigned char* root,const wchar_t* name) noexcept {
    if(!root || !name || !Readable(root+kPartsHead,16))return Found::unknown;
    const auto* head=At<const unsigned char*>(root,kPartsHead);
    const auto size=At<std::size_t>(root,kPartsSize);
    if(!head || size>kMostParts || !Readable(head,16))return Found::unknown;
    std::size_t length=0;
    while(length<256 && Readable(name+length,sizeof(wchar_t)) && name[length])++length;
    if(length>=256 || !Readable(name+length,sizeof(wchar_t)))return Found::unknown;
    const unsigned char* previous=head;
    const auto* node=At<const unsigned char*>(head,kNodeNext);
    std::size_t walked=0;
    while(node!=head) {
        if(walked>=size || !Readable(node,kNodeBytes) || At<const unsigned char*>(node,kNodePrev)!=previous)return Found::unknown;
        const auto keyLength=At<std::size_t>(node,kKeySize);
        const wchar_t* key=At<std::size_t>(node,kKeyCapacity)>=8 ? At<const wchar_t*>(node,kNodeKey) :
            reinterpret_cast<const wchar_t*>(node+kNodeKey);
        if(keyLength==length) {
            if(length && !Readable(key,length*sizeof(wchar_t)))return Found::unknown;
            if(std::wmemcmp(key,name,length)==0)return Found::yes;
        }
        ++walked;previous=node;node=At<const unsigned char*>(node,kNodeNext);
    }
    return walked==size && At<const unsigned char*>(head,kNodePrev)==previous ? Found::no : Found::unknown;
}

void* __fastcall Lookup(void* root,const wchar_t* name) {
    if(Has(static_cast<const unsigned char*>(root),name)==Found::no) {
        if(InterlockedIncrement(&misses)<=8)
            Log("GREYBOSS %p: a part event names %ls, which this boss has no part for: no part acts",root,name);
        return nullptr;
    }
    return reinterpret_cast<LookupFn>(image+kLookup)(root,name);
}

bool Restore() noexcept {
    if(!installedImage)return true;
    bool ok=true;
    for(auto& site:sites) {
        if(site.changed) {
            if(edf::PatchCode(installedImage+site.rva,site.patched,site.original,5))site.changed=false;
            else ok=false;
        }
        if(!site.changed && site.thunk) { VirtualFree(site.thunk,0,MEM_RELEASE);site.thunk=nullptr; }
    }
    if(ok)installedImage=nullptr;
    return ok;
}
}  // namespace greyboss_parts

bool InstallGreyBossParts() noexcept {
    using namespace greyboss_parts;
    __try {
        if(!image)return false;
        if(installedImage) {
            if(installedImage!=image)return false;
            bool complete=true;
            for(const auto& site:sites)
                if(!site.changed || !Readable(image+site.rva,5) || std::memcmp(image+site.rva,site.patched,5))complete=false;
            if(complete)return true;
            if(!Restore())return false;
        }
        // Every site checked before anything is allocated or written.
        for(auto& site:sites) {
            const auto* code=image+site.rva;
            if(!Readable(code,5) || code[0]!=0xE8)return false;
            std::int32_t relative=0;std::memcpy(&relative,code+1,4);
            if(code+5+relative!=image+kLookup)return false;
            std::memcpy(site.original,code,5);std::memcpy(site.patched,code,5);
        }
        installedImage=image;
        for(auto& site:sites) {
            site.thunk=AllocateNearThunk(image+site.rva,reinterpret_cast<void*>(&Lookup));
            if(!site.thunk) { Restore();return false; }
            const auto delta=static_cast<unsigned char*>(site.thunk)-(image+site.rva+5);
            if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0) { Restore();return false; }
            const auto relative=static_cast<std::int32_t>(delta);std::memcpy(site.patched+1,&relative,4);
        }
        for(auto& site:sites) {
            const bool ok=edf::PatchCode(image+site.rva,site.original,site.patched,5);
            site.changed=Readable(image+site.rva,5) && std::memcmp(image+site.rva,site.patched,5)==0;
            if(!ok || !site.changed) { Restore();return false; }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { greyboss_parts::Restore();return false; }
}

bool RestoreGreyBossParts() noexcept {
    __try { return greyboss_parts::Restore(); } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}  // namespace crew
