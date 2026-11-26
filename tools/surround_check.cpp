// Where a sound goes among the speakers, offline (src/surround.h, jetaudio.cpp's output matrices): every layout the
// mastering voice can report (stereo, 2.1, quad, 5.1, 7.1, a device naming no mask, mono) keeps a point source's power
// whatever its direction; a sound ahead is in the front speakers, one behind in the back ones, one to the right on the
// right; a spread sound is all around at the same power; the LFE channel gets the low layers alone and nothing else, and
// with no LFE channel they are not dropped; a stereo recording's two channels are two sources the same power together
// as a mono one, apart by their spread; a bad gain or direction plays nothing or plays ahead, never NaN.
//
//   cmake --build build --target surround_check && build\surround_check.exe
#include "../src/surround.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {
using namespace crew::surround;
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}
bool Near(float a,float b,float eps=1e-4f) { return std::fabs(a-b)<=eps; }

float Power(const float* m,int n) {
    float p=0.0f;
    for(int i=0;i<n;++i)p+=m[i]*m[i];
    return p;
}
// A mono source's level on the channel of speaker bit `bit` of `mask`.
float On(std::uint32_t mask,const Layout& l,const float* m,std::uint32_t bit,int inputs=1,int input=0) {
    int channel=0;
    for(int b=0;b<32;++b) {
        const std::uint32_t s=1u<<b;
        if(!(mask&s))continue;
        if(s==bit)return channel<l.channels ? m[input+inputs*channel] : 0.0f;
        ++channel;
    }
    return 0.0f;
}
float Dir(float deg,float* side,float* front) {
    const float r=deg*kPi/180.0f;
    *side=std::sin(r);*front=std::cos(r);
    return deg;
}

void Layouts() {
    const Layout st=LayoutOf(kStereo,2);
    Check(st.channels==2 && st.ring==2 && st.lfe<0,"stereo: two speakers around, no LFE");
    const Layout s21=LayoutOf(kStereo|kLowFrequency,3);
    Check(s21.ring==2 && s21.lfe==2,"2.1: the LFE is the third channel, not one around the listener");
    const Layout q=LayoutOf(kQuad,4);
    Check(q.ring==4 && q.lfe<0 && Near(q.ringAngle[0],45.0f) && Near(q.ringAngle[3],315.0f),"quad: four at +-45, +-135");
    const Layout f=LayoutOf(k51,6);
    Check(f.ring==5 && f.lfe==3,"5.1: five around, the LFE the fourth channel");
    const Layout s=LayoutOf(k71,8);
    Check(s.ring==7 && s.lfe==3,"7.1: seven around");
    const Layout none=LayoutOf(0,6);
    Check(none.ring==5 && none.lfe==3,"no mask over six channels: taken as 5.1");
    const Layout mono=LayoutOf(0x4,1);
    Check(mono.channels==1 && mono.ring==1 && mono.lfe<0,"a mono master: its one speaker the ring");
    const Layout top=LayoutOf(k51|0x800,7);   // SPEAKER_TOP_CENTER after the 5.1
    Check(top.ring==5 && top.channels==7,"a top speaker is not put around the listener");
    bool sorted=true;
    for(int i=1;i<s.ring;++i)sorted=sorted && s.ringAngle[i-1]<s.ringAngle[i];
    Check(sorted,"the ring is in angle order");
}

void PowerKept() {
    const std::uint32_t masks[]={kStereo,kStereo|kLowFrequency,kQuad,k51,k71,0};
    const int counts[]={2,3,4,6,8,6};
    bool kept=true,finite=true,noLfe=true;
    for(int k=0;k<6;++k) {
        const Layout l=LayoutOf(masks[k],counts[k]);
        for(int deg=-180;deg<=180;deg+=7)
            for(float spread : {0.0f,0.3f,1.0f}) {
                float m[kMostChannels*2];
                float side,front;Dir(static_cast<float>(deg),&side,&front);
                Matrix(l,Placing{0.8f,side,front,spread,false},1,m);
                kept=kept && Near(Power(m,l.channels),0.64f,1e-3f);
                for(int i=0;i<l.channels;++i)finite=finite && std::isfinite(m[i]);
                if(l.lfe>=0)noLfe=noLfe && m[l.lfe]==0.0f;
                Matrix(l,Placing{0.8f,side,front,spread,false},2,m);
                kept=kept && Near(Power(m,2*l.channels),0.64f,1e-3f);
            }
    }
    Check(kept,"every layout, direction and spread: a source's power is its gain squared (mono and stereo)");
    Check(finite,"no NaN in any matrix");
    Check(noLfe,"the LFE gets nothing of a layer not sent to it");
}

