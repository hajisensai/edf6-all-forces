// The chase follower (src/chase_cam.h) offline: frame-rate independence, the critically damped spring's lag and overshoot,
// the pause (dt 0), teleports, refused NaNs, the roll's rows and sign (cockpit_instruments.h BankOf), shortest arcs.
#include "../src/chase_cam.h"
#include "../src/cockpit_instruments.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
float Dist(const float* a,const float* b) {
    return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}

// A path of straight legs with its corners at multiples of 1/6 s (every rate tested steps on them): a run, a hard
// turn, a climb, a stop.
void Path(double t,float* p) {
    struct Corner { double t; float p[3]; };
    static const Corner c[]={{0.0,{0,0,0}},{1.0/3.0,{6,0,0}},{2.0/3.0,{6,0,9}},{1.0,{2,3,11}},{4.0/3.0,{2,3,11}},{2.0,{-10,0,11}}};
    const int n=static_cast<int>(sizeof(c)/sizeof(c[0]));
    if(t>=c[n-1].t){std::memcpy(p,c[n-1].p,12);return;}
    for(int i=1;i<n;++i)if(t<=c[i].t) {
        const float k=static_cast<float>((t-c[i-1].t)/(c[i].t-c[i-1].t));
        for(int j=0;j<3;++j)p[j]=c[i-1].p[j]+(c[i].p[j]-c[i-1].p[j])*k;
        return;
    }
}

// The follower's point at each 1/6 s over 2 s, stepped `hz` times a second along Path.
std::vector<float> Run(const chase::Tuning& t,int hz) {
    chase::Spring s;
    std::vector<float> out;
    float p[3],at[3];
    Path(0.0,p);chase::Follow(s,t,p,0.0f,at);
    const int perSixth=hz/6;
    for(int i=1;i<=2*hz;++i) {
        Path(static_cast<double>(i)/hz,p);
        chase::Follow(s,t,p,1.0f/static_cast<float>(hz),at);
        if(hz%6==0 ? i%perSixth==0 : false)out.insert(out.end(),at,at+3);
    }
    return out;
}

void FrameRate() {
    for(float feed:{0.0f,0.5f,1.0f}) {
        chase::Tuning t=chase::kGround;t.feed=feed;
        const std::vector<float> ref=Run(t,240);
        float worst=0.0f;
        for(int hz:{30,60,144}) {
            const std::vector<float> r=Run(t,hz);
            Check(r.size()==ref.size(),"every rate samples the same 1/6 s points");
            for(std::size_t i=0;i+2<r.size() && i+2<ref.size();i+=3)worst=std::fmax(worst,Dist(&r[i],&ref[i]));
        }
        char what[96];std::snprintf(what,sizeof(what),"30/60/144/240 Hz follow one path within 1 mm (feed %.1f: %.6f m)",feed,worst);
        Check(worst<1e-3f,what);
    }
    // A step: the rates' first steps take it over a different time; 2 s on they agree.
    float end[4][3];int k=0;
    for(int hz:{30,60,144,240}) {
        chase::Spring s;float at[3];const float zero[3]={0,0,0},far[3]={15,-3,4};
        chase::Follow(s,chase::kGround,zero,0.0f,at);
        for(int i=1;i<=hz/2;++i)chase::Follow(s,chase::kGround,zero,1.0f/hz,at);
        for(int i=1;i<=2*hz;++i)chase::Follow(s,chase::kGround,far,1.0f/hz,at);
        std::memcpy(end[k++],at,12);
    }
    Check(Dist(end[0],end[3])<1e-3f && Dist(end[1],end[3])<1e-3f && Dist(end[2],end[3])<1e-3f,"a step: every rate settles on the same point");
}

void Overshoot() {
    for(float feed:{0.0f,0.5f}) {
        chase::Tuning t=chase::kGround;t.feed=feed;
        chase::Spring s;float at[3];const float zero[3]={0,0,0},target[3]={10,0,0};
        chase::Follow(s,t,zero,0.0f,at);
        bool monotone=true;float was=0.0f,settled=-1.0f;
        for(int i=1;i<=240;++i) {
            chase::Follow(s,t,target,1.0f/60.0f,at);
            monotone=monotone && at[0]>=was-1e-6f && at[0]<=10.0f+1e-4f;
            if(settled<0.0f && 10.0f-at[0]<0.1f)settled=static_cast<float>(i)/60.0f;
            was=at[0];
        }
        Check(monotone,feed==0.0f ? "critically damped: a step is reached without overshoot (feed 0)" : "... nor with half the velocity fed (feed 0.5)");
        Check(settled>0.0f && settled<=7.0f/t.omega,"within 1% in 7 / omega");
    }
}

