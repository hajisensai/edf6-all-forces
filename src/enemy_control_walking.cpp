// Exact instruction/asset contract: tests/enemy_control_walking_native_audit.py.
#include "enemy_control_walking.h"
#include "enemy_control_walking_net.h"
#include "enemy_control_walking_parts.h"
#include "enemy_control_action_native.h"
#include "enemy_control_types.h"
#include "enemy_control.h"
#include "crew.h"
#include "memory.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace crew::enemy_native {
namespace walking_control {
constexpr unsigned kMaxActions=4096,kRoots=8,kSelectionCall=0x34544B,kLookup=0xD7480,kEnter=0x33EDA0;
struct Handle {const unsigned char* tree=nullptr;std::uint32_t index=0,pad=0;};
static_assert(sizeof(Handle)==16,"native SGO handle");
using LookupFn=Handle*(__fastcall*)(const Handle*,Handle*,const wchar_t*);
struct Print {unsigned at;unsigned char bytes[16];};
constexpr Print prints[]={
    {0x11664F0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x7C,0x24,0x10,0x8B,0xC2,0x45,0x33,0xC9,0x48}},
    {0x34539B,{0x4C,0x8B,0x87,0xE8,0x03,0x00,0x00,0x48,0xB8,0x65,0x89,0x07,0x6C,0x65,0x8B,0x58}},
    {0x345428,{0x4C,0x8D,0x05,0x91,0x7B,0x46,0x01,0x89,0x44,0x24,0x28,0x48,0x8D,0x54,0x24,0x20}},
    {0x345450,{0x33,0xD2,0x48,0x8D,0x8F,0xD8,0x17,0x00,0x00,0xE8,0x82,0xD9,0xD0,0xFF,0x48,0x8B}},
    {0x345310,{0x40,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0xF9,0x85,0xD2,0x0F,0x85,0xAE,0x01,0x00}},
    {0xD7480,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0x4D,0x8B,0xD0}},
    {0x52DE0,{0x4C,0x8B,0x09,0x44,0x8B,0x51,0x08,0x44,0x8B,0xC2,0x4E,0x8D,0x04,0x85,0x00,0x00}},
    {0x52E50,{0x48,0x8B,0x11,0x48,0x85,0xD2,0x75,0x04,0x8D,0x42,0x05,0xC3,0x44,0x8B,0x49,0x08}},
    {0x53060,{0x48,0x8B,0x11,0x44,0x8B,0x49,0x08,0x4C,0x39,0x4A,0x18,0x76,0x26,0x48,0x8B,0x42}},
    {0x551E0,{0x48,0x83,0xEC,0x28,0x4C,0x8B,0xD1,0xE8,0xC4,0xDC,0xFF,0xFF,0x84,0xC0,0x74,0x1A}},
    {0x33EDA0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x48}},
};
unsigned char* admittedImage=nullptr;bool installed=false,redirected=false;
struct Scope {const NativeLease* lease=nullptr;unsigned char* root=nullptr;const void* control=nullptr;Handle row{};bool selected=false;};
thread_local Scope* selection=nullptr;
struct Entry {unsigned char* root=nullptr;const unsigned char* control=nullptr;std::uint64_t sequence=0,seed3=0,seed4=0,intentSeed=0;unsigned ordinal=0;std::uint8_t partOrdinal=0;bool result=false,intentKnown=false,published=false,intentInFlight=false,nativeInFlight=false;};
Entry entries[kRoots]{};
struct Label {char id[40]{};};
const char* Id(unsigned ordinal) noexcept {
    struct Labels {std::array<Label,kMaxActions> items{};Labels() noexcept {
        for(unsigned i=0;i<kMaxActions;++i)std::snprintf(items[i].id,sizeof(items[i].id),"walking_attack_%u",i);
    }};
    static const Labels labels;return ordinal<kMaxActions ? labels.items[ordinal].id : nullptr;
}
bool Family(const NativeLease& lease) noexcept {
    if(!lease.profile || lease.profile->kind!=Kind::walking)return false;
    std::size_t count=0;const auto profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(lease.profile==profiles+i) {
        const unsigned vt=lease.profile->vtableRva;
        return vt==0x17ADA78 || vt==0x17AE088 || vt==0x17AE968 || vt==0x17AED50 || vt==0x17AF200 || vt==0x17AF9F8;
    }
    return false;
}
bool Current(unsigned char* root,const void* control,bool alive=true) noexcept {
    return root && control && Readable(root,0x1941) && Readable(control,16) && At<LONG>(control,8)>0 &&
        At<const void*>(root,0x30)==control && (!alive || (!(root[0x18]&4) && !root[0x2E8]));
}
bool Identity(const NativeLease& lease) noexcept {return Family(lease) && Current(lease.object,lease.control);}
bool Native() noexcept {return installed && image && image==admittedImage && WalkingPartsControlReady();}
bool HandleValid(const Handle& h) noexcept {
    return h.tree && h.index!=UINT32_MAX && Readable(h.tree,0x58);
}
unsigned Type(const Handle& h) noexcept {
    if(!HandleValid(h))return 5;
    return reinterpret_cast<unsigned(__fastcall*)(const Handle*)>(image+0x52E50)(&h);
}
bool Count(const Handle& h,unsigned* out) noexcept {
    if(!out || Type(h)!=3)return false; // native DSGO array, not numeric expression/VM
    const int count=reinterpret_cast<int(__fastcall*)(const Handle*)>(image+0x53060)(&h);
    if(count<0 || count>static_cast<int>(kMaxActions))return false;
    *out=static_cast<unsigned>(count);return true;
}
bool Child(const Handle& parent,unsigned ordinal,Handle* out) noexcept {
    unsigned count=0;if(!out || !Count(parent,&count) || ordinal>=count)return false;
    const auto index=reinterpret_cast<std::uint32_t(__fastcall*)(const Handle*,std::uint32_t)>(image+0x52DE0)(&parent,ordinal);
    *out=Handle{parent.tree,index,0};return HandleValid(*out);
}
bool Text(const Handle& h,const wchar_t** out,std::uint64_t* digest=nullptr) noexcept {
    if(!out || Type(h)!=1)return false; // names must be literal strings, never evaluated blobs
    const wchar_t* text=reinterpret_cast<const wchar_t*(__fastcall*)(const Handle*)>(image+0x551E0)(&h);
    if(!text)return false;
    for(unsigned i=0;i<128;++i) {
        if(!Readable(text+i,sizeof(wchar_t)))return false;
        const auto c=static_cast<std::uint16_t>(text[i]);
        if(digest){*digest^=static_cast<unsigned char>(c);*digest*=1099511628211ull;*digest^=c>>8;*digest*=1099511628211ull;}
        if(!c){if(!i)return false;*out=text;return true;}
    }
    return false;
}
bool Table(const NativeLease& lease,Handle* attacks,unsigned* count) noexcept {
    if(!Native() || !Identity(lease) || !attacks || !count)return false;
    const Handle settings=At<Handle>(lease.object,0x17C8);
    return Child(settings,1,attacks) && Count(*attacks,count);
}
bool MainPlayerReady(const NativeLease& lease) noexcept {
    if(!Identity(lease) || !Readable(lease.object+0xC30,24))return false;
    const auto players=At<const unsigned char*>(lease.object,0xC30);
    const auto count=At<std::size_t>(lease.object,0xC40);const int main=At<int>(lease.object,0xFD0);
    if(!count || count>256 || main<0 || static_cast<std::size_t>(main)>=count || !Readable(players,count*0x160))return false;
    const auto states=At<const unsigned char*>(players+static_cast<std::size_t>(main)*0x160,0xE0);
    if(!Readable(states,12))return false;
    const unsigned stateCount=At<unsigned>(states,4);
    if(!stateCount || stateCount>65536)return false;
    const auto address=reinterpret_cast<std::uintptr_t>(states);
    const auto offset=static_cast<std::int64_t>(At<std::int32_t>(states,8));
    std::uintptr_t start=0;
    if(offset<0) {const auto back=static_cast<std::uint64_t>(-offset);if(back>address)return false;start=address-static_cast<std::uintptr_t>(back);}
    else {if(static_cast<std::uint64_t>(offset)>UINTPTR_MAX-address)return false;start=address+static_cast<std::uintptr_t>(offset);}
    return Readable(reinterpret_cast<const void*>(start),static_cast<std::size_t>(stateCount)*0x24);
}
bool HasState(const NativeLease& lease,const wchar_t* name) noexcept {
    return reinterpret_cast<int(__fastcall*)(void*,int,const wchar_t*,bool)>(image+0x11664F0)(lease.object+0xBD0,At<int>(lease.object,0xFD0),name,true)>=0;
}
bool Row(const NativeLease& lease,unsigned ordinal,Handle* out,const wchar_t** name=nullptr,std::uint64_t* digest=nullptr,bool* available=nullptr) noexcept {
    Handle attacks{};unsigned count=0;
    if(!out || !Table(lease,&attacks,&count) || !MainPlayerReady(lease) || ordinal>=count || !Child(attacks,ordinal,out))return false;
    unsigned fields=0;if(!Count(*out,&fields) || !fields || fields>8)return false;
    std::uint64_t hash=14695981039346656037ull;bool actual=true;
    for(unsigned i=0;i<fields;++i) {
        Handle field{};const wchar_t* value=nullptr;
        if(!Child(*out,i,&field) || !Text(field,&value,&hash))return false;
        if(!HasState(lease,value))actual=false;
        if(i==0 && name)*name=value;
    }
    if(digest)*digest=hash;
    if(available)*available=actual;
    return available || actual;
}
const wchar_t* LabelFor(const wchar_t* name) noexcept {
    struct Pair {const wchar_t* state;const wchar_t* label;};
    constexpr Pair labels[]={
        {L"attack_armStomp_l",L"左臂重击"},{L"attack_armStomp_r",L"右臂重击"},{L"attack_armStomp_both",L"双臂重击"},
        {L"attack_shoot_both",L"双拳齐发"},{L"attack_shoot_lr",L"先左后右连拳"},{L"attack_shoot_rl",L"先右后左连拳"},
        {L"attack_head_cannon",L"头部炮击"},{L"attack_head_beam",L"头部光束"},
        {L"attack_suicideBomb_banzai",L"举弹自爆"},{L"attack_suicideBomb_try",L"前扑自爆"}
    };
    for(const auto& pair:labels)if(std::wcscmp(name,pair.state)==0)return pair.label;
    return name;
}
bool Ready(const NativeLease& lease) noexcept {
    if(!Identity(lease) || !(lease.object[0x12D0]&1))return false;
    const auto vt=lease.profile->vtableRva;
    if(vt==0x17ADA78)return true; // Large wrapper 34EEE0 has no main-mode restriction.
    if(vt==0x17AF200) {
        // In an established tether, native Attack(0) means pull/release, not an
        // AttackSetting row. Keep those real contextual actions on the pad.
        return At<int>(lease.object,0x12DC)==0 && At<std::size_t>(lease.object,0x1820)==0;
    }
    return At<int>(lease.object,0x12DC)!=1;
}
bool Entered(const NativeLease& lease,const Handle& row) noexcept {
    if(!Identity(lease) || !Readable(lease.object+0x18A8,12))return false;
    const Handle actual=At<Handle>(lease.object,0x17D8);
    if(actual.tree!=row.tree || actual.index!=row.index || (lease.object[0x12D0]&1) ||
       At<const void*>(lease.object,0x18A8)!=image+0x33E124 || At<int>(lease.object,0x18B0)!=0 || !MainPlayerReady(lease))return false;
    return true;
}
bool Committed(const NativeLease& lease,const Handle& row) noexcept {
    if(!Entered(lease,row))return false;
    const auto players=At<const unsigned char*>(lease.object,0xC30);
    const auto main=static_cast<unsigned>(At<int>(lease.object,0xFD0));
    const int playing=At<int>(players+static_cast<std::size_t>(main)*0x160,0x104);
    unsigned fields=0;if(!Count(row,&fields))return false;
    for(unsigned i=0;i<fields;++i) {
        Handle field{};const wchar_t* name=nullptr;
        if(!Child(row,i,&field) || !Text(field,&name))return false;
        const int state=reinterpret_cast<int(__fastcall*)(void*,int,const wchar_t*,bool)>(image+0x11664F0)(lease.object+0xBD0,static_cast<int>(main),name,true);
        if(state>=0 && playing==state)return true;
    }
    return false;
}
Handle* __fastcall Choose(const Handle* settings,Handle* out,const wchar_t* key) {
    Scope* const scope=selection;
    if(scope && settings==reinterpret_cast<const Handle*>(scope->root+0x17C8)) {
        // This wrapper is installed at exactly the one call AFTER the native
        // chooser stores its row and BEFORE its full FSM consumes that row.
        const auto* lease=scope->lease;
        if(!lease || lease->object!=scope->root || lease->control!=scope->control ||
           !Current(scope->root,scope->control) || settings!=reinterpret_cast<const Handle*>(scope->root+0x17C8) ||
           !key || std::wcscmp(key,L"animation_speed")!=0 || !Readable(scope->root+0x17D8,sizeof(Handle),true)) {
            RaiseException(0xE0422001,0,0,nullptr);return nullptr;
        }
        Put<Handle>(scope->root,0x17D8,scope->row);scope->selected=true;
    }
    return reinterpret_cast<LookupFn>(image+kLookup)(settings,out,key);
}
bool Pin(const void* control) noexcept {
    if(!Readable(control,16,true))return false;
    auto* uses=reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(const_cast<void*>(control))+8);
    LONG value=*uses;
    while(value>0 && value<0x7FFFFFFF) {
        const LONG was=InterlockedCompareExchange(uses,value+1,value);
        if(was==value)return true;value=was;
    }
    return false;
}
void Unpin(const void* control) noexcept {
    auto* c=static_cast<unsigned char*>(const_cast<void*>(control));
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(c+8))!=0)return;
    const auto table=At<void* const*>(c,0);
    reinterpret_cast<void(__fastcall*)(void*)>(table[0])(c);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(c+12))==0)
        reinterpret_cast<void(__fastcall*)(void*)>(table[1])(c);
}
Entry* Owner(const NativeLease& lease,bool create) noexcept {
    Entry* free=nullptr;
    for(auto& entry:entries) {
        if(entry.root==lease.object && entry.control==lease.control)return &entry;
        if(!entry.root && !free)free=&entry;
    }
    if(!create || !free || !Identity(lease) || !Readable(lease.control,16,true))return nullptr;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(const_cast<void*>(lease.control))+12));
    free->root=lease.object;free->control=static_cast<const unsigned char*>(lease.control);return free;
}
bool Drop(Entry& entry) noexcept {
    if(!entry.root)return true;
    if(!Readable(entry.control,16,true) || At<LONG>(entry.control,12)<=0)return false;
    auto* c=const_cast<unsigned char*>(entry.control);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(c+12))==0) {
        const auto table=At<void* const*>(c,0);reinterpret_cast<void(__fastcall*)(void*)>(table[1])(c);
    }
    entry={};return true;
}
}
bool InstallWalkingControl() noexcept {
    using namespace walking_control;
    __try {
        if(Native())return InstallWalkingAcceptedActions();
        if(!image || (admittedImage && admittedImage!=image))return false;
        for(const auto& print:prints)if(!Readable(image+print.at,16) || std::memcmp(image+print.at,print.bytes,16))return false;
        constexpr unsigned char rowStore[]={0x0F,0x11,0x87,0xD8,0x17,0x00,0x00};
        if(!Readable(image+0x345444,sizeof(rowStore)) || std::memcmp(image+0x345444,rowStore,sizeof(rowStore)))return false;
        if(!redirected) {
            bool changed=false;const bool ok=RedirectCall(image+kSelectionCall,image+kLookup,reinterpret_cast<void*>(&Choose),changed);
            redirected=changed;if(!ok)return false;
        }
        admittedImage=image;installed=InstallWalkingAcceptedActions();
        return installed;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ReadWalkingActionCount(const NativeLease& lease,unsigned* out) noexcept {
    if(!out)return false;*out=0;
    __try {
        walking_control::Handle attacks{};unsigned count=0,actualCount=0;
        if(!walking_control::Table(lease,&attacks,&count) || !walking_control::MainPlayerReady(lease))return false;
        for(unsigned i=0;i<count;++i) {
            walking_control::Handle row{};bool available=false;
            if(!walking_control::Row(lease,i,&row,nullptr,nullptr,&available))return false;
            if(available)++actualCount;
        }
        *out=actualCount;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool WalkingNativeMainAttackAvailable(const NativeLease& lease) noexcept {
    unsigned actual=0;if(!ReadWalkingActionCount(lease,&actual))return false;
    return actual>0 || lease.profile->vtableRva==0x17AF200; // Grapple also owns real pull/release contexts.
}
unsigned ListWalkingActionAbilities(const NativeLease& lease,NativeAbility* out,unsigned capacity) noexcept {
    using namespace walking_control;
    __try {
        Handle attacks{};unsigned count=0,actualCount=0;
        if(!Table(lease,&attacks,&count) || !MainPlayerReady(lease))return 0;
        // Validate even hidden rows. Missing native CAS states are inherited
        // template data, not selectable attacks (e.g. ported Hector's stomps).
        for(unsigned i=0;i<count;++i) {
            Handle row{};const wchar_t* name=nullptr;bool available=false;
            if(!Row(lease,i,&row,&name,nullptr,&available))return 0;
            if(!available)continue;
            if(out && actualCount<capacity)out[actualCount]=NativeAbility{Id(i),LabelFor(name),0,0,AbilityKind::walkingAction,name,static_cast<std::int32_t>(i)};
            ++actualCount;
        }
        return actualCount;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}
bool WalkingActionDefinition(const NativeLease& lease,unsigned ordinal,std::uint64_t* digest) noexcept {
    __try {walking_control::Handle row{};return digest && walking_control::Row(lease,ordinal,&row,nullptr,digest);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool WalkingActionReady(const NativeLease& lease,unsigned ordinal) noexcept {
    __try {walking_control::Handle row{};return walking_control::Row(lease,ordinal,&row) && walking_control::Ready(lease);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool WalkingActionEntered(const NativeLease& lease,unsigned ordinal) noexcept {
    __try {walking_control::Handle row{};return walking_control::Row(lease,ordinal,&row) && walking_control::Entered(lease,row);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool WalkingActionCommitted(const NativeLease& lease,unsigned ordinal) noexcept {
    __try {walking_control::Handle row{};return walking_control::Row(lease,ordinal,&row) && walking_control::Committed(lease,row);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool CommitWalkingAction(const NativeLease& lease,unsigned ordinal,std::uint64_t sequence,std::uint64_t pre3E8,std::uint64_t pre490,std::uint8_t prePartOrdinal) noexcept {
    using namespace walking_control;
    __try {
        Handle row{};if(!sequence || !Row(lease,ordinal,&row) || !Readable(lease.object+0x3E8,8,true) ||
            !Readable(lease.object+0x490,8,true) || !Readable(lease.object+0x1940,1,true) || !Readable(lease.object+0x17D8,16,true))return false;
        Entry* entry=Owner(lease,true);if(!entry)return false;
        if(sequence<=entry->sequence)return sequence==entry->sequence && ordinal==entry->ordinal &&
            pre3E8==entry->seed3 && pre490==entry->seed4 && prePartOrdinal==entry->partOrdinal && entry->result;
        if(entry->nativeInFlight || !Ready(lease) || !Pin(lease.control))return false;
        bool result=false;Scope scope{&lease,lease.object,lease.control,row,false};Scope* prior=selection;
        __try {
            if(!Identity(lease) || !Ready(lease))return false;
            entry->nativeInFlight=true;entry->sequence=sequence;entry->ordinal=ordinal;entry->seed3=pre3E8;entry->seed4=pre490;entry->partOrdinal=prePartOrdinal;entry->result=false;
            Put<std::uint64_t>(lease.object,0x3E8,pre3E8);Put<std::uint64_t>(lease.object,0x490,pre490);
            lease.object[0x1940]=prePartOrdinal;
            if(lease.profile->vtableRva==0x17AED50)Put<unsigned>(lease.object,0x380,At<unsigned>(lease.object,0x380)|0x10000000u);
            selection=&scope;
            reinterpret_cast<void(__fastcall*)(void*)>(image+kEnter)(lease.object);
            result=scope.selected && Committed(lease,row);entry->result=result;
        } __finally {entry->nativeInFlight=false;selection=prior;Unpin(scope.control);}
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool CommitWalkingAction(const NativeLease& lease,unsigned ordinal,std::uint64_t sequence,std::uint64_t pre3E8,std::uint64_t pre490) noexcept {
    __try {
        return walking_control::Identity(lease) && CommitWalkingAction(lease,ordinal,sequence,pre3E8,pre490,lease.object[0x1940]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ExecuteWalkingAction(const NativeLease& lease,unsigned ordinal,std::uint64_t sequence,std::uint64_t seed) noexcept {
    using namespace walking_control;
    bool began=false;
    __try {
        Handle row{};if(!sequence || !Row(lease,ordinal,&row))return false;
        bool authority=false;
        const bool bound=NativeActionRole(lease.object,&authority);
        // The retained Core session supplies immutable network context, also
        // while closing/faulted. A missing role or context never grants offline
        // authority through a global probe which can fail or become stale.
        bool networked=bound;
        if(!bound && !EnemyControlNetworkContext(lease.object,&networked))return false;
        if(networked) {
            if(!bound || NativeActionQuiescing(lease.object))return false;
            if(!authority)return true;
        }
        if(auto* old=Owner(lease,false)) {
            if(old->intentInFlight || old->nativeInFlight)return false;
            if(sequence<=old->sequence)return sequence==old->sequence && ordinal==old->ordinal &&
                old->intentKnown && seed==old->intentSeed && old->result && old->published;
        }
        if(!Ready(lease) || (networked && !WalkingAcceptedCanQueue(lease)))return false;
        Entry* entry=Owner(lease,true);if(!entry)return false;
        entry->intentInFlight=true;
        __try {
            if(!SetSeed(lease,seed))return false;
            if(!Identity(lease))return false; // extension seeding may hand the lease back reentrantly
            const auto pre3=At<std::uint64_t>(lease.object,0x3E8),pre4=At<std::uint64_t>(lease.object,0x490);
            if(networked) {
                if(!BeginWalkingAcceptedAction(lease,ordinal,sequence,pre3,pre4))return false;
                began=true;
            }
            if(!CommitWalkingAction(lease,ordinal,sequence,pre3,pre4)) {
                if(began)CancelWalkingAcceptedAction(lease);
                return false;
            }
            entry->intentSeed=seed;entry->intentKnown=true;
            entry->published=!networked || QueueWalkingAcceptedAction(lease,ordinal,sequence,pre3,pre4);
            began=false;
            return entry->published;
        } __finally {entry->intentInFlight=false;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(began)CancelWalkingAcceptedAction(lease);
        return false;
    }
}
bool ReadWalkingActionState(const NativeLease& lease,unsigned ordinal,enemy_control::Ability* out) noexcept {
    if(!out)return false;
    out->state=enemy_control::AbilityState::unavailable;out->cooldownKnown=false;out->cooldownSeconds=0;
    __try {
        walking_control::Handle row{};if(!walking_control::Row(lease,ordinal,&row))return false;
        out->state=walking_control::Committed(lease,row) ? enemy_control::AbilityState::active : walking_control::Ready(lease) ?
            enemy_control::AbilityState::native : enemy_control::AbilityState::cooldown;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool RestoreWalkingControl(const NativeLease& lease) noexcept {
    __try {
        auto* entry=walking_control::Owner(lease,false);
        if(entry && (entry->intentInFlight || entry->nativeInFlight))return false;
        if(!RestoreWalkingAcceptedActions(lease))return false;
        return !entry || (!entry->intentInFlight && !entry->nativeInFlight && walking_control::Drop(*entry));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
} // namespace crew::enemy_native
