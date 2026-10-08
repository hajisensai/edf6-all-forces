// payload.cpp: the weapons of the seat the local player sits in on a stock vehicle, as a readout for the vehicle HUD,
// and the stock helicopters' switchable stores (docs/stock-payload-re.md).
// seatswitch.cpp: the player moving to another seat of the vehicle they are in (docs/stock-payload-re.md §5).
// Included by crew.h; game thread unless said.
#pragma once
#include <cstdint>

namespace crew {
// --- The seat's weapons (payload.cpp) ---
constexpr int kMostPayload=8;
// Which of the seat's controls fires a weapon, as the vehicle class's own input does (docs/stock-payload-re.md §2):
// `primary` the seat's primary trigger (seat +0x2E4 >= 0.8), `secondary` its second fire control (the 506's / 409's
// button bit 0x20, fire byte +0x2021; the Titan's left trigger +0x2E0), `store` one of the stores the secondary fires
// when it is the one picked (any of the plugin's weapons hung on the seat: they ride the seat's secondary when a stock
// weapon is on it, else its primary), `other` a control the plugin has not read for this class (a mech's arms).
enum class PayloadFire : std::uint8_t { other, primary, secondary, store };
struct PayloadEntry {
    wchar_t name[32];          // the weapon's own name in the game's language (weapon +0x1B0), or its store's
    std::int32_t rounds;       // rounds in it now (weapon +0xBE8)
    std::int32_t capacity;     // a full load (AmmoCount, weapon +0x248)
    float ready;               // 0..1: its reload as the stock weapon gauge reckons it (1: loaded, rounds left)
    float reloadSec;           // seconds of reload left (0: loaded, or none coming: its rounds are spent for good)
    PayloadFire fire;
    bool picked;               // the one the secondary fires now (a seat whose secondary has stores to switch)
    bool homing;               // it locks on (LockonType 1)
    bool selectable;           // a live choice in the R/LB cycle; independent primary guns remain read-only
};
// The local player's seat on a stock vehicle (a plugin aircraft has the cockpit readout, PlayerJetHud, instead).
struct PayloadReadout {
    std::uint64_t selectionToken; // opaque current player/vehicle/seat/holder snapshot identity; 0 means no clickable choices
    int seat,seats;            // the seat they sit in, of how many
    int count;                 // entries (the fuel tank, which every seat lists, left out)
    int picked;                // the entry the secondary fires, -1 when the seat has no stores to switch between
    int choices;               // how many the switch goes round (>= 2 when `picked` >= 0)
    int switchButton;          // EDF seat-button mask used by Switch (read-only HUD binding)
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
// Independent fire-control target: default native primary, most recent trigger edge, or explicit R/LB payload choice.
// Read-only, validates live holders; retains a reloading weapon but safely falls back from a spent/removed one.
unsigned char* PayloadSightPicked(const void* vehicle,unsigned seat) noexcept;
// The same fire-control view, including all known native weapons fired by that control.
// Applies live store redirects and deduplicates final weapons; the first is the single optic owner above.
// Returns entries written (at most capacity); null output/nonpositive capacity returns 0. Does not fire.
int PayloadSightWeapons(const void* vehicle,unsigned seat,unsigned char** out,int capacity) noexcept;
// Authoritative NPC seat: choose an existing, loaded, reachable weapon for this target.
// Call before native seat aiming/firing; writes no trigger. nullptr means none is usable.
// `fire` reports the native trigger control to use (primary or secondary).
// Repeat each frame; redirects expire and check AiGunner again at the native trigger pull.
unsigned char* NpcPayloadSelect(unsigned char* vehicle,unsigned seat,float distance,bool airborne,PayloadFire* fire=nullptr) noexcept;
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
