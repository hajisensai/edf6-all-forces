// The RuneBlade's arms, the pure part (src/runeblade_melee_logic.h, src/runeblade_melee_net.h, src/runeblade_ether.h): the
// rows' names, the gauge, the strike queue, each arm's frames (M1-M5, R2, R3), the ring's path and the ZWRM codec.
#include "runeblade_melee_logic.h"
#include "runeblade_melee_net.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace rb=crew::runeblade;
namespace {
int checks=0,failed=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { ++failed; std::printf("FAIL %s\n",what); }
}
bool Near(float a,float b,float eps=1e-3f) { return std::fabs(a-b)<=eps*(std::fabs(b)+1.0f); }

rb::Frame Base(rb::Arm arm) {
    rb::Frame f;f.inHand=arm;f.base=100.0f;f.gauge={100.0f,100.0f,false};f.pos={0.0f,0.0f,0.0f};
    return f;
}
// Holds the trigger `frames` frames then releases it; the release frame's Out.
rb::Out HoldRelease(rb::Machine& m,rb::Frame f,int frames) {
    f.held=true;
    for(int i=0;i<frames;++i)rb::Step(m,f);
    f.held=false;
    return rb::Step(m,f);
}

void Names() {
    Check(rb::ArmOf(L"ZW_RuneSaber01")==rb::Arm::daybreak,"M1 row");
    Check(rb::ArmOf(L"ZW_RuneStars01")==rb::Arm::septenary,"R3 row");
    Check(rb::ArmOf(L"ZW_MeleeNone")==rb::Arm::none && rb::ArmOf(nullptr)==rb::Arm::none,"other rows none");
    for(const auto& r:rb::kArmRows)Check(rb::IsMelee(r.arm)==(r.category==rb::kMeleeCategory),"melee rows are category 500");
}

void Gauge() {
    namespace e=rb::ether;
    e::Gauge g{20.0f,100.0f,false};
    Check(e::Spend(g,8.0f) && Near(g.now,12.0f),"spend");
    Check(!e::Spend(g,13.0f) && Near(g.now,12.0f),"not enough: unchanged");
    Check(e::Spend(g,12.0f) && g.spent && g.now==0.0f,"emptied: spent");
    Check(!e::CanSpend(g,1.0f),"spent pays nothing");
    e::Gauge d{40.0f,100.0f,false};
    Check(Near(e::Drain(d),40.0f) && d.spent && e::Drain(d)==0.0f,"drain takes all, once");
    unsigned char soldier[0x400]{};
    const float most=100.0f,now=30.0f;
    std::memcpy(soldier+e::kMost,&most,4);std::memcpy(soldier+e::kNow,&now,4);
    Check(e::Spend(soldier,10.0f),"live spend");
    e::Gauge live;
    Check(e::Read(soldier,&live) && Near(live.now,20.0f) && !live.spent,"live read");
    Check(Near(e::Drain(static_cast<void*>(soldier)),20.0f) && e::Read(soldier,&live) && live.spent,"live drain sets +0x2FC");
}

void QueueRuns() {
    rb::Queue<4> q;
    rb::Strike s;s.arm=rb::Arm::daybreak;s.effect=rb::Effect::trail;s.from={0,0,0};s.dir={1,0,0};s.length=4.0f;
    s.points=3;s.waves=2;s.every=5;s.delay=2;s.stagger=1;s.radius=1.0f;s.grow=0.5f;s.damage=3.0f;
    Check(q.Push(s,7),"push");
    rb::Strike bad=s;bad.dir={0,0,0};
    Check(!q.Push(bad,7),"no direction refused");
    int hits=0,lastFrame=-1,frame=0;float lastX=-1.0f,lastR=0.0f;
    for(;frame<20;++frame)q.Step([&](const rb::Hit& h){++hits;lastFrame=frame;lastX=h.at.x;lastR=h.radius;Check(h.owner==7,"owner kept");});
    Check(hits==6,"points x waves hits");
    Check(lastFrame==rb::StrikeFrames(s) && lastFrame==2+5+2,"last hit at delay + every + stagger*2");
    Check(Near(lastX,4.0f) && Near(lastR,1.5f),"last point at the end, radius grown");
    Check(q.Pending()==0,"done entries freed");
}

