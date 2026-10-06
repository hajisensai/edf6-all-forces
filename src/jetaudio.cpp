// The jets' engine sound, played by the plugin (see jetaudio.h). An XAudio2 engine of its own on the default output,
// two looping mono voices per jet:
//  - the roar: the exhaust's broadband noise, heard mostly behind and to the side of a jet;
//  - the whine: the fan's blade-pass tone and its harmonics over a narrow band of noise, heard mostly ahead of it.
// Both are made here once (Synth*, seamless loops). A <dll name>_jet.wav next to the DLL (16-bit PCM, mono or
// stereo, any rate) replaces the roar and silences the whine: a recording carries both.
// Each voice has a low-pass filter (the air takes a far engine's highs) and an output matrix (left / right ear).
// A watchdog thread silences the master voice once the game stops beating (Beat): paused, loading, in a menu; the
// game's own sounds stop then too.
// The cockpit's sounds play here as well (LockTone, Warn): the lock tones, the threat beeps and the launch warble on a
// tone, the stall horn, and the warnings' callouts (PULL UP's whoops and voice, TERRAIN, SINK RATE...) one at a time on
// a voice of their own. The callouts' voice is the Windows one (SAPI 5, rendered into memory once on a thread of its
// own: an English one when there is one), or the player's WAVs next to the DLL; with neither, tones made here.
// The ground vehicles' sounds play here too (OpenLoop / PlayOnce, vehsound.cpp decides them): their clips made in
// vsynth.h on a thread of their own (MakeClips) or read from the player's WAVs, looping voices from a pool of kLoops and
// one-shots from a pool of kShots, each with its filter and its ears as the jets' are.
#include "jetaudio.h"
#include "crew.h"
#include "vsynth.h"
#include <windows.h>
#include <xaudio2.h>
#pragma warning(push,0)
#include <sapi.h>
#pragma warning(pop)
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace crew::audio {
namespace {
constexpr UINT32 kRate=48000;
constexpr int kLoopSamples=kRate*3;            // the made loops: 3 s
constexpr int kFade=kRate/4;                   // ...their ends cross-faded over 0.25 s
constexpr float kMaxRatio=4.0f;                // a voice's highest playback rate (pitch and Doppler)
constexpr int kSlots=40;
constexpr ULONGLONG kQuietMs=150;              // no beat this long: the game is not running, nor is the sound
constexpr float kNearCutoff=16000.0f,kFarCutoff=1200.0f;

IXAudio2* engine=nullptr;
IXAudio2MasteringVoice* master=nullptr;
UINT32 masterChannels=2;
bool tried=false,ok=false,whine=true;
std::vector<std::int16_t> roarPcm,whinePcm;
WAVEFORMATEX roarFormat{},whineFormat{};
struct Slot { IXAudio2SourceVoice* roar; IXAudio2SourceVoice* whine; bool used; };
// The lock tone (LockTone): a 1 kHz sine loop, whole cycles; locked it plays at kLockedRatio steadily, locking it
// beeps kBeepMs every kBeepSlow..kBeepFast ms as the lock closes in.
constexpr float kToneHz=1000.0f,kLockedRatio=1.6f,kToneVolume=0.25f;
constexpr ULONGLONG kBeepMs=70,kBeepSlow=450,kBeepFast=120,kToneStaleMs=200;
IXAudio2SourceVoice* lockVoice=nullptr;
std::vector<std::int16_t> tonePcm;
WAVEFORMATEX toneFormat{};
ULONGLONG toneAt=0,beepFrom=0;
int toneState=0;
// The threat warning (ThreatTone): the lock tone's loop on a voice of its own, lower and beeping: locked on by an
// enemy kThreatLockRatio, kThreatLockMs on in every kThreatLockPeriod; a missile coming kThreatMissileRatio,
// kThreatMissileMs on in every kThreatMissilePeriod; just launched, steady and warbling between kLaunchHigh and
// kLaunchLow every kWarbleMs (an RWR's launch tone).
constexpr float kThreatLockRatio=0.8f,kThreatMissileRatio=1.9f,kThreatVolume=0.3f,kLaunchHigh=1.35f,kLaunchLow=0.85f;
constexpr ULONGLONG kThreatLockMs=150,kThreatLockPeriod=600,kThreatMissileMs=60,kThreatMissilePeriod=120,kWarbleMs=80;
IXAudio2SourceVoice* threatVoice=nullptr;
ULONGLONG threatAt=0;
// The warnings' callouts (Warn). Each a one-shot clip at kRate from the first of: the player's WAV next to the DLL
// (<dll name>_warn_<kCallName>.wav), the Windows voice's (Cfg().warnVoice: SpeakAll renders kCallText once on a thread
// of its own; PULL UP gets two GPWS whoops before it), made here (MadeClips: PULL UP's whoops, a chime for TERRAIN,
// SINK RATE and the gear; none for the stall and the missile, whose horn and warble say it).
const wchar_t* const kCallName[kCallCount]={L"pullup",L"missile",L"stall",L"terrain",L"sinkrate",L"gear"};
const wchar_t* const kCallText[kCallCount]={L"Pull up",L"Missile",L"Stall",L"Terrain, terrain",L"Sink rate",L"Too low, gear"};
// Each again this long after it ends while it stays on; kOnce: once each time it comes on.
constexpr ULONGLONG kOnce=~0ull>>2;
const ULONGLONG kCallRepeat[kCallCount]={150,kOnce,3000,1500,2000,3000};
constexpr float kCalloutVolume=0.55f,kHornVolume=0.2f;
std::vector<std::int16_t> madeClip[kCallCount],ownClip[kCallCount],hornPcm;
// Written by SpeakAll's thread only, before spokenReady (release); read only after it (acquire).
std::vector<std::int16_t> spokenClip[kCallCount];
std::atomic<bool> spokenReady{false};
bool warnMade=false,speaking=false;
IXAudio2SourceVoice* calloutVoice=nullptr;
IXAudio2SourceVoice* hornVoice=nullptr;
int calling=-1;                    // the callout playing (-1: none), till callEnd
ULONGLONG callEnd=0,dueAt[kCallCount]{},warnAt=0;
unsigned callsOn=0;                // the callouts on last frame (a rise makes one due at once)
Slot slots[kSlots]{};
std::atomic<ULONGLONG> beatAt{0};
std::atomic<float> masterVolume{1.0f};

// --- Making the sounds ---
struct Noise {
    std::uint32_t s=0x9E3779B9u;
    float Next() noexcept { s^=s<<13;s^=s>>17;s^=s<<5;return static_cast<float>(s)/2147483648.0f-1.0f; }
};
// A one-pole low-pass at `hz`.
struct Low {
    float a,y=0.0f;
    explicit Low(float hz) noexcept : a(1.0f-std::exp(-6.2831853f*hz/static_cast<float>(kRate))) {}
    float Run(float x) noexcept { y+=a*(x-y);return y; }
};
// A band-pass (RBJ biquad, constant peak gain) at `hz`, quality `q`.
struct Band {
    float b0,b2,a1,a2,x1=0,x2=0,y1=0,y2=0;
    Band(float hz,float q) noexcept {
        const float w=6.2831853f*hz/static_cast<float>(kRate),al=std::sin(w)/(2.0f*q),a0=1.0f+al;
        b0=al/a0;b2=-al/a0;a1=-2.0f*std::cos(w)/a0;a2=(1.0f-al)/a0;
    }
    float Run(float x) noexcept { const float y=b0*x+b2*x2-a1*y1-a2*y2;x2=x1;x1=x;y2=y1;y1=y;return y; }
};

// `gen` made kLoopSamples + kFade long into a seamless loop of kLoopSamples, peak `peak`, 16-bit.
std::vector<std::int16_t> Loop(std::vector<float> gen,float peak) {
    std::vector<float> out(gen.begin(),gen.begin()+kLoopSamples);
    for(int i=0;i<kFade;++i) {   // the tail laid over the head with equal power: no click, no dip
        const float t=static_cast<float>(i)/kFade;
        out[i]=gen[i]*std::sqrt(t)+gen[kLoopSamples+i]*std::sqrt(1.0f-t);
    }
    float top=1e-6f;
    for(float x:out)top=std::fabs(x)>top ? std::fabs(x) : top;
    std::vector<std::int16_t> pcm(out.size());
    for(std::size_t i=0;i<out.size();++i)pcm[i]=static_cast<std::int16_t>(std::lround(out[i]/top*peak*32767.0f));
    return pcm;
}

// The roar: rumble (noise under 90 Hz), body (under 450 Hz) and hiss (a band round 1.8 kHz), slowly swelling.
std::vector<std::int16_t> SynthRoar() {
    Noise n;Low rumble(90.0f),rumble2(90.0f),body(450.0f),swell(3.0f);Band hiss(1800.0f,0.7f);
    std::vector<float> g(kLoopSamples+kFade);
    for(auto& x:g) {
        const float w=n.Next();
        const float s=0.8f+0.4f*swell.Run(n.Next()*8.0f);
        x=(2.6f*rumble2.Run(rumble.Run(w))+1.1f*body.Run(w)+0.35f*hiss.Run(w))*s;
    }
    return Loop(std::move(g),0.85f);
}

// The whine: the blade-pass tone kBlade and its harmonics (whole cycles in the loop: seamless on their own), a
// lower shaft tone, and the narrow band of noise round the blade-pass the tone rides on, its level wavering a little.
std::vector<std::int16_t> SynthWhine() {
    constexpr float kBlade=1400.0f,kShaft=470.0f;
    Noise n;Band band(kBlade,9.0f),band2(2.0f*kBlade,12.0f);Low waver(4.0f);
    std::vector<float> g(kLoopSamples+kFade);
    for(int i=0;i<static_cast<int>(g.size());++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate),w=n.Next();
        const float m=1.0f+0.25f*waver.Run(n.Next()*6.0f);
        const float tone=0.55f*std::sin(6.2831853f*kBlade*t)+0.22f*std::sin(6.2831853f*2.0f*kBlade*t)+
                         0.10f*std::sin(6.2831853f*3.0f*kBlade*t)+0.18f*std::sin(6.2831853f*kShaft*t);
        g[i]=(tone*m+2.2f*band.Run(w)+1.2f*band2.Run(w));
    }
    return Loop(std::move(g),0.6f);
}

// A 16-bit PCM WAV (mono or stereo) as mono samples and their rate; false: none or not that.
bool ReadWav(const wchar_t* path,std::vector<std::int16_t>& pcm,UINT32& rate) {
    const HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};GetFileSizeEx(f,&size);
    std::vector<unsigned char> d(size.QuadPart>0 && size.QuadPart<(64ll<<20) ? static_cast<std::size_t>(size.QuadPart) : 0);
    DWORD got=0;
    const bool read=!d.empty() && ReadFile(f,d.data(),static_cast<DWORD>(d.size()),&got,nullptr) && got==d.size();
    CloseHandle(f);
    if(!read || d.size()<12 || std::memcmp(d.data(),"RIFF",4) || std::memcmp(d.data()+8,"WAVE",4))return false;
    int channels=0,bits=0,format=0;
    for(std::size_t at=12;at+8<=d.size();) {
        std::uint32_t len;std::memcpy(&len,d.data()+at+4,4);
        const unsigned char* body=d.data()+at+8;
        if(at+8+len>d.size())break;
        if(!std::memcmp(d.data()+at,"fmt ",4) && len>=16) {
            format=body[0]|(body[1]<<8);channels=body[2]|(body[3]<<8);
            std::memcpy(&rate,body+4,4);bits=body[14]|(body[15]<<8);
        } else if(!std::memcmp(d.data()+at,"data",4) && format==1 && bits==16 && (channels==1 || channels==2)) {
            const std::size_t frames=len/(2*channels);
            pcm.resize(frames);
            for(std::size_t i=0;i<frames;++i) {
                std::int16_t a,b;std::memcpy(&a,body+i*2*channels,2);
                if(channels==2){std::memcpy(&b,body+i*4+2,2);pcm[i]=static_cast<std::int16_t>((a+b)/2);}
                else pcm[i]=a;
            }
            return frames>static_cast<std::size_t>(rate/10) && rate>=8000 && rate<=192000;
        }
        at+=8+len+(len&1);
    }
    return false;
}

