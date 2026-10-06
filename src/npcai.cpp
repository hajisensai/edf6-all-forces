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
constexpr int kArmMissFrames=30;        // a pick not taken this long: the pick is off for that soldier (logged)
constexpr int kMaxArms=6,kMaxSoldiers=192,kMaxEnemies=256,kMaxFriends=128,kMaxRoot=16;

struct Soldier {
    ObjRef ref;
    ULONGLONG seen,loggedAt,rollAt,spotAt,losFrame;
    npc::Control control;
    bool controlSet,homeSet,spotSet,los,noSwitch;
    float home[3],spot[3];
    int arm,wantArm,armMiss;
    ObjRef target;
    const void* losObject;   // what the cached map ray was cast at
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
    bool player,lane;
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
        if(!o || o[kDead] || w.friends>=kMaxFriends)return;
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
    if(!w.player || !Cfg().npcFireLane || !CameraRay(eye,dir))return;
    npc::Lane& l=w.laneOf;
    l.from[0]=w.playerAt[0];l.from[1]=w.playerAt[1]+kEye;l.from[2]=w.playerAt[2];
    for(int i=0;i<3;++i)l.to[i]=l.from[i]+dir[i]*Cfg().npcLaneLength;
    float hit[3];
    if(MapRay(l.from,l.to,hit)>=0.0f && std::isfinite(hit[0]+hit[1]+hit[2]))std::memcpy(l.to,hit,12);
    l.radius=Cfg().npcLaneWidth;
    std::memcpy(w.look,dir,12);
    w.lane=true;
}

