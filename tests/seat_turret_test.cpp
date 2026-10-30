// The gunner seats' own turrets off the game (src/seat_turret_pose.h, src/seat_turret_net.h): their aim, turn and pose,
// and the 'STUR' block a receiver admits.
#include "../src/seat_turret_net.h"
#include "../src/seat_turret_pose.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace st=crew::seat_turret;
namespace sn=crew::seat_turret_net;
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
bool Near(float a,float b){return std::fabs(a-b)<1e-4f;}
// Point `q` of a bone's local frame through its local `m` (row vectors, the translation the fourth row).
void Through(const float* q,const float* m,float* out){for(int c=0;c<3;++c)out[c]=q[0]*m[c]+q[1]*m[4+c]+q[2]*m[8+c]+m[12+c];}
int main(){
 const float ahead[3]={0,0,5},right[3]={3,0,0},up[3]={0,2,2};
 Check(Near(st::AnglesOf(ahead).yaw,0) && Near(st::AnglesOf(ahead).pitch,0),"ahead is the bind's aim");
 Check(Near(st::AnglesOf(right).yaw,st::kPi/2),"yaw turns toward +x");
 Check(Near(st::AnglesOf(up).pitch,st::kPi/4),"pitch lifts toward +y");

 const st::Limits l{-0.1f,1.2f,1.0f,1.0f,0.5f};
 st::Aim a=st::Limit({2.0f,-1.0f},l);
 Check(Near(a.yaw,1.0f) && Near(a.pitch,-0.1f),"a want past the traverse and the depression is held at the stops");
 st::Aim now{};
 Check(!st::Swing(now,{0.5f,0.5f},l,0.1f,0.01f),"a turn takes its time");
 Check(Near(now.yaw,0.1f) && Near(now.pitch,0.05f),"each axis at its own rate");
 for(int i=0;i<40;++i)st::Swing(now,{0.5f,0.5f},l,0.1f,0.01f);
 Check(st::Swing(now,{0.5f,0.5f},l,0.1f,0.01f) && Near(now.yaw,0.5f) && Near(now.pitch,0.5f),"it settles on the aim");
 const st::Limits round{-0.1f,1.2f,st::kPi,1.0f,1.0f};
 st::Aim back{3.0f,0.0f};
 st::Swing(back,{-3.0f,0.0f},round,0.1f,0.01f);
 Check(back.yaw>3.0f || back.yaw<-3.0f,"an all-round turret goes the short way across its back");
 st::Aim limited{0.9f,0.0f};
 st::Swing(limited,{-0.9f,0.0f},l,0.1f,0.01f);
 Check(Near(limited.yaw,0.8f),"a limited traverse never turns through its back");

 // Two leaves of one parent: the yaw bone at (1,0,0), the guns' at (1,1,1).
 const float bindYaw[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 1,0,0,1};
 const float bindGun[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 1,1,1,1};
 float yaw[16],gun[16];
 st::Pose(bindYaw,bindGun,{},yaw,gun);
 bool rest=true;
 for(int i=0;i<16;++i)rest=rest && yaw[i]==bindYaw[i] && gun[i]==bindGun[i];   // == : -0 is 0
 Check(rest,"the rest pose is the bind");
 st::Pose(bindYaw,bindGun,{st::kPi/2,0.0f},yaw,gun);
 float p[3];const float barrel[3]={0,0,1},origin[3]={0,0,0};
 Through(origin,gun,p);
 Check(Near(p[0],2) && Near(p[1],1) && Near(p[2],0),"the guns' pivot turns with the turret about the yaw bone");
 Through(barrel,gun,p);
 Check(Near(p[0],3) && Near(p[1],1) && Near(p[2],0),"turned a quarter right the barrel points +x");
 Through(origin,yaw,p);
 Check(Near(p[0],1) && Near(p[1],0) && Near(p[2],0),"the yaw bone turns in place");
 st::Pose(bindYaw,bindGun,{0.0f,st::kPi/4},yaw,gun);
 Through(barrel,gun,p);
 const float d[3]={p[0]-1,p[1]-1,p[2]-1};
 Check(Near(st::AnglesOf(d).pitch,st::kPi/4) && Near(st::AnglesOf(d).yaw,0),"lifted, the barrel climbs ahead");
 st::Pose(bindYaw,bindGun,{0.6f,0.3f},yaw,gun);
 Through(barrel,gun,p);
 const float e[3]={p[0]-gun[12],p[1]-gun[13],p[2]-gun[14]};
 Check(Near(st::AnglesOf(e).yaw,0.6f) && Near(st::AnglesOf(e).pitch,0.3f),"the pose points where its angles say");

 sn::State s{};
 s.head.sender=7;s.head.sequence=1;s.trained=1u<<2;s.holder[2]=4;s.yaw[2]=0.5f;s.pitch[2]=0.2f;
 Check(sn::Valid(s),"a trained turret's block");
 sn::State out{};
 Check(sn::Decode(&s,sizeof(s),out) && out.holder[2]==4 && !sn::Decode(&s,sizeof(s)-1,out),"decoded only whole");
 sn::State bad=s;bad.yaw[1]=0.1f;Check(!sn::Valid(bad),"an untrained entry is all zero");
 bad=s;bad.yaw[2]=NAN;Check(!sn::Valid(bad),"no NaN aim");
 bad=s;bad.holder[2]=-1;Check(!sn::Valid(bad),"its holder is one of the vehicle's");
 bad=s;bad.trained=0;Check(!sn::Valid(bad),"a block trains something");
 bad=s;bad.head.magic=0x4B435053u;Check(!sn::Valid(bad),"another kind's magic");
 std::printf("seat_turret: %d checks\n",checks);
 return 0;
}
