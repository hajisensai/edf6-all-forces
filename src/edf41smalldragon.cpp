// EDF4.1's small dragons (the DLC's DragonSmall401_HS, RedSmallDragon_HS, YerrowSmallDragon_HS: Mission Pack 1, M805 /
// M993) on EDF6's DragonSmall with 4.1's own model, CAS, MAB and ragdoll (tools/make_edf41_objects.py SMALL_DRAGON_*).
// Only an object whose SGO carries the key `edf41_ragdoll_bodies` is touched; EDF6's own dragons go through unchanged.
//
// The hit reaction's ragdoll bodies. When a hit knocks the dragon about (the reaction state, EDF.dll 0x339550 with
// edx 0), the class builds the list {koshi, mune} (0x3395E6 / 0x339601, two push_backs through 0x33B3C0) and hands it
// to 0x4A8570(object, names, count), which looks each name up in the ragdoll's body table (this+0xB10, 0x6E4160 with
// mode 4) and dereferences a miss: the end sentinel's null value, EDF+0x6E42EB `mov rbx,[rax+8]` (the M805 crash,
// 2026-10-14). The names are EDF5's frog dragon's, the model EDF6's class was written for; 4.1's small-dragon ragdolls
// (Ragdoll_SmallDragon401_has_hka, Ragdoll_RedSmallDragon, Ragdoll_YerrowSmallDragon) have koshi and the torso bodies
// spine1 / spine2, no mune. 4.1's own DragonSmall, in the same reaction (EDF41.exe 0x1fafc0, the same random draw and
// state play), hands {koshi, spine1, spine2} to its 0x22d7b0 (0x1fb06b / 0x1fb0af / 0x1fb0eb). The call at 0x33962C
// goes through RagdollHook: for a marked object the names are the key's (the converter writes 4.1's three and checks
// each is a body of the ragdoll it ships, check_class_names), the class's own otherwise. The class's other ragdoll
// call (0x339DD6 -> 0x4A8620) passes no names (every body), so it needs nothing.
//
// The key is read the way the class reads its own (classic SGO): 0x528A0(&object+0xA0, name) gives the 12-byte node
// {type (0 array, 3 string), count, offset of the first child / the characters from the node} or null.
// EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
constexpr std::size_t kSite=0x33962C;            // call 0x4A8570 in the reaction state (0x339550)
constexpr std::size_t kSpanFrom=0x3395E6;        // the list {koshi, mune} built, then the call
constexpr std::size_t kPartialRagdoll=0x4A8570;  // (object, const wchar_t** names, int count)
constexpr std::size_t kKoshi=0x17AC278,kMune=0x17AC288;
constexpr std::size_t kKeyNode=0x528A0;
constexpr std::size_t kSgoAt=0xA0;
constexpr int kMostBodies=8;                     // make_edf41_objects.SMALL_DRAGON_MOST_BODIES
constexpr int kLongestName=31;                   // make_edf41_objects.SMALL_DRAGON_LONGEST_BODY

// 0x3395E6..0x339631: lea rax,[koshi]; mov [rsp+20],rax; lea rdx,[rsp+20]; lea rcx,[rsp+30]; call 0x33B3C0; the same for
// mune; mov r8d,[rsp+48]; mov rsi,[rsp+38]; mov rdx,rsi; mov rcx,rdi; call 0x4A8570.
constexpr unsigned char kSpan[]={
    0x48,0x8D,0x05,0x8B,0x2C,0x47,0x01,0x48,0x89,0x44,0x24,0x20,0x48,0x8D,0x54,0x24,0x20,0x48,0x8D,0x4C,0x24,0x30,0xE8,
    0xBF,0x1D,0x00,0x00,0x48,0x8D,0x05,0x80,0x2C,0x47,0x01,0x48,0x89,0x44,0x24,0x20,0x48,0x8D,0x54,0x24,0x20,0x48,0x8D,
    0x4C,0x24,0x30,0xE8,0xA4,0x1D,0x00,0x00,0x44,0x8B,0x44,0x24,0x48,0x48,0x8B,0x74,0x24,0x38,0x48,0x8B,0xD6,0x48,0x8B,
    0xCF,0xE8,0x3F,0xEF,0x16,0x00};
