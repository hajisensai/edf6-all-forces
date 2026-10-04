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
#include "crew.h"
#include "memory.h"
#include <cmath>

namespace crew {
// The game clock: wall time, except that a gap between two reads longer than kPauseMs (the pause menu,
// loading: nothing is flown or updated) counts as one 16 ms frame.
namespace { constexpr ULONGLONG kPauseMs=250; ULONGLONG clockWall=0,clockGame=0; }
ULONGLONG GameMs() noexcept {
    const ULONGLONG wall=GetTickCount64();
    if(clockWall)clockGame+=wall-clockWall>kPauseMs ? 16 : wall-clockWall;
    clockWall=wall;
    return clockGame;
}
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
using RideAiFn=void(__fastcall*)(void*,bool);
// Forwarded with all four register arguments: CarBase's input (slot 55) also reads r8 (its drive block)
// and the 502's pre-update (slot 4) takes `this` alone.
using InputFn=void(__fastcall*)(void*,std::uintptr_t,void*,void*);
using PromptFn=void(__fastcall*)(void*,void*);
using CanRideSeatFn=bool(__fastcall*)(void*,void*,void*);
using CanRideFn=bool(__fastcall*)(void*,void*);
using SeatRideFn=unsigned char*(__fastcall*)(void*,void*,int,bool);
using SeatFn=void(__fastcall*)(void*,void*);

// Every vehicle class we crew: vtable RVA, the stock function in its input slot (0: not hooked), name,
// its stock slot 49 (FindSeat), the slot we chain its per-frame input on, and whether only an armed one
// (a weapon holder, veh+0x648) gets an NPC driver.
// The 502 has the 54-slot VehicleBase vtable: no slot 55, its per-frame input copy is slot 4 (0x612D20).
// Vehicle_Car (the Grape, also the unarmed 512 Kei truck and 513 trailer cab) has its own slot 49
// (0x65B910, a preferred-seat wrapper round the stock one) and its CarBase input in slot 55 (0x65A390).
struct VehicleClass {
    unsigned vtable; unsigned input; const char* name;
    unsigned findSeat=kFindSeat; std::size_t inputSlot=kSlotInput; bool armedOnly=false;
};
constexpr std::size_t kHolderCount=0x648;
const VehicleClass kClasses[]={
    {0x17D8B50,0x5FD8E0,"402_Rocket"},{0x17D8FA0,0x5FEBE0,"403_Tank"},{0x17D9458,0x5FFC50,"404_Tank"},
    {0x17D98C8,0,"501_FortressRobo"},{0x17DA028,0x612D20,"502_GroundRobo",kFindSeat,4},{0x17DA508,0x6178B0,"503_Bike"},
    {0x17DA960,0x63C1C0,"504_begaruta"},{0x17DADB0,0x61ACD0,"505_Tank"},{0x17DB238,0x61B8F0,"506_Helicopter"},
    {0x17DB9D8,0x61DDF0,"510_Maser"},{0x17DBDF8,0x61F080,"511_Bike"},{0x17DC250,0x620790,"601_Tank"},
    {0x17DC620,0x621460,"603_Flak"},{0x17DD440,0x63C1C0,"612_nix"},{0x17DD720,0,"VehicleBase"},
    {0x17DE0A8,0x63C1C0,"Begaruta"},{0x17DEC40,0,"BigBegaruta"},{0x17DEF98,0x64C020,"Helicopter409"},
    {0x17DF338,0x64E080,"Helicopter410"},{0x17DF790,0x6543A0,"HelicopterBase"},{0x17DFDC8,0,"BikeBase"},
    {0x17E0A80,0,"CarBase"},{0x17E1828,0,"TankBase"},
    {0x17E01B0,0x65A390,"Car",0x65B910,kSlotInput,true},
};
constexpr int kClassCount=static_cast<int>(sizeof(kClasses)/sizeof(kClasses[0]));
// The next function in each patched input slot: the stock one, or another plugin's hook
// (EDF6AutoTurret hooks 403/404/603) that ran its patch before ours.
InputFn nextInput[kClassCount]{};
// Each class's own slot 49, which our FindSeat hook calls through (null: that class not hooked).
FindSeatFn originalFindSeat_[kClassCount]{};
PromptFn originalPrompt=nullptr;
bool inputsHooked=false;

struct State {
    const void* vehicle;
    ULONGLONG emptySince,playerAt,bumpedAt,crewedAt,loggedAt,seen;
    std::int32_t ownTeam;   // the vehicle's team before we crewed it (valid when crewedAt != 0)
};
State states[64]{};

// The state of a vehicle we track, without claiming a slot for one we do not.
State* FindState(const void* vehicle) noexcept {
    for(auto& s:states)if(s.vehicle==vehicle)return &s;
    return nullptr;
}

// The team a player boards it as: the team it had before we crewed it, else its own.
std::int32_t OwnTeam(const unsigned char* vehicle) noexcept {
    const State* st=FindState(vehicle);
    return st && st->crewedAt ? st->ownTeam : At<std::int32_t>(vehicle,kTeam);
}

State& StateFor(const void* vehicle) noexcept {
    State* slot=&states[0];
    for(auto& s:states) {
        if(s.vehicle==vehicle)return s;
        if(s.seen<slot->seen)slot=&s;
    }
    *slot=State{};slot->vehicle=vehicle;
    return *slot;
}

int ClassOf(const void* object) noexcept {
    if(!Readable(object,8))return -1;
    const auto vtable=At<const unsigned char*>(object,0);
    for(int i=0;i<kClassCount;++i)if(vtable==image+kClasses[i].vtable)return i;
    return -1;
}

const void* RiderObject(const unsigned char* seat) noexcept { return At<const void*>(seat,kSeatRider); }

// Aim lines (docs/aim-line-re.md): the red line out of a vehicle gun's muzzle. Weapon_VehicleShoot's ctor
// (0x6B3250, also VehicleMaser / RailGun / SwingShoot) makes a WeaponAimLine (vtable kAimLineVtable) of
// custom_parameter[0] segments and keeps it at weapon+kWeaponAimLine; each frame 0x6899F0 builds the line
// from its segment count (+kAimLineSegments, 0: no line). An NPC's seat gets 0; a player in the seat gets
// the count back.
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr std::size_t kWeaponAimLine=0x1638,kAimLineSegments=0x130;
constexpr unsigned kAimLineVtable=0x17E2418;
struct HiddenLine { unsigned char* line; std::int32_t segments; };
HiddenLine hiddenLines[64]{};
unsigned hiddenNext=0;

HiddenLine* HiddenOf(const unsigned char* line) noexcept {
    for(auto& h:hiddenLines)if(h.line==line)return &h;
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

void SetLine(unsigned char* line,Rider rider) noexcept {
    const auto segments=At<std::int32_t>(line,kAimLineSegments);
    HiddenLine* h=HiddenOf(line);
    if(rider==Rider::dummy && segments>0) {
        if(!h)h=&hiddenLines[hiddenNext++%64];
        *h=HiddenLine{line,segments};
        Put<std::int32_t>(line,kAimLineSegments,0);
    } else if(rider==Rider::player && h) {
        Put<std::int32_t>(line,kAimLineSegments,h->segments);
        *h=HiddenLine{};
    }
}

// Every seat's guns: no line while an NPC holds the seat, or while it is empty in a vehicle an NPC
// drives (the 410's door guns, aimed by the plugin with nobody in them); the stock line while the player
// holds it.
void AimLines(unsigned char* vehicle) noexcept {
    const unsigned count=SeatCount(vehicle);
    const bool npcDriven=count>0 && SeatRider(SeatAt(vehicle,0))==Rider::dummy;
    for(unsigned i=0;i<count && i<16;++i) {
        auto seat=SeatAt(vehicle,i);
        Rider rider=SeatRider(seat);
        if(rider==Rider::none && npcDriven)rider=Rider::dummy;
        if(rider!=Rider::dummy && rider!=Rider::player)continue;
        const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(n>8 || !Readable(holders,n*8))continue;
        for(std::uint64_t w=0;w<n;++w)
            if(auto line=AimLineOf(holders,w))SetLine(line,rider);
    }
}

// Run `check` with the NPC riders of the vehicle's NPC-held seats hidden and the vehicle on its own
// team (see OwnTeam); restores both before returning.
template<class F> bool WithDummiesHidden(unsigned char* vehicle,F check) noexcept {
    const std::int32_t team=At<std::int32_t>(vehicle,kTeam);
    Put<std::int32_t>(vehicle,kTeam,OwnTeam(vehicle));
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
    Put<std::int32_t>(vehicle,kTeam,team);
    return ok;
}

// A free seat other than `skip` for the NPC to move into, or -1.
int FreeGunnerSeat(unsigned char* vehicle,unsigned skip) noexcept {
    const unsigned count=SeatCount(vehicle);
    for(unsigned i=0;i<count;++i)if(i!=skip && SeatRider(SeatAt(vehicle,i))==Rider::none)return static_cast<int>(i);
    return -1;
}

// Free the NPC-held seat `index` for the player: move the NPC to a free gunner seat, else kick it.
void Bump(unsigned char* vehicle,unsigned index) noexcept {
    Put<std::int32_t>(vehicle,kTeam,OwnTeam(vehicle));   // the stock slot 49 re-checks the team next
    auto seat=SeatAt(vehicle,index);
    auto rider=const_cast<void*>(RiderObject(seat));
    const int gunner=Cfg().bumpToGunner ? FreeGunnerSeat(vehicle,index) : -1;
    if(gunner>=0 && reinterpret_cast<SeatRideFn>(image+kSeatRide)(vehicle,rider,gunner,false)) {
        reinterpret_cast<SeatFn>(image+kSeatClear)(vehicle,seat);
        Log("BUMP v=%p seat=%u -> npc moved to gunner seat %d",vehicle,index,gunner);
    } else {
        reinterpret_cast<SeatFn>(image+kSeatKick)(vehicle,seat);
        Log("BUMP v=%p seat=%u -> npc kicked (no free gunner seat)",vehicle,index);
    }
    StateFor(vehicle).bumpedAt=GameMs();
}

unsigned char* __fastcall FindSeatHook(void* vehicle,void* human) {
    const int cls=ClassOf(vehicle);
    const FindSeatFn originalFindSeat=cls>=0 && originalFindSeat_[cls] ? originalFindSeat_[cls] : reinterpret_cast<FindSeatFn>(image+kFindSeat);
    auto seat=originalFindSeat(vehicle,human);
    if(seat || !Cfg().enabled || !Cfg().bump || BumpSuppressed())return seat;
    __try {
        auto v=static_cast<unsigned char*>(vehicle);
        // The jets are NPC aircraft: their pilot is never bumped for the player (they have no other seat).
        if(!IsPlayer(static_cast<const unsigned char*>(human)) || IsJet(v) || IsSub(v))return nullptr;
        // An NPC still aboard (one moved to a gunner seat, the player gone again) keeps the vehicle on
        // its team, and the stock check then refuses even a free seat: ask again on the vehicle's own.
        const auto team=At<std::int32_t>(v,kTeam),own=OwnTeam(v);
        if(team!=own) {
            Put<std::int32_t>(v,kTeam,own);
            if(auto free=originalFindSeat(vehicle,human))return free;
            Put<std::int32_t>(v,kTeam,team);
        }
        const unsigned count=SeatCount(v);
        for(unsigned i=0;i<count;++i) {
            auto s=SeatAt(v,i);
            if(SeatRider(s)!=Rider::dummy)continue;
            const bool ok=WithDummiesHidden(v,[&]{ return reinterpret_cast<CanRideSeatFn>(image+kCanRideSeat)(v,human,s); });
            if(!ok)continue;
            Bump(v,i);
            return originalFindSeat(vehicle,human);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

void InstallInputs() noexcept;

// The on-foot ride prompt, once per object per frame for every human on foot.
void __fastcall PromptHook(void* functor,void* object) {
    originalPrompt(functor,object);
    if(!Cfg().enabled)return;
    __try {
        auto f=static_cast<unsigned char*>(functor);
        auto human=At<unsigned char*>(f,kFunctorHuman);
        if(!IsPlayer(human))return;
        SeePlayer(reinterpret_cast<const float*>(human+kPosition),At<std::int32_t>(human,kTeam));
        JetReap(object);   // a withdrawn jet with no other vehicle about (the player on foot)
        HeliReap(object);  // ...and a called heli that left
        if(!inputsHooked)InstallInputs();   // first mission frame: every plugin has loaded by now
        if(!Cfg().bump || BumpSuppressed() || f[kFunctorResult] || ClassOf(object)<0 || IsJet(object) || IsSub(object))return;
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
    State& st=StateFor(vehicle);st.seen=now;
    const unsigned count=SeatCount(vehicle);
    bool anyPlayer=false,driver=false;
    int dummies=0;
    for(unsigned i=0;i<count;++i) {
        const Rider r=SeatRider(SeatAt(vehicle,i));
        anyPlayer=anyPlayer || r==Rider::player;
        driver=driver || (i==0 && r!=Rider::none);
        dummies+=r==Rider::dummy;
    }
    if(Cfg().debug && now-st.loggedAt>5000) {
        st.loggedAt=now;
        char riders[17]{};
        for(unsigned i=0;i<count && i<16;++i)riders[i]="-dPo"[static_cast<int>(SeatRider(SeatAt(vehicle,i)))];
        const float* p=reinterpret_cast<const float*>(vehicle+kPosition);
        Log("VEH v=%p %s team=%d playerTeam=%d seats=[%s] pos=(%.0f,%.0f,%.0f) dist=%.0f",vehicle,kClasses[cls].name,
            At<std::int32_t>(vehicle,kTeam),player.team,riders,p[0],p[1],p[2],player.at ? std::sqrt(Distance2(vehicle,player.pos)) : -1.0f);
    }
    if(anyPlayer) {
        st.playerAt=now;st.emptySince=0;
        SeePlayer(reinterpret_cast<const float*>(vehicle+kPosition),At<std::int32_t>(vehicle,kTeam));
        return;
    }
    if(driver || !Cfg().autoCrew || IsPlayerJet(vehicle)){st.emptySince=0;return;}   // a player jet waits for the player
    if(!st.emptySince)st.emptySince=now;
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
    auto rideAi=reinterpret_cast<RideAiFn*>(At<void**>(vehicle,0))[kSlotRideAi];
    rideAi(vehicle,false);
    st.crewedAt=now;st.emptySince=0;st.ownTeam=team;
    if(IsHelicopter(vehicle))HeliCrewed(vehicle);
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

template<int I> void __fastcall InputHook(void* vehicle,std::uintptr_t hasInput,void* a3,void* a4) {
    LARGE_INTEGER t0,t1,t2;QueryPerformanceCounter(&t0);
    nextInput[I](vehicle,hasInput,a3,a4);
    QueryPerformanceCounter(&t1);
    ReloadConfigIfChanged();   // before the Enabled test: Enabled=0 must be able to come back on
    if(!Cfg().enabled)return;
    __try {
        auto v=static_cast<unsigned char*>(vehicle);
        SeeFrame(v);
        Crew(v,I);
        AimLines(v);
        JetReap(v);
        HeliReap(v);
        PlayerJetFrame(v);
        if(IsHelicopter(v))HeliFrame(v);
        if(IsGroundRobo(v))GroundFrame(v);
        HudSee(v);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    QueryPerformanceCounter(&t2);
    SlowLog(I,vehicle,t1.QuadPart-t0.QuadPart,t2.QuadPart-t1.QuadPart);
}

template<int... I> struct Hooks { static constexpr InputFn table[]={&InputHook<I>...}; };
using AllHooks=Hooks<0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23>;
static_assert(sizeof(AllHooks::table)/sizeof(AllHooks::table[0])==kClassCount,"one hook per class");

// Chains every concrete class's input slot. Runs on the game thread, first mission frame.
void InstallInputs() noexcept {
    inputsHooked=true;
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

bool InstallCrew() noexcept {
    // The prompt visitor must be stock; a class whose slot 49 is not its own stock FindSeat (another
    // plugin's) is left alone, seats and input both.
    if(reinterpret_cast<void**>(image+kPromptFunctorVtable)[1]!=image+kPromptVisit){Log("HOOK crew: prompt visitor not stock");return false;}
    originalPrompt=reinterpret_cast<PromptFn>(image+kPromptVisit);
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
    const bool prompt=PatchVtableSlot(reinterpret_cast<void**>(image+kPromptFunctorVtable)+1,image+kPromptVisit,reinterpret_cast<void*>(&PromptHook));
    Log("HOOK crew findSeat=%d/%d prompt=%d (inputs on the first mission frame)",seats,kClassCount,prompt);
    return seats>0 || prompt;
}
// A new mission (mission.cpp MissionStart): TODO(review) drop this module's per-object state.
void ResetCrew() noexcept {}
}  // namespace crew
