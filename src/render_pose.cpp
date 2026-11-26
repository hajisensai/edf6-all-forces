// EDF.dll render-thread pose ownership. The recording sees 0x1100F10's copied
// world matrices, then seals them after the plugin's first-projection edits.
// Render queue markers select and release that exact recording's storage.
#include "render_pose.h"
#include "render_pose_frame.h"
#include "render_record.h"
#include "crew.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <array>
#include <cstring>

namespace crew::renderpose {
namespace {
constexpr unsigned kWrite=0x1100F10,kPalette=0x11009A0,kMesh=0x1103DA0;
constexpr unsigned kAlloc=0x51D00,kCommit=0x51D60,kCommands=0x2136FF0,kRing=0x28A8;
constexpr unsigned kBones=0x10,kCount=0x20,kPose=0x30,kPaletteHolder=0x50,kBoneStride=0x110,kInverseBind=0x30;
constexpr unsigned char kWriteSig[]={0x33,0xD2,0x4C,0x8B,0xC1,0x48,0x39,0x51,0x20,0x76,0x52};
constexpr unsigned char kPaletteSig[]={0xB8,0x78,0xC0,0x00,0x00,0xE8,0x56,0x7D,0x1D,0x00,0x48,0x2B,0xE0};
constexpr unsigned char kMeshSig[]={0x4C,0x89,0x4C,0x24,0x20,0x48,0x89,0x4C,0x24,0x08,0x55,0x53,0x57};
constexpr unsigned char kAllocSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0x41,0x48,0x8B,0xFA,0x48};
constexpr unsigned char kCommitSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x01,0x51,0x50,0x33,0xFF,0x8B};

struct PoseList {std::uintptr_t header;const Matrix* matrix;};
using WriteFn=void(__fastcall*)(const unsigned char*);
using PaletteFn=std::uintptr_t(__fastcall*)(const unsigned char*,void*,const PoseList*);
using MeshFn=std::uintptr_t(__fastcall*)(void*,void*,int,const unsigned char*,const PoseList*,unsigned char);
using QueueCallback=void(__fastcall*)(void*,void*);
using Alloc=void*(__fastcall*)(void*,int);
using Commit=void(__fastcall*)(void*,int);
WriteFn stockWrite=nullptr;
PaletteFn stockPalette=nullptr;
MeshFn stockMesh=nullptr;
Alloc allocate=nullptr;
Commit commit=nullptr;

constexpr std::size_t kFrames=3;
std::array<Frame,kFrames> frames;
thread_local Frame* capture=nullptr;
thread_local Frame* queued=nullptr;
thread_local Frame* reading=nullptr;

Skeleton Identify(const unsigned char* inst) noexcept {
    return {inst,edf::At<const void*>(inst,kBones),edf::At<const Matrix*>(inst,kPose),edf::At<std::uint64_t>(inst,kCount)};
}

