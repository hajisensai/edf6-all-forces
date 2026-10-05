// Offline run of the Primer creatures' pose code (src/primer_pose.h, the same header primer.cpp flies with): the
// local matrices the plugin would write into the model's bone records along a scripted sortie, for
// tools/primer_pose_view.py to render with pylib/model_view.py. No game needed.
//
//   primer_pose_sim dragonfly|centipede T1 T2 ...  < binds.txt  > frames.txt
//   primer_pose_sim centipede-state SPEED FLYING HEAD TAIL BENDFRONT BENDREAR WRITHE T  < binds.txt
//     one moment in a given state (HEAD / TAIL: how much shows, 0 hidden .. 1 whole; WRITHE 0..1)
//     (tools/primer_chain_view.py: each link of a long one), stepped from 0 to T at that state
//
// binds.txt: one line per bone of the creature's pose table, "name m0 .. m15" (its bind local, row-major, as the
// model has it). Output: per time asked, "frame <t> <state>", then "bone <name> m0 .. m15" lines. Stepped at 60
// frames a second, as the game steps. The sorties (game time s):
//   dragonfly: cruising until 2, its target in reach from 2 (it arms: the abdomen curls, then it may fire);
//   centipede: crawling alone at 15 m/s until 3, linked into the middle of a longer one from 3 (head and tail
//     hidden), taking off with it at 4 (flying), the one ahead shot down at 5 (headless: it writhes while its head
//     grows back over kRegrowSec).
#include "../src/primer_pose.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace {
struct Bind { char name[64]; float m[16]; bool found; };

bool ReadBinds(Bind* binds,int n) {
    char line[1024];
    while(std::fgets(line,sizeof(line),stdin)) {
        char name[64];
        float m[16];
        int used=0;
        if(std::sscanf(line,"%63s%n",name,&used)!=1)continue;
        const char* p=line+used;
        int got=0;
        for(;got<16;++got) {
            char* end=nullptr;
            m[got]=std::strtof(p,&end);
            if(end==p)break;
            p=end;
        }
        if(got!=16)continue;
        for(int i=0;i<n;++i)if(std::strcmp(binds[i].name,name)==0){std::memcpy(binds[i].m,m,sizeof(m));binds[i].found=true;}
    }
    for(int i=0;i<n;++i)if(!binds[i].found){std::fprintf(stderr,"no bind for %s\n",binds[i].name);return false;}
    return true;
}

void Print(const Bind& b,primer::Axis axis,float angle,float scale) {
    float out[16];
    primer::TurnLocal(b.m,axis,angle,out,scale);
    std::printf("bone %s",b.name);
    for(float x:out)std::printf(" %.6f",x);
    std::printf("\n");
}

int Dragonfly(int argc,char** argv) {
    using namespace primer;
    Bind binds[kDragonflyBoneCount]{};
    for(int i=0;i<kDragonflyBoneCount;++i)std::snprintf(binds[i].name,sizeof(binds[i].name),"%ls",kDragonflyBones[i].name);
    if(!ReadBinds(binds,kDragonflyBoneCount))return 2;
    float curl=0.0f,t=0.0f;
    const float dt=1.0f/60.0f;
    for(int a=0;a<argc;++a) {
        const float until=std::strtof(argv[a],nullptr);
        while(t+dt*0.5f<until){t+=dt;curl=CurlStep(curl,DragonflyInput{t,t>=2.0f},dt);}
        const DragonflyInput in{t,t>=2.0f};
        float angles[kDragonflyBoneCount];
        DragonflyAngles(in,curl,angles);
        std::printf("frame %.2f %s curl=%.2f fire=%d\n",t,in.arm ? "armed" : "cruise",curl,in.arm && curl>=kCurlFire);
        for(int i=0;i<kDragonflyBoneCount;++i)Print(binds[i],kDragonflyBones[i].axis,angles[i],1.0f);
    }
    return 0;
}

