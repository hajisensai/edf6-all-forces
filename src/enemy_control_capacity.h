#pragma once
#include "coop_extension_api.h"
namespace crew {
constexpr unsigned kEnemyControlInboxMessages=4096,kEnemyControlPendingBytes=1024*1024;
inline bool ValidEnemyControlCapacity(const EDF6CoopExtensionCapacity& capacity) noexcept {
    return capacity.size==sizeof(capacity) && capacity.version==EDF6COOP_EXTENSION_CAPACITY_VERSION &&
        capacity.maxPayload>=EDF6COOP_EXTENSION_MAX_PAYLOAD && capacity.maxPeers>=32 &&
        capacity.inboxMessages>=kEnemyControlInboxMessages && capacity.pendingBytes>=kEnemyControlPendingBytes &&
        capacity.congestedBacklogBytes>=kEnemyControlPendingBytes &&
        (capacity.flags&(EDF6COOP_EXTENSION_CAPACITY_RELIABLE_ORDERED|EDF6COOP_EXTENSION_CAPACITY_BOUNDED_TRY_SEND))==
            (EDF6COOP_EXTENSION_CAPACITY_RELIABLE_ORDERED|EDF6COOP_EXTENSION_CAPACITY_BOUNDED_TRY_SEND);
}
// Reads a local exported capability, never a player's payload or a guessed DLL version number.
bool EnemyControlLocalCapacityReady() noexcept;
}
