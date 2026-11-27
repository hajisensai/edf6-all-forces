// How many engine voices XAudio2 carries, and at what CPU (docs/feature-wt-sound.md 「声道的上限」, 2026-11-27): the
// plugin's own path (jetaudio.cpp: its engine, its master voice and limiter, its loops opened as OpenWtLoop opens them,
// set every frame as SetLoop sets them) with the jet's War Thunder rear recording (assets/sound/wt/jet_max_rear_1.wav),
// N voices at once for a few seconds each, every one silent (placed at gain 0: XAudio2 mixes it all the same).
// It needs an audio output (the machine's default device) and is no ctest: run it by hand.
//   cmake --build build --target jet_voice_bench && build\jet_voice_bench.exe [seconds]
#include "../src/jetaudio.cpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace crew {
namespace { Config config{}; }
const Config& Cfg() noexcept { return config; }
void Log(const char* format,...) noexcept {
    std::printf("  log: ");
    va_list a;va_start(a,format);std::vprintf(format,a);va_end(a);
    std::printf("\n");
}
}  // namespace crew

namespace {
using namespace crew::audio;
double Seconds(const FILETIME& f) noexcept {
    return static_cast<double>((static_cast<unsigned long long>(f.dwHighDateTime)<<32)|f.dwLowDateTime)/1e7;
}
double ProcessCpu() noexcept {
    FILETIME c,e,k,u;
    GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);
    return Seconds(k)+Seconds(u);
}
}  // namespace

int main(int argc,char** argv) {
    const double run=argc>1 ? std::atof(argv[1]) : 3.0;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);   // the game has COM up before the plugin opens its engine
    if(!Start()){std::printf("no XAudio2 engine / output device\n");return 2;}
    // The clip as the install puts it (read from the repository's copy, not the game's folder).
    const int g=crew::wt::k_jet_max_rear;
    UINT32 rate=0;int channels=1;
    if(!ReadWav(L"assets\\sound\\wt\\jet_max_rear_1.wav",wtClips[g][0].pcm,rate,&channels) || !rate) {
        std::printf("cannot read assets\\sound\\wt\\jet_max_rear_1.wav (run from the repository)\n");return 2;
    }
    wtClips[g][0].format=Pcm16Format(rate,static_cast<WORD>(channels));
    wtThere[g]=true;clipsAsked=true;clipsReady.store(true);   // no clip thread: this one clip
    SYSTEM_INFO si{};GetSystemInfo(&si);
    std::printf("clip %u Hz, %d channel(s), %zu samples; %lu logical processors; the pool %d loops\n",rate,channels,
                wtClips[g][0].pcm.size(),si.dwNumberOfProcessors,kLoops);
    std::printf("voices  opened  audio-thread%%  process-cpu%%(of one core)  SetLoop us/frame  glitches\n");
    const int counts[]={0,64,128,256,512,1024,2048};
    for(const int want:counts) {
        if(want>kLoops)break;
        int opened=0;
        int ids[kLoops];
        for(int i=0;i<want;++i){ids[i]=OpenWtLoop(g);if(ids[i]<0)break;++opened;}
        XAUDIO2_PERFORMANCE_DATA p0{},p1{};
        Sleep(200);
        engine->GetPerformanceData(&p0);
        const double cpu0=ProcessCpu();
        LARGE_INTEGER f,t0,t1;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t0);
        double setUs=0.0;int frames=0;
        const ULONGLONG end=GetTickCount64()+static_cast<ULONGLONG>(run*1000.0);
        while(GetTickCount64()<end) {
            LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
            for(int i=0;i<opened;++i) {   // as Voices sets them: a pitch, a low-pass, a place (silent)
                const float k=static_cast<float>((i*7+frames)%100)/100.0f;
                SetLoop(ids[i],Heard{0.0f,k-0.5f,0.5f,0.3f,0.8f+0.4f*k,4000.0f+12000.0f*k,false});
            }
            QueryPerformanceCounter(&b);
            setUs+=1e6*static_cast<double>(b.QuadPart-a.QuadPart)/static_cast<double>(f.QuadPart);
            ++frames;
            Sleep(16);
        }
        QueryPerformanceCounter(&t1);
        const double wall=static_cast<double>(t1.QuadPart-t0.QuadPart)/static_cast<double>(f.QuadPart);
        const double cpu=ProcessCpu()-cpu0;
        engine->GetPerformanceData(&p1);
        const double audio=p1.TotalCyclesSinceLastQuery ? 100.0*static_cast<double>(p1.AudioCyclesSinceLastQuery)/
                                                          static_cast<double>(p1.TotalCyclesSinceLastQuery) : 0.0;
        std::printf("%6d  %6d  %12.2f  %26.2f  %16.1f  %8u\n",want,opened,audio,100.0*cpu/wall,setUs/frames,
                    p1.GlitchesSinceEngineStarted-p0.GlitchesSinceEngineStarted);
        for(int i=0;i<opened;++i)CloseLoop(ids[i]);
    }
    return 0;
}
