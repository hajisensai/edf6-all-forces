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
//  - It wanders as 4.1's does. 4.1 picks each step (one whole clip) at random when its gait is idle and it is clear of the
//    map's edge: walk 3 in 5, turn left (its turn_l, the class's walk_fwd_l) 1 in 5, turn right 1 in 5 (4.1 0x1cc845,
//    r = rand(5): 0..2 walk, 3 turn_l, 4 turn_r). The class steers away from the edge the same way (0x4CBC00) and otherwise
//    takes the step's sideways input from [+0x1970] (0x4C3244: 0 / 1 left, 3 / 4 right, anything else straight), which only
//    its network sync writes (0x4C41D2 / 0x4C9D72): offline it never changes. When the main player starts a new clip the
//    next step is drawn as 4.1 draws it and written there (2 straight, 0 left, 3 right).
//  - The script's ObjectAction (0x1000000D) means what it means to 4.1 (src/edf41fortress_action.h): 0..5 switch one part
//    group on / off through the class's own 0x4CA7E0, 6 destroys it through the class's own death (0x4CB6D0: HP 0, the
//    dead flags, the destroy state; what ApplyDamage + slot 18 0x4C60E0 do when the HP runs out). None of it reaches the
//    class's table, whose 1 spawns rubble at a locator 4.1's model has not: M117 sends ObjectAction(1) about two minutes
//    after the Ufo301 wave the fortress drops is cleared, and EDF.dll crashed at 0x4C3A30 every time
//    (docs/feedback-2026-10-18-edf41-m16-crash.md).
//
// The key is read the way the class reads its own (classic SGO): 0x528A0(&object+0xA0, name) gives the node or null.
// Hooks: MovingFortress's vtable 0x17C6900, slot 4 (update 0x4C67E0) and slot 9 (messages 0x4C6240), EDF.dll
// TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "edf41fortress_action.h"
#include "memory.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

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
constexpr std::uint32_t kMsgAction=0x1000000D;  // ObjectAction (native 3200); data: the int action
constexpr std::size_t kPartsSwitch=0x4CA7E0;     // (this, group mask, on): every part whose mask matches (the class's 3 / 4)
constexpr std::size_t kDie=0x4CB6D0;             // (this): once (guard [+0xE69]) HP 0, [+0x2E8] dead, destroy state 0x4CAE70
constexpr std::uint32_t kMsgDrop=0x20100;
constexpr std::uint32_t kMsgGenerator=0x20103;
constexpr std::size_t kDropIndexAt=0x48;
constexpr int kCoreGenerator=1;
constexpr std::size_t kStepChoiceAt=0x1970;  // int: 0 / 1 left, 3 / 4 right, other straight (0x4C3244)
constexpr std::size_t kCasAt=0x910;          // the CAS controller (0x4CB5C4); players at *(+0x60), 0x160 each
constexpr std::size_t kMainPlayerAt=0x14D0;  // int: the main player's index
constexpr std::size_t kPlayers=0x60,kPlayerSize=0x160,kPlayerState=0x104;
constexpr std::size_t kPlayerTime=0xE8;      // the clip time (the CAS update 0x1165B40 adds the advance at +0xF4; a new clip zeroes it)
constexpr int kStraight=2,kLeft=0,kRight=3;

using UpdateFn=std::uint64_t(__fastcall*)(void* self,void* dt);
using MessageFn=bool(__fastcall*)(void* self,std::uint32_t msg,void* data);
using KeyNodeFn=void*(__fastcall*)(void* sgo,const wchar_t* key);
using PartsSwitchFn=void(__fastcall*)(void* self,unsigned mask,int on);
using DieFn=void(__fastcall*)(void* self);

UpdateFn nextUpdate=nullptr;
MessageFn nextMessage=nullptr;
void* lastStood=nullptr;

struct Gait {
    int state=-2;
    float time=0.0f;
    std::uint64_t rng=0;
    ULONGLONG seen=0;
};
std::mutex gaitLock;
std::unordered_map<const void*,Gait> gaits;

// 4.1's draw of the next step (0x1cc845: rand(5), 0..2 walk, 3 turn_l, 4 turn_r) in the class's [+0x1970] codes.
int DrawStep(std::uint64_t& rng) noexcept {
    rng=rng*6364136223846793005ull+1442695040888963407ull;
    const unsigned r=static_cast<unsigned>((rng>>33)%5u);
    return r<3u ? kStraight : r==3u ? kLeft : kRight;
}

// A new clip on the main player: the next step drawn. The player is read the way 0x116BBE0 reads it.
void Wander(char* self) noexcept {
    const int index=*reinterpret_cast<int*>(self+kMainPlayerAt);
    char* players=*reinterpret_cast<char**>(self+kCasAt+kPlayers);
    if(index<0 || !players)return;
    char* clip=players+static_cast<std::size_t>(index)*kPlayerSize;
    const int state=*reinterpret_cast<int*>(clip+kPlayerState);
    const float time=*reinterpret_cast<float*>(clip+kPlayerTime);
    const ULONGLONG now=GetTickCount64();
    std::lock_guard<std::mutex> hold(gaitLock);
    Gait& g=gaits[self];
    if(g.seen==0 || now-g.seen>5000) {      // new here, or another object at this address
        g=Gait{};
        g.rng=reinterpret_cast<std::uintptr_t>(self)^(now*0x9E3779B97F4A7C15ull);
        *reinterpret_cast<int*>(self+kStepChoiceAt)=DrawStep(g.rng);
    } else if(state!=g.state || time<g.time) {
        *reinterpret_cast<int*>(self+kStepChoiceAt)=DrawStep(g.rng);
    }
    g.state=state;
    g.time=time;
    g.seen=now;
    if(gaits.size()>64)
        for(auto it=gaits.begin();it!=gaits.end();)
            it=now-it->second.seen>5000 ? gaits.erase(it) : std::next(it);
}

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
        Wander(static_cast<char*>(self));
    }
    return nextUpdate(self,dt);
}

// 4.1's ObjectAction on a 4.1 fortress (edf41fortress_action.h); 4.1 answers every action as handled (slot 6 returns 1).
bool Edf41Action(void* self,int action) noexcept {
    const fortress41::Action a=fortress41::FromEdf41(action);
    Log("EDF41FORTRESS %p ObjectAction %d (%s)",self,action,fortress41::Name(action));
    switch(a.kind) {
        case fortress41::Kind::Parts: reinterpret_cast<PartsSwitchFn>(image+kPartsSwitch)(self,a.groupMask,a.on ? 1 : 0); break;
        case fortress41::Kind::Destroy: reinterpret_cast<DieFn>(image+kDie)(self); break;
        case fortress41::Kind::Nothing: break;
    }
    return true;
}

bool __fastcall MessageHook(void* self,std::uint32_t msg,void* data) {
    if(msg==kMsgAction && data && Marked(self))return Edf41Action(self,*static_cast<const int*>(data));
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
    // The two class functions 4.1's actions run on (edf41fortress_action.h): their first bytes in EDF.dll 0x678CCB46.
    static constexpr unsigned char kPartsSwitchHead[8]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C};
    static constexpr unsigned char kDieHead[8]={0x40,0x53,0x48,0x83,0xEC,0x30,0x80,0xB9};
    if(!Readable(image+kPartsSwitch,8) || std::memcmp(image+kPartsSwitch,kPartsSwitchHead,8)!=0 ||
       !Readable(image+kDie,8) || std::memcmp(image+kDie,kDieHead,8)!=0) {
        Log("EDF41FORTRESS the class's parts switch / death are not EDF.dll's: not hooked");
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
