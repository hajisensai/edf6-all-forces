#include "../src/vulcan_rotor.h"
#include <cstdio>
#include <cstdlib>
namespace r=crew::vulcanrotor;
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
 r::Sample s;r::Reading w{reinterpret_cast<void*>(1),2000,0,100,100};
 Check(!r::Observe(s,w),"historical count on a new weapon establishes only a baseline");
 w.state=2;Check(!r::Observe(s,w),"reload/cooldown is not actual fire");
 w.state=1;w.ammo=0;s={};Check(!r::Observe(s,w),"held empty gun does not spin");
 w.ammo=2000;Check(r::Observe(s,w),"native fire flag1 with ammunition drives the motor");
 w.state=0;w.ammo=1999;Check(r::Observe(s,w),"offline actual ammunition decrease is fire without network counters");
 Check(!r::Observe(s,w),"an unchanged last-shot count cannot hold the motor forever");
 ++w.received;Check(r::Observe(s,w),"received remote shot pending replay drives the visual motor");
 w.weapon=reinterpret_cast<void*>(2);w.received=9000;Check(!r::Observe(s,w),"weapon replacement never replays its accumulated shot history");
 r::Motion m;for(unsigned i=1;i<=120;++i)r::Step(m,true,i*16,0.016f);
 Check(std::fabs(m.speed-r::kRunSpeed)<1e-4f,"sustained real fire reaches rotor cyclic speed");
 const float before=m.phase;r::Step(m,true,1936,1.0f/60.0f);const float delta=std::fmod(m.phase-before+r::kTau,r::kTau);
 Check(std::fabs(delta-r::kPi/3)>0.01f && delta>0.01f,"six-fold symmetry does not sample as a static 60-degree step");
 const float paused=m.phase;r::Step(m,false,1936,0);Check(m.phase==paused,"paused game clock does not advance phase");
 for(unsigned i=1;i<=60;++i)r::Step(m,false,1936+i*16,.016f);
 Check(m.speed==0,"motor coasts to a stop after actual fire ends");
 const float stopped=m.phase;r::Step(m,false,4000,.016f);Check(m.phase==stopped,"idle renderer does not invent barrel rotation");
 float bind[16]={1,0,0,0,0,1,0,0,0,0,1,0,-.195785f,0,-.63693f,1},out[16];r::Local(bind,.7f,out);
 Check(out[12]==bind[12]&&out[13]==bind[13]&&out[14]==bind[14]&&out[10]==1,"rotation preserves native barrel axis and pivot translation");
 const float cross=out[0]*out[5]-out[1]*out[4];Check(std::fabs(cross-1)<1e-5f,"rotor transform stays a unit nonmirrored rotation");
 r::Step(m,true,5000,1.0f);Check(m.speed==0,"a large scene-clock discontinuity creates no catch-up spin");
 float gb[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,.4f,-2,1};
 float hb[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,.7f,-3,1};
 float gp[16]={1,0,0,0,0,0,1,0,0,-1,0,0,0,.5f,-2,1},hp[16];
 Check(r::Follow(gb,hb,gp,hp),"same-parent half wheel accepts real unit bind transforms");
 Check(std::fabs(hp[13]-1.5f)<1e-5f&&std::fabs(hp[14]+1.7f)<1e-5f,"H rebase rotates around G pivot instead of old H centre");
 const float point[4]={.1f,-.3f,.8f,1};float drawn[4]{};
 for(int c=0;c<4;++c)for(int k=0;k<4;++k)drawn[c]+=point[k]*hp[k*4+c];
 Check(std::fabs(drawn[0]-.1f)<1e-5f&&std::fabs(drawn[1]-.7f)<1e-5f&&std::fabs(drawn[2]+2)<1e-5f,"inverse-H skinning produces exactly the original G wheel motion");
 gb[0]=2;Check(!r::Follow(gb,hb,gp,hp),"nonrigid model bind is rejected rather than inverted as a rotation");
 std::printf("%d M163 rotor checks passed\n",checks);
}
