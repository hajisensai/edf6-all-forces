#include "command_identity.h"
#include "memory.h"
#include "support_soldier.h"

namespace crew {
namespace command_identity {
constexpr unsigned kEnumAllTeams=0x5E0C80,kTeamManager=0x20B2978,kGameStatus=0x20B2890;
constexpr unsigned kMaxPlayers=1024,kMaxIds=kMaxCommandIdentities+2;
constexpr unsigned char kEnumSignature[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83};
using WalkFn=void(__fastcall*)(void*,void*);
struct Visitor {
    const void* const* vtable;
    unsigned char ids[kMaxIds][32]{};
    ObjRef refs[kMaxIds]{};
    unsigned count=0,expected=0;
    bool failed=false;
    void* puid=nullptr;
};
bool Canonical(const unsigned char* id) noexcept {
    if(!id || !Readable(id,32))return false;
    unsigned any=0;
    for(unsigned i=0;i<32;++i){if(i>=20 && i<24 && id[i])return false;any|=id[i];}
    return any!=0;
}
bool Live(const unsigned char* object,ObjRef* ref) noexcept {
    if(!Readable(object,kDead+1) || (object[0x18]&4) || object[kDead])return false;
    const auto ctrl=At<const unsigned char*>(object,kSelfCtrl);
    if(!Readable(ctrl,16) || At<LONG>(ctrl,8)<=0 || At<const void*>(object,kSelf)!=object)return false;
    *ref=ObjRef{object,ctrl};return true;
}
bool Requester(const unsigned char* object,unsigned expected,void** puid) noexcept {
    if(!Readable(object,0x1EE0) || !IsAnyPlayer(object) || !IsSoldierClass(object))return false;
    const auto user=At<const unsigned char*>(object,0x1ED0);
    const auto ctrl=At<const unsigned char*>(object,0x1ED8);
    if(!Readable(user,0x4C) || !Readable(ctrl,16) || At<LONG>(ctrl,8)<=0)return false;
    const int index=At<int>(user,0x48);
    if(index<0 || static_cast<unsigned>(index)>=expected)return false;
    *puid=At<void*>(user,0x18);return *puid!=nullptr;
}
void __fastcall Ignore(void*) noexcept {}
void __fastcall Visit(Visitor* v,const unsigned char* object) noexcept {
    if(v->failed || !object)return;
    __try {
        unsigned char id[32]{};
        if(!ReadNativeObjectId(object,id))return;
        for(unsigned i=0;i<v->count;++i) {
            if(std::memcmp(id,v->ids[i],32))continue;
            ObjRef ref{};
            if(!Live(object,&ref) || (v->refs[i] &&
                (v->refs[i].obj!=ref.obj || v->refs[i].ctrl!=ref.ctrl))) {v->failed=true;return;}
            if(i==0 && !Requester(object,v->expected,&v->puid)){v->failed=true;return;}
            v->refs[i]=ref;return;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){v->failed=true;}
}
const void* const kVisitorVtable[]={reinterpret_cast<const void*>(&Ignore),reinterpret_cast<const void*>(&Visit)};
bool Add(Visitor* v,const unsigned char* id) noexcept {
    if(!Canonical(id) || v->count>=kMaxIds)return false;
    for(unsigned i=0;i<v->count;++i)if(!std::memcmp(id,v->ids[i],32))return false;
    std::memcpy(v->ids[v->count++],id,32);return true;
}
bool StillLive(const Visitor& v) noexcept {
    for(unsigned i=0;i<v.count;++i) {
        const auto object=static_cast<const unsigned char*>(v.refs[i].obj);
        ObjRef ref{};unsigned char id[32]{};
        if(!Live(object,&ref) || ref.ctrl!=v.refs[i].ctrl ||
            !ReadNativeObjectId(object,id) || std::memcmp(id,v.ids[i],32))return false;
    }
    void* puid=nullptr;
    return Requester(static_cast<const unsigned char*>(v.refs[0].obj),v.expected,&puid) && puid==v.puid;
}
}
bool ResolveCommandIdentities(const unsigned char requester[32],const unsigned char units[][32],
                              unsigned count,const unsigned char focus[32],CommandIdentities* out) noexcept {
    using namespace command_identity;
    if(!out)return false;
    __try {
        *out=CommandIdentities{};
        if(count>kMaxCommandIdentities || (count && !units))return false;
        Visitor visitor{kVisitorVtable};
        if(!Add(&visitor,requester))return false;
        for(unsigned i=0;i<count;++i)if(!Add(&visitor,units[i]))return false;
        if(focus && !Add(&visitor,focus))return false;
        if(!Matches(kEnumAllTeams,kEnumSignature,sizeof(kEnumSignature)))return false;
        void* mgr=At<void*>(image,kTeamManager);
        const auto status=At<const unsigned char*>(image,kGameStatus);
        if(!Readable(mgr,0x50) || !Readable(status,0x14FFC))return false;
        void* teams=At<void*>(mgr,0x38);
        visitor.expected=At<unsigned>(status,0x14FF8);
        if(!visitor.expected || visitor.expected>kMaxPlayers || !Readable(teams,7*0x38))return false;
        reinterpret_cast<WalkFn>(image+kEnumAllTeams)(mgr,&visitor);
        if(visitor.failed || At<void*>(image,kTeamManager)!=mgr || At<const void*>(image,kGameStatus)!=status ||
           At<void*>(mgr,0x38)!=teams || At<unsigned>(status,0x14FF8)!=visitor.expected || !StillLive(visitor))return false;
        CommandIdentities result{};result.requester=visitor.refs[0];result.requesterPuid=visitor.puid;result.count=count;
        for(unsigned i=0;i<count;++i)result.units[i]=visitor.refs[i+1];
        if(focus)result.focus=visitor.refs[count+1];
        *out=result;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
