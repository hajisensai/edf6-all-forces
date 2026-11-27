// Vehicle thermal imagery: native opaque meshes -> private heat/depth mask -> world colour composite.
// Only Tick reads game objects. Render callbacks consume the published model identities and native GPU state.
// A soldier's night vision rides the same world begin / end: on foot (every class) the same toggle switches it, and the
// whole world view (the scope lens's own view too) is drawn amplified in green at the world's end, before the post stage
// and the HUD (which stays untinted). Purely this machine's picture: no network message, online the same as offline.
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
    bool rejected=false;   // the key was just pressed on a seat without thermal equipment (shown briefly on the HUD)
    bool pad=false;        // the seat is played with a pad: its toggle is the seat-button chord (ini VehicleThermalButton)
    // Night vision (on foot): the soldier may switch it (alive, on foot, ini InfantryNightVision), it is switched on,
    // and the last world view drew it.
    bool nightEligible=false,nightVision=false,nightShown=false;
};
// Install once during plugin setup. All native call sites are fingerprinted before any patch is written.
bool Install() noexcept;
// Game-thread frame, for each of this machine's players (split screen: two, each their own seat, mode and image). The
// toggle (keyboard: the key; pad: ini VehicleThermalButton's seat-button chord) cycles the requested mode on an equipped
// seat in or out of the sight view (drawn only through the sight); ignored while a menu/map owns input and while paused.
// On foot the same key (and the first local pad's ini InfantryNightVisionButton chord) switches night vision off / on,
// for this machine's first local player (the keyboard's and the plugin's XInput pad's owner). It stays chosen through a
// vehicle ride (not drawn inside one: the seat's own thermal cycle has the key there) until the mission ends.
void Tick(bool enabled=true,unsigned toggleKey=0x4E) noexcept;
// UI-safe requests/readback for one local player (null: the first, PlayerHuman). An unsupported seat rejects thermal
// requests; optical is always accepted. An accepted request outside the sight or without the render hooks stays optical
// with that reason.
bool SetMode(Mode mode,const void* human=nullptr) noexcept;
Mode CycleMode(const void* human=nullptr) noexcept;
Status GetStatus(const void* human=nullptr) noexcept;
// UI-safe night vision request for one local player: on is admitted only while the soldier can use it (on foot).
bool SetNightVision(bool on,const void* human=nullptr) noexcept;
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
