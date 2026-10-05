// The Primers' creatures (docs/primer-plan.md): enemies made of the plugin's 506 bodies, in models of their own
// (pylib/centipede_model.py, pylib/dragonfly_model.py), placed by a mission like any jet (CreateFriend) and turned
// to the enemy's team on their first frame and every frame after (PrimerTeam: a vehicle's team is its riders',
// and RideAi seats a friend: docs/swarm-team-re.md). Their moving parts are posed every frame (src/primer_pose.h).
//  - The centipede (EDF6VC_CENTIPEDE, mark 7012). Each one fights alone: on the ground it crawls at the player
//    in a weave, circles them close and spits plasma when its head is on them. Linked, centipedes are one long
//    creature: a lone one (or a short one's front) makes for the tail of a longer one near it and joins it there;
//    a joined one follows the trail the one ahead of it leaves, kLinkSpacing behind it (its head and the one
//    ahead's tail hidden, so the whole shows one head and one tail). Two or more take off: the long form flies,
//    winds round the player and now and then dives through, every link spitting when it faces them; more can
//    join it (at most CentipedeLinkMax). One shot out of the middle splits it there: the part behind has no head
//    for a while: its new front writhes, slows and sinks, out of the fight (no fire, no joining), while its head grows
//    back over kRegrowSec (src/primer_pose.h GrowScale), then it is a front again (one left alone comes down to
//    crawl); the part ahead grows its tail back in half that, fighting on. Until its head is back the headless one
//    is a wound: every hit on it does CentipedeWoundDamage times the damage (PrimerMessage).
// PrimerTrace=1 writes a line every kTraceMs per creature into Mods/Plugins/EDF6VehicleCrew.primer.csv (Trace):
// its flight and fight as the game ran them, for tools/primer_trace_view.py to draw.
//  - The dragonfly (EDF6VC_DRAGONFLY, mark 7013): an air superiority fighter that hunts as a dragonfly does. It
//    goes for flying targets first (the plugin's friendly jets, the player off the ground) and the player on the
//    ground otherwise: it flies an interception (where the target will be), comes up from below and behind,
//    holds there kStandoff off with its abdomen curled at it (src/primer_pose.h: it fires only once curled: the
//    warning) and fires its needles, then darts off sideways and up and comes round again.
// Both die as any 506 (the stock crash). Only the local player is aimed at. Game thread, under the input hook's
// guard. All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"
#include "primer_pose.h"
#include <cstdio>
#include <cwchar>

