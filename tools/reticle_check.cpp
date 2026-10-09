// The sights' styles (src/reticle.h), offline:
//  - Classify on the real vehicle weapons (kWeapons: their numbers as Root.cpk's SGO has them; tests/sight_data_audit.py
//    checks every row against the game's archive): a tank's main gun a cannon sight (AP for a kinetic round, HE for one
//    that bursts, BEAM for an energy gun), the 502's sniper and the Begaruta's scoped long cannons a precision sight, the
//    machine guns, gatlings and the Striker's quick cannon a ring sight, the flak's guns the anti-aircraft ring sight, a
//    quick laser the beam cross, missiles their gate, the rockets, grenades and flamethrowers no gun sight;
//  - the geometry each style draws on a known projection (focal f px per unit tan): the lead rings at f x target / round
//    speed, the mil marks every f tan(m / 1000) (doubled until they stand kMilApart apart), the duplex's heavy posts
//    outside its thin cross, the cannon's chevron tip on the aim point and its ladder numbered downward in range order,
//    the gate's brackets round the bore; the turn the focal length is measured by exactly the angle asked;
//  - each style's scope posts (Spec).
// Exit code 1 when one fails. cmake --build build --target reticle_check && build\reticle_check.exe
#include "../src/reticle.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace crew;
using reticle::Style;using reticle::Shell;
int failures=0;

void Expect(bool ok,const char* what,double a=0.0,double b=0.0) {
    std::printf("%s  %s (%.3f, %.3f)\n",ok ? "ok  " : "FAIL",what,a,b);
    if(!ok)++failures;
}

