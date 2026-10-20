// The gravity operations online (gravity_net.h; the wire gravity_net_protocol.h). Game thread only.
#include "gravity_net.h"
#include "command_identity.h"
#include "gravity_net_protocol.h"
#include "memory.h"
#include "plugin_net.h"
#include "support_protocol.h"
#include "support_net.h"
#include "support_soldier.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using gravity_net::kMaxPeers;
constexpr ULONGLONG kHoldStaleMs=150;    // the carrier called no Hold this long: it let go
constexpr ULONGLONG kRetryMs=250;        // a peer's grab whose target is not found here: looked for again after this
constexpr float kSameAt=4.0f;            // m: a thing found by the ray within this of where the grabber met it is the same
constexpr float kMostLead=0.25f;         // s: a hold's goal is moved on by its velocity at most this far past its message
constexpr int kMostQueued=8;

bool Same(const gravity::Target& a,const gravity::Target& b) noexcept {
    return a.kind==b.kind && a.obj.obj==b.obj.obj && a.obj.ctrl==b.obj.ctrl && (a.obj.obj || a.body==b.body);
}
float Dist(const float* a,const float* b) noexcept {
    const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};
    return std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}

// ---- this machine's player's grab ----
struct Out {
    bool holding=false;
    gravity::Target t;
    gravity_net::Grab hold{};       // its state as it goes out (origin, grab, IDs, ray, goal)
    ULONGLONG calledAt=0,sentAt=0;
    gravity_net::Grab queue[kMostQueued]{};   // throws and let-goes not sent yet
    int queued=0;
    std::uint32_t origin=1,serial=0;
};
Out out;
bool netReady=false;
std::uint32_t peerCount=0;

void Queue(const gravity_net::Grab& g) noexcept {
    if(out.queued<kMostQueued)out.queue[out.queued++]=g;
    else out.queue[kMostQueued-1]=g;   // full (no transport for a while): the last one stands for the rest
}

void End(gravity_net::GrabKind kind,const float* vel) noexcept {
    if(!out.holding)return;
    gravity_net::Grab g=out.hold;
    g.kind=kind;
    if(vel)std::memcpy(g.vel,vel,12);
    else g.vel[0]=g.vel[1]=g.vel[2]=0.0f;
    out.holding=false;out.t=gravity::Target{};
    if(netReady)Queue(g);
}

// ---- the peers' grabs ----
struct In {
    char puid[65];
    gravity::Target t;
    bool found=false;
    ULONGLONG lookedAt=0;
};
In in[kMaxPeers+1]{};
gravity_net::GrabInbox inbox;
int logs=0;

bool Zero(const unsigned char* id) noexcept { for(int i=0;i<32;++i)if(id[i])return false;return true; }

// The peer's grab's target in this world: by its ID (a character, alive or dead, a vehicle), else along the grab's ray near
// where the grabber met it. Its grabber must be the sending machine's own player (a peer cannot speak for another).
bool Look(std::uint32_t peer,const gravity_net::Grab& g,ULONGLONG now) noexcept {
    In& r=in[peer];
    r.lookedAt=now;r.found=false;r.t=gravity::Target{};
    MarkIdentities ids{};
    if(!ResolveGrabIdentities(g.grabber,Zero(g.target) ? nullptr : g.target,&ids) || !ids.player)return false;
    if(!SenderIsPlayer(ids.puid,r.puid)) {
        if(logs++<32)Log("GRAVITY NET peer %u's grab names a player that is not its own: ignored",peer);
        return false;
    }
    gravity::Target t;
    // Found: no longer followed here (a throw of this machine's it may still be in): its impacts and contacts are the
    // grabber's machine's to deal now, never this one's too.
    if(ids.target && gravity::TargetOf(ids.target.obj,0xFFFFFFu,&t)){r.t=t;r.found=true;gravity::Untrack(t);return true;}
    float hit[3];
    if(gravity::Pick(g.rayA,g.rayB,ids.player.obj,&t,hit)) {
        float at[3];
        if(!gravity::Position(t,at))std::memcpy(at,hit,12);
        const bool same=ids.target ? t.obj.obj==ids.target.obj : (Dist(hit,g.at)<=kSameAt || Dist(at,g.at)<=kSameAt);
        if(same){r.t=t;r.found=true;gravity::Untrack(t);return true;}
    }
    return false;
}

