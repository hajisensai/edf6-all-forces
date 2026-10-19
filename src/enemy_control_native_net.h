// Exact supported-image admission for native enemy pose replication. This is a read-only gate, not a pose writer.
#pragma once
#include "enemy_control_native.h"
namespace crew {
// True only for an explicitly audited XYZ+heading profile whose +0x120 NetObj identity, RTTI, slots and code match.
// A non-empty writer alone is not evidence. Unknown, partial and empty pose serializers fail closed.
bool EnemyControlNativeNetworkPoseReady(const enemy_native::NativeLease& lease) noexcept;
}
