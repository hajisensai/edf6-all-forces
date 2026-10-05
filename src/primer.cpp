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
//    join it (at most CentipedeLinkMax). One shot out of the middle splits it there: the part behind gets a head
//    again (its new front), and one left alone comes down to crawl.
//  - The dragonfly (EDF6VC_DRAGONFLY, mark 7013): an air superiority fighter that hunts as a dragonfly does. It
//    goes for flying targets first (the plugin's friendly jets, the player off the ground) and the player on the
//    ground otherwise: it flies an interception (where the target will be), comes up from below and behind,
//    holds there kStandoff off with its abdomen curled at it (src/primer_pose.h: it fires only once curled: the
//    warning) and fires its needles, then darts off sideways and up and comes round again.
// Both die as any 506 (the stock crash). Only the local player is aimed at. Game thread, under the input hook's
// guard. All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"
#include "primer_pose.h"
#include <cwchar>

namespace crew {
namespace jet {
namespace {
constexpr std::size_t kOwnTeam=0x318;
constexpr ULONGLONG kLogMs=2000;
constexpr float kChest=1.2f;         // m: what is aimed at over the player's feet
constexpr float kMaxPitch=0.8f;      // rad: the most a body pitches its nose at a target
constexpr float kAimGain=4.0f;
// The centipede (see the file's head).
// m between linked bodies' origins: five segments 1.8 m apart each (pylib/centipede_model.py), so the segments run
// on 1.8 m apart across the links too (tools/primer_chain_view.py reads it).
constexpr float kLinkSpacing=9.0f;
constexpr float kTrailStep=1.0f;     // m of travel between the trail's samples (kPrimerTrail of them)
constexpr float kCrawlClear=1.5f;    // m its origin rides over the ground crawling (its legs reach 1.35 down)
constexpr float kCrawlSpeed=16.0f,kCrawlNear=45.0f,kWeave=20.0f,kWeaveHz=0.12f;
constexpr float kAirRange=130.0f,kAirHeight=45.0f,kAirSpeed=32.0f,kAirSwell=12.0f;
constexpr ULONGLONG kDiveEveryMs=22000,kDiveMaxMs=12000;
constexpr float kDiveLow=12.0f,kDiveDone=30.0f;
constexpr float kFollowGain=3.0f,kFollowCatch=40.0f,kFollowTop=80.0f;
// m from its link point it links; m/s at least on its way there; m from it on it rides along with the tail.
constexpr float kJoinAt=4.0f,kJoinSpeed=28.0f,kJoinRide=40.0f;
constexpr ULONGLONG kLookMs=1000;
constexpr float kSpitReach=400.0f,kSpitCone=0.12f;   // pylib/vcobjects.py PRIMER_GUN_FILES: 3.5 x 120 m
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

// Its moving parts: the bone records of `bones` (looked up again when the model's bone array changes; every one
// found or none posed: logged once), each written R(angle) x bind, scaled.
void Pose(Jet& j,unsigned char* v,const primer::PoseBone* bones,int n,const float* angle,const float* scale) noexcept {
    PrimerState& s=j.primer;
    const unsigned char* const inst=v+kModelInst506;
    const auto array=At<const unsigned char*>(inst,kInstBones506);
    if(!array)return;
    if(array!=s.poseModel) {
        s.poseModel=array;s.posed=true;
        for(int i=0;i<n;++i) {
            s.poseRec[i]=BoneRecord506(inst,bones[i].name);
            if(!s.poseRec[i]){s.posed=false;continue;}
            std::memcpy(s.poseBind[i],s.poseRec[i]+kBoneLocal506,64);
        }
        if(!s.posed)Log("PRIMER v=%p %s: its model has not its moving parts: not posed",v,KindOf(j).name);
    }
    if(!s.posed)return;
    for(int i=0;i<n;++i) {
        alignas(16) float local[16];
        primer::TurnLocal(s.poseBind[i],bones[i].axis,angle[i],local,scale ? scale[i] : 1.0f);
        std::memcpy(s.poseRec[i]+kBoneLocal506,local,64);
    }
}
static_assert(primer::kCentipedeBoneCount<=kPrimerParts && primer::kDragonflyBoneCount<=kPrimerParts,"PrimerState holds the parts");

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

void CentipedeFrame(Jet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept {
    PrimerState& s=j.primer;
    const Kind& k=KindOf(j);
    Trail(j,pos);
    float aim[3];
    const bool hasAim=PlayerAim(aim,ms);
    Jet* ahead=Centipede(s.ahead);
    if(s.ahead && !ahead) {
        // A front again: its winding starts from where it is (what it held from its first frame is stale).
        s.ahead=nullptr;
        float at[3];
        if(PlayerAim(at,ms))s.orbit=std::atan2(pos[2]-at[2],pos[0]-at[0]);
        s.dive=false;s.diveAt=ms;
        Log("PRIMER v=%p centipede: the one ahead of it is gone: a front again",v);
    }
    if(s.behind && !Centipede(s.behind))s.behind=nullptr;
    float face[3]={0.0f,0.0f,1.0f};
    const char* what;
    if(ahead) {                                  // linked: on the trail of the one ahead
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
    // Its parts: the legs' wave at its speed, its head hidden behind another, its tail before one, its halves bent
    // toward them.
    primer::CentipedeInput in{static_cast<float>(ms%600000)*0.001f,Len(j.m.vel),s.flying,s.ahead!=nullptr,s.behind!=nullptr,0.0f,0.0f};
    {
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        const float right[3]={m[0],m[1],m[2]},fwd[3]={m[8],m[9],m[10]};
        if(const Jet* a=Centipede(s.ahead)) {
            const float* p=reinterpret_cast<const float*>(a->Vehicle()+kPosition);
            const float d[3]={p[0]-pos[0],p[1]-pos[1],p[2]-pos[2]};
            in.bendFront=primer::LinkBend(Dot(d,right),Dot(d,fwd));
        }
        if(const Jet* b=Centipede(s.behind)) {
            const float* p=reinterpret_cast<const float*>(b->Vehicle()+kPosition);
            const float d[3]={p[0]-pos[0],p[1]-pos[1],p[2]-pos[2]};
            in.bendRear=-primer::LinkBend(Dot(d,right),-Dot(d,fwd));
        }
    }
    s.phase=primer::CentipedeStep(s.phase,in,dt);
    float angle[primer::kCentipedeBoneCount],scale[primer::kCentipedeBoneCount];
    primer::CentipedeAngles(in,s.phase,angle,scale);
    Pose(j,v,primer::kCentipedeBones,primer::kCentipedeBoneCount,angle,scale);
    const bool fire=Cfg().primerFire && hasAim && OnTarget(v,pos,aim,kSpitReach,kSpitCone);
    v[kFireGun]=fire ? 1 : 0;v[kFireMissile]=0;
    Debug(j,v,what,fire,ms);
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
    Pose(j,v,primer::kDragonflyBones,primer::kDragonflyBoneCount,angle,nullptr);
    const bool fire=Cfg().primerFire && arm && (!s.posed || s.curl>=primer::kCurlFire) &&
                    OnTarget(v,pos,prey.pos,kNeedleReach,kNeedleCone);
    v[kFireGun]=fire ? 1 : 0;v[kFireMissile]=0;
    Debug(j,v,what,fire,ms);
}
}  // namespace

void PrimerTeam(unsigned char* v) noexcept {
    for(unsigned i=0;i<SeatCount(v);++i) {
        unsigned char* const seat=SeatAt(v,i);
        if(SeatRider(seat)!=Rider::dummy)continue;
        const auto rider=At<unsigned char*>(seat,kSeatRider);
        if(rider && !OnEnemySide(rider))reinterpret_cast<SetTeamFn>(image+kSetTeam)(rider,kTeamEnemy,false);
    }
    if(!OnEnemySide(v))SetObjectTeam(v,kTeamEnemy);
}

void PrimerFrame(Jet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept {
    if(!Cfg().primer) {
        if(!j.reap){j.reap=true;j.why="Primer off";}
        v[kFireGun]=0;v[kFireMissile]=0;
        return;
    }
    if(!j.primer.init)Init(j,v,pos,ms);
    else PrimerTeam(v);
    j.m.ready=true;
    if(IsCentipede(j))CentipedeFrame(j,v,pos,dt,ms);
    else DragonflyFrame(j,v,pos,dt,ms);
}
}  // namespace jet
}  // namespace crew
