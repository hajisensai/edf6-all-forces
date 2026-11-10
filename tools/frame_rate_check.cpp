// The frame rate decoupling's pure parts (src/frame_rate_logic.h, docs/unlimited-fps-re.md), offline:
//  - FrameRateCap: off / no cap / clamped, and the slot period never letting the rate pass the cap;
//  - the slots on a fake clock: a frame runs only at or after its slot, the rate stays at the cap over a long run with
//    random frame costs, a late frame restarts the slots (no burst), the wait ends exactly at the slot;
//  - the whole loop played out: the game's tick clock (0x11DAE60 as frame_pacing_logic.h reads it) asked only for a frame
//    that runs, so over a long run the steps are exactly the ticks that passed (the simulation keeps 59.94 Hz) at caps
//    61 / 120 / 144 / 240 / 1000 / no cap;
//  - the render phase: 1 on a frame that steps, the part of a tick on one that does not, rising between steps, the
//    shown time continuous across a step to within the slot a step waited;
//  - the rigid interpolation: round trip of rotations, ends exact (0 and 1), the middle of a known turn, mirrored
//    matrices, the shortest arc, cuts (too far, too much turn) and broken matrices refused;
//  - the input edges: random presses over random step / no-step frames: every press reaches the step exactly once and
//    the UI exactly once, where without the shadow the step misses some.
// Exit code 1 when one fails. Built with offline_checks: cmake --build build --target frame_rate_check && build\frame_rate_check.exe
#include "../src/frame_rate_logic.h"
#include "../src/frame_pacing_logic.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>

