// The picture-in-picture scope's lens view (src/scope_lens.cpp, the production hooks), offline, on a stand-in of the
// engine's System (4 viewport slots), its render recording 0x11978D0 (the slot loop: lock the current camera, the
// projection 0x118DFF0, the G-buffer set 0x112B230, the tail calls, the submit 0x111B410), its camera calls (the
// factory 0x82F60, the queue's join 0x1195A80, set camera 0x1193FA0, set viewport 0x1198C40) and its command ring:
//  - nothing wanted, a stale want, another camera, a stock-zoomed frame: the recording passes through untouched;
//  - wanted: the lens camera made once (following viewport 0's camera, viewport 3 set to S x S), armed only once the
//    engine took it in; then viewport 0's tail held and recorded after viewport 3's world, its G-buffer set again first
//    (the engine's own handle copy, balanced), the post's 0x48-byte block as it was for viewport 0; viewport 3's tail
//    never recorded, one lens command in its place, which calls InfantryScopeLensCommand with the device context;
//  - the lens camera aimed as viewport 0's camera, at the lens's field; taken off viewport 3 at the submit;
//  - a resize, the mission's cameras going (recreated), a split screen, viewport 3 in use, a camera that never joins:
//    refused once, logged, until the mission's reset (which lets the weak reference go).
// Not the game: whether the engine renders viewport 3 as docs/scope-pip-re.md §4 reads it is for the game (§7).
#include "../src/scope_lens.cpp"
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
int logs=0;
std::string lastLog;
void Log(const char* format,...) noexcept { ++logs;lastLog=format; }
ID3D11DeviceContext* commanded=nullptr;
int lensCommands=0;
void InfantryScopeLensCommand(ID3D11DeviceContext* context) noexcept { ++lensCommands;commanded=context; }
}

namespace {
using namespace crew;
using namespace crew::scopelens;
int checks=0,failures=0;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}

// MSVC's _Ref_count_base stand-in: +0 vtable, +8 uses, +0xC weaks.
struct Control { void* const* vtable; long uses; long weaks; int destroyed; int deleted; };
static_assert(offsetof(Control,uses)==8 && offsetof(Control,weaks)==0xC,"MSVC's control block");
void __fastcall Destroy(void* c){++static_cast<Control*>(c)->destroyed;}
void __fastcall DeleteThis(void* c){++static_cast<Control*>(c)->deleted;}
void* const kControlVtable[]={reinterpret_cast<void*>(&Destroy),reinterpret_cast<void*>(&DeleteThis)};

alignas(16) unsigned char sys[0xD00]{};
unsigned char* sysRef=sys;
alignas(16) unsigned char cam0[0x360]{},lensCam[0x360]{},otherCam[0x360]{};
Control control0{kControlVtable,1,1,0,0},lensControl{kControlVtable,0,0,0,0},otherControl{kControlVtable,1,1,0,0};
alignas(16) unsigned char commandContext[0x2A00]{};
// The list node viewport 3's list holds (next, prev, object, control).
alignas(16) unsigned char node[0x20]{},head[0x20]{};
const float kScopeFov=scopepip::kBaseFov/8.0f;

unsigned char* SlotAt(unsigned i){return sys+0x4D0+i*0x188;}
void SetSlotCamera(unsigned i,void* object,Control* control){Put<void*>(SlotAt(i),8,object);Put<void*>(SlotAt(i),0x10,control);}

