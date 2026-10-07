// The ground vehicles' sounds without the game (src/vsynth.h's clips made as src/jetaudio.cpp makes them, included whole
// so its own table of names and peaks is the one checked; src/vehmix.h's decisions): every clip written out as a WAV to
// listen to, their levels measured (no clipping, no offset, loops that meet themselves and hold their level, a report
// punchier near than far), the mix's rules against cases (the revs, the tracks, the turret, the report's near / far fade
// and its delay, which guns are main guns, the loader's cues against a fire interval and a magazine's reload), and a
// scenario mixed down as the plugin mixes it frame by frame (a tank starting, idling, driving off, turning its turret,
// firing near, reloading, a second gun firing far off), its peak under full scale; the calibres (vehmix.h kProfiles):
// the stock weapons each told apart, their loaders' steps in order and inside their intervals, no case where there is
// none, their reports and cases lower and longer the bigger they are, and a scenario of the big guns firing together;
// the Sazabi's clips, its mix's rules and a scenario of its own the same way. No sound is played: the XAudio2 engine is
// never started.
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
    // Every weapon's calibre (docs/sound-re.md §9.5): the stock vehicle weapons' and the generated ones' numbers (the
    // dumps of Root.cpk; tools/make_artillery.py, make_katyusha.py, pylib/vcobjects.py): round, homing, looped fire sound,
    // FireInterval, FireBurstCount, fire volume, AmmoDamage, AmmoExplosion, AmmoSpeed.
    struct BoreCase { const char* what; GunFacts f; Bore bore; };
    const BoreCase bores[]={
        {"the Blacker's cannon (RocketBullet01, 120 frames, 350)",{Round::cannon,false,false,120,1,0.8f,350.0f,8.0f,8.0f},Bore::tank},
        {"the E551's cannon (EfsBullet, 150 frames, 400)",{Round::beam,false,false,150,1,0.8f,400.0f,12.0f,8.0f},Bore::tank},
        {"the Begaruta's long cannon (RocketBullet01, 160 frames, 400)",{Round::cannon,false,false,160,1,0.8f,400.0f,20.0f,6.0f},Bore::tank},
        {"the Titan's main gun (RocketBullet01, 300 frames, 750, blast 30)",{Round::cannon,false,false,300,1,1.0f,750.0f,30.0f,6.0f},Bore::heavy},
        {"the Titan's sub gun (RocketBullet01, 80 frames, 200)",{Round::cannon,false,false,80,1,0.9f,200.0f,8.0f,8.0f},Bore::medium},
        {"the Begaruta's cannon (RocketBullet01, 80 frames, 100)",{Round::cannon,false,false,80,1,0.8f,100.0f,5.0f,8.0f},Bore::medium},
        {"the 403's rail gun (SolidBullet01Rail, 180 frames, 600)",{Round::rail,false,false,180,1,0.9f,600.0f,0.0f,15.0f},Bore::rail},
        {"the Blacker's rail gun (SolidBullet01Rail, 100 frames)",{Round::rail,false,false,100,1,0.9f,350.0f,0.0f,15.0f},Bore::rail},
        {"the 403's four-round rail gun (burst 4)",{Round::rail,false,false,180,4,0.9f,200.0f,0.0f,15.0f},Bore::rail},
        {"the Titan's sub rail gun (120 frames, 80)",{Round::rail,false,false,120,1,0.9f,80.0f,0.0f,15.0f},Bore::rail},
        {"the howitzer (GrenadeBullet01, 300 frames, 2500)",{Round::grenade,false,false,300,1,1.0f,2500.0f,25.0f,4.0f},Bore::howitzer},
        {"the Katyusha (GrenadeBullet01, 40 a ripple, 600 frames)",{Round::grenade,false,false,600,40,1.0f,300.0f,12.0f,2.0f},Bore::rocket},
        {"the Titan's side launcher (GrenadeBullet01, 8 a salvo, 0.5 m a frame)",{Round::grenade,false,false,80,8,0.8f,120.0f,18.0f,0.5f},Bore::grenade},
        {"the Begaruta's grenade (40 frames)",{Round::grenade,false,false,40,1,0.7f,80.0f,12.0f,2.0f},Bore::grenade},
        {"the Begaruta's grenade pod (8 a salvo, 420 frames)",{Round::grenade,false,false,420,8,0.7f,60.0f,8.0f,2.0f},Bore::grenade},
        {"the Blacker's DLC grenade cannon (180 frames, 90)",{Round::grenade,false,false,180,1,0.8f,90.0f,6.0f,3.0f},Bore::grenade},
        {"the Kepler's grenade gun (5 frames)",{Round::grenade,false,false,5,1,0.53f,9.0f,4.0f,5.0f},Bore::mg},
        {"the 409's bomb (0.25 m a frame: let fall)",{Round::grenade,false,false,240,4,0.8f,500.0f,20.0f,0.25f},Bore::stock},
        {"a jet's Mk 82 (0.05 m a frame, 1500)",{Round::grenade,false,false,8,1,0.8f,1500.0f,25.0f,0.05f},Bore::stock},
        {"the Kepler's gatling (3 frames, looped)",{Round::gun,false,true,3,1,0.6f,3.0f,0.0f,10.0f},Bore::mg},
        {"the 506's door gatling (3 frames, looped)",{Round::gun,false,true,3,1,0.6f,10.0f,0.0f,10.0f},Bore::mg},
        {"the 403's machine gun (4 frames, looped)",{Round::gun,false,true,4,1,0.64f,6.0f,0.0f,10.0f},Bore::mg},
        {"the Striker's cannon (RocketBullet01, 20 frames)",{Round::cannon,false,false,20,1,0.75f,32.0f,5.0f,8.0f},Bore::autocannon},
        {"the robot truck's rifle (SolidBullet01, 30 frames)",{Round::gun,false,false,30,1,0.75f,24.0f,0.0f,10.0f},Bore::autocannon},
        {"the 410's heavy gun (SolidBullet01, 180 frames, 600)",{Round::gun,false,false,180,1,0.75f,600.0f,0.0f,10.0f},Bore::autocannon},
        {"the heli's missile (MissileBullet01)",{Round::missile,false,false,300,4,0.9f,120.0f,12.0f,2.0f},Bore::missile},
        {"the Naegling's rockets (MissileBullet01, 10 a salvo)",{Round::missile,false,false,240,10,0.71f,100.0f,8.0f,2.0f},Bore::missile},
        {"a guided missile (homing)",{Round::missile,true,false,300,1,0.9f,120.0f,12.0f,2.0f},Bore::missile},
        {"the 612's laser rifle (EfsExposureBullet, looped)",{Round::beam,false,true,1,1,0.6f,10.0f,0.0f,10.0f},Bore::stock},
        {"the Proteus' beam (EfsExposureBullet, looped)",{Round::beam,false,true,1,1,0.4f,10.0f,0.0f,10.0f},Bore::stock},
        {"the 506's laser cannon (LaserBullet01)",{Round::other,false,false,120,5,0.56f,100.0f,0.0f,10.0f},Bore::stock},
        {"a flamethrower (FlameBullet02)",{Round::other,false,true,2,1,0.7f,6.0f,0.0f,1.0f},Bore::stock},
        {"a homing laser (HomingLaserBullet01)",{Round::other,true,false,1,10,0.71f,20.0f,2.0f,2.0f},Bore::stock},
        {"the Barga's cannon (looped fire, 180 frames)",{Round::cannon,false,true,180,1,1.0f,500.0f,10.0f,5.0f},Bore::stock},
        {"the drill (the Blacker's cannon, silent)",{Round::cannon,false,false,120,1,0.0f,350.0f,8.0f,8.0f},Bore::stock},
    };
    const char* const boreNames[]={"stock","machine gun","autocannon","grenade launcher","75-105 mm gun","120 mm tank gun",
                                   "155 mm howitzer","super-heavy gun","rail gun","rocket rails","missile"};
    static_assert(sizeof(boreNames)/sizeof(boreNames[0])==static_cast<std::size_t>(Bore::count));
    for(const BoreCase& c:bores) {
        char what[220];
        sprintf_s(what,"%s: %s",c.what,boreNames[static_cast<int>(c.bore)]);
        Check(ProfileOf(c.f)==c.bore,what);
    }
    // The table: each row its own calibre; a report has its near and far clips, a round its clip, a burst its two
    // loops; the main guns in the gun group, the launches in the missile group; no case where the real one has none (a
    // rail gun's, the rockets', a separate-loading gun's, a missile's), a case where it has one; a loader only on guns
    // whose wait is a loader's; every step's clip a one-shot.
    bool table=true,cases=true,steps=true;
    for(int b=0;b<static_cast<int>(Bore::count);++b) {
        const Profile& pf=kProfiles[b];
        const bool report=pf.fire==Fire::report,burst=pf.fire==Fire::burst;
        table=table && static_cast<int>(pf.bore)==b && (b==0)==(pf.fire==Fire::none) && (b==0)==(pf.group==Group::none) &&
              (b==0 || (pf.nearClip>=0 && (report || burst)==(pf.farClip>=0) && pf.share>0.0f && pf.share<=1.0f && pf.ref>0.0f)) &&
              (report==(pf.group==Group::main)) && (pf.steps>=0 && pf.steps<=kMostSteps);
        for(int i=0;i<pf.steps;++i)steps=steps && pf.step[i].clip>=0 && !audio::kClipLoops[pf.step[i].clip] && pf.step[i].frames>0.0f &&
                                         pf.step[i].pitch>0.5f && pf.step[i].pitch<2.0f;
        if(pf.casing.clip>=0)cases=cases && (audio::kClipLoops[pf.casing.clip]==burst) && pf.casing.delay>=0.0f &&
                                   pf.casing.muffle>=0.0f && pf.casing.muffle<1.0f;
    }
    Check(table,"the calibres' table: each row its calibre, its clips and its group (a report: the main guns')");
    const Bore none[]={Bore::rail,Bore::rocket,Bore::howitzer,Bore::heavy,Bore::missile},some[]={Bore::mg,Bore::autocannon,
                       Bore::grenade,Bore::medium,Bore::tank};
    for(const Bore b:none)cases=cases && ProfileFor(b).casing.clip<0;
    for(const Bore b:some)cases=cases && ProfileFor(b).casing.clip>=0;
    Check(cases,"the cases: none for a rail gun, rockets, a separate-loading gun (howitzer, super-heavy), a missile; one for the rest");
    Check(steps && ProfileFor(Bore::mg).steps==0 && ProfileFor(Bore::autocannon).steps==0 && ProfileFor(Bore::missile).steps==0 &&
          ProfileFor(Bore::tank).casing.muffle>0.0f,"the loaders: one-shot steps on the guns with a loader, none on a belt or a launch; "
          "the tank gun's stub lands inside the turret (muffled)");
    Check(!RemoteShot(0,0,0) && RemoteShot(5,6,5) && RemoteShot(5,8,5) && !RemoteShot(5,6,6) && !RemoteShot(6,6,3) && !RemoteShot(7,5,3),
          "another machine's shot: its count sent past the copy's own (not a local shot sent, not a count going back)");
    Check(std::fabs(BurstRatio(60.0f/s::kMgRate,s::kMgRate)-1.0f)<1e-4f && BurstRatio(1.0f,s::kGatlingRate)==kBurstRatioHi &&
          BurstRatio(30.0f,s::kMgRate)==kBurstRatioLo,"a burst: as made at its own rate, its pitch held within its bounds");
    Check(Firing(0.0f,3.0f) && Firing(6.0f,3.0f) && !Firing(7.0f,3.0f) && Firing(4.0f,1.0f) && !Firing(5.0f,1.0f),
          "a gun fires its interval and kBurstHold frames past each round, no longer");
}

