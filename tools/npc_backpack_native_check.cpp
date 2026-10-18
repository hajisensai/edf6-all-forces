// Execute the production backpack reader / gates / transition against stand-in memory. Only the external RTTI,
// state-machine and message entry points are recorded; no EDF.dll is loaded and no game is started.
#include "../src/npc_backpack.cpp"
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
bool session=false;
bool InSession() noexcept { return session; }
unsigned char human[0x1B00]{},weapon[0x1800]{},rifle[0x1000]{},set[0x150]{},records[0x20]{};
unsigned char* list[2]={rifle,weapon};
unsigned char* held=rifle;
bool castable=true;
int transitions=0,constructed=0,destroyed=0,sent=0,written=0,failures=0,cases=0;
std::int8_t message[2]{};
void* lastMachine=nullptr;void* lastState=nullptr;
const wchar_t* WeaponFile(const unsigned char* w,std::size_t* n) noexcept {
    constexpr wchar_t name[]=L"aHandGrenade01.SGO";
    *n=w==weapon ? std::wcslen(name) : 0;
    return w==weapon ? L"aHandGrenade01.SGO" : nullptr;
}
void* __cdecl CastRecord(void* w,long,void*,void*,int) {
    return w==weapon && castable ? weapon+0x20 : nullptr; // distinguish WeaponBase fields from the cast Weapon_Sub
}
void __fastcall GoRecord(void* machine,void* state,std::uint64_t) {
    ++transitions;lastMachine=machine;lastState=state;
}
void* __fastcall ConstructRecord(void* stream,int capacity) {
    if(capacity==0x40)++constructed;return stream;
}
void __fastcall DestroyRecord(void*) {++destroyed;}
bool __fastcall WriteRecord(void*,std::int8_t value) {
    if(written<2)message[written]=value;++written;return true;
}
bool __fastcall SendRecord(void* net,void*,std::uint64_t) {if(net==human+kNet)++sent;return true;}
void Jump(unsigned rva,const void* to) {
    auto* at=image+rva;at[0]=0x48;at[1]=0xB8;std::memcpy(at+2,&to,8);at[10]=0xFF;at[11]=0xE0;
}
void Check(bool pass,const char* reason) {
    ++cases;if(!pass){++failures;std::printf("FAIL: %s\n",reason);}
}
void Reset() {
    std::memset(human,0,sizeof(human));std::memset(weapon,0,sizeof(weapon));std::memset(set,0,sizeof(set));
    std::memset(rifle,0,sizeof(rifle));std::memset(records,0,sizeof(records));
    Put<void*>(human,kSubs,records);Put<std::uint64_t>(human,kSubCount,1);Put<int>(records,kSubIndex,1);
    Put<void*>(human,kWeaponList,list);Put<std::uint64_t>(human,kWeaponListCount,2);
    Put<void*>(human,kSets,set);Put<std::uint64_t>(human,kSetCount,1);Put<void*>(set,kSetWeapon,&held);
    Put<std::uint32_t>(human,kControlMask,0x2013);Put<int>(weapon,kWeaponAmmo,3);
    Put<std::uint16_t>(rifle,kMainBusy,7);human[kSubChain]=1;
    castable=true;transitions=constructed=destroyed=sent=written=0;session=false;lastMachine=lastState=nullptr;
}
void Refused(const char* reason) {
    unsigned char beforeHuman[sizeof(human)],beforeSet[sizeof(set)],beforeRifle[sizeof(rifle)];
    std::memcpy(beforeHuman,human,sizeof(human));std::memcpy(beforeSet,set,sizeof(set));std::memcpy(beforeRifle,rifle,sizeof(rifle));
    Check(!NpcPackCanUse(human,0) && !NpcPackUse(human,0) && !transitions &&
          !std::memcmp(beforeHuman,human,sizeof(human)) && !std::memcmp(beforeSet,set,sizeof(set)) &&
          !std::memcmp(beforeRifle,rifle,sizeof(rifle)),reason);
}
}  // namespace crew

