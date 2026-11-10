// What stores weigh (test hub #39, src/store_burden.h): the generated store table (src/stores.inc, tools/gen_stores.py)
// and the production burden math, no game.
//  - a holder hangs on ceil(rounds / perPylon) pylons; spent, its pylons' mass and drag stay;
//  - a full holder drags what it did before the pylons were split out (the jets' tuning keeps its full-load numbers);
//  - a stock helicopter's lift per rotor speed and blend scale by its mass ratio: under the game's flight law
//    (docs/aircraft-re.md §2) a loaded one needs more rotor to hover, climbs and accelerates slower, tops out lower, and a
//    spent load (only its pylons left) lies between it and the clean craft.
#include "../src/store_burden.h"
#include "../src/heliaim.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace crew {
namespace {
#include "../src/stores.inc"
}
}  // namespace crew

namespace {
using namespace crew;
int checks=0;
void Check(bool condition,const char* description) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}
bool Near(float a,float b,float eps) { return std::fabs(a-b)<=eps; }

const StoreSpec& Spec(const wchar_t* prefix) {
    for(const auto& s:kStores)if(!std::wcscmp(s.prefix,prefix))return s;
    std::fprintf(stderr,"FAIL: no store %ls\n",prefix);std::exit(1);
}

Store Holder(const StoreSpec& s,std::int32_t rounds,std::int32_t ammo) { return Store{nullptr,&s,ammo,0,0.0f,rounds}; }

void Pylons() {
    Check(burden::Pylons(0,19)==0,"no rounds: no pylon");
    Check(burden::Pylons(19,19)==1 && burden::Pylons(20,19)==2 && burden::Pylons(38,19)==2,"pods of 19");
    Check(burden::Pylons(4,1)==4 && burden::Pylons(6,4)==2,"a pylon a round / four to a launcher");
    Check(burden::Pylons(32,0)==0,"perPylon 0: nothing hangs on a pylon");
}

// The per-round drag the jets flew on before the pylons were split out (a round carried its pylon's share).
struct Before { const wchar_t* prefix; std::int32_t rounds; float drag; };
constexpr Before kBefore[]={
    {L"EDF6VC_AAM_S_",2,0.006f},{L"EDF6VC_AAM_M_",4,0.010f},{L"EDF6VC_AAM_L_",4,0.025f},{L"EDF6VC_AGM_",6,0.020f},
    {L"EDF6VC_AGM_L_",4,0.004f},{L"EDF6VC_MK82_",6,0.015f},{L"EDF6VC_RKT_",19,0.0015f},{L"EDF6VC_RKT_",38,0.0015f},
};

void Holders() {
    for(const auto& b:kBefore) {
        const StoreSpec& s=Spec(b.prefix);
        const burden::Load full=burden::HolderLoad(s,b.rounds,b.rounds),spent=burden::HolderLoad(s,b.rounds,0);
        Check(Near(full.drag,b.drag*static_cast<float>(b.rounds),2e-4f),"a full holder drags what it did");
        Check(s.perPylon>0 && s.pylonMass>0.0f && s.pylonDrag>0.0f,"a jet store hangs on a pylon of some mass and drag");
        const float pylons=static_cast<float>(burden::Pylons(b.rounds,s.perPylon));
        Check(Near(spent.kg,s.pylonMass*pylons,1e-3f) && Near(spent.drag,s.pylonDrag*pylons,1e-6f),
              "spent: only its pylons are left");
        Check(Near(full.kg-spent.kg,s.mass*static_cast<float>(b.rounds),1e-2f),"the rounds weigh what they weigh");
        // Each round fired takes its own mass and drag off, never the pylon's.
        for(std::int32_t left=b.rounds;left>0;--left) {
            const burden::Load a=burden::HolderLoad(s,b.rounds,left),c=burden::HolderLoad(s,b.rounds,left-1);
            Check(Near(a.kg-c.kg,s.mass,1e-2f) && c.kg>=spent.kg-1e-3f,"a round fired: its mass off, the pylon kept");
        }
    }
    // The rocket pod: nearly all of its drag stays empty (a pod is a pod).
    const StoreSpec& rkt=Spec(L"EDF6VC_RKT_");
    Check(burden::HolderLoad(rkt,19,0).drag>0.8f*burden::HolderLoad(rkt,19,19).drag,"an empty rocket pod still drags");
    // No pylon: a gun's rounds and the ship's cells weigh nothing at all.
    for(const auto& s:kStores)
        if(s.perPylon==0)Check(s.pylonMass==0.0f && s.pylonDrag==0.0f,"no pylon, no pylon mass");
    Check(burden::HolderLoad(Spec(L"EDF6VC_ESSM_"),32,0).kg==0.0f,"the ship's cells: nothing");
}