void SteadyLag() {
    const float v=12.0f;
    for(float feed:{0.0f,0.5f,1.0f}) {
        chase::Tuning t=chase::kGround;t.feed=feed;
        chase::Spring s;float at[3],p[3]={0,0,0};
        chase::Follow(s,t,p,0.0f,at);
        for(int i=1;i<=180;++i){p[0]=v*i/60.0f;chase::Follow(s,t,p,1.0f/60.0f,at);}
        const float lag=p[0]-at[0],want=2.0f*(1.0f-feed)*v/t.omega;
        char what[96];std::snprintf(what,sizeof(what),"steady lag 2 (1 - feed) v / omega (feed %.1f: %.4f m, want %.4f)",feed,lag,want);
        Check(std::fabs(lag-want)<1e-3f,what);
        Check(std::fabs(s.targetVel[0]-v)<1e-3f,"the target's velocity over the step is measured");
    }
    // feed 1: an acceleration's lag a / omega^2 only
    chase::Tuning t=chase::kGround;t.feed=1.0f;
    chase::Spring s;float at[3],p[3]={0,0,0};
    chase::Follow(s,t,p,0.0f,at);
    const float a=6.0f;
    for(int i=1;i<=240*3;++i){const float time=i/240.0f;p[0]=0.5f*a*time*time;chase::Follow(s,t,p,1.0f/240.0f,at);}
    Check(std::fabs((p[0]-at[0])-a/(t.omega*t.omega))<2e-3f,"feed 1: an acceleration lags a / omega^2");
}

void Pause() {
    chase::Spring s;float at[3],p[3]={0,0,0};
    chase::Follow(s,chase::kGround,p,0.0f,at);
    for(int i=1;i<=20;++i){p[2]=0.3f*i;chase::Follow(s,chase::kGround,p,1.0f/60.0f,at);}
    const chase::Spring before=s;float first[3];std::memcpy(first,at,12);
    bool same=true;
    for(int i=0;i<5;++i) {
        Check(chase::Follow(s,chase::kGround,p,0.0f,at)==chase::Step::held,"dt 0 holds");
        same=same && std::memcmp(at,first,12)==0 && std::memcmp(&s,&before,sizeof(s))==0;
    }
    Check(same,"dt 0 (the pause's camera steps) is idempotent: no integration, the last point");
    chase::Follow(s,chase::kGround,p,-0.01f,at);
    Check(std::memcmp(&s,&before,sizeof(s))==0,"a negative step integrates nothing either");
    float next[3];chase::Follow(s,chase::kGround,p,1.0f/60.0f,next);
    Check(Dist(next,first)<0.3f,"after the pause the first step does not jump");
}

void Snap() {
    chase::Spring s;float at[3],p[3]={0,0,0};
    chase::Follow(s,chase::kGround,p,0.0f,at);
    for(int i=1;i<=10;++i){p[0]=0.5f*i;chase::Follow(s,chase::kGround,p,1.0f/60.0f,at);}
    const float far[3]={500,20,-300};
    Check(chase::Follow(s,chase::kGround,far,1.0f/60.0f,at)==chase::Step::snapped && Dist(at,far)==0.0f &&
          s.vel[0]==0.0f && s.vel[1]==0.0f && s.vel[2]==0.0f,"a teleport past snapDist: on the target at once, still");
    Check(chase::Follow(s,chase::kGround,p,chase::kMostDt+0.01f,at)==chase::Step::snapped && Dist(at,p)==0.0f,"a gap over kMostDt starts over");
}

void Nan() {
    chase::Spring s;float at[3],p[3]={1,2,3};
    chase::Follow(s,chase::kGround,p,0.0f,at);
    p[0]=4;chase::Follow(s,chase::kGround,p,1.0f/60.0f,at);
    const chase::Spring before=s;
    const float bad[3]={1,NAN,3};
    Check(chase::Follow(s,chase::kGround,bad,1.0f/60.0f,at)==chase::Step::refused && std::memcmp(&s,&before,sizeof(s))==0,
          "a NaN target is refused and leaves the state");
    Check(chase::Follow(s,chase::kGround,p,NAN,at)==chase::Step::refused && std::memcmp(&s,&before,sizeof(s))==0,"a NaN step too");
    Check(std::isfinite(at[0]+at[1]+at[2]),"... and the point handed back is the last good one");
    chase::Chase c;chase::Placement out{};
    float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float eye[3]={0,3,-20},look[3]={0,1,10};
    chase::FollowCraft(c,chase::kJet,m,eye,look,0.0f,&out);
    const chase::Chase was=c;
    m[13]=NAN;
    Check(!chase::FollowCraft(c,chase::kJet,m,eye,look,1.0f/60.0f,&out) && std::memcmp(&c,&was,sizeof(c))==0,"a NaN craft matrix is refused");
    m[13]=0;
    Check(!chase::FollowAnchor(c,chase::kHeli,m,bad,look,1.0f/60.0f,&out) && std::memcmp(&c,&was,sizeof(c))==0,"a NaN eye is refused");
}

