// NPC crews for friendly vehicles, and the player bumping them out.
//
// Auto-crew: a friendly vehicle with nobody aboard gets the stock NPC driver (VehicleBase slot 50,
// RideAi, the same call the mission scripts make for the NPC tank columns). The game seats a
// DummyVehicleRider in seat 0 and the vehicle AI (slot 72) drives and fires. Helicopters have no
// such AI; heli.cpp flies the ones crewed here.
//
// Bump: the stock code never lets anyone board an occupied seat. The on-foot prompt (0x62DCB0) and
// the board button (vehicle slot 49, 0x633B80 -> 0x6346D0) both skip a seat whose rider's use count
// is non-zero, and RideVehicle (0x5765E0) only flags an occupant to leave and then fails. So:
//   prompt: hide the NPC rider of each NPC-held seat (null its control block for one call) and ask
//           the stock check again;
//   board:  the same check per seat; on a hit the NPC moves to a free gunner seat (seat + clear,
//           neither tells the rider anything) or, with none free, is kicked (it dies), and the stock
//           slot 49 then reserves the now-free seat for the player.
// Team: RideAi puts the vehicle on its NPC rider's team (2), and the stock seat check (0x6346D0) only
// lets a human board a vehicle of their own team or the unowned team 5. A vehicle crewed here keeps
// the team it had before (State::ownTeam): both checks run with it, and the bump gives it back.
// The NPC in a gunner seat stays until the player leaves; then the vehicle is crewed afresh.
// Per vehicle the module keeps a State, keyed by the vehicle's ObjRef (a new object at an old address is a
// new vehicle), dropped at a new mission (ResetCrew) and reused only once its vehicle has not run its
// per-frame input for kStaleMs (gone): a full table takes on no new vehicle rather than drop a live one.
#include "crew.h"
#include "boarding_entrance.h"
#include "body506.h"
#include "game_clock.h"
#include "edf/host.h"
#include "heli.h"
#include "layout.h"
#include "memory.h"
#include "online_authority.h"
#include "playarea.h"
#include "warn.h"
#include <cmath>

