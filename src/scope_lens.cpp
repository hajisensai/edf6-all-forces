// The picture-in-picture scope's lens view (src/scope_lens.h; docs/scope-pip-re.md §4-§5).
#include "crew.h"
#include "scope_lens.h"
#include "scope_pip.h"
#include "memory.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew {
namespace scopelens {
namespace {
// xgs::game::System (*(EDF+0x20B2958)): 4 viewport slots from +0x4D0, kSlot bytes each (docs/scope-pip-re.md §4.1).
constexpr std::size_t kSystemRef=0x20B2958;
constexpr std::size_t kSlots=0x4D0,kSlot=0x188;
// Within a slot: +0 the mode, +8 / +0x10 the current camera (weak_ptr), +0x18 / +0x20 its cameras (std::list head,
// count), +0x28 its render targets built, +0x34 the rect {x, y, w, h}, +0x44 / +0x48 the target size, +0xC8 a second
// colour target (the post's history).
constexpr std::size_t kSlotMode=0x0,kSlotCurrent=0x8,kSlotList=0x18,kSlotCount=0x20,kSlotBuilt=0x28,kSlotRect=0x34,
                      kSlotWidth=0x44,kSlotHeight=0x48,kSlotHistory=0xC8;
// The camera (SceneCamera): +0x20 / +0x24 its two FOVs (+0x24 the vertical one the projection reads), +0x28 / +0x2C near
// and far, +0x220 its world matrix (16 floats).
constexpr std::size_t kCamFovBase=0x20,kCamFov=0x24,kCamNear=0x28,kCamFar=0x2C,kCamWorld=0x220;
// MSVC's _Ref_count_base: +8 uses, +0xC weaks; vtable [0] _Destroy, [1] _Delete_this.
constexpr std::size_t kUses=0x8,kWeaks=0xC;
// The renderer's command context (0x2136FF0) and its recording ring (+0x28A8).
constexpr std::size_t kCommands=0x2136FF0,kRing=0x28A8;
// TraceCamera's creation description: its vtable (as stereo_camera 0xACD16 writes it), the followed camera's weak_ptr.
constexpr std::size_t kDescVtable=0x1762178,kDescFollow=0x80,kDescSize=0xA0;
constexpr ULONGLONG kFreshMs=200;    // a Want older than this: none (the camera step stopped running)
constexpr ULONGLONG kJoinMs=3000;    // a lens camera not in viewport 3's list this long after its creation: refused
using scopepip::kLensViewport;

// The recording's calls (docs/scope-pip-re.md §4.3, §5.1): the call site, its callee.
constexpr unsigned kProjectCall=0x1197B50,kProject=0x118DFF0;
constexpr unsigned kGbufferCall=0x119863C,kGbuffer=0x112B230;
constexpr unsigned kPostCall=0x1198742,kPost=0x112C230;            // the full post stage and composite (dl set)
constexpr unsigned kPlainCall=0x1198753,kPlain=0x112C460;          // the simple one (dl clear)
constexpr unsigned kCopyCall=0x1198777,kCopy=0x11B6E20;
constexpr unsigned kHistoryCall=0x119879F,kHistory=0x112C5A0;
constexpr unsigned kModeCall=0x11987BF,kModeTail=0x112A7F0;
constexpr unsigned kSubmitCall=0x119889B,kSubmit=0x111B410;
// Callees the plugin calls itself.
constexpr unsigned kSetViewport=0x1198C40,kSetCamera=0x1193FA0,kFactory=0x82F60,kAlloc=0x51D00,kCommit=0x51D60;
constexpr unsigned kHandleNew=0x1120500,kHandleAssign=0x8F000,kHandleFree=0x11207A0;
constexpr std::size_t kHandle=0x20,kHandles=3;          // 0x112B230's argument: three 0x20-byte texture handles
constexpr std::size_t kPostBlock=0x48;                  // 0x112C230's r9: system+0x280, copied whole when recorded

struct Guard { unsigned rva; const unsigned char* bytes; std::size_t size; const char* what; };
// lea rdx,[rdi+504h]; add rdx,r14; mov rcx,rsi; call 0x118DFF0 (the slot's rect, the slot's locked camera)
const unsigned char kProjectCode[]={0x48,0x8D,0x97,0x04,0x05,0x00,0x00,0x49,0x03,0xD6,0x48,0x8B,0xCE,0xE8,0x9B,0x64,0xFF,0xFF};
// lea r8,[rbp-60h]; lea rdx,[0x2136FF0]; lea rcx,[0x2139A90]; call 0x112B230
const unsigned char kGbufferCode[]={0x4C,0x8D,0x45,0xA0,0x48,0x8D,0x15,0xBB,0xE9,0xF9,0x00,0x48,0x8D,0x0D,0x54,0x14,0xFA,0x00,
                                    0xE8,0xEF,0x2B,0xF9,0xFF};
// mov r8,rsi; lea rcx,[0x2139A90]; call 0x112C230
const unsigned char kPostCode[]={0x4C,0x8B,0xC6,0x48,0x8D,0x0D,0x4E,0x13,0xFA,0x00,0xE8,0xE9,0x3A,0xF9,0xFF};
// mov r8,r14; lea rcx,[0x2139A90]; call 0x112C460
const unsigned char kPlainCode[]={0x4D,0x8B,0xC6,0x48,0x8D,0x0D,0x3D,0x13,0xFA,0x00,0xE8,0x08,0x3D,0xF9,0xFF};
// lea r9,[0x2139A90]; mov r8,rsi; lea rdx,[0x2136FF0]; mov rcx,[0x20B2FE8]; call 0x11B6E20
const unsigned char kCopyCode[]={0x4C,0x8D,0x0D,0x2A,0x13,0xFA,0x00,0x4C,0x8B,0xC6,0x48,0x8D,0x15,0x80,0xE8,0xF9,0x00,0x48,0x8B,
                                 0x0D,0x71,0xA8,0xF1,0x00,0xE8,0xA4,0xE6,0x01,0x00};
// mov r9,r14; mov r8,rsi; lea rdx,[0x2136FF0]; lea rcx,[0x2139A90]; call 0x112C5A0
const unsigned char kHistoryCode[]={0x4D,0x8B,0xCE,0x4C,0x8B,0xC6,0x48,0x8D,0x15,0x58,0xE8,0xF9,0x00,0x48,0x8D,0x0D,0xF1,0x12,
                                    0xFA,0x00,0xE8,0xFC,0x3D,0xF9,0xFF};
// mov r8,r14; lea rdx,[0x2136FF0]; lea rcx,[0x2139A90]; call 0x112A7F0
const unsigned char kModeCode[]={0x4D,0x8B,0xC6,0x48,0x8D,0x15,0x38,0xE8,0xF9,0x00,0x48,0x8D,0x0D,0xD1,0x12,0xFA,0x00,0xE8,0x2C,
                                 0x20,0xF9,0xFF};
// lea rcx,[0x2136FF0]; call 0x111B410 (after the slot loop's cmp r12d,4 / jne)
const unsigned char kSubmitCode[]={0x48,0x8D,0x0D,0x55,0xE7,0xF9,0x00,0xE8,0x70,0x2B,0xF8,0xFF};
const unsigned char kSetViewportCode[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x41,0x0F,0x10,0x00,0x45,0x8B,0x48,0x0C,0x48,0x8B,0xD9,0x45,
                                        0x8B,0x50,0x08,0x48,0x81,0xC1};
const unsigned char kSetCameraCode[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x57,0x41,0x56,
                                      0x41,0x57,0x48,0x83,0xEC,0x30};
const unsigned char kFactoryCode[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x41,0x56,0x48,
                                    0x83,0xEC,0x40,0x4C,0x8B,0xF2};
// The factory's new 0x360 (a TraceCamera), its constructor 0x1051F0, its AddCamera 0x1195990.
const unsigned char kFactoryNewCode[]={0xB9,0x60,0x03,0x00,0x00,0xE8,0xCC,0x55,0x25,0x01};
const unsigned char kFactoryCtorCode[]={0x48,0x8B,0xD3,0x48,0x8B,0xC8,0xE8,0xF5,0x21,0x08,0x00};
const unsigned char kFactoryAddCode[]={0x48,0x8D,0x54,0x24,0x30,0x48,0x8B,0xCF,0xE8,0x78,0x29,0x11,0x01};
// TraceCamera's constructor copies desc+0x80 / +0x88 (the followed camera's weak_ptr, weaks +1) to +0x350 / +0x358.
const unsigned char kFollowCode[]={0x48,0x8B,0x87,0x88,0x00,0x00,0x00,0x48,0x85,0xC0,0x74,0x0E,0x48,0x8B,0x8F,0x80,0x00,0x00,
                                   0x00,0xF0,0xFF,0x40,0x0C,0x48,0x8B,0xD0,0x48,0x89,0x8B,0x50,0x03,0x00,0x00};
// stereo_camera's description vtable: lea rax,[0x1762178]
const unsigned char kDescVtableCode[]={0x48,0x8D,0x05,0x5B,0x54,0x6B,0x01};
const unsigned char kAllocCode[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0x41,0x48,0x8B,0xFA,0x48};
const unsigned char kCommitCode[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x01,0x51,0x50,0x33,0xFF,0x8B};
// The render thread's dispatcher: fn = one unit read; if(!fn) done; fn(context+0x140, reader).
const unsigned char kDispatchCode[]={0xE8,0x52,0x65,0xF3,0xFE,0xBA,0x01,0x00,0x00,0x00,0x48,0x8B,0xCB,0x48,0x8B,0x38,0xE8,0xB2,
                                     0x65,0xF3,0xFE,0x48,0x85,0xFF,0x74,0x2A,0x48,0x8B,0xD3,0x48,0x8B,0xCE,0xFF,0xD7};
const unsigned char kHandleNewCode[]={0x48,0x8D,0x05,0x11,0xE8,0x67,0x00,0x48,0x89,0x01,0x33,0xC0,0x48,0x89,0x41,0x10};
const unsigned char kHandleAssignCode[]={0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,0x48,0x8B,0xDA};
const unsigned char kHandleFreeCode[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8D,0x05};
const Guard kGuards[]={
    {kProjectCall-13,kProjectCode,sizeof(kProjectCode),"projection call"},
    {kGbufferCall-18,kGbufferCode,sizeof(kGbufferCode),"G-buffer call"},
    {kPostCall-10,kPostCode,sizeof(kPostCode),"post call"},
    {kPlainCall-10,kPlainCode,sizeof(kPlainCode),"plain post call"},
    {kCopyCall-24,kCopyCode,sizeof(kCopyCode),"copy call"},
    {kHistoryCall-20,kHistoryCode,sizeof(kHistoryCode),"history call"},
    {kModeCall-17,kModeCode,sizeof(kModeCode),"mode 3 call"},
    {kSubmitCall-7,kSubmitCode,sizeof(kSubmitCode),"submit call"},
    {kSetViewport,kSetViewportCode,sizeof(kSetViewportCode),"set viewport"},
    {kSetCamera,kSetCameraCode,sizeof(kSetCameraCode),"set camera"},
    {kFactory,kFactoryCode,sizeof(kFactoryCode),"camera factory"},
    {0x82FDA,kFactoryNewCode,sizeof(kFactoryNewCode),"factory new"},
    {0x82FF0,kFactoryCtorCode,sizeof(kFactoryCtorCode),"factory constructor"},
    {0x8300B,kFactoryAddCode,sizeof(kFactoryAddCode),"factory add"},
    {0x10522C,kFollowCode,sizeof(kFollowCode),"trace camera follow"},
    {0xACD16,kDescVtableCode,sizeof(kDescVtableCode),"description vtable"},
    {kAlloc,kAllocCode,sizeof(kAllocCode),"ring alloc"},
    {kCommit,kCommitCode,sizeof(kCommitCode),"ring commit"},
    {0x111B939,kDispatchCode,sizeof(kDispatchCode),"dispatcher"},
    {kHandleNew,kHandleNewCode,sizeof(kHandleNewCode),"handle new"},
    {kHandleAssign,kHandleAssignCode,sizeof(kHandleAssignCode),"handle assign"},
    {kHandleFree,kHandleFreeCode,sizeof(kHandleFreeCode),"handle free"},
};

using ProjectFn=void(__fastcall*)(void* camera,const int* rect);
using TailFn=void(__fastcall*)(void*,void*,void*,void*);
using SubmitFn=void(__fastcall*)(void* commands);
using SetViewportFn=void(__fastcall*)(void* system,unsigned slot,const int* rect);
using SetCameraFn=void(__fastcall*)(void* slot,void* weak);
using FactoryFn=void(__fastcall*)(void* system,void* out,unsigned slot,const float* world,void* desc);
using AllocFn=void*(__fastcall*)(void* ring,int units);
using CommitFn=void(__fastcall*)(void* ring,int units);
using HandleFn=void*(__fastcall*)(void* handle);
using AssignFn=void*(__fastcall*)(void* handle,const void* from);
using ControlFn=void(__fastcall*)(void* control);

// The native entries (set by Install; tests set stand-ins).
unsigned char* const* systemRef=nullptr;
void* commands=nullptr;
ProjectFn project=nullptr;
TailFn gbuffer=nullptr;
enum Tail : unsigned { post,plain,copy,history,modeTail,kTails };
TailFn tails[kTails]{};
SubmitFn submit=nullptr;
SetViewportFn setViewport=nullptr;
SetCameraFn setCamera=nullptr;
FactoryFn factory=nullptr;
AllocFn alloc=nullptr;
CommitFn commit=nullptr;
HandleFn handleNew=nullptr,handleFree=nullptr;
AssignFn handleAssign=nullptr;
const void* descVtable=nullptr;
std::atomic<bool> installed{false};

// Any thread.
std::atomic<const void*> wantCamera{nullptr};
std::atomic<float> wantFov{0.0f};
std::atomic<ULONGLONG> wantAt{0};
std::atomic<bool> refused{false};
std::atomic<int> side{0};

// Game thread only (the camera step, the recording, the mission's reset).
struct Weak { void* object; void* control; };
Weak lens{};                 // the lens camera: the plugin's weak_ptr (viewport 3's list owns it)
ULONGLONG createdAt=0;
bool readyLogged=false;
// This recording (0x11978D0): the slot being recorded, whether the lens is armed, viewport 0's camera and the lens's
// field, whether viewport 3 records the lens camera and the lens command went in.
int slotNow=-1;
bool armed=false,lensSlot=false,commandIn=false;
unsigned char* mainCamera=nullptr;
float lensFov=0.0f;
// Viewport 0's held tail, in recording order (the five calls run at most once each per slot).
struct Held { void* a; void* b; void* c; void* d; unsigned char block[kPostBlock]; Tail tail; };
constexpr int kMostHeld=8;
Held held[kMostHeld]{};
int heldCount=0;
// Viewport 0's G-buffer handles (0x112B230's argument), copied with the engine's own handle functions.
alignas(16) unsigned char gbufferCopy[kHandles*kHandle]{};
void* gbufferOwner=nullptr;
bool gbufferHeld=false;

unsigned char* System() noexcept {
    return systemRef ? *systemRef : nullptr;
}
unsigned char* Slot(unsigned char* system,unsigned i) noexcept { return system+kSlots+i*kSlot; }
// The slot whose rect `rect` is (0x1197B43: system+0x504+i*0x188), else -1.
int SlotOf(unsigned char* system,const int* rect) noexcept {
    if(!system || !rect)return -1;
    const auto offset=reinterpret_cast<const unsigned char*>(rect)-(system+kSlots+kSlotRect);
    if(offset<0 || offset%static_cast<std::ptrdiff_t>(kSlot)!=0)return -1;
    const auto i=offset/static_cast<std::ptrdiff_t>(kSlot);
    return i<4 ? static_cast<int>(i) : -1;
}
void* CurrentOf(unsigned char* slot) noexcept { return At<void*>(slot,kSlotCurrent); }
long Uses(void* control) noexcept { return control ? *reinterpret_cast<volatile long*>(static_cast<unsigned char*>(control)+kUses) : 0; }
void Drop(Weak& w) noexcept {
    if(w.control) {
        auto c=static_cast<unsigned char*>(w.control);
        if(InterlockedDecrement(reinterpret_cast<volatile long*>(c+kWeaks))==0)
            reinterpret_cast<ControlFn>(At<void* const*>(c,0)[1])(c);
    }
    w=Weak{};
}
// 0x1193FA0(slot, weak_ptr by value): the callee releases its argument, so it gets a copy of its own.
void SetCurrent(unsigned char* slot,const Weak& w) noexcept {
    alignas(16) Weak copy=w;
    if(copy.control)InterlockedIncrement(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(copy.control)+kWeaks));
    setCamera(slot,&copy);
}
void Refuse(const char* why) noexcept {
    if(!refused.exchange(true,std::memory_order_acq_rel))
        Log("INFANTRYSCOPE the lens view cannot be used (%s): the whole screen zooms (InfantryScope=1's picture) this mission",why);
}
bool LensCurrent(unsigned char* system) noexcept {
    return lens.object && CurrentOf(Slot(system,kLensViewport))==lens.object;
}
void TakeOff(unsigned char* system) noexcept {
    if(LensCurrent(system))SetCurrent(Slot(system,kLensViewport),Weak{});
}
bool RectIs(const unsigned char* slot,int s) noexcept {
    return At<int>(slot,kSlotRect)==0 && At<int>(slot,kSlotRect+4)==0 && At<int>(slot,kSlotRect+8)==s && At<int>(slot,kSlotRect+12)==s;
}

// Viewport 3's lens camera there and its targets built for S x S: true. Creates it (once a mission, while viewport 3 is
// free and the screen is not split) or resizes the viewport; false until the engine's next update took it in.
bool Ensure(unsigned char* system,int s,const Weak& follow) noexcept {
    if(lens.control && Uses(lens.control)==0)Drop(lens);   // the mission's cameras went (0x11956C0)
    unsigned char* const v=Slot(system,kLensViewport);
    const int rect[4]={0,0,s,s};
    const ULONGLONG now=GetTickCount64();
    if(!lens.object) {
        for(unsigned i=1;i<kLensViewport;++i)if(CurrentOf(Slot(system,i))){Refuse("a split screen");return false;}
        if(At<std::uint64_t>(v,kSlotCount) || CurrentOf(v) || At<int>(v,kSlotMode)!=0){Refuse("viewport 3 is in use");return false;}
        if(!follow.object || !follow.control)return false;
        setViewport(system,kLensViewport,rect);
        alignas(16) float world[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        alignas(16) unsigned char desc[kDescSize]{};
        std::memcpy(desc,&descVtable,sizeof(descVtable));
        std::memcpy(desc+kDescFollow,&follow,sizeof(follow));   // the constructor takes its own weak reference
        Weak out{};
        factory(system,&out,kLensViewport,world,desc);
        if(!out.object){Drop(out);Refuse("the camera factory made none");return false;}
        lens=out;createdAt=now;readyLogged=false;
        Log("INFANTRYSCOPE lens view created: viewport %u, %dx%d",kLensViewport,s,s);
        return false;
    }
    const auto count=At<std::uint64_t>(v,kSlotCount);
    const auto head=At<unsigned char*>(v,kSlotList);
    const auto first=head ? At<unsigned char*>(head,0) : nullptr;
    const void* firstCamera=count && first ? At<void*>(first,0x10) : nullptr;
    if(count==0) {
        if(now-createdAt>kJoinMs)Refuse("the camera never joined viewport 3");
        return false;
    }
    if(count!=1 || firstCamera!=lens.object){Refuse("viewport 3 is in use");return false;}
    if(!RectIs(v,s)){setViewport(system,kLensViewport,rect);readyLogged=false;return false;}
    if(!At<void*>(v,kSlotBuilt))return false;
    if(!readyLogged) {
        readyLogged=true;
        Log("INFANTRYSCOPE lens view ready: viewport %u %dx%d, main %dx%d, taa=%d",kLensViewport,s,s,
            At<int>(Slot(system,0),kSlotWidth),At<int>(Slot(system,0),kSlotHeight),Slot(system,0)[kSlotHistory]);
    }
    return true;
}

// Viewport 0's projection: whether this recording renders the lens.
void ArmFor(unsigned char* system,unsigned char* camera) noexcept {
    mainCamera=camera;
    const ULONGLONG now=GetTickCount64();
    const float scopeFov=wantFov.load(std::memory_order_acquire);
    const bool want=installed.load(std::memory_order_acquire) && !refused.load(std::memory_order_acquire) &&
                    camera && wantCamera.load(std::memory_order_acquire)==camera &&
                    now-wantAt.load(std::memory_order_acquire)<=kFreshMs && scopeFov>0.0f;
    unsigned char* const main=Slot(system,0);
    const float w=static_cast<float>(At<int>(main,kSlotWidth)),h=static_cast<float>(At<int>(main,kSlotHeight));
    const float r=scopepip::LensRadius(w,h);
    const int s=scopepip::LensViewSide(r);
    const Weak follow{At<void*>(main,kSlotCurrent),At<void*>(main,kSlotCurrent+8)};
    // The frame wider than the scope (the camera step left it unmagnified): a lens to draw. The stock zoom's frame: none.
    const bool wide=scopepip::Magnification(scopepip::YScale(At<float>(camera,kCamFov)),scopeFov)>=scopepip::kLeast;
    if(!want || !(w>0.0f) || !(h>0.0f) || s<=0 || !wide || !Ensure(system,s,follow)){TakeOff(system);return;}
    lensFov=scopepip::LensViewFov(h,r,scopeFov);
    if(!(lensFov>0.0f)){TakeOff(system);return;}
    if(!LensCurrent(system))SetCurrent(Slot(system,kLensViewport),lens);
    armed=true;
    side.store(s,std::memory_order_release);
}
// Viewport 3's projection, the lens camera: viewport 0's camera's view, the lens's field.
void Aim(unsigned char* camera) noexcept {
    std::memcpy(camera+kCamWorld,mainCamera+kCamWorld,16*sizeof(float));
    Put<float>(camera,kCamFovBase,At<float>(mainCamera,kCamFovBase));
    Put<float>(camera,kCamNear,At<float>(mainCamera,kCamNear));
    Put<float>(camera,kCamFar,At<float>(mainCamera,kCamFar));
    Put<float>(camera,kCamFov,lensFov);
}

void Forget() noexcept {
    slotNow=-1;armed=false;lensSlot=false;commandIn=false;mainCamera=nullptr;heldCount=0;
}

void __fastcall LensCommand(void* native,void*) {
    __try {
        InfantryScopeLensCommand(native ? At<ID3D11DeviceContext*>(native,8) : nullptr);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void RecordLensCommand() noexcept {
    void* const ring=static_cast<unsigned char*>(commands)+kRing;
    void** unit=static_cast<void**>(alloc(ring,1));
    using CommandFn=void(__fastcall*)(void*,void*);
    const CommandFn fn=&LensCommand;
    std::memcpy(unit,&fn,sizeof(fn));
    commit(ring,1);
}

void __fastcall ProjectHook(void* camera,const int* rect) {
    __try {
        unsigned char* const system=System();
        const int i=SlotOf(system,rect);
        slotNow=i;
        if(i==0){Forget();slotNow=0;ArmFor(system,static_cast<unsigned char*>(camera));}
        else if(i==static_cast<int>(kLensViewport) && lens.object && camera==lens.object) {
            lensSlot=true;
            if(armed && mainCamera)Aim(static_cast<unsigned char*>(camera));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { armed=false; }
    project(camera,rect);
}

void __fastcall GbufferHook(void* a,void* b,void* c,void* d) {
    gbuffer(a,b,c,d);
    if(slotNow!=0 || !armed || gbufferHeld)return;
    __try {
        for(std::size_t k=0;k<kHandles;++k) {
            handleNew(gbufferCopy+k*kHandle);
            handleAssign(gbufferCopy+k*kHandle,static_cast<unsigned char*>(c)+k*kHandle);
        }
        gbufferOwner=a;gbufferHeld=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void OnTail(Tail t,void* a,void* b,void* c,void* d) noexcept {
    if(slotNow==static_cast<int>(kLensViewport) && lensSlot) {
        // The lens view never reaches the screen: its first tail call becomes the lens command (armed), the rest none.
        if(armed && !commandIn){commandIn=true;RecordLensCommand();}
        return;
    }
    if(slotNow==0 && armed && heldCount<kMostHeld) {
        Held& h=held[heldCount++];
        h.tail=t;h.a=a;h.b=b;h.c=c;h.d=d;
        if(t==post){std::memcpy(h.block,d,kPostBlock);h.d=h.block;}
        return;
    }
    tails[t](a,b,c,d);
}
template<Tail T> void __fastcall TailHook(void* a,void* b,void* c,void* d) {
    __try { OnTail(T,a,b,c,d); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void __fastcall SubmitHook(void* commandContext) {
    __try {
        if(gbufferHeld) {
            gbuffer(gbufferOwner,commandContext,gbufferCopy,nullptr);
            for(std::size_t k=0;k<kHandles;++k)handleFree(gbufferCopy+k*kHandle);
            gbufferHeld=false;gbufferOwner=nullptr;
        }
        for(int k=0;k<heldCount;++k){const Held& h=held[k];tails[h.tail](h.a,h.b,h.c,h.d);}
        heldCount=0;
        if(unsigned char* const system=System())TakeOff(system);
        Forget();
    } __except(EXCEPTION_EXECUTE_HANDLER) { heldCount=0;Forget(); }
    submit(commandContext);
}
}  // namespace

bool Install() noexcept {
    if(installed.load(std::memory_order_acquire))return true;
    for(const Guard& g:kGuards) {
        if(!Matches(g.rva,g.bytes,g.size)) {
            Log("INFANTRYSCOPE the lens view's %s (0x%X) is not where docs/scope-pip-re.md has it: no lens view",g.what,g.rva);
            return false;
        }
    }
    systemRef=reinterpret_cast<unsigned char* const*>(image+kSystemRef);
    commands=image+kCommands;
    project=reinterpret_cast<ProjectFn>(image+kProject);
    gbuffer=reinterpret_cast<TailFn>(image+kGbuffer);
    tails[post]=reinterpret_cast<TailFn>(image+kPost);
    tails[plain]=reinterpret_cast<TailFn>(image+kPlain);
    tails[copy]=reinterpret_cast<TailFn>(image+kCopy);
    tails[history]=reinterpret_cast<TailFn>(image+kHistory);
    tails[modeTail]=reinterpret_cast<TailFn>(image+kModeTail);
    submit=reinterpret_cast<SubmitFn>(image+kSubmit);
    setViewport=reinterpret_cast<SetViewportFn>(image+kSetViewport);
    setCamera=reinterpret_cast<SetCameraFn>(image+kSetCamera);
    factory=reinterpret_cast<FactoryFn>(image+kFactory);
    alloc=reinterpret_cast<AllocFn>(image+kAlloc);
    commit=reinterpret_cast<CommitFn>(image+kCommit);
    handleNew=reinterpret_cast<HandleFn>(image+kHandleNew);
    handleAssign=reinterpret_cast<AssignFn>(image+kHandleAssign);
    handleFree=reinterpret_cast<HandleFn>(image+kHandleFree);
    descVtable=image+kDescVtable;
    // Every redirect passes through until `installed`: one that fails leaves the others inert.
    bool changed=false;
    const bool ok=RedirectCall(image+kProjectCall,image+kProject,reinterpret_cast<void*>(&ProjectHook),changed) &&
        RedirectCall(image+kGbufferCall,image+kGbuffer,reinterpret_cast<void*>(&GbufferHook),changed) &&
        RedirectCall(image+kPostCall,image+kPost,reinterpret_cast<void*>(&TailHook<post>),changed) &&
        RedirectCall(image+kPlainCall,image+kPlain,reinterpret_cast<void*>(&TailHook<plain>),changed) &&
        RedirectCall(image+kCopyCall,image+kCopy,reinterpret_cast<void*>(&TailHook<copy>),changed) &&
        RedirectCall(image+kHistoryCall,image+kHistory,reinterpret_cast<void*>(&TailHook<history>),changed) &&
        RedirectCall(image+kModeCall,image+kModeTail,reinterpret_cast<void*>(&TailHook<modeTail>),changed) &&
        RedirectCall(image+kSubmitCall,image+kSubmit,reinterpret_cast<void*>(&SubmitHook),changed);
    if(!ok){Log("INFANTRYSCOPE the render recording's calls could not be redirected: no lens view");return false;}
    installed.store(true,std::memory_order_release);
    return true;
}
bool Installed() noexcept { return installed.load(std::memory_order_acquire); }

void Want(const void* camera,float scopeFov) noexcept {
    wantFov.store(scopeFov,std::memory_order_release);
    wantAt.store(GetTickCount64(),std::memory_order_release);
    wantCamera.store(camera,std::memory_order_release);
}
bool Refused() noexcept { return refused.load(std::memory_order_acquire); }
int Side() noexcept { return side.load(std::memory_order_acquire); }

void Reset() noexcept {
    __try { Drop(lens); } __except(EXCEPTION_EXECUTE_HANDLER) { lens=Weak{}; }
    readyLogged=false;createdAt=0;
    wantCamera.store(nullptr,std::memory_order_release);
    refused.store(false,std::memory_order_release);
    side.store(0,std::memory_order_release);
}
}  // namespace scopelens
}  // namespace crew