void Directions() {
    float m[kMostChannels];float side,front;
    const Layout st=LayoutOf(kStereo,2);
    Dir(0.0f,&side,&front);Matrix(st,Placing{1,side,front,0,false},1,m);
    Check(Near(m[0],m[1]) && Near(m[0],0.70710678f,1e-3f),"stereo, ahead: both the same at equal power");
    Dir(90.0f,&side,&front);Matrix(st,Placing{1,side,front,0,false},1,m);
    Check(m[1]>0.99f && m[0]<0.05f,"stereo, to the right: the right speaker");
    Dir(-90.0f,&side,&front);Matrix(st,Placing{1,side,front,0,false},1,m);
    Check(m[0]>0.99f && m[1]<0.05f,"stereo, to the left: the left speaker");
    Dir(180.0f,&side,&front);Matrix(st,Placing{1,side,front,0,false},1,m);
    Check(Near(m[0],m[1],1e-3f),"stereo, behind: both (there is no speaker behind)");

    const Layout f=LayoutOf(k51,6);
    Dir(0.0f,&side,&front);Matrix(f,Placing{1,side,front,0,false},1,m);
    Check(On(k51,f,m,kFrontCenter)>0.99f,"5.1, ahead: the centre");
    Dir(180.0f,&side,&front);Matrix(f,Placing{1,side,front,0,false},1,m);
    Check(Near(On(k51,f,m,kBackLeft),On(k51,f,m,kBackRight),1e-3f) && On(k51,f,m,kBackLeft)>0.7f &&
              On(k51,f,m,kFrontLeft)<0.01f && On(k51,f,m,kFrontCenter)<0.01f,
          "5.1, behind: the two back speakers, nothing in front");
    Dir(110.0f,&side,&front);Matrix(f,Placing{1,side,front,0,false},1,m);
    Check(On(k51,f,m,kBackRight)>0.99f,"5.1, at the right back speaker's angle: it alone");
    Dir(70.0f,&side,&front);Matrix(f,Placing{1,side,front,0,false},1,m);
    Check(On(k51,f,m,kFrontRight)>0.1f && On(k51,f,m,kBackRight)>0.1f && On(k51,f,m,kFrontLeft)<0.01f,
          "5.1, to the right: between the right front and back");

    const Layout s=LayoutOf(k71,8);
    Dir(-90.0f,&side,&front);Matrix(s,Placing{1,side,front,0,false},1,m);
    Check(On(k71,s,m,kSideLeft)>0.99f,"7.1, to the left: the left side speaker");
    Dir(-150.0f,&side,&front);Matrix(s,Placing{1,side,front,0,false},1,m);
    Check(On(k71,s,m,kBackLeft)>0.99f,"7.1, behind left: the left back");

    const Layout q=LayoutOf(kQuad,4);
    Dir(180.0f,&side,&front);Matrix(q,Placing{1,side,front,0,false},1,m);
    Check(On(kQuad,q,m,kBackLeft)>0.7f && On(kQuad,q,m,kFrontLeft)<0.01f,"quad, behind: the back pair");
}

