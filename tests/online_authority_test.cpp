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
Facts Vehicle(bool host,std::uint16_t net,bool npcSeat0,int stock,bool localPlayer=false) {
    return Facts{true,true,host,net,true,npcSeat0,localPlayer,stock};
}
Facts Object(bool host,std::uint16_t net) { return Facts{true,true,host,net,false,false,false,0}; }
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
    Check(!MaySeatNpc(Facts{true,false,true,ours,true,false,false,1}),"unknown session code: no NPC is seated online");

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
    Check(Authority(Vehicle(true,none,true,1)),"host's copy deals the damage");
    Check(!Authority(Vehicle(false,none,true,1)),"a client's NPC-flown copy deals none");
    Check(Authority(Vehicle(false,none,false,1,true)),"a client's copy its own player rides deals the damage");
    Check(Authority(Object(true,none)) && !Authority(Object(false,none)),"an unregistered object: the host's copy counts");

    // Other registered objects: their own flags.
    Check(Authority(Object(false,ours)) && !Authority(Object(true,theirs)),"a registered object counts where it is run");

    // Unknown session code online: nothing gated runs except each machine's own unregistered copy.
    const Facts unknown{true,false,true,ours,true,false,false,1};
    Check(!Authority(unknown) && !RunsHere(unknown),"unknown session code: a registered vehicle is nobody's here");
    Check(RunsHere(Facts{true,false,true,none,true,true,false,1}),"unknown session code: its own copy still flies");

    std::printf("online_authority: %d checks passed\n",checks);
    return 0;
}