void __fastcall Write(const unsigned char* inst) {
    stockWrite(inst);
    if(reading)reading->UseLive(inst);
    if(!capture)return;
    __try {capture->Observe(Identify(inst));}
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

bool Copy(const Skeleton& key,Matrix* destination) noexcept {
    __try {
        if(!(Identify(static_cast<const unsigned char*>(key.instance))==key))return false;
        std::memcpy(destination,key.live,key.count*sizeof(Matrix));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

ID3D11Buffer* NativePalette(const unsigned char* inst) noexcept {
    const auto holder=edf::At<const unsigned char*>(inst,kPaletteHolder);
    return holder ? edf::At<ID3D11Buffer*>(holder,0) : nullptr;
}

ID3D11Buffer* CapturePalette(const Skeleton& key,Matrix* inverse) noexcept {
    __try {
        const auto* inst=static_cast<const unsigned char*>(key.instance);
        if(!(Identify(inst)==key))return nullptr;
        auto* const buffer=NativePalette(inst);
        if(!buffer)return nullptr;
        D3D11_BUFFER_DESC desc{};
        buffer->GetDesc(&desc);
        if(desc.ByteWidth!=key.count*sizeof(PackedBone) || desc.Usage!=D3D11_USAGE_DEFAULT ||
           !(desc.BindFlags&D3D11_BIND_CONSTANT_BUFFER))return nullptr;
        const auto* bones=static_cast<const unsigned char*>(key.bones);
        for(std::size_t i=0;i<key.count;++i)std::memcpy(inverse+i,bones+i*kBoneStride+kInverseBind,sizeof(Matrix));
        // All fallible native reads precede the retained reference.
        buffer->AddRef();
        return buffer;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
}

bool Stage(const unsigned char* inst,const float* transform) noexcept {
    __try {return capture && capture->Stage(Identify(inst),transform,&Copy);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

const float* ReadFrozen(const unsigned char* inst) noexcept {
    __try {
        if(const auto* copy=reading->Lookup(Identify(inst)))return copy->elements;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

const PoseList* Select(const unsigned char* inst,const PoseList* source,PoseList& own) noexcept {
    if(!reading || !inst || !source)return source;
    __try {
        const Skeleton key=Identify(inst);
        // A native caller supplying custom matrices retains those matrices.
        if(source->matrix!=key.live)return source;
        if(const auto* override=OverrideOf(inst)) {
            // A null override explicitly requests its native in-place pose.
            if(!override->matrices)return source;
            own={source->header,reinterpret_cast<const Matrix*>(override->matrices)};
            return &own;
        }
        if(const auto* copy=reading->Lookup(key)) {
            own={source->header,copy};
            return &own;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return source;
}

bool UploadPalette(const unsigned char* inst,void* context,const PoseList& list) noexcept {
    __try {
        auto* const buffer=NativePalette(inst);
        const auto* packed=reading->Palette(Identify(inst),list.matrix,buffer);
        if(!packed)return false;
        auto* const d3d=context ? edf::At<ID3D11DeviceContext*>(context,8) : nullptr;
        if(!d3d)return false;
        // Same subresource and whole-buffer update as the native 0x113F860 path.
        d3d->UpdateSubresource(buffer,0,nullptr,packed,0,0);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

std::uintptr_t __fastcall Palette(const unsigned char* inst,void* context,const PoseList* source) {
    PoseList own{};
    const auto* list=Select(inst,source,own);
    if(list==&own && UploadPalette(inst,context,own))return 0;
    return stockPalette(inst,context,list);
}

std::uintptr_t __fastcall Mesh(void* lod,void* context,int pass,const unsigned char* inst,const PoseList* source,unsigned char flags) {
    PoseList own{};
    return stockMesh(lod,context,pass,inst,Select(inst,source,own),flags);
}

template<std::size_t I> void __fastcall Open(void*,void*) {
    if(frames[I].Open()) {
        reading=&frames[I];
        frozenPose=&ReadFrozen;
    }
}
template<std::size_t I> void __fastcall Retire(void*,void*) {
    if(reading==&frames[I]) {
        reading=nullptr;
        frozenPose=nullptr;
        frames[I].Retire();
    }
}

constexpr QueueCallback kOpen[]={&Open<0>,&Open<1>,&Open<2>};
constexpr QueueCallback kRetire[]={&Retire<0>,&Retire<1>,&Retire<2>};
static_assert(std::size(kOpen)==kFrames && std::size(kRetire)==kFrames);

void Enqueue(QueueCallback command) noexcept {
    // Identical one-unit command format to the native dispatcher and scope_lens.
    // Using a distinct callback per slot keeps the slot identity in the FIFO.
    void* const ring=image+kCommands+kRing;
    void* const unit=allocate(ring,1);
    std::memcpy(unit,&command,sizeof(command));
    commit(ring,1);
}

void Before() noexcept {
    for(auto& slot:frames)if(slot.Begin()) {
        capture=&slot;
        stagePose=&Stage;
        return;
    }
}

void Project(void*,const int*,bool first) noexcept {
    if(!first || !capture)return;
    Frame* frame=capture;
    stagePose=nullptr;
    capture=nullptr;
    if(!frame->Seal(&Copy,&CapturePalette))return;
    queued=frame;
    Enqueue(kOpen[frame-frames.data()]);
}

void After() noexcept {
    stagePose=nullptr;
    if(capture) {
        capture->Cancel();
        capture=nullptr;
    }
    if(queued) {
        Frame* const frame=queued;
        queued=nullptr;
        Enqueue(kRetire[frame-frames.data()]);
    }
}

struct Detour {
    unsigned rva;
    const unsigned char* original;
    std::size_t sigSize;
    void* replacement;
    void* page=nullptr;
    unsigned char jump[5]{0xE9};
};

void AbsoluteJump(unsigned char* out,const void* target) noexcept {
    const unsigned char indirect[]={0xFF,0x25,0,0,0,0};
    std::memcpy(out,indirect,sizeof(indirect));
    std::memcpy(out+sizeof(indirect),&target,sizeof(target));
}
bool Prepare(Detour& h) noexcept {
    // The first five bytes at each entry form complete, position-independent
    // instructions. The trampoline leaves RAX and the argument registers intact.
    unsigned char code[5+14+14]{};
    std::memcpy(code,h.original,5);
    AbsoluteJump(code+5,image+h.rva+5);
    AbsoluteJump(code+19,h.replacement);
    h.page=edf::AllocateNearCode(image+h.rva,code,sizeof(code));
    if(!h.page)return false;
    const auto from=reinterpret_cast<std::intptr_t>(image+h.rva+5);
    const auto to=reinterpret_cast<std::intptr_t>(h.page)+19;
    const auto relative=static_cast<std::int32_t>(to-from);
    std::memcpy(h.jump+1,&relative,sizeof(relative));
    return true;
}
}

bool Install() noexcept {
    const auto& cfg=Cfg();
    if(!cfg.enabled || !cfg.renderPoseSnapshots)return false;
    if(GetModuleHandleW(L"EDF6VR.dll"))return false;
    Detour hooks[]={{kWrite,kWriteSig,sizeof(kWriteSig),reinterpret_cast<void*>(&Write)},
                    {kPalette,kPaletteSig,sizeof(kPaletteSig),reinterpret_cast<void*>(&Palette)},
                    {kMesh,kMeshSig,sizeof(kMeshSig),reinterpret_cast<void*>(&Mesh)}};
    for(const auto& hook:hooks)if(!edf::Matches(image,hook.rva,hook.original,hook.sigSize))return false;
    if(!edf::Matches(image,kAlloc,kAllocSig,sizeof(kAllocSig)) ||
       !edf::Matches(image,kCommit,kCommitSig,sizeof(kCommitSig)) || !renderrecord::Install())return false;
    for(auto& hook:hooks)if(!Prepare(hook)) {
        for(const auto& allocated:hooks)if(allocated.page)VirtualFree(allocated.page,0,MEM_RELEASE);
        return false;
    }
    stockWrite=reinterpret_cast<WriteFn>(hooks[0].page);
    stockPalette=reinterpret_cast<PaletteFn>(hooks[1].page);
    stockMesh=reinterpret_cast<MeshFn>(hooks[2].page);
    allocate=reinterpret_cast<Alloc>(image+kAlloc);
    commit=reinterpret_cast<Commit>(image+kCommit);

    std::size_t installed=0;
    for(const auto& hook:hooks) {
        if(!edf::PatchCode(image+hook.rva,hook.original,hook.jump,sizeof(hook.jump))) {
            for(std::size_t i=0;i<std::size(hooks);++i) {
                auto& candidate=hooks[i];
                // Retain the original trampoline only if a live entry still needs it.
                if(i>=installed || edf::PatchCode(image+candidate.rva,candidate.jump,candidate.original,sizeof(candidate.jump)))
                    VirtualFree(candidate.page,0,MEM_RELEASE);
            }
            return false;
        }
        ++installed;
    }
    renderrecord::Set(renderrecord::Slot::kPoseSnapshot,&Before);
    renderrecord::SetProject(renderrecord::Slot::kPoseSnapshot,&Project);
    renderrecord::SetAfter(renderrecord::Slot::kPoseSnapshot,&After);
    return true;
}
}