namespace crew {
// The game's pause (docs/hud-re.md §10, H): xgs::game::System (*(EDF+0x20B2958), vtable 0x1AE2278) keeps the pause
// reasons in +0xCD8 and the ones that count in +0xCDC; the game is paused while they share a bit. The pause menu
// (HUiPause) sets reason 2 when it is built (0x934A46: 0x1196FC0(system, 2, true)) and clears it when it goes
// (0x934ED3); the System's update skips the scene (every object's update, the vehicles' input with it) while it is
// set (0x1198DFE, 0x11990C7) but still steps the viewport cameras (0x119953B on). Checked at load; off (never paused,
// the old gap rule alone) when any of it differs.
namespace {
constexpr std::size_t kSystem=0x20B2958,kPauseWhy=0xCD8,kPauseCounts=0xCDC;
constexpr unsigned kSystemVtable=0x1AE2278;
bool pauseOk=false;
}  // namespace
bool CheckPauseFlag() noexcept {
    static const unsigned char kOn[]={0x41,0xB0,0x01,0xBA,0x02,0x00,0x00,0x00,0x48,0x8B,0x0D,0x03,0xDF,0x77,0x01,0xE8,0x66,0x25,0x86,0x00};
    static const unsigned char kOff[]={0x45,0x33,0xC0,0x41,0x8D,0x50,0x02,0x48,0x8B,0x0D,0x77,0xDA,0x77,0x01,0xE8,0xDA,0x20,0x86,0x00};
    static const unsigned char kSet[]={0x40,0x56,0x48,0x83,0xEC,0x20,0x44,0x8B,0x89,0xDC,0x0C,0x00,0x00,0x8B,0x81,0xD8,0x0C,0x00,0x00};
    static const unsigned char kSkip[]={0x41,0x8B,0x86,0xD8,0x0C,0x00,0x00,0x41,0x85,0x86,0xDC,0x0C,0x00,0x00,0x0F,0x85,0x67,0x04,0x00,0x00};
    pauseOk=Matches(0x934A46,kOn,sizeof(kOn)) && Matches(0x934ED3,kOff,sizeof(kOff)) && Matches(0x1196FC0,kSet,sizeof(kSet)) &&
            Matches(0x11990C0,kSkip,sizeof(kSkip));
    Log("HOOK pause flag=%d (the plugin's game clock and HUD stop with the pause menu)",pauseOk);
    return pauseOk;
}
bool GamePaused() noexcept {
    if(!pauseOk)return false;
    __try {
        const unsigned char* const system=At<const unsigned char*>(image,kSystem);
        if(!system || !Readable(system,kPauseCounts+4) || At<const void*>(system,0)!=image+kSystemVtable)return false;
        return (At<std::uint32_t>(system,kPauseWhy)&At<std::uint32_t>(system,kPauseCounts))!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// The game clock (game_clock.h): stopped while the game is paused, a gap longer than 250 ms (loading) one 16 ms frame.
// It starts an hour in: 0 is "never" for the timestamps it fills (crashAt, missileAt, launchAt, ...).
namespace { gameclock::Clock clock; }
ULONGLONG GameMs() noexcept { return gameclock::Read(clock,GetTickCount64(),GamePaused()); }
// The frame: every vehicle's input runs once a frame, so the first vehicle of a frame coming round again
// starts the next one. If it is deleted, the next repeat of any vehicle seen this frame does.
namespace { constexpr int kFrameSeen=128; const void* frameSeen[kFrameSeen]{}; int frameSeenCount=0; ULONGLONG frame=1; }
ULONGLONG GameFrame() noexcept { return frame; }
void SeeFrame(const void* vehicle) noexcept {
    for(int i=0;i<frameSeenCount;++i)
        if(frameSeen[i]==vehicle){++frame;frameSeenCount=0;break;}
    if(frameSeenCount<kFrameSeen)frameSeen[frameSeenCount++]=vehicle;
}
namespace {
using FindSeatFn=unsigned char*(__fastcall*)(void*,void*);
// Forwarded with all four register arguments: CarBase's input (slot 55) also reads r8 (its drive block)
// and the 502's pre-update (slot 4) takes `this` alone.
using InputFn=edf::VehicleInputFn;   // the slot 55 signature both plugins chain (common/edf/layout.h)
using PromptFn=void(__fastcall*)(void*,void*);
using CanRideSeatFn=bool(__fastcall*)(void*,void*,void*);
using CanRideFn=bool(__fastcall*)(void*,void*);
using SeatRideFn=unsigned char*(__fastcall*)(void*,void*,int,bool);
using SeatFn=void(__fastcall*)(void*,void*);

// Every vehicle class we crew: vtable RVA, the stock function in its input slot (0: not hooked), name,
// its stock slot 49 (FindSeat), the slot we chain its per-frame input on, and whether only an armed one
// (a weapon holder, veh+0x648) gets an NPC driver.
// The 502 has the 54-slot VehicleBase vtable: no slot 55, its per-frame input copy is slot 4 (0x612D20).
// The Begaruta family (504, the 612 Nix, Begaruta, BigBegaruta = Proteus) all share slot 4 0x644350, their per-frame
// update: the player's input (0x641800) and the guns' fire (0x645190) are reached from it alone (0x6443A2 / 0x6443F6).
// Their slot 55 (0x63C1C0; the Proteus's 0x648F70 calls it first) is the AI's think, which slot 6 (0x642970, 0x642CC5)
// registers as a callback on the AI component (veh+0x1FA0), not the per-frame update: with the player driving it is not
// the frame. So the whole family chains slot 4, as the crawler does: a player-driven one runs every step (HUD, seat
// switch, stabilizer, sounds, the Proteus rework) whoever drives it. That function takes (vehicle, step); the shared
// four-register forwarding preserves both and it ignores r8/r9.
// Vehicle_Car (the Grape, also the unarmed 512 Kei truck and 513 trailer cab) has its own slot 49
// (0x65B910, a preferred-seat wrapper round the stock one) and its CarBase input in slot 55 (0x65A390).
struct VehicleClass {
    unsigned vtable; unsigned input; const char* name;
    unsigned findSeat=kFindSeat; std::size_t inputSlot=kSlotInput; bool armedOnly=false;
};
const VehicleClass kClasses[]={
    {0x17D8B50,0x5FD8E0,"402_Rocket"},{0x17D8FA0,0x5FEBE0,"403_Tank"},{0x17D9458,0x5FFC50,"404_Tank"},
    {0x17D98C8,0,"501_FortressRobo"},{kVt502,0x612D20,"502_GroundRobo",kFindSeat,4},{0x17DA508,0x6178B0,"503_Bike"},
    {0x17DA960,0x644350,"504_begaruta",kFindSeat,4},{0x17DADB0,0x61ACD0,"505_Tank"},{kVt506,0x61B8F0,"506_Helicopter"},
    {0x17DB9D8,0x61DDF0,"510_Maser"},{0x17DBDF8,0x61F080,"511_Bike"},{0x17DC250,0x620790,"601_Tank"},
    {0x17DC620,0x621460,"603_Flak"},{0x17DD440,0x644350,"612_nix",kFindSeat,4},{0x17DD720,0,"VehicleBase"},
    {0x17DE0A8,0x644350,"Begaruta",kFindSeat,4},{0x17DEC40,0x644350,"BigBegaruta",kFindSeat,4},{kVt409,0x64C020,"Helicopter409"},
    {kVt410,0x64E080,"Helicopter410"},{kVtHeliBase,0x6543A0,"HelicopterBase"},{0x17DFDC8,0,"BikeBase"},
    {0x17E0A80,0,"CarBase"},{0x17E1828,0,"TankBase"},
    {0x17E01B0,0x65A390,"Car",0x65B910,kSlotInput,true},
};
constexpr int kClassCount=static_cast<int>(sizeof(kClasses)/sizeof(kClasses[0]));
// The next function in each patched input slot: the stock one, or another plugin's hook
// (EDF6AutoTurret hooks 403/404/603) that ran its patch before ours.
InputFn nextInput[kClassCount]{};
// Each class's own slot 49, which our FindSeat hook calls through (null: that class not hooked).
FindSeatFn originalFindSeat_[kClassCount]{};
PromptFn originalPrompt=nullptr;   // null: the prompt hook is not in (the prompt features are off)
volatile LONG inputsHooked=0;      // EnsureInputs has run (once)

// Aim lines (docs/aim-line-re.md): the red line out of a vehicle gun's muzzle. Weapon_VehicleShoot's ctor
// (0x6B3250, also VehicleMaser / RailGun / SwingShoot) makes a WeaponAimLine (vtable kAimLineVtable) of
// custom_parameter[0] segments and keeps it at weapon+kWeaponAimLine; each frame 0x6899F0 builds the line
// from its segment count (+kAimLineSegments, 0: no line). An NPC's seat gets 0; a player in the seat gets
// the count back. The count taken away is kept with its vehicle's State (the lines are its weapons'), so it
// is never handed to another vehicle's line at a reused address and never overwritten while still owed.
constexpr std::size_t kWeaponAimLine=0x1638,kAimLineSegments=0x130;
constexpr unsigned kAimLineVtable=0x17E2418;
constexpr int kLinesPerVehicle=12;
struct HiddenLine { const unsigned char* line; std::int32_t segments; };

struct State {
    ObjRef ref;
    ULONGLONG emptySince,playerAt,bumpedAt,crewedAt,loggedAt,seen;
    std::int32_t ownTeam;   // the vehicle's team before we crewed it (valid when crewedAt != 0)
    HiddenLine lines[kLinesPerVehicle];
};
// Every live vehicle runs its input every frame (seen); one not seen for kStaleMs is gone.
constexpr int kMaxStates=128;
constexpr ULONGLONG kStaleMs=2000;
State states[kMaxStates]{};
ULONGLONG fullLoggedAt=0;

// The state of a vehicle we track, without claiming a slot for one we do not.
State* FindState(const void* vehicle) noexcept {
    for(auto& s:states)if(s.ref.Is(vehicle))return &s;
    return nullptr;
}

const unsigned char kSetTeamSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
bool setTeamOk=false;

}  // namespace

void SetObjectTeam(unsigned char* object,std::int32_t team) noexcept {
    if(!setTeamOk || At<std::int32_t>(object,kTeam)==team)return;
    const bool registered=((At<std::uint32_t>(object,kObjectFlags)>>6)&1)!=0;
    reinterpret_cast<void(__fastcall*)(void*,std::int32_t,bool)>(image+kSetTeam)(object,team,registered);
}

namespace {

// The team a player boards it as: the team it had before we crewed it, else its own. One of the plugin's aircraft the
// player may board (playerjet.cpp PlayerJetBoardable) is nobody's vehicle to the seat check: its NPC pilot holds it on
// the friends' team (2), which the stock check lets no player into.
std::int32_t OwnTeam(const unsigned char* vehicle) noexcept {
    if(IsJet(vehicle) && PlayerJetBoardable(vehicle))return kTeamVehicle;
    const State* st=FindState(vehicle);
    return st && st->crewedAt ? st->ownTeam : At<std::int32_t>(vehicle,kTeam);
}

// The vehicle's state, a new one for a vehicle not tracked yet: in a free slot, the slot of a gone object at
// the same address, or one whose vehicle is stale. Never a live vehicle's: with none free, nullptr (the
// vehicle is left to the stock game, logged).
State* StateFor(const void* vehicle,ULONGLONG now) noexcept {
    State* slot=nullptr;
    for(auto& s:states) {
        if(s.ref.Is(vehicle))return &s;
        if(!slot && (!s.ref || s.ref.obj==vehicle || now-s.seen>kStaleMs))slot=&s;
    }
    if(!slot) {
        if(now-fullLoggedAt>10000){fullLoggedAt=now;Log("CREW table full (%d live vehicles): v=%p left to the stock game",kMaxStates,vehicle);}
        return nullptr;
    }
    *slot=State{};slot->ref=ObjRef::Of(vehicle);slot->seen=now;
    return slot;
}

int ClassOf(const void* object) noexcept {
    if(!Readable(object,8))return -1;
    const auto vtable=At<const unsigned char*>(object,0);
    for(int i=0;i<kClassCount;++i)if(vtable==image+kClasses[i].vtable)return i;
    return -1;
}

const void* RiderObject(const unsigned char* seat) noexcept { return At<const void*>(seat,kSeatRider); }

HiddenLine* HiddenOf(State& st,const unsigned char* line) noexcept {
    for(auto& h:st.lines)if(h.line==line)return &h;
    return nullptr;
}

// The aim line of a seat's weapon `i`, or nullptr.
unsigned char* AimLineOf(unsigned char* const* holders,std::uint64_t i) noexcept {
    if(!Readable(holders[i],kHolderWeapon+8))return nullptr;
    const auto weapon=At<unsigned char*>(holders[i],kHolderWeapon);
    if(!Readable(weapon,kWeaponAimLine+8))return nullptr;
    const auto line=At<unsigned char*>(weapon,kWeaponAimLine);
    if(!Readable(line,kAimLineSegments+4,true) || At<const unsigned char*>(line,0)!=image+kAimLineVtable)return nullptr;
    return line;
}

// The line's points: a std::vector<hkVector4> at line+kAimLinePoints (begin +8, capacity +0x10, size +0x18),
// built by 0x6899F0 only while the segment count is non-zero. A line hidden from the vehicle's first frame
// never got one, and the render (0x687F20, on the render thread) reads `segments` points from begin without
// looking at the size: putting the count back onto a null begin crashed it (EDF.dll+0x6880C1, null read, the
// frame the player was bumped in). So the buffer is made before the count: grown the way 0x6899F0 grows it
// (game operator new; 0x1000 bytes and up 32-aligned with the raw pointer at [-8], what the release 0x6895C0
// frees), every point at the vehicle -- a zero-length line until the next build fills it.
constexpr std::size_t kAimLinePoints=0x70,kGameNew=0x12D85B0,kPointsRelease=0x6895C0;
using GameNewFn=void*(*)(std::size_t);
using PointsReleaseFn=void(*)(void*);

bool EnsurePoints(unsigned char* line,std::int32_t segments,const float* at) noexcept {
    unsigned char* const vec=line+kAimLinePoints;
    const auto count=static_cast<std::uint64_t>(segments);
    if(At<const void*>(vec,8) && At<std::uint64_t>(vec,0x10)>=count)return true;
    const std::size_t bytes=count*16;
    const auto gameNew=reinterpret_cast<GameNewFn>(image+kGameNew);
    float* points=nullptr;
    if(bytes>=0x1000) {
        auto* raw=static_cast<unsigned char*>(gameNew(bytes+0x27));
        if(!raw)return false;
        points=reinterpret_cast<float*>((reinterpret_cast<std::uintptr_t>(raw)+0x27)&~std::uintptr_t{0x1F});
        reinterpret_cast<unsigned char**>(points)[-1]=raw;
    } else {
        points=static_cast<float*>(gameNew(bytes));
        if(!points)return false;
    }
    for(std::uint64_t i=0;i<count;++i) {
        points[i*4]=at[0];points[i*4+1]=at[1];points[i*4+2]=at[2];points[i*4+3]=1.0f;
    }
    if(At<const void*>(vec,8))reinterpret_cast<PointsReleaseFn>(image+kPointsRelease)(vec);
    Put<std::uint64_t>(vec,0x18,count);
    Put<std::uint64_t>(vec,0x10,count);
    Put<float*>(vec,8,points);
    return true;
}

// What a seat's lines get this frame (Want).
enum class LineWant { keep, hide, show };

// Hides the line (its count kept in the vehicle's state; with no room left the line stays as it is), or gives back
// what was taken (its point buffer first: EnsurePoints).
void SetLine(State& st,unsigned char* line,LineWant want,const float* at) noexcept {
    const auto segments=At<std::int32_t>(line,kAimLineSegments);
    HiddenLine* h=HiddenOf(st,line);
    if(want==LineWant::hide && segments>0) {
        if(!h)h=HiddenOf(st,nullptr);
        if(!h)return;
        *h=HiddenLine{line,segments};
        Put<std::int32_t>(line,kAimLineSegments,0);
    } else if(want==LineWant::show && h) {
        if(!EnsurePoints(line,h->segments,at))return;
        Put<std::int32_t>(line,kAimLineSegments,h->segments);
        *h=HiddenLine{};
    }
}

// A seat's lines: hidden while an NPC holds the seat, or while it is empty in a vehicle an NPC drives (the 410's door
// guns, aimed by the plugin with nobody in them), and while the player holds it in an aircraft whose HUD draws a gun
// sight of its own: one the player-jet flight flies (playerjet.cpp PlayerJetOwnSight: the user, 2026-10-05, "delete
// the stock gun's two red lines"), a stock helicopter (helisight.cpp PlayerHeliOwnSight: "the heli's sight ours
// too") or any other stock vehicle (vhud.cpp PlayerStockOwnSight, 2026-10-06: its impact points in their place). The stock line (a count taken away given back) for the player with our sight off (the ini turned off: the
// next frame). Any other seat is left as it was (as before this list grew): an empty one of a vehicle no NPC drives
// (one the player got out of keeps its line hidden, nobody there to see it, until they or an NPC sit in it) and a
// remote player's.
LineWant Want(Rider rider,bool npcDriven,bool ownSight) noexcept {
    switch(rider) {
        case Rider::dummy: return LineWant::hide;
        case Rider::none: return npcDriven ? LineWant::hide : LineWant::keep;
        case Rider::player: return ownSight ? LineWant::hide : LineWant::show;
        default: return LineWant::keep;
    }
}

// Every seat's guns' lines as Want has them. Only for a vehicle with a state (Crew made it this frame).
void AimLines(unsigned char* vehicle) noexcept {
    State* const st=FindState(vehicle);
    if(!st)return;
    const unsigned count=SeatCount(vehicle);
    const bool npcDriven=count>0 && SeatRider(SeatAt(vehicle,0))==Rider::dummy;
    const bool ownSight=PlayerJetOwnSight(vehicle) || PlayerHeliOwnSight(vehicle) || PlayerStockOwnSight(vehicle);
    for(unsigned i=0;i<count && i<16;++i) {
        auto seat=SeatAt(vehicle,i);
        const LineWant want=Want(SeatRider(seat),npcDriven,ownSight);
        const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(want==LineWant::keep || n>8 || !Readable(holders,n*8))continue;
        for(std::uint64_t w=0;w<n;++w)
            if(auto line=AimLineOf(holders,w))SetLine(*st,line,want,reinterpret_cast<const float*>(vehicle+kPosition));
    }
}

// WithTeamField: `f` run with the vehicle on `team` for the stock seat checks, put back right after (a fault too): the field alone (+0x314), not
// registered. The board prompt (0x5735E7) and the board button (0x56D77F) call their visitors, and the visitors
// FindSeat (slot 49), while the team manager walks a team's set (0x5E11D0): a SetTeam there takes the vehicle out
// of the set the walk is in and frees the node the walk stands on. It read freed memory (a dynamic_cast on it threw:
// dumps EDF6.exe.76548, .66844) or a broken tree (an 'object' at 0x68, the walk never ending: the game hung,
// 2026-10-05 10:30). A field changed and put back within the visit leaves every set as it is (they are keyed by
// the object's address); the vehicle's real team changes only as the stock code changes it (the player getting in).
// The one raw write of +0x314 (tools/selftest.py team_changes_go_through_set_team names this function).
template<class F> auto WithTeamField(unsigned char* v,std::int32_t team,F f) noexcept -> decltype(f()) {
    const std::int32_t was=At<std::int32_t>(v,kTeam);
    Put<std::int32_t>(v,kTeam,team);
    decltype(f()) r{};
    __try { r=f(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    Put<std::int32_t>(v,kTeam,was);
    return r;
}

// `check` with the NPC riders of the vehicle's NPC-held seats hidden (put back after, a fault too).
template<class F> bool WithRidersHidden(unsigned char* vehicle,F check) noexcept {
    void* saved[16]{};
    const unsigned count=SeatCount(vehicle);
    for(unsigned i=0;i<count && i<16;++i) {
        auto seat=SeatAt(vehicle,i);
        if(SeatRider(seat)!=Rider::dummy)continue;
        saved[i]=At<void*>(seat,kSeatRiderCtrl);Put<void*>(seat,kSeatRiderCtrl,nullptr);
    }
    bool ok=false;
    __try { ok=check(); } __except(EXCEPTION_EXECUTE_HANDLER) { ok=false; }
    for(unsigned i=0;i<count && i<16;++i)if(saved[i])Put<void*>(SeatAt(vehicle,i),kSeatRiderCtrl,saved[i]);
    return ok;
}

// Run `check` with the NPC riders of the vehicle's NPC-held seats hidden and the vehicle on its own
// team (see OwnTeam, WithTeamField); restores both before returning.
template<class F> bool WithDummiesHidden(unsigned char* vehicle,F check) noexcept {
    return WithTeamField(vehicle,OwnTeam(vehicle),[&]{ return WithRidersHidden(vehicle,check); });
}


// A free seat other than `skip` for the NPC to move into, or -1.
int FreeGunnerSeat(unsigned char* vehicle,unsigned skip) noexcept {
    const unsigned count=SeatCount(vehicle);
    for(unsigned i=0;i<count;++i)if(i!=skip && SeatRider(SeatAt(vehicle,i))==Rider::none)return static_cast<int>(i);
    return -1;
}

// The NPC now seated in `to` as well leaves `from`; a fault in that clear takes it out of `to` again, so it
// is never left in two seats (or the player's seat still held). False then.
bool LeaveSeat(unsigned char* vehicle,unsigned char* from,unsigned char* to) noexcept {
    __try { reinterpret_cast<SeatFn>(image+kSeatClear)(vehicle,from); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { reinterpret_cast<SeatFn>(image+kSeatClear)(vehicle,to); }
    __except(EXCEPTION_EXECUTE_HANDLER){Log("BUMP v=%p: the move undo faulted too",vehicle);}
    return false;
}

}  // namespace

// The NPC in seat `from` to seat `to` (seat, then clear: LeaveSeat pairs them). False: not moved, the NPC still where
// it was (the seat refused it, or the clear faulted and the seating was undone).
bool MoveRider(unsigned char* vehicle,unsigned from,unsigned to) noexcept {
    auto seat=SeatAt(vehicle,from);
    auto rider=const_cast<void*>(RiderObject(seat));
    auto dest=reinterpret_cast<SeatRideFn>(image+kSeatRide)(vehicle,rider,static_cast<int>(to),false);
    return dest && LeaveSeat(vehicle,seat,dest);
}

namespace {
// Free the NPC-held seat `index` for the player: move the NPC to a free gunner seat, else kick it. Every
// choice is made before the first write; the move is seat, then clear (LeaveSeat pairs them). False when the
// seat could not be freed (the caller then offers the player nothing).
bool Bump(unsigned char* vehicle,unsigned index) noexcept {
    auto seat=SeatAt(vehicle,index);
    auto rider=const_cast<void*>(RiderObject(seat));
    const int gunner=Cfg().bumpToGunner ? FreeGunnerSeat(vehicle,index) : -1;
    bool freed=false;
    if(gunner>=0) {
        if(auto to=reinterpret_cast<SeatRideFn>(image+kSeatRide)(vehicle,rider,gunner,false)) {
            freed=LeaveSeat(vehicle,seat,to);
            Log(freed ? "BUMP v=%p seat=%u -> npc moved to gunner seat %d" : "BUMP v=%p seat=%u -> the move to seat %d faulted: undone",
                vehicle,index,gunner);
            if(!freed)return false;   // as it was: the NPC still in its seat
        }
    }
    if(!freed) {
        reinterpret_cast<SeatFn>(image+kSeatKick)(vehicle,seat);
        Log("BUMP v=%p seat=%u -> npc kicked (no free gunner seat)",vehicle,index);
    }
    if(State* st=FindState(vehicle))st->bumpedAt=GameMs();
    return true;
}

// The gunship's two seats (playerjet_crew.inc, README 炮舰机): the board button takes the seat GunshipBoardSeat names,
// not the stock first free one (the gunner's, while the NPC flies). The NPC in that seat moves to the other one when it
// is free (Bump: the pilot to the gun, or the gunner up to the stick: the gunship keeps its pilot), else it is kicked;
// then the seat is reserved as the stock FindSeat reserves the one it finds (0x633BFE: 0x633FE0(vehicle, human, seat),
// returned). nullptr: not a gunship with both seats, not the player's to board now, or that seat not theirs to take (the
// stock / generic path goes on).
using ReserveSeatFn=void(__fastcall*)(void*,void*,void*);
constexpr unsigned kReserveSeat=0x633FE0;
unsigned char* GunshipSeat(unsigned char* v,void* human) noexcept {
    if(!IsPlayer(static_cast<const unsigned char*>(human)) || !GunshipCrewSeats(v) || !PlayerJetBoardable(v))return nullptr;
    const unsigned want=GunshipBoardSeat();
    auto seat=SeatAt(v,want);
    const Rider rider=SeatRider(seat);
    if(rider!=Rider::none && rider!=Rider::dummy)return nullptr;
    const auto canRide=[&]{ return reinterpret_cast<CanRideSeatFn>(image+kCanRideSeat)(v,human,seat); };
    if(!WithDummiesHidden(v,canRide))return nullptr;   // out of reach, or its class may not sit there
    if(rider==Rider::dummy && !Bump(v,want))return nullptr;
    return WithTeamField(v,OwnTeam(v),[&]()->unsigned char* {   // the stock seat check's team (see WithTeamField)
        if(!canRide())return nullptr;
        reinterpret_cast<ReserveSeatFn>(image+kReserveSeat)(v,human,seat);
        Log("BOARD v=%p gunship: the player takes the %s seat",v,want==kGunnerSeat ? "gunner" : "pilot");
        return seat;
    });
}

unsigned char* __fastcall FindSeatHook(void* vehicle,void* human) {
    EnsureInputs();   // the board button in the first mission (with no prompt hook nor mission start hooked)
    const int cls=ClassOf(vehicle);
    const FindSeatFn originalFindSeat=cls>=0 && originalFindSeat_[cls] ? originalFindSeat_[cls] : reinterpret_cast<FindSeatFn>(image+kFindSeat);
    // The boarding gun's press (boarding.cpp): the vehicle its round hit, no other the visitor comes to first.
    if(const void* only=BoardingOnly(); only && only!=vehicle)return nullptr;
    // The sidecar motorcycle's sidecar (sidecar.cpp): the player standing nearer it than the saddle takes it, no seat.
    if(Cfg().enabled) {
        bool sidecar=false;
        __try { sidecar=SidecarBoard(static_cast<unsigned char*>(vehicle),static_cast<unsigned char*>(human)); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if(sidecar)return nullptr;
    }
    if(Cfg().enabled && Cfg().bump && !BumpSuppressed()) {
        unsigned char* crewSeat=nullptr;
        __try { crewSeat=GunshipSeat(static_cast<unsigned char*>(vehicle),human); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if(crewSeat)return crewSeat;
    }
    auto seat=originalFindSeat(vehicle,human);
    if(seat || !Cfg().enabled || !Cfg().bump || BumpSuppressed())return seat;
    __try {
        auto v=static_cast<unsigned char*>(vehicle);
        // The jets: only one of ours the player may board now (on the ground or hovering low and slow, playerjet.cpp
        // PlayerJetBoardable); its pilot is kicked (it has no other seat).
        if(!IsPlayer(static_cast<const unsigned char*>(human)) || (IsJet(v) && !PlayerJetBoardable(v)) || IsSub(v))return nullptr;
        // An NPC still aboard (one moved to a gunner seat, the player gone again) keeps the vehicle on
        // its team, and the stock check then refuses even a free seat: ask again on the vehicle's own.
        const auto team=At<std::int32_t>(v,kTeam),own=OwnTeam(v);
        if(team!=own) {
            if(auto free=WithTeamField(v,own,[&]{ return originalFindSeat(vehicle,human); }))return free;   // see WithTeamField
        }
        const unsigned count=SeatCount(v);
        for(unsigned i=0;i<count;++i) {
            auto s=SeatAt(v,i);
            if(SeatRider(s)!=Rider::dummy)continue;
            const bool ok=WithDummiesHidden(v,[&]{ return reinterpret_cast<CanRideSeatFn>(image+kCanRideSeat)(v,human,s); });
            if(!ok)continue;
            // the stock slot 49 re-checks the team (see WithTeamField)
            return WithTeamField(v,own,[&]{ return Bump(v,i) ? originalFindSeat(vehicle,human) : nullptr; });
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

// The boarding point as the game reads it (debug; docs/player-jet-re.md §12): once per jet the player may board,
// when the player on foot first comes within kDoorLogRange of it, seat 0's riding point and stock reach (heli.cpp
// SeatPoint, CanRideSeat's own reading) from its position (its collision box's centre) in its own frame, where the
// player stands in that frame, their distance to the point and the stock prompt's answer. pylib/vcobjects.py
// move_door puts the point on the ground beside the box; its y says whether `mdl` carries the grounding's lift there.
constexpr float kDoorLogRange=40.0f;
constexpr int kDoorLogged=64;
const void* doorLogged[kDoorLogged]{};
int doorLoggedNext=0;

void ToFrame(const unsigned char* v,const float* world,float* out) noexcept {
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float d[3]={world[0]-p[0],world[1]-p[1],world[2]-p[2]};
    for(int r=0;r<3;++r)out[r]=d[0]*m[r*4]+d[1]*m[r*4+1]+d[2]*m[r*4+2];
}

void DoorLog(const unsigned char* v,const unsigned char* human,bool prompt) noexcept {
    for(const void* p:doorLogged)if(p==v)return;
    const float* hp=reinterpret_cast<const float*>(human+kPosition);
    float who[3],door[3],at[3],reach=0.0f;
    ToFrame(v,hp,who);
    if(who[0]*who[0]+who[1]*who[1]+who[2]*who[2]>kDoorLogRange*kDoorLogRange)return;
    if(!SeatPoint(v,0,at,&reach))return;
    doorLogged[doorLoggedNext++%kDoorLogged]=v;
    ToFrame(v,at,door);
    const float g[3]={hp[0]-at[0],hp[1]-at[1],hp[2]-at[2]};
    Log("DOOR v=%p seat 0's door at (%.2f,%.2f,%.2f) from its centre (its frame), reach %.2f m; the player at "
        "(%.2f,%.2f,%.2f), %.2f m from the door; the stock prompt %s",v,door[0],door[1],door[2],reach,who[0],who[1],
        who[2],std::sqrt(g[0]*g[0]+g[1]*g[1]+g[2]*g[2]),prompt ? "shows" : "does not show");
}

// The on-foot ride prompt, once per object per frame for every human on foot.
void __fastcall PromptHook(void* functor,void* object) {
    originalPrompt(functor,object);
    if(!Cfg().enabled)return;
    __try {
        auto f=static_cast<unsigned char*>(functor);
        auto human=At<unsigned char*>(f,kFunctorHuman);
        if(!IsPlayer(human))return;
        SeePlayer(reinterpret_cast<const float*>(human+kPosition),At<std::int32_t>(human,kTeam));
        EnsureInputs();    // first mission frame: every plugin has loaded by now
        if(Cfg().debug && IsJet(object) && PlayerJetBoardable(object))
            DoorLog(static_cast<const unsigned char*>(object),human,f[kFunctorResult]!=0);
        if(!Cfg().bump || BumpSuppressed() || f[kFunctorResult] || ClassOf(object)<0 || (IsJet(object) && !PlayerJetBoardable(object)) ||
           IsSub(object))return;
        auto v=static_cast<unsigned char*>(object);
        bool any=false;
        for(unsigned i=0;i<SeatCount(v);++i)any=any || SeatRider(SeatAt(v,i))==Rider::dummy;
        if(any && WithDummiesHidden(v,[&]{ return reinterpret_cast<CanRideFn>(image+kCanRide)(v,human); }))
            f[kFunctorResult]=1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

float Distance2(const unsigned char* vehicle,const float* pos) noexcept {
    const float* p=reinterpret_cast<const float*>(vehicle+kPosition);
    const float d[3]={p[0]-pos[0],p[1]-pos[1],p[2]-pos[2]};
    return d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
}

void Crew(unsigned char* vehicle,int cls) noexcept {
    if(!Readable(vehicle,kSeatCount+8,true) || vehicle[kDead])return;
    const auto now=GameMs();
    const unsigned count=SeatCount(vehicle);
    // anyPlayer: a player of any machine aboard (no NPC driver for it); localPlayer: this machine's (its fix).
    bool anyPlayer=false,localPlayer=false,driver=false;
    int dummies=0;
    for(unsigned i=0;i<count;++i) {
        const Rider r=SeatRider(SeatAt(vehicle,i));
        localPlayer=localPlayer || r==Rider::player;
        anyPlayer=anyPlayer || AnyPlayerIn(SeatAt(vehicle,i));
        driver=driver || (i==0 && r!=Rider::none);
        dummies+=r==Rider::dummy;
    }
    // The player riding: their fix, whether or not this vehicle has a state.
    if(localPlayer)SeePlayer(reinterpret_cast<const float*>(vehicle+kPosition),At<std::int32_t>(vehicle,kTeam));
    State* const sp=StateFor(vehicle,now);
    if(!sp)return;
    State& st=*sp;st.seen=now;
    if(Cfg().debug && now-st.loggedAt>5000) {
        st.loggedAt=now;
        char riders[17]{};
        for(unsigned i=0;i<count && i<16;++i)riders[i]="-dPo"[static_cast<int>(SeatRider(SeatAt(vehicle,i)))];
        const float* p=reinterpret_cast<const float*>(vehicle+kPosition);
        Log("VEH v=%p %s team=%d playerTeam=%d seats=[%s] pos=(%.0f,%.0f,%.0f) dist=%.0f",vehicle,kClasses[cls].name,
            At<std::int32_t>(vehicle,kTeam),player.team,riders,p[0],p[1],p[2],player.at ? std::sqrt(Distance2(vehicle,player.pos)) : -1.0f);
    }
    if(anyPlayer){if(SeatRider(SeatAt(vehicle,0))==Rider::player)st.playerAt=now;st.emptySince=0;return;}
    // A player jet waits for the player, and so does one of the plugin's aircraft the player holds (playerjet.cpp).
    // A sidecar bike with the player in its sidecar is driven for them by the plugin (sidecar.cpp): no NPC driver.
    if(driver || !Cfg().autoCrew || IsPlayerJet(vehicle) || PlayerJetHolds(vehicle) || SidecarHoldsPlayer(vehicle) || IsPrimerVehicle(vehicle)){st.emptySince=0;return;}
    // Online, a registered vehicle gets its NPC driver on the host only (online_authority.h): a DummyVehicleRider has no
    // network identity, so a client that seated one would take the vehicle for its own and send its pose against the
    // host's (docs/online-re.md sections 3.4, 5). A client's copy is driven by what the host's copy replicates.
    if(!OnlineMaySeatNpc(vehicle)){st.emptySince=0;return;}
    if(!st.emptySince)st.emptySince=now;
    // Every first-use parked vehicle belongs to the waiting player, not only helicopters/Proteus.
    // An existing mission NPC is untouched above; a player must have driven seat 0 before auto-crew is eligible.
    if(!st.playerAt)return;
    // Wait out the delay since it emptied, since a player left it and since a bump (the player is
    // walking up to the seat it reserved).
    ULONGLONG since=st.emptySince;
    if(st.playerAt>since)since=st.playerAt;
    if(st.bumpedAt>since)since=st.bumpedAt;
    if(now-since<Cfg().crewDelayMs)return;
    // Its own team, not the one an NPC left aboard (in a gunner seat) holds it on.
    const auto team=OwnTeam(vehicle);
    if(!player.at || now-player.at>10000 || (team!=player.team && team!=kTeamVehicle))return;
    if(Cfg().crewRange>0.0f && Distance2(vehicle,player.pos)>Cfg().crewRange*Cfg().crewRange)return;
    // An unarmed truck of an armed vehicle's class: nothing for a driver to do.
    if(kClasses[cls].armedOnly && At<std::uint64_t>(vehicle,kHolderCount)==0)return;
    // The NPC that moved to a gunner seat when the player boarded goes with the driver seat:
    // the vehicle gets a fresh driver from the stock RideAi rather than a hand-moved one.
    for(unsigned i=0;i<count && dummies;++i)
        if(SeatRider(SeatAt(vehicle,i))==Rider::dummy)reinterpret_cast<SeatFn>(image+kSeatKick)(vehicle,SeatAt(vehicle,i));
    if(!SeatNpcRider(vehicle,false))return;
    st.crewedAt=now;st.emptySince=0;st.ownTeam=team;
    if(IsHelicopter(vehicle))HeliCrewed(vehicle);   // false (its table full): logged there, the heli sits
    Log("CREW v=%p %s seats=%u driver=%d",vehicle,kClasses[cls].name,count,SeatRider(SeatAt(vehicle,0))==Rider::dummy);
}

// A vehicle's input step that took kSlowMs or more (the stock one, the plugin's) is logged (Debug=1, once a
// second at most): the hitch at a mission's start with the submarine carrier out, to tell whose it is.
constexpr double kSlowMs=8.0;
void SlowLog(int cls,const void* v,LONGLONG stock,LONGLONG plugin) noexcept {
    static ULONGLONG at=0;
    LARGE_INTEGER f;QueryPerformanceFrequency(&f);
    const double s=static_cast<double>(stock)*1000.0/static_cast<double>(f.QuadPart),p=static_cast<double>(plugin)*1000.0/static_cast<double>(f.QuadPart);
    if(!Cfg().debug || (s<kSlowMs && p<kSlowMs))return;
    const ULONGLONG now=GetTickCount64();
    if(now-at<1000)return;
    at=now;
    Log("SLOW v=%p %s: stock input %.1f ms, plugin %.1f ms",v,kClasses[cls].name,s,p);
}

// The per-frame steps, each under its own guard: a fault in one (logged per step at most every kFaultLogMs,
// with how many so far) skips that step for that vehicle this frame, not every step after it.
enum Step { kStepCrew, kStepAimLines, kStepJetReap, kStepHeliReap, kStepPlayerJet, kStepSub, kStepHeli, kStepGround, kStepHud,
            kStepJetSound, kStepLockSound, kStepRescue, kStepHudPublish, kStepJetSoundTick, kStepUnderground, kStepShield, kStepView, kStepDrill,
            kStepLauncher, kStepHeliSight, kStepNet, kStepHighCam, kStepStockHud, kStepWarn, kStepSeats, kStepPayload, kStepSidecar, kStepTurretCam, kStepRam, kStepStab, kStepVehicleSound, kStepEmc, kStepProteus, kStepBoarding, kStepNpcPost, kStepNpcGunners, kStepCount };
const char* const kStepNames[kStepCount]={"crew","aim lines","jet reap","heli reap","player jet","carrier","heli","ground","hud see",
                                          "jet sound","lock sound","rescue","hud publish","jet sound tick","underground","shield","view","drill",
                                          "launcher","heli sight","net probe","high cam","stock hud","warn","seat switch","payload","sidecar","turret cam","ram","stabilizer","vehicle sound","emc","proteus","boarding","npc post","npc gunners"};
constexpr ULONGLONG kFaultLogMs=10000;
struct Faults { unsigned count; ULONGLONG loggedAt; } faults[kStepCount]{};

int StepFault(int step,const EXCEPTION_POINTERS* e) noexcept {
    Faults& f=faults[step];
    ++f.count;
    const ULONGLONG now=GetTickCount64();
    if(f.loggedAt && now-f.loggedAt<kFaultLogMs)return EXCEPTION_EXECUTE_HANDLER;
    f.loggedAt=now;
    const auto at=static_cast<const unsigned char*>(e->ExceptionRecord->ExceptionAddress);
    const bool inGame=at>=image && at<image+0x22CE000;
    Log("FAULT %s: %08lX at %s%llX (%u so far; the step is skipped, the others go on)",kStepNames[step],e->ExceptionRecord->ExceptionCode,
        inGame ? "EDF+" : "",static_cast<unsigned long long>(inGame ? static_cast<std::uintptr_t>(at-image) : reinterpret_cast<std::uintptr_t>(at)),f.count);
    return EXCEPTION_EXECUTE_HANDLER;
}
using VehicleStep=void(*)(unsigned char*);
void Guarded(int step,VehicleStep run,unsigned char* v) noexcept {
    __try { run(v); } __except(StepFault(step,GetExceptionInformation())) {}
}
void GuardedTick(int step,void(*run)()) noexcept {
    __try { run(); } __except(StepFault(step,GetExceptionInformation())) {}
}

template<int I> void CrewStep(unsigned char* v) noexcept { Crew(v,I); }
void JetReapStep(unsigned char* v) noexcept { JetReap(v); }
void HeliReapStep(unsigned char* v) noexcept { HeliReap(v); }
void HeliStep(unsigned char* v) noexcept { if(IsHelicopter(v))HeliFrame(v); }
// The submarine carrier is the plugin's own (subcarrier.cpp's registry), driven whoever sits in it: not from
// HeliFrame, which wants an NPC pilot in seat 0 and the heli profile.
void SubStep(unsigned char* v) noexcept { if(BodyOf(v)==PluginBody::sub){SubFrame(v);CarrierLaserFrame(v);} }
void GroundStep(unsigned char* v) noexcept { if(IsGroundRobo(v))GroundFrame(v); }

// Once a game frame, from the first vehicle input of the frame: what is no one vehicle's (the sea rescue,
// which needs no heli to exist yet; the HUD's publish of what it gathered last frame).
ULONGLONG tickFrame=0;
// Diagnostics of things under the ground (the user, 2026-10-04: everything falls through it now and then, the
// player too; the RE found no single cause, docs/bigworld-re.md): an object is under the terrain when a map ray
// from kProbeUp over it comes down on ground more than kUnder over it, and nothing is under it within kUnderFloor
// (a bridge or a roof over it has ground under it too). Each object's going under and coming back up is logged
// (UNDERGROUND), at most one line per kUnderLogMs: where, the surface over it, its fall speed, a soldier's support
// state; coming up, the jump (the game's put-back, 0x5A9E50, moves a vehicle up but keeps its fall speed).
constexpr float kProbeUp=400.0f,kUnder=2.5f,kUnderFloor=20.0f;
constexpr ULONGLONG kUnderLogMs=5000,kUnderEveryMs=100;
constexpr std::size_t kHumanSupport=0x711;   // CharacterControl_Walk +0x91: 2 on the ground, 1 sliding, 0 in the air
struct UnderWatch { const void* object; float y; ULONGLONG ms,loggedAt; bool under; };
UnderWatch underWatch[64]{};

UnderWatch& WatchOf(const void* object,ULONGLONG ms) noexcept {
    UnderWatch* free=&underWatch[0];
    for(auto& w:underWatch) {
        if(w.object==object)return w;
        if(ms-w.ms>kUnderLogMs*4 && ms-free->ms<=kUnderLogMs*4)free=&w;
    }
    *free=UnderWatch{object,0.0f,0,0,false};
    return *free;
}

bool UnderTerrain(const float* p,float* top) noexcept {
    const float from[3]={p[0],p[1]+kProbeUp,p[2]},to[3]={p[0],p[1]-1.0f,p[2]};
    float hit[3];
    if(MapRay(from,to,hit)<0.0f || !(hit[1]>p[1]+kUnder))return false;
    const float below=GroundClearance(p);
    *top=hit[1];
    return below==kNoGround || below>kUnderFloor;
}

void WatchUnder(const unsigned char* object,const char* what,const unsigned char* human) noexcept {
    const float* p=reinterpret_cast<const float*>(object+kPosition);
    if(!std::isfinite(p[0]+p[1]+p[2]))return;
    const ULONGLONG ms=GameMs();
    UnderWatch& w=WatchOf(object,ms);
    if(w.ms && ms-w.ms<kUnderEveryMs)return;   // two rays an object a kUnderEveryMs, not a frame
    float top=0.0f;
    const bool under=UnderTerrain(p,&top);
    const float dt=w.ms && ms>w.ms ? static_cast<float>(ms-w.ms)*0.001f : 0.0f,vy=dt>0.0f ? (p[1]-w.y)/dt : 0.0f;
    if(under!=w.under && ms-w.loggedAt>=kUnderLogMs) {
        w.loggedAt=ms;
        if(under)Log("UNDERGROUND %s %p went under the ground at (%.1f,%.1f,%.1f): the surface %.1f m over it, falling %.1f m/s%s%d",
                     what,object,p[0],p[1],p[2],top-p[1],-vy,human ? ", support " : "",human ? At<unsigned char>(human,kHumanSupport) : 0);
        else Log("UNDERGROUND %s %p came back up to (%.1f,%.1f,%.1f): %.0f m in %.2f s",what,object,p[0],p[1],p[2],p[1]-w.y,dt);
    }
    w.under=under;w.y=p[1];w.ms=ms;
}

void UnderPlayer() noexcept {
    if(const auto human=PlayerHuman())WatchUnder(human,"player",human);
}

void UnderVehicle(unsigned char* v) noexcept {
    const int c=ClassOf(v);
    WatchUnder(v,c>=0 ? kClasses[c].name : "vehicle",nullptr);
}

// The frame time, every kPerfMs (debug; the user, 2026-10-05: "it plays choppy"): the frames' mean and worst, and
// how many took over kPerfSlowMs: whether the game is slow all along or stalls now and then.
constexpr ULONGLONG kPerfMs=5000;
constexpr double kPerfSlowMs=33.4;
struct Perf { LARGE_INTEGER last; double worst,sum; unsigned frames,slow; ULONGLONG at; } perf{};
void PerfTick() noexcept {
    LARGE_INTEGER now,hz;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&hz);
    if(perf.last.QuadPart) {
        const double ms=1000.0*static_cast<double>(now.QuadPart-perf.last.QuadPart)/static_cast<double>(hz.QuadPart);
        if(ms<1000.0){perf.sum+=ms;++perf.frames;perf.slow+=ms>kPerfSlowMs;if(ms>perf.worst)perf.worst=ms;}
    }
    perf.last=now;
    const ULONGLONG t=GetTickCount64();
    if(t-perf.at<kPerfMs)return;
    if(Cfg().debug && perf.frames)Log("PERF %u frames: mean %.1f ms (%.0f fps), worst %.1f ms, %u over %.0f ms",perf.frames,
        perf.sum/perf.frames,1000.0*perf.frames/perf.sum,perf.worst,perf.slow,kPerfSlowMs);
    perf.at=t;perf.worst=perf.sum=0.0;perf.frames=perf.slow=0;
}

void FrameTick() noexcept {
    if(tickFrame==GameFrame())return;
    tickFrame=GameFrame();
    PerfTick();
    GuardedTick(kStepUnderground,&UnderPlayer);
    GuardedTick(kStepRescue,&RescueTick);
    GuardedTick(kStepWarn,&WarnTick);   // before the HUD's publish: it carries what this decides
    GuardedTick(kStepHudPublish,&HudPublish);
    GuardedTick(kStepUnderground,&BigWorldProbe);
    GuardedTick(kStepUnderground,&PlayAreaTick);   // the walls where the map's ground ends (playarea.cpp)
    GuardedTick(kStepPlayerJet,&PlayerEjectTick);
    GuardedTick(kStepView,&ViewTick);
    GuardedTick(kStepBoarding,&BoardingTick);
}

template<int I> void __fastcall InputHook(void* vehicle,std::uintptr_t hasInput,void* a3,void* a4) {
    LARGE_INTEGER t0,t1,t2;QueryPerformanceCounter(&t0);
    // The drill tank's trigger is its drill's: taken off the seat before the stock input reads it (drill.cpp).
    if(Cfg().enabled)Guarded(kStepDrill,&DrillInput,static_cast<unsigned char*>(vehicle));
    // The EMC's trigger is its charge's (emc.cpp): the same, the stock burst never starts.
    if(Cfg().enabled)Guarded(kStepEmc,&EmcInput,static_cast<unsigned char*>(vehicle));
    // An NPC tank pushed off its post drives back: seat 0's stick written before the stock input reads it (npcpost.cpp).
    if(Cfg().enabled)Guarded(kStepNpcPost,&NpcPostInput,static_cast<unsigned char*>(vehicle));
    // The NPC soldiers in its gunner seats aim and fire, before the stock input reads the seats (npcai.cpp).
    if(Cfg().enabled)Guarded(kStepNpcGunners,&NpcGunnersInput,static_cast<unsigned char*>(vehicle));
    nextInput[I](vehicle,hasInput,a3,a4);
    QueryPerformanceCounter(&t1);
    ReloadConfigIfChanged();   // before the Enabled test: Enabled=0 must be able to come back on
    auto v=static_cast<unsigned char*>(vehicle);
    SeeFrame(v);               // the frame is a clock: it steps with the plugin off too (body506's steps test it)
    GuardedTick(kStepJetSoundTick,&JetSoundTick);   // once a frame, the plugin off too: it stops the sounds then
    Guarded(kStepTurretCam,&TurretCamFrame,v);      // the plugin off too: it lets the camera go then
    Guarded(kStepProteus,&ProteusFrame,v);          // the plugin off too: a reworked Proteus gets its stock numbers back
    Guarded(kStepHighCam,&HighCamFrame,v);          // the plugin off too: the high view goes then
    Guarded(kStepVehicleSound,&VehicleSound,v);     // the plugin off too: the stock sounds are given back then
    Guarded(kStepEmc,&EmcFrame,v);                  // the plugin off too: a charge going is let go then (its loop, its glow)
    GuardedTick(kStepEmc,&EmcTick);                 // the plugin off too: an EMC gone mid-charge has its loop stopped
    if(!Cfg().enabled)return;
    FrameTick();
    Guarded(kStepCrew,&CrewStep<I>,v);
    Guarded(kStepSeats,&SeatSwitchFrame,v);    // before the steps that read who sits where this frame
    Guarded(kStepStab,&StabFrame,v);           // its seats' aims, for the stabilizer in the aim step after this input
    Guarded(kStepAimLines,&AimLines,v);
    Guarded(kStepJetReap,&JetReapStep,v);
    Guarded(kStepHeliReap,&HeliReapStep,v);
    Guarded(kStepPlayerJet,&PlayerJetFrame,v);
    Guarded(kStepSub,&SubStep,v);
    Guarded(kStepHeli,&HeliStep,v);
    Guarded(kStepHeli,&HeliCueStep,v);
    Guarded(kStepGround,&GroundStep,v);
    Guarded(kStepDrill,&DrillFrame,v);
    Guarded(kStepSidecar,&SidecarFrame,v);
    Guarded(kStepRam,&VehicleRamFrame,v);       // after the drill and the sidecar: what the parts drove into this frame
    Guarded(kStepHud,&HudSee,v);
    Guarded(kStepLauncher,&LauncherFrame,v);
    Guarded(kStepPayload,&PayloadFrame,v);       // before the sight: it marks the store the secondary fires
    Guarded(kStepHeliSight,&HeliSightFrame,v);   // after AimLines: the sight reads which lines are hidden
    Guarded(kStepStockHud,&StockHudFrame,v);
    Guarded(kStepJetSound,&JetSound,v);
    Guarded(kStepLockSound,&LockSound,v);
    Guarded(kStepUnderground,&UnderVehicle,v);
    Guarded(kStepShield,&ShieldVehicle,v);
    Guarded(kStepNet,&NetProbe,v);
    QueryPerformanceCounter(&t2);
    SlowLog(I,vehicle,t1.QuadPart-t0.QuadPart,t2.QuadPart-t1.QuadPart);
}

template<int... I> struct Hooks { static constexpr InputFn table[]={&InputHook<I>...}; };
using AllHooks=Hooks<0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23>;
static_assert(sizeof(AllHooks::table)/sizeof(AllHooks::table[0])==kClassCount,"one hook per class");

// Chains every concrete class's input slot (see EnsureInputs).
void InstallInputs() noexcept {
    int hooked=0;
    for(int i=0;i<kClassCount;++i) {
        if(!kClasses[i].input || !originalFindSeat_[i])continue;   // abstract bases, classes we leave alone or could not hook
        auto slot=reinterpret_cast<void**>(image+kClasses[i].vtable)+kClasses[i].inputSlot;
        void* current=*slot;
        nextInput[i]=reinterpret_cast<InputFn>(current);
        if(current!=image+kClasses[i].input)Log("HOOK input %s: chaining onto %p (another plugin)",kClasses[i].name,current);
        hooked+=PatchVtableSlot(slot,current,reinterpret_cast<void*>(AllHooks::table[i]));
    }
    Log("HOOK inputs=%d",hooked);
}
}  // namespace

// Not at load: another plugin (EDF6AutoTurret hooks 403/404/603) may patch the same input slots after us, and
// ours must chain onto theirs. Once, from the first of the mission's start (mission.cpp), the on-foot prompt
// and the board button, whichever is hooked and comes first: no single hook's failure leaves the per-frame
// layer off.
void EnsureInputs() noexcept {
    if(InterlockedCompareExchange(&inputsHooked,1,0)!=0)return;
    InstallInputs();
    InstallNpcAi();   // the soldiers' Think, chained after any other plugin's the same way (npcai.cpp)
}

bool InstallCrew() noexcept {
    // Without the game's SetTeam the plugin does not change a team at all (SetObjectTeam): no crew.
    setTeamOk=Matches(kSetTeam,kSetTeamSig,sizeof(kSetTeamSig));
    if(!setTeamOk){Log("HOOK crew: SetTeam not as expected: crew off");return false;}
    // A class whose slot 49 is not its own stock FindSeat (another plugin's) is left alone, seats and input
    // both. The prompt visitor not stock costs only the prompt's part (on-foot prompt for an NPC's seat, the
    // on-foot player fix, the reaps with the player on foot); the board button still bumps.
    int seats=0;
    for(int i=0;i<kClassCount;++i) {
        const auto& c=kClasses[i];
        void* stock=image+c.findSeat;
        auto slot=reinterpret_cast<void**>(image+c.vtable)+kSlotFindSeat;
        if(*slot!=stock){Log("HOOK crew: %s slot 49 not stock, class skipped",c.name);continue;}
        if(!PatchVtableSlot(slot,stock,reinterpret_cast<void*>(&FindSeatHook)))continue;
        originalFindSeat_[i]=reinterpret_cast<FindSeatFn>(stock);
        ++seats;
    }
    if(!seats)return false;   // no class hooked: no input to chain either (InstallInputs takes the hooked ones)
    void** const promptSlot=reinterpret_cast<void**>(image+kPromptFunctorVtable)+1;
    bool prompt=false;
    if(*promptSlot!=image+kPromptVisit)Log("HOOK crew: prompt visitor not stock: no prompt for an NPC's seat (the board button still bumps)");
    else {
        originalPrompt=reinterpret_cast<PromptFn>(image+kPromptVisit);
        prompt=PatchVtableSlot(promptSlot,image+kPromptVisit,reinterpret_cast<void*>(&PromptHook));
        if(!prompt){originalPrompt=nullptr;Log("HOOK crew: prompt patch failed: no prompt for an NPC's seat");}
    }
    Log("HOOK crew findSeat=%d/%d prompt=%d (inputs at the mission's start or the first prompt / board)",seats,kClassCount,prompt);
    return true;
}

int HiddenAimGuns(const unsigned char* seat,const unsigned char** out,int most) noexcept {
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(n>8 || !Readable(holders,n*8))return 0;
    int found=0;
    for(std::uint64_t w=0;w<n && found<most;++w) {
        const unsigned char* line=AimLineOf(holders,w);
        if(line && At<std::int32_t>(line,kAimLineSegments)==0)out[found++]=At<const unsigned char*>(holders[w],kHolderWeapon);
    }
    return found;
}

bool KnownVehicle(const void* object) noexcept { return ClassOf(object)>=0; }

bool PlayerBoardingEntrance(BoardingEntrance* out) noexcept {
    if(!out || !Cfg().enabled || !Cfg().playerJet)return false;
    __try {
        const auto* human=PlayerHuman();
        if(!human || human[kDead])return false;
        const auto* riding=At<const unsigned char*>(human,kHumanVehicleCtrl);
        if(riding && At<std::int32_t>(riding,8)>0)return false;
        const auto mask=At<std::uint32_t>(human,0x31C);
        const auto* pos=reinterpret_cast<const float*>(human+kPosition);
        float best=120.0f*120.0f;
        bool found=false;
        const ULONGLONG now=GameMs();
        for(const auto& st:states) {
            if(!st.ref || now-st.seen>300)continue;
            auto* v=static_cast<unsigned char*>(const_cast<void*>(st.ref.obj));
            if(!Readable(v,kSeatCount+8) || !st.ref.Is(v) || !IsJet(v) || !PlayerJetBoardable(v))continue;
            for(unsigned i=0;i<SeatCount(v);++i) {
                const auto* seat=SeatAt(v,i);
                const Rider rider=SeatRider(seat);
                if(rider!=Rider::none && !(rider==Rider::dummy && Cfg().bump))continue;
                if(!(mask & At<std::uint32_t>(seat,0x30) & At<std::uint32_t>(seat,0x34)))continue;
                float point[3],reach=0.0f;
                if(!SeatPoint(v,i,point,&reach) || !std::isfinite(reach) || reach<=0.0f)continue;
                const float dx=point[0]-pos[0],dy=point[1]-pos[1],dz=point[2]-pos[2];
                const float d=dx*dx+dy*dy+dz*dz;
                if(!std::isfinite(d) || d>=best)continue;
                best=d;found=true;
                std::memcpy(out->at,point,12);out->reach=reach;out->distance=std::sqrt(d);out->inReach=d<=reach*reach;
            }
        }
        return found;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

const char* VehicleClassName(const void* vehicle) noexcept {
    const int c=ClassOf(vehicle);
    return c>=0 ? kClasses[c].name : "vehicle";
}


// A new mission (mission.cpp MissionStart): the last mission's vehicles are gone, their lines with them.
void ResetCrew() noexcept {
    for(auto& s:states)s=State{};
    fullLoggedAt=0;
    for(auto& p:doorLogged)p=nullptr;
}
int VehicleClassOf(const void* object) noexcept { return ClassOf(object); }
}  // namespace crew
