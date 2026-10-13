// EDF4.1's four-legged fortress (Alien4LegTank301) on EDF6's MovingFortress: what the class cannot be told through its
// SGO (tools/make_edf41_objects.py FORTRESS_*, jobs/4bf89026/tmp/fortress41/NOTES.md). Only an object whose SGO carries
// the key `edf41_fortress` is touched; EDF6's own fortresses (E510) go through unchanged.
//
//  - It stands from the start. The class spawns lying down (mode 0: the state `ground`, no AI) and stands up only on a
//    script command (0x1000000D sub 0 / 2) or a controller (EDF.dll 0x4C6293..0x4C634D, update 0x4C67E0); 4.1's fortress
//    has no such phase and 4.1's mission scripts send no such command. Each frame, before the class's update, the target
//    mode [+0x1A88] is set to 1 (what sub-command 0 does): the class then plays wakeup (4.1's stand) and runs its AI.
//  - The mission's drop goes to the core generator. 4.1's fortress has one ObjectGenerator, at the belly hatch over its
//    core (4.1 +0x1040). MovingFortress has two and picks one by a field of the message: 0x20100 (a drop setting) by the
//    int at +0x48 of its body (0x4C64AA, unchecked), 0x20103 (get the generator) by the int at +0 (0x4C6352, < 2). Only
//    generators[1] opens the core hatch (0x4CAA20 -> 0x4CA980 sets the core-open flag [+0xE68] the hit filter 0x4C6650
//    asks), so a drop on generators[0] would leave the core shut and the fortress unkillable. For a 4.1 fortress the index
//    is 1 while the class handles the message, then the body is put back as it was (the sender owns it).
//
// The key is read the way the class reads its own (classic SGO): 0x528A0(&object+0xA0, name) gives the node or null.
// Hooks: MovingFortress's vtable 0x17C6900, slot 4 (update 0x4C67E0) and slot 9 (messages 0x4C6240), EDF.dll
// TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"

#include <cstdint>

namespace crew {
namespace {
constexpr std::size_t kVtable=0x17C6900;
constexpr std::size_t kSlotUpdate=4;
constexpr std::size_t kSlotMessage=9;
constexpr std::size_t kUpdate=0x4C67E0;
constexpr std::size_t kMessage=0x4C6240;
constexpr std::size_t kKeyNode=0x528A0;     // (sgo handle*, key) -> node* or null
constexpr std::size_t kSgoAt=0xA0;
constexpr std::size_t kTargetModeAt=0x1A88;
constexpr std::size_t kModeAt=0x1A8C;
constexpr std::uint32_t kMsgDrop=0x20100;
constexpr std::uint32_t kMsgGenerator=0x20103;
constexpr std::size_t kDropIndexAt=0x48;
constexpr int kCoreGenerator=1;

using UpdateFn=std::uint64_t(__fastcall*)(void* self,void* dt);
using MessageFn=bool(__fastcall*)(void* self,std::uint32_t msg,void* data);
using KeyNodeFn=void*(__fastcall*)(void* sgo,const wchar_t* key);

UpdateFn nextUpdate=nullptr;
MessageFn nextMessage=nullptr;
void* lastStood=nullptr;

bool Marked(void* self) noexcept {
    return reinterpret_cast<KeyNodeFn>(image+kKeyNode)(static_cast<char*>(self)+kSgoAt,L"edf41_fortress")!=nullptr;
}

std::uint64_t __fastcall UpdateHook(void* self,void* dt) {
    if(Marked(self)) {
        int* target=reinterpret_cast<int*>(static_cast<char*>(self)+kTargetModeAt);
        if(*target==0) {
            *target=1;
            if(self!=lastStood) {
                lastStood=self;
                Log("EDF41FORTRESS %p stands up (mode %d -> 1)",self,*reinterpret_cast<int*>(static_cast<char*>(self)+kModeAt));
            }
        }
    }
    return nextUpdate(self,dt);
}

bool __fastcall MessageHook(void* self,std::uint32_t msg,void* data) {
    int* index=nullptr;
    if(data && (msg==kMsgDrop || msg==kMsgGenerator) && Marked(self))
        index=reinterpret_cast<int*>(static_cast<char*>(data)+(msg==kMsgDrop ? kDropIndexAt : 0));
    if(!index)return nextMessage(self,msg,data);
    const int was=*index;
    Log("EDF41FORTRESS %p message %X generator %d -> %d",self,msg,was,kCoreGenerator);
    *index=kCoreGenerator;
    const bool handled=nextMessage(self,msg,data);
    *index=was;
    return handled;
}
}  // namespace

bool InstallEdf41Fortress() noexcept {
    if(!Cfg().edf41Ai) {
        Log("EDF41FORTRESS off (ini Edf41Ai=0)");
        return false;
    }
    void** vtable=reinterpret_cast<void**>(image+kVtable);
    void* const update=vtable[kSlotUpdate];
    void* const message=vtable[kSlotMessage];
    if(update!=image+kUpdate || message!=image+kMessage) {
        Log("EDF41FORTRESS MovingFortress vtable is not EDF.dll's (update %p, messages %p): not hooked",update,message);
        return false;
    }
    nextUpdate=reinterpret_cast<UpdateFn>(update);
    nextMessage=reinterpret_cast<MessageFn>(message);
    const bool u=PatchVtableSlot(vtable+kSlotUpdate,update,reinterpret_cast<void*>(&UpdateHook));
    const bool m=PatchVtableSlot(vtable+kSlotMessage,message,reinterpret_cast<void*>(&MessageHook));
    Log("EDF41FORTRESS hooked: update=%d messages=%d",u ? 1 : 0,m ? 1 : 0);
    return u && m;
}
}  // namespace crew
