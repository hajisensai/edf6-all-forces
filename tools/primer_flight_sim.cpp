// The Primer creatures' flight and fight run without the game: src/primer.cpp and the jets' flight code it uses
// (src/jet_flight.cpp: Hover, HoldOffGround, Sense; src/body506.cpp: BodyAttitude, GroundClearance) compiled as
// they are, against a stand-in world: flat ground at y = 0, bodies that are blocks of zeroed memory with the
// fields the code reads and writes (matrix, position, HP, team, the fire bytes), moved each frame by the velocity
// and spin the code asks for (what the 506 physics hook does in the game, src/jet_hooks.cpp JetBodyStep), and a
// player on a script. The plugin's own trace (PrimerTrace) writes what happened to
// build/Mods/Plugins/EDF6VehicleCrew.primer.csv; tools/primer_trace_view.py draws it.
//
//   primer_flight_sim [--centipedes 6] [--dragonflies 2] [--seconds 90] [--player stand|walk|fly|land]
//                     [--kill SECONDS:ID ...] [--kill-middle SECONDS] [--jets N] [--shoot DPS] [--shoot-from SECONDS]
//                     [--binds FILE --frames FILE [--every 6]]
//
// --binds: lines "centipede|dragonfly <bone> m0 .. m15", the bind locals of the bones the plugin poses
// (src/primer_pose.h tables, from the models): each body then has a model instance (veh+0xEE0) holding records for
// them, which the plugin's Pose finds by name and writes as in the game. --frames: every --every frames, each live
// body's matrix and its bone records as the plugin left them ("body ID KIND m0..m15", "bone ID NAME m0..m15"), the
// player's position ("player x y z"): tools/primer_sim_video.py renders them.
//
// --kill shoots creature ID (its table index; centipedes first) down at that time (its dead byte: the code sees it
// as the game's dead body); --jets puts N friendly jets circling the player (the dragonfly's air prey). The player:
// --shoot DPS: the player fires at the nearest centipede within 300 m, DPS damage a second in hits of 10 frames,
// each hit going through the plugin's damage message (PrimerMessage: a headless one's wound multiplies it) and then
// off its HP as the stock handler does; at 0 HP it is down. The player:
// stand (at the origin), walk (a 60 m circle at 5 m/s), fly (a 300 m circle 80 m up at 40 m/s: flying prey), land
// (flies until half time, then stands on the ground).
#include "../src/jet_internal.h"
#include <cmath>
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
}  // namespace
const Config& Cfg() noexcept { return config; }
ULONGLONG GameMs() noexcept { return nowMs; }
ULONGLONG GameFrame() noexcept { return nowMs/16; }
void Log(const char* format,...) noexcept {
    if(!logFile)return;
    std::fprintf(logFile,"%8.2f ",static_cast<double>(nowMs)*0.001);
    va_list a;va_start(a,format);std::vfprintf(logFile,format,a);va_end(a);
    std::fputc('\n',logFile);
}
void SetObjectTeam(unsigned char* object,std::int32_t team) noexcept { Put<std::int32_t>(object,kTeam,team);Put<std::int32_t>(object,0x318,team); }
// Flat ground at y = 0, no buildings.
float MapRay(const float* a,const float* b,float* hit) noexcept {
    if((a[1]>0.0f)==(b[1]>0.0f))return -1.0f;
    const float t=a[1]/(a[1]-b[1]);
    for(int i=0;i<3;++i)hit[i]=a[i]+(b[i]-a[i])*t;
    return t;
}
// What the plugin's other files give the code compiled here, as the simulation needs it: the jets' table and an
// entry's identity (jet.cpp: the control block's use count, the deleted flag), flights (jet_spawn.cpp: numbers
// only, no bullets fly here), and nothing for the parts that are not simulated (other bodies' physics and
// messages, the carrier's flames).
namespace jet {
Jet jets[kMaxJets]{};
bool Alive(const ObjRef& r) noexcept {
    if(!r.obj || !r.ctrl || At<long>(r.ctrl,8)<=0)return false;
    const auto o=static_cast<const unsigned char*>(r.obj);
    return At<const void*>(o,kSelfCtrl)==r.ctrl && !(o[kObjFlags]&kObjDeleted);
}
Jet* FindJet(const unsigned char* v) noexcept {
    if(!v)return nullptr;
    const void* const ctrl=At<const void*>(v,kSelfCtrl);
    for(auto& j:jets)if(j.ref.obj==v && j.ref.ctrl==ctrl)return &j;
    return nullptr;
}
void JoinFlight(Jet& j,unsigned flight) noexcept { j.flight=flight; }
unsigned NewFlight() noexcept { static unsigned next=100;return next++; }
void Publish(bool) noexcept {}
void HoldRef(const ObjRef&) noexcept {}   // the bodies live as long as the run: no weak references to count
void DropRef(const ObjRef&) noexcept {}
}  // namespace jet
void CarrierFlames(const unsigned char*,unsigned char* const*,float,ULONGLONG) noexcept {}
void JetFlames(const unsigned char*,float,bool,ULONGLONG) noexcept {}
// No barrels in the stand-in world (the weapons' muzzles are the game's): nothing ever has its barrel on a line.
bool GunBarrel(const unsigned char*,const unsigned char*,float*,float*) noexcept { return false; }
bool JetBodyStep(unsigned char*,float*,float*) noexcept { return false; }
bool SubBodyStep(unsigned char*,float*,float*) noexcept { return false; }
bool PlayerJetBodyStep(unsigned char*,float*,float*) noexcept { return false; }
bool SubMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { return false; }
bool PlayerJetMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { return false; }
}  // namespace crew