void Daybreak() {
    rb::Machine m;rb::Frame f=Base(rb::Arm::daybreak);
    rb::Out o;
    int trails=0;
    for(int stage=0;stage<3;++stage) {
        f.held=true;o=rb::Step(m,f);
        if(o.strikes)++trails;
        f.held=false;for(int i=0;i<10;++i)rb::Step(m,f);
    }
    Check(trails==1 && m.comboStep==3,"only stage 3 leaves the trail");
    f.held=true;o=rb::Step(m,f);
    Check(m.comboStep==1,"a fourth press starts again");
    rb::Machine t;f.held=false;
    for(int stage=0;stage<3;++stage){f.held=true;o=rb::Step(t,f);f.held=false;rb::Step(t,f);}
    Check(o.strikes==1 && o.strike[0].effect==rb::Effect::trail,"trail strike");
    const rb::Strike& s=o.strike[0];
    Check(s.delay+(s.waves-1)*s.every+s.every==rb::kTrailDelay+rb::kTrailFrames,"the trail lasts 0.5 s");
    rb::Machine w;f=Base(rb::Arm::daybreak);for(int i=0;i<rb::kComboWindow+1;++i)rb::Step(w,f);
    f.held=true;rb::Step(w,f);f.held=false;for(int i=0;i<rb::kComboWindow;++i)rb::Step(w,f);
    f.held=true;rb::Step(w,f);
    Check(w.comboStep==1,"window closed: back to stage 1");
    // the dive
    rb::Machine d;f=Base(rb::Arm::daybreak);f.air=true;f.held=true;
    o=rb::Step(d,f);
    Check(d.phase==rb::Phase::dive && o.velocity && o.vel.y<0.0f,"pressed in the air: dive");
    f.held=false;o=rb::Step(d,f);
    Check(o.velocity,"keeps falling");
    f.air=false;o=rb::Step(d,f);
    Check(o.strikes==1 && o.strike[0].effect==rb::Effect::dive && Near(o.strike[0].damage,100.0f*rb::kDiveDamage),"lands: slash");
}

void Firmament() {
    rb::Machine m;rb::Frame f=Base(rb::Arm::firmament);
    rb::Out o=HoldRelease(m,f,rb::kLanceMin-2);
    Check(!o.velocity && m.phase==rb::Phase::idle,"short charge: nothing");
    f.pitch=-0.5f;   // looking up
    o=HoldRelease(m,f,rb::kLanceFull+10);
    Check(o.velocity && o.vel.y>0.0f && Near(rb::Len(o.vel),rb::kLanceFast),"full charge: fast dash along the view");
    Check(o.strikes==1 && o.strike[0].points==rb::kLancePoints && o.strike[0].stagger==rb::kLanceStagger,"pierce line");
    int frames=0;bool burst=false;
    f.held=false;
    for(;frames<40 && !burst;++frames){o=rb::Step(m,f);burst=o.strikes && o.strike[0].effect==rb::Effect::burst;}
    Check(burst && frames==rb::kLanceFrames+1,"burst at the dash's end");
    rb::Machine a;rb::Frame air=Base(rb::Arm::firmament);air.air=true;
    Check(HoldRelease(a,air,rb::kLanceMin).velocity,"in the air too");
}

void Stars() {
    rb::Machine m;rb::Frame f=Base(rb::Arm::stars);
    rb::Out o=HoldRelease(m,f,rb::kRingHold-1);
    Check(!o.ringThrown,"short hold: the stock dance only");
    o=HoldRelease(m,f,rb::kRingHold);
    Check(o.ringThrown && m.ringFrame==0,"thrown");
    int out=0,hits=0;bool back=false;
    for(int i=0;i<rb::kRingFrames+5 && !back;++i){o=rb::Step(m,f);out+=o.ringOut;hits+=o.strikes;back=o.ringBack;}
    Check(back && out==rb::kRingFrames,"back after its flight");
    Check(hits==rb::kRingFrames/rb::kRingEvery,"hits each kRingEvery");
    const rb::V3 home{5.0f,0.0f,0.0f};
    const rb::V3 start=rb::RingAt({0,0,0},home,0.0f,0),mid=rb::RingAt({0,0,0},home,0.0f,rb::kRingFrames/2),
                 end=rb::RingAt({0,0,0},home,0.0f,rb::kRingFrames);
    Check(Near(start.x,0.0f) && Near(start.z,0.0f),"leaves from the thrower");
    Check(Near(rb::Len({mid.x,0.0f,mid.z}),rb::kRingReach),"circles at its reach");
    Check(Near(end.x,5.0f) && Near(end.z,0.0f),"comes back to where the thrower is now");
    rb::Marks k;
    Check(k.Add(3) && k.Add(4) && !k.Add(3) && !k.Add(0) && k.count==2,"marks: each once");
    const rb::Strike b0=rb::MarkBurst(rb::Arm::stars,0,{0,0,0},100.0f),b2=rb::MarkBurst(rb::Arm::stars,2,{0,0,0},100.0f);
    Check(b0.delay==0 && b2.delay==2*rb::kMarkStagger,"one after another");
}