void Carry(std::uint32_t peer,ULONGLONG now) noexcept {
    In& r=in[peer];
    if(!inbox.Holding(peer,now))return;
    const gravity_net::RemoteGrab& g=inbox.Peer(peer);
    if(!r.found && now-r.lookedAt>=kRetryMs)Look(peer,g.last,now);
    if(!r.found)return;
    if(!gravity::Refresh(r.t)){r.found=false;inbox.End(peer);return;}
    const float lead=static_cast<float>(now-g.heardAt)*0.001f;
    const float k=lead<kMostLead ? lead : kMostLead;
    const float goal[3]={g.last.goal[0]+g.last.vel[0]*k,g.last.goal[1]+g.last.vel[1]*k,g.last.goal[2]+g.last.vel[2]*k};
    gravity::HoldAt(r.t,goal,g.last.vel);
}
}  // namespace

namespace gravity::net {
void Hold(const Target& t,const unsigned char* by,const float* hit,const float* a,const float* b,const float* goal,
          const float* goalVel) noexcept {
    if(!InSession() || !t || !by || !goal)return;
    const ULONGLONG now=GameMs();
    if(out.holding && !Same(out.t,t))End(gravity_net::GrabKind::letGo,nullptr);   // another thing: the last one let go
    if(!out.holding) {
        gravity_net::Grab g{};
        g.kind=gravity_net::GrabKind::hold;
        if(!ReadNativeObjectId(by,g.grabber))return;   // no network identity: nobody else can find whose it is
        if(t.obj.obj && !ReadNativeObjectId(static_cast<const unsigned char*>(t.obj.obj),g.target))std::memset(g.target,0,32);
        float at[3];
        if(hit)std::memcpy(g.at,hit,12);
        else if(gravity::Position(t,at))std::memcpy(g.at,at,12);
        if(a)std::memcpy(g.rayA,a,12);
        if(b)std::memcpy(g.rayB,b,12);
        g.origin=out.origin;g.grab=++out.serial;
        out.hold=g;out.t=t;out.holding=true;out.sentAt=0;
    }
    std::memcpy(out.hold.goal,goal,12);
    if(goalVel)std::memcpy(out.hold.vel,goalVel,12);
    else out.hold.vel[0]=out.hold.vel[1]=out.hold.vel[2]=0.0f;
    out.calledAt=now;
}

void Thrown(const Target& t,const unsigned char* by,const float* vel) noexcept {
    if(!InSession() || !vel)return;
    if(!out.holding || !Same(out.t,t)) {   // thrown without a hold told: its grab told now, then ended
        float at[3];
        if(!gravity::Position(t,at))return;
        const float zero[3]{};
        Hold(t,by,at,nullptr,nullptr,at,zero);
        if(!out.holding)return;
    }
    End(gravity_net::GrabKind::thrown,vel);
}

void LetGo(const Target& t,const unsigned char*) noexcept {
    if(out.holding && Same(out.t,t))End(gravity_net::GrabKind::letGo,nullptr);
}

void Frame() noexcept {
    if(!InSession())return;
    const ULONGLONG now=GameMs();
    if(out.holding && now-out.calledAt>kHoldStaleMs)End(gravity_net::GrabKind::letGo,nullptr);
    if(!netReady || !gravity::Ready())return;
    for(std::uint32_t p=1;p<=peerCount;++p) {
        __try { Carry(p,now); } __except(EXCEPTION_EXECUTE_HANDLER){in[p].found=false;inbox.End(p);}
    }
}

void Reset() noexcept {
    out.holding=false;out.t=gravity::Target{};out.queued=0;
    for(auto& r:in){r.t=gravity::Target{};r.found=false;}
    inbox.Reset();
}
}  // namespace gravity::net