// A camera's rows as the native LookTo leaves them (level) along `forward`.
void NativeRows(const float* forward,float* rows) {
    float f[3]={forward[0],forward[1],forward[2]};
    const float l=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    for(float& x:f)x/=l;
    float left[3],up[3];
    chase::LevelFrame(f,left,up);
    std::memset(rows,0,64);
    for(int i=0;i<3;++i){rows[i]=left[i];rows[4+i]=up[i];rows[8+i]=f[i];}
    rows[15]=1.0f;
}
bool Orthonormal(const float* r) {
    const auto dot=[&](int a,int b){return r[a]*r[b]+r[a+1]*r[b+1]+r[a+2]*r[b+2];};
    float cross[3];   // left = up x forward
    cross[0]=r[5]*r[10]-r[6]*r[9];cross[1]=r[6]*r[8]-r[4]*r[10];cross[2]=r[4]*r[9]-r[5]*r[8];
    return std::fabs(dot(0,0)-1)<1e-5f && std::fabs(dot(4,4)-1)<1e-5f && std::fabs(dot(8,8)-1)<1e-5f && std::fabs(dot(0,4))<1e-5f &&
           std::fabs(dot(0,8))<1e-5f && std::fabs(dot(4,8))<1e-5f && std::fabs(cross[0]-r[0])<1e-5f && std::fabs(cross[1]-r[1])<1e-5f &&
           std::fabs(cross[2]-r[2])<1e-5f;
}
// A craft's matrix heading +z, banked `bank` deg (BankOf's sense: right wing down positive), pitched `pitch` deg up.
void Craft(float bank,float pitch,const float* at,float* m) {
    const float b=bank*chase::kPi/180.0f,p=pitch*chase::kPi/180.0f;
    const float f[3]={0,std::sin(p),std::cos(p)},level[3]={1,0,0},up0[3]={0,std::cos(p),-std::sin(p)};
    std::memset(m,0,64);
    for(int i=0;i<3;++i){m[i]=level[i]*std::cos(b)+up0[i]*std::sin(b);m[4+i]=up0[i]*std::cos(b)-level[i]*std::sin(b);m[8+i]=f[i];m[12+i]=at[i];}
    m[15]=1.0f;
}
// The picture's bank (deg) once the chase of a still craft has settled: the craft's eye straight behind it.
float SettledBank(const chase::Tuning& t,float bank,float pitch,float* eyeOut=nullptr,const float* offset=nullptr) {
    const float at[3]={100,50,-30};float m[16];Craft(bank,pitch,at,m);
    const float local[3]={offset ? offset[0] : 0.0f,offset ? offset[1] : 0.0f,offset ? offset[2] : -20.0f};
    float eye[3],look[3];
    for(int i=0;i<3;++i){eye[i]=at[i]+m[i]*local[0]+m[4+i]*local[1]+m[8+i]*local[2];look[i]=at[i]+m[8+i]*15.0f;}
    chase::Chase c;chase::Placement out{};
    for(int i=0;i<=300;++i)chase::FollowCraft(c,t,m,eye,look,i ? 1.0f/60.0f : 0.0f,&out);
    if(eyeOut)std::memcpy(eyeOut,out.eye,12);
    const float f[3]={out.look[0]-out.eye[0],out.look[1]-out.eye[1],out.look[2]-out.eye[2]};
    float rows[16];NativeRows(f,rows);
    if(!chase::UpRows(rows,out.up) || !Orthonormal(rows))return 999.0f;
    return crew::instruments::BankOf(rows);
}