void Spread() {
    float m[kMostChannels];
    const Layout f=LayoutOf(k51,6);
    Matrix(f,Placing{1,1,0,1.0f,false},1,m);
    bool even=true;
    for(int i=0;i<f.ring;++i)even=even && Near(m[f.ringChannel[i]],std::sqrt(1.0f/5.0f),1e-3f);
    Check(even,"spread 1: every speaker around the same, the power kept");
    Matrix(f,Placing{1,1,0,0.5f,false},1,m);
    Check(On(k51,f,m,kFrontLeft)>0.0f && On(k51,f,m,kFrontRight)>On(k51,f,m,kFrontLeft),
          "half spread: heard all around, more on its side");
    Check(Near(SpreadAt(0.0f,10.0f,0.0f),1.0f) && Near(SpreadAt(10.0f,10.0f,0.0f),0.5f) &&
              SpreadAt(1000.0f,10.0f,0.0f)<0.02f && Near(SpreadAt(1000.0f,10.0f,0.2f),0.2f),
          "SpreadAt: all around within it, half at its size off, narrowing, at least `least`");
    Check(Near(SpreadAt(-5.0f,10.0f,0.0f),1.0f) && std::isfinite(SpreadAt(NAN,10.0f,0.0f)),"SpreadAt: a bad distance is near");
}

void Sub() {
    float m[kMostChannels*2];
    const Layout f=LayoutOf(k51,6);
    Matrix(f,Placing{0.5f,1,0,0,true},1,m);
    bool only=Near(m[f.lfe],0.5f);
    for(int i=0;i<f.channels;++i)if(i!=f.lfe)only=only && m[i]==0.0f;
    Check(only,"5.1, a sub layer: the LFE alone at its gain");
    Matrix(f,Placing{0.5f,1,0,0,true},2,m);
    Check(Near(m[0+2*f.lfe],0.5f*0.70710678f) && Near(m[1+2*f.lfe],0.5f*0.70710678f) && m[0]==0.0f,
          "5.1, a stereo sub layer: both channels to the LFE");
    const Layout st=LayoutOf(kStereo,2);
    Matrix(st,Placing{0.5f,1,0,0,true},1,m);
    Check(Near(m[0],m[1]) && Near(Power(m,2),0.25f,1e-3f),"stereo, a sub layer: not dropped, every speaker the same");
    const Layout mono=LayoutOf(0,1);
    Matrix(mono,Placing{0.5f,-1,0,0,false},1,m);
    Check(Near(m[0],0.5f),"mono master: all of it in the one channel");
}

void Stereo() {
    float m[kMostChannels*2];
    const Layout st=LayoutOf(kStereo,2);
    Matrix(st,Placing{1,0,1,0.5f,false},2,m);   // ahead, spread half
    Check(m[0+2*0]>m[0+2*1] && m[1+2*1]>m[1+2*0],"a wide stereo recording ahead: its left channel left, its right right");
    Matrix(st,Placing{1,0,1,0.0f,false},2,m);   // ahead, a point
    Check(Near(m[0],m[1]) && Near(m[2],m[3]) && Near(m[0],m[2]),"a stereo recording heard as a point: both channels at its place");
    const Layout f=LayoutOf(k51,6);
    Matrix(f,Placing{1,1,0,0.0f,false},2,m);   // to the right, a point
    Check(Near(On(k51,f,m,kFrontLeft,2,0),0.0f) && Near(On(k51,f,m,kFrontLeft,2,1),0.0f),
          "a stereo recording to the right as a point: nothing on the left");
}

void Bad() {
    float m[kMostChannels];
    const Layout f=LayoutOf(k51,6);
    Matrix(f,Placing{NAN,1,0,0,false},1,m);
    Check(Power(m,f.channels)==0.0f,"a NaN gain: silent");
    Matrix(f,Placing{-1.0f,1,0,0,false},1,m);
    Check(Power(m,f.channels)==0.0f,"a negative gain: silent");
    Matrix(f,Placing{1,NAN,NAN,NAN,false},1,m);
    Check(On(k51,f,m,kFrontCenter)>0.99f,"a NaN direction and spread: ahead, a point");
    Matrix(f,Placing{1,0,0,0,false},1,m);
    Check(On(k51,f,m,kFrontCenter)>0.99f,"no direction (at the camera): ahead");
}
}  // namespace

int main() {
    Layouts();
    PowerKept();
    Directions();
    Spread();
    Sub();
    Stereo();
    Bad();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
