// Offline A/B of the jets' engine sound (tools/jetsound_ab.py builds and runs it): the same scenes rendered through an
// older sound_engine.h ("A", tools/jetsound_ab_old.cpp) and the current one with jet_air.h ("B": the passby, its duck,
// the wind, the airframe), into mono 48 kHz WAVs of one shared scale (A and B comparable by ear and by their levels).
//
// Ours, the same for both sides: the WT recordings looped and resampled linearly, a one-pole low-pass per voice (B:
// a layer's WT cutoff where it has one, else jetaudio.h AirCutoff, as jetsound.cpp Voices; A: AirCutoff alone), the
// far gain (jet_mix.h FarGain, reach 8000 m), the Doppler from the geometry (c 343 m/s; the sound's travel time not
// delayed), no surround (a mono mixdown), gains moved per 10 ms block.
//
// jetsound_ab <wt dir> <out dir>
#include "jet_air.h"
#include "jet_mix.h"
#include "jetaudio.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct AbIn { float rpm,load,wep,kmh,share,speedRel,direction,g,seed,seed2; };
struct AbLayer { int clip; float gain,ratio,cutoff; };
int AbHearOld(int ev,const AbIn& x,float d,float cone,float doppler,AbLayer* out,float* maxD);

namespace {
using namespace crew;
constexpr int kRate=48000,kBlock=480;
constexpr float kDt=static_cast<float>(kBlock)/kRate;
constexpr float kSoundSpeed=343.0f,kReach=8000.0f,kPi=3.14159265f;

std::string wtDir,outDir;
std::vector<float> clips[wt::kCount][6];

bool LoadWav(const std::string& path,std::vector<float>& out) {
    FILE* f=nullptr;
    if(fopen_s(&f,path.c_str(),"rb") || !f)return false;
    std::vector<unsigned char> b;
    unsigned char buf[65536];
    for(std::size_t n;(n=fread(buf,1,sizeof buf,f))>0;)b.insert(b.end(),buf,buf+n);
    fclose(f);
    if(b.size()<12 || std::memcmp(b.data(),"RIFF",4) || std::memcmp(b.data()+8,"WAVE",4))return false;
    int channels=0,bits=0;
    for(std::size_t p=12;p+8<=b.size();) {
        std::uint32_t size;std::memcpy(&size,b.data()+p+4,4);
        const unsigned char* c=b.data()+p+8;
        if(!std::memcmp(b.data()+p,"fmt ",4)){std::uint16_t ch,bp;std::memcpy(&ch,c+2,2);std::memcpy(&bp,c+14,2);channels=ch;bits=bp;}
        if(!std::memcmp(b.data()+p,"data",4) && channels>0 && bits==16) {
            const std::size_t frames=size/(2u*static_cast<unsigned>(channels));
            out.resize(frames);
            for(std::size_t i=0;i<frames;++i) {
                float s=0.0f;
                for(int k=0;k<channels;++k){std::int16_t v;std::memcpy(&v,c+(i*channels+k)*2,2);s+=v/32768.0f;}
                out[i]=s/static_cast<float>(channels);
            }
            return true;
        }
        p+=8+size+(size&1u);
    }
    return false;
}
const std::vector<float>& Clip(int clip,int variant) {
    std::vector<float>& c=clips[clip][variant];
    if(c.empty()) {
        char name[256];
        std::snprintf(name,sizeof name,"%s/%ls_%d.wav",wtDir.c_str(),wt::kName[clip],variant+1);
        if(!LoadWav(name,c)){std::fprintf(stderr,"missing %s\n",name);c.assign(1,0.0f);}
    }
    return c;
}

// A voice: a clip at a rate (looping or once), through its low-pass, its gain ramped over each block.
struct Voice { int clip=-1,variant=0;bool loop=true;double pos=0.0;float lp=0.0f,gain=0.0f; };
void Play(Voice& v,float gain,float ratio,float cutoff,float* out) {
    const std::vector<float>& c=Clip(v.clip,v.variant);
    const double n=static_cast<double>(c.size());
    const float a=1.0f-std::exp(-2.0f*kPi*(cutoff<20.0f ? 20.0f : cutoff>22000.0f ? 22000.0f : cutoff)/kRate);
    for(int i=0;i<kBlock;++i) {
        if(!v.loop && v.pos>=n-1.0){v.gain=gain;return;}
        const std::size_t j=static_cast<std::size_t>(v.pos);
        const float f=static_cast<float>(v.pos-static_cast<double>(j));
        const float s=c[j]+(c[j+1<c.size() ? j+1 : 0]-c[j])*f;
        v.lp+=a*(s-v.lp);
        const float g=v.gain+(gain-v.gain)*static_cast<float>(i+1)/kBlock;
        out[i]+=g*v.lp;
        v.pos+=ratio;
        if(v.loop && v.pos>=n)v.pos-=n;
    }
    v.gain=gain;
}

// Where a jet is against the listener this block.
struct Scene {
    int ev=1;                // 0 the player's own jet, 1 another's
    AbIn in{};
    float d=100.0f,cone=180.0f,doppler=1.0f;
    float rel[3]{},relV[3]{};   // the jet from the listener (m), its velocity against it (m/s): the passby's
};
struct Side {
    bool b=false,passby=false,duck=false,wind=false,overload=false,engineOff=false;
    Voice engine[engine::kMostEventLayers];
    Voice pass[jetair::kTracks];
    bool passOn=false,armed=true,started[jetair::kTracks]{};
    float age=0.0f,kmh=0.0f,ends[jetair::kTracks]{};
    Voice air[3];
    std::vector<float> pcm;
};

void Block(Side& s,const Scene& sc) {
    float out[kBlock]{};
    AbLayer l[engine::kMostEventLayers];
    float maxD=0.0f;
    int n=0;
    if(s.b) {
        engine::Inputs in{};
        in.v[engine::kRpm]=sc.in.rpm;in.v[engine::kLoad]=sc.in.load;in.v[engine::kWep]=sc.in.wep;
        in.v[engine::kSpeed]=sc.in.kmh;in.v[engine::kSpeedShare]=sc.in.share;in.v[engine::kSpeedRel]=sc.in.speedRel;
        in.v[engine::kDirection]=sc.in.direction;in.v[engine::kG]=sc.in.g;in.v[engine::kSeed]=sc.in.seed;
        in.v[engine::kSeed2]=sc.in.seed2;
        const engine::Event& e=sc.ev==0 ? engine::kJetPlayer : engine::kJetAi;
        engine::Heard h[engine::kMostEventLayers];
        n=engine::Hear(e,in,sc.d,sc.cone,h);
        const float dop=engine::Doppler(e,sc.doppler,sc.cone);
        for(int k=0;k<n;++k)l[k]=AbLayer{h[k].clip,h[k].gain,h[k].ratio*dop,h[k].cutoff};
        maxD=e.maxD;
    } else {
        n=AbHearOld(sc.ev,sc.in,sc.d,sc.cone,sc.doppler,l,&maxD);
    }
    const float far=sc.ev==0 ? 1.0f : jetmix::FarGain(maxD,sc.d,kReach);
    const float air=audio::AirCutoff(sc.d/2500.0f);
    // The passby (B): armed, it starts at its closest approach's lead; the jet's engine ducked under it.
    float duck=1.0f;
    if(s.passby && sc.ev==1) {
        const jetair::Approach a=jetair::Closest(sc.rel,sc.relV);
        const float speed=std::sqrt(jetair::Dot3(sc.relV,sc.relV));
        if(s.armed && !s.passOn && jetair::Fires(a,speed)) {
            s.passOn=true;s.armed=false;s.age=0.0f;s.kmh=speed*3.6f;
            constexpr int kClip[jetair::kTracks]={wt::k_jet_passby_close,wt::k_jet_passby_med,wt::k_jet_passby_far,
                                                  wt::k_jet_passby_fast};
            for(int k=0;k<jetair::kTracks;++k) {
                s.pass[k]=Voice{kClip[k],0,false};
                s.started[k]=false;
                s.ends[k]=(k==jetair::kClose ? jetair::kCloseAt : 0.0f)+
                          static_cast<float>(Clip(kClip[k],0).size())/kRate;
            }
            if(s.kmh<jetair::kFastFrom)s.started[jetair::kFast]=true,s.pass[jetair::kFast].clip=-1;
            std::printf("    passby at %.0f m closest in %.2f s, %.0f km/h\n",a.d,a.t,s.kmh);
        }
        if(!s.armed && jetair::Rearms(a))s.armed=true;
        if(s.passOn) {
            const jetair::Gains g=jetair::PassbyAt(sc.d,s.kmh,0);
            bool playing=false;
            for(int k=0;k<jetair::kTracks;++k) {
                Voice& v=s.pass[k];
                if(v.clip<0)continue;
                if(!s.started[k] && s.age>=(k==jetair::kClose ? jetair::kCloseAt : 0.0f))s.started[k]=true;
                if(s.started[k] && s.age<s.ends[k]){Play(v,g.g[k],1.0f,air,out);playing=true;}
                else if(!s.started[k])playing=true;
            }
            if(s.duck)duck=jetair::DuckGain(jetair::DuckAt(s.age));
            s.age+=kDt;
            if(!playing)s.passOn=false;
        }
    }
    for(int k=0;k<n && !s.engineOff;++k) {
        Voice& v=s.engine[k];
        v.clip=l[k].clip;
        const float cutoff=s.b && l[k].cutoff>0.0f ? l[k].cutoff : air;
        Play(v,l[k].gain*far*duck,l[k].ratio,cutoff,out);
    }
    // The wind and the airframe (B, the own jet): at the camera.
    if(sc.ev==0 && (s.wind || s.overload)) {
        const jetair::Wind w=jetair::WindAt(sc.in.share,sc.in.kmh);
        const float g[3]={s.wind ? w.low : 0.0f,s.wind ? w.high : 0.0f,s.overload ? jetair::OverloadAt(sc.in.g) : 0.0f};
        constexpr int kClip[3]={wt::k_jet_wind_low,wt::k_jet_wind_high,wt::k_jet_overload};
        for(int i=0;i<3;++i){s.air[i].clip=kClip[i];Play(s.air[i],g[i],1.0f,22000.0f,out);}
    }
    s.pcm.insert(s.pcm.end(),out,out+kBlock);
}

float Rms(const std::vector<float>& p) {
    double e=0.0;
    for(float x:p)e+=static_cast<double>(x)*x;
    return p.empty() ? 0.0f : static_cast<float>(std::sqrt(e/static_cast<double>(p.size())));
}
void WriteWav(const std::string& path,const std::vector<float>& p,float scale) {
    FILE* f=nullptr;
    if(fopen_s(&f,path.c_str(),"wb") || !f){std::fprintf(stderr,"cannot write %s\n",path.c_str());return;}
    const std::uint32_t data=static_cast<std::uint32_t>(p.size()*2),riff=36+data,fmt=16,rate=kRate,bytes=kRate*2;
    const std::uint16_t pcm=1,ch=1,align=2,bits=16;
    fwrite("RIFF",1,4,f);fwrite(&riff,4,1,f);fwrite("WAVEfmt ",1,8,f);fwrite(&fmt,4,1,f);fwrite(&pcm,2,1,f);
    fwrite(&ch,2,1,f);fwrite(&rate,4,1,f);fwrite(&bytes,4,1,f);fwrite(&align,2,1,f);fwrite(&bits,2,1,f);
    fwrite("data",1,4,f);fwrite(&data,4,1,f);
    for(float x:p) {
        float y=x*scale;
        y=y>1.0f ? 1.0f : y<-1.0f ? -1.0f : y;
        const std::int16_t v=static_cast<std::int16_t>(std::lround(y*32767.0f));
        fwrite(&v,2,1,f);
    }
    fclose(f);
}

// A scene's sides rendered over `seconds`, its geometry from `at(t, scene&)`.
struct Take { std::string name;Side side; };
template<class F> void Render(std::vector<Take>& takes,float seconds,F at) {
    const int blocks=static_cast<int>(seconds/kDt);
    for(int i=0;i<blocks;++i) {
        Scene sc;
        at(static_cast<float>(i)*kDt,sc);
        for(Take& t:takes)Block(t.side,sc);
    }
}
std::vector<Take> AB(const char* scene,bool air) {
    std::vector<Take> t(air ? 3 : 2);
    t[0].name=std::string(scene)+"_A";
    t[1].name=std::string(scene)+"_B";
    t[1].side.b=true;
    if(air) {
        t[1].side.passby=t[1].side.duck=t[1].side.wind=t[1].side.overload=true;
        t[2].name=std::string(scene)+"_B_engine_only";
        t[2].side.b=true;
        t.emplace_back();
        t[3].name=std::string(scene)+"_B_air_only";
        t[3].side=t[1].side;
        t[3].side.engineOff=true;
    }
    return t;
}

AbIn AiFull() { return AbIn{engine::kJetAiTop,1.0f,0.0f,900.0f,100.0f,250.0f,0.0f,1.0f,0.0f,0.0f}; }
}  // namespace

