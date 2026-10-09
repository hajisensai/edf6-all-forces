// The plugin's own AI for the friendly NPC soldiers (docs/npc-ai-design.md §3, §4; ini CustomNpcAi and the Npc* keys).
//
// The stock soldier AI is a state machine inside the human (SoldierBase, h+0x1BF0) run from vtable slot 7 (Think,
// 0x596B50 for the Ranger and the Air Raider, 0x567080 the Fencer, 0x580460 the Wing Diver): it clears the intent block
// h+0xD50..+0xD84 (0x596BAE) and writes it the way a pad would (move, look, triggers, jump, weapon picks); slots 4 and
// 5 consume it later the same frame. So the plugin wraps slot 7: the stock Think runs first, then the plugin rewrites
// the block for the soldiers it drives. Its writes live exactly as long as the stock AI's own (§3.2).
//
// Per soldier, per frame (Decide):
//  - the script's units (§4.2: a route, an NPC root leader with a route, a fixed position, a direction order): their
//    moves and targets stay the stock AI's (§4.3); the plugin only takes the trigger off a shot that would cross a friend
//    or blast one (ShotClear) and picks the weapon for the stock AI's own target;
//  - every other one: a target within its leash plus its engage range round its anchor (the player it follows, its NPC
//    leader, the spot it was free at), the weapon for that range and kind (PickArm), the look onto it, the trigger only
//    with the round's line and blast clear of friends and the map ray open; it moves only when it must: crowded (back
//    off, side-step, roll), hurt (behind the player), in the player's lane (out of it), engaged (to a combat spot beside
//    the player, never between them and the target); else the stock follow / formation move stands.
// Online: only the soldiers this machine runs (online_authority.h IsOnlineAuthority: its own registered ones, the host for
// one with no identity), as the stock AI writes only theirs; the block goes to the other machines the way the stock AI's
// does (§2.1).
#include "crew.h"
#include "npc_command.h"
#include "command_net.h"
#include "layout.h"
#include "memory.h"
#include "formation.h"
#include "ground_navigation.h"
#include "pickup.h"
#include "npc_logic.h"
#include "npc_mark.h"
#include "online_authority.h"
#include "real_driver_native.h"
#include "support_soldier.h"
#include "vhud.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace crew {
namespace {
void ResetGunnerInputs() noexcept;
bool InstallDriverPayload() noexcept;
void ResetDriverPayload() noexcept;
// --- The human (docs/npc-ai-design.md §3.1) ---
constexpr std::size_t kMoveX=0xD50,kMoveY=0xD54,kMoveZ=0xD58,kMoveW=0xD5C;   // the move stick, local (x, 0, z, 1)
constexpr std::size_t kLookPitch=0xD60,kLookYaw=0xD64;                       // the look's change this frame (rad)
constexpr std::size_t kTrigger=0xD70;                                         // WeaponSet i's trigger (held) at +i
constexpr std::size_t kJumpPress=0xD76;                                       // jump / evade pressed
constexpr std::size_t kPickWeapon=0xD82;                                      // pick weapon 0..2 (+i)
constexpr std::size_t kAimPitch=0x1230,kAimYaw=0x1234;                        // the look's target (0x573BA8 adds d60 to it)
constexpr std::size_t kViewPitch=0x1240,kViewYaw=0x1244;                      // the look as it is (eased onto the target)
constexpr std::size_t kListFlags=0x1A;constexpr unsigned char kInAiList=8;  // slot 4 leaves the block of these alone
constexpr std::size_t kControlMask=0x158C;                                    // bit 0 move, 1 look, 4 trigger 0, 11 roll
constexpr unsigned kMaskMove=0x1,kMaskLook=0x2,kMaskTrigger=0x10,kMaskRoll=0x800;
constexpr std::size_t kWeapons=0x1950,kWeaponCount=0x1960,kSets=0x1970,kSetCount=0x1980,kSetWeapon=0x40;
// The WeaponSets are an array of kSetStride each; set i holds **(set+kSetWeapon) and fires on kTrigger+i (0x59ACE2 walks
// it, 0x59ADE9 tests d70+i, 0x59B15E steps 0x150, H). Two hands: the Fencer's second weapon is set 1 (d71).
constexpr std::size_t kSetStride=0x150;
constexpr int kHands=2;
constexpr std::size_t kStockTarget=0x1CD0,kStockTargetCtrl=0x1CD8;
constexpr std::size_t kHumanHpMax=0x2F4,kHumanHp=0x2F8;
// --- Script control (§4.1) ---
constexpr std::size_t kRoute=0x4A8,kLeader=0x548,kDirectionFrames=0x4E0;
constexpr std::uint32_t kFixPosition=0x8000;
constexpr unsigned kNavigationExplorer=0x17D6940;
// --- A weapon (§3.5) ---
constexpr std::size_t kArmReach=0x224,kArmDamage=0x89C,kArmBlast=0x8B0;
// --- The friends' walk (as sidecar.cpp's): 0x5E11D0(manager, team, functor) visits every object of every team friendly
// to `team` through functor slot 1.
constexpr unsigned kTeamWalk=0x5E11D0,kTeamManager=0x20B2978;

struct SoldierClass { unsigned vtable,think; bool rolls; const char* name; };
// Slot 68 of the Ranger and the Air Raider rolls sideways on the evade press; the Wing Diver's and the Fencer's jump /
// boost (§3.1): the Wing Diver's jump takes it off the ground (an evade too), the Fencer only backs off.
constexpr int kClasses=4;
const SoldierClass kSoldiers[kClasses]={{0x17CDF28,0x596B50,true,"Ranger"},{0x17D0FF8,0x580460,true,"WingDiver"},
                                         {0x17CF5B8,0x567080,false,"Fencer"},{0x17CF100,0x596B50,true,"AirRaider"}};
struct Signature { std::size_t rva; unsigned char bytes[16]; std::size_t size; };
const Signature kSignatures[]={
    {0x596B50,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56},16},   // Think
    {0x596BAE,{0xE8,0x4D,0x67,0xFD,0xFF,0x49,0x8B,0xD4,0x48,0x8B,0xCB,0xE8,0x42,0x34,0xFB,0xFF},16},   // block cleared, base AI
    {0x573BA8,{0xF3,0x0F,0x10,0x86,0x3C,0x12,0x00,0x00,0x0F,0x10,0x8E,0x30,0x12,0x00,0x00,0x0F},16},   // look target += d60
    {0x59ADE9,{0x41,0x80,0xBC,0x1E,0x70,0x0D,0x00,0x00,0x00,0x74,0x24,0x48,0x8B,0x46,0x40,0x48},16},   // trigger d70 + set
    {0x596C30,{0x48,0x8B,0xC8,0x48,0x8B,0x80,0x48,0x05,0x00,0x00,0x48,0x85,0xC0,0x75,0xF1,0xEB},16},   // up +0x548 to the root
    {0x567080,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0xC2,0xFA,0x02,0x00,0x48,0x8D},16},   // Fencer: stock Think first
    {0x580460,{0xE9,0xEB,0x66,0x01,0x00},5},                                                           // Wing Diver: jmp Think
};
using ThinkFn=void(__fastcall*)(void*,const float*);
ThinkFn nextThink[kClasses]{};
bool ok=false;

constexpr float kEye=1.5f;              // m over the feet the soldier looks and shoots from
constexpr float kSoldierRadius=0.8f,kVehicleRadius=4.0f,kOtherRadius=1.5f;
constexpr float kSpread=0.6f;           // m a round's line is kept off a friend besides their radius
constexpr float kAirAbove=15.0f;        // m over the soldier: a flyer
constexpr int kLargePoints=3;           // lock points of one enemy: a large one
constexpr float kFriendNearTarget=10.0f;
constexpr float kFireConeMin=0.05f,kHitRadius=1.5f;
constexpr float kLookMost=0.35f;        // rad a frame
constexpr float kSpotStop=3.0f;         // m from its combat spot it stops
constexpr float kBehindPlayer=8.0f;     // m behind the player a hurt soldier falls back to
constexpr ULONGLONG kSpotMs=4000,kLosFrames=6,kStaleMs=2000,kLogMs=2000,kPlayerFixMs=2000;
constexpr int kArmMissFrames=90;        // a pick not taken this long (1.5 s: a heavy weapon's switch takes a while): picking off for it
constexpr int kMaxArms=16,kMaxSoldiers=256,kMaxEnemies=1024,kMaxFriends=512,kMaxRoot=16;

struct Soldier {
    ObjRef ref;
    ULONGLONG seen,loggedAt,rollAt,spotAt,losFrame;
    npc::Control control;
    bool controlSet,homeSet,spotSet,los,noSwitch;
    float home[3],spot[3];
    int arm,wantArm,armMiss;
    ObjRef target;
    const void* losObject;   // what the cached map ray was cast at
    ObjRef boardV;           // a board order (§7): the vehicle, the seat it was given, when
    int boardSeat;
    ULONGLONG boardAt;
    float fallTo[3];         // the fall-back's point (re-picked every kFallMs)
    ULONGLONG fallAt;
    npc::navigation::State navigation;   // connected ground route, tied to this ObjRef lifetime
    void* pickUnit;          // the box it goes for (the sweep: DropItemManager::Unit*), this frame's (pickFrame)
    float pickPos[3];
    int pickKind;
    ULONGLONG pickFrame;
};
Soldier soldiers[kMaxSoldiers]{};
ULONGLONG fullLoggedAt=0,listLoggedAt=0;

// --- The frame's world, gathered once a frame by the first soldier's Think ---
struct Enemy { const void* object; float aim[3]; int points; };
struct World {
    ULONGLONG frame;
    std::int32_t team;
    int enemies,friends;
    Enemy enemy[kMaxEnemies];
    npc::Friend fr[kMaxFriends];
    const void* frObject[kMaxFriends];
    bool player,lane,lookOk;
    float playerAt[3],look[3];
    npc::Lane laneOf;
};
World world{};

const float* Pos(const unsigned char* o) noexcept { return reinterpret_cast<const float*>(o+kPosition); }

void SeeEnemy(void* ctx,const void* object,const float* aim) {
    auto& w=*static_cast<World*>(ctx);
    for(int i=0;i<w.enemies;++i)if(w.enemy[i].object==object){++w.enemy[i].points;return;}
    if(w.enemies>=kMaxEnemies)return;
    Enemy& e=w.enemy[w.enemies++];
    e.object=object;std::memcpy(e.aim,aim,12);e.points=1;
}

