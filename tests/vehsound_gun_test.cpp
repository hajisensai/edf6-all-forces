// src/vehsound.cpp as built (the production source), with the game's readers and the voices replaced: a 403's rail gun
// heard as the plugin's own report (Bore::rail: no War Thunder event), counted once a shot in Debug's tally
// (`report_rail`, audio::Note) and its near / far clips played after the sound's travel time; its stock FireSe held while
// ours plays and given back when the group is off; a reload, a frame with no shot and a second look in one frame no
// report; a shot received from another machine (+0x1544) one; two rail guns firing in one frame one report; a far tank
// still heard. What took a running game (a 403 that fires its rail gun near the camera) as a fixture: no game, no
// mission, a few milliseconds.
#include "../src/vehsound.cpp"
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>

namespace fixture {
crew::Config config{};
ULONGLONG now=1000,frame=1;
float listener[3]{};
// The 403: its vtable (CarBase's engine sound at kEngineSlot), its controller, one seat holding its rail gun and a
// second one (the 4-round version) a test may give it; no engine / turret presets (an empty table: nothing to hold).
void* vtable[0x300/8]{};
unsigned char vehicle[0x2000]{},ctrl[0x40]{},seats[2*crew::kSeatStride]{},holders[2*0x48]{},weapon[2][0x1600]{};
unsigned char* holderList[2]{};   // the seat's weapons: pointers to their holders
unsigned char stockCue[crew::kCueBytes]{};   // the FireSe's {bank, cue} as the game made it
std::map<std::string,int> notes;             // Debug's tally: audio::Note's event, a count per call
std::map<int,int> played;                    // the one-shots played (audio::PlayOnce), by clip
int fails=0,checks=0;
void Check(bool ok,const char* what){++checks;if(!ok){++fails;std::printf("FAIL: %s\n",what);}}
int Note(const char* n){const auto i=notes.find(n);return i==notes.end() ? 0 : i->second;}
int Played(int clip){const auto i=played.find(clip);return i==played.end() ? 0 : i->second;}

// The 403's rail gun as its SGO makes it (vsound_check's numbers): FireInterval 180, AmmoDamage 600, its FireSe at
// volume 1 not looping, a magazine of `ammo`.
void Gun(unsigned char* w,int ammo){
    std::memset(w,0,0x1600);
    edf::Put<std::int32_t>(w,crew::kFireInterval,180);edf::Put<std::int32_t>(w,crew::kFireBurst,1);
    edf::Put<float>(w,crew::kWeaponDamage,600.0f);edf::Put<float>(w,crew::kWeaponBlast,0.0f);
    edf::Put<float>(w,crew::kFirePreset+crew::kPresetVolume,1.0f);edf::Put<float>(w,crew::kFirePreset+crew::kPresetPitch,1.0f);
    std::memcpy(w+crew::kFirePreset+crew::kCues,stockCue,sizeof stockCue);
    edf::Put<std::int32_t>(w,crew::kWeaponAmmo,ammo);edf::Put<std::int32_t>(w,crew::kReloadTime,600);
}
void Reset(float distance,int guns=1){
    crew::sigOk=true;crew::ResetVehicleSound();notes.clear();played.clear();
    config=crew::Config{};config.enabled=true;config.vehicleSound=true;
    now=1000;frame=1;
    for(int i=0;i<8;++i)stockCue[i]=static_cast<unsigned char>(0xA0+i);   // a bank pointer's bytes: not null
    std::memset(vehicle,0,sizeof vehicle);std::memset(seats,0,sizeof seats);std::memset(holders,0,sizeof holders);
    vtable[crew::kEngineSlot]=crew::image+crew::kEngineSound;
    edf::Put<void*>(vehicle,0,vtable);edf::Put<void*>(vehicle,crew::kSelfCtrl,ctrl);
    const float at[3]={distance,0.0f,0.0f};std::memcpy(vehicle+crew::kPosition,at,12);
    edf::Put<float>(vehicle,crew::kMatrix+40,1.0f);   // its nose along +z
    edf::Put<void*>(vehicle,crew::kSeats,seats);edf::Put<std::uint64_t>(vehicle,crew::kSeatCount,1);
    holderList[0]=holders;holderList[1]=holders+0x48;edf::Put<void*>(seats,crew::kSeatWeapons,holderList);edf::Put<std::uint64_t>(seats,crew::kSeatWeaponCount,static_cast<std::uint64_t>(guns));
    for(int i=0;i<2;++i){edf::Put<void*>(holders+i*0x48,crew::kHolderWeapon,weapon[i]);Gun(weapon[i],i ? 4 : 10);}
}
void Step(){++frame;now+=16;crew::VehicleSound(vehicle);}
void Steps(int n){for(int i=0;i<n;++i)Step();}
// A shot as the game's fire makes it (0x6981E0: the wait set to FireInterval; a round gone).
void Fire(unsigned char* w){
    edf::Put<std::int32_t>(w,crew::kWeaponAmmo,edf::At<std::int32_t>(w,crew::kWeaponAmmo)-1);
    edf::Put<float>(w,crew::kCooldown,static_cast<float>(edf::At<std::int32_t>(w,crew::kFireInterval)));
}
// The game's countdown of the wait, a frame.
void Cool(unsigned char* w){
    const float c=edf::At<float>(w,crew::kCooldown);
    edf::Put<float>(w,crew::kCooldown,c>1.0f ? c-1.0f : 0.0f);
}
void Run(int n){for(int i=0;i<n;++i){Cool(weapon[0]);Cool(weapon[1]);Step();}}
bool Held(const unsigned char* w){return edf::At<const void*>(w,crew::kFirePreset+crew::kCues)==nullptr;}
}  // namespace fixture