// A vehicle weapon's SGO numbers (Root.cpk WEAPON/<sgo>): AmmoClass, LockonType, FireInterval (frames), AmmoSpeed
// (m/frame), AmmoExplosion (m), SecondaryFire_Type; the vehicle class it is mounted on (crew.cpp kClasses); the sight
// it must get. tests/sight_data_audit.py reads these rows and checks the numbers against the game's own files.
struct Weapon { const char* sgo; const char* ammo; int lock,interval; float speed,blast; int scope; const char* vehicle; Style style; Shell shell; };
const Weapon kWeapons[]={
    {"V_403TANK_CANNON02.SGO","SolidBullet01Rail",0,180,22.5f,0.0f,0,"403_Tank",Style::cannon,Shell::ap},
    {"V_403TANK_MACHINEGUN.SGO","SolidBullet01",0,4,6.0f,0.0f,0,"403_Tank",Style::autocannon,Shell::ap},
    {"V_404BIGTANK_MAINCANNON.SGO","RocketBullet01",0,300,8.0f,30.0f,0,"404_Tank",Style::cannon,Shell::he},
    {"V_404BIGTANK_SUBCANNONSOLID.SGO","SolidBullet01Rail",0,120,6.0f,0.0f,0,"404_Tank",Style::cannon,Shell::ap},
    {"V_404BIGTANK_GATLING.SGO","SolidBullet01",0,3,4.0f,0.0f,0,"404_Tank",Style::autocannon,Shell::ap},
    {"V_404BIGTANK_SIDEMISSILE_L.SGO","MissileBullet01",1,720,0.1f,8.0f,0,"404_Tank",Style::missile,Shell::none},
    {"V_404BIGTANK_SIDEGRENADE_L.SGO","GrenadeBullet01",0,80,0.5f,18.0f,0,"404_Tank",Style::none,Shell::none},
    {"V_505TANK_CANNON04L.SGO","RocketBullet01",0,120,11.0f,15.0f,0,"505_Tank",Style::cannon,Shell::he},
    {"V_505TANK_CANNON02S.SGO","SolidBullet01Rail",0,100,24.0f,0.0f,0,"505_Tank",Style::cannon,Shell::ap},
    {"V601_TANK_CANNON01.SGO","EfsBullet",0,150,10.0f,12.0f,0,"601_Tank",Style::cannon,Shell::beam},
    {"V_401STRIKER_CANNON.SGO","RocketBullet01",0,20,4.0f,5.0f,0,"VehicleBase",Style::autocannon,Shell::he},
    {"V603_FLAK_GUN02_L.SGO","SolidBullet01",0,3,6.0f,0.0f,0,"603_Flak",Style::flak,Shell::ap},
    {"V603_FLAK_GUNH01_DLC_L.SGO","SolidBullet01",0,3,16.0f,0.0f,0,"603_Flak",Style::flak,Shell::ap},
    {"V603_FLAK_GLGUN01_DLC_L.SGO","GrenadeBullet01_MapNoDamage",0,5,5.0f,4.0f,0,"603_Flak",Style::none,Shell::none},
    {"V_502_GROUNDROBO_SNIPE01_L.SGO","SolidBullet01",0,120,20.0f,0.0f,0,"502_GroundRobo",Style::precision,Shell::ap},
    {"V_502_GROUNDROBO_GATLING.SGO","SolidBullet01",0,3,4.0f,0.0f,0,"502_GroundRobo",Style::autocannon,Shell::ap},
    {"V_502_GROUNDROBO_FLAMEGUN01_L.SGO","FlameBullet02",0,0,1.4f,0.0f,0,"502_GroundRobo",Style::none,Shell::none},
    {"V_504BEGARUTA_DLC_LONGCANNON015_L.SGO","LaserBullet01",0,80,30.0f,0.0f,1,"504_begaruta",Style::precision,Shell::beam},
    {"V_504BEGARUTA_DLC2_LONGCANNON015_L.SGO","RocketBullet01",0,80,20.0f,30.0f,1,"504_begaruta",Style::precision,Shell::he},
    {"V_504BEGARUTA_LONGCANNON01_L.SGO","RocketBullet01",0,160,10.0f,20.0f,0,"504_begaruta",Style::cannon,Shell::he},
    {"V_504BEGARUTA_ROCKET01_L.SGO","MissileBullet01",0,80,0.1f,8.0f,0,"504_begaruta",Style::rocket,Shell::none},
    {"V_612_A_LASERRIFLE_DLC2_L.SGO","LaserBullet01",0,10,120.0f,0.0f,0,"612_nix",Style::energy,Shell::beam},
    {"V_612_LASER_CANNON01_R.SGO","LaserBullet01",0,80,30.0f,0.0f,1,"612_nix",Style::precision,Shell::beam},
    {"V_612_HOMING_LASER01_L.SGO","HomingLaserBullet01",1,120,4.0f,2.0f,0,"612_nix",Style::missile,Shell::none},
};

// What vhud.cpp reads for a weapon (rounds.cpp's class table: its label, its kind; LockonType 1 a homing missile).
reticle::Traits TraitsOf(const Weapon& w) {
    reticle::Traits t{};
    const char* a=w.ammo;
    const bool missile=!std::strcmp(a,"MissileBullet01");
    t.homing=(missile && w.lock!=0) || !std::strcmp(a,"MissileBullet02") || !std::strcmp(a,"HomingLaserBullet01");
    t.rocket=missile && w.lock==0;
    t.lobbed=!std::strncmp(a,"GrenadeBullet01",15) || !std::strcmp(a,"NapalmBullet01");
    t.energy=!std::strcmp(a,"LaserBullet01") || !std::strcmp(a,"EfsBullet") || !std::strcmp(a,"EfsExposureBullet");
    t.sprayer=!std::strcmp(a,"FlameBullet02") || !std::strcmp(a,"AcidBullet01") || !std::strcmp(a,"NapalmBullet01");
    t.shellClass=!std::strcmp(a,"SolidBullet01Rail") || !std::strcmp(a,"RocketBullet01") || !std::strcmp(a,"SolidExpBullet01");
    t.scoped=w.scope==1;t.antiAir=reticle::AntiAirClass(w.vehicle);
    t.interval=w.interval;t.speed=w.speed*60.0f;t.blast=w.blast;
    return t;
}

