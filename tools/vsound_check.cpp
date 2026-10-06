// The ground vehicles' sounds without the game (src/vsynth.h's clips made as src/jetaudio.cpp makes them, included whole
// so its own table of names and peaks is the one checked; src/vehmix.h's decisions): every clip written out as a WAV to
// listen to, their levels measured (no clipping, no offset, loops that meet themselves and hold their level, a report
// punchier near than far), the mix's rules against cases (the revs, the tracks, the turret, the report's near / far fade
// and its delay, which guns are main guns, the loader's cues against a fire interval and a magazine's reload), and a
// scenario mixed down as the plugin mixes it frame by frame (a tank starting, idling, driving off, turning its turret,
// firing near, reloading, a second gun firing far off), its peak under full scale. No sound is played: the XAudio2
// engine is never started.
//
//   vsound_check [--out DIR]      (DIR: where the WAVs go; default %TEMP%\edf6_vsound_check)
//
// Exit code 0: every check passed.
#include "../src/jetaudio.cpp"
#include "../src/vehmix.h"
#include "../src/vecmath.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace crew {
namespace {
Config config{};
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}
}  // namespace
const Config& Cfg() noexcept { return config; }
void Log(const char* format,...) noexcept {
    std::printf("      log: ");
    va_list a;va_start(a,format);std::vprintf(format,a);va_end(a);
    std::printf("\n");
}

