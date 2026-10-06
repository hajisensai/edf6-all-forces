// Offline turn-handling sim of the player's fixed-wing flight (src/playerjet.cpp Air), every wing the player can fly
// (playerjet.cpp kKinds fighter/strike + playerjet_kinds.h kBoardable wings), under three inputs from level flight at the
// cruise throttle's speed: a pad's right stick full over (Roll's coordinated turn), the keys (D to roll, W to pull once
// the path is banked 90 deg), and the mouse's aim put 90 deg right (AimSteer). It prints, per kind: the time the path's
// bank (PJet::up) and the visible body (BodyAttitude, the seat camera rides the body: docs/camera-re.md §1) take to
// reach 90 deg, the heading turned after 1 s and 3 s, the sustained turn rate, and the worst body-vs-path lag. A small
// aim step (kStepDeg) shows how much the path banks for a few degrees of correction.
// `pjet_turn_sim` = the code before 2026-10-06; `--fix` = the plugin's now (src/pjet_handling.h, the same functions
// playerjet.cpp calls); `--both`; `--roll-scale x` ini PlayerJetRollScale; `--check`: the handling's bounds below, exit 1
// on a miss (CTest).
// Built on request only: `cmake --build build --target pjet_turn_sim`, then `build\pjet_turn_sim.exe [--fix]`.
// The formulas are playerjet.cpp's (line numbers as of 4229d83): SmoothStick 417, Roll 626, BankToward 620,
// AimSteer 690, Hold 716, Air 729-787; body506.cpp BodyAttitude 226.
#include "playerjet_kinds.h"
#include "pjet_handling.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using namespace crew;

// playerjet.cpp constants (lines 70-150).
constexpr float kG=9.8f,kLevelRate=1.8f,kRollDead=0.08f,kLevelPull=0.15f,kVertical=0.97f;
constexpr float kHold=0.988f,kSettleSink=1.5f,kMinBankCos=0.25f,kPush=0.5f,kInduced=0.3f;
constexpr float kAimBankMin=0.3f,kAimTurnFrom=0.09f,kAimSteep=0.77f;
constexpr float kAoaPerG=0.026f,kAoaMin=-0.05f,kAoaMax=0.2f,kAoaTau=0.3f,kAttGain=6.0f;
constexpr float kStallFloor=25.0f,kBodyTop=340.0f,kStickTau=0.12f,kCruiseThrottle=0.55f;
constexpr float kDt=1.0f/60.0f,kPi=3.14159265f;
constexpr float kStepDeg=6.0f;

struct K { const char* name; float minAir,top,thrust,maxG,corner,roll; };

// The model's knobs: as is (fix false) or proposed.
struct Model {
    bool fix;
    float rollRate;   // playerjet.cpp:111 kRollRate (the path)
    float turnBank;   // playerjet.cpp:113 kTurnBank
    float steer;      // playerjet.cpp:137 kSteer
    float rollScale;  // proposed ini PlayerJetRollScale
    // The path's roll rate for kind k (as is: kRollRate for all).
    float PathRoll(const K& k) const noexcept { return fix ? handling::PathRoll(k.roll,rollScale) : rollRate; }
    // The body's (and so the camera's) rate cap, BodyAttitude's maxRate (as was: Kind::roll).
    float MinBankCos() const noexcept { return fix ? handling::kMinBankCos : kMinBankCos; }
    float BodyCap(const K& k) const noexcept { return fix ? handling::BodyCap(k.roll,k.maxG,k.corner,rollScale) : k.roll; }
    // The bank a full turn stick asks for (as was: kTurnBank 69 deg for every kind).
    float Bank(const K& k,float) const noexcept { return fix ? handling::TurnBank(k.maxG,k.corner,k.minAir,k.top) : turnBank; }
    // The aim's sideways demand share at `off` (as was: 1).
    float AimShare(float off) const noexcept { return fix ? handling::AimShare(off) : 1.0f; }
    // The roll rate toward the aim's lift (as was: a step kRollRate / kLevelRate at kAimTurnFrom).
    float AimRoll(const K& k,float off) const noexcept {
        if(!fix)return off>=kAimTurnFrom ? PathRoll(k) : kLevelRate;
        return handling::AimRoll(PathRoll(k),off);
    }
};

float Dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
void Cross(const float* a,const float* b,float* c){c[0]=a[1]*b[2]-a[2]*b[1];c[1]=a[2]*b[0]-a[0]*b[2];c[2]=a[0]*b[1]-a[1]*b[0];}
float Len(const float* a){return std::sqrt(Dot(a,a));}
bool Norm(float* a){const float l=Len(a);if(l<1e-6f)return false;for(int i=0;i<3;++i)a[i]/=l;return true;}
float Clamp(float x,float a,float b){return x<a ? a : x>b ? b : x;}
void Turn(float* v,const float* axis,float angle){float c[3];Cross(axis,v,c);const float co=std::cos(angle),si=std::sin(angle),a=Dot(axis,v)*(1.0f-co);
    for(int i=0;i<3;++i)v[i]=v[i]*co+c[i]*si+axis[i]*a;}
bool Across(float* u,const float* d){const float a=Dot(u,d);for(int i=0;i<3;++i)u[i]-=d[i]*a;return Norm(u);}
void BankToward(float* up,const float* dir,const float* want,float most){float c[3];Cross(up,want,c);
    Turn(up,dir,Clamp(std::atan2(Dot(c,dir),Dot(up,want)),-most,most));Across(up,dir);}
float BankOf(const float* dir,const float* up){float level[3]={-dir[0]*dir[1],1.0f-dir[1]*dir[1],-dir[2]*dir[1]};
    if(!Norm(level))return 0.0f;float right[3];Cross(dir,level,right);return std::atan2(Dot(up,right),Dot(up,level));}
float Angle(const float* a,const float* b){return std::acos(Clamp(Dot(a,b),-1.0f,1.0f));}

enum class Input { pad, keys, mouse, step };

struct Out { float pathTo90,bodyTo90,head1,head3,rate,lag,stepBank,drop3; int stepFlips; };

