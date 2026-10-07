// The Katyusha's rack (src/katyusha_rack.h) against the launcher weapon stepped as the game steps it, offline:
//  - the weapon: a salvo's first round sets the burst's rounds to come (FireBurstCount - 1, 0x690C75), each frame the wait
//    (+0xE0C) falls by one (0x693A58), a further round of the burst goes when it is spent (0x6940DF), each round fires
//    muzzle (AmmoCount - rounds left) % 16 (0x690C84 / 0x6940EA -> 0x6B3640) and, when the weapon counts its rounds,
//    takes one off; after a round the wait is the burst interval while the burst goes on, FireInterval after its last
//    (0x6981E0); tools/make_katyusha.py ROCKETS: 160 rounds, salvos of 16, 4 frames apart, 240 frames between;
//  - over every salvo of the load, at every frame: the rockets off the rack are exactly the ones whose muzzle has fired
//    since the rack was last full (so for each of 16..0 rounds left in the rack the right ones are gone), none is half
//    off, and the rack shows the rounds still in it;
//  - between salvos the rack is loaded, rocket 0 first, never unloading, each rocket sliding onto its stop and never
//    back, and it is full again (every rocket on its stop) by the time the next salvo may go;
//  - the last salvo: the rack stays empty after it (ReloadTime -1); with a reload it is loaded over the reload;
//  - fewer rounds left than rockets: only the ones the next rounds fire from are on the rack.
// Exit code 1 when one fails. cmake --build build --target katyusha_rack_check && build\katyusha_rack_check.exe
// --dump prints the rack (each rocket's on[k]) in a few states, one line each, for tools/katyusha_rack_view.py.
#include "../src/katyusha_rack.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using crew::rack::kRockets;
int failures=0;

void Expect(bool ok,const char* what,int a=0,int b=0) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%d, %d)\n",what,a,b);
}

// The launcher weapon as the game steps it (the file comment), with the Katyusha's parameters.
struct Weapon {
    int capacity=160,rounds=160,burst=16,burstLeft=0,burstInterval=4,interval=240,reloadTime=-1,reloadLeft=0;
    float wait=0.0f;
    int Shot() {   // the muzzle this round leaves from
        const int muzzle=(capacity-rounds)%kRockets;
        wait=static_cast<float>(burstLeft>0 ? burstInterval : interval);
        --rounds;
        return muzzle;
    }
    // One frame with the trigger held: -1 or the muzzle fired.
    int Step() {
        wait=wait>1.0f ? wait-1.0f : 0.0f;
        if(rounds<=0 || wait>0.0f)return -1;
        if(burstLeft>0){--burstLeft;return Shot();}
        burstLeft=burst-1;
        return Shot();
    }
    crew::rack::Launcher State() const { return {rounds,burst,burstLeft,wait,interval,reloadTime,reloadLeft}; }
};

// A load fired off salvo by salvo, checked every frame (the file comment). Returns the salvos fired.
int FireLoad(Weapon w,const char* tag) {
    bool gone[kRockets]{};
    float last[kRockets];
    for(float& x:last)x=1.0f;
    int salvos=0,frames=0,fullBefore=0,spent=0;
    bool loading=false;
    while(frames<100000) {
        ++frames;
        const int muzzle=w.Step();
        if(muzzle>=0) {
            Expect(!loading || w.burstLeft==w.burst-1,tag,muzzle,w.burstLeft);
            if(w.burstLeft==w.burst-1){++salvos;for(bool& g:gone)g=false;loading=false;}
            Expect(!gone[muzzle],"a muzzle fires twice in a salvo",muzzle,salvos);
            gone[muzzle]=true;
        }
        float on[kRockets];
        crew::rack::Rockets(w.State(),on);
        const bool salvoOver=w.burstLeft==0 && (w.wait>0.0f || w.rounds<=0);
        if(!salvoOver || muzzle>=0) {   // in the salvo: off exactly the fired ones, the rest on their stops
            for(int k=0;k<kRockets;++k)Expect(on[k]==(gone[k] ? 0.0f : 1.0f),tag,k,frames);
        } else {                        // loading (or spent): never unloading, rocket 0 first
            loading=true;
            for(int k=0;k<kRockets;++k) {
                Expect(on[k]>=last[k],"a rocket goes back while the rack is loaded",k,frames);
                if(k>0)Expect(on[k]<=on[k-1],"a rocket loaded before the one ahead of it",k,frames);
            }
            const bool due=w.rounds>0 && w.wait<=(1.0f-crew::rack::kLoadedBy)*static_cast<float>(w.interval);
            for(int k=0;due && k<kRockets;++k)Expect(on[k]==1.0f,"the rack not full before the next salvo may go",k,frames);
            if(due && w.wait<=1.0f)++fullBefore;
        }
        for(int k=0;k<kRockets;++k)last[k]=on[k];
        if(w.rounds<=0 && w.burstLeft==0 && ++spent>=600)break;   // spent: watched 10 s more (it stays empty)
    }
    for(float x:last)Expect(x==0.0f,"the rack not empty after the last salvo",static_cast<int>(x*100.0f));
    Expect(fullBefore==salvos-1,"the rack seen full the frame before each next salvo",fullBefore,salvos);
    return salvos;
}

