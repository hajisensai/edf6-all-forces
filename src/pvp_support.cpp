// Support deployment ownership is resolved from a live canonical native ID on
// this machine. No PUID or team claimed by the support payload is authority.
#include "pvp_match.h"
#include "pvp.h"
#include "pvp_native.h"
#include "command_identity.h"
#include "net_object_ref.h"
#include "coop_extension_api.h"
#include "support_net.h"
#include "support_soldier.h"
#include "memory.h"
#include <cstring>

namespace crew {
namespace {
bool EmptyRequester(const unsigned char (&requester)[32]) noexcept {
    for(unsigned char byte:requester)if(byte)return false;
    return true;
}
// The requester is the player's mission slot (net_object_ref.h), never its NetId: a stock mission numbers the players'
// NetIds per machine, so a guest found no host player by the host's and refused every plan the host asked for (2026-10-31).
// The mark resolver finds the one live player of that slot here (borrowed, immediate game-thread use); Coop resolves the
// slot to a real transport PUID, then the sealed-world gate proves it is also that player's native EOS identity.
bool ResolveOwner(const unsigned char (&requester)[32],const ObjRef* expected,EDF6CoopPeer* out) noexcept {
    if(!out)return false;
    *out={};
    int slot=-1;
    if(!netref::IsPlayer(requester,&slot))return false;
    MarkIdentities mark{};
    if(!ResolveMarkIdentities(requester,nullptr,&mark) || !mark.player || mark.slot!=slot || !mark.puid)return false;
    if(expected && (mark.player.obj!=expected->obj || mark.player.ctrl!=expected->ctrl))return false;
    const HMODULE coop=GetModuleHandleW(L"EDF6Coop.dll");
    const auto resolve=coop ? reinterpret_cast<EDF6CoopResolveMissionPlayerPuidFn>(
        GetProcAddress(coop,"EDF6Coop_ResolveMissionPlayerPuid")) : nullptr;
    if(!resolve)return false;
    EDF6CoopPeer owner{};
    __try {
        if(!resolve(slot,&owner) || !pvp_protocol::ValidPuid(owner.id) ||
           !SenderIsPlayer(mark.puid,owner.id))return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    *out=owner;return true;
}
bool LiveParent(const ObjRef& parent) noexcept {
    const auto* object=static_cast<const unsigned char*>(parent.obj);
    return parent.ctrl && Readable(parent.ctrl,16) && At<LONG>(parent.ctrl,8)>0 &&
        object && Readable(object,kDead+1) && At<const void*>(object,kSelfCtrl)==parent.ctrl &&
        !(object[0x18]&4) && !object[kDead];
}
}

bool PvpRefreshSupportOwner(support_net::Plan* plan) noexcept {
    if(!plan)return false;
    if(EmptyRequester(plan->requester)) {
        if(PvpSupportTeam(nullptr)!=0)return false;
        plan->team=0;return true;
    }
    EDF6CoopPeer owner{};
    if(!ResolveOwner(plan->requester,nullptr,&owner))return false;
    const int team=PvpSupportTeam(owner.id);
    if(team<0)return false;
    plan->team=static_cast<std::uint32_t>(team);return true;
}

bool PvpMakeInheritedSupportPlan(const ObjRef& parent,support_net::Plan* plan) noexcept {
    if(!plan)return false;
    __try {
        if(!LiveParent(parent))return false;
        char cached[65]{};
        const bool known=PvpNativeSupportOwner(parent,cached,sizeof(cached));
        if(!EmptyRequester(plan->requester)) {
            EDF6CoopPeer owner{};
            if(!known || !ResolveOwner(plan->requester,nullptr,&owner) || std::strcmp(cached,owner.id))return false;
            const int team=PvpSupportTeam(owner.id);
            if(team<0)return false;
            plan->team=static_cast<std::uint32_t>(team);return true;
        }
        ObjRef ownerPlayer=parent;
        if(!((IsSoldierClass(parent.obj) && IsAnyPlayer(static_cast<const unsigned char*>(parent.obj))) ||
             PvpNativeSupportDriver(parent,&ownerPlayer))) {
            if(InSession() || PvpSupportTeam(nullptr)!=0)return false;
            plan->team=0;return true;
        }
        unsigned char requester[32]{};EDF6CoopPeer owner{};
        if(!PlayerRequesterOf(ownerPlayer,requester) ||
           !ResolveOwner(requester,&ownerPlayer,&owner) || (known && std::strcmp(cached,owner.id)))return false;
        const int team=PvpSupportTeam(owner.id);
        if(team<0)return false;
        std::memcpy(plan->requester,requester,sizeof(requester));plan->team=static_cast<std::uint32_t>(team);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool PvpInheritSupport(const ObjRef& parent,const ObjRef* children,unsigned count) noexcept {
    if(!children || !count || count>support_net::kMaxUnits)return false;
    char puid[65]{};
    bool owned=PvpNativeSupportOwner(parent,puid,sizeof(puid));
    if(!owned) {
        __try {
            ObjRef ownerPlayer=parent;
            if(LiveParent(parent) && ((IsSoldierClass(parent.obj) && IsAnyPlayer(static_cast<const unsigned char*>(parent.obj))) ||
               PvpNativeSupportDriver(parent,&ownerPlayer))) {
                unsigned char requester[32]{};EDF6CoopPeer owner{};
                if(PlayerRequesterOf(ownerPlayer,requester) && ResolveOwner(requester,&ownerPlayer,&owner)) {
                    std::memcpy(puid,owner.id,sizeof(puid));owned=true;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    if(!owned)return PvpSupportTeam(nullptr)==0;
    const int team=PvpSupportTeam(puid);
    return team>=0 && PvpNativeAttachSupport(puid,children,count,team);
}

bool PvpValidateSupportPlan(const support_net::Plan& plan) noexcept {
    if(!plan.count || plan.count>support_net::kMaxUnits || (plan.team && !pvp_match::PvpTeam(static_cast<int>(plan.team))))return false;
    // Old peers/planners have neither owner nor PvP metadata. They are safe
    // only while fully disabled, outside every mode/team transaction or restore.
    if(EmptyRequester(plan.requester))return plan.team==0 && PvpSupportTeam(nullptr)==0;
    EDF6CoopPeer owner{};
    if(!ResolveOwner(plan.requester,nullptr,&owner))return false;
    const int team=PvpSupportTeam(owner.id);
    return team>=0 && plan.team==static_cast<std::uint32_t>(team);
}

bool PvpAttachSupportPlan(const support_net::Plan& plan,const ObjRef* objects,unsigned count) noexcept {
    // Never register a truncated deployment or silently drop the tail of a plan.
    if(!objects || count!=plan.count || !count || count>support_net::kMaxUnits ||
       !PvpValidateSupportPlan(plan))return false;
    if(EmptyRequester(plan.requester))return true;
    // Validate again at the mutation boundary; no borrowed native actor/PUID
    // reference from planning or validation is retained in a deployment.
    EDF6CoopPeer owner{};
    if(!ResolveOwner(plan.requester,nullptr,&owner))return false;
    const int team=PvpSupportTeam(owner.id);
    if(team<0 || plan.team!=static_cast<std::uint32_t>(team))return false;
    return PvpNativeAttachSupport(owner.id,objects,count,team);
}
}
