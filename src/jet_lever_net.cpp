// A registered jet's engine lever online: AFJL (jet_lever_protocol.h: the wire, its pacing and why; jet_lever_net.h).
// Sender: the machine that runs the jet (online_authority.h OnlineRunsHere of a registered one) sends what it flies it at
// (jetsound::LocalLever), or that nothing does (off), under one state key a jet (its slot here). Receiver: a jet it does
// not run, from the machine that does (a player in its seat 0: that player's; else the host's: online_authority.h), is
// heard at that lever for jetsound.cpp's engine and its exhaust (booster.cpp JetFlames) burns with it here.
#include "jet_lever_net.h"
#include "jet_lever_protocol.h"
#include "jetsound_state.h"
#include "objref.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "scene.h"
#include "plugin_body.h"
#include "booster.h"
#include "jet.h"
#include "netprobe.h"
#include "npcai.h"
#include "march_pace.h"
#include "support_call.h"
#include "transport.h"
#include "online_authority.h"
#include "net_object_resolve.h"
#include "command_identity.h"
#include "plugin_net.h"
#include "object_net.h"
#include <cstring>

namespace crew {
namespace {
constexpr int kJets=64;                  // registered plugin jets of a mission at once (the jets' own table is smaller)
constexpr ULONGLONG kGoneMs=2000;        // a slot whose jet was not seen this long is free again
static_assert(kJets<=static_cast<int>(plugin_net::kStateObjects),"a state key a slot");

struct Out {                             // a jet this machine runs
    ObjRef ref;
    jetlever::Pace pace;
    ULONGLONG seen=0;
};
struct In {                              // a jet another machine runs
    ObjRef ref;
    std::uint8_t lever=0,flags=0;
    ULONGLONG heard=0;
};
Out outs[kJets]{};
In ins[kJets]{};
bool netReady=false;
unsigned logs=0;

bool Registered(const unsigned char* v) noexcept { return object_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)); }

float Within(float x,float lo,float hi) noexcept { return x<lo ? lo : x>hi ? hi : x; }

// `v`'s slot (made if `make` and one is free: unused, or its jet gone kGoneMs / not heard of kSilentMs).
Out* OutSlot(const unsigned char* v,ULONGLONG ms,bool make) noexcept {
    Out* free=nullptr;
    for(Out& e:outs) {
        if(e.ref.Is(v))return &e;
        if(!free && (!e.ref || ms-e.seen>kGoneMs))free=&e;
    }
    if(!make || !free)return nullptr;
    *free=Out{};free->ref=ObjRef::Of(v);
    return free;
}
In* InSlot(const unsigned char* v,ULONGLONG ms,bool make) noexcept {
    In* free=nullptr;
    for(In& e:ins) {
        if(e.ref.Is(v))return &e;
        if(!free && (!e.ref || !jetlever::Fresh(e.heard,ms)))free=&e;
    }
    if(!make || !free)return nullptr;
    *free=In{};free->ref=ObjRef::Of(v);
    return free;
}

void Send(const unsigned char* v,ULONGLONG ms) noexcept {
    Out* const o=OutSlot(v,ms,true);
    if(!o)return;
    o->seen=ms;
    jetsound::Lever l{};
    const bool flown=jetsound::LocalLever(v,&l);
    const std::uint8_t q=flown ? jetlever::Quantize(l.lever) : 0;
    const std::uint8_t flags=static_cast<std::uint8_t>(!flown ? jetlever::kOff :
        (l.rotor ? jetlever::kRotor : 0)|(l.parked ? jetlever::kParked : 0));
    if(!jetlever::Due(o->pace,q,flags,ms))return;
    jetlever::Msg m{};
    m.lever=q;m.flags=flags;
    if(!EncodeNetRef(v,m.jet) || !netref::IsNative(m.jet))return;   // no NetId (not yet): again next frame
    unsigned char bytes[jetlever::kWire];
    if(!jetlever::Encode(m,bytes,sizeof(bytes)))return;
    const auto key=plugin_net::StateKey(plugin_net::StateStream::jetLever,static_cast<std::uint32_t>(o-outs));
    plugin_net::BroadcastState(key,bytes,sizeof(bytes));
    jetlever::Sent(o->pace,q,flags,ms);
}

