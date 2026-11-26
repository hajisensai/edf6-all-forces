// The offline replay's recording (docs/trace-replay.md; the user, 2026-11-26: "不能全自动吗，为什么每次要重写"): with
// TraceFile set in the ini, every call of the vehicles' sounds (vehsound.cpp VehicleSound) from the first mission begun
// on is recorded for TraceSeconds game seconds: the game's memory the step reads, as it is when the step begins (the
// vehicle, its seats, their riders, holders, weapons and muzzles, its sound presets and the vtable slot it compares),
// whatever else it asks of the plugin and the game as values (the listener, the config, a class's name, a weapon's
// round, which War Thunder recordings were read), and what it did (its sounds and Debug's tally: jetaudio.cpp's hooks).
// tests/vehsound_replay_test.cpp plays a recording back through the same vehsound.cpp, its memory at the very addresses
// it had in the game. Nothing changes in the game: the recorder only reads (Readable first), on the game thread.
#include "crew.h"
#include "jetaudio.h"
#include "vhud.h"
#include "heli.h"
#include "layout.h"
#include "wt_clips.h"
#include "sound_place.h"
#include "build_id.h"
#include "edf/trace.h"
#include "edf/weapon.h"
#include "memory.h"
#include <cstring>
#include <string>
#include <unordered_map>

#ifndef EDF6_TRACE_SOURCES
#define EDF6_TRACE_SOURCES ""
#endif

