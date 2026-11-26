// The recorder (src/trace_rec.cpp) and the vehicles' sounds (src/vehsound.cpp) as built, on a 403 made in memory instead
// of the game's: it fires its rail gun near the camera, then far, and its coaxial gun's reload comes round, every frame
// recorded as the game would be. The recording it writes is played back by vehsound_replay_test in another process
// (ctest: vehsound_trace_record -> vehsound_trace_replay), so the recorder and the replay are checked with each other
// without a game: the memory at the very addresses it had here, the outputs one by one.
//
//   vehsound_trace_fixture OUT.trc
#include "../src/trace_rec.cpp"
#include "../src/vehsound.cpp"
#include <cstdarg>
#include <cstdio>

namespace fixture {
// Everything at a fixed address of its own, far from where a process's images and heaps go, so the replay can put it
// back at the same place.
constexpr std::uintptr_t kBase=0x0000500000000000ull;
crew::Config config{};
ULONGLONG now=1000,frame=1;
float listener[crew::soundplace::kListenerFloats]{},listenerVel[3]{};
unsigned char* block=nullptr;
unsigned char *vehicle,*ctrl,*seats,*holders,*weapon,*holderList,*vtable;
std::uint8_t* const fakeImage=reinterpret_cast<std::uint8_t*>(kBase+0x10000000ull);   // never read: a slot compared with it

void Make(){
    block=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(kBase),0x100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!block){std::printf("vehsound_trace_fixture: cannot allocate at %p\n",reinterpret_cast<void*>(kBase));std::exit(2);}
    vehicle=block;ctrl=block+0x4000;seats=block+0x5000;holderList=block+0x6000;holders=block+0x6100;vtable=block+0x7000;
    weapon=block+0x8000;
    edf::Put<void*>(vtable,0x250,fakeImage+0x676370);   // CarBase's engine sound: a heavy body
    edf::Put<void*>(vehicle,0,vtable);edf::Put<void*>(vehicle,crew::kSelfCtrl,ctrl);
    edf::Put<float>(vehicle,crew::kMatrix+40,1.0f);
    edf::Put<void*>(vehicle,crew::kSeats,seats);edf::Put<std::uint64_t>(vehicle,crew::kSeatCount,1);
    edf::Put<void*>(seats,crew::kSeatWeapons,holderList);edf::Put<std::uint64_t>(seats,crew::kSeatWeaponCount,1);
    edf::Put<void*>(holderList,0,holders);edf::Put<void*>(holders,crew::kHolderWeapon,weapon);
    // The 403's rail gun (its SGO's numbers), a magazine of 10.
    edf::Put<std::int32_t>(weapon,crew::kFireInterval,180);edf::Put<std::int32_t>(weapon,crew::kFireBurst,1);
    edf::Put<float>(weapon,crew::kWeaponDamage,600.0f);
    edf::Put<float>(weapon,crew::kFirePreset+crew::kPresetVolume,1.0f);edf::Put<float>(weapon,crew::kFirePreset+crew::kPresetPitch,1.0f);
    for(int i=0;i<8;++i)weapon[crew::kFirePreset+crew::kCues+i]=static_cast<unsigned char>(0xA0+i);
    edf::Put<std::int32_t>(weapon,crew::kWeaponAmmo,10);edf::Put<std::int32_t>(weapon,crew::kReloadTime,600);
    // The camera at the origin, its inverse world matrix the identity.
    for(int r=0;r<4;++r)listener[4+r*4+r]=1.0f;
}
void At(float x){const float p[3]={x,0.0f,0.0f};std::memcpy(vehicle+crew::kPosition,p,12);}
void Step(){
    ++frame;now+=16;
    const float c=edf::At<float>(weapon,crew::kCooldown);
    edf::Put<float>(weapon,crew::kCooldown,c>1.0f ? c-1.0f : 0.0f);
    // As crew.cpp's input step: the recording's inputs, the step, its end.
    crew::TraceVehicleSound(vehicle);crew::VehicleSound(vehicle);crew::TraceEnd();
}
void Fire(){
    edf::Put<std::int32_t>(weapon,crew::kWeaponAmmo,edf::At<std::int32_t>(weapon,crew::kWeaponAmmo)-1);
    edf::Put<float>(weapon,crew::kCooldown,180.0f);
}
}  // namespace fixture

