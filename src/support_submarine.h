#pragma once
#include <cstdint>

namespace crew {
struct ObjRef;
// Dedicated support resources; not native aircraft/ground hulls and never interpreted by legacy planners.
inline constexpr std::uint32_t kSupportSubmarineResource=0x30000u;
inline constexpr std::uint32_t kSupportSubDroneResource=0x30001u;
inline constexpr std::uint32_t kSupportCarrierDroneBase=0x31000u;
constexpr bool IsCarrierDroneResource(std::uint32_t id) noexcept {
    const auto bits=id-kSupportCarrierDroneBase;
    return id>=kSupportCarrierDroneBase && bits<128 && !(bits&8u) && (bits&7u)<6 && ((bits>>4)&3u)<3;
}
constexpr unsigned CarrierDroneKind(std::uint32_t id) noexcept {return ((id-kSupportCarrierDroneBase)>>4)&3u;}
constexpr unsigned CarrierDroneSlot(std::uint32_t id) noexcept {return (id-kSupportCarrierDroneBase)&7u;}
constexpr bool CarrierDroneOrdered(std::uint32_t id) noexcept {return ((id-kSupportCarrierDroneBase)&64u)!=0;}
constexpr std::uint32_t CarrierDroneResource(unsigned kind,unsigned slot,bool ordered) noexcept {
    return kSupportCarrierDroneBase+kind*16u+slot+(ordered ? 64u : 0u);
}
// Host-only dynamic bay request. The same authenticated requester identity is retained across deployments.
bool SubmitSubmarineDrone(const ObjRef& parent,const float* from,const float* heading,const float* anchor,
                          const unsigned char requester[32]) noexcept;
}