struct FriendWalk { void** vtable; World* w; };
void __fastcall FriendVisit(void* self,void* object) noexcept {
    __try {
        auto& f=*static_cast<FriendWalk*>(self);
        auto o=static_cast<const unsigned char*>(object);
        World& w=*f.w;
        if(!o || o[kDead] || w.friends>=kMaxFriends || (w.friends>0 && w.frObject[0]==o))return;
        const float r=IsSoldierClass(o) || IsAnyPlayer(o) ? kSoldierRadius : KnownVehicle(o) ? kVehicleRadius : kOtherRadius;
        const float* p=Pos(o);
        if(!std::isfinite(p[0]+p[1]+p[2]))return;
        npc::Friend& fr=w.fr[w.friends];
        fr.pos[0]=p[0];fr.pos[1]=p[1]+(r==kVehicleRadius ? 1.5f : 1.0f);fr.pos[2]=p[2];fr.radius=r;
        w.frObject[w.friends++]=o;
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void __fastcall FriendDtor(void*,unsigned) noexcept {}
void* kFriendVtable[]={reinterpret_cast<void*>(&FriendDtor),reinterpret_cast<void*>(&FriendVisit)};
using WalkFn=void(__fastcall*)(void*,std::int32_t,void*);

// The player's lane (§3.4): from their chest along the camera's centre to the first map hit, or NpcLaneLength.
void PlayerLane(World& w) noexcept {
    w.lane=false;
    float eye[3],dir[3];
    w.lookOk=w.player && CameraRay(eye,dir);   // the look (the fall-back's "behind the player") whether or not the lane is on
    if(w.lookOk)std::memcpy(w.look,dir,12);
    if(!w.lookOk || !Cfg().npcFireLane)return;
    npc::Lane& l=w.laneOf;
    l.from[0]=w.playerAt[0];l.from[1]=w.playerAt[1]+kEye;l.from[2]=w.playerAt[2];
    for(int i=0;i<3;++i)l.to[i]=l.from[i]+dir[i]*Cfg().npcLaneLength;
    float hit[3];
    if(MapRay(l.from,l.to,hit)>=0.0f && std::isfinite(hit[0]+hit[1]+hit[2]))std::memcpy(l.to,hit,12);
    l.radius=Cfg().npcLaneWidth;
    w.lane=true;
}

// --- The mark (§6.3) ---
// On foot, the map shut, the game in front: NpcMarkKey marks the enemy lock point nearest the screen's centre within
// NpcMarkCone degrees (the same one again: the mark let go; another: the mark moves to it). With no enemy within
// kPointClear times that cone it is a point instead: the units selected on the map (mapcmd.cpp) guard where the centre
// meets the ground (MapCommandGuardAt), the mark kept; between the two (a near miss) nothing is done but a word that the
// key marks what it is aimed at. In the map the key marks the enemy under the pointer (mapcmd.cpp NpcMarkEnemy).
// The key is read on the player's own frame (map.cpp MapHumanFrame -> NpcMarkFrame), not in a soldier's Think: the point
// order is for any unit the map commands (helis, jets, tanks), with or without a friendly soldier in the mission.
// The mark is kept until that enemy dies or is gone (the user, 2026-10-07: "标记效果应该先打死才换吧"): not only while its
// lock point is lockable (the frame's enemy list holds the lockable ones alone: an enemy out of sight or out of lock range
// for a moment dropped the mark). Gone: its control block's strong count spent, another object at its address, or the
// object deleted (removed without dying: a script's despawn); jet.cpp Alive's test. The squads told to focus fire all
// take it while it is in the enemy list; every other soldier takes it first when it is within its longest reach plus how
// far its order lets it move (npc::MarkInReach). This machine's alone (§2.3).
struct MarkState { ObjRef obj; float at[3]; bool held; };
MarkState mark{};
struct MarkPub { bool on; float at[3]; ULONGLONG wall; NpcPing ping; };
MarkPub markPub{};
NpcPing ping{};
SRWLOCK markLock=SRWLOCK_INIT;
constexpr float kMarkFar=2000.0f;   // m: no mark past this, no point past this
constexpr float kPointClear=3.0f;   // the point needs no enemy within this many times NpcMarkCone of the centre
constexpr ULONGLONG kPingMs=3000;   // wall ms a point's ring and its result are shown

const Enemy* MarkedEnemy() noexcept {
    if(!npcmark::Alive(mark.obj))return nullptr;
    for(int i=0;i<world.enemies;++i)if(mark.obj.Is(world.enemy[i].object))return &world.enemy[i];
    return nullptr;
}

// The marked object still the one marked and in the game (jet.cpp Alive) and alive.
bool MarkAlive() noexcept {
    return npcmark::Alive(mark.obj);
}

// Where the marked enemy's lock point is now (lockable or not); where it was when it has none.
struct LastSeen { const ObjRef* ref; float* at; };
void SeeMarked(void* ctx,const void* object,const float* aim) {
    auto& l=*static_cast<LastSeen*>(ctx);
    if(object==l.ref->obj)std::memcpy(l.at,aim,12);
}

void Mark(const void* object,const float* at) noexcept {
    npcmark::Assign(mark.obj,npcmark::Capture(object));
    if(at)std::memcpy(mark.at,at,12);
}

void Ping(const float* at,int given) noexcept { ping=NpcPing{true,{at[0],at[1],at[2]},given,GetTickCount64()}; }

// The point under the screen's centre the selected units are sent to (no enemy near the centre).
void SendToPoint(const float* eye,const float* dir) noexcept {
    const float end[3]={eye[0]+dir[0]*kMarkFar,eye[1]+dir[1]*kMarkFar,eye[2]+dir[2]*kMarkFar};
    float hit[3];
    if(!(MapFloorRay(eye,end,hit)>=0.0f) || !std::isfinite(hit[0]+hit[1]+hit[2])) {
        Log("NPCAI mark: nothing near the screen's centre to mark, no ground under it");
        return;
    }
    const int given=MapCommandGuardAt(hit);
    Ping(hit,given);
    Log("NPCAI mark: no enemy near the screen's centre: the point (%.0f,%.0f,%.0f) -> %d",hit[0],hit[1],hit[2],given);
}

// The enemy lock point nearest the centre's ray (the lock registry's lockable ones of the player's side's enemies).
struct Aimed { const float* eye; const float* dir; const void* best; float off; float at[3]; };
void SeeAimed(void* ctx,const void* object,const float* aim) {
    auto& a=*static_cast<Aimed*>(ctx);
    const float to[3]={aim[0]-a.eye[0],aim[1]-a.eye[1],aim[2]-a.eye[2]};
    const float d=npc::Len(to);
    if(d<1.0f || d>kMarkFar)return;
    const float off=std::acos(npc::Clamp(npc::Dot(to,a.dir)/d,-1.0f,1.0f));
    if(off<a.off){a.off=off;a.best=object;std::memcpy(a.at,aim,12);}
}

void ToggleMark(std::int32_t team) noexcept {
    float eye[3],dir[3];
    if(!CameraRay(eye,dir))return;
    const float cone=Cfg().npcMarkCone*npc::kPi/180.0f;
    Aimed a{eye,dir,nullptr,cone*kPointClear,{}};
    VisitEnemiesOf(team,&SeeAimed,&a);
    if(!a.best){SendToPoint(eye,dir);return;}   // a miss never lets the mark go: only the same enemy again does
    if(a.off>cone) {                             // near an enemy, not on it: neither a mark nor a point
        Ping(a.at,kPingNearEnemy);
        Log("NPCAI mark: an enemy %.1f deg off the centre (the mark takes %.1f): nothing done",a.off*180.0f/npc::kPi,Cfg().npcMarkCone);
        return;
    }
    const bool same=mark.obj.Is(a.best);
    if(same)npcmark::Assign(mark.obj,{});
    else Mark(a.best,a.at);
    Log("NPCAI mark: %s",same ? "let go" : "an enemy marked");
}

// The mark kept while its enemy is in the game, at its lock point; published for the HUD.
void KeepMark() noexcept {
    if(MarkAlive()){LastSeen l{&mark.obj,mark.at};VisitLockPoints(&SeeMarked,&l);}
    else if(mark.obj){npcmark::Assign(mark.obj,{});Log("NPCAI mark: the marked enemy is dead or gone");}
    AcquireSRWLockExclusive(&markLock);
    markPub.on=npcmark::Enabled() && static_cast<bool>(mark.obj);std::memcpy(markPub.at,mark.at,12);markPub.wall=GetTickCount64();
    markPub.ping=ping;
    if(!npcmark::Enabled())markPub.ping.on=false;
    ReleaseSRWLockExclusive(&markLock);
}

void SweepFrame() noexcept;   // the box sweep (below): its key and who goes for which box, once a frame
void ResetSweep() noexcept;   // ...over, a new mission
void ResetCommandSnapshots() noexcept;
void InstallBoxes() noexcept;   // ...at load: the item boxes' code checked

// --- Formations (formation.h; the user, 2026-10-08) ---
// The march: the player's recruited squads as one formation round the player (FormationKey cycles it, the map's T
// too); a guard: each squad told to guard its own (NpcGuardFormation, the map's T on it).
struct March {
    npc::formation::Shape shape;
    bool shapeSet,held;
    ULONGLONG changedAt;          // wall clock: the HUD's banner
    npc::formation::Heading heading;
    npc::formation::Bound bound;
    ULONGLONG frame;              // the roster's frame
    int n;
    const void* member[32];
    int movers,pending,moversLast,pendingLast;   // bounding: this frame's and the last frame's
};
March march{};
SRWLOCK marchLock=SRWLOCK_INIT;

npc::formation::Shape MarchShape() noexcept {
    if(!march.shapeSet) {
        const npc::formation::Shape s=npc::formation::FromInt(Cfg().npcFormation);
        march.shape=npc::formation::MarchShape(s) ? s : npc::formation::Shape::stock;
        march.shapeSet=true;
    }
    return march.shape;
}
void SetMarchShape(npc::formation::Shape s,const char* by) noexcept {
    AcquireSRWLockExclusive(&marchLock);
    march.shape=s;march.shapeSet=true;march.changedAt=GetTickCount64();
    march.bound=npc::formation::Bound{};
    ReleaseSRWLockExclusive(&marchLock);
    Log("NPCAI formation: %s (%s)",npc::formation::Name(s),by);
}

bool KeyHeld(int vk) noexcept {
    if(vk<=0)return false;
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

void FormationTick() noexcept {
    unsigned char* const me=PlayerHuman();
    const bool down=Cfg().npcFormationKey>0 && me && HumanOnFoot(me) && KeyHeld(Cfg().npcFormationKey);
    if(down && !march.held && !MapHoldsKeys())SetMarchShape(npc::formation::Next(MarchShape(),false),"the key");
    march.held=down;
}

void Gather(std::int32_t team) noexcept {
    World& w=world;
    w.frame=GameFrame();w.team=team;w.enemies=w.friends=0;
    w.player=player.at && GameMs()-player.at<kPlayerFixMs;
    if(w.player)std::memcpy(w.playerAt,player.pos,12);
    VisitEnemiesOf(team,&SeeEnemy,&w);
    // The local player first: whatever the walk's order and the table's room, no shot crosses them (B1).
    if(unsigned char* const me=PlayerHuman(); me && HumanOnFoot(me)) {
        const float* p=Pos(me);
        w.fr[0]=npc::Friend{{p[0],p[1]+1.0f,p[2]},kSoldierRadius};w.frObject[0]=me;w.friends=1;
    }
    if(const auto manager=At<void*>(image,kTeamManager)) {
        FriendWalk f{kFriendVtable,&w};
        reinterpret_cast<WalkFn>(image+kTeamWalk)(manager,team,&f);
    }
    PlayerLane(w);
    FormationTick();
    SweepFrame();
}

Soldier* Entry(const unsigned char* h,ULONGLONG ms) noexcept {
    Soldier* slot=nullptr;
    for(auto& s:soldiers) {
        if(s.ref.Is(h))return &s;
        if(!slot && (!s.ref || s.ref.obj==h || ms-s.seen>kStaleMs))slot=&s;
    }
    if(!slot) {
        if(ms-fullLoggedAt>10000){fullLoggedAt=ms;Log("NPCAI table full: soldier %p left to the stock AI",h);}
        return nullptr;
    }
    *slot=Soldier{};
    slot->ref=ObjRef::Of(h);slot->arm=slot->wantArm=-1;
    return slot;
}

// --- What the stock fields say (§4.2) ---
const unsigned char* RootLeader(const unsigned char* h) noexcept {
    const unsigned char* root=nullptr;
    const unsigned char* at=At<const unsigned char*>(h,kLeader);
    for(int i=0;at && i<kMaxRoot;++i) {
        if(!Readable(at,kLeader+8))break;
        root=at;at=At<const unsigned char*>(at,kLeader);
    }
    return root;
}
bool Routed(const unsigned char* o) noexcept { return At<const void*>(o,kRoute)!=nullptr; }
bool Escort(const unsigned char* o) noexcept {
    const auto e=At<const unsigned char*>(o,kRoute);
    return e && Readable(e,8) && At<const unsigned char*>(e,0)==image+kNavigationExplorer;
}
npc::Control ControlOf(const unsigned char* h,const unsigned char* root) noexcept {
    npc::ScriptFacts f{};
    f.route=Routed(h);
    f.npcLeader=root!=nullptr;
    f.rootPlayer=root && IsAnyPlayer(root);   // a player of any machine leads it: recruited
    f.rootRouted=root && !f.rootPlayer && Routed(root);
    f.escort=(f.route && Escort(h)) || (f.rootRouted && Escort(root));
    f.fixed=(At<std::uint32_t>(h,kObjectFlags)&kFixPosition)!=0;
    f.directionFrames=At<std::int32_t>(h,kDirectionFrames);
    return npc::Classify(f);
}
const char* ControlName(npc::Control c) noexcept {
    switch(c) {
    case npc::Control::free: return "free";
    case npc::Control::squad: return "squad";
    case npc::Control::recruited: return "recruited";
    case npc::Control::script: return "script";
    case npc::Control::hold: return "hold";
    case npc::Control::escort: return "escort";
    }
    return "?";
}

// --- Its weapons (§3.5) ---
// held[k]: the index in arm[] of the weapon WeaponSet k holds (its trigger kTrigger+k), -1 when not known. held[0] is
// the one the plugin picks and aims; held[1] is the Fencer's second hand, which only the stock pulls.
struct Arms { int n=0; int held[kHands]{-1,-1}; npc::Arm arm[kMaxArms]{}; };
// WeaponSet k's weapon, nullptr when it has none or is not readable.
const unsigned char* SetWeapon(const unsigned char* h,int k) noexcept {
    if(At<std::uint64_t>(h,kSetCount)<=static_cast<std::uint64_t>(k))return nullptr;
    const auto set=At<const unsigned char*>(h,kSets)+k*kSetStride;
    if(!Readable(set,kSetWeapon+8))return nullptr;
    const auto entry=At<unsigned char* const*>(set,kSetWeapon);
    return Readable(entry,8) ? *entry : nullptr;
}
Arms ArmsOf(const unsigned char* h) noexcept {
    Arms a{};
    const auto list=At<unsigned char* const*>(h,kWeapons);
    const auto count=At<std::uint64_t>(h,kWeaponCount);
    if(!count || count>16 || !Readable(list,count*8))return a;
    const unsigned char* held[kHands];
    for(int k=0;k<kHands;++k)held[k]=SetWeapon(h,k);
    a.n=count<kMaxArms ? static_cast<int>(count) : kMaxArms;
    for(int i=0;i<a.n;++i) {
        const unsigned char* w=list[i];
        npc::Arm& m=a.arm[i];m=npc::Arm{};
        if(!Readable(w,kWeaponAmmo+4))continue;
        for(int k=0;k<kHands;++k)if(w==held[k])a.held[k]=i;
        const float reach=At<float>(w,kArmReach),damage=At<float>(w,kArmDamage),blast=At<float>(w,kArmBlast);
        m.reach=std::isfinite(reach) && reach>0.0f ? reach : 0.0f;
        m.blast=std::isfinite(blast) && blast>0.0f ? blast : 0.0f;
        m.dps=std::isfinite(damage) && damage>0.0f ? damage : 1.0f;
        m.homing=At<std::int32_t>(w,kWeaponLockon)==kHoming;
        m.antiAir=m.homing;
        m.ready=At<std::int32_t>(w,kWeaponAmmo)>0;
    }
    return a;
}

// --- The target ---
struct Pick { const Enemy* e; float dist; };
Pick PickTarget(const Soldier& s,const float* eye,const float* anchor,float scope) noexcept {
    Pick best{nullptr,0.0f};float bestScore=0.0f;
    for(int i=0;i<world.enemies;++i) {
        const Enemy& e=world.enemy[i];
        if(npc::Horiz(anchor,e.aim)>scope)continue;
        const float d=npc::Dist(eye,e.aim);
        const float score=d-(s.target.Is(e.object) ? 20.0f : 0.0f);
        if(!best.e || score<bestScore){best={&e,d};bestScore=score;}
    }
    return best;
}
const Enemy* StockTarget(const unsigned char* h) noexcept {
    const auto o=At<const void*>(h,kStockTarget);
    const auto ctrl=At<const unsigned char*>(h,kStockTargetCtrl);
    if(!o || !ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,8)==0)return nullptr;
    for(int i=0;i<world.enemies;++i)if(world.enemy[i].object==o)return &world.enemy[i];
    return nullptr;
}
npc::TargetKind KindOf(const Enemy& e,const float* pos) noexcept {
    if(e.aim[1]-pos[1]>kAirAbove)return npc::TargetKind::air;
    return e.points>=kLargePoints ? npc::TargetKind::large : npc::TargetKind::light;
}
// The friends (the player among them) but `self`, for the shot's test.
int FriendsBut(const void* self,npc::Friend* out) noexcept {
    int n=0;
    for(int i=0;i<world.friends;++i)if(world.frObject[i]!=self)out[n++]=world.fr[i];
    return n;
}
bool FriendNear(const void* self,const float* at,float r) noexcept {
    for(int i=0;i<world.friends;++i)if(world.frObject[i]!=self && npc::Dist(world.fr[i].pos,at)<r)return true;
    return false;
}

// --- The block's writes ---
void Move(unsigned char* h,const float* dir,float magnitude) noexcept {
    float x,z;
    npc::LocalMove(At<float>(h,kViewYaw),dir,npc::Clamp(magnitude,0.0f,1.0f),&x,&z);
    Put<float>(h,kMoveX,x);Put<float>(h,kMoveY,0.0f);Put<float>(h,kMoveZ,z);Put<float>(h,kMoveW,1.0f);
}
void Stand(unsigned char* h) noexcept { Put<float>(h,kMoveX,0.0f);Put<float>(h,kMoveY,0.0f);Put<float>(h,kMoveZ,0.0f);Put<float>(h,kMoveW,1.0f); }
void MoveTo(unsigned char* h,const float* pos,const float* to,float stop) noexcept {
    const ULONGLONG ms=GameMs();
    Soldier* const soldier=Entry(h,ms);
    float waypoint[3],dir[3];
    if(!soldier || GroundNavigate(soldier->navigation,pos,to,stop,ms,waypoint)!=npc::navigation::Result::moving ||
       !npc::HorizDir(pos,waypoint,dir)){Stand(h);return;}
    const float d=npc::Horiz(pos,waypoint);
    Move(h,dir,d/6.0f+0.3f);
}
void Look(unsigned char* h,const float* dir) noexcept {
    float d[2];
    if(!npc::AimDelta(At<float>(h,kAimPitch),At<float>(h,kAimYaw),dir,1.0f,kLookMost,d))return;
    Put<float>(h,kLookPitch,d[0]);Put<float>(h,kLookYaw,d[1]);
}

struct Plan { const char* move; bool fire; int arm; };

// The weapon for the target: picked through d82+i (only the first three can be: §3.5), the trigger off while the
// pick goes through; a pick the soldier never takes turns the picking off for it (the RE's M claim proven wrong there).
int ChooseArm(Soldier& s,unsigned char* h,const Arms& a,const Enemy* t,const float* eye,const float* pos) noexcept {
    if(!t || a.n==0)return a.held[0];
    const float dist=npc::Dist(eye,t->aim);
    const auto kind=KindOf(*t,pos);
    const bool friendNear=FriendNear(h,t->aim,kFriendNearTarget);
    const int pick=npc::PickArm(a.arm,a.n<3 ? a.n : 3,a.held[0],dist,kind,friendNear);
    // Only d82..d84 exist. An inaccessible fourth weapon must not hide a usable second one, but an already held
    // fourth weapon can still win the same hysteresis comparison and be fired without selecting it again.
    if(a.held[0]>=3 && a.held[0]<a.n) {
        const float held=npc::ArmScore(a.arm[a.held[0]],dist,kind,friendNear);
        if(held>0.0f && (pick<0 || held*1.25f>=npc::ArmScore(a.arm[pick],dist,kind,friendNear)))return a.held[0];
    }
    if(s.wantArm>=0) {
        if(a.held[0]==s.wantArm){s.wantArm=-1;s.armMiss=0;}
        else if(++s.armMiss>kArmMissFrames) {
            s.noSwitch=true;
            Log("NPCAI soldier %p: weapon %d picked for %d frames, still holding %d: weapon picking off for it",h,s.wantArm,kArmMissFrames,a.held[0]);
            s.wantArm=-1;
        }
    }
    if(pick<0 || pick==a.held[0] || pick>=3 || s.noSwitch || !Cfg().npcWeaponSwitch)return a.held[0];
    h[kPickWeapon+pick]=1;
    if(s.wantArm!=pick){s.wantArm=pick;s.armMiss=0;}
    return -1;   // switching: no shot this frame
}

// The trigger for target `t` with arm `i`: ready, in its true reach, the look on it, the map open, the round's line and
// blast clear of friends.
bool ShotOk(Soldier& s,unsigned char* h,const Arms& a,int i,const Enemy& t,const float* eye) noexcept {
    if(i<0 || i>=a.n || !a.arm[i].ready)return false;
    const float dist=npc::Dist(eye,t.aim);
    if(dist>a.arm[i].reach || dist<npc::MinRange(a.arm[i]))return false;
    const float dir[3]={t.aim[0]-eye[0],t.aim[1]-eye[1],t.aim[2]-eye[2]};
    const float cone=std::atan(kHitRadius/(dist>1.0f ? dist : 1.0f));
    if(npc::AimError(At<float>(h,kViewPitch),At<float>(h,kViewYaw),dir)>(cone>kFireConeMin ? cone : kFireConeMin))return false;
    if(GameFrame()-s.losFrame>=kLosFrames || s.losObject!=t.object) {
        float hit[3];
        const float wall=MapRay(eye,t.aim,hit);
        s.los=wall<0.0f || wall>dist-2.0f;s.losFrame=GameFrame();s.losObject=t.object;
    }
    if(!s.los)return false;
    npc::Friend fr[kMaxFriends];
    const int n=FriendsBut(h,fr);
    return npc::ShotClear(eye,t.aim,kSpread,a.arm[i].blast,fr,n);
}

// A pulled trigger taken off a shot at `t` that would hit a friend. Each hand is tested with the weapon its own WeaponSet
// holds (d70: set 0, d71: the Fencer's set 1): a rifle in one hand is never vetoed for the rocket launcher carried in the
// list. Only a hand whose weapon is not known is tested with the largest blast and the longest reach it carries.
float LargestBlast(const Arms& a) noexcept {
    float b=0.0f;
    for(int i=0;i<a.n;++i)if(a.arm[i].blast>b)b=a.arm[i].blast;
    return b;
}
float LongestReach(const Arms& a) noexcept {
    float reach=0.0f;
    for(int i=0;i<a.n;++i)if(a.arm[i].reach>reach)reach=a.arm[i].reach;
    return reach;
}
// Whether a round of `blast` at `t` (none: along the look, as far as `reach` or the ground) keeps off every friend.
bool ShotSafe(const unsigned char* h,const Enemy* t,const float* eye,float blast,float reach) noexcept {
    npc::Friend fr[kMaxFriends];
    const int n=FriendsBut(h,fr);
    if(t)return npc::ShotClear(eye,t->aim,kSpread,blast,fr,n);
    if(reach<=0.0f)return false;
    float dir[3];
    const float pitch=At<float>(h,kViewPitch),yaw=At<float>(h,kViewYaw);
    dir[0]=std::sin(yaw)*std::cos(pitch);dir[1]=-std::sin(pitch);dir[2]=std::cos(yaw)*std::cos(pitch);
    float to[3]={eye[0]+dir[0]*reach,eye[1]+dir[1]*reach,eye[2]+dir[2]*reach},hit[3];
    if(MapRay(eye,to,hit)>=0.0f)std::memcpy(to,hit,12);
    return npc::ShotClear(eye,to,kSpread,blast,fr,n);
}
void Veto(unsigned char* h,const Enemy* t,const float* eye,const Arms& a) noexcept {
    for(int k=0;k<kHands;++k) {
        if(!h[kTrigger+k])continue;
        const int w=a.held[k];
        const bool known=w>=0 && w<a.n;
        if(!ShotSafe(h,t,eye,known ? a.arm[w].blast : LargestBlast(a),known ? a.arm[w].reach : LongestReach(a)))h[kTrigger+k]=0;
    }
}

// A script's unit (§4.3): its moves and target the stock AI's; the trigger taken off a shot that would hit a friend,
// the weapon picked for the stock target (not while the held one is unknown).
Plan Scripted(Soldier& s,unsigned char* h,const Arms& a,const float* eye,const float* pos) noexcept {
    Plan p{"stock",false,a.held[0]};
    const Enemy* t=StockTarget(h);
    if(t && a.held[0]>=0)p.arm=ChooseArm(s,h,a,t,eye,pos);
    if(p.arm<0 && a.held[0]>=0)h[kTrigger]=h[kTrigger+1]=0;   // switching
    // Each hand against its own WeaponSet's weapon: d71 is the Fencer's independently held second one.
    Veto(h,t,eye,a);
    p.fire=h[kTrigger]!=0;
    return p;
}

// The player a soldier fights for: the one who recruited its squad (this machine's or another's: IsPlayer holds for a
// remote player too), else this machine's player. `look` is known for this machine's player alone (its camera); for
// another machine's it is nullptr and "behind them" is taken from the threat instead.
struct Served { const float* at; const float* look; };
Served ServedBy(npc::Control control,const unsigned char* root) noexcept {
    if(control==npc::Control::recruited && root && root!=PlayerHuman())return Served{Pos(root),nullptr};
    if(!world.player)return Served{nullptr,nullptr};
    return Served{world.playerAt,world.lookOk ? world.look : nullptr};
}

// Where it fights from: the combat spot (re-picked every kSpotMs) no farther than the leash from its anchor, flanking
// the target off the line from the player it fights for.
void Spot(Soldier& s,const float* pos,const float* aim,const float* anchor,const float* servedAt,float engage,float leash,
          ULONGLONG ms) noexcept {
    if(s.spotSet && ms-s.spotAt<kSpotMs)return;
    npc::CombatSpot(pos,aim,servedAt,engage,Cfg().npcFlankDeg*npc::kPi/180.0f,s.spot);
    const float off=npc::Horiz(anchor,s.spot);
    if(off>leash) {
        const float k=leash/off;
        s.spot[0]=anchor[0]+(s.spot[0]-anchor[0])*k;s.spot[2]=anchor[2]+(s.spot[2]-anchor[2])*k;
    }
    s.spotSet=true;s.spotAt=ms;
}

// Crowded (§3.6): back off, side-step or roll; true when it moved.
bool Evade(Soldier& s,unsigned char* h,const SoldierClass& c,const float* pos,ULONGLONG ms,const char** what) noexcept {
    if(!Cfg().npcEvade)return false;
    npc::Threat threats[64];int n=0;
    const float reach=Cfg().npcDangerRange*2.0f;
    for(int i=0;i<world.enemies && n<64;++i)
        if(npc::Horiz(pos,world.enemy[i].aim)<reach)
            threats[n++]=npc::Threat{{world.enemy[i].aim[0],world.enemy[i].aim[1],world.enemy[i].aim[2]},world.enemy[i].points>=kLargePoints ? 3.0f : 1.0f};
    const bool rollReady=c.rolls && (At<std::uint32_t>(h,kControlMask)&kMaskRoll) && ms-s.rollAt>=static_cast<ULONGLONG>(Cfg().npcRollSec*1000.0f);
    const npc::Evade e=npc::CrowdResponse(pos,threats,n,Cfg().npcDangerRange,Cfg().npcCrowd,Cfg().npcGrabRange,rollReady);
    if(e.move==npc::Move::hold)return false;
    if(e.move==npc::Move::roll) {
        float x,z,rx,rz;
        npc::LocalMove(At<float>(h,kViewYaw),e.dir,1.0f,&x,&z);
        npc::RollStick(x,z,0.6f,&rx,&rz);
        Put<float>(h,kMoveX,rx);Put<float>(h,kMoveY,0.0f);Put<float>(h,kMoveZ,rz);Put<float>(h,kMoveW,1.0f);
        h[kJumpPress]=1;
        s.rollAt=ms;*what="roll";
        return true;
    }
    Move(h,e.dir,1.0f);
    *what=e.move==npc::Move::back ? "back off" : "side-step";
    return true;
}

// Hurt (§3.6, §3.7): behind the player it fights for (out of their lane, a wall between it and the nearest threat when
// one of a few points has one). Behind: against their look, or, their look not known (another machine's player), on
// their side away from the nearest threat; neither known: at the player.
constexpr ULONGLONG kFallMs=1000;
bool FallBack(Soldier& sol,unsigned char* h,const float* pos,const Served& served,ULONGLONG ms) noexcept {
    const float hpMax=At<float>(h,kHumanHpMax),hp=At<float>(h,kHumanHp);
    if(!(hpMax>0.0f) || !(hp/hpMax<Cfg().npcRetreatHp) || !served.at)return false;
    if(sol.fallAt && ms-sol.fallAt<kFallMs){MoveTo(h,pos,sol.fallTo,kSpotStop);return true;}
    const Enemy* nearest=nullptr;float nd=1e30f;
    for(int i=0;i<world.enemies;++i){const float d=npc::Horiz(pos,world.enemy[i].aim);if(d<nd){nd=d;nearest=&world.enemy[i];}}
    float back[3]={0.0f,0.0f,0.0f};
    if(served.look){back[0]=-served.look[0];back[2]=-served.look[2];}
    else if(nearest){back[0]=served.at[0]-nearest->aim[0];back[2]=served.at[2]-nearest->aim[2];}
    const float l=std::sqrt(back[0]*back[0]+back[2]*back[2]);
    if(l<1e-3f)back[0]=back[2]=0.0f;else{back[0]/=l;back[2]/=l;}
    const float* const at=served.at;
    float to[3]={at[0]+back[0]*kBehindPlayer,at[1],at[2]+back[2]*kBehindPlayer};
    if(nearest && l>=1e-3f) {
        // A few points on the half circle behind the player: the first one the nearest threat cannot see.
        for(int k=-2;k<=2;++k) {
            const float a=static_cast<float>(k)*0.5f,c=std::cos(a),s=std::sin(a);
            const float p[3]={at[0]+(back[0]*c-back[2]*s)*kBehindPlayer,at[1]+kEye,
                              at[2]+(back[0]*s+back[2]*c)*kBehindPlayer};
            float hit[3];
            if(MapRay(nearest->aim,p,hit)>=0.0f){to[0]=p[0];to[2]=p[2];break;}
        }
    }
    std::memcpy(sol.fallTo,to,12);sol.fallAt=ms;
    MoveTo(h,pos,to,kSpotStop);
    return true;
}

// --- The squads (§5, §6) ---
// One entry a squad, keyed by its top NPC (the soldier at the top of the +0x548 chain below the player, or with no
// leader at all): its order from the map, how many members its soldiers' Think counted last frame, its control, its
// dismissal's cooldown. The members are never stored: the stock chain is read again each frame.
constexpr int kMaxSquads=64;
constexpr ULONGLONG kSquadSeenMs=500;
constexpr ULONGLONG kDismissedGoneMs=60000;
constexpr ULONGLONG kOrderSeenMs=kSquadSeenMs; // same freshness interval as visible/selectable squad rows
constexpr std::size_t kAutoFollow=0x540;
struct Squad {
    ObjRef top;
    ULONGLONG seen,frame;
    int alive,counting,cls;
    npc::Control control;
    Command cmd;
    npc::Lead cmdLead;         // the lead `cmd` was given under; another lead drops it (npc::LeadOf)
    std::uint8_t autoFollow;   // +0x540 before a dismissal (put back when its cooldown ends)
    bool dismissed;
    npc::ScriptWatch script;   // the end of a script's control over it (§4.4)
    npc::formation::Shape guardShape;   // its defence on a guard point (formation.h; the ini's NpcGuardFormation)
    float guardFwd[2];         // the way the guard faces: from where the player gave it towards the point
    bool routeActive=false,routeCancelled=false;
    float routePoint[3]{},routeArrival=1.0f;
    npc::Lead routeLead{};
    ObjRef commandFocus{}; // one squad's explicit attack target; never another player's global marker
};
Squad squads[kMaxSquads]{};
npc::Cooldowns<kMaxSquads> cooldowns;
int dismissedCount=0;   // squads cooling down now (their +0x540 to give back)
std::uint32_t SquadKey(const void* top) noexcept {
    const auto a=reinterpret_cast<std::uintptr_t>(top);
    return static_cast<std::uint32_t>(a^(a>>32));
}

// The top NPC of `h`'s squad: up +0x548 as far as an NPC leads (the player above it ends the walk).
unsigned char* TopNpc(unsigned char* h) noexcept {
    unsigned char* top=h;
    for(int i=0;i<kMaxRoot;++i) {
        const auto up=At<unsigned char*>(top,kLeader);
        if(!up || !Readable(up,kLeader+8) || IsAnyPlayer(up) || up[kDead] || !IsSoldierClass(up))break;
        top=up;
    }
    return top;
}

Squad* FindSquad(const void* top) noexcept {
    for(auto& q:squads)if(q.top.Is(top))return &q;
    return nullptr;
}

// Counted by every member's Think; the top's own Think sets its class and control.
Squad* SeeSquad(unsigned char* top,const unsigned char* h,int cls,npc::Control control,ULONGLONG ms) noexcept {
    Squad* q=FindSquad(top);
    if(!q) {
        // A slot no squad has been counted in lately; never a dismissed one's whose cooldown has yet to put its +0x540
        // back (its soldiers may only be out of this machine's update for a while).
        for(auto& e:squads)if(!e.top || (ms-e.seen>kSquadSeenMs*4 && (!e.dismissed || ms-e.seen>kDismissedGoneMs))){q=&e;break;}
        if(!q)return nullptr;
        if(q->dismissed)--dismissedCount;
        npcmark::Assign(q->commandFocus,{});*q=Squad{};q->top=ObjRef::Of(top);q->cls=cls;
    }
    if(h==top)q->cls=cls;
    ++q->counting;
    q->seen=ms;
    if(q->frame!=GameFrame()) {
        q->alive=q->counting-1;q->counting=1;q->frame=GameFrame();
        // Its control from its top's own fields (the top may be another machine's soldier, whose Think does not get here).
        const unsigned char* const root=RootLeader(top);
        q->control=h==top ? control : ControlOf(top,root);
        if(q->routeActive && (npc::Scripted(q->control) || npc::LeadOf(q->control)!=q->routeLead)) {
            q->routeActive=false;q->routeCancelled=true;
        }
        if(q->cmd.order!=Order::none && npc::LeadOf(q->control)!=q->cmdLead) {
            Log("NPCAI squad %p: led by %s now, its order dropped",top,ControlName(q->control));
            q->cmd=Command{Order::none,{0.0f,0.0f,0.0f}};
            npcmark::Assign(q->commandFocus,{});
        }
        // The script let it go (its route ended, it was unfollowed, its position freed) and has not taken it back
        // within ScriptNpcSettleSec: a squad of the plugin's now; with ScriptNpcRecruit the player may recruit it.
        const bool held=Routed(top) || (root && !IsAnyPlayer(root) && Routed(root)) || (At<std::uint32_t>(top,kObjectFlags)&kFixPosition);
        if(npc::Step(q->script,held,ms,static_cast<std::uint64_t>(Cfg().scriptNpcSettleSec*1000.0f))) {
            const bool open=!InSession() && IsOnlineAuthority(top) &&
                            Cfg().scriptNpcRecruit && !q->dismissed && !top[kAutoFollow];
            if(open)top[kAutoFollow]=1;
            Log("NPCAI squad %p: the script let it go (%s): the plugin's now%s",top,ControlName(q->control),open ? ", recruitable" : "");
        }
    }
    // The dismissal's cooldown over: the stock "joins a player who comes near" back (§5.4).
    if(q->dismissed && cooldowns.Ready(SquadKey(top),ms)) {
        top[kAutoFollow]=q->autoFollow;q->dismissed=false;--dismissedCount;
        Log("NPCAI squad %p: its cooldown is over, it may be recruited again",top);
    }
    return q;
}

// What the squad's order makes of a member's anchor and reach (§6.2): guard holds it within NpcGuardRadius of the point,
// engage lets it fight NpcFreeRange round the point it was given at.
struct Orders { const float* anchor; float leash; bool hold; };
Orders OrdersOf(const Squad* q,const float* anchor) noexcept {
    Orders o{anchor,Cfg().npcLeash,false};
    if(!q)return o;
    if(q->routeActive){o.anchor=Pos(static_cast<const unsigned char*>(q->top.obj));return o;}
    if(mapcmd::PointOrder(q->cmd.order)){o.anchor=q->cmd.at;o.leash=Cfg().npcGuardRadius;o.hold=true;}
    else if(q->cmd.order==Order::engage || q->cmd.order==Order::focus){o.anchor=q->cmd.at;o.leash=Cfg().npcFreeRange;}
    return o;
}

// --- Boarding (§7) ---
// The stock has no way for a soldier to board (the scripts' RideVehicle seats a DummyVehicleRider). The human side's
// RideVehicle 0x5765E0(human, shared_ptr<vehicle>* by value, seat) does all of it (off the old vehicle, +0x1540/+0x1548/
// +0x1550, SeatRide with force 0, the ride state) and releases one strong reference at its end (0x57690D), so the caller
// takes one first. It checks neither team, nor class mask, nor reach: the plugin does (a free seat whose masks take the
// soldier's class, the seat's riding point within reach). Driver input is preserved by the narrow native
// seat-clear hook; real soldiers occupy driver seats as well as gunner and passenger seats.
constexpr unsigned kRideVehicle=0x5765E0;
constexpr std::size_t kHumanMask=0x31C,kSeatClass=0x30,kSeatEnable=0x34,kHumanSeat=0x1540,kHumanRiding=0x1548;
constexpr ULONGLONG kBoardMs=20000;        // a board order not done in this long is dropped
const unsigned char kRideSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x48};
const unsigned char kRideReleaseSig[]={0x49,0x8B,0x5E,0x08,0x48,0x85,0xDB,0x74,0x27,0x8B,0xC7,0xF0,0x0F,0xC1,0x43,0x08};   // 0x57690D
bool rideOk=false;
struct SharedRef { void* obj; void* ctrl; };
using RideFn=void(__fastcall*)(void*,SharedRef*,int);

bool OwnsNpcSeatInput(unsigned char* seat) noexcept {
    if(!Cfg().enabled || !Cfg().customNpcAi || !Cfg().npcBoarding || !Readable(seat,kSeatRiderCtrl+8) ||
       SeatRider(seat)!=Rider::other)return false;
    auto* h=At<unsigned char*>(seat,kSeatRider);
    if(!IsSoldierClass(h) || IsAnyPlayer(h) || h[kDead] || !IsOnlineAuthority(h) ||
       At<const void*>(h,kHumanSeat)!=seat)return false;
    auto* v=At<unsigned char*>(h,kHumanRiding);
    return Readable(v,kSeatCount+8) && !v[kDead] && SeatCount(v)>0 && SeatAt(v,0)==seat && IsOnlineAuthority(v);
}

bool SeatTakes(const unsigned char* v,unsigned i,const unsigned char* h) noexcept {
    const unsigned char* seat=SeatAt(const_cast<unsigned char*>(v),i);
    if(SeatRider(seat)!=Rider::none || (i==0 && !RealDriverNativeReady()))return false;
    return (At<std::uint32_t>(h,kHumanMask)&At<std::uint32_t>(seat,kSeatClass)&At<std::uint32_t>(seat,kSeatEnable))!=0;
}

// Walks to its seat's riding point and boards there; false once the order is over (done, gone, timed out).
bool Board(Soldier& s,unsigned char* h,const float* pos,ULONGLONG ms) noexcept {
    auto v=static_cast<unsigned char*>(const_cast<void*>(s.boardV.obj));
    if(!v || !Readable(v,kSeatCount+8) || !s.boardV.Is(v) || v[kDead] || ms-s.boardAt>kBoardMs || s.boardSeat<0 || static_cast<unsigned>(s.boardSeat)>=SeatCount(v) ||
       !IsOnlineAuthority(h) || !OnlineMaySeatNpc(v) || !SeatTakes(v,static_cast<unsigned>(s.boardSeat),h)) {
        Log("NPCAI soldier %p: board order dropped (%s)",h,ms-s.boardAt>kBoardMs ? "too long" : "the seat or the vehicle is gone");
        s.boardV=ObjRef{};
        return false;
    }
    float at[3],reach=0.0f;
    if(!SeatPoint(v,static_cast<unsigned>(s.boardSeat),at,&reach) || !std::isfinite(reach) || reach<=0.0f) {
        s.boardV=ObjRef{};
        return false;
    }
    // RideVehicle does not check distance itself. Match the seat's real spherical reach, including height;
    // horizontal distance alone would seat a soldier standing on a different floor.
    if(npc::Dist(pos,at)>=reach) {
        // The native riding point is a trigger centre, not necessarily a walkable foot position.
        // Project only within that trigger's vertical reach, never onto a roof or another floor.
        const float above[3]={at[0],at[1]+0.3f,at[2]},below[3]={at[0],at[1]-reach,at[2]};
        float walkAt[3];
        if(MapFloorRay(above,below,walkAt)<0.0f || npc::Dist(walkAt,at)>=reach) {
            Stand(h); // nextThink already wrote native movement; a rejected entrance must hold, not reuse that intent.
            return true;
        }
        MoveTo(h,pos,walkAt,0.5f);
        return true;
    }
    auto ctrl=At<unsigned char*>(v,kSelfCtrl);
    if(!rideOk || !ctrl || At<std::int32_t>(ctrl,8)==0){s.boardV=ObjRef{};return false;}
    // The by-value shared_ptr's reference: RideVehicle (callee-destroyed argument) lets go of it on every return, the
    // seated or refused path at 0x57690D and the already-in-that-seat one through 0x8DF40 (0x576723), so none leaks.
    if(s.boardSeat==0 && !PrepareNpcVehicle(v,false)){s.boardV=ObjRef{};return false;}
    _InterlockedIncrement(reinterpret_cast<volatile long*>(ctrl+8));
    SharedRef ref{v,ctrl};
    reinterpret_cast<RideFn>(image+kRideVehicle)(h,&ref,s.boardSeat);
    if(!HumanOnFoot(h))AnnounceNpcBoarding(h);
    Log("NPCAI soldier %p boards v=%p seat %d: %s",h,v,s.boardSeat,HumanOnFoot(h) ? "refused by the stock ride" : "seated");
    s.boardV=ObjRef{};
    return true;
}

bool FormationMove(Soldier& s,unsigned char* h,const float* pos,const Squad* q,const unsigned char* root,ULONGLONG ms,
                   const char** move,float* left=nullptr) noexcept;
bool PickUp(Soldier& s,unsigned char* h,const float* pos) noexcept;

// A move / attack-move (the user, 2026-10-09: "如果npc在打怪，就没办法移动了"): the soldier walks to its place by the
// order's point (its formation slot, else the point itself) -- the player's command, over its own dodging, falling
// back and combat spot (Drive takes this first; its look and trigger stay on its target). Its squad's top there: the
// order is a guard of the point from then on (mapcmd_logic.h Arrive), the squad holds it and fights what comes.
constexpr float kPointArrive=6.0f;   // m: the top this near its place has reached the order's point
bool Pursue(Soldier& s,unsigned char* h,const float* pos,Squad* q,const unsigned char* root,ULONGLONG ms,const char** move) noexcept {
    float left=0.0f;
    if(!FormationMove(s,h,pos,q,root,ms,move,&left)) {
        left=npc::Horiz(pos,q->cmd.at);
        MoveTo(h,pos,q->cmd.at,Cfg().npcGuardRadius*0.5f);
    }
    *move=q->cmd.order==Order::move ? "move order" : "attack-move";
    if(q->top.Is(h)) {
        const Order was=q->cmd.order;
        q->cmd.order=mapcmd::Arrive(q->cmd.order,left,kPointArrive+(q->guardShape==npc::formation::Shape::stock ? Cfg().npcGuardRadius*0.5f : 0.0f));
        if(q->cmd.order!=was)Log("NPCAI squad %p: at its %s point (%.0f,%.0f,%.0f): guarding it",h,was==Order::move ? "move" : "attack-move",
                                 q->cmd.at[0],q->cmd.at[1],q->cmd.at[2]);
    }
    return true;
}

Plan Drive(Soldier& s,unsigned char* h,const SoldierClass& c,const Arms& a,const unsigned char* root,const float* eye,
           const float* pos,Squad* q,ULONGLONG ms) noexcept {
    Plan p{"stock",false,a.held[0]};
    // Its anchor: the player it follows (the one who recruited it, whichever machine's), its NPC leader, the spot it
    // was free at; a map order's point over them.
    const Served served=ServedBy(s.control,root);
    Orders o=OrdersOf(q,s.control==npc::Control::recruited && served.at ? served.at :
                         s.control==npc::Control::squad && root ? Pos(root) : s.home);
    // On the way under a move / attack-move it looks for targets round itself (its leash), not round the point ahead.
    const mapcmd::Pursuit pursuit=mapcmd::PursuitOf(q ? q->cmd.order : Order::none);
    if(pursuit.forced || pursuit.fightFirst){o.anchor=pos;o.leash=Cfg().npcLeash;o.hold=false;}
    const float* anchor=o.anchor;
    const float engage=npc::EngageRange(a.arm,a.n,Cfg().npcEngageShare);
    Pick t=engage>0.0f ? PickTarget(s,eye,anchor,o.leash+engage) : Pick{nullptr,0.0f};
    // The mark first (§6.3): always for a squad told to focus on it, else when within its reach plus its leash.
    const Enemy* commandTarget=nullptr;
    if(q && q->cmd.order==Order::focus && npcmark::Alive(q->commandFocus)) {
        for(int i=0;i<world.enemies;++i)if(q->commandFocus.Is(world.enemy[i].object)){commandTarget=&world.enemy[i];break;}
    }
    if(const Enemy* m=commandTarget ? commandTarget : MarkedEnemy()) {
        float reach=0.0f;
        for(int i=0;i<a.n;++i)if(a.arm[i].reach>reach)reach=a.arm[i].reach;
        const bool focus=commandTarget!=nullptr;
        if(focus || npc::MarkInReach(eye,m->aim,reach,o.leash))t=Pick{m,npc::Dist(eye,m->aim)};
    }
    s.target=t.e ? ObjRef::Of(t.e->object) : ObjRef{};
    const unsigned mask=At<std::uint32_t>(h,kControlMask);
    if(a.held[0]<0) {
        // The held weapon not known (§3.5's M claim does not hold for this soldier): its look and trigger stay the stock
        // AI's, vetoed against friends (an unknown hand with its largest blast); the plugin only moves it.
        Veto(h,StockTarget(h),eye,a);
        p.fire=h[kTrigger]!=0;
    } else if(t.e) {
        p.arm=ChooseArm(s,h,a,t.e,eye,pos);
        const float dir[3]={t.e->aim[0]-eye[0],t.e->aim[1]-eye[1],t.e->aim[2]-eye[2]};
        if(mask&kMaskLook)Look(h,dir);
        p.fire=ShotOk(s,h,a,p.arm,*t.e,eye) && (mask&kMaskTrigger);
        h[kTrigger]=p.fire ? 1 : 0;
        Veto(h,t.e,eye,a);   // the second hand (d71) the stock may have pulled, with its own weapon
    } else {
        s.spotSet=false;
        h[kTrigger]=0;
        Veto(h,StockTarget(h),eye,a);
    }
    if(!(mask&kMaskMove))return p;
    // Its moves, the first that applies: the player's move order before anything of its own; an attack-move's once it
    // has nothing to fight.
    if((pursuit.forced || (pursuit.fightFirst && !t.e)) && Pursue(s,h,pos,q,root,ms,&p.move))return p;
    if(Evade(s,h,c,pos,ms,&p.move))return p;
    if(s.boardV && Board(s,h,pos,ms)){p.move="to its seat";return p;}
    if(FallBack(s,h,pos,served,ms)){p.move="fall back";return p;}
    float out[3];
    if(world.lane && npc::LaneEscape(world.laneOf,pos,out)){Move(h,out,1.0f);p.move="out of the lane";return p;}
    if(s.pickUnit && s.pickFrame==GameFrame() && PickUp(s,h,pos)){p.move="to a box";return p;}
    if(!t.e && q && q->routeActive) {
        // The route is only the leader's quiet movement. Never apply guard-radius/formation offsets
        // to its short waypoint, or rewrite followers' native leader links and follow inputs.
        if(q->top.Is(h)){MoveTo(h,pos,q->routePoint,q->routeArrival);p.move="support route";}
        else p.move="support follow";
        return p;
    }
    if(!t.e && FormationMove(s,h,pos,q,root,ms,&p.move))return p;
    if(t.e) {
        Spot(s,pos,t.e->aim,anchor,served.at,engage,o.leash,ms);
        MoveTo(h,pos,s.spot,kSpotStop);
        p.move="combat spot";
    } else if(o.hold) {   // within its post's radius it stands (the stock follow of a recruited squad would pull it away)
        MoveTo(h,pos,anchor,npc::Horiz(pos,anchor)>o.leash ? o.leash*0.5f : 1e9f);
        p.move="at its post";
    }
    return p;
}

void Think(unsigned char* h,int cls) noexcept {
    if(IsAnyPlayer(h) || h[kDead] || !IsOnlineAuthority(h))return;   // only the NPCs whose AI this machine runs (online_authority.h)
    const std::int32_t team=At<std::int32_t>(h,kTeam);
    if(team!=0 && team!=kTeamFriend) {
        // An explicitly assigned mission crew may follow its vehicle's scripted faction change.
        // Only complete that already-authorized boarding; do not take over ordinary enemy AI.
        for(auto& s:soldiers)if(s.ref.Is(h) && s.boardV){Board(s,h,Pos(h),GameMs());break;}
        return;
    }
    const ULONGLONG ms=GameMs();
    SeeFrame(h);
    if(world.frame!=GameFrame())Gather(team);   // 0 and 2 have the same enemies (TeamManager's table, docs/swarm-team-re.md)
    const unsigned char* root=RootLeader(h);
    const npc::Control control=ControlOf(h,root);
    // Counted in its squad whether it walks or rides (a squad in a vehicle is listed and takes dismount, §7).
    Squad* const q=SeeSquad(TopNpc(h),h,cls,control,ms);
    if(!HumanOnFoot(h)) {   // riding: its vehicle's (a gunner seat: NpcGunnersInput); a board order done
        if(Soldier* r=Entry(h,ms)){r->seen=ms;r->boardV=ObjRef{};}
        return;
    }
    if(!(h[kListFlags]&kInAiList)) {
        if(ms-listLoggedAt>10000){listLoggedAt=ms;Log("NPCAI soldier %p: not in the AI list (+0x1A bit 3): its block is cleared before use, left stock",h);}
        return;
    }
    Soldier* const s=Entry(h,ms);
    if(!s)return;
    s->seen=ms;
    const float* pos=Pos(h);
    const float eye[3]={pos[0],pos[1]+kEye,pos[2]};
    if(!s->controlSet || control!=s->control) {
        if(s->controlSet && Cfg().debug)Log("NPCAI soldier %p: %s -> %s",h,ControlName(s->control),ControlName(control));
        s->control=control;s->controlSet=true;
        std::memcpy(s->home,pos,12);s->homeSet=true;s->spotSet=false;
    }
    if(s->boardV && Board(*s,h,pos,ms))return; // boarding does not depend on readable hand weapons
    const Arms a=ArmsOf(h);
    if(a.n==0) {   // its weapons cannot be read: nothing to fight with that the plugin knows of, so all of it stays stock
        if(Cfg().debug && ms-s->loggedAt>kLogMs){s->loggedAt=ms;Log("NPCAI %s %p: weapons unreadable, left stock",kSoldiers[cls].name,h);}
        return;
    }
    const Plan p=npc::Scripted(control) ? Scripted(*s,h,a,eye,pos) : Drive(*s,h,kSoldiers[cls],a,root,eye,pos,q,ms);
    if(Cfg().debug && ms-s->loggedAt>kLogMs) {
        s->loggedAt=ms;
        Log("NPCAI %s %p %s pos=(%.0f,%.0f,%.0f) arms=%d held=%d arm=%d reach=%.0f target=%p move=%s fire=%d",kSoldiers[cls].name,h,
            ControlName(control),pos[0],pos[1],pos[2],a.n,a.held[0],p.arm,p.arm>=0 && p.arm<a.n ? a.arm[p.arm].reach : 0.0f,s->target.obj,
            p.move,p.fire);
    }
}

constexpr ULONGLONG kFaultLogMs=10000;
unsigned faults=0;ULONGLONG faultAt=0;
int Fault(const EXCEPTION_POINTERS* e) noexcept {
    ++faults;
    const ULONGLONG now=GetTickCount64();
    if(!faultAt || now-faultAt>=kFaultLogMs) {
        faultAt=now;
        const auto at=static_cast<const unsigned char*>(e->ExceptionRecord->ExceptionAddress);
        Log("FAULT npc ai: %08lX at %p (%u so far; that soldier's frame is left stock)",e->ExceptionRecord->ExceptionCode,at,faults);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// --- The leader's death (§5.3) ---
// The stock (0x596C1B..0x596D91): a follower whose leader is dead (and comes back by no auto-resurrection) unfollows,
// each one alone. Before that code runs for any of them (the first follower's Think of the frame), the plugin picks
// the next leader among the live followers (PickLeader), puts it under the dead one's own leader (the player for a
// recruited squad) and the others under it; a squad down to fewer than NpcSquadMin joins the nearest squad within
// NpcSquadJoinRange with room (JoinSquad). Every change through the stock SetFollow and its replication (vslot 39).
constexpr std::size_t kFollowers=0x550,kListNodeNext=0x0,kListNodeObject=0x10,kFollowerCount=0x558;
constexpr std::uint32_t kAutoResurrect=0x20000;
constexpr unsigned kSetFollow=0x54EC50;
constexpr std::size_t kSlotNetFollow=39;
constexpr unsigned kNetFollow=0x59C3D0;
const unsigned char kSetFollowSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,
                                     0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x30,0x45,0x0F,0xB6,0xF0,0x48,0x8B,0xFA,0x48,0x8B,0xD9,
                                     0xE8,0xC7,0xF0,0xFF,0xFF};   // ...movzx r14d,r8b; mov rdi,rdx; mov rbx,rcx; call Unfollow 0x54DD40
using SetFollowFn=void(__fastcall*)(void*,void*,bool);
bool followOk=false;
constexpr int kMaxSquad=16;

// Follow `leader` (nullptr: none) the stock way, and tell the other machines (the soldier's vslot 39, 0x59C3D0).
void Follow(unsigned char* h,unsigned char* leader) noexcept {
    reinterpret_cast<SetFollowFn>(image+kSetFollow)(h,leader,false);
    const auto vt=At<void* const*>(h,0);
    if(vt[kSlotNetFollow]==image+kNetFollow)reinterpret_cast<SetFollowFn>(vt[kSlotNetFollow])(h,leader,false);
}

// The live NPC soldiers following `leader` (its +0x550 list), at most `most`.
int Followers(const unsigned char* leader,unsigned char** out,int most) noexcept {
    const auto head=At<const unsigned char*>(leader,kFollowers);
    if(!Readable(head,0x18))return 0;
    int n=0,guard=0;
    for(auto node=At<const unsigned char*>(head,kListNodeNext);node && node!=head && n<most && guard<64;
        node=At<const unsigned char*>(node,kListNodeNext),++guard) {
        if(!Readable(node,0x18))break;
        const auto o=At<unsigned char*>(node,kListNodeObject);
        if(o && Readable(o,kDead+1) && !o[kDead] && IsSoldierClass(o) && !IsAnyPlayer(o))out[n++]=o;
    }
    return n;
}

// The squads a leaderless remnant may join: live, unscripted NPC soldiers leading followers (or recruited by the
// player), but `except`; their places and sizes.
int OtherSquads(const unsigned char* except,bool playersOnly,unsigned char** leaders,float (*at)[3],int* sizes,int most) noexcept {
    int n=0;
    for(int i=0;i<world.friends && n<most;++i) {
        const auto o=static_cast<unsigned char*>(const_cast<void*>(world.frObject[i]));
        if(o==except || !IsSoldierClass(o) || IsAnyPlayer(o) || o[kDead] ||
           npc::Scripted(ControlOf(o,RootLeader(o))))continue;
        if(const Squad* q=FindSquad(o); q && q->dismissed)continue;
        const auto up=At<const unsigned char*>(o,kLeader);
        const auto count=At<std::uint64_t>(o,kFollowerCount);
        if((up && !IsAnyPlayer(up)) || (count==0 && !up))continue;   // a follower, or alone and not recruited
        if(playersOnly && !up)continue;                            // a recruited remnant stays with the player
        leaders[n]=o;std::memcpy(at[n],Pos(o),12);sizes[n]=static_cast<int>(count<64 ? count : 64)+1;++n;
    }
    return n;
}

void Succeed(unsigned char* dead) noexcept {
    unsigned char* m[kMaxSquad];
    const int n=Followers(dead,m,kMaxSquad);
    if(!n)return;
    // Reparenting any one of these changes the native tree for all of them. Keep the whole transition native if
    // the leader or any affected member is under mission control, even when the Think caller itself is free.
    if(npc::Scripted(ControlOf(dead,RootLeader(dead))))return;
    for(int i=0;i<n;++i)if(npc::Scripted(ControlOf(m[i],RootLeader(m[i]))))return;
    npc::Member members[kMaxSquad];
    for(int i=0;i<n;++i) {
        members[i]=npc::Member{static_cast<std::uint32_t>(i),true,false,At<float>(m[i],kHumanHp),0,{}};
        std::memcpy(members[i].pos,Pos(m[i]),12);
    }
    const int pick=npc::PickLeader(members,n);
    if(pick<0)return;
    unsigned char* const lead=m[pick];
    auto up=At<unsigned char*>(dead,kLeader);
    if(up && (!Readable(up,kDead+1) || up[kDead]))up=nullptr;
    // Too few left: join another squad instead (the nearest with room).
    unsigned char* others[32];float at[32][3];int sizes[32];
    const Squad* const oldSquad=FindSquad(dead);
    const int k=n<Cfg().npcSquadMin && !(oldSquad && oldSquad->dismissed) ?
                OtherSquads(dead,up && IsAnyPlayer(up),others,at,sizes,32) : 0;
    const int join=k ? npc::JoinSquad(n,Cfg().npcSquadMin,Pos(lead),at,sizes,k,Cfg().npcSquadMax,Cfg().npcSquadJoinRange) : -1;
    unsigned char* const top=join>=0 ? others[join] : lead;
    if(join<0)Follow(lead,up);
    for(int i=0;i<n;++i)if(m[i]!=top)Follow(m[i],top);
    if(Squad* const old=FindSquad(dead)) {
        if(join>=0){if(old->dismissed){old->dismissed=false;--dismissedCount;}old->top=ObjRef{};}
        else {
            old->top=ObjRef::Of(lead);
            if(old->dismissed) {   // still cooling down: the new leader may not be recruited either until it ends
                const ULONGLONG ms=GameMs();
                cooldowns.Start(SquadKey(lead),ms,cooldowns.Left(SquadKey(dead),ms));
                lead[kAutoFollow]=0;
            }
        }
    }
    if(join>=0)Log("NPCAI squad of dead leader %p: %d left, joined squad %p (%d)",dead,n,top,sizes[join]);
    else Log("NPCAI squad of dead leader %p: %p leads the %d left%s",dead,lead,n,up ? (IsAnyPlayer(up) ? " (still the player's)" : " (under its leader)") : "");
}

void PreThink(unsigned char* h) noexcept {
    if(!followOk || !Cfg().npcSquadSuccession || IsAnyPlayer(h) || h[kDead] || !IsOnlineAuthority(h))return;
    const auto team=At<std::int32_t>(h,kTeam);
    if(team!=0 && team!=kTeamFriend)return;
    const auto leader=At<unsigned char*>(h,kLeader);
    if(!leader || !Readable(leader,kObjectFlags+4) || !leader[kDead] || IsAnyPlayer(leader))return;
    if(At<std::uint32_t>(leader,kObjectFlags)&kAutoResurrect)return;   // it comes back: the stock keeps following it
    if(!OnlineHostOnly())return;                                       // the host decides squads (§2.2; online_authority.h)
    if(world.frame!=GameFrame())Gather(At<std::int32_t>(h,kTeam));
    Succeed(leader);
}

// The plugin (or its AI) turned off while a dismissed squad cools down: its +0x540 back as it was (stock again).
void GiveBackDismissed(unsigned char* h) noexcept {
    for(auto& q:squads)
        if(q.dismissed && q.top.Is(h)){h[kAutoFollow]=q.autoFollow;q.dismissed=false;--dismissedCount;Log("NPCAI squad %p: the AI is off, given back to the stock",h);}
}

template<int I> void __fastcall ThinkHook(void* human,const float* dt) {
    if(SupportSoldierHeld(human)) {
        // The actor exists on this peer, but native movement/fire/boarding must wait until every
        // peer acknowledges creation. Clear intent only; native physics and transforms remain intact.
        auto* h=static_cast<unsigned char*>(human);
        std::memset(h+kMoveX,0,0x35);Put<float>(h,kMoveW,1.0f);
        return;
    }
    if(ok && dismissedCount && !(Cfg().enabled && Cfg().customNpcAi)) {
        __try { GiveBackDismissed(static_cast<unsigned char*>(human)); } __except(Fault(GetExceptionInformation())) {}
    }
    if(ok && Cfg().enabled && Cfg().customNpcAi) {
        __try { PreThink(static_cast<unsigned char*>(human)); } __except(Fault(GetExceptionInformation())) {}
    }
    nextThink[I](human,dt);
    if(!ok || !Cfg().enabled || !Cfg().customNpcAi)return;
    __try { Think(static_cast<unsigned char*>(human),I); } __except(Fault(GetExceptionInformation())) {}
}
constexpr ThinkFn kHooks[kClasses]={&ThinkHook<0>,&ThinkHook<1>,&ThinkHook<2>,&ThinkHook<3>};
constexpr std::size_t kSlotThink=7;
}  // namespace

bool IsSoldierClass(const void* human) noexcept {
    if(!Readable(human,8))return false;
    const auto vt=At<const unsigned char*>(human,0);
    for(const auto& c:kSoldiers)if(vt==image+c.vtable)return true;
    return false;
}

bool InstallNpcAi() noexcept {
    __try {
        for(const auto& s:kSignatures)if(!Matches(s.rva,s.bytes,s.size)){Log("NPCAI profile mismatch at %#zx: the soldiers' AI stays stock",s.rva);return false;}
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    __try { followOk=Matches(kSetFollow,kSetFollowSig,sizeof(kSetFollowSig)); } __except(EXCEPTION_EXECUTE_HANDLER){followOk=false;}
    __try { rideOk=Matches(kRideVehicle,kRideSig,sizeof(kRideSig)) && Matches(0x57690D,kRideReleaseSig,sizeof(kRideReleaseSig)); }
    __except(EXCEPTION_EXECUTE_HANDLER){rideOk=false;}
    if(!rideOk)Log("NPCAI RideVehicle not as read: squads do not board vehicles");
    if(!followOk)Log("NPCAI SetFollow not as read: squads are not reorganized when a leader dies");
    InstallRealDriverNative(&OwnsNpcSeatInput);
    InstallBoxes();
    int hooked=0;
    for(int i=0;i<kClasses;++i) {
        auto slot=reinterpret_cast<void**>(image+kSoldiers[i].vtable)+kSlotThink;
        void* current=*slot;
        if(current!=image+kSoldiers[i].think)Log("NPCAI %s Think: chaining onto %p (another plugin)",kSoldiers[i].name,current);
        nextThink[i]=reinterpret_cast<ThinkFn>(current);
        if(PatchVtableSlot(slot,current,reinterpret_cast<void*>(kHooks[i])))++hooked;
        else nextThink[i]=nullptr;
    }
    ok=hooked==kClasses;
    Log("NPCAI soldiers' Think hooked %d/%d%s",hooked,kClasses,ok ? "" : ": the custom AI stays off");
    if(ok)InstallDriverPayload();
    ConfigureNpcCommandNetwork(ok ? &NpcSquadCommandForRequester : nullptr);
    return ok;
}

void ResetNpcAi() noexcept {
    ResetCommandSnapshots();
    ResetGunnerInputs();ResetDriverPayload();
    for(auto& s:soldiers)s=Soldier{};
    for(auto& q:squads){npcmark::Assign(q.commandFocus,{});q=Squad{};}
    cooldowns=npc::Cooldowns<kMaxSquads>{};
    dismissedCount=0;
    npcmark::Assign(mark.obj,{});mark=MarkState{};ping=NpcPing{};
    AcquireSRWLockExclusive(&markLock);markPub=MarkPub{};ReleaseSRWLockExclusive(&markLock);
    world=World{};
    march.heading=npc::formation::Heading{};march.bound=npc::formation::Bound{};march.frame=0;march.n=0;
    march.movers=march.pending=march.moversLast=march.pendingLast=0;
    ResetSweep();
    fullLoggedAt=listLoggedAt=0;
}

// --- The squads on the map (§6) ---
namespace {
char squadNames[kMaxSquads*2][24];
const char* kClassWords[kClasses]={"RANGER","WING DIVER","FENCER","AIR RAIDER"};
// Counted by its soldiers' Think within kSquadSeenMs: alive then (the map reads no object it has not seen lately).
bool Live(const Squad& q,ULONGLONG ms) noexcept {
    __try {
        const auto* top=static_cast<const unsigned char*>(q.top.obj);
        return q.top && q.seen && ms-q.seen<=kSquadSeenMs && Readable(top,kHumanVehicleCtrl+8) &&
            q.top.Is(top) && !top[kDead] && !(top[kObjectFlags]&4) && IsSoldierClass(top) && !IsAnyPlayer(top);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct RemoteSquad {
    ObjRef top{};int cls=0,alive=0;npc::Control control=npc::Control::free;float pos[3]{};
};
RemoteSquad remoteSquads[kMaxSquads]{};int remoteSquadCount=0;ULONGLONG remoteSquadFrame=~ULONGLONG{0};
void ResetCommandSnapshots() noexcept {remoteSquadCount=0;remoteSquadFrame=~ULONGLONG{0};}
struct CommandSquadVisitor { const void* const* vtable; };
void __fastcall CommandSquadIgnore(void*) noexcept {}
void __fastcall CommandSquadVisit(CommandSquadVisitor*,unsigned char* human) noexcept {
    __try {
        if(!IsSoldierClass(human) || !Readable(human,kHumanVehicleCtrl+8) || IsAnyPlayer(human) || human[kDead] || (human[kObjectFlags]&4))return;
        auto* top=TopNpc(human);
        if(!top || !Readable(top,kHumanVehicleCtrl+8) || top[kDead] || IsOnlineAuthority(top))return;
        const auto identity=ObjRef::Of(top);
        unsigned char nativeId[32];if(!identity.ctrl || !ReadNativeObjectId(top,nativeId))return;
        for(int i=0;i<remoteSquadCount;++i)if(remoteSquads[i].top.obj==top && remoteSquads[i].top.ctrl==identity.ctrl){++remoteSquads[i].alive;return;}
        if(remoteSquadCount==kMaxSquads)return;
        auto& row=remoteSquads[remoteSquadCount++];row={};row.top=identity;row.alive=1;
        const auto vt=At<const unsigned char*>(top,0);
        for(int i=0;i<kClasses;++i)if(vt==image+kSoldiers[i].vtable)row.cls=i;
        row.control=ControlOf(top,RootLeader(top));std::memcpy(row.pos,Pos(top),12);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void RefreshCommandSnapshots() noexcept {
    if(!InSession() || !ok || !Cfg().enabled || !Cfg().customNpcAi){remoteSquadCount=0;return;}
    if(remoteSquadFrame==GameFrame())return;
    remoteSquadFrame=GameFrame();remoteSquadCount=0;
    // This read-only scene walk must not call SeeSquad/Think/Gather on replicas. They have no local
    // AI table entry, but their actual native identity is sufficient to select and request authority.
    constexpr unsigned char sig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    __try {
        if(!Matches(kTeamWalk,sig,sizeof(sig)))return;
        auto* manager=At<void*>(image,kTeamManager);if(!Readable(manager,0x50))return;
        static const void* const table[]={reinterpret_cast<const void*>(&CommandSquadIgnore),reinterpret_cast<const void*>(&CommandSquadVisit)};
        CommandSquadVisitor visitor{table};reinterpret_cast<WalkFn>(image+kTeamWalk)(manager,player.team,&visitor);
    } __except(EXCEPTION_EXECUTE_HANDLER){remoteSquadCount=0;}
}
// A squad whose top sits in a vehicle (the plugin's real crews, a support's soldiers): its vehicle's crew.
bool SquadRiding(const Squad& q) noexcept { return !HumanOnFoot(static_cast<const unsigned char*>(q.top.obj)); }
// Whether the map offers RECRUIT for it (mapcmd_logic.h OffersRecruit): nobody's, on foot, not cooling down.
bool SquadRecruitable(const Squad& q) noexcept {
    return mapcmd::OffersRecruit(q.control==npc::Control::recruited,npc::Scripted(q.control),q.dismissed,SquadRiding(q));
}
const char* StatusOf(const Squad& q,ULONGLONG ms,char* buf,std::size_t size) noexcept {
    if(q.dismissed){std::snprintf(buf,size,"WAIT %llus",static_cast<unsigned long long>((cooldowns.Left(SquadKey(q.top.obj),ms)+999)/1000));return buf;}
    if(SquadRiding(q) && !npc::Scripted(q.control))return "RIDING";
    switch(q.control) {
    case npc::Control::recruited: return "RECRUITED";
    case npc::Control::free: return "FREE";
    case npc::Control::squad: return "SQUAD";
    case npc::Control::script: return "SCRIPT";
    case npc::Control::hold: return "HOLD";
    case npc::Control::escort: return "ESCORT";
    }
    return "?";
}
}  // namespace

int SquadCommandUnits(CommandUnit* out,int most) noexcept {
    if(!ok || !Cfg().customNpcAi)return 0;
    const ULONGLONG ms=GameMs();
    int n=0;
    RefreshCommandSnapshots();
    static char status[kMaxSquads*2][16];
    for(int i=0;i<kMaxSquads && n<most;++i) {
        const Squad& q=squads[i];
        if(!Live(q,ms) || (InSession() && !IsOnlineAuthority(q.top.obj)))continue;
        std::snprintf(squadNames[n],sizeof(squadNames[n]),"%s x%d",kClassWords[q.cls],q.alive>0 ? q.alive : 1);
        out[n]=CommandUnit{q.top.obj,squadNames[n],q.cmd,false,{},npc::Scripted(q.control),StatusOf(q,ms,status[n],sizeof(status[n])),
                           SquadRiding(q),SquadRecruitable(q)};
        std::memcpy(out[n++].pos,static_cast<const unsigned char*>(q.top.obj)+kPosition,12);
    }
    for(int i=0;i<remoteSquadCount && n<most && n<kMaxSquads*2;++i) {
        const auto& q=remoteSquads[i];
        std::snprintf(squadNames[n],sizeof(squadNames[n]),"%s x%d",kClassWords[q.cls],q.alive);
        const bool riding=!HumanOnFoot(static_cast<const unsigned char*>(q.top.obj));
        out[n]={q.top.obj,squadNames[n],{},false,{},npc::Scripted(q.control),"REMOTE",riding,
                mapcmd::OffersRecruit(q.control==npc::Control::recruited,npc::Scripted(q.control),false,riding)};
        std::memcpy(out[n++].pos,q.pos,12);
    }
    return n;
}

int SquadRows(SquadRow* out,int most) noexcept {
    if(!ok || !Cfg().customNpcAi)return 0;
    const ULONGLONG ms=GameMs();
    int n=0;
    RefreshCommandSnapshots();
    for(int i=0;i<kMaxSquads && n<most;++i) {
        const Squad& q=squads[i];
        if(!Live(q,ms) || (InSession() && !IsOnlineAuthority(q.top.obj)))continue;
        SquadRow& r=out[n++];
        r.leader=q.top.obj;
        r.identity=q.top;
        std::snprintf(r.name,sizeof(r.name),"%s",kClassWords[q.cls]);
        char buf[16];
        std::snprintf(r.status,sizeof(r.status),"%s",StatusOf(q,ms,buf,sizeof(buf)));
        r.alive=q.alive>0 ? q.alive : 1;
        r.cooldown=q.dismissed ? static_cast<int>((cooldowns.Left(SquadKey(q.top.obj),ms)+999)/1000) : 0;
        r.now=q.cmd;r.locked=npc::Scripted(q.control);r.riding=SquadRiding(q);
    }
    for(int i=0;i<remoteSquadCount && n<most;++i) {
        const auto& q=remoteSquads[i];auto& row=out[n++];row={};
        row.leader=q.top.obj;row.identity=q.top;row.alive=q.alive;row.locked=npc::Scripted(q.control);
        std::snprintf(row.name,sizeof(row.name),"%s",kClassWords[q.cls]);std::snprintf(row.status,sizeof(row.status),"%s",row.locked ? "SCRIPT" : "REMOTE");
    }
    return n;
}

namespace {
// The squad's soldiers: its top and its followers down the tree (live ones, on foot or riding), at most `most`.
int Members(unsigned char* top,unsigned char** out,int most) noexcept {
    if(!top || !out || most<=0)return 0;
    int n=0;
    out[n++]=top;
    for(int i=0;i<n && n<most;++i) {
        unsigned char* f[kMaxSquad];
        const int k=Followers(out[i],f,kMaxSquad);
        for(int j=0;j<k && n<most;++j) {
            bool present=false;for(int used=0;used<n;++used)present=present || out[used]==f[j];
            if(!present)out[n++]=f[j];
        }
    }
    return n;
}

// The march's roster this frame: the player's recruited squads with no order (on foot), squad by squad.
void MarchRoster(ULONGLONG ms) noexcept {
    if(march.frame==GameFrame())return;
    march.frame=GameFrame();march.n=0;
    march.moversLast=march.movers;march.pendingLast=march.pending;march.movers=march.pending=0;
    if(MarchShape()==npc::formation::Shape::bounding)npc::formation::Step(march.bound,march.moversLast,march.pendingLast,ms);
    unsigned char* const me=PlayerHuman();
    if(!me)return;
    for(const Squad& q:squads) {
        if(!Live(q,ms) || q.control!=npc::Control::recruited || q.cmd.order!=Order::none)continue;
        auto top=static_cast<unsigned char*>(const_cast<void*>(q.top.obj));
        if(!q.top.Is(top) || top[kDead] || TopNpc(top)!=top || RootLeader(top)!=me)continue;
        unsigned char* m[kMaxSquad];
        const int k=Members(top,m,kMaxSquad);
        for(int i=0;i<k && march.n<static_cast<int>(sizeof(march.member)/sizeof(march.member[0]));++i)
            if(HumanOnFoot(m[i]) && IsOnlineAuthority(m[i]) && !npc::Scripted(ControlOf(m[i],RootLeader(m[i]))))
                march.member[march.n++]=m[i];
    }
}

// Walks the soldier to its formation slot (formation.h) and holds it there; false when it has none (stock shape,
// not in a formation). Blocked routes wait/replan inside MoveTo; they never fall back to walking through a wall.
bool FormationMove(Soldier& s,unsigned char* h,const float* pos,const Squad* q,const unsigned char* root,ULONGLONG ms,
                   const char** move,float* left) noexcept {
    using namespace npc::formation;
    const float spacing=Cfg().npcFormationSpacing;
    Shape shape=Shape::stock;
    const float* anchor=nullptr;
    float fwd[2]={0.0f,1.0f};
    int k=-1,n=0;
    bool marching=false;
    if(q && mapcmd::PointOrder(q->cmd.order) && q->guardShape!=Shape::stock) {
        shape=q->guardShape;anchor=q->cmd.at;fwd[0]=q->guardFwd[0];fwd[1]=q->guardFwd[1];
        unsigned char* m[kMaxSquad];
        const int c=Members(static_cast<unsigned char*>(const_cast<void*>(q->top.obj)),m,kMaxSquad);
        for(int i=0;i<c;++i){if(!HumanOnFoot(m[i]))continue;if(m[i]==h)k=n;++n;}
    } else if(s.control==npc::Control::recruited && root && root==PlayerHuman() && (!q || q->cmd.order==Order::none) &&
              MarchShape()!=Shape::stock && world.player) {
        MarchRoster(ms);
        shape=MarchShape();anchor=world.playerAt;marching=true;
        for(int i=0;i<march.n;++i)if(march.member[i]==h)k=i;
        n=march.n;
        const float look[2]={world.lookOk ? world.look[0] : 0.0f,world.lookOk ? world.look[2] : 1.0f};
        Track(march.heading,anchor,look,2.0f);
        fwd[0]=march.heading.fwd[0];fwd[1]=march.heading.fwd[1];
    }
    float x,z;
    if(!anchor || k<0 || !Offset(shape,k,n,spacing,&x,&z))return false;
    float slot[3];
    npc::formation::World(anchor,fwd,x,z,slot);
    const float d=npc::Horiz(pos,slot);
    if(left)*left=d;
    if(marching && shape==Shape::bounding) {
        if(TeamOf(k)!=march.bound.moving && npc::Horiz(pos,anchor)<Cfg().npcLeash) {
            Stand(h);*move="overwatch";   // the other half covers: it holds where it is
            return true;
        }
        ++march.movers;
        if(d>kArrive)++march.pending;
    }
    // World() inherits the anchor's height, which is only a hint on a slope.
    // Project locally; never search the whole vertical map and choose a roof.
    const float top[3]={slot[0],slot[1]+0.55f,slot[2]},bottom[3]={slot[0],slot[1]-2.0f,slot[2]};
    float floor[3];
    if(MapFloorRay(top,bottom,floor)>=0.0f && std::isfinite(floor[1])) {
        slot[1]=floor[1];MoveTo(h,pos,slot,kArrive);
    } else Stand(h);
    *move=marching ? "formation" : "guard formation";
    return true;
}

// --- The box sweep (pickup.h; docs/itembox-re.md; the user, 2026-10-08) ---
// The player's key (NpcPickupKey, on foot) sends the recruited squads with no order out for the item boxes within
// NpcPickupRange of the player, one soldier a box (pickup::Assign), until none is left or NpcPickupSec is up (the key
// again calls them back). The boxes are DropItemManager's list (H): not objects, read where they are each frame.
//  - Weapon and armour boxes use Collect's per-box body: Notify 0x2C7D50, Apply 0x2C7540, then mark this Unit taken.
//    The radius-based Collect itself can consume overlapping health boxes, even with a tiny radius. These native
//    calls preserve the player's mission credit, sound and online arbitration without touching adjacent boxes.
//  - A health box heals the soldier that took it (the plugin marks it taken and calls the stock heal 0x547870 on
//    that soldier with the box's share of its full health); offline only, NpcPickupHealth on, a hurt soldier.
constexpr unsigned kDropManager=0x20B2988,kBoxVtable=0x17A6C18,kNotifyBox=0x2C7D50,kApplyBox=0x2C7540,kHealHuman=0x547870,kNodePos=0x11B15B0;
constexpr std::size_t kBoxList=0xDE0,kBoxNodeUnit=0x18,kBoxModel=0xB0,kBoxKind=0xC0,kBoxTaken=0xC4,kBoxId=0xC8;
constexpr int kMaxBoxes=128;
const unsigned char kNotifyBoxSig[]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x81,0xEC,0x60,0x06,0x00,0x00};
const unsigned char kApplyBoxSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x0F,0x29,0x74,0x24,0x20};
const unsigned char kHealHumanSig[]={0x80,0xB9,0xE8,0x02,0x00,0x00,0x00,0x75,0x20,0xF3,0x0F,0x58,0x89,0xF8,0x02,0x00,0x00,0xF3,0x0F,0x5D};
const unsigned char kNodePosSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x8B,0x91,0xF0,0x00,0x00,0x00,0x48,0x8B,0x48};
using NotifyBoxFn=void(__fastcall*)(void*,std::int32_t,std::int32_t,void*);
using ApplyBoxFn=void(__fastcall*)(void*,void*,std::int32_t,float);
using HealHumanFn=void(__fastcall*)(void*,float);
using NodePosFn=const float*(__fastcall*)(const void*);
bool boxesOk=false;

struct SweepBox { void* unit; float pos[3]; int kind; };
constexpr int kSweepTops=16;
struct Sweep {
    bool on,held,logged;
    ObjRef top[kSweepTops];   // the squads sent (the map's selection; none: the player's recruited squads)
    int tops;
    ULONGLONG until,frame;
    int taken,left;
    ULONGLONG endedAt;    // wall clock: the HUD's closing line
};
Sweep sweep{};
SRWLOCK sweepLock=SRWLOCK_INIT;

void* DropManager() noexcept {
    void* const m=At<void*>(image,kDropManager);
    return m && Readable(m,kBoxList+0x10) ? m : nullptr;
}

// The boxes on the ground (not taken), at most `most`: their Unit, position (the model's, as Collect reads it) and kind.
int Boxes(SweepBox* out,int most) noexcept {
    unsigned char* const m=static_cast<unsigned char*>(DropManager());
    if(!m)return 0;
    const auto head=At<unsigned char*>(m,kBoxList);
    if(!Readable(head,0x20))return 0;
    int n=0,guard=0;
    for(auto node=At<unsigned char*>(head,0);node && node!=head && n<most && guard<kMaxBoxes*2;node=At<unsigned char*>(node,0),++guard) {
        if(!Readable(node,0x20))break;
        auto u=At<unsigned char*>(node,kBoxNodeUnit);
        if(!Readable(u,kBoxId+sizeof(std::int32_t)) || At<const void*>(u,0)!=image+kBoxVtable || u[kBoxTaken])continue;
        const int kind=At<std::int32_t>(u,kBoxKind);
        const void* const model=At<const void*>(u,kBoxModel);
        if(kind<0 || kind>3 || !model)continue;
        const float* p=reinterpret_cast<NodePosFn>(image+kNodePos)(model);
        if(!p || !std::isfinite(p[0]+p[1]+p[2]))continue;
        out[n++]=SweepBox{u,{p[0],p[1],p[2]},kind};
    }
    return n;
}

void EndSweep(const char* why) noexcept {
    if(!sweep.on)return;
    AcquireSRWLockExclusive(&sweepLock);
    sweep.on=false;sweep.endedAt=GetTickCount64();
    ReleaseSRWLockExclusive(&sweepLock);
    Log("NPCAI box sweep over (%s): %d taken",why,sweep.taken);
}

void InstallBoxes() noexcept {
    __try {
        boxesOk=Matches(kNotifyBox,kNotifyBoxSig,sizeof(kNotifyBoxSig)) && Matches(kApplyBox,kApplyBoxSig,sizeof(kApplyBoxSig)) &&
                Matches(kHealHuman,kHealHumanSig,sizeof(kHealHumanSig)) &&
                Matches(kNodePos,kNodePosSig,sizeof(kNodePosSig));
    } __except(EXCEPTION_EXECUTE_HANDLER){boxesOk=false;}
    if(!boxesOk)Log("NPCAI the item boxes' code is not as docs/itembox-re.md reads it: no box sweep");
}

void ResetSweep() noexcept {
    AcquireSRWLockExclusive(&sweepLock);
    sweep.on=false;sweep.held=false;sweep.taken=sweep.left=0;sweep.endedAt=0;
    ReleaseSRWLockExclusive(&sweepLock);
}

// The player's choice of health boxes this session: the ini's NpcPickupHealth until the map's switch flips it.
int healthPick=-1;
bool PickupHealth() noexcept { return healthPick<0 ? Cfg().npcPickupHealth : healthPick!=0; }

// Starts the sweep: the squads whose tops are `tops` (`n` 0: the player's recruited squads with no order).
void StartSweep(const void* const* tops,int n,const char* by) noexcept {
    const Config& c=Cfg();
    if(!boxesOk) {
        if(!sweep.logged){sweep.logged=true;Log("NPCAI box sweep: the item boxes are not as docs/itembox-re.md reads them: off");}
        return;
    }
    ObjRef selected[kSweepTops];int count=0;
    for(int i=0;i<n && count<kSweepTops;++i)if(tops[i])selected[count++]=ObjRef::Of(tops[i]);
    AcquireSRWLockExclusive(&sweepLock);
    sweep.on=true;sweep.taken=0;sweep.left=0;sweep.endedAt=0;
    sweep.until=GameMs()+static_cast<ULONGLONG>(c.npcPickupSec*1000.0f);
    sweep.tops=count;
    for(int i=0;i<count;++i)sweep.top[i]=selected[i];
    ReleaseSRWLockExclusive(&sweepLock);
    Log("NPCAI box sweep (%s): %s out for the boxes within %.0f m (health boxes %s)",by,sweep.tops ? "the selected squads" : "the recruited squads",
        c.npcPickupRange,PickupHealth() && !InSession() ? "too, for the hurt" : "left alone");
}

// The soldiers the sweep sends (on foot): its squads' when it was given some, else the march's roster.
int SweepRoster(ULONGLONG ms,const void** out,int most) noexcept {
    int n=0;
    if(!sweep.tops) {
        MarchRoster(ms);
        for(int i=0;i<march.n && n<most;++i)out[n++]=march.member[i];
        return n;
    }
    for(int i=0;i<sweep.tops;++i) {
        auto t=static_cast<unsigned char*>(const_cast<void*>(sweep.top[i].obj));
        if(!sweep.top[i].Is(t) || t[kDead])continue;
        const Squad* q=FindSquad(t);
        if(!q || !Live(*q,ms) || q->dismissed || TopNpc(t)!=t || npc::Scripted(ControlOf(t,RootLeader(t))))continue;
        const unsigned char* const root=RootLeader(t);
        if(InSession() && root && IsAnyPlayer(root) && root!=PlayerHuman())continue;
        unsigned char* m[kMaxSquad];
        const int k=Members(t,m,kMaxSquad);
        for(int j=0;j<k && n<most;++j)
            if(HumanOnFoot(m[j]) && IsOnlineAuthority(m[j]) && !npc::Scripted(ControlOf(m[j],RootLeader(m[j]))))out[n++]=m[j];
    }
    return n;
}

void SweepFrame() noexcept {
    const Config& c=Cfg();
    unsigned char* const me=PlayerHuman();
    const bool down=c.npcPickupKey>0 && me && HumanOnFoot(me) && KeyHeld(c.npcPickupKey);
    const ULONGLONG ms=GameMs();
    if(down && !sweep.held && !MapHoldsKeys()) {
        if(sweep.on)EndSweep("called back by the key");
        else StartSweep(nullptr,0,"the key");
    }
    sweep.held=down;
    if(!sweep.on)return;
    if(!me || !world.player){EndSweep("the player is gone");return;}
    if(ms>=sweep.until){EndSweep("its time is up");return;}
    SweepBox boxes[kMaxBoxes];
    const int nb=Boxes(boxes,kMaxBoxes);
    const void* who[32];
    const int np=SweepRoster(ms,who,32);
    npc::pickup::Box b[kMaxBoxes];
    for(int i=0;i<nb;++i){std::memcpy(b[i].pos,boxes[i].pos,12);b[i].kind=boxes[i].kind;}
    npc::pickup::Picker p[32];
    for(int i=0;i<np;++i) {
        auto h=static_cast<const unsigned char*>(who[i]);
        std::memcpy(p[i].pos,Pos(h),12);
        p[i].hp=At<float>(h,kHumanHp);p[i].hpMax=At<float>(h,kHumanHpMax);
    }
    npc::pickup::Rules r{PickupHealth(),InSession(),c.npcPickupRange,{world.playerAt[0],world.playerAt[1],world.playerAt[2]}};
    int out[32];
    const int given=npc::pickup::Assign(b,nb,p,np,r,out);
    int left=0;
    for(int i=0;i<nb;++i)for(int k=0;k<np;++k)if(npc::pickup::Takes(b[i],p[k],r)){++left;break;}
    AcquireSRWLockExclusive(&sweepLock);sweep.left=left;ReleaseSRWLockExclusive(&sweepLock);
    if(!given){EndSweep(np ? "no box left within reach" : "no soldier to send");return;}
    for(int k=0;k<np;++k) {
        if(out[k]<0)continue;
        Soldier* s=Entry(static_cast<const unsigned char*>(who[k]),ms);
        if(!s)continue;
        const SweepBox& x=boxes[out[k]];
        s->pickUnit=x.unit;std::memcpy(s->pickPos,x.pos,12);s->pickKind=x.kind;s->pickFrame=GameFrame();
    }
}

// The box still lying where the sweep saw it this frame (not taken since): its current position.
bool BoxStill(const void* unit,float* at4) noexcept {
    SweepBox boxes[kMaxBoxes];
    const int n=Boxes(boxes,kMaxBoxes);
    for(int i=0;i<n;++i) {
        if(boxes[i].unit!=unit)continue;
        const float* p=reinterpret_cast<NodePosFn>(image+kNodePos)(At<const void*>(static_cast<const unsigned char*>(unit),kBoxModel));
        std::memcpy(at4,p,16);
        return true;
    }
    return false;
}

// A dropped box's centre is not necessarily a foot point. The walking target
// must be ground inside its 3D pickup sphere; collecting still checks the box.
void MoveToBox(unsigned char* h,const float* pos,const float* box) noexcept {
    const float top[3]={box[0],box[1]+0.1f,box[2]},bottom[3]={box[0],box[1]-npc::pickup::kReach,box[2]};
    float at[3];
    if(MapFloorRay(top,bottom,at)<0.0f || !std::isfinite(at[1]) || npc::Dist(at,box)>npc::pickup::kReach){Stand(h);return;}
    MoveTo(h,pos,at,0.2f);
}

// Runs to its box and takes it there; false when the box is gone (someone took it): the soldier's other moves then.
bool PickUp(Soldier& s,unsigned char* h,const float* pos) noexcept {
    if(!sweep.on || !IsOnlineAuthority(h) || h[kDead])return false;
    if(npc::Dist(pos,s.pickPos)>npc::pickup::kReach){MoveToBox(h,pos,s.pickPos);return true;}
    alignas(16) float at[4];
    auto u=static_cast<unsigned char*>(s.pickUnit);
    if(!boxesOk || !BoxStill(u,at))return false;
    if(npc::Dist(pos,at)>npc::pickup::kReach){MoveToBox(h,pos,at);return true;}
    s.pickUnit=nullptr;
    Stand(h);
    if(npc::pickup::IsHeal(s.pickKind)) {
        const float hpMax=At<float>(h,kHumanHpMax);
        if(!PickupHealth() || InSession() || !(At<float>(h,kHumanHp)<hpMax))return true;   // healed meanwhile: leave it
        u[kBoxTaken]=1;
        reinterpret_cast<HealHumanFn>(image+kHealHuman)(h,npc::pickup::HealShare(s.pickKind)*hpMax);
        AcquireSRWLockExclusive(&sweepLock);++sweep.taken;ReleaseSRWLockExclusive(&sweepLock);
        Log("NPCAI soldier %p took a health box: %.0f/%.0f",h,At<float>(h,kHumanHp),hpMax);
        return true;
    }
    unsigned char* const me=PlayerHuman();
    void* const m=DropManager();
    if(!me || !m || !At<const void*>(me,0x340))return true;
    reinterpret_cast<NotifyBoxFn>(image+kNotifyBox)(m,At<std::int32_t>(u,kBoxId),s.pickKind,me);
    reinterpret_cast<ApplyBoxFn>(image+kApplyBox)(m,me,s.pickKind,0.0f);
    u[kBoxTaken]=1;   // Notify/Apply do not take a Unit pointer; deletion is deferred to the manager's update
    AcquireSRWLockExclusive(&sweepLock);++sweep.taken;ReleaseSRWLockExclusive(&sweepLock);
    Log("NPCAI soldier %p brought in a %s box",h,s.pickKind==npc::pickup::kWeapon ? "weapon" : "armour");
    return true;
}

int CancelBoarding(unsigned char* top) noexcept {
    unsigned char* members[kMaxSquad];
    const int n=Members(top,members,kMaxSquad);
    int cancelled=0;
    for(int i=0;i<n;++i)for(auto& s:soldiers)if(s.ref.Is(members[i]) && s.boardV) {
        s.boardV=ObjRef{};++cancelled;break;
    }
    return cancelled;
}

// The vehicle a squad boards: the one the player rides when it has a seat for them, else the nearest friendly vehicle
// within kBoardFar of the top with one.
constexpr float kBoardFar=150.0f;
bool Reserved(const unsigned char* v,unsigned seat,ULONGLONG ms,const void* except=nullptr) noexcept;
bool FriendlyVehicle(const unsigned char* v,const unsigned char* member) noexcept {
    const int team=At<int>(v,kTeam),soldier=At<int>(member,kTeam);
    return team==soldier || ((soldier==0 || soldier==kTeamFriend) && (team==0 || team==kTeamFriend || team==kTeamVehicle));
}
float BoardDistance(unsigned char* v,unsigned char* const* members,int count,ULONGLONG ms) noexcept {
    if(!v || !Readable(v,kSeatCount+8) || v[kDead] || !OnlineMaySeatNpc(v))return -1;
    float nearest=kBoardFar+1;
    for(int m=0;m<count;++m) {
        auto* h=members[m];
        if(!HumanOnFoot(h) || SupportSoldierHeld(h) || !IsOnlineAuthority(h) ||
           npc::Scripted(ControlOf(h,RootLeader(h))) || !FriendlyVehicle(v,h))continue;
        for(unsigned seat=0;seat<SeatCount(v);++seat) {
            if(!SeatTakes(v,seat,h) || Reserved(v,seat,ms,h))continue;
            float point[3],reach;
            if(!SeatPoint(v,seat,point,&reach) || !std::isfinite(reach) || reach<=0)continue;
            const float distance=npc::Dist(Pos(h),point);if(distance<nearest)nearest=distance;
        }
    }
    return nearest<=kBoardFar ? nearest : -1;
}
unsigned char* BoardTarget(unsigned char* top,const unsigned char* requester,ULONGLONG ms,bool* sawVehicle=nullptr) noexcept {
    unsigned char* members[kMaxSquad];const int count=Members(top,members,kMaxSquad);
    if(sawVehicle)*sawVehicle=false;
    if(requester && !HumanOnFoot(requester)) {
        auto* v=At<unsigned char*>(requester,kHumanRiding);
        if(v && Readable(v,kSeatCount+8) && !v[kDead]) {
            if(sawVehicle)*sawVehicle=true;
            if(BoardDistance(v,members,count,ms)>=0)return v;
        }
    }
    unsigned char* best=nullptr;float bestD=kBoardFar+1;
    for(int i=0;i<world.friends;++i) {
        auto* object=static_cast<unsigned char*>(const_cast<void*>(world.frObject[i]));
        if(!object || !Readable(object,kSeatCount+8) || !KnownVehicle(object) || object[kDead])continue;
        if(sawVehicle)*sawVehicle=true;
        const float distance=BoardDistance(object,members,count,ms);
        if(distance>=0 && distance<bestD){best=object;bestD=distance;}
    }
    return best;
}

// Pending walkers reserve their seats across squads and automatic recruitment. Stale/dead/reused
// soldiers cannot reserve a seat, and a seat occupied before arrival cancels the pending order.
bool Reserved(const unsigned char* v,unsigned seat,ULONGLONG ms,const void* except) noexcept {
    for(const auto& s:soldiers)
        if(s.ref.obj!=except && s.boardV.Is(v) && s.boardSeat==static_cast<int>(seat) && ms-s.boardAt<=kBoardMs &&
           Readable(s.ref.obj,kDead+1) && s.ref.Is(s.ref.obj) && !At<unsigned char>(s.ref.obj,kDead))return true;
    return false;
}

int SeatPriority(const unsigned char* v,unsigned seat) noexcept {
    return seat==0 ? 0 : At<std::uint64_t>(SeatAt(const_cast<unsigned char*>(v),seat),kSeatWeaponCount)>0 ? 1 : 2;
}

bool AssignBoard(unsigned char* v,unsigned char* h,ULONGLONG ms) noexcept {
    if(!HumanOnFoot(h) || SupportSoldierHeld(h) || !IsOnlineAuthority(h) || !OnlineMaySeatNpc(v) ||
       npc::Scripted(ControlOf(h,RootLeader(h))))return false;
    Soldier* const s=Entry(h,ms);
    if(!s)return false;
    if(s->boardV) return s->boardV.Is(v) && s->boardSeat>=0 && static_cast<unsigned>(s->boardSeat)<SeatCount(v) &&
        ms-s->boardAt<=kBoardMs && SeatTakes(v,static_cast<unsigned>(s->boardSeat),h);
    for(int priority=0;priority<3;++priority)for(unsigned k=0;k<SeatCount(v) && k<edf::kMaxSeats;++k) {
        if(SeatPriority(v,k)!=priority || Reserved(v,k,ms,h) || !SeatTakes(v,k,h))continue;
        s->seen=ms;s->boardV=ObjRef::Of(v);s->boardSeat=static_cast<int>(k);s->boardAt=ms;
        return true;
    }
    return false;
}

// Driver first, then actual armed seats, then passengers. Assignment never creates a rider.
bool BoardSquad(unsigned char* top,ULONGLONG ms,const unsigned char* requester=nullptr,int* affected=nullptr,NpcCommandReason* why=nullptr) noexcept {
    bool saw=false;
    unsigned char* const v=BoardTarget(top,requester ? requester : PlayerHuman(),ms,&saw);
    if(affected)*affected=0;
    if(why)*why=saw ? NpcCommandReason::noSeat : NpcCommandReason::noVehicle;
    if(!v){Log("NPCAI squad %p: no friendly vehicle with a seat for it within %.0f m",top,kBoardFar);return false;}
    unsigned char* m[kMaxSquad];
    const int n=Members(top,m,kMaxSquad);
    int given=0;
    for(int i=0;i<n;++i)given+=AssignBoard(v,m[i],ms);
    Log("NPCAI squad %p boards v=%p: %d of %d members have a seat",top,v,given,n);
    if(affected)*affected=given;
    if(given && why)*why=NpcCommandReason::none;
    return given>0;
}

// Every riding member off (SeatKick: the get-off message, a real soldier lands and walks on; a dummy rider would die,
// so only soldiers are kicked). Also cancels members still walking to a seat; false when neither applied.
bool DismountSquad(unsigned char* top,int* affected=nullptr) noexcept {
    const int cancelled=CancelBoarding(top);
    unsigned char* m[kMaxSquad];
    const int n=Members(top,m,kMaxSquad);
    int off=0;
    for(int i=0;i<n;++i) {
        if(HumanOnFoot(m[i]) || !IsOnlineAuthority(m[i]) || npc::Scripted(ControlOf(m[i],RootLeader(m[i]))))continue;
        const auto v=At<unsigned char*>(m[i],kHumanRiding);
        const auto seat=At<unsigned char*>(m[i],kHumanSeat);
        if(!v || !seat || !Readable(seat,kSeatRiderCtrl+8) || At<const void*>(seat,kSeatRider)!=m[i])continue;
        if(!OnlineMaySeatNpc(v))continue;   // NPC riders come and go where they may be seated (online_authority.h)
        reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kSeatKick)(v,seat);
        AnnounceNpcDismount(m[i]);
        ++off;
    }
    Log("NPCAI squad %p dismounts: %d off",top,off);
    if(affected)*affected=off+cancelled;
    return off>0 || cancelled>0;
}
}  // namespace

int NpcBoardCrew(unsigned char* v,unsigned char* const* humans,int count) noexcept {
    if(!ok || !rideOk || !Cfg().enabled || !Cfg().customNpcAi || !Cfg().npcBoarding || !v ||
       !Readable(v,kDead+1) || v[kDead] || !humans || count<=0 || !OnlineMaySeatNpc(v))return 0;
    int assigned=0;
    const ULONGLONG ms=GameMs();
    for(int i=0;i<count;++i) {
        auto* h=humans[i];
        if(!IsSoldierClass(h) || IsAnyPlayer(h) || h[kDead])continue;
        const auto team=At<std::int32_t>(v,kTeam),theirTeam=At<std::int32_t>(h,kTeam);
        if(theirTeam!=team && !(theirTeam==kTeamFriend && (team==0 || team==kTeamVehicle)))continue;
        assigned+=AssignBoard(v,h,ms);
    }
    return assigned;
}

// Recruit only real, already registered soldiers from the world's friendly roster. A request is
// asynchronous: they walk to the native entry point and RideVehicle consumes its shared_ptr there.
bool NpcRequestCrew(unsigned char* v,bool /*spawned*/) noexcept {
    if(!ok || !rideOk || !Cfg().enabled || !Cfg().customNpcAi || !Cfg().npcBoarding || !v ||
       !Readable(v,kDead+1) || v[kDead] || !OnlineMaySeatNpc(v))return false;
    const ULONGLONG ms=GameMs();
    bool pending=false;
    for(const auto& s:soldiers)pending=pending || (s.boardV.Is(v) && ms-s.boardAt<=kBoardMs);
    if(pending)return true;
    if(world.frame!=GameFrame())Gather(At<std::int32_t>(v,kTeam)==kTeamVehicle ? player.team : At<std::int32_t>(v,kTeam));
    int given=0;
    for(int i=0;i<world.friends;++i) {
        auto* const h=static_cast<unsigned char*>(const_cast<void*>(world.frObject[i]));
        if(!IsSoldierClass(h) || IsAnyPlayer(h) || h[kDead] || npc::Dist(Pos(h),Pos(v))>kBoardFar)continue;
        given+=AssignBoard(v,h,ms);
    }
    if(given)Log("CREW v=%p: %d existing soldiers walking to their seats",v,given);
    return given>0;
}

bool NpcCanYieldSeat(const unsigned char* seat) noexcept {
    if(!seat || SeatRider(seat)!=Rider::other)return false;
    auto* h=At<unsigned char*>(seat,kSeatRider);
    return IsSoldierClass(h) && !IsAnyPlayer(h) && !h[kDead] && IsOnlineAuthority(h) &&
        !npc::Scripted(ControlOf(h,RootLeader(h)));
}

bool NpcMoveSeat(unsigned char* v,unsigned from,int to) noexcept {
    auto* seat=SeatAt(v,from);
    if(!rideOk || !NpcCanYieldSeat(seat) || !OnlineMaySeatNpc(v))return false;
    auto* h=At<unsigned char*>(seat,kSeatRider);
    if(to<0) {
        reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kSeatKick)(v,seat);
        if(SeatRider(seat)==Rider::none)AnnounceNpcDismount(h);
        return SeatRider(seat)==Rider::none;
    }
    if(static_cast<unsigned>(to)>=SeatCount(v) || !SeatTakes(v,static_cast<unsigned>(to),h))return false;
    auto* ctrl=At<unsigned char*>(v,kSelfCtrl);
    if(!ctrl || At<std::int32_t>(ctrl,8)<=0 || (to==0 && !PrepareNpcVehicle(v,false)))return false;
    _InterlockedIncrement(reinterpret_cast<volatile long*>(ctrl+8));
    SharedRef ref{v,ctrl};
    reinterpret_cast<RideFn>(image+kRideVehicle)(h,&ref,to);
    const bool moved=SeatRider(seat)==Rider::none && At<const void*>(SeatAt(v,static_cast<unsigned>(to)),kSeatRider)==h;
    if(moved)AnnounceNpcBoarding(h);
    return moved;
}

bool NpcRestoreMissionSeat(unsigned char* v,unsigned char* h) noexcept {
    if(!rideOk || !v || !IsSoldierClass(h) || IsAnyPlayer(h) || h[kDead] ||
       !OnlineMaySeatNpc(v) || !IsOnlineAuthority(h) || At<const void*>(h,kHumanRiding)!=v)return false;
    const auto* current=At<const unsigned char*>(h,kHumanSeat);
    for(unsigned i=0;i<SeatCount(v);++i) {
        auto* seat=SeatAt(v,i);
        // Never dereference an old seat pointer after a reallocation. Same-seat RideVehicle still
        // calls SeatRide before its early return, so it can repair a cleared snapshot seat in place.
        if(seat!=current || !SeatTakes(v,i,h))continue;
        auto* ctrl=At<unsigned char*>(v,kSelfCtrl);
        if(!ctrl || At<std::int32_t>(ctrl,8)<=0)return false;
        _InterlockedIncrement(reinterpret_cast<volatile long*>(ctrl+8));
        SharedRef ref{v,ctrl};reinterpret_cast<RideFn>(image+kRideVehicle)(h,&ref,static_cast<int>(i));
        if(At<const void*>(seat,kSeatRider)!=h)return false;
        AnnounceNpcBoarding(h);return true;
    }
    return false;
}

bool NpcReleaseVehicleCrew(unsigned char* v) noexcept {
    if(!v || !Readable(v,kSeatCount+8) || !OnlineMaySeatNpc(v))return false;
    bool released=false;
    for(unsigned i=0;i<SeatCount(v);++i)
        if(NpcCanYieldSeat(SeatAt(v,i)))released=NpcMoveSeat(v,i,-1) || released;
    return released;
}

bool NpcDriver(const unsigned char* v) noexcept {
    if(!v || SeatCount(v)==0)return false;
    const auto* seat=SeatAt(const_cast<unsigned char*>(v),0);
    if(SeatRider(seat)==Rider::dummy)return true; // original mission-script crew, never fabricated here
    const auto* h=At<const unsigned char*>(seat,kSeatRider);
    return SeatRider(seat)==Rider::other && IsSoldierClass(h) && !IsAnyPlayer(h) && !h[kDead];
}

// An order to a squad (§6.2): guard / engage / release change what its members work round; follow / recruit make it the
// player's (the stock SetFollow, as the stock recruit does), refused during its dismissal's cooldown; dismiss lets go a
// recruited squad where it stands and starts the cooldown (+0x540 cleared meanwhile, or the stock would take it back at
// once). The squads of a mission script take none (§4.3).
// --- Fireteams (the user, 2026-10-08: "支持编组") ---
// A squad is the stock follow tree (§5.1); a fireteam is a squad of its own: SplitSquad makes one squad two (its places
// in Members' order: the even ones stay under the top, the odd ones under the first of them, who follows the top's own
// leader: the player for a recruited squad, nobody for a free one), every member re-parented explicitly (a moved
// soldier's own followers are not dragged along with it); MergeSquads puts other squads' tops under one squad's top.
// Each half is then a squad like any other: its own row on the panel, its own orders and formation. Offline only (as
// the other orders: the follow tree's changes are replicated by vslot 39, but the panel's orders are not, §9).
namespace {
Squad* Commandable(const void* leader) noexcept {
    if(!ok || !Cfg().enabled || !Cfg().customNpcAi || InSession() || !followOk)return nullptr;
    Squad* const q=FindSquad(leader);
    if(!q || npc::Scripted(q->control) || q->dismissed || GameMs()-q->seen>kOrderSeenMs)return nullptr;
    auto top=static_cast<unsigned char*>(const_cast<void*>(leader));
    return q->top.Is(top) && !top[kDead] && IsSoldierClass(top) && TopNpc(top)==top &&
           !npc::Scripted(ControlOf(top,RootLeader(top))) ? q : nullptr;
}

// A mission can take control of a follower independently of its top. Reparenting changes every affected tree,
// so reject the whole operation before its first native call when any member belongs to a mission script.
bool CanRegroup(unsigned char* const* members,int n) noexcept {
    for(int i=0;i<n;++i)if(npc::Scripted(ControlOf(members[i],RootLeader(members[i]))))return false;
    return true;
}
}  // namespace

int SplitSquad(const void* leader) noexcept {
    __try {
        Squad* const q=Commandable(leader);
        if(!q)return -1;
        auto top=static_cast<unsigned char*>(const_cast<void*>(leader));
        unsigned char* m[kMaxSquad];
        const int n=Members(top,m,kMaxSquad);
        if(n<2 || !CanRegroup(m,n))return -1;
        unsigned char* const above=At<unsigned char*>(top,kLeader);
        unsigned char* const second=m[1];
        CancelBoarding(top);   // both halves, before the native follow lists are changed
        Follow(second,above);
        for(int i=2;i<n;++i)Follow(m[i],i%2 ? second : top);
        Log("NPCAI squad %p split: %d stay, %d go with %p",top,(n+1)/2,n/2,second);
        return n/2;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}

bool MergeSquads(const void* into,const void* from) noexcept {
    __try {
        Squad* const a=Commandable(into);
        Squad* const b=Commandable(from);
        if(!a || !b || a==b)return false;
        auto top=static_cast<unsigned char*>(const_cast<void*>(into));
        auto other=static_cast<unsigned char*>(const_cast<void*>(from));
        unsigned char* aMembers[kMaxSquad];unsigned char* bMembers[kMaxSquad];
        const int na=Members(top,aMembers,kMaxSquad),nb=Members(other,bMembers,kMaxSquad);
        if(na+nb>kMaxSquad || !CanRegroup(aMembers,na) || !CanRegroup(bMembers,nb))return false;
        CancelBoarding(other);
        Follow(other,top);
        npcmark::Assign(b->commandFocus,{});*b=Squad{};   // no stale panel/roster entry may command the former top and form a follow cycle
        Log("NPCAI squad %p joins squad %p",other,top);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

int CycleGuardFormation(const void* leader) noexcept {
    if(!ok || !Cfg().enabled || !Cfg().customNpcAi || InSession())return -2;
    Squad* const q=FindSquad(leader);
    if(!q || npc::Scripted(q->control))return -2;
    if(q->cmd.order!=Order::guard)return -1;
    q->guardShape=npc::formation::Next(q->guardShape,true);
    Log("NPCAI squad %p guard formation: %s",leader,npc::formation::Name(q->guardShape));
    return static_cast<int>(q->guardShape);
}

int CycleMarchFormation() noexcept {
    const npc::formation::Shape s=npc::formation::Next(MarchShape(),false);
    SetMarchShape(s,"the map");
    return static_cast<int>(s);
}

constexpr ULONGLONG kSweepEndMs=3000;
bool PlayerSweepCue(SweepCue* out) noexcept {
    AcquireSRWLockShared(&sweepLock);
    const SweepCue c{sweep.on,sweep.left,sweep.taken,Cfg().npcPickupKey};
    const ULONGLONG ended=sweep.endedAt;
    ReleaseSRWLockShared(&sweepLock);
    if(!c.on && (!ended || GetTickCount64()-ended>kSweepEndMs))return false;
    *out=c;
    return true;
}

bool NpcSweepToggle(const void* const* tops,int n) noexcept {
    if(!ok || !Cfg().enabled || !Cfg().customNpcAi)return false;
    if(sweep.on){EndSweep("called back from the map");return false;}
    StartSweep(tops,n,"the map");
    return sweep.on;
}
bool NpcSweepOn() noexcept { return sweep.on; }
bool NpcPickupHealthToggle() noexcept {
    healthPick=PickupHealth() ? 0 : 1;
    Log("NPCAI health boxes: %s (the map)",healthPick ? "hurt soldiers may take them" : "left for the player");
    return healthPick!=0;
}
bool NpcPickupHealthOn() noexcept { return PickupHealth(); }
int NpcMarchShape() noexcept { return static_cast<int>(MarchShape()); }

constexpr ULONGLONG kFormationCueMs=2500;
bool PlayerFormationCue(FormationCue* out) noexcept {
    AcquireSRWLockShared(&marchLock);
    const ULONGLONG at=march.changedAt;
    const int shape=static_cast<int>(march.shape);
    ReleaseSRWLockShared(&marchLock);
    if(!at || GetTickCount64()-at>kFormationCueMs)return false;
    out->shape=shape;out->key=Cfg().npcFormationKey;
    return true;
}

namespace {
void GuardOrder(Squad& q,const Command& c,const unsigned char* requester=nullptr) noexcept {
    q.cmd=c;q.cmdLead=npc::LeadOf(q.control);
    const auto g=npc::formation::FromInt(Cfg().npcGuardFormation);
    if(q.guardShape==npc::formation::Shape::stock)q.guardShape=npc::formation::GuardShape(g) ? g : npc::formation::Shape::stock;
    float dir[3];const auto* me=requester ? requester : PlayerHuman();
    if(me && npc::HorizDir(Pos(me),c.at,dir)){q.guardFwd[0]=dir[0];q.guardFwd[1]=dir[2];}
    else{q.guardFwd[0]=0;q.guardFwd[1]=1;}
}
Squad* SupportRouteOwner(unsigned char* top) noexcept {
    if(!ok || !Cfg().enabled || !Cfg().customNpcAi || !followOk || !top ||
       !Readable(top,kHumanVehicleCtrl+8) || !IsSoldierClass(top) || IsAnyPlayer(top) || top[kDead] ||
       !IsOnlineAuthority(top) || !HumanOnFoot(top) || TopNpc(top)!=top || SupportSoldierHeld(top))return nullptr;
    Squad* const q=FindSquad(top);
    if(!q || GameMs()-q->seen>kOrderSeenMs || !q->top.Is(top) || q->routeCancelled ||
       npc::Scripted(ControlOf(top,RootLeader(top))) || (!q->routeActive && q->cmd.order!=Order::none))return nullptr;
    return q;
}
}
bool NpcPrepareSquadRoute(unsigned char* leader,const float* waypoint,float arrivalRadius) noexcept {
    __try {
        Squad* const q=SupportRouteOwner(leader);
        if(!q || !waypoint || !std::isfinite(waypoint[0]+waypoint[1]+waypoint[2]) ||
           !std::isfinite(arrivalRadius) || arrivalRadius<=0 || arrivalRadius>10 || npc::Horiz(Pos(leader),waypoint)>32)return false;
        if(!q->routeActive)q->routeLead=npc::LeadOf(q->control);
        q->routeActive=true;q->routeArrival=arrivalRadius;std::memcpy(q->routePoint,waypoint,12);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NpcFinishSquadRoute(unsigned char* leader,const float* destination) noexcept {
    __try {
        Squad* const q=SupportRouteOwner(leader);
        if(!q || !q->routeActive || !destination || !std::isfinite(destination[0]+destination[1]+destination[2]) ||
           npc::Horiz(Pos(leader),destination)>32)return false;
        GuardOrder(*q,Command{Order::guard,{destination[0],destination[1],destination[2]}});
        q->routeActive=false;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

namespace {
NpcCommandResult lastCommand{};
NpcCommandResult CommandResult(NpcCommandReason reason,unsigned affected=0) noexcept {
    lastCommand={reason,affected};return lastCommand;
}
bool CommandActor(const ObjRef& ref,bool playerActor) noexcept {
    const auto* h=static_cast<const unsigned char*>(ref.obj);
    return h && Readable(h,kHumanVehicleCtrl+8) && ref.Is(h) && !h[kDead] && !(h[kObjectFlags]&4) &&
        IsSoldierClass(h) && (playerActor ? IsAnyPlayer(h) : !IsAnyPlayer(h));
}
struct CommandFocusCheck { ObjRef wanted;bool found=false;float at[3]{}; };
void SeeCommandFocus(void* context,const void* object,const float* aim) noexcept {
    auto& check=*static_cast<CommandFocusCheck*>(context);
    if(object==check.wanted.obj && Readable(object,kSelfCtrl+8) && check.wanted.Is(object)) {
        check.found=true;std::memcpy(check.at,aim,12);
    }
}
}
NpcCommandResult LastNpcCommandResult() noexcept {return lastCommand;}
ObjRef NpcMarkedIdentity() noexcept {return npcmark::Enabled() && MarkAlive() ? mark.obj : ObjRef{};}
NpcCommandResult NpcSquadCommandForRequester(const ObjRef& selected,const mapcmd::Command& c,
    const ObjRef& requester,const ObjRef& focus) noexcept {
    using Reason=NpcCommandReason;
    __try {
        if(!ok || !Cfg().enabled || !Cfg().customNpcAi || !followOk)return CommandResult(Reason::disabled);
        if((InSession() || requester.obj) && !CommandActor(requester,true))return CommandResult(Reason::invalidRequester);
        if(!CommandActor(selected,false))return CommandResult(Reason::notFound);
        auto* top=static_cast<unsigned char*>(const_cast<void*>(selected.obj));
        const auto* caller=static_cast<const unsigned char*>(requester.obj);
        if(TopNpc(top)!=top)return CommandResult(Reason::notLeader);
        if(!IsOnlineAuthority(top))return CommandResult(Reason::notAuthority);
        const int team=At<int>(top,kTeam),callerTeam=caller ? At<int>(caller,kTeam) : player.team;
        if((team!=0 && team!=kTeamFriend) || (caller && callerTeam!=0 && callerTeam!=kTeamFriend))return CommandResult(Reason::notFriendly);
        const auto* root=RootLeader(top);
        if(root && IsAnyPlayer(root) && root!=caller)return CommandResult(Reason::notOwner);
        const auto control=ControlOf(top,root);
        if(npc::Scripted(control))return CommandResult(Reason::scripted);
        const ULONGLONG ms=GameMs();
        Squad* const q=FindSquad(top);
        if(!q || !q->top.Is(top) || ms-q->seen>kOrderSeenMs)return CommandResult(Reason::stale);
        q->control=control;
        const bool recruited=control==npc::Control::recruited;
        unsigned char* members[kMaxSquad];const int count=Members(top,members,kMaxSquad);
        int affected=count;
        if(mapcmd::PointOrder(c.order) || c.order==Order::engage || c.order==Order::focus) {
            affected=0;
            for(int i=0;i<count;++i)if(HumanOnFoot(members[i]) && IsOnlineAuthority(members[i]) &&
                !npc::Scripted(ControlOf(members[i],RootLeader(members[i]))))++affected;
            if(!affected)return CommandResult(Reason::unsupported); // select the ridden vehicle to command its driver
        }
        switch(c.order) {
        case Order::guard:
        case Order::move:
        case Order::attackMove:   // the guard's point and formation; Drive walks there first (Pursue)
            if(!std::isfinite(c.at[0]+c.at[1]+c.at[2]))return CommandResult(Reason::noTarget);
            GuardOrder(*q,c,caller);break;
        case Order::engage:
            q->cmd=c;q->cmdLead=npc::LeadOf(control);std::memcpy(q->cmd.at,Pos(top),12);break;
        case Order::focus: {
            if(!focus.obj || !Readable(focus.obj,kDead+1) || static_cast<const unsigned char*>(focus.obj)[kDead])return CommandResult(Reason::noTarget);
            CommandFocusCheck checked{focus};VisitEnemiesOf(caller ? callerTeam : team,&SeeCommandFocus,&checked);
            if(!checked.found)return CommandResult(Reason::noTarget);
            q->cmd=c;q->cmdLead=npc::LeadOf(control);std::memcpy(q->cmd.at,checked.at,12);npcmark::Assign(q->commandFocus,focus);break;
        }
        case Order::none:q->cmd={};break;
        case Order::follow:
        case Order::recruit:
            if(!caller)return CommandResult(Reason::invalidRequester);
            if(q->dismissed)return CommandResult(Reason::cooldown);
            // A squad seated in a vehicle is that vehicle's crew: not recruited out of it (command the vehicle).
            if(!HumanOnFoot(top))return CommandResult(Reason::riding);
            if(!recruited)Follow(top,const_cast<unsigned char*>(caller));
            q->cmd={};break;
        case Order::dismiss:
            if(!recruited)return CommandResult(Reason::notOwner);
            q->autoFollow=top[kAutoFollow];top[kAutoFollow]=0;Follow(top,nullptr);
            cooldowns.Start(SquadKey(top),ms,static_cast<std::uint64_t>(Cfg().npcRecruitCooldownSec*1000.0f));
            q->dismissed=true;++dismissedCount;
            q->cmd={Order::guard,{Pos(top)[0],Pos(top)[1],Pos(top)[2]}};q->cmdLead=npc::Lead::own;break;
        case Order::board: {
            if(!Cfg().npcBoarding || !rideOk)return CommandResult(Reason::boardingUnavailable);
            Reason why=Reason::noVehicle;
            if(!BoardSquad(top,ms,caller,&affected,&why))return CommandResult(why);break;
        }
        case Order::dismount:
            if(!Cfg().npcBoarding || !rideOk)return CommandResult(Reason::boardingUnavailable);
            if(!DismountSquad(top,&affected))return CommandResult(Reason::noSeat);break;
        default:return CommandResult(Reason::unsupported);
        }
        if(c.order!=Order::focus)npcmark::Assign(q->commandFocus,{});
        if(c.order!=Order::board)CancelBoarding(top);
        q->routeActive=false;q->routeCancelled=true;
        return CommandResult(Reason::none,static_cast<unsigned>(affected));
    } __except(EXCEPTION_EXECUTE_HANDLER){return CommandResult(Reason::failed);}
}
bool SquadCommand(const void* leader,const Command& c) noexcept {
    __try {
        const auto result=NpcSquadCommandForRequester(ObjRef::Of(leader),c,ObjRef::Of(PlayerHuman()),
            c.order==Order::focus && MarkAlive() ? mark.obj : ObjRef{});
        return result.Accepted();
    } __except(EXCEPTION_EXECUTE_HANDLER){CommandResult(NpcCommandReason::failed);return false;}
}

namespace {
// --- NPC gunners (§7) ---
// An AI rider (AiGunner: a soldier, or the stock RideAi's DummyVehicleRider a bump or a seat swap moved there; the
// user 2026-10-07: "上车的npc应该可以用对应的炮塔武器") in a gunner seat (1..) of a CarBase vehicle (slot 70 the stock
// 0x65F6F0): the stock aims and fires only seat 0 for its AI (0x661440 calls 0x65F6F0 with 0), and a DummyVehicleRider
// never writes its seat's stick (docs/ground-ai-re.md), so the plugin calls it for those seats, before the stock
// input reads the seats (crew.cpp InputHook): it aims by the seat's right stick and pulls its trigger (+0x2E4) only in
// reach, the limits and the map ray, as for the driver. The target: the mark in reach, else the nearest enemy in reach.
constexpr std::size_t kSlotSeatFire=70;
constexpr unsigned kSeatFire=0x65F6F0;
const unsigned char kSeatFireSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
using SeatFireFn=void(__fastcall*)(void*,int,const void*);
struct GunnerPick { const float* from; float reach; const void* best; float bestD; };
void GunnerVisit(void* ctx,const void* object,const float* aim) {
    auto& p=*static_cast<GunnerPick*>(ctx);
    const float d=npc::Dist(p.from,aim);
    if(d<=p.reach && d<p.bestD){p.best=object;p.bestD=d;}
}
}  // namespace

namespace {
// Only inputs written by this module are reclaimed, including switch-off and authority transfer. Human seats
// and a new rider at a reused address are never cleared. No-target frames cannot retain the NPC's last trigger.
struct GunnerWrite { ObjRef vehicle,rider; unsigned seat; float values[4]; };
std::vector<GunnerWrite> gunnerWrites;
constexpr std::size_t kGunnerInputs[4]={0x2D0,0x2D4,0x2E4,0x2E0};
void ResetGunnerInputs() noexcept { gunnerWrites.clear(); }
void ReleaseGunnerInputs(unsigned char* v) noexcept {
    for(auto it=gunnerWrites.begin();it!=gunnerWrites.end();) {
        if(!Readable(it->vehicle.obj,kSelfCtrl+8) || !it->vehicle.Is(it->vehicle.obj)){it=gunnerWrites.erase(it);continue;}
        if(it->vehicle.obj!=v){++it;continue;}
        unsigned char* seat=it->seat<SeatCount(v) ? SeatAt(v,it->seat) : nullptr;
        if(seat && !AnyPlayerIn(seat) && Readable(it->rider.obj,kSelfCtrl+8) && it->rider.Is(At<const void*>(seat,kSeatRider)))
            for(int k=0;k<4;++k)if(At<float>(seat,kGunnerInputs[k])==it->values[k])Put<float>(seat,kGunnerInputs[k],0.0f);
        it=gunnerWrites.erase(it);
    }
}
void RememberGunnerInputs(unsigned char* v,unsigned index,const unsigned char* seat) {
    GunnerWrite write{ObjRef::Of(v),ObjRef::Of(At<const void*>(seat,kSeatRider)),index,{}};
    bool any=false;
    for(int k=0;k<4;++k){write.values[k]=At<float>(seat,kGunnerInputs[k]);any=any || write.values[k]!=0.0f;}
    if(any)gunnerWrites.push_back(write);
}
} // namespace

bool AiGunner(const unsigned char* vehicle,const unsigned char* seat) noexcept {
    if(!Cfg().enabled || !Cfg().npcGunners || !seat)return false;
    const Rider who=SeatRider(seat);
    if(who==Rider::dummy) {
        if(!InSession())return true;
        // A registered vehicle's Dummy exists on the host only. Never pick the driver machine instead:
        // native shots carry the NPC's firing event to every vehicle copy, including a remote driver's.
        if(!Readable(vehicle,0x12A))return false;
        if(online::LocalCopy(At<std::uint16_t>(vehicle,0x128)))return IsOnlineAuthority(vehicle);
        return OnlineHostOnly();
    }
    if(who!=Rider::other || !Cfg().customNpcAi || !Cfg().npcBoarding)return false;
    const auto rider=At<const unsigned char*>(seat,kSeatRider);
    return IsSoldierClass(rider) && !IsAnyPlayer(rider) && IsOnlineAuthority(rider);
}

namespace {
// 0x65F74F and 0x65FF90 use the seat's first holder for native aiming, lock and
// safety tests. Supply the chosen existing holder for this synchronous call, then
// restore the original list even on an exception. Weapon/holder ownership is unchanged.
void AimSelectedGunner(unsigned char* v,unsigned index,const void* target,unsigned char* weapon,PayloadFire fire) {
    static bool aiming=false; // game thread; a nested callback must not borrow another stack list
    if(aiming)return;
    auto seat=SeatAt(v,index);
    auto original=At<unsigned char**>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>16 || !Readable(original,count*8))return;
    unsigned char* selected[16]{};
    std::memcpy(selected,original,count*8);
    bool found=false;
    for(std::uint64_t i=0;i<count;++i)if(Readable(original[i],kHolderWeapon+8) && At<unsigned char*>(original[i],kHolderWeapon)==weapon) {
        selected[0]=original[i];found=true;break;
    }
    if(!found)return;
    __try {
        aiming=true;
        Put<void*>(seat,kSeatWeapons,selected);
        reinterpret_cast<SeatFireFn>(image+kSeatFire)(v,static_cast<int>(index),target);
    } __finally { Put<void*>(seat,kSeatWeapons,original);aiming=false; }
    if(fire==PayloadFire::secondary) {
        Put<float>(seat,0x2E0,At<float>(seat,0x2E4));Put<float>(seat,0x2E4,0.0f);
    }
}
bool GunnerAirTarget(const void* target) noexcept {
    if(!Readable(target,kPosition+12))return false;
    const auto p=reinterpret_cast<const float*>(static_cast<const unsigned char*>(target)+kPosition);
    const float from[3]={p[0],p[1]+1.0f,p[2]},end[3]={p[0],p[1]-512.0f,p[2]};float hit[3];
    return MapFloorRay(from,end,hit)>=0.0f && p[1]-hit[1]>15.0f;
}

// CarBase's stock AI calls slot 70 for seat 0 after deciding its target and driving
// inputs. Wrap those calls, rather than running a second driver fire pass in InputHook.
constexpr unsigned kDriverAimCalls[]={0x66173E,0x6617DB};
constexpr unsigned char kDriverAimCall[]={0xFF,0x90,0x30,0x02,0,0};
constexpr unsigned char kDriverAimContext[2][17]={
    {0x48,0x8B,0x03,0x4C,0x8B,0xC5,0x33,0xD2,0x48,0x8B,0xCB,0xFF,0x90,0x30,0x02,0,0},
    {0x48,0x8B,0x03,0x45,0x33,0xC0,0x33,0xD2,0x48,0x8B,0xCB,0xFF,0x90,0x30,0x02,0,0}};
bool driverPayloadReady=false;
struct DriverWrite { ObjRef vehicle,rider; float fire[2]; ULONGLONG at; };
std::vector<DriverWrite> driverWrites;
void ResetDriverPayload() noexcept { driverWrites.clear(); }
bool RealPayloadDriver(unsigned char* v) noexcept {
    return Cfg().enabled && Cfg().stockStores && !v[kDead] && SeatCount(v)>0 &&
        edf::LivingSoldierInSeat(image,SeatAt(v,0)) && AiGunner(v,SeatAt(v,0));
}
void ReleaseDriverPayload(unsigned char* v,bool replace) noexcept {
    for(auto it=driverWrites.begin();it!=driverWrites.end();) {
        if(!Readable(it->vehicle.obj,kSelfCtrl+8) || !it->vehicle.Is(it->vehicle.obj)){it=driverWrites.erase(it);continue;}
        if(it->vehicle.obj!=v){++it;continue;}
        if(!replace && RealPayloadDriver(v) && GameMs()-it->at<=200){++it;continue;}
        auto seat=SeatCount(v) ? SeatAt(v,0) : nullptr;
        if(seat && !AnyPlayerIn(seat) && Readable(it->rider.obj,kSelfCtrl+8) && it->rider.Is(At<const void*>(seat,kSeatRider))) {
            constexpr std::size_t offsets[]={0x2E4,0x2E0};
            for(int k=0;k<2;++k)if(At<float>(seat,offsets[k])==it->fire[k])Put<float>(seat,offsets[k],0.0f);
        }
        it=driverWrites.erase(it);
    }
}
void RememberDriverPayload(unsigned char* v) {
    const auto seat=SeatAt(v,0);
    driverWrites.push_back({ObjRef::Of(v),ObjRef::Of(At<const void*>(seat,kSeatRider)),
                           {At<float>(seat,0x2E4),At<float>(seat,0x2E0)},GameMs()});
}
void __fastcall DriverPayloadAim(unsigned char* v,int index,const void* target) noexcept {
    const auto vt=At<void* const*>(v,0);
    const auto native=reinterpret_cast<SeatFireFn>(vt[kSlotSeatFire]);
    // Preserve the original virtual dispatch on unmodified classes and occupants.
    if(!driverPayloadReady || !ok || index!=0 || native!=reinterpret_cast<SeatFireFn>(image+kSeatFire) || !RealPayloadDriver(v)) {
        native(v,index,target);return;
    }
    __try {
        ReleaseDriverPayload(v,true);
        const auto seat=SeatAt(v,0);
        PayloadFire fire=PayloadFire::other;
        unsigned char* weapon=nullptr;
        if(target && Readable(target,kPosition+12))
            weapon=NpcPayloadSelect(v,0,npc::Dist(Pos(v),Pos(static_cast<const unsigned char*>(target))),GunnerAirTarget(target),&fire);
        else NpcPayloadSelect(v,0,0.0f,false,&fire); // clear the old redirect on target loss
        Put<float>(seat,0x2E4,0.0f);Put<float>(seat,0x2E0,0.0f);
        if(weapon)AimSelectedGunner(v,0,target,weapon,fire);
        else {
            native(v,0,target); // keep native tracking/idle behavior, but no unavailable shot
            Put<float>(seat,0x2E4,0.0f);Put<float>(seat,0x2E0,0.0f);
        }
        RememberDriverPayload(v);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // The selected-list helper restores its temporary pointer in __finally.
        if(SeatCount(v)){Put<float>(SeatAt(v,0),0x2E4,0.0f);Put<float>(SeatAt(v,0),0x2E0,0.0f);}
    }
}
bool InstallDriverPayload() noexcept {
    if(driverPayloadReady)return true;
    __try {
        if(!Matches(kSeatFire,kSeatFireSig,sizeof(kSeatFireSig)))return false;
        for(int i=0;i<2;++i)if(!Matches(kDriverAimCalls[i]-11,kDriverAimContext[i],sizeof(kDriverAimContext[i])))return false;
        unsigned char patch[2][6]{};
        for(int i=0;i<2;++i) {
            const auto at=image+kDriverAimCalls[i];
            const auto thunk=AllocateNearThunk(at,reinterpret_cast<void*>(&DriverPayloadAim));
            if(!thunk)return false;
            const auto rel=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(at+5);
            if(rel<INT32_MIN || rel>INT32_MAX)return false;
            patch[i][0]=0xE8;const auto rel32=static_cast<std::int32_t>(rel);std::memcpy(patch[i]+1,&rel32,4);patch[i][5]=0x90;
        }
        if(!edf::PatchCode(image+kDriverAimCalls[0],kDriverAimCall,patch[0],6))return false;
        if(!edf::PatchCode(image+kDriverAimCalls[1],kDriverAimCall,patch[1],6)) {
            edf::PatchCode(image+kDriverAimCalls[0],patch[0],kDriverAimCall,6);return false;
        }
        driverPayloadReady=true;Log("NPCAI driver payload: wraps both native seat-0 aim calls");
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

}

void NpcGunnersInput(unsigned char* v) noexcept {
    ReleaseDriverPayload(v,false); // never erase this frame's still-valid stock driver aim
    ReleaseGunnerInputs(v);
    if(!ok || !Cfg().enabled || !Cfg().npcGunners || v[kDead])return;
    static int sig=0;
    if(!sig)sig=Matches(kSeatFire,kSeatFireSig,sizeof(kSeatFireSig)) ? 1 : -1;
    if(sig<0)return;
    const auto vt=At<void* const*>(v,0);
    if(!Readable(vt,(kSlotSeatFire+1)*8) || vt[kSlotSeatFire]!=image+kSeatFire)return;
    for(unsigned i=1;i<SeatCount(v);++i) {
        unsigned char* const seat=SeatAt(v,i);
        if(!AiGunner(v,seat))continue;
        for(const auto offset:kGunnerInputs)Put<float>(seat,offset,0.0f);
        const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(!n || n>16 || !Readable(holders,n*8))continue; // same holder limit as ReadSeat/AimSelectedGunner
        float reach=0.0f;
        for(std::uint64_t k=0;k<n;++k) {
            if(!Readable(holders[k],kHolderWeapon+8))continue;
            const auto w=At<const unsigned char*>(holders[k],kHolderWeapon);
            if(Readable(w,kArmReach+4) && At<float>(w,kArmReach)>reach)reach=At<float>(w,kArmReach);
        }
        if(!(reach>0.0f))continue;
        GunnerPick p{Pos(v),reach,nullptr,1e30f};
        const Enemy* const m=world.frame==GameFrame() ? MarkedEnemy() : nullptr;   // the soldiers' list of this frame only
        if(m && npc::Dist(Pos(v),m->aim)<=reach){p.best=m->object;p.bestD=npc::Dist(Pos(v),m->aim);}
        else VisitEnemies(v,&GunnerVisit,&p);
        if(p.best) {
            if(Cfg().stockStores) {
                PayloadFire fire=PayloadFire::other;
                const auto w=NpcPayloadSelect(v,i,p.bestD,GunnerAirTarget(p.best),&fire);
                if(!w)continue;
                AimSelectedGunner(v,i,p.best,w,fire);
            } else reinterpret_cast<SeatFireFn>(image+kSeatFire)(v,static_cast<int>(i),p.best);
            RememberGunnerInputs(v,i,seat);
        }
    }
}

bool NpcMarked() noexcept { return npcmark::Enabled() && MarkAlive(); }

bool NpcMarkEnemy(const void* object,const float* at,bool toggle) noexcept {
    if(!npcmark::Enabled() || !npcmark::Capture(object))return false;
    const bool same=mark.obj.Is(object);
    if(toggle && same){npcmark::Assign(mark.obj,{});Log("NPCAI mark: let go (the map)");return false;}
    if(!same)Log("NPCAI mark: an enemy marked (the map)");
    Mark(object,at);
    return true;
}

void NpcMarkFrame(unsigned char* human,bool mapOpen) noexcept {
    const Config& c=Cfg();
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    const bool down=c.npcMarkKey>0 && pid==GetCurrentProcessId() && (GetAsyncKeyState(c.npcMarkKey)&0x8000)!=0;
    // Held is followed while the map holds the keys too: a press made in the map (mapcmd.cpp) and still down when it
    // closes is not a second press on foot.
    if(down && !mark.held && !mapOpen && !MapHoldsKeys() && c.enabled && c.customNpcAi && HumanOnFoot(human))
        ToggleMark(At<std::int32_t>(human,kTeam));
    mark.held=down;
    KeepMark();
}

bool NpcMarkReadout(float* at) noexcept {
    AcquireSRWLockShared(&markLock);
    const bool on=markPub.on && GetTickCount64()-markPub.wall<=500;
    if(on)std::memcpy(at,markPub.at,12);
    ReleaseSRWLockShared(&markLock);
    return on;
}

bool NpcPingReadout(NpcPing* out) noexcept {
    AcquireSRWLockShared(&markLock);
    *out=markPub.ping;
    ReleaseSRWLockShared(&markLock);
    return out->on && GetTickCount64()-out->wall<=kPingMs;
}
}  // namespace crew
