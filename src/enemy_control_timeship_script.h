// AS call-site ownership gates; original native functions stay callable by replay.
#pragma once
namespace crew::enemy_native {
bool InstallTimeShipScriptGates() noexcept;
bool RestoreTimeShipScriptGates() noexcept;
}
