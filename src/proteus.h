// proteus.cpp: the Proteus rework (README 普罗透斯, docs/proteus-re.md; ini Proteus*). Included by crew.h.
#pragma once
#include <Windows.h>
#include <cstdint>
#include "proteus_logic.h"

namespace crew {
// At load: the code the rework rests on checked; the weapon mounts' empty-seat callback, the weapon user, the pose and
// the native shield's update hooked; the soldiers' target search chained. False: no Proteus is reworked.
bool InstallProteus() noexcept;
void ResetProteus() noexcept;                     // a new mission (mission.cpp)
bool ProteusReady() noexcept;                     // InstallProteus found the rework's code (the ini decides each frame)
bool IsProteus(const void* vehicle) noexcept;     // a VehicleBigBegaruta (the Proteus, V614_PROTEUS_MK2* / VEHICLE407_BIGBEGARUTA*)
// The stock weapons seat `seat` works without holding them (a borrowed mount, proteus_logic.h Operator): up to `max`
// into `out`, their count. The vehicle HUD lists them with the seat's own (vhud.cpp).
int ProteusBorrowedWeapons(const unsigned char* vehicle,unsigned seat,const unsigned char** out,int max) noexcept;
// Engine seats 2/3 still hold the weapons; empty closed seats are not public seats.
unsigned ProteusVisibleSeats(const unsigned char* vehicle,unsigned count) noexcept;
// Every vehicle's input (crew.cpp InputHook), before the seat switch and before the plugin's Enabled test: a Proteus a
// local player rides is reworked, one they left gets its stock numbers back (the plugin off too: it gives everything
// back then). Before the seat switch on purpose: the seats it closes (their class masks) are closed by the time the seat
// switch asks which seats the player may move to, the frame they board too; a move the switch makes is seen next frame.
void ProteusFrame(unsigned char* vehicle) noexcept;
// The turret camera's look-at fetch (turretcam.cpp LookHook), for a seat camera it does not place itself: a deployed
// Proteus the player rides raises the camera's targets (`look`, `eye`: the look-at and the eye, world). False: none.
bool ProteusViewLift(const unsigned char* seat,float* look,float* eye) noexcept;
// Where the allies are to look first (the shield up walking, the user 2026-10-06: "己方 NPC 和自动炮塔会优先攻击"): enemies
// within `radius` of `centre`, or whose target is `vehicle`, weigh `weight` of their distance in an ally's choice.
// False with no zone this moment. Any thread (EDF6AutoTurret asks through common/edf/aimlink.h PriorityZoneV1).
struct ProteusZone { float centre[3]; float radius,weight; const void* vehicle; };
bool ProteusPriorityZone(ProteusZone* out) noexcept;

// The stock vehicle HUD's Proteus part (hud.cpp StockBlock), as of the last frame. Shares 0..1; `ring` the field's
// edge on the ground (deployed).
constexpr int kProteusRing=40;
struct ProteusReadout {
    const void* vehicle;
    float pos[3],hull[3];          // hull: the way the shield faces (horizontal)
    unsigned seat;                 // the player's seat (0 the driver's)
    bool driver;                   // the player drives it (the keys are theirs)
    proteus::Mode mode;
    float stagger;                 // the stagger's share done
    bool shieldOn,shieldUp,dirShield,overheated,broken;
    bool shieldReady;              // the native shield can be raised this mission (its SGO installed and preloaded)
    float heat,shieldHalfArc;
    float shield,shieldHp;         // the shield's HP share left, its full HP
    bool launcher;                 // the driver works the missile launcher now (deployed, its seat empty)
    bool priority;                 // the allies' priority zone is up
    float fieldRadius;
    int allies;                    // allies the field covers now
    int ringCount;
    float ring[kProteusRing][3];
    bool keys;                     // the driver on the keyboard and mouse (else a pad): which binding the HUD names
    int modeKey,modeButton,shieldKey,shieldButton,launcherKey;
};
bool PlayerProteus(ProteusReadout* out) noexcept;
}  // namespace crew