using namespace crew;
using namespace crew::jet;

namespace {
constexpr std::size_t kBodySize=0x3000,kCtrlSize=0x40;
constexpr float kDt=1.0f/60.0f;

struct SimBody { std::vector<unsigned char> mem,ctrl,recs; bool dead; int bones; };
// The bones a body's records hold: the posed ones of its kind, their names (wide, for BoneRecord506) and bind locals.
struct SimBone { std::string kind,name; std::wstring wide; float bind[16]; };
std::vector<SimBone> simBones;
constexpr std::size_t kRecStride=0x110,kRecLocal=0x70,kInstCount=0x20;

unsigned char* Make(SimBody& b,Role role,const float* at,float hp,float heading) {
    b.mem.assign(kBodySize,0);b.ctrl.assign(kCtrlSize,0);b.dead=false;
    unsigned char* v=b.mem.data();
    const float c=std::cos(heading),s=std::sin(heading);
    const float m[16]={c,0,-s,0, 0,1,0,0, s,0,c,0, at[0],at[1],at[2],1};
    std::memcpy(v+kMatrix,m,64);
    std::memcpy(v+kPosition,at,12);
    Put<float>(v,kHpMax,hp);Put<float>(v,kHp,hp);
    Put<long>(b.ctrl.data(),8,1);   // a live control block (use count)
    // Its model instance's bone records (veh+0xEE0: the array at +0x10, the count at +0x20), as many as --binds
    // gave its kind: name at +0, local at +0x70, what BoneRecord506 and Pose read and write.
    const char* const kind=role==Role::centipede ? "centipede" : role==Role::dragonfly ? "dragonfly" : "";
    std::vector<const SimBone*> mine;
    for(const auto& sb:simBones)if(sb.kind==kind)mine.push_back(&sb);
    b.bones=static_cast<int>(mine.size());
    b.recs.assign(mine.size()*kRecStride+16,0);
    for(std::size_t i=0;i<mine.size();++i) {
        unsigned char* rec=b.recs.data()+i*kRecStride;
        Put<const wchar_t*>(rec,0,mine[i]->wide.c_str());
        std::memcpy(rec+kRecLocal,mine[i]->bind,64);
    }
    if(!mine.empty()) {
        Put<unsigned char*>(v+kModelInst506,kInstBones506,b.recs.data());
        Put<std::int32_t>(v+kModelInst506,kInstCount,b.bones);
    }
    Put<const void*>(v,kSelfCtrl,b.ctrl.data());
    // Its entry, as NewEntry makes one (the role its mark would name).
    for(auto& j:jets) {
        if(j.ref)continue;
        j=Jet{};
        j.ref=ObjRef{v,b.ctrl.data()};j.role=role;j.drone.slot=-1;j.bornAt=j.modeAt=j.seen=nowMs;
        std::memcpy(j.anchor,at,12);
        break;
    }
    return v;
}

// The 506 physics step's part: the body moves by the velocity and spin the code set (JetBodyStep), kept over the
// ground by its box (0.6 m: a centipede's belly).
void Move(Jet& j,unsigned char* v) {
    float* p=reinterpret_cast<float*>(v+kPosition);
    float* m=reinterpret_cast<float*>(v+kMatrix);
    for(int i=0;i<3;++i)p[i]+=j.m.vel[i]*kDt;
    if(p[1]<0.6f)p[1]=0.6f;
    // Each row turned by omega x row (small angle), then made orthonormal again.
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
}

void Player(const char* how,float t,float total) {
    float pos[3]={0,0,0};
    const bool flying=!std::strcmp(how,"fly") || (!std::strcmp(how,"land") && t<total*0.5f);
    if(flying){pos[0]=300.0f*std::cos(t*40.0f/300.0f);pos[1]=80.0f;pos[2]=300.0f*std::sin(t*40.0f/300.0f);}
    else if(!std::strcmp(how,"walk")){pos[0]=60.0f*std::cos(t*5.0f/60.0f);pos[2]=60.0f*std::sin(t*5.0f/60.0f);}
    std::memcpy(player.pos,pos,12);player.team=0;player.at=nowMs;
}
}  // namespace

