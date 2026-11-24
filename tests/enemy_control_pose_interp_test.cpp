// Guest pose timeline (src/enemy_control_pose_interp.h, net-9): host motion measurement, sample acceptance, clock
// baseline, per-field interpolation, bounded extrapolation and residual convergence. Pure, no game.
#include "../src/enemy_control_pose_interp.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
namespace pose=crew::enemy_native::pose;
namespace pi=crew::enemy_native::pose_interp;
using pose::Kind;
using pose::State;
int checks=0;
void Check(bool good,const char* why,int line) {
    ++checks;
    if(!good){std::fprintf(stderr,"FAIL line %d: %s\n",line,why);std::exit(1);}
}
#define CHECK(good,why) Check((good),(why),__LINE__)
bool Near(double a,double b,double eps=1.0e-4){return std::fabs(a-b)<=eps;}
std::uint32_t Bits(float value){std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof bits);return bits;}

State Make(Kind kind) {
    State s{};s.kind=kind;s.profile=0x1234u;s.rotation[3]=1;
    switch(kind) {
    case Kind::deiroi:s.targetAngles[0]=0.5f;s.targetAngles[1]=0.25f;break;
    case Kind::large:
        s.currentAngles[0]=0.1f;s.currentAngles[1]=0.2f;s.targetAngles[0]=0.3f;s.targetAngles[1]=0.4f;
        s.accumulatedVelocity[0]=1;s.auxiliaryRotation[3]=1;s.rotationStep[3]=1;s.rotationDelta[3]=1;break;
    case Kind::timeShip:
        s.targetRotation[3]=1;s.accumulatedVelocity[1]=1;s.deformation[0]=s.deformation[5]=s.deformation[10]=s.deformation[15]=1;
        s.phase=0.5f;s.scale=1;break;
    case Kind::squid:for(int i=0;i<3;++i){s.currentAngles[i]=0.1f*(i+1);s.targetAngles[i]=0.2f*(i+1);s.accumulatedVelocity[i]=1;}break;
    case Kind::ufo515:s.currentAngles[0]=0.1f;s.currentAngles[1]=0.2f;s.accumulatedVelocity[2]=1;s.bank=0.5f;break;
    }
    return s;
}
pose::Packet At(const State& base,std::uint32_t frame,float x,float motionX=0) {
    pose::Packet p{};p.state=base;p.state.position[0]=x;p.senderFrame=frame;p.motion[0]=motionX;return p;
}
State SampleOk(pi::Track& track,std::uint64_t frame) {
    State out{};CHECK(track.Sample(frame,&out)==pi::Output::ready,"sample ready");CHECK(pose::Valid(out),"every output is pose::Valid");return out;
}

void MotionMeasurement() {
    pi::SendTrack t;float m[3]{9,9,9};
    const float p0[3]={0,0,0};pi::MeasureMotion(t,p0,100,m);
    CHECK(m[0]==0 && m[1]==0 && m[2]==0,"first sample has zero motion");
    const float p1[3]={0.2f,0,-0.4f};pi::MeasureMotion(t,p1,102,m);
    CHECK(Near(m[0],0.1) && m[1]==0 && Near(m[2],-0.2),"two frames, 0.2 m: 0.1 m per frame");
    const float p2[3]={1.2f,0,-0.4f};pi::MeasureMotion(t,p2,118,m);
    CHECK(m[0]==0 && m[2]==0,"gap over 15 frames: zero");
    const float p3[3]={30,0,-0.4f};pi::MeasureMotion(t,p3,120,m);
    CHECK(m[0]==0,"move over 20 m: zero (teleport)");
    const float p4[3]={40,0,-0.4f};pi::MeasureMotion(t,p4,121,m);
    CHECK(m[0]==0,"10 m in one frame is over 5 m/frame: zero");
    const float p5[3]={40,0,-0.4f};pi::MeasureMotion(t,p5,121,m);
    CHECK(m[0]==0,"same frame twice: zero");
    const float p6[3]={42,0,-0.4f};pi::MeasureMotion(t,p6,125,m);
    CHECK(Near(m[0],0.5),"resumes after a teleport");
}