unsigned char* crew::image=reinterpret_cast<unsigned char*>(0x180000000ull);   // never read: the vtable's slot compared
namespace crew {
const Config& Cfg() noexcept{return fixture::config;}
ULONGLONG GameMs() noexcept{return fixture::now;}
ULONGLONG GameFrame() noexcept{return fixture::frame;}
bool WeaponStatusOk() noexcept{return true;}
bool SoundListening() noexcept{return true;}
bool ListenerAt(float* pos) noexcept{pos[0]=pos[1]=pos[2]=0.0f;return true;}
const char* VehicleClassName(const void*) noexcept{return "403_Tank";}
PluginBody BodyOf(const void*) noexcept{return PluginBody::none;}
bool IsHelicopter(const void*) noexcept{return false;}
bool IsPlayerJet(const void*) noexcept{return false;}
bool IsSub(const void*) noexcept{return false;}
bool ReadRound(const unsigned char*,RoundModel* out) noexcept{
    *out=RoundModel{};out->kind=RoundKind::arc;out->label="CANNON";out->rtti=".?AVFactory@SolidBullet01Rail@@";
    out->speed=40.0f;out->alive=60;
    return true;
}
bool SoundAt(const float* pos,const float*,SoundPlace* out) noexcept{
    float d[3];for(int i=0;i<3;++i)d[i]=pos[i]-fixture::listener[i];
    *out=SoundPlace{};out->distance=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);out->left=out->right=0.7f;out->doppler=1.0f;out->front=1.0f;
    return true;
}
void Log(const char* fmt,...) noexcept{va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);std::printf("\n");}
}  // namespace crew
namespace crew::audio {
bool ClipsReady() noexcept{return true;}
int OpenLoop(int) noexcept{return -1;}
int OpenWtLoop(int) noexcept{return -1;}
int LoopsOpen() noexcept{return 0;}
void SetLoop(int,const Heard&) noexcept{}
void CloseLoop(int) noexcept{}
void PlayOnce(int clip,const Heard& h) noexcept{if(h.gain>0.0f)++fixture::played[clip];}
bool WtThere(int) noexcept{return true;}
void PlayWt(int,int,const Heard&) noexcept{}
void Note(const char* event,int layers,float,const Heard&) noexcept{fixture::notes[event]+=layers;}
}  // namespace crew::audio

