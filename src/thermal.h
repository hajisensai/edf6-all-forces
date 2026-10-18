// Vehicle thermal imagery: native opaque meshes -> private heat/depth mask -> world colour composite.
// Only Tick reads game objects. Render callbacks consume the published model identities and native GPU state.
#pragma once
#include <cstdint>
struct ID3D11DeviceContext;

namespace crew::thermal {
enum class Mode : std::uint8_t { optical, whiteHot, blackHot };
enum class Reason : std::uint8_t {
    optical, unsupportedSeat, notInSight, hooksUnavailable, waitingFrame,
    unsupportedSurface, active
};
struct Status {
    Mode requested=Mode::optical,effective=Mode::optical;
    Reason reason=Reason::optical;
    bool eligible=false,installed=false;
    unsigned models=0;
};
// Install once during plugin setup. All native call sites are fingerprinted before any patch is written.
bool Install() noexcept;
// Game-thread frame. The key is ignored while a menu/map owns input, while paused, and for non-keyboard seats.
void Tick(bool enabled=true,unsigned toggleKey=0x4E) noexcept;
// UI-safe requests/readback. An unsupported seat rejects thermal requests; optical is always accepted.
bool SetMode(Mode mode) noexcept;
Mode CycleMode() noexcept;
Status GetStatus() noexcept;
// Mission shutdown: clear game identities and requests. GPU resources retire on their own render thread.
void Reset() noexcept;
// True only inside a published enemy's opaque model submission. The shared native helper preserves its state.
bool WantsCapture() noexcept;
// Non-target native meshes publish the opaque surface without replaying geometry; other passes are ignored.
void ObserveSurface(ID3D11DeviceContext* context,int pass) noexcept;
// Unified native mesh callback. Target meshes call BEFORE DrawIndexed; other meshes only observe the surface.
// It is inert outside the world pass and never changes the native depth resource.
void CaptureIndexed(ID3D11DeviceContext* context,unsigned count,unsigned first,int base) noexcept;
} // namespace crew::thermal