void Roll() {
    float rows[16];const float f[3]={0.3f,-0.2f,0.9f};NativeRows(f,rows);
    const float before[3]={rows[8],rows[9],rows[10]},eye[3]={rows[12],rows[13],rows[14]};
    const float up[3]={0.5f,0.8f,0.1f};
    Check(chase::UpRows(rows,up) && Orthonormal(rows),"UpRows keeps the rows orthonormal and right-handed (left = up x forward)");
    Check(std::fabs(rows[8]-before[0])+std::fabs(rows[9]-before[1])+std::fabs(rows[10]-before[2])<1e-6f && rows[12]==eye[0] &&
          rows[13]==eye[1] && rows[14]==eye[2],"... the forward row (the screen's centre) and the eye untouched");
    const float along[3]={rows[8],rows[9],rows[10]};float copy[16];std::memcpy(copy,rows,64);
    Check(!chase::UpRows(rows,along) && std::memcmp(copy,rows,64)==0,"an up along the forward row writes nothing");
    // The jets: 40% of the bank in the picture, BankOf's sign (a craft banked right rolls the picture right).
    const float b30=SettledBank(chase::kJet,30.0f,0.0f),bm30=SettledBank(chase::kJet,-30.0f,0.0f);
    Check(std::fabs(b30-0.4f*30.0f)<0.05f && std::fabs(bm30+0.4f*30.0f)<0.05f,"a banked jet's picture rolls rollShare of the bank, the same way");
    Check(std::fabs(SettledBank(chase::kJet,0.0f,10.0f))<0.05f,"wings level: no roll (the stock picture)");
    float eyeBanked[3];const float above[3]={0,5,-20};
    SettledBank(chase::kJet,90.0f,0.0f,eyeBanked,above);
    // Knife edge: the eye 5 m over the craft swings round the fuselage by 40% of 90 deg, not all of it.
    Check(std::fabs(std::atan2(-(eyeBanked[0]-100.0f),eyeBanked[1]-50.0f)*180.0f/chase::kPi-36.0f)<0.1f,
          "the eye over the craft turns round it by rollShare of the bank only");
    // A roll through 180 deg: no jump (Fold); the inverted craft followed whole.
    float prev=SettledBank(chase::kJet,170.0f,0.0f);bool smooth=true;
    for(float b=171.0f;b<=190.0f;b+=1.0f) {
        const float now=SettledBank(chase::kJet,b>180.0f ? b-360.0f : b,0.0f);
        float d=now-prev;d=std::fmod(d+540.0f,360.0f)-180.0f;
        smooth=smooth && std::fabs(d)<2.0f;prev=now;
    }
    Check(smooth,"rolling through inverted: the picture's roll has no jump");
    Check(std::fabs(std::fabs(SettledBank(chase::kJet,180.0f,0.0f))-180.0f)<0.1f,"inverted: followed whole");
    {
        const float at0[3]={0,0,0};float steep[16];Craft(40.0f,89.5f,at0,steep);float weight=1.0f;
        chase::WorldBank(steep,steep+4,steep+8,&weight);
        Check(weight<0.01f,"pointed at the sky the bank is no angle: its weight (the share taken out) fades to 0");
    }
    // The heli's anchor follower: the bank seen along the view, rollShare of it.
    chase::Chase c;chase::Placement out{};
    const float at[3]={0,10,0};float m[16];Craft(40.0f,0.0f,at,m);
    const float e[3]={0,14,-18},l[3]={0,12,0};
    for(int i=0;i<=300;++i)chase::FollowAnchor(c,chase::kHeli,m,e,l,i ? 1.0f/60.0f : 0.0f,&out);
    const float hf[3]={l[0]-e[0],l[1]-e[1],l[2]-e[2]};float hr[16];NativeRows(hf,hr);
    Check(out.hasUp && chase::UpRows(hr,out.up) && std::fabs(crew::instruments::BankOf(hr)-0.15f*40.0f)<0.5f,
          "a heli banked 40 deg seen from behind: the picture rolls the heli's share, the same way");
    Check(std::fabs(out.eye[1]-e[1])<1e-4f && std::fabs(out.look[1]-l[1])<1e-4f,"a still heli: the anchor on its origin");
}