WAVEFORMATEX Mono16(UINT32 rate) noexcept {
    WAVEFORMATEX f{};
    f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=1;f.nSamplesPerSec=rate;f.wBitsPerSample=16;f.nBlockAlign=2;
    f.nAvgBytesPerSec=rate*2;
    return f;
}

// <dll path without .dll><suffix> into `path` (MAX_PATH); false when it does not fit.
bool BesideDll(const wchar_t* suffix,wchar_t* path) noexcept {
    HMODULE self=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&BesideDll),&self);
    if(!GetModuleFileNameW(self,path,MAX_PATH))return false;
    wchar_t* dot=wcsrchr(path,L'.');
    return dot && wcscpy_s(dot,MAX_PATH-(dot-path),suffix)==0;
}

// The watchdog: silent while the game does not beat (see Beat).
DWORD WINAPI Watch(void*) {
    bool quiet=false;
    for(;;) {
        Sleep(25);
        const bool stopped=GetTickCount64()-beatAt.load()>kQuietMs;
        if(stopped && !quiet){master->SetVolume(0.0f);quiet=true;}
        else if(!stopped){master->SetVolume(masterVolume.load());quiet=false;}
    }
}

IXAudio2SourceVoice* Voice(const WAVEFORMATEX& format,const std::vector<std::int16_t>& pcm) noexcept {
    IXAudio2SourceVoice* v=nullptr;
    if(FAILED(engine->CreateSourceVoice(&v,&format,XAUDIO2_VOICE_USEFILTER,kMaxRatio)))return nullptr;
    XAUDIO2_BUFFER b{};
    b.AudioBytes=static_cast<UINT32>(pcm.size()*2);b.pAudioData=reinterpret_cast<const BYTE*>(pcm.data());
    b.LoopCount=XAUDIO2_LOOP_INFINITE;
    if(FAILED(v->SubmitSourceBuffer(&b))){v->DestroyVoice();return nullptr;}
    v->SetVolume(0.0f);
    v->Start();
    return v;
}

