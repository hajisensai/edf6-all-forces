// The paratroop plane's passes along one line (src/ferry_line.h), with a point mass that turns at its radius: it follows
// the line (not the point), crosses the drop point on it, turns round past each end the same way, and with its stick out
// is gone past the end of that pass. The production flight (Wing, Guard, the soft edge) is jet_obstacle_sim's --selftest.
#include "../src/ferry_line.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
using namespace crew;
int checks=0;
void check(bool ok,const char* why,double a=0,double b=0){++checks;if(!ok){std::fprintf(stderr,"FAIL %s (%g, %g)\n",why,a,b);std::exit(1);}}

struct Flight { float pos[3],vel[3]; };
// One step of a point mass at `speed` that turns toward `way` at most speed / radius rad/s.
void Fly(Flight& f,const float* way,float speed,float radius,float dt) {
    const float a=std::atan2(f.vel[2],f.vel[0]),b=std::atan2(way[2],way[0]);
    float d=b-a;while(d>3.14159265f)d-=6.2831853f;while(d< -3.14159265f)d+=6.2831853f;
    const float most=speed/radius*dt,turn=d>most ? most : d< -most ? -most : d;
    f.vel[0]=std::cos(a+turn)*speed;f.vel[2]=std::sin(a+turn)*speed;
    for(int i=0;i<3;++i)f.pos[i]+=f.vel[i]*dt;
}
}  // namespace

int main() {
    const support::PassLine line{{100,0,-50},{1,0,0},-1900,1800};
    const float speed=98,radius=266,dt=1.0f/60;
    for(float offset:{0.0f,250.0f,-400.0f}) {
        Flight f{{-2300,150,-50+offset},{speed,0,0}};
        ferry::Pass pass;
        int crossings=0,reversals=0,lastDir=pass.dir;float worst=0,mostOut=0;
        bool ahead=true,gone=false;
        for(int i=0;i<60*400 && !gone;++i) {
            float way[3];
            gone=ferry::Steer(line,pass,f.pos,f.vel,ferry::Lead(radius),way);
            if(gone)break;
            check(std::fabs(way[1])<1e-6f && std::fabs(way[0]*way[0]+way[2]*way[2]-1)<1e-3f,"a level unit way");
            if(pass.dir!=lastDir){++reversals;lastDir=pass.dir;}
            Fly(f,way,speed,radius,dt);
            float along,across;ferry::Along(line,f.pos,&along,&across);
            const bool nowAhead=along<0 ? pass.dir>0 : pass.dir<0;
            if(ahead && !nowAhead && std::fabs(along)<200) {
                ++crossings;
                if(std::fabs(across)>worst)worst=std::fabs(across);
                if(crossings==3)pass.done=true;
            }
            ahead=nowAhead;
            const float out=pass.dir>0 ? along-line.hi : line.lo-along;
            if(out>mostOut)mostOut=out;
        }
        check(gone,"its last pass over: gone",offset);
        check(crossings==3 && reversals==2,"three passes over the point, turning round past each end between",crossings,reversals);
        check(worst<10,"each pass crosses the point on the line, not by it",worst,offset);
        check(mostOut<support::kTurnRoom*radius,"round within the room the line's ends keep",mostOut);
        float along,across;ferry::Along(line,f.pos,&along,&across);
        check(along>=line.hi || along<=line.lo,"gone past the end of its pass, off the map",along);
    }
    // Turning round: always the same way (its right), never the shortest arc's coin toss each frame.
    {
        const support::PassLine l{{0,0,0},{1,0,0},-1000,1000};
        ferry::Pass p;p.dir=-1;
        const float pos[3]={1100,150,0},vel[3]={98,0,0};float way[3];
        check(!ferry::Steer(l,p,pos,vel,300,way),"past the end, not done: on");
        check(std::fabs(way[0]+vel[2]/98)<1e-6f && std::fabs(way[2]-vel[0]/98)<1e-6f,"the aim behind it: square to its right",way[0],way[2]);
        const float nudged[3]={98,0,0.5f};float way2[3];
        ferry::Steer(l,p,pos,nudged,300,way2);
        check(way[2]*way2[2]>0,"the same side the next frame");
        p.done=true;p.dir=1;
        check(ferry::Steer(l,p,pos,vel,300,way),"past the end with its stick out: gone");
    }
    std::printf("ferry_line_test: %d checks passed\n",checks);
}