void Arcs() {
    const float yaw=179.0f*chase::kPi/180.0f,want=-179.0f*chase::kPi/180.0f;
    float d[3]={std::sin(yaw),0.0f,std::cos(yaw)};const float t[3]={std::sin(want),0.0f,std::cos(want)};
    chase::EaseDir(d,t,0.5f);
    Check(std::fabs(d[2]+1.0f)<1e-5f && std::fabs(d[0])<1e-4f,"a direction across +-180 deg eases the short way (through 180, not 0)");
    float opposite[3]={0,0,1};const float back[3]={0,0,-1};
    Check(chase::EaseDir(opposite,back,0.5f) && std::fabs(opposite[2])<1e-5f,"straight opposite: a way round, still unit");
    // Attitudes: q and -q are one; the slerp never goes the long way.
    const float l[3]={1,0,0},u[3]={0,1,0},f[3]={0,0,1};
    const chase::Quat a=chase::FromRows(l,u,f);
    const float c=std::cos(0.1f),s=std::sin(0.1f);
    const float l2[3]={c,0,-s},f2[3]={s,0,c};
    chase::Quat b=chase::FromRows(l2,u,f2);b.w=-b.w;b.x=-b.x;b.y=-b.y;b.z=-b.z;
    const chase::Quat h=chase::Slerp(a,b,0.5f);
    Check(std::fabs(chase::Angle(a,h)-0.05f)<1e-4f,"an attitude's slerp takes the short way when the quaternion is the negated one");
    float rl[3],ru[3],rf[3];chase::ToRows(chase::FromRows(l2,u,f2),rl,ru,rf);
    Check(std::fabs(rl[0]-l2[0])+std::fabs(rl[2]-l2[2])+std::fabs(rf[0]-f2[0])+std::fabs(rf[2]-f2[2])<1e-5f,"rows -> quaternion -> rows");
    // The attitude's ease toward a still goal: one result at 30, 60, 144 and 240 Hz.
    float ends[4];int k=0;
    for(int hz:{30,60,144,240}) {
        chase::Quat q=a;
        const chase::Quat goal=chase::FromRows(l2,u,f2);
        for(int i=0;i<hz;++i)q=chase::Slerp(q,goal,chase::Share(1.0f/hz,0.15f));
        ends[k++]=chase::Angle(q,goal);
    }
    Check(std::fabs(ends[0]-ends[3])<1e-5f && std::fabs(ends[1]-ends[3])<1e-5f && std::fabs(ends[2]-ends[3])<1e-5f,
          "the attitude's exponential ease is frame-rate independent");
}

void Anchor() {
    // The anchor follower keeps the input's view direction exactly while it lags the moving vehicle.
    chase::Chase c;chase::Placement out{};
    float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    bool same=true;float lag=0.0f;
    for(int i=0;i<=240;++i) {
        m[14]=0.25f*i;m[12]=std::sin(i*0.1f);
        const float yaw=0.03f*i;
        const float eye[3]={m[12]-20.0f*std::sin(yaw),m[13]+5.0f,m[14]-20.0f*std::cos(yaw)};
        const float look[3]={m[12],m[13]+2.0f,m[14]};
        chase::FollowAnchor(c,chase::kGround,m,eye,look,i ? 1.0f/60.0f : 0.0f,&out);
        float d0[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]},d1[3]={out.look[0]-out.eye[0],out.look[1]-out.eye[1],out.look[2]-out.eye[2]};
        same=same && Dist(d0,d1)<1e-4f;
        lag=Dist(out.eye,eye);
    }
    Check(same,"the anchor follower moves eye and look-at by one offset: the view's direction is the input's");
    Check(lag>0.3f && !out.hasUp,"... it lags the moving vehicle; no roll without a share");
}

void Lead() {
    // Straight and level the look-at is the stock one; flying off the nose it leads along the path.
    chase::Chase c;chase::Placement out{};
    float m[16];float at[3]={0,500,0};Craft(0,0,at,m);
    float eye[3],look[3];
    for(int i=0;i<=240;++i) {
        at[2]=200.0f*i/60.0f;m[14]=at[2];
        eye[0]=at[0];eye[1]=at[1]+4;eye[2]=at[2]-20;look[0]=at[0];look[1]=at[1]+1;look[2]=at[2]+10;
        chase::FollowCraft(c,chase::kJet,m,eye,look,i ? 1.0f/60.0f : 0.0f,&out);
    }
    Check(Dist(out.look,look)<1e-3f && Dist(out.eye,eye)<1e-3f,"straight and level at 200 m/s: the stock eye and look-at, no lag at speed");
    for(int i=241;i<=300;++i) {   // sliding sideways at 20 m/s off the nose
        at[0]+=20.0f/60.0f;at[2]+=200.0f/60.0f;m[12]=at[0];m[14]=at[2];
        eye[0]=at[0];eye[1]=at[1]+4;eye[2]=at[2]-20;look[0]=at[0];look[1]=at[1]+1;look[2]=at[2]+10;
        chase::FollowCraft(c,chase::kJet,m,eye,look,1.0f/60.0f,&out);
    }
    Check(out.look[0]-look[0]>2.0f,"a path off the nose: the look-at leads toward it");
}
}  // namespace

int main() {
    FrameRate();Overshoot();SteadyLag();Pause();Snap();Nan();Roll();Arcs();Anchor();Lead();
    std::printf("chase cam: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