void Acceptance() {
    pi::Track track;const State base=Make(Kind::deiroi);
    CHECK(track.Accept(At(base,100,0),1000),"first accepted");
    CHECK(!track.Accept(At(base,100,1),1001),"duplicate frame dropped");
    CHECK(!track.Accept(At(base,98,1),1001),"older frame dropped");
    CHECK(track.Accept(At(base,102,1),1002),"newer accepted");
    // u32 wrap: a frame just past 0xFFFFFFFF is newer.
    pi::Track wrap;
    CHECK(wrap.Accept(At(base,0xFFFFFFFEu,0),50),"pre-wrap accepted");
    CHECK(wrap.Accept(At(base,0u,2,1),52),"post-wrap frame is newer");
    CHECK(!wrap.Accept(At(base,0xFFFFFFFDu,0),52),"pre-wrap frame after the wrap is older");
    State s=SampleOk(wrap,54);  // render = 54 - offset - 2 lands on the newest (post-wrap) sample
    CHECK(Near(s.position[0],2),"unwrapped timeline orders the samples");
    // kind / profile change restarts.
    pi::Track change;CHECK(change.Accept(At(base,10,0),100),"first");CHECK(change.Accept(At(base,12,5),102),"second");
    State other=base;other.profile=0x9999u;
    CHECK(change.Accept(At(other,5,7),103),"different profile restarts (an older frame is accepted)");
    s=SampleOk(change,103);CHECK(s.profile==0x9999u && Near(s.position[0],7),"only the new profile's sample remains");
}

void ClockBaseline() {
    pi::Track track;const State base=Make(Kind::deiroi);
    track.Accept(At(base,100,0),1010);State out{};track.Sample(1010,&out);CHECK(Near(track.Offset(),910),"first baseline");
    track.Accept(At(base,102,0),1015);track.Sample(1015,&out);CHECK(Near(track.Offset(),910.125),"slower sample creeps 0.125");
    track.Accept(At(base,104,0),1020);track.Sample(1020,&out);CHECK(Near(track.Offset(),910.25),"creeps again");
    track.Accept(At(base,106,0),1010);track.Sample(1020,&out);CHECK(Near(track.Offset(),904),"faster sample drops at once");
}

void Interpolation() {
    pi::Track track;State a=Make(Kind::large);State b=a;
    b.position[0]=10;b.rotation[3]=-1;  // -q of the identity: same orientation, must not spin through 180
    b.targetAngles[0]=0.9f;b.currentAngles[0]=0.7f;b.linearVelocity[1]=4;
    pose::Packet pa{};pa.state=a;pa.senderFrame=100;pose::Packet pb{};pb.state=b;pb.senderFrame=102;
    track.Accept(pa,200);track.Accept(pb,202);
    // offset = 100; render = local - 102.
    State mid=SampleOk(track,203);  // render 101: midpoint
    CHECK(Near(mid.position[0],5),"position midpoint");CHECK(Near(mid.linearVelocity[1],2),"native velocity lerps");
    CHECK(Near(std::fabs(mid.rotation[3]),1),"q and -q blend along the short arc, unit length");
    CHECK(mid.targetAngles[0]==a.targetAngles[0] && mid.currentAngles[0]==a.currentAngles[0],"held fields take the earlier sample");
    State atB=SampleOk(track,204);  // render 102 = B.frame
    CHECK(atB.targetAngles[0]==b.targetAngles[0] && Near(atB.position[0],10),"held fields switch once render reaches B");
    // A real rotation: 90 degrees about Y, halfway is 45.
    pi::Track turn;State c=Make(Kind::squid);State d=c;d.rotation[1]=std::sqrt(0.5f);d.rotation[3]=std::sqrt(0.5f);
    pose::Packet pc{};pc.state=c;pc.senderFrame=10;pose::Packet pd{};pd.state=d;pd.senderFrame=12;
    turn.Accept(pc,20);turn.Accept(pd,22);
    const State half=SampleOk(turn,23);
    CHECK(Near(std::atan2(half.rotation[1],half.rotation[3])*2,std::acos(-1.0)/4,1.0e-3),"nlerp midpoint of 90 degrees is 45");
    // ufo515 bank lerps.
    pi::Track ufo;State e=Make(Kind::ufo515);State f=e;f.bank=1.5f;
    pose::Packet pe{};pe.state=e;pe.senderFrame=0;pose::Packet pf{};pf.state=f;pf.senderFrame=2;
    ufo.Accept(pe,0);ufo.Accept(pf,2);CHECK(Near(SampleOk(ufo,3).bank,1.0),"ufo bank midpoint");
    // render before the oldest sample: the oldest.
    pi::Track early;early.Accept(pa,200);CHECK(SampleOk(early,200).position[0]==0,"before the first sample: hold it");
}

