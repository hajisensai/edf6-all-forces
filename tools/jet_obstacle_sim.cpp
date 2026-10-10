// NPC jets against buildings, without the game: src/jet_flight.cpp (Ahead, Guard, Wing, Sense, HoldOffGround, Patrol),
// src/jet_combat.cpp (Attack -> Strike, InSight) and src/body506.cpp (GroundClearance, BodyAttitude) compiled as they
// are, against a stand-in world: flat ground at y = 0 plus axis-aligned box buildings, seen by MapRay as the game's map
// ray sees them (metres to the nearest hit, -1 with none; a ray starting inside a box sees nothing of that box). The
// jet is a block of zeroed memory with the fields the code reads and writes, moved each frame by the velocity and spin
// the code set (what the 506 physics hook does: jet_hooks.cpp JetBodyStep); a box stops it (pushed out through the
// nearest face, as a rigid body would be held), which Sense sees as being blocked. Modelled on
// origin/feat/primer-swarm tools/primer_flight_sim.cpp.
//
//   jet_obstacle_sim --scenario a|b|b2|c|c2|d|d0 [--kind strike|fighter] [--seconds N] [--out DIR] [--tag NAME]
//   jet_obstacle_sim --selftest [--out DIR]
//   jet_obstacle_sim --edge-suite   (the soft edge's cases, EdgeSuite: exit 1 when one fails; edge_suite.log here)
//   jet_obstacle_sim --player-rotor-suite (real Hover: player/hail goals in the NPC band remain reachable)
//   jet_obstacle_sim --ground-settle-suite (real HoldOffGround: a craft standing on the ground is not thrown up)
//
//   a   level 60 m over the ground at cruise, heading +z, a 150 m tall 60x60 building 1500 m ahead
//   b   the same at a 400 m tall 80x80 tower (too steep to climb: it must turn)
//   b2  b, but steering at a point 600 m behind the tower (not a fixed heading): does it come back into it?
//   b3  b with the tower 600 m ahead (found late, too steep to climb from there: it must turn)
//   b4  b3 steering at a point 600 m behind the tower
//   c   a 5x5 city block (40-180 m tall) under its patrol circle at its kind's patrol height (alt over the anchor)
//   c2  c with the patrol circle 100 m over the ground (inside the block's heights)
//   d   a strike dive at a ground point with a 120 m building 150-210 m in front of it (Attack -> Strike)
//   d0  d with no building (the dive as it is when nothing is in the way)
//
// --selftest (CTest jet_attack_runs): the attack runs the 2026-10-06 log showed failing, each case printed, exit 1 on any
// failure:
//   pull  a dive Guard alone must pull out of (the guidance holds the dive into the ground): every kind that dives, loaded
//         and clean, 15-80 deg, 150-250 m/s, upright and inverted, over flat ground and with a 150 m plateau where its
//         pull-out bottoms, started just high enough for that pull-out (1.3 times PredictPullOut's drop, 60 m more): the
//         lowest it gets over what is under it is kPullMargin or more;
//   entry a called strike jet arriving with its missiles at a ground target ahead and off its nose (Attack -> Strike,
//         Fire): its guns and its missiles fire within its arrival (kEntryMs), the game's lock given.
// Writes DIR/<tag>.csv (the trajectory, every frame) and DIR/<tag>.log (the plugin's own log), prints the summary.
// The "before" build (jet_obstacle_sim_before) compiles the same files with Wing's Ahead call and Strike's InSight
// test cut out (CMakeLists.txt makes the copies), so the two show what Ahead and InSight change.
#include "../src/jet_internal.h"
#include "../src/gear.h"
#include "../src/jet_pullout.h"
#include "../src/stores.h"
#include "../src/pjet_handling.h"
#include "../src/nacelle_reach.h"
#include "../src/heliaim.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
PlayerFix player{};
namespace {
Config config{};
ULONGLONG nowMs=0;
std::FILE* logFile=nullptr;

struct Box { float lo[3],hi[3]; };
std::vector<Box> boxes;

// Ray a->b against box `b`: the fraction to its entry face, or -1 (missed, or `a` inside it: nothing of it seen).
float RayBox(const float* a,const float* d,const Box& b) noexcept {
    float t0=-1e30f,t1=1e30f;
    for(int i=0;i<3;++i) {
        if(std::fabs(d[i])<1e-9f) {
            if(a[i]<b.lo[i] || a[i]>b.hi[i])return -1.0f;
            continue;
        }
        float u=(b.lo[i]-a[i])/d[i],w=(b.hi[i]-a[i])/d[i];
        if(u>w){const float x=u;u=w;w=x;}
        if(u>t0)t0=u;
        if(w<t1)t1=w;
    }
    if(t0>t1 || t0<0.0f || t0>1.0f)return -1.0f;
    return t0;
}
}  // namespace
const Config& Cfg() noexcept { return config; }
// The play area (playarea.cpp measures the map's ground in the game): the physics square, or (the edge suite's ground
// cases) a stock map's ground, `groundHalf` m a side, its walls playarea.h's kVoidMargin inside it.
float groundHalf=0.0f;
PlayArea MapPlayArea() noexcept {
    const float e=groundHalf>0.0f ? groundHalf-area::kVoidMargin : PlayEdge();
    return PlayArea{{-e,-e},{e,e},groundHalf>0.0f,0.0f,false};
}
ULONGLONG GameMs() noexcept { return nowMs; }
ULONGLONG GameFrame() noexcept { return nowMs/16; }
void Log(const char* format,...) noexcept {
    if(!logFile)return;
    std::fprintf(logFile,"%8.3f ",static_cast<double>(nowMs)*0.001);
    va_list a;va_start(a,format);std::vfprintf(logFile,format,a);va_end(a);
    std::fputc('\n',logFile);
}
void SetObjectTeam(unsigned char* object,std::int32_t team) noexcept { Put<std::int32_t>(object,kTeam,team); }
// The map ray (heli.cpp CastRay): metres along a->b to the nearest of the ground and the boxes, -1 with none.
float MapRay(const float* a,const float* b,float* hit) noexcept {
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    float best=2.0f;
    if(a[1]>0.0f && b[1]<=0.0f)best=a[1]/(a[1]-b[1]);
    for(const auto& bx:boxes) {
        const float t=RayBox(a,d,bx);
        if(t>=0.0f && t<best)best=t;
    }
    if(best>1.0f)return -1.0f;
    for(int i=0;i<3;++i)hit[i]=a[i]+d[i]*best;
    return best*std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}
// Nothing else is in this world: no enemies to visit (Strike gets its target from the harness), nobody in line.
// The enemies the harness puts in (--selftest's focus cases); none elsewhere.
struct SimEnemy { const void* object; float aim[3]; };
std::vector<SimEnemy> simEnemies;
bool VisitEnemies(const unsigned char*,EnemyVisitor visit,void* ctx) noexcept {
    for(const auto& e:simEnemies)visit(ctx,e.object,e.aim);
    return !simEnemies.empty();
}
bool FriendInLine(const float*,const float*,const void*) noexcept { return false; }
bool GunBarrel(const unsigned char*,const unsigned char*,float*,float*) noexcept { return false; }
int ReadStores(unsigned char*,Store*,int) noexcept { return 0; }
void TriggerStore(const Store&) noexcept {}
void NpcGear(unsigned char*,float,float) noexcept {}
void JetFlames(const unsigned char*,float,bool,ULONGLONG) noexcept {}
void JetSmoke(const unsigned char*,bool,ULONGLONG) noexcept {}
bool JetBodyStep(unsigned char*,float*,float*) noexcept { return false; }
bool SubBodyStep(unsigned char*,float*,float*) noexcept { return false; }
bool PlayerJetBodyStep(unsigned char*,float*,float*) noexcept { return false; }
bool SazabiBodyStep(unsigned char*,float*,float*) noexcept { return false; }
bool SubMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { return false; }
bool PlayerJetMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { return false; }
bool SazabiMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { return false; }
// No enemy creatures participate in the building-avoidance scenario.
bool PrimerMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { return false; }
namespace jet {
Jet jets[kMaxJets]{};
bool Preloaded(Body) noexcept { return true; }   // every body there (LimitsOf: a doll carrier launches doll drones)
}  // namespace jet
}  // namespace crew

using namespace crew;
using namespace crew::jet;