void Pan(IXAudio2SourceVoice* v,float left,float right) noexcept {
    float m[XAUDIO2_MAX_AUDIO_CHANNELS]{};   // mono into the master's channels: front left and right (a surround layout's first two)
    m[0]=left;m[1]=masterChannels>1 ? right : 0.5f*(left+right);
    v->SetOutputMatrix(nullptr,1,masterChannels,m);
}

// The cockpit's own share of the volume (Cfg().warnVolume, the master voice being the game's).
float WarnGain() noexcept {
    const float v=Cfg().warnVolume;
    return v<0.0f || !std::isfinite(v) ? 0.0f : v>4.0f ? 4.0f : v;
}

// --- Making the warnings' sounds ---
// Float samples to 16-bit, their peak at `peak` of full scale.
std::vector<std::int16_t> Pcm16(const std::vector<float>& x,float peak) {
    float top=1e-6f;
    for(float v:x)top=std::fabs(v)>top ? std::fabs(v) : top;
    std::vector<std::int16_t> out(x.size());
    for(std::size_t i=0;i<x.size();++i)out[i]=static_cast<std::int16_t>(std::lround(x[i]/top*peak*32767.0f));
    return out;
}
void Gap(std::vector<float>& x,float sec) { x.insert(x.end(),static_cast<std::size_t>(sec*static_cast<float>(kRate)),0.0f); }
// A GPWS whoop: a tone (its fundamental and third) sweeping up from 350 to 1300 Hz in 0.38 s, faded in and out, at
// `gain`.
void Whoop(std::vector<float>& x,float gain) {
    const int n=static_cast<int>(0.38f*static_cast<float>(kRate));
    const float in=0.01f*static_cast<float>(kRate),out=0.03f*static_cast<float>(kRate);
    float phase=0.0f;
    for(int i=0;i<n;++i) {
        const float t=static_cast<float>(i)/static_cast<float>(n),hz=350.0f*std::pow(1300.0f/350.0f,t);
        phase+=6.2831853f*hz/static_cast<float>(kRate);
        const float env=std::fmin(1.0f,static_cast<float>(i)/in)*std::fmin(1.0f,static_cast<float>(n-i)/out);
        x.push_back(gain*env*(std::sin(phase)+0.3f*std::sin(3.0f*phase))/1.3f);
    }
}
// PULL UP's two whoops (the voice, when there is one, after them).
void Whoops(std::vector<float>& x) { Whoop(x,0.8f);Gap(x,0.1f);Whoop(x,0.8f);Gap(x,0.12f); }
// A caution chime: two struck notes falling, 1050 then 750 Hz.
void Chime(std::vector<float>& x) {
    for(const float hz:{1050.0f,750.0f}) {
        const int n=static_cast<int>(0.24f*static_cast<float>(kRate));
        for(int i=0;i<n;++i) {
            const float t=static_cast<float>(i)/static_cast<float>(kRate),env=std::exp(-t*9.0f);
            x.push_back(env*std::sin(6.2831853f*hz*t)+0.25f*env*env*std::sin(6.2831853f*2.0f*hz*t));
        }
    }
}
// The stall horn: a harsh steady 420 Hz (its odd harmonics to the 9th), 0.5 s of whole cycles: a seamless loop.
std::vector<std::int16_t> SynthHorn() {
    std::vector<float> x(kRate/2);
    for(std::size_t i=0;i<x.size();++i) {
        const float t=static_cast<float>(i)/static_cast<float>(kRate);
        float v=0.0f;
        for(int k=1;k<=9;k+=2)v+=std::sin(6.2831853f*420.0f*static_cast<float>(k)*t)/static_cast<float>(k);
        x[i]=v;
    }
    return Pcm16(x,0.7f);
}
// `in` at `rate` to kRate (linear: a callout, not music).
std::vector<std::int16_t> Resampled(const std::vector<std::int16_t>& in,UINT32 rate) {
    if(rate==kRate || in.empty())return in;
    const double step=static_cast<double>(rate)/static_cast<double>(kRate);
    std::vector<std::int16_t> out(static_cast<std::size_t>(static_cast<double>(in.size()-1)/step));
    for(std::size_t i=0;i<out.size();++i) {
        const double at=static_cast<double>(i)*step;
        const std::size_t a=static_cast<std::size_t>(at);
        const double f=at-static_cast<double>(a);
        out[i]=static_cast<std::int16_t>(std::lround(in[a]*(1.0-f)+in[a+1]*f));
    }
    return out;
}