void Load() {
    Weapon w;
    Expect(FireLoad(w,"the rack against the salvos fired")==10,"160 rounds are 10 salvos");
}

void Ends() {
    float on[kRockets];
    crew::rack::Rockets({160,16,0,0.0f,240,-1,0},on);
    for(int k=0;k<kRockets;++k)Expect(on[k]==1.0f,"a full load: the rack full",k);
    crew::rack::Rockets({0,16,0,0.0f,240,-1,0},on);
    for(int k=0;k<kRockets;++k)Expect(on[k]==0.0f,"spent for good: the rack empty",k);
    // With a reload: empty while it waits (+0xE68 still the whole reload), half loaded half way, full at its end.
    crew::rack::Rockets({0,16,0,0.0f,240,600,600},on);
    for(int k=0;k<kRockets;++k)Expect(on[k]==0.0f,"the reload not begun: empty",k);
    crew::rack::Rockets({0,16,0,0.0f,240,600,600-static_cast<int>(600.0f*crew::rack::kLoadedBy/2.0f)},on);
    for(int k=0;k<kRockets;++k)Expect(on[k]==(k<8 ? 1.0f : 0.0f),"half the reload's loading: the first 8 on",k);
    crew::rack::Rockets({0,16,0,0.0f,240,600,0},on);
    for(int k=0;k<kRockets;++k)Expect(on[k]==1.0f,"the reload done: full",k);
    // A quarter of a rocket's turn into its loading: on its rail, 3/4 of kLoad short of its stop.
    crew::rack::Rockets({160,16,0,240.0f*(1.0f-crew::rack::kLoadedBy*0.25f/16.0f),240,-1,0},on);
    Expect(std::fabs(on[0]-0.25f)<1e-4f && on[1]==0.0f,"rocket 0 a quarter in",static_cast<int>(on[0]*100.0f));
    // Fewer rounds left than rockets: the next rounds fire muzzles 16 - rounds .. 15.
    crew::rack::Rockets({5,16,0,0.0f,240,-1,0},on);
    for(int k=0;k<kRockets;++k)Expect(on[k]==(k>=11 ? 1.0f : 0.0f),"5 rounds left: the last 5 rockets",k);
    // A state not read as a salvo or a loading (negative counts, no interval): the rack full.
    crew::rack::Rockets({160,16,-3,0.0f,0,-1,0},on);
    for(int k=0;k<kRockets;++k)Expect(on[k]==1.0f,"an odd state: the rack full",k);
    Expect(crew::rack::kRockets==16 && std::fabs(crew::rack::kLoad-0.8f)<1e-6f,"the model's 16 rockets and LOAD");
}

// The states tools/katyusha_rack_view.py draws: label, then the 16 values.
void Dump() {
    const struct { const char* label; crew::rack::Launcher w; } states[]={
        {"full",{160,16,0,0.0f,240,-1,0}},
        {"half_fired",{152,16,8,4.0f,240,-1,0}},
        {"empty",{144,16,0,240.0f,240,-1,0}},
        {"loading",{144,16,0,240.0f*(1.0f-crew::rack::kLoadedBy*8.5f/16.0f),240,-1,0}},
        {"spent",{0,16,0,0.0f,240,-1,0}},
    };
    for(const auto& s:states) {
        float on[kRockets];
        crew::rack::Rockets(s.w,on);
        std::printf("%s",s.label);
        for(float x:on)std::printf(" %.4f",x);
        std::printf("\n");
    }
}
}  // namespace

int main(int argc,char** argv) {
    if(argc>1 && std::strcmp(argv[1],"--dump")==0){Dump();return 0;}
    Load();
    Ends();
    if(failures){std::printf("katyusha_rack_check: %d failures\n",failures);return 1;}
    std::printf("katyusha_rack_check: ok (10 salvos of 16 against the rack frame by frame, the ends)\n");
    return 0;
}
