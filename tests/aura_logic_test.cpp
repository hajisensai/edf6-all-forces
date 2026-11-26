// src/aura_logic.h offline: when an aura stands, which auras reach a friend (per kind the strongest, never stacked; other
// teams and out of range none), the attack multiplier's max, the repair and ammo steps, and the AFAU message.
#include "../src/aura_logic.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
bool Near(float a,float b) { return std::fabs(a-b)<1e-4f; }
crew::aura::Source At(crew::aura::Kind k,float x,float z,float radius,float strength,std::int32_t team=1) {
    crew::aura::Source s{};
    s.kind=k;s.pos[0]=x;s.pos[1]=0.0f;s.pos[2]=z;s.radius=radius;s.strength=strength;s.team=team;
    return s;
}
}  // namespace

int main() {
    using namespace crew::aura;
    // When an aura stands.
    Check(Active(false,true,30.0f,1.0f),"driven: stands while it moves");
    Check(Active(false,false,0.5f,1.0f) && Active(false,false,1.0f,1.0f),"empty and parked: stands");
    Check(!Active(false,false,1.5f,1.0f),"empty and rolling: none");
    Check(!Active(true,true,0.0f,1.0f) && !Active(true,false,0.0f,1.0f),"wrecked: none");
    Check(!Active(false,false,-1.0f,1.0f),"no speed known (negative) and empty: none");

    // Selection.
    const float here[3]={0.0f,0.0f,0.0f};
    {
        const Source s[]={At(Kind::command,10,0,60,0.3f),At(Kind::command,0,20,60,0.5f),At(Kind::command,5,5,60,0.2f)};
        const Effect e=Select(s,3,here,1);
        Check(Near(e.attack,0.5f),"two command trucks: the strongest, not the sum");
        Check(e.kinds==(1u<<0) && e.repair==0.0f && e.ammo==0.0f,"only the command kind");
    }
    {
        const Source s[]={At(Kind::command,0,10,60,0.3f),At(Kind::repair,0,-10,60,0.02f),At(Kind::ammo,10,0,60,0.1f),
                          At(Kind::repair,0,30,60,0.05f)};
        const Effect e=Select(s,4,here,1);
        Check(Near(e.attack,0.3f) && Near(e.repair,0.05f) && Near(e.ammo,0.1f),"three kinds together all apply, each its strongest");
        Check(e.kinds==7,"all three kinds");
    }
    {
        const Source s[]={At(Kind::repair,59.9f,0,60,0.02f),At(Kind::ammo,60.1f,0,60,0.1f),At(Kind::command,0,0,60,0.3f,2)};
        const Effect e=Select(s,3,here,1);
        Check(Near(e.repair,0.02f),"just inside the radius: reached");
        Check(e.ammo==0.0f,"just outside: not");
        Check(e.attack==0.0f,"another team's: not");
        Check(e.kinds==(1u<<1),"the repair kind only");
    }
    {
        const Source s[]={At(Kind::repair,0,0,0,0.02f),At(Kind::repair,0,0,60,0.0f),At(Kind::repair,0,0,60,-1.0f)};
        Check(Select(s,3,here,1).kinds==0,"no radius or no strength: nothing");
        Check(Select(nullptr,3,here,1).kinds==0 && Select(s,3,nullptr,1).kinds==0,"no list or no position: nothing");
    }
    {
        Source s[kMaxSources+4];
        for(int i=0;i<kMaxSources+4;++i)s[i]=At(Kind::command,0,0,60,0.01f*static_cast<float>(i+1));
        Check(Near(Select(s,kMaxSources+4,here,1).attack,0.01f*kMaxSources),"past kMaxSources: not read");
    }

    // The attack multiplier: never lowered, never stacked.
    Check(Near(AttackMultiplier(1.0f,0.3f),1.3f),"the bonus over the reset 1");
    Check(Near(AttackMultiplier(1.5f,0.3f),1.5f),"a stronger native device's kept (max)");
    Check(Near(AttackMultiplier(AttackMultiplier(1.0f,0.3f),0.3f),1.3f),"applied twice in a frame: still once");
    Check(Near(AttackMultiplier(std::numeric_limits<float>::quiet_NaN(),0.3f),1.3f),"not finite: the bonus");
    Check(Near(AttackMultiplier(1.0f,-0.5f),1.0f),"a negative bonus: none");

    // Repair.
    float given=-1.0f;
    Check(Near(Heal(50.0f,0.0f,100.0f,0.02f,1.0f,&given),52.0f) && Near(given,2.0f),"2% of max a second");
    Check(Near(Heal(99.5f,0.0f,100.0f,0.02f,1.0f,&given),100.0f) && Near(given,0.5f),"clamped at max");
    Check(Near(Heal(100.0f,0.0f,100.0f,0.02f,1.0f,&given),100.0f) && given==0.0f,"full: nothing");
    Check(Near(Heal(50.0f,0.0f,100.0f,0.0f,1.0f,&given),50.0f) && given==0.0f,"no share: nothing");
    Check(Near(Heal(50.0f,0.0f,100.0f,0.02f,0.0f,&given),50.0f) && given==0.0f,"no time: nothing");
    Check(Near(Heal(50.0f,0.0f,0.0f,0.02f,1.0f,&given),50.0f) && given==0.0f,"no max: nothing");

    // Ammo: the fraction carried, the capacity a ceiling.
    float carry=0.0f;
    std::int32_t left=0;
    for(int f=0;f<60;++f)left=Refill(left,100,0.1f,1.0f/60.0f,carry);
    Check(left==10 || left==9,"10% of 100 a second: ten rounds in a second");
    carry=0.0f;left=0;
    int frames=0;
    while(left<1 && frames<1000){left=Refill(left,1,0.1f,1.0f/60.0f,carry);++frames;}
    Check(left==1 && frames>=590 && frames<=610,"one rocket at 10% a second: after ten seconds, not never");
    carry=0.5f;
    Check(Refill(100,100,0.1f,1.0f,carry)==100 && carry==0.0f,"full: stays, the carry dropped");
    carry=0.0f;
    Check(Refill(95,100,1.0f,1.0f,carry)==100,"over capacity: capped");
    carry=0.0f;
    Check(Refill(-3,10,0.0f,1.0f,carry)==0,"a negative count: read as 0");
    Check(Refillable(0) && Refillable(1) && Refillable(3) && !Refillable(2),"the points weapons (Air Raider calls) not filled");

    // The message.
    {
        Msg m{};
        m.count=3;
        m.sources[0]=At(Kind::command,100.5f,-20.25f,60,0.3f);
        m.sources[1]=At(Kind::repair,1,2,45,0.02f,3);
        m.sources[2]=At(Kind::ammo,-7,8,80,0.1f);
        unsigned char bytes[kWire];
        Check(Encode(m,bytes,sizeof(bytes)),"encodes");
        Check(Owned(bytes,sizeof(bytes)) && TakesState(bytes,sizeof(bytes)),"AFAU, a state");
        Msg back{};
        Check(Decode(bytes,sizeof(bytes),back),"decodes");
        Check(back.count==3 && back.sources[1].kind==Kind::repair && back.sources[1].team==3 && Near(back.sources[0].pos[0],100.5f) &&
              Near(back.sources[0].pos[2],-20.25f) && Near(back.sources[2].radius,80.0f) && Near(back.sources[0].strength,0.3f),
              "the same sources back");
        Check(Same(m,back),"the same list");
        Check(!Decode(bytes,sizeof(bytes)-1,back),"short: refused");
        unsigned char bad[kWire];
        std::memcpy(bad,bytes,sizeof(bad));bad[4]=2;
        Check(!Decode(bad,sizeof(bad),back),"another version: refused");
        std::memcpy(bad,bytes,sizeof(bad));bad[8]=kMaxSources+1;
        Check(!Decode(bad,sizeof(bad),back),"too many: refused");
        std::memcpy(bad,bytes,sizeof(bad));bad[9]=7;
        Check(!Decode(bad,sizeof(bad),back),"a kind not one: refused");
        Msg wrong=m;wrong.sources[0].strength=std::numeric_limits<float>::infinity();
        Check(!Encode(wrong,bytes,sizeof(bytes)),"not finite: not sent");
        wrong=m;wrong.count=kMaxSources+1;
        Check(!Encode(wrong,bytes,sizeof(bytes)),"too many: not sent");
        Msg none{};
        Check(Encode(none,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),back) && back.count==0,"none standing: sent too (they end)");
    }

    // Pacing.
    {
        Msg a{};a.count=1;a.sources[0]=At(Kind::repair,0,0,60,0.02f);
        Msg moved=a;moved.sources[0].pos[0]=0.4f;
        Msg away=a;away.sources[0].pos[0]=2.0f;
        Check(Due(false,a,0,a,10),"never sent: due");
        Check(!Due(true,a,1000,a,1500) && Due(true,a,1000,a,2000),"unchanged: the heartbeat only");
        Check(!Due(true,a,1000,moved,1200),"moved under half a metre: waits");
        Check(!Due(true,a,1000,away,1050) && Due(true,a,1000,away,1100),"changed: after kGapMs");
        Check(Fresh(1000,1000+kSilentMs-1) && !Fresh(1000,1000+kSilentMs) && !Fresh(0,10),"heard: stands kSilentMs");
    }

    // The markers: told apart within the 16 characters the game's bone lookup compares.
    for(int i=0;i<kKinds;++i)for(int j=i+1;j<kKinds;++j)
        Check(std::wcsncmp(kMarkers[i],kMarkers[j],16)!=0,"markers apart within 16 characters");
    for(int i=0;i<kKinds;++i)Check(std::wcslen(kMarkers[i])<=16,"a marker within 16 characters");

    std::printf("aura_logic: %d checks passed\n",checks);
    return 0;
}
