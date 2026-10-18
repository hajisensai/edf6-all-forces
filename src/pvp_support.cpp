// Support deployment ownership is resolved from a live canonical native ID on
// this machine. No PUID or team claimed by the support payload is authority.
#include "pvp.h"
#include "pvp_native.h"
#include "command_identity.h"
#include "coop_extension_api.h"
#include "support_net.h"
#include <cstring>

namespace crew {
namespace {
bool EmptyRequester(const unsigned char (&requester)[32]) noexcept {
    for(unsigned char byte:requester)if(byte)return false;
    return true;
}
bool CanonicalRequester(const unsigned char (&requester)[32]) noexcept {
    if(EmptyRequester(requester))return false;
    for(unsigned i=20;i<24;++i)if(requester[i])return false;
    return true;
}
bool SameRequester(const CommandIdentities& command,const MarkIdentities& mark) noexcept {
    return command.requester && mark.player && mark.slot>=0 && mark.puid &&
        command.requester.obj==mark.player.obj && command.requester.ctrl==mark.player.ctrl &&
        command.requesterPuid==mark.puid;
}
// Both native helpers return borrowed identities for immediate game-thread use.
// The strict command resolver rejects ambiguous IDs and registry changes; the
// mark resolver additionally supplies the mission slot of that exact player.
// Coop resolves that slot to a real transport PUID, then the sealed-world gate
// proves it is also the player's native EOS identity in the current world.
bool ResolveOwner(const support_net::Plan& plan,EDF6CoopPeer* out) noexcept {
    if(!out)return false;
    *out={};
    if(!CanonicalRequester(plan.requester))return false;
    CommandIdentities command{};
    MarkIdentities mark{};
    if(!ResolveCommandIdentities(plan.requester,nullptr,0,nullptr,&command) ||
       !ResolveMarkIdentities(plan.requester,nullptr,&mark) || !SameRequester(command,mark))return false;
    const HMODULE coop=GetModuleHandleW(L"EDF6Coop.dll");
    const auto resolve=coop ? reinterpret_cast<EDF6CoopResolveMissionPlayerPuidFn>(
        GetProcAddress(coop,"EDF6Coop_ResolveMissionPlayerPuid")) : nullptr;
    if(!resolve)return false;
    EDF6CoopPeer owner{};
    __try {
        if(!resolve(mark.slot,&owner) || !pvp_protocol::ValidPuid(owner.id) ||
           !SupportCommandRequesterMatches(mark.puid,owner.id))return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    *out=owner;return true;
}
}

bool PvpValidateSupportPlan(const support_net::Plan& plan) noexcept {
    if(!plan.count || plan.count>support_net::kMaxUnits || plan.team>2)return false;
    // Old peers/planners have neither owner nor PvP metadata. They are safe
    // only while fully disabled, outside every mode/team transaction or restore.
    if(EmptyRequester(plan.requester))return plan.team==0 && PvpSupportTeam(nullptr)==0;
    EDF6CoopPeer owner{};
    if(!ResolveOwner(plan,&owner))return false;
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
    if(!ResolveOwner(plan,&owner))return false;
    const int team=PvpSupportTeam(owner.id);
    if(team<0 || plan.team!=static_cast<std::uint32_t>(team))return false;
    return PvpNativeAttachSupport(owner.id,objects,count,team);
}
}
