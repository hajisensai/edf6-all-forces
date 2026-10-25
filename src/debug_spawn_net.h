// The test range's spawns online (NetSpawn: an enemy, an empty vehicle, a support soldier, a gun drone) share the existing
// reliable support transaction -- the host plans, every peer prepares (its resources loaded first: Hooks::prepare), creates
// and registers the same object under the host's NetId, and it is activated once all of them have: no second ID
// allocator or transport. The support calls' rows keep their own (support_dispatch.cpp), on the same session.
#pragma once
#include "debug_spawn.h"
#include "support_net.h"
#include <cstring>
#include <cwchar>

namespace crew::debugspawn {
inline constexpr std::uint32_t kNetworkCatalog=1022;
// A PvP member's own unit (pvp_match.h Role::unit): the same transaction, made on the member's faction team (the plan's
// team), the requester the member; once every peer has it, the member's machine takes control of it (enemy control).
inline constexpr std::uint32_t kPvpUnitCatalog=1021;
constexpr bool NetworkCatalog(std::uint32_t c) noexcept { return c==kNetworkCatalog || c==kPvpUnitCatalog; }
// Stable across catalog reorderings. The complete descriptor hash is sent in variant as well: another build with a
// changed path, lift or required files refuses at PREPARE, before any peer creates anything.
constexpr std::uint64_t HashByte(std::uint64_t h,unsigned char b) noexcept { return (h^b)*1099511628211ull; }
inline std::uint64_t Descriptor(const Entry& e) noexcept {
    std::uint64_t h=14695981039346656037ull;
    for(const char* p=e.id;*p;++p)h=HashByte(h,static_cast<unsigned char>(*p));
    if(e.sgo)for(const wchar_t* p=e.sgo;*p;++p) {
        h=HashByte(h,static_cast<unsigned char>(*p));h=HashByte(h,static_cast<unsigned char>(*p>>8));
    }
    h=HashByte(h,static_cast<unsigned char>(e.how));
    // Catalog lift is an integer number of metres in the generated catalog.
    const auto lift=static_cast<unsigned>(e.lift*1000.0f);
    for(unsigned b=0;b<4;++b)h=HashByte(h,static_cast<unsigned char>(lift>>(8*b)));
    if(e.files)for(const wchar_t* p=e.files;*p;p+=std::wcslen(p)+1) {
        for(const wchar_t* q=p;*q;++q) {
            h=HashByte(h,static_cast<unsigned char>(*q));h=HashByte(h,static_cast<unsigned char>(*q>>8));
        }
        h=HashByte(h,0);
    }
    return h ? h : 1;
}
// The rows a peer can make from a network plan, and the kind byte of their resource (what each peer makes: Spawn): an
// enemy 0xE1 (the first kind, an older build's only one), an empty vehicle 0xE2, a support soldier 0xE3, a gun drone
// 0xE4. A build before the kinds reads only 0xE1 rows: it refuses the others at PREPARE (the host says so), never makes
// an enemy of them.
inline bool NetworkKind(How how) noexcept { return how==How::enemy || how==How::vehicle || how==How::soldier || how==How::drone; }
inline std::uint32_t KindByte(How how) noexcept {
    return how==How::vehicle ? 0xE2u : how==How::soldier ? 0xE3u : how==How::drone ? 0xE4u : 0xE1u;
}
inline std::uint32_t Resource(const Entry& e) noexcept {
    return (KindByte(e.how)<<24) | (static_cast<std::uint32_t>(Descriptor(e))&0x00FFFFFFu);
}
inline int NetworkRow(std::uint32_t resource,std::uint64_t descriptor) noexcept {
    int found=-1;
    for(int i=0;i<kEntryCount;++i)if(NetworkKind(kEntries[i].how) && Resource(kEntries[i])==resource && Descriptor(kEntries[i])==descriptor) {
        if(found>=0)return -1; // ambiguous identities must never create different objects on different peers
        found=i;
    }
    return found;
}
inline int NetworkRow(std::uint64_t descriptor) noexcept {
    int found=-1;
    for(int i=0;i<kEntryCount;++i)if(NetworkKind(kEntries[i].how) && Descriptor(kEntries[i])==descriptor) {
        if(found>=0)return -1;
        found=i;
    }
    return found;
}
inline bool BuildNetworkPlan(int row,const float* matrix,support_net::Plan* out,std::uint32_t catalog=kNetworkCatalog) noexcept {
    if(!out || !matrix || row<0 || row>=kEntryCount || !NetworkKind(kEntries[row].how) || !NetworkCatalog(catalog))return false;
    if(catalog==kPvpUnitCatalog && kEntries[row].how!=How::enemy)return false;
    *out={};out->catalogId=catalog;out->count=1;
    auto& unit=out->units[0];unit.resourceId=Resource(kEntries[row]);unit.variant=Descriptor(kEntries[row]);
    std::memcpy(unit.matrix,matrix,sizeof(unit.matrix));
    for(unsigned i=0;i<3;++i)out->target[i]=matrix[12+i];
    return support_net::ValidPlan(*out,false);
}
inline int NetworkPlanRow(const support_net::Plan& plan,bool requireIds=true) noexcept {
    if(!NetworkCatalog(plan.catalogId) || plan.count!=1 || plan.units[0].role || !support_net::ValidPlan(plan,requireIds))return -1;
    for(unsigned i=0;i<3;++i)if(plan.units[0].matrix[12+i]!=plan.target[i])return -1;
    const int row=NetworkRow(plan.units[0].resourceId,plan.units[0].variant);
    return row>=0 && (plan.catalogId!=kPvpUnitCatalog || kEntries[row].how==How::enemy) ? row : -1;
}
} // namespace crew::debugspawn

namespace crew {
// support_net.cpp bridge; all native operations run on its game-thread transaction callbacks. The names are the first
// kind's (an enemy); every network row goes through them. Plan: pending while the host loads the row; Prepare (a peer's
// PREPARE): pending while this machine loads it, its READY sent once loaded.
support_net::PlanResult PlanRangeEnemy(std::uint32_t,const float*,std::uint64_t,support_net::Plan*) noexcept;
support_net::PlanResult PrepareRangeEnemy(const support_net::Plan&) noexcept;
bool ValidateRangeEnemy(const support_net::Plan&) noexcept;
bool SpawnRangeEnemy(std::uint64_t,const support_net::Plan&,bool remote) noexcept;
void DestroyRangeEnemy(std::uint64_t) noexcept;
void RangeEnemyNetworkTick() noexcept;
void ResetRangeEnemyNetwork() noexcept;
bool RangeEnemyRequestNotice(std::uint32_t,support_net::RequestStatus) noexcept;
// The PvP unit's plan on the host for the member `requesterPuid` (authenticated by support_net): their committed pick
// must be the unit role with this row (pvp.h PvpMemberPick). Ownership (requester, team) is added by support_net after.
support_net::PlanResult PlanPvpUnit(const char* requesterPuid,const float* target,std::uint64_t descriptor,support_net::Plan* out) noexcept;
// This member's machine (game thread): ask for its own unit at `at` (pvp.cpp, the committed match). False: not sent.
bool RequestPvpUnit(int row,const float* at,wchar_t* note,std::size_t size) noexcept;
// This member's unit made by the transaction and still alive (its control asked for once activated).
bool PvpUnitLive() noexcept;
}
