#include "support_source.h"
#include "crew.h"
#include "memory.h"
#include "team_walk.h"
#include "support_soldier.h"
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kEnumAllTeams=0x5E0C80,kTeamManager=0x20B2978;
constexpr unsigned char kEnumSignature[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,
    0x56,0x57,0x41,0x56,0x48,0x83};
bool LiveSource(const unsigned char* object,ObjRef* out) noexcept {
    if(!object || !Readable(object,kDead+1) || (object[0x18]&4) || object[kDead] ||
       At<const void*>(object,kSelf)!=object)return false;
    const auto* control=At<const unsigned char*>(object,kSelfCtrl);
    if(!Readable(control,16) || At<LONG>(control,8)<=0)return false;
    *out={object,control};return true;
}
std::uint64_t KeyOf(const unsigned char* id) noexcept {
    unsigned any=0;
    for(unsigned i=0;i<32;++i){if(i>=20 && i<24 && id[i])return 0;any|=id[i];}
    if(!any)return 0;
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<32;++i)hash=(hash^id[i])*1099511628211ull;
    return hash ? hash : 1;
}
struct Visitor { const void* const* vtable;std::uint64_t key;ObjRef ref{};unsigned char id[32]{};bool failed=false; };
void __fastcall Ignore(void*) noexcept {}
void __fastcall Visit(Visitor* visitor,const unsigned char* object) noexcept {
    if(visitor->failed || !object)return;
    __try {
        unsigned char id[32]{};ObjRef ref{};
        if(!ReadNativeObjectId(object,id) || KeyOf(id)!=visitor->key)return;
        if(!LiveSource(object,&ref) || (visitor->ref &&
            (visitor->ref.obj!=ref.obj || visitor->ref.ctrl!=ref.ctrl || std::memcmp(visitor->id,id,32)))) {
            visitor->failed=true;return;
        }
        visitor->ref=ref;std::memcpy(visitor->id,id,32);
    } __except(EXCEPTION_EXECUTE_HANDLER){visitor->failed=true;}
}
const void* const kVisitorVtable[]={reinterpret_cast<const void*>(&Ignore),reinterpret_cast<const void*>(&Visit)};
}
bool ResolveSupportSource(std::uint64_t key,ObjRef* out) noexcept {
    if(!out)return false;
    *out={};
    __try {
        if(!key || !image || !Matches(kEnumAllTeams,kEnumSignature,sizeof(kEnumSignature)))return false;
        void* const manager=At<void*>(image,kTeamManager);
        if(!Readable(manager,0x50) || !team_walk::WalkRows(manager))return false;
        Visitor visitor{kVisitorVtable,key};
        reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kEnumAllTeams)(manager,&visitor);
        if(visitor.failed || !visitor.ref || At<void*>(image,kTeamManager)!=manager)return false;
        ObjRef current{};unsigned char id[32]{};
        if(!LiveSource(static_cast<const unsigned char*>(visitor.ref.obj),&current) || current.ctrl!=visitor.ref.ctrl ||
           !ReadNativeObjectId(static_cast<const unsigned char*>(current.obj),id) || std::memcmp(id,visitor.id,32))return false;
        *out=current;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){*out={};return false;}
}
std::uint64_t SupportSourceKey(const ObjRef& source) noexcept {
    __try {
        ObjRef current{};unsigned char id[32]{};
        if(!LiveSource(static_cast<const unsigned char*>(source.obj),&current) || current.ctrl!=source.ctrl ||
           !ReadNativeObjectId(static_cast<const unsigned char*>(source.obj),id))return 0;
        const auto key=KeyOf(id);ObjRef resolved{};
        return key && ResolveSupportSource(key,&resolved) && resolved.obj==source.obj && resolved.ctrl==source.ctrl ? key : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
}