int main(int argc,char** argv) {
    if(argc<3){std::fprintf(stderr,"jetsound_ab <wt dir> <out dir>\n");return 2;}
    wtDir=argv[1];outDir=argv[2];
    std::vector<std::vector<Take>> all;
    std::vector<std::string> notes;

    // 1. Another jet at full power, behind it (cone 180), held 2 s at each distance.
    {
        static const float ds[]={50,150,300,600,1000,1500,2000,2500,3000,4000,6000};
        auto t=AB("ai_distance",false);
        Render(t,2.0f*static_cast<float>(std::size(ds)),[&](float time,Scene& sc){
            sc.ev=1;sc.in=AiFull();sc.cone=180.0f;sc.d=ds[static_cast<int>(time/2.0f)];
        });
        all.push_back(std::move(t));
        notes.push_back("ai_distance: AI jet full power, from behind, 2 s each at 50/150/300/600/1000/1500/2000/2500/3000/4000/6000 m");
    }
    // 2. Another jet at 300 m, 2 s at each cone angle 0 (its nose at us) .. 180 (its tail).
    {
        auto t=AB("ai_angle",false);
        Render(t,14.0f,[&](float time,Scene& sc){
            sc.ev=1;sc.in=AiFull();sc.d=300.0f;sc.cone=30.0f*static_cast<float>(static_cast<int>(time/2.0f));
        });
        all.push_back(std::move(t));
        notes.push_back("ai_angle: AI jet full power 300 m off, 2 s each at cone 0/30/60/90/120/150/180 (0 = nose toward us)");
    }
    // 3. Another jet's rpm, 200 m off side-on: idle to its top over 10 s.
    {
        auto t=AB("ai_rpm",false);
        Render(t,10.0f,[&](float time,Scene& sc){
            sc.ev=1;sc.in=AiFull();sc.d=200.0f;sc.cone=90.0f;
            sc.in.rpm=engine::kJetIdle+(engine::kJetAiTop-engine::kJetIdle)*time/10.0f;sc.in.load=time/10.0f;
        });
        all.push_back(std::move(t));
        notes.push_back("ai_rpm: AI jet 200 m off side-on, rpm idle -> top over 10 s");
    }
    // 4. The player's own jet from the chase camera (15 m, behind): idle to military over 8 s, the afterburner 8..10 s,
    // then 450 -> 1100 km/h with 4 g and 8 g pulls (the wind and the airframe on B).
    {
        auto t=AB("player",true);
        Render(t,16.0f,[&](float time,Scene& sc){
            sc.ev=0;sc.d=15.0f;sc.cone=170.0f;
            const float r=time<8.0f ? time/8.0f : 1.0f;
            sc.in=AbIn{engine::kJetIdle+(engine::kJetMil-engine::kJetIdle)*r,r,0.0f,0.0f,0.0f,0.0f,0.0f,1.0f,0.0f,0.0f};
            if(time>=8.0f){sc.in.rpm=engine::kJetWep;sc.in.wep=time<9.0f ? time-8.0f : 1.0f;}
            const float kmh=time<8.0f ? 450.0f*r : 450.0f+650.0f*(time-8.0f)/8.0f;
            sc.in.kmh=kmh;sc.in.share=100.0f*kmh/1100.0f;
            sc.in.g=time>=11.0f && time<12.5f ? 4.0f : time>=13.5f && time<15.0f ? 8.0f : 1.0f;
        });
        all.push_back(std::move(t));
        notes.push_back("player: own jet from the chase camera (15 m behind); rpm idle -> military 0..8 s, afterburner 8..10 s; "
                        "450 -> 1100 km/h; 4 g at 11..12.5 s, 8 g at 13.5..15 s");
    }
    // 5. Flybys: another jet at full power past a still listener, 50 m and 150 m to its side, at 900 km/h and
    // 450 km/h, its nose along its path (B: the passby and its duck; B_engine_only: without them).
    struct Pass { const char* name;float offset,speed; };
    static const Pass passes[]={{"flyby_50m_900kmh",50.0f,250.0f},{"flyby_150m_900kmh",150.0f,250.0f},
                                {"flyby_50m_450kmh",50.0f,125.0f}};
    for(const Pass& p:passes) {
        auto t=AB(p.name,true);
        std::printf("  %s\n",p.name);
        const float from=-6.0f*p.speed;
        Render(t,12.0f,[&](float time,Scene& sc){
            sc.ev=1;sc.in=AiFull();sc.in.kmh=p.speed*3.6f;
            const float x=from+p.speed*time,y=p.offset;
            sc.d=std::sqrt(x*x+y*y);
            const float to[3]={-x/sc.d,-y/sc.d,0.0f};   // from the jet to the listener
            const float nose[3]={1.0f,0.0f,0.0f};
            sc.cone=engine::ConeAngle(nose,to);
            const float radial=p.speed*to[0];          // its speed toward the listener
            sc.doppler=kSoundSpeed/(kSoundSpeed-(radial>300.0f ? 300.0f : radial));
            sc.in.speedRel=p.speed;
            // the listener faces the path (+y): Direction 0 ahead .. 180 behind
            sc.in.direction=std::atan2(std::fabs(x),y)*57.29578f;
            sc.rel[0]=x;sc.rel[1]=y;sc.rel[2]=0.0f;
            sc.relV[0]=p.speed;sc.relV[1]=0.0f;sc.relV[2]=0.0f;
        });
        all.push_back(std::move(t));
    }
    notes.push_back("flyby_*: AI jet full power past a still listener (its closest approach at 6 s); _B adds the passby whoosh "
                    "and its 23 dB duck, _B_engine_only is B's engine alone, _B_air_only the passby (or the wind and the "
                    "airframe) alone");

    // One scale for every file: the loudest peak at -1 dBFS.
    float peak=0.0f;
    for(auto& t:all)for(Take& k:t)for(float x:k.side.pcm)peak=std::fabs(x)>peak ? std::fabs(x) : peak;
    const float scale=peak>0.0f ? 0.891f/peak : 1.0f;
    std::printf("one scale for all: x%.2f (%.1f dB)\n",scale,20.0f*std::log10(scale));
    FILE* readme=nullptr;
    const std::string rp=outDir+"/README.txt";
    fopen_s(&readme,rp.c_str(),"w");
    if(readme) {
        std::fprintf(readme,"A = sound_engine.h before this round; B = now (+ jet_air.h). One gain for every file (x%.2f).\n",scale);
        for(const std::string& n:notes)std::fprintf(readme,"%s\n",n.c_str());
        std::fprintf(readme,"\nRMS (dBFS):\n");
    }
    for(auto& t:all)for(Take& k:t) {
        WriteWav(outDir+"/"+k.name+".wav",k.side.pcm,scale);
        const float r=Rms(k.side.pcm)*scale;
        std::printf("  %-32s %6.1f dBFS rms\n",k.name.c_str(),r>0.0f ? 20.0f*std::log10(r) : -999.0f);
        if(readme)std::fprintf(readme,"  %-32s %6.1f\n",k.name.c_str(),r>0.0f ? 20.0f*std::log10(r) : -999.0f);
    }
    // ai_distance per step: A and B's level at each distance.
    std::printf("ai_distance per 2 s step (dBFS rms, A / B):\n");
    if(readme)std::fprintf(readme,"\nai_distance per step (A / B dBFS):\n");
    static const float ds[]={50,150,300,600,1000,1500,2000,2500,3000,4000,6000};
    for(int i=0;i<static_cast<int>(std::size(ds));++i) {
        float r[2];
        for(int k=0;k<2;++k) {
            const std::vector<float>& p=all[0][k].side.pcm;
            const std::size_t a=static_cast<std::size_t>(i)*2*kRate+kRate/2,b=a+kRate;
            r[k]=Rms(std::vector<float>(p.begin()+static_cast<std::ptrdiff_t>(a),p.begin()+static_cast<std::ptrdiff_t>(b)))*scale;
        }
        std::printf("  %5.0f m  %6.1f / %6.1f\n",ds[i],20.0f*std::log10(r[0]+1e-9f),20.0f*std::log10(r[1]+1e-9f));
        if(readme)std::fprintf(readme,"  %5.0f m  %6.1f / %6.1f\n",ds[i],20.0f*std::log10(r[0]+1e-9f),20.0f*std::log10(r[1]+1e-9f));
    }
    if(readme)fclose(readme);
    return 0;
}
