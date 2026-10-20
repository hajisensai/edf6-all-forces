// The gravity operations' and the float guns' online sharing (gravity_net.cpp, floatgun.cpp; the user, 2026-10-20: 「只在离线时
// 生效，联机时插件改的速度不会同步。不可接受」): pure, no game, no transport.
// Why every machine applies the same moves (docs/gravity-re.md §11, static RE 2026-10-20): every machine runs its own copy of
// every object and its own physics; an owner sends poses for its characters only when they walk (an insect or berserker in
// the air sends none), corpses, containers, map objects and debris are never replicated, and the game's own knockback is
// "the machine that settles the hit broadcasts the velocity, every copy replays it" (GiantAnt message 9). So a grab is a
// state its grabber's machine sends every peer (what is held, the point it is drawn to and how that point moves); each
// machine carries its own copy there, and a throw is the velocity every copy is given. The damage of what is thrown counts
// on the grabber's machine alone (its player is the attacker: jet_bay.cpp ImpactDamage, online_authority.h ShotCounts).
// The float guns are the host's: it sees a colonist die and says which of its guns lies where (`drop`), a machine whose
// player walks up to one asks for it (`pick`), the host gives it to the first who asked (`owner`) and ends it (`gone`);
// every machine draws and fires each gun round its owner, and only the owner's machine's rounds deal damage (the game's
// own rule: a round whose attacker is another machine's hurts nothing here; EDF6Coop docs/net-re/damage.md).
// Wire: fixed sizes, little-endian, over the EDF6Coop extension transport (support_net.cpp routes them by magic). Version 1:
// a newer version is owned and ignored. Objects go by their native network ID (support_soldier.h ReadNativeObjectId), all
// zero for one without it (a debris piece, a body of an unregistered object): found by the grab's ray and position instead.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew::gravity_net {
constexpr std::uint32_t kGrabMagic=0x54565247u;   // "GRVT"
constexpr std::uint32_t kGunMagic=0x4E554746u;    // "FGUN"
constexpr std::uint32_t kVersion=1;
constexpr std::size_t kGrabWire=160,kGunWire=96;
constexpr std::uint32_t kMaxPeers=1024;
constexpr std::uint64_t kHoldGapMs=50;     // a hold's state goes out this often while it lasts
constexpr std::uint64_t kHoldSilentMs=600; // a hold not heard of this long is let go (its grabber left, its plugin stopped)
constexpr std::uint32_t kMostLeftMs=3600000;

enum class GrabKind : std::uint8_t { hold=1, thrown=2, letGo=3 };
struct Grab {
    GrabKind kind=GrabKind::hold;
    std::uint32_t origin=0;          // the sender's run (a new one: its grabs start over)
    std::uint32_t grab=0;            // the grab's serial in that run (a new one: a new thing held)
    unsigned char grabber[32]{};     // the grabbing player's native ID
    unsigned char target[32]{};      // what is held (zero: found by the ray and `at`)
    float at[3]{};                   // where it was when grabbed (the ray's hit), on the grabber's machine
    float rayA[3]{},rayB[3]{};       // the grab's ray
    float goal[3]{};                 // hold: the point it is drawn to
    float vel[3]{};                  // hold: how fast that point moves; thrown: the velocity it is thrown at
};
bool OwnedGrab(const void* bytes,std::size_t size) noexcept;   // the GRVT magic (any version)
bool Encode(const Grab&,void* bytes,std::size_t size) noexcept;
// False: not GRVT, the wrong size, a newer version, an unknown kind, an ID not canonical, a number not finite.
bool Decode(const void* bytes,std::size_t size,Grab& out) noexcept;

enum class GunKind : std::uint8_t { drop=1, pick=2, owner=3, gone=4 };
struct Gun {
    GunKind kind=GunKind::drop;
    std::uint32_t origin=0;          // the host's run (drop / owner / gone), the asker's (pick)
    std::uint16_t serial=0;          // the gun's, given by the host
    std::uint8_t index=0;            // drop: which of the colonist's guns (its weak list's order)
    unsigned char who[32]{};         // drop: the colonist's native ID; pick: the asking player's; owner: the owner's
    float at[3]{};                   // drop: where it fell
    std::uint32_t leftMs=0;          // owner: how long it floats from now (0: to the mission's end)
};
bool OwnedGun(const void* bytes,std::size_t size) noexcept;    // the FGUN magic (any version)
bool Encode(const Gun&,void* bytes,std::size_t size) noexcept;
bool Decode(const void* bytes,std::size_t size,Gun& out) noexcept;

// The receiver of the grabs: each peer's current one.
enum class GrabEvent { none, started, held, thrown, letGo };
struct RemoteGrab {
    bool active=false;
    std::uint64_t heardAt=0;
    Grab last{};
};
class GrabInbox {
public:
    // A peer's message: `started` for a hold of a grab not followed yet (the receiver finds its target), `held` for a later
    // state of the one followed, `thrown` / `letGo` ending it (a throw of a grab never followed is still `thrown`: its target
    // is found then), `none` for one of an ended grab or of an older one of the same run.
    GrabEvent Receive(std::uint32_t peer,const Grab& g,std::uint64_t now) noexcept;
    // Whether the peer's grab is still held at `now` (heard within kHoldSilentMs); a silent one is let go here.
    bool Holding(std::uint32_t peer,std::uint64_t now) noexcept;
    const RemoteGrab& Peer(std::uint32_t peer) const noexcept { return peer_[peer<=kMaxPeers ? peer : 0]; }
    void End(std::uint32_t peer) noexcept { if(peer && peer<=kMaxPeers)peer_[peer].active=false; }
    void Reset() noexcept { for(auto& r:peer_)r=RemoteGrab{}; for(auto& d:done_)d=Done{}; }
private:
    struct Done { std::uint32_t origin=0,grab=0; bool any=false; };   // the last grab each peer ended (late holds of it: none)
    RemoteGrab peer_[kMaxPeers+1]{};
    Done done_[kMaxPeers+1]{};
};
}  // namespace crew::gravity_net