Out Fly(const K& k,const Model& m,Input in){
    const float want=k.minAir+kCruiseThrottle*(k.top-k.minAir);
    float dir[3]={0,0,1},up[3]={0,1,0};
    float speed=want,aoa=0.0f;
    float bf[3]={0,0,1},bu[3]={0,1,0};   // the body's nose and up (right = up x nose: BodyAttitude's hand = +1)
    float aim[3];
    const float aimAt=in==Input::step ? kStepDeg*kPi/180.0f : kPi/2.0f;
    aim[0]=-std::sin(aimAt);aim[1]=0.0f;aim[2]=std::cos(aimAt);   // right of +z is -x (playerjet.cpp RightOf)
    float stickYaw=0,stickRoll=0,stickPitch=0;   // SmoothStick's state
    Out o{-1,-1,0,0,0,0,0,0,0};
    float y=0.0f;
    bool pulling=false;
    float lastBankSign=0.0f;
    const float head0=std::atan2(dir[0],dir[2]);
    float headAt3=0,headAt5=0;
    const int steps=static_cast<int>(6.0f/kDt);
    for(int n=1;n<=steps;++n){
        const float t=n*kDt;
        // the raw stick (Stick) for this input
        float yaw=0,roll=0,pitch=0;
        if(in==Input::pad)yaw=1.0f;
        if(in==Input::keys){ if(!pulling)roll=1.0f; else pitch=1.0f; }
        const float a=1.0f-std::exp(-kDt/kStickTau);   // SmoothStick (playerjet.cpp:417)
        stickYaw+=(yaw-stickYaw)*a;stickRoll+=(roll-stickRoll)*a;stickPitch+=(pitch-stickPitch)*a;
        // Air (playerjet.cpp:729)
        if(speed<kStallFloor)speed=kStallFloor;
        float level[3]={-dir[0]*dir[1],1.0f-dir[1]*dir[1],-dir[2]*dir[1]};
        const bool vertical=!Norm(level) || std::fabs(dir[1])>kVertical;
        if(vertical){level[0]=0;level[1]=1;level[2]=0;}
        const bool aiming=in==Input::mouse || in==Input::step;
        if(!aiming){   // Roll (626)
            Across(up,dir);
            if(std::fabs(stickRoll)>kRollDead)Turn(up,dir,stickRoll*m.PathRoll(k)*kDt);
            else if(!vertical && std::fabs(stickPitch)<kLevelPull){float w[3];std::memcpy(w,level,12);
                Turn(w,dir,Clamp(stickYaw,-1.0f,1.0f)*m.Bank(k,want));BankToward(up,dir,w,kLevelRate*kDt);}
            Across(up,dir);
        }
        const float wing=speed<k.corner ? (speed/k.corner)*(speed/k.corner) : 1.0f;
        const float most=k.maxG*kG*wing;
        const float gPerp[3]={dir[0]*kG*dir[1],-kG+dir[1]*kG*dir[1],dir[2]*kG*dir[1]};
        const float across=Len(gPerp);
        const float wantSpeed=k.minAir+kCruiseThrottle*(k.top-k.minAir);
        // Hold (716)
        float hold=across*Clamp(1.0f-(1.0f-kHold)*(1.0f-(-dir[1]*speed)/kSettleSink),kHold,1.0f);
        if(!vertical && std::fabs(stickYaw)>=kRollDead){const float b=Dot(up,level);
            if(b>m.MinBankCos())hold*=1.0f+std::fabs(Clamp(stickYaw,-1.0f,1.0f))*(1.0f/b-1.0f);}
        hold=Clamp(hold,0.0f,most);
        float lift;
        if(aiming){   // AimSteer (690)
            const float c=Clamp(Dot(aim,dir),-1.0f,1.0f);
            float toward[3]={aim[0]-dir[0]*c,aim[1]-dir[1]*c,aim[2]-dir[2]*c};
            if(!Norm(toward))toward[0]=toward[1]=toward[2]=0.0f;
            const float off=std::acos(c),turn=m.steer*off*speed*m.AimShare(off);
            float l[3];for(int i=0;i<3;++i)l[i]=toward[i]*turn-(across>1e-4f ? gPerp[i]/across*hold : 0.0f);
            float w[3];std::memcpy(w,l,12);
            const bool steep=vertical || std::fabs(dir[1])>kAimSteep;
            if(Len(l)>kAimBankMin*kG && (off>=kAimTurnFrom || !steep) && Across(w,dir))BankToward(up,dir,w,m.AimRoll(k,off)*kDt);
            lift=Clamp(Dot(l,up),-kPush*most,most);
        } else lift=stickPitch>=0.0f ? hold+stickPitch*(most-hold) : hold+stickPitch*(hold+kPush*most);
        float next[3];for(int i=0;i<3;++i)next[i]=dir[i]+(up[i]*lift+gPerp[i])*kDt/speed;
        Norm(next);
        Across(up,next);
        const float g=lift/kG,top2=k.top*k.top;
        const float thrust=k.thrust*wantSpeed*wantSpeed/top2;
        const float slow=k.corner/speed,drag=k.thrust*speed*speed/top2+kInduced*g*g*slow*slow;
        speed=Clamp(speed+(thrust-drag-kG*next[1])*kDt,kStallFloor,kBodyTop);
        std::memcpy(dir,next,12);
        y+=dir[1]*speed*kDt;
        const float mid=0.5f*(k.minAir+k.top),past=mid/speed;
        aoa+=(Clamp(kAoaPerG*g*past*past,kAoaMin,kAoaMax)-aoa)*(kDt<kAoaTau ? kDt/kAoaTau : 1.0f);
        float nose[3]={dir[0],dir[1],dir[2]},bodyUp[3]={up[0],up[1],up[2]};
        {const float al=Dot(bodyUp,nose);for(int i=0;i<3;++i)bodyUp[i]-=nose[i]*al;Norm(bodyUp);
         const float co=std::cos(aoa),si=std::sin(aoa);for(int i=0;i<3;++i){const float nn=nose[i],u=bodyUp[i];nose[i]=nn*co+u*si;bodyUp[i]=u*co-nn*si;}}
        // BodyAttitude (body506.cpp:226), then the body turned by omega over the step (Havok's integration)
        {float br[3],tr[3];Cross(bu,bf,br);Cross(bodyUp,nose,tr);
         float w[3]={0,0,0},c[3];Cross(br,tr,c);for(int i=0;i<3;++i)w[i]+=c[i];Cross(bu,bodyUp,c);for(int i=0;i<3;++i)w[i]+=c[i];
         Cross(bf,nose,c);for(int i=0;i<3;++i)w[i]+=c[i];for(int i=0;i<3;++i)w[i]*=0.5f*kAttGain;
         const float l=Len(w),cap=m.BodyCap(k);if(l>cap)for(int i=0;i<3;++i)w[i]*=cap/l;
         const float wl=Len(w);
         if(wl>1e-6f){float ax[3]={w[0]/wl,w[1]/wl,w[2]/wl};Turn(bf,ax,wl*kDt);Turn(bu,ax,wl*kDt);Across(bu,bf);}}
        // measures
        const float pathBank=std::fabs(BankOf(dir,up)),bodyBank=std::fabs(BankOf(bf,bu));
        if(o.pathTo90<0 && pathBank>=kPi/2*0.999f)o.pathTo90=t;
        if(o.bodyTo90<0 && bodyBank>=kPi/2*0.999f)o.bodyTo90=t;
        if(in==Input::keys && !pulling && pathBank>=kPi/2)pulling=true;
        float head=std::remainder(head0-std::atan2(dir[0],dir[2]),2*kPi);   // positive: right
        if(n==static_cast<int>(1.0f/kDt))o.head1=head*180/kPi;
        if(n==static_cast<int>(3.0f/kDt)){o.head3=head*180/kPi;headAt3=head;o.drop3=-y;}
        if(n==static_cast<int>(5.0f/kDt))headAt5=head;
        if(t>0.25f){const float lag=Angle(bu,bodyUp);if(lag*180/kPi>o.lag)o.lag=lag*180/kPi;}   // the body's up off the one the path wants
        if(in==Input::step){const float b=BankOf(dir,up);if(std::fabs(b)>o.stepBank)o.stepBank=std::fabs(b);
            const float sg=b>0.05f ? 1.0f : b<-0.05f ? -1.0f : 0.0f;if(sg!=0.0f && lastBankSign!=0.0f && sg!=lastBankSign)++o.stepFlips;if(sg!=0.0f)lastBankSign=sg;}
    }
    o.rate=(headAt5-headAt3)/2.0f*180/kPi;
    o.stepBank*=180/kPi;
    return o;
}