namespace {
constexpr std::size_t kBodySize=0x3000,kCtrlSize=0x40;
constexpr float kDt=1.0f/60.0f;

void AddBox(float cx,float cz,float w,float d,float h) {
    boxes.push_back(Box{{cx-w*0.5f,0.0f,cz-d*0.5f},{cx+w*0.5f,h,cz+d*0.5f}});
}
// Metres from `p` to box `b` (0 inside).
float BoxGap(const float* p,const Box& b) {
    float s=0.0f;
    for(int i=0;i<3;++i) {
        const float e=p[i]<b.lo[i] ? b.lo[i]-p[i] : p[i]>b.hi[i] ? p[i]-b.hi[i] : 0.0f;
        s+=e*e;
    }
    return std::sqrt(s);
}
bool Inside(const float* p,const Box& b) {
    return p[0]>b.lo[0] && p[0]<b.hi[0] && p[1]>b.lo[1] && p[1]<b.hi[1] && p[2]>b.lo[2] && p[2]<b.hi[2];
}

// The 506 step's part: the body moves by the velocity and spin the code set; a box holds it at its surface (out
// through the face it went in least deep: a wall, or its roof). Returns whether a box held it this frame.
bool Move(Jet& j,unsigned char* v) {
    float* p=reinterpret_cast<float*>(v+kPosition);
    float* m=reinterpret_cast<float*>(v+kMatrix);
    for(int i=0;i<3;++i)p[i]+=j.m.vel[i]*kDt;
    bool held=false;
    for(const auto& b:boxes) {
        if(!Inside(p,b))continue;
        held=true;
        int axis=0;float best=1e30f,to=0.0f;
        for(int i=0;i<3;++i) {
            const float dl=p[i]-b.lo[i],dh=b.hi[i]-p[i];
            if(i!=1 && dl<best){best=dl;axis=i;to=b.lo[i]-0.01f;}
            if(dh<best){best=dh;axis=i;to=b.hi[i]+0.01f;}
        }
        p[axis]=to;
    }
    if(p[1]<0.5f){p[1]=0.5f;held=true;}
    for(int r=0;r<3;++r) {
        float* row=m+r*4;
        float c[3];Cross(j.m.omega,row,c);
        for(int i=0;i<3;++i)row[i]+=c[i]*kDt;
    }
    float* rx=m;float* ry=m+4;float* rz=m+8;
    Normalize(rz);
    float x[3];Cross(ry,rz,x);Normalize(x);std::memcpy(rx,x,12);
    float y[3];Cross(rz,rx,y);Normalize(y);std::memcpy(ry,y,12);
    std::memcpy(m+12,p,12);
    return held;
}

Role RoleNamed(const char* n) {
    if(!std::strcmp(n,"fighter"))return Role::fighter;
    if(!std::strcmp(n,"interceptor"))return Role::interceptor;
    if(!std::strcmp(n,"multirole"))return Role::multirole;
    return Role::strike;
}

// --- --selftest ---
constexpr float kFerryCheckOver=30.0f;    // m: a ferry's pass crosses its point this near its line at least
constexpr float kPullMargin=15.0f;   // m: the least a pull-out may leave under it (Guard aims at kMinAlt, 25)

// A body for a selftest case: at `at`, flying `dir` (unit) at `speed`, rolled `bank` rad about its path (0 upright).
void Place(Jet& j,unsigned char* v,unsigned char* ctrl,Role role,const float* at,const float* dir,float speed,float bank) {
    std::memset(v,0,kBodySize);
    float f[3]={dir[0],dir[1],dir[2]};Normalize(f);
    float r[3]={f[2],0.0f,-f[0]};   // right: as main() builds it (heading +z: right +x)
    if(!Normalize(r)){r[0]=1;r[1]=0;r[2]=0;}
    float u[3];Cross(f,r,u);Normalize(u);
    if(u[1]<0.0f)for(int i=0;i<3;++i)u[i]=-u[i];
    const float c=std::cos(bank),sn=std::sin(bank);
    float u2[3],r2[3];
    for(int i=0;i<3;++i){u2[i]=u[i]*c+r[i]*sn;r2[i]=r[i]*c-u[i]*sn;}
    const float mat[16]={r2[0],r2[1],r2[2],0, u2[0],u2[1],u2[2],0, f[0],f[1],f[2],0, at[0],at[1],at[2],1};
    std::memcpy(v+kMatrix,mat,64);
    std::memcpy(v+kPosition,at,12);
    Put<float>(v,kHpMax,1000.0f);Put<float>(v,kHp,1000.0f);
    std::memset(ctrl,0,kCtrlSize);
    Put<long>(ctrl,8,1);
    Put<const void*>(v,kSelfCtrl,ctrl);
    j=Jet{};
    j.ref=ObjRef{v,ctrl};j.role=role;j.mode=Mode::patrol;j.drone.slot=-1;
    j.bornAt=j.modeAt=j.seen=nowMs;
    for(int i=0;i<3;++i)j.m.vel[i]=f[i]*speed;
}

// One pull case: the lowest it got over what is under it (GroundClearance), or -1 if it went into it.
float PullCase(Role role,float mass,float diveDeg,float speed,bool inverted,bool plateau,float* startY) {
    boxes.clear();
    const Kind& k=KindOf(role);
    const float dive=diveDeg*kPi/180.0f,dir[3]={0.0f,-std::sin(dive),std::cos(dive)};
    const float top=k.attack*1.3f<kBodyTop ? k.attack*1.3f : kBodyTop;
    const PullOutIn in{speed,std::sin(dive),k.maxG/mass,inverted ? kPi-0.05f : 0.0f,k.roll,6.0f,1.0f,k.thrust/mass,top,kG};
    const PullOut p=PredictPullOut(in);
    const float ground=plateau ? 150.0f : 0.0f;
    const float y0=ground+p.drop*1.3f+60.0f;
    *startY=y0;
    // The plateau: from half the pull-out's run on (under where it bottoms), none under the start.
    if(plateau)AddBox(0.0f,p.run*0.5f+p.run*2.0f,4000.0f,p.run*4.0f,ground);
    std::vector<unsigned char> mem(kBodySize,0),ctrl(kCtrlSize,0);
    unsigned char* v=mem.data();
    Jet& j=jets[0];
    const float at[3]={0.0f,y0,0.0f};
    Place(j,v,ctrl.data(),role,at,dir,speed,inverted ? kPi-0.05f : 0.0f);
    j.burden=Burden{mass,0.0f};
    float lowest=1e9f;
    bool into=false;
    for(int f=0;f<60*30;++f) {
        nowMs+=16;
        const float* pos=reinterpret_cast<const float*>(v+kPosition);
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        float nose[3]={m[8],m[9],m[10]};
        if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
        j.seen=nowMs;
        Sense(j,pos,nowMs);
        const float clear=GroundClearance(pos);
        float want[3]={dir[0],dir[1],dir[2]};   // held in the dive: only Guard pulls it out
        Wing(j,k,v,pos,GroundClearance(pos),nose,want,k.attack,kDt,nowMs);
        const float sink=j.m.vel[1];
        HoldOffGround(j,pos,clear,kDt,nowMs);
        if(j.m.vel[1]>sink+0.01f)into=true;   // the floor caught it: it would have gone in
        j.m.ready=true;
        if(Move(j,v))into=true;
        const float c=GroundClearance(pos);
        if(c!=kNoGround && c<lowest)lowest=c;
        if(f>60 && j.m.vel[1]>20.0f)break;   // climbing away: out of it
    }
    return into ? -1.0f : lowest;
}

// The entry case: a strike jet (loaded) 2600 m from its target and `over` over it, the target `offDeg` off its nose, the
// game locking its missiles; called (`launched`: arriving, Entering) or not. When (s since it came) its guns first fire
// and its first missile goes (-1: never), and its height over the target at its first burst.
void EntryCase(float offDeg,float over,bool launched,float* gunAt,float* missileAt,float* overAtFire) {
    boxes.clear();
    const Role role=Role::strike;
    const Kind& k=KindOf(role);
    const float aim[3]={0.0f,0.0f,0.0f};
    const float off=offDeg*kPi/180.0f;
    const float at[3]={0.0f,over,-2600.0f},dir[3]={std::sin(off),0.0f,std::cos(off)};
    std::vector<unsigned char> mem(kBodySize,0),ctrl(kCtrlSize,0);
    unsigned char* v=mem.data();
    Jet& j=jets[0];
    Place(j,v,ctrl.data(),role,at,dir,k.cruise,0.0f);
    j.launched=launched;j.burden=Burden{launched ? 1.21f : 1.0f,0.0f};
    static int targetDummy=0;
    j.t.target=&targetDummy;j.t.flyer=false;std::memcpy(j.t.aim,aim,12);std::memcpy(j.t.tgtPrev,aim,12);
    Arms arms{};arms.gunSpeed=960.0f;arms.gunRange=960.0f;arms.pick=-1;arms.rocket=-1;arms.guns=3600;arms.hasGun=true;
    arms.hasMissile=true;arms.missiles=6;arms.locked=1;arms.missileRange=1800.0f;
    const ULONGLONG born=nowMs;
    *gunAt=*missileAt=-1.0f;*overAtFire=0.0f;
    for(int f=0;f<60*20;++f) {
        nowMs+=16;
        const float* pos=reinterpret_cast<const float*>(v+kPosition);
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        float nose[3]={m[8],m[9],m[10]};
        if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
        j.seen=nowMs;
        Sense(j,pos,nowMs);
        const float clear=GroundClearance(pos);
        float want[3]={nose[0],0.0f,nose[2]},speed=k.cruise;
        bool gunsOk=false,missileOk=false;
        Attack(j,arms,pos,nose,aim,aim[1]+k.alt,nowMs,want,&speed,&gunsOk,&missileOk);
        Wing(j,k,v,pos,GroundClearance(pos),nose,want,speed,kDt,nowMs);
        HoldOffGround(j,pos,clear,kDt,nowMs);
        j.m.ready=true;
        Fire(j,v,pos,nose,aim,gunsOk,missileOk,arms,nowMs);
        const float t=static_cast<float>(nowMs-born)*0.001f;
        if(v[kFireGun] && *gunAt<0.0f){*gunAt=t;*overAtFire=pos[1]-aim[1];}
        if(v[kFireMissile] && *missileAt<0.0f)*missileAt=t;
        Move(j,v);
    }
}

int SelfTest(const char* outDir) {
    const std::string logPath=std::string(outDir)+"/selftest.log";
    logFile=std::fopen(logPath.c_str(),"w");
    config.debug=true;config.jetPilot=true;
    // A world big enough that no case reaches the play edge's soft line (src/airbound.h, the edge suite's business): an
    // entry 2600 m out, a dive's pull-out run out to 4 km.
    config.bigWorld=20000.0f;
    int bad=0,cases=0;
    float worst=1e9f;
    // Loaded: the strike jet's 6 AGM, 6 Mk 82, 38 rockets and 2 AIM-9X on its 22 t (1.21); the multirole's load on 18 t.
    const struct { Role role; float mass; } loads[]={{Role::strike,1.21f},{Role::strike,1.0f},{Role::multirole,1.18f},
                                                     {Role::fighter,1.0f},{Role::drone,1.0f}};
    const float dives[]={15.0f,30.0f,45.0f,60.0f,80.0f},speeds[]={150.0f,200.0f,250.0f};
    for(const auto& l:loads)for(float d:dives)for(float s:speeds)for(int inv=0;inv<2;++inv)for(int pl=0;pl<2;++pl) {
        float y0=0.0f;
        const float low=PullCase(l.role,l.mass,d,s,inv!=0,pl!=0,&y0);
        ++cases;
        const bool ok=low>=kPullMargin;
        if(!ok)++bad;
        if(low<worst)worst=low;
        std::printf("pull %-9s x%.2f %2.0f deg %3.0f m/s %-8s %-7s from %5.0f m: lowest %6.1f m %s\n",KindOf(l.role).name,
                    static_cast<double>(l.mass),static_cast<double>(d),static_cast<double>(s),inv ? "inverted" : "upright",
                    pl ? "plateau" : "flat",static_cast<double>(y0),static_cast<double>(low),ok ? "ok" : "FAIL");
    }
    std::printf("pull: %d cases, %d failed, lowest %.1f m (margin %.0f m)\n",cases,bad,static_cast<double>(worst),
                static_cast<double>(kPullMargin));
    // Called, arriving 150 m over its target (as the 2026-10-06 log's did); and runs in at its attack height (alt), not
    // arriving: the ordinary strike run must fire as well.
    const float alt=KindOf(Role::strike).alt;
    const struct { float off,over; bool launched; } runs[]={{0.0f,150.0f,true},{15.0f,150.0f,true},{30.0f,150.0f,true},
                                                            {0.0f,alt,false},{20.0f,alt,false}};
    for(const auto& r:runs) {
        float gun=0.0f,msl=0.0f,over=0.0f;
        EntryCase(r.off,r.over,r.launched,&gun,&msl,&over);
        const float entry=static_cast<float>(kEntryMs)*0.001f;
        const bool ok=gun>=0.0f && gun<entry && msl>=0.0f && msl<entry;
        if(!ok)++bad;
        std::printf("%s %3.0f m over, target %2.0f deg off the nose: guns at %.1f s (%.0f m over it), missile at %.1f s %s\n",
                    r.launched ? "entry" : "run  ",static_cast<double>(r.over),static_cast<double>(r.off),static_cast<double>(gun),
                    static_cast<double>(over),static_cast<double>(msl),ok ? "ok" : "FAIL");
    }
    // The paratroop plane's passes (jet_flight.cpp Ferry, ferry_line.h; 2026-10-10, the user: 「空降时外援飞来的路线不对……
    // 直线之类的更合理的路线才对吧」): on a stock map's ground (+-1750 m, its soft box +-1000) it comes in from off the map
    // (support_entry.h AirRoute: 1950-2700 m out) on the line through the point, along it over the point and on to the line's
    // end off the map, round there (inside the square, never deleted at the world's edge) and back over the point along the
    // same line; its stick out, on to the end of that pass and gone there. The soft edge never turns it back on its line.
    // Before: chasing the point it went by 288 m abeam and circled the north-east corner for 2 minutes (13:17-13:19 log).
    {
        boxes.clear();
        const float wasGround=groundHalf;groundHalf=1750.0f;
        const Role role=Role::strike;
        std::vector<unsigned char> mem(kBodySize,0),ctrl(kCtrlSize,0);
        unsigned char* v=mem.data();
        Jet& j=jets[0];
        const float point[3]={150.0f,0.0f,-200.0f};
        const struct { const char* what; float at[3],dir[3]; } runs[]={
            {"from off the map, on the line",{-2350.0f,150.0f,-200.0f},{1.0f,0.0f,0.0f}},
            {"from off the map, 300 m off the line",{-2350.0f,150.0f,100.0f},{1.0f,0.0f,0.0f}},
            {"diagonal, from a corner",{-1700.0f,150.0f,-2050.0f},{0.707f,0.0f,0.707f}}};
        for(const auto& r:runs) {
            float dir[3]={r.dir[0],0.0f,r.dir[2]};Normalize(dir);
            Place(j,v,ctrl.data(),role,r.at,dir,KindOf(role).cruise,0.0f);
            support::PassLine line{};
            const support::Reach reach{WorldHalf()-kArrivalRoom,1000.0f};
            const bool lined=support::MakePassLine(MapPlayArea(),reach,point,dir,FerryTurn(KindOf(role)),line);
            j.ferry=true;j.ferryLine=line;j.ferryPass=ferry::Pass{};j.ferryGone=false;
            int crossings=0,ends=0;float worstAcross=0.0f,mostOut=0.0f,firstAt=-1.0f;
            bool lastAhead=true,inEnd=false,gone=false;float goneAt[3]{};
            const ULONGLONG born=nowMs;
            for(int f=0;f<60*300 && !gone;++f) {
                nowMs+=16;
                const float* pos=reinterpret_cast<const float*>(v+kPosition);
                const float* m=reinterpret_cast<const float*>(v+kMatrix);
                float nose[3]={m[8],m[9],m[10]};
                if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
                j.seen=nowMs;
                Sense(j,pos,nowMs);
                const float clear=GroundClearance(pos);
                float want[3]={nose[0],0.0f,nose[2]};
                const float speed=Ferry(j,pos,point[1]+150.0f,want);
                Wing(j,KindOf(role),v,pos,clear,nose,want,speed,kDt,nowMs);
                HoldOffGround(j,pos,clear,kDt,nowMs);
                j.m.ready=true;
                Move(j,v);
                if(j.ferryGone){gone=true;std::memcpy(goneAt,pos,12);}
                float along,across;ferry::Along(line,pos,&along,&across);
                const bool ahead=along<0.0f ? j.ferryPass.dir>0 : j.ferryPass.dir<0;   // the point still ahead on this pass
                if(lastAhead && !ahead && std::fabs(along)<200.0f) {   // over the point (along its pass)
                    ++crossings;
                    if(firstAt<0.0f)firstAt=static_cast<float>(nowMs-born)*0.001f;
                    if(crossings>0 && std::fabs(across)>worstAcross)worstAcross=std::fabs(across);
                    if(crossings==2)j.ferryPass.done=true;   // the stick out on the second pass (JetFerryDone)
                }
                lastAhead=ahead;
                const bool atEnd=along>=line.hi || along<=line.lo;
                if(atEnd && !inEnd)++ends;
                inEnd=atEnd;
                const float out=std::fmax(std::fabs(pos[0]),std::fabs(pos[2]));
                if(out>mostOut)mostOut=out;
            }
            float glo[2],ghi[2];support::GroundEdge(MapPlayArea(),glo,ghi);
            const bool offMap=gone && (goneAt[0]<glo[0] || goneAt[0]>ghi[0] || goneAt[2]<glo[1] || goneAt[2]>ghi[1]);
            const bool ok=lined && crossings>=2 && worstAcross<kFerryCheckOver && ends>=2 && gone && offMap &&
                          mostOut<WorldHalf()-50.0f && firstAt>=0.0f && firstAt<60.0f;
            if(!ok)++bad;
            std::printf("ferry %s: over the point at %.1f s, %d passes %.0f m off its line at most, %d ends reached, gone %s at "
                        "(%.0f,%.0f), %.0f m out at most %s\n",r.what,static_cast<double>(firstAt),crossings,static_cast<double>(worstAcross),
                        ends,offMap ? "off the map" : gone ? "ON THE MAP" : "never",static_cast<double>(goneAt[0]),static_cast<double>(goneAt[2]),
                        static_cast<double>(mostOut),ok ? "ok" : "FAIL");
        }
        groundHalf=wasGround;
    }
    // The map's focus order (2026-10-09, "飞机没办法指定攻击目标"): the marked enemy is the target before a nearer one and
    // past its guard order's range (production PickTarget / VisitTarget); out past the walls it waits, the others fought;
    // gone from the enemies, the focus is let go and its order's targets come back.
    {
        const Role role=Role::multirole;
        std::vector<unsigned char> mem(kBodySize,0),ctrl(kCtrlSize,0),close(0x400,0),distant(0x400,0),closeCtrl(16,0),distantCtrl(16,0);
        Put<const void*>(close.data(),kSelfCtrl,closeCtrl.data());Put<const void*>(distant.data(),kSelfCtrl,distantCtrl.data());
        Jet& j=jets[0];
        const float at[3]={0.0f,400.0f,0.0f},dir[3]={0.0f,0.0f,1.0f};
        Place(j,mem.data(),ctrl.data(),role,at,dir,KindOf(role).cruise,0.0f);
        const Kind& k=KindOf(role);
        const float anchor[3]={0.0f,0.0f,0.0f};
        ApplyMapCommand(j,Command{Order::guard,{0.0f,0.0f,0.0f}},nowMs);
        const SimEnemy closeOne{close.data(),{300.0f,0.0f,200.0f}},distantOne{distant.data(),{0.0f,0.0f,k.range+3000.0f}};
        simEnemies={closeOne,distantOne};
        auto pick=[&]{PickTarget(j,mem.data(),at,anchor,k.range,kDt,nowMs);return j.t.target;};
        struct { const char* what; bool ok; } cases[5]{};
        cases[0]={"guard order: the enemy in its range, the one past it not",pick()==close.data()};
        ApplyMapFocus(j,ObjRef::Of(distant.data()),nowMs);
        cases[1]={"focus: the marked enemy past its range before the nearer one",
                  pick()==distant.data() && static_cast<bool>(j.focus) && j.cmd.order==Order::guard};
        simEnemies[1].aim[2]=PlayEdge()+500.0f;
        cases[2]={"focus out past the walls: the others fought, the focus kept",pick()==close.data() && static_cast<bool>(j.focus)};
        simEnemies.pop_back();
        cases[3]={"focus gone from the enemies: let go, its order's target back",pick()==close.data() && !j.focus};
        ApplyMapFocus(j,ObjRef::Of(close.data()),nowMs);ApplyMapCommand(j,Command{Order::none,{}},nowMs);
        cases[4]={"another order lets the focus go",!j.focus};
        for(const auto& c:cases) {
            if(!c.ok)++bad;
            std::printf("focus %-70s %s\n",c.what,c.ok ? "ok" : "FAIL");
        }
        simEnemies.clear();
    }
    // What a weapon that cannot reach every enemy goes for (2026-10-10, src/air_chase.h; production PickTarget / VisitTarget,
    // the flyer probe on this world's ground at y 0): the gunship never a flyer; a doll drone, and a doll carrier, nothing
    // over the charge's ceiling, the current target neither, nor one out of its range; one it gave up (shunned) not again
    // until its shun is over; a gun jet any flyer as before.
    {
        std::vector<unsigned char> mem(kBodySize,0),ctrl(kCtrlSize,0),low(0x400,0),high(0x400,0),ground(0x400,0),lowCtrl(16,0),
            highCtrl(16,0),groundCtrl(16,0);
        Put<const void*>(low.data(),kSelfCtrl,lowCtrl.data());Put<const void*>(high.data(),kSelfCtrl,highCtrl.data());
        Put<const void*>(ground.data(),kSelfCtrl,groundCtrl.data());
        Jet& j=jets[0];
        const float at[3]={0.0f,150.0f,0.0f},dir[3]={0.0f,0.0f,1.0f},anchor[3]={0.0f,150.0f,0.0f};
        // The low one a flyer 80 m up and 400 m off; the high one 483 m up (the log's) and nearer.
        simEnemies={SimEnemy{low.data(),{400.0f,80.0f,0.0f}},SimEnemy{high.data(),{0.0f,483.0f,100.0f}}};
        auto pick=[&](Role role,float range){
            nowMs+=1000;   // past the flyer memo's look
            const Role drones=j.carrier.drones;
            const void* const was=j.t.target;
            const airchase::ShunList shun=j.t.shun;
            Place(j,mem.data(),ctrl.data(),role,at,dir,0.0f,0.0f);
            j.carrier.drones=drones;j.t.target=was;j.t.shun=shun;j.m.groundSeen=true;j.m.groundY=0.0f;
            PickTarget(j,mem.data(),at,anchor,range,kDt,nowMs);
            return j.t.target;
        };
        struct { const char* what; bool ok; } cases[9]{};
        j.t.target=nullptr;j.carrier.drones=Role::drone;
        cases[0]={"a gun jet: the nearer flyer, 483 m up",pick(Role::fighter,1800.0f)==high.data()};
        j.t.target=nullptr;
        cases[1]={"the gunship: no flyer at all",pick(Role::gunship,1800.0f)==nullptr};
        simEnemies.push_back(SimEnemy{ground.data(),{600.0f,2.0f,0.0f}});   // a third: on the ground
        j.t.target=nullptr;
        cases[2]={"the gunship: the one on the ground",pick(Role::gunship,1800.0f)==ground.data()};
        simEnemies.pop_back();
        j.t.target=nullptr;
        cases[3]={"a doll drone: the low flyer, not the one over its ceiling",pick(Role::doll,1800.0f)==low.data()};
        j.t.target=high.data();
        cases[4]={"a doll drone: its current target over its ceiling let go",pick(Role::doll,1800.0f)==low.data()};
        j.t.target=low.data();
        cases[5]={"a doll drone: its current target out of its range let go",pick(Role::doll,300.0f)==nullptr};
        j.t.target=nullptr;j.carrier.drones=Role::doll;
        cases[6]={"a doll carrier: no drone at the one over their ceiling",pick(Role::carrier,1800.0f)==low.data()};
        j.t.target=nullptr;j.carrier.drones=Role::drone;
        airchase::Shun(j.t.shun,low.data(),nowMs+1000+airchase::kShunMs);
        cases[7]={"a doll drone: the one it gave up not taken while shunned",pick(Role::doll,1800.0f)==nullptr};
        nowMs+=airchase::kShunMs;
        cases[8]={"...and taken again after",pick(Role::doll,1800.0f)==low.data()};
        for(const auto& c:cases) {
            if(!c.ok)++bad;
            std::printf("limits %-70s %s\n",c.what,c.ok ? "ok" : "FAIL");
        }
        simEnemies.clear();
        jets[0]=Jet{};
    }
    if(logFile)std::fclose(logFile);
    std::printf("%s\n",bad ? "SELFTEST FAILED" : "selftest passed");
    return bad ? 1 : 0;
}
}  // namespace