int main(){
    using namespace fixture;
    namespace vmix=crew::vmix;namespace audio=crew::audio;
    // The profile the fixture's gun is read as: the rail gun's (vehmix.h ProfileOf), a report with no War Thunder event.
    Reset(100.0f);Step();
    Check(crew::vehicles[0].guns==1 && crew::vehicles[0].gun[0].bore==vmix::Bore::rail,"the 403's gun is told a rail gun");
    Check(crew::EventOf(vmix::Bore::rail)==nullptr,"a rail gun has no War Thunder event: the plugin's own report");
    Check(Held(weapon[0]),"its stock FireSe held while ours plays (no two at once)");
    Run(30);
    Check(Note("report_rail")==0 && Played(audio::kClipRailShot)==0,"no shot: no report");

    // Three shots, each its interval apart: three reports, each its near and far clips after the travel time.
    for(int s=0;s<3;++s){Fire(weapon[0]);Run(181);}
    Check(Note("report_rail")==3,"three rail gun shots: report_rail counted three times");
    Check(Note("report_other")==0,"...and never as another calibre's");
    Check(Played(audio::kClipRailShot)==3 && Played(audio::kClipRailFar)==3,"each shot's near and far clips played (100 m: both heard)");
    Check(Note("reload_insert")==0,"no loader's step heard past kReloadHear (80 m)");

    // A second look in one frame (VehicleSound per seat's input, the same frame): no second report.
    Fire(weapon[0]);Cool(weapon[0]);Step();crew::VehicleSound(vehicle);crew::VehicleSound(vehicle);Run(181);
    Check(Note("report_rail")==4,"a vehicle looked at more than once a frame reports a shot once");

    // A reload (rounds up) is no shot; the magazine's long wait neither.
    edf::Put<std::int32_t>(weapon[0],crew::kWeaponAmmo,10);Run(5);
    Check(Note("report_rail")==4,"a reload: no report");

    // Online: the shot count received from the operator's machine (+0x1544) above the copy's own (+0xBD0).
    edf::Put<std::int32_t>(weapon[0],crew::kWeaponShots,1);Run(2);
    Check(Note("report_rail")==5,"a shot received from another machine: a report");
    edf::Put<std::int32_t>(weapon[0],crew::kWeaponFired,1);Run(181);
    Check(Note("report_rail")==5,"...once, the copy's own count caught up");

    // The sound travels: played only after distance / 340 m/s.
    Fire(weapon[0]);Cool(weapon[0]);Step();
    const int nearBefore=Played(audio::kClipRailShot);
    Check(Note("report_rail")==6 && nearBefore==5,"the report noted at the shot, its clip not played yet (100 m: 294 ms)");
    Run(10);Check(Played(audio::kClipRailShot)==5,"...nor 160 ms later");
    Run(15);Check(Played(audio::kClipRailShot)==6,"...played once the sound has come (400 ms)");

    // Near: the capacitors' charge (the loader's step, 90 frames before ready) heard within kReloadHear.
    Reset(20.0f);Step();Fire(weapon[0]);Run(181);
    Check(Note("report_rail")==1 && Played(audio::kClipRailCharge)==1 && Played(audio::kClipRailReady)==1,
          "near (20 m): the shot's report, the capacitors' charge and the ready click before it is ready again");

    // Two rail guns in one frame (the 4-round version beside it): one report a calibre a frame.
    Reset(100.0f,2);Step();
    Check(crew::vehicles[0].guns==2 && crew::vehicles[0].gun[1].bore==vmix::Bore::rail,"the second gun told a rail gun too");
    Fire(weapon[0]);Fire(weapon[1]);Run(181);
    Check(Note("report_rail")==1,"two rail guns firing in one frame: one report");

    // Far off (2 km): still the report (its far clip).
    Reset(2000.0f);Step();Fire(weapon[0]);Run(400);
    Check(Note("report_rail")==1 && Played(audio::kClipRailFar)==1,"a 403 2 km off: its report heard (the far clip)");

    // The main guns' volume 0: the stock FireSe given back, nothing of ours.
    Reset(100.0f);Step();config.vehicleGunVolume=0.0f;Step();
    Check(!Held(weapon[0]) && std::memcmp(weapon[0]+crew::kFirePreset+crew::kCues,stockCue,sizeof stockCue)==0,
          "VehicleGunVolume=0: the stock FireSe given back as it was");
    Fire(weapon[0]);Run(181);
    Check(Note("report_rail")==0,"...and no report of ours");

    // VehicleSound=0: everything given back.
    Reset(100.0f);Step();config.vehicleSound=false;Step();
    Check(!Held(weapon[0]),"VehicleSound=0: the stock FireSe given back");

    std::printf("vehsound_gun: %d of %d checks passed\n",checks-fails,checks);
    return fails ? 1 : 0;
}