void Extrapolation() {
    pi::Track track;const State base=Make(Kind::deiroi);
    track.Accept(At(base,100,0,0.5f),200);  // offset 100, render = local - 102
    CHECK(Near(SampleOk(track,205).position[0],1.5),"three frames past the newest: pos + 3 * motion");
    CHECK(Near(SampleOk(track,208).position[0],3.0),"six frames: the cap");
    CHECK(Near(SampleOk(track,212).position[0],3.0),"ten frames: held at six");
    State out{};pi::Track none;CHECK(none.Sample(5,&out)==pi::Output::none,"empty track writes nothing");
}

void ResidualConvergence() {
    pi::Track track;const State base=Make(Kind::deiroi);
    track.Accept(At(base,100,0),200);
    CHECK(Near(SampleOk(track,200).position[0],0),"settled at 0");
    for(std::uint64_t f=201;f<210;++f)SampleOk(track,f);
    track.Accept(At(base,102,1),210);  // arrives late (offset creeps by 0.125 only): target jumps to 1 m
    const State first=SampleOk(track,210);
    CHECK(Near(first.position[0],0,1.0e-3),"the frame the jump arrives the body does not move");
    double last=first.position[0];bool monotonic=true;
    for(std::uint64_t f=211;f<=224;++f){const double x=SampleOk(track,f).position[0];if(x<last-1.0e-6)monotonic=false;last=x;}
    CHECK(monotonic,"converges without overshoot");
    CHECK(1-last<0.1,"14 frames later under 0.1 m remains");
    for(std::uint64_t f=225;f<=260;++f)last=SampleOk(track,f).position[0];
    CHECK(last==1.0 && track.Residual()==0,"under 0.01 m the residual clears exactly");
    // Snap: a jump over max(8 m, |motion| * 15) is not smoothed.
    track.Accept(At(base,104,50),262);
    CHECK(Near(SampleOk(track,262).position[0],50),"over 8 m: on the target at once");
    // With fast motion the snap limit is |motion| * 15: 2 m/frame allows 30 m.
    // A late sample 20 m off the extrapolated line (render 8, six frames extrapolated, then the frame-2 sample at 20 m).
    pi::Track fast;fast.Accept(At(base,0,0,2),0);CHECK(Near(SampleOk(fast,10).position[0],12),"extrapolated 6 frames");
    fast.Accept(At(base,2,20,2),10);const double x=SampleOk(fast,10).position[0];
    CHECK(Near(x,12,1.0e-3),"a 20 m correction at 2 m/frame is under 30 m: smoothed, not snapped");
    pi::Track slow;slow.Accept(At(base,0,0,0.1f),0);SampleOk(slow,10);
    slow.Accept(At(base,2,20,0.1f),10);
    CHECK(SampleOk(slow,10).position[0]>20,"the same correction at 0.1 m/frame is over 8 m: snapped");
    // Orientation residual converges toward the new target.
    pi::Track spin;State q0=Make(Kind::squid);State q1=q0;q1.rotation[1]=std::sin(0.25f);q1.rotation[3]=std::cos(0.25f);
    pose::Packet s0{};s0.state=q0;s0.senderFrame=0;spin.Accept(s0,0);SampleOk(spin,0);
    pose::Packet s1{};s1.state=q1;s1.senderFrame=2;spin.Accept(s1,10);  // late
    CHECK(Near(SampleOk(spin,10).rotation[1],0,1.0e-4),"orientation does not jump either");
    State end{};for(std::uint64_t f=11;f<=80;++f)end=SampleOk(spin,f);
    CHECK(Near(end.rotation[1],q1.rotation[1],1.0e-6) && Near(end.rotation[3],q1.rotation[3],1.0e-6),"orientation residual settles on the target");
}