namespace crew {
namespace jet {
namespace {
constexpr std::size_t kOwnTeam=0x318;
constexpr ULONGLONG kLogMs=2000,kTraceMs=100;
constexpr float kChest=1.2f;         // m: what is aimed at over the player's feet
constexpr float kMaxPitch=0.8f;      // rad: the most a body pitches its nose at a target
constexpr float kAimGain=4.0f;
// The centipede (see the file's head).
// m between linked bodies' origins: five segments 1.8 m apart each (pylib/centipede_model.py), so the segments run
// on 1.8 m apart across the links too (tools/primer_chain_view.py reads it).
constexpr float kLinkSpacing=3.0f;   // m: one segment's length (pylib/centipede_model.py)
constexpr float kTrailStep=1.0f;     // m of travel between the trail's samples (kPrimerTrail of them)
constexpr float kCrawlClear=1.5f;    // m its origin rides over the ground crawling (its legs reach 1.35 down)
constexpr float kCrawlSpeed=16.0f,kCrawlNear=45.0f,kWeave=20.0f,kWeaveHz=0.12f;
constexpr float kAirRange=130.0f,kAirHeight=45.0f,kAirSpeed=32.0f,kAirSwell=12.0f;
constexpr ULONGLONG kDiveEveryMs=22000,kDiveMaxMs=12000;
constexpr float kDiveLow=12.0f,kDiveDone=30.0f;
constexpr float kFollowGain=3.0f,kFollowCatch=40.0f,kFollowTop=80.0f;
// m from its link point it links; m/s at least on its way there; m from it on it rides along with the tail.
constexpr float kJoinAt=1.5f,kJoinSpeed=28.0f,kJoinRide=40.0f;
constexpr ULONGLONG kLookMs=1000;
constexpr float kSpitReach=400.0f,kSpitCone=0.12f;
// Its other weapons (pylib/vcobjects.py PRIMER_GUN_FILES): the barbs on its back (a middle one's), aimed at the target
// from its side; the stinger (the chain's last one's), lobbed onto it on a high arc (the round falls at kGravityLob).
constexpr float kBarbReach=300.0f,kBarbCone=0.1f;
constexpr float kStingSpeed=90.0f,kStingReach=520.0f,kStingMost=1.25f,kStingCone=0.1f,kGravityLob=14.7f;
// A writhing one's nose swings kWritheYaw rad each way at kWritheYawHz.
// The blood (爆浆): the stock insects' splash, the global EffectGenUtil's (its pointer at kEffectGen, made at the
// game's start, its textures app:/Effect/basic.rab, there in every mission), called as the giant ant calls it
// (docs/primer-plan.md 爆浆): kBloodHit at a hit's point (scale min(4, damage x 0.1): the ant's, its BloodScale 1),
// kBloodBurst along the body and kBloodPool under it on death. It makes objects in the object manager and draws on
// a global random number: called only from a creature's frame (the game's update), never from the physics step
// or the damage message, which only note the hit.
constexpr std::size_t kEffectGen=0x20B2980,kObjectMgr=0x20B2958;
constexpr unsigned kBloodHit=0x2E5680,kBloodBurst=0x2E1090,kBloodPool=0x2E5BB0,kBloodTexLea=0x2E57E9,kBloodTex=0x17A82B8;
const unsigned char kBloodHitSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
const unsigned char kBloodBurstSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x55};
const unsigned char kBloodPoolSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x78,0x10,0x55,0x48,0x8D,0x68,0xC8};
struct BloodHitParams { float sizeMul; std::uint8_t spray,useAttackDir,pad[2]; float sprayCap; };
using BloodHitFn=void(__fastcall*)(void*,const float*,const float*,const float*,const float*,const float*,float,const BloodHitParams*);
using BloodBurstFn=void(__fastcall*)(void*,const float*,const float*,const float*,float);
using BloodPoolFn=void(__fastcall*)(void*,const float*,const float*,float);
// The giant ant's acid (0x17BC080; its alpha the splash's x8), a dragonfly's greener.
alignas(16) constexpr float kCentipedeBlood[4]={0.675f,0.525f,0.10f,0.125f},kDragonflyBlood[4]={0.35f,0.70f,0.15f,0.125f};
constexpr float kBloodHitMax=4.0f,kBloodHitPerDamage=0.1f,kBloodHitMin=0.01f,kBloodBurstScale=5.0f,kBloodPoolScale=2.0f;
constexpr float kWritheYaw=0.45f,kWritheYawHz=1.7f;
// Its weapon holders (vcobjects JETS: [0, 1] the spit on its head, fired by veh+0x2020; [2] the stinger, by
// +0x2021; [3] the barbs, which no stock byte fires: its weapon's own trigger, as stores.cpp TriggerStore's).
constexpr int kBarbHolder=3;
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10,kWeaponTrigger=0x139;
constexpr float kWritheDrag=1.2f,kWritheSink=4.0f;   // a headless front: 1/s of its speed lost, m/s it sinks   // pylib/vcobjects.py PRIMER_GUN_FILES: 3.5 x 120 m
// The dragonfly.
constexpr float kHuntSpeed=85.0f,kDartSpeed=95.0f,kStrikeRange=260.0f,kStandoff=140.0f,kBelow=25.0f,kAbove=30.0f,
                kBehind=60.0f,kDartDist=350.0f,kHuntClear=15.0f;
constexpr ULONGLONG kStrikeMs=3200,kDartMs=2500;
constexpr float kNeedleReach=360.0f,kNeedleCone=0.08f;   // pylib/vcobjects.py PRIMER_GUN_FILES: 12 x 30 m
constexpr float kPreyRange=1500.0f,kAirborne=6.0f;  // m: prey taken from this far; the player flies this far up

bool IsCentipede(const Jet& j) noexcept { return j.role==Role::centipede; }

bool OnEnemySide(const unsigned char* o) noexcept {
    return At<std::int32_t>(o,kTeam)==kTeamEnemy && At<std::int32_t>(o,kOwnTeam)==kTeamEnemy;
}

// The player's chest, if they were seen lately; false with no player.
bool PlayerAim(float* at,ULONGLONG ms) noexcept {
    if(!player.at || ms-player.at>2000)return false;
    at[0]=player.pos[0];at[1]=player.pos[1]+kChest;at[2]=player.pos[2];
    return true;
}

// The player's velocity, from where they were seen over the last frames (shared by every creature).
void PlayerVelocity(float* vel) noexcept {
    static float prev[3]{},v[3]{};
    static ULONGLONG prevAt=0;
    if(player.at && player.at!=prevAt) {
        const float s=prevAt && player.at>prevAt ? static_cast<float>(player.at-prevAt)*0.001f : 0.0f;
        if(s>0.0f && s<0.5f)for(int i=0;i<3;++i)v[i]+=((player.pos[i]-prev[i])/s-v[i])*0.3f;
        std::memcpy(prev,player.pos,12);prevAt=player.at;
    }
    std::memcpy(vel,v,12);
}

// `p` raised to `clear` over the ground under it.
void OverGround(float* p,float clear) noexcept {
    const float top[3]={p[0],p[1]+600.0f,p[2]},bottom[3]={p[0],p[1]-1500.0f,p[2]};
    float hit[3];
    if(MapRay(top,bottom,hit)>=0.0f && p[1]<hit[1]+clear)p[1]=hit[1]+clear;
}
// `p` put `clear` over the ground under it (up or down).
void OnGround(float* p,float clear) noexcept {
    const float top[3]={p[0],p[1]+600.0f,p[2]},bottom[3]={p[0],p[1]-1500.0f,p[2]};
    float hit[3];
    if(MapRay(top,bottom,hit)>=0.0f)p[1]=hit[1]+clear;
}

// The nose along `dir` (pitch at most kMaxPitch), up as level as that allows.
void Face(Jet& j,const unsigned char* v,const float* dir) noexcept {
    float nose[3]={dir[0],0.0f,dir[2]};
    const float flat=std::sqrt(dir[0]*dir[0]+dir[2]*dir[2]);
    if(flat>1e-3f) {
        const float pitch=Clamp(std::atan2(dir[1],flat),-kMaxPitch,kMaxPitch);
        nose[0]=dir[0]/flat*std::cos(pitch);nose[1]=std::sin(pitch);nose[2]=dir[2]/flat*std::cos(pitch);
    } else {
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        nose[0]=m[8];nose[2]=m[10];
    }
    if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
    float up[3]={0.0f,1.0f,0.0f};
    const float along=Dot(up,nose);
    for(int i=0;i<3;++i)up[i]-=nose[i]*along;
    if(!Normalize(up)){up[0]=0;up[1]=1;up[2]=0;}
    BodyAttitude(v,nose,up,kAimGain,KindOf(j).roll,j.m.omega);
}

// Whether v's nose is within `cone` of `aim` and `reach` of it.
bool OnTarget(const unsigned char* v,const float* pos,const float* aim,float reach,float cone) noexcept {
    float to[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
    if(Len(to)>reach || !Normalize(to))return false;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float fwd[3]={m[8],m[9],m[10]};
    return Normalize(fwd) && std::acos(Clamp(Dot(to,fwd),-1.0f,1.0f))<cone;
}

// The centipede's aimed mounts (pylib/centipede_model.py): the barbs' on its back, the stinger's on its tail. Their
// records follow its moving parts' in PrimerState (kAimGun, kAimSting).
inline constexpr const wchar_t* kCentipedeAims[]={L"gun",L"sting"};
constexpr int kAimCount=2,kAimGun=primer::kCentipedeBoneCount,kAimSting=kAimGun+1;
static_assert(primer::kCentipedeBoneCount+kAimCount<=kPrimerParts && primer::kDragonflyBoneCount<=kPrimerParts,"PrimerState holds the parts");

// Its moving parts: the bone records of `bones` then of `aims` (looked up again when the model's bone array changes;
// every one found or none posed: logged once), each of `bones` written R(axis2, angle2) x R(axis, angle) x bind,
// scaled (angle2 / scale nullptr: 0 / 1). The aimed ones are AimPart's.
void Pose(Jet& j,unsigned char* v,const primer::PoseBone* bones,int n,const float* angle,const float* angle2,
          const float* scale,const wchar_t* const* aims=nullptr,int na=0) noexcept {
    PrimerState& s=j.primer;
    const unsigned char* const inst=v+kModelInst506;
    const auto array=At<const unsigned char*>(inst,kInstBones506);
    if(!array)return;
    if(array!=s.poseModel) {
        s.poseModel=array;s.posed=true;
        for(int i=0;i<n+na;++i) {
            s.poseRec[i]=BoneRecord506(inst,i<n ? bones[i].name : aims[i-n]);
            if(!s.poseRec[i]){s.posed=false;continue;}
            std::memcpy(s.poseBind[i],s.poseRec[i]+kBoneLocal506,64);
        }
        if(!s.posed)Log("PRIMER v=%p %s: its model has not its moving parts: not posed",v,KindOf(j).name);
    }
    if(!s.posed)return;
    for(int i=0;i<n;++i) {
        alignas(16) float local[16];
        primer::TurnLocal2(s.poseBind[i],bones[i].axis,angle[i],bones[i].axis2,angle2 ? angle2[i] : 0.0f,local,
                           scale ? scale[i] : 1.0f);
        std::memcpy(s.poseRec[i]+kBoneLocal506,local,64);
    }
}

// Aimed mount `k` (Pose's record index) pointed along world `dir` (its parent, the body, level-bound: the body's
// frame), or put back as bound (dir nullptr).
void AimPart(Jet& j,const unsigned char* v,int k,const float* dir) noexcept {
    PrimerState& s=j.primer;
    if(!s.posed || !s.poseRec[k])return;
    if(!dir){std::memcpy(s.poseRec[k]+kBoneLocal506,s.poseBind[k],64);return;}
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float local[3]={dir[0]*m[0]+dir[1]*m[1]+dir[2]*m[2],dir[0]*m[4]+dir[1]*m[5]+dir[2]*m[6],
                          dir[0]*m[8]+dir[1]*m[9]+dir[2]*m[10]};
    alignas(16) float out[16];
    primer::AimLocal(s.poseBind[k],local,out);
    std::memcpy(s.poseRec[k]+kBoneLocal506,out,64);
}

// The splash functions, checked once (their heads, and the hit's texture name where its lea r8 reads it);
// nullptr: not this game build, no blood (logged once).
void* BloodFx() noexcept {
    static int ok=-1;
    if(ok<0) {
        std::int32_t rel=0;
        const bool lea=Readable(image+kBloodTexLea,7) && (image[kBloodTexLea]&0xFB)==0x48 && image[kBloodTexLea+1]==0x8D &&
                       (std::memcpy(&rel,image+kBloodTexLea+3,4),kBloodTexLea+7+rel==kBloodTex);
        ok=Matches(kBloodHit,kBloodHitSig,sizeof(kBloodHitSig)) && Matches(kBloodBurst,kBloodBurstSig,sizeof(kBloodBurstSig)) &&
           Matches(kBloodPool,kBloodPoolSig,sizeof(kBloodPoolSig)) && lea ? 1 : 0;
        Log(ok ? "PRIMER blood: the insects' splash found" : "PRIMER blood: the insects' splash not as expected: no blood");
    }
    if(!ok || !Cfg().primerBlood)return nullptr;
    void* const fx=At<void*>(image,kEffectGen);
    return fx && At<void*>(image,kObjectMgr) ? fx : nullptr;
}

const float* BloodColour(const Jet& j) noexcept { return IsCentipede(j) ? kCentipedeBlood : kDragonflyBlood; }

// The hits it took since its last frame (PrimerMessage noted them): one splash at the last one's point, as big as
// their damage, out from its body.
void BleedHits(Jet& j,unsigned char* v,const float* pos) noexcept {
    PrimerState& s=j.primer;
    const float damage=s.bloodDamage;
    s.bloodDamage=0.0f;
    void* const fx=damage>0.0f ? BloodFx() : nullptr;
    if(!fx)return;
    const float scale=std::fmin(kBloodHitMax,damage*kBloodHitPerDamage)*Cfg().primerBlood;
    if(scale<kBloodHitMin)return;
    alignas(16) float at[4]={s.bloodAt[0],s.bloodAt[1],s.bloodAt[2],1.0f},out[4]={at[0]-pos[0],at[1]-pos[1],at[2]-pos[2],0.0f};
    if(!Normalize(out)){out[0]=0.0f;out[1]=1.0f;out[2]=0.0f;}
    const BloodHitParams p{1.0f,1,0,{0,0},500.0f};
    reinterpret_cast<BloodHitFn>(image+kBloodHit)(fx,at,out,out,reinterpret_cast<const float*>(v+kMatrix),BloodColour(j),scale,&p);
}

// Its death: bursts out of it (up, to each side, back) and a pool under it, as the ant's (bursts at its bones,
// DeathBloodScale x5; the pool x2).
void BleedOut(const Jet& j,const unsigned char* v) noexcept {
    void* const fx=BloodFx();
    if(!fx)return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    const float k=Cfg().primerBlood*(IsCentipede(j) ? 1.0f : 1.5f);
    const float dirs[4][3]={{m[4],m[5],m[6]},{m[0],m[1],m[2]},{-m[0],-m[1],-m[2]},{-m[8],-m[9],-m[10]}};
    for(const auto& d:dirs) {
        alignas(16) const float at[4]={p[0]+d[0]*0.8f,p[1]+d[1]*0.8f,p[2]+d[2]*0.8f,1.0f},dir[4]={d[0],d[1],d[2],0.0f};
        reinterpret_cast<BloodBurstFn>(image+kBloodBurst)(fx,at,dir,BloodColour(j),kBloodBurstScale*k);
    }
    alignas(16) const float at[4]={p[0],p[1],p[2],1.0f};
    reinterpret_cast<BloodPoolFn>(image+kBloodPool)(fx,at,BloodColour(j),kBloodPoolScale*k);
}

// Holder `i`'s weapon of its seat 0 (vcobjects JETS order), or nullptr.
unsigned char* HolderWeapon(unsigned char* v,int i) noexcept {
    if(SeatCount(v)==0)return nullptr;
    unsigned char* const seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(i<0 || static_cast<std::uint64_t>(i)>=count || count>8 || !Readable(holders,count*8) || !Readable(holders[i],kHolderWeapon+8))
        return nullptr;
    return At<unsigned char*>(holders[i],kHolderWeapon);
}

// Whether holder `i`'s barrel (its muzzles on their bone, as the game fires it) points within `cone` of `dir`.
bool BarrelAlong(unsigned char* v,int i,const float* dir,float cone) noexcept {
    const unsigned char* const w=HolderWeapon(v,i);
    float at[3],d[3];
    if(!w || !GunBarrel(v,w,at,d))return false;
    float want[3]={dir[0],dir[1],dir[2]};
    return Normalize(want) && std::acos(Clamp(Dot(d,want),-1.0f,1.0f))<cone;
}

unsigned PrimerFlight() noexcept {
    static unsigned flight=0;   // one flight for every creature: their rounds pass through each other
    if(!flight)flight=NewFlight();
    return flight;
}

// HP times PrimerHpScale, the enemy's team, the flight: once, on its first frame.
void Init(Jet& j,unsigned char* v,const float* pos,ULONGLONG ms) noexcept {
    PrimerState& s=j.primer;
    s.init=true;
    PrimerTeam(v);
    const float max=At<float>(v,kHpMax)*Cfg().primerHpScale;
    if(max>2.0f){Put<float>(v,kHpMax,max);Put<float>(v,kHp,max);}
    j.mode=Mode::patrol;
    JoinFlight(j,PrimerFlight());
    std::memcpy(s.last,pos,12);
    s.trailCount=0;s.trailHead=0;
    s.seed=static_cast<float>((reinterpret_cast<std::uintptr_t>(v)>>4)%1000)*0.01f;
    float at[3];
    s.orbit=PlayerAim(at,ms) ? std::atan2(pos[2]-at[2],pos[0]-at[0]) : s.seed;
    s.diveAt=ms;s.huntAt=ms;
    Log("PRIMER v=%p %s: enemy team %d, hp %.0f, flight %u",v,KindOf(j).name,At<std::int32_t>(v,kTeam),At<float>(v,kHp),j.flight);
    Publish(true);
}

void Debug(Jet& j,const unsigned char* v,const char* what,bool fire,ULONGLONG ms) noexcept {
    if(!Cfg().debug || ms-j.primer.logAt<kLogMs)return;
    j.primer.logAt=ms;
    Log("PRIMER v=%p %s %s hp=%.0f/%.0f fire=%d vel=(%.0f,%.0f,%.0f)",v,KindOf(j).name,what,At<float>(v,kHp),At<float>(v,kHpMax),fire,
        j.m.vel[0],j.m.vel[1],j.m.vel[2]);
}

// --- The centipede ---

// The centipede entry whose control block is `ctrl` (alive, flown), or nullptr.
Jet* Centipede(const void* ctrl) noexcept {
    if(!ctrl)return nullptr;
    for(auto& c:jets)
        if(c.ref && c.ref.ctrl==ctrl && IsCentipede(c) && !c.reap && Alive(c.ref) && !c.Vehicle()[kDead])return &c;
    return nullptr;
}
Jet* FrontOf(Jet* j) noexcept {
    for(int n=0;n<kMaxJets && j;++n){Jet* a=Centipede(j->primer.ahead);if(!a)return j;j=a;}
    return j;
}
int LengthFrom(const Jet* j) noexcept {   // it and every one behind it
    int n=0;
    for(;j && n<kMaxJets;++n)j=Centipede(j->primer.behind);
    return n;
}

void Trail(Jet& j,const float* pos) noexcept {
    PrimerState& s=j.primer;
    const float d[3]={pos[0]-s.last[0],pos[1]-s.last[1],pos[2]-s.last[2]};
    if(s.trailCount && Len(d)<kTrailStep)return;
    std::memcpy(s.trail[s.trailHead],pos,12);
    s.trailHead=(s.trailHead+1)%kPrimerTrail;
    if(s.trailCount<kPrimerTrail)++s.trailCount;
    std::memcpy(s.last,pos,12);
}

// The point `back` m behind `lead` along the trail it left (from where it is now, newest sample first); past the
// trail's end, on along its last stretch.
void TrailPoint(const Jet& lead,float back,float* out) noexcept {
    const PrimerState& s=lead.primer;
    float prev[3];
    std::memcpy(prev,lead.Vehicle()+kPosition,12);
    float dir[3]={0.0f,0.0f,0.0f};
    for(int k=0;k<s.trailCount;++k) {
        const float* p=s.trail[(s.trailHead-1-k+kPrimerTrail*2)%kPrimerTrail];
        float seg[3]={p[0]-prev[0],p[1]-prev[1],p[2]-prev[2]};
        const float l=Len(seg);
        if(l<1e-3f)continue;
        if(back<=l){for(int i=0;i<3;++i)out[i]=prev[i]+seg[i]*(back/l);return;}
        back-=l;
        for(int i=0;i<3;++i)dir[i]=seg[i]/l;
        std::memcpy(prev,p,12);
    }
    if(Len(dir)<0.5f) {   // no trail yet: straight behind it
        const float* m=reinterpret_cast<const float*>(lead.Vehicle()+kMatrix);
        dir[0]=-m[8];dir[1]=-m[9];dir[2]=-m[10];
        Normalize(dir);
    }
    for(int i=0;i<3;++i)out[i]=prev[i]+dir[i]*back;
}

// Whether the front `j` (its chain `len` long) may join tail `t`: a tail of another chain, that chain longer than
// its own (or as long, its front earlier in the table), the two together at most CentipedeLinkMax. Never its own
// chain's, and the order is strict, so no ring can form. Asked again of the tail it already makes for each frame:
// its own chain may have grown on the way.
bool MayJoin(const Jet& j,const Jet& t,int len) noexcept {
    if(&t==&j || !t.ref || !IsCentipede(t) || !t.primer.init || t.reap || !Alive(t.ref) || t.Vehicle()[kDead])return false;
    if(Centipede(t.primer.behind))return false;                       // not a tail
    Jet* const front=FrontOf(const_cast<Jet*>(&t));
    if(front==&j)return false;                                        // its own chain
    const int other=LengthFrom(front);
    return !(other<len || (other==len && IndexOf(*front)>IndexOf(j)) || other+len>Cfg().centipedeLinkMax);
}

// A front (alone or of a chain) looks for a tail to join (MayJoin), the nearest within CentipedeLinkRange, kLookMs
// apart; the one it makes for it keeps while that may still be joined and within twice that.
Jet* TailToJoin(Jet& j,const float* pos,int len,ULONGLONG ms) noexcept {
    PrimerState& s=j.primer;
    if(Jet* t=Centipede(s.joining)) {
        const float* tp=reinterpret_cast<const float*>(t->Vehicle()+kPosition);
        const float d[3]={tp[0]-pos[0],tp[1]-pos[1],tp[2]-pos[2]};
        if(MayJoin(j,*t,len) && Len(d)<Cfg().centipedeLinkRange*2.0f)return t;
        s.joining=nullptr;
    }
    if(ms-s.lookAt<kLookMs)return nullptr;
    s.lookAt=ms;
    Jet* best=nullptr;
    float bestD=Cfg().centipedeLinkRange;
    for(auto& t:jets) {
        if(!MayJoin(j,t,len))continue;
        const float* tp=reinterpret_cast<const float*>(t.Vehicle()+kPosition);
        const float d[3]={tp[0]-pos[0],tp[1]-pos[1],tp[2]-pos[2]};
        if(Len(d)<bestD){bestD=Len(d);best=&t;}
    }
    if(best && best->ref.ctrl!=s.joining)Log("PRIMER v=%p centipede (%d long) makes for the tail %p (%.0f m)",j.Vehicle(),len,best->Vehicle(),bestD);
    s.joining=best ? best->ref.ctrl : nullptr;
    return best;
}

// Velocity onto `goal` riding along `with` (a follower's: the one ahead's velocity plus the pull onto its point).
void Follow(Jet& j,const float* pos,const float* goal,const float* with) noexcept {
    float pull[3]={(goal[0]-pos[0])*kFollowGain,(goal[1]-pos[1])*kFollowGain,(goal[2]-pos[2])*kFollowGain};
    const float p=Len(pull);
    if(p>kFollowCatch)for(int i=0;i<3;++i)pull[i]*=kFollowCatch/p;
    for(int i=0;i<3;++i)j.m.vel[i]=with[i]+pull[i];
    const float s=Len(j.m.vel);
    if(s>kFollowTop)for(int i=0;i<3;++i)j.m.vel[i]*=kFollowTop/s;
}

// A split's headless new front (while its head grows back): its way off (kWritheDrag a second of its speed lost),
// in the air sinking at kWritheSink, its nose kept where it points.
void Writhe(Jet& j,float dt,ULONGLONG ms,float* face) noexcept {
    const float keep=std::exp(-kWritheDrag*dt);
    j.m.vel[0]*=keep;j.m.vel[2]*=keep;
    j.m.vel[1]=j.primer.flying ? -kWritheSink : j.m.vel[1]*keep;
    const float* m=reinterpret_cast<const float*>(j.Vehicle()+kMatrix);
    // the way it pointed when the link broke, swung kWritheYaw each way (about it, not a turn that adds up)
    if(!j.primer.writheFace[0] && !j.primer.writheFace[2]){j.primer.writheFace[0]=m[8];j.primer.writheFace[2]=m[10];}
    const float a=kWritheYaw*std::sin(2.0f*kPi*kWritheYawHz*static_cast<float>(ms)*0.001f+j.primer.seed);
    const float c=std::cos(a),sn=std::sin(a),*w=j.primer.writheFace;
    face[0]=w[0]*c+w[2]*sn;face[1]=0.0f;face[2]=w[2]*c-w[0]*sn;
}

// A lone one on the ground: at the player in a weave, round them close; its nose on them once near.
void Crawl(Jet& j,const Kind& k,unsigned char* v,const float* pos,const float* aim,bool hasAim,float dt,ULONGLONG ms,float* face) noexcept {
    PrimerState& s=j.primer;
    const float* centre=hasAim ? player.pos : j.anchor;
    float to[3]={centre[0]-pos[0],0.0f,centre[2]-pos[2]};
    const float d=Len(to);
    float goal[3];
    if(d>kCrawlNear*1.5f && Normalize(to)) {
        const float side=kWeave*std::sin(2.0f*kPi*kWeaveHz*static_cast<float>(ms)*0.001f+s.seed);
        for(int i=0;i<3;++i)goal[i]=pos[i]+to[i]*30.0f;
        goal[0]+=to[2]*side;goal[2]-=to[0]*side;
        s.orbit=std::atan2(pos[2]-centre[2],pos[0]-centre[0]);
    } else {
        s.orbit+=kCrawlSpeed/kCrawlNear*dt;
        goal[0]=centre[0]+std::cos(s.orbit)*kCrawlNear;goal[2]=centre[2]+std::sin(s.orbit)*kCrawlNear;goal[1]=pos[1];
    }
    OnGround(goal,kCrawlClear);
    Hover(j,k,v,pos,goal,goal,kCrawlSpeed,kHoverClimb,dt);
    if(hasAim && d<kSpitReach){for(int i=0;i<3;++i)face[i]=aim[i]-pos[i];}
    else{float vel[3]={j.m.vel[0],0.0f,j.m.vel[2]};if(Normalize(vel))std::memcpy(face,vel,12);}
}

// The front of a chain in the air: winding round the player kAirRange out, kAirHeight up (with a swell), now
// and then diving low through them to as far out on the other side.
void Wind(Jet& j,const Kind& k,unsigned char* v,const float* pos,bool hasAim,float dt,ULONGLONG ms,float* face) noexcept {
    PrimerState& s=j.primer;
    const float* centre=hasAim ? player.pos : j.anchor;
    float goal[3];
    float speed=kAirSpeed;
    if(s.dive) {
        if(HorizDist(pos,s.diveTo)<kDiveDone || ms-s.diveAt>kDiveMaxMs) {
            s.dive=false;s.orbit=std::atan2(pos[2]-centre[2],pos[0]-centre[0]);
        }
    } else if(hasAim && ms-s.diveAt>kDiveEveryMs) {
        float across[3]={centre[0]-pos[0],0.0f,centre[2]-pos[2]};
        if(Normalize(across)) {
            s.dive=true;s.diveAt=ms;
            s.diveTo[0]=centre[0]+across[0]*kAirRange;s.diveTo[2]=centre[2]+across[2]*kAirRange;s.diveTo[1]=centre[1]+kDiveLow;
            Log("PRIMER v=%p centipede (%d long) dives through the player",v,LengthFrom(&j));
        }
    }
    if(s.dive){std::memcpy(goal,s.diveTo,12);speed=kAirSpeed*1.5f;}
    else {
        s.orbit+=kAirSpeed/kAirRange*dt;
        goal[0]=centre[0]+std::cos(s.orbit)*kAirRange;goal[2]=centre[2]+std::sin(s.orbit)*kAirRange;
        goal[1]=centre[1]+kAirHeight+kAirSwell*std::sin(static_cast<float>(ms)*0.0005f+s.seed);
    }
    OverGround(goal,kDiveLow);
    Hover(j,k,v,pos,goal,goal,speed,kHoverClimb*1.5f,dt);
    float vel[3]={j.m.vel[0],j.m.vel[1],j.m.vel[2]};
    if(Normalize(vel))std::memcpy(face,vel,12);
}

// Its weapons by its place in the chain: a head (shown: a front, alone or of a chain) spits forward at the target
// in front of it; a tail (shown) lobs its stinger onto it; one with neither (a middle one) turns the barbs on its
// back to it and fires them. Each fires only with its barrel on its line (BarrelAlong: the mount as the game posed
// it). What it fired (1 spit, 2 stinger, 4 barbs).
int CentipedeFire(Jet& j,unsigned char* v,const float* pos,const float* aim,bool arm,bool head,bool tail) noexcept {
    int fired=0;
    const float to[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
    const float across=std::sqrt(to[0]*to[0]+to[2]*to[2]),dist=Len(to);
    // the barbs (a middle one's): their mount on the target
    const bool middle=!head && !tail;
    AimPart(j,v,kAimGun,middle && arm ? to : nullptr);
    const bool barbs=middle && arm && dist<kBarbReach && BarrelAlong(v,kBarbHolder,to,kBarbCone);
    if(unsigned char* const w=HolderWeapon(v,kBarbHolder); w && Readable(w+kWeaponTrigger,1,true))w[kWeaponTrigger]=barbs ? 1 : 0;
    // the stinger (a tail's): up its lob's arc
    float elevation=0.0f;
    const bool lob=tail && arm && dist<kStingReach && across>1.0f &&
                   primer::LobElevation(across,to[1],kStingSpeed,kGravityLob,kStingMost,&elevation);
    float up[3]={0.0f,1.0f,0.0f};
    if(lob){up[0]=to[0]/across*std::cos(elevation);up[1]=std::sin(elevation);up[2]=to[2]/across*std::cos(elevation);}
    AimPart(j,v,kAimSting,lob ? up : nullptr);
    const bool sting=lob && BarrelAlong(v,2,up,kStingCone);
    // the spit (a head's): forward, its nose on the target
    const bool spit=head && arm && OnTarget(v,pos,aim,kSpitReach,kSpitCone);
    v[kFireGun]=spit ? 1 : 0;v[kFireMissile]=sting ? 1 : 0;
    fired|=spit ? 1 : 0;fired|=sting ? 2 : 0;fired|=barbs ? 4 : 0;
    return fired;
}

void CentipedeFrame(Jet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept {
    PrimerState& s=j.primer;
    const Kind& k=KindOf(j);
    Trail(j,pos);
    float aim[3];
    const bool hasAim=PlayerAim(aim,ms);
    Jet* ahead=Centipede(s.ahead);
    if(s.ahead && !ahead)PrimerUnlinked(j,true,ms);
    if(s.behind && !Centipede(s.behind))PrimerUnlinked(j,false,ms);
    // How much of its head and tail shows (src/primer_pose.h): hidden while linked, growing back after a split.
    const float regrowMs=primer::kRegrowSec*1000.0f;
    float headShows=s.ahead ? 0.0f : 1.0f,tailShows=s.behind ? 0.0f : 1.0f;
    if(s.regrowAt) {
        headShows=static_cast<float>(ms-s.regrowAt)/regrowMs;
        if(headShows>=1.0f){headShows=1.0f;s.regrowAt=0;Log("PRIMER v=%p centipede: its head has grown back: a front",v);}
    }
    if(s.tailAt) {
        tailShows=static_cast<float>(ms-s.tailAt)/(regrowMs*primer::kTailRegrowShare);
        if(tailShows>=1.0f){tailShows=1.0f;s.tailAt=0;}
    }
    const bool regrowing=s.regrowAt!=0;
    if(!regrowing)s.writheFace[0]=s.writheFace[2]=0.0f;
    float face[3]={0.0f,0.0f,1.0f};
    const char* what;
    if(regrowing) {                              // headless: writhing, slowing, sinking; out of the fight
        Writhe(j,dt,ms,face);
        what="regrowing";
    } else if(ahead) {                                  // linked: on the trail of the one ahead
        float goal[3];
        TrailPoint(*ahead,kLinkSpacing,goal);
        Follow(j,pos,goal,ahead->m.vel);
        const float* ap=reinterpret_cast<const float*>(ahead->Vehicle()+kPosition);
        for(int i=0;i<3;++i)face[i]=ap[i]-pos[i];
        s.flying=ahead->primer.flying;
        what="linked";
    } else {                                     // a front
        const int len=LengthFrom(&j);
        s.flying=len>=2;
        Jet* tail=len<Cfg().centipedeLinkMax ? TailToJoin(j,pos,len,ms) : nullptr;
        if(tail) {                               // making for a tail to join
            float goal[3];
            TrailPoint(*tail,kLinkSpacing,goal);
            const float to[3]={goal[0]-pos[0],goal[1]-pos[1],goal[2]-pos[2]};
            if(Len(to)<kJoinAt) {
                s.ahead=tail->ref.ctrl;tail->primer.behind=j.ref.ctrl;s.joining=nullptr;
                Log("PRIMER v=%p centipede linked behind %p: %d long",v,tail->Vehicle(),LengthFrom(FrontOf(&j)));
            }
            // Near it, it rides along with the tail (its velocity plus the pull onto the point, as a follower):
            // Hover only flies at a point, so it would hang back v^2 / 2 brake behind a moving one and never link.
            if(Len(to)<kJoinRide)Follow(j,pos,goal,tail->m.vel);
            else {
                const float speed=Len(tail->m.vel)*1.3f>kJoinSpeed ? Len(tail->m.vel)*1.3f : kJoinSpeed;
                Hover(j,k,v,pos,goal,goal,speed,kHoverClimb*2.0f,dt);
            }
            for(int i=0;i<3;++i)face[i]=to[i];
            what="joining";
        } else if(s.flying){Wind(j,k,v,pos,hasAim,dt,ms,face);what="front (flying)";}
        else{Crawl(j,k,v,pos,aim,hasAim,dt,ms,face);what="crawling";}
    }
    HoldOffGround(j,pos,GroundClearance(pos),dt,ms);
    Face(j,v,face);
    // Its parts: its legs a step behind the one ahead's (the wave down the chain; a front's at its own speed), its
    // head hidden behind another, its tail before one.
    primer::CentipedeInput in{static_cast<float>(ms%600000)*0.001f,Len(j.m.vel),s.flying,headShows,tailShows,
                              regrowing ? 1.0f-headShows : 0.0f,-1.0f};
    s.phase=ahead && !regrowing ? ahead->primer.phase-2.0f*kPi*primer::kChainLag : primer::CentipedeStep(s.phase,in,dt);
    s.headShown=headShows;s.tailShown=tailShows;
    float angle[primer::kCentipedeBoneCount],angle2[primer::kCentipedeBoneCount],scale[primer::kCentipedeBoneCount];
    primer::CentipedeAngles(in,s.phase,angle,angle2,scale);
    Pose(j,v,primer::kCentipedeBones,primer::kCentipedeBoneCount,angle,angle2,scale,kCentipedeAims,kAimCount);
    s.what=what;
    s.fired=CentipedeFire(j,v,pos,aim,hasAim && !regrowing && Cfg().primerFire,headShows>=1.0f,tailShows>=1.0f);
    Debug(j,v,what,s.fired!=0,ms);
}

// --- The dragonfly ---

// What it hunts: a flying target first (the plugin's friendly jets, the player off the ground) within kPreyRange,
// the nearest; else the player. Its position (a jet's origin, the player's chest) and velocity.
struct Prey { const void* obj; float pos[3],vel[3]; bool air; };
bool PickPrey(const Jet& j,const float* pos,ULONGLONG ms,Prey* out) noexcept {
    float best=kPreyRange;
    bool found=false;
    for(const auto& o:jets) {
        if(!o.ref || IsPrimer(o) || o.reap || !Flown(o,ms) || !Alive(o.ref) || o.Vehicle()[kDead])continue;
        if(At<std::int32_t>(o.Vehicle(),kTeam)==kTeamEnemy)continue;
        const float* p=reinterpret_cast<const float*>(o.Vehicle()+kPosition);
        const float d[3]={p[0]-pos[0],p[1]-pos[1],p[2]-pos[2]};
        if(Len(d)>=best)continue;
        best=Len(d);found=true;
        out->obj=o.ref.obj;std::memcpy(out->pos,p,12);std::memcpy(out->vel,o.m.vel,12);out->air=true;
    }
    float aim[3];
    if(!PlayerAim(aim,ms))return found;
    const float clear=GroundClearance(player.pos);
    const bool flying=clear!=kNoGround && clear>kAirborne;
    const float d[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
    if(found && !(flying && Len(d)<best))return true;
    out->obj=nullptr;std::memcpy(out->pos,aim,12);PlayerVelocity(out->vel);out->air=flying;
    (void)j;
    return true;
}

void DragonflyFrame(Jet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept {
    PrimerState& s=j.primer;
    const Kind& k=KindOf(j);
    Prey prey{};
    const bool has=PickPrey(j,pos,ms,&prey);
    float goal[3],face[3];
    float speed=kHuntSpeed;
    bool arm=false;
    const char* what="patrol";
    if(!has) {   // nothing to hunt: over where it was placed
        std::memcpy(goal,j.anchor,12);goal[1]+=60.0f;
        std::memcpy(face,goal,12);
        s.hunt=0;
    } else {
        const float to[3]={prey.pos[0]-pos[0],prey.pos[1]-pos[1],prey.pos[2]-pos[2]};
        const float dist=Len(to);
        if(s.hunt==1 && ms-s.huntAt>kStrikeMs) {          // strike over: dart off sideways and up
            float side[3]={-to[2],0.0f,to[0]};
            if(!Normalize(side)){side[0]=1;side[2]=0;}
            if(static_cast<int>(s.seed*100.0f+static_cast<float>(ms/1000))%2)for(float& x:side)x=-x;
            for(int i=0;i<3;++i)s.dartTo[i]=pos[i]+(side[i]*0.8f+(i==1 ? 0.6f : 0.0f))*kDartDist;
            s.hunt=2;s.huntAt=ms;
        } else if(s.hunt==2 && ms-s.huntAt>kDartMs) {
            s.hunt=0;s.huntAt=ms;
        } else if(s.hunt==0 && dist<kStrikeRange) {
            s.hunt=1;s.huntAt=ms;
            Log("PRIMER v=%p dragonfly strikes at %s %.0f m off",v,prey.obj ? "a jet" : prey.air ? "the flying player" : "the player",dist);
        }
        float back[3]={-prey.vel[0],0.0f,-prey.vel[2]};
        if(!Normalize(back)){back[0]=-to[0];back[1]=0.0f;back[2]=-to[2];if(!Normalize(back)){back[0]=0;back[2]=-1;}}
        const float under=prey.air ? -kBelow : kAbove;
        if(s.hunt==2) {
            std::memcpy(goal,s.dartTo,12);speed=kDartSpeed;
            // Facing the way it flies; still (the frame its strike ended), the way its nose points.
            const float* m=reinterpret_cast<const float*>(v+kMatrix);
            float way[3]={m[8],m[9],m[10]};
            float vel[3]={j.m.vel[0],j.m.vel[1],j.m.vel[2]};
            if(Normalize(vel))std::memcpy(way,vel,12);
            for(int i=0;i<3;++i)face[i]=pos[i]+way[i]*50.0f;
            what="dart";
        } else if(s.hunt==1) {
            // Held kStandoff off on its own side, under (a flyer) or over (on the ground); its nose on the target.
            float from[3]={-to[0],-to[1],-to[2]};
            if(!Normalize(from))std::memcpy(from,back,12);
            for(int i=0;i<3;++i)goal[i]=prey.pos[i]+prey.vel[i]*0.3f+from[i]*kStandoff;
            goal[1]=prey.pos[1]+under;
            std::memcpy(face,prey.pos,12);
            arm=true;
            what="strike";
        } else {
            // The interception: where the target will be when it gets there, from below and behind.
            const float tgo=Clamp(dist/kHuntSpeed,0.0f,3.0f);
            for(int i=0;i<3;++i)goal[i]=prey.pos[i]+prey.vel[i]*tgo+back[i]*kBehind;
            goal[1]+=under;
            std::memcpy(face,goal,12);
            what="hunt";
        }
    }
    OverGround(goal,kHuntClear);
    Hover(j,k,v,pos,goal,face,speed,kHoverClimb*3.0f,dt);
    HoldOffGround(j,pos,GroundClearance(pos),dt,ms);
    const float dir[3]={face[0]-pos[0],face[1]-pos[1],face[2]-pos[2]};
    Face(j,v,dir);
    const primer::DragonflyInput in{static_cast<float>(ms%600000)*0.001f,arm};
    s.curl=primer::CurlStep(s.curl,in,dt);
    float angle[primer::kDragonflyBoneCount];
    primer::DragonflyAngles(in,s.curl,angle);
    Pose(j,v,primer::kDragonflyBones,primer::kDragonflyBoneCount,angle,nullptr,nullptr);
    const bool fire=Cfg().primerFire && arm && (!s.posed || s.curl>=primer::kCurlFire) &&
                    OnTarget(v,pos,prey.pos,kNeedleReach,kNeedleCone);
    v[kFireGun]=fire ? 1 : 0;v[kFireMissile]=0;
    s.what=what;s.fired=fire ? 1 : 0;
    Debug(j,v,what,fire,ms);
}

// --- The trace (PrimerTrace) ---
// One CSV line per creature every kTraceMs: game ms, its table index, kind, what it did, position, velocity, HP,
// what it fired (1 spit / needles, 2 stinger, 4 barbs), the table indexes of the centipedes ahead of and behind it (-1: none), the player's position.
// Opened on the first line (appended to; a header first when the file is new), flushed once a second.
std::FILE* traceFile=nullptr;
bool traceFailed=false;
ULONGLONG traceFlushAt=0;

std::FILE* TraceFile() noexcept {
    if(traceFile || traceFailed)return traceFile;
    wchar_t path[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(path,L'\\') : nullptr;
    if(!slash || (*slash=0,wcscat_s(path,L"\\Mods\\Plugins\\EDF6VehicleCrew.primer.csv"))!=0){traceFailed=true;return nullptr;}
    const bool fresh=GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES;
    if(_wfopen_s(&traceFile,path,L"ab")!=0 || !traceFile){traceFile=nullptr;traceFailed=true;Log("PRIMER trace: cannot open %ls",path);return nullptr;}
    if(fresh)std::fprintf(traceFile,"ms,id,kind,what,x,y,z,vx,vy,vz,hp,fire,ahead,behind,px,py,pz\n");
    Log("PRIMER trace: writing %ls",path);
    return traceFile;
}

int IndexOfCtrl(const void* ctrl) noexcept {
    for(int i=0;i<kMaxJets;++i)if(ctrl && jets[i].ref && jets[i].ref.ctrl==ctrl)return i;
    return -1;
}

void Trace(Jet& j,const float* pos,const unsigned char* v,ULONGLONG ms) noexcept {
    if(!Cfg().primerTrace || ms-j.primer.traceAt<kTraceMs)return;
    j.primer.traceAt=ms;
    std::FILE* const f=TraceFile();
    if(!f)return;
    std::fprintf(f,"%llu,%d,%s,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.0f,%d,%d,%d,%.2f,%.2f,%.2f\n",ms,IndexOf(j),KindOf(j).name,
                 j.primer.what ? j.primer.what : "-",pos[0],pos[1],pos[2],j.m.vel[0],j.m.vel[1],j.m.vel[2],At<float>(v,kHp),
                 j.primer.fired,IndexOfCtrl(j.primer.ahead),IndexOfCtrl(j.primer.behind),player.pos[0],player.pos[1],player.pos[2]);
    if(ms-traceFlushAt>1000){traceFlushAt=ms;std::fflush(f);}
}
}  // namespace


void PrimerUnlinked(Jet& j,bool front,ULONGLONG ms) noexcept {
    PrimerState& s=j.primer;
    if(front) {
        if(!s.ahead)return;
        // A front again, headless at first (its head grows back: CentipedeFrame); its winding then starts from
        // where it is (what it held from its first frame is stale).
        s.ahead=nullptr;s.regrowAt=ms ? ms : 1;s.joining=nullptr;
        float at[3];
        const float* pos=reinterpret_cast<const float*>(j.Vehicle()+kPosition);
        if(PlayerAim(at,ms))s.orbit=std::atan2(pos[2]-at[2],pos[0]-at[0]);
        s.dive=false;s.diveAt=ms;
        Log("PRIMER v=%p centipede: the one ahead of it is gone: headless, its head grows back",j.Vehicle());
    } else {
        if(!s.behind)return;
        s.behind=nullptr;s.tailAt=ms ? ms : 1;
    }
}

void PrimerTeam(unsigned char* v) noexcept {
    for(unsigned i=0;i<SeatCount(v);++i) {
        unsigned char* const seat=SeatAt(v,i);
        if(SeatRider(seat)!=Rider::dummy)continue;
        const auto rider=At<unsigned char*>(seat,kSeatRider);
        if(rider && !OnEnemySide(rider))reinterpret_cast<SetTeamFn>(image+kSetTeam)(rider,kTeamEnemy,false);
    }
    if(!OnEnemySide(v))SetObjectTeam(v,kTeamEnemy);
}

}  // namespace jet

// Its damage message (body506's hook, before the stock handler): a centipede growing its head back takes
// CentipedeWoundDamage times the damage (the hit's GameDamageInfo +0x50, docs/subcarrier-re.md §8.1, put back right
// after the stock handler: that copy is the queue's own). Every other message, and every other jet, as it came.
bool PrimerMessage(unsigned char* v,std::uint32_t msg,void* data,MessageRestore* restore) noexcept {
    constexpr std::size_t kDamage=0x50;
    if(msg!=kMsgDamage || !data || !Cfg().enabled || !Cfg().primer || v[kDead])return false;
    constexpr std::size_t kHitPoint=0x30;   // the round's point of impact or the blast's centre (docs/subcarrier-re.md §8.1)
    jet::Jet* const j=jet::FindJet(v);
    if(!j || !jet::IsPrimer(*j))return false;
    float* const damage=reinterpret_cast<float*>(static_cast<unsigned char*>(data)+kDamage);
    if(!(*damage>0.0f))return false;   // healing, or nothing
    // the hit, for its frame's splash (BleedHits: not from here, the message is no place to make objects)
    std::memcpy(j->primer.bloodAt,static_cast<unsigned char*>(data)+kHitPoint,12);
    j->primer.bloodDamage+=*damage;
    if(j->role!=jet::Role::centipede || !j->primer.regrowAt || Cfg().centipedeWoundDamage==1.0f)return false;
    restore->at=damage;restore->was=*damage;
    *damage*=Cfg().centipedeWoundDamage;
    return false;
}

namespace jet {
void PrimerFrame(Jet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept {
    if(!Cfg().primer) {
        if(!j.reap){j.reap=true;j.why="Primer off";}
        v[kFireGun]=0;v[kFireMissile]=0;
        return;
    }
    if(!j.primer.init)Init(j,v,pos,ms);
    else PrimerTeam(v);
    j.m.ready=true;
    BleedHits(j,v,pos);
    if(IsCentipede(j))CentipedeFrame(j,v,pos,dt,ms);
    else DragonflyFrame(j,v,pos,dt,ms);
    Trace(j,pos,v,ms);
}

// --- The dead ---
// A shot-down centipede's entry goes (Sweep: Finished), but its wreck falls on (the 506's crash): it keeps its bone
// records here, with a weak reference of its own on the object's control block, and each physics step of the dead
// body (jet_hooks.cpp JetBodyStep) poses it dead (primer_pose.h: legs folding in and kicking, head drooping, tail
// curling) until kCorpseMs or the object is gone.
namespace {
constexpr int kCorpses=24;
constexpr ULONGLONG kCorpseMs=10000;
struct Corpse {
    ObjRef ref;
    ULONGLONG diedAt;
    const unsigned char* model;   // the bone array its records are in (another: not posed on)
    unsigned char* rec[primer::kCentipedeBoneCount];
    float bind[primer::kCentipedeBoneCount][16];
    float head,tail;
};
Corpse corpses[kCorpses];

void Bury(Corpse& c) noexcept {
    DropRef(c.ref);
    c=Corpse{};
}
}  // namespace

bool PrimerDied(const Jet& j) noexcept {
    const PrimerState& s=j.primer;
    if(!Alive(j.ref) || !j.Vehicle()[kDead])return false;
    BleedOut(j,j.Vehicle());   // Release runs from the update (Sweep): the game's thread
    if(!IsCentipede(j) || !s.posed)return false;
    Corpse* slot=nullptr;
    for(auto& c:corpses)if(!c.ref.obj || !Alive(c.ref)){if(c.ref.obj)Bury(c);slot=&c;break;}
    if(!slot)return false;   // as many falling as that: this one falls stiff
    slot->ref=j.ref;HoldRef(j.ref);
    slot->diedAt=GameMs();
    slot->model=s.poseModel;
    for(int i=0;i<primer::kCentipedeBoneCount;++i){slot->rec[i]=s.poseRec[i];std::memcpy(slot->bind[i],s.poseBind[i],64);}
    slot->head=s.headShown;slot->tail=s.tailShown;
    return true;
}

void PrimerCorpseStep(unsigned char* v) noexcept {
    for(auto& c:corpses) {
        if(c.ref.obj!=v)continue;
        const ULONGLONG ms=GameMs();
        if(!Alive(c.ref) || ms-c.diedAt>kCorpseMs ||
           At<const unsigned char*>(v+kModelInst506,kInstBones506)!=c.model){Bury(c);return;}
        primer::CentipedeInput in{static_cast<float>(ms%600000)*0.001f,0.0f,false,c.head,c.tail,0.0f,
                                  static_cast<float>(ms-c.diedAt)*0.001f};
        float angle[primer::kCentipedeBoneCount],angle2[primer::kCentipedeBoneCount],scale[primer::kCentipedeBoneCount];
        primer::CentipedeAngles(in,0.0f,angle,angle2,scale);
        for(int i=0;i<primer::kCentipedeBoneCount;++i) {
            alignas(16) float local[16];
            const auto& b=primer::kCentipedeBones[i];
            primer::TurnLocal2(c.bind[i],b.axis,angle[i],b.axis2,angle2[i],local,scale[i]);
            std::memcpy(c.rec[i]+kBoneLocal506,local,64);
        }
        return;
    }
}

void ResetCorpses() noexcept {
    for(auto& c:corpses)if(c.ref.obj)Bury(c);
}
}  // namespace jet
}  // namespace crew