namespace {
namespace s=vsynth;
using audio::kClipCount;

void WriteWav(const std::wstring& path,const std::vector<std::int16_t>& pcm,std::uint16_t channels) {
    FILE* f=nullptr;
    if(_wfopen_s(&f,path.c_str(),L"wb") || !f){std::printf("FAIL  cannot write %ls\n",path.c_str());++failures;return;}
    const std::uint32_t data=static_cast<std::uint32_t>(pcm.size()*2),riff=36+data,fmtLen=16,rate=s::kRate,bytes=s::kRate*2u*channels;
    const std::uint16_t pcmTag=1,align=static_cast<std::uint16_t>(2*channels),bits=16;
    std::fwrite("RIFF",1,4,f);std::fwrite(&riff,4,1,f);std::fwrite("WAVEfmt ",1,8,f);std::fwrite(&fmtLen,4,1,f);
    std::fwrite(&pcmTag,2,1,f);std::fwrite(&channels,2,1,f);std::fwrite(&rate,4,1,f);std::fwrite(&bytes,4,1,f);
    std::fwrite(&align,2,1,f);std::fwrite(&bits,2,1,f);std::fwrite("data",1,4,f);std::fwrite(&data,4,1,f);
    std::fwrite(pcm.data(),2,pcm.size(),f);
    std::fclose(f);
}
std::vector<float> Floats(const std::vector<std::int16_t>& pcm) {
    std::vector<float> x(pcm.size());
    for(std::size_t i=0;i<pcm.size();++i)x[i]=static_cast<float>(pcm[i])/32768.0f;
    return x;
}
double Rms(const std::vector<float>& x,std::size_t from,std::size_t to) {
    double e=0.0;
    for(std::size_t i=from;i<to && i<x.size();++i)e+=static_cast<double>(x[i])*x[i];
    return std::sqrt(e/static_cast<double>(to>from ? to-from : 1));
}
// The share of `x`'s energy (from..to) under `lowHz` and over `highHz` (two biquads each way).
void Bands(const std::vector<float>& x,std::size_t from,std::size_t to,float lowHz,float highHz,double* low,double* high) {
    s::Biquad l1=s::Lp(lowHz),l2=s::Lp(lowHz),h1=s::Hp(highHz),h2=s::Hp(highHz);
    double all=1e-12,lo=0.0,hi=0.0;
    for(std::size_t i=0;i<x.size() && i<to;++i) {
        const float a=l2.Run(l1.Run(x[i])),b=h2.Run(h1.Run(x[i]));
        if(i<from)continue;
        all+=static_cast<double>(x[i])*x[i];lo+=static_cast<double>(a)*a;hi+=static_cast<double>(b)*b;
    }
    *low=lo/all;*high=hi/all;
}

// --- The clips ---
std::vector<std::int16_t> pcm[kClipCount];
void Clips(const std::wstring& dir) {
    using namespace audio;
    for(int c=0;c<kClipCount;++c) {
        pcm[c]=Pcm16(MadeClip(c),kClipPeak[c]);
        const std::vector<float> x=Floats(pcm[c]);
        char what[160];
        float peak=0.0f;double mean=0.0;bool finite=true;
        for(float v:x){peak=std::fabs(v)>peak ? std::fabs(v) : peak;mean+=v;finite=finite && std::isfinite(v);}
        mean/=static_cast<double>(x.size());
        const double rms=Rms(x,0,x.size());
        sprintf_s(what,"%ls: %.2f s, peak %.2f (no clip: under 0.99), offset %.4f, rms %.3f",kClipName[c],
                  static_cast<double>(x.size())/s::kRate,peak,mean,rms);
        Check(finite && peak>0.5f && peak<0.99f && std::fabs(mean)<0.01 && rms>0.02,what);
        if(kClipLoops[c]) {
            // The seam no steeper than the steepest few steps inside (the loop meets itself), and the level across it
            // (0.2 s round the wrap) no louder or quieter than anywhere else in the loop (no swell, no dip).
            std::vector<float> steps;
            for(std::size_t i=1;i<x.size();++i)steps.push_back(std::fabs(x[i]-x[i-1]));
            std::sort(steps.begin(),steps.end());
            const float seam=std::fabs(x.front()-x.back()),p999=steps[steps.size()*999/1000];
            const std::size_t w=static_cast<std::size_t>(s::kRate/5),n=x.size();
            auto window=[&](std::size_t from){ double e=0.0;for(std::size_t i=0;i<w;++i){const float v=x[(from+i)%n];e+=static_cast<double>(v)*v;}
                                               return std::sqrt(e/static_cast<double>(w)); };
            double lo=1e9,hi=0.0;
            for(std::size_t from=0;from+w<=n;from+=w/4){const double r=window(from);lo=r<lo ? r : lo;hi=r>hi ? r : hi;}
            const double wrap=window(n-w/2);
            sprintf_s(what,"%ls: a seamless loop (seam %.4f against the 99.9th step %.4f; level round the wrap %.3f, inside %.3f..%.3f)",
                      kClipName[c],seam,p999,wrap,lo,hi);
            Check(seam<=p999 && wrap>=lo*0.9 && wrap<=hi*1.1,what);
        }
        WriteWav(dir+L"\\"+kClipName[c]+L".wav",pcm[c],1);
    }
    // What each is for, in its sound.
    double lowNear,highNear,lowFar,highFar,lowI,highI,lowL,highL,lowH,highH,lowLi,highLi;
    const std::vector<float> nearX=Floats(pcm[kClipGunNear]),farX=Floats(pcm[kClipGunFar]);
    Bands(nearX,0,s::kRate/2,120.0f,2000.0f,&lowNear,&highNear);
    Bands(farX,0,s::kRate/2,120.0f,2000.0f,&lowFar,&highFar);
    char what[200];
    const double crest=1.0/Rms(nearX,0,s::kRate/2);
    sprintf_s(what,"the report near: punch under 120 Hz %.0f%% of its first 0.5 s, crack over 2 kHz %.1f%%, crest %.1f",lowNear*100.0,highNear*100.0,crest);
    Check(lowNear>0.2 && highNear>0.01 && crest>3.0,what);
    sprintf_s(what,"the report far: the highs gone (over 2 kHz %.2f%% against near's %.1f%%), the rumble kept (under 120 Hz %.0f%%)",highFar*100.0,
              highNear*100.0,lowFar*100.0);
    Check(highFar<highNear*0.2 && lowFar>0.2,what);
    Bands(Floats(pcm[kClipHeavyIdle]),0,s::kRate*2,120.0f,2000.0f,&lowI,&highI);
    Bands(Floats(pcm[kClipHeavyLoad]),0,s::kRate*2,120.0f,2000.0f,&lowL,&highL);
    sprintf_s(what,"the heavy engine: its load layer brighter than its idle (over 2 kHz %.1f%% against %.1f%%)",highL*100.0,highI*100.0);
    Check(highL>highI,what);
    Bands(Floats(pcm[kClipHeavyIdle]),0,s::kRate*2,120.0f,2000.0f,&lowH,&highH);
    Bands(Floats(pcm[kClipLightIdle]),0,s::kRate*2,120.0f,2000.0f,&lowLi,&highLi);
    sprintf_s(what,"the heavy idle deeper than the light one (under 120 Hz %.0f%% against %.0f%%)",lowH*100.0,lowLi*100.0);
    Check(lowH>lowLi,what);
}

// --- The rules ---
void Rules() {
    using namespace vmix;
    bool ok=true;
    float last=-1.0f;
    for(float sp=0.0f;sp<=30.0f;sp+=1.0f){const float r=Revs(sp,0.3f,kHeavy);ok=ok && r>=last && r>=0.0f && r<=1.0f;last=r;}
    last=-1.0f;
    for(float l=0.0f;l<=1.0f;l+=0.1f){const float r=Revs(5.0f,l,kHeavy);ok=ok && r>=last;last=r;}
    Check(ok,"the revs rise with the speed and with the load, 0..1");
    const EngineMix idle=Engine(Revs(0.0f,0.0f,kHeavy),0.0f,1.0f,kHeavy),full=Engine(Revs(16.0f,1.0f,kHeavy),1.0f,1.0f,kHeavy);
    const EngineMix off=Engine(0.0f,0.0f,0.0f,kHeavy);
    Check(idle.idle>full.idle && full.load>idle.load && std::fabs(idle.ratio-1.0f)<1e-4f && full.ratio>2.0f && full.ratio<=kHeavy.revPitch+1e-4f,
          "the engine: idle layer at rest at its own pitch, the load layer and the pitch up driven hard");
    Check(off.idle==0.0f && off.load==0.0f,"the engine off: silent");
    Check(Tracks(0.0f,s::kTrackSpeed).gain==0.0f && Tracks(8.0f,s::kTrackSpeed).gain>Tracks(2.0f,s::kTrackSpeed).gain &&
          std::fabs(Tracks(s::kTrackSpeed,s::kTrackSpeed).ratio-1.0f)<1e-4f,"the tracks: silent standing, louder with speed, as made at their speed");
    Check(Turret(0.0f).gain==0.0f && Turret(1.0f).gain>Turret(0.3f).gain && Turret(0.3f).gain>0.0f && Turret(1.0f).ratio>Turret(0.0f).ratio,
          "the turret: silent still, louder and higher slewing faster");
    const GunMix g20=Gun(20.0f),g60=Gun(kGunNear),g700=Gun(kGunFar),g1500=Gun(1500.0f);
    Check(g20.farGain==0.0f && g20.nearGain==1.0f && g700.nearGain<1e-3f && g1500.farGain>0.0f,"the report: all near close by, all far from kGunFar");
    ok=true;
    GunMix prev=Gun(1.0f);
    for(float d=2.0f;d<3000.0f;d*=1.05f) {
        const GunMix m=Gun(d);
        ok=ok && std::fabs(m.nearGain-prev.nearGain)<0.08f && std::fabs(m.farGain-prev.farGain)<0.08f && m.nearGain<=prev.nearGain+1e-6f;
        prev=m;
    }
    Check(ok && std::fabs(g60.delay-kGunNear/kSoundSpeed)<1e-5f && std::fabs(g1500.delay-1500.0f/340.0f)<1e-4f,
          "the report: no jump anywhere in its fade, delayed by the distance at 340 m/s");
    // The main guns (docs/sound-re.md §9): the stock tanks', the E551's beam shell, the howitzer; not the rest.
    struct Case { const char* what; Round r; bool homing; int frames; bool looped; float volume; bool main; };
    const Case cases[]={
        {"the Blacker's cannon (RocketBullet01, 120 frames)",Round::cannon,false,120,false,0.8f,true},
        {"the E551's (EfsBullet, 150 frames)",Round::beam,false,150,false,0.8f,true},
        {"the howitzer (GrenadeBullet01, 300 frames)",Round::grenade,false,300,false,1.0f,true},
        {"the Titan's sub cannon (RocketBullet01, 80 frames)",Round::cannon,false,80,false,0.9f,true},
        {"the Kepler's gatling (SolidBullet01, 3 frames, looped)",Round::other,false,3,true,0.6f,false},
        {"the Striker's cannon (RocketBullet01, 20 frames)",Round::cannon,false,20,false,0.75f,false},
        {"the Begaruta's grenade (GrenadeBullet01, 40 frames)",Round::grenade,false,40,false,0.7f,false},
        {"the drill (the Blacker's cannon, its fire sound silent)",Round::cannon,false,120,false,0.0f,false},
        {"the Proteus' beam (EfsExposureBullet, looped)",Round::beam,false,1,true,0.4f,false},
        {"a homing launcher",Round::other,true,300,false,0.8f,false},
        {"the Barga's cannon (looped fire)",Round::cannon,false,180,true,1.0f,false},
    };
    for(const Case& c:cases) {
        char what[200];
        sprintf_s(what,"%s: %s",c.what,c.main ? "a main gun" : "not a main gun");
        Check(MainGun(c.r,c.homing,c.frames,c.looped,c.volume)==c.main,what);
    }
}

// The loader's cues of a gun waiting `interval` frames between shots, a magazine of `rounds` reloaded over `reload`
// frames (0: none), as GunStep sees them frame by frame (the stock counters as 0x6981D6 sets them: the interval after a
// shot, 20 frames after the last, then the reload). The frames each cue came on.
struct Heard3 { int eject[8],load[8],close[8]; int ejects,loads,closes; };
Heard3 Loader(int interval,int rounds,int reload,int shots) {
    Heard3 h{};
    float wait=0.0f,before=0.0f,total=0.0f,since=1e9f;
    int ammo=rounds,left=0,fired=0;
    float cooldown=0.0f;
    for(int f=0;f<4000;++f) {
        // the game's frame: fire as soon as ready, count down
        bool shot=false;
        if(cooldown<=0.0f && left<=0 && ammo>0 && fired<shots) {
            shot=true;++fired;--ammo;
            cooldown=static_cast<float>(interval);
            if(ammo==0 && reload>0){cooldown=20.0f;left=-1;}
        } else if(cooldown>0.0f)cooldown-=1.0f;
        else if(left<0)left=reload;
        else if(left>0 && --left==0)ammo=rounds;
        wait=cooldown>0.0f || ammo>0 || reload<=0 ? cooldown : static_cast<float>(left>0 ? left : 0);
        // GunStep's
        const float sinceBefore=since;
        if(wait>before+1.0f)total=wait;
        since=shot ? 0.0f : since+1.0f;
        const float cycle=std::fmax(total,static_cast<float>(interval));
        const unsigned c=vmix::ReloadCues(before,wait,cycle,shot ? -1.0f : sinceBefore,since);
        if((c&vmix::kCueEject) && h.ejects<8)h.eject[h.ejects++]=f;
        if((c&vmix::kCueLoad) && h.loads<8)h.load[h.loads++]=f;
        if((c&vmix::kCueClose) && h.closes<8)h.close[h.closes++]=f;
        before=wait;
    }
    return h;
}
void Loaders() {
    char what[220];
    const Heard3 tank=Loader(180,25,0,3);   // the Blacker: a shot every 3 s
    sprintf_s(what,"a tank gun, 180 frames a shot, 3 shots: cases out at %d/%d/%d, rammed at %d/%d, closed at %d/%d",tank.eject[0],tank.eject[1],tank.eject[2],
              tank.load[0],tank.load[1],tank.close[0],tank.close[1]);
    // shots at frames 0, 181, 362: the cases 24 frames after each; the next round rammed 54 frames before it is ready
    // (each wait's last 54 frames: 181 - 54 = 127), closed 16 before (165); the last shot's wait too (no shot follows,
    // but the gun is loaded again).
    Check(tank.ejects==3 && tank.eject[0]==24 && tank.eject[1]==205 && tank.loads==3 && tank.load[0]==126 && tank.closes==3 &&
          tank.close[0]==164 && tank.load[0]<tank.close[0],what);
    const Heard3 fast=Loader(60,100,0,4);   // an autoloader's second: nothing
    Check(fast.ejects==0 && fast.loads==0 && fast.closes==0,"a gun of 60 frames a shot: no loader to hear");
    const Heard3 mag=Loader(40,2,300,2);    // two quick shots, then a 5 s reload
    sprintf_s(what,"a two-round magazine reloaded over 300 frames: rammed %d time(s) at %d, closed at %d",mag.loads,mag.load[0],mag.close[0]);
    Check(mag.loads==1 && mag.closes==1 && mag.load[0]>200 && mag.close[0]>mag.load[0],what);
}

// --- The scenario ---
// A loop played at a varying rate (linear interpolation) and gain into `out` from `from` for `n` samples.
struct Player {
    const std::vector<float>* x;
    double at=0.0;
    void Mix(std::vector<float>& out,std::size_t from,std::size_t n,float ratio,float gain0,float gain1) {
        const std::size_t len=x->size();
        for(std::size_t i=0;i<n && from+i<out.size();++i) {
            const std::size_t a=static_cast<std::size_t>(at)%len,b=(a+1)%len;
            const double f=at-std::floor(at);
            const float g=gain0+(gain1-gain0)*static_cast<float>(i)/static_cast<float>(n);
            out[from+i]+=g*static_cast<float>((*x)[a]*(1.0-f)+(*x)[b]*f);
            at+=ratio;
            if(at>=static_cast<double>(len))at-=static_cast<double>(len);
        }
    }
};
void OneShot(std::vector<float>& out,const std::vector<float>& x,std::size_t from,float gain) {
    for(std::size_t i=0;i<x.size() && from+i<out.size();++i)out[from+i]+=gain*x[i];
}
void Scenario(const std::wstring& dir) {
    using namespace vmix;
    using namespace audio;
    constexpr float kSec=16.0f,kPan=0.70710678f;   // the camera behind the tank: both ears alike
    const int frames=static_cast<int>(kSec*60.0f),spf=s::kRate/60;
    std::vector<float> out(static_cast<std::size_t>(frames*spf),0.0f);
    std::vector<float> clip[kClipCount];
    for(int c=0;c<kClipCount;++c)clip[c]=Floats(pcm[c]);
    Player idle{&clip[kClipHeavyIdle]},load{&clip[kClipHeavyLoad]},tracks{&clip[kClipTracks]},turret{&clip[kClipTurret]};
    float on=0.0f,speed=0.0f,engineLoad=0.0f,slew=0.0f,gIdle=0.0f,gLoad=0.0f,gTracks=0.0f,gTurret=0.0f;
    const float camera=14.0f;   // m: the riding camera behind the tank
    const float atEngine=vmix::Falloff(camera,kEngineRef,1.0f),atTracks=vmix::Falloff(camera,kTracksRef,1.0f);
    const float atTurret=vmix::Falloff(camera,kTurretRef,1.2f),atLoader=vmix::Falloff(camera,kReloadRef,1.3f);
    bool moving=false;
    for(int f=0;f<frames;++f) {
        const float t=static_cast<float>(f)/60.0f;
        // what the tank does: in at 0.5 s, idles, drives off at 3 s to 12 m/s by 7 s, slews its turret 7.5-9.5 s,
        // fires at 10 s (its loader after), and a second tank fires 900 m off at 12 s
        on=vec::Approach(on,t>0.5f ? 1.0f : 0.0f,1.0f/60.0f/1.2f);
        const float wantLoad=t>3.0f && t<7.0f ? 0.9f : t>=7.0f ? 0.35f : 0.0f;
        engineLoad+=(wantLoad-engineLoad)*0.05f;
        speed=vec::Approach(speed,t>3.0f ? 12.0f : 0.0f,3.0f/60.0f);
        const float want=t>7.5f && t<9.5f ? 1.0f : 0.0f;
        slew=vec::Approach(slew,want,1.0f/60.0f/0.3f);
        const EngineMix e=Engine(Revs(speed,engineLoad,kHeavy),engineLoad,on,kHeavy);
        const TrackMix tr=Tracks(speed,s::kTrackSpeed);
        const TurretMix tu=Turret(slew);
        const float ge=kEngineShare*atEngine*kPan,nIdle=ge*e.idle,nLoad=ge*e.load;
        const float nTracks=kEngineShare*atTracks*kPan*tr.gain,nTurret=kTurretShare*atTurret*kPan*tu.gain;
        const std::size_t at=static_cast<std::size_t>(f*spf);
        idle.Mix(out,at,static_cast<std::size_t>(spf),e.ratio,gIdle,nIdle);
        load.Mix(out,at,static_cast<std::size_t>(spf),e.ratio,gLoad,nLoad);
        tracks.Mix(out,at,static_cast<std::size_t>(spf),tr.ratio,gTracks,nTracks);
        turret.Mix(out,at,static_cast<std::size_t>(spf),tu.ratio,gTurret,nTurret);
        gIdle=nIdle;gLoad=nLoad;gTracks=nTracks;gTurret=nTurret;
        if(slew>kTurretMoving)moving=true;
        else if(moving && slew<kTurretStill){moving=false;OneShot(out,clip[kClipTurretStop],at,kTurretShare*atTurret*kPan*0.8f);}
    }
    auto at=[&](float sec){ return static_cast<std::size_t>(sec*static_cast<float>(s::kRate)); };
    const GunMix close=Gun(camera),away=Gun(900.0f);
    OneShot(out,clip[kClipGunNear],at(10.0f+close.delay),kGunShare*kPan*close.nearGain);
    OneShot(out,clip[kClipGunFar],at(10.0f+close.delay),kGunShare*kPan*close.farGain);
    OneShot(out,clip[kClipEject],at(10.0f+vmix::kEjectFrames/60.0f),kReloadShare*kPan*atLoader);
    OneShot(out,clip[kClipLoad],at(13.0f-vmix::kLoadFrames/60.0f),kReloadShare*kPan*atLoader);
    OneShot(out,clip[kClipClose],at(13.0f-vmix::kCloseFrames/60.0f),kReloadShare*kPan*atLoader);
    OneShot(out,clip[kClipGunNear],at(12.0f+away.delay),kGunShare*kPan*away.nearGain);
    OneShot(out,clip[kClipGunFar],at(12.0f+away.delay),kGunShare*kPan*away.farGain);
    float peak=0.0f;
    for(float v:out)peak=std::fabs(v)>peak ? std::fabs(v) : peak;
    char what[160];
    sprintf_s(what,"the scenario at full volume: peak %.2f of full scale (each ear; under 1: no clipping)",peak);
    Check(peak<1.0f && peak>0.3f,what);
    const double engineRms=Rms(out,at(1.5f),at(2.5f)),gunRms=Rms(out,at(10.05f),at(10.35f));
    sprintf_s(what,"the gun over the engine: %.1f dB louder in its first 0.3 s than the idle",20.0*std::log10(gunRms/engineRms));
    Check(gunRms>engineRms*4.0,what);
    std::vector<std::int16_t> pcmOut(out.size()*2);
    for(std::size_t i=0;i<out.size();++i)
        pcmOut[2*i]=pcmOut[2*i+1]=static_cast<std::int16_t>(std::lround((out[i]>1.0f ? 1.0f : out[i]<-1.0f ? -1.0f : out[i])*32767.0f));
    WriteWav(dir+L"\\scenario_tank.wav",pcmOut,2);
}
}  // namespace
}  // namespace crew

int wmain(int argc,wchar_t** argv) {
    using namespace crew;
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp);
    std::wstring out=std::wstring(temp)+L"edf6_vsound_check";
    for(int i=1;i+1<argc;++i)if(!wcscmp(argv[i],L"--out"))out=argv[i+1];
    audio::tried=true;audio::ok=false;   // never an XAudio2 engine: nothing plays
    CreateDirectoryW(out.c_str(),nullptr);
    Clips(out);
    Rules();
    Loaders();
    Scenario(out);
    std::printf("      the sounds are in %ls\n",out.c_str());
    std::printf(failures ? "%d check(s) FAILED\n" : "all checks passed\n",failures);
    return failures ? 1 : 0;
}
