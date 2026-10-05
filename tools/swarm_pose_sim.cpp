// Offline run of the Primer swarm drone's pose code (src/swarm_pose.h, the same header jet_swarm.cpp flies with):
// the local matrices the plugin would write into the model's bone records along a scripted sortie, for
// tools/swarm_pose_view.py to render with pylib/model_view.py. No game needed.
//
//   swarm_pose_sim T1 T2 ...  < binds.txt  > frames.txt
//
// binds.txt: one line per bone of swarm::kDroneBones, "name m0 .. m15" (its bind local, row-major, as the model
// has it). Output: per time asked, "frame <t> <state> curl=<0..1> fire=<0|1>", then "bone <name> m0 .. m15".
// The sortie (game time s): cruising until 2, its target in reach from 2 (it arms: the abdomen curls), shot down
// at 4.5 (a wreck). Stepped at 60 frames a second, as the game steps.
#include "../src/swarm_pose.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace {
struct Bind { char name[64]; float m[16]; bool found; };

swarm::DroneInput Scene(float t) {
    swarm::DroneInput in{};
    in.t=t;
    in.arm=t>=2.0f && t<4.5f;
    in.wreck=t>=4.5f;
    return in;
}
const char* StateOf(const swarm::DroneInput& in) { return in.wreck ? "wreck" : in.arm ? "armed" : "cruise"; }
}  // namespace

int main(int argc,char** argv) {
    Bind binds[swarm::kDroneBoneCount]{};
    for(int i=0;i<swarm::kDroneBoneCount;++i)std::snprintf(binds[i].name,sizeof(binds[i].name),"%ls",swarm::kDroneBones[i].name);
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
        for(auto& b:binds)if(std::strcmp(b.name,name)==0){std::memcpy(b.m,m,sizeof(m));b.found=true;}
    }
    for(const auto& b:binds)if(!b.found){std::fprintf(stderr,"no bind for %s\n",b.name);return 2;}
    float curl=0.0f,t=0.0f;
    const float dt=1.0f/60.0f;
    for(int a=1;a<argc;++a) {
        const float until=std::strtof(argv[a],nullptr);
        while(t+dt*0.5f<until){t+=dt;curl=swarm::CurlStep(curl,Scene(t),dt);}
        const swarm::DroneInput in=Scene(t);
        float angles[swarm::kDroneBoneCount];
        swarm::DroneAngles(in,curl,angles);
        std::printf("frame %.2f %s curl=%.2f fire=%d\n",t,StateOf(in),curl,in.arm && curl>=swarm::kCurlFire);
        for(int i=0;i<swarm::kDroneBoneCount;++i) {
            float out[16];
            swarm::TurnLocal(binds[i].m,swarm::kDroneBones[i].axis,angles[i],out);
            std::printf("bone %s",binds[i].name);
            for(float x:out)std::printf(" %.6f",x);
            std::printf("\n");
        }
    }
    return 0;
}
