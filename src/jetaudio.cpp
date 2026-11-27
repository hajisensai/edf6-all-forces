// The plugin's own sounds (see jetaudio.h), on an XAudio2 engine of its own on the default output. Every voice has a
// low-pass filter (the air takes a far sound's highs) and an output matrix over the speakers (surround.h).
// The engines (jetsound.cpp, vehsound.cpp, sound_engine.h decide them) are War Thunder's layered loops (wt_clips.h),
// one voice a layer (OpenWtLoop); nothing about an engine is made here.
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
#include "config.h"
#include "plugin_log.h"
#include "shield.h"
#include "sound.h"
#include "lock_cue.h"
#include "surround.h"
#include "wt_clips.h"
#include "vsynth.h"
#include <windows.h>
#include <xaudio2.h>
#include <xapofx.h>
#pragma warning(push,0)
#include <sapi.h>
#pragma warning(pop)
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace crew::audio {
namespace {
constexpr UINT32 kRate=48000;
constexpr float kMaxRatio=4.0f;                // a voice's highest playback rate (pitch and Doppler)
constexpr ULONGLONG kQuietMs=150;              // no beat this long: the game is not running, nor is the sound

IXAudio2* engine=nullptr;
IXAudio2MasteringVoice* master=nullptr;
UINT32 masterChannels=2;
surround::Layout layout=surround::LayoutOf(surround::kStereo,2);   // the master's speakers (Start)
bool tried=false,ok=false;
// The lock tone (LockTone): a 1 kHz sine loop, whole cycles; what it sounds when, and for how long, lock_cue.h decides
// (locked it plays at kLockedRatio, locking it beeps as the lock closes in; both only for the first few cues).
constexpr float kToneHz=1000.0f,kToneVolume=0.25f;
constexpr ULONGLONG kToneStaleMs=200;
IXAudio2SourceVoice* lockVoice=nullptr;
std::vector<std::int16_t> tonePcm;
WAVEFORMATEX toneFormat{};
ULONGLONG toneAt=0;
lock_cue::LockCue toneCue;
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
std::atomic<ULONGLONG> beatAt{0};
std::atomic<float> masterVolume{1.0f};

// A 16-bit PCM WAV (mono or stereo) as mono samples and their rate (`stereo`: a stereo one kept so, interleaved, and
// its channel count there); false: none or not that.
bool ReadWav(const wchar_t* path,std::vector<std::int16_t>& pcm,UINT32& rate,int* stereo=nullptr) {
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
            if(stereo) {
                *stereo=channels;
                pcm.resize(frames*channels);
                std::memcpy(pcm.data(),body,frames*channels*2);
                return frames>static_cast<std::size_t>(rate/100) && rate>=8000 && rate<=192000;
            }
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

WAVEFORMATEX Pcm16Format(UINT32 rate,WORD channels) noexcept {
    WAVEFORMATEX f{};
    f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=channels;f.nSamplesPerSec=rate;f.wBitsPerSample=16;
    f.nBlockAlign=static_cast<WORD>(2*channels);f.nAvgBytesPerSec=rate*2*channels;
    return f;
}
WAVEFORMATEX Mono16(UINT32 rate) noexcept { return Pcm16Format(rate,1); }

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
    // The master volume set only when it changes (it was set 40 times a second either way).
    float set=-1.0f;
    for(;;) {
        Sleep(25);
        const bool stopped=GetTickCount64()-beatAt.load()>kQuietMs;
        const float want=stopped ? 0.0f : masterVolume.load();
        if(want!=set){master->SetVolume(want);set=want;}
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

// The cockpit's tones (not heard from anywhere): mono into the front left and right at these gains (a surround layout's
// first two channels).
void Pan(IXAudio2SourceVoice* v,float left,float right) noexcept {
    float m[XAUDIO2_MAX_AUDIO_CHANNELS]{};
    m[0]=left;m[1]=masterChannels>1 ? right : 0.5f*(left+right);
    v->SetOutputMatrix(nullptr,1,masterChannels,m);
}
// A sound heard from somewhere: a voice of `inputs` channels among the master's speakers (surround.h).
void Place(IXAudio2SourceVoice* v,int inputs,const surround::Placing& p) noexcept {
    float m[2*XAUDIO2_MAX_AUDIO_CHANNELS];
    surround::Matrix(layout,p,inputs,m);
    v->SetOutputMatrix(nullptr,inputs==2 ? 2 : 1,masterChannels,m);
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
const wchar_t* const kClipName[kClipCount]={L"turret",L"turret_stop",L"gun_near",L"gun_far",L"reload_eject",L"reload_load",L"reload_close",L"mg_burst",
                                            L"gatling_burst",L"burst_tail",L"autocannon",L"brass",L"case_small",L"missile_launch",L"sazabi_beam_shot",
                                            L"sazabi_beam_hit",L"sazabi_saber_on",L"sazabi_saber_off",L"sazabi_whoosh",L"sazabi_saber_hit",
                                            L"sazabi_cannon_shot",L"sazabi_funnel_launch",L"sazabi_funnel_shot",L"sazabi_funnel_dock",L"sazabi_footstep",
                                            L"sazabi_land",L"sazabi_dash",L"sazabi_thrusters",L"sazabi_saber_hum",L"sazabi_cannon_charge",
                                            L"gun_medium_near",L"gun_medium_far",L"howitzer_near",L"howitzer_far",L"gun_heavy_near",L"gun_heavy_far",
                                            L"railgun_shot",L"railgun_far",L"railgun_charge",L"railgun_ready",L"grenade_shot",L"rocket_rail_launch",
                                            L"rocket_rail_load",L"case_medium",L"case_stub",L"case_grenade",L"reload_breech_open",L"reload_shell_ram",
                                            L"reload_charge",L"reload_primer",L"mark_own",L"mark_team",L"mark_off",L"sazabi_shield_block"};
constexpr bool kClipLoops[kClipCount]={true,false,false,false,false,false,false,true,true,false,false,true,false,false,false,false,false,false,false,
                                       false,false,false,false,false,false,false,false,true,true,true,false,false,false,false,false,false,false,
                                       false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false};
// Peak of each clip as made, of full scale: the loops a little under (several play at once), the gun's report at the top.
constexpr float kClipPeak[kClipCount]={0.6f,0.7f,0.98f,0.95f,0.8f,0.8f,0.85f,0.85f,0.85f,0.8f,0.95f,0.6f,0.8f,0.9f,0.95f,0.9f,0.8f,0.8f,0.8f,0.95f,
                                       0.98f,0.8f,0.85f,0.8f,0.95f,0.98f,0.9f,0.8f,0.7f,0.7f,0.98f,0.95f,0.98f,0.95f,0.98f,0.95f,0.95f,0.9f,0.75f,
                                       0.7f,0.9f,0.9f,0.8f,0.8f,0.85f,0.8f,0.8f,0.85f,0.75f,0.7f,0.7f,0.6f,0.6f,0.9f};
// The ultimate's (jetaudio.h UltClip): <dll name>_ult_<name>.wav, the player's recordings only.
const wchar_t* const kUltClipName[kUltClipCount]={L"charge",L"circle",L"thunder",L"gather",L"beam",L"blast",L"shock"};
constexpr int kLoops=kLoopPool,kShots=160;   // the Sazabi's funnels fire six at once, a Katyusha ripples its rockets, a cannon's
                                     // War Thunder layers ten a shot (sound_event.h), an engine up to
                                     // engine::kMostEventLayers a vehicle: room for them
struct ClipPcm { std::vector<std::int16_t> pcm; WAVEFORMATEX format; };
ClipPcm clips[kClipCount]{};       // written by MakeClips' thread before clipsReady (release), read after it (acquire)
ClipPcm ultClips[kUltClipCount]{};  // the same; an empty one: no recording
ClipPcm wtClips[wt::kCount][wt::kMostVariants]{};   // the same (stereo ones kept so); wtThere: every variant read
bool wtThere[wt::kCount]{};
// The vehicles' clips War Thunder's recordings stand in for (the synthesised ones gone): the turret's and the loader's. A
// one-shot plays the group's variants in turn, a loop the group's first; each levelled to the clip's kClipPeak (the whole
// group by one gain: the vehicles' mix is set by those peaks). The player's own <dll>_veh_<name>.wav still first; a group
// not read (logged once with the rest): the clip silent.
struct WtStandIn { int clip,group; };
constexpr WtStandIn kWtFor[]={{kClipTurret,wt::k_turret_loop},{kClipTurretStop,wt::k_turret_stop},
                              {kClipBreechOpen,wt::k_reload_open},{kClipEject,wt::k_reload_eject},
                              {kClipLoad,wt::k_reload_insert},{kClipShellRam,wt::k_reload_insert},
                              {kClipClose,wt::k_reload_close}};
int WtForClip(int c) noexcept {
    for(const WtStandIn& s:kWtFor)if(s.clip==c)return s.group;
    return -1;
}
int wtOf[kClipCount]{};        // set by MakeClips (as clips): the group standing in for a one-shot + 1, 0 none
float wtLevel[kClipCount]{};   // its gain to the clip's peak
std::atomic<unsigned> wtTurn{0};
std::atomic<bool> clipsReady{false};
bool clipsAsked=false;
struct LoopVoice { IXAudio2SourceVoice* v; int inputs; bool used; };
LoopVoice loops[kLoops]{};
struct ShotVoice { IXAudio2SourceVoice* v; UINT32 rate; WORD channels; };
ShotVoice shots[kShots]{};
// The Debug tallies (Note / Count / StatsTick): the voices' use since the last line.
bool limiterOn=false;
std::atomic<int> shotsBusyPeak{0},loopsOpen{0},loopsPeak{0};
std::atomic<unsigned> shotsStarted{0},shotsDropped{0},loopsNoneFree{0};
UINT32 loopStart=0;                // where the next loop begins to play (spread: two tanks idling are not in phase)

std::vector<float> MadeClip(int c) {
    namespace s=vsynth;
    switch(c) {
    case kClipTurret: return s::Turret();
    case kClipTurretStop: return s::TurretStop();
    case kClipGunNear: return s::GunNear(s::kTankReport);
    case kClipGunFar: return s::GunFar(s::kTankReport);
    case kClipEject: return s::ReloadEject();
    case kClipLoad: return s::ReloadLoad(s::kRoundRam);
    case kClipClose: return s::ReloadClose();
    case kClipMg: return s::MachineGun();
    case kClipGatling: return s::Gatling();
    case kClipBurstTail: return s::BurstTail();
    case kClipAutocannon: return s::Autocannon();
    case kClipBrass: return s::Brass();
    case kClipCaseSmall: return s::Case(s::kCaseSmall);
    case kClipSzBeamShot: return s::BeamShot();
    case kClipSzBeamHit: return s::BeamHit();
    case kClipSzSaberOn: return s::SaberOn();
    case kClipSzSaberOff: return s::SaberOff();
    case kClipSzWhoosh: return s::Whoosh();
    case kClipSzSaberHit: return s::SaberHit();
    case kClipSzCannonShot: return s::CannonShot();
    case kClipSzFunnelLaunch: return s::FunnelLaunch();
    case kClipSzFunnelShot: return s::FunnelShot();
    case kClipSzFunnelDock: return s::FunnelDock();
    case kClipSzFootstep: return s::Footstep();
    case kClipSzLand: return s::Land();
    case kClipSzDash: return s::Dash();
    case kClipSzThrusters: return s::Thrusters();
    case kClipSzSaberHum: return s::SaberHum();
    case kClipSzCharge: return s::CannonCharge();
    case kClipGunMediumNear: return s::GunNear(s::kMediumReport);
    case kClipGunMediumFar: return s::GunFar(s::kMediumReport);
    case kClipHowitzerNear: return s::GunNear(s::kHowitzerReport);
    case kClipHowitzerFar: return s::GunFar(s::kHowitzerReport);
    case kClipGunHeavyNear: return s::GunNear(s::kHeavyReport);
    case kClipGunHeavyFar: return s::GunFar(s::kHeavyReport);
    case kClipRailShot: return s::RailShot();
    case kClipRailFar: return s::RailFar();
    case kClipRailCharge: return s::RailCharge();
    case kClipRailReady: return s::RailReady();
    case kClipGrenadeShot: return s::GrenadeShot();
    case kClipRocketRail: return s::RocketRail();
    case kClipRocketLoad: return s::RocketLoad();
    case kClipCaseMedium: return s::Case(s::kCaseMedium);
    case kClipCaseStub: return s::Case(s::kCaseStub);
    case kClipCaseGrenade: return s::Case(s::kCaseGrenade);
    case kClipBreechOpen: return s::BreechOpen();
    case kClipShellRam: return s::ReloadLoad(s::kShellRam);
    case kClipCharge: return s::ChargeModule();
    case kClipPrimer: return s::Primer();
    case kClipMarkOwn: return s::MarkOwn();
    case kClipMarkTeam: return s::MarkTeam();
    case kClipMarkOff: return s::MarkOff();
    case kClipSzShieldBlock: return s::ShieldBlock();
    default: return s::MissileLaunch();
    }
}
void MakeClipsNow() {
    int own=0,made=0;
    for(int c=0;c<kClipCount;++c) {
        wchar_t path[MAX_PATH],suffix[64];
        UINT32 rate=kRate;
        swprintf_s(suffix,L"_veh_%ls.wav",kClipName[c]);
        wtOf[c]=0;
        if(BesideDll(suffix,path) && ReadWav(path,clips[c].pcm,rate)){++own;Log("SOUND vehicles: %ls from %ls",kClipName[c],path);}
        else if(WtForClip(c)>=0){rate=kRate;clips[c].pcm.clear();wtOf[c]=WtForClip(c)+1;}   // filled below, once read
        else{rate=kRate;clips[c].pcm=Pcm16(MadeClip(c),kClipPeak[c]);++made;}
        clips[c].format=Mono16(rate);
    }
    int ult=0;
    for(int c=0;c<kUltClipCount;++c) {
        wchar_t path[MAX_PATH],suffix[64];
        UINT32 rate=kRate;
        swprintf_s(suffix,L"_ult_%ls.wav",kUltClipName[c]);
        if(BesideDll(suffix,path) && ReadWav(path,ultClips[c].pcm,rate)){++ult;ultClips[c].format=Mono16(rate);}
        else ultClips[c].pcm.clear();
    }
    int wtRead=0,wtMissing=0;
    wchar_t firstMissing[MAX_PATH]{};
    for(int c=0;c<wt::kCount;++c) {
        bool all=true;
        for(int i=0;i<wt::kVariants[c];++i) {
            wchar_t path[MAX_PATH]{},suffix[96];
            UINT32 rate=kRate;
            int channels=1;
            swprintf_s(suffix,L"_wt\\%ls_%d.wav",wt::kName[c],i+1);
            ClipPcm& clip=wtClips[c][i];
            if(BesideDll(suffix,path) && ReadWav(path,clip.pcm,rate,&channels)) {
                clip.format=Pcm16Format(rate,static_cast<WORD>(channels));
            } else {
                clip.pcm.clear();all=false;++wtMissing;
                if(!firstMissing[0])wcscpy_s(firstMissing,path);
            }
        }
        wtThere[c]=all;
        if(all)++wtRead;
    }
    int stoodIn=0;
    for(int c=0;c<kClipCount;++c) {
        const int g=wtOf[c]-1;
        if(g<0 || !wtThere[g])continue;
        int most=1;
        for(int i=0;i<wt::kVariants[g];++i)for(std::int16_t x:wtClips[g][i].pcm)most=(std::max)(most,std::abs(static_cast<int>(x)));
        wtLevel[c]=kClipPeak[c]*32767.0f/static_cast<float>(most);
        ++stoodIn;
        if(!kClipLoops[c])continue;
        const ClipPcm& from=wtClips[g][0];   // a loop: its own copy at the level (its voice loops one buffer)
        clips[c].pcm.resize(from.pcm.size());
        for(std::size_t i=0;i<from.pcm.size();++i)
            clips[c].pcm[i]=static_cast<std::int16_t>((std::clamp)(std::lround(from.pcm[i]*wtLevel[c]),-32768L,32767L));
        clips[c].format=from.format;
        wtOf[c]=0;
    }
    if(wtMissing)Log("SOUND vehicles: %d War Thunder recording(s) missing (the first %ls): their %d group(s) stay silent, the "
                     "install is broken",wtMissing,firstMissing,wt::kCount-wtRead);
    clipsReady.store(true,std::memory_order_release);
    Log("SOUND vehicles: %d clip(s) made in the plugin, %d the player's, %d War Thunder's; the ultimate's: %d of %d recorded; "
        "War Thunder's: %d of %d groups",made,own,stoodIn,ult,kUltClipCount,
        wtRead,wt::kCount);
}
DWORD WINAPI MakeClips(void*) {
    __try { MakeClipsNow(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { Log("SOUND vehicles: making the sounds faulted: the vehicles keep the stock ones"); }
    return 0;
}

// The low-pass at `cutoff` Hz. The state-variable one (2 sin(pi f / rate), XAudio2CutoffFrequencyToRadians) reaches only
// rate / 6 (8 kHz: XAUDIO2_MAX_FILTER_FREQUENCY), which dulled every near sound (it was the 16 kHz asked for); above that
// the one-pole one (1 - (1 - 2 f / rate)^2, XAudio2CutoffFrequencyToOnePoleCoefficient), nearly open by 16 kHz.
XAUDIO2_FILTER_PARAMETERS CutoffFilter(float cutoff) noexcept {
    const float rate=static_cast<float>(kRate);
    const float f=!(cutoff>20.0f) ? 20.0f : cutoff>0.5f*rate ? 0.5f*rate : cutoff;
    if(f>=rate/6.0f) {
        const float open=1.0f-2.0f*f/rate;
        return XAUDIO2_FILTER_PARAMETERS{LowPassOnePoleFilter,1.0f-open*open,1.0f};
    }
    const float radians=2.0f*std::sin(3.14159265f*f/rate);
    return XAUDIO2_FILTER_PARAMETERS{LowPassFilter,radians<XAUDIO2_MAX_FILTER_FREQUENCY ? radians : XAUDIO2_MAX_FILTER_FREQUENCY,1.0f};
}
float Ratio(float r) noexcept { return !(r>1.0f/kMaxRatio) ? 1.0f/kMaxRatio : r>kMaxRatio ? kMaxRatio : r; }
void Hear(IXAudio2SourceVoice* v,int inputs,const Heard& h) noexcept {
    const XAUDIO2_FILTER_PARAMETERS f=CutoffFilter(h.cutoff);
    v->SetFrequencyRatio(Ratio(h.ratio));
    v->SetFilterParameters(&f);
    Place(v,inputs,surround::Placing{h.gain,h.side,h.front,h.spread,h.sub});
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
    DWORD mask=0;
    if(FAILED(master->GetChannelMask(&mask)))mask=0;
    layout=surround::LayoutOf(mask,static_cast<int>(masterChannels));
    // A limiter on the output (XAPOFX's mastering limiter, its default release and loudness): the layers of guns firing
    // together add past full scale, held under it rather than clipped.
    const char* limiter="none (it failed: loud volleys may clip)";
    IUnknown* fx=nullptr;
    FXMASTERINGLIMITER_PARAMETERS lp{FXMASTERINGLIMITER_DEFAULT_RELEASE,FXMASTERINGLIMITER_DEFAULT_LOUDNESS};
    if(SUCCEEDED(CreateFX(__uuidof(FXMasteringLimiter),&fx,&lp,sizeof(lp))) && fx) {
        XAUDIO2_EFFECT_DESCRIPTOR d{fx,TRUE,masterChannels};
        const XAUDIO2_EFFECT_CHAIN chain{1,&d};
        if(SUCCEEDED(master->SetEffectChain(&chain))){limiter="on";limiterOn=true;}
        fx->Release();
    }
    beatAt=GetTickCount64();
    const HANDLE t=CreateThread(nullptr,0,&Watch,nullptr,0,nullptr);
    if(t)CloseHandle(t);
    ok=true;
    Log("SOUND XAudio2 up: %u output channel(s), mask 0x%lX: %d around the listener, LFE %s; limiter %s",masterChannels,mask,
        layout.ring,layout.lfe>=0 ? "yes" : "no",limiter);
    return true;
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

void LockTone(int state,float progress,const void* weapon,const void* target) noexcept {
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
    const Config& cfg=Cfg();
    const lock_cue::ToneOut out=lock_cue::Step(toneCue,weapon,target,state,progress,now,cfg.lockToneBeeps,
                                               static_cast<ULONGLONG>(cfg.lockToneLockedMs));
    const float volume=out.on ? kToneVolume*WarnGain() : 0.0f;
    lockVoice->SetFrequencyRatio(out.ratio);
    Pan(lockVoice,volume,volume);
    lockVoice->SetVolume(1.0f);
}

void Beat(float volume) noexcept {
    const ULONGLONG now=GetTickCount64();
    if(lockVoice && now-toneAt>kToneStaleMs){Pan(lockVoice,0.0f,0.0f);toneCue=lock_cue::LockCue{};}
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
    if(clip<0 || clip>=kClipCount || !kClipLoops[clip] || !ClipsReady() || clips[clip].pcm.empty())return -1;
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
        loops[i]=LoopVoice{v,1,true};
        const int open=loopsOpen.fetch_add(1,std::memory_order_relaxed)+1;
        if(open>loopsPeak.load(std::memory_order_relaxed))loopsPeak.store(open,std::memory_order_relaxed);
        return i;
    }
    loopsNoneFree.fetch_add(1,std::memory_order_relaxed);
    return -1;
}

void SetLoop(int i,const Heard& h) noexcept {
    if(i<0 || i>=kLoops || !loops[i].used)return;
    Hear(loops[i].v,loops[i].inputs,h);
}

void CloseLoop(int i) noexcept {
    if(i<0 || i>=kLoops || !loops[i].used)return;
    loops[i].v->DestroyVoice();
    loops[i]=LoopVoice{};
    loopsOpen.fetch_sub(1,std::memory_order_relaxed);
}
int OpenWtLoop(int group) noexcept {
    if(!WtThere(group))return -1;
    const ClipPcm& c=wtClips[group][0];
    const UINT32 frames=static_cast<UINT32>(c.pcm.size()/c.format.nChannels);
    if(!frames)return -1;
    for(int i=0;i<kLoops;++i) {
        if(loops[i].used)continue;
        IXAudio2SourceVoice* v=nullptr;
        if(FAILED(engine->CreateSourceVoice(&v,&c.format,XAUDIO2_VOICE_USEFILTER,kMaxRatio)))return -1;
        XAUDIO2_BUFFER b{};
        b.AudioBytes=static_cast<UINT32>(c.pcm.size()*2);b.pAudioData=reinterpret_cast<const BYTE*>(c.pcm.data());
        b.LoopCount=XAUDIO2_LOOP_INFINITE;
        loopStart=(loopStart+7919u*61u)%frames;
        b.PlayBegin=loopStart;   // two engines of a kind are not in phase
        if(FAILED(v->SubmitSourceBuffer(&b))){v->DestroyVoice();return -1;}
        Place(v,c.format.nChannels,surround::Placing{0.0f,0.0f,1.0f,0.0f,false});   // silent until set
        v->Start();
        loops[i]=LoopVoice{v,static_cast<int>(c.format.nChannels),true};
        const int open=loopsOpen.fetch_add(1,std::memory_order_relaxed)+1;
        if(open>loopsPeak.load(std::memory_order_relaxed))loopsPeak.store(open,std::memory_order_relaxed);
        return i;
    }
    loopsNoneFree.fetch_add(1,std::memory_order_relaxed);
    return -1;
}
int LoopsOpen() noexcept;
int LoopsFree() noexcept { return kLoops-LoopsOpen(); }
int LoopsOpen() noexcept {
    int n=0;
    for(const LoopVoice& l:loops)n+=l.used ? 1 : 0;
    return n;
}

void PlayPcm(const ClipPcm& c,const Heard& h) noexcept;
void PlayOnce(int clip,const Heard& h) noexcept {
    if(TraceRecording()){const float d[2]={static_cast<float>(clip),h.gain};TraceOutput("once",d,sizeof(d));}
    if(clip<0 || clip>=kClipCount || kClipLoops[clip] || !ClipsReady())return;
    if(const int g=wtOf[clip]-1;g>=0) {
        if(!wtThere[g])return;
        Heard at=h;
        at.gain*=wtLevel[clip];
        PlayPcm(wtClips[g][wtTurn.fetch_add(1,std::memory_order_relaxed)%static_cast<unsigned>(wt::kVariants[g])],at);
        return;
    }
    PlayPcm(clips[clip],h);
}
bool UltClipThere(int clip) noexcept {
    return clip>=0 && clip<kUltClipCount && ClipsReady() && !ultClips[clip].pcm.empty();
}
void PlayUlt(int clip,const Heard& h) noexcept {
    if(UltClipThere(clip))PlayPcm(ultClips[clip],h);
}
bool WtThere(int clip) noexcept { return clip>=0 && clip<wt::kCount && ClipsReady() && wtThere[clip]; }
void PlayWt(int clip,int variant,const Heard& h) noexcept {
    if(TraceRecording()){const float d[3]={static_cast<float>(clip),static_cast<float>(variant),h.gain};TraceOutput("wt",d,sizeof(d));}
    if(WtThere(clip) && variant>=0 && variant<wt::kVariants[clip])PlayPcm(wtClips[clip][variant],h);
}
void PlayPcm(const ClipPcm& c,const Heard& h) noexcept {
    if(c.pcm.empty())return;   // a clip not read: silent
    const UINT32 rate=c.format.nSamplesPerSec;
    const WORD channels=c.format.nChannels;
    // An idle voice already at this clip's rate and channels, else any idle one (remade so); none idle: dropped (a burst
    // past kShots).
    ShotVoice* pick=nullptr;
    auto fits=[&](const ShotVoice& s){ return s.v && s.rate==rate && s.channels==channels; };
    int busy=0;
    for(auto& s:shots) {
        XAUDIO2_VOICE_STATE st{};
        if(s.v)s.v->GetState(&st,XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if(s.v && st.BuffersQueued){++busy;continue;}
        if(!pick || (fits(s) && !fits(*pick)))pick=&s;
    }
    if(const int inUse=pick ? busy+1 : busy;inUse>shotsBusyPeak.load(std::memory_order_relaxed))shotsBusyPeak.store(inUse,std::memory_order_relaxed);
    if(!pick){shotsDropped.fetch_add(1,std::memory_order_relaxed);return;}
    shotsStarted.fetch_add(1,std::memory_order_relaxed);
    if(pick->v && !fits(*pick)){pick->v->DestroyVoice();*pick=ShotVoice{};}
    if(!pick->v) {
        if(FAILED(engine->CreateSourceVoice(&pick->v,&c.format,XAUDIO2_VOICE_USEFILTER,kMaxRatio))){*pick=ShotVoice{};return;}
        pick->rate=rate;pick->channels=channels;
    }
    XAUDIO2_BUFFER b{};
    b.Flags=XAUDIO2_END_OF_STREAM;b.AudioBytes=static_cast<UINT32>(c.pcm.size()*2);b.pAudioData=reinterpret_cast<const BYTE*>(c.pcm.data());
    pick->v->Stop(0);pick->v->FlushSourceBuffers();
    if(FAILED(pick->v->SubmitSourceBuffer(&b)))return;
    Hear(pick->v,channels,h);
    pick->v->Start(0);
}

// --- The Debug tallies ---
namespace {
constexpr ULONGLONG kNoteMs=2000,kStatsMs=5000;
constexpr int kTallies=64;
struct Tally { const char* name; unsigned count; ULONGLONG noted; };
SRWLOCK tallyLock=SRWLOCK_INIT;
Tally tallies[kTallies]{};
ULONGLONG statsAt=0;
// The row of `name` (pointer or text the same), made if it is not there yet; nullptr: the table full.
Tally* TallyOf(const char* name) noexcept {
    for(Tally& t:tallies) {
        if(!t.name){t.name=name;return &t;}
        if(t.name==name || std::strcmp(t.name,name)==0)return &t;
    }
    return nullptr;
}
}  // namespace

void Note(const char* event,int layers,float distance,const Heard& first) noexcept {
    if(TraceRecording() && event) {   // the offline replay's record of it (trace_rec.cpp), Debug or not
        char name[64];std::snprintf(name,sizeof(name),"note.%s",event);
        const float d[3]={static_cast<float>(layers),distance,first.gain};TraceOutput(name,d,sizeof(d));
    }
    if(!Cfg().debug || !event || !event[0])return;
    const ULONGLONG now=GetTickCount64();
    bool log=false;
    AcquireSRWLockExclusive(&tallyLock);
    if(Tally* t=TallyOf(event)) {
        ++t->count;
        if(!t->noted || now-t->noted>=kNoteMs){t->noted=now;log=true;}
    }
    ReleaseSRWLockExclusive(&tallyLock);
    if(!log)return;
    float m[XAUDIO2_MAX_AUDIO_CHANNELS]{};
    surround::Matrix(layout,surround::Placing{first.gain,first.side,first.front,first.spread,first.sub},1,m);
    char row[XAUDIO2_MAX_AUDIO_CHANNELS*8+1]{};
    int at=0;
    for(UINT32 c=0;c<masterChannels && c<XAUDIO2_MAX_AUDIO_CHANNELS && at<static_cast<int>(sizeof(row))-8;++c)
        at+=std::snprintf(row+at,sizeof(row)-static_cast<std::size_t>(at),c ? " %.3f" : "%.3f",static_cast<double>(m[c]));
    Log("SOUND event %s: %d layer(s) at %.0f m; first gain %.3f side %.2f front %.2f spread %.2f rate %.3f cutoff %.0f Hz%s; matrix [%s] (%u ch)",
        event,layers,static_cast<double>(distance),static_cast<double>(first.gain),static_cast<double>(first.side),
        static_cast<double>(first.front),static_cast<double>(first.spread),static_cast<double>(first.ratio),
        static_cast<double>(first.cutoff),first.sub ? " LFE" : "",row,masterChannels);
}

void Count(const char* what,int n) noexcept {
    if(!Cfg().debug || !what || n<=0)return;
    AcquireSRWLockExclusive(&tallyLock);
    if(Tally* t=TallyOf(what))t->count+=static_cast<unsigned>(n);
    ReleaseSRWLockExclusive(&tallyLock);
}

void StatsTick() noexcept {
    if(!Cfg().debug || !ok)return;
    const ULONGLONG now=GetTickCount64();
    if(!statsAt){statsAt=now;return;}
    if(now-statsAt<kStatsMs)return;
    statsAt=now;
    char list[1024]{};
    int at=0;
    unsigned any=0;
    AcquireSRWLockExclusive(&tallyLock);
    for(Tally& t:tallies) {
        if(!t.name || !t.count)continue;
        any+=t.count;
        if(at<static_cast<int>(sizeof(list))-64)
            at+=std::snprintf(list+at,sizeof(list)-static_cast<std::size_t>(at),"%s%s=%u",at ? " " : "",t.name,t.count);
        t.count=0;
    }
    ReleaseSRWLockExclusive(&tallyLock);
    const unsigned started=shotsStarted.exchange(0,std::memory_order_relaxed),dropped=shotsDropped.exchange(0,std::memory_order_relaxed);
    const unsigned noneFree=loopsNoneFree.exchange(0,std::memory_order_relaxed);
    const int busyPeak=shotsBusyPeak.exchange(0,std::memory_order_relaxed);
    const int open=loopsOpen.load(std::memory_order_relaxed);
    const int loopPeak=loopsPeak.exchange(open,std::memory_order_relaxed);
    if(!any && !started && !dropped && !noneFree)return;
    Log("SOUND stats %llu s: one-shots started %u dropped %u, busy peak %d/%d; loops open %d peak %d/%d, none free %u; limiter %s, %u ch; %s",
        static_cast<unsigned long long>(kStatsMs/1000),started,dropped,busyPeak,kShots,open,loopPeak,kLoops,noneFree,
        limiterOn ? "on" : "off",masterChannels,at ? list : "(no events)");
}
}  // namespace crew::audio
