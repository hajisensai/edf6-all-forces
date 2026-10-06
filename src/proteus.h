// proteus.cpp: the Proteus rework (README 普罗透斯, docs/proteus-re.md; ini Proteus*). Included by crew.h.
#pragma once
#include <Windows.h>
#include <cstdint>
#include "proteus_logic.h"

namespace crew {
// At load: the code the rework rests on checked; the damage call (0x54A586), the Proteus's weapon user (its interface
// slot 11) and the soldiers' target search (SearchAttackTarget slot 1) hooked. False: no Proteus is reworked.
bool InstallProteus() noexcept;
void ResetProteus() noexcept;                     // a new mission (mission.cpp)
bool IsProteus(const void* vehicle) noexcept;     // a VehicleBigBegaruta (the Proteus, V614_PROTEUS_MK2* / VEHICLE407_BIGBEGARUTA*)
// Every vehicle's input (crew.cpp InputHook), after the seat switch: a Proteus a local player rides is reworked, one
// they left gets its stock numbers back. The plugin off too (it gives everything back then).
void ProteusFrame(unsigned char* vehicle) noexcept;
// The turret camera's look-at fetch (turretcam.cpp LookHook), for a seat camera it does not place itself: a deployed
// Proteus the player rides raises the camera's targets (`look`, `eye`: the look-at and the eye, world). False: none.
bool ProteusViewLift(const unsigned char* seat,float* look,float* eye) noexcept;
// Where the allies are to look first (the front shield up, the user 2026-10-06: "己方 NPC 和自动炮塔会优先攻击"): enemies
// within `radius` of `centre`, or whose target is `vehicle`, weigh `weight` of their distance in an ally's choice.
// False with no zone this moment. Any thread (EDF6AutoTurret asks through common/edf/aimlink.h PriorityZoneV1).
struct ProteusZone { float centre[3]; float radius,weight; const void* vehicle; };
bool ProteusPriorityZone(ProteusZone* out) noexcept;

// The stock vehicle HUD's Proteus part (hud.cpp StockBlock), as of the last frame. Shares 0..1; `ring` the field's
// edge on the ground (deployed).
constexpr int kProteusRing=40;
struct ProteusReadout {
    const void* vehicle;
    float pos[3],hull[3];
    unsigned seat;                 // the player's seat (0 the driver's)
    bool driver;                   // the player drives it (the keys are theirs)
    proteus::Mode mode;
    float stagger;                 // the stagger's share done
    bool shieldOn,shieldUp,dirShield,overheated;
    float heat,shieldHalfArc;
    float barrier,barrierHp;       // share left, its full HP
    bool marked;
    float markAt[3],markRange;
    float salvoWait,salvoCooldown;
    int salvoLeft;
    bool salvoArmed;               // shells preloaded this mission (else no salvo, no driver gun)
    bool gun;                      // the driver's gun is there (deployed, ProteusDriverGun, shells preloaded)
    bool priority;                 // the allies' priority zone is up
    float fieldRadius;
    int allies;                    // allies the field covers now
    int ringCount;
    float ring[kProteusRing][3];
    bool keys;                     // the driver on the keyboard and mouse (else a pad): which binding the HUD names
    int modeKey,modeButton,shieldKey,shieldButton,markKey,markButton,salvoKey;
};
bool PlayerProteus(ProteusReadout* out) noexcept;
}  // namespace crew
