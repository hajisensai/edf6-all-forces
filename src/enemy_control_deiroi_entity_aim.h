// Only bypass the old AI target gate inside a trusted manual/replay factory scope.
#pragma once
namespace crew::enemy_native {
bool InstallDeiroiEntityFactoryAim() noexcept;
bool DeiroiEntityFactoryAimReady() noexcept;
}
