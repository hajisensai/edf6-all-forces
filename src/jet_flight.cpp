// NPC jets' flight (jet.cpp): a wing's guidance and attitude (JetSteer, Attitude, its elevons), a rotor craft's
// (Hover, the carrier's nacelles), and what keeps either flying: the ground, the ceiling, the world's walls and
// the walls it learns.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"
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
// wall margin (Guard) no longer fits inside kWorldWall.
// Patrol: the speed that holds the patrol circle at a 60 degree bank (tan 1.73, 2 g), between kLoiterMin
// times the stall speed and cruise. At cruise the circle took 5 g and 78 degrees of bank the whole time; at
// 45 degrees (until 2026-10-04) it crawled round at 100 m/s.
constexpr float kLoiterTan=1.73f,kLoiterMin=1.15f;
constexpr float kAttGain=6.0f;         // 1/s: the body closes on the attitude it should have this fast
constexpr float kNegG=1.0f;            // g: the most it pushes (lift down the body's up)
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
// The Havok broadphase ends at 3000 m a side: the world's walls at kWorldWall keep the jets in (jet.cpp deletes
// one past kWorldGone). They are never forgotten and no learned wall takes their place.
constexpr float kWorldWall=2400.0f;
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
constexpr std::size_t kModelInst=0xEE0,kInstBones=0x10,kInstBoneCount=0x20,kBoneStride=0x110,kBoneAuto=0x8,kBoneLocal=0x70;
constexpr float kElevonMax=0.35f,kElevonRate=2.0f;
const wchar_t* const kElevonNames[2]={L"elevon_L",L"elevon_R"};
// The carrier's thrusters (docs/jet-model-re.md §6): the V508 transport's four nacelles, bones boosterF_l/r and
// boosterB_l/r under body, each bound level (nose +z) at its mount, hinged along its local X like the elevons.
// The stock Transporter508 tilts them only by its CAS clips (hover_start: 0 -> -90 deg about X, the nacelle's
// nose up, its thrust along the body's up; hover_end back; fly level), which the carrier (the V506's CAS) never
// plays. The plugin poses them (Thrusters) along the thrust its flight asks for: theta = atan2(-up, forward) of
// that thrust in the body (-90 deg hovering, toward 0 flying forward, past -90 braking), within
// [-kThrustBack, 0], at most kThrustRate; turning, the nacelles on either side tilt kThrustYaw apart (a
// tilt-rotor's yaw in the hover).
constexpr float kThrustBack=1.92f,kThrustRate=0.8f,kThrustYaw=0.25f;
const wchar_t* const kThrusterNames[4]={L"boosterF_l",L"boosterF_r",L"boosterB_l",L"boosterB_r"};
// The ground under a jet is body506's GroundClearance: under the ground a ray down sees nothing, so a jet that
// went through it was once taken for one over a void, Guard let it be, and it flew on under the map
// (2026-10-03: a fighter 5 s down to -109, drones to -310); its ray from above finds the surface then.