void Table(const Model& m){
    K kinds[16];int n=0;
    kinds[n++]=K{"fighter*",65.0f,260.0f,16.0f,6.0f,150.0f,2.6f};   // playerjet.cpp:87 kKinds[0]
    kinds[n++]=K{"strike*",60.0f,240.0f,11.0f,5.0f,140.0f,1.6f};    // playerjet.cpp:88 kKinds[1]
    for(const auto& b:pjet::kBoardable){if(b.frame!=pjet::Airframe::wing)continue;const auto& p=b.perf;
        kinds[n++]=K{p.name,p.minAir,p.top,p.thrust,p.maxG,p.corner,p.roll};}
    std::printf("== %s (path roll / body cap / turn bank / aim)\n",m.fix ? "PROPOSED" : "AS IS");
    std::printf("%-12s %5s %4s %5s | %5s %5s %5s | %-24s | %-24s | %-24s | %s\n","kind","cruise","maxG","corner","pRoll","bCap","bank",
                "pad: 1s/3s deg, rate d/s","keys: p90/b90 s, 1s/3s","mouse90: 1s/3s, rate, lag","aim 6deg: bank | pad drop 3s m");
    for(int i=0;i<n;++i){const K& k=kinds[i];const float cruise=k.minAir+kCruiseThrottle*(k.top-k.minAir);
        const Out pad=Fly(k,m,Input::pad),keys=Fly(k,m,Input::keys),mouse=Fly(k,m,Input::mouse),step=Fly(k,m,Input::step);
        std::printf("%-12s %5.0f %4.1f %5.0f | %5.2f %5.2f %5.1f | %5.1f %5.1f %5.1f          | %4.2f %4.2f %5.1f %5.1f    | %5.1f %5.1f %5.1f lag%4.0f | %4.0f | %5.1f\n",
                    k.name,cruise,k.maxG,k.corner,m.PathRoll(k),m.BodyCap(k),m.Bank(k,cruise)*180/kPi,pad.head1,pad.head3,pad.rate,
                    keys.pathTo90,keys.bodyTo90,keys.head1,keys.head3,mouse.head1,mouse.head3,mouse.rate,mouse.lag,step.stepBank,pad.drop3);
    }
}
}  // namespace

