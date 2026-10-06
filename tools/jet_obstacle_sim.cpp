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
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
bool VisitEnemies(const unsigned char*,EnemyVisitor,void*) noexcept { return false; }
bool FriendInLine(const float*,const float*,const void*) noexcept { return false; }
bool GunBarrel(const unsigned char*,const unsigned char*,float*,float*) noexcept { return false; }
int ReadStores(unsigned char*,Store*,int) noexcept { return 0; }
void TriggerStore(const Store&) noexcept {}
void NpcGear(unsigned char*,float,float) noexcept {}
void CarrierFlames(const unsigned char*,unsigned char* const*,float,ULONGLONG) noexcept {}
void JetFlames(const unsigned char*,float,bool,ULONGLONG) noexcept {}
void JetSmoke(const unsigned char*,bool,ULONGLONG) noexcept {}
bool JetBodyStep(unsigned char*,float*,float*) noexcept { return false; }
bool SubBodyStep(unsigned char*,float*,float*) noexcept { return false; }
bool PlayerJetBodyStep(unsigned char*,float*,float*) noexcept { return false; }
bool SubMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { return false; }
bool PlayerJetMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { return false; }
// No enemy creatures participate in the building-avoidance scenario.
bool PrimerMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { return false; }
namespace jet {
Jet jets[kMaxJets]{};
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
        Wing(j,k,v,pos,nose,want,k.attack,kDt,nowMs);
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

// The entry case: a called strike jet (loaded) 2600 m from its target and 150 m over it, the target `offDeg` off its
// nose, the game locking its missiles. When (s since it came) its guns first fire and its first missile goes (-1:
// never), and its height over the target at its first burst.
void EntryCase(float offDeg,float* gunAt,float* missileAt,float* overAtFire) {
    boxes.clear();
    const Role role=Role::strike;
    const Kind& k=KindOf(role);
    const float aim[3]={0.0f,0.0f,0.0f};
    const float off=offDeg*kPi/180.0f;
    const float at[3]={0.0f,150.0f,-2600.0f},dir[3]={std::sin(off),0.0f,std::cos(off)};
    std::vector<unsigned char> mem(kBodySize,0),ctrl(kCtrlSize,0);
    unsigned char* v=mem.data();
    Jet& j=jets[0];
    Place(j,v,ctrl.data(),role,at,dir,k.cruise,0.0f);
    j.launched=true;j.burden=Burden{1.21f,0.0f};
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
        Wing(j,k,v,pos,nose,want,speed,kDt,nowMs);
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
    const float offs[]={0.0f,15.0f,30.0f};
    for(float off:offs) {
        float gun=0.0f,msl=0.0f,over=0.0f;
        EntryCase(off,&gun,&msl,&over);
        const float entry=static_cast<float>(kEntryMs)*0.001f;
        const bool ok=gun>=0.0f && gun<entry && msl>=0.0f && msl<entry;
        if(!ok)++bad;
        std::printf("entry target %2.0f deg off the nose: guns at %.1f s (%.0f m over it), missile at %.1f s %s\n",
                    static_cast<double>(off),static_cast<double>(gun),static_cast<double>(over),static_cast<double>(msl),ok ? "ok" : "FAIL");
    }
    if(logFile)std::fclose(logFile);
    std::printf("%s\n",bad ? "SELFTEST FAILED" : "selftest passed");
    return bad ? 1 : 0;
}
}  // namespace

int main(int argc,char** argv) {
    const char* scenario="a";
    const char* kindName="strike";
    const char* outDir=".";
    std::string tag;
    float seconds=0.0f;
    bool selftest=false;
    for(int a=1;a<argc;++a) {
        if(!std::strcmp(argv[a],"--scenario") && a+1<argc)scenario=argv[++a];
        else if(!std::strcmp(argv[a],"--kind") && a+1<argc)kindName=argv[++a];
        else if(!std::strcmp(argv[a],"--seconds") && a+1<argc)seconds=static_cast<float>(std::atof(argv[++a]));
        else if(!std::strcmp(argv[a],"--out") && a+1<argc)outDir=argv[++a];
        else if(!std::strcmp(argv[a],"--tag") && a+1<argc)tag=argv[++a];
        else if(!std::strcmp(argv[a],"--selftest"))selftest=true;
        else {std::fprintf(stderr,"unknown argument %s\n",argv[a]);return 1;}
    }
    // The image: zeroed, so the ceiling's pointer (image+0x20B2998) reads null: no ceiling (CeilingY 1e9).
    static std::vector<unsigned char> fakeImage(0x20B2998+0x100,0);
    image=fakeImage.data();
    config.debug=true;
    if(selftest)return SelfTest(outDir);
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
        Wing(j,kind,v,pos,nose,want,speed,kDt,nowMs);
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