// The machine that runs `v` (online_authority.h): its seat-0 player's, else the host's.
bool FromItsRunner(const unsigned char* v,std::uint32_t peer,const char* puid) noexcept {
    const unsigned char* const seat=SeatCount(v)>0 ? SeatAt(const_cast<unsigned char*>(v),0) : nullptr;   // read only
    if(seat && SeatRider(seat)==Rider::player) {
        unsigned char id[32];
        NetResolved who{};
        return EncodeNetRef(At<const void*>(seat,kSeatRider),id) && netref::IsPlayer(id) &&
               ResolveNetRef(id,NetLife::present,&who) && SenderOwns(who,puid);
    }
    return peer==plugin_net::HostPeer() && peer!=0;
}

bool Receive(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    jetlever::Msg m{};
    if(!jetlever::Decode(bytes,size,m)) {
        if(logs++<16)Log("JETLEVER peer %u: a message that does not read (%zu bytes): dropped",peer,size);
        return true;
    }
    if(!netReady)return true;
    __try {
        NetResolved r{};
        if(!ResolveNetRef(m.jet,NetLife::live,&r) || !r.found)return true;   // not here (yet, or any more)
        const auto* v=static_cast<const unsigned char*>(r.obj.obj);
        const PluginBody body=BodyOf(v);
        if((body!=PluginBody::jet && body!=PluginBody::playerJet) || !Registered(v) || OnlineRunsHere(v))return true;
        if(!FromItsRunner(v,peer,puid)) {
            if(logs++<16)Log("JETLEVER peer %u: a lever for jet %p it does not run: dropped",peer,v);
            return true;
        }
        const ULONGLONG ms=GameMs();
        In* const e=InSlot(v,ms,true);
        if(!e)return true;
        e->lever=m.lever;e->flags=m.flags;e->heard=ms ? ms : 1;
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return true;
}

void Update(const CommandNetworkContext& context,std::uint64_t) noexcept {
    const bool ready=context.ready && context.sendState;
    if(ready==netReady)return;
    netReady=ready;
    for(Out& o:outs)o=Out{};
    for(In& i:ins)i=In{};
}
void Reset() noexcept {
    netReady=false;
    for(Out& o:outs)o=Out{};
    for(In& i:ins)i=In{};
}
// A peer newly here: every lever goes out again on its jet's next frame.
void Joined(std::uint32_t) noexcept { for(Out& o:outs)o.pace.sent=false; }
}  // namespace

bool jetsound::HeardLever(const unsigned char* v,Lever* out) noexcept {
    if(!v || !out || !netReady)return false;
    const In* const e=InSlot(v,GameMs(),false);
    if(!e || !jetlever::Fresh(e->heard,GameMs()) || (e->flags&jetlever::kOff))return false;
    *out=Lever{jetlever::Lever(e->lever),(e->flags&jetlever::kRotor)!=0,(e->flags&jetlever::kParked)!=0};
    return true;
}

void JetLeverFrame(unsigned char* v) noexcept {
    if(!netReady || !v || v[kDead])return;
    const PluginBody body=BodyOf(v);
    if((body!=PluginBody::jet && body!=PluginBody::playerJet) || !Registered(v))return;   // a copy: its own lever here
    const ULONGLONG ms=GameMs();
    if(OnlineRunsHere(v)){Send(v,ms);return;}
    jetsound::Lever l{};
    if(!jetsound::HeardLever(v,&l) || l.parked)return;
    // As its machine burns it: a rotor's with its power (jet_flight.cpp Hover), a wing's and a player's with the lever,
    // the afterburner past its last notch.
    if(l.rotor)JetFlames(v,Within(0.5f+0.5f*l.lever,0.5f,1.0f),false,ms);
    else JetFlames(v,Within(l.lever,0.4f,1.0f),jetlever::Burner(l.lever),ms);
}

plugin_net::Channel JetLeverNetChannel() noexcept {
    plugin_net::Channel c;
    c.name="jet lever";c.magic=jetlever::kMagic;c.version=jetlever::kVersion;
    c.minSize=c.maxSize=jetlever::kWire;c.takesState=&jetlever::TakesState;
    c.receive=&Receive;c.update=&Update;c.reset=&Reset;c.joined=&Joined;
    return c;
}
}  // namespace crew