void Gather(std::int32_t team) noexcept {
    World& w=world;
    w.frame=GameFrame();w.team=team;w.enemies=w.friends=0;
    w.player=player.at && GameMs()-player.at<kPlayerFixMs;
    if(w.player)std::memcpy(w.playerAt,player.pos,12);
    VisitEnemiesOf(team,&SeeEnemy,&w);
    if(const auto manager=At<void*>(image,kTeamManager)) {
        FriendWalk f{kFriendVtable,&w};
        reinterpret_cast<WalkFn>(image+kTeamWalk)(manager,team,&f);
    }
    PlayerLane(w);
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
    const int pick=npc::PickArm(a.arm,a.n,a.current,dist,KindOf(*t,pos),FriendNear(h,t->aim,kFriendNearTarget));
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

// A script's unit (§4.3): its moves and target the stock AI's; the trigger taken off a shot that would hit a friend,
// the weapon picked for the stock target.
Plan Scripted(Soldier& s,unsigned char* h,const Arms& a,const float* eye,const float* pos) noexcept {
    Plan p{"stock",false,a.current};
    const Enemy* t=StockTarget(h);
    if(!t)return p;
    p.arm=ChooseArm(s,h,a,t,eye,pos);
    if(!h[kTrigger])return p;
    npc::Friend fr[kMaxFriends];
    const int n=FriendsBut(h,fr);
    const float blast=p.arm>=0 && p.arm<a.n ? a.arm[p.arm].blast : 0.0f;
    if(p.arm<0 || !npc::ShotClear(eye,t->aim,kSpread,blast,fr,n))h[kTrigger]=0;
    p.fire=h[kTrigger]!=0;
    return p;
}

// Where it fights from: the combat spot (re-picked every kSpotMs) no farther than the leash from its anchor.
void Spot(Soldier& s,const float* pos,const float* aim,const float* anchor,float engage,ULONGLONG ms) noexcept {
    if(s.spotSet && ms-s.spotAt<kSpotMs)return;
    npc::CombatSpot(pos,aim,world.player ? world.playerAt : nullptr,engage,Cfg().npcFlankDeg*npc::kPi/180.0f,s.spot);
    const float off=npc::Horiz(anchor,s.spot),leash=Cfg().npcLeash;
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
bool FallBack(unsigned char* h,const float* pos) noexcept {
    const float hpMax=At<float>(h,kHumanHpMax),hp=At<float>(h,kHumanHp);
    if(!(hpMax>0.0f) || !(hp/hpMax<Cfg().npcRetreatHp) || !world.player)return false;
    float back[3]={-world.look[0],0.0f,-world.look[2]};
    const float l=std::sqrt(back[0]*back[0]+back[2]*back[2]);
    if(!world.lane || l<1e-3f){back[0]=0.0f;back[2]=-1.0f;}else{back[0]/=l;back[2]/=l;}
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
    MoveTo(h,pos,to,kSpotStop);
    return true;
}

Plan Drive(Soldier& s,unsigned char* h,const SoldierClass& c,const Arms& a,const unsigned char* root,const float* eye,
           const float* pos,ULONGLONG ms) noexcept {
    Plan p{"stock",false,a.current};
    // Its anchor: the player it follows, its NPC leader, the spot it was free at.
    const float* anchor=s.control==npc::Control::recruited && world.player ? world.playerAt :
                        s.control==npc::Control::squad && root ? Pos(root) : s.home;
    const float engage=npc::EngageRange(a.arm,a.n,Cfg().npcEngageShare);
    const Pick t=engage>0.0f ? PickTarget(s,eye,anchor,Cfg().npcLeash+engage) : Pick{nullptr,0.0f};
    s.target=t.e ? ObjRef::Of(t.e->object) : ObjRef{};
    const unsigned mask=At<std::uint32_t>(h,kControlMask);
    if(t.e) {
        p.arm=ChooseArm(s,h,a,t.e,eye,pos);
        const float dir[3]={t.e->aim[0]-eye[0],t.e->aim[1]-eye[1],t.e->aim[2]-eye[2]};
        if(mask&kMaskLook)Look(h,dir);
        p.fire=ShotOk(s,h,a,p.arm,*t.e,eye) && (mask&kMaskTrigger);
    } else s.spotSet=false;
    h[kTrigger]=p.fire ? 1 : 0;
    if(!(mask&kMaskMove))return p;
    // Its moves, the first that applies.
    if(Evade(s,h,c,pos,ms,&p.move))return p;
    if(FallBack(h,pos)){p.move="fall back";return p;}
    float out[3];
    if(world.lane && npc::LaneEscape(world.laneOf,pos,out)){Move(h,out,1.0f);p.move="out of the lane";return p;}
    if(t.e) {
        Spot(s,pos,t.e->aim,anchor,engage,ms);
        MoveTo(h,pos,s.spot,kSpotStop);
        p.move="combat spot";
    }
    return p;
}

void Think(unsigned char* h,int cls) noexcept {
    if(IsPlayer(h) || h[kDead] || (At<std::uint8_t>(h,kNet)&1) || !HumanOnFoot(h))return;
    const std::int32_t team=At<std::int32_t>(h,kTeam);
    if(team!=0 && team!=kTeamFriend)return;
    const ULONGLONG ms=GameMs();
    SeeFrame(h);
    if(world.frame!=GameFrame())Gather(team);   // 0 and 2 have the same enemies (TeamManager's table, docs/swarm-team-re.md)
    if(!(h[kListFlags]&kInAiList)) {
        if(ms-listLoggedAt>10000){listLoggedAt=ms;Log("NPCAI soldier %p: not in the AI list (+0x1A bit 3): its block is cleared before use, left stock",h);}
        return;
    }
    Soldier* const s=Entry(h,ms);
    if(!s)return;
    s->seen=ms;
    const float* pos=Pos(h);
    const float eye[3]={pos[0],pos[1]+kEye,pos[2]};
    const unsigned char* root=RootLeader(h);
    const npc::Control control=ControlOf(h,root);
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
    const Plan p=npc::Scripted(control) ? Scripted(*s,h,a,eye,pos) : Drive(*s,h,kSoldiers[cls],a,root,eye,pos,ms);
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

template<int I> void __fastcall ThinkHook(void* human,const float* dt) {
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
    world=World{};
    fullLoggedAt=listLoggedAt=0;
}
}  // namespace crew
