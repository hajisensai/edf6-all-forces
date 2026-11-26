// The Balam's impact marker's pure part (src/barga_aim.h, docs/feature-barga-aim.md) offline:
//  - the cannon's sweep (0x6B4190): a(t) = sum of begin + (end - begin) t^pow; V605's numbers (begin.x 0.75, end 0,
//    pow 0.25) give 0.75 rad at the burst's first round and none at its end; SweepDir turns a level direction down by
//    it (kSweepSign +1), keeps the yaw, gives +Z for a zero direction, and the burst's progress (0x6B4080) is
//    1 - left / count;
//  - the rounds still to come by the burst's state (0x6B4100 waiting: the whole burst; 0x6B4080 sweeping: those left, or
//    with none left and the trigger held the whole next one, along the old frozen direction) and the sampled ones (the
//    next and the last always in, in firing order);
//  - a hit sphere's centre (0x6BB420): offset (x, y, z, w) times the bone's rows;
//  - the combo's availability (0x60F570's rule) and the variant match over both generated tables: a jab opens from
//    idle only, a follow-up only after the attack it follows, a stomp needs flag 1, nothing while the input is not
//    accepted; a list one entry off matches none;
//  - the fists' picks by button (mask 1 left, 2 right; V515's maxBeat is a right-button move led by the left arm); the
//    idle rings (each jab's reach and sphere) in the view from the cockpit's eye (41.3 m over the feet, 6.3 m ahead, as
//    the 2026-11-27 run logged) and either side of its middle, apart (the arcs they replace read as one line);
//  - the off-screen chevron (ToEdge): on the inset border every way, the way to turn from the clip x, y (right behind
//    the camera still right), straight behind down.
// Exit code 1 when one fails. cmake --build build --target barga_aim_check && build\barga_aim_check.exe
#include "barga_aim.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace bargaaim;

