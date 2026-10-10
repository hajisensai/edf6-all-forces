// NPC jets' flight (jet.cpp): a wing's guidance and attitude (JetSteer, Attitude, its elevons), a rotor craft's
// (Hover, the carrier's nacelles), and what keeps either flying: the ground, the ceiling, the world's walls and
// the walls it learns.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"
#include "gear.h"
#include "jet_pullout.h"
#include "hover_lift.h"
#include "nacelle_reach.h"
#include <cwchar>

namespace crew {
namespace jet {
namespace {
// The stock input (0x6543A0) pushes the body under the ceiling *(*(image+kCeiling)+0x3C) every frame.
constexpr std::size_t kCeiling=0x20B2998,kCeilingY=0x3C;
// m/s^2 of speed lost per g pulled over 1 (induced drag): with thrust that sustains 1 + thrust/kTurnBleed
// g (strike ~4.3, fighter 6); harder it slows, and the turn tightens, as a wing does.
constexpr float kTurnBleed=3.0f;
// s: the path turns toward `want` at angle/kSteerTau (at most maxG). Turning at maxG until within a frame's
// turn and then snapping onto it (as before) asked full bank one frame and none the next: in the patrol
// circle the jets flicked between level and knife edge.
constexpr float kSteerTau=0.5f;
// On the bombing run it turns at most kBombG and closes on the line over kBombTau: the stock bomber flies
// dead straight; at the strike jet's 5 g and 0.5 s the BOMBER401 (120 m/s) rolled past 90 degrees
// righting its line on 2026-10-03.
constexpr float kBombG=1.5f,kBombTau=2.0f;
// kBodyTop (jet_internal.h), 250 m/s = 900 km/h: Havok caps every dynamic body at its motion properties'
// maxLinearSpeed (hknpMotionProperties+0x10, 200 m/s in the preset the vehicles use): the 18:58 run's jets
// commanded 260-360 m/s flew 200-211, so turn radius, lead and bomb release were planned for a speed never
// flown. Since 2026-10-04 a jet body gets its own copy of the motion properties with a 600 m/s cap
// (jetprops.cpp), so the old 200 m/s Havok cap no longer binds. Past 250 the 5 g turn radius (1275 m) times the
// wall margin (Guard) no longer fits inside the world walls (the play edge, crew.h PlayEdge).
// Patrol: the speed that holds the patrol circle at a 60 degree bank (tan 1.73, 2 g), between kLoiterMin
// times the stall speed and cruise. At cruise the circle took 5 g and 78 degrees of bank the whole time; at
// 45 degrees (until 2026-10-04) it crawled round at 100 m/s.
constexpr float kLoiterTan=1.73f,kLoiterMin=1.15f;
constexpr float kAttGain=6.0f;         // 1/s: the body closes on the attitude it should have this fast
constexpr float kNegG=1.0f;            // g: the most it pushes (lift down the body's up)
constexpr float kPushAngle=0.35f;      // rad: a turn down by less than this is pushed, not rolled into (JetSteer)
constexpr float kLookAhead=2.5f;       // s: the ground and the ceiling are checked this far ahead too
// Angle of attack (JetSteer, PitchBy): the nose rides kAoaPerG (1.5 deg) above the path per g pulled at cruise,
// more as it slows (lift ~ aoa * speed^2), kAoaMin to kAoaMax (-3 to 10 deg), eased over kAoaTau s. Only pitch:
// the body has no sideslip, and its control surfaces/vectoring are not bones it can move (see docs/jet-model-re.md).
constexpr float kAoaPerG=0.026f,kAoaMin=-0.05f,kAoaMax=0.17f,kAoaTau=0.3f;
// The body is held back by what the plugin does not see (the map's edge, buildings, other jets): it is
// blocked once for kBlockedMs it made less than kBlockedPart of the commanded way along it. The command
// then follows the body (along what holds it), and the obstacle, unless another jet or something near the
// ground, is learned as a wall that Guard turns off before. A learned wall is a vertical plane only kWallSpan
// either side of where it was met (a building's face, a stretch of the map's edge: not the whole map cut in two),
// and is forgotten kWallLifeMs after it was last met.
constexpr float kBlockedPart=0.5f,kWallJet=60.0f,kWallGround=40.0f,kWallSame=60.0f,kWallSpan=400.0f;
constexpr ULONGLONG kBlockedMs=250,kWallLifeMs=120000;
// The Havok broadphase ends at 3000 m a side: the play edge (crew.h PlayEdge: 2400 stock, the big map's ground edge)
// keeps the jets in (jet.cpp deletes one past kWorldGoneIn of the world's edge), with its soft edge inside it (SoftEdge,
// airbound.h): no learned wall takes its place.
// Diving it must keep the height a maxG pull-out takes (v^2/(n g) (1 - cos dive)) plus the sink (gravity
// along the path speeding it up) while it gets ready: kReact seconds, and the roll that turns its lift up
// (RollToLift). With kReact alone a fighter diving inverted at an air target began to pull at 75 m, 92 m/s
// down, and went through the ground (2026-10-03).
constexpr float kReact=1.0f;
// Elevons (EDF6VC_JET.MRAB, tools/mdb_jet.py, docs/mdb-format.md §3-4): the tailless bomber's outer
// trailing edges, bones elevon_L/R under bomber501, hinged along their local X. The model instance is at
// veh+kModelInst (bone records at +kInstBones, kBoneStride each, name at +0, local 4x4 at +kBoneLocal,
// count at +kInstBoneCount); local = Rx(theta) x bind (row vectors), theta > 0 = trailing edge up. Both up
// pitch the nose up, opposite they roll: kElevonMax at the full pitch rate (maxG) or roll rate, moved at
// most kElevonRate. The engine makes world = local x parent world each frame.
constexpr std::size_t kModelInst=0xEE0,kInstBones=0x10,kBoneAuto=0x8,kBoneLocal=0x70;   // count, stride: body506.h
// (2026-10-05, the user: the control surfaces should visibly move: kElevonGain times the turn's share of its most, up to
// kElevonMax ~29 deg, kElevonRate fast: an ordinary turn shows them deflected, not a degree or two.)
constexpr float kElevonMax=0.5f,kElevonRate=4.0f,kElevonGain=3.0f;
const wchar_t* const kElevonNames[2]={L"elevon_L",L"elevon_R"};
// The carrier's thrusters (docs/jet-model-re.md §6): the V508 transport's four nacelles, bones boosterF_l/r and
// boosterB_l/r under body, each bound level (nose +z) at its mount, hinged along its local X like the elevons.
// The stock Transporter508 tilts them only by its CAS clips (hover_start: 0 -> -90 deg about X, the nacelle's
// nose up, its thrust along the body's up; hover_end back; fly level), which the carrier (the V506's CAS) never
// plays. The plugin poses them (Thrusters) along the thrust its flight asks for: theta = atan2(-up, forward) of
// that thrust in the body (-90 deg hovering, toward 0 flying forward, past -90 braking), within
// [-kThrustBack, 0], at most kThrustRate; turning, the nacelles on either side tilt kThrustYaw apart (a
// tilt-rotor's yaw in the hover).
constexpr float kThrustBack=nacelle::kMostBack,kThrustRate=0.8f,kThrustYaw=0.25f;
constexpr float kNacelleFar=50.0f;   // m: a bottom this far over the ground leaves the nacelles free (they reach 5.2 m)
const wchar_t* const kThrusterNames[4]={L"boosterF_l",L"boosterF_r",L"boosterB_l",L"boosterB_r"};
// The Primer fighter's wings (Flap): both beat kFlapAmplitude rad about their bones' local X (the model's forward)
// kFlapHz times a second, faster (up to kFlapHzMost) the more it climbs or speeds up; each downstroke lifts it
// (kFlapLift m/s^2 at the stroke's middle, none on the upstroke): it flies in beats. kFlapUp: the sign of theta that
// raises each wing's tip (pylib/primer_fighter_model.py).
const wchar_t* const kWingNames[2]={L"wing_l",L"wing_r"};
constexpr float kFlapAmplitude=0.6f,kFlapHz=2.5f,kFlapHzMost=4.5f,kFlapLift=14.0f;
constexpr float kFlapUp[2]={1.0f,-1.0f};
// The ground under a jet is body506's GroundClearance: under the ground a ray down sees nothing, so a jet that
// went through it was once taken for one over a void, Guard let it be, and it flew on under the map
// (2026-10-03: a fighter 5 s down to -109, drones to -310); its ray from above finds the surface then.

// The walls learned this mission (see kWallSpan): where one was met and its horizontal normal, into it.
struct Wall { float at[3],n[3]; ULONGLONG seen; bool on; };
constexpr int kLearnedWalls=16;
Wall learned[kLearnedWalls]{};

// m from `pos` to wall `w`'s plane along its normal (negative: past it).
float Gap(const Wall& w,const float* pos) noexcept { return (w.at[0]-pos[0])*w.n[0]+(w.at[2]-pos[2])*w.n[2]; }
// Whether learned wall `w` is there for `pos`: met within kWallLifeMs, and `pos` within kWallSpan of where along it.
bool Holds(const Wall& w,const float* pos,ULONGLONG ms) noexcept {
    if(!w.on || ms-w.seen>kWallLifeMs)return false;
    const float along=-(pos[0]-w.at[0])*w.n[2]+(pos[2]-w.at[2])*w.n[0];
    return std::fabs(along)<=kWallSpan;
}

// Learns the wall met at `at` facing `n`, or moves up the one it is (same facing, within kWallSame of its plane
// and kWallSpan along it); a new one takes a forgotten wall's place, else the oldest's.
void LearnWall(const float* at,const float* n,ULONGLONG ms) noexcept {
    for(auto& w:learned) {
        if(!Holds(w,at,ms) || Dot(w.n,n)<0.9f || std::fabs(Gap(w,at))>kWallSame)continue;
        std::memcpy(w.at,at,12);w.seen=ms;
        return;
    }
    Wall* oldest=&learned[0];
    for(auto& w:learned) {
        if(!w.on || ms-w.seen>kWallLifeMs){oldest=&w;break;}
        if(w.seen<oldest->seen)oldest=&w;
    }
    std::memcpy(oldest->at,at,12);std::memcpy(oldest->n,n,12);oldest->seen=ms;oldest->on=true;
    Log("JET wall learned at (%.0f,%.0f,%.0f) facing (%.2f,%.2f)",at[0],at[1],at[2],n[0],n[2]);
}

// Another jet within kWallJet of `pos` (what a jet runs into is no wall).
bool JetNear(const Jet& self,const float* pos,ULONGLONG ms) noexcept {
    for(const auto& o:jets) {
        if(&o==&self || !Flown(o,ms))continue;
        const float d[3]={o.m.prevPos[0]-pos[0],o.m.prevPos[1]-pos[1],o.m.prevPos[2]-pos[2]};
        if(Dot(d,d)<kWallJet*kWallJet)return true;
    }
    return false;
}

// `want` turned off wall `w` from a turn's radius `r` out (more closing fast), never flown into.
void TurnOff(const Wall& w,const Jet& j,const float* pos,float r,float* want,float least=0.0f) noexcept {
    const float gap=Gap(w,pos);
    const float closing=j.m.vel[0]*w.n[0]+j.m.vel[2]*w.n[2];
    const float turn=r*1.5f+(closing>0.0f ? closing*kReact : 0.0f),reach=turn>least ? turn : least;
    if(gap>reach)return;
    const float push=Clamp(1.0f-gap/reach,0.2f,1.0f),into=want[0]*w.n[0]+want[2]*w.n[2];
    if(into<=-push)return;
    want[0]-=w.n[0]*(into+push);want[2]-=w.n[2]*(into+push);
    Normalize(want);
}

// Radians the body must roll before its lift points up out of a dive: from its up to the world's up square
// to its track. Inverted (bank 160-180) that is near pi, 1.2-1.3 s for a fighter, all of it falling.
float RollToLift(const Jet& j) noexcept {
    const float* m=reinterpret_cast<const float*>(j.Vehicle()+kMatrix);
    float up[3]={m[4],m[5],m[6]},dir[3]={j.m.vel[0],j.m.vel[1],j.m.vel[2]};
    if(!Normalize(up) || !Normalize(dir))return 0.0f;
    float lift[3]={-dir[0]*dir[1],1.0f-dir[1]*dir[1],-dir[2]*dir[1]};
    if(!Normalize(lift))return 0.0f;   // straight down: any roll pulls out
    return std::acos(Clamp(Dot(up,lift),-1.0f,1.0f));
}

// A wing's flight step. The lift it wants: gravity held up plus the turn toward `want` (unit), at most
// maxG; `up` gets that lift's direction across the path, where Attitude rolls the body. The lift it gets
// is only along the body's up as it is (`bodyUp`), at most maxG and kNegG pushing: so it banks first and
// then turns, like a wing, instead of sliding round. The speed closes on `speed` at thrust/brake, while
// gravity along the path takes it off climbing and adds it diving.
void JetSteer(Jet& j,const Kind& k,const float* fwd,const float* bodyUp,const float* want,float speed,float dt,float* up) noexcept {
    const bool bombing=j.mode==Mode::bomb;
    // Its stores (BurdenOf): the same wing lifts fewer g of a heavier jet, the same engine speeds it up less, and
    // their drag takes off top speed (drag ~ v^2: the speed it can hold falls as the root of 1 + their share).
    const float mass=j.burden.mass>1.0f ? j.burden.mass : 1.0f;
    const float maxG=(bombing ? kBombG : k.maxG)/mass,tau=bombing ? kBombTau : kSteerTau;
    speed/=std::sqrt(1.0f+(j.burden.drag>0.0f ? j.burden.drag : 0.0f));
    float dir[3]={j.m.vel[0],j.m.vel[1],j.m.vel[2]};
    float s=Len(dir);
    if(s<1.0f || !Normalize(dir)){std::memcpy(dir,fwd,12);s=k.minSpeed;}
    const float most=maxG*kG/(s>k.minSpeed ? s : k.minSpeed)*dt;
    const float angle=std::acos(Clamp(Dot(dir,want),-1.0f,1.0f));
    const float eased=angle*(dt<tau ? dt/tau : 1.0f);
    const float turn=eased<most ? eased : most;
    float next[3];
    if(angle<1e-5f)std::memcpy(next,want,12);
    else {
        float axis[3];Cross(dir,want,axis);
        if(!Normalize(axis)){axis[0]=0;axis[1]=1;axis[2]=0;}
        float side[3];Cross(axis,dir,side);   // in the turn plane, toward `want`
        for(int i=0;i<3;++i)next[i]=dir[i]*std::cos(turn)+side[i]*std::sin(turn);
        Normalize(next);
    }
    float lift[3]={0,kG,0};
    for(int i=0;i<3;++i)lift[i]+=(next[i]-dir[i])/dt*s;
    const float along=Dot(lift,dir);
    for(int i=0;i<3;++i)lift[i]-=dir[i]*along;
    std::memcpy(up,lift,12);
    if(!Normalize(up)){up[0]=0;up[1]=1;up[2]=0;}
    // A small push (the path wanted under it by less than kPushAngle: the lift asked for points down): pushed, upright,
    // at what kNegG gives, instead of rolled inverted to pull. Rolled over for a few degrees, a strike jet rolling in
    // on a dive from 150 or 450 m over its target spent the dive inverted, its nose off the target, and was still
    // inverted where it had to pull out (2026-10-06 offline, tools/jet_obstacle_sim.cpp --selftest; the log's
    // multirole that hit the ground had dived at bank 165-180).
    float sky[3]={-dir[0]*dir[1],1.0f-dir[1]*dir[1],-dir[2]*dir[1]};   // the world's up square to the path
    if(angle<kPushAngle && Dot(up,sky)<0.0f && Normalize(sky)) {
        const float d=Dot(up,dir);
        for(int i=0;i<3;++i)up[i]=-(up[i]-dir[i]*d);
        if(!Normalize(up))std::memcpy(up,sky,12);
    }
    // What the wing gives along the body's up now, and the path that bends.
    const float pull=Clamp(Dot(lift,bodyUp),-kNegG*kG,(bombing ? maxG+1.0f : maxG)*kG);
    float acc[3]={bodyUp[0]*pull,bodyUp[1]*pull-kG,bodyUp[2]*pull};
    const float accAlong=Dot(acc,dir);
    for(int i=0;i<3;++i)next[i]=dir[i]+(acc[i]-dir[i]*accAlong)*dt/s;
    if(!Normalize(next))std::memcpy(next,dir,12);
    const float bleed=pull>kG ? (pull/kG-1.0f)*kTurnBleed : 0.0f;
    s+=Clamp(speed-s,-k.brake*dt,k.thrust*dt/mass)-(kG*next[1]+bleed)*dt;
    const float top=j.m.top>0.0f ? j.m.top : k.attack*1.3f;
    s=Clamp(s,k.minSpeed*0.8f,top<kBodyTop ? top : kBodyTop);
    for(int i=0;i<3;++i)j.m.vel[i]=next[i]*s;
    // The nose rides above the path by what the wing needs: more pulling, more slowly (lift ~ aoa * speed^2).
    const float slow=k.cruise/s;
    const float aoaWant=Clamp(kAoaPerG*(pull/kG)*slow*slow,kAoaMin,kAoaMax);
    j.m.aoa+=(aoaWant-j.m.aoa)*(dt<kAoaTau ? dt/kAoaTau : 1.0f);
}

// `nose` and `up` (unit, square) pitched up by `aoa` about their right.
void PitchBy(float aoa,float* nose,float* up) noexcept {
    const float c=std::cos(aoa),s=std::sin(aoa);
    for(int i=0;i<3;++i){const float n=nose[i],u=up[i];nose[i]=n*c+u*s;up[i]=u*c-n*s;}
}

// The angular velocity that turns the body's rows onto `nose` and `up` (body506 BodyAttitude), kAttGain,
// at most the kind's roll rate.
void Attitude(Jet& j,const Kind& k,const unsigned char* v,const float* nose,const float* up) noexcept {
    BodyAttitude(v,nose,up,kAttGain,k.roll,j.m.omega);
}

// The bone record named `name` in model instance `inst`, or nullptr (body506 BoneRecord506).
unsigned char* BoneRecord(const unsigned char* inst,const wchar_t* name) noexcept { return BoneRecord506(inst,name); }

// The `n` bones `names` of v's model (looked up again only when the model's bone array changes): true with all
// of them there. `what` names them in the log.
bool FindSurfaces(Jet& j,const unsigned char* v,const wchar_t* const* names,int n,const char* what) noexcept {
    const unsigned char* inst=v+kModelInst;
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!bones)return false;
    Surfaces& s=j.surf;
    if(bones==s.model)return s.count==n;
    s=Surfaces{};s.model=bones;s.fresh=true;
    int found=0;
    for(int i=0;i<n;++i) {
        s.rec[i]=BoneRecord(inst,names[i]);
        if(!s.rec[i])continue;
        std::memcpy(s.bind[i],s.rec[i]+kBoneLocal,64);
        ++found;
    }
    s.count=found==n ? n : 0;
    Log("JET v=%p %s: %d of %d found (auto %d/%d) among %d bones",v,what,found,n,s.rec[0] ? s.rec[0][kBoneAuto] : -1,
        s.rec[n-1] ? s.rec[n-1][kBoneAuto] : -1,At<std::int32_t>(inst,kInstBoneCount));
    return s.count==n;
}

// Each found surface turned toward want[i] (rad about its local X, theta > 0: its -z end up) at most `rate`:
// local = Rx(theta) x bind (row vectors; see kElevonMax). The first pose after FindSurfaces snaps.
void PoseSurfaces(Jet& j,const float* want,float rate,float dt,const char* what) noexcept {
    Surfaces& s=j.surf;
    // Something else writing them (the animation) would undo every frame: said once.
    bool rewritten=false;
    for(int i=0;i<s.count;++i)rewritten|=s.written && std::memcmp(s.rec[i]+kBoneLocal,s.set[i],64)!=0;
    if(rewritten && !s.logged){s.logged=true;Log("JET v=%p %s: their local matrices were rewritten by the game between frames",j.Vehicle(),what);}
    for(int i=0;i<s.count;++i) {
        s.at[i]=s.fresh ? want[i] : s.at[i]+Clamp(want[i]-s.at[i],-rate*dt,rate*dt);
        const float co=std::cos(s.at[i]),si=std::sin(s.at[i]);
        const float* b=s.bind[i];
        float* o=s.set[i];
        for(int x=0;x<4;++x) {
            o[x]=b[x];
            o[4+x]=co*b[4+x]+si*b[8+x];
            o[8+x]=-si*b[4+x]+co*b[8+x];
            o[12+x]=b[12+x];
        }
        std::memcpy(s.rec[i]+kBoneLocal,o,64);
    }
    s.fresh=false;s.written=s.count>0;
}

// The Primer fighter's wings beating (see kFlapAmplitude); its velocity lifted by each downstroke.
void Flap(Jet& j,const Kind& k,unsigned char* v,float dt) noexcept {
    if(!FindSurfaces(j,v,kWingNames,2,"wings"))return;
    const float s=Len(j.m.vel),work=Clamp((j.m.vel[1]/10.0f)+(k.attack-s)/k.attack,0.0f,1.0f);
    j.m.flap+=(kFlapHz+(kFlapHzMost-kFlapHz)*work)*2.0f*kPi*dt;
    if(j.m.flap>2.0f*kPi)j.m.flap-=2.0f*kPi;
    const float stroke=std::sin(j.m.flap);   // > 0 going up, < 0 going down
    const float want[2]={kFlapUp[0]*kFlapAmplitude*stroke,kFlapUp[1]*kFlapAmplitude*stroke};
    PoseSurfaces(j,want,100.0f,dt,"wings");
    const float down=-std::cos(j.m.flap);   // the stroke's speed downward: the lift comes then
    if(down>0.0f)j.m.vel[1]+=kFlapLift*down*dt;
}

// The elevons after the commanded turn (see kElevonMax).
void Elevons(Jet& j,const Kind& k,unsigned char* v,float dt) noexcept {
    if(!FindSurfaces(j,v,kElevonNames,2,"elevons"))return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* r=m;const float* u=m+4;const float* f=m+8;   // r: the model's +x, the right wing
    float c[3];
    Cross(j.m.omega,f,c);const float pitch=Dot(c,u);           // nose toward the body's up
    Cross(j.m.omega,r,c);const float roll=-Dot(c,u);           // right wing going down
    const float s=Len(j.m.vel),pitchMax=k.maxG*kG/(s>k.minSpeed ? s : k.minSpeed);
    const float p=Clamp(kElevonGain*pitch/pitchMax,-1.0f,1.0f),q=Clamp(kElevonGain*roll/k.roll,-1.0f,1.0f);
    const float want[2]={Clamp((p-q)*kElevonMax,-kElevonMax,kElevonMax),Clamp((p+q)*kElevonMax,-kElevonMax,kElevonMax)};
    PoseSurfaces(j,want,kElevonRate,dt,"elevons");
}
}  // namespace

void SetMode(Jet& j,Mode m,ULONGLONG ms) noexcept {
    if(j.mode==m)return;
    if(Cfg().debug)Log("JET v=%p %s -> %s",j.Vehicle(),kModeNames[static_cast<int>(j.mode)],kModeNames[static_cast<int>(m)]);
    j.mode=m;j.modeAt=ms;
}

void Withdraw(Jet& j,const char* why,ULONGLONG ms) noexcept {
    if(j.mode==Mode::withdraw)return;
    j.why=why;SetMode(j,Mode::withdraw,ms);
    Log("JET v=%p withdraws: %s",j.Vehicle(),why);
}


// The carrier's nacelles along the thrust Hover asked for (see kThrustBack). A nacelle's thrust, along its nose
// (local +z) turned theta about X, is (0, -sin theta, cos theta) in the body: theta = atan2(-up, forward). Its
// yaw: a nacelle at body x tilted forward pushes the body round its up by -x times that (r x F), so to turn at
// the yaw rate the body is told (omega . up) each nacelle tilts -sign(x) of kThrustYaw forward.
void Thrusters(Jet& j,const Kind& k,unsigned char* v,float dt,ULONGLONG ms,float clear) noexcept {
    if(!FindSurfaces(j,v,kThrusterNames,4,"thrusters"))return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float u[3]={m[4],m[5],m[6]},f[3]={m[8],m[9],m[10]};
    if(!Normalize(u) || !Normalize(f))return;
    const float up=Dot(j.m.thrust,u),fwd=Dot(j.m.thrust,f);
    const float tilt=Clamp(std::atan2(-up,fwd),-kThrustBack,0.0f);
    const float yaw=Clamp(Dot(j.m.omega,u)/k.roll,-1.0f,1.0f)*kThrustYaw;
    float want[4];
    for(int i=0;i<4;++i) {
        const float side=j.surf.bind[i][12]>=0.0f ? 1.0f : -1.0f;   // the nacelle's x in the body
        want[i]=Clamp(tilt-side*yaw,-kThrustBack,0.0f);
        // Near the ground no further than keeps it over the ground (nacelle_reach.h; F_l, F_r the front pair).
        if(clear<kNacelleFar)want[i]=std::fmax(want[i],nacelle::MostTilt(i<2 ? nacelle::kFront : nacelle::kBack,-clear,-kThrustBack));
    }
    PoseSurfaces(j,want,kThrustRate,dt,"thrusters");
    // The stock Booster flame on each nozzle, with the engine (Hover's power): half at idle, full at all of it. It was
    // the thrust over gravity, so full from the hover on: the same flame hovering, cruising or climbing.
    CarrierFlames(v,j.surf.rec,Clamp(0.5f+0.5f*j.m.power,0.5f,1.0f),ms);
    if(Cfg().debug && ms-j.m.thrustLogAt>1000) {
        j.m.thrustLogAt=ms;
        Log("JET v=%p thrusters: thrust %.1f m/s^2 (up %.1f, fwd %.1f) tilt %.0f deg, yaw %.0f deg, at F %.0f/%.0f B %.0f/%.0f",v,
            Len(j.m.thrust),up,fwd,tilt*180.0f/kPi,yaw*180.0f/kPi,j.surf.at[0]*180.0f/kPi,j.surf.at[1]*180.0f/kPi,
            j.surf.at[2]*180.0f/kPi,j.surf.at[3]*180.0f/kPi);
    }
}

// A unit direction to `goal`.
void Toward(const float* pos,const float* goal,float* out) noexcept {
    out[0]=goal[0]-pos[0];out[1]=goal[1]-pos[1];out[2]=goal[2]-pos[2];
    if(!Normalize(out)){out[0]=0;out[1]=0;out[2]=1;}
}

// Level flight (climbing or sinking toward `height`) along the horizontal `dir`.
void Level(const float* pos,const float* dir,float height,float* out) noexcept {
    float h[3]={dir[0],0,dir[2]};
    if(!Normalize(h)){h[0]=0;h[2]=1;}
    const float climb=Clamp((height-pos[1])/150.0f,-0.45f,0.6f);
    out[0]=h[0];out[1]=climb;out[2]=h[2];
    Normalize(out);
}

// The way the body went since the last frame against the commanded velocity (see kBlockedPart). Blocked,
// the command turns along what holds it, so the nose never points where the jet cannot go. Returns
// whether it was blocked this frame.
bool Sense(Jet& j,const float* pos,ULONGLONG ms) noexcept {
    Motion& mo=j.m;
    const ULONGLONG since=mo.prevAt ? ms-mo.prevAt : 0;
    const float moved[3]={pos[0]-mo.prevPos[0],pos[1]-mo.prevPos[1],pos[2]-mo.prevPos[2]};
    std::memcpy(mo.prevPos,pos,12);mo.prevAt=ms;
    // The game steps a frame at a time: a slow frame moves the body no more than 1/60 s of the way.
    const float wall=static_cast<float>(since)*0.001f,s=Len(mo.vel),dt=wall<1.0f/60.0f ? wall : 1.0f/60.0f;
    if(since)mo.real+=(Len(moved)/(static_cast<float>(since)*0.001f)-mo.real)*0.1f;
    if(!since || !mo.ready || s<1.0f)return false;
    const float dir[3]={mo.vel[0]/s,mo.vel[1]/s,mo.vel[2]/s};
    if(Dot(moved,dir)>=s*dt*kBlockedPart){mo.blockedFor=0;return false;}
    mo.blockedFor+=since;
    if(mo.blockedFor<kBlockedMs)return false;
    mo.blockedFor=0;
    float n[3]={dir[0]*s-moved[0]/dt,0.0f,dir[2]*s-moved[2]/dt};
    if(Len(n)<s*0.3f || !Normalize(n))return false;   // held from below or above: Guard's
    const float clear=GroundClearance(pos);
    const bool jet=JetNear(j,pos,ms);
    if(!jet && (clear==kNoGround || clear>kWallGround))LearnWall(pos,n,ms);
    float slide[3]={dir[0],dir[1],dir[2]};
    const float into=Dot(slide,n);
    for(int i=0;i<3;++i)slide[i]-=n[i]*into;
    if(!Normalize(slide)){slide[0]=n[2];slide[1]=0;slide[2]=-n[0];}
    // Along it at the speed the body kept (at least the least it flies at), not at the commanded speed:
    // a jump to that sideways is the snap that slid the body flank first.
    const float kept=Clamp(Dot(moved,slide)/dt,KindOf(j).minSpeed,s);
    for(int i=0;i<3;++i)mo.vel[i]=slide[i]*kept;
    Log("JET v=%p blocked (%.0f of %.0f m/s)%s: turned along it at %.0f",j.Vehicle(),Dot(moved,dir)/dt,s,jet ? " by a jet" : "",kept);
    return true;
}

// A wall within `range` ahead of `pos`: its soft edge, or one learned.
bool NearWall(const Jet& j,const float* pos,float range,ULONGLONG ms) noexcept {
    if(airbound::Depth(JetSoftBox(j),pos)<range)return true;
    for(const auto& w:learned)if(Holds(w,pos,ms) && Gap(w,pos)<range)return true;
    return false;
}

// What stands on its track ahead (the user, 2026-10-05: "飞机会撞楼"). Guard's two rays look straight down (under it
// now and kLookAhead s on), so a building between them was never seen, and one the far ray saw was forgotten the next
// frame as the probe went past it: the jets flew into buildings and slid along them (Sense). Ahead sweeps its track
// kMinAlt under it out to a pull-up's time (kReact, the roll to lift, the climb's turn s / (maxG g); kSweepMin to
// kSweepMax s) in kSweepSlots stretches, kSweepPerFrame a frame (a whole sweep every 3 frames: 2 rays a jet a frame);
// a stretch that hits something has its top found and kept as the obstacle, the highest one, until the jet is past it,
// it lies kObstOff to the side of its track while it is not turning off it (a building it only grazed; turning off, the
// turn itself takes it off the track, and dropping it then turned the jet back onto it), or the sweep's time and
// kObstKeepMs more is up. The top: a ray down from kRoofProbe over the jet (or the hit) onto a point kObstIn on along
// the stretch from the hit and kObstSink under it, its first hit (a roof); none, it starts inside something taller
// still, taken as that tall. (A ray down from the point itself, inside the building, found the street under it; one
// onto a point level with a hit on the ground, or past a corner it grazed, missed and made a 400 m wall of nothing:
// the offline flight test, tools/jet_obstacle_sim.cpp, pulled strafing dives out at 117 m instead of 65.)
constexpr int kSweepSlots=6,kSweepPerFrame=2;
constexpr float kSweepMin=3.0f,kSweepMax=8.0f,kObstIn=0.5f,kObstSink=1.0f,kRoofProbe=400.0f,kObstOff=40.0f;
constexpr ULONGLONG kObstKeepMs=2000;
// Guard over it: kObstMargin times the slope to its top, kObstSteep (the sine) at most. When the climb it needs is more
// than kClimbShare of what a pull-up at its g gets it in that distance (Unclimbable: a 45 deg rule sent a jet at a
// 400 m tower 600 m out up its face), it turns off as well, kAsideAngle off the line to it, to the side (picked once,
// when it is found: TurnSide) whose ray at its height reaches further past it (kAsideBeyond).
constexpr float kObstMargin=1.3f,kObstSteep=0.9f,kClimbShare=0.8f,kAsideAngle=0.6f,kAsideBeyond=150.0f;

// Whether `rise` is more than it climbs in `d` (horizontal) on a pull-up at its g: the arc of radius s^2 / ((maxG - 1) g)
// rises r - sqrt(r^2 - d^2) in d (straight up past a quarter turn), kClimbShare of that.
bool Unclimbable(const Jet& j,float rise,float d) noexcept {
    const Kind& k=KindOf(j);
    const float s=Len(j.m.vel),r=s*s/(std::fmax(k.maxG-1.0f,0.5f)*kG);
    const float most=d<r ? r-std::sqrt(r*r-d*d) : d;
    return rise>most*kClimbShare;
}

// `dir` (level, unit) turned `side` (+1 / -1) by kAsideAngle.
void Turned(const float* dir,float side,float* out) noexcept {
    const float c=std::cos(kAsideAngle),s=std::sin(kAsideAngle)*side;
    out[0]=dir[0]*c+dir[2]*s;out[1]=0.0f;out[2]=dir[2]*c-dir[0]*s;
}

std::int8_t TurnSide(const float* pos,const float* at) noexcept {
    float to[3]={at[0]-pos[0],0.0f,at[2]-pos[2]};
    const float reach=Len(to)+kAsideBeyond;
    if(!Normalize(to))return 1;
    float room[2];
    for(int i=0;i<2;++i) {
        float way[3];Turned(to,i ? -1.0f : 1.0f,way);
        const float end[3]={pos[0]+way[0]*reach,pos[1],pos[2]+way[2]*reach};
        float hit[3];
        const float d=MapRay(pos,end,hit);
        room[i]=d<0.0f ? reach : d;
    }
    return room[1]>room[0] ? -1 : 1;
}

void Ahead(Jet& j,const float* pos,ULONGLONG ms) noexcept {
    Motion& mo=j.m;
    const Kind& k=KindOf(j);
    const float s=Len(mo.vel);
    float dir[3]={mo.vel[0],0.0f,mo.vel[2]};
    const bool moving=s>=1.0f && Normalize(dir);
    if(mo.obstUntil) {
        const float to[3]={mo.obstAt[0]-pos[0],0.0f,mo.obstAt[2]-pos[2]};
        const float along=to[0]*dir[0]+to[2]*dir[2],off=std::fabs(to[0]*dir[2]-to[2]*dir[0]);
        if(ms>mo.obstUntil || !moving || along<0.0f || (off>kObstOff && !mo.obstSide)){mo.obstUntil=0;mo.obstSide=0;}
    }
    if(!moving)return;
    const float span=Clamp(kReact+RollToLift(j)/k.roll+s/(k.maxG*kG),kSweepMin,kSweepMax);
    for(int n=0;n<kSweepPerFrame;++n) {
        const int i=mo.sweep++%kSweepSlots;
        const float t0=span*static_cast<float>(i)/kSweepSlots,t1=span*static_cast<float>(i+1)/kSweepSlots;
        const float a[3]={pos[0]+mo.vel[0]*t0,pos[1]+mo.vel[1]*t0-kMinAlt,pos[2]+mo.vel[2]*t0};
        const float b[3]={pos[0]+mo.vel[0]*t1,pos[1]+mo.vel[1]*t1-kMinAlt,pos[2]+mo.vel[2]*t1};
        float hit[3];
        if(MapRay(a,b,hit)<0.0f)continue;
        float seg[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
        if(!Normalize(seg))std::memcpy(seg,dir,12);
        const float in[3]={hit[0]+seg[0]*kObstIn,hit[1]+seg[1]*kObstIn-kObstSink,hit[2]+seg[2]*kObstIn};
        const float from[3]={in[0],(pos[1]>hit[1] ? pos[1] : hit[1])+kRoofProbe,in[2]};
        float roof[3];
        const float top=MapRay(from,in,roof)>=0.0f ? roof[1] : from[1];
        if(mo.obstUntil && top<=mo.obstTop)continue;
        if(Cfg().debug && (!mo.obstUntil || top>mo.obstTop+10.0f))
            Log("JET v=%p obstacle ahead: top y=%.0f (%.0f over it), %.0f m out",j.Vehicle(),top,top-pos[1],HorizDist(pos,hit));
        mo.obstTop=top;std::memcpy(mo.obstAt,hit,12);
        mo.obstUntil=ms+static_cast<ULONGLONG>(span*1000.0f)+kObstKeepMs;
    }
    // Too steep to climb from here (now, or once it has come closer): its side picked, once.
    if(mo.obstUntil && !mo.obstSide && Unclimbable(j,mo.obstTop+kMinAlt-pos[1],HorizDist(pos,mo.obstAt)))
        mo.obstSide=TurnSide(pos,mo.obstAt);
}

// What PredictPullOut needs of jet `j` now: its speed and dive, the g and thrust its stores leave it (JetSteer's
// Burden), the roll its lift is from up (RollToLift) and the attitude's gain and rate, kPullReact.
// kPullReact: s before the pull the prediction allows. Guard and Strike's dive turn `want` up the frame they see it and
// JetSteer lifts along the body's up as it is (the roll is in the prediction itself); this is the body's attitude
// catching up (kAttGain) and a margin. (kReact, 1 s, stays the look-ahead's: Ahead, the walls.)
constexpr float kPullReact=0.3f;
PullOutIn PullOutFor(const Jet& j,const Kind& k) noexcept {
    const float s=Len(j.m.vel),mass=j.burden.mass>1.0f ? j.burden.mass : 1.0f;
    const float top=j.m.top>0.0f ? j.m.top : k.attack*1.3f;
    return PullOutIn{s,s>1.0f ? Clamp(-j.m.vel[1]/s,0.0f,1.0f) : 0.0f,k.maxG/mass,RollToLift(j),k.roll,kAttGain,kPullReact,
                     k.thrust/mass,top<kBodyTop ? top : kBodyTop,kG};
}

// The soft edge (airbound.h, the user 2026-10-06: a small edge before the edge; past it the jet comes back, and as a rule
// it never crosses it). The walls at the play edge only turned a jet off from a turn's reach out with a push that
// started at a fifth of its way (TurnOff), and only past the play edge itself did coming back come first: a jet chasing a
// target out there, or crank / extend running out at full speed, was pulled back out by its want each frame it was not
// pushed, and flew on to the edge. Now the turn back starts where the turn would end on the soft line (KeepIn: its
// radius at its g, the roll into it), and past the line it flies in, level, until airbound::kBackDepth inside.
// Before its turn bites it has kReact s, and a quarter roll at its roll rate (SoftEdge's `react`).
// The turn it flies is not the one at maxG: past the g its thrust holds (1 + thrust / kTurnBleed) it slows (JetSteer), and
// banking into it takes the turn's first part. The offline flight test (jet_obstacle_sim --edge-suite) measured a strike
// jet's half circle some 30% wider than at its 5 g: the radius is taken at the g it holds, times kTurnWide.
constexpr float kTurnWide=1.15f;
// m: the radius of its level turn at `speed`.
float TurnRadiusOf(const Kind& k,float mass,float speed) noexcept {
    float g=1.0f+k.thrust/kTurnBleed;
    if(g>k.maxG)g=k.maxG;
    g/=mass>1.0f ? mass : 1.0f;
    return airbound::TurnRadius(speed,std::sqrt(g*g>1.0f ? g*g-1.0f : 0.25f))*kTurnWide;
}

// m/s: the most a wing flies inside a play area too small for its turns at its attack speed (a stock map's ground,
// some 1.5 km a side: a 245 m/s fighter's turn is 2.9 km across): the speed whose turn's radius is kTightShare of the
// area's half size, so it turns back inside the ground (at least kTightLeast times its least speed). Its attack speed
// where the area holds that.
constexpr float kTightShare=0.3f,kTightLeast=1.15f;
float TightSpeed(const Kind& k,float half) noexcept {
    float g=1.0f+k.thrust/kTurnBleed;
    if(g>k.maxG)g=k.maxG;
    const float lat=std::sqrt(g*g>1.0f ? g*g-1.0f : 0.25f);
    const float s=std::sqrt(kTightShare*half*lat*airbound::kGrav/kTurnWide);
    return Clamp(s,k.minSpeed*kTightLeast,k.attack);
}

airbound::Box JetSoftBox(const Jet& j,float* band) noexcept {
    const Kind& k=KindOf(j);
    const airbound::Box play=PlayBox();   // mapbounds.h: the ground's walls
    const float half=airbound::HalfOf(play);
    const float reach=k.flight==FlightModel::rotor ? k.attack*k.attack/(4.0f*(k.brake>1.0f ? k.brake : 1.0f))
                                                   : TurnRadiusOf(k,1.0f,TightSpeed(k,half));
    const float b=airbound::Band(reach,Cfg().airSoftTurns,Cfg().airSoftEdge,half);
    if(band)*band=b;
    return airbound::Inset(play,b);
}

const float* SoftAnchor(const Jet& j,const float* anchor,float* room) noexcept {
    const Kind& k=KindOf(j);
    const float circle=k.patrol+k.patrolStep*static_cast<float>(j.wing%kPatrolRings);
    std::memcpy(room,anchor,12);
    return airbound::ClampIn(JetSoftBox(j),room,circle) ? room : anchor;
}

namespace {
// The horizontal soft edge and the soft ceiling (CeilingY less kCeilingGap less ini AirSoftCeil): `want` turned so its
// turn back (or its push over) ends on them. Logged as it goes past the soft line and as it is back inside.
void SoftEdge(Jet& j,const float* pos,float* want) noexcept {
    const Kind& k=KindOf(j);
    const float s=std::sqrt(j.m.vel[0]*j.m.vel[0]+j.m.vel[2]*j.m.vel[2]),s3=Len(j.m.vel);
    const float r=TurnRadiusOf(k,j.burden.mass,s);
    const float react=kReact+0.5f*kPi*0.5f/(k.roll>0.1f ? k.roll : 0.1f);
    float band=0.0f;
    const airbound::Box soft=JetSoftBox(j,&band);
    const bool was=j.m.edgeBack;
    // A gun dive at a point on the ground inside the soft box keeps its line: it ends at that point (Strike pulls out
    // gunClose short of it), and the band past the soft line holds its turn's reach (JetSoftBox), so the pull-out turns
    // back inside the play edge as any turn there does. Bent by the edge (eased off outward, or levelled flying back in)
    // the nose never came onto the lead: on a stock map (soft box +-960 m, the target's whole run within the edge's ease)
    // three called multirole jets dived 64 times in 4 minutes with the nose 15-60 deg off it and fired no gun (2026-10-09
    // log). The edge's state still follows the jet (KeepIn on a copy); the pull-out is the edge's again.
    float kept[3]={want[0],want[1],want[2]};
    const bool dive=j.mode==Mode::dive && j.t.target && !j.t.flyer && airbound::Depth(soft,j.t.aim)>=0.0f;
    airbound::KeepIn(soft,pos,j.m.vel,r,react,dive ? kept : want,&j.m.edgeBack,&j.m.edgeTurn);
    if(j.m.edgeBack!=was)
        Log("JET v=%p %s the soft edge at (%.0f,%.0f): soft x %.0f..%.0f z %.0f..%.0f, band %.0f, %.0f m/s%s",j.Vehicle(),
            was ? "back inside" : "past",pos[0],pos[2],soft.lo[0],soft.hi[0],soft.lo[1],soft.hi[1],band,s,was ? "" : ": back in first");
    // Over the top it pushes over: its lift down at most kNegG, gravity with it (JetSteer), a radius of s^2 / ((1 + kNegG) g).
    const float top=CeilingY()-kCeilingGap-Cfg().airSoftCeil;
    airbound::CapClimb(pos[1],j.m.vel[1],s3,top,airbound::TurnRadius(s3,1.0f+kNegG)*kTurnWide,react,0.15f,want);
}
}  // namespace

// Keeps `want` off the walls and the ground and under the ceiling. The ground is the highest under it now and
// kLookAhead seconds along its track; sinking, the lowest it gets is where a maxG pull-out started
// kReact seconds from now bottoms out (so a dive runs down to kMinAlt instead of pulling up 100 m early);
// climbing, the ceiling is checked kLookAhead seconds out.
void Guard(Jet& j,const float* pos,float clear,float* want,ULONGLONG ms) noexcept {
    const Kind& k=KindOf(j);
    const float s=Len(j.m.vel);
    // A building (or a ridge) Ahead found on its track: a floor too (below), climbed from where it is now (`rise` over
    // `toObst`); too steep for that, it turns off kAsideAngle from the line to it on its picked side, before the walls
    // (they still turn it back in).
    float rise=0.0f,toObst=0.0f;
    if(j.m.obstUntil) {
        toObst=HorizDist(pos,j.m.obstAt);
        rise=j.m.obstTop+kMinAlt-pos[1];
        float to[3]={j.m.obstAt[0]-pos[0],0.0f,j.m.obstAt[2]-pos[2]};
        const float h=std::sqrt(want[0]*want[0]+want[2]*want[2]);
        if(j.m.obstSide && Unclimbable(j,rise,toObst) && Normalize(to)) {
            float way[3];Turned(to,static_cast<float>(j.m.obstSide),way);
            const float flat=h>1e-4f ? h : 1.0f;
            want[0]=way[0]*flat;want[2]=way[2]*flat;
        }
    }
    // Learned walls: turned off from a turn's radius out (more closing fast), never flown into.
    const float r=s*s/(k.maxG*kG);
    for(const auto& w:learned)if(Holds(w,pos,ms))TurnOff(w,j,pos,r,want);
    // The soft edge last: coming back in comes before its fight, its target out there, a learned wall.
    SoftEdge(j,pos,want);
    const float ahead[3]={pos[0]+j.m.vel[0]*kLookAhead,pos[1]+j.m.vel[1]*kLookAhead,pos[2]+j.m.vel[2]*kLookAhead};
    const float probe[3]={ahead[0],pos[1]>ahead[1] ? pos[1] : ahead[1],ahead[2]};
    const float here=clear,there=GroundClearance(probe);
    // Neither ray finding ground (past the map's terrain) left it no floor at all: an interceptor rolled over
    // there and flew on to -1100 (2026-10-04). It keeps the surface it last saw under it then.
    const float seen=j.m.groundSeen ? j.m.groundY : -1e9f;
    const float lowest=there!=kNoGround ? probe[1]-there : seen;
    float floorY=here!=kNoGround && pos[1]-here>lowest ? pos[1]-here : lowest;
    if(j.m.obstUntil && j.m.obstTop>floorY)floorY=j.m.obstTop;
    // Sinking: the lowest it gets is the bottom of a pull-out begun kPullReact from now (PredictPullOut: at the g its stores
    // leave it, against gravity, gaining speed), over the ground where that bottom is as well (rising terrain past the
    // look-ahead's point).
    float bottom=pos[1];
    if(s>1.0f && j.m.vel[1]<0.0f) {
        const PullOut p=PredictPullOut(PullOutFor(j,k));
        bottom-=p.drop;
        float flat[3]={j.m.vel[0],0.0f,j.m.vel[2]};
        if(Normalize(flat)) {
            const float end[3]={pos[0]+flat[0]*p.run,pos[1],pos[2]+flat[2]*p.run};
            const float under=GroundClearance(end);
            if(under!=kNoGround && end[1]-under>floorY)floorY=end[1]-under;
        }
    }
    if(bottom<floorY+kMinAlt) {
        // Pulling up, never turning back at the same time: `want` behind it made JetSteer's shortest arc go through
        // straight down (over a tower, its target behind it, a jet dived at -47 deg to 1 m: the offline flight test).
        // Up first, along its way; the turn after.
        float flatV[3]={j.m.vel[0],0.0f,j.m.vel[2]};
        if(Normalize(flatV) && want[0]*flatV[0]+want[2]*flatV[2]<0.0f) {
            const float hw=std::sqrt(want[0]*want[0]+want[2]*want[2]);
            want[0]=flatV[0]*hw;want[2]=flatV[2]*hw;
        }
        const float need=Clamp((floorY+kMinAlt-bottom)/40.0f,0.3f,0.8f);
        if(want[1]<need){want[1]=need;Normalize(want);}
        // Over what is ahead: kObstMargin times the slope from here to its top, kObstSteep at most, as the path's own
        // climb (the sine itself, not one normalized away).
        const float slope=rise>0.0f ? Clamp(kObstMargin*rise/std::sqrt(rise*rise+toObst*toObst),0.0f,kObstSteep) : 0.0f;
        const float h=std::sqrt(want[0]*want[0]+want[2]*want[2]);
        if(want[1]<slope && h>1e-4f) {
            const float keep=std::sqrt(1.0f-slope*slope)/h;
            want[0]*=keep;want[2]*=keep;want[1]=slope;
        }
    }
    const float top=CeilingY()-kCeilingGap;
    const float rising=pos[1]+(j.m.vel[1]>0.0f ? j.m.vel[1]*kLookAhead : 0.0f);
    if(rising>top && want[1]>-0.15f){want[1]=-0.15f;Normalize(want);}
}

void ResetWalls() noexcept {
    for(auto& w:learned)w=Wall{};
}

// The ground as a hard floor. The plugin sets the body's velocity, and at 150-200 m/s (2.5-3.5 m a frame) a dive
// went through the terrain and on under it (2026-10-04: a fighter inverted at 120 m/s to -460, doll drones half
// under the ground over their targets). So after the frame's steering:
//  - over the ground, its descent is cut to stop kFloorGap over the floor: the surface under it, or where the
//    next kFloorSweep frames of its track meet the ground. Only the descent: it is not lifted (a carrier or a
//    jet standing on the ground stays put; terrain rising ahead is Guard's);
//  - under the ground (its position under the surface: clear < -rest; or no ground under it at all and
//    below the surface last seen under it, off the map's edge) it climbs at kFloorClimb at least.
// `clear` is the clearance the floor is kept from (kNoGround: none found): GroundClearance(pos) for the NPCs (rest 0),
// a player craft's bottom's (playerjet FloorClear, rest its position's height over its bottom). Sunk is the position
// under the surface, not the bottom: a craft standing on a bump reads its bottom a few cm under it, and taking that
// for sunk threw it up at kFloorClimb every time it settled (the user, 2026-10-07: the aircraft shake on the ground).
constexpr float kFloorGap=1.0f,kFloorClimb=80.0f,kFloorSweep=3.0f;
void HoldOffGround(Jet& j,const float* pos,float clear,float dt,ULONGLONG ms,float rest) noexcept {
    Motion& mo=j.m;
    if(clear!=kNoGround){mo.groundY=pos[1]-clear;mo.groundSeen=true;}
    if(!mo.groundSeen)return;
    const bool under=clear!=kNoGround ? clear< -rest : pos[1]<mo.groundY-rest;
    float floorY=mo.groundY,need=kFloorClimb;
    if(!under) {
        if(mo.vel[1]>=0.0f)return;
        // Along its track from its bottom (`rest` under its position), what it meets in the position's units (+ rest): from
        // the position it met only what rose to its centre, and that was taken for its bottom's floor (rest m too low).
        const float from[3]={pos[0],pos[1]-rest,pos[2]};
        const float end[3]={from[0]+mo.vel[0]*dt*kFloorSweep,from[1]+mo.vel[1]*dt*kFloorSweep-kFloorGap,from[2]+mo.vel[2]*dt*kFloorSweep};
        float hit[3];
        if(MapRay(from,end,hit)>=0.0f && hit[1]+rest>floorY && hit[1]<from[1])floorY=hit[1]+rest;
        need=(floorY+kFloorGap-pos[1])/dt;
        if(need>0.0f)need=0.0f;
    }
    if(mo.vel[1]>=need)return;
    const float was=mo.vel[1];
    mo.vel[1]=need;
    if(Cfg().debug && ms-mo.floorLogAt>=1000) {
        mo.floorLogAt=ms;
        Log("JET v=%p held off the ground%s: y=%.0f floor=%.0f vy %.1f -> %.1f",j.Vehicle(),under ? " (under it)" : "",
            pos[1],floorY,was,mo.vel[1]);
    }
}

// A rotor craft (FlightModel::rotor): no wing, so it goes where it wants at any speed, hovering still over its
// station. Its velocity closes on the one that stops it at `goal` (at most `speed`, braking at its kind's brake:
// the speed that can still stop there, sqrt(2 brake d)) at its thrust; its body leans into that acceleration
// (up = acceleration + gravity, as a rotor's thrust does) as its kind's Lean says, the nose on `face`.
// `climb`: m/s up or down at the most (kHoverClimb; a blast drone dives faster). j.m.thrust gets the thrust
// asked for (gravity held, the acceleration, the drag shown), which the lean and the thrusters follow; j.m.power the
// engine's share of its full thrust that takes (hover_lift.h Power).
void Hover(Jet& j,const Kind& k,const unsigned char* v,const float* pos,const float* goal,const float* face,float speed,float climb,
           float dt,float lift,bool npcGoal) noexcept {
    const Lean& how=*k.lean;
    Motion& mo=j.m;
    // NPC goals stop inside their soft edge. Player flight and its hail retain their requested goal instead.
    float inside[3];
    std::memcpy(inside,goal,12);
    if(npcGoal)airbound::ClampIn(JetSoftBox(j),inside,0.0f);
    goal=inside;
    float to[3]={goal[0]-pos[0],0.0f,goal[2]-pos[2]};
    const float d=Len(to);
    float wantV[3]={0.0f,0.0f,0.0f};
    if(Normalize(to)) {
        const float stop=std::sqrt(2.0f*k.brake*d),s=stop<speed ? stop : speed;
        wantV[0]=to[0]*s;wantV[2]=to[2]*s;
    }
    wantV[1]=Clamp((goal[1]-pos[1])*0.5f,-climb,climb);
    float acc[3];
    const float respond=how.respond>dt ? how.respond : dt;
    const hover::Budget budget{k.thrust,lift,how.jerk};
    hover::Accel(wantV,mo.vel,respond,budget,mo.acc,dt,acc);   // hover_lift.h
    std::memcpy(mo.acc,acc,12);
    for(int i=0;i<3;++i)mo.vel[i]+=acc[i]*dt;
    hover::Thrust(acc,mo.vel,how.drag,kG,mo.thrust);
    mo.power=hover::Power(mo.thrust,budget,kG);   // the engine: the player's throttle readout, the sound, the flames
    float nose[3]={face[0]-pos[0],0.0f,face[2]-pos[2]};
    if(!Normalize(nose)) {
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        nose[0]=m[8];nose[1]=0.0f;nose[2]=m[10];
        if(!Normalize(nose)){nose[0]=0;nose[2]=1;}
    }
    // The lean: the horizontal thrust against gravity (its fore-and-aft part times pitchShare, the sideways part
    // times bank), at most maxLean.
    const float fore=mo.thrust[0]*nose[0]+mo.thrust[2]*nose[2];
    const float across[2]={mo.thrust[0]-nose[0]*fore,mo.thrust[2]-nose[2]*fore};
    const float side[3]={nose[0]*fore*how.pitchShare+across[0]*how.bank,0.0f,nose[2]*fore*how.pitchShare+across[1]*how.bank};
    float up[3]={side[0],kG,side[2]};
    const float flat=std::sqrt(side[0]*side[0]+side[2]*side[2]);
    if(std::atan2(flat,kG)>how.maxLean) {
        const float h=std::tan(how.maxLean)*kG/flat;
        up[0]*=h;up[2]*=h;
    }
    Normalize(up);
    // The nose across `up` (the body's forward lies in the plane the lean makes).
    const float along=Dot(nose,up);
    for(int i=0;i<3;++i)nose[i]-=up[i]*along;
    Normalize(nose);
    Attitude(j,k,v,nose,up);
}

// The circle round the anchor, patrol out (plus patrolStep per jet ahead of it in its flight, so they do not
// share one circle), counterclockwise. Returns the speed to fly it at (see kLoiterTan).
float Patrol(const Jet& j,const float* pos,const float* anchor,float height,float* want) noexcept {
    const Kind& k=KindOf(j);
    const float r=k.patrol+k.patrolStep*static_cast<float>(j.wing%kPatrolRings);
    float out[3]={pos[0]-anchor[0],0,pos[2]-anchor[2]};
    float dist=Len(out);
    if(!Normalize(out)){out[0]=1;out[2]=0;dist=0.0f;}
    const float tangent[3]={out[2],0,-out[0]};
    const float pull=Clamp((r-dist)/r,-1.5f,1.5f);
    const float dir[3]={tangent[0]+out[0]*pull,0,tangent[2]+out[2]*pull};
    Level(pos,dir,height,want);
    return Clamp(std::sqrt(r*kG*kLoiterTan),k.minSpeed*kLoiterMin,k.cruise);
}

// A ferry's pass (the paratroop plane: transport.cpp's stick jumps within kDropRadius, 400 m, of the point). Patrol
// cannot carry it there: a strike body's circle is 1000 m round its anchor, so it never comes nearer the point than that
// (the 2026-10-10 runs: the plane went round the point, never over it). In: straight at the point. Over it (within
// kFerryOver) or past it (the point behind it): straight on along its heading, out kFerryRoom of its turns, then in
// again, every pass from far enough out to line up on the point.
constexpr float kFerryOver=60.0f,kFerryRoom=2.2f;
float Ferry(Jet& j,const float* pos,const float* point,float height,float* want) noexcept {
    const Kind& k=KindOf(j);
    const float speed=Clamp(k.minSpeed*kLoiterMin,k.minSpeed,k.cruise);
    const float turn=speed*speed/(kG*kLoiterTan);   // m: its turn's radius at that speed, Patrol's bank
    float to[3]={point[0]-pos[0],0.0f,point[2]-pos[2]};
    const float dist=Len(to);
    if(j.ferryOut) {
        if(dist>=turn*kFerryRoom)j.ferryOut=false;   // room to come round: the next pass
    } else {
        const float ahead=to[0]*j.m.vel[0]+to[2]*j.m.vel[2];
        if(dist<=kFerryOver || (ahead<0.0f && dist<turn*kFerryRoom)) {   // over it, or by it: on, out for the next pass
            j.ferryOut=true;
            j.ferryDir[0]=j.m.vel[0];j.ferryDir[1]=0.0f;j.ferryDir[2]=j.m.vel[2];
            if(!Normalize(j.ferryDir)){j.ferryDir[0]=0.0f;j.ferryDir[2]=1.0f;}
        }
    }
    if(j.ferryOut)Level(pos,j.ferryDir,height,want);
    else {
        if(!Normalize(to)){to[0]=j.ferryDir[0];to[2]=j.ferryDir[2];}
        Level(pos,to,height,want);
    }
    return speed;
}

void Wing(Jet& j,const Kind& k,unsigned char* v,const float* pos,float clear,const float* nose,float* want,float speed,float dt,
          ULONGLONG ms) noexcept {
    // A play area too small for its turns at full speed: no faster than turns inside it (TightSpeed). A bomber on its
    // run keeps its run's speed.
    if(j.mode!=Mode::bomb){const float most=TightSpeed(k,airbound::HalfOf(PlayBox()));if(speed>most)speed=most;}
    Ahead(j,pos,ms);
    Guard(j,pos,clear,want,ms);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float up[3];
    float bodyUp[3]={m[4],m[5],m[6]};
    if(!Normalize(bodyUp)){bodyUp[0]=0;bodyUp[1]=1;bodyUp[2]=0;}
    JetSteer(j,k,nose,bodyUp,want,speed,dt,up);
    float dir[3]={j.m.vel[0],j.m.vel[1],j.m.vel[2]};
    if(!Normalize(dir))std::memcpy(dir,nose,12);
    PitchBy(j.m.aoa,dir,up);
    Attitude(j,k,v,dir,up);
    if(k.pose==Pose::elevons)Elevons(j,k,v,dt);
    else if(k.pose==Pose::flap)Flap(j,k,v,dt);
    NpcGear(v,Len(j.m.vel),dt);   // the landing gear up in flight (gear.cpp; a model without it: nothing)
    // The exhaust (booster.cpp JetFlames): burning with the speed between its slowest and its attack speed, the
    // afterburner near the top.
    const float s=Len(j.m.vel),share=k.attack>k.minSpeed ? (s-k.minSpeed)/(k.attack-k.minSpeed) : 1.0f;
    JetFlames(v,Clamp(share,0.4f,1.0f),speed>=k.attack-5.0f,ms);
    JetSmoke(v,Entering(j,ms),ms);   // arriving: smoke from its exhausts (booster.cpp)
}
}  // namespace jet
}  // namespace crew
