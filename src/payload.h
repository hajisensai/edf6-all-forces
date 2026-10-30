// payload.cpp: the weapons of the seat the local player sits in on a stock vehicle, as a readout for the vehicle HUD,
// and the stock helicopters' switchable stores (docs/stock-payload-re.md).
// seatswitch.cpp: the player moving to another seat of the vehicle they are in (docs/stock-payload-re.md §5).
// Included by crew.h; game thread unless said.
#pragma once
#include "stores.h"
#include <cstdint>

namespace crew {
// --- The seat's weapons (payload.cpp) ---
constexpr int kMostPayload=8;
// Which of the seat's controls fires a weapon, as the vehicle class's own input does (docs/stock-payload-re.md §2):
// `primary` the seat's primary trigger (seat +0x2E4 >= 0.8), `secondary` its second fire control (the 506's / 409's
// button bit 0x20, fire byte +0x2021; the Titan's left trigger +0x2E0), `store` one of the stores the secondary fires
// when it is the one picked (any of the plugin's weapons hung on the seat: they ride the seat's secondary when a stock
// weapon is on it, else its primary), `other` a control the plugin has not read for this class (a mech's arms).
// The BMPT's intrinsic missiles (the Ataka, the 9M120F) use the V603's otherwise unused secondary input: their own
// cycle on VehicleSecondarySwitchKey (T) / VehicleSecondarySwitchButton (the d-pad right) (payload.cpp Switch), never a
// choice of the primary's, whose cycle alone is R / LB.
// A coaxial gun (StoreRole::coax, tools/make_stock_stores.py COAX) is `secondary` for the player on a class whose stock
// input reads no second control (the Blacker 505, the Varius 601, the Grape): payload.cpp pulls it on that trigger.
// `key`: one of the vehicle's extra fire keys (PayloadEntry::key: VehicleFire<key>Key; payload.cpp OwnKey).
enum class PayloadFire : std::uint8_t { other, primary, secondary, store, key };
struct PayloadEntry {
    wchar_t name[32];          // the weapon's own name in the game's language (weapon +0x1B0), or its store's
    std::int32_t rounds;       // rounds in it now (weapon +0xBE8)
    std::int32_t capacity;     // a full load (AmmoCount, weapon +0x248)
    float ready;               // 0..1: its reload as the stock weapon gauge reckons it (1: loaded, rounds left)
    float reloadSec;           // seconds of reload left (0: loaded, or none coming: its rounds are spent for good)
    PayloadFire fire;
    bool picked;               // selected loadout (every linked barrel is marked; r.picked remains its leader)
    bool homing;               // it locks on (LockonType 1)
    bool selectable;           // a live choice in its cycle (R / LB the primary's, T / the d-pad right the own
                               // secondary's); independent primary guns remain read-only
    bool coax;                 // a coaxial machine gun (StoreRole::coax): its own secondary trigger, never cycled
    unsigned char key;         // its own fire key (StoreSpec::fireKey: 2 the second trigger, 3..6 VehicleFire<n>Key), 0 none
    bool autoTurret;           // an auto turret (StoreSpec::autoTurret): it fires itself with the auto turrets on
};
// The local player's seat on a stock vehicle (a plugin aircraft has the cockpit readout, PlayerJetHud, instead).
struct PayloadReadout {
    std::uint64_t selectionToken; // opaque current player/vehicle/seat/holder snapshot identity; 0 means no clickable choices
    int seat,seats;            // the seat they sit in, of how many
    int count;                 // entries (the fuel tank, which every seat lists, left out)
    int picked;                // the entry the secondary fires, -1 when the seat has no stores to switch between
    int choices;               // how many the switch goes round (>= 2 when `picked` >= 0)
    int switchButton;          // EDF seat-button mask used by Switch (read-only HUD binding)
    // The own secondary's cycle (the BMPT's missiles: their own switch, VehicleSecondarySwitchKey / -Button): the entry it
    // fires (-1 none), how many it goes round (>= 2 when it switches), its pad buttons (XInput bits).
    int ownPicked,ownChoices,ownSwitchButton;
    int autoTurrets;           // the auto turrets on the seat (VehicleAutoTurretKey switches them all)
    bool autoOn;               // ...on now
    bool keys;                 // the player is on the keyboard and mouse (else a pad): which switch to name
    PayloadEntry entry[kMostPayload];
};
void PayloadFrame(unsigned char* vehicle) noexcept;    // every vehicle's input, after the stock step
// The last readout (a frame old at most), false with the player in no stock vehicle's seat.
bool PlayerPayload(PayloadReadout* out) noexcept;
// UI thread: queue a choice from the exact drawn snapshot. True means queued, not yet applied.
// Does not read game objects or fire. Caller must route only an explicit mouse UI (currently map M).
// PayloadFrame revalidates identity, topology, live holder and availability, including while the map owns keys.
bool RequestPayloadSelection(std::uint64_t selectionToken,int seat,int entry) noexcept;
// Draw-thread safe: copies one locked, wall-fresh snapshot; never reads game objects.
// The game-thread payload/map pump validates and clears it on ownership/topology changes.
bool PlayerSelectablePayload(PayloadReadout* out) noexcept;
struct Store;
// playerjet.cpp game-thread bridge. Returns requested store index or -1; never fires.
// Null-weapon special actions may be shown, but are never clickable weapon selections.
int AircraftPayloadChoice(unsigned char* vehicle,const Store* stores,int count,int picked) noexcept;
void ForgetAircraftPayload(const void* vehicle=nullptr) noexcept;
// MapHumanFrame game thread, after map command input: processes UI only, never a vehicle flight/fire frame.
void PumpPayloadUi(unsigned char* human) noexcept;
void PumpAircraftPayloadUi(unsigned char* vehicle) noexcept; // playerjet.cpp bridge, called only by PumpPayloadUi
// The weapon the secondary fires now on `vehicle` when its seat's stores are switched (helisight.cpp's mark follows
// it), else nullptr: the stock weapon stands.
unsigned char* PayloadPicked(const void* vehicle) noexcept;
// The own secondary's pick on the player's seat of `vehicle` (the missile the BMPT's secondary fires), else nullptr.
unsigned char* PayloadOwnSecondaryPicked(const void* vehicle) noexcept;
// Independent fire-control target: default native primary, most recent trigger edge, or an explicit switch choice.
// Read-only, validates live holders; retains a reloading weapon but safely falls back from a spent/removed one.
unsigned char* PayloadSightPicked(const void* vehicle,unsigned seat) noexcept;
// The same fire-control view, including all known native weapons fired by that control.
// Applies live store redirects and deduplicates final weapons; the first is the single optic owner above.
// Returns entries written (at most capacity); null output/nonpositive capacity returns 0. Does not fire.
int PayloadSightWeapons(const void* vehicle,unsigned seat,unsigned char** out,int capacity) noexcept;
// Every weapon a trigger of the player's seat fires now: the primary's group, then the secondary's (a tank's coaxial gun,
// the Titan's hull gatling), store redirects applied, deduplicated; whichever is the fire-control pick. The HUD marks each
// (the user, 2026-10-12: 「应该两个准星都同时显示才对」). Same contract as PayloadSightWeapons otherwise.
int PayloadTriggerWeapons(const void* vehicle,unsigned seat,unsigned char** out,int capacity) noexcept;
// Resolve a live source holder's trigger group to its actual usable weapon. Optional cross-plugin aiming adapter:
// true + null is an authoritative empty group; false leaves an unsupported/non-authoritative caller native.
// Uses the same fresh from/to mapping as PullHook, including surviving linked barrels and preceding-frame NPC picks.
bool PayloadAimWeapon(const void* vehicle,unsigned seat,const void* source,const unsigned char** out) noexcept;
// Intrinsic alternate ammunition and independent secondary weapons remain managed with optional stores disabled.
bool NativePayloadControl(const void* vehicle,unsigned seat) noexcept;
// Authoritative NPC seat: choose an existing, loaded, reachable weapon for this target.
// Call before native seat aiming/firing; writes no trigger. nullptr means none is usable.
// `fire` reports the native trigger control to use (primary or secondary).
// Repeat each frame; redirects expire and check AiGunner again at the native trigger pull.
unsigned char* NpcPayloadSelect(unsigned char* vehicle,unsigned seat,float distance,bool airborne,PayloadFire* fire=nullptr) noexcept;
// locked: whether a burst from -> to would pass a friend (heli.h FriendInLine); nullptr none checked.
using LineBlocked=bool(*)(const float* from,const float* to,const void* self) noexcept;
// An NPC crew's own weapons, fired alongside the main gun / store NpcPayloadSelect chose (payload.cpp FireSeat, the same
// pull and shot rule as the player's auto turrets): its coaxial gun (StoreRole::coax) laid within ~3 degrees of `target`
// inside 900 m (and its reach), and its weapons on the extra fire keys (StoreSpec::fireKey 3..6) that have a shot at it
// (HasShot: a missile once its lock holds, a gun laid within ~3 degrees of it in its reach); no friend in the line. Their
// trigger latches pulled this frame. True when one fired.
bool NpcOwnWeaponsFire(unsigned char* vehicle,unsigned seat,const void* target,LineBlocked blocked=nullptr) noexcept;
// The auto turrets of gunner seat `seat` of `vehicle` with nobody in it (StoreSpec::seat, seat_turret.cpp trains them on
// the machine that runs the vehicle): each fires itself on a shot at any enemy (HasShot), no friend in the line. True when
// one fired.
bool UnmannedTurretsFire(unsigned char* vehicle,unsigned seat) noexcept;
// Whether `vehicle`'s auto turrets are on here: the local player's switch with them aboard (VehicleAutoTurretKey), else on.
bool VehicleAutoTurretsOn(const unsigned char* vehicle) noexcept;
// Whether the HUD's control row has anything to say for this seat: the own secondary's cycle, an extra fire key, an auto
// turret (hud.cpp PayloadControls).
inline bool PayloadControlsShown(const PayloadReadout& p) noexcept {
    if(p.ownChoices>1 || p.autoTurrets>0)return true;
    for(int i=0;i<p.count && i<kMostPayload;++i)if(p.entry[i].key>2 || p.entry[i].key==kSeatTriggerKey)return true;
    return false;
}
// Intrinsic secondary weapon: plan separately from the primary; the NPC caller must run native seat aim/lock/safety.
unsigned char* NpcSecondarySelect(unsigned char* vehicle,unsigned seat,float distance,bool airborne) noexcept;
// Pull the live intrinsic secondary from its real seat input; local player or authoritative NPC only.
// NPCs pass the exact weapon whose native lock/safety evaluation approved this intent.
bool FireIndependentSecondary(unsigned char* vehicle,unsigned seat,const unsigned char* expectedWeapon=nullptr) noexcept;
// Every vehicle's input, after the stock step and PayloadFrame: the twin barrels' pulls held back to fire them in turn
// (twin_cadence.h; GroundVehicle.alternate: the BMPT). Clears only latches this frame set; any rider, any machine's copy.
void TwinBarrelFrame(unsigned char* vehicle) noexcept;
void ResetPayload() noexcept;
bool InstallPayload() noexcept;                       // at load: the holder pull 0x62C000 taken over (checked)

// --- Seat switching (seatswitch.cpp) ---
constexpr int kMostSeatsShown=8;
enum class SeatHolder : std::uint8_t { empty, you, npc, other };   // other: another player, or a rider not read
struct SeatPrompt {
    int seats,at;              // the vehicle's seats, the one the player sits in
    SeatHolder holder[kMostSeatsShown];
    bool gun[kMostSeatsShown]; // the seat has a weapon of its own (a gunner's seat)
    bool keys;                 // on the keyboard (name the keys), else a pad (name the button)
    bool aircraft;             // a helicopter or one of the plugin's aircraft: seat 0 is the pilot's
    bool locked;               // no move now: online, with SeatSwitchOnline off
    int refused;               // the seat a press asked for and could not have, shown a moment (-1 none, -2 no seat free)
    bool hints;                // the prompt's moment (boarding, a move, the key held, a refusal): the keys / lock shown
};
void SeatSwitchFrame(unsigned char* vehicle) noexcept; // every vehicle's input, after the crew step
// The prompt while it is shown: the whole ride with SeatList, else after boarding a vehicle with more than one seat,
// after a move, the key held.
bool PlayerSeatPrompt(SeatPrompt* out) noexcept;
bool InstallSeatSwitch() noexcept;                     // at load: the stock functions it calls checked
void ResetSeatSwitch() noexcept;
}  // namespace crew
