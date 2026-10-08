#pragma once
#include "npc_command.h"
#include <cstddef>
#include <cstdint>
namespace crew {
struct ObjRef;
using NpcCommandExecutor=NpcCommandResult(*)(const ObjRef&,const mapcmd::Command&,const ObjRef&,const ObjRef&) noexcept;
void ConfigureNpcCommandNetwork(NpcCommandExecutor executor) noexcept;
constexpr unsigned kCommandNetUnits=16;
enum class CommandNetworkState : std::uint32_t { idle,pending,completed,unavailable,timedOut,interrupted,invalid,rateLimited,stale };
struct CommandNetworkResult {
    std::uint32_t request=0;
    CommandNetworkState state=CommandNetworkState::idle;
    std::uint32_t count=0;
    NpcCommandResult units[kCommandNetUnits]{};
};
// Nonzero means queued, never that the native order executed. Read the correlated
// result below. More than 16 units is rejected, never silently truncated.
std::uint32_t SubmitMapCommand(const ObjRef& requester,const ObjRef* units,unsigned count,
    const mapcmd::Command& command,const ObjRef& focus,wchar_t* note,std::size_t noteSize,
    const std::uint32_t* formationSlots=nullptr,std::uint32_t formationTotal=0) noexcept;
bool ReadMapCommandNetworkResult(CommandNetworkResult* out) noexcept;
bool MapCommandNetworkReady() noexcept;

// Single consumer: support_net owns the only extension poll. All callbacks run
// synchronously on the game thread and identity strings come from that poll.
struct CommandNetworkContext {
    bool ready=false,host=false;
    std::uint64_t epoch=0;
    std::uint32_t hostPeer=0,peerCount=0;
    const char* localPuid=nullptr;
    bool (*send)(std::uint32_t,const void*,std::size_t) noexcept=nullptr;
};
void UpdateCommandNetwork(const CommandNetworkContext&,std::uint64_t now) noexcept;
bool ReceiveCommandNetwork(std::uint32_t senderPeer,const char* authenticatedPuid,
    const void* data,std::size_t size,std::uint64_t now) noexcept;
void ResetCommandNetwork() noexcept;
} // namespace crew
