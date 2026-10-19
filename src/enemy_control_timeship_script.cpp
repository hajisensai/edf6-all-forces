#include "enemy_control_timeship_script.h"
#include "enemy_control_action_native.h"
#include "enemy_control.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
#include <iterator>

namespace crew::enemy_native {
namespace timeship_script {
using WarpFn=void(__fastcall*)(void*,const void*,int);
using TargetFn=void(__fastcall*)(void*,const void*);
using VoidFn=void(__fastcall*)(void*);
bool Allowed(const void* root) noexcept {
    // A missing role is not offline. Mission AS may still address a body whose
    // slot7 AI is suspended; guest script execution must not create a second FX.
    bool authority=false;
    if(NativeActionRole(root,&authority))return authority && !NativeActionQuiescing(root);
    bool online=false;
    if(EnemyControlNetworkContext(root,&online))return !online;
    return !EnemyControlTargetActive(root); // autonomous/unbound vanilla only
}
void __fastcall Warp(void* root,const void* matrix,int frames){if(Allowed(root))reinterpret_cast<WarpFn>(image+0x3F3040)(root,matrix,frames);}
void __fastcall Warp2(void* root,const void* matrix,int frames){if(Allowed(root))reinterpret_cast<WarpFn>(image+0x3F3160)(root,matrix,frames);}
void __fastcall Anchor(void* root,const void* point){if(Allowed(root))reinterpret_cast<TargetFn>(image+0x3ECA80)(root,point);}
void __fastcall AnchorBelow(void* root){if(Allowed(root))reinterpret_cast<VoidFn>(image+0x3ECAB0)(root);}
void __fastcall Vanish(void* root){if(Allowed(root))reinterpret_cast<VoidFn>(image+0x3F3280)(root);}
void __fastcall Vanish2(void* root){if(Allowed(root))reinterpret_cast<VoidFn>(image+0x3F3290)(root);}
struct Site {
    unsigned rva,target;void* replacement;unsigned char original[5]{},patched[5]{};void* thunk=nullptr;bool changed=false;
};
Site sites[]={
    {0x1CB74E,0x3F3040,reinterpret_cast<void*>(&Warp)},
    {0x1CB67E,0x3F3160,reinterpret_cast<void*>(&Warp2)},
    {0x1CB397,0x3ECA80,reinterpret_cast<void*>(&Anchor)},
    {0x1CB2A7,0x3ECAB0,reinterpret_cast<void*>(&AnchorBelow)},
    {0x1CB857,0x3F3280,reinterpret_cast<void*>(&Vanish)},
    {0x1CB7D7,0x3F3290,reinterpret_cast<void*>(&Vanish2)}
};
unsigned char* installedImage=nullptr;
bool Restore() noexcept {
    if(!installedImage)return true;
    bool ok=true;
    for(auto& site:sites){
        if(site.changed){
            const bool restored=edf::PatchCode(installedImage+site.rva,site.patched,site.original,5);
            if(restored)site.changed=false;else ok=false;
        }
        if(!site.changed && site.thunk){VirtualFree(site.thunk,0,MEM_RELEASE);site.thunk=nullptr;}
    }
    if(ok)installedImage=nullptr;return ok;
}
}
bool InstallTimeShipScriptGates() noexcept {
    using namespace timeship_script;
    __try {
        if(!image)return false;
        if(installedImage){
            if(installedImage!=image)return false;
            bool complete=true;for(const auto& site:sites)if(!site.changed || !Readable(image+site.rva,5) || std::memcmp(image+site.rva,site.patched,5))complete=false;
            if(complete)return true;if(!Restore())return false;
        }
        // Validate the whole set before allocating or changing any instruction.
        for(auto& site:sites){
            const auto* code=image+site.rva;if(!Readable(code,5) || code[0]!=0xE8)return false;
            std::int32_t relative=0;std::memcpy(&relative,code+1,4);
            if(code+5+relative!=image+site.target)return false;
            std::memcpy(site.original,code,5);std::memcpy(site.patched,code,5);
        }
        installedImage=image;
        for(auto& site:sites){
            site.thunk=AllocateNearThunk(image+site.rva,site.replacement);if(!site.thunk){Restore();return false;}
            const auto delta=static_cast<unsigned char*>(site.thunk)-(image+site.rva+5);
            if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0){Restore();return false;}
            const auto relative=static_cast<std::int32_t>(delta);std::memcpy(site.patched+1,&relative,4);
        }
        for(auto& site:sites){
            const bool ok=edf::PatchCode(image+site.rva,site.original,site.patched,5);
            site.changed=Readable(image+site.rva,5) && std::memcmp(image+site.rva,site.patched,5)==0;
            if(!ok || !site.changed){Restore();return false;}
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){timeship_script::Restore();return false;}
}
bool RestoreTimeShipScriptGates() noexcept {
    __try{return timeship_script::Restore();}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
