#pragma once

namespace crew {
// Installs the narrow stock-410 network-aim call hook. Signature mismatch leaves online gunners disabled.
bool InstallNpcGunnerAim() noexcept;
bool NpcGunnerAimReady() noexcept;
}
