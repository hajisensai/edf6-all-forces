// nix.cpp: the Nix's torso twist (README 尼克斯, docs/nix-re.md, ini NixTorsoTwist). Included by crew.h.
#pragma once
namespace crew {
// At load: checks the code it rests on and chains the Nix's update (Vehicle612_nix slot 4). False: left stock.
bool InstallNix() noexcept;
void ResetNix() noexcept;   // a new mission (mission.cpp)

// The local player's Nix as drawn this frame (published from the game thread once a frame; any thread may read it):
// for a vehicle camera that looks where the torso aims (feat/ov-cam) and a twist indicator on the HUD (feat/ov-vhud).
// Yaws are world angles in radians, positive from +z toward +x (atan2(x, z) of a direction); `twist` is the torso's
// yaw off the legs (the seat aim's yaw axis), inside [twistMin, twistMax].
struct NixTorso {
    const void* vehicle;          // tells one Nix from another; never read through
    float at[3];                  // the vehicle's origin
    float legsYaw;                // where the legs face (the vehicle's forward row)
    float torsoYaw;               // where the torso faces: legsYaw + twist
    float twist,twistMin,twistMax;
    float pitch;                  // the torso's elevation, up positive
    float dir[3];                 // the torso's aim, a world unit vector
    bool held;                    // NixTorsoTwist on: the torso keeps its world yaw while the legs turn
};
// False with no fresh one (the player not driving a Nix, the plugin off).
bool PlayerNixTorso(NixTorso* out) noexcept;
}  // namespace crew
