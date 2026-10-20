// The ultimate strike online and its shows (src/ultimate_net_protocol.h, src/ultimate_show.h; src/ultimate.cpp plays them):
//  - the wire: a show's message round trip, every field; each kind; what is refused (another magic, size or version, an
//    unknown kind, numbers not finite or out of bounds, a frame outside the show, no caller or a non-canonical one, no key,
//    ground planes not over the centre);
//  - several shows at once: each its own slot by its key, a second call never refused for the first, a full table refusing,
//    an ended show's late keep never starting it again, an end of a show never played doing nothing;
//  - the dark sky's holders: the first takes the snapshot, the last brings it back, one starting while the sky comes back
//    keeps the snapshot, one after that takes a new one; a value the mission faded since is not the shows';
//  - the ticks: a hitch is played out frame by frame, a late start or a long stall seeks (the frames between never played:
//    their one-off events, the bolts and blasts, dropped), the lasting state of the sought frame (the sky, the beam).
#include "../src/ultimate_net_protocol.h"
#include "../src/ultimate_show.h"
#include <cstdio>
#include <cstring>

namespace {
using namespace crew;
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}

ultimate_net::Msg Sample() {
    ultimate_net::Msg m;
    m.kind=ultimate_net::Kind::start;m.origin=0xA1B2C3D4u;m.serial=7;m.frame=123;m.seed=0x0123456789ABCDEFull;m.damage=15000.0f;
    m.at[0]=-1234.5f;m.at[1]=42.25f;m.at[2]=987.0f;
    m.plane[0]=43.75f;m.plane[1]=60.0f;m.plane[2]=42.25f+ultimate::kPlaneMost;
    for(int i=0;i<32;++i)m.caller[i]=static_cast<unsigned char>(i+1);
    for(int i=20;i<24;++i)m.caller[i]=0;   // the NetId's padding
    return m;
}
bool Same(const ultimate_net::Msg& a,const ultimate_net::Msg& b) {
    return a.kind==b.kind && a.origin==b.origin && a.serial==b.serial && a.frame==b.frame && a.seed==b.seed && a.damage==b.damage &&
           !std::memcmp(a.at,b.at,sizeof(a.at)) && !std::memcmp(a.plane,b.plane,sizeof(a.plane)) && !std::memcmp(a.caller,b.caller,32);
}
// `m` changed by `change` must not encode (Decode refuses the same: both check detail::Valid; the wire's own cases below).
bool Refused(void (*change)(ultimate_net::Msg&)) {
    ultimate_net::Msg m=Sample();
    change(m);
    unsigned char b[ultimate_net::kWire];
    return !ultimate_net::Encode(m,b,sizeof(b));
}
void PutF(unsigned char* b,std::size_t at,float f) { std::memcpy(b+at,&f,4); }
void PutU(unsigned char* b,std::size_t at,std::uint32_t v) { std::memcpy(b+at,&v,4); }