// The handling's bounds (--check), on the plugin's own functions (pjet_handling.h) for every wing the player flies:
//  - the body (the camera) keeps up: at most kLagMost deg behind the path flying the mouse's aim 90 deg off (was 43-68);
//  - it rolls into view: the body at 90 deg of bank within kBody90Most s on the keys (was 0.75-1.67);
//  - a full pad turn holds its height: at most kDropMost m down in 3 s (the 2 g gunship sank 7.9 m and on);
//  - a few degrees of aim correction ask a few degrees of bank: at most kStepBankMost deg for kStepDeg off (was 59-72);
//  - a stronger wing turns harder on the pad: the 6 g fighter's sustained rate at least kFighterOver x the 3 g bomber's
//    (was 8.6 against 10.0 deg/s).
constexpr float kLagMost=36.0f,kBody90Most=1.2f,kDropMost=2.0f,kStepBankMost=45.0f,kFighterOver=1.1f;
int Check(const Model& m){
    int fails=0;
    float fighterRate=0.0f,bomberRate=0.0f;
    auto kind=[&](const K& k){
        const Out pad=Fly(k,m,Input::pad),keys=Fly(k,m,Input::keys),mouse=Fly(k,m,Input::mouse),step=Fly(k,m,Input::step);
        const bool ok=mouse.lag<=kLagMost && keys.bodyTo90>0.0f && keys.bodyTo90<=kBody90Most && pad.drop3<=kDropMost &&
                      step.stepBank<=kStepBankMost;
        std::printf("%s %-12s lag %4.0f deg, body to 90 %.2f s, pad drop 3 s %.1f m, 6 deg aim bank %.0f deg, pad rate %.1f deg/s\n",
                    ok ? "ok  " : "FAIL",k.name,mouse.lag,keys.bodyTo90,pad.drop3,step.stepBank,pad.rate);
        if(!ok)++fails;
        if(std::strcmp(k.name,"fighter")==0)fighterRate=pad.rate;
        if(std::strcmp(k.name,"bomber401")==0)bomberRate=pad.rate;
    };
    kind(K{"fighter*",65.0f,260.0f,16.0f,6.0f,150.0f,2.6f});
    kind(K{"strike*",60.0f,240.0f,11.0f,5.0f,140.0f,1.6f});
    for(const auto& b:pjet::kBoardable){if(b.frame!=pjet::Airframe::wing)continue;const auto& p=b.perf;
        kind(K{p.name,p.minAir,p.top,p.thrust,p.maxG,p.corner,p.roll});}
    const bool order=fighterRate>=kFighterOver*bomberRate;
    std::printf("%s the fighter's pad turn %.1f deg/s against the bomber's %.1f\n",order ? "ok  " : "FAIL",fighterRate,bomberRate);
    fails+=!order;
    std::printf(fails ? "%d FAILED\n" : "all passed\n",fails);
    return fails ? 1 : 0;
}

int main(int argc,char** argv){
    if(argc>1 && std::strcmp(argv[1],"--check")==0)return Check(Model{true,4.2f,1.2f,1.6f,1.0f});
    const bool fix=argc>1 && std::strcmp(argv[1],"--fix")==0;
    const bool both=argc>1 && std::strcmp(argv[1],"--both")==0;
    float scale=1.0f;
    for(int i=1;i+1<argc;++i)if(std::strcmp(argv[i],"--roll-scale")==0)scale=static_cast<float>(std::atof(argv[i+1]));
    Model asIs{false,4.2f,1.2f,1.6f,1.0f},prop{true,4.2f,1.2f,1.6f,scale};
    if(both || !fix)Table(asIs);
    if(both || fix)Table(prop);
    return 0;
}