// What the stand-in recorders saw, in order.
enum class Kind { project,gbuffer,tail,submit };
struct Event { Kind kind; int slot; unsigned tail; void* c; unsigned char block[0x48]; unsigned char handles[0x60]; float fov; };
std::vector<Event> events;
int recordingSlot=-1;   // the slot the stand-in loop is in (the recorders do not know it; the events say)
void __fastcall Project(void* camera,const int*) { Event e{};e.kind=Kind::project;e.slot=recordingSlot;e.c=camera;e.fov=At<float>(camera,0x24);events.push_back(e); }
void __fastcall Gbuffer(void*,void*,void* c,void*) {
    Event e{};e.kind=Kind::gbuffer;e.slot=recordingSlot;e.c=c;std::memcpy(e.handles,c,sizeof(e.handles));events.push_back(e);
}
template<unsigned T> void __fastcall Recorder(void*,void*,void* c,void* d) {
    Event e{};e.kind=Kind::tail;e.slot=recordingSlot;e.tail=T;e.c=c;
    if constexpr(T==post)std::memcpy(e.block,d,sizeof(e.block));
    events.push_back(e);
}
void __fastcall Submit(void*) { Event e{};e.kind=Kind::submit;e.slot=recordingSlot;events.push_back(e); }
int viewportSets=0;
void __fastcall SetViewport(void* system,unsigned i,const int* rect) {
    ++viewportSets;
    unsigned char* s=static_cast<unsigned char*>(system)+0x4D0+i*0x188;
    std::memcpy(s+0x34,rect,16);Put<int>(s,0x44,rect[2]);Put<int>(s,0x48,rect[3]);
    Put<void*>(s,0x28,At<std::uint64_t>(s,0x20) ? s : nullptr);   // targets (re)built when it has cameras
}
void __fastcall SetCamera(void* slot,void* weak) {
    auto s=static_cast<unsigned char*>(slot);
    auto w=static_cast<void**>(weak);
    auto next=static_cast<Control*>(w[1]);
    if(next)++next->weaks;
    if(auto old=At<Control*>(s,0x10))--old->weaks;
    Put<void*>(s,8,w[0]);Put<void*>(s,0x10,next);
    if(next)--next->weaks;   // the argument's own reference, released by the callee
}
int factories=0;
void* followed=nullptr;
void __fastcall Factory(void*,void* out,unsigned i,const float* world,void* desc) {
    ++factories;
    Check(i==3 && world[0]==1.0f && world[15]==1.0f && (reinterpret_cast<std::uintptr_t>(world)&15)==0 &&
          (reinterpret_cast<std::uintptr_t>(desc)&15)==0,"the factory: viewport 3, an aligned identity matrix and description");
    followed=At<void*>(desc,0x80);
    if(auto c=At<Control*>(desc,0x88))++c->weaks;   // the TraceCamera's own copy of the followed weak_ptr
    lensControl=Control{kControlVtable,1,1,0,0};   // the queue's strong reference, out's weak one
    auto o=static_cast<void**>(out);o[0]=lensCam;o[1]=&lensControl;
}
// The engine's next update (0x1195A80): the queued camera joins viewport 3's list, becomes current, targets built.
void Join() {
    unsigned char* v=SlotAt(3);
    Put<void*>(head,0,node);Put<void*>(node,0,head);Put<void*>(node,0x10,lensCam);Put<void*>(node,0x18,&lensControl);
    Put<void*>(v,0x18,head);Put<std::uint64_t>(v,0x20,1);
    alignas(16) void* w[2]={lensCam,&lensControl};++lensControl.weaks;SetCamera(v,w);
    Put<void*>(v,0x28,v);
}
// The mission's end (0x11956C0): every slot's camera and list gone.
void Teardown() {
    for(unsigned i=1;i<4;++i){alignas(16) void* w[2]={nullptr,nullptr};SetCamera(SlotAt(i),w);}
    Put<std::uint64_t>(SlotAt(3),0x20,0);Put<void*>(SlotAt(3),0x28,nullptr);
    lensControl.uses=0;++lensControl.destroyed;
}
// The command ring: units of 16 bytes.
alignas(16) unsigned char ring[16*64]{};
int allocated=0,committed=0;
void* __fastcall Alloc(void*,int n){void* u=ring+16*allocated;allocated+=n;return u;}
void __fastcall Commit(void*,int n){committed+=n;}
int handlesMade=0,handlesAssigned=0,handlesFreed=0;
void* __fastcall HandleNew(void* h){++handlesMade;std::memset(h,0,0x20);return h;}
void* __fastcall HandleAssign(void* h,const void* from){++handlesAssigned;std::memcpy(h,from,0x20);return h;}
void* __fastcall HandleFree(void* h){++handlesFreed;std::memset(h,0xDD,0x20);return h;}
const unsigned char kDescVtableStandIn[8]{};