// Crawling alone until 3, linked into a longer one from 3 (head and tail hidden), flying from 4; at 5 the one ahead
// of it is shot down: its head grows back over kRegrowSec while it writhes, its tail stays hidden (one behind it).
primer::CentipedeInput CentipedeScene(float t) {
    const float split=5.0f;
    const float head=t<3.0f ? 1.0f : t<split ? 0.0f : (t-split)/primer::kRegrowSec;
    const bool regrowing=t>=split && head<1.0f;
    return primer::CentipedeInput{t,15.0f,t>=4.0f,head>1.0f ? 1.0f : head,t<3.0f ? 1.0f : 0.0f,0.0f,0.0f,regrowing ? 1.0f-head : 0.0f};
}
const char* StateName(const primer::CentipedeInput& in) {
    return in.writhe>0.0f ? "headless, regrowing" : in.head<1.0f ? "linked" : in.flying ? "front, flying" : "alone";
}

int Centipede(int argc,char** argv) {
    using namespace primer;
    Bind binds[kCentipedeBoneCount]{};
    for(int i=0;i<kCentipedeBoneCount;++i)std::snprintf(binds[i].name,sizeof(binds[i].name),"%ls",kCentipedeBones[i].name);
    if(!ReadBinds(binds,kCentipedeBoneCount))return 2;
    float phase=0.0f,t=0.0f;
    const float dt=1.0f/60.0f;
    for(int a=0;a<argc;++a) {
        const float until=std::strtof(argv[a],nullptr);
        while(t+dt*0.5f<until){t+=dt;phase=CentipedeStep(phase,CentipedeScene(t),dt);}
        const CentipedeInput in=CentipedeScene(t);
        float angle[kCentipedeBoneCount],scale[kCentipedeBoneCount];
        CentipedeAngles(in,phase,angle,scale);
        std::printf("frame %.2f %s,%s head=%.2f\n",t,in.flying ? "flying" : "crawling",StateName(in),in.head);
        for(int i=0;i<kCentipedeBoneCount;++i)Print(binds[i],kCentipedeBones[i].axis,angle[i],scale[i]);
    }
    return 0;
}
}  // namespace

int CentipedeState(int argc,char** argv) {
    using namespace primer;
    if(argc!=8){std::fprintf(stderr,"centipede-state SPEED FLYING HEAD TAIL BENDFRONT BENDREAR WRITHE T\n");return 1;}
    const CentipedeInput base{0.0f,std::strtof(argv[0],nullptr),std::atoi(argv[1])!=0,std::strtof(argv[2],nullptr),
                              std::strtof(argv[3],nullptr),std::strtof(argv[4],nullptr),std::strtof(argv[5],nullptr),
                              std::strtof(argv[6],nullptr)};
    const float until=std::strtof(argv[7],nullptr);
    Bind binds[kCentipedeBoneCount]{};
    for(int i=0;i<kCentipedeBoneCount;++i)std::snprintf(binds[i].name,sizeof(binds[i].name),"%ls",kCentipedeBones[i].name);
    if(!ReadBinds(binds,kCentipedeBoneCount))return 2;
    float phase=0.0f,t=0.0f;
    const float dt=1.0f/60.0f;
    CentipedeInput in=base;
    while(t+dt*0.5f<until){t+=dt;in.t=t;phase=CentipedeStep(phase,in,dt);}
    in.t=t;
    float angle[kCentipedeBoneCount],scale[kCentipedeBoneCount];
    CentipedeAngles(in,phase,angle,scale);
    std::printf("frame %.2f state\n",t);
    for(int i=0;i<kCentipedeBoneCount;++i)Print(binds[i],kCentipedeBones[i].axis,angle[i],scale[i]);
    return 0;
}

int main(int argc,char** argv) {
    if(argc>=2 && std::strcmp(argv[1],"dragonfly")==0)return Dragonfly(argc-2,argv+2);
    if(argc>=2 && std::strcmp(argv[1],"centipede")==0)return Centipede(argc-2,argv+2);
    if(argc>=2 && std::strcmp(argv[1],"centipede-state")==0)return CentipedeState(argc-2,argv+2);
    std::fprintf(stderr,"usage: primer_pose_sim dragonfly|centipede T1 T2 ... < binds.txt\n");
    return 1;
}