namespace {
using namespace crew::framerate;
using u64=std::uint64_t;
int failures=0,checks=0;

void Expect(bool ok,const char* what,double a=0,double b=0) {
    ++checks;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}

void CapSettings() {
    Expect(FixCap(0)==kOff,"0 is off");
    Expect(FixCap(-1)==kUnlimited,"-1 is no cap");
    Expect(FixCap(-2)==kOff && FixCap(-1000)==kOff,"under -1 is off");
    Expect(FixCap(1)==kOff && FixCap(30)==kOff && FixCap(60)==kOff,"1..60 is off (the game gives 59.94 already)");
    Expect(FixCap(61)==61 && FixCap(120)==120 && FixCap(1000)==1000,"61..1000 as given");
    Expect(FixCap(1001)==1000 && FixCap(100000)==1000,"over 1000 is 1000");
    Expect(PresentPeriod(10'000'000,0)==0 && PresentPeriod(10'000'000,kUnlimited)==0 && PresentPeriod(0,120)==0,"no period off");
    const u64 freqs[]={10'000'000,3'579'545,2'000'000'000,1'000'003};
    for(u64 f:freqs)for(int cap:{61,120,144,165,240,1000}) {
        const u64 p=PresentPeriod(f,cap);
        Expect(p*static_cast<u64>(cap)>=f && (p-1)*static_cast<u64>(cap)<f,"period is the ceiling of freq/cap",static_cast<double>(f),cap);
    }
}

void Slots() {
    const u64 freq=10'000'000;
    for(int cap:{61,120,144,240}) {
        const u64 period=PresentPeriod(freq,cap);
        Gate g{};
        u64 now=123456789,runs=0,start=now;
        std::mt19937_64 rng(static_cast<u64>(cap));
        // A frame's cost: mostly under a slot, now and then over it.
        std::uniform_int_distribution<u64> cost(period/10,period*3/2);
        while(now-start<freq*20) {
            const Gate before=g;
            if(Ready(g,now,cap,period)) {
                Expect(!before.armed || static_cast<std::int64_t>(now-before.next)>=0,"a frame never runs before its slot");
                ++runs;
                now+=cost(rng);
            } else {
                const u64 w=WaitCounts(g,now,cap);
                Expect(w>0,"not ready means a wait",static_cast<double>(w));
                Expect(!Ready(g,now+w-1,cap,period),"not ready one count before the slot");
                now+=w;
            }
        }
        const double rate=static_cast<double>(runs)/20.0;
        Expect(rate<=cap+0.05,"never past the cap",rate,cap);
        Expect(rate>=cap*0.80,"the cap reached when frames are cheap enough on average",rate,cap);
    }
    // Cheap frames: exactly the cap.
    {
        const u64 period=PresentPeriod(freq,144);
        Gate g{};
        u64 now=5,runs=0;
        for(u64 end=now+freq*10;now<end;) {
            if(Ready(g,now,144,period)){++runs;now+=1000;}
            else now+=WaitCounts(g,now,144);
        }
        Expect(runs>=1439 && runs<=1441,"cheap frames run at the cap",static_cast<double>(runs));
    }
    // A long frame: the next runs at once, the one after a whole period later (no catch-up burst).
    {
        const u64 period=PresentPeriod(freq,120);
        Gate g{};
        Expect(Ready(g,1000,120,period),"the first frame runs");
        const u64 late=1000+period*5;
        Expect(Ready(g,late,120,period),"a frame after a long one runs");
        Expect(!Ready(g,late+1,120,period),"...and the next waits");
        Expect(WaitCounts(g,late+1,120)==period-1,"...a whole period after the late one",static_cast<double>(WaitCounts(g,late+1,120)));
    }
    // Slightly late frames keep the slots' phase (no drift).
    {
        const u64 period=PresentPeriod(freq,120);
        Gate g{};
        Ready(g,0,120,period);
        Expect(Ready(g,period+7,120,period) && g.next==2*period,"slightly late keeps the phase",static_cast<double>(g.next));
    }
    // No cap: always ready, no wait.
    {
        Gate g{};
        for(u64 t=0;t<100;++t)Expect(Ready(g,t,kUnlimited,0) && WaitCounts(g,t,kUnlimited)==0,"no cap: every frame runs");
    }
    // QPC near the top of 64 bits: slots across the wrap.
    {
        const u64 period=PresentPeriod(freq,120);
        Gate g{};
        const u64 t0=~0ull-period/2;
        Expect(Ready(g,t0,120,period),"wrap: first");
        Expect(!Ready(g,t0+period/4,120,period),"wrap: before the slot (past the wrap)");
        Expect(Ready(g,t0+period,120,period),"wrap: at the slot");
    }
}

// The game's tick clock (0x11DAE60) on a state: the ticks, moving last the way the game does.
struct GameClock {
    u64 base,last=0,period;
    u64 Count(u64 now) {
        const u64 n=crew::pacing::TicksPassed(now,base,last,period);
        if(n)last+=n*period;
        return n;
    }
};

void Loop() {
    const u64 freq=10'000'000,tick=crew::pacing::TickPeriod(freq);
    for(int cap:{61,120,144,240,1000,kUnlimited}) {
        const u64 period=PresentPeriod(freq,cap);
        GameClock clock{987654321,0,tick};
        Gate g{};
        u64 now=clock.base+11,steps=0,frames=0,presentFrames=0;
        std::mt19937_64 rng(static_cast<u64>(cap+7));
        std::uniform_int_distribution<u64> cost(freq/2000,freq/400);   // 0.5..2.5 ms
        float lastShown=-1.0f;   // the shown time in ticks: steps before the frame + phase - 1
        bool continuous=true;
        u64 stepsSoFar=0;
        while(now-clock.base<freq*30) {
            if(!Ready(g,now,cap,period)){now+=WaitCounts(g,now,cap);continue;}
            const u64 n=clock.Count(now);
            u64 capped=n<3 ? n : 3;   // the loop's min(n, 3)
            const float a=RenderAlpha(capped,now,clock.base,clock.last,tick);
            Expect(a>=0.0f && a<=1.0f,"phase in [0, 1]",a);
            // Render comes before the frame's steps: the pair is (stepsSoFar-1, stepsSoFar).
            const float shown=static_cast<float>(stepsSoFar)-1.0f+a;
            if(lastShown>=0.0f && shown+1e-4f<lastShown)continuous=false;
            // Never more than one tick plus a slot ahead of the last shown (no jump forward).
            if(lastShown>=0.0f && cap!=kUnlimited && shown-lastShown>1.0f+static_cast<float>(period)/static_cast<float>(tick)+1e-3f)
                continuous=false;
            lastShown=shown;
            stepsSoFar+=capped;steps+=n;++frames;
            if(!capped)++presentFrames;
            now+=cost(rng);
        }
        // What the loop really stepped (after its min(n, 3)) against the ticks that passed: `steps` sums the clock's own
        // counts and equals clock.last/tick by construction, so only `stepsSoFar` says whether the gate lost a tick.
        const u64 passed=(now-clock.base)/tick;
        Expect(steps==clock.last/tick,"the clock's counts are its boundaries",static_cast<double>(steps),static_cast<double>(clock.last/tick));
        Expect(stepsSoFar==clock.last/tick,"no counted tick dropped by the loop's min(n, 3)",static_cast<double>(stepsSoFar),static_cast<double>(clock.last/tick));
        Expect(stepsSoFar+1>=passed && stepsSoFar<=passed,"every tick that passed is stepped once",static_cast<double>(stepsSoFar),static_cast<double>(passed));
        const double tickRate=static_cast<double>(stepsSoFar)/(static_cast<double>(now-clock.base)/static_cast<double>(freq));
        Expect(std::fabs(tickRate-59.94)<0.1,"the simulation keeps 59.94 Hz",tickRate,cap);
        Expect(continuous,"the shown time never goes back nor jumps",cap);
        const double rate=static_cast<double>(frames)/30.0;
        if(cap!=kUnlimited)Expect(rate<=cap+0.05 && rate>=std::min(cap,400)*0.9,"frames at the cap",rate,cap);
        Expect(presentFrames>0,"some frames only present",static_cast<double>(presentFrames));
    }
}

// The control for Loop: a gate that counts the ticks first and then skips the frame when its slot has not come drops
// the ticks it counted (0x11DAE60 has already moved its boundary on), so the simulation runs slow. This is why Ready is
// asked before the clock; the check above would not tell the two orders apart if it counted the clock's own sums.
void LoopCountFirst() {
    const u64 freq=10'000'000,tick=crew::pacing::TickPeriod(freq);
    const int cap=61;
    const u64 period=PresentPeriod(freq,cap);
    GameClock clock{987654321,0,tick};
    Gate g{};
    u64 now=clock.base+11,stepped=0;
    std::mt19937_64 rng(3);
    std::uniform_int_distribution<u64> cost(freq/2000,freq/400);
    while(now-clock.base<freq*30) {
        const u64 n=clock.Count(now);
        if(!Ready(g,now,cap,period)){now+=WaitCounts(g,now,cap);continue;}
        stepped+=n<3 ? n : 3;
        now+=cost(rng);
    }
    Expect(stepped<clock.last/tick*99/100,"counting before the gate loses ticks (the order Ready guards against)",
           static_cast<double>(stepped),static_cast<double>(clock.last/tick));
}

void Alpha() {
    const u64 tick=1000;
    Expect(RenderAlpha(1,500,0,0,tick)==1.0f,"a frame that steps renders the last step");
    Expect(RenderAlpha(3,999,0,0,tick)==1.0f,"catch-up too");
    Expect(RenderAlpha(0,0,0,0,tick)==0.0f,"right on the boundary: the step before");
    Expect(std::fabs(RenderAlpha(0,250,0,0,tick)-0.25f)<1e-6f,"a quarter in");
    Expect(RenderAlpha(0,5000,0,0,tick)==1.0f,"past the tick (not yet counted): held at 1");
    Expect(RenderAlpha(0,5,0,0,0)==1.0f,"no period: 1");
    // Across the wrap of QPC.
    const u64 base=~0ull-100;
    Expect(std::fabs(RenderAlpha(0,base+600,base,0,tick)-0.6f)<1e-6f,"phase across the QPC wrap");
}

// A camera-like rigid matrix: yaw about up, pitch about right, position.
void Make(float m[16],double yaw,double pitch,float x,float y,float z,bool mirror=false) {
    const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    const double right[3]={cy,0,-sy},fwd[3]={sy*cp,-sp,cy*cp};
    const double up[3]={fwd[1]*right[2]-fwd[2]*right[1],fwd[2]*right[0]-fwd[0]*right[2],fwd[0]*right[1]-fwd[1]*right[0]};
    for(int k=0;k<3;++k){m[k]=static_cast<float>(right[k]);m[4+k]=static_cast<float>(up[k]);m[8+k]=static_cast<float>(fwd[k]*(mirror ? -1 : 1));}
    m[3]=m[7]=m[11]=0.0f;m[12]=x;m[13]=y;m[14]=z;m[15]=1.0f;
}
double MaxDiff(const float a[16],const float b[16]) {
    double d=0;
    for(int i=0;i<16;++i)d=std::fmax(d,std::fabs(static_cast<double>(a[i])-b[i]));
    return d;
}

void Rigid() {
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> ang(-3.1,3.1),pit(-1.4,1.4);
    for(int i=0;i<2000;++i) {
        float m[16],r[16];
        Make(m,ang(rng),pit(rng),1,2,3,i%2==1);
        const double s=RigidSign(m);
        Expect(s==(i%2 ? -1.0 : 1.0),"handedness read",s);
        Quat q{};
        Expect(RotationOf(m,s,q),"a rotation has a quaternion");
        RowsOf(q,s,r);
        double d=0;
        for(int k=0;k<12;++k)if(k%4!=3)d=std::fmax(d,std::fabs(static_cast<double>(m[k])-r[k]));
        Expect(d<1e-5,"rotation round trip",d);
    }
    float a[16],b[16],o[16],e[16];
    Make(a,0.0,0.0,0,0,0);Make(b,0.4,0.0,1,0,0);
    Expect(InterpolateRigid(a,b,0.0f,o,10,1) && MaxDiff(o,a)<1e-5,"alpha 0 is the previous",MaxDiff(o,a));
    Expect(InterpolateRigid(a,b,1.0f,o,10,1) && MaxDiff(o,b)<1e-5,"alpha 1 is the current",MaxDiff(o,b));
    Make(e,0.1,0.0,0.25f,0,0);
    Expect(InterpolateRigid(a,b,0.25f,o,10,1) && MaxDiff(o,e)<1e-5,"a quarter of a yaw and a move",MaxDiff(o,e));
    // Pitch and yaw together, mirrored: the middle stays orthonormal and mirrored.
    Make(a,0.3,0.2,0,0,0,true);Make(b,0.5,-0.1,0,1,0,true);
    Expect(InterpolateRigid(a,b,0.5f,o,10,1) && RigidSign(o)==-1.0,"mirrored stays mirrored and rigid");
    // Shortest arc: from just under pi to just over -pi is a small turn.
    Make(a,3.1,0,0,0,0);Make(b,-3.1,0,0,0,0);Make(e,3.1+0.0415926535,0,0,0,0);
    Expect(InterpolateRigid(a,b,0.5f,o,10,0.5f) && MaxDiff(o,e)<1e-4,"the shortest way round",MaxDiff(o,e));
    // Cuts refused.
    Make(a,0,0,0,0,0);Make(b,0,0,50,0,0);
    Expect(!InterpolateRigid(a,b,0.5f,o,10,1),"a teleport is a cut");
    Make(b,2.0,0,0,0,0);
    Expect(!InterpolateRigid(a,b,0.5f,o,10,1),"a big turn is a cut");
    // Mixed handedness, broken matrices, NaN, alpha out of range.
    Make(a,0,0,0,0,0);Make(b,0.1,0,0,0,0,true);
    Expect(!InterpolateRigid(a,b,0.5f,o,10,1),"mirrored against unmirrored refused");
    Make(b,0.1,0,0,0,0);b[0]*=2.0f;
    Expect(!InterpolateRigid(a,b,0.5f,o,10,1),"a scaled matrix refused");
    Make(b,0.1,0,0,0,0);b[5]=std::nanf("");
    Expect(!InterpolateRigid(a,b,0.5f,o,10,1),"NaN refused");
    Make(b,0.1,0,0,0,0);
    Expect(!InterpolateRigid(a,b,1.5f,o,10,1) && !InterpolateRigid(a,b,-0.1f,o,10,1) && !InterpolateRigid(a,b,std::nanf(""),o,10,1),"alpha out of [0, 1] refused");
    // The fourth column comes from the current.
    b[3]=0.0f;b[15]=1.0f;
    Expect(InterpolateRigid(a,b,0.5f,o,10,1) && o[15]==1.0f && o[3]==0.0f,"fourth column from the current");
}

// The game's wrapper update (0x1182BC0) for one action: clears the edges, sets them on a change.
struct Action {
    bool current=false;
    Edges edges{};
    void Update(bool down) {
        edges={};
        if(down!=current){if(down)edges.pressed=1;else edges.released=1;}
        current=down;
    }
};

void InputEdges() {
    std::mt19937_64 rng(7);
    for(int withShadow=0;withShadow<2;++withShadow) {
        u64 presses=0,uiSaw=0,stepSaw=0,stepShould=0;
        Action act;
        Shadow sh;
        bool down=false,pressSinceStep=false;
        for(int frame=0;frame<200000;++frame) {
            // A step one frame in three on average (any number of presentation frames between), presses of 1.. frames.
            const u64 ticks=(rng()%3)==0 ? 1 : 0;
            // The frame: System::Update (the step reads the edges), then the wrapper update, then the UI.
            if(ticks) {
                const Edges e=withShadow ? ForStep(sh,act.edges) : act.edges;
                stepSaw+=e.pressed;
                // The step should see a press when any came since the last step (two in one tick are one to a step,
                // as at the game's own rate a step never sees two either).
                stepShould+=pressSinceStep ? 1u : 0u;
                pressSinceStep=false;
            }
            bool next=down;
            if(!down && rng()%5==0)next=true;
            else if(down && rng()%2==0)next=false;
            if(withShadow)BeforeUpdate(sh,act.edges,ticks);
            act.Update(next);
            down=next;
            if(act.edges.pressed){++presses;pressSinceStep=true;}
            uiSaw+=act.edges.pressed;
        }
        Expect(uiSaw==presses,"the UI sees each press once",static_cast<double>(uiSaw),static_cast<double>(presses));
        if(withShadow)Expect(stepSaw==stepShould,"with the shadow every tick with a press sees it, once",
                             static_cast<double>(stepSaw),static_cast<double>(stepShould));
        else Expect(stepSaw<stepShould*9/10,"without it the step misses presses (the failure the shadow is for)",
                    static_cast<double>(stepSaw),static_cast<double>(stepShould));
    }
}
}  // namespace

int main() {
    CapSettings();
    Slots();
    Loop();
    LoopCountFirst();
    Alpha();
    Rigid();
    InputEdges();
    std::printf("frame_rate_check: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