// --edge-suite: the soft edge (src/airbound.h, jet_flight.cpp SoftEdge) against jets at the play edge, run through Wing
// (Ahead, Guard, JetSteer) as the game runs it. Each case starts a jet `start` m inside its soft line (negative: past
// it, in the band), flying `heading` degrees off straight out (+x; the corner cases toward +x +z), at a share of its
// attack speed, steering at a target out past the play edge (fixed far out along its heading, or flying along the edge
// out there, or up over the ceiling). A case started with room for its turn back (SoftEdge's own reach: Excursion, and
// in a corner the whole circle of its turn, airbound::Margin) must never go more than kEdgeTol past the soft line (nor
// over the soft ceiling); one started without (too near, too fast: what the band is for) or past the line must stay
// off the play edge and the ceiling and be back inside at its end.
namespace {
constexpr float kEdgeTol=50.0f;
unsigned char fakeArea[0x100]{};   // the move area manager stand-in: +0x3C the ceiling (body506.cpp CeilingY)

airbound::Box SuiteSoft(const Jet& j,float* band) {
#ifdef EDGE_SUITE_OLD
    // The tree before the soft edge: the same box, from the same ini defaults.
    const Kind& k=KindOf(j);
    float g=1.0f+k.thrust/3.0f;
    if(g>k.maxG)g=k.maxG;
    const float b=airbound::Band(airbound::TurnRadius(k.attack,std::sqrt(g*g-1.0f))*1.15f,1.0f,600.0f,PlayEdge());
    *band=b;
    return airbound::Inset(airbound::Square(PlayEdge()),b);
#else
    return JetSoftBox(j,band);
#endif
}
float SuiteSoftTop() {
#ifdef EDGE_SUITE_OLD
    return CeilingY()-kCeilingGap-150.0f;
#else
    return CeilingY()-kCeilingGap-Cfg().airSoftCeil;
#endif
}

enum class Chase { out, along, corner, climb };
const char* const kChaseNames[]={"out","along","corner","climb"};
struct EdgeCase { Role role; float bigWorld,start,heading,share; Chase chase; float seconds; float ground=0.0f; };
struct EdgeOut { float pastSoft,pastHard,overTop,overCeil,endDepth,pastGround; bool fits,room,ok,judged; };

EdgeOut EdgeRun(const EdgeCase& c) {
    config.bigWorld=c.bigWorld;
    groundHalf=c.ground;
    ResetWalls();
    const Kind& kind=KindOf(c.role);
    std::vector<unsigned char> mem(kBodySize,0),ctrl(kCtrlSize,0);
    unsigned char* v=mem.data();
    Jet& j=jets[0];
    j=Jet{};
    j.ref=ObjRef{v,ctrl.data()};j.role=c.role;j.mode=Mode::chase;j.drone.slot=-1;
    nowMs=1000;j.bornAt=j.modeAt=j.seen=nowMs;
    float band=0.0f;
    const airbound::Box soft=SuiteSoft(j,&band),hard=PlayBox();
    const float softTop=SuiteSoftTop(),ceil=CeilingY()-kCeilingGap;
    const float rad=c.heading*kPi/180.0f;
    float heading[3]={std::cos(rad),0.0f,std::sin(rad)};
    float start[3]={soft.hi[0]-c.start,600.0f,0.0f};
    if(c.chase==Chase::corner) {
        const float a=(45.0f+c.heading)*kPi/180.0f;   // heading off the corner's diagonal
        heading[0]=std::cos(a);heading[2]=std::sin(a);
        start[2]=soft.hi[1]-c.start;
    }
    float climb=0.0f;
    if(c.chase==Chase::climb){start[0]=0.0f;start[1]=softTop-c.start;climb=0.5f;}   // 30 deg up, mid-map
    const float right[3]={heading[2],0.0f,-heading[0]};
    const float mat[16]={right[0],0,right[2],0, 0,1,0,0, heading[0],0,heading[2],0, start[0],start[1],start[2],1};
    std::memcpy(v+kMatrix,mat,64);
    std::memcpy(v+kPosition,start,12);
    Put<float>(v,kHpMax,1000.0f);Put<float>(v,kHp,1000.0f);
    Put<long>(ctrl.data(),8,1);
    Put<const void*>(v,kSelfCtrl,ctrl.data());
    const float s0=std::fmin(kind.attack,TightSpeed(kind,airbound::HalfOf(hard)))*c.share;   // a small area's jet flies slower
    const float hs=std::sqrt(1.0f-climb*climb);
    j.m.vel[0]=heading[0]*s0*hs;j.m.vel[1]=s0*climb;j.m.vel[2]=heading[2]*s0*hs;
    float goal[3]={start[0]+heading[0]*20000.0f,start[1],start[2]+heading[2]*20000.0f};
    if(c.chase==Chase::along){goal[0]=hard.hi[0]+600.0f;goal[2]=-3000.0f;}
    if(c.chase==Chase::climb){goal[0]=start[0]+heading[0]*3000.0f;goal[1]=ceil+5000.0f;goal[2]=start[2]+heading[2]*3000.0f;}
    // Room for its turn back at the start, as SoftEdge reckons it (kReact 1 s plus a quarter roll; the turn at its g,
    // the push over at 1 + kNegG = 2 g).
    const float react=1.0f+0.5f*kPi*0.5f/kind.roll,hs0=s0*hs;
    float gHeld=1.0f+kind.thrust/3.0f;   // jet_flight.cpp TurnRadiusOf: the g its thrust holds (kTurnBleed 3), x 1.15
    if(gHeld>kind.maxG)gHeld=kind.maxG;
    const float r=airbound::TurnRadius(hs0,std::sqrt(gHeld*gHeld-1.0f))*1.15f;
    // The box holds its turns (airbound::kRoomTurns radii at its attack speed each side of the middle): the stock 2400 m
    // edge does not for these kinds (the band falls back to the ini's 600 m), and there only the play edge is held.
    const bool fits=c.ground<=0.0f && airbound::HalfOf(soft)+1.0f>=airbound::kRoomTurns*
                    airbound::TurnRadius(kind.attack,std::sqrt(gHeld*gHeld-1.0f))*1.15f;
    bool room=start[0]+0.0f<=soft.hi[0] && soft.hi[0]-start[0]>=airbound::Excursion(hs0,j.m.vel[0],r,react)+airbound::kSlack;
    if(c.chase==Chase::corner)
        room=std::fmax(airbound::Margin(soft,start,j.m.vel,r,react,1.0f),airbound::Margin(soft,start,j.m.vel,r,react,-1.0f))>=0.0f;
    if(c.chase==Chase::climb)
        room=softTop-start[1]>=airbound::Excursion(s0,j.m.vel[1],airbound::TurnRadius(s0,2.0f)*1.15f,react);
    if(c.start<0.0f)room=false;
    // The ground cases: the ground's own edge, not the walls the code under test puts on it (a wrong PlayBox moves both).
    const airbound::Box groundBox=airbound::Square(c.ground);
    EdgeOut o{-1e9f,-1e9f,-1e9f,-1e9f,0.0f,-1e9f,fits,room,true,true};
    double clock=1.0;
    const int frames=static_cast<int>(c.seconds*60.0f);
    for(int f=0;f<frames;++f) {
        clock+=1.0/60.0;
        nowMs=static_cast<ULONGLONG>(clock*1000.0+0.5);
        if(c.chase==Chase::along)goal[2]+=150.0f*kDt;   // the target flies along the edge out there at 150 m/s
        const float* pos=reinterpret_cast<const float*>(v+kPosition);
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        float nose[3]={m[8],m[9],m[10]};
        if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
        j.seen=nowMs;
        Sense(j,pos,nowMs);
        const float clear=GroundClearance(pos);
        float want[3];
        Toward(pos,goal,want);
        if(c.chase!=Chase::climb)Level(pos,want,start[1],want);
        Wing(j,kind,v,pos,GroundClearance(pos),nose,want,kind.attack,kDt,nowMs);
        HoldOffGround(j,pos,clear,kDt,nowMs);
        j.m.ready=true;
        Move(j,v);
        const float ps=-airbound::Depth(soft,pos),ph=-airbound::Depth(hard,pos);
        if(ps>o.pastSoft)o.pastSoft=ps;
        if(ph>o.pastHard)o.pastHard=ph;
        if(c.ground>0.0f)o.pastGround=std::fmax(o.pastGround,-airbound::Depth(groundBox,pos));
        if(pos[1]-softTop>o.overTop)o.overTop=pos[1]-softTop;
        if(pos[1]-ceil>o.overCeil)o.overCeil=pos[1]-ceil;
        o.endDepth=airbound::Depth(soft,pos);
    }
    // Fits, with room: never over the soft line (nor the soft ceiling) by more than kEdgeTol. Fits, without room (in
    // the band, or too near and too fast): never at the play edge, back inside at the end. A box that does not fit:
    // with room never at the play edge; without, only shown. How far over the ceiling a climb without room goes is shown.
    if(fits && room)o.ok=o.pastSoft<=kEdgeTol && o.overTop<=kEdgeTol;
    else if(fits)o.ok=o.pastHard<=0.0f && o.endDepth>=0.0f;
    else if(c.ground>0.0f)o.ok=o.pastGround<=0.0f;   // the ground cases: never out over the void
    else if(room)o.ok=o.pastHard<=0.0f;
    else o.judged=false;
    std::printf("%-11s edge %5.0f band %4.0f start %6.0f head %3.0f speed %3.0f %-6s | past soft %7.1f  past edge %7.1f  over soft top %7.1f"
                "  end %6.0f in | %s %s\n",kind.name,hard.hi[0],band,c.start,c.heading,s0,kChaseNames[static_cast<int>(c.chase)],
                o.pastSoft,o.pastHard,o.overTop,o.endDepth,room ? "room   " : "no room",!o.judged ? "shown" : o.ok ? "ok" : "FAIL");
    return o;
}

// The player and a hail use Hover too. At a legal position outside the NPC soft box, a player's outward input must
// remain outward, and a requested landing point must stay reachable. NPCs must still return to their inner box.
int PlayerRotorSuite() {
    config=Config{};groundHalf=1500.0f;
    int failures=0,cases=0;
    for(const Role role:{Role::carrier,Role::blast,Role::doll})for(const int axis:{0,2})for(const float lift:{0.0f,10.0f}) {
        const Kind& kind=KindOf(role);
        std::vector<unsigned char> mem(kBodySize,0),ctrl(kCtrlSize,0);
        Jet j{};j.role=role;
        const airbound::Box soft=JetSoftBox(j),hard=PlayBox();
        const int k=axis/2;
        const float start=0.5f*(soft.hi[k]+hard.hi[k]);
        float at[3]={0.0f,100.0f,0.0f},goal[3]={0.0f,100.0f,0.0f};
        at[axis]=start;goal[axis]=start+100.0f;
        const float direction[3]={0.0f,0.0f,1.0f};
        for(const bool npc:{false,true}) {
            Place(j,mem.data(),ctrl.data(),role,at,direction,0.0f,0.0f);
            float* pos=reinterpret_cast<float*>(mem.data()+kPosition);
            Hover(j,kind,mem.data(),pos,goal,goal,20.0f,kHoverClimb,kDt,lift,npc);
            const bool rightWay=npc ? j.m.vel[axis]<0.0f : j.m.vel[axis]>0.0f;
            for(int frame=0;frame<3600;++frame) {
                Hover(j,kind,mem.data(),pos,goal,goal,20.0f,kHoverClimb,kDt,lift,npc);
                for(int i=0;i<3;++i)pos[i]+=j.m.vel[i]*kDt;
            }
            const float expected=npc ? soft.hi[k] : goal[axis];
            const bool reached=npc ? pos[axis]<=expected+15.0f : std::fabs(pos[axis]-expected)<15.0f;
            const bool ok=rightWay && reached;
            failures+=!ok;++cases;
            std::printf("rotor %-7s %s axis %d lift %.0f: start %.0f, goal %.0f, reached %.1f (%s)\n",kind.name,
                        npc ? "NPC   " : "player",axis,lift,start,expected,pos[axis],ok ? "ok" : "FAIL");
        }
    }
    std::printf("player rotor suite: %d production Hover cases, %d failed\n",cases,failures);
    return failures ? 1 : 0;
}

// --ground-settle-suite (CTest jet_ground_settle): the production HoldOffGround with a player rotor craft's bottom
// clearance (playerjet FloorClear, `rest` its position over its bottom: the carrier's 8.516 m) standing on the ground,
// its bottom a few cm under a bump or over it, settling or still: never thrown up (the user, 2026-10-07: "the aircraft
// shake on the ground": it read its bottom under a bump as sunk and climbed at kFloorClimb). Its position truly under
// the surface still climbs out. (The wing on the ground: tools/ground_contact_check.cpp, pjet_handling.h GroundContact.)
int GroundSettleSuite() {
    config=Config{};
    int failures=0,cases=0;
    auto check=[&](bool ok,const char* what,float a,float b) {
        failures+=!ok;++cases;
        std::printf("%-58s %8.3f %8.3f (%s)\n",what,a,b,ok ? "ok" : "FAIL");
    };
    const float rest=8.516f;
    for(const float bottom:{-0.3f,-0.05f,0.0f,0.05f,0.3f})for(const float vy:{-1.0f,0.0f}) {
        Jet j{};
        j.m.vel[1]=vy;
        const float pos[3]={0.0f,rest+bottom,0.0f};
        HoldOffGround(j,pos,bottom,kDt,0,rest);
        char what[96];std::snprintf(what,sizeof(what),"standing: bottom %+.2f m, vy %+.1f -> not thrown up",bottom,vy);
        check(j.m.vel[1]<=0.0f,what,bottom,j.m.vel[1]);
    }
    {
        Jet j{};
        const float pos[3]={0.0f,-2.0f,0.0f};
        HoldOffGround(j,pos,-2.0f-rest,kDt,0,rest);
        check(j.m.vel[1]>=79.0f,"sunk: position 2 m under the surface -> climbs out",-2.0f,j.m.vel[1]);
    }
    {
        Jet j{};   // an NPC (rest 0, GroundClearance from its position): under the surface climbs, as before
        const float pos[3]={0.0f,-0.5f,0.0f};
        HoldOffGround(j,pos,-0.5f,kDt,0);
        check(j.m.vel[1]>=79.0f,"NPC: position 0.5 m under (rest 0) -> climbs out",-0.5f,j.m.vel[1]);
    }
    {
        // The look-ahead from its bottom (the user, 2026-10-09: the carrier let down onto what it met): a carrier's bottom
        // 5.8 m over flat ground, sliding 30 m/s on and sinking 10 m/s toward a block whose top (5.5 m) is 0.5 m ahead.
        // The ray from its position (14.3 m) saw nothing of it and left the sink as it was; from its bottom it meets the
        // block's side and stops the sink. An NPC (rest 0) is as before.
        boxes.push_back(Box{{0.5f,0.0f,-20.0f},{20.0f,5.5f,20.0f}});
        Jet j{};
        j.m.vel[0]=30.0f;j.m.vel[1]=-10.0f;
        const float pos[3]={0.0f,rest+5.8f,0.0f};
        HoldOffGround(j,pos,5.8f,kDt,0,rest);
        check(j.m.vel[1]>=0.0f,"carrier sinking toward a block ahead -> sink stopped",-10.0f,j.m.vel[1]);
        Jet n{};
        n.m.vel[0]=30.0f;n.m.vel[1]=-10.0f;
        const float npos[3]={0.0f,5.8f,0.0f};
        HoldOffGround(n,npos,5.8f,kDt,0);
        check(n.m.vel[1]>=0.0f,"NPC (rest 0) the same block -> sink stopped as before",-10.0f,n.m.vel[1]);
        boxes.pop_back();
    }
    {
        // The carrier's nacelles over the ground (nacelle_reach.h): far up they tilt all the way; standing on the ground
        // the front pair stops where its lowest point meets the bottom's plane, the back pair (never under 2.8 m) is free.
        const float high=nacelle::MostTilt(nacelle::kFront,-60.0f,-nacelle::kMostBack);
        check(high==-nacelle::kMostBack,"nacelles 60 m up -> tilt all the way",high,-nacelle::kMostBack);
        const float front=nacelle::MostTilt(nacelle::kFront,0.0f,-nacelle::kMostBack),back=nacelle::MostTilt(nacelle::kBack,0.0f,-nacelle::kMostBack);
        check(front<-0.5f && front>-0.62f && nacelle::Lowest(nacelle::kFront,front)>=0.0f && nacelle::Lowest(nacelle::kFront,front-0.02f)<0.0f,
              "on the ground -> front nacelles stop at the bottom's plane",front,nacelle::Lowest(nacelle::kFront,front));
        check(back==-nacelle::kMostBack,"on the ground -> back nacelles free",back,-nacelle::kMostBack);
        const float low=nacelle::MostTilt(nacelle::kFront,-3.0f,-nacelle::kMostBack);
        check(nacelle::Lowest(nacelle::kFront,low)>=-3.0f && low<front,"3 m up -> further, its lowest over the ground",low,
              nacelle::Lowest(nacelle::kFront,low));
        // Over the whole range: never a tilt whose nacelle reaches under the ground, for every height to 6 m.
        bool under=false;
        for(float c=0.0f;c<=6.0f;c+=0.25f) {
            const float most=nacelle::MostTilt(nacelle::kFront,-c,-nacelle::kMostBack);
            for(float at=0.0f;at>=most;at-=0.01f)under=under || nacelle::Lowest(nacelle::kFront,at)< -c-1e-4f;
        }
        check(!under,"0-6 m up -> no tilt on the way reaches under the ground",0.0f,under ? 1.0f : 0.0f);
    }
    {
        // The mouse-aim flight's height hold on a bounce (playerjet_board.inc HoverAim): the carrier let down on the ground
        // (grounded: nothing held), the solver throws it 4 m up at 30 m/s; the next frame it is off the ground and the hold
        // takes a height. From the flight's own velocity (sinking 1 m/s, what HoverAim passes now) it holds where it is;
        // from the measured one (the bounce's 30 m/s, what it passed) it held 15 m higher and climbed there.
        const float level[3]={0.0f,0.0f,1.0f},pos[3]={0.0f,rest+4.0f,0.0f};
        const float own[3]={0.0f,-1.0f,0.0f},measured[3]={0.0f,30.0f,0.0f};
        const aim::Keys none{0.0f,0.0f,0.0f};
        aim::Hold now{},was{};
        aim::Fly(now,level,level,pos,own,none,60.0f,12.0f,true,true,0.0f,kDt);
        aim::Fly(was,level,level,pos,measured,none,60.0f,12.0f,true,true,0.0f,kDt);
        const aim::Want a=aim::Fly(now,level,level,pos,own,none,60.0f,12.0f,false,false,4.0f,kDt);
        const aim::Want b=aim::Fly(was,level,level,pos,measured,none,60.0f,12.0f,false,false,4.0f,kDt);
        check(a.climb<=0.0f && now.y<=pos[1],"bounced off the ground -> holds where it is (own velocity)",now.y-pos[1],a.climb);
        check(b.climb>0.0f && was.y>pos[1]+10.0f,"...the measured bounce would hold it 15 m up (the old input)",was.y-pos[1],b.climb);
    }
    std::printf("ground settle suite: %d cases, %d failed\n",cases,failures);
    return failures ? 1 : 0;
}

int EdgeSuite() {
    static std::vector<unsigned char> fakeImage(0x20B2998+0x100,0);
    image=fakeImage.data();
    Put<unsigned char*>(image,0x20B2998,fakeArea);
    Put<float>(fakeArea,0x3C,1200.0f);   // the big map's ceiling as the 2026-10-06 log has it (ceil=1200)
    config.debug=true;
    logFile=std::fopen("edge_suite.log","w");
    const Role roles[]={Role::strike,Role::fighter,Role::interceptor};
    std::vector<EdgeCase> cases;
    for(const float world:{6000.0f,0.0f})
        for(const Role r:roles) {
            for(const float head:{0.0f,30.0f,60.0f,85.0f})
                for(const float share:{0.6f,1.0f})
                    for(const float start:{world>0.0f ? 3000.0f : 1400.0f,1500.0f,800.0f,150.0f})
                        cases.push_back(EdgeCase{r,world,start,head,share,Chase::out,40.0f});
            for(const float start:{1500.0f,400.0f})cases.push_back(EdgeCase{r,world,start,0.0f,1.0f,Chase::along,60.0f});
            for(const float head:{0.0f,20.0f,-20.0f})
                for(const float start:{world>0.0f ? 2500.0f : 1300.0f,1200.0f})
                    cases.push_back(EdgeCase{r,world,start,head,1.0f,Chase::corner,40.0f});
            for(const float start:{-200.0f,-600.0f})cases.push_back(EdgeCase{r,world,start,0.0f,1.0f,Chase::out,60.0f});
            cases.push_back(EdgeCase{r,world,-300.0f,0.0f,1.0f,Chase::along,60.0f});
            for(const float start:{600.0f,250.0f})cases.push_back(EdgeCase{r,world,start,0.0f,1.0f,Chase::climb,30.0f});
        }
    // A stock map on the big world (the user's: BigWorld 6000, the ground only some 1500 m a side, the void past it):
    // the walls on the ground's edge (playarea.h), the soft edge inside them; never out over the void.
    for(const Role r:roles) {
        for(const float head:{0.0f,30.0f,60.0f,85.0f})
            for(const float share:{0.7f,1.0f})
                for(const float start:{1200.0f,600.0f,300.0f,0.0f,-300.0f})
                    cases.push_back(EdgeCase{r,6000.0f,start,head,share,Chase::out,60.0f,1500.0f});
        for(const float start:{600.0f,0.0f})cases.push_back(EdgeCase{r,6000.0f,start,0.0f,1.0f,Chase::along,90.0f,1500.0f});
        for(const float head:{0.0f,20.0f,-20.0f})cases.push_back(EdgeCase{r,6000.0f,600.0f,head,1.0f,Chase::corner,60.0f,1500.0f});
    }
    // [fits][room]: cases, the most past the soft line, past the play edge, over the soft ceiling; the ground cases apart.
    int fails=0,count[2][2]{},grounds=0;
    float groundPast=-1e9f,groundSoft=-1e9f;
    float soft[2][2],edge[2][2],top[2][2];
    for(int a=0;a<2;++a)for(int b=0;b<2;++b)soft[a][b]=edge[a][b]=top[a][b]=-1e9f;
    for(const auto& c:cases) {
        const EdgeOut o=EdgeRun(c);
        fails+=!o.ok;
        if(c.ground>0.0f){++grounds;groundPast=std::fmax(groundPast,o.pastGround);groundSoft=std::fmax(groundSoft,o.pastSoft);continue;}
        const int f=o.fits,r=o.room;
        ++count[f][r];
        soft[f][r]=std::fmax(soft[f][r],o.pastSoft);edge[f][r]=std::fmax(edge[f][r],o.pastHard);top[f][r]=std::fmax(top[f][r],o.overTop);
    }
    for(int f=1;f>=0;--f)for(int r=1;r>=0;--r)
        if(count[f][r])std::printf("edge suite: %-26s %-24s %3d cases: at most %7.1f m past the soft line, %7.1f m past the play edge,"
                                   " %7.1f m over the soft ceiling\n",f ? "box holds its turns," : "box too small (stock map),",
                                   r ? "room for the turn back:" : "no room (band / too near):",count[f][r],soft[f][r],edge[f][r],top[f][r]);
    std::printf("edge suite: stock map's ground +-1500 m on the big world, %d cases: at most %.1f m past the soft line, %.1f m past"
                " the ground's edge (void: > 0)\n",grounds,groundSoft,groundPast);
    std::printf("edge suite: %d cases, %d failed (tolerance %.0f m)\n",static_cast<int>(cases.size()),fails,kEdgeTol);
    if(logFile)std::fclose(logFile);
    return fails ? 1 : 0;
}
}  // namespace