int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2050000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    for(const auto& signature:kSignatures)std::memcpy(image+signature.rva,signature.bytes,signature.size);
    Check(InstallNpcBackpack(),"the production profile installs only after all signatures match");
    Jump(kDynamicCast,reinterpret_cast<const void*>(&CastRecord));Jump(kMachineGo,reinterpret_cast<const void*>(&GoRecord));
    Jump(kConstruct,reinterpret_cast<const void*>(&ConstructRecord));Jump(kDestroy,reinterpret_cast<const void*>(&DestroyRecord));
    Jump(kWriteSmall,reinterpret_cast<const void*>(&WriteRecord));
    Reset();Check(NpcPackCanUse(human,0) && !transitions && At<std::uint16_t>(rifle,kMainBusy)==7 && human[kSubChain],
                  "readiness reads real slots and gates without starting a use or changing the main weapon");
    NpcPackSlot slot{};
    Check(NpcPackSlots(human,&slot,1)==1 && slot.weapon==weapon && slot.slot==0 && slot.index==1 && slot.row==PackOf(1),
          "the production reader resolves the sub record through the actual weapon list");
    Check(NpcPackIndex(human,1) && !NpcPackIndex(human,0) && !NpcPackCanUse(human,-1) && !NpcPackCanUse(human,1),
          "the main list index and nonexistent sub slots are distinct");
    Reset();Put<std::uint32_t>(human,kControlMask,0x13);Refused("the main look / trigger may be allowed while the backpack slot is masked off");
    Reset();human[kStateBits]=kStateNoPack;Refused("the human state forbids the backpack without consuming a round or altering the main weapon");
    Reset();Put<std::uint32_t>(human,kHumanState,1);Refused("an occupied human action refuses the backpack");
    Reset();Put<void*>(human,kSubLock,human);Put<int>(human,kSubLockCount,1);Refused("a counted backpack lock refuses the use");
    Reset();Put<void*>(set,kSetSub,weapon+0x20);Refused("an ongoing backpack action is never cut by another use");
    Reset();Put<int>(weapon,kWeaponAmmo,0);Refused("an empty backpack is refused");
    Reset();castable=false;Refused("an accessory cannot be used as Weapon_Sub");
    Reset();human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);Refused("the player's own native input remains responsible for the backpack");
    Reset();Put<int>(records,kSubIndex,2);Refused("an out-of-bounds sub record cannot become a main weapon or state pointer");
    Reset();Check(NpcPackCanUse(human,0),"the backpack begins ready");
    Put<std::uint32_t>(human,kControlMask,0x13);Refused("Use rechecks the same gates if native state changed after the look's readiness check");
    Reset();Check(NpcPackUse(human,0) && transitions==1 && lastMachine==set+kSetMachine && lastState==image+kSubState &&
                  At<void*>(set,kSetSub)==weapon+0x20 && At<std::uint16_t>(rifle,kMainBusy)==0 && !human[kSubChain],
                  "a valid use enters the real sub state with the cast pointer and releases the main busy state");
    Reset();session=true;void* netTable[15]{};netTable[kSlotSend]=reinterpret_cast<void*>(&SendRecord);
    Put<void*>(human,kNet,netTable);Put<std::uint16_t>(human,kNetFlags,online::kNetLocal);
    Check(NpcPackUse(human,0) && constructed==1 && destroyed==1 && sent==1 && written==2 && message[0]==0x10 && message[1]==0,
          "an owned registered soldier sends one native slot event and destroys its stream");
    Reset();session=true;Put<std::uint16_t>(human,kNetFlags,online::kNetRemote);
    Check(NpcPackUse(human,0) && !constructed && !sent,"a remote copy never sends a duplicate event (the AI authority gate is upstream)");
    Reset();Put<int>(weapon+0x20,kReloadType,1);
    Check(NpcPackReload(weapon) && weapon[0x20+kReloadPress]==1 && !weapon[kReloadPress],
          "manual reload writes the cast Weapon_Sub's reload key");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("npc_backpack_native_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
