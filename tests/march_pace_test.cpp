// The paced march (src/march_pace.h): the slowest unit sets the group's pace, the others wait for it, all arrive
// together; a stuck unit stops holding the group up.
#include "../src/march_pace.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* text) {++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
}
int main() {
    using namespace pace;
    // Three units 300 m from their points: a soldier at 5 m/s, a tank at 12 m/s, a heli at 25 m/s, simulated at 10 Hz.
    Member m[3];const float speed[3]={5.0f,12.0f,25.0f};float left[3]={300.0f,300.0f,300.0f};
    std::uint64_t now=1000;
    for(int i=0;i<3;++i)Begin(m[i],left[i],now);
    float most=0.0f;int arrivedTogether=-1;
    for(int step=0;step<1000;++step) {
        float s[3];Scales(m,3,now,s);
        Check(s[0]==1.0f,"the slowest unit always goes at its own full speed");
        for(int i=0;i<3;++i){left[i]=std::fmax(0.0f,left[i]-speed[i]*s[i]*0.1f);Update(m[i],left[i],now);}
        const float gap=std::fmax(std::fabs(left[1]-left[0]),std::fabs(left[2]-left[0]));
        if(gap>most)most=gap;
        now+=100;
        if(left[0]==0.0f){arrivedTogether=left[1]<=40.0f && left[2]<=40.0f ? 1 : 0;break;}
    }
    Check(most<=2.0f*kSlackLeast+25.0f*0.1f+0.01f,"the fast units never get more than twice the slack ahead");
    Check(arrivedTogether==1,"they arrive together, at the soldier's pace");
    // No pacing with fewer than two units on their way.
    Member one[1];Begin(one[0],100.0f,0);float s1[1];Scales(one,1,0,s1);Check(s1[0]==1.0f,"alone: full speed");
    // A unit stuck (no 2 m gained for 8 s) is no longer waited for: the one far ahead goes on.
    Member st[2];Begin(st[0],300.0f,0);Begin(st[1],300.0f,0);Update(st[1],100.0f,0);
    float s2[2];Scales(st,2,1000,s2);Check(s2[1]==0.0f,"far ahead of a moving slowest: held");
    Scales(st,2,kStuckMs+1001,s2);Check(s2[1]==1.0f,"the slowest stuck: the others go on");
    // Different starting distances arrive together (progress is each one's own share of its way).
    Member d[2];Begin(d[0],400.0f,0);Begin(d[1],100.0f,0);Update(d[0],200.0f,0);Update(d[1],50.0f,0);
    float s3[2];Scales(d,2,0,s3);Check(s3[0]==1.0f && s3[1]==1.0f,"half of either way: both at full speed");
    std::printf("march_pace_test: %d checks passed\n",checks);
    return 0;
}
