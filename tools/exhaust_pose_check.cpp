// The exhausts' frame (src/exhaust_pose.h) offline: a jet flown frame by frame in the order the game runs them (the
// input step, where the plugin places the flames, reads the bone records the game posed the frame before; the game
// then poses and draws the body where it is now; the physics step moves it on: docs/jet-model-re.md, the muzzle
// lag measured in JetLog), the flame's root against its nozzle on the body as drawn, with the record as it is (before)
// and carried to this frame (after), at 100 / 300 / 600 km/h and the arrival speeds (strike 215, fighter 235,
// interceptor 245 m/s), flying straight and turning (20 deg/s, banked 60 deg, pitching 5 deg/s).
// cmake --build build --target exhaust_pose_check && build\exhaust_pose_check.exe   (exit 1 on a failure)
#include "../src/exhaust_pose.h"
#include <cmath>
#include <cstdio>

namespace {
using crew::exhaust::BodyTrack;

// A rotation (row vectors) of `yaw` about y, then `pitch` about x, `bank` about z (rad), at `at`.
void Pose(float yaw,float pitch,float bank,const float* at,float* m) noexcept {
    const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch),cb=std::cos(bank),sb=std::sin(bank);
    const float rz[16]={cb,sb,0,0, -sb,cb,0,0, 0,0,1,0, 0,0,0,1};
    const float rx[16]={1,0,0,0, 0,cp,sp,0, 0,-sp,cp,0, 0,0,0,1};
    const float ry[16]={cy,0,-sy,0, 0,1,0,0, sy,0,cy,0, 0,0,0,1};
    float a[16];
    crew::exhaust::Mul(rz,rx,a);
    crew::exhaust::Mul(a,ry,m);
    m[12]=at[0];m[13]=at[1];m[14]=at[2];
}

void Apply(const float* p,const float* m,float* out) noexcept {
    for(int j=0;j<3;++j)out[j]=p[0]*m[j]+p[1]*m[4+j]+p[2]*m[8+j]+m[12+j];
}

struct Result { float before,after; };

// The largest distance over 600 frames between the flame's root and its nozzle as drawn.
Result Fly(float speed,float yawRate,float bank,float pitchRate) noexcept {
    // the mesh bone in the vehicle's frame (the player fighter's, FLAME log): 1.385 m under, 1.69 m behind its origin
    const float local[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0.0f,-1.385f,-1.69f,1};
    const float nozzle[3]={2.327f,1.511f,-7.804f};   // the interceptor's right exit, in the mesh bone's frame
    const float dt=1.0f/60.0f;
    float pos[3]={0,500,0},yaw=0,pitch=0;
    float record[16];
    bool posed=false;
    BodyTrack track;
    Result r{0,0};
    for(int frame=1;frame<=600;++frame) {
        float body[16];
        Pose(yaw,pitch,bank,pos,body);
        crew::exhaust::Observe(track,static_cast<std::uint64_t>(frame),body);
        if(posed) {   // the input step: the record is the frame before's pose
            float drawn[16],carried[16],want[3],old[3],now[3];
            crew::exhaust::Mul(local,body,drawn);         // where the game draws the mesh bone this frame
            crew::exhaust::Carry(record,track,carried);
            Apply(nozzle,drawn,want);
            Apply(nozzle,record,old);
            Apply(nozzle,carried,now);
            const float b=std::sqrt((old[0]-want[0])*(old[0]-want[0])+(old[1]-want[1])*(old[1]-want[1])+(old[2]-want[2])*(old[2]-want[2]));
            const float a=std::sqrt((now[0]-want[0])*(now[0]-want[0])+(now[1]-want[1])*(now[1]-want[1])+(now[2]-want[2])*(now[2]-want[2]));
            r.before=b>r.before ? b : r.before;
            r.after=a>r.after ? a : r.after;
        }
        crew::exhaust::Mul(local,body,record);   // the game poses the body where it is (the next input step reads it)
        posed=true;
        const float fwd[3]={body[8],body[9],body[10]};
        for(int c=0;c<3;++c)pos[c]+=fwd[c]*speed*dt;   // the physics step
        yaw+=yawRate*dt;pitch+=pitchRate*dt;
    }
    return r;
}
}  // namespace

int main() {
    int bad=0;
    const float kDeg=3.14159265f/180.0f;
    const struct { const char* name; float speed; } cases[]={{"100 km/h",100.0f/3.6f},{"300 km/h",300.0f/3.6f},
        {"600 km/h",600.0f/3.6f},{"arrival strike 215 m/s",215.0f},{"arrival fighter 235 m/s",235.0f},
        {"arrival interceptor 245 m/s",245.0f}};
    for(const auto& c:cases) {
        const Result straight=Fly(c.speed,0.0f,0.0f,0.0f);
        const Result turning=Fly(c.speed,20.0f*kDeg,60.0f*kDeg,5.0f*kDeg);
        std::printf("%-28s flame root from its nozzle: straight %.3f m -> %.4f m, turning %.3f m -> %.4f m (one frame: %.3f m)\n",
                    c.name,straight.before,straight.after,turning.before,turning.after,c.speed/60.0f);
        if(straight.after>0.005f || turning.after>0.005f){std::printf("  FAIL: the flame is not on its nozzle\n");++bad;}
        if(std::fabs(straight.before-c.speed/60.0f)>0.01f){std::printf("  FAIL: the model's lag is not one frame\n");++bad;}
    }
    // The frames: none before (the first), a skipped one (no carry: the record as it is), one call a frame.
    BodyTrack t;
    const float a[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1},b[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,5,1};
    float out[16];
    crew::exhaust::Observe(t,1,a);
    crew::exhaust::Carry(a,t,out);
    if(t.posedOk || out[14]!=0.0f){std::printf("FAIL: carried with no frame before\n");++bad;}
    crew::exhaust::Observe(t,2,b);
    crew::exhaust::Observe(t,2,a);   // a second call in the same frame changes nothing
    crew::exhaust::Carry(a,t,out);
    if(!t.posedOk || std::fabs(out[14]-5.0f)>1e-5f){std::printf("FAIL: not carried by the frame's motion (%.3f)\n",out[14]);++bad;}
    crew::exhaust::Observe(t,4,a);   // frame 3 skipped
    crew::exhaust::Carry(b,t,out);
    if(t.posedOk || std::fabs(out[14]-5.0f)>1e-5f){std::printf("FAIL: carried over a skipped frame\n");++bad;}
    std::printf(bad ? "%d FAILED\n" : "all passed\n",bad);
    return bad ? 1 : 0;
}