namespace crew {
unsigned char* image=fixture::fakeImage;
const Config& Cfg() noexcept{return fixture::config;}
ULONGLONG GameMs() noexcept{return fixture::now;}
ULONGLONG GameFrame() noexcept{return fixture::frame;}
bool WeaponStatusOk() noexcept{return true;}
bool SoundListening() noexcept{return true;}
bool ListenerState(float* l,float* v) noexcept{std::memcpy(l,fixture::listener,sizeof(fixture::listener));std::memcpy(v,fixture::listenerVel,12);return true;}
bool SoundAt(const float* pos,const float* vel,SoundPlace* out) noexcept{*out=soundplace::Place(fixture::listener,fixture::listenerVel,pos,vel);return true;}
const char* VehicleClassName(const void*) noexcept{return "403_Tank";}
PluginBody BodyOf(const void*) noexcept{return PluginBody::none;}
bool IsHelicopter(const void*) noexcept{return false;}
bool IsPlayerJet(const void*) noexcept{return false;}
bool IsSub(const void*) noexcept{return false;}
bool ReadRound(const unsigned char*,RoundModel* out) noexcept{
    *out=RoundModel{};out->kind=RoundKind::arc;out->label="CANNON";out->rtti=".?AVFactory@SolidBullet01Rail@@";out->speed=40.0f;out->alive=60;
    return true;
}
void Log(const char* fmt,...) noexcept{va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);std::printf("\n");}
}  // namespace crew
// The voices as jetaudio.cpp records them (its hooks: the same names and numbers).
namespace crew::audio {
bool ClipsReady() noexcept{return true;}
int OpenLoop(int) noexcept{static int n=0;return n++;}
void SetLoop(int,const Heard&) noexcept{}
void CloseLoop(int) noexcept{}
bool WtThere(int) noexcept{return true;}
void PlayOnce(int clip,const Heard& h) noexcept{const float d[2]={static_cast<float>(clip),h.gain};TraceOutput("once",d,sizeof(d));}
void PlayWt(int clip,int variant,const Heard& h) noexcept{const float d[3]={static_cast<float>(clip),static_cast<float>(variant),h.gain};TraceOutput("wt",d,sizeof(d));}
void Note(const char* event,int layers,float distance,const Heard& first) noexcept{
    char name[64];std::snprintf(name,sizeof(name),"note.%s",event);
    const float d[3]={static_cast<float>(layers),distance,first.gain};TraceOutput(name,d,sizeof(d));
}
}  // namespace crew::audio

int main(int argc,char** argv){
    using namespace fixture;
    if(argc!=2){std::printf("usage: vehsound_trace_fixture OUT.trc\n");return 2;}
    config.enabled=true;config.vehicleSound=true;config.traceSeconds=600;
    std::swprintf(config.traceFile,std::size(config.traceFile),L"%hs",argv[1]);
    crew::sigOk=true;
    Make();
    crew::ResetVehicleSound();crew::TraceReset();   // a mission begun (mission.cpp)
    At(20.0f);
    for(int i=0;i<30;++i)Step();
    for(int s=0;s<3;++s){Fire();for(int i=0;i<181;++i)Step();}   // near: the reports, the charge and the ready click
    At(2000.0f);
    for(int i=0;i<10;++i)Step();
    Fire();for(int i=0;i<400;++i)Step();                           // far: the far clip after its 6 s on the way
    crew::ResetVehicleSound();crew::TraceReset();                   // a second mission: the replay resets there too
    At(100.0f);
    for(int i=0;i<10;++i)Step();
    Fire();for(int i=0;i<181;++i)Step();
    crew::tracerec::Finish("the fixture's end");
    return 0;
}
