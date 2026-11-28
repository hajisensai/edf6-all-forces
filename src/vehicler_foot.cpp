// The Vehicler on foot (src/vehicler_foot.h; what and why: src/vehicler_foot_logic.h).
//  - The dash: AssultSoldier's live vtable slot 5 chained (edf::ChainVtableSlot). The new classes' clones copy every slot they do
//    not own from the live table before each object is made (soldier_factory.cpp Inherit), so the hook is theirs too. Only a
//    Vehicler's frame (its vptr the Vehicler clone's, soldier::zwVtables[0]: an inline compare, no query, as every Ranger and
//    Ranger NPC passes here each frame) sees its dash button cleared; the byte is put back after the frame, so anything reading
//    it later (the plugin's own input, EDF6Coop's sync) sees what the player pressed. It runs wherever the soldier's frame runs
//    (the owner and every remote copy), so no machine starts the dash.
//  - The falloff gate: its 17 bytes (0x235E7B) a jump to BuildCave's code (edf::RedirectToCode, Branch::kJump).
#include "vehicler_foot.h"
#include "vehicler_foot_logic.h"
#include "soldier_factory.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "memory.h"
#include "edf/patch.h"
#include <cstdint>
#include <initializer_list>

namespace crew {
namespace {
namespace vf=vehicler_foot;
using FrameFn=void(__fastcall*)(void*,void*,void*,void*);
void* nextFrame=nullptr;   // set before the slot is written (ChainVtableSlot)

void __fastcall FrameHook(void* self,void* a,void* b,void* c) noexcept {
    const auto next=reinterpret_cast<FrameFn>(nextFrame);
    const void* vt=soldier::zwVtables[0];
    if(!vt || !self || *static_cast<const void* const*>(self)!=vt) { next(self,a,b,c); return; }
    auto* button=static_cast<unsigned char*>(self)+vf::kDashButton;
    const unsigned char held=*button;
    *button=0;
    next(self,a,b,c);
    *button=held;
}

bool PrintMatches(std::size_t rva) noexcept {
    const vf::Print* p=vf::Find(rva);
    unsigned char bytes[vf::kMostPrintBytes];
    const std::size_t n=p ? vf::Decode(p->hex,bytes,sizeof(bytes)) : 0;
    if(n && Readable(image+rva,n) && Matches(rva,bytes,n))return true;
    Log("VEHICLER the code at EDF.dll+0x%zX (%s) is not what this was written for",rva,p ? p->what : "unlisted");
    return false;
}

bool HookDash() noexcept {
    if(!PrintMatches(0x550C30) || !PrintMatches(0x550C41) || !PrintMatches(0x550C5F) || !PrintMatches(0x570855))return false;
    auto** slot=reinterpret_cast<void**>(image+vf::kSoldierVtable)+vf::kFrameSlot;
    if(*slot!=image+vf::kStockFrame)Log("VEHICLER AssultSoldier frame (slot 5): chaining onto %p",*slot);
    return edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&FrameHook),&nextFrame);
}

bool PatchGate() noexcept {
    for(const std::size_t rva:{std::size_t{0x235E7B},std::size_t{0x235E8C},std::size_t{0x235E9C},std::size_t{0x179E1D8},
                               std::size_t{0x231080}})
        if(!PrintMatches(rva))return false;
    const vf::Print* site=vf::Find(vf::kGate);
    unsigned char gate[vf::kGateSize];
    if(!site || vf::Decode(site->hex,gate,sizeof(gate))!=vf::kGateSize)return false;
    unsigned char cave[vf::kCaveSize];
    vf::BuildCave(cave,reinterpret_cast<std::uint64_t>(image+vf::kSkip),reinterpret_cast<std::uint64_t>(image+vf::kRefresh));
    return edf::RedirectToCode(image+vf::kGate,gate,vf::kGateSize,cave,sizeof(cave),edf::Branch::kJump)!=nullptr;
}
}  // namespace

bool InstallVehiclerFoot() noexcept {
    __try {
        const bool dash=HookDash();
        const bool gate=PatchGate();
        Log("HOOK vehicler foot: dash held back=%d, damage growth gate=%d (AmmoDamageReduce a > 1)",dash,gate);
        return dash && gate;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