void WireChecks() {
    using namespace ultimate_net;
    unsigned char b[kWire];
    const Msg m=Sample();
    Check(Encode(m,b,sizeof(b)),"a show's start encodes");
    Msg out;
    Check(Decode(b,sizeof(b),out) && Same(m,out),"and decodes to the same, every field (the seed's 64 bits, the caller's 32 bytes)");
    Check(Owned(b,sizeof(b)) && !std::memcmp(b,"ULTS",4),"its magic reads ULTS");
    const Kind kinds[]={Kind::keep,Kind::end};
    for(Kind k:kinds) {
        Msg n=m;n.kind=k;
        Check(Encode(n,b,sizeof(b)) && Decode(b,sizeof(b),out) && out.kind==k,k==Kind::keep ? "a keep round trips" : "an end round trips");
    }
    {Msg n=m;n.frame=ultimate::kAfterEnd;n.kind=Kind::end;
     Check(Encode(n,b,sizeof(b)) && Decode(b,sizeof(b),out) && out.frame==ultimate::kAfterEnd,"an end at the show's last frame");}
    Check(!Encode(m,b,sizeof(b)-1),"no room: not encoded");
    Check(Refused([](Msg& x){x.frame=-1;}) && Refused([](Msg& x){x.frame=ultimate::kAfterEnd+1;}),"a frame outside the show: refused");
    Check(Refused([](Msg& x){x.damage=-1.0f;}) && Refused([](Msg& x){x.damage=2.0e9f;}),"a damage no weapon has: refused");
    Check(Refused([](Msg& x){x.origin=0;}) && Refused([](Msg& x){x.serial=0;}),"no key: refused");
    Check(Refused([](Msg& x){std::memset(x.caller,0,32);}),"no caller: refused");
    Check(Refused([](Msg& x){x.caller[21]=1;}),"a caller ID not canonical: refused");
    Check(Refused([](Msg& x){x.plane[1]=x.at[1]-5.0f;}) && Refused([](Msg& x){x.plane[2]=x.at[1]+ultimate::kPlaneMost+1.0f;}),
          "a ground circle under the centre or past kPlaneMost over it: refused");
    Check(Refused([](Msg& x){x.kind=static_cast<Kind>(9);}),"an unknown kind: refused");
    // On the wire.
    Encode(m,b,sizeof(b));
    unsigned char c[kWire];
    std::memcpy(c,b,kWire);c[0]='X';Check(!Decode(c,kWire,out) && !Owned(c,kWire),"another magic: not this channel's");
    std::memcpy(c,b,kWire);PutU(c,4,2);Check(!Decode(c,kWire,out),"another version: dropped");
    Check(!Decode(b,kWire-4,out),"another size: dropped");
    std::memcpy(c,b,kWire);c[8]=0;Check(!Decode(c,kWire,out),"kind 0: dropped");
    std::memcpy(c,b,kWire);PutF(c,36,std::nanf(""));Check(!Decode(c,kWire,out),"a centre not finite: dropped");
    std::memcpy(c,b,kWire);PutF(c,40,2.0e7f);Check(!Decode(c,kWire,out),"a centre out of any world: dropped");
    std::memcpy(c,b,kWire);PutU(c,20,0xFFFFFFFFu);Check(!Decode(c,kWire,out),"a negative frame: dropped");
    std::memcpy(c,b,kWire);c[60+22]=7;Check(!Decode(c,kWire,out),"a caller ID with its padding set: dropped");
    bool tail=true;for(std::size_t i=92;i<kWire;++i)tail=tail && b[i]==0;
    Check(tail,"the bytes past the fields go out zero");
}

void TableChecks() {
    using namespace ultimate;
    Table t;
    const Key a{1,1},b{1,2},peer{9,1};
    const int i=t.Claim(a,false,0);
    Check(i>=0 && t.Find(a)==i,"a show gets a slot by its key");
    const int j=t.Claim(b,false,0);
    Check(j>=0 && j!=i && t.Count()==2,"a second call while one plays: a second show (never refused for the first)");
    Check(t.Claim(a,false,0)==i,"the same key: the same slot");
    Check(t.Receive(peer,false)==Act::begin && t.Receive(peer,true)==Act::none,"a peer's unknown show: a start or keep begins it, an end does nothing");
    const int k=t.Claim(peer,true,3);
    Check(k>=0 && t.At(k).remote && t.At(k).peer==3,"a peer's show in its own slot, its peer kept");
    Check(t.Receive(peer,false)==Act::follow && t.Receive(peer,true)==Act::stop,"heard again: followed; its end: stopped");
    t.SetPeer(k,0);
    Check(t.Receive(peer,false)==Act::follow,"the room changed (its peer index forgotten): its keep is the same show, never a second");
    t.Release(k);
    Check(t.Find(peer)<0 && t.Receive(peer,false)==Act::none,"ended: a late keep of it never starts it again");
    Check(t.Receive(Key{},false)==Act::none && t.Claim(Key{},false,0)==-1,"no key: nothing");
    for(std::uint32_t s=10;t.Count()<kMaxShows;++s)t.Claim(Key{2,s},false,0);
    Check(t.Claim(Key{3,1},false,0)==-1,"every slot taken: the next show refused (its call keeps the stock beam)");
    t.Reset();
    Check(t.Count()==0 && t.Receive(peer,false)==Act::begin,"a new mission: every show and ended key forgotten");
}

