// The online authority's rules (src/online_authority.h) on every case they decide: which machine seats an NPC driver,
// runs a vehicle's plugin flight, and deals the plugin's damage. No game: the facts are what the game would answer.
#include "../src/online_authority.h"
#include <cstdio>
#include <cstdlib>

namespace {
using crew::online::Facts;
int checks=0;
void Check(bool condition,const char* description) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}

// A vehicle online: this machine `host` or a client, its flags `net`, seat 0's NPC, the stock operator `stock`.
Facts Vehicle(bool host,std::uint16_t net,bool npcSeat0,int stock,std::uint8_t copyOwner=crew::online::kCopyHost) {
    return Facts{true,true,host,net,true,npcSeat0,copyOwner,stock};
}
Facts Object(bool host,std::uint16_t net) { return Facts{true,true,host,net,false,false,crew::online::kCopyHost,0}; }

// How many of the room's two machines (the host's view `h`, the client's `c`) count one round.
int Counting(const Facts& h,const Facts& c,crew::online::Shooter onHost,crew::online::Shooter onClient,bool firesOnHost,
             bool firesOnClient) {
    return (firesOnHost && crew::online::ShotCounts(h,onHost) ? 1 : 0)+(firesOnClient && crew::online::ShotCounts(c,onClient) ? 1 : 0);
}
}  // namespace