// --- The Windows voice (SAPI 5) ---
// The voice's samples (mono 16-bit at kRate, written raw into `mem`; a RIFF header skipped should there be one) as
// floats in `out`, the silence it leaves at either end cut. False: nothing said.
bool ReadSpoken(IStream* mem,std::vector<float>& out) {
    STATSTG st{};HGLOBAL h=nullptr;
    if(FAILED(mem->Stat(&st,STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(mem,&h)) || !h)return false;
    const std::size_t size=static_cast<std::size_t>(st.cbSize.QuadPart);
    const auto* p=static_cast<const unsigned char*>(GlobalLock(h));
    if(!p)return false;
    std::size_t at=0,end=size;
    if(size>=12 && !std::memcmp(p,"RIFF",4))
        for(at=12;at+8<=size;) {
            std::uint32_t len;std::memcpy(&len,p+at+4,4);
            if(!std::memcmp(p+at,"data",4)){at+=8;end=at+len<=size ? at+len : size;break;}
            at+=8+len+(len&1);
        }
    std::vector<float> x;
    for(std::size_t i=at;i+2<=end;i+=2){std::int16_t v;std::memcpy(&v,p+i,2);x.push_back(static_cast<float>(v)/32768.0f);}
    GlobalUnlock(h);
    float top=0.0f;
    for(float v:x)top=std::fabs(v)>top ? std::fabs(v) : top;
    if(top<0.01f)return false;
    std::size_t first=0,last=x.size();
    while(first<last && std::fabs(x[first])<0.02f*top)++first;
    while(last>first && std::fabs(x[last-1])<0.02f*top)--last;
    const std::size_t pad=kRate/100;
    first=first>pad ? first-pad : 0;last=last+pad<x.size() ? last+pad : x.size();
    out.clear();
    for(std::size_t i=first;i<last;++i)out.push_back(x[i]/top);
    return out.size()>kRate/20;
}
// `voice` saying `text` into `out` (ReadSpoken).
bool Speak(ISpVoice* voice,const wchar_t* text,std::vector<float>& out) {
    IStream* mem=nullptr;ISpStream* stream=nullptr;
    bool said=false;
    const WAVEFORMATEX f=Mono16(kRate);
    if(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&mem)) &&
       SUCCEEDED(CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_ALL,IID_ISpStream,reinterpret_cast<void**>(&stream))) &&
       SUCCEEDED(stream->SetBaseStream(mem,SPDFID_WaveFormatEx,&f)) && SUCCEEDED(voice->SetOutput(stream,FALSE)) &&
       SUCCEEDED(voice->Speak(text,SPF_DEFAULT|SPF_IS_NOT_XML,nullptr)))
        said=ReadSpoken(mem,out);
    if(stream){stream->Close();stream->Release();}
    if(mem)mem->Release();
    return said;
}
// An English voice (a woman's first: the warnings' "Betty") when one is installed, else the system's own; logged.
void PickVoice(ISpVoice* voice) {
    ISpObjectTokenCategory* category=nullptr;IEnumSpObjectTokens* list=nullptr;ISpObjectToken* token=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory,nullptr,CLSCTX_ALL,IID_ISpObjectTokenCategory,
                                  reinterpret_cast<void**>(&category))) &&
       SUCCEEDED(category->SetId(SPCAT_VOICES,FALSE)) && SUCCEEDED(category->EnumTokens(L"Language=409",L"Gender=Female",&list)) &&
       list->Next(1,&token,nullptr)==S_OK)
        voice->SetVoice(token);
    if(token)token->Release();
    if(list)list->Release();
    if(category)category->Release();
    ISpObjectToken* used=nullptr;
    wchar_t* name=nullptr;
    if(SUCCEEDED(voice->GetVoice(&used)) && used && SUCCEEDED(used->GetStringValue(nullptr,&name)) && name)
        Log("SOUND warnings: the callouts' voice is \"%ls\"",name);
    if(name)CoTaskMemFree(name);
    if(used)used->Release();
}
void SpeakAllNow() {
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED))){Log("SOUND warnings: no COM on the voice's thread: tones");return;}
    ISpVoice* voice=nullptr;
    if(FAILED(CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_ALL,IID_ISpVoice,reinterpret_cast<void**>(&voice))) || !voice) {
        Log("SOUND warnings: no Windows voice (SAPI 5): the callouts are tones");
        CoUninitialize();
        return;
    }
    PickVoice(voice);
    voice->SetRate(1);
    int said=0;
    for(int k=0;k<kCallCount;++k) {
        std::vector<float> words,clip;
        if(!ownClip[k].empty() || !Speak(voice,kCallText[k],words))continue;   // ownClip: written before this thread began
        if(k==kCallPullUp)Whoops(clip);
        clip.insert(clip.end(),words.begin(),words.end());
        spokenClip[k]=Pcm16(clip,0.9f);
        ++said;
    }
    voice->Release();
    CoUninitialize();
    spokenReady.store(true,std::memory_order_release);
    Log("SOUND warnings: %d callout(s) spoken by the Windows voice",said);
}
DWORD WINAPI SpeakAll(void*) {
    __try { SpeakAllNow(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { Log("SOUND warnings: the Windows voice faulted: the callouts are tones"); }
    return 0;
}

// The callouts' clips made here and read from the player's WAVs, the horn, their voices (once, on the first Warn).
void MakeWarnings() {
    warnMade=true;
    std::vector<float> x;
    Whoops(x);madeClip[kCallPullUp]=Pcm16(x,0.8f);
    x.clear();Chime(x);
    madeClip[kCallTerrain]=madeClip[kCallSinkRate]=madeClip[kCallGear]=Pcm16(x,0.7f);
    hornPcm=SynthHorn();
    for(int k=0;k<kCallCount;++k) {
        wchar_t path[MAX_PATH],suffix[48];
        std::vector<std::int16_t> pcm;UINT32 rate=kRate;
        swprintf_s(suffix,L"_warn_%ls.wav",kCallName[k]);
        if(!BesideDll(suffix,path) || !ReadWav(path,pcm,rate))continue;
        ownClip[k]=Resampled(pcm,rate);
        Log("SOUND warnings: %ls from %ls",kCallName[k],path);
    }
    const WAVEFORMATEX f=Mono16(kRate);
    if(FAILED(engine->CreateSourceVoice(&calloutVoice,&f)))calloutVoice=nullptr;
    hornVoice=Voice(f,hornPcm);
    Log("SOUND warnings: callouts=%d horn=%d",calloutVoice!=nullptr,hornVoice!=nullptr);
}

// The callout `k` sounds as: the player's, the voice's, else the one made here (maybe none: empty).
const std::vector<std::int16_t>& ClipOf(int k) noexcept {
    if(!ownClip[k].empty())return ownClip[k];
    if(Cfg().warnVoice && spokenReady.load(std::memory_order_acquire) && !spokenClip[k].empty())return spokenClip[k];
    return madeClip[k];
}
void StopCallout() noexcept {
    if(calloutVoice){calloutVoice->Stop(0);calloutVoice->FlushSourceBuffers();}
    calling=-1;
}
void Play(int k,ULONGLONG now) noexcept {
    const auto& pcm=ClipOf(k);
    StopCallout();
    XAUDIO2_BUFFER b{};
    b.Flags=XAUDIO2_END_OF_STREAM;b.AudioBytes=static_cast<UINT32>(pcm.size()*2);b.pAudioData=reinterpret_cast<const BYTE*>(pcm.data());
    if(FAILED(calloutVoice->SubmitSourceBuffer(&b)))return;
    const float v=kCalloutVolume*WarnGain();
    Pan(calloutVoice,v,v);
    calloutVoice->Start(0);
    calling=k;callEnd=now+pcm.size()*1000/kRate;
}
// The callouts this frame (see jetaudio.h Warn), a GPWS's priorities: the most urgent one on with something to say (a
// once-only one said is done) is the only one that may play, cutting a lesser one short (due again once it is over);
// while it waits out its own repeat nothing lesser starts in between. One whose warning went out stops.
void Callouts(unsigned on,ULONGLONG now) noexcept {
    if(!calloutVoice)return;
    const unsigned rose=on&~callsOn;
    callsOn=on;
    for(int k=0;k<kCallCount;++k)if(rose>>k&1u)dueAt[k]=now;
    bool busy=calling>=0 && now<callEnd;
    if(busy && !(on>>calling&1u)){StopCallout();busy=false;}
    if(!busy)calling=-1;
    for(int k=0;k<kCallCount;++k) {
        if(!(on>>k&1u) || dueAt[k]==kOnce || ClipOf(k).empty())continue;
        if(busy && k>=calling)return;   // the one playing is at least as urgent
        if(now<dueAt[k])return;         // the most urgent one between its repeats: nothing lesser in the gap
        if(busy)dueAt[calling]=now;
        Play(k,now);
        dueAt[k]=kCallRepeat[k]==kOnce ? kOnce : callEnd+kCallRepeat[k];
        return;
    }
}

// The threat tone (Warn): see kThreatLockRatio.
void ThreatTone(int state,bool launch,ULONGLONG now) noexcept {
    if(!threatVoice) {
        if(tonePcm.empty()) {   // the lock tone's loop (LockTone makes it the same way)
            const int n=kRate/10;
            tonePcm.resize(static_cast<std::size_t>(n));
            for(int i=0;i<n;++i)tonePcm[static_cast<std::size_t>(i)]=static_cast<std::int16_t>(std::lround(
                std::sin(6.2831853f*kToneHz*static_cast<float>(i)/static_cast<float>(kRate))*0.8f*32767.0f));
            toneFormat=Mono16(kRate);
        }
        threatVoice=Voice(toneFormat,tonePcm);
        if(!threatVoice)return;
    }
    threatAt=now;
    float volume=0.0f,ratio=1.0f;
    if(launch){ratio=(now/kWarbleMs)%2 ? kLaunchHigh : kLaunchLow;volume=kThreatVolume;}
    else if(state==2){ratio=kThreatMissileRatio;volume=now%kThreatMissilePeriod<kThreatMissileMs ? kThreatVolume : 0.0f;}
    else if(state==1){ratio=kThreatLockRatio;volume=now%kThreatLockPeriod<kThreatLockMs ? kThreatVolume : 0.0f;}
    volume*=WarnGain();
    threatVoice->SetFrequencyRatio(ratio);
    Pan(threatVoice,volume,volume);
    threatVoice->SetVolume(1.0f);
}

// --- The ground vehicles' sounds ---
const wchar_t* const kClipName[kClipCount]={L"engine_heavy_idle",L"engine_heavy_load",L"engine_light_idle",L"engine_light_load",
                                            L"tracks",L"turret",L"turret_stop",L"gun_near",L"gun_far",L"reload_eject",
                                            L"reload_load",L"reload_close"};
constexpr bool kClipLoops[kClipCount]={true,true,true,true,true,true,false,false,false,false,false,false};
// Peak of each clip as made, of full scale: the loops a little under (several play at once), the gun's report at the top.
constexpr float kClipPeak[kClipCount]={0.8f,0.8f,0.8f,0.8f,0.7f,0.6f,0.7f,0.98f,0.95f,0.8f,0.8f,0.85f};
constexpr int kLoops=64,kShots=24;
struct ClipPcm { std::vector<std::int16_t> pcm; WAVEFORMATEX format; };
ClipPcm clips[kClipCount]{};       // written by MakeClips' thread before clipsReady (release), read after it (acquire)
std::atomic<bool> clipsReady{false};
bool clipsAsked=false;
struct LoopVoice { IXAudio2SourceVoice* v; bool used; };
LoopVoice loops[kLoops]{};
struct ShotVoice { IXAudio2SourceVoice* v; UINT32 rate; };
ShotVoice shots[kShots]{};
UINT32 loopStart=0;                // where the next loop begins to play (spread: two tanks idling are not in phase)

std::vector<float> MadeClip(int c) {
    namespace s=vsynth;
    switch(c) {
    case kClipHeavyIdle: return s::EngineLayer(s::kHeavyEngine,false);
    case kClipHeavyLoad: return s::EngineLayer(s::kHeavyEngine,true);
    case kClipLightIdle: return s::EngineLayer(s::kLightEngine,false);
    case kClipLightLoad: return s::EngineLayer(s::kLightEngine,true);
    case kClipTracks: return s::Tracks();
    case kClipTurret: return s::Turret();
    case kClipTurretStop: return s::TurretStop();
    case kClipGunNear: return s::GunNear();
    case kClipGunFar: return s::GunFar();
    case kClipEject: return s::ReloadEject();
    case kClipLoad: return s::ReloadLoad();
    default: return s::ReloadClose();
    }
}
void MakeClipsNow() {
    int own=0;
    for(int c=0;c<kClipCount;++c) {
        wchar_t path[MAX_PATH],suffix[48];
        UINT32 rate=kRate;
        swprintf_s(suffix,L"_veh_%ls.wav",kClipName[c]);
        if(BesideDll(suffix,path) && ReadWav(path,clips[c].pcm,rate)){++own;Log("SOUND vehicles: %ls from %ls",kClipName[c],path);}
        else{rate=kRate;clips[c].pcm=Pcm16(MadeClip(c),kClipPeak[c]);}
        clips[c].format=Mono16(rate);
    }
    clipsReady.store(true,std::memory_order_release);
    Log("SOUND vehicles: %d clip(s) made in the plugin, %d the player's",kClipCount-own,own);
}
DWORD WINAPI MakeClips(void*) {
    __try { MakeClipsNow(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { Log("SOUND vehicles: making the sounds faulted: the vehicles keep the stock ones"); }
    return 0;
}

// The filter a sound `distance` (0..1) off is heard through (as Set's).
XAUDIO2_FILTER_PARAMETERS FarFilter(float distance) noexcept {
    const float d=distance<0.0f ? 0.0f : distance>1.0f ? 1.0f : distance;
    const float cutoff=kNearCutoff*std::pow(kFarCutoff/kNearCutoff,d);
    const float radians=2.0f*std::sin(3.14159265f*cutoff/static_cast<float>(kRate));
    return XAUDIO2_FILTER_PARAMETERS{LowPassFilter,radians<XAUDIO2_MAX_FILTER_FREQUENCY ? radians : XAUDIO2_MAX_FILTER_FREQUENCY,1.0f};
}
float Ratio(float r) noexcept { return !(r>1.0f/kMaxRatio) ? 1.0f/kMaxRatio : r>kMaxRatio ? kMaxRatio : r; }
void Hear(IXAudio2SourceVoice* v,const Heard& h) noexcept {
    const XAUDIO2_FILTER_PARAMETERS f=FarFilter(h.distance);
    v->SetFrequencyRatio(Ratio(h.ratio));
    v->SetFilterParameters(&f);
    Pan(v,std::isfinite(h.left) ? h.left : 0.0f,std::isfinite(h.right) ? h.right : 0.0f);
    v->SetVolume(1.0f);
}
}  // namespace

bool Start() noexcept {
    if(tried)return ok;
    tried=true;
    if(FAILED(XAudio2Create(&engine,0,XAUDIO2_DEFAULT_PROCESSOR)) || !engine) {
        Log("SOUND no XAudio2 engine: the jets stay silent");
        return false;
    }
    if(FAILED(engine->CreateMasteringVoice(&master)) || !master) {
        Log("SOUND no audio output device: the jets stay silent");
        return false;
    }
    XAUDIO2_VOICE_DETAILS details{};master->GetVoiceDetails(&details);
    masterChannels=details.InputChannels>=1 && details.InputChannels<=XAUDIO2_MAX_AUDIO_CHANNELS ? details.InputChannels : 2;
    wchar_t path[MAX_PATH]{};
    UINT32 rate=kRate;
    const bool own=BesideDll(L"_jet.wav",path) && ReadWav(path,roarPcm,rate);
    if(own) {
        whine=false;
        Log("SOUND engine sound from %ls (%u Hz, %.1f s)",path,rate,static_cast<double>(roarPcm.size())/rate);
    } else {
        roarPcm=SynthRoar();whinePcm=SynthWhine();
        Log("SOUND engine sound made in the plugin (no %ls)",path);
    }
    roarFormat=Mono16(rate);whineFormat=Mono16(kRate);
    beatAt=GetTickCount64();
    const HANDLE t=CreateThread(nullptr,0,&Watch,nullptr,0,nullptr);
    if(t)CloseHandle(t);
    ok=true;
    Log("SOUND XAudio2 up: %u output channel(s)",masterChannels);
    return true;
}

int Open() noexcept {
    if(!ok)return -1;
    for(int i=0;i<kSlots;++i) {
        Slot& s=slots[i];
        if(s.used)continue;
        s.roar=Voice(roarFormat,roarPcm);
        s.whine=whine ? Voice(whineFormat,whinePcm) : nullptr;
        if(!s.roar){if(s.whine)s.whine->DestroyVoice();s=Slot{};return -1;}
        s.used=true;
        return i;
    }
    return -1;
}

void Set(int i,const Mix& m) noexcept {
    if(i<0 || i>=kSlots || !slots[i].used)return;
    const Slot& s=slots[i];
    const float distance=m.distance<0.0f ? 0.0f : m.distance>1.0f ? 1.0f : m.distance;
    // The cutoff falls geometrically from kNearCutoff to kFarCutoff.
    const float cutoff=kNearCutoff*std::pow(kFarCutoff/kNearCutoff,distance);
    // XAudio2's filter frequency: 2 sin(pi f / rate), at most XAUDIO2_MAX_FILTER_FREQUENCY (XAudio2CutoffFrequencyToRadians).
    const float radians=2.0f*std::sin(3.14159265f*cutoff/static_cast<float>(kRate));
    XAUDIO2_FILTER_PARAMETERS f{LowPassFilter,radians<XAUDIO2_MAX_FILTER_FREQUENCY ? radians : XAUDIO2_MAX_FILTER_FREQUENCY,1.0f};
    auto ratio=[](float r) noexcept { return r<1.0f/kMaxRatio ? 1.0f/kMaxRatio : r>kMaxRatio ? kMaxRatio : r; };
    s.roar->SetFrequencyRatio(ratio(m.roarRatio));
    s.roar->SetFilterParameters(&f);
    Pan(s.roar,m.roarL,m.roarR);
    s.roar->SetVolume(1.0f);
    if(s.whine) {
        s.whine->SetFrequencyRatio(ratio(m.whineRatio));
        s.whine->SetFilterParameters(&f);
        Pan(s.whine,m.whineL,m.whineR);
        s.whine->SetVolume(1.0f);
    }
}

void Close(int i) noexcept {
    if(i<0 || i>=kSlots || !slots[i].used)return;
    if(slots[i].roar)slots[i].roar->DestroyVoice();
    if(slots[i].whine)slots[i].whine->DestroyVoice();
    slots[i]=Slot{};
}

bool Running() noexcept { return ok; }

void Warn(const Cockpit& c) noexcept {
    if(!Start())return;
    if(!warnMade)MakeWarnings();
    if(Cfg().warnAudio && Cfg().warnVoice && !speaking) {   // the voice once asked for (it may be switched on later in the game)
        speaking=true;
        const HANDLE t=CreateThread(nullptr,0,&SpeakAll,nullptr,0,nullptr);
        if(t)CloseHandle(t);
    }
    const ULONGLONG now=GetTickCount64();
    warnAt=now;
    ThreatTone(c.threat,c.launch,now);
    if(hornVoice) {
        const float v=c.stall ? kHornVolume*WarnGain() : 0.0f;
        Pan(hornVoice,v,v);
        hornVoice->SetVolume(1.0f);
    }
    Callouts(c.callouts,now);
}

void LockTone(int state,float progress) noexcept {
    if(!Start())return;
    if(!lockVoice) {
        const int n=kRate/10;   // 100 ms: 100 whole cycles of kToneHz
        tonePcm.resize(static_cast<std::size_t>(n));
        for(int i=0;i<n;++i)tonePcm[static_cast<std::size_t>(i)]=static_cast<std::int16_t>(std::lround(
            std::sin(6.2831853f*kToneHz*static_cast<float>(i)/static_cast<float>(kRate))*0.8f*32767.0f));
        toneFormat=Mono16(kRate);
        lockVoice=Voice(toneFormat,tonePcm);
        if(!lockVoice)return;
    }
    const ULONGLONG now=GetTickCount64();
    toneAt=now;
    if(state!=toneState){toneState=state;beepFrom=now;}
    float volume=0.0f,ratio=1.0f;
    if(state==2){volume=kToneVolume;ratio=kLockedRatio;}
    else if(state==1) {
        const float p=progress<0.0f ? 0.0f : progress>1.0f ? 1.0f : progress;
        const ULONGLONG period=kBeepSlow-static_cast<ULONGLONG>(p*static_cast<float>(kBeepSlow-kBeepFast));
        volume=(now-beepFrom)%period<kBeepMs ? kToneVolume : 0.0f;
    }
    volume*=WarnGain();
    lockVoice->SetFrequencyRatio(ratio);
    Pan(lockVoice,volume,volume);
    lockVoice->SetVolume(1.0f);
}

void Beat(float volume) noexcept {
    const ULONGLONG now=GetTickCount64();
    if(lockVoice && now-toneAt>kToneStaleMs){Pan(lockVoice,0.0f,0.0f);toneState=0;}
    if(threatVoice && now-threatAt>kToneStaleMs)Pan(threatVoice,0.0f,0.0f);
    if(warnMade && now-warnAt>kToneStaleMs) {   // no aircraft flown (or the plugin off): the warnings stop
        if(hornVoice)Pan(hornVoice,0.0f,0.0f);
        if(calling>=0)StopCallout();
        callsOn=0;
    }
    masterVolume=volume<0.0f ? 0.0f : volume>4.0f ? 4.0f : volume;
    beatAt=GetTickCount64();
}

bool ClipsReady() noexcept {
    if(!Start())return false;
    if(!clipsAsked) {
        clipsAsked=true;
        const HANDLE t=CreateThread(nullptr,0,&MakeClips,nullptr,0,nullptr);
        if(t)CloseHandle(t);
        else Log("SOUND vehicles: no thread to make the sounds on: the vehicles keep the stock ones");
    }
    return clipsReady.load(std::memory_order_acquire);
}

int OpenLoop(int clip) noexcept {
    if(clip<0 || clip>=kClipCount || !kClipLoops[clip] || !ClipsReady())return -1;
    for(int i=0;i<kLoops;++i) {
        if(loops[i].used)continue;
        const ClipPcm& c=clips[clip];
        IXAudio2SourceVoice* v=nullptr;
        if(FAILED(engine->CreateSourceVoice(&v,&c.format,XAUDIO2_VOICE_USEFILTER,kMaxRatio)))return -1;
        XAUDIO2_BUFFER b{};
        b.AudioBytes=static_cast<UINT32>(c.pcm.size()*2);b.pAudioData=reinterpret_cast<const BYTE*>(c.pcm.data());
        b.LoopCount=XAUDIO2_LOOP_INFINITE;
        loopStart=(loopStart+7919u*61u)%static_cast<UINT32>(c.pcm.size());
        b.PlayBegin=loopStart;   // the loop region stays the whole clip (LoopBegin 0 < PlayBegin + the rest)
        if(FAILED(v->SubmitSourceBuffer(&b))){v->DestroyVoice();return -1;}
        Pan(v,0.0f,0.0f);
        v->Start();
        loops[i]=LoopVoice{v,true};
        return i;
    }
    return -1;
}

void SetLoop(int i,const Heard& h) noexcept {
    if(i<0 || i>=kLoops || !loops[i].used)return;
    Hear(loops[i].v,h);
}

void CloseLoop(int i) noexcept {
    if(i<0 || i>=kLoops || !loops[i].used)return;
    loops[i].v->DestroyVoice();
    loops[i]=LoopVoice{};
}

void PlayOnce(int clip,const Heard& h) noexcept {
    if(clip<0 || clip>=kClipCount || kClipLoops[clip] || !ClipsReady())return;
    const ClipPcm& c=clips[clip];
    const UINT32 rate=c.format.nSamplesPerSec;
    // An idle voice already at this clip's rate, else any idle one (remade at it); none idle: dropped (a burst past kShots).
    ShotVoice* pick=nullptr;
    for(auto& s:shots) {
        XAUDIO2_VOICE_STATE st{};
        if(s.v)s.v->GetState(&st,XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if(s.v && st.BuffersQueued)continue;
        const bool fits=s.v && s.rate==rate;
        if(!pick || (fits && !(pick->v && pick->rate==rate)))pick=&s;
    }
    if(!pick)return;
    if(pick->v && pick->rate!=rate){pick->v->DestroyVoice();*pick=ShotVoice{};}
    if(!pick->v) {
        if(FAILED(engine->CreateSourceVoice(&pick->v,&c.format,XAUDIO2_VOICE_USEFILTER,kMaxRatio))){*pick=ShotVoice{};return;}
        pick->rate=rate;
    }
    XAUDIO2_BUFFER b{};
    b.Flags=XAUDIO2_END_OF_STREAM;b.AudioBytes=static_cast<UINT32>(c.pcm.size()*2);b.pAudioData=reinterpret_cast<const BYTE*>(c.pcm.data());
    pick->v->Stop(0);pick->v->FlushSourceBuffers();
    if(FAILED(pick->v->SubmitSourceBuffer(&b)))return;
    Hear(pick->v,h);
    pick->v->Start(0);
}
}  // namespace crew::audio