int main(int argc,char** argv) {
    int centipedes=6,dragonflies=2,friendly=0;
    float seconds=90.0f;
    const char* how="stand";
    std::vector<std::pair<float,int>> kills;
    float dps=0.0f,shootFrom=0.0f,killMiddle=-1.0f;   // --kill-middle: the middle one of the longest chain then
    const char* framesPath=nullptr;
    int every=6;
    for(int a=1;a<argc;++a) {
        if(!std::strcmp(argv[a],"--centipedes") && a+1<argc)centipedes=std::atoi(argv[++a]);
        else if(!std::strcmp(argv[a],"--dragonflies") && a+1<argc)dragonflies=std::atoi(argv[++a]);
        else if(!std::strcmp(argv[a],"--jets") && a+1<argc)friendly=std::atoi(argv[++a]);
        else if(!std::strcmp(argv[a],"--seconds") && a+1<argc)seconds=static_cast<float>(std::atof(argv[++a]));
        else if(!std::strcmp(argv[a],"--player") && a+1<argc)how=argv[++a];
        else if(!std::strcmp(argv[a],"--shoot") && a+1<argc)dps=static_cast<float>(std::atof(argv[++a]));
        else if(!std::strcmp(argv[a],"--shoot-from") && a+1<argc)shootFrom=static_cast<float>(std::atof(argv[++a]));
        else if(!std::strcmp(argv[a],"--kill-middle") && a+1<argc)killMiddle=static_cast<float>(std::atof(argv[++a]));
        else if(!std::strcmp(argv[a],"--frames") && a+1<argc)framesPath=argv[++a];
        else if(!std::strcmp(argv[a],"--every") && a+1<argc)every=std::atoi(argv[++a]);
        else if(!std::strcmp(argv[a],"--binds") && a+1<argc) {
            std::FILE* bf=std::fopen(argv[++a],"r");
            if(!bf){std::fprintf(stderr,"cannot read %s\n",argv[a]);return 1;}
            char kind[32],name[64];
            SimBone sb{};
            while(std::fscanf(bf,"%31s %63s",kind,name)==2) {
                for(float& x:sb.bind)if(std::fscanf(bf,"%f",&x)!=1)x=0.0f;
                sb.kind=kind;sb.name=name;sb.wide=std::wstring(sb.name.begin(),sb.name.end());
                simBones.push_back(sb);
            }
            std::fclose(bf);
        }
        else if(!std::strcmp(argv[a],"--kill") && a+1<argc) {
            float t=0;int id=0;
            if(std::sscanf(argv[++a],"%f:%d",&t,&id)==2)kills.emplace_back(t,id);
        } else {std::fprintf(stderr,"unknown argument %s\n",argv[a]);return 1;}
    }
    config.debug=true;config.primerTrace=true;
    // The trace and the log next to the exe, under Mods/Plugins (as the plugin finds them by the exe's path).
    std::remove("Mods/Plugins/EDF6VehicleCrew.primer.csv");
    logFile=std::fopen("Mods/Plugins/primer_flight_sim.log","w");
    nowMs=1000;
    Player(how,0.0f,seconds);
    std::vector<SimBody> bodies(static_cast<std::size_t>(centipedes+dragonflies+friendly));
    std::vector<unsigned char*> vs;
    int k=0;
    for(int i=0;i<centipedes;++i,++k) {   // on a ring 120-260 m out, on the ground
        const float a=static_cast<float>(i)*2.4f,r=120.0f+static_cast<float>((i*53)%140);
        const float at[3]={r*std::cos(a),1.5f,r*std::sin(a)};
        vs.push_back(Make(bodies[k],Role::centipede,at,400.0f,a+3.14f));
    }
    for(int i=0;i<dragonflies;++i,++k) {  // 500 m out, 90 m up
        const float a=1.0f+static_cast<float>(i)*3.1f;
        const float at[3]={500.0f*std::cos(a),90.0f,500.0f*std::sin(a)};
        vs.push_back(Make(bodies[k],Role::dragonfly,at,600.0f,a+3.14f));
    }
    for(int i=0;i<friendly;++i,++k) {     // friendly jets: flown here on a circle, only their position and velocity matter
        const float at[3]={200.0f,120.0f,0.0f};
        vs.push_back(Make(bodies[k],Role::fighter,at,1000.0f,0.0f));
        Put<std::int32_t>(vs.back(),kTeam,2);
    }
    const int frames=static_cast<int>(seconds*60.0f);
    std::FILE* out=framesPath ? std::fopen(framesPath,"w") : nullptr;
    if(framesPath && !out){std::fprintf(stderr,"cannot write %s\n",framesPath);return 1;}
    for(int f=0;f<frames;++f) {
        nowMs+=16;
        const float t=static_cast<float>(f)*kDt;
        Player(how,t,seconds);
        for(auto& [kt,id]:kills)
            if(kt>=0.0f && t>=kt && id>=0 && id<static_cast<int>(vs.size())){vs[id][kDead]=1;bodies[id].dead=true;Log("SIM %d shot down",id);kt=-1.0f;}
        if(killMiddle>=0.0f && t>=killMiddle) {   // the middle one of the longest chain (by their links, as the plugin keeps them)
            killMiddle=-1.0f;
            std::vector<int> best;
            for(int i=0;i<static_cast<int>(vs.size());++i) {
                if(bodies[i].dead || jets[i].role!=Role::centipede || jets[i].primer.ahead)continue;
                std::vector<int> chain{i};
                for(const void* b=jets[i].primer.behind;b && chain.size()<64;) {
                    int next=-1;
                    for(int k2=0;k2<static_cast<int>(vs.size());++k2)if(jets[k2].ref.ctrl==b)next=k2;
                    if(next<0)break;
                    chain.push_back(next);b=jets[next].primer.behind;
                }
                if(chain.size()>best.size())best=chain;
            }
            if(best.size()>=3) {
                const int id=best[best.size()/2];
                vs[id][kDead]=1;bodies[id].dead=true;
                Log("SIM %d shot down: the middle of a chain of %d",id,static_cast<int>(best.size()));
            } else Log("SIM no chain of 3 or more to split");
        }
        if(dps>0.0f && t>=shootFrom && f%10==0) {   // a hit on the nearest centipede in reach
            int best=-1;
            float bestD=300.0f;
            for(int i=0;i<static_cast<int>(vs.size());++i) {
                if(bodies[i].dead || jets[i].role!=Role::centipede)continue;
                const float* p=reinterpret_cast<const float*>(vs[i]+kPosition);
                const float d[3]={p[0]-player.pos[0],p[1]-player.pos[1],p[2]-player.pos[2]};
                if(Len(d)<bestD){bestD=Len(d);best=i;}
            }
            if(best>=0) {
                alignas(16) unsigned char gdi[0xA0]{};
                Put<float>(gdi,0x50,dps/6.0f);
                MessageRestore restore{nullptr,0.0f};
                PrimerMessage(vs[best],kMsgDamage,gdi,&restore);   // the plugin's say first, as body506's hook gives it
                const float hit=At<float>(gdi,0x50);
                if(restore.at)*restore.at=restore.was;
                float hp=At<float>(vs[best],kHp)-hit;
                if(hit>dps/6.0f+0.01f)Log("SIM hit %d for %.0f (x%.1f: a wound)",best,hit,hit/(dps/6.0f));
                if(hp<=0.0f){hp=0.0f;vs[best][kDead]=1;bodies[best].dead=true;Log("SIM %d shot down",best);}
                Put<float>(vs[best],kHp,hp);
            }
        }
        for(int i=0;i<static_cast<int>(vs.size());++i) {
            Jet& j=jets[i];
            unsigned char* v=vs[i];
            if(bodies[i].dead)continue;
            const float* pos=reinterpret_cast<const float*>(v+kPosition);
            j.seen=nowMs;
            if(IsPrimer(j))PrimerFrame(j,v,pos,kDt,nowMs);
            else {   // a friendly jet: a 250 m circle 120 m up round the player at 150 m/s
                const float a=t*150.0f/250.0f+static_cast<float>(i);
                const float want[3]={player.pos[0]+250.0f*std::cos(a),120.0f,player.pos[2]+250.0f*std::sin(a)};
                for(int c=0;c<3;++c)j.m.vel[c]=(want[c]-pos[c])*2.0f;
            }
            Move(j,v);
        }
        if(out && f%(every>0 ? every : 1)==0) {
            std::fprintf(out,"frame %.3f\nplayer %.2f %.2f %.2f\n",t,player.pos[0],player.pos[1],player.pos[2]);
            for(int i=0;i<static_cast<int>(vs.size());++i) {
                if(bodies[i].dead)continue;
                const float* m=reinterpret_cast<const float*>(vs[i]+kMatrix);
                std::fprintf(out,"body %d %s",i,jets[i].role==Role::centipede ? "centipede" : jets[i].role==Role::dragonfly ? "dragonfly" : "jet");
                for(int c=0;c<16;++c)std::fprintf(out," %.4f",m[c]);
                std::fprintf(out,"\n");
                for(int b=0;b<bodies[i].bones;++b) {
                    const unsigned char* rec=bodies[i].recs.data()+b*kRecStride;
                    std::fprintf(out,"bone %d %ls",i,At<const wchar_t*>(rec,0));
                    const float* l=reinterpret_cast<const float*>(rec+kRecLocal);
                    for(int c=0;c<16;++c)std::fprintf(out," %.5f",l[c]);
                    std::fprintf(out,"\n");
                }
            }
        }
    }
    if(out)std::fclose(out);
    if(logFile)std::fclose(logFile);
    std::printf("simulated %.0f s: %d centipedes, %d dragonflies, %d friendly jets, player %s\n",seconds,centipedes,dragonflies,friendly,how);
    return 0;
}