int main() {
    using namespace crew::online;
    constexpr std::uint16_t ours=kNetLocal,theirs=kNetRemote,none=0;

    // Offline: everything here, whatever else the facts say.
    for(int bits=0;bits<64;++bits) {
        Facts f{false,(bits&1)!=0,(bits&2)!=0,static_cast<std::uint16_t>(bits>>2&3),(bits&16)!=0,(bits&32)!=0,false,2};
        Check(Authority(f) && RunsHere(f) && MaySeatNpc(f),"offline: every rule says this machine");
    }
    Check(HostOnly(false,false,false),"offline: host-only work runs");

    // Host-only work.
    Check(HostOnly(true,true,true),"online host: host-only work runs");
    Check(!HostOnly(true,true,false),"online client: host-only work does not");
    Check(!HostOnly(true,false,true),"online, unknown session code: host-only work runs nowhere");

    // AutoCrew (MaySeatNpc): a registered vehicle on the host only; the plugin's own copy wherever it is.
    const std::uint16_t registered[]={ours,theirs,static_cast<std::uint16_t>(ours|theirs)};
    for(const std::uint16_t net:registered) {
        Check(MaySeatNpc(Vehicle(true,net,false,1)),"host seats the NPC driver of a registered vehicle");
        Check(!MaySeatNpc(Vehicle(false,net,false,1)),"a client never seats one, even where the stock operator says local");
        Check(!MaySeatNpc(Vehicle(false,net,false,2)),"a client never seats one in a vehicle the host runs");
    }
    Check(MaySeatNpc(Vehicle(false,none,false,1)),"a client seats the pilot of a copy it made itself (no identity)");
    Check(!MaySeatNpc(Facts{true,false,true,ours,true,false,kCopyHost,1}),"unknown session code: no NPC is seated online");

    // Authority of a registered vehicle: the stock operator (seat 0's rider's machine, its last rider's, the host).
    Check(Authority(Vehicle(false,theirs,false,1)),"client whose player drives the host's vehicle is its authority");
    Check(!Authority(Vehicle(true,ours,false,2)),"host is not the authority of the vehicle a client drives");
    Check(Authority(Vehicle(true,ours,false,1)),"host runs an empty vehicle (stock host fallback)");
    Check(!Authority(Vehicle(false,theirs,false,2)),"client does not run an empty host vehicle");
    // An NPC rider with no identity counts as local on every machine that seated one: the host decides.
    Check(Authority(Vehicle(true,ours,true,1)),"host is the authority of its NPC-driven vehicle");
    Check(!Authority(Vehicle(false,theirs,true,1)),"a client's own NPC in seat 0 does not make it the authority");
    Check(!RunsHere(Vehicle(false,theirs,true,1)),"a client does not fly a vehicle its own NPC sits in: it replays");
    Check(RunsHere(Vehicle(true,ours,true,1)),"the host flies its NPC-driven vehicle");
    Check(!RunsHere(Vehicle(false,theirs,false,2)),"a client replays the host's NPC heli (seat 0 empty there)");

    // The plugin's own copies (never registered): each machine flies its own; the damage counts once.
    Check(RunsHere(Vehicle(false,none,true,1)) && RunsHere(Vehicle(true,none,true,1)),"every machine flies its own copy");
    Check(Authority(Vehicle(true,none,true,1)),"host's copy of an unrecorded object deals the damage");
    Check(!Authority(Vehicle(false,none,true,1)),"a client's copy of an unrecorded object deals none");
    Check(Authority(Vehicle(false,none,false,1,kCopyHere)),"a copy of this machine's player's call deals the damage");
    Check(!Authority(Vehicle(true,none,false,1,kCopyElsewhere)),"the host's copy of a client's call deals none");
    Check(Authority(Object(true,none)) && !Authority(Object(false,none)),"an unregistered object: the host's copy counts");

    // Other registered objects: their own flags.
    Check(Authority(Object(false,ours)) && !Authority(Object(true,theirs)),"a registered object counts where it is run");

    // Unknown session code online: nothing gated runs except each machine's own unregistered copy.
    const Facts unknown{true,false,true,ours,true,false,kCopyHost,1};
    Check(!Authority(unknown) && !RunsHere(unknown),"unknown session code: a registered vehicle is nobody's here");
    Check(RunsHere(Facts{true,false,true,none,true,true,kCopyHost,1}),"unknown session code: its own copy still flies");

    // Every round counts on exactly one machine (the review's two cases and their neighbours).
    using S=Shooter;
    // A plugin aircraft of the HOST's call: the host's copy (owner here) and the client's copy (owner elsewhere). Its NPC
    // crew fires on both machines; a player may ride either machine's copy.
    {
        const Facts h=Vehicle(true,none,true,1,kCopyHere),c=Vehicle(false,none,true,1,kCopyElsewhere);
        Check(Counting(h,c,S::vehicle,S::vehicle,true,true)==1,"host's call, NPC crews on both copies: counted once (host)");
        Check(Counting(h,c,S::vehicle,S::localPlayer,true,true)==1,"host's call, a client player in the client's copy: once (host)");
        Check(Counting(h,c,S::localPlayer,S::vehicle,true,true)==1,"host's call, the host player in it: once (host)");
    }
    // A plugin aircraft of the CLIENT's call (the reviewed double: the client player in it and the host's NPC copy).
    {
        const Facts h=Vehicle(true,none,true,1,kCopyElsewhere),c=Vehicle(false,none,false,1,kCopyHere);
        Check(Counting(h,c,S::vehicle,S::localPlayer,true,true)==1,"client's call, the client player at its gun, the host's NPC copy firing: once");
        Check(Counting(h,c,S::vehicle,S::vehicle,true,true)==1,"client's call, NPC crews on both copies: once (client)");
        Check(ShotCounts(c,S::localPlayer) && !ShotCounts(h,S::vehicle),"client's call: the client's copy is the one that counts");
    }
    // An unrecorded copy (a mission's, the carrier's drones of an unrecorded carrier): the host's copy.
    {
        const Facts h=Vehicle(true,none,true,1),c=Vehicle(false,none,true,1);
        Check(Counting(h,c,S::vehicle,S::vehicle,true,true)==1,"unrecorded copies: once (host)");
        Check(Counting(h,c,S::vehicle,S::localPlayer,true,true)==1,"unrecorded copy a client player rides: once (host)");
    }
    // A REGISTERED vehicle (a delivered one), the host's NPC pilot (seat 0 empty on the client: stock operator 2 there), a
    // client player at a plugin gun (the reviewed nobody): the client's own round counts there, the host has nobody
    // firing for him (his rider there is another machine's).
    {
        const Facts h=Vehicle(true,ours,true,1),c=Vehicle(false,theirs,false,2);
        Check(Counting(h,c,S::vehicle,S::localPlayer,false,true)==1,"host NPC pilot, client player gunner's round: once (client)");
        Check(Counting(h,c,S::vehicle,S::vehicle,true,false)==1,"host NPC pilot, its NPC crew's round: once (host)");
        Check(Counting(h,c,S::vehicle,S::vehicle,true,true)==1,"its NPC crew's round even if the client ran it too: once (host)");
    }
    // A registered vehicle a client player drives (its ram / drill / EMC on both machines' copies).
    {
        const Facts h=Vehicle(true,ours,false,2),c=Vehicle(false,theirs,false,1);
        Check(Counting(h,c,S::vehicle,S::vehicle,true,true)==1,"client driver's ram on both copies: once (client)");
        Check(Counting(h,c,S::localPlayer,S::vehicle,true,false)==1,"the host player at its gun: once (host)");
    }
    // Offline nothing changes.
    Check(ShotCounts(Facts{false,true,false,none,true,false,kCopyElsewhere,2},S::vehicle),"offline: every round counts");

    std::printf("online_authority: %d checks passed\n",checks);
    return 0;
}
