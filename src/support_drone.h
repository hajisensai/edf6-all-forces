#pragma once
#include "crew.h"

namespace crew {
enum class SupportDroneKind : unsigned { gun=0,blast=1,doll=2 };
constexpr std::uint64_t kCarrierDroneRequestQueued=~std::uint64_t{0};
// Dynamic support transactions use the host's exact transform. Creation is inert until unanimous activation.
bool SupportDroneReady() noexcept;
bool SupportDroneReady(SupportDroneKind kind) noexcept;
bool CarrierSupportDroneReady(const ObjRef& parent,SupportDroneKind kind,unsigned slot) noexcept;
bool CarrierSupportSlotAvailable(const ObjRef& parent,unsigned slot) noexcept;
int ApplyCarrierSupportRecall(const ObjRef& parent) noexcept;
bool SubmitCarrierSupportRecall(const ObjRef& parent) noexcept;
unsigned char* PrepareSupportDrone(const float* exactMatrix,const float* anchor,const void* parent) noexcept;
bool ActivateSupportDrone(const ObjRef& drone) noexcept;
bool SupportDroneLeft(const ObjRef& drone) noexcept;
// Deletes only a drone prepared by this module, including its inactive rollback path.
bool DeleteSupportDrone(const ObjRef& drone) noexcept;
// Carrier slots retain exact process-monotonic local transaction tokens. Confirm follows successful Activate.
bool ReserveCarrierSupportDrone(const ObjRef& parent,unsigned slot,std::uint64_t token) noexcept;
unsigned char* PrepareCarrierSupportDrone(const float* exactMatrix,const float* anchor,const ObjRef& parent,
    SupportDroneKind kind,unsigned slot,std::uint64_t token,bool ordered=false) noexcept;
bool ConfirmCarrierSupportDrone(const ObjRef& parent,unsigned slot,std::uint64_t token) noexcept;
bool CancelCarrierSupportDrone(const ObjRef& parent,unsigned slot,std::uint64_t token) noexcept;
// Session cancellation can precede SpawnDeployment: drain exact cached tokens without a native source lookup.
void CancelCarrierSupportTransaction(std::uint64_t token) noexcept;
// Dispatcher implementation: host-authenticated plan, canonical parent ID and all-peer spawn/ACK/activation.
std::uint64_t SubmitCarrierSupportDrone(const ObjRef& parent,const float* from,const float* heading,const float* anchor,
    SupportDroneKind kind,unsigned slot) noexcept;
}