void Wire() {
    systemRef=&sysRef;
    crew::scopelens::commands=commandContext;
    project=&Project;gbuffer=&Gbuffer;
    tails[post]=&Recorder<post>;tails[plain]=&Recorder<plain>;tails[copy]=&Recorder<copy>;
    tails[history]=&Recorder<history>;tails[modeTail]=&Recorder<modeTail>;
    submit=&Submit;setViewport=&SetViewport;setCamera=&SetCamera;factory=&Factory;alloc=&Alloc;commit=&Commit;
    handleNew=&HandleNew;handleAssign=&HandleAssign;handleFree=&HandleFree;descVtable=kDescVtableStandIn;
    installed.store(true);
}

// One recording, as 0x11978D0 runs it: per slot whose current camera locks, the projection, the G-buffer set (its
// local handles marked with the slot), the tail (the post's block system+0x280 marked with the slot, as 0x119872A
// writes system+0x2A0), the submit.
void Record(bool full=true) {
    events.clear();
    for(unsigned i=0;i<4;++i) {
        unsigned char* s=SlotAt(i);
        auto object=At<unsigned char*>(s,8);auto control=At<Control*>(s,0x10);
        if(!object || !control || control->uses<=0)continue;
        recordingSlot=static_cast<int>(i);
        ++control->uses;
        ProjectHook(object,reinterpret_cast<const int*>(s+0x34));
        alignas(16) unsigned char handles[0x60];std::memset(handles,0x10+static_cast<int>(i),sizeof(handles));
        GbufferHook(commandContext+0x2AA0,commandContext,handles,nullptr);
        std::memset(handles,0xEE,sizeof(handles));   // the locals die with the slot's iteration
        void* colour=s+0xF0;
        if(full){std::memset(sys+0x280,0x40+static_cast<int>(i),0x48);TailHook<post>(commandContext+0x2AA0,commandContext,colour,sys+0x280);}
        else TailHook<plain>(commandContext+0x2AA0,commandContext,colour,nullptr);
        TailHook<copy>(nullptr,commandContext,colour,commandContext+0x2AA0);
        if(full && s[0xC8])TailHook<history>(commandContext+0x2AA0,commandContext,colour,s+0x110);
        if(At<int>(s,0)==3)TailHook<modeTail>(commandContext+0x2AA0,commandContext,s+0x110,nullptr);
        --control->uses;
    }
    recordingSlot=-1;
    SubmitHook(commandContext);
}
// The recorded events as a compact string: P<slot> projection, G<slot> G-buffer, T<slot><tail> tail, S submit.
std::string Trace() {
    std::string out;
    for(const Event& e:events) {
        const char slot=e.slot<0 ? '-' : static_cast<char>('0'+e.slot);
        switch(e.kind) {
        case Kind::project: out+="P";out+=slot;break;
        case Kind::gbuffer: out+="G";out+=slot;break;
        case Kind::tail: out+="T";out+=slot;out+=static_cast<char>('a'+e.tail);break;
        case Kind::submit: out+="S";break;
        }
        out+=' ';
    }
    return out;
}
const Event* Find(Kind kind,int nth=0){for(const Event& e:events)if(e.kind==kind && nth--==0)return &e;return nullptr;}

void Main(int w,int h) {
    std::memset(sys,0,sizeof(sys));
    SetSlotCamera(0,cam0,&control0);
    const int rect[4]={0,0,w,h};std::memcpy(SlotAt(0)+0x34,rect,16);Put<int>(SlotAt(0),0x44,w);Put<int>(SlotAt(0),0x48,h);
    Put<std::uint64_t>(SlotAt(0),0x20,1);
    for(int k=0;k<16;++k)Put<float>(cam0,0x220+4*k,static_cast<float>(k+1));
    Put<float>(cam0,0x20,0.7f);Put<float>(cam0,0x24,scopepip::kBaseFov);Put<float>(cam0,0x28,0.25f);Put<float>(cam0,0x2C,4000.0f);
}
}  // namespace