void Fallstar() {
    rb::Machine g;rb::Frame f=Base(rb::Arm::fallstar);
    Check(!HoldRelease(g,f,30).velocity && g.phase==rb::Phase::idle,"on the ground: the stock sweep only");
    rb::Machine m;f.air=true;f.pos.y=30.0f;
    rb::Out o=HoldRelease(m,f,rb::kSmashMin);
    Check(o.drain && o.velocity && o.vel.y<0.0f && m.phase==rb::Phase::smashFall,"released: drains, falls");
    f.pos.y=0.0f;f.air=false;
    o=rb::Step(m,f);
    Check(o.strikes==1 && o.strike[0].effect==rb::Effect::smash,"the smash");
    Check(Near(o.strike[0].damage,100.0f*(1.0f+30.0f*rb::kSmashPerMetre)),"damage grows with the height");
    Check(o.strike[0].waves==rb::kSmashWaves && o.strike[0].grow>0.0f,"the shock wave spreads");
    rb::Machine e;f=Base(rb::Arm::fallstar);f.air=true;f.gauge={0.0f,100.0f,true};
    o=HoldRelease(e,f,rb::kSmashMin);
    Check(o.refused && !o.velocity,"no ether: refused");
    rb::Machine h;f=Base(rb::Arm::fallstar);f.air=true;f.pos.y=1000.0f;HoldRelease(h,f,rb::kSmashMin);
    f.pos.y=0.0f;f.air=false;o=rb::Step(h,f);
    Check(Near(o.strike[0].damage,100.0f*rb::kSmashMostScale) && Near(o.strike[0].radius,rb::kSmashMostRadius),"capped");
}

void Afterglow() {
    rb::Machine m;rb::Frame f=Base(rb::Arm::afterglow);
    rb::Out o=HoldRelease(m,f,rb::kDialTicks*rb::kStanceTick-2);
    Check(o.strikes==0 && !o.markLine,"released early: nothing");
    f.held=true;
    for(int i=0;i<rb::kDialTicks*rb::kStanceTick+20;++i)o=rb::Step(m,f);
    Check(o.dial==rb::kDialTicks,"the dial full");
    f.held=false;o=rb::Step(m,f);
    Check(o.strikes==1 && o.markLine && o.strike[0].effect==rb::Effect::iaido && Near(o.strike[0].length,rb::kIaidoLength),"the cut");
    const rb::Strike a=rb::MarkBurst(rb::Arm::afterglow,0,{0,0,0},100.0f),b=rb::MarkBurst(rb::Arm::afterglow,9,{0,0,0},100.0f);
    Check(a.delay==rb::kIaidoDelay && b.delay==rb::kIaidoDelay,"all burst together about 1 s later");
    Check(Near(rb::SegmentDistance({10,0,2},{0,0,0},{1,0,0},40.0f),2.0f) && Near(rb::SegmentDistance({-3,0,4},{0,0,0},{1,0,0},40.0f),5.0f),
          "segment distance");
}

void Firefly() {
    rb::Machine m;rb::Frame f=Base(rb::Arm::daybreak);f.ranged=rb::Arm::firefly;
    f.held=true;rb::Step(m,f);f.held=false;rb::Step(m,f);   // stage 1
    int shots=0;
    for(int i=0;i<200;++i){f.second=(i%2)==0;shots+=rb::Step(m,f).firefly;if(i==20)break;}
    Check(shots>=2 && m.comboStep==1,"the shots leave the combo as it was");
    rb::Machine r;f=Base(rb::Arm::daybreak);f.ranged=rb::Arm::firefly;shots=0;
    for(int i=0;i<200;++i){f.second=(i%(rb::kFireflyCool*2))==0;shots+=rb::Step(r,f).firefly;}
    Check(shots>rb::kFireflyRounds,"reloads after six");
    rb::Machine n;f=Base(rb::Arm::daybreak);f.ranged=rb::Arm::none;f.second=true;
    Check(!rb::Step(n,f).firefly,"no Firefly in the ranged slot: no shot");
}

