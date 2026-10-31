// The gravity operations online (gravity_net.h; the wire gravity_net_protocol.h; the channel plugin_net.h GRVT, sent only to
// the peers that announced support_protocol.h kExtGravity). Game thread only.
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
constexpr float kSteer=4.0f;             // 1/s: a flight's copy is steered onto its grabber's place this fast...
constexpr float kMostSteer=25.0f;        // ...by at most this many m/s on top of the grabber's velocity
constexpr float kRestSpeed=0.5f;         // m/s: a flight this slow (past kFlightRestMs) is at rest: its last state goes out
constexpr ULONGLONG kFlightRestMs=500;
constexpr int kMostQueued=8;

bool Same(const gravity::Target& a,const gravity::Target& b) noexcept {
    return a.kind==b.kind && a.obj.obj==b.obj.obj && a.obj.ctrl==b.obj.ctrl && (a.obj.obj || a.body==b.body);
}
float Dist(const float* a,const float* b) noexcept {
    const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};
    return std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}
float Len(const float* v) noexcept { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
bool Zero(const unsigned char* id) noexcept { for(int i=0;i<32;++i)if(id[i])return false;return true; }

// ---- this machine's player's grab ----
struct Out {
    bool holding=false;
    gravity::Target t;
    gravity_net::Grab hold{};       // its state as it goes out (origin, grab, IDs, ray, goal)
    ULONGLONG calledAt=0,sentAt=0;
    gravity_net::Grab queue[kMostQueued]{};   // throws, let-goes, radials not sent yet
    int queued=0;
    std::uint32_t origin=1,serial=0;
    // A thrown thing with no ID (found by the ray on the peers): its flight's states follow its throw.
    struct Flight { bool on=false; gravity::Target t; gravity_net::Grab g{}; ULONGLONG since=0,sentAt=0,slowSince=0; } flight;
};
Out out;
bool netReady=false,netHost=false;
std::uint32_t peerCount=0,hostPeer=0;
char localPuid[65]{};

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
    // Thrown, with no ID to find it by: every copy is steered by its flight from now on (FlightTick).
    if(kind==gravity_net::GrabKind::thrown && Zero(g.target))
        out.flight=Out::Flight{true,out.t,g,GameMs(),0,0};
    out.holding=false;out.t=gravity::Target{};
    if(netReady)Queue(g);
}

// ---- the peers' grabs ----
struct In {
    char puid[65];
    gravity::Target t;
    bool found=false;
    ULONGLONG lookedAt=0;
    gravity::Target flight;   // the thing it threw last (no ID: steered by its flight states)
    ULONGLONG flightAt=0;
};
In in[kMaxPeers+1]{};
gravity_net::GrabInbox inbox;
int logs=0;

// Whether peer `p`'s grab wins over this machine's on the same thing: a host's over its guests', else the lower PUID's
// (every machine ranks the two the same way).
bool Outranks(std::uint32_t p) noexcept { return gravity_net::OtherOutranks(netHost,p==hostPeer,localPuid,in[p].puid); }
// A peer holds `t` now (as found here).
std::uint32_t HolderOf(const gravity::Target& t,ULONGLONG now) noexcept {
    for(std::uint32_t p=1;netReady && p<=peerCount;++p)if(in[p].found && Same(in[p].t,t) && inbox.Holding(p,now))return p;
    return 0;
}

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
    if(r.flight && now-r.flightAt>gravity_net::kFlightSilentMs)r.flight=gravity::Target{};   // its flight over: this machine's own
    if(!inbox.Holding(peer,now))return;
    const gravity_net::RemoteGrab& g=inbox.Peer(peer);
    if(!r.found && now-r.lookedAt>=kRetryMs)Look(peer,g.last,now);
    if(!r.found)return;
    const gravity::Liveness live=gravity::Update(r.t);
    if(live==gravity::Liveness::gone){r.found=false;inbox.End(peer);return;}
    if(live==gravity::Liveness::settling)return;   // its corpse not on yet: kept
    const float lead=static_cast<float>(now-g.heardAt)*0.001f;
    const float k=lead<kMostLead ? lead : kMostLead;
    const float goal[3]={g.last.goal[0]+g.last.vel[0]*k,g.last.goal[1]+g.last.vel[1]*k,g.last.goal[2]+g.last.vel[2]*k};
    gravity::HoldAt(r.t,goal,g.last.vel);
}

