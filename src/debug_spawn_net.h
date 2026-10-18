// Range enemy spawns share the existing reliable support transaction: no second ID allocator or transport.
#pragma once
#include "debug_spawn.h"
#include "support_net.h"
#include <cstring>
#include <cwchar>

namespace crew::debugspawn {
inline constexpr std::uint32_t kNetworkCatalog=1022;
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
inline std::uint32_t Resource(const Entry& e) noexcept {
    return 0xE1000000u | (static_cast<std::uint32_t>(Descriptor(e))&0x00FFFFFFu);
}
inline int NetworkRow(std::uint32_t resource,std::uint64_t descriptor) noexcept {
    int found=-1;
    for(int i=0;i<kEntryCount;++i)if(kEntries[i].how==How::enemy && Resource(kEntries[i])==resource && Descriptor(kEntries[i])==descriptor) {
        if(found>=0)return -1; // ambiguous identities must never create different objects on different peers
        found=i;
    }
    return found;
}
inline int NetworkRow(std::uint64_t descriptor) noexcept {
    int found=-1;
    for(int i=0;i<kEntryCount;++i)if(kEntries[i].how==How::enemy && Descriptor(kEntries[i])==descriptor) {
        if(found>=0)return -1;
        found=i;
    }
    return found;
}
inline bool BuildNetworkPlan(int row,const float* matrix,support_net::Plan* out) noexcept {
    if(!out || !matrix || row<0 || row>=kEntryCount || kEntries[row].how!=How::enemy)return false;
    *out={};out->catalogId=kNetworkCatalog;out->count=1;
    auto& unit=out->units[0];unit.resourceId=Resource(kEntries[row]);unit.variant=Descriptor(kEntries[row]);
    std::memcpy(unit.matrix,matrix,sizeof(unit.matrix));
    for(unsigned i=0;i<3;++i)out->target[i]=matrix[12+i];
    return support_net::ValidPlan(*out,false);
}
inline int NetworkPlanRow(const support_net::Plan& plan,bool requireIds=true) noexcept {
    if(plan.catalogId!=kNetworkCatalog || plan.count!=1 || plan.units[0].role || !support_net::ValidPlan(plan,requireIds))return -1;
    for(unsigned i=0;i<3;++i)if(plan.units[0].matrix[12+i]!=plan.target[i])return -1;
    return NetworkRow(plan.units[0].resourceId,plan.units[0].variant);
}
} // namespace crew::debugspawn

namespace crew {
// support_net.cpp bridge; all native operations run on its game-thread transaction callbacks.
support_net::PlanResult PlanRangeEnemy(std::uint32_t,const float*,std::uint64_t,support_net::Plan*) noexcept;
bool ValidateRangeEnemy(const support_net::Plan&) noexcept;
bool SpawnRangeEnemy(std::uint64_t,const support_net::Plan&,bool remote) noexcept;
void DestroyRangeEnemy(std::uint64_t) noexcept;
void RangeEnemyNetworkTick() noexcept;
void ResetRangeEnemyNetwork() noexcept;
bool RangeEnemyRequestNotice(std::uint32_t,support_net::RequestStatus) noexcept;
}
