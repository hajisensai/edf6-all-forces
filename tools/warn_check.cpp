// The cockpit's warnings without the game (src/warn.cpp compiled as it is, src/jetaudio.cpp included whole so its own
// helpers are reachable): the ground-proximity cases ClosureIn / GpwsOf must call, the launch detection WarnTick does on
// the threats it is handed, the callouts' scheduler (jetaudio.cpp Callouts: priority, cutting short, repeats, a missile
// once) against a voice that only records what it is told, and every warning sound written out as a WAV to listen to
// (the ones made here and, when Windows has a voice, the spoken ones). No sound is played: the XAudio2 engine is never
// started.
//
//   warn_check [--out DIR]      (DIR: where the WAVs go; default %TEMP%\edf6_warn_check)
//
// Exit code 0: every check passed.
#include "../src/jetaudio.cpp"
#include "../src/warn.h"
#include "../src/gear.h"
#include "../src/body506.h"
#include <cstdarg>
#include <cstdio>
#include <string>

namespace crew {
unsigned char* image=nullptr;
PlayerFix player{};
namespace {
Config config{};
// The stand-in world: flat ground at y = 0 and one box (a building, a hill's face) when `boxOn`.
bool boxOn=false;
float boxLo[3],boxHi[3];
// What the player flies this frame (WarnTick asks PlayerJetHud).
bool flying=false;
PlayerJetReadout flown{};
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
float GameEffectVolume() noexcept { return 1.0f; }
// The map ray (heli.cpp's): metres along a->b to the nearest of the ground and the box, -1 with none.
float MapRay(const float* a,const float* b,float* hit) noexcept {
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    float best=2.0f;
    if(a[1]>0.0f && b[1]<=0.0f)best=a[1]/(a[1]-b[1]);
    if(boxOn) {
        float t0=-1e30f,t1=1e30f;
        bool miss=false;
        for(int i=0;i<3 && !miss;++i) {
            if(std::fabs(d[i])<1e-9f){miss=a[i]<boxLo[i] || a[i]>boxHi[i];continue;}
            float u=(boxLo[i]-a[i])/d[i],w=(boxHi[i]-a[i])/d[i];
            if(u>w){const float x=u;u=w;w=x;}
            t0=u>t0 ? u : t0;t1=w<t1 ? w : t1;
        }
        if(!miss && t0<=t1 && t0>=0.0f && t0<=1.0f && t0<best)best=t0;
    }
    if(best>1.0f)return -1.0f;
    for(int i=0;i<3;++i)hit[i]=a[i]+d[i]*best;
    return best*std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}
bool PlayerJetHud(PlayerJetReadout* out) noexcept { if(flying)*out=flown;return flying; }
bool PlayerHeliHud(PlayerHeliReadout*) noexcept { return false; }
bool GearHudLatest(GearHud*) noexcept { return false; }
}  // namespace crew

using namespace crew;

namespace {
// --- The ground-proximity warning ---
Gpws Case(float y,float vx,float vy,float vz,float gentle,float* t) {
    const float pos[3]={0.0f,y,0.0f},vel[3]={vx,vy,vz};
    bool rising=false;
    *t=ClosureIn(pos,vel,vy,y,gentle,kTerrainSeconds,&rising);
    return GpwsOf(*t,rising);
}
void Ground() {
    float t;
    Check(Case(50.0f,0.0f,0.0f,100.0f,10.0f,&t)==Gpws::none,"level at 50 m over flat ground: none");
    Check(Case(60.0f,0.0f,-30.0f,100.0f,10.0f,&t)==Gpws::pullUp && std::fabs(t-2.0f)<0.01f,"diving 30 m/s from 60 m: PULL UP in 2 s");
    Check(Case(80.0f,0.0f,-15.0f,100.0f,10.0f,&t)==Gpws::sinkRate && t>5.0f && t<5.5f,"sinking 15 m/s from 80 m: SINK RATE (5.3 s)");
    Check(Case(20.0f,0.0f,-5.0f,70.0f,10.0f,&t)==Gpws::none,"a landing's glide (5 m/s at 20 m, the ground ahead): none");
    Check(Case(200.0f,0.0f,-15.0f,100.0f,10.0f,&t)==Gpws::none,"sinking 15 m/s from 200 m (13 s out): none");
    boxOn=true;
    boxLo[0]=-30.0f;boxLo[1]=0.0f;boxLo[2]=400.0f;boxHi[0]=30.0f;boxHi[1]=150.0f;boxHi[2]=460.0f;
    Check(Case(50.0f,0.0f,0.0f,100.0f,10.0f,&t)==Gpws::terrain && std::fabs(t-4.0f)<0.01f,"level at 50 m, a 150 m building 400 m ahead: TERRAIN in 4 s");
    boxLo[2]=250.0f;
    Check(Case(50.0f,0.0f,0.0f,100.0f,10.0f,&t)==Gpws::pullUp && std::fabs(t-2.5f)<0.01f,"...250 m ahead: PULL UP in 2.5 s");
    boxLo[2]=700.0f;boxHi[2]=760.0f;
    Check(Case(50.0f,0.0f,0.0f,100.0f,10.0f,&t)==Gpws::none,"...700 m ahead (7 s): none");
    boxOn=false;
    Check(Case(30.0f,0.0f,-6.0f,0.0f,7.0f,&t)==Gpws::none,"a heli coming down at the descent key's 6 m/s: none");
    Check(Case(30.0f,0.0f,-12.0f,0.0f,7.0f,&t)==Gpws::pullUp,"a heli falling 12 m/s from 30 m: PULL UP");
}

// --- The launch detection (WarnTick on the threats it is handed) ---
void Threats(int missiles) {
    flown.sym.threats=missiles;
    for(int i=0;i<missiles;++i){flown.sym.threatKind[i]=2;flown.sym.threatAt[i][0]=1000.0f;}
}
void Launches() {
    flying=true;flown=PlayerJetReadout{};flown.air=true;
    Warnings w{};
    WarnTick();WarnLatest(&w);
    Check(w.on==0 && w.launchAt==0,"flying, nothing coming: no warning, no launch");
    Threats(1);WarnTick();WarnLatest(&w);
    const ULONGLONG first=w.launchAt;
    Check((w.on>>kWarnMissile&1u) && first!=0,"a missile coming: MISSILE and a launch");
    Threats(0);WarnTick();Threats(1);WarnTick();WarnLatest(&w);
    Check(w.launchAt==first,"the missile flickering out and back within kMissileMemoryMs: the same launch");
    Sleep(20);
    Threats(2);WarnTick();WarnLatest(&w);
    Check(w.launchAt>first,"a second one coming: a new launch");
    Threats(1);WarnTick();WarnLatest(&w);
    const ULONGLONG second=w.launchAt;
    Sleep(1600);
    Threats(1);WarnTick();WarnLatest(&w);
    Check(w.launchAt==second,"down to one and staying: no launch");
    Threats(2);WarnTick();WarnLatest(&w);
    Check(w.launchAt>second,"one more after kMissileMemoryMs: a launch");
    flown.gpws=Gpws::pullUp;flown.stall=true;Threats(0);WarnTick();WarnLatest(&w);
    Check((w.on>>kWarnPullUp&1u) && (w.on>>kWarnStall&1u) && !(w.on>>kWarnMissile&1u),"PULL UP and STALL lit, MISSILE out");
    flying=false;WarnTick();
    Check(!WarnLatest(&w),"no aircraft: nothing published");
}

// --- The callouts' scheduler, against a voice that records ---
struct Recorder : IXAudio2SourceVoice {
    std::string played;   // a letter a clip: its first sample's value
    bool running=false;
    void STDMETHODCALLTYPE GetVoiceDetails(XAUDIO2_VOICE_DETAILS*) override {}
    HRESULT STDMETHODCALLTYPE SetOutputVoices(const XAUDIO2_VOICE_SENDS*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetEffectChain(const XAUDIO2_EFFECT_CHAIN*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnableEffect(UINT32,UINT32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DisableEffect(UINT32,UINT32) override { return S_OK; }
    void STDMETHODCALLTYPE GetEffectState(UINT32,BOOL*) override {}
    HRESULT STDMETHODCALLTYPE SetEffectParameters(UINT32,const void*,UINT32,UINT32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetEffectParameters(UINT32,void*,UINT32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS*,UINT32) override { return S_OK; }
    void STDMETHODCALLTYPE GetFilterParameters(XAUDIO2_FILTER_PARAMETERS*) override {}
    HRESULT STDMETHODCALLTYPE SetOutputFilterParameters(IXAudio2Voice*,const XAUDIO2_FILTER_PARAMETERS*,UINT32) override { return S_OK; }
    void STDMETHODCALLTYPE GetOutputFilterParameters(IXAudio2Voice*,XAUDIO2_FILTER_PARAMETERS*) override {}
    HRESULT STDMETHODCALLTYPE SetVolume(float,UINT32) override { return S_OK; }
    void STDMETHODCALLTYPE GetVolume(float*) override {}
    HRESULT STDMETHODCALLTYPE SetChannelVolumes(UINT32,const float*,UINT32) override { return S_OK; }
    void STDMETHODCALLTYPE GetChannelVolumes(UINT32,float*) override {}
    HRESULT STDMETHODCALLTYPE SetOutputMatrix(IXAudio2Voice*,UINT32,UINT32,const float*,UINT32) override { return S_OK; }
    void STDMETHODCALLTYPE GetOutputMatrix(IXAudio2Voice*,UINT32,UINT32,float*) override {}
    void STDMETHODCALLTYPE DestroyVoice() override {}
    HRESULT STDMETHODCALLTYPE Start(UINT32,UINT32) override { running=true;return S_OK; }
    HRESULT STDMETHODCALLTYPE Stop(UINT32,UINT32) override { running=false;return S_OK; }
    HRESULT STDMETHODCALLTYPE SubmitSourceBuffer(const XAUDIO2_BUFFER* b,const XAUDIO2_BUFFER_WMA*) override {
        played+=static_cast<char>('A'+reinterpret_cast<const std::int16_t*>(b->pAudioData)[0]);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FlushSourceBuffers() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Discontinuity() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExitLoop(UINT32) override { return S_OK; }
    void STDMETHODCALLTYPE GetState(XAUDIO2_VOICE_STATE*,UINT32) override {}
    HRESULT STDMETHODCALLTYPE SetFrequencyRatio(float,UINT32) override { return S_OK; }
    void STDMETHODCALLTYPE GetFrequencyRatio(float*) override {}
    HRESULT STDMETHODCALLTYPE SetSourceSampleRate(UINT32) override { return S_OK; }
};
// Run the scheduler from `from` to `to` ms in 16 ms frames with `on` lit.
void Run(unsigned on,ULONGLONG& now,ULONGLONG to) { for(;now<to;now+=16)audio::Callouts(on,now); }
void Scheduler() {
    using namespace audio;
    Recorder rec;
    calloutVoice=&rec;
    // Each callout a clip of its own letter, 500 ms long (PULL UP 'A' .. the gear 'F').
    for(int k=0;k<kCallCount;++k)ownClip[k].assign(kRate/2,static_cast<std::int16_t>(k));
    const unsigned pull=1u<<kCallPullUp,missile=1u<<kCallMissile,terrain=1u<<kCallTerrain,gear=1u<<kCallGear;
    ULONGLONG now=1000;
    Run(terrain,now,now+3000);
    Check(rec.played=="DD","TERRAIN alone: at once and again 1.5 s after it ends");
    rec.played.clear();
    Run(terrain,now,now+1100);
    Check(rec.played=="D","...and again (playing on into the next step)");
    rec.played.clear();
    Run(terrain|pull,now,now+100);
    Check(rec.played=="A","PULL UP coming on cuts TERRAIN short");
    rec.played.clear();
    Run(terrain|pull,now,now+1600);
    Check(rec.played=="AA","PULL UP again and again while it stays on; TERRAIN not between them");
    rec.played.clear();
    Run(terrain,now,now+600);
    Check(rec.played=="D","PULL UP gone (cut off): TERRAIN, cut short before, is said");
    rec.played.clear();
    Run(terrain|missile,now,now+3000);
    Check(rec.played.size()>=2 && rec.played[0]=='B' && rec.played.find('B',1)==std::string::npos,
          "a launch's MISSILE cuts TERRAIN, said once while it stays on, TERRAIN after it");
    rec.played.clear();
    Run(gear,now,now+100);
    Check(rec.played=="F","the gear's comes on: said");
    Run(0,now,now+100);
    Check(!rec.running && calling<0,"every warning out: the voice stops mid-callout");
    calloutVoice=nullptr;
    for(auto& c:ownClip)c.clear();
    callsOn=0;calling=-1;
}

// --- The sounds, written out ---
void WriteWav(const std::wstring& path,const std::vector<std::int16_t>& pcm) {
    FILE* f=nullptr;
    if(_wfopen_s(&f,path.c_str(),L"wb") || !f){std::printf("FAIL  cannot write %ls\n",path.c_str());++failures;return;}
    const std::uint32_t data=static_cast<std::uint32_t>(pcm.size()*2),riff=36+data,fmtLen=16,rate=audio::kRate,bytes=audio::kRate*2;
    const std::uint16_t pcmTag=1,channels=1,align=2,bits=16;
    std::fwrite("RIFF",1,4,f);std::fwrite(&riff,4,1,f);std::fwrite("WAVEfmt ",1,8,f);std::fwrite(&fmtLen,4,1,f);
    std::fwrite(&pcmTag,2,1,f);std::fwrite(&channels,2,1,f);std::fwrite(&rate,4,1,f);std::fwrite(&bytes,4,1,f);
    std::fwrite(&align,2,1,f);std::fwrite(&bits,2,1,f);std::fwrite("data",1,4,f);std::fwrite(&data,4,1,f);
    std::fwrite(pcm.data(),2,pcm.size(),f);
    std::fclose(f);
}
void Sounds(const std::wstring& dir) {
    using namespace audio;
    CreateDirectoryW(dir.c_str(),nullptr);
    std::vector<float> x;
    Whoops(x);madeClip[kCallPullUp]=Pcm16(x,0.8f);
    x.clear();Chime(x);madeClip[kCallTerrain]=Pcm16(x,0.7f);
    hornPcm=SynthHorn();
    int steepest=0;   // the loop's seam no steeper than its steepest step inside: whole cycles, no click
    for(std::size_t i=1;i<hornPcm.size();++i)steepest=std::abs(hornPcm[i]-hornPcm[i-1])>steepest ? std::abs(hornPcm[i]-hornPcm[i-1]) : steepest;
    Check(hornPcm.size()==kRate/2 && std::abs(hornPcm.front()-hornPcm.back())<=steepest,"the stall horn: a 0.5 s loop that meets itself");
    WriteWav(dir+L"\\made_pullup.wav",madeClip[kCallPullUp]);
    WriteWav(dir+L"\\made_chime.wav",madeClip[kCallTerrain]);
    std::vector<std::int16_t> horn;
    for(int i=0;i<4;++i)horn.insert(horn.end(),hornPcm.begin(),hornPcm.end());
    WriteWav(dir+L"\\stall_horn_2s.wav",horn);
    SpeakAllNow();
    for(int k=0;k<kCallCount;++k)
        if(!spokenClip[k].empty())WriteWav(dir+L"\\spoken_"+kCallName[k]+L".wav",spokenClip[k]);
    std::printf("      the sounds are in %ls\n",dir.c_str());
}
}  // namespace

int wmain(int argc,wchar_t** argv) {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp);
    std::wstring out=std::wstring(temp)+L"edf6_warn_check";
    for(int i=1;i+1<argc;++i)if(!wcscmp(argv[i],L"--out"))out=argv[i+1];
    audio::tried=true;audio::ok=false;   // never an XAudio2 engine: nothing plays
    Ground();
    Launches();
    Scheduler();
    Sounds(out);
    std::printf("%s (%d failed)\n",failures ? "FAILED" : "all passed",failures);
    return failures ? 1 : 0;
}