void BatchArrival() {
    pi::Track track;const State base=Make(Kind::deiroi);
    // Steady 0.1 m/frame, then three samples arrive together after a stall.
    std::uint32_t frame=0;std::uint64_t local=1000;double last=-1;bool monotonic=true;
    for(int i=0;i<10;++i){track.Accept(At(base,frame,0.1f*frame,0.1f),local);
        for(int k=0;k<2;++k){const double x=SampleOk(track,local).position[0];if(x<last-1.0e-6)monotonic=false;last=x;++local;}
        frame+=2;}
    local+=4;  // stall: the next three arrive in one frame
    for(int i=0;i<3;++i){track.Accept(At(base,frame,0.1f*frame,0.1f),local);frame+=2;}
    for(int k=0;k<20;++k){const double x=SampleOk(track,local).position[0];if(x<last-1.0e-6)monotonic=false;last=x;++local;
        if(k%2==1){track.Accept(At(base,frame,0.1f*frame,0.1f),local);frame+=2;}}
    CHECK(monotonic,"a batch of late samples never moves the body backwards");
}

void SteadyMotion() {
    pi::Track track;const State base=Make(Kind::deiroi);
    std::uint64_t local=500;double previous=0;bool first=true,even=true;
    for(std::uint32_t frame=0;frame<120;frame+=2) {
        track.Accept(At(base,frame,0.25f*frame,0.25f),local);  // fixed 3-frame latency built into offset
        for(int k=0;k<2;++k) {
            const double x=SampleOk(track,local).position[0];
            if(!first && frame>8 && !Near(x-previous,0.25,1.0e-4))even=false;
            previous=x;first=false;++local;
        }
    }
    CHECK(even,"constant velocity: every frame moves exactly v (no sawtooth)");
}

void ValidityPerKind() {
    const Kind kinds[]={Kind::deiroi,Kind::large,Kind::timeShip,Kind::squid,Kind::ufo515};
    for(Kind kind:kinds) {
        pi::Track track;State a=Make(kind);State b=a;b.position[2]=3;b.rotation[0]=0.6f;b.rotation[3]=0.8f;b.linearVelocity[0]=2;
        pose::Packet pa{};pa.state=a;pa.senderFrame=0;pa.motion[2]=1;pose::Packet pb{};pb.state=b;pb.senderFrame=2;pb.motion[2]=1.5f;
        track.Accept(pa,0);track.Accept(pb,2);
        for(std::uint64_t f=2;f<16;++f) {
            const State s=SampleOk(track,f);
            float fa[pose::kFloatCount]{},fs[pose::kFloatCount]{};
            pose::codec_detail::Flatten(a,fa);pose::codec_detail::Flatten(s,fs);
            bool zeros=true;for(std::size_t i=0;i<pose::kFloatCount;++i)if(Bits(fa[i])==0 && i>15 && Bits(fs[i])!=0)zeros=false;
            CHECK(zeros,"fields unused by the kind stay positive zero");
        }
    }
}

void InvalidOutput() {
    pi::Track track;State base=Make(Kind::deiroi);base.position[0]=999999.0f;
    track.Accept(At(base,0,999999.0f,4.0f),0);State out=Make(Kind::squid);const State before=out;
    CHECK(track.Sample(8,&out)==pi::Output::invalid,"extrapolated past the codec bound: invalid, not written");
    CHECK(!std::memcmp(&out,&before,sizeof(out)),"invalid leaves the output unchanged");
}
} // namespace

int main() {
    MotionMeasurement();Acceptance();ClockBaseline();Interpolation();Extrapolation();ResidualConvergence();BatchArrival();
    SteadyMotion();ValidityPerKind();InvalidOutput();
    std::printf("%d pose interpolation checks passed\n",checks);
    return 0;
}