void Jets() {
    // The strike jet (pylib/vcobjects.py _STRIKE over JET_MASSES 7001: 22 t): full, then everything dropped.
    const Store full[]={Holder(Spec(L"EDF6VC_AGM_"),6,6),Holder(Spec(L"EDF6VC_RKT_"),38,38),Holder(Spec(L"EDF6VC_MK82_"),6,6),
                        Holder(Spec(L"EDF6VC_AAM_S_"),2,2)};
    const Store spent[]={Holder(Spec(L"EDF6VC_AGM_"),6,0),Holder(Spec(L"EDF6VC_RKT_"),38,0),Holder(Spec(L"EDF6VC_MK82_"),6,0),
                         Holder(Spec(L"EDF6VC_AAM_S_"),2,0)};
    float clean=0.0f;
    for(const auto& m:kJetMasses)if(m.mark==7001.0f)clean=m.mass;
    Check(clean==22000.0f,"the strike jet's clean mass");
    float kg=0.0f,drag=0.0f,pkg=0.0f,pdrag=0.0f;
    for(const auto& h:full){const auto l=burden::HolderLoad(*h.spec,h.rounds,h.ammo);kg+=l.kg;drag+=l.drag;}
    for(const auto& h:spent){const auto l=burden::HolderLoad(*h.spec,h.rounds,h.ammo);pkg+=l.kg;pdrag+=l.drag;}
    const Burden f=burden::Ratio(clean,kg,drag),e=burden::Ratio(clean,pkg,pdrag);
    Check(f.mass>e.mass && e.mass>1.0f,"spent: lighter than loaded, still its pylons' weight over clean");
    Check(f.drag>e.drag && e.drag>0.0f,"spent: less drag than loaded, still its pylons'");
    // 6 rails x 60 + 2 pods x 40 + 6 racks x 50 + 2 rails x 35.
    Check(Near(pkg,6*60.0f+2*40.0f+6*50.0f+2*35.0f,1e-2f),"the strike jet's pylons");
    const Burden none=burden::Ratio(0.0f,kg,drag);
    Check(none.mass==1.0f && none.drag==0.0f,"no clean mass: no burden");
}

// The game's flight law for a stock heli (docs/aircraft-re.md §2), per frame: the horizontal velocity v' = d v + b (k s - d v);
// the vertical vy' = lerp(1, vdamp, t) vy + rotor L - g/60 (m/s; Havok's gravity a frame), t the rotor's way from idle to
// the hover crossover (M g / (60 L), hover rotor), g/60 what the 506's measured hover (aim::kHoverRotor at aim::kHoverLift)
// holds. climb: vy after 5 s at full rotor, m/s.
struct Flight { float top,reach5s,climb5s,hover; };
Flight Fly(burden::HeliFlight f) {
    constexpr float d=0.999f,k=80.0f,vdamp=0.95f,idle=0.2f,gFrame=aim::kHoverRotor*aim::kHoverLift;
    const float hover=gFrame/f.lift;
    float v=0.0f,vy=0.0f;
    for(int i=0;i<300;++i){v=d*v+f.blend*(k-d*v);}
    float reach=v;
    for(int i=0;i<300;++i) {
        const float t=vec::Clamp((1.0f-idle)/(hover-idle),0.0f,1.0f);
        vy=(1.0f+(vdamp-1.0f)*t)*vy+1.0f*f.lift-gFrame;
    }
    return Flight{f.blend*k/(1.0f-d*(1.0f-f.blend)),reach,vy,hover};
}

