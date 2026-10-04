// The jets' engine sound, played by the plugin (see jetaudio.h). An XAudio2 engine of its own on the default output,
// two looping mono voices per jet:
//  - the roar: the exhaust's broadband noise, heard mostly behind and to the side of a jet;
//  - the whine: the fan's blade-pass tone and its harmonics over a narrow band of noise, heard mostly ahead of it.
// Both are made here once (Synth*, seamless loops). A <dll name>_jet.wav next to the DLL (16-bit PCM, mono or
// stereo, any rate) replaces the roar and silences the whine: a recording carries both.
// Each voice has a low-pass filter (the air takes a far engine's highs) and an output matrix (left / right ear).
// A watchdog thread silences the master voice once the game stops beating (Beat): paused, loading, in a menu; the
// game's own sounds stop then too.
#include "jetaudio.h"
#include "crew.h"
#include <windows.h>
#include <xaudio2.h>
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
    wchar_t path[MAX_PATH]{};HMODULE self=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Start),&self);
    UINT32 rate=kRate;
    bool own=false;
    if(GetModuleFileNameW(self,path,MAX_PATH)) {
        wchar_t* dot=wcsrchr(path,L'.');
        if(dot && wcscpy_s(dot,MAX_PATH-(dot-path),L"_jet.wav")==0)own=ReadWav(path,roarPcm,rate);
    }
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
    lockVoice->SetFrequencyRatio(ratio);
    Pan(lockVoice,volume,volume);
    lockVoice->SetVolume(1.0f);
}

void Beat(float volume) noexcept {
    if(lockVoice && GetTickCount64()-toneAt>kToneStaleMs){Pan(lockVoice,0.0f,0.0f);toneState=0;}
    masterVolume=volume<0.0f ? 0.0f : volume>4.0f ? 4.0f : volume;
    beatAt=GetTickCount64();
}
}  // namespace crew::audio
