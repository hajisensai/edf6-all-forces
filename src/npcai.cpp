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
// Online: only the soldiers this machine runs (h+0x128 bit 0 clear), as the stock AI writes only theirs; the block goes
// to the other machines the way the stock AI's does (§2.1).
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "npc_logic.h"
#include "vhud.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace crew {
namespace {
// --- The human (docs/npc-ai-design.md §3.1) ---
constexpr std::size_t kMoveX=0xD50,kMoveY=0xD54,kMoveZ=0xD58,kMoveW=0xD5C;   // the move stick, local (x, 0, z, 1)
constexpr std::size_t kLookPitch=0xD60,kLookYaw=0xD64;                       // the look's change this frame (rad)
constexpr std::size_t kTrigger=0xD70;                                         // WeaponSet 0's trigger (held)
constexpr std::size_t kJumpPress=0xD76;                                       // jump / evade pressed
constexpr std::size_t kPickWeapon=0xD82;                                      // pick weapon 0..2 (+i)
constexpr std::size_t kAimPitch=0x1230,kAimYaw=0x1234;                        // the look's target (0x573BA8 adds d60 to it)
constexpr std::size_t kViewPitch=0x1240,kViewYaw=0x1244;                      // the look as it is (eased onto the target)
constexpr std::size_t kListFlags=0x1A;constexpr unsigned char kInAiList=8;  // slot 4 leaves the block of these alone
constexpr std::size_t kNet=0x128;                                             // bit 0: another machine runs it
constexpr std::size_t kControlMask=0x158C;                                    // bit 0 move, 1 look, 4 trigger 0, 11 roll
constexpr unsigned kMaskMove=0x1,kMaskLook=0x2,kMaskTrigger=0x10,kMaskRoll=0x800;
constexpr std::size_t kWeapons=0x1950,kWeaponCount=0x1960,kSets=0x1970,kSetCount=0x1980,kSetWeapon=0x40;
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
        const float r=IsSoldierClass(o) || IsPlayer(o) ? kSoldierRadius : KnownVehicle(o) ? kVehicleRadius : kOtherRadius;
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
// NpcMarkCone degrees (the same one again: the mark let go). Kept while that enemy is in the frame's enemy list. The
// squads told to focus fire all take it; every other soldier takes it first when it is within its longest reach plus
// how far its order lets it move (npc::MarkInReach). This machine's alone (§2.3).
struct MarkState { ObjRef obj; float at[3]; bool held; };
MarkState mark{};
struct MarkPub { bool on; float at[3]; ULONGLONG wall; };
MarkPub markPub{};
SRWLOCK markLock=SRWLOCK_INIT;
constexpr float kMarkFar=2000.0f;   // m: no mark past this

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

const Enemy* MarkedEnemy() noexcept {
    if(!mark.obj)return nullptr;
    for(int i=0;i<world.enemies;++i)if(world.enemy[i].object==mark.obj.obj)return &world.enemy[i];
    return nullptr;
}

void ToggleMark() noexcept {
    float eye[3],dir[3];
    if(!CameraRay(eye,dir))return;
    const float cone=Cfg().npcMarkCone*npc::kPi/180.0f;
    const Enemy* best=nullptr;float bestOff=cone;
    for(int i=0;i<world.enemies;++i) {
        const Enemy& e=world.enemy[i];
        const float to[3]={e.aim[0]-eye[0],e.aim[1]-eye[1],e.aim[2]-eye[2]};
        const float d=npc::Len(to);
        if(d<1.0f || d>kMarkFar)continue;
        const float off=std::acos(npc::Clamp(npc::Dot(to,dir)/d,-1.0f,1.0f));
        if(off<bestOff){bestOff=off;best=&e;}
    }
    const bool same=best && mark.obj.obj==best->object;
    if(best && !same){mark.obj=ObjRef::Of(best->object);std::memcpy(mark.at,best->aim,12);}
    else mark.obj=ObjRef{};
    Log("NPCAI mark: %s",best ? (same ? "let go" : "an enemy marked") : "nothing near the screen's centre to mark");
}

void MarkTick() noexcept {
    unsigned char* const me=PlayerHuman();
    const bool down=Cfg().npcMarkKey>0 && me && HumanOnFoot(me) && KeyHeld(Cfg().npcMarkKey);
    if(down && !mark.held)ToggleMark();
    mark.held=down;
    if(const Enemy* e=MarkedEnemy())std::memcpy(mark.at,e->aim,12);
    else if(mark.obj){mark.obj=ObjRef{};Log("NPCAI mark: the marked enemy is gone");}
    AcquireSRWLockExclusive(&markLock);
    markPub.on=static_cast<bool>(mark.obj);std::memcpy(markPub.at,mark.at,12);markPub.wall=GetTickCount64();
    ReleaseSRWLockExclusive(&markLock);
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
    MarkTick();
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
    f.rootPlayer=root && IsPlayer(root);
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
struct Arms { int n,current; npc::Arm arm[kMaxArms]; };
Arms ArmsOf(const unsigned char* h) noexcept {
    Arms a{};a.current=-1;
    const auto list=At<unsigned char* const*>(h,kWeapons);
    const auto count=At<std::uint64_t>(h,kWeaponCount);
    if(!count || count>16 || !Readable(list,count*8))return a;
    const unsigned char* held=nullptr;
    const auto sets=At<const unsigned char*>(h,kSets);
    if(At<std::uint64_t>(h,kSetCount)>0 && Readable(sets,kSetWeapon+8)) {
        const auto entry=At<unsigned char* const*>(sets,kSetWeapon);
        if(Readable(entry,8))held=*entry;
    }
    a.n=count<kMaxArms ? static_cast<int>(count) : kMaxArms;
    for(int i=0;i<a.n;++i) {
        const unsigned char* w=list[i];
        npc::Arm& m=a.arm[i];m=npc::Arm{};
        if(!Readable(w,kWeaponAmmo+4))continue;
        if(w==held)a.current=i;
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
    float dir[3];
    const float d=npc::Horiz(pos,to);
    if(d<=stop || !npc::HorizDir(pos,to,dir)){Stand(h);return;}
    Move(h,dir,(d-stop)/6.0f+0.3f);
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
    if(!t || a.n==0)return a.current;
    const float dist=npc::Dist(eye,t->aim);
    const auto kind=KindOf(*t,pos);
    const bool friendNear=FriendNear(h,t->aim,kFriendNearTarget);
    const int pick=npc::PickArm(a.arm,a.n<3 ? a.n : 3,a.current,dist,kind,friendNear);
    // Only d82..d84 exist. An inaccessible fourth weapon must not hide a usable second one, but an already held
    // fourth weapon can still win the same hysteresis comparison and be fired without selecting it again.
    if(a.current>=3 && a.current<a.n) {
        const float held=npc::ArmScore(a.arm[a.current],dist,kind,friendNear);
        if(held>0.0f && (pick<0 || held*1.25f>=npc::ArmScore(a.arm[pick],dist,kind,friendNear)))return a.current;
    }
    if(s.wantArm>=0) {
        if(a.current==s.wantArm){s.wantArm=-1;s.armMiss=0;}
        else if(++s.armMiss>kArmMissFrames) {
            s.noSwitch=true;
            Log("NPCAI soldier %p: weapon %d picked for %d frames, still holding %d: weapon picking off for it",h,s.wantArm,kArmMissFrames,a.current);
            s.wantArm=-1;
        }
    }
    if(pick<0 || pick==a.current || pick>=3 || s.noSwitch || !Cfg().npcWeaponSwitch)return a.current;
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

// The stock trigger (both WeaponSets: the Fencer's second hand is d71) taken off a shot at `t` that would hit a friend,
// tested with blast `blast` (the held weapon's, or the largest of its weapons when the held one is not known).
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
void Veto(unsigned char* h,const Enemy* t,const float* eye,float blast,float reach) noexcept {
    if(!h[kTrigger] && !h[kTrigger+1])return;
    npc::Friend fr[kMaxFriends];
    const int n=FriendsBut(h,fr);
    if(t && npc::ShotClear(eye,t->aim,kSpread,blast,fr,n))return;
    if(!t) {   // no target known to the plugin: the shot along its look, as far as its blast weapon would reach
        float dir[3];
        const float pitch=At<float>(h,kViewPitch),yaw=At<float>(h,kViewYaw);
        dir[0]=std::sin(yaw)*std::cos(pitch);dir[1]=-std::sin(pitch);dir[2]=std::cos(yaw)*std::cos(pitch);
        float to[3]={eye[0]+dir[0]*reach,eye[1]+dir[1]*reach,eye[2]+dir[2]*reach},hit[3];
        if(MapRay(eye,to,hit)>=0.0f)std::memcpy(to,hit,12);
        if(reach>0.0f && npc::ShotClear(eye,to,kSpread,blast,fr,n))return;
    }
    h[kTrigger]=0;h[kTrigger+1]=0;
}

// A script's unit (§4.3): its moves and target the stock AI's; the trigger taken off a shot that would hit a friend,
// the weapon picked for the stock target (not while the held one is unknown).
Plan Scripted(Soldier& s,unsigned char* h,const Arms& a,const float* eye,const float* pos) noexcept {
    Plan p{"stock",false,a.current};
    const Enemy* t=StockTarget(h);
    if(t && a.current>=0)p.arm=ChooseArm(s,h,a,t,eye,pos);
    if(p.arm<0 && a.current>=0)h[kTrigger]=h[kTrigger+1]=0;   // switching
    // d71 is the Fencer's independently held second weapon. Until both sets are resolved, either trigger must
    // pass the largest carried blast, not just the primary weapon's (which may be a nonexplosive gun).
    Veto(h,t,eye,LargestBlast(a),LongestReach(a));
    p.fire=h[kTrigger]!=0;
    return p;
}

// Where it fights from: the combat spot (re-picked every kSpotMs) no farther than the leash from its anchor.
void Spot(Soldier& s,const float* pos,const float* aim,const float* anchor,float engage,float leash,ULONGLONG ms) noexcept {
    if(s.spotSet && ms-s.spotAt<kSpotMs)return;
    npc::CombatSpot(pos,aim,world.player ? world.playerAt : nullptr,engage,Cfg().npcFlankDeg*npc::kPi/180.0f,s.spot);
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

// Hurt (§3.6, §3.7): behind the player (out of their lane, a wall between it and the nearest threat when one of a few
// points has one), else away from the nearest threat.
constexpr ULONGLONG kFallMs=1000;
bool FallBack(Soldier& sol,unsigned char* h,const float* pos,ULONGLONG ms) noexcept {
    const float hpMax=At<float>(h,kHumanHpMax),hp=At<float>(h,kHumanHp);
    if(!(hpMax>0.0f) || !(hp/hpMax<Cfg().npcRetreatHp) || !world.player)return false;
    if(sol.fallAt && ms-sol.fallAt<kFallMs){MoveTo(h,pos,sol.fallTo,kSpotStop);return true;}
    float back[3]={-world.look[0],0.0f,-world.look[2]};
    const float l=std::sqrt(back[0]*back[0]+back[2]*back[2]);
    if(!world.lookOk || l<1e-3f){back[0]=0.0f;back[2]=-1.0f;}else{back[0]/=l;back[2]/=l;}
    const Enemy* nearest=nullptr;float nd=1e30f;
    for(int i=0;i<world.enemies;++i){const float d=npc::Horiz(pos,world.enemy[i].aim);if(d<nd){nd=d;nearest=&world.enemy[i];}}
    float to[3]={world.playerAt[0]+back[0]*kBehindPlayer,world.playerAt[1],world.playerAt[2]+back[2]*kBehindPlayer};
    if(nearest) {
        // A few points on the half circle behind the player: the first one the nearest threat cannot see.
        for(int k=-2;k<=2;++k) {
            const float a=static_cast<float>(k)*0.5f,c=std::cos(a),s=std::sin(a);
            const float p[3]={world.playerAt[0]+(back[0]*c-back[2]*s)*kBehindPlayer,world.playerAt[1]+kEye,
                              world.playerAt[2]+(back[0]*s+back[2]*c)*kBehindPlayer};
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
constexpr ULONGLONG kOrderSeenMs=100;         // an order goes only to a squad counted within this (alive then)   // a dismissed squad nobody counted this long: its soldiers are gone
constexpr std::size_t kAutoFollow=0x540;
struct Squad {
    ObjRef top;
    ULONGLONG seen,frame;
    int alive,counting,cls;
    npc::Control control;
    Command cmd;
    std::uint8_t autoFollow;   // +0x540 before a dismissal (put back when its cooldown ends)
    bool dismissed;
    npc::ScriptWatch script;   // the end of a script's control over it (§4.4)
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
        if(!up || !Readable(up,kLeader+8) || IsPlayer(up) || up[kDead] || !IsSoldierClass(up))break;
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
        *q=Squad{};q->top=ObjRef::Of(top);q->cls=cls;
    }
    if(h==top)q->cls=cls;
    ++q->counting;
    q->seen=ms;
    if(q->frame!=GameFrame()) {
        q->alive=q->counting-1;q->counting=1;q->frame=GameFrame();
        // Its control from its top's own fields (the top may be another machine's soldier, whose Think does not get here).
        const unsigned char* const root=RootLeader(top);
        q->control=h==top ? control : ControlOf(top,root);
        // The script let it go (its route ended, it was unfollowed, its position freed) and has not taken it back
        // within ScriptNpcSettleSec: a squad of the plugin's now; with ScriptNpcRecruit the player may recruit it.
        const bool held=Routed(top) || (root && !IsPlayer(root) && Routed(root)) || (At<std::uint32_t>(top,kObjectFlags)&kFixPosition);
        if(npc::Step(q->script,held,ms,static_cast<std::uint64_t>(Cfg().scriptNpcSettleSec*1000.0f))) {
            const bool open=!InSession() && !(At<std::uint8_t>(top,kNet)&1) &&
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
    if(q->cmd.order==Order::guard){o.anchor=q->cmd.at;o.leash=Cfg().npcGuardRadius;o.hold=true;}
    else if(q->cmd.order==Order::engage || q->cmd.order==Order::focus){o.anchor=q->cmd.at;o.leash=Cfg().npcFreeRange;}
    return o;
}

// --- Boarding (§7) ---
// The stock has no way for a soldier to board (the scripts' RideVehicle seats a DummyVehicleRider). The human side's
// RideVehicle 0x5765E0(human, shared_ptr<vehicle>* by value, seat) does all of it (off the old vehicle, +0x1540/+0x1548/
// +0x1550, SeatRide with force 0, the ride state) and releases one strong reference at its end (0x57690D), so the caller
// takes one first. It checks neither team, nor class mask, nor reach: the plugin does (a free seat whose masks take the
// soldier's class, the seat's riding point within reach). Never seat 0: a soldier at the wheel clears its own seat's
// block each frame (0x573A7C), the stock driver's output among it; seat 0 stays AutoCrew's.
constexpr unsigned kRideVehicle=0x5765E0;
constexpr std::size_t kHumanMask=0x31C,kSeatClass=0x30,kSeatEnable=0x34,kHumanSeat=0x1540,kHumanRiding=0x1548;
constexpr ULONGLONG kBoardMs=20000;        // a board order not done in this long is dropped
const unsigned char kRideSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x48};
const unsigned char kRideReleaseSig[]={0x49,0x8B,0x5E,0x08,0x48,0x85,0xDB,0x74,0x27,0x8B,0xC7,0xF0,0x0F,0xC1,0x43,0x08};   // 0x57690D
bool rideOk=false;
struct SharedRef { void* obj; void* ctrl; };
using RideFn=void(__fastcall*)(void*,SharedRef*,int);

bool SeatTakes(const unsigned char* v,unsigned i,const unsigned char* h) noexcept {
    const unsigned char* seat=SeatAt(const_cast<unsigned char*>(v),i);
    if(SeatRider(seat)!=Rider::none)return false;
    return (At<std::uint32_t>(h,kHumanMask)&At<std::uint32_t>(seat,kSeatClass)&At<std::uint32_t>(seat,kSeatEnable))!=0;
}

// Walks to its seat's riding point and boards there; false once the order is over (done, gone, timed out).
bool Board(Soldier& s,unsigned char* h,const float* pos,ULONGLONG ms) noexcept {
    auto v=static_cast<unsigned char*>(const_cast<void*>(s.boardV.obj));
    if(!s.boardV.Is(v) || v[kDead] || ms-s.boardAt>kBoardMs || s.boardSeat<1 || static_cast<unsigned>(s.boardSeat)>=SeatCount(v) ||
       !SeatTakes(v,static_cast<unsigned>(s.boardSeat),h)) {
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
        MoveTo(h,pos,at,0.5f);
        return true;
    }
    auto ctrl=At<unsigned char*>(v,kSelfCtrl);
    if(!rideOk || !ctrl || At<std::int32_t>(ctrl,8)==0){s.boardV=ObjRef{};return false;}
    _InterlockedIncrement(reinterpret_cast<volatile long*>(ctrl+8));   // the reference RideVehicle lets go of (0x57690D)
    SharedRef ref{v,ctrl};
    reinterpret_cast<RideFn>(image+kRideVehicle)(h,&ref,s.boardSeat);
    Log("NPCAI soldier %p boards v=%p seat %d: %s",h,v,s.boardSeat,HumanOnFoot(h) ? "refused by the stock ride" : "seated");
    s.boardV=ObjRef{};
    return true;
}

Plan Drive(Soldier& s,unsigned char* h,const SoldierClass& c,const Arms& a,const unsigned char* root,const float* eye,
           const float* pos,const Squad* q,ULONGLONG ms) noexcept {
    Plan p{"stock",false,a.current};
    // Its anchor: the player it follows, its NPC leader, the spot it was free at; a map order's point over them.
    const Orders o=OrdersOf(q,s.control==npc::Control::recruited && world.player ? world.playerAt :
                               s.control==npc::Control::squad && root ? Pos(root) : s.home);
    const float* anchor=o.anchor;
    const float engage=npc::EngageRange(a.arm,a.n,Cfg().npcEngageShare);
    Pick t=engage>0.0f ? PickTarget(s,eye,anchor,o.leash+engage) : Pick{nullptr,0.0f};
    // The mark first (§6.3): always for a squad told to focus on it, else when within its reach plus its leash.
    if(const Enemy* m=MarkedEnemy()) {
        float reach=0.0f;
        for(int i=0;i<a.n;++i)if(a.arm[i].reach>reach)reach=a.arm[i].reach;
        const bool focus=q && q->cmd.order==Order::focus;
        if(focus || npc::MarkInReach(eye,m->aim,reach,o.leash))t=Pick{m,npc::Dist(eye,m->aim)};
    }
    s.target=t.e ? ObjRef::Of(t.e->object) : ObjRef{};
    const unsigned mask=At<std::uint32_t>(h,kControlMask);
    if(a.current<0) {
        // The held weapon not known (§3.5's M claim does not hold for this soldier): its look and trigger stay the stock
        // AI's, vetoed against friends with its largest blast; the plugin only moves it.
        Veto(h,StockTarget(h),eye,LargestBlast(a),LongestReach(a));
        p.fire=h[kTrigger]!=0;
    } else if(t.e) {
        p.arm=ChooseArm(s,h,a,t.e,eye,pos);
        const float dir[3]={t.e->aim[0]-eye[0],t.e->aim[1]-eye[1],t.e->aim[2]-eye[2]};
        if(mask&kMaskLook)Look(h,dir);
        p.fire=ShotOk(s,h,a,p.arm,*t.e,eye) && (mask&kMaskTrigger);
        h[kTrigger]=p.fire ? 1 : 0;
        Veto(h,t.e,eye,LargestBlast(a),LongestReach(a));   // the second hand (d71) the stock may have pulled
    } else {
        s.spotSet=false;
        h[kTrigger]=0;
        Veto(h,StockTarget(h),eye,LargestBlast(a),LongestReach(a));
    }
    if(!(mask&kMaskMove))return p;
    // Its moves, the first that applies.
    if(Evade(s,h,c,pos,ms,&p.move))return p;
    if(s.boardV && Board(s,h,pos,ms)){p.move="to its seat";return p;}
    if(FallBack(s,h,pos,ms)){p.move="fall back";return p;}
    float out[3];
    if(world.lane && npc::LaneEscape(world.laneOf,pos,out)){Move(h,out,1.0f);p.move="out of the lane";return p;}
    if(t.e) {
        Spot(s,pos,t.e->aim,anchor,engage,o.leash,ms);
        MoveTo(h,pos,s.spot,kSpotStop);
        p.move="combat spot";
    } else if(o.hold) {   // within its post's radius it stands (the stock follow of a recruited squad would pull it away)
        MoveTo(h,pos,anchor,npc::Horiz(pos,anchor)>o.leash ? o.leash*0.5f : 1e9f);
        p.move="at its post";
    }
    return p;
}

void Think(unsigned char* h,int cls) noexcept {
    if(IsPlayer(h) || h[kDead] || (At<std::uint8_t>(h,kNet)&1))return;
    const std::int32_t team=At<std::int32_t>(h,kTeam);
    if(team!=0 && team!=kTeamFriend)return;
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
    const Arms a=ArmsOf(h);
    if(a.n==0) {   // its weapons cannot be read: nothing to fight with that the plugin knows of, so all of it stays stock
        if(Cfg().debug && ms-s->loggedAt>kLogMs){s->loggedAt=ms;Log("NPCAI %s %p: weapons unreadable, left stock",kSoldiers[cls].name,h);}
        return;
    }
    const Plan p=npc::Scripted(control) ? Scripted(*s,h,a,eye,pos) : Drive(*s,h,kSoldiers[cls],a,root,eye,pos,q,ms);
    if(Cfg().debug && ms-s->loggedAt>kLogMs) {
        s->loggedAt=ms;
        Log("NPCAI %s %p %s pos=(%.0f,%.0f,%.0f) arms=%d held=%d arm=%d reach=%.0f target=%p move=%s fire=%d",kSoldiers[cls].name,h,
            ControlName(control),pos[0],pos[1],pos[2],a.n,a.current,p.arm,p.arm>=0 && p.arm<a.n ? a.arm[p.arm].reach : 0.0f,s->target.obj,
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
        if(o && Readable(o,kDead+1) && !o[kDead] && IsSoldierClass(o) && !IsPlayer(o))out[n++]=o;
    }
    return n;
}

// The squads a leaderless remnant may join: live, unscripted NPC soldiers leading followers (or recruited by the
// player), but `except`; their places and sizes.
int OtherSquads(const unsigned char* except,bool playersOnly,unsigned char** leaders,float (*at)[3],int* sizes,int most) noexcept {
    int n=0;
    for(int i=0;i<world.friends && n<most;++i) {
        const auto o=static_cast<unsigned char*>(const_cast<void*>(world.frObject[i]));
        if(o==except || !IsSoldierClass(o) || IsPlayer(o) || o[kDead] ||
           npc::Scripted(ControlOf(o,RootLeader(o))))continue;
        if(const Squad* q=FindSquad(o); q && q->dismissed)continue;
        const auto up=At<const unsigned char*>(o,kLeader);
        const auto count=At<std::uint64_t>(o,kFollowerCount);
        if((up && !IsPlayer(up)) || (count==0 && !up))continue;   // a follower, or alone and not recruited
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
                OtherSquads(dead,up && IsPlayer(up),others,at,sizes,32) : 0;
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
    else Log("NPCAI squad of dead leader %p: %p leads the %d left%s",dead,lead,n,up ? (IsPlayer(up) ? " (still the player's)" : " (under its leader)") : "");
}

void PreThink(unsigned char* h) noexcept {
    if(!followOk || !Cfg().npcSquadSuccession || IsPlayer(h) || h[kDead] || (At<std::uint8_t>(h,kNet)&1))return;
    const auto team=At<std::int32_t>(h,kTeam);
    if(team!=0 && team!=kTeamFriend)return;
    const auto leader=At<unsigned char*>(h,kLeader);
    if(!leader || !Readable(leader,kObjectFlags+4) || !leader[kDead] || IsPlayer(leader))return;
    if(At<std::uint32_t>(leader,kObjectFlags)&kAutoResurrect)return;   // it comes back: the stock keeps following it
    if(InSession() && !IsRoomHost())return;                            // the host decides squads (§2.2)
    if(world.frame!=GameFrame())Gather(At<std::int32_t>(h,kTeam));
    Succeed(leader);
}

// The plugin (or its AI) turned off while a dismissed squad cools down: its +0x540 back as it was (stock again).
void GiveBackDismissed(unsigned char* h) noexcept {
    for(auto& q:squads)
        if(q.dismissed && q.top.Is(h)){h[kAutoFollow]=q.autoFollow;q.dismissed=false;--dismissedCount;Log("NPCAI squad %p: the AI is off, given back to the stock",h);}
}

template<int I> void __fastcall ThinkHook(void* human,const float* dt) {
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
    return ok;
}

void ResetNpcAi() noexcept {
    for(auto& s:soldiers)s=Soldier{};
    for(auto& q:squads)q=Squad{};
    cooldowns=npc::Cooldowns<kMaxSquads>{};
    dismissedCount=0;
    mark=MarkState{};
    world=World{};
    fullLoggedAt=listLoggedAt=0;
}

// --- The squads on the map (§6) ---
namespace {
char squadNames[kMaxSquads][24];
const char* kClassWords[kClasses]={"RANGER","WING DIVER","FENCER","AIR RAIDER"};
// Counted by its soldiers' Think within kSquadSeenMs: alive then (the map reads no object it has not seen lately).
bool Live(const Squad& q,ULONGLONG ms) noexcept { return q.top && q.seen && ms-q.seen<=kSquadSeenMs; }
const char* StatusOf(const Squad& q,ULONGLONG ms,char* buf,std::size_t size) noexcept {
    if(q.dismissed){std::snprintf(buf,size,"WAIT %llus",static_cast<unsigned long long>((cooldowns.Left(SquadKey(q.top.obj),ms)+999)/1000));return buf;}
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
    static char status[kMaxSquads][16];
    for(int i=0;i<kMaxSquads && n<most;++i) {
        const Squad& q=squads[i];
        if(!Live(q,ms))continue;
        std::snprintf(squadNames[i],sizeof(squadNames[i]),"%s x%d",kClassWords[q.cls],q.alive>0 ? q.alive : 1);
        out[n]=CommandUnit{q.top.obj,squadNames[i],q.cmd,false,{},npc::Scripted(q.control),StatusOf(q,ms,status[i],sizeof(status[i]))};
        std::memcpy(out[n++].pos,static_cast<const unsigned char*>(q.top.obj)+kPosition,12);
    }
    return n;
}

int SquadRows(SquadRow* out,int most) noexcept {
    if(!ok || !Cfg().customNpcAi)return 0;
    const ULONGLONG ms=GameMs();
    int n=0;
    for(int i=0;i<kMaxSquads && n<most;++i) {
        const Squad& q=squads[i];
        if(!Live(q,ms))continue;
        SquadRow& r=out[n++];
        r.leader=q.top.obj;
        std::snprintf(r.name,sizeof(r.name),"%s",kClassWords[q.cls]);
        char buf[16];
        std::snprintf(r.status,sizeof(r.status),"%s",StatusOf(q,ms,buf,sizeof(buf)));
        r.alive=q.alive>0 ? q.alive : 1;
        r.cooldown=q.dismissed ? static_cast<int>((cooldowns.Left(SquadKey(q.top.obj),ms)+999)/1000) : 0;
        r.now=q.cmd;r.locked=npc::Scripted(q.control);
    }
    return n;
}

namespace {
// The squad's soldiers: its top and its followers down the tree (live ones, on foot or riding), at most `most`.
int Members(unsigned char* top,unsigned char** out,int most) noexcept {
    int n=0;
    out[n++]=top;
    for(int i=0;i<n && n<most;++i) {
        unsigned char* f[kMaxSquad];
        const int k=Followers(out[i],f,kMaxSquad);
        for(int j=0;j<k && n<most;++j)out[n++]=f[j];
    }
    return n;
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
unsigned char* BoardTarget(const unsigned char* top) noexcept {
    unsigned char* const me=PlayerHuman();
    if(me && !HumanOnFoot(me)) {
        const auto v=At<unsigned char*>(me,kHumanRiding);
        if(v && Readable(v,kDead+1) && !v[kDead]) {
            for(unsigned i=1;i<SeatCount(v);++i)if(SeatTakes(v,i,top))return v;
        }
    }
    unsigned char* best=nullptr;float bestD=kBoardFar;
    for(int i=0;i<world.friends;++i) {
        const auto o=static_cast<unsigned char*>(const_cast<void*>(world.frObject[i]));
        if(!KnownVehicle(o) || o[kDead])continue;
        const float d=npc::Horiz(Pos(top),Pos(o));
        if(d>=bestD)continue;
        for(unsigned k=1;k<SeatCount(o);++k)if(SeatTakes(o,k,top)){best=o;bestD=d;break;}
    }
    return best;
}

// Each member on foot gets a free seat (not seat 0) its class may take; false when none did.
bool BoardSquad(unsigned char* top,ULONGLONG ms) noexcept {
    unsigned char* const v=BoardTarget(top);
    if(!v){Log("NPCAI squad %p: no friendly vehicle with a seat for it within %.0f m",top,kBoardFar);return false;}
    unsigned char* m[kMaxSquad];
    const int n=Members(top,m,kMaxSquad);
    bool taken[edf::kMaxSeats]{};
    int given=0;
    for(int i=0;i<n;++i) {
        if(!HumanOnFoot(m[i]))continue;
        Soldier* const s=Entry(m[i],ms);
        if(!s)continue;
        s->seen=ms;   // a new entry must not be taken back by the next member's Entry
        for(unsigned k=1;k<SeatCount(v) && k<edf::kMaxSeats;++k) {
            if(taken[k] || !SeatTakes(v,k,m[i]))continue;
            taken[k]=true;s->boardV=ObjRef::Of(v);s->boardSeat=static_cast<int>(k);s->boardAt=ms;++given;
            break;
        }
    }
    Log("NPCAI squad %p boards v=%p: %d of %d members have a seat",top,v,given,n);
    return given>0;
}

// Every riding member off (SeatKick: the get-off message, a real soldier lands and walks on; a dummy rider would die,
// so only soldiers are kicked). Also cancels members still walking to a seat; false when neither applied.
bool DismountSquad(unsigned char* top) noexcept {
    const int cancelled=CancelBoarding(top);
    unsigned char* m[kMaxSquad];
    const int n=Members(top,m,kMaxSquad);
    int off=0;
    for(int i=0;i<n;++i) {
        if(HumanOnFoot(m[i]))continue;
        const auto v=At<unsigned char*>(m[i],kHumanRiding);
        const auto seat=At<unsigned char*>(m[i],kHumanSeat);
        if(!v || !seat || !Readable(seat,kSeatRiderCtrl+8) || At<const void*>(seat,kSeatRider)!=m[i])continue;
        reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kSeatKick)(v,seat);
        ++off;
    }
    Log("NPCAI squad %p dismounts: %d off",top,off);
    return off>0 || cancelled>0;
}
}  // namespace

// An order to a squad (§6.2): guard / engage / release change what its members work round; follow / recruit make it the
// player's (the stock SetFollow, as the stock recruit does), refused during its dismissal's cooldown; dismiss lets go a
// recruited squad where it stands and starts the cooldown (+0x540 cleared meanwhile, or the stock would take it back at
// once). The squads of a mission script take none (§4.3).
bool SquadCommand(const void* leader,const Command& c) noexcept {
    if(!ok || !Cfg().enabled || !Cfg().customNpcAi || InSession())return false;
    __try {
        const ULONGLONG ms=GameMs();
        Squad* const q=FindSquad(leader);
        if(!q || ms-q->seen>kOrderSeenMs || npc::Scripted(q->control) || !followOk)return false;
        auto top=static_cast<unsigned char*>(const_cast<void*>(leader));
        if(!q->top.Is(top) || top[kDead] || !IsSoldierClass(top))return false;
        const bool recruited=q->control==npc::Control::recruited;
        switch(c.order) {
        case Order::guard:
            q->cmd=c;
            break;
        case Order::engage:
        case Order::focus:   // the mark (mapcmd refuses it with none)
            q->cmd=c;std::memcpy(q->cmd.at,Pos(top),12);
            break;
        case Order::none:
            q->cmd=Command{Order::none,{0.0f,0.0f,0.0f}};
            break;
        case Order::follow:
        case Order::recruit: {
            q->cmd=Command{Order::none,{0.0f,0.0f,0.0f}};
            if(recruited)break;
            unsigned char* const me=PlayerHuman();
            if(!me || q->dismissed)return false;
            Follow(top,me);
            Log("NPCAI squad %p recruited by command",top);
            break;
        }
        case Order::dismiss:
            if(!recruited)return false;
            q->autoFollow=top[kAutoFollow];top[kAutoFollow]=0;
            Follow(top,nullptr);
            cooldowns.Start(SquadKey(top),ms,static_cast<std::uint64_t>(Cfg().npcRecruitCooldownSec*1000.0f));
            q->dismissed=true;++dismissedCount;
            q->cmd=Command{Order::guard,{0.0f,0.0f,0.0f}};std::memcpy(q->cmd.at,Pos(top),12);
            Log("NPCAI squad %p dismissed: it holds here, recruitable again in %.0f s",top,Cfg().npcRecruitCooldownSec);
            break;
        case Order::board:
            if(!Cfg().npcBoarding || !rideOk || !BoardSquad(top,ms))return false;
            break;
        case Order::dismount:
            if(!Cfg().npcBoarding || !DismountSquad(top))return false;
            break;
        default:
            return false;
        }
        if(c.order!=Order::board)CancelBoarding(top);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

namespace {
// --- NPC gunners (§7) ---
// A soldier in a gunner seat (1..) of a CarBase vehicle (slot 70 the stock 0x65F6F0): the stock aims and fires only
// seat 0 for its AI (0x661440 calls 0x65F6F0 with 0), so the plugin calls it for the soldiers' seats, before the stock
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

void NpcGunnersInput(unsigned char* v) noexcept {
    if(!ok || !Cfg().enabled || !Cfg().customNpcAi || !Cfg().npcBoarding || InSession() || v[kDead])return;
    static int sig=0;
    if(!sig)sig=Matches(kSeatFire,kSeatFireSig,sizeof(kSeatFireSig)) ? 1 : -1;
    if(sig<0)return;
    const auto vt=At<void* const*>(v,0);
    if(!Readable(vt,(kSlotSeatFire+1)*8) || vt[kSlotSeatFire]!=image+kSeatFire)return;
    for(unsigned i=1;i<SeatCount(v);++i) {
        unsigned char* const seat=SeatAt(v,i);
        if(SeatRider(seat)!=Rider::other)continue;
        const auto rider=At<const unsigned char*>(seat,kSeatRider);
        if(!IsSoldierClass(rider) || IsPlayer(rider) || (At<std::uint8_t>(rider,kNet)&1))continue;
        const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(!n || n>8 || !Readable(holders,n*8))continue;
        float reach=0.0f;
        for(std::uint64_t k=0;k<n;++k) {
            if(!Readable(holders[k],kHolderWeapon+8))continue;
            const auto w=At<const unsigned char*>(holders[k],kHolderWeapon);
            if(Readable(w,kArmReach+4) && At<float>(w,kArmReach)>reach)reach=At<float>(w,kArmReach);
        }
        if(!(reach>0.0f))continue;
        GunnerPick p{Pos(v),reach,nullptr,1e30f};
        const Enemy* const m=world.frame==GameFrame() ? MarkedEnemy() : nullptr;   // the soldiers' list of this frame only
        if(m && npc::Dist(Pos(v),m->aim)<=reach)p.best=m->object;
        else VisitEnemies(v,&GunnerVisit,&p);
        if(p.best)reinterpret_cast<SeatFireFn>(image+kSeatFire)(v,static_cast<int>(i),p.best);
    }
}

bool NpcMarked() noexcept { return mark.obj.obj!=nullptr; }

bool NpcMarkReadout(float* at) noexcept {
    AcquireSRWLockShared(&markLock);
    const bool on=markPub.on && GetTickCount64()-markPub.wall<=500;
    if(on)std::memcpy(at,markPub.at,12);
    ReleaseSRWLockShared(&markLock);
    return on;
}
}  // namespace crew