// --strike-room-suite (CTest jet_strike_room): ground attack runs on a stock-size map (walls +-1600 m: the ground
// +-1750, playarea.h's kVoidMargin inside it), where the soft box is some +-960 m. The 2026-10-09 log (a stock map,
// AREA walls -1600..1597): three called multirole jets arrived 600 m outside the soft line at (x,197,1567) and flew
// approach / dive / pull / extend for 4 minutes at ground targets 1100-1500 m away without one gun burst (guns 3600
// throughout, 481 extend lines, every line "back"). Each case: a loaded jet called in (Entering) at the log's arrival
// point and height, or already inside at its kind's height, at a ground target; it must fire its guns within
// kRoomFirstS and make at least kRoomPasses separate gun passes within kRoomSeconds, never past the play edge.
namespace {
constexpr float kRoomSeconds=120.0f,kRoomFirstS=45.0f;
constexpr int kRoomPasses=2;
struct RoomCase { Role role; float mass; float start[3]; float heading; bool launched; float target[3]; };
struct RoomOut { float first; int passes; float pastHard,fireS,lowest; };

RoomOut RoomRun(const RoomCase& c,bool print) {
    boxes.clear();
    config=Config{};config.debug=true;config.jetPilot=true;
    groundHalf=1750.0f;
    ResetWalls();
    const Kind& k=KindOf(c.role);
    std::vector<unsigned char> mem(kBodySize,0),ctrl(kCtrlSize,0);
    unsigned char* v=mem.data();
    Jet& j=jets[0];
    const float rad=c.heading*kPi/180.0f,dir[3]={std::sin(rad),0.0f,std::cos(rad)};
    nowMs=1000;
    Place(j,v,ctrl.data(),c.role,c.start,dir,k.cruise,0.0f);
    j.launched=c.launched;j.burden=Burden{c.mass,0.0f};
    static int targetDummy=0;
    j.t.target=&targetDummy;j.t.flyer=false;std::memcpy(j.t.aim,c.target,12);std::memcpy(j.t.tgtPrev,c.target,12);
    Arms arms{};arms.gunSpeed=960.0f;arms.gunRange=960.0f;arms.pick=-1;arms.rocket=-1;arms.guns=3600;arms.hasGun=true;
    const airbound::Box hard=PlayBox();
    const ULONGLONG born=nowMs;
    RoomOut o{-1.0f,0,-1e9f,0.0f,1e9f};
    bool firing=false;
    for(int f=0;f<static_cast<int>(kRoomSeconds*60.0f);++f) {
        nowMs+=16;
        const float* pos=reinterpret_cast<const float*>(v+kPosition);
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        float nose[3]={m[8],m[9],m[10]};
        if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
        j.seen=nowMs;
        Sense(j,pos,nowMs);
        const float clear=GroundClearance(pos);
        float want[3]={nose[0],0.0f,nose[2]},speed=k.cruise;
        bool gunsOk=false,missileOk=false;
        Attack(j,arms,pos,nose,c.target,c.target[1]+k.alt,nowMs,want,&speed,&gunsOk,&missileOk);
        Wing(j,k,v,pos,clear,nose,want,speed,kDt,nowMs);
        HoldOffGround(j,pos,clear,kDt,nowMs);
        j.m.ready=true;
        Fire(j,v,pos,nose,c.target,gunsOk,missileOk,arms,nowMs);
        const float t=static_cast<float>(nowMs-born)*0.001f;
        const bool gun=v[kFireGun]!=0;
        if(gun && o.first<0.0f)o.first=t;
        if(gun && !firing)++o.passes;
        if(gun)o.fireS+=kDt;
        if(clear!=kNoGround)o.lowest=std::fmin(o.lowest,clear);
        firing=gun || (firing && j.mode==Mode::dive);
        o.pastHard=std::fmax(o.pastHard,-airbound::Depth(hard,pos));
        if(f%60==0)Log("SIM %s at (%.0f,%.0f,%.0f) %.0f m/s heading (%.2f,%.2f) target %.0f m out, %.0f over",kModeNames[static_cast<int>(j.mode)],
                       pos[0],pos[1],pos[2],Len(j.m.vel),j.m.vel[0]/(Len(j.m.vel)+1e-3f),j.m.vel[2]/(Len(j.m.vel)+1e-3f),
                       HorizDist(pos,c.target),pos[1]-c.target[1]);
        Move(j,v);
    }
    const bool ok=o.first>=0.0f && o.first<=kRoomFirstS && o.passes>=kRoomPasses && o.pastHard<=0.0f;
    if(print)
        std::printf("room %-9s x%.2f %s from (%5.0f,%4.0f,%5.0f) heading %4.0f at (%5.0f,%3.0f,%5.0f): first burst %6.1f s, "
                    "%d passes, %4.1f s of fire, lowest %4.0f m, %+6.0f m past the play edge %s\n",k.name,static_cast<double>(c.mass),c.launched ? "called" : "inside",
                    static_cast<double>(c.start[0]),static_cast<double>(c.start[1]),static_cast<double>(c.start[2]),
                    static_cast<double>(c.heading),static_cast<double>(c.target[0]),static_cast<double>(c.target[1]),
                    static_cast<double>(c.target[2]),static_cast<double>(o.first),o.passes,static_cast<double>(o.fireS),static_cast<double>(o.lowest),static_cast<double>(o.pastHard),
                    ok ? "ok" : "FAIL");
    return o;
}

int StrikeRoomSuite(const char* outDir) {
    const std::string logPath=std::string(outDir)+"/strike_room.log";
    logFile=std::fopen(logPath.c_str(),"w");
    int bad=0,cases=0;
    float fireAll=0.0f;
    // The log's arrival (support_entry.h: the entry point on the soft side, 197 m = 173 over this flat ground) and a run in
    // at the kind's height from mid-map; targets where the log's were (z 450), mid-map, and off in a corner.
    const float targets[][3]={{40.0f,0.0f,450.0f},{0.0f,0.0f,0.0f},{-600.0f,0.0f,-500.0f},{700.0f,0.0f,650.0f}};
    const struct { Role role; float mass; } loads[]={{Role::multirole,1.18f},{Role::strike,1.21f},{Role::strike,1.0f}};
    for(const auto& l:loads)for(const auto& t:targets)for(int s=0;s<3;++s) {
        RoomCase c{l.role,l.mass,{-2.0f,173.0f,1567.0f},180.0f,true,{t[0],t[1],t[2]}};
        if(s==1){c.start[0]=600.0f;c.heading=200.0f;}
        if(s==2){c.start[0]=-800.0f;c.start[1]=KindOf(l.role).alt;c.start[2]=-200.0f;c.heading=90.0f;c.launched=false;}
        const RoomOut o=RoomRun(c,true);
        ++cases;fireAll+=o.fireS;
        if(!(o.first>=0.0f && o.first<=kRoomFirstS && o.passes>=kRoomPasses && o.pastHard<=0.0f))++bad;
    }
    if(logFile)std::fclose(logFile);
    std::printf("strike room: %d cases, %d failed, %.0f s of fire in all\n",cases,bad,static_cast<double>(fireAll));
    return bad ? 1 : 0;
}
}  // namespace