namespace crew {
// Its own namespace, not an unnamed one: a test builds it with vehsound.cpp in one file (tests/vehsound_trace_fixture.cpp).
namespace tracerec {
namespace trace=edf::trace;
// What vehsound.cpp reads past the objects' own layouts (its constants, kept in step with it: a region missed shows as
// the replay of a fresh recording differing from what the game did, tests/vehsound_replay_test.cpp).
constexpr std::uint32_t kVehicleBytes=0x1B00;    // the vehicle up to its engine's load (+0x1A80) and the aim table
constexpr std::size_t kEngineSlotAt=0x250;       // the vtable slot compared with CarBase's engine sound
constexpr std::size_t kPresets=0x1A28,kPresetCount=0x1A38,kPresetSize=0x80;
constexpr std::size_t kAimTableData=0x1A48,kAimTableCount=0x1A58,kAimEntry=0xB0;
constexpr std::uint32_t kWeaponBytes=0x1600;     // the weapon up to its sent shot count (+0x1544)
constexpr std::uint32_t kHolderBytes=kHolderWeapon+8;
constexpr unsigned kMostSeats=8;
constexpr long long kMostBytes=1ll<<30;          // a recording stops at 1 GB whatever TraceSeconds says

trace::Writer out;
bool armed=false;       // a mission begun since load (TraceReset): the replay's state and the game's agree from here
bool finished=false;    // the recording closed (its seconds, its size, a write failing): none again until the game restarts
bool opened=false;
ULONGLONG startMs=0,frameWritten=~0ull;
std::unordered_map<std::string,std::string> written;   // the last bytes of each object's named value

void Mem(const void* at,std::uint32_t bytes) noexcept {
    if(at && bytes && Readable(at,bytes))out.Region(at,bytes);
}
// A value written only when it differs from the last of that object and name (a class's name, a weapon's round: the
// same every frame).
void ValueIfNew(const void* object,const char* name,const void* data,std::size_t size) noexcept {
    std::string key(reinterpret_cast<const char*>(&object),sizeof(object));key+=name;
    std::string bytes(static_cast<const char*>(data),size);
    auto it=written.find(key);
    if(it!=written.end() && it->second==bytes)return;
    written[key]=bytes;
    out.Value(object,name,data,size);
}
void StringIfNew(const void* object,const char* name,const char* s) noexcept {
    ValueIfNew(object,name,s ? s : "",s ? std::strlen(s)+1 : 0);   // empty: a null pointer
}

// The game's state once a frame: the clocks are in the call; the listener, the statuses, the recordings read.
struct Status { std::uint8_t placing,listening,weaponStatus,clipsReady; float listener[soundplace::kListenerFloats],listenerVel[3]; };
void Frame() noexcept {
    const ULONGLONG frame=GameFrame();
    if(frame==frameWritten)return;
    frameWritten=frame;
    Status s{};
    s.placing=ListenerState(s.listener,s.listenerVel) ? 1 : 0;
    s.listening=SoundListening() ? 1 : 0;s.weaponStatus=WeaponStatusOk() ? 1 : 0;s.clipsReady=audio::ClipsReady() ? 1 : 0;
    out.Value(nullptr,"status",s);
    std::uint8_t wt[(wt::kCount+7)/8]{};
    for(int c=0;c<wt::kCount;++c)if(audio::WtThere(c))wt[c/8]|=static_cast<std::uint8_t>(1u<<(c%8));
    ValueIfNew(nullptr,"wt",wt,sizeof(wt));
    ValueIfNew(nullptr,"cfg",&Cfg(),sizeof(Config));
}

// A weapon: its memory, its muzzles' (edf::MeanMuzzle reads them and their bones' records), its round (rounds.cpp's
// table: a value, the strings apart).
void Weapon(const unsigned char* w) noexcept {
    if(!w || !Readable(w,kWeaponBytes))return;
    Mem(w,kWeaponBytes);
    const auto muzzles=At<const unsigned char*>(w,edf::kMuzzles);
    const auto count=At<std::uint64_t>(w,edf::kMuzzleCount);
    if(count && count<=64 && Readable(muzzles,count*edf::kMuzzleStride)) {
        Mem(muzzles,static_cast<std::uint32_t>(count*edf::kMuzzleStride));
        for(std::uint64_t i=0;i<count;++i)Mem(At<const unsigned char*>(muzzles+i*edf::kMuzzleStride,0),edf::kBoneRows+0x40);
    }
    RoundModel m{};
    const bool ok=ReadRound(w,&m);
    StringIfNew(w,"round.label",ok ? m.label : nullptr);
    StringIfNew(w,"round.rtti",ok ? m.rtti : nullptr);
    m.label=nullptr;m.rtti=nullptr;
    struct { std::uint8_t ok; RoundModel m; } r{};
    r.ok=ok ? 1 : 0;r.m=m;
    ValueIfNew(w,"round",&r,sizeof(r));
}

void Vehicle(unsigned char* v) noexcept {
    Mem(v,kVehicleBytes);
    const auto vt=At<const unsigned char*>(v,0);
    Mem(vt+kEngineSlotAt,8);
    Mem(At<const void*>(v,kSelfCtrl),0x10);
    StringIfNew(v,"class",VehicleClassName(v));
    const std::int32_t kinds[4]={static_cast<std::int32_t>(BodyOf(v)),IsHelicopter(v) ? 1 : 0,IsPlayerJet(v) ? 1 : 0,IsSub(v) ? 1 : 0};
    ValueIfNew(v,"kind",kinds,sizeof(kinds));
    const auto presets=At<const unsigned char*>(v,kPresets);
    const auto np=At<std::uint64_t>(v,kPresetCount);
    if(np && np<=64)Mem(presets,static_cast<std::uint32_t>(np*kPresetSize));
    const auto aims=At<const unsigned char*>(v,kAimTableData);
    const auto na=At<std::uint64_t>(v,kAimTableCount);
    if(na && na<=6)Mem(aims,static_cast<std::uint32_t>(na*kAimEntry));
    const unsigned seats=SeatCount(v);
    if(!seats)return;
    Mem(At<const unsigned char*>(v,kSeats),seats*static_cast<std::uint32_t>(kSeatStride));
    for(unsigned si=0;si<seats && si<kMostSeats;++si) {
        const unsigned char* const seat=SeatAt(v,si);
        Mem(At<const void*>(seat,kSeatRiderCtrl),0x10);
        const auto rider=At<const unsigned char*>(seat,kSeatRider);
        Mem(rider,edf::kHumanPlayer+1);
        const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(!n || n>8 || !Readable(holders,n*8))continue;
        Mem(holders,static_cast<std::uint32_t>(n*8));
        for(std::uint64_t i=0;i<n;++i) {
            if(!Readable(holders[i],kHolderBytes))continue;
            Mem(holders[i],kHolderBytes);
            Weapon(At<const unsigned char*>(holders[i],kHolderWeapon));
        }
    }
}

void Output(const char* name,const void* data,std::size_t size) noexcept { out.Output(name,data,size); }

void Finish(const char* why) noexcept {
    if(opened)Log("TRACE closed (%s): %lld bytes",why,out.Bytes());
    out.Close();finished=true;traceOutput=nullptr;
}
bool Open() noexcept {
    trace::Header h{};
    h.version=trace::kVersion;
    h.image=reinterpret_cast<std::uint64_t>(image);
    strncpy_s(h.sources,EDF6_TRACE_SOURCES,_TRUNCATE);
    strncpy_s(h.build,EDF6_BUILD_ID,_TRUNCATE);
    if(!out.Open(Cfg().traceFile,h)){Log("TRACE cannot write %ls",Cfg().traceFile);finished=true;return false;}
    opened=true;startMs=GameMs();
    Log("TRACE recording the vehicles' sounds to %ls for %d s (build %s)",Cfg().traceFile,Cfg().traceSeconds,EDF6_BUILD_ID);
    out.Reset();   // it begins where a mission did
    return true;
}
void Begin(unsigned char* v) noexcept {
    traceOutput=nullptr;
    if(finished || !armed || !Cfg().traceFile[0] || !v)return;
    if(!opened && !Open())return;
    const ULONGLONG ms=GameMs();
    if(ms-startMs>static_cast<ULONGLONG>(Cfg().traceSeconds>0 ? Cfg().traceSeconds : 0)*1000ull){Finish("its seconds");return;}
    if(out.Bytes()>kMostBytes){Finish("1 GB");return;}
    out.Call(trace::kVehicleSound,v,GameFrame(),ms);
    Frame();
    Vehicle(v);
    traceOutput=&Output;
}
void Reset() noexcept {
    armed=true;
    if(opened && !finished){out.Reset();written.clear();frameWritten=~0ull;}
}
}  // namespace tracerec

void TraceVehicleSound(unsigned char* v) noexcept { tracerec::Begin(v); }
void TraceEnd() noexcept { traceOutput=nullptr; }
void TraceReset() noexcept { tracerec::Reset(); }
}  // namespace crew