// The loader's steps of a gun of calibre `pf` waiting `interval` frames between shots, a magazine of `rounds` reloaded
// over `reload` frames (0: none), as GunStep sees them frame by frame (the stock counters as 0x6981D6 sets them: the
// interval after a shot, 20 frames after the last, then the reload). The frames each step came on, and the shots'.
struct Heard3 { int at[vmix::kMostSteps][8]; int n[vmix::kMostSteps]; int shotAt[8]; int shots; };
Heard3 Loader(const vmix::Profile& pf,int interval,int rounds,int reload,int shots) {
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
        if(shot && h.shots<8)h.shotAt[h.shots++]=f;
        // GunStep's
        const float sinceBefore=since;
        if(wait>before+1.0f)total=wait;
        since=shot ? 0.0f : since+1.0f;
        const float cycle=std::fmax(total,static_cast<float>(interval));
        const unsigned c=vmix::ReloadCues(pf,before,wait,cycle,shot ? -1.0f : sinceBefore,since);
        for(int i=0;i<pf.steps;++i)if((c&(1u<<i)) && h.n[i]<8)h.at[i][h.n[i]++]=f;
        before=wait;
    }
    return h;
}
// Each step heard once a shot, in the table's order, after the shot and before the next one is ready (the interval's
// end, or the reload's).
bool InOrder(const vmix::Profile& pf,const Heard3& h,int shots,int interval) {
    bool ok=true;
    for(int k=0;k<shots;++k) {
        int last=h.shotAt[k];
        for(int i=0;i<pf.steps;++i) {
            ok=ok && h.n[i]==shots && h.at[i][k]>last && h.at[i][k]<=h.shotAt[k]+interval;
            last=h.at[i][k];
        }
    }
    return ok;
}
void Loaders() {
    using vmix::Bore; using vmix::ProfileFor;
    char what[260];
    const vmix::Profile& tank=ProfileFor(Bore::tank);
    const Heard3 t=Loader(tank,180,25,0,3);   // the Blacker: a shot every 3 s
    sprintf_s(what,"a tank gun, 180 frames a shot, 3 shots: cases out at %d/%d/%d, rammed at %d/%d, closed at %d/%d",t.at[0][0],t.at[0][1],
              t.at[0][2],t.at[1][0],t.at[1][1],t.at[2][0],t.at[2][1]);
    // shots at frames 0, 181, 362: the cases 24 frames after each; the next round rammed 54 frames before it is ready
    // (each wait's last 54 frames: 181 - 54 = 127), closed 16 before (165); the last shot's wait too (no shot follows,
    // but the gun is loaded again).
    Check(t.n[0]==3 && t.at[0][0]==24 && t.at[0][1]==205 && t.n[1]==3 && t.at[1][0]==126 && t.n[2]==3 && t.at[2][0]==164 &&
          InOrder(tank,t,3,181),what);
    const Heard3 fast=Loader(tank,60,100,0,4);   // an autoloader's second: nothing
    Check(fast.n[0]==0 && fast.n[1]==0 && fast.n[2]==0,"a gun of 60 frames a shot: no loader to hear");
    const Heard3 mag=Loader(tank,40,2,300,2);    // two quick shots, then a 5 s reload
    sprintf_s(what,"a two-round magazine reloaded over 300 frames: rammed %d time(s) at %d, closed at %d",mag.n[1],mag.at[1][0],mag.at[2][0]);
    Check(mag.n[1]==1 && mag.n[2]==1 && mag.at[1][0]>200 && mag.at[2][0]>mag.at[1][0],what);
    // Each calibre's loader at the shortest interval it is given (docs/sound-re.md §9.5; the 75-105 mm guns' 80 frames
    // are an autoloader's: from kReloadFrames) and at a longer one: every step once a shot, in order, inside the wait.
    struct LoaderCase { Bore bore; int interval; };
    const LoaderCase loaders[]={{Bore::medium,90},{Bore::medium,240},{Bore::tank,120},{Bore::tank,180},{Bore::howitzer,300},
                                {Bore::howitzer,200},{Bore::heavy,300},{Bore::rail,100},{Bore::rail,180},{Bore::rocket,600},
                                {Bore::rocket,300}};
    for(const LoaderCase& c:loaders) {
        const vmix::Profile& pf=ProfileFor(c.bore);
        const Heard3 h=Loader(pf,c.interval,25,0,3);
        int len=sprintf_s(what,"%s loader, %d frames a shot: ",c.bore==Bore::medium ? "a 75-105 mm gun's" : c.bore==Bore::tank ?
                          "a tank gun's" : c.bore==Bore::howitzer ? "the howitzer's" : c.bore==Bore::heavy ? "the super-heavy gun's" :
                          c.bore==Bore::rail ? "the rail gun's" : "the rocket rails'",c.interval);
        for(int i=0;i<pf.steps && len>0 && len<200;++i)len+=sprintf_s(what+len,sizeof(what)-static_cast<std::size_t>(len),"%d ",h.at[i][0]);
        Check(InOrder(pf,h,3,c.interval+1),what);
    }
    // The howitzer's separate loading in its order: breech open, the shell, three charges, breech closed, the primer.
    const vmix::Profile& how=ProfileFor(Bore::howitzer);
    Check(how.steps==7 && how.step[0].clip==audio::kClipBreechOpen && how.step[1].clip==audio::kClipShellRam &&
          how.step[2].clip==audio::kClipCharge && how.step[3].clip==audio::kClipCharge && how.step[4].clip==audio::kClipCharge &&
          how.step[5].clip==audio::kClipClose && how.step[6].clip==audio::kClipPrimer,
          "the howitzer: breech opened, shell rammed, three charges, breech closed, primer (no case of its own)");
    const vmix::Profile& rail=ProfileFor(Bore::rail);
    Check(rail.steps==2 && rail.step[0].clip==audio::kClipRailCharge && std::fabs(rail.step[0].frames-s::kRailChargeSec*60.0f)<1.0f &&
          rail.step[1].clip==audio::kClipRailReady,"the rail gun: its capacitors charging the clip's length before it is ready, then its click");
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
    const Profile& tank=ProfileFor(Bore::tank);   // its loader's steps at a 180-frame interval (3 s), its stub case
    for(int i=0;i<tank.steps;++i) {
        const Step& st=tank.step[i];
        OneShot(out,clip[st.clip],at(10.0f+(st.fromReady ? 3.0f-st.frames/60.0f : st.frames/60.0f)),kReloadShare*kPan*atLoader);
    }
    OneShot(out,clip[tank.casing.clip],at(10.0f+tank.casing.delay),kBrassShare*kPan*atLoader);
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

// The small guns' scenario: a bike riding by (its engine revving up), a Kepler-like gatling firing a 2 s burst at 30 m
// and stopping (its tail), a machine gun's short bursts, an autocannon's rounds, a missile launched, all as GunStep
// plays them (the loops at their burst ratio, the rounds one-shots, the cases near).
void SmallScenario(const std::wstring& dir) {
    using namespace vmix;
    using namespace audio;
    constexpr float kSec=12.0f,kPan=0.70710678f;
    const int frames=static_cast<int>(kSec*60.0f),spf=s::kRate/60;
    std::vector<float> out(static_cast<std::size_t>(frames*spf),0.0f);
    std::vector<float> clip[kClipCount];
    for(int c=0;c<kClipCount;++c)clip[c]=Floats(pcm[c]);
    Player idle{&clip[kClipBikeIdle]},load{&clip[kClipBikeLoad]},gat{&clip[kClipGatling]},mg{&clip[kClipMg]},brass{&clip[kClipBrass]};
    float on=0.0f,speed=0.0f,bikeLoad=0.0f,gI=0.0f,gL=0.0f,gG=0.0f,gM=0.0f,gB=0.0f;
    const float gun=30.0f,bikeAt=10.0f;
    const float atGun=Falloff(gun,kRapidRef,1.0f),atBrass=Falloff(8.0f,kReloadRef,1.3f);
    bool wasGat=false,wasMg=false;
    auto at=[&](float sec){ return static_cast<std::size_t>(sec*static_cast<float>(s::kRate)); };
    for(int f=0;f<frames;++f) {
        const float t=static_cast<float>(f)/60.0f;
        on=vec::Approach(on,1.0f,1.0f/60.0f/1.2f);
        bikeLoad+=((t>1.0f && t<4.0f ? 1.0f : 0.2f)-bikeLoad)*0.05f;
        speed=vec::Approach(speed,t>1.0f && t<4.0f ? 25.0f : 5.0f,6.0f/60.0f);
        const EngineMix e=Engine(Revs(speed,bikeLoad,kBike),bikeLoad,on,kBike);
        const float ge=kEngineShare*Falloff(bikeAt,kEngineRef,1.0f)*kPan;
        const std::size_t a=static_cast<std::size_t>(f*spf),n=static_cast<std::size_t>(spf);
        idle.Mix(out,a,n,e.ratio,gI,ge*e.idle);load.Mix(out,a,n,e.ratio,gL,ge*e.load);
        gI=ge*e.idle;gL=ge*e.load;
        const bool gatFiring=t>4.0f && t<6.0f,mgFiring=(t>7.0f && t<7.6f) || (t>8.0f && t<8.5f);
        const float nG=gatFiring ? kRapidShare*atGun*kPan : 0.0f,nM=mgFiring ? kRapidShare*atGun*kPan : 0.0f;
        const float nB=gatFiring || mgFiring ? kBrassShare*atBrass*kPan : 0.0f;
        gat.Mix(out,a,n,BurstRatio(3.0f,s::kGatlingRate),gG,nG);mg.Mix(out,a,n,BurstRatio(5.0f,s::kMgRate),gM,nM);
        brass.Mix(out,a,n,BurstRatio(3.0f,s::kBrassRate),gB,nB);
        gG=nG;gM=nM;gB=nB;
        if(wasGat && !gatFiring)OneShot(out,clip[kClipBurstTail],a,kRapidShare*atGun*kPan);
        if(wasMg && !mgFiring)OneShot(out,clip[kClipBurstTail],a,kRapidShare*atGun*kPan);
        wasGat=gatFiring;wasMg=mgFiring;
    }
    for(int k=0;k<4;++k) {
        const float t=9.2f+0.33f*static_cast<float>(k);
        OneShot(out,clip[kClipAutocannon],at(t+gun/kSoundSpeed),kAutoShare*atGun*kPan);
        OneShot(out,clip[kClipCaseSmall],at(t+0.3f),kBrassShare*atBrass*kPan);
    }
    OneShot(out,clip[kClipMissile],at(10.8f),kMissileShare*Falloff(20.0f,kMissileRef,1.0f)*kPan);
    float peak=0.0f;
    for(float v:out)peak=std::fabs(v)>peak ? std::fabs(v) : peak;
    char what[160];
    sprintf_s(what,"the small guns' scenario at full volume: peak %.2f of full scale (under 1: no clipping)",peak);
    Check(peak<1.0f && peak>0.2f,what);
    const double bike=Rms(out,at(0.5f),at(1.0f)),burst=Rms(out,at(4.5f),at(5.5f));
    sprintf_s(what,"the gatling over the bike's idle: %.1f dB louder",20.0*std::log10(burst/bike));
    Check(burst>bike*2.0,what);
    std::vector<std::int16_t> pcmOut(out.size()*2);
    for(std::size_t i=0;i<out.size();++i)
        pcmOut[2*i]=pcmOut[2*i+1]=static_cast<std::int16_t>(std::lround((out[i]>1.0f ? 1.0f : out[i]<-1.0f ? -1.0f : out[i])*32767.0f));
    WriteWav(dir+L"\\scenario_small_guns.wav",pcmOut,2);
}
// --- The calibres (src/vehmix.h kProfiles, src/vsynth.h) ---
// The spectral centroid (Hz) of `x` (from..to): its energy through octave bands 31.5 Hz .. 16 kHz, each band's at its
// centre.
double Centroid(const std::vector<float>& x,std::size_t from,std::size_t to) {
    double num=0.0,den=1e-12;
    for(float hz=31.5f;hz<17000.0f;hz*=2.0f) {
        s::Biquad b=s::Bp(hz,1.41f);
        double e=0.0;
        for(std::size_t i=0;i<x.size() && i<to;++i){const float v=b.Run(x[i]);if(i>=from)e+=static_cast<double>(v)*v;}
        num+=hz*e;den+=e;
    }
    return num/den;
}
// How long (s) `x`'s level (50 ms windows) takes to fall 30 dB under its loudest window, for good.
double DecaySec(const std::vector<float>& x) {
    const std::size_t w=static_cast<std::size_t>(s::kRate/20);
    std::vector<double> level;
    for(std::size_t from=0;from+w<=x.size();from+=w)level.push_back(Rms(x,from,from+w));
    double top=0.0;
    std::size_t at=0;
    for(std::size_t i=0;i<level.size();++i)if(level[i]>top){top=level[i];at=i;}
    std::size_t last=at;
    for(std::size_t i=at;i<level.size();++i)if(level[i]>top*0.0316)last=i;
    return static_cast<double>(last+1-at)*0.05;
}
void Calibres() {
    using namespace audio;
    char what[260];
    // The reports, smallest to biggest: each lower (centroid of its first 0.5 s, near) and longer (its fall by 30 dB).
    const int nearOf[4]={kClipGunMediumNear,kClipGunNear,kClipHowitzerNear,kClipGunHeavyNear},farOf[4]={kClipGunMediumFar,kClipGunFar,
                         kClipHowitzerFar,kClipGunHeavyFar};
    const char* const names[4]={"75-105 mm","120 mm","155 mm","super-heavy"};
    double c[4],d[4],cf[4],df[4];
    bool lower=true,longer=true;
    int len=sprintf_s(what,"the reports lower and longer with the calibre (near: centroid / 30 dB fall; far centroid):");
    for(int k=0;k<4;++k) {
        const std::vector<float> nx=Floats(pcm[nearOf[k]]),fx=Floats(pcm[farOf[k]]);
        c[k]=Centroid(nx,0,s::kRate/2);d[k]=DecaySec(nx);cf[k]=Centroid(fx,0,s::kRate);df[k]=DecaySec(fx);
        if(k>0){lower=lower && c[k]<c[k-1] && cf[k]<cf[k-1];longer=longer && d[k]>d[k-1] && df[k]>=df[k-1];}
        len+=sprintf_s(what+len,sizeof(what)-static_cast<std::size_t>(len)," %s %.0f Hz / %.2f s (%.0f Hz);",names[k],c[k],d[k],cf[k]);
    }
    Check(lower && longer,what);
    // The rail gun: a crack and a zap, not a charge's boom: less under 120 Hz than the tank gun's, more over 2 kHz.
    double rl,rh,tl,th;
    Bands(Floats(pcm[kClipRailShot]),0,s::kRate/2,120.0f,2000.0f,&rl,&rh);
    Bands(Floats(pcm[kClipGunNear]),0,s::kRate/2,120.0f,2000.0f,&tl,&th);
    sprintf_s(what,"the rail gun's discharge: under 120 Hz %.0f%% (the tank gun's %.0f%%), over 2 kHz %.1f%% (%.1f%%)",rl*100.0,tl*100.0,rh*100.0,th*100.0);
    Check(rl<tl*0.5 && rh>th*2.0,what);
    const std::vector<float> charge=Floats(pcm[kClipRailCharge]);
    const double c0=Centroid(charge,0,s::kRate*3/10),c1=Centroid(charge,charge.size()-s::kRate*4/10,charge.size()-s::kRate/10);
    sprintf_s(what,"the rail gun's capacitors whining up as they charge (centroid %.0f Hz at first, %.0f Hz at the end)",c0,c1);
    Check(c1>c0*3.0,what);
    // The 40 mm grenade launcher: low pressure, no crack: duller than the autocannon's round.
    double gl,gh,al,ah;
    Bands(Floats(pcm[kClipGrenadeShot]),0,s::kRate/4,120.0f,2000.0f,&gl,&gh);
    Bands(Floats(pcm[kClipAutocannon]),0,s::kRate/4,120.0f,2000.0f,&al,&ah);
    sprintf_s(what,"the grenade launcher's thump duller than an autocannon's round (over 2 kHz %.1f%% against %.1f%%)",gh*100.0,ah*100.0);
    Check(gh<ah,what);
    // The rocket off its rail: shorter than a missile's launch (a ripple of them).
    const double rocket=DecaySec(Floats(pcm[kClipRocketRail])),missile=DecaySec(Floats(pcm[kClipMissile]));
    sprintf_s(what,"a rocket off its rail shorter than a missile's launch (%.2f s against %.2f s)",rocket,missile);
    Check(rocket<missile,what);
    // The cases: the bigger the lower (30-40 mm brass, a 75-105 mm brass case, the 120 mm stub's steel base); the stub a
    // short clunk, the big brass case ringing and rolling on.
    const double cs=Centroid(Floats(pcm[kClipCaseSmall]),0,s::kRate/10),cm=Centroid(Floats(pcm[kClipCaseMedium]),0,s::kRate/10),
                 cb=Centroid(Floats(pcm[kClipCaseStub]),0,s::kRate/10);
    const double ds=DecaySec(Floats(pcm[kClipCaseStub])),dm=DecaySec(Floats(pcm[kClipCaseMedium]));
    sprintf_s(what,"the cases lower the bigger (centroid %.0f / %.0f / %.0f Hz), the stub's clunk shorter than the brass case's (%.2f / %.2f s)",
              cs,cm,cb,ds,dm);
    Check(cs>cm && cm>cb && ds<dm,what);
    // The 155 mm shell rammed lower and longer than a tank gun's round; a charge module no ring of steel: shorter.
    const double cr=Centroid(Floats(pcm[kClipShellRam]),0,pcm[kClipShellRam].size()),cl=Centroid(Floats(pcm[kClipLoad]),0,pcm[kClipLoad].size());
    const double dc=DecaySec(Floats(pcm[kClipCharge])),dr=DecaySec(Floats(pcm[kClipShellRam])),dl=DecaySec(Floats(pcm[kClipLoad]));
    sprintf_s(what,"the shell's ram lower and longer than a tank round's (%.0f / %.0f Hz, %.2f / %.2f s), a charge module's shorter (%.2f s)",
              cr,cl,dr,dl,dc);
    Check(cr<cl && dr>dl && dc<dr,what);
}
// The big guns together, as GunStep plays them (each shot's report near / far by its distance and delay, its case, its
// loader's steps at their frames): a rail gun 20 m off charging and firing, the howitzer's pair 15 m off (one report)
// with both loaders, a Katyusha rippling forty rockets 30 m off (one a rail every 4 frames) and latching on its next, a
// tank gun 14 m off with its stub case, the super-heavy gun 25 m off. Its peak under full scale.
void CalibreScenario(const std::wstring& dir) {
    using namespace vmix;
    using namespace audio;
    constexpr float kSec=22.0f,kPan=0.70710678f;
    std::vector<float> out(static_cast<std::size_t>(kSec*static_cast<float>(s::kRate)),0.0f);
    std::vector<float> clip[kClipCount];
    for(int c=0;c<kClipCount;++c)clip[c]=Floats(pcm[c]);
    auto at=[&](float sec){ return static_cast<std::size_t>(sec*static_cast<float>(s::kRate)); };
    // One shot of `pf` at `t` s, `d` m off, waiting `interval` frames: its report, its case, its loader.
    auto fire=[&](const Profile& pf,float t,float d,float interval,int loaders) {
        const GunMix m=Gun(d);
        if(pf.fire==Fire::report) {
            OneShot(out,clip[pf.nearClip],at(t+m.delay),pf.share*kPan*m.nearGain);
            OneShot(out,clip[pf.farClip],at(t+m.delay),pf.share*kPan*m.farGain);
        } else OneShot(out,clip[pf.nearClip],at(t+d/kSoundSpeed),pf.share*kPan*Falloff(d,pf.ref,1.0f));
        const float by=Falloff(d,kReloadRef,1.3f);
        if(pf.casing.clip>=0)OneShot(out,clip[pf.casing.clip],at(t+pf.casing.delay),kBrassShare*kPan*by);
        const float k=StepScale(pf,interval);
        for(int l=0;l<loaders;++l)
            for(int i=0;i<pf.steps;++i) {
                const Step& st=pf.step[i];
                OneShot(out,clip[st.clip],at(t+(st.fromReady ? interval-st.frames*k : st.frames*k)/60.0f),kReloadShare*kPan*by);
            }
    };
    fire(ProfileFor(Bore::rail),0.5f,20.0f,100.0f,1);
    fire(ProfileFor(Bore::howitzer),2.5f,15.0f,300.0f,2);
    for(int r=0;r<40;++r) {
        Profile rocket=ProfileFor(Bore::rocket);
        rocket.steps=r==39 ? rocket.steps : 0;   // the rails reloaded after the last
        fire(rocket,8.5f+4.0f*static_cast<float>(r)/60.0f,30.0f,600.0f,1);
    }
    fire(ProfileFor(Bore::tank),12.0f,14.0f,180.0f,1);
    fire(ProfileFor(Bore::heavy),15.0f,25.0f,300.0f,1);
    float peak=0.0f;
    for(float v:out)peak=std::fabs(v)>peak ? std::fabs(v) : peak;
    char what[200];
    sprintf_s(what,"the big guns' scenario at full volume: peak %.2f of full scale (each ear; under 1: no clipping)",peak);
    Check(peak<1.0f && peak>0.3f,what);
    const double ripple=Rms(out,at(9.0f),at(10.5f)),howitzer=Rms(out,at(2.55f),at(2.85f));
    sprintf_s(what,"the ripple heard (rms %.3f) under the howitzer's report (%.3f)",ripple,howitzer);
    Check(ripple>0.02 && howitzer>ripple,what);
    std::vector<std::int16_t> pcmOut(out.size()*2);
    for(std::size_t i=0;i<out.size();++i)
        pcmOut[2*i]=pcmOut[2*i+1]=static_cast<std::int16_t>(std::lround((out[i]>1.0f ? 1.0f : out[i]<-1.0f ? -1.0f : out[i])*32767.0f));
    WriteWav(dir+L"\\scenario_calibres.wav",pcmOut,2);
}

// --- The Sazabi (src/sazabi_sound.cpp; docs/sound-re.md §10) ---
// What each of its clips is for, in its sound (their levels and loops are checked with the rest in Clips).
void SazabiClips() {
    using namespace audio;
    char what[220];
    auto bands=[&](int c,float from,float to,float lowHz,double* low,double* high) {
        Bands(Floats(pcm[c]),static_cast<std::size_t>(from*s::kRate),static_cast<std::size_t>(to*s::kRate),lowHz,2000.0f,low,high);
    };
    double lo,hi,lo2,hi2;
    bands(kClipSzBeamShot,0.0f,0.05f,120.0f,&lo,&hi);
    bands(kClipSzBeamShot,0.2f,1.2f,120.0f,&lo2,&hi2);
    sprintf_s(what,"the beam rifle: a bright crack (over 2 kHz %.0f%% of its first 50 ms), a deep tail (under 120 Hz %.0f%% of 0.2-1.2 s)",hi*100.0,lo2*100.0);
    Check(hi>0.1 && lo2>0.3,what);
    double fhi,flo;
    bands(kClipSzFunnelShot,0.0f,0.1f,120.0f,&flo,&fhi);
    bands(kClipSzBeamShot,0.0f,0.1f,120.0f,&lo,&hi);
    sprintf_s(what,"a funnel's beam thinner than the rifle's (under 120 Hz %.1f%% against %.1f%% of their first 0.1 s)",flo*100.0,lo*100.0);
    Check(flo<lo*0.5,what);
    const std::vector<float> step=Floats(pcm[kClipSzFootstep]),land=Floats(pcm[kClipSzLand]),cannon=Floats(pcm[kClipSzCannonShot]),
                             beam=Floats(pcm[kClipSzBeamShot]);
    bands(kClipSzFootstep,0.0f,0.5f,120.0f,&lo,&hi);
    sprintf_s(what,"a footfall: heavy (under 120 Hz %.0f%% of its first 0.5 s) with the armour's ring over it (over 2 kHz %.2f%%)",lo*100.0,hi*100.0);
    Check(lo>0.4 && hi>0.0005,what);
    const double stepTail=Rms(step,s::kRate*3/10,s::kRate*12/10),landTail=Rms(land,s::kRate*3/10,s::kRate*12/10);
    sprintf_s(what,"a landing longer than a step (0.3-1.2 s: rms %.3f against %.3f)",landTail,stepTail);
    Check(landTail>stepTail*1.2,what);
    const double cannonBody=Rms(cannon,0,s::kRate),beamBody=Rms(beam,0,s::kRate),cannonTail=Rms(cannon,s::kRate,s::kRate*2);
    sprintf_s(what,"the chest cannon over the rifle (its first second's rms %.3f against %.3f) and rolling on (1-2 s: %.3f)",cannonBody,beamBody,cannonTail);
    Check(cannonBody>beamBody && cannonTail>0.03,what);
    double hum,humHi;
    bands(kClipSzSaberHum,0.0f,1.0f,300.0f,&hum,&humHi);
    sprintf_s(what,"the blade's hum: a low buzz (under 300 Hz %.0f%%) with its crackle (over 2 kHz %.1f%%)",hum*100.0,humHi*100.0);
    Check(hum>0.3 && humHi>0.01,what);
    double tlo,thi;
    bands(kClipSzThrusters,0.0f,2.0f,120.0f,&tlo,&thi);
    sprintf_s(what,"the thrusters' roar: broad, a rumble (under 120 Hz %.0f%%) and a hiss (over 2 kHz %.1f%%)",tlo*100.0,thi*100.0);
    Check(tlo>0.1 && thi>0.005,what);
}
// Its mix's rules (src/vehmix.h).
void SazabiRules() {
    using namespace vmix;
    bool silent=true,rising=true;
    for(int k=0;k<static_cast<int>(SzLoop::count);++k) {
        const SzLoop w=static_cast<SzLoop>(k);
        silent=silent && SazabiLoopMix(w,0.0f).gain==0.0f && SazabiLoopMix(w,-1.0f).gain==0.0f && SazabiLoopMix(w,NAN).gain==0.0f;
        SzLoopMix last=SazabiLoopMix(w,0.01f);
        for(float l=0.05f;l<=1.0f;l+=0.05f) {
            const SzLoopMix m=SazabiLoopMix(w,l);
            rising=rising && m.gain>=last.gain && m.ratio>=last.ratio && m.gain<=1.0f && m.ratio>0.25f && m.ratio<4.0f;
            last=m;
        }
    }
    Check(silent,"the Sazabi's loops: silent at level 0 (or none, or nonsense)");
    const float lowCharge=SazabiLoopMix(SzLoop::cannonCharge,0.05f).ratio,fullCharge=SazabiLoopMix(SzLoop::cannonCharge,1.0f).ratio;
    char what[200];
    sprintf_s(what,"the Sazabi's loops: louder and higher with their level, the charge's whine climbing %.1f octaves",std::log2(fullCharge/lowCharge));
    Check(rising && SazabiLoopMix(SzLoop::thrusters,1.0f).ratio>SazabiLoopMix(SzLoop::thrusters,0.2f).ratio && fullCharge>=1.8f*lowCharge,what);
    float g=1.0f,t=0.0f;
    while(g>0.0f && t<2.0f){g=SzFade(g,0.0f,1.0f/60.0f);t+=1.0f/60.0f;}
    float up=0.0f;
    for(int i=0;i<60;++i)up=SzFade(up,0.5f,1.0f/60.0f);
    sprintf_s(what,"a loop let go of: out in %.2f s (no click: kSzFadeOut), up to its level without passing it (%.3f)",t,up);
    Check(t>=kSzFadeOut-0.02f && t<=kSzFadeOut+0.05f && up==0.5f && SzFade(0.2f,0.2f,0.1f)==0.2f,what);
    bool sane=true;
    for(const SzSound& x:kSzSfx)sane=sane && x.share>0.0f && x.share<=1.0f && x.ref>0.0f && x.fall>=1.0f && x.jitter>=0.0f && x.jitter<0.1f;
    for(const SzSound& x:kSzLoop)sane=sane && x.share>0.0f && x.share<=1.0f && x.ref>0.0f && x.fall>=1.0f;
    Check(sane && kSzSfx[static_cast<int>(SzSfx::missileLaunch)].group==SzGroup::missile &&
          kSzSfx[static_cast<int>(SzSfx::footstep)].group==SzGroup::move && kSzSfx[static_cast<int>(SzSfx::beamShot)].group==SzGroup::gun,
          "the Sazabi's sounds: shares, distances and groups (the missiles the launches', the steps the engines', the beams the guns')");
}
// The Sazabi seen from its riding camera (35 m off): walking (a step every 0.9 s), a dash and a jump on its thrusters
// (their level up and down), landing, two rifle shots and their hits 200 m off, the blade lit, two swings and a hit, put
// out, the chest cannon charging and fired, a missile, a funnel volley 100 m off (launched, six shots, docked). Each
// sound as SazabiSfx / SazabiLoop and SazabiSoundTick hear it (its share, its distance's fall and delay, a loop eased).
void SazabiScenario(const std::wstring& dir) {
    using namespace vmix;
    using namespace audio;
    constexpr float kSec=18.0f,kPan=0.70710678f,kCam=35.0f;
    const int frames=static_cast<int>(kSec*60.0f),spf=s::kRate/60;
    std::vector<float> out(static_cast<std::size_t>(frames*spf),0.0f);
    std::vector<float> clip[kClipCount];
    for(int c=0;c<kClipCount;++c)clip[c]=Floats(pcm[c]);
    auto at=[&](float sec){ return static_cast<std::size_t>(sec*static_cast<float>(s::kRate)); };
    auto play=[&](SzSfx w,float sec,float d) {
        const SzSound& x=kSzSfx[static_cast<int>(w)];
        OneShot(out,clip[kSazabiSfxClip[static_cast<int>(w)]],at(sec+d/kSoundSpeed),x.share*Falloff(d,x.ref,x.fall)*kPan);
    };
    Player loop[3]={{&clip[kSazabiLoopClip[0]]},{&clip[kSazabiLoopClip[1]]},{&clip[kSazabiLoopClip[2]]}};
    float gain[3]{};   // each loop's level as SazabiSoundTick eases it
    for(int f=0;f<frames;++f) {
        const float t=static_cast<float>(f)/60.0f;
        const float level[3]={t>3.0f && t<5.0f ? 0.6f+0.4f*std::sin(3.0f*(t-3.0f)) : 0.0f,t>8.2f && t<10.5f ? 1.0f : 0.0f,
                              t>11.0f && t<13.0f ? (t-11.0f)/2.0f : 0.0f};
        const std::size_t a=static_cast<std::size_t>(f*spf),n=static_cast<std::size_t>(spf);
        for(int k=0;k<3;++k) {
            const SzLoopMix m=SazabiLoopMix(static_cast<SzLoop>(k),level[k]);
            const float next=SzFade(gain[k],m.gain,1.0f/60.0f),g=kSzLoop[k].share*Falloff(kCam,kSzLoop[k].ref,kSzLoop[k].fall)*kPan;
            loop[k].Mix(out,a,n,m.ratio,gain[k]*g,next*g);
            gain[k]=next;
        }
    }
    for(float t=0.3f;t<2.8f;t+=0.9f)play(SzSfx::footstep,t,kCam);
    play(SzSfx::dash,3.0f,kCam);
    play(SzSfx::land,5.4f,kCam);
    for(float t=6.0f;t<7.5f;t+=0.9f)play(SzSfx::footstep,t,kCam);
    play(SzSfx::beamShot,6.5f,kCam);play(SzSfx::beamHit,6.7f,200.0f);
    play(SzSfx::beamShot,7.2f,kCam);play(SzSfx::beamHit,7.4f,200.0f);
    play(SzSfx::saberOn,8.0f,kCam);play(SzSfx::whoosh,8.8f,kCam);play(SzSfx::whoosh,9.4f,kCam);
    play(SzSfx::saberHit,9.6f,kCam);play(SzSfx::saberOff,10.5f,kCam);
    play(SzSfx::cannonShot,13.0f,kCam);
    play(SzSfx::missileLaunch,14.0f,kCam);
    play(SzSfx::funnelLaunch,14.5f,kCam);
    for(int k=0;k<6;++k)play(SzSfx::funnelShot,15.2f+0.05f*static_cast<float>(k),100.0f);
    play(SzSfx::funnelDock,17.0f,kCam);
    float peak=0.0f;
    for(float v:out)peak=std::fabs(v)>peak ? std::fabs(v) : peak;
    char what[200];
    sprintf_s(what,"the Sazabi's scenario at full volume: peak %.2f of full scale (each ear; under 1: no clipping)",peak);
    Check(peak<1.0f && peak>0.3f,what);
    const float delay=kCam/kSoundSpeed;
    const double steps=Rms(out,at(0.3f+delay),at(0.8f+delay)),cannon=Rms(out,at(13.0f+delay),at(13.5f+delay)),thrusters=Rms(out,at(3.6f),at(4.6f));
    sprintf_s(what,"the cannon over a footstep (%.1f dB), the thrusters heard (rms %.3f)",20.0*std::log10(cannon/steps),thrusters);
    Check(cannon>steps && thrusters>0.02,what);
    std::vector<std::int16_t> pcmOut(out.size()*2);
    for(std::size_t i=0;i<out.size();++i)
        pcmOut[2*i]=pcmOut[2*i+1]=static_cast<std::int16_t>(std::lround((out[i]>1.0f ? 1.0f : out[i]<-1.0f ? -1.0f : out[i])*32767.0f));
    WriteWav(dir+L"\\scenario_sazabi.wav",pcmOut,2);
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
    SmallScenario(out);
    Calibres();
    CalibreScenario(out);
    SazabiClips();
    SazabiRules();
    SazabiScenario(out);
    std::printf("      the sounds are in %ls\n",out.c_str());
    std::printf(failures ? "%d check(s) FAILED\n" : "all checks passed\n",failures);
    return failures ? 1 : 0;
}
