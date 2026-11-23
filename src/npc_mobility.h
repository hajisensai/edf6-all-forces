// The NPC soldiers' own dashes (docs/npc-mobility-re.md), the pure part of npc_logic.h: no game, no Windows, so
// tools/npc_mobility_check.cpp runs it offline. src/npcai.cpp Mobility reads the soldier into these structs after Drive
// and writes the one press they ask for into the intent block.
//  - Wing Diver: the stock dash (d7B pressed, PaleWing slot 5 0x580B06): its gates read before pressing, the energy it
//    costs (+0x2380, taken at once; more than is left overheats her) kept within a budget, and when it is worth it (away
//    from a crowd, to a far goal, catching up).
//  - Fencer: the dash and the jet boost (d72+k: the secondary action of WeaponSet k's weapon, 0x59AE42 -> 0x698570 ->
//    message 0x2003 kind 1 / 0 -> 0x569D50 / 0x566BE0), which hand does which (the weapon's SecondaryFire_Type and the
//    equipment's swap flags), and which of dash-jump / dash / boost / nothing a stretch of the way calls for; the
//    dash-jump's boost goes in while the dash still carries its speed.
#pragma once
#include <cmath>
#include <cstdint>

namespace npc {
namespace mobility {
enum class Act : std::uint8_t { none, dash, dashJump, boost };
inline const char* ActName(Act a) noexcept {
    switch(a) {
    case Act::dash: return "dash";
    case Act::dashJump: return "dashjump";
    case Act::boost: return "boost";
    case Act::none: break;
    }
    return "none";
}
// At least `sec` s since the last one (`sinceMs` ms ago).
inline bool Rested(std::uint64_t sinceMs,float sec) noexcept {
    return std::isfinite(sec) && sinceMs>=static_cast<std::uint64_t>((sec>0.0f ? sec : 0.0f)*1000.0f);
}

// --- Wing Diver ---
// What 0x580B06..0x580B94 asks before the dash (H), read off the soldier: control mask bit 10 (+0x158C & 0x400), no
// occupying action (+0x5D0 & 4), not downed / grabbed / aboard (+0x39C & ~2), not overheated (+0x2FC). `locked`: the
// action lock held by a weapon (+0x1A28: a reload or a switch; 0x590A10 would cut it, the plugin waits instead), or its
// level +0x1A30 at 2 or more with no holder (0x590D70(h,2) refuses that too: 0x590A10(h,0,2) leaves it so).
// `air`: not supported (+0x711 != 2). Energy +0x308 of +0x304, the dash's cost +0x2380 (loaded per energy core).
struct WingState { bool permitted,busy,downed,overheated,locked,air; float energy,energyMax,cost; };
// `locked` as 0x590D70(h,2) and 0x590A43 read it: a holder at +0x1A28, or the level +0x1A30 at 2 or more.
inline bool WingLocked(bool holder,std::int32_t level) noexcept { return holder || level>=2; }
constexpr float kAirReserve=0.6f;   // share of the tank kept in the air: dashing down out of the sky she must fly again
inline float WingReserve(float reserve,bool air) noexcept { return air && reserve<kAirReserve ? kAirReserve : reserve; }
// The energy left after the dash keeps at least `reserve` of the tank and one more dash. Never true when the dash costs
// all that is left (0x54A2E0: cost >= energy empties it, more than it overheats her: +0x2FC).
inline bool WingBudget(float energy,float energyMax,float cost,float reserve) noexcept {
    if(!std::isfinite(energy) || !std::isfinite(energyMax) || !std::isfinite(cost) || !std::isfinite(reserve))return false;
    if(!(energyMax>0.0f) || !(cost>0.0f) || cost>energyMax)return false;
    const float share=reserve*energyMax,keep=share>cost ? share : cost;
    return energy-cost>=keep;
}
// The first gate that refuses the dash now (its name, for the log), nullptr when every one is open.
inline const char* WingGate(const WingState& s,float reserve) noexcept {
    if(!s.permitted)return "not permitted";
    if(s.busy)return "busy";
    if(s.downed)return "downed";
    if(s.overheated)return "overheated";
    if(s.locked)return "weapon lock";
    if(!WingBudget(s.energy,s.energyMax,s.cost,WingReserve(reserve,s.air)))return s.air ? "energy (air)" : "energy";
    return nullptr;
}
inline bool WingReady(const WingState& s,float reserve) noexcept { return WingGate(s,reserve)==nullptr; }
enum class Why : std::uint8_t { none, evade, goal, follow };
inline const char* WhyName(Why w) noexcept {
    switch(w) {
    case Why::evade: return "evade";
    case Why::goal: return "goal";
    case Why::follow: return "follow";
    case Why::none: break;
    }
    return "none";
}

// The route straight ahead (the dash's direction and whether a dash fits it). The ground route is a chain of 2 m grid
// cells (ground_navigation.h Navigate: every cell of the path, no corners collapsed), so its next waypoint is always
// within ~3 m and the way to it one of 8 grid directions: neither says where the walk goes over the dash's ~17 m. Off the
// grid's axes and diagonals the open-ground route is not even near the line: the search's tie rule (the farthest along
// among equally short) puts the diagonal cells of a 15 deg walk together, 5 of them and then 14 straight, its corner 6 m
// off the line (npc_mobility_check WingRuns, Navigate itself). The run is the farthest point of the path ahead, up to
// `most` m, to which the path goes straight: every point before it within `slack` m of the line from `from` (ahead of
// it), or the path to it no longer than the shortest 8-way grid walk there (Octile, kRunTaut over: the walk does not
// turn round anything, its cells are one of the grid's equally short walks between the two ends, and on open ground the
// line between them is the walk's own way; what stands on that line the caller's ray sees). Its length over the ground,
// `dir` the horizontal way along it (0 and dir untouched with no path). P: any point with x / z (navigation::Point).
constexpr float kRunTaut=0.5f;   // m a path may run over the shortest grid walk (the cells are placed from where it began)
inline float Octile(float dx,float dz) noexcept {
    dx=std::fabs(dx);dz=std::fabs(dz);
    return (dx>dz ? dx : dz)+0.41421356f*(dx>dz ? dz : dx);
}
template<class P>
inline float StraightRun(const float* from,const P* pts,int n,float most,float slack,float* dir) noexcept {
    if(!pts || n<=0 || !(most>0.0f) || !(slack>=0.0f))return 0.0f;
    float bestX=0.0f,bestZ=0.0f,best=0.0f,walked=0.0f,lastX=0.0f,lastZ=0.0f;
    for(int k=0;k<n;++k) {
        const float ex=pts[k].x-from[0],ez=pts[k].z-from[2],len=std::sqrt(ex*ex+ez*ez);
        if(!std::isfinite(len))break;
        walked+=std::sqrt((ex-lastX)*(ex-lastX)+(ez-lastZ)*(ez-lastZ));lastX=ex;lastZ=ez;
        if(len<1e-3f)continue;
        bool straight=walked<=Octile(ex,ez)+kRunTaut;
        for(int i=0;i<k && !straight;++i) {   // else each point before it within `slack` of the line from `from`
            const float px=pts[i].x-from[0],pz=pts[i].z-from[2];
            const float along=(px*ex+pz*ez)/len,off=std::fabs(px*ez-pz*ex)/len;
            if(!(off<=slack && along>=-slack && along<=len+slack))break;
            if(i==k-1)straight=true;
        }
        if(!straight && k==0)straight=true;   // the first point: the line to it is the walk itself
        if(!straight)break;
        if(len>=most){bestX=ex/len*most;bestZ=ez/len*most;best=most;break;}
        bestX=ex;bestZ=ez;best=len;
    }
    if(!(best>0.0f))return 0.0f;
    dir[0]=bestX/best;dir[1]=0.0f;dir[2]=bestZ/best;
    return best;
}
constexpr float kRunLook=30.0f,kRunSlack=1.5f;   // m: how far the run is looked for; off its line a cell may lie

// What the walk is for: keeping up (a formation, its post, a support follow), a fight (to its combat spot), a falling
// back (low on health, away from the threat), the player's order (a move / attack-move). A falling back and an order
// outrank a shot; only the order may dash towards the nearest enemy (the player sent it that way; a falling back's goal
// behind the player is kept off only the one threat it fled, npcai.cpp FallBack, not every enemy).
enum class Need : std::uint8_t { keepUp, fight, retreat, order };
inline const char* NeedName(Need n) noexcept {
    switch(n) {
    case Need::order: return "order";
    case Need::retreat: return "retreat";
    case Need::fight: return "fight";
    case Need::keepUp: break;
    }
    return "keep-up";
}
// Why a long walk was not dashed (the log's `because=`): none dashed it.
enum class Block : std::uint8_t { none, tooNear, turn, wall, enemyAhead, firing, paced };
inline const char* BlockName(Block b) noexcept {
    switch(b) {
    case Block::tooNear: return "near";
    case Block::turn: return "route turns";
    case Block::wall: return "wall ahead";
    case Block::enemyAhead: return "towards enemy";
    case Block::firing: return "firing";
    case Block::paced: return "paced march";
    case Block::none: break;
    }
    return "none";
}
// Its situation this frame: `pressed` the crowd response backs it off (a crowd, or one in grab range); `need` what the
// walk is for; `paced` the walk is a paced march's (MoveTo's pace under 1: mapcmd.cpp MapPaceScale, the squad at its
// top's share of a full walk, which a full-stick dash would break out of); `firing` a trigger pulled this frame (a ready weapon's shot going out: between two shots of a single-shot,
// charging or reloading weapon it is not); `threat` an enemy within twice the danger reach, `threatDot` the cosine between
// the dash and the way to it; `left` m it still has to walk (to the goal less where its walk stops); `run` m the route
// goes straight ahead (StraightRun); `clear` m free along the run (the map at the waist, the vehicles' bodies).
struct WingGoal { bool pressed; Need need; bool paced,firing,threat; float left,run,threatDot,clear; };
// m the dash carries: a static estimate (~17 m, docs/npc-mobility-re.md §3, L: the travel is not measured and scales
// with SGO 0xC8 per energy core), a metre over. The full-stick dash goes this far along the run whatever the route does
// after it, so both the straight run and the free way ahead are at least this: a shorter run turns (round what is in
// the way) before the dash ends, and the dash would leave the walk's way past the corner. Re-check once measured.
constexpr float kWingCarry=18.0f;
constexpr float kWingClear=kWingCarry;   // m free ahead along the run
constexpr float kWingRun=kWingCarry;     // m of straight route
static_assert(kWingRun<=kRunLook,"the run is looked for at least as far as a dash needs");
// How far a walk must have left before a dash: `range` for a fight's, a falling back's or an order's, twice that to
// keep up.
inline float WingFar(Need n,float range) noexcept { return n==Need::keepUp ? 2.0f*range : range; }
struct WingVerdict { Why why; Block block; };
// Why to dash now (`why`), or the first reason not to (`block`): out of the crowd first; else a walk with more than
// WingFar left, not a paced march's, along a route straight for kWingRun m and clear kWingClear m, not towards the
// nearest enemy (unless the player ordered the walk: a falling back is not exempt), and not while a shot goes out unless
// the walk outranks the shot: a falling back or an order, or a fight's walk past twice `range` (it is far out of place;
// one frame's shot gives way, the aim is kept through the dash: §2, +0x5D0 bit 1 clear). Never a walk within WingFar
// (its fixed launch overshoots it).
inline WingVerdict WingDecide(const WingGoal& g,float range) noexcept {
    if(g.pressed)return {Why::evade,Block::none};
    if(!(range>0.0f) || !std::isfinite(g.left) || g.left<=WingFar(g.need,range))return {Why::none,Block::tooNear};
    if(g.paced)return {Why::none,Block::paced};
    const bool priority=g.need==Need::order || g.need==Need::retreat || g.left>2.0f*range;
    if(g.firing && !priority)return {Why::none,Block::firing};
    if(!(g.run>=kWingRun))return {Why::none,Block::turn};
    if(!(g.clear>=kWingClear))return {Why::none,Block::wall};
    if(g.need!=Need::order && g.threat && g.threatDot>=0.5f)return {Why::none,Block::enemyAhead};
    return {g.need==Need::keepUp ? Why::follow : Why::goal,Block::none};
}

// --- How far a walk's dash carried (measured, logged: nothing held on it) ---
// 2026-11-21 (r2b): a Wing Diver ordered on stood ~70 s at one spot by a parked helicopter, her walk going nowhere the
// whole time and eight goal dashes pressed there (each taken: +0x5D0 0xE39, so never a miss), the map ray seeing nothing
// ahead (layer 22 has no vehicles). The dashes' clear way now sees the vehicles' bodies (VehicleAlong below); the walk
// itself still does not, which is its own fix (the route / the stuck check seeing vehicles), not a pause put on the
// dashes. What the press does is still not what the dash does, so the carry is measured for the log (WDDASH carry /
// blocked: kWingCarry is an unmeasured estimate, and a dash going nowhere again names what the clear way still misses):
// the press's spot is kept; when the dash state ends (+0x5D0 no longer 0xE39) or kCarryWaitMs after the press, whichever
// first, the horizontal metres from that spot are its carry, blocked under kCarryShare of kWingCarry. A press missed (not
// taken: CountMiss) carried nothing to measure; one measured past 2 x kCarryWaitMs was not driven in between (aboard,
// left to the stock) and is dropped; a new press with the last one still watched (NpcWingDashSec under kCarryWaitMs) has
// the last one measured first.
constexpr std::uint64_t kCarryWaitMs=1500;   // the dash measured at the latest this long after its press
constexpr float kCarryShare=0.25f;           // under this share of the estimate: blocked (~4.5 m of 18)
struct Carry { bool on=false,seen=false; float from[3]{}; std::uint64_t atMs=0; };
// The press taken at `from`: its carry watched (the last one's settled first: CarryPress).
inline void CarryStart(Carry& c,const float* from,std::uint64_t ms) noexcept {
    c.on=true;c.seen=false;c.atMs=ms;
    for(int i=0;i<3;++i)c.from[i]=from[i];
}
inline bool CarryBlocked(float moved,float expected) noexcept {
    return std::isfinite(expected) && expected>0.0f && !(std::isfinite(moved) && moved>=kCarryShare*expected);
}
enum class CarryNow : std::uint8_t { none, measure, dropped };
// This frame's watch, before anything is pressed: `inState` +0x5D0 the dash's, `missed` the press in flight ended not
// taken (Advance this frame). `measure`: the carry is due, the watch over (CarryBlocked on the metres from c.from).
inline CarryNow CarryTick(Carry& c,bool inState,bool missed,std::uint64_t ms) noexcept {
    if(!c.on)return CarryNow::none;
    if(missed){c.on=false;return CarryNow::dropped;}
    if(inState)c.seen=true;
    if(!(c.seen && !inState) && ms-c.atMs<kCarryWaitMs)return CarryNow::none;
    c.on=false;
    return ms-c.atMs>2*kCarryWaitMs ? CarryNow::dropped : CarryNow::measure;
}
// A dash pressed now (any: a crowd's too moves her): the last one still watched is handed back in `last` to be measured
// first (`measure`), never written over unjudged; then the new one is watched when it is a walk's (`walk`).
inline CarryNow CarryPress(Carry& c,bool walk,const float* from,std::uint64_t ms,Carry& last) noexcept {
    last=c;
    CarryNow was=CarryNow::none;
    if(c.on){c.on=false;was=ms-c.atMs>2*kCarryWaitMs ? CarryNow::dropped : CarryNow::measure;}
    if(walk)CarryStart(c,from,ms);
    return was;
}

// The first vehicle body along a ray the map ray does not see (layer 22: no units, no vehicles): `ray(a, b, hit, &body)`
// casts the object layer from a towards b (m to the nearest body hit, < 0 none, its point in `hit`), `vehicle(body)` says
// whether that body is a vehicle's (parked, driven, nobody's, aircraft included). A body that is not a vehicle's (a
// soldier, the dasher's own) is passed kBodyPast m and the ray goes on, at most `hits` bodies. The metres from `a` to
// the first vehicle body, `most` with none within `most` (or past `hits` others).
constexpr float kBodyPast=0.05f;
template<class Ray,class IsVehicle>
inline float VehicleAlong(const float* a,const float* dir,float most,int hits,Ray&& ray,IsVehicle&& vehicle) noexcept {
    if(!(most>0.0f))return most;
    const float b[3]={a[0]+dir[0]*most,a[1]+dir[1]*most,a[2]+dir[2]*most};
    float from[3]={a[0],a[1],a[2]},gone=0.0f;
    for(int n=0;n<hits;++n) {
        float at[3]{};std::uint32_t body=0;
        const float d=ray(from,b,at,&body);
        if(!(d>=0.0f) || !std::isfinite(at[0]+at[1]+at[2]))return most;
        const float ex=at[0]-a[0],ey=at[1]-a[1],ez=at[2]-a[2];
        gone=std::sqrt(ex*ex+ey*ey+ez*ez);
        if(!(gone<most))return most;
        if(vehicle(body))return gone;
        for(int k=0;k<3;++k)from[k]=at[k]+dir[k]*kBodyPast;
    }
    return most;
}
// The vehicle look (two object-layer rays, each body hit asked for its owner) is not cast every frame a dash is weighed
// (a Fencer walking with its uses back weighs one each frame; a Wing Diver held by a wall too): a soldier's last look,
// as far as `most`, stands kVehicleLookMs from within kVehicleLookMove m of its spot and for a way within
// kVehicleLookCos of its way. A vehicle moving in on it in that time is seen a quarter second late; the dash it would
// meet still has to be pressed and taken.
constexpr std::uint64_t kVehicleLookMs=250;
constexpr float kVehicleLookMove=1.0f,kVehicleLookCos=0.97f;   // ~14 deg
struct VehicleLook { bool on=false; std::uint64_t atMs=0; float pos[3]{},dir[3]{},most=0.0f,m=0.0f; };
inline bool LookFresh(const VehicleLook& l,const float* pos,const float* dir,float most,std::uint64_t ms) noexcept {
    if(!l.on || ms-l.atMs>=kVehicleLookMs || l.most!=most)return false;
    const float dx=pos[0]-l.pos[0],dz=pos[2]-l.pos[2];
    return dx*dx+dz*dz<kVehicleLookMove*kVehicleLookMove && dir[0]*l.dir[0]+dir[2]*l.dir[2]>=kVehicleLookCos;
}
inline void LookKeep(VehicleLook& l,const float* pos,const float* dir,float most,float m,std::uint64_t ms) noexcept {
    l.on=true;l.atMs=ms;l.most=most;l.m=m;
    for(int i=0;i<3;++i){l.pos[i]=pos[i];l.dir[i]=dir[i];}
}

// --- Fencer ---
enum class Kind : std::uint8_t { none, dash, boost };
// What the secondary press of a weapon does (H): SecondaryFire_Type (+0x690) 5 sends 0x2003 kind 1 (dash), 4 kind 0
// (boost), anything else no move (0 none, 1 zoom, 6 the shield). 0x5672B1 then swaps: +0x2082 (custom parameter 307)
// turns a dash into a boost, +0x2083 (306) a boost into a dash, each on the kind as sent.
inline Kind Effective(int secondaryType,bool dashToBoost,bool boostToDash) noexcept {
    const int sent=secondaryType==5 ? 1 : secondaryType==4 ? 0 : -1;
    if(sent<0)return Kind::none;
    int k=sent;
    if(dashToBoost && sent==1)k=0;
    if(boostToDash && sent==0)k=1;
    return k==1 ? Kind::dash : Kind::boost;
}
// Which WeaponSet dashes and which boosts (the first that does), -1 none.
struct Ability { int dashSet=-1,boostSet=-1; };
inline Ability AbilityOf(Kind hand0,Kind hand1) noexcept {
    Ability a;
    const Kind k[2]={hand0,hand1};
    for(int i=0;i<2;++i) {
        if(k[i]==Kind::dash && a.dashSet<0)a.dashSet=i;
        if(k[i]==Kind::boost && a.boostSet<0)a.boostSet=i;
    }
    return a;
}
inline bool Any(const Ability& a) noexcept { return a.dashSet>=0 || a.boostSet>=0; }
// The soldier and its way this frame. `goalDist` m to where it walks (0: nowhere); `wayDist` / `rise` m to and over the
// feet of the route's next corner; `clear` m free along the way at the waist on the map (terrain, buildings: as far as it
// was looked); `vehicle` m to the first vehicle body along it (VehicleAlong; the map has none), which stops the dashes
// only: a vehicle is not boosted over (kBlocked reads the map's wall alone, a car beside the walk is no ledge); `stuck` it has
// wanted to walk and not got anywhere; `evade` the crowd response backs it off; `threatNear` an enemy within twice the
// danger reach (the dashes kept for dodging); `landed` supported (+0x711 == 2); `dashBusy` dashing and not yet free to
// chain (+0x5D0 0xA38, +0x2080 0); `dashLeft` / `boostLeft` the uses left (+0x207C / +0x2068: the game refills them).
struct Situation {
    float goalDist,wayDist,rise,clear;
    bool stuck,evade,threatNear,landed,dashBusy;
    int dashLeft,boostLeft;
    float vehicle=HUGE_VALF;   // none looked for: none
};
struct Ranges { float dashJump,dash; int reserve; };
constexpr float kDashJumpClear=25.0f,kDashClear=18.0f;   // m free ahead: a dash-jump carries ~35 m, a dash ~20 m (L)
constexpr float kBoostRise=2.0f,kBoostNear=12.0f;        // a corner this much higher and this near: boost up to it
constexpr float kBoostSteep=0.5f;                        // ...and steeper than this (rise / run): a ramp is walked
constexpr float kBlocked=3.0f;                           // m: the waist meets a wall this near: boost over it
constexpr float kBoostStraightUp=3.0f;                   // m: a ledge this near is boosted straight up (stick at rest)
// What this stretch of the way calls for, the first that applies:
//  - pressed by a crowd: a dash out of it (the reserve is for this);
//  - the way up (a ledge too steep to walk, the map's wall at the waist, stuck): a boost from the ground;
//  - far and clear (the map and the vehicles), on the ground, both hands able: the dash-jump;
//  - not so far, or far with no boost: a dash, above the reserve when an enemy is near;
//  - else nothing (a walk does it; its launch would overshoot).
inline Act Choose(const Ability& a,const Situation& s,const Ranges& r) noexcept {
    const bool canDash=a.dashSet>=0 && !s.dashBusy,canBoost=a.boostSet>=0 && s.boostLeft>0 && s.landed;
    if(s.evade)return canDash && s.dashLeft>0 ? Act::dash : Act::none;
    if(!std::isfinite(s.goalDist) || !(s.goalDist>0.0f))return Act::none;
    const bool climb=(s.rise>kBoostRise && s.wayDist<kBoostNear && s.rise>s.wayDist*kBoostSteep) || s.clear<kBlocked || s.stuck;
    if(climb)return canBoost ? Act::boost : Act::none;
    const int keep=s.threatNear && r.reserve>0 ? r.reserve : 0;
    const bool dashOk=canDash && s.landed && s.dashLeft>keep;
    const float free=s.vehicle<s.clear ? s.vehicle : s.clear;
    if(dashOk && canBoost && s.goalDist>=r.dashJump && free>=kDashJumpClear)return Act::dashJump;
    if(dashOk && s.goalDist>=r.dash && free>=kDashClear)return Act::dash;
    return Act::none;
}

// --- The presses in flight ---
// A press is a frame's d72+k (Fencer) / d7B (Wing Diver); the weapon acts on its +0x13B's rising edge (0x6934F0), so a
// set is never pressed two frames running. The press goes through in that frame's slot 5 (0x59ABB0), so the next Think
// sees it taken (the uses down, +0x5D0 the state's).
// The dash-jump: the boost pressed kBoostFirst..kBoostLast frames after the dash, the dash state on (0x566BE0 checks
// neither the dash state nor its lock, which the dash took as nobody's: 0x590A10(h,0,2) passes again); the launch takes
// the dash's speed, capped at 42 (0x569A40), and the dash's 60 x 0.95^k stays over that through k = 6 (kCarryLast).
constexpr int kConfirmFrames=3,kBoostFirst=2,kBoostLast=4,kCarryLast=6;
static_assert(kBoostFirst>=1 && kBoostFirst<=kBoostLast && kBoostLast<=kCarryLast,"the boost goes in while the dash carries");
enum class Outcome : std::uint8_t { pending, done, missed };
struct Combo {
    Act act=Act::none;
    int dashSet=-1,boostSet=-1;
    std::uint64_t dashFrame=0,boostFrame=0;
    bool dashSeen=false,boostPressed=false;
    std::uint64_t lastPress[2]={0,0};   // each set's last press frame (+1: 0 is never)
};
inline bool Busy(const Combo& c) noexcept { return c.act!=Act::none; }
inline bool MayPress(const Combo& c,int set,std::uint64_t frame) noexcept {
    return set>=0 && set<2 && !(c.lastPress[set]!=0 && c.lastPress[set]==frame);   // stored +1: last frame's reads `frame`
}
inline void Pressed(Combo& c,int set,std::uint64_t frame) noexcept { c.lastPress[set]=frame+1; }
// Starts `act` with `a`'s sets at `frame`: the set to press now, -1 when it may not be (pressed last frame, or none).
inline int Begin(Combo& c,Act act,const Ability& a,std::uint64_t frame) noexcept {
    const int set=act==Act::boost ? a.boostSet : act==Act::none ? -1 : a.dashSet;
    if(!MayPress(c,set,frame) || (act==Act::dashJump && (a.boostSet<0 || a.boostSet==a.dashSet)))return -1;
    c.act=act;c.dashSet=a.dashSet;c.boostSet=a.boostSet;
    c.dashSeen=false;c.boostPressed=false;
    if(act==Act::boost)c.boostFrame=frame;else c.dashFrame=frame;
    Pressed(c,set,frame);
    return set;
}
struct Step { int press=-1; Outcome outcome=Outcome::pending; };
// One frame after Begin: `dashSeen` / `boostSeen` the press was taken (the uses down, or the state's +0x5D0). A press
// not taken within kConfirmFrames is a miss; a dash-jump whose boost window passed unpressed ends as the dash it was.
inline Step Advance(Combo& c,std::uint64_t frame,bool dashSeen,bool boostSeen) noexcept {
    Step s;
    if(c.act==Act::none)return s;
    const auto end=[&c,&s](Outcome o) noexcept { s.outcome=o;c.act=Act::none; };
    if(c.act==Act::boost) {
        if(boostSeen)end(Outcome::done);
        else if(frame-c.boostFrame>static_cast<std::uint64_t>(kConfirmFrames))end(Outcome::missed);
        return s;
    }
    const std::uint64_t k=frame-c.dashFrame;
    if(!c.dashSeen) {
        if(dashSeen)c.dashSeen=true;
        else {
            if(k>static_cast<std::uint64_t>(kConfirmFrames))end(Outcome::missed);
            return s;
        }
    }
    if(c.act==Act::dash){end(Outcome::done);return s;}
    // The dash-jump, its dash taken.
    if(c.boostPressed) {
        if(boostSeen)end(Outcome::done);
        else if(frame-c.boostFrame>static_cast<std::uint64_t>(kConfirmFrames))end(Outcome::missed);
        return s;
    }
    if(k>static_cast<std::uint64_t>(kBoostLast)){end(Outcome::done);return s;}
    if(k>=static_cast<std::uint64_t>(kBoostFirst) && MayPress(c,c.boostSet,frame)) {
        Pressed(c,c.boostSet,frame);
        c.boostPressed=true;c.boostFrame=frame;
        s.press=c.boostSet;
    }
    return s;
}
// Consecutive misses: kMissOff of them turn the soldier's own dashes off (true then).
constexpr int kMissOff=5;
inline bool CountMiss(int& misses,Outcome o) noexcept {
    if(o==Outcome::done)misses=0;
    else if(o==Outcome::missed)++misses;
    return misses>=kMissOff;
}
// Stuck: it has wanted to walk for `windowMs` and moved less than `least` m over the ground in that time.
constexpr std::uint64_t kStuckMs=1500;
constexpr float kStuckLeast=1.0f;
inline bool Stuck(float moved,std::uint64_t elapsedMs) noexcept { return elapsedMs>=kStuckMs && moved<kStuckLeast; }
}  // namespace mobility
}  // namespace npc