namespace {
int failures=0,checks=0;
void Expect(bool ok,const char* what,float a=0.0f,float b=0.0f) {
    ++checks;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
bool Near(float a,float b,float eps=1e-4f) { return std::fabs(a-b)<=eps; }

int Find(const Variant& v,const char* name) {
    for(int i=0;i<v.count;++i)if(std::strcmp(v.attacks[i].name,name)==0)return i;
    return -1;
}

void Sweeps() {
    const Sweep s{{0.75f,0.0f,0.0f},{0.0f,0.0f,0.0f},0.25f};
    Expect(Near(SweepAngle(s,0.0f),0.75f),"sweep at t 0",SweepAngle(s,0.0f),0.75f);
    Expect(Near(SweepAngle(s,1.0f),0.0f),"sweep at t 1",SweepAngle(s,1.0f),0.0f);
    Expect(Near(SweepAngle(s,0.0625f),0.75f*0.5f),"sweep at t 1/16 (t^0.25 = 1/2)",SweepAngle(s,0.0625f),0.375f);
    const Sweep spread{{0.1f,0.2f,0.3f},{0.0f,0.0f,0.0f},1.0f};
    Expect(Near(SweepAngle(spread,0.0f),0.6f),"components add",SweepAngle(spread,0.0f),0.6f);
    Expect(Near(SweepAngle(spread,0.5f),0.3f),"linear at pow 1",SweepAngle(spread,0.5f),0.3f);
    for(int left=60;left>1;--left)
        Expect(SweepAngle(s,BurstT(left-1,60))<=SweepAngle(s,BurstT(left,60))+1e-6f,"the barrel only rises through the burst",
               static_cast<float>(left));

    Expect(Near(BurstT(60,60),0.0f),"first round t 0");
    Expect(Near(BurstT(30,60),0.5f),"half way t 0.5");
    Expect(Near(BurstT(0,60),1.0f),"none left t 1");
    Expect(Near(BurstT(5,0),0.0f),"no count t 0");

    const float level[3]={0.0f,0.0f,1.0f};
    float d[3];
    SweepDir(level,0.75f,d);
    Expect(Near(d[1],-std::sin(0.75f)*kSweepSign),"a positive angle turns a level barrel down",d[1],-std::sin(0.75f));
    Expect(Near(d[0],0.0f) && Near(d[2],std::cos(0.75f)),"keeps the heading",d[0],d[2]);
    const float east[3]={2.0f,0.0f,0.0f};   // not unit: only its heading counts
    SweepDir(east,0.0f,d);
    Expect(Near(d[0],1.0f) && Near(d[1],0.0f) && Near(d[2],0.0f),"zero angle: the frozen heading",d[0],d[2]);
    const float up30[3]={0.0f,std::sin(0.5f),std::cos(0.5f)};
    SweepDir(up30,0.5f,d);
    Expect(Near(d[1],0.0f) && Near(d[2],1.0f),"pitch less the sweep",d[1],d[2]);
    const float none[3]={0.0f,0.0f,0.0f};
    SweepDir(none,0.0f,d);
    Expect(Near(d[2],1.0f),"zero direction: +Z",d[2],1.0f);
    float len=0.0f;
    SweepDir(east,0.3f,d);
    for(float c:d)len+=c*c;
    Expect(Near(len,1.0f),"unit",len,1.0f);
}

void Rounds() {
    using S=SweepState;
    Expect(RoundsToCome(S::sweeping,17,60,false)==17,"sweeping: the rounds left");
    Expect(RoundsToCome(S::waiting,0,60,false)==60,"waiting: the whole burst");
    Expect(RoundsToCome(S::sweeping,90,60,false)==60,"clamped to the burst");
    Expect(RoundsToCome(S::sweeping,5,0,true)==0,"no burst");
    Expect(RoundsToCome(S::sweeping,0,60,true)==60,"none left, trigger held: the next burst again");
    Expect(RoundsToCome(S::sweeping,0,60,false)==0,"none left, let go: none");
    Expect(RoundsToCome(S::other,10,60,true)==0,"an unknown handler: none");
    Expect(FromFrozen(S::sweeping) && !FromFrozen(S::waiting) && !FromFrozen(S::other),"the frozen direction only while sweeping");
    int left[kMostRounds];
    Expect(SampleRounds(5,kMostRounds,left)==5 && left[0]==5 && left[4]==1,"few: each");
    const int n=SampleRounds(60,kMostRounds,left);
    Expect(n==kMostRounds,"many: at most kMostRounds",static_cast<float>(n));
    Expect(left[0]==60 && left[n-1]==1,"the next and the last",static_cast<float>(left[0]),static_cast<float>(left[n-1]));
    bool order=true;
    for(int i=1;i<n;++i)order=order && left[i]<left[i-1];
    Expect(order,"in firing order, no repeats");
    Expect(SampleRounds(0,kMostRounds,left)==0,"none to come");
    Expect(SampleRounds(9,1,left)==1 && left[0]==9,"one: the next");
}

void Spheres() {
    // Bone rows: right (0, 0, -1), up (0, 1, 0), forward (1, 0, 0), position (10, 20, 30).
    const float rows[16]={0,0,-1,0, 0,1,0,0, 1,0,0,0, 10,20,30,1};
    const float offset[4]={1.0f,2.0f,3.0f,1.0f};
    float c[3];
    SphereCentre(offset,rows,c);
    Expect(Near(c[0],13.0f) && Near(c[1],22.0f) && Near(c[2],29.0f),"offset times the rows",c[0],c[2]);
    const float free[4]={1.0f,2.0f,3.0f,0.0f};   // w 0: a direction, no position
    SphereCentre(free,rows,c);
    Expect(Near(c[0],3.0f) && Near(c[2],-1.0f),"w 0 leaves the position out",c[0],c[2]);
    float w[3];
    const float hull[16]={-1,0,0,0, 0,1,0,0, 0,0,-1,0, 100,5,200,1};   // turned round
    const float p[3]={2.0f,0.0f,10.0f};
    ModelToWorld(hull,p,w);
    Expect(Near(w[0],98.0f) && Near(w[1],5.0f) && Near(w[2],190.0f),"model to world",w[0],w[2]);
}

void Combo() {
    Expect(kVariantCount==2,"two variants");
    for(int vi=0;vi<kVariantCount;++vi) {
        const Variant& v=kVariants[vi];
        const int jabL=Find(v,"jab_l"),jabR=Find(v,"jab_r"),chop=Find(v,"chop_l"),straight=Find(v,"straight_r");
        const int stomp=Find(v,"stomp_l"),hook=Find(v,"longhook_l");
        Expect(jabL>=0 && jabR>=0 && chop>=0 && straight>=0 && stomp>=0 && hook>=0,v.file);
        Expect(Available(v,jabL,-1,true,0) && Available(v,jabR,-1,true,0),"jabs open from idle");
        Expect(!Available(v,jabL,jabR,true,0),"a jab not in a combo");
        Expect(!Available(v,jabL,-1,false,0),"nothing while not accepted");
        Expect(Available(v,chop,jabL,true,0) && !Available(v,chop,-1,true,0) && !Available(v,chop,jabR,true,0),
               "chop_l follows jab_l only");
        Expect(Available(v,straight,jabR,true,0),"straight_r follows jab_r");
        Expect(Available(v,hook,jabR,true,0) && Available(v,hook,Find(v,"longhook_r"),true,0),"longhook_l follows two");
        Expect(!Available(v,stomp,-1,true,0) && Available(v,stomp,-1,true,1),"a stomp needs flag 1");

        // The fists: idle, each side's jab may start; after jab_l the left's chop, the right none (only its base).
        FistPick l=PickFist(v,kLeft,-1,true,0),r=PickFist(v,kRight,-1,true,0);
        Expect(l.next==jabL && r.next==jabR && l.base==jabL && r.base==jabR,"idle: the jabs",static_cast<float>(l.next),static_cast<float>(r.next));
        l=PickFist(v,kLeft,jabL,true,0);r=PickFist(v,kRight,jabL,true,0);
        Expect(l.next==chop,"after jab_l: chop_l",static_cast<float>(l.next));
        Expect(r.next==Find(v,"longhook_r") && r.base==jabR,"after jab_l: longhook_r on the right",static_cast<float>(r.next));
        l=PickFist(v,kLeft,jabL,false,0);
        Expect(l.next<0 && l.base==jabL,"not accepted: none, the base dim");

        // The match: the variant's own prev lists, and none one entry off.
        int prevCount[32],prev[32][2]={};
        for(int i=0;i<v.count;++i){prevCount[i]=v.attacks[i].prevCount;prev[i][0]=v.attacks[i].prev[0];prev[i][1]=v.attacks[i].prev[1];}
        Expect(MatchVariant(v.count,prevCount,prev)==vi,"its own list matches",static_cast<float>(MatchVariant(v.count,prevCount,prev)));
        prev[chop][0]=jabR;
        Expect(MatchVariant(v.count,prevCount,prev)==-1,"one entry off: none");
        Expect(MatchVariant(v.count-1,prevCount,prev)==-1,"another count: none");
    }
    // The two lists differ (V605's maxBeat follows ground_charge at 15, V515's at 14), so each matches only itself.
    Expect(kVariants[0].cannon<0 && kVariants[1].cannon>=0,"only V605 has the cannon");
    Expect(std::strcmp(kVariants[1].attacks[kVariants[1].cannon].name,"attack_cannon")==0,"cannon index");
    Expect(std::strcmp(kVariants[1].attacks[kVariants[1].charge].name,"ground_charge")==0,"charge index");
    Expect(std::strcmp(kVariants[0].attacks[kVariants[0].charge].name,"ground_charge")==0,"V515 charge index");
}

void Fists() {
    // In the view from the cockpit: the eye the 2026-11-27 run logged ("eye at its cockpit (-295.45 42.27 158.07)", the
    // vehicle at (-301,1,155) heading (0.86,0,0.51): 41.3 m up, 6.3 m ahead). The view is pi/4 high (docs/zoom-re.md:
    // 0xF8906, fovY pi/4 over the zoom; the seat's camera is the soldier's), pi/8 either side of its middle. Looking level,
    // the idle rings' centres (the opening attacks' reach points, the jabs) are within it (a ring's lower edge may run
    // off it: V605's jab_r is 0.32 rad down, its ring 0.31 rad wide, drawn clipped by the screen). (The low follow-ups,
    // maxBeat's downward beat, land lower: in view as the player looks down, else the chevron points there.)
    constexpr float kEyeUp=41.3f,kEyeAhead=6.3f,kInView=3.14159265f/8.0f;
    float worst=0.0f;
    for(int vi=0;vi<kVariantCount;++vi)
        for(int i=0;i<kVariants[vi].count;++i) {
            const Attack& x=kVariants[vi].attacks[i];
            if(FistSide(x)<0 || x.limb<0 || x.prevCount!=0 || x.flag!=0)continue;
            const float ahead=x.reach[2]-kEyeAhead,flat=std::sqrt(x.reach[0]*x.reach[0]+ahead*ahead);
            worst=std::fmax(worst,std::fabs(std::atan2(x.reach[1]-kEyeUp,flat)));
        }
    Expect(worst<kInView,"the idle rings' centres in the level view from the eye (rad)",worst,kInView);
    // The two sides apart on the screen (the arcs they replace were two lines at one height: one line to the eye):
    // the jabs' bearings from the eye differ by more than their rings' width.
    for(int vi=0;vi<kVariantCount;++vi) {
        const Attack& l=kVariants[vi].attacks[Find(kVariants[vi],"jab_l")];
        const Attack& r=kVariants[vi].attacks[Find(kVariants[vi],"jab_r")];
        const float bl=std::atan2(l.reach[0],l.reach[2]-kEyeAhead),br=std::atan2(r.reach[0],r.reach[2]-kEyeAhead);
        const float half=std::asin(l.radius/std::sqrt(l.reach[0]*l.reach[0]+(l.reach[2]-kEyeAhead)*(l.reach[2]-kEyeAhead)));
        Expect(bl>0.0f && br<0.0f && bl-br>half,"the jabs' rings either side of the middle",bl,br);
    }
}

void Edges() {
    constexpr float kW=1280.0f,kH=720.0f,kIn=30.0f;
    auto on=[&](const EdgeMark& e,float x,float y,const char* what){Expect(Near(e.x,x,0.5f) && Near(e.y,y,0.5f),what,e.x,e.y);};
    on(ToEdge(2.0f,0.0f,kW,kH,kIn),kW-kIn,kH*0.5f,"right: the right border's middle");
    on(ToEdge(-2.0f,0.0f,kW,kH,kIn),kIn,kH*0.5f,"left");
    on(ToEdge(0.0f,3.0f,kW,kH,kIn),kW*0.5f,kIn,"up (clip y up, screen y down)");
    on(ToEdge(0.0f,0.0f,kW,kH,kIn),kW*0.5f,kH-kIn,"straight behind: down");
    // Behind and to the right (w < 0 with cx > 0): still the right; dividing by w would have put it left.
    const auto behind=ToEdge(0.5f,-0.1f,kW,kH,kIn);
    Expect(behind.x>kW*0.5f && behind.dx>0.0f,"behind and right: right",behind.x,behind.dx);
    // The way is the screen's: a clip (1, 1) is the corner's direction, (w/2, -h/2) in pixels.
    const auto corner=ToEdge(1.0f,1.0f,kW,kH,kIn);
    Expect(Near(corner.dx*kH,-corner.dy*kW,1e-3f) && corner.dx>0.0f,"the corner's direction",corner.dx,corner.dy);
    Expect(corner.x<=kW-kIn+0.5f && corner.y>=kIn-0.5f && (Near(corner.x,kW-kIn,0.5f) || Near(corner.y,kIn,0.5f)),
           "on the inset border",corner.x,corner.y);
    for(int a=0;a<36;++a) {
        const float ang=6.2831853f*static_cast<float>(a)/36.0f;
        const auto e=ToEdge(std::cos(ang),std::sin(ang),kW,kH,kIn);
        const bool inside=e.x>=kIn-0.5f && e.x<=kW-kIn+0.5f && e.y>=kIn-0.5f && e.y<=kH-kIn+0.5f;
        const bool border=Near(e.x,kIn,0.5f) || Near(e.x,kW-kIn,0.5f) || Near(e.y,kIn,0.5f) || Near(e.y,kH-kIn,0.5f);
        Expect(inside && border && Near(e.dx*e.dx+e.dy*e.dy,1.0f,1e-4f),"every way: on the border, a unit direction",e.x,e.y);
    }
}

void Picks() {
    // The reaches are ahead of the feet and over them (the tables are the model's).
    for(int vi=0;vi<kVariantCount;++vi)
        for(int i=0;i<kVariants[vi].count;++i) {
            const Attack& x=kVariants[vi].attacks[i];
            if(x.limb<0)continue;
            Expect(x.reach[2]>5.0f && x.reach[1]>0.0f && x.radius>0.0f,x.name,x.reach[2],x.reach[1]);
        }
    // The sides by button: every fist attack's mask is 1 or 2 and FistSide follows it; V515's maxBeat (mask 2, led by
    // the left arm) is on the right, as V605's.
    for(int vi=0;vi<kVariantCount;++vi) {
        const Variant& v=kVariants[vi];
        for(int i=0;i<v.count;++i) {
            const Attack& x=v.attacks[i];
            if(x.limb==kLeft || x.limb==kRight)
                Expect((x.mask==kLeftButton && FistSide(x)==kLeft) || (x.mask==kRightButton && FistSide(x)==kRight),x.name,
                       static_cast<float>(x.mask),static_cast<float>(FistSide(x)));
        }
        Expect(FistSide(v.attacks[Find(v,"maxBeat")])==kRight,"maxBeat on the right button");
        Expect(FistSide(v.attacks[Find(v,"stomp_l")])<0 && FistSide(v.attacks[Find(v,"ground_charge")])<0,"no fist side");
    }
    {
        const Variant& v=kVariants[0];
        const int charge=v.charge,beat=Find(v,"maxBeat");
        const FistPick l=PickFist(v,kLeft,charge,true,0),r2=PickFist(v,kRight,charge,true,0);
        Expect(r2.next==beat,"after ground_charge: maxBeat lights the right",static_cast<float>(r2.next));
        Expect(l.next==Find(v,"maxHook_l"),"and maxHook_l the left",static_cast<float>(l.next));
    }
    // The left jab on the left hand's side (+X), the right on the other.
    for(int vi=0;vi<kVariantCount;++vi) {
        const Variant& v=kVariants[vi];
        Expect(v.attacks[Find(v,"jab_l")].reach[0]>0.0f && v.attacks[Find(v,"jab_r")].reach[0]<0.0f,"jab sides");
    }
    bool named=true;
    for(const auto& g:kHitGroups)named=named && g.name && g.name[0];
    Expect(named,"hit groups named");
}
}  // namespace

int main() {
    Sweeps();
    Rounds();
    Spheres();
    Combo();
    Fists();
    Edges();
    Picks();
    std::printf("%d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