void SkyChecks() {
    using namespace ultimate;
    SkyRefs r;
    const std::uint64_t back=static_cast<std::uint64_t>(kSkyBackSec*kFps);
    Check(r.Acquire(100),"the first show to darken the sky takes the snapshot");
    r.Snapped(true);
    Check(!r.Acquire(400) && r.Holders()==2 && r.HasSnapshot(),"a second, overlapping: no new snapshot (the sky is dark already: D)");
    Check(!r.Release(900,back),"the first lets go: the sky stays dark for the second");
    Check(r.Release(1300,back),"the last lets go: the sky comes back");
    Check(!r.Release(1301,back),"let go of once only");
    Check(!r.Acquire(1300+back/2) && r.HasSnapshot(),"one starting while the sky comes back: the snapshot kept (not a half-dark sky)");
    Check(r.Release(1400+back,back),"and it brings it back as the last");
    Check(r.Acquire(1400+3*back) && !r.HasSnapshot(),"one starting after the sky is back: a new snapshot (the mission's sky may have changed)");
    r.Reset();
    Check(r.Holders()==0 && !r.HasSnapshot(),"a new mission: nothing held");
    const float wrote[4]={0.32f,0.03f,0.09f,1.0f};
    const float same[4]={0.321f,0.0301f,0.0899f,1.0f},faded[4]={0.6f,0.5f,0.5f,1.0f};
    Check(SkyStillOurs(same,wrote,4),"a value where the shows faded it: theirs, brought back");
    Check(!SkyStillOurs(faded,wrote,4),"a value the mission faded since: the mission's, left alone (F)");
    const float dist[4]={40.0f,1800.0f,0.0f,0.0f},distNow[4]={40.0f,1800.0f,123.0f,9.0f};
    Check(SkyStillOurs(distNow,dist,2),"only the components the type uses are compared");
    const float nan[4]={std::nanf(""),0,0,0};
    Check(!SkyStillOurs(nan,wrote,1),"a value not readable as a number: not taken for the shows'");
}

void TickChecks() {
    using namespace ultimate;
    TickPlan p=PlanTick(-1,0);
    Check(!p.seek && p.from==0 && p.to==0,"a show's first tick plays frame 0");
    p=PlanTick(100,103);
    Check(!p.seek && p.from==101 && p.to==103,"a hitch: every frame played (every ring goes off)");
    p=PlanTick(100,100);
    Check(p.to<p.from,"no new frame: nothing");
    p=PlanTick(-1,900);
    Check(p.seek && p.from==900 && p.to==900,"a show joined late: seeks to its frame (E)");
    p=PlanTick(200,200+kMostCatchUp+1);
    Check(p.seek && p.from==200+kMostCatchUp+1,"a long stall: seeks");
    p=PlanTick(10,kAfterEnd+500);
    Check(p.seek && p.from==kAfterEnd,"past the end: to the end");
    // Seek drops the frames between: count the one-off events of the frames a late joiner plays.
    constexpr std::uint64_t seed=0xFEEDull;
    const int joined=kChargeEnd+400;   // mid-storm
    int played=0,bolts=0,missed=0;
    const TickPlan q=PlanTick(-1,joined);
    for(int f=q.from;f<=q.to;++f){++played;Bolt b[kMaxBoltsAFrame];bolts+=BoltsAt(seed,f-kChargeEnd,b);}
    for(int f=kChargeEnd;f<joined;++f){Bolt b[kMaxBoltsAFrame];missed+=BoltsAt(seed,f-kChargeEnd,b);}
    Check(played==1 && missed>100 && bolts<=kMaxBoltsAFrame,"a late joiner plays the sought frame alone: the bolts of the frames before it are never fired");
    Lasting l=LastingAt(joined);
    Check(l.skyDark && !l.beam,"mid-storm the sky is dark, no main beam yet");
    l=LastingAt(kStormEnd+200);
    Check(l.skyDark && l.beam,"in the cannon the main beam lasts (made on the sought frame)");
    l=LastingAt(kSkyBackAt);
    Check(!l.skyDark && !l.beam,"from the sky's way back on: neither");
    Check(DarkSecFrom(0)==kSkyDarkSec && DarkSecFrom(kChargeEnd)==0.5f,"the darkening takes what is left of the charge's fade, at least a moment");
    // The rings all go off when every frame is played; a seek past one drops it.
    int rings=0;
    for(int f=kStormEnd;f<kCannonEnd;++f)rings+=RingAt(f-kStormEnd)>=0;
    Check(rings==kRings,"played frame by frame, every ring goes off");
    // A peer's show: where it starts, when it catches up, an end.
    Check(StartFrame(0)==kLeadFrames && StartFrame(kAfterEnd)==kAfterEnd,"a heard show starts where its caller is, plus the message's way");
    Check(!Behind(100,100) && Behind(100,100+kResync) && !Behind(500,100),"behind its caller by more than kResync: catches up; ahead: plays on");
    Check(EndSeek(300,400)==kCannonEnd && EndSeek(300,kAfterEnd)==-1 && EndSeek(kCannonEnd+5,kCannonEnd+10)==-1,
          "an end mid-show winds it down to the after; at the show's end, or here past the cannon, it plays on");
}
}  // namespace

int main() {
    WireChecks();
    TableChecks();
    SkyChecks();
    TickChecks();
    std::printf("%s (%d failed)\n",failures ? "FAILED" : "all passed",failures);
    return failures ? 1 : 0;
}
