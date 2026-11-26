// A recording of the vehicles' sounds in the real game (src/trace_rec.cpp, ini TraceFile; docs/trace-replay.md) played
// back through src/vehsound.cpp as built (the production source): every recorded call's memory put back at the address
// it had in the game (its pointers are its own), the rest it asks for (the listener, the config, a class's name, a
// weapon's round) answered from the recording, the real edf readers (SeatCount, SeatRider, MeanMuzzle, Readable) run on
// that memory. Then:
//  - a recording made from these very sources (its header's hash == EDF6_TRACE_SOURCES): the replay must do what the game
//    did, output by output (each sound and Debug tally, in order; its gain and distance within 1e-3). A difference is
//    the recorder missing what the step reads (or a step that reads the wall clock): fix the recorder;
//  - always: the --at-least NAME=N expectations (a Debug tally, `note.report_rail`, a sound, `once`) over the replay.
// No game, no mission: a recording of minutes plays in a second or two.
//
//   vehsound_replay_test TRACE [--at-least NAME=N]... [--list]
//
// Exit 0: every check passed; 1: one failed; 2: the recording cannot be read or placed.
#include "../src/vehsound.cpp"
#include "../common/edf/trace.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#ifndef EDF6_TRACE_SOURCES
#define EDF6_TRACE_SOURCES ""
#endif

namespace replay {
namespace trace=edf::trace;
static_assert(std::is_trivially_copyable_v<crew::Config>,"the config is recorded as its bytes");
struct Status { std::uint8_t placing,listening,weaponStatus,clipsReady; float listener[crew::soundplace::kListenerFloats],listenerVel[3]; };
struct Round { std::uint8_t ok; crew::RoundModel m; };
struct Output { std::string name; std::vector<float> data; };

crew::Config config{};
Status status{};
std::uint8_t wt[(crew::wt::kCount+7)/8]{};
ULONGLONG now=0,frame=0;
std::map<std::pair<std::uint64_t,std::string>,std::string> values;   // the latest of each object's named value
std::vector<Output> outputs;                                          // the replay's, this call
int nextLoop=0;

const std::string* ValueOf(const void* object,const char* name) {
    const auto it=values.find({reinterpret_cast<std::uint64_t>(object),name});
    return it==values.end() ? nullptr : &it->second;
}
template<class T> bool ValueOf(const void* object,const char* name,T* out) {
    const std::string* v=ValueOf(object,name);
    if(!v || v->size()!=sizeof(T))return false;
    std::memcpy(out,v->data(),sizeof(T));
    return true;
}
const char* StringOf(const void* object,const char* name) {
    const std::string* v=ValueOf(object,name);
    return v && !v->empty() ? v->c_str() : nullptr;   // the recorded bytes end in their NUL
}
void Put(const char* name,std::initializer_list<float> data){outputs.push_back(Output{name,std::vector<float>(data)});}

// Every page any region of the recording lies on, committed at its address (the game's) before the first call.
bool Place(const trace::File& f,std::string* error) {
    SYSTEM_INFO si{};GetSystemInfo(&si);
    const std::uint64_t page=si.dwPageSize,grain=si.dwAllocationGranularity;
    std::set<std::uint64_t> pages;
    for(const auto& r:f.records)
        if(r.type==trace::kRegion && !r.data.empty())
            for(std::uint64_t p=r.address/page*page;p<r.address+r.data.size();p+=page)pages.insert(p);
    // Reserved by the allocation granularity's blocks (64 KB), committed page by page.
    std::set<std::uint64_t> blocks;
    for(const auto p:pages)blocks.insert(p/grain*grain);
    for(const auto b:blocks) {
        void* const at=reinterpret_cast<void*>(b);
        MEMORY_BASIC_INFORMATION mbi{};
        if(VirtualQuery(at,&mbi,sizeof(mbi)) && mbi.State!=MEM_FREE) {
            if(mbi.State==MEM_RESERVE && mbi.AllocationBase==at)continue;   // ours already
            char m[160];std::snprintf(m,sizeof(m),"the game's address %p is taken in this process (state %lX)",at,mbi.State);
            *error=m;return false;
        }
        if(VirtualAlloc(at,grain,MEM_RESERVE,PAGE_READWRITE)!=at) {
            char m[96];std::snprintf(m,sizeof(m),"cannot reserve the game's address %p (%lu)",at,GetLastError());
            *error=m;return false;
        }
    }
    for(const auto p:pages)
        if(!VirtualAlloc(reinterpret_cast<void*>(p),page,MEM_COMMIT,PAGE_READWRITE)) {
            char m[96];std::snprintf(m,sizeof(m),"cannot commit the game's page %p (%lu)",reinterpret_cast<void*>(p),GetLastError());
            *error=m;return false;
        }
    return true;
}
}  // namespace replay

namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept{return replay::config;}
ULONGLONG GameMs() noexcept{return replay::now;}
ULONGLONG GameFrame() noexcept{return replay::frame;}
bool WeaponStatusOk() noexcept{return replay::status.weaponStatus!=0;}
bool SoundListening() noexcept{return replay::status.listening!=0;}
bool SoundAt(const float* pos,const float* vel,SoundPlace* out) noexcept{
    if(!replay::status.placing)return false;
    *out=soundplace::Place(replay::status.listener,replay::status.listenerVel,pos,vel);
    return true;
}
const char* VehicleClassName(const void* v) noexcept{const char* s=replay::StringOf(v,"class");return s ? s : "vehicle";}
namespace { std::int32_t Kind(const void* v,int i) noexcept{std::int32_t k[4]{};replay::ValueOf(v,"kind",&k);return k[i];} }
PluginBody BodyOf(const void* v) noexcept{return static_cast<PluginBody>(Kind(v,0));}
bool IsHelicopter(const void* v) noexcept{return Kind(v,1)!=0;}
bool IsPlayerJet(const void* v) noexcept{return Kind(v,2)!=0;}
bool IsSub(const void* v) noexcept{return Kind(v,3)!=0;}
bool ReadRound(const unsigned char* w,RoundModel* out) noexcept{
    replay::Round r{};
    if(!replay::ValueOf(w,"round",&r) || !r.ok)return false;
    *out=r.m;out->label=replay::StringOf(w,"round.label");out->rtti=replay::StringOf(w,"round.rtti");
    return true;
}
void Log(const char* fmt,...) noexcept{va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);std::printf("\n");}
}  // namespace crew
namespace crew::audio {
bool ClipsReady() noexcept{return replay::status.clipsReady!=0;}
int OpenLoop(int) noexcept{return replay::nextLoop++;}
void SetLoop(int,const Heard&) noexcept{}
void CloseLoop(int) noexcept{}
void PlayOnce(int clip,const Heard& h) noexcept{replay::Put("once",{static_cast<float>(clip),h.gain});}
bool WtThere(int c) noexcept{return c>=0 && c<wt::kCount && (replay::wt[c/8]>>(c%8)&1)!=0;}
void PlayWt(int clip,int variant,const Heard& h) noexcept{replay::Put("wt",{static_cast<float>(clip),static_cast<float>(variant),h.gain});}
void Note(const char* event,int layers,float distance,const Heard& first) noexcept{
    replay::Put((std::string("note.")+event).c_str(),{static_cast<float>(layers),distance,first.gain});
}
}  // namespace crew::audio

namespace {
using namespace replay;
bool Same(const Output& a,const trace::Record& b) {
    if(a.name!=b.name || b.data.size()!=a.data.size()*sizeof(float))return false;
    for(std::size_t i=0;i<a.data.size();++i) {
        float x;std::memcpy(&x,b.data.data()+i*sizeof(float),sizeof(float));
        if(std::fabs(x-a.data[i])>1e-3f*std::fmax(1.0f,std::fabs(x)))return false;
    }
    return true;
}
std::string Show(const std::string& name,const float* d,std::size_t n) {
    std::string s=name+"(";
    for(std::size_t i=0;i<n;++i){char b[24];std::snprintf(b,sizeof(b),"%s%g",i ? "," : "",d[i]);s+=b;}
    return s+")";
}
}  // namespace