int wmain() {
    Wire();
    Main(1920,1080);
    const std::string passThrough="P0 G0 T0a T0c S ";

    // Nothing wanted: the recording as it was.
    Record();
    Check(Trace()==passThrough && factories==0,"nothing wanted: the recording passes through, no camera made");

    // Wanted: the camera made (following viewport 0's), viewport 3 S x S; not armed until the engine takes it in.
    Want(cam0,kScopeFov);
    Record();
    const int lensSide=scopepip::LensViewSide(scopepip::LensRadius(1920,1080));
    Check(factories==1 && followed==cam0 && control0.weaks==2,"wanted: one lens camera, following viewport 0's camera");
    Check(At<int>(SlotAt(3),0x3C)==lensSide && At<int>(SlotAt(3),0x40)==lensSide && At<int>(SlotAt(3),0x34)==0,"viewport 3 set to S x S");
    Check(Trace()==passThrough && !CurrentOf(SlotAt(3)),"...not armed before it joins: the recording as it was");
    Record();
    Check(factories==1 && Trace()==passThrough,"a camera in the queue is not made twice");

    // Joined: armed.
    Join();
    Record();
    Check(Trace()=="P0 G0 P3 G3 G- T-a T-c S ","armed: viewport 0's tail held after viewport 3's world, its G-buffer set again first, viewport 3's tail never recorded");
    Check(Side()==lensSide,"the lens view's side is published");
    {
        const Event* lensProject=Find(Kind::project,1);
        Check(lensProject && lensProject->c==lensCam,"viewport 3 renders the lens camera");
        Check(std::memcmp(lensCam+0x220,cam0+0x220,64)==0 && At<float>(lensCam,0x20)==0.7f && At<float>(lensCam,0x28)==0.25f &&
              At<float>(lensCam,0x2C)==4000.0f,"the lens camera is aimed as viewport 0's camera (its matrix, +0x20, near, far)");
        const float r=scopepip::LensRadius(1920,1080);
        Check(lensProject && std::fabs(lensProject->fov-scopepip::LensViewFov(1080,r,kScopeFov))<1e-6f,"...at the lens's field");
        const Event* replayG=Find(Kind::gbuffer,2);
        bool slot0Handles=replayG!=nullptr;
        for(int k=0;replayG && k<0x60;++k)slot0Handles=slot0Handles && replayG->handles[k]==0x10;
        Check(slot0Handles,"the G-buffer set again with viewport 0's handles (copied before its locals died)");
        Check(handlesMade==3 && handlesAssigned==3 && handlesFreed==3,"the handle copies made, assigned and freed with the engine's own functions");
        const Event* replayPost=Find(Kind::tail,0);
        bool slot0Block=replayPost!=nullptr;
        for(int k=0;replayPost && k<0x48;++k)slot0Block=slot0Block && replayPost->block[k]==0x40;
        Check(slot0Block,"the post's block is viewport 0's, as it was when its call came (viewport 3's iteration rewrote system+0x280)");
        Check(replayPost && replayPost->c==SlotAt(0)+0xF0,"...on viewport 0's colour");
    }
    Check(allocated==1 && committed==1,"one lens command in the ring");
    {
        using CommandFn=void(__fastcall*)(void*,void*);
        CommandFn fn=nullptr;std::memcpy(&fn,ring,sizeof(fn));
        alignas(16) unsigned char native[0x10]{};
        auto* const context=reinterpret_cast<ID3D11DeviceContext*>(static_cast<std::uintptr_t>(0x1234560));
        Put<ID3D11DeviceContext*>(native,8,context);
        fn(native,nullptr);
        Check(fn==&LensCommand && lensCommands==1 && commanded==context,"the lens command calls InfantryScopeLensCommand with the device context");
    }
    Check(!CurrentOf(SlotAt(3)),"the lens camera is taken off viewport 3 at the submit (no HUD stage, no fourth player)");

    // The simple post path and the history copy are held as well.
    SlotAt(0)[0xC8]=1;
    Record();
    Check(Trace()=="P0 G0 P3 G3 G- T-a T-c T-d S ","the history copy is held with the rest");
    SlotAt(0)[0xC8]=0;
    Record(false);
    Check(Trace()=="P0 G0 P3 G3 G- T-b T-c S ","the simple post path is held with the rest");
    Check(allocated==3,"one lens command per armed recording");

    // A stock-zoomed frame, another camera, a stale want: not armed.
    Put<float>(cam0,0x24,kScopeFov);
    Record();
    Check(Trace()==passThrough && !CurrentOf(SlotAt(3)),"a frame the stock zoom renders: no lens view");
    Put<float>(cam0,0x24,scopepip::kBaseFov);
    Want(otherCam,kScopeFov);
    Record();
    Check(Trace()==passThrough,"another camera wanted: no lens view");
    Want(cam0,kScopeFov);wantAt.store(GetTickCount64()-10*kFreshMs);
    Record();
    Check(Trace()==passThrough,"a stale want: no lens view");
    Want(cam0,kScopeFov);
    Record();
    Check(Trace()=="P0 G0 P3 G3 G- T-a T-c S ","wanted again: armed again");

    // A resize: viewport 3 set to the new side, armed again the next recording.
    Put<int>(SlotAt(0),0x44,1280);Put<int>(SlotAt(0),0x48,720);
    const int sets=viewportSets;
    Record();
    const int smaller=scopepip::LensViewSide(scopepip::LensRadius(1280,720));
    Check(viewportSets==sets+1 && At<int>(SlotAt(3),0x3C)==smaller && Trace()==passThrough,"a resize: viewport 3 resized, no lens that recording");
    Record();
    Check(Trace()=="P0 G0 P3 G3 G- T-a T-c S " && Side()==smaller,"...then armed at the new side");

    // The mission's cameras gone: the weak reference dropped, the camera made again.
    Teardown();
    Record();
    Check(factories==2 && lensControl.uses==1,"the mission's cameras gone: the lens camera made again");
    Join();
    Record();
    Check(Trace()=="P0 G0 P3 G3 G- T-a T-c S ","...and armed once it joined");

    // Viewport 0 not rendered (its camera did not lock) while the queue made the lens camera current: viewport 3's tail
    // is still never recorded, no command, taken off at the submit.
    {
        Control* saved=At<Control*>(SlotAt(0),0x10);
        control0.uses=0;
        alignas(16) void* w[2]={lensCam,&lensControl};++lensControl.weaks;SetCamera(SlotAt(3),w);
        const int before=allocated;
        Record();
        Check(Trace()=="P3 G3 S " && allocated==before && !CurrentOf(SlotAt(3)),"an unarmed lens view never reaches the screen");
        control0.uses=1;(void)saved;
    }

    // The mission's reset: the weak reference let go (the control block deleted with its last weak).
    Teardown();
    Reset();
    Check(!lens.object && lensControl.weaks==0 && lensControl.deleted==1,"the reset lets the lens camera's control block go");

    // Refusals: a split screen, viewport 3 in use, a camera that never joins. Logged once, until the reset.
    {
        Want(cam0,kScopeFov);
        SetSlotCamera(1,otherCam,&otherControl);
        const int before=logs;
        Record();Record();
        Check(Refused() && logs==before+1 && Trace()=="P0 G0 T0a T0c P1 G1 T1a T1c S " && factories==2,"a split screen: refused once, no camera made, both players' views as they were");
        SetSlotCamera(1,nullptr,nullptr);
        Record();
        Check(Refused() && Trace()==passThrough,"...until the mission's reset");
        Reset();
        Check(!Refused(),"the reset forgets the refusal");
        Want(cam0,kScopeFov);

        Put<std::uint64_t>(SlotAt(3),0x20,1);
        Record();
        Check(Refused() && factories==2,"viewport 3 in use: refused");
        Put<std::uint64_t>(SlotAt(3),0x20,0);
        Reset();

        Want(cam0,kScopeFov);
        Record();
        Check(factories==3 && !Refused(),"made again after the reset");
        createdAt=GetTickCount64()-2*kJoinMs;
        Record();
        Check(Refused(),"a camera that never joins viewport 3: refused");
        Reset();
    }

    std::printf("scope_lens: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
