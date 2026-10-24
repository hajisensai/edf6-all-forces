// The arrival's smoke (src/smoke_trail.h) offline: a jet arriving at the called jets' speeds (strike 215, fighter 235,
// interceptor 245 m/s), straight and turning, its trail laid the way the stock LineStripObject lays and draws it (a node
// at the emitter's position each frame, 0x2F6110 -> 0x2F4C10; every node drawn through the object's own matrix,
// 0x2F6310 / 0x2F583A). The smoke's newest node as drawn must sit on its nozzle (no further than the frame of flight the
// emitter's update may run behind the input step), and every node as drawn where the nozzle was when it was laid; the
// trail made with the nozzle's matrix as the object's (the bug, 2026-10-21) must be found off by far more.
// cmake --build build --target smoke_trail_check && build\smoke_trail_check.exe   (exit 1 on a failure)
#include "../src/smoke_trail.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
// A rotation (row vectors) of `yaw` about y at `at`: +z the nose.
void Pose(float yaw,const float* at,float* m) noexcept {
    const float c=std::cos(yaw),s=std::sin(yaw);
    const float r[16]={c,0,-s,0, 0,1,0,0, s,0,c,0, at[0],at[1],at[2],1};
    std::memcpy(m,r,sizeof r);
}

// p x m (row vectors).
void Apply(const float* p,const float* m,float* out) noexcept {
    for(int j=0;j<3;++j)out[j]=p[0]*m[j]+p[1]*m[4+j]+p[2]*m[8+j]+m[12+j];
}

float Dist(const float* a,const float* b) noexcept {
    return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}

struct Result { float head,worst; };

// 120 frames of an arrival from far off the origin (a called jet comes in from the map's edge), `made`: the object's
// matrix the trail is made with (null: the nozzle's world matrix as the trail begins, as before the fix).
Result Arrive(float speed,float yawRate,const float* made) noexcept {
    const float dt=1.0f/60.0f;
    // the nozzle in the body: the strike jet's right exit, its flame along the body's -z (the matrix's +z rearward)
    const float local[16]={-1,0,0,0, 0,1,0,0, 0,0,-1,0, 1.2f,-0.4f,-7.8f,1};
    float pos[3]={-1800.0f,420.0f,1500.0f},yaw=2.3f;
    float frame[16];
    bool begun=false;
    std::vector<float> nodes,where;   // each node as laid (world) and where its nozzle was then
    Result r{0,0};
    for(int f=0;f<120;++f) {
        float body[16],nozzle[16];
        Pose(yaw,pos,body);
        // nozzle = local x body
        for(int i=0;i<4;++i)for(int j=0;j<4;++j)nozzle[i*4+j]=local[i*4]*body[j]+local[i*4+1]*body[4+j]+local[i*4+2]*body[8+j]+local[i*4+3]*body[12+j];
        if(!begun){std::memcpy(frame,made ? made : nozzle,sizeof frame);begun=true;}
        float at[4],way[4];
        crew::smoke::Emitter(nozzle,at,way);
        if(std::fabs(way[0]-nozzle[8])>1e-6f || std::fabs(way[2]-nozzle[10])>1e-6f || at[3]!=1.0f || way[3]!=0.0f)return {1e9f,1e9f};
        nodes.insert(nodes.end(),at,at+3);   // the emitter's update: a node where it was put
        where.insert(where.end(),nozzle+12,nozzle+15);
        float drawn[3];
        crew::smoke::Drawn(frame,&nodes[nodes.size()-3],drawn);
        const float head=Dist(drawn,nozzle+12);
        r.head=head>r.head ? head : r.head;
        for(std::size_t n=0;n<nodes.size();n+=3) {
            crew::smoke::Drawn(frame,&nodes[n],drawn);
            const float off=Dist(drawn,&where[n]);
            r.worst=off>r.worst ? off : r.worst;
        }
        float fwd[3]={body[8],body[9],body[10]};
        for(int k=0;k<3;++k)pos[k]+=fwd[k]*speed*dt;
        yaw+=yawRate*dt;
    }
    return r;
}
}  // namespace

int main() {
    int bad=0;
    const float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for(int k=0;k<16;++k)
        if(crew::smoke::kObjectFrame[k]!=identity[k]){std::printf("FAIL kObjectFrame[%d]=%g: the trail's frame is not the identity\n",k,crew::smoke::kObjectFrame[k]);++bad;}
    const float speeds[]={215.0f,235.0f,245.0f},turns[]={0.0f,0.35f};
    for(float s:speeds)
        for(float t:turns) {
            const Result now=Arrive(s,t,crew::smoke::kObjectFrame);
            const Result was=Arrive(s,t,nullptr);
            // the head: on the nozzle (laid this frame); every node: where its nozzle was (the smoke stays where laid)
            const bool ok=now.head<0.01f && now.worst<0.01f && was.head>100.0f;
            std::printf("%s %.0f m/s turning %.2f rad/s: the smoke's head %.3f m from its nozzle, its nodes at most %.3f m from "
                        "where they were laid (with the nozzle's matrix as the object's: %.0f m / %.0f m)\n",ok ? "ok  " : "FAIL",s,t,
                        now.head,now.worst,was.head,was.worst);
            if(!ok)++bad;
        }
    std::printf(bad ? "%d failure(s)\n" : "all passed\n",bad);
    return bad ? 1 : 0;
}