// This machine's thrown thing with no ID: its state to the peers while it flies (gravity_net_protocol.h flight).
void FlightTick(ULONGLONG now) noexcept {
    Out::Flight& f=out.flight;
    if(!f.on)return;
    float at[3],vel[3];
    if(now-f.since>gravity_net::kFlightMostMs || gravity::Update(f.t)!=gravity::Liveness::live || !gravity::Position(f.t,at) ||
       !gravity::Velocity(f.t,vel)){f=Out::Flight{};return;}
    if(Len(vel)<kRestSpeed){if(!f.slowSince)f.slowSince=now;}
    else f.slowSince=0;
    const bool rest=f.slowSince && now-f.slowSince>=kFlightRestMs;
    if(!rest && f.sentAt && now-f.sentAt<gravity_net::kFlightGapMs)return;
    f.sentAt=now;
    gravity_net::Grab g=f.g;
    g.kind=gravity_net::GrabKind::flight;
    std::memcpy(g.at,at,12);std::memcpy(g.vel,vel,12);
    if(netReady)Queue(g);
    if(rest)f=Out::Flight{};   // its last state out: at rest on every copy
}

// A peer's flight state: this machine's copy steered onto the grabber's (its place and velocity). The game has no pose of
// these to replicate and the plugin no reversed pose write (docs/gravity-re.md §11, 推测): a velocity that closes the gap
// (kSteer, at most kMostSteer more than the grabber's) is what every copy gets, never a jump.
void Steer(In& r,const gravity_net::Grab& g,ULONGLONG now) noexcept {
    if(!r.flight)return;
    r.flightAt=now;
    if(gravity::Update(r.flight)!=gravity::Liveness::live){r.flight=gravity::Target{};return;}
    float at[3];
    if(!gravity::Position(r.flight,at))return;
    float fix[3]={(g.at[0]-at[0])*kSteer,(g.at[1]-at[1])*kSteer,(g.at[2]-at[2])*kSteer};
    const float fl=Len(fix);
    if(fl>kMostSteer)for(float& c:fix)c*=kMostSteer/fl;
    const float v[3]={g.vel[0]+fix[0],g.vel[1]+fix[1],g.vel[2]+fix[2]};
    gravity::SetVelocity(r.flight,v);
}
}  // namespace

namespace gravity::net {
bool Hold(const Target& t,const unsigned char* by,const float* hit,const float* a,const float* b,const float* goal,
          const float* goalVel) noexcept {
    if(!InSession() || !t || !by || !goal)return true;
    const ULONGLONG now=GameMs();
    const bool fresh=!out.holding || !Same(out.t,t);
    // Two players on one thing: one keeps it. A grab of a thing a peer holds already is refused (the first comes first); a
    // peer's grab started while this one was held (neither had heard of the other: they started within a message's time)
    // is settled by rank, the same on both machines (Outranks), and the lower lets go.
    if(const std::uint32_t holder=HolderOf(t,now)) {
        if(fresh || Outranks(holder)) {
            if(!fresh)End(gravity_net::GrabKind::letGo,nullptr);
            return false;
        }
    }
    if(out.holding && fresh)End(gravity_net::GrabKind::letGo,nullptr);   // another thing: the last one let go
    if(!out.holding) {
        gravity_net::Grab g{};
        g.kind=gravity_net::GrabKind::hold;
        if(!PlayerRequesterOf(by,g.grabber))return true;   // no live player: nobody else can find whose it is
        if(t.obj.obj && !ReadNativeObjectId(static_cast<const unsigned char*>(t.obj.obj),g.target))std::memset(g.target,0,32);
        float at[3];
        if(hit)std::memcpy(g.at,hit,12);
        else if(gravity::Position(t,at))std::memcpy(g.at,at,12);
        if(a)std::memcpy(g.rayA,a,12);
        if(b)std::memcpy(g.rayB,b,12);
        g.origin=out.origin;g.grab=++out.serial;
        out.hold=g;out.t=t;out.holding=true;out.sentAt=0;
        if(out.flight.on && Same(out.flight.t,t))out.flight=Out::Flight{};   // caught again: its flight is a hold now
    }
    std::memcpy(out.hold.goal,goal,12);
    if(goalVel)std::memcpy(out.hold.vel,goalVel,12);
    else out.hold.vel[0]=out.hold.vel[1]=out.hold.vel[2]=0.0f;
    out.calledAt=now;
    return true;
}

void Thrown(const Target& t,const unsigned char* by,const float* vel) noexcept {
    if(!InSession() || !vel)return;
    if(!out.holding || !Same(out.t,t)) {   // thrown without a hold told: its grab told now, then ended
        float at[3];
        if(!gravity::Position(t,at))return;
        const float zero[3]{};
        if(!Hold(t,by,at,nullptr,nullptr,at,zero) || !out.holding)return;
    }
    End(gravity_net::GrabKind::thrown,vel);
}

void LetGo(const Target& t,const unsigned char*) noexcept {
    if(out.holding && Same(out.t,t))End(gravity_net::GrabKind::letGo,nullptr);
}

int Radial(const float* centre,float radius,float speed,const unsigned char* by,float scale) noexcept {
    const int moved=gravity::Radial(centre,radius,speed,by,scale,true);
    if(!InSession() || !netReady || !centre || !by || !(radius>0.0f) || radius>gravity_net::kMostRadial ||
       std::fabs(speed)>gravity_net::kMostRadialSpeed)return moved;
    gravity_net::Grab g{};
    g.kind=gravity_net::GrabKind::radial;
    if(!PlayerRequesterOf(by,g.grabber))return moved;
    std::memcpy(g.at,centre,12);
    g.goal[0]=radius;g.goal[1]=speed;
    g.origin=out.origin;g.grab=out.serial;
    Queue(g);
    return moved;
}

void Frame() noexcept {
    if(!InSession())return;
    const ULONGLONG now=GameMs();
    if(out.holding && now-out.calledAt>kHoldStaleMs)End(gravity_net::GrabKind::letGo,nullptr);
    if(!netReady || !gravity::Ready())return;
    __try { FlightTick(now); } __except(EXCEPTION_EXECUTE_HANDLER){out.flight=Out::Flight{};}
    for(std::uint32_t p=1;p<=peerCount;++p) {
        __try { Carry(p,now); } __except(EXCEPTION_EXECUTE_HANDLER){in[p].found=false;in[p].flight=gravity::Target{};inbox.End(p);}
    }
}

void Reset() noexcept {
    out.holding=false;out.t=gravity::Target{};out.queued=0;out.flight=Out::Flight{};
    for(auto& r:in){r.t=gravity::Target{};r.found=false;r.flight=gravity::Target{};}
    inbox.Reset();
}
}  // namespace gravity::net

