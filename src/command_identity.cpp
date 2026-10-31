#include "command_identity.h"
#include "memory.h"
#include <cstring>

// The older resolvers over the one walk of net_object_resolve.cpp (ResolveNetRefs): the same references, the same rules.
namespace crew {
bool ResolveCommandIdentities(const unsigned char requester[32],const unsigned char units[][32],
                              unsigned count,const unsigned char focus[32],CommandIdentities* out) noexcept {
    if(!out)return false;
    __try {
        *out=CommandIdentities{};
        if(count>kMaxCommandIdentities || (count && !units) || !netref::IsPlayer(requester))return false;
        const unsigned char* refs[kMaxNetRefs]{};
        NetLife lives[kMaxNetRefs]{};
        unsigned n=0;
        refs[n++]=requester;
        for(unsigned i=0;i<count;++i) {
            if(!netref::IsNative(units[i]))return false;   // an NPC unit: never a player
            refs[n++]=units[i];
        }
        if(focus) {
            const netref::Kind k=netref::Classify(focus);
            if(k!=netref::Kind::player && k!=netref::Kind::native)return false;
            refs[n++]=focus;
        }
        for(unsigned i=0;i<n;++i)for(unsigned j=0;j<i;++j)if(!std::memcmp(refs[i],refs[j],32))return false;
        for(unsigned i=0;i<n;++i)lives[i]=NetLife::live;
        NetResolved found[kMaxNetRefs]{};
        if(!ResolveNetRefs(refs,lives,n,found))return false;
        for(unsigned i=0;i<n;++i) {
            if(!found[i].found)return false;
            for(unsigned j=0;j<i;++j)if(found[j].obj.obj==found[i].obj.obj)return false;   // one object named twice
        }
        if(!found[0].puid)return false;
        for(unsigned i=0;i<n;++i)if(!StillNetRef(refs[i],NetLife::live,found[i]))return false;
        CommandIdentities result{};result.requester=found[0].obj;result.requesterPuid=found[0].puid;result.count=count;
        for(unsigned i=0;i<count;++i)result.units[i]=found[i+1].obj;
        if(focus)result.focus=found[count+1].obj;
        *out=result;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){*out=CommandIdentities{};return false;}
}
namespace {
bool ResolveMarks(const unsigned char marker[32],const unsigned char target[32],bool deadTarget,MarkIdentities* out) noexcept {
    if(!out)return false;
    *out=MarkIdentities{};
    // The marker goes by its mission slot alone (a NetId names no player); a reference not valid is not asked.
    const unsigned char* refs[2]={netref::IsPlayer(marker) ? marker : nullptr,
                                  netref::Valid(target,false) ? target : nullptr};
    if(!refs[0] && !refs[1])return false;
    const NetLife lives[2]={NetLife::live,deadTarget ? NetLife::present : NetLife::live};
    NetResolved found[2]{};
    if(!ResolveNetRefs(refs,lives,2,found))return false;
    if(found[0].found){out->player=found[0].obj;out->slot=found[0].slot;out->puid=found[0].puid;}
    if(found[1].found)out->target=found[1].obj;
    return true;
}
}  // namespace
bool ResolveMarkIdentities(const unsigned char marker[32],const unsigned char target[32],MarkIdentities* out) noexcept {
    return ResolveMarks(marker,target,false,out);
}
bool ResolveGrabIdentities(const unsigned char grabber[32],const unsigned char target[32],MarkIdentities* out) noexcept {
    return ResolveMarks(grabber,target,true,out);
}
bool PlayerRequesterOf(const ObjRef& who,unsigned char out[32]) noexcept {
    if(!out)return false;
    __try {
        const auto object=static_cast<const unsigned char*>(who.obj);
        if(!EncodeNetRef(who,out) || !netref::IsPlayer(out) || !Readable(object,kDead+1) || object[kDead] || (object[0x18]&4)) {
            std::memset(out,0,32);return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){std::memset(out,0,32);return false;}
}
bool PlayerRequesterOf(const void* object,unsigned char out[32]) noexcept {
    if(out)std::memset(out,0,32);
    __try { return object && out && Readable(object,kSelfCtrl+8) && PlayerRequesterOf(ObjRef::Of(object),out); }
    __except(EXCEPTION_EXECUTE_HANDLER){if(out)std::memset(out,0,32);return false;}
}
}