int main(int argc,char** argv){
    if(argc<2){std::printf("usage: vehsound_replay_test TRACE [--at-least NAME=N]... [--list]\n");return 2;}
    std::map<std::string,long long> atLeast;
    bool list=false;
    for(int i=2;i<argc;++i) {
        const std::string a=argv[i];
        if(a=="--list")list=true;
        else if(a=="--at-least" && i+1<argc) {
            const std::string e=argv[++i];const auto eq=e.find('=');
            if(eq==std::string::npos){std::printf("--at-least wants NAME=N: %s\n",e.c_str());return 2;}
            atLeast[e.substr(0,eq)]=std::atoll(e.c_str()+eq+1);
        } else {std::printf("unknown argument %s\n",a.c_str());return 2;}
    }
    const trace::File f=trace::Read(argv[1]);
    if(!f.error.empty()){std::printf("vehsound_replay: %s: %s\n",argv[1],f.error.c_str());return 2;}
    std::string error;
    if(!Place(f,&error)){std::printf("vehsound_replay: %s\n",error.c_str());return 2;}
    crew::image=reinterpret_cast<unsigned char*>(f.header.image);
    crew::sigOk=true;   // InstallVehicleSound's signatures were checked in the game that recorded it
    const bool exact=f.header.sources[0] && !std::strcmp(f.header.sources,EDF6_TRACE_SOURCES);
    std::printf("vehsound_replay: %s, build %s, %zu records; sources %s: %s\n",argv[1],f.header.build,f.records.size(),
                exact ? "the same" : "changed since",exact ? "the game's outputs checked one by one" : "only the --at-least expectations");

    std::map<std::string,long long> tally,recordedTally;
    long long calls=0,differ=0,shown=0;
    std::size_t i=0;
    while(i<f.records.size()) {
        const trace::Record& r=f.records[i];
        if(r.type==trace::kReset){crew::ResetVehicleSound();++i;continue;}
        if(r.type!=trace::kCall){++i;continue;}   // (values before any call: none are written so)
        now=r.ms;frame=r.frame;
        void* const object=reinterpret_cast<void*>(r.object);
        std::vector<const trace::Record*> recorded;
        for(++i;i<f.records.size() && f.records[i].type!=trace::kCall && f.records[i].type!=trace::kReset;++i) {
            const trace::Record& in=f.records[i];
            if(in.type==trace::kRegion)std::memcpy(reinterpret_cast<void*>(in.address),in.data.data(),in.data.size());
            else if(in.type==trace::kValue) {
                values[{in.object,in.name}]=std::string(in.data.begin(),in.data.end());
                if(!in.object && in.name=="status" && in.data.size()==sizeof(Status))std::memcpy(&status,in.data.data(),sizeof(Status));
                else if(!in.object && in.name=="wt" && in.data.size()==sizeof(wt))std::memcpy(wt,in.data.data(),sizeof(wt));
                else if(!in.object && in.name=="cfg" && in.data.size()==sizeof(crew::Config))std::memcpy(&config,in.data.data(),sizeof(crew::Config));
            } else if(in.type==trace::kOutput)recorded.push_back(&in);
        }
        if(r.step!=trace::kVehicleSound)continue;
        ++calls;
        outputs.clear();
        crew::VehicleSound(static_cast<unsigned char*>(object));
        for(const auto& o:outputs)++tally[o.name];
        for(const auto* o:recorded)++recordedTally[o->name];
        if(exact) {
            const std::size_t n=outputs.size()>recorded.size() ? outputs.size() : recorded.size();
            for(std::size_t k=0;k<n;++k) {
                const bool have=k<outputs.size(),want=k<recorded.size();
                if(have && want && Same(outputs[k],*recorded[k]))continue;
                ++differ;
                if(shown++<20) {
                    const std::string got=have ? Show(outputs[k].name,outputs[k].data.data(),outputs[k].data.size()) : "nothing";
                    const std::string game=want ? Show(recorded[k]->name,reinterpret_cast<const float*>(recorded[k]->data.data()),recorded[k]->data.size()/4) : "nothing";
                    std::printf("DIFFER: frame %llu vehicle %p output %zu: the replay %s, the game %s\n",
                                static_cast<unsigned long long>(frame),object,k,got.c_str(),game.c_str());
                }
                break;   // the rest of this call follows from the first difference
            }
        }
    }
    int fails=0;
    if(list || differ) {
        std::printf("%-34s %10s %10s\n","output","replay","game");
        std::set<std::string> names;
        for(const auto& t:tally)names.insert(t.first);
        for(const auto& t:recordedTally)names.insert(t.first);
        for(const auto& n:names)std::printf("%-34s %10lld %10lld\n",n.c_str(),tally[n],recordedTally[n]);
    }
    if(!calls){std::printf("FAIL: the recording has no call of the vehicles' sounds\n");++fails;}
    if(differ){std::printf("FAIL: %lld call(s) of %lld did otherwise than the game\n",differ,calls);++fails;}
    for(const auto& [name,n]:atLeast)
        if(tally[name]<n){std::printf("FAIL: %s %lld times, expected at least %lld\n",name.c_str(),tally[name],n);++fails;}
    std::printf("vehsound_replay: %lld calls replayed, %s\n",calls,fails ? "FAILED" : "all checks passed");
    return fails ? 1 : 0;
}
