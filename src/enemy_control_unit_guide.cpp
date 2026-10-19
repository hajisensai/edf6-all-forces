// The final native Unit descriptor guide, after authored callbacks and before1194280.
#include "enemy_control_unit_guide.h"
#include "enemy_control.h"
#include "enemy_control_native.h"
#include "crew.h"
#include "memory.h"
#include "edf/patch.h"
#include <cstring>

namespace crew {
#ifdef ENEMY_CONTROL_UNIT_GUIDE_TEST
void* UnitGuideTestFunction(unsigned) noexcept;
#endif
namespace unit_guide {
constexpr unsigned kSite=0x6D3301,kMove=0x58EB40;
constexpr unsigned char kOriginal[]={0x4C,0x8D,0x8D,0x80,0x01,0x00,0x00,0x4C,0x8B,0x86,0xF0,0x02,0x00,0x00};
constexpr unsigned char kMoveBytes[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x02,0x48,0x8B,0xD9,0x4C,0x8B,0x42,0x08};
struct Weak {const void* object=nullptr;const void* control=nullptr;};
struct Code {
    unsigned char bytes[512]{};std::size_t size=0;
    void Data(const unsigned char* p,std::size_t n) noexcept {std::memcpy(bytes+size,p,n);size+=n;}
    void Ptr(const void* p) noexcept {std::memcpy(bytes+size,&p,8);size+=8;}
};
unsigned char* admitted=nullptr;void* thunk=nullptr;Code installed{};unsigned char siteBytes[sizeof(kOriginal)]{};
bool faulted=false;
bool Known(const enemy_native::NativeProfile* p) noexcept {
    if(!p)return false;std::size_t count=0;const auto profiles=enemy_native::Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(p==profiles+i)return true;return false;
}
bool Needs(const enemy_native::NativeProfile* p) noexcept {
    constexpr const char* names[]={"EDF6_FlyingDyno","GiantAntEX","GiantBeeEX","GiantSpiderEX",
        "EDF6_Berserker_Middle","EDF6_Berserker_TypeShooter","EDF6_GiantDangoLarge",
        "EDF6_Merman","EDF6_ShellFish","EDF6_SpinnerUfo"};
    if(!p->name)return false;for(const auto* name:names)if(!std::strcmp(name,p->name))return true;return false;
}
bool Integrity() noexcept {
    return !faulted && image && image==admitted && thunk && installed.size && Readable(thunk,installed.size) &&
        Readable(image+kSite,sizeof(siteBytes)) && !std::memcmp(thunk,installed.bytes,installed.size) &&
        !std::memcmp(image+kSite,siteBytes,sizeof(siteBytes)) && Readable(image+kMove,sizeof(kMoveBytes)) &&
        !std::memcmp(image+kMove,kMoveBytes,sizeof(kMoveBytes));
}
bool Profile(const void* root) noexcept {
    const auto table=At<const unsigned char*>(root,0);if(!Readable(table-8,8))return false;
    const auto col=At<const void*>(table-8,0);std::size_t count=0;const auto profiles=enemy_native::Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(Readable(image+profiles[i].vtableRva-8,8) &&
        At<const void*>(image+profiles[i].vtableRva-8,0)==col)return true;
    return false;
}
bool Owner(const void* unit,const void* owner,const void* frameControl) noexcept {
    // Unit is a plain helper. Its real owner weak was locked into this frame by6D26B9..26F2.
    if(!Readable(unit,0x2C0) || !Readable(owner,0x38) || !Readable(frameControl,16) ||
       At<const void*>(unit,0x2B0)!=owner || At<const void*>(unit,0x2B8)!=frameControl ||
       At<const void*>(owner,0x28)!=owner || At<const void*>(owner,0x30)!=frameControl ||
       At<LONG>(frameControl,8)<=0 || !EnemyControlTargetActive(owner) || !Profile(owner))return false;
    bool online=false;return EnemyControlNetworkContext(owner,&online);
}
constexpr unsigned kJournals=64;
struct Journal {
    const void* owner=nullptr;const void* control=nullptr;Weak root{},old{},next{},publishingToken{};
    bool busy=false,uncertain=false,publishing=false;
};
Journal journals[kJournals]{};
bool Empty(const Journal& j) noexcept {return !j.owner && !j.control && !j.root.control && !j.old.control && !j.next.control && !j.busy && !j.uncertain && !j.publishing;}
bool WritableNow(const void* pointer,std::size_t bytes) noexcept {
    auto at=reinterpret_cast<std::uintptr_t>(pointer);if(!at || bytes>UINTPTR_MAX-at)return false;const auto end=at+bytes;
    while(at<end) {
        MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(reinterpret_cast<const void*>(at),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
            (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto p=info.Protect&0xFFu;if(p!=PAGE_READWRITE && p!=PAGE_WRITECOPY && p!=PAGE_EXECUTE_READWRITE && p!=PAGE_EXECUTE_WRITECOPY)return false;
        const auto next=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;if(next<=at)return false;at=next;
    }
    return true;
}
bool Drop(Journal& j,Weak& token) noexcept {
    if(!token.control){token={};return true;}
    if(j.uncertain || !Readable(token.control,16) || !WritableNow(token.control,16) || At<LONG>(token.control,12)<=0)return false;
    const auto table=At<const void* const*>(token.control,0);if(!Readable(table,16) || !Readable(table[1],1))return false;
    const void* control=token.control;const void* destroy=table[1];
    // Retain the token until the actual atomic instruction has succeeded.
    const LONG was=InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(const_cast<unsigned char*>(static_cast<const unsigned char*>(control))+12),-1);
    token={};
    if(was<=0){j.uncertain=true;return false;}
    if(was==1) {
        j.uncertain=true; // a final native delete has entered; never repeat a partial callback
        reinterpret_cast<void(__fastcall*)(const void*)>(destroy)(control);j.uncertain=false;
    }
    return true;
}
bool Cleanup(Journal& j) noexcept {
    if(j.uncertain || j.publishing)return false;
    if(!Drop(j,j.next) || !Drop(j,j.old) || !Drop(j,j.root))return false;
    j={};return true;
}
Journal* Begin(const void* owner,const void* control) noexcept {
    for(auto& j:journals)if(j.owner==owner && j.control==control)return nullptr;
    Journal* free=nullptr;for(auto& j:journals)if(Empty(j)){free=&j;break;}
    if(!free || !Readable(control,16) || !WritableNow(control,16) || At<LONG>(control,12)<=0 || At<LONG>(control,12)==0x7FFFFFFF)return nullptr;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(const_cast<unsigned char*>(static_cast<const unsigned char*>(control))+12));
    free->owner=owner;free->control=control;free->root={owner,control};free->busy=true;return free;
}
using MoveFn=void*(__fastcall*)(void*,void*);
void Move(Weak* out,Weak* input) {
#ifdef ENEMY_CONTROL_UNIT_GUIDE_TEST
    reinterpret_cast<MoveFn>(UnitGuideTestFunction(kMove))(out,input);
#else
    reinterpret_cast<MoveFn>(image+kMove)(out,input);
#endif
}
bool Equal(Weak a,Weak b) noexcept {return a.object==b.object && a.control==b.control;}
void RecoverPublish(Journal& j,Weak* destination) noexcept {
    if(!j.publishing)return;
    if(!Readable(destination,sizeof(*destination))) {j.uncertain=true;return;}
    if(!j.next.control && !j.next.object) {
        if(Equal(*destination,j.publishingToken)) { /* the native descriptor owns this token */ }
        else if(!destination->control)j.next=j.publishingToken;
        else j.uncertain=true;
    } else if(!Equal(j.next,j.publishingToken) || destination->control)j.uncertain=true;
    j.publishing=false;j.publishingToken={};
}
bool __fastcall Guide(void* unit,const void* owner,const void* frameControl,Weak* destination) noexcept {
    bool owned=false;
    __try {owned=Owner(unit,owner,frameControl);} __except(EXCEPTION_EXECUTE_HANDLER){owned=false;}
    if(!owned)return true;
    Journal* journal=nullptr;
    __try {
        __try {
            if(!Integrity() || !Readable(destination,sizeof(*destination),true) || !WritableNow(destination,sizeof(*destination)))return false;
            journal=Begin(owner,frameControl);if(!journal){faulted=true;return false;}
            // Transfer the already-owned shot weak to a PERSISTENT ledger, with
            // no count change. The original descriptor is now safely empty.
            journal->old=*destination;*destination={};
            bool canonical=false;
            __try {canonical=EnemyControlWeaponLock(owner,&journal->next);}
            __except(EXCEPTION_EXECUTE_HANDLER){canonical=false;}
            // Core CopyLock validates its selected LOCAL ordinal. Portable wire
            // IDs are not a prerequisite for legitimate offline lock points.
            if(!canonical && !Drop(*journal,journal->next)){faulted=true;return false;}
            if(journal->next.control && !journal->next.object){faulted=true;return false;}
            journal->publishingToken=journal->next;journal->publishing=true;
            Move(destination,&journal->next); // destination empty; old release is separately journaled
            RecoverPublish(*journal,destination);
            if(journal->uncertain || journal->next.control || journal->next.object){faulted=true;return false;}
            // Same native +C/vslot1 release, but consumed is known only AFTER the
            // atomic instruction. Read-only refusals keep the exact owned token.
            if(!Cleanup(*journal)){faulted=true;return false;}
            journal=nullptr;return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            faulted=true;if(journal)RecoverPublish(*journal,destination);return false;
        }
    } __finally {if(journal)journal->busy=false;}
}
void Jump(Code& c,const void* p) noexcept {constexpr unsigned char op[]={0xFF,0x25,0,0,0,0};c.Data(op,sizeof(op));c.Ptr(p);}
void Vectors(Code& c,bool load) noexcept {
    for(unsigned i=0;i<6;++i){const unsigned char op[]={0xF3,0x0F,static_cast<unsigned char>(load ? 0x6F : 0x7F),
        static_cast<unsigned char>(0x44|(i<<3)),0x24,static_cast<unsigned char>(0x20+i*16)};c.Data(op,sizeof(op));}
}
Code Build() noexcept {
    Code c{};constexpr unsigned char enter[]={0x9C,0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,
        0x48,0x81,0xEC,0x80,0,0,0,0xFC};c.Data(enter,sizeof(enter));Vectors(c,false);
    // RSI=real unit. RBP-frame owns the exact owner/control strong pair.
    constexpr unsigned char args[]={0x48,0x8B,0xCE,0x48,0x8B,0x55,0xA8,0x4C,0x8B,0x45,0xB0,
        0x4C,0x8D,0x8D,0xE0,0x01,0,0,0x48,0xB8};c.Data(args,sizeof(args));c.Ptr(reinterpret_cast<const void*>(&Guide));
    constexpr unsigned char call[]={0xFF,0xD0,0x84,0xC0,0x0F,0x84};c.Data(call,sizeof(call));const auto branch=c.size;
    const unsigned zero=0;c.Data(reinterpret_cast<const unsigned char*>(&zero),4);
    Vectors(c,true);
    constexpr unsigned char restore[]={0x48,0x81,0xC4,0x80,0,0,0,0x41,0x5B,0x41,0x5A,0x41,0x59,0x41,0x58,0x5A,0x59,0x58,0x9D};
    c.Data(restore,sizeof(restore));c.Data(kOriginal,sizeof(kOriginal));Jump(c,image+kSite+sizeof(kOriginal));
    const auto blocked=c.size;Vectors(c,true);c.Data(restore,sizeof(restore));
    constexpr unsigned char empty[]={0x33,0xC0};c.Data(empty,sizeof(empty));Jump(c,image+0x6D3322);
    const auto relative=static_cast<std::uint32_t>(blocked-(branch+4));std::memcpy(c.bytes+branch,&relative,4);return c;
}
}
bool EnemyControlUnitGuideReady(const enemy_native::NativeProfile* profile) noexcept {
    __try {return unit_guide::Known(profile) && (!unit_guide::Needs(profile) || unit_guide::Integrity());}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool RestoreEnemyControlUnitGuide(const void* root,const void* control) noexcept {
    using namespace unit_guide;
    __try {
        for(auto& j:journals)if(j.owner==root && j.control==control) {
            if(j.busy || j.uncertain || j.publishing)return false;
            bool ok=false;j.busy=true;
            __try {ok=Cleanup(j);} __finally {j.busy=false;}
            if(!ok)return false;
        }
        bool empty=true;for(const auto& j:journals)empty=empty && Empty(j);
        if(empty)faulted=false;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}
}
bool InstallEnemyControlUnitGuide() noexcept {
    using namespace unit_guide;
    __try {
        if(thunk)return Integrity();
        if(!image || (admitted && image!=admitted) || !Readable(image+kSite,sizeof(kOriginal)) ||
           std::memcmp(image+kSite,kOriginal,sizeof(kOriginal)) || !Readable(image+kMove,sizeof(kMoveBytes)) ||
           std::memcmp(image+kMove,kMoveBytes,sizeof(kMoveBytes)))return false;
        const Code code=Build();void* allocated=AllocateNearCode(image+kSite,code.bytes,code.size);if(!allocated)return false;
        Code patch{};Jump(patch,allocated);
        if(!edf::PatchCode(image+kSite,kOriginal,patch.bytes,sizeof(kOriginal))){VirtualFree(allocated,0,MEM_RELEASE);return false;}
        admitted=image;thunk=allocated;installed=code;std::memcpy(siteBytes,patch.bytes,sizeof(siteBytes));return Integrity();
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
