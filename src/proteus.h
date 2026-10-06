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
bool ProteusReady() noexcept;                     // InstallProteus found the rework's code (the ini decides each frame)
// `target` is where the rework redirected the damage call 0x54A586 to (its thunk to DamageHook, which calls the stock
// 0x547C30 for every object): subcarrier.cpp's check of that call takes it as intact, whichever installs first.
bool ProteusDamageThunk(const void* target) noexcept;
bool IsProteus(const void* vehicle) noexcept;     // a VehicleBigBegaruta (the Proteus, V614_PROTEUS_MK2* / VEHICLE407_BIGBEGARUTA*)
// Every vehicle's input (crew.cpp InputHook), before the seat switch and before the plugin's Enabled test: a Proteus a
// local player rides is reworked, one they left gets its stock numbers back (the plugin off too: it gives everything
// back then). Before the seat switch on purpose: the seats it closes (their class masks) are closed by the time the seat
// switch asks which seats the player may move to, the frame they board too; a move the switch makes is seen next frame.
void ProteusFrame(unsigned char* vehicle) noexcept;
// The shot countdown (weapon +0xE0C, frames) the rework parks the stock missile launcher at while the salvo is its: no
// shot comes before it runs out. A countdown of half this or more is that hold, never a shot's wait (vehsound.cpp reads it).
constexpr float kProteusHoldCountdown=1.0e9f;
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