// The walls: the world's four (fixed) and the ones learned this mission (see kWallSpan), where one was met and
// its horizontal normal, into it.
struct Wall { float at[3],n[3]; ULONGLONG seen; bool on; };
constexpr Wall kWorldWalls[4]={{{kWorldWall,0.0f,0.0f},{1.0f,0.0f,0.0f},0},{{-kWorldWall,0.0f,0.0f},{-1.0f,0.0f,0.0f},0},
                               {{0.0f,0.0f,kWorldWall},{0.0f,0.0f,1.0f},0},{{0.0f,0.0f,-kWorldWall},{0.0f,0.0f,-1.0f},0}};
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
void TurnOff(const Wall& w,const Jet& j,const float* pos,float r,float* want) noexcept {
    const float gap=Gap(w,pos);
    const float closing=j.m.vel[0]*w.n[0]+j.m.vel[2]*w.n[2];
    const float reach=r*1.5f+(closing>0.0f ? closing*kReact : 0.0f);
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
    const float maxG=bombing ? kBombG : k.maxG,tau=bombing ? kBombTau : kSteerTau;
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
    // What the wing gives along the body's up now, and the path that bends.
    const float pull=Clamp(Dot(lift,bodyUp),-kNegG*kG,(bombing ? maxG+1.0f : maxG)*kG);
    float acc[3]={bodyUp[0]*pull,bodyUp[1]*pull-kG,bodyUp[2]*pull};
    const float accAlong=Dot(acc,dir);
    for(int i=0;i<3;++i)next[i]=dir[i]+(acc[i]-dir[i]*accAlong)*dt/s;
    if(!Normalize(next))std::memcpy(next,dir,12);
    const float bleed=pull>kG ? (pull/kG-1.0f)*kTurnBleed : 0.0f;
    s+=Clamp(speed-s,-k.brake*dt,k.thrust*dt)-(kG*next[1]+bleed)*dt;
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

// The elevons after the commanded turn (see kElevonMax).
void Elevons(Jet& j,const Kind& k,unsigned char* v,float dt) noexcept {
    if(!FindSurfaces(j,v,kElevonNames,2,"elevons"))return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* r=m;const float* u=m+4;const float* f=m+8;   // r: the model's +x, the right wing
    float c[3];
    Cross(j.m.omega,f,c);const float pitch=Dot(c,u);           // nose toward the body's up
    Cross(j.m.omega,r,c);const float roll=-Dot(c,u);           // right wing going down
    const float s=Len(j.m.vel),pitchMax=k.maxG*kG/(s>k.minSpeed ? s : k.minSpeed);
    const float p=Clamp(pitch/pitchMax,-1.0f,1.0f),q=Clamp(roll/k.roll,-1.0f,1.0f);
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
void Thrusters(Jet& j,const Kind& k,unsigned char* v,float dt,ULONGLONG ms) noexcept {
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
    }
    PoseSurfaces(j,want,kThrustRate,dt,"thrusters");
    CarrierFlames(v,j.surf.rec,Clamp(Len(j.m.thrust)/kG,0.5f,1.0f),ms);   // the stock Booster flame on each nozzle
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

// A wall within `range` ahead of `pos`.
bool NearWall(const float* pos,float range,ULONGLONG ms) noexcept {
    for(const auto& w:kWorldWalls)if(Gap(w,pos)<range)return true;
    for(const auto& w:learned)if(Holds(w,pos,ms) && Gap(w,pos)<range)return true;
    return false;
}

// Keeps `want` off the walls and the ground and under the ceiling. The ground is the highest under it now and
// kLookAhead seconds along its track; sinking, the lowest it gets is where a maxG pull-out started
// kReact seconds from now bottoms out (so a dive runs down to kMinAlt instead of pulling up 100 m early);
// climbing, the ceiling is checked kLookAhead seconds out.
void Guard(const Jet& j,const float* pos,float* want,ULONGLONG ms) noexcept {
    const Kind& k=KindOf(j);
    const float s=Len(j.m.vel);
    // Walls: turned off from a turn's radius out (more closing fast), never flown into.
    const float r=s*s/(k.maxG*kG);
    for(const auto& w:kWorldWalls)TurnOff(w,j,pos,r,want);
    for(const auto& w:learned)if(Holds(w,pos,ms))TurnOff(w,j,pos,r,want);
    const float ahead[3]={pos[0]+j.m.vel[0]*kLookAhead,pos[1]+j.m.vel[1]*kLookAhead,pos[2]+j.m.vel[2]*kLookAhead};
    const float probe[3]={ahead[0],pos[1]>ahead[1] ? pos[1] : ahead[1],ahead[2]};
    const float here=GroundClearance(pos),there=GroundClearance(probe);
    // Neither ray finding ground (past the map's terrain) left it no floor at all: an interceptor rolled over
    // there and flew on to -1100 (2026-10-04). It keeps the surface it last saw under it then.
    const float seen=j.m.groundSeen ? j.m.groundY : -1e9f;
    const float lowest=there!=kNoGround ? probe[1]-there : seen;
    const float floorY=here!=kNoGround && pos[1]-here>lowest ? pos[1]-here : lowest;
    float bottom=pos[1];
    if(s>1.0f && j.m.vel[1]<0.0f) {
        const float sinDive=Clamp(-j.m.vel[1]/s,0.0f,1.0f),cosDive=std::sqrt(1.0f-sinDive*sinDive);
        const float t=kReact+RollToLift(j)/k.roll;
        bottom-=s*s/(k.maxG*kG)*(1.0f-cosDive)-j.m.vel[1]*t+0.5f*kGravity*sinDive*sinDive*t*t;
    }
    if(bottom<floorY+kMinAlt) {
        const float need=Clamp((floorY+kMinAlt-bottom)/40.0f,0.3f,0.8f);
        if(want[1]<need){want[1]=need;Normalize(want);}
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
//  - under the ground (Clearance found the surface above it: clear < 0; or no ground under it at all and
//    below the surface last seen under it, off the map's edge) it climbs at kFloorClimb at least.
// `clear` is GroundClearance(pos) (kNoGround: none found).
constexpr float kFloorGap=1.0f,kFloorClimb=80.0f,kFloorSweep=3.0f;
void HoldOffGround(Jet& j,const float* pos,float clear,float dt,ULONGLONG ms) noexcept {
    Motion& mo=j.m;
    if(clear!=kNoGround){mo.groundY=pos[1]-clear;mo.groundSeen=true;}
    if(!mo.groundSeen)return;
    const bool under=clear!=kNoGround ? clear<0.0f : pos[1]<mo.groundY;
    float floorY=mo.groundY,need=kFloorClimb;
    if(!under) {
        if(mo.vel[1]>=0.0f)return;
        const float end[3]={pos[0]+mo.vel[0]*dt*kFloorSweep,pos[1]+mo.vel[1]*dt*kFloorSweep-kFloorGap,pos[2]+mo.vel[2]*dt*kFloorSweep};
        float hit[3];
        if(MapRay(pos,end,hit)>=0.0f && hit[1]>floorY && hit[1]<pos[1])floorY=hit[1];
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
// asked for (gravity held, the acceleration, the drag shown), which the lean and the thrusters follow.
void Hover(Jet& j,const Kind& k,const unsigned char* v,const float* pos,const float* goal,const float* face,float speed,float climb,
           float dt) noexcept {
    const Lean& how=*k.lean;
    Motion& mo=j.m;
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
    for(int i=0;i<3;++i)acc[i]=(wantV[i]-mo.vel[i])/respond;
    const float a=Len(acc),most=k.thrust;
    if(a>most)for(int i=0;i<3;++i)acc[i]*=most/a;
    if(how.jerk>0.0f) {
        float change[3]={acc[0]-mo.acc[0],acc[1]-mo.acc[1],acc[2]-mo.acc[2]};
        const float c=Len(change),step=how.jerk*dt;
        if(c>step)for(int i=0;i<3;++i)change[i]*=step/c;
        for(int i=0;i<3;++i)acc[i]=mo.acc[i]+change[i];
    }
    std::memcpy(mo.acc,acc,12);
    for(int i=0;i<3;++i)mo.vel[i]+=acc[i]*dt;
    for(int i=0;i<3;++i)mo.thrust[i]=acc[i]+mo.vel[i]*how.drag;
    mo.thrust[1]+=kG;
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

void Wing(Jet& j,const Kind& k,unsigned char* v,const float* pos,const float* nose,float* want,float speed,float dt,ULONGLONG ms) noexcept {
    Guard(j,pos,want,ms);
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
}
}  // namespace jet
}  // namespace crew