void Helis() {
    const burden::HeliFlight own{34.0f/60.0f,0.0003f};   // the 506's (docs/aircraft-re.md §1)
    const burden::HeliFlight same=burden::Weighed(own,1.0f);
    Check(same.lift==own.lift && same.blend==own.blend,"nothing aboard: its own params");
    Check(burden::Weighed(own,0.5f).lift==own.lift,"a ratio under 1 never lightens it");
    // The stock heli's stores (tools/make_stock_stores.py _HELI): a Hydra pod, four AGM-114s, two AIM-9X.
    const Store full[]={Holder(Spec(L"EDF6VC_RKT_"),19,19),Holder(Spec(L"EDF6VC_AGM_L_"),4,4),Holder(Spec(L"EDF6VC_AAM_S_"),2,2)};
    const Store spent[]={Holder(Spec(L"EDF6VC_RKT_"),19,0),Holder(Spec(L"EDF6VC_AGM_L_"),4,0),Holder(Spec(L"EDF6VC_AAM_S_"),2,0)};
    float kg=0.0f,pkg=0.0f;
    for(const auto& h:full)kg+=burden::HolderLoad(*h.spec,h.rounds,h.ammo).kg;
    for(const auto& h:spent)pkg+=burden::HolderLoad(*h.spec,h.rounds,h.ammo).kg;
    const float m=burden::Ratio(burden::kHeliMass506,kg,0.0f).mass,me=burden::Ratio(burden::kHeliMass506,pkg,0.0f).mass;
    Check(m>1.1f && m<1.3f && me>1.0f && me<m,"the 506's stores: a tenth to a quarter over clean, its pylons a little");
    const Flight clean=Fly(own),loaded=Fly(burden::Weighed(own,m)),pylons=Fly(burden::Weighed(own,me));
    Check(Near(clean.hover,aim::kHoverRotor,1e-4f),"clean: the measured hover rotor");
    Check(Near(loaded.hover,clean.hover*m,1e-4f) && loaded.hover<1.0f,"loaded: it needs more rotor to hover, and can");
    Check(Near(aim::HoverRotor(burden::Weighed(own,m).lift,1.0f),loaded.hover,1e-3f),"the plugin's hover reads the weighed lift");
    Check(loaded.climb5s<pylons.climb5s && pylons.climb5s<clean.climb5s && loaded.climb5s>1.0f,"climb: loaded < pylons < clean, still climbs");
    // The steady climb (L - g/60) / (1 - vdamp): what 5 s at full rotor reaches.
    Check(Near(clean.climb5s,(own.lift-aim::kHoverRotor*aim::kHoverLift)/0.05f,0.05f),"clean: its steady climb, m/s");
    Check(loaded.reach5s<pylons.reach5s && pylons.reach5s<clean.reach5s,"acceleration: loaded < pylons < clean");
    Check(loaded.top<pylons.top && pylons.top<clean.top && loaded.top>0.7f*clean.top,"top speed: a little lower loaded");
    std::printf("506 + stores x%.3f (spent x%.3f): hover %.3f -> %.3f, climb %.1f -> %.1f m/s, 5 s %.1f -> %.1f m/s, top %.1f -> %.1f m/s\n",
                m,me,clean.hover,loaded.hover,clean.climb5s,loaded.climb5s,clean.reach5s,loaded.reach5s,clean.top,loaded.top);
}
}  // namespace

int main() {
    Pylons();
    Holders();
    Jets();
    Helis();
    std::printf("store_burden_test: %d checks passed\n",checks);
    return 0;
}