void UpdateGravityNetwork(const CommandNetworkContext& context,std::uint64_t now) noexcept {
    if(!context.ready || !context.send || !context.peerCount || context.peerCount>kMaxPeers) {
        if(netReady)ResetGravityNetwork();
        return;
    }
    if(!netReady || context.peerCount!=peerCount){ResetGravityNetwork();netReady=true;peerCount=context.peerCount;}
    unsigned char bytes[gravity_net::kGrabWire];
    for(int i=0;i<out.queued;++i)
        if(gravity_net::Encode(out.queue[i],bytes,sizeof(bytes)))plugin_net::Broadcast(support_net::kExtGravity,bytes,sizeof(bytes));
    out.queued=0;
    const ULONGLONG ms=GameMs();
    if(!out.holding || (out.sentAt && ms-out.sentAt<gravity_net::kHoldGapMs))return;
    out.sentAt=ms;
    if(!gravity_net::Encode(out.hold,bytes,sizeof(bytes)))return;
    plugin_net::Broadcast(support_net::kExtGravity,bytes,sizeof(bytes));
    (void)now;
}
// A peer newly has the gravity channel (a new member, or its hello heard only now): the hold goes out in full at once.
void GravityNetworkJoined(std::uint32_t) noexcept { if(out.holding)out.sentAt=0; }

plugin_net::Channel GravityNetChannel() noexcept {
    plugin_net::Channel c;
    c.name="gravity";c.magic=gravity_net::kGrabMagic;c.version=gravity_net::kVersion;c.featureBit=support_net::kExtGravity;
    c.minSize=c.maxSize=gravity_net::kGrabWire;
    c.receive=&ReceiveGravityNetwork;c.update=&UpdateGravityNetwork;c.reset=&ResetGravityNetwork;c.joined=&GravityNetworkJoined;
    return c;
}

bool ReceiveGravityNetwork(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    if(!gravity_net::OwnedGrab(bytes,size))return false;
    gravity_net::Grab g;
    if(!netReady || !peer || peer>peerCount || !puid || !std::memchr(puid,0,65) || !gravity_net::Decode(bytes,size,g))return true;
    const ULONGLONG now=GameMs();
    In& r=in[peer];
    if(std::strcmp(r.puid,puid)){r=In{};strcpy_s(r.puid,puid);}
    __try {
        switch(inbox.Receive(peer,g,now)) {
        case gravity_net::GrabEvent::started:
            if(!gravity::Ready())break;
            Look(peer,g,now);
            if(logs++<64)Log("GRAVITY NET peer %u grabs %s (%.0f,%.0f,%.0f)%s",peer,Zero(g.target) ? "a thing at" : "an object at",
                             g.at[0],g.at[1],g.at[2],r.found ? "" : ": not found here yet");
            break;
        case gravity_net::GrabEvent::thrown: {
            if(!gravity::Ready())break;
            if(!r.found || !gravity::Refresh(r.t))Look(peer,g,now);
            const bool thrown=r.found && gravity::Refresh(r.t) && gravity::SetVelocity(r.t,g.vel);
            if(logs++<64)Log("GRAVITY NET peer %u throws at %.0f m/s: %s",peer,std::sqrt(g.vel[0]*g.vel[0]+g.vel[1]*g.vel[1]+g.vel[2]*g.vel[2]),
                             thrown ? "thrown here" : "its target not found here");
            r.t=gravity::Target{};r.found=false;
            break;
        }
        case gravity_net::GrabEvent::letGo:r.t=gravity::Target{};r.found=false;break;
        case gravity_net::GrabEvent::held:case gravity_net::GrabEvent::none:break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){r.t=gravity::Target{};r.found=false;}
    return true;
}

void ResetGravityNetwork() noexcept {
    for(auto& r:in)r=In{};
    inbox.Reset();
    out.queued=0;
    out.origin=plugin_net::NewOrigin();out.serial=0;
    if(out.holding)out.sentAt=0;   // the hold goes out again in full to the new roster
    netReady=false;peerCount=0;
}
}  // namespace crew