void Septenary() {
    rb::Machine m;rb::Frame f=Base(rb::Arm::septenary);
    f.held=true;
    int raised=0;
    for(int i=0;i<200;++i){const rb::Out o=rb::Step(m,f);if(o.spend>0.0f){++raised;f.gauge.now-=o.spend;}}
    Check(raised==rb::kArraysMost && m.arrays==rb::kArraysMost,"at most 7");
    f.held=false;const rb::Out o=rb::Step(m,f);
    Check(o.salvo==rb::kArraysMost && m.arrays==0,"released: the salvo");
    rb::Machine p;f=Base(rb::Arm::septenary);f.gauge={20.0f,100.0f,false};f.held=true;raised=0;
    for(int i=0;i<200;++i){const rb::Out x=rb::Step(p,f);if(x.spend>0.0f){++raised;rb::ether::Spend(f.gauge,x.spend);}}
    Check(raised==2,"as many as the ether pays");
    const rb::V3 a0=rb::ArrayOffset(0,0.0f),a6=rb::ArrayOffset(6,0.0f);
    Check(a0.x<0.0f && a6.x>0.0f && a0.y>1.0f,"the Dipper across the shoulders");
}

void Codec() {
    namespace z=crew::zwrb;
    z::Msg m;m.kind=z::Kind::strike;crew::netref::MakePlayer(2,m.player);
    m.s.arm=rb::Arm::fallstar;m.s.effect=rb::Effect::smash;m.s.from={1,2,3};m.s.dir={0,0,1};m.s.radius=8.0f;m.s.grow=4.0f;
    m.s.damage=250.0f;m.s.waves=3;m.s.every=6;
    unsigned char b[z::kWire];
    Check(z::Encode(m,b,sizeof(b)),"encode strike");
    z::Msg d;
    Check(z::Decode(b,sizeof(b),d) && d.kind==z::Kind::strike && d.s.arm==rb::Arm::fallstar && Near(d.s.from.z,3.0f) &&
          d.s.waves==3 && d.s.every==6 && Near(d.s.grow,4.0f) && !std::memcmp(d.player,m.player,32),"round trip strike");
    Check(!z::Decode(b,sizeof(b)-1,d),"wrong size");
    unsigned char v[z::kWire];std::memcpy(v,b,sizeof(b));v[4]=9;
    Check(!z::Decode(v,sizeof(v),d),"other version");
    std::memcpy(v,b,sizeof(b));v[9]=0;
    Check(!z::Decode(v,sizeof(v),d),"no arm");
    z::Msg r;r.kind=z::Kind::ring;crew::netref::MakePlayer(1,r.player);r.s.arm=rb::Arm::stars;r.s.from={4,5,6};r.yaw=1.25f;
    Check(z::Encode(r,b,sizeof(b)) && z::Decode(b,sizeof(b),d) && d.kind==z::Kind::ring && Near(d.yaw,1.25f) && Near(d.s.from.x,4.0f),
          "round trip ring");
    r.s.arm=rb::Arm::daybreak;
    Check(!z::Encode(r,b,sizeof(b)),"a ring is the Stars'");
    z::Msg t;t.kind=z::Kind::bolt;crew::netref::MakePlayer(0,t.player);t.s.arm=rb::Arm::septenary;t.s.effect=rb::Effect::bolt;
    t.s.dir={0,1,0};t.s.length=4.0f;t.s.radius=1.0f;t.s.damage=30.0f;
    Check(z::Encode(t,b,sizeof(b)) && z::Decode(b,sizeof(b),d) && d.kind==z::Kind::bolt && Near(d.s.length,4.0f),"round trip bolt");
    z::Msg n=m;std::memset(n.player,0,32);
    Check(!z::Encode(n,b,sizeof(b)),"a player's only");
}
}  // namespace

int main() {
    Names();Gauge();QueueRuns();Daybreak();Firmament();Stars();Fallstar();Afterglow();Firefly();Septenary();Codec();
    std::printf("runeblade_melee: %d checks, %d failed\n",checks,failed);
    return failed ? 1 : 0;
}