int main(int argc,char** argv) {
    const char* scenario="a";
    const char* kindName="strike";
    const char* outDir=".";
    std::string tag;
    float seconds=0.0f;
    bool selftest=false,playerRotors=false,strikeRoom=false;
    for(int a=1;a<argc;++a)if(!std::strcmp(argv[a],"--edge-suite"))return EdgeSuite();
    for(int a=1;a<argc;++a) {
        if(!std::strcmp(argv[a],"--scenario") && a+1<argc)scenario=argv[++a];
        else if(!std::strcmp(argv[a],"--kind") && a+1<argc)kindName=argv[++a];
        else if(!std::strcmp(argv[a],"--seconds") && a+1<argc)seconds=static_cast<float>(std::atof(argv[++a]));
        else if(!std::strcmp(argv[a],"--out") && a+1<argc)outDir=argv[++a];
        else if(!std::strcmp(argv[a],"--tag") && a+1<argc)tag=argv[++a];
        else if(!std::strcmp(argv[a],"--selftest"))selftest=true;
        else if(!std::strcmp(argv[a],"--ground-settle-suite"))return GroundSettleSuite();
        else if(!std::strcmp(argv[a],"--player-rotor-suite"))playerRotors=true;
        else if(!std::strcmp(argv[a],"--strike-room-suite"))strikeRoom=true;
        else {std::fprintf(stderr,"unknown argument %s\n",argv[a]);return 1;}
    }
    // The image: zeroed, so the ceiling's pointer (image+0x20B2998) reads null: no ceiling (CeilingY 1e9).
    static std::vector<unsigned char> fakeImage(0x20B2998+0x100,0);
    image=fakeImage.data();
    config.debug=true;
    if(playerRotors)return PlayerRotorSuite();
    if(selftest)return SelfTest(outDir);
    if(strikeRoom)return StrikeRoomSuite(outDir);
    if(tag.empty())tag=std::string(scenario)+"_"+kindName;
    const std::string csvPath=std::string(outDir)+"/"+tag+".csv",logPath=std::string(outDir)+"/"+tag+".log";
    logFile=std::fopen(logPath.c_str(),"w");
    std::FILE* csv=std::fopen(csvPath.c_str(),"w");
    if(!logFile || !csv){std::fprintf(stderr,"cannot write %s\n",csvPath.c_str());return 1;}

    const Role role=RoleNamed(kindName);
    const Kind& kind=KindOf(role);
    const std::string sc=scenario;
    // The world and where the jet starts (heading +z unless said).
    float start[3]={0.0f,60.0f,-1000.0f},goal[3]={0.0f,60.0f,3000.0f},anchor[3]={0.0f,0.0f,0.0f};
    float patrolHeight=kind.alt;
    bool strike=false;
    float aim[3]={0.0f,0.0f,0.0f};
    if(sc=="a")AddBox(0.0f,500.0f+30.0f,60.0f,60.0f,150.0f);
    else if(sc=="b" || sc=="b2" || sc=="b3" || sc=="b4") {
        const float face=sc=="b3" || sc=="b4" ? start[2]+600.0f : 500.0f;
        AddBox(0.0f,face+40.0f,80.0f,80.0f,400.0f);
        if(sc=="b2" || sc=="b4"){goal[2]=face+80.0f+600.0f;goal[1]=60.0f;}
    } else if(sc=="c" || sc=="c2") {
        // 5x5, 450 m apart, 80x80 m, 40-180 m tall (a fixed shuffle): the patrol circle (strike 1000 m, fighter
        // 1400 m round the anchor at the block's middle) crosses its outer rows.
        const float hs[25]={120,40,180,75,150, 60,165,95,140,45, 180,110,50,130,85, 70,155,100,40,175, 135,65,170,90,125};
        for(int r=0;r<5;++r)for(int c=0;c<5;++c)AddBox((c-2)*450.0f,(r-2)*450.0f,80.0f,80.0f,hs[r*5+c]);
        if(sc=="c2")patrolHeight=100.0f;
        start[0]=kind.patrol;start[1]=patrolHeight;start[2]=0.0f;   // on its circle, flying it (counterclockwise: -z)
    } else if(sc=="d" || sc=="d0") {
        strike=true;
        if(sc=="d")AddBox(0.0f,-180.0f,60.0f,60.0f,120.0f);
        start[0]=0.0f;start[1]=aim[1]+kind.alt;start[2]=-2200.0f;
    } else {std::fprintf(stderr,"unknown scenario %s\n",scenario);return 1;}
    if(seconds<=0.0f)seconds=sc=="c" || sc=="c2" ? 60.0f : sc[0]=='d' ? 25.0f : 15.0f;

    // The body.
    std::vector<unsigned char> mem(kBodySize,0),ctrl(kCtrlSize,0);
    unsigned char* v=mem.data();
    float heading[3]={0.0f,0.0f,1.0f};
    if(sc[0]=='c'){heading[0]=0.0f;heading[2]=-1.0f;}
    const float right[3]={heading[2],0.0f,-heading[0]};
    const float mat[16]={right[0],0,right[2],0, 0,1,0,0, heading[0],0,heading[2],0, start[0],start[1],start[2],1};
    std::memcpy(v+kMatrix,mat,64);
    std::memcpy(v+kPosition,start,12);
    Put<float>(v,kHpMax,1000.0f);Put<float>(v,kHp,1000.0f);
    Put<long>(ctrl.data(),8,1);
    Put<const void*>(v,kSelfCtrl,ctrl.data());
    Jet& j=jets[0];
    j=Jet{};
    j.ref=ObjRef{v,ctrl.data()};j.role=role;j.mode=Mode::patrol;j.drone.slot=-1;
    nowMs=1000;j.bornAt=j.modeAt=j.seen=nowMs;
    std::memcpy(j.anchor,anchor,12);
    const float s0=strike ? kind.cruise : sc[0]=='c' ? Patrol(j,start,anchor,patrolHeight,heading) : kind.cruise;
    if(sc[0]=='c'){heading[0]=0.0f;heading[1]=0.0f;heading[2]=-1.0f;}
    for(int i=0;i<3;++i)j.m.vel[i]=heading[i]*s0;
    static int targetDummy=0;
    if(strike){j.t.target=&targetDummy;j.t.flyer=false;std::memcpy(j.t.aim,aim,12);std::memcpy(j.t.tgtPrev,aim,12);}
    Arms arms{};arms.gunSpeed=1000.0f;arms.gunRange=1000.0f;arms.pick=-1;arms.rocket=-1;arms.guns=500;arms.hasGun=true;

    Log("SIM scenario %s kind %s: %d boxes, start (%.0f,%.0f,%.0f) at %.0f m/s",scenario,kind.name,static_cast<int>(boxes.size()),
        start[0],start[1],start[2],s0);
    for(const auto& b:boxes)Log("SIM box %.1f %.1f %.1f %.1f %.1f %.1f",b.lo[0],b.lo[1],b.lo[2],b.hi[0],b.hi[1],b.hi[2]);
    if(strike)Log("SIM target %.1f %.1f %.1f",aim[0],aim[1],aim[2]);
    std::fprintf(csv,"t,x,y,z,vx,vy,vz,speed,mode,obst,obstTop,obstSide,obstX,obstY,obstZ,held,boxGap,gunsOk\n");
    const int frames=static_cast<int>(seconds*60.0f);
    int entries=0,heldFrames=0,groundHits=0,flips=0,lastTurn=0;
    bool wasHeld=false;
    float minBox=1e9f,minGround=1e9f,maxClimb=-90.0f,minClimb=90.0f,prevHeading=std::atan2(heading[0],heading[2]);
    float diveAt=-1.0f,diveDh=0.0f,diveOver=0.0f;
    double clock=1.0;
    for(int f=0;f<frames;++f) {
        clock+=1.0/60.0;
        nowMs=static_cast<ULONGLONG>(clock*1000.0+0.5);
        const float t=static_cast<float>(f)*kDt;
        const float* pos=reinterpret_cast<const float*>(v+kPosition);
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        float nose[3]={m[8],m[9],m[10]};
        if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
        j.seen=nowMs;
        // jet.cpp JetFrame's order: Sense, the guidance, Wing (Ahead, Guard, JetSteer, Attitude), HoldOffGround.
        Sense(j,pos,nowMs);
        const float clear=GroundClearance(pos);
        float want[3]={nose[0],0.0f,nose[2]},speed=kind.cruise;
        bool gunsOk=false,missileOk=false;
        if(strike) {
            const Mode was=j.mode;
            Attack(j,arms,pos,nose,aim,aim[1]+kind.alt,nowMs,want,&speed,&gunsOk,&missileOk);
            if(j.mode==Mode::dive && was!=Mode::dive && diveAt<0.0f){diveAt=t;diveDh=HorizDist(pos,aim);diveOver=pos[1]-aim[1];}
        } else if(sc[0]=='c') {
            speed=Patrol(j,pos,anchor,patrolHeight,want);
        } else if(sc=="b2" || sc=="b4") {
            float to[3];Toward(pos,goal,to);
            Level(pos,to,goal[1],want);
        } else {
            Level(pos,heading,start[1],want);
        }
        Wing(j,kind,v,pos,GroundClearance(pos),nose,want,speed,kDt,nowMs);
        HoldOffGround(j,pos,clear,kDt,nowMs);
        j.m.ready=true;
        const bool held=Move(j,v);
        if(held && !wasHeld)++entries;
        if(held)++heldFrames;
        wasHeld=held;
        // Measures.
        float gap=1e9f;
        for(const auto& b:boxes){const float g=BoxGap(pos,b);if(g<gap)gap=g;}
        if(gap<minBox)minBox=gap;
        if(pos[1]<minGround)minGround=pos[1];
        if(pos[1]<=0.6f)++groundHits;
        const float hs=std::sqrt(j.m.vel[0]*j.m.vel[0]+j.m.vel[2]*j.m.vel[2]);
        const float climb=std::atan2(j.m.vel[1],hs)*180.0f/kPi;
        if(climb>maxClimb)maxClimb=climb;
        if(climb<minClimb)minClimb=climb;
        const float hd=std::atan2(j.m.vel[0],j.m.vel[2]);
        float dh=hd-prevHeading;
        if(dh>kPi)dh-=2.0f*kPi;
        if(dh<-kPi)dh+=2.0f*kPi;
        prevHeading=hd;
        const float rate=dh/kDt*180.0f/kPi;   // deg/s
        const int turn=rate>3.0f ? 1 : rate<-3.0f ? -1 : 0;
        if(turn && lastTurn && turn!=lastTurn)++flips;
        if(turn)lastTurn=turn;
        std::fprintf(csv,"%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%s,%d,%.1f,%d,%.1f,%.1f,%.1f,%d,%.1f,%d\n",t,pos[0],pos[1],pos[2],
                     j.m.vel[0],j.m.vel[1],j.m.vel[2],Len(j.m.vel),kModeNames[static_cast<int>(j.mode)],j.m.obstUntil ? 1 : 0,
                     j.m.obstTop,static_cast<int>(j.m.obstSide),j.m.obstAt[0],j.m.obstAt[1],j.m.obstAt[2],held ? 1 : 0,gap>1e8f ? -1.0f : gap,
                     gunsOk ? 1 : 0);
    }
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    std::printf("%s: %s %s %.0f s | box entries %d (%d frames held) | ground hits %d | min box gap %.1f m | min y %.1f m | "
                "climb max %.1f / min %.1f deg | turn flips %d | end (%.0f,%.0f,%.0f)",
                tag.c_str(),scenario,kind.name,seconds,entries,heldFrames,groundHits,minBox>1e8f ? -1.0f : minBox,minGround,maxClimb,minClimb,
                flips,pos[0],pos[1],pos[2]);
    if(strike)std::printf(" | dive %s",diveAt<0.0f ? "never" : "");
    if(strike && diveAt>=0.0f)std::printf("at %.2f s, %.0f m out, %.0f m over",diveAt,diveDh,diveOver);
    std::printf("\n");
    Log("SIM end: entries %d held %d ground %d minBox %.1f minY %.1f climb %.1f flips %d",entries,heldFrames,groundHits,minBox,minGround,
        maxClimb,flips);
    std::fclose(csv);
    std::fclose(logFile);
    return 0;
}