void Classes() {
    for(const Weapon& w:kWeapons) {
        const reticle::Traits t=TraitsOf(w);
        const Style got=reticle::Classify(t);const Shell shell=reticle::ShellOf(t);
        char what[128];std::snprintf(what,sizeof(what),"%s: style %d shell %d",w.sgo,static_cast<int>(got),static_cast<int>(shell));
        Expect(got==w.style && shell==w.shell,what,static_cast<double>(w.style),static_cast<double>(w.shell));
    }
}

int Count(const reticle::Sketch& k,float w) {
    int n=0;
    for(int i=0;i<k.strokes;++i)n+=std::fabs(k.stroke[i].w-w)<1e-4f;
    return n;
}
bool HasNote(const reticle::Sketch& k,const wchar_t* text) {
    for(int i=0;i<k.notes;++i)if(!std::wcscmp(k.note[i].text,text))return true;
    return false;
}

void Geometry() {
    const float f=1037.0f;   // px a unit of tan: 1080 lines, a 55 degree vertical view
    const reticle::View v{960.0f,540.0f,f,1.0f,0.6f};
    // The focal length's probe turn.
    {
        const float raw[][3]={{0.0f,0.0f,1.0f},{0.6f,0.3f,0.7416f},{-0.2f,-0.5f,0.8426f}};
        for(const auto& r:raw) {
            const float n=std::sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]),b[3]={r[0]/n,r[1]/n,r[2]/n};   // a unit bore, as MuzzleFrame's
            float t[3];
            const bool ok=reticle::Turned(b,0.01f,t);
            const float dot=b[0]*t[0]+b[1]*t[1]+b[2]*t[2];
            Expect(ok && std::fabs(std::acos(std::fmin(dot,1.0f))-0.01f)<2e-4f && std::fabs(t[1]-b[1]*std::cos(0.01f))<1e-5f,
                   "the probe turn is the angle asked, level",std::acos(std::fmin(dot,1.0f)),0.01);
        }
        const float up[3]={0.0f,1.0f,0.0f};float t[3];
        Expect(!reticle::Turned(up,0.01f,t),"no level turn for a bore straight up");
    }
    // The flak rings: f x target / round speed (V603_FLAK_GUN02: 6 m/frame = 360 m/s).
    {
        reticle::Aim a{};a.style=Style::flak;a.speed=360.0f;
        reticle::Sketch k{};reticle::Draw(a,v,&k);
        Expect(k.circles==2,"the flak sight: a ring for each crossing speed",k.circles,2);
        for(int i=0;i<k.circles && i<2;++i)
            Expect(std::fabs(k.circle[i].r-f*reticle::kFlakTargets[i]/360.0f)<0.01f && k.circle[i].x==v.cx && k.circle[i].y==v.cy,
                   "  its lead ring at f x target / round speed",k.circle[i].r,f*reticle::kFlakTargets[i]/360.0f);
        Expect(HasNote(k,L"90") && HasNote(k,L"180") && HasNote(k,L"AA"),"  rings named 90 / 180 km/h, the sight AA");
        // Zoomed 3x: three times as far out; a round too fast for the first ring to show (its ring under 14 px) gives one.
        const reticle::View z{960.0f,540.0f,3.0f*f,1.0f,0.6f};
        reticle::Sketch kz{};reticle::Draw(a,z,&kz);
        Expect(kz.circles==2 && std::fabs(kz.circle[0].r-3.0f*k.circle[0].r)<0.05f,"  3x: the rings three times as far",kz.circle[0].r,3.0f*k.circle[0].r);
        a.speed=3000.0f;reticle::Sketch kf{};reticle::Draw(a,v,&kf);
        Expect(kf.circles==1,"  a round too fast to lead visibly: only the ring that shows",kf.circles,1);
        a.speed=0.0f;reticle::Sketch k0{};reticle::Draw(a,v,&k0);
        Expect(k0.circles==0,"  no round speed: no rings",k0.circles,0);
    }
    // The mils: spaced f tan(m/1000), doubled until 8 px apart.
    {
        Expect(reticle::MilStep(v,1.0f,500.0f)==8.0f,"mil dots at 1x: every 8 mils (8.3 px)",reticle::MilStep(v,1.0f,500.0f),8);
        const reticle::View z{960.0f,540.0f,6.0f*f,1.0f,0.6f};
        Expect(reticle::MilStep(z,1.0f,500.0f)==2.0f,"mil dots at 6x: every 2 mils (12.4 px)",reticle::MilStep(z,1.0f,500.0f),2);
        Expect(reticle::MilStep(reticle::View{0,0,0,1,0.6f},1.0f,500.0f)==0.0f,"no focal: no mils");
        reticle::Aim a{};a.style=Style::precision;a.shell=Shell::ap;
        reticle::Sketch k{};reticle::Draw(a,z,&k);
        const float step=z.focal*std::tan(0.002f);
        bool spaced=k.dots>0;
        const float inner=reticle::kDuplex*reticle::kDuplexThin;
        for(int i=0;i<k.dots;++i) {
            const float d=std::fabs(k.dot[i].x-z.cx)+std::fabs(k.dot[i].y-z.cy);
            const float n=d/step;
            spaced=spaced && std::fabs(n-std::round(n))<0.02f && d<=inner+0.01f;
        }
        Expect(spaced,"  the duplex's mil dots on the mil and inside its thin part",k.dots,0);
        // The duplex: four heavy posts outside, a thin cross inside.
        Expect(Count(k,5.0f)==4 && Count(k,1.2f)==4,"  the duplex: four heavy posts, a thin cross of four",Count(k,5.0f),Count(k,1.2f));
        bool outside=true;
        for(int i=0;i<k.strokes;++i)if(k.stroke[i].w==5.0f) {
            const auto& s=k.stroke[i];
            const float n=std::fmin(std::fabs(s.x0-z.cx)+std::fabs(s.y0-z.cy),std::fabs(s.x1-z.cx)+std::fabs(s.y1-z.cy));
            outside=outside && std::fabs(n-inner)<0.01f;
        }
        Expect(outside,"  the heavy posts start where the thin cross ends");
    }
    // The cannon: the chevron's tip on the aim point; lead stadia ticks every 5 mils; the ladder numbered downward.
    {
        reticle::Aim a{};a.style=Style::cannon;a.shell=Shell::he;a.step=500.0f;
        const float ys[]={570.0f,600.0f,650.0f,720.0f};
        for(int i=0;i<4;++i)a.tick[a.ticks++]=reticle::Mark{960.0f,ys[i],500.0f*(i+1)};
        const reticle::View z{960.0f,540.0f,3.0f*f,1.0f,0.6f};
        reticle::Sketch k{};reticle::Draw(a,z,&k);
        bool tip=false;
        for(int i=0;i<k.strokes;++i)tip=tip || (k.stroke[i].x1==z.cx && k.stroke[i].y1==z.cy);
        Expect(tip,"the cannon's chevron tip on the aim point");
        const float five=z.focal*std::tan(0.005f);
        int ticks=0;bool onMil=true;
        for(int i=0;i<k.strokes;++i) {
            const auto& s=k.stroke[i];
            if(s.x0!=s.x1 || s.y0!=z.cy || std::fabs(s.x0-z.cx)<reticle::kStadiaIn || std::fabs(s.x0-z.cx)>reticle::kStadiaOut-0.5f)continue;
            ++ticks;const float n=std::fabs(s.x0-z.cx)/five;onMil=onMil && std::fabs(n-std::round(n))<0.02f;
        }
        Expect(ticks>=4 && onMil,"  its lead ticks every 5 mils (3x: 15.6 px)",ticks,five);
        Expect(HasNote(k,L"HE") && HasNote(k,L"5") && HasNote(k,L"10") && HasNote(k,L"15") && HasNote(k,L"20"),"  the round HE, the ladder 5..20 hundreds");
        float last=-1.0f;bool down=true;
        for(int i=0;i<k.notes;++i){int r=0;if(swscanf_s(k.note[i].text,L"%d",&r)==1){down=down && k.note[i].y>last;last=k.note[i].y;}}
        Expect(down,"  farther ranges further down");
        a.shell=Shell::beam;reticle::Sketch kb{};reticle::Draw(a,z,&kb);
        Expect(!HasNote(kb,L"5") && HasNote(kb,L"BEAM"),"  an energy gun: no drop ladder, BEAM");
    }
    // The ladder's numbers: 250 m steps keep the half hundreds, 50 m steps metres.
    {
        reticle::Aim a{};a.step=250.0f;a.tick[0]={960,600,250};a.tick[1]={960,640,750};a.ticks=2;
        reticle::Sketch k{};reticle::Ladder(k,v,a,960,560,reticle::kLadderTick);
        Expect(HasNote(k,L"2.5") && HasNote(k,L"7.5"),"a 250 m ladder says 2.5 / 7.5");
        a.step=50.0f;a.tick[0]={960,600,50};a.tick[1]={960,640,100};
        reticle::Sketch m{};reticle::Ladder(m,v,a,960,560,reticle::kLadderTick);
        Expect(HasNote(m,L"50") && HasNote(m,L"100"),"a 50 m ladder says metres");
        a.tick[1]={960,602,100};
        reticle::Sketch c{};const int n=reticle::Ladder(c,v,a,960,560,reticle::kLadderTick);
        Expect(n==1,"a tick within kLadderApart of the last left out",n,1);
    }
    // The ring sight; the seeker gate.
    {
        reticle::Aim a{};a.style=Style::autocannon;a.shell=Shell::ap;
        reticle::Sketch k{};reticle::Draw(a,v,&k);
        Expect(k.circles==1 && k.circle[0].r==reticle::kRing && k.dots==1 && k.dot[0].x==v.cx,"the machine gun's ring and centre dot");
        reticle::Sketch g{};reticle::Gate(g,v);
        bool within=g.strokes==12;
        for(int i=0;i<g.strokes;++i)
            within=within && std::fabs(g.stroke[i].x0-v.cx)<=reticle::kGate && std::fabs(g.stroke[i].y0-v.cy)<=reticle::kGate &&
                   g.stroke[i].ink==reticle::Ink::dim;
        Expect(within,"the seeker gate: four corner brackets and a cross round the bore, dim",g.strokes,12);
        reticle::Sketch e{};a.style=Style::energy;reticle::Draw(a,v,&e);
        Expect(e.circles==0 && HasNote(e,L"BEAM") && e.strokes==8,"the beam's open cross and diamond",e.strokes,8);
    }
    // The scope's posts by style.
    Expect(reticle::SpecOf(Style::cannon).posts==reticle::Posts::three && reticle::SpecOf(Style::precision).posts==reticle::Posts::duplex &&
           reticle::SpecOf(Style::autocannon).posts==reticle::Posts::none && reticle::SpecOf(Style::flak).posts==reticle::Posts::none,
           "scope posts: tank three, duplex four, ring sights none");
    Expect(!reticle::GunStyle(Style::none) && !reticle::GunStyle(Style::missile) && !reticle::GunStyle(Style::rocket) && reticle::GunStyle(Style::flak),
           "gun reticles only for guns");
    Expect(std::fabs(reticle::LeadAngle(25.0f,360.0f)-std::atan(25.0f/360.0f))<1e-6f && reticle::LeadAngle(25.0f,0.0f)==0.0f,"lead angle atan(target / round)");
}
}  // namespace

int main() {
    Classes();
    Geometry();
    std::printf("%s: %d failed\n",failures ? "FAIL" : "PASS",failures);
    return failures ? 1 : 0;
}
