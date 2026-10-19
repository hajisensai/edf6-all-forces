#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>

namespace crew {
struct CommandNetworkContext;
// Game-thread snapshot of an already registered submarine. The native hull's
// own NetworkObject carries its transform, hull HP and seat weapons; these
// plugin-owned deck systems and turret bones need an independent snapshot.
struct SubSupportState {
    unsigned char netId[32]{};
    float wear[4]{};
    std::uint32_t repairRemainingMs[4]{},reloadRemainingMs[4]{};
    std::uint32_t missileRemainingMs=0,droneRemainingMs=0;
    std::uint8_t downMask=0,flags=0,droneCount=0,turretMask=0;
    float turretAim[2][3]{};
};
namespace sub_support {
constexpr unsigned kSystems=4,kMaxHulls=3,kTurrets=2;
constexpr std::uint8_t kReady=1,kSurfacing=2,kSea=4,kFlags=7;
constexpr std::uint32_t kRepairMs=90000,kReloadMs=12000,kMissileMs=2500,kDroneMs=10000;
constexpr float kMaxWear=1.0e12f;
inline bool ValidId(const unsigned char* id) noexcept {
    if(!id)return false;
    for(unsigned i=20;i<24;++i)if(id[i])return false;
    for(unsigned i=0;i<32;++i)if(id[i])return true;
    return false;
}
inline bool ValidState(const SubSupportState& s) noexcept {
    if(!ValidId(s.netId) || (s.downMask&~15u) || (s.flags&~kFlags) || s.droneCount>4 ||
       (s.turretMask&~3u) || s.missileRemainingMs>kMissileMs || s.droneRemainingMs>kDroneMs)return false;
    for(unsigned i=0;i<kSystems;++i)
        if(!std::isfinite(s.wear[i]) || s.wear[i]<0 || s.wear[i]>kMaxWear ||
           s.repairRemainingMs[i]>kRepairMs || s.reloadRemainingMs[i]>kReloadMs ||
           (!(s.downMask&(1u<<i)) && s.repairRemainingMs[i]))return false;
    for(unsigned i=0;i<kTurrets;++i) {
        float q=0;
        for(float v:s.turretAim[i]) {
            if(!std::isfinite(v) || std::fabs(v)>1.001f || (!(s.turretMask&(1u<<i)) && v!=0))return false;
            q+=v*v;
        }
        if((s.turretMask&(1u<<i)) && (q<0.9f || q>1.1f))return false;
    }
    return true;
}
inline bool SameState(const SubSupportState& a,const SubSupportState& b) noexcept {
    return !std::memcmp(a.netId,b.netId,32) && !std::memcmp(a.wear,b.wear,sizeof(a.wear)) &&
        !std::memcmp(a.repairRemainingMs,b.repairRemainingMs,sizeof(a.repairRemainingMs)) &&
        !std::memcmp(a.reloadRemainingMs,b.reloadRemainingMs,sizeof(a.reloadRemainingMs)) &&
        a.missileRemainingMs==b.missileRemainingMs && a.droneRemainingMs==b.droneRemainingMs &&
        a.downMask==b.downMask && a.flags==b.flags && a.droneCount==b.droneCount && a.turretMask==b.turretMask &&
        !std::memcmp(a.turretAim,b.turretAim,sizeof(a.turretAim));
}
// No pointers, native structs/padding, sender claims or world creation metadata
// are serialized. Successful transport sends must be reliable and ordered; the
// support extension invalidates CommandNetworkContext on a permanent failure.
constexpr std::uint32_t kMagic=0x42534641u; // AFSB
constexpr std::uint8_t kVersion=1;
constexpr std::size_t kHeaderBytes=32,kRowBytes=124,kMaxWireBytes=kHeaderBytes+kMaxHulls*kRowBytes;
static_assert(kMaxWireBytes<1024,"subsystem snapshots fit one support-extension payload");
struct Row { std::uint64_t revision=0;SubSupportState state{}; };
struct Snapshot {
    std::uint64_t epoch=0,sequence=0;
    unsigned count=0;
    Row rows[kMaxHulls]{};
};
bool Owned(const void*,std::size_t) noexcept;
std::uint32_t Checksum(const unsigned char*,std::size_t) noexcept;
std::size_t Encode(const Snapshot&,void*,std::size_t) noexcept;
bool Decode(const void*,std::size_t,Snapshot&) noexcept;
struct Hooks {
    void* user=nullptr;
    unsigned (*enumerate)(void*,SubSupportState*,unsigned) noexcept=nullptr;
    bool (*read)(void*,const unsigned char*,SubSupportState*) noexcept=nullptr;
    bool (*apply)(void*,const SubSupportState&) noexcept=nullptr;
};
// Instance-based protocol so the actual production codec and state machine can
// run as two independent endpoints in a pure fixture, without a game DLL.
// A context loss/rebinding retires its shared epoch. The support session must
// negotiate a new epoch before resuming; resetting one endpoint must never make
// old packets current again or restart counters under an existing wire epoch.
// Native registration must not reuse a complete NativeNetId32 for a different
// hull within one shared epoch; the support transaction's IDs supply that lifetime.
class Protocol {
public:
    explicit Protocol(Hooks hooks={}) noexcept:hooks_(hooks) {}
    void Reset() noexcept;
    void Update(const CommandNetworkContext&,std::uint64_t now) noexcept;
    bool Receive(std::uint32_t peer,const char* authenticatedPuid,const void*,std::size_t,std::uint64_t now) noexcept;
    bool Ready() const noexcept { return ready_; }
    std::uint64_t Epoch() const noexcept { return epoch_; }
private:
    struct Applied { bool used=false;std::uint64_t seen=0;Row row{}; };
    Hooks hooks_{};
    bool ready_=false,host_=false,retry_=false,sent_=false;
    std::uint64_t epoch_=0,retiredEpoch_=0,sequence_=0,revision_=0,lastSend_=0,received_=0;
    std::uint32_t hostPeer_=0,peerCount_=0;
    char localPuid_[65]{},hostPuid_[65]{};
    bool (*send_)(std::uint32_t,const void*,std::size_t) noexcept=nullptr;
    Snapshot current_{};
    Applied applied_[kMaxHulls]{};
    bool SetContext(const CommandNetworkContext&) noexcept;
    void Publish(std::uint64_t now) noexcept;
};
} // namespace sub_support

// Native callbacks: the host enumerates only live, registered, activated hulls
// whose native authority runs here. Read never creates/adopts an object. Apply
// requires a live activated registered REMOTE hull, matches all 32 ID bytes,
// validates local part HP and commits atomically; metadata cannot activate one.
// All remaining times are derived from / applied to local GameMs, never compared
// with the transport wall clock. Apply only updates presentation/skill state;
// it must not run AI, repair, reload, fire or launch drones on a guest.
unsigned EnumerateSubSupportStates(SubSupportState* out,unsigned capacity) noexcept;
bool ReadSubSupportState(const unsigned char netId[32],SubSupportState* out) noexcept;
bool ApplySubSupportState(const SubSupportState&) noexcept;
void UpdateSubSupportNetwork(const CommandNetworkContext&,std::uint64_t now) noexcept;
bool ReceiveSubSupportNetwork(std::uint32_t peer,const char* authenticatedPuid,const void*,std::size_t,std::uint64_t now) noexcept;
void ResetSubSupportNetwork() noexcept;
} // namespace crew
