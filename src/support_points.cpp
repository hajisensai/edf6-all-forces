// The map's support pt (support_points.h): each team's pool, charged from its team score as the stock pt reload reads it
// (support_points_logic.h: EDF.dll 0x693EAC..0x693F1E), kept by the host and shared with its team's guests
// (support_points_net.h: the SPTS channel). Offline this machine is its own host.
#include "support_points.h"
#include "support_points_logic.h"
#include "support_points_net.h"
#include "crew.h"
#include "memory.h"
#include "plugin_net.h"
#include "support_net.h"
#include <cstring>

namespace crew {
namespace {
// The team manager (*(image+0x20B2978)): its rows (+0x38, 0x38 bytes a row), a row's score (+0x30); the soldier's team
// (+0x314). The stock reload's read (0x693EAC..0x693ED3) checked against these at the first read.
constexpr unsigned kTeamManager=0x20B2978,kReloadRead=0x693EAC;
constexpr std::size_t kTeamRows=0x38,kTeamStride=0x38,kTeamScore=0x30,kTeamField=0x314;
// mov rax,[rsi+120h]; movsxd rcx,dword [rax+314h]; cmp ecx,-1; je; cmp ecx,7; jge; mov rdx,[rip+..]; imul rcx,rcx,38h;
// mov rax,[rdx+38h]; movss xmm6,[rcx+rax+30h]
const unsigned char kReloadReadSig[]={0x48,0x8B,0x86,0x20,0x01,0x00,0x00,0x48,0x63,0x88,0x14,0x03,0x00,0x00,0x83,0xF9,0xFF,
                                      0x74,0x1A,0x83,0xF9,0x07,0x7D,0x15,0x48,0x8B,0x15};
const unsigned char kReloadScaleSig[]={0x48,0x6B,0xC9,0x38,0x48,0x8B,0x42,0x38,0xF3,0x0F,0x10,0x74,0x01,0x30};
// The host sends its state as it changes, no more often than kStateGapMs, and every kStateKeepMs regardless (a message
// lost is mended by the next); a guest sends a change again while it is not confirmed after kChangeAgainMs.
constexpr ULONGLONG kStateGapMs=100,kStateKeepMs=1000,kChangeAgainMs=1500;
constexpr int kGuestsMost=32;

support_points::Pools pools;     // the host's (offline: this machine's)
support_points::Mirror mirror;   // a guest's
// The host's: each guest by its PUID, with the peer index it last sent from (in the room generation then).
struct Guest { char puid[64]; support_points::Peer peer; std::uint32_t index,generation; };
Guest guests[kGuestsMost]{};
support_points::Ledger ledger;
bool guest=false;                // this machine was a guest at the last tick
bool stateDirty=true;
ULONGLONG stateAt=0;
int checked=0;              // 0 not yet, 1 the stock read is as expected, -1 it is not (no pt are earned)
std::int32_t lastTeam=-1;   // the player's team when last seen (a dead player keeps earning for it)
ULONGLONG loggedAt=0;
std::int32_t loggedPoints=-1;
int netLogs=0;

support_points::Settings Settings() noexcept {
    const Config& c=Cfg();
    support_points::Settings s;
    s.start=c.supportPtStart;s.most=c.supportPtMost;s.scale=c.supportPtScale;
    return s;
}
bool Checked() noexcept {
    if(!checked) {
        checked=Matches(kReloadRead,kReloadReadSig,sizeof(kReloadReadSig)) &&
                Matches(kReloadRead+0x1F,kReloadScaleSig,sizeof(kReloadScaleSig)) ? 1 : -1;
        Log("SUPPORT PT the stock pt reload's team score read: %s",checked>0 ? "as expected" :
            "profile mismatch (no pt are earned; the pool keeps its start)");
    }
    return checked>0;
}
// The score of `team` (its row's +0x30), false when the manager or the row is not readable.
bool TeamScore(std::int32_t team,float* out) noexcept {
    if(team<0 || team>=support_points::kTeams)return false;
    const auto manager=At<const unsigned char*>(image,kTeamManager);
    if(!manager || !Readable(manager+kTeamRows,8))return false;
    const auto rows=At<const unsigned char*>(manager,kTeamRows);
    const unsigned char* const row=rows ? rows+static_cast<std::size_t>(team)*kTeamStride : nullptr;
    if(!row || !Readable(row+kTeamScore,4))return false;
    *out=At<float>(row,kTeamScore);
    return true;
}
bool IsGuest() noexcept { return plugin_net::Ready() && !plugin_net::Host(); }
Guest* GuestOf(const char* puid) noexcept {
    if(!puid || !puid[0])return nullptr;
    for(Guest& g:guests)if(g.puid[0] && !std::strncmp(g.puid,puid,sizeof(g.puid)-1))return &g;
    for(Guest& g:guests)if(!g.puid[0]){g=Guest{};strncpy_s(g.puid,puid,_TRUNCATE);return &g;}
    return nullptr;
}
// The guest now at peer index `peer`, as far as its last change tells.
const Guest* GuestAt(std::uint32_t peer) noexcept {
    for(const Guest& g:guests)if(g.puid[0] && g.index==peer && g.generation==plugin_net::RoomGeneration())return &g;
    return nullptr;
}

// ---- the channel (plugin_net.h: SPTS) ----
bool SendState(std::uint32_t peer) noexcept {
    support_points::Msg m;
    m.kind=support_points::Kind::state;m.most=Cfg().supportPtMost;m.scale=Cfg().supportPtScale;
    for(int t=0;t<support_points::kTeams;++t)m.points[t]=pools.team[t].points;
    if(const Guest* g=GuestAt(peer)){m.origin=g->peer.origin;m.serial=g->peer.taken;}
    unsigned char bytes[support_points::kWire];
    return support_points::Encode(m,bytes,sizeof(bytes)) && plugin_net::Send(peer,0,bytes,sizeof(bytes));
}
void SendStates() noexcept {
    for(std::uint32_t p=1;p<=plugin_net::PeerCount();++p)SendState(p);
}
bool SendChange(const support_points::Msg& m) noexcept {
    unsigned char bytes[support_points::kWire];
    return support_points::Encode(m,bytes,sizeof(bytes)) && plugin_net::SendHost(0,bytes,sizeof(bytes));
}
// The team of the player whose machine is `peer`, -1 unknown.
std::int32_t PeerTeam(std::uint32_t peer) noexcept {
    ObjRef who;
    __try {
        if(!SupportAuthenticatedRequester(peer,&who) || !who.obj)return -1;
        const auto human=static_cast<const unsigned char*>(who.obj);
        return Readable(human+kTeamField,4) ? At<std::int32_t>(human,kTeamField) : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}

bool ReceivePointsNetwork(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    support_points::Msg m;
    if(!support_points::Decode(bytes,size,m)) {
        if(netLogs++<16)Log("SUPPORT PT NET peer %u: a message that does not read (%zu bytes): dropped",peer,size);
        return true;
    }
    if(m.kind==support_points::Kind::state) {
        if(!IsGuest() || peer!=plugin_net::HostPeer())return true;   // only the host's word
        mirror.Hear(m);
        return true;
    }
    Guest* const g=plugin_net::Host() && peer ? GuestOf(puid) : nullptr;
    if(!g)return true;
    g->index=peer;g->generation=plugin_net::RoomGeneration();
    // A guest spends and gets back only its own team's pt (a PvP faction never the other's).
    const std::int32_t team=PeerTeam(peer);
    if(team<0)return true;   // its player not resolved yet: not taken (the guest sends it again)
    if(team!=m.team) {
        if(netLogs++<16)Log("SUPPORT PT NET peer %u: a change of team %d's pt from a player of team %d: dropped",peer,m.team,team);
        g->peer.Next(m);stateDirty=true;   // still taken in order: the guest drops it once confirmed
        return true;
    }
    if(g->peer.Next(m)) {
        pools.Apply(m.team,m.pt,Settings());stateDirty=true;
        Log("SUPPORT PT NET peer %u: team %d %+d (%d now)",peer,m.team,m.pt,pools.team[m.team].points);
    }
    else stateDirty=true;   // a change again (its confirmation lost) or out of order: the state says what was taken
    return true;
}
void PointsNetworkJoined(std::uint32_t peer) noexcept {
    if(plugin_net::Host())SendState(peer);
}
void ResetPointsNetwork() noexcept { stateDirty=true; }

std::int32_t OwnTeam() noexcept { return lastTeam>=0 && lastTeam<support_points::kTeams ? lastTeam : -1; }
}  // namespace

plugin_net::Channel SupportPointsNetChannel() noexcept {
    plugin_net::Channel c;
    c.name="support pt";c.magic=support_points::kMagic;c.version=support_points::kVersion;
    c.minSize=c.maxSize=support_points::kWire;
    c.receive=&ReceivePointsNetwork;c.reset=&ResetPointsNetwork;c.joined=&PointsNetworkJoined;
    return c;
}

void SupportPointsReset() noexcept {
    pools.Start(Settings());
    mirror.Start(Settings(),plugin_net::NewOrigin());
    for(Guest& g:guests)g=Guest{};
    ledger.Clear();
    guest=IsGuest();stateDirty=true;stateAt=0;
    lastTeam=-1;loggedPoints=-1;
    Log("SUPPORT PT a new mission: %d pt a team (most %d, prices x%.2f), %s",pools.team[0].points,Cfg().supportPtMost,
        Cfg().supportPtScale,guest ? "kept by the host" : "kept here");
}

void SupportPointsTick() noexcept {
    __try {
        if(const unsigned char* const human=PlayerHuman(); human && Readable(human+kTeamField,4))lastTeam=At<std::int32_t>(human,kTeamField);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    const bool nowGuest=IsGuest();
    if(nowGuest!=guest) {
        // The host moved: a guest that hosts now starts from what it saw; a host that became a guest waits for its host's.
        if(!nowGuest)for(int t=0;t<support_points::kTeams;++t){pools.team[t].points=mirror.Shown(t);pools.team[t].seenSet=false;}
        else {
            mirror.Start(Settings(),plugin_net::NewOrigin());
            for(int t=0;t<support_points::kTeams;++t)mirror.points[t]=pools.team[t].points;
        }
        Log("SUPPORT PT this machine is %s now",nowGuest ? "a guest (the host keeps the pt)" : "the keeper of the pt");
        guest=nowGuest;stateDirty=true;
    }
    const ULONGLONG now=GetTickCount64();
    if(guest) {
        for(int i=0;i<mirror.count;++i)
            if(now-mirror.changes[i].sentAt>=kChangeAgainMs){mirror.changes[i].sentAt=now;SendChange(mirror.ChangeMsg(mirror.changes[i]));}
    } else {
        if(Checked()) {
            __try {
                const support_points::Settings s=Settings();
                for(int t=0;t<support_points::kTeams;++t) {
                    float score=0.0f;
                    if(TeamScore(t,&score) && pools.Take(t,score,s)>0)stateDirty=true;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER){}
        }
        if(plugin_net::Ready() && plugin_net::PeerCount() &&
           ((stateDirty && now-stateAt>=kStateGapMs) || now-stateAt>=kStateKeepMs)){SendStates();stateAt=now;stateDirty=false;}
    }
    if(Cfg().debug && SupportPoints()!=loggedPoints && now-loggedAt>=5000) {
        loggedAt=now;loggedPoints=SupportPoints();
        const std::int32_t t=OwnTeam();
        if(guest)Log("SUPPORT PT %d pt (team %d, the host's %s, %d changes unconfirmed)",loggedPoints,t,mirror.heard ? "heard" : "not heard yet",mirror.count);
        else if(t>=0) {
            const support_points::Pool& p=pools.team[t];
            Log("SUPPORT PT %d pt (team %d score %.0f; this mission earned %lld, spent %lld, back %lld)",p.points,t,p.seen,
                static_cast<long long>(p.earned),static_cast<long long>(p.spent),static_cast<long long>(p.refunded));
        }
    }
}

std::int32_t SupportPoints() noexcept {
    const std::int32_t t=OwnTeam();
    if(t<0)return 0;
    return guest ? mirror.Shown(t) : pools.team[t].points;
}
std::int32_t SupportPointsMost() noexcept { return guest ? mirror.most : Cfg().supportPtMost; }
std::int32_t SupportPointsPrice(std::uint32_t pt) noexcept { return support_points::Price(pt,guest ? mirror.scale : Cfg().supportPtScale); }

bool SupportPointsSpend(std::int32_t price) noexcept {
    if(price<=0)return true;
    const std::int32_t t=OwnTeam();
    if(t<0 || SupportPoints()<price)return false;
    if(guest) {
        support_points::Msg m;
        if(!mirror.Keep(t,-price,GetTickCount64(),&m)){Log("SUPPORT PT %d changes unconfirmed by the host: no call paid",mirror.count);return false;}
        SendChange(m);
    } else {
        if(!pools.team[t].Spend(price))return false;
        stateDirty=true;
    }
    Log("SUPPORT PT team %d spent %d (%d left)",t,price,SupportPoints());
    return true;
}
void SupportPointsRefund(std::int32_t price) noexcept {
    const std::int32_t t=OwnTeam();
    if(price<=0 || t<0)return;
    if(guest) {
        support_points::Msg m;
        if(mirror.Keep(t,price,GetTickCount64(),&m))SendChange(m);
        else Log("SUPPORT PT %d changes unconfirmed by the host: %d pt not given back",mirror.count,price);
    } else {
        pools.team[t].Refund(price,Settings());
        stateDirty=true;
    }
    Log("SUPPORT PT the call came to nothing: team %d %d back (%d now)",t,price,SupportPoints());
}
bool SupportPointsOwe(std::int32_t price,std::uint32_t request) noexcept {
    if(!SupportPointsSpend(price))return false;
    if(price>0 && !ledger.Keep(request,price))Log("SUPPORT PT more than %d calls owed at once: the oldest one's pt stay spent",
                                                  support_points::kOwedMost);
    return true;
}
void SupportPointsSettle(std::uint32_t request,bool back) noexcept {
    const std::int32_t price=ledger.Settle(request);
    if(price>0 && back)SupportPointsRefund(price);
}
}  // namespace crew