static_assert(kSpanFrom+sizeof(kSpan)==kSite+5,"the span ends with the call at kSite");

struct Node {
    std::int32_t type;
    std::int32_t count;
    std::int32_t value;
};
using KeyNodeFn=const Node*(__fastcall*)(void*,const wchar_t*);
using PartialRagdollFn=void(__fastcall*)(void*,const wchar_t* const*,int);

std::atomic<int> logged{0};

void LogLimited(const char* what,void* self,int value) noexcept {
    if(logged.fetch_add(1)<64)Log("EDF41SMALLDRAGON %p %s (%d)",self,what,value);
}

// The key's names in `out` (NUL-ended): their count, 0 when the object has no key, -1 when the key is malformed.
int KeyBodies(void* self,wchar_t (&out)[kMostBodies][kLongestName+1]) noexcept {
    const Node* key=reinterpret_cast<KeyNodeFn>(image+kKeyNode)(static_cast<char*>(self)+kSgoAt,L"edf41_ragdoll_bodies");
    if(!key)return 0;
    if(key->type!=0 || key->count<1 || key->count>kMostBodies)return -1;
    const Node* first=reinterpret_cast<const Node*>(reinterpret_cast<const char*>(key)+key->value);
    for(int i=0;i<key->count;++i) {
        const Node* s=first+i;
        if(s->type!=3 || s->count<1)return -1;
        const wchar_t* text=reinterpret_cast<const wchar_t*>(reinterpret_cast<const char*>(s)+s->value);
        const std::size_t length=wcsnlen(text,static_cast<std::size_t>(s->count));   // the count may include the NUL
        if(length<1 || length>static_cast<std::size_t>(kLongestName))return -1;
        std::memcpy(out[i],text,length*sizeof(wchar_t));
        out[i][length]=L'\0';
    }
    return key->count;
}

void __fastcall RagdollHook(void* self,const wchar_t* const* names,int count) {
    wchar_t own[kMostBodies][kLongestName+1];
    const int n=KeyBodies(self,own);
    if(n<0) {   // the converter writes the key checked (check_class_names): a malformed one is not its; the class's names go
        LogLimited("edf41_ragdoll_bodies is malformed: the class's own body names used",self,n);
        reinterpret_cast<PartialRagdollFn>(image+kPartialRagdoll)(self,names,count);
        return;
    }
    if(n==0) {
        reinterpret_cast<PartialRagdollFn>(image+kPartialRagdoll)(self,names,count);
        return;
    }
    const wchar_t* list[kMostBodies];
    for(int i=0;i<n;++i)list[i]=own[i];
    LogLimited("hit reaction on 4.1's ragdoll bodies (edf41_ragdoll_bodies)",self,n);
    reinterpret_cast<PartialRagdollFn>(image+kPartialRagdoll)(self,list,n);
}
}  // namespace

bool InstallEdf41SmallDragon() noexcept {
    const bool names=std::wcscmp(reinterpret_cast<const wchar_t*>(image+kKoshi),L"koshi")==0 &&
                     std::wcscmp(reinterpret_cast<const wchar_t*>(image+kMune),L"mune")==0;
    if(!names || !Matches(kSpanFrom,kSpan,sizeof(kSpan))) {
        Log("EDF41SMALLDRAGON EDF+%zX is not DragonSmall's {koshi, mune} hit-reaction call (names %d): not hooked; the DLC's "
            "4.1 small dragons would crash on a hit",kSite,names ? 1 : 0);
        return false;
    }
    bool changed=false;
    const bool ok=RedirectCall(image+kSite,image+kPartialRagdoll,reinterpret_cast<void*>(&RagdollHook),changed);
    Log("EDF41SMALLDRAGON hooked: hit-reaction ragdoll bodies=%d",ok ? 1 : 0);
    return ok;
}
}  // namespace crew