void UpdateGravityNetwork(const CommandNetworkContext& context,std::uint64_t now) noexcept {
    if(!context.ready || !context.send || !context.peerCount || context.peerCount>kMaxPeers) {
        if(netReady)ResetGravityNetwork();
        return;
    }
    if(!netReady || context.peerCount!=peerCount || context.host!=netHost || context.hostPeer!=hostPeer) {
        ResetGravityNetwork();
        netReady=true;peerCount=context.peerCount;netHost=context.host;hostPeer=context.hostPeer;
    }
    if(context.localPuid && std::memchr(context.localPuid,0,sizeof(localPuid)))strcpy_s(localPuid,context.localPuid);
    else localPuid[0]=0;
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
            if(r.found && Same(r.flight,r.t))r.flight=gravity::Target{};   // caught again: a hold now
            if(logs++<64)Log("GRAVITY NET peer %u grabs %s (%.0f,%.0f,%.0f)%s",peer,Zero(g.target) ? "a thing at" : "an object at",
                             g.at[0],g.at[1],g.at[2],r.found ? "" : ": not found here yet");
            break;
        case gravity_net::GrabEvent::thrown: {
            if(!gravity::Ready())break;
            if(!r.found || gravity::Update(r.t)==gravity::Liveness::gone)Look(peer,g,now);
            // A dying one is thrown once its corpse is on (gravity::Launch), as on the grabber's machine.
            const bool thrown=r.found && gravity::Launch(r.t,g.vel);
            if(logs++<64)Log("GRAVITY NET peer %u throws at %.0f m/s: %s",peer,Len(g.vel),thrown ? "thrown here" : "its target not found here");
            // No ID: its flight's states follow (Steer); with one, every copy has the same velocity and its own physics.
            r.flight=thrown && Zero(g.target) ? r.t : gravity::Target{};r.flightAt=now;
            r.t=gravity::Target{};r.found=false;
            break;
        }
        case gravity_net::GrabEvent::flight:Steer(r,g,now);break;
        case gravity_net::GrabEvent::radial: {
            if(!gravity::Ready())break;
            // Every copy moved the same way, followed by nobody here: the damage is the grabber's machine's.
            MarkIdentities ids{};
            if(!SenderIsPlayer(g.grabber,puid,&ids,true))break;
            gravity::Radial(g.at,g.goal[0],g.goal[1],static_cast<const unsigned char*>(ids.player.obj),1.0f,false);
            break;
        }
        case gravity_net::GrabEvent::letGo:r.t=gravity::Target{};r.found=false;break;
        case gravity_net::GrabEvent::held: {
            // A peer's hold of the thing this machine holds too (both started before either heard the other): the rank
            // decides at this machine's next Hold (HolderOf / Outranks); its target looked for if not found yet.
            if(!r.found && gravity::Ready() && now-r.lookedAt>=kRetryMs)Look(peer,g,now);
            break;
        }
        case gravity_net::GrabEvent::none:break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){r.t=gravity::Target{};r.found=false;r.flight=gravity::Target{};}
    return true;
}

void ResetGravityNetwork() noexcept {
    for(auto& r:in)r=In{};
    inbox.Reset();
    out.queued=0;out.flight=Out::Flight{};
    out.origin=plugin_net::NewOrigin();out.serial=0;
    if(out.holding)out.sentAt=0;   // the hold goes out again in full to the new roster
    netReady=false;netHost=false;peerCount=0;hostPeer=0;
}
}  // namespace crew
