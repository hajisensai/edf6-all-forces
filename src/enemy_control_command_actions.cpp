// Typed, host-accepted catalogue commands. Native state constructors retain
// their own MAB/event/exit lifecycle; raw intent never starts a guest action.
#include "enemy_control_command_actions.h"
#include "enemy_control_action_native.h"
#include "enemy_control.h"
#include "enemy_control_monster_net.h"
#include "enemy_control_merman_net.h"
#include "enemy_control_boss_gun_net.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <cwchar>

namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_COMMAND_ACTIONS_TEST
void* CommandActionsTestFunction(unsigned) noexcept;
#endif
namespace command_actions {
using Result=enemy_control::ActionResult;
constexpr std::uint32_t kTag=0x31414345u; // ECA1
constexpr unsigned kRoots=8,kQueued=64,kBatch=8,kMaxRows=4096;
constexpr std::size_t kHeader=8,kEventBytes=57,kMaximum=512;
enum class Operation:unsigned char { monster=1,merman=2,bossNamed=3,bossHand=4,carrier=5 };
// This record proves command acceptance and the native constructor parameters.
// Actual delayed MAB/Unit factories keep their separate native outcome channel.
struct Event {
    Operation operation{};unsigned key=0;std::uint64_t serial=0,input=0,definition=0,seed3=0,seed4=0;
    float scale=1.0f;std::uint32_t mode=0,flags=0;
};
struct Receipt {std::uint64_t serial=0,hash=0;};
struct Request {std::uint64_t sequence=0,id=0,hash=0;bool result=false;};
struct Ref {unsigned char* object=nullptr;unsigned char* control=nullptr;};
struct Stream {
    Ref root{};const NativeProfile* profile=nullptr;bool held=false,authority=false,quiescing=false,faulted=false;
    bool rootHeld=false,cleanupEntered=false,cleanupUncertain=false;unsigned nativeCalls=0;
    std::uint64_t next=1,applied=0,lastRequest=0;unsigned head=0,count=0,requestCursor=0;Event queue[kQueued]{};
    Receipt receipts[kQueued]{};Request requests[kQueued]{};
};
struct Descriptor {const void* callback;std::int32_t adjustor=0,padding=0;};
constexpr const wchar_t* kBossNames[]={L"attack_rollingLazer",L"attack_bathingKick",L"attack_dive",L"attack_genocide",L"attack_meteorite"};
struct NativeWide {const wchar_t* text=nullptr;std::uint64_t unused=0;std::size_t length=0,capacity=0;};
struct NamedAction {NativeWide name{};std::int32_t priority=1;float blend=0.1f,speed=1;bool turnTarget=false;unsigned char reserved[3]{};};
static_assert(sizeof(NativeWide)==0x20 && sizeof(NamedAction)==0x30 && offsetof(NamedAction,turnTarget)==0x2C,"supported native named descriptor");
struct Definition {const unsigned char* row=nullptr;const wchar_t* name=nullptr;std::uint64_t digest=0;float scale=1;unsigned mode=0,flags=0;};
struct Sig {unsigned rva;unsigned char bytes[16];};
// Full supported-image fingerprints are added alongside each native contract.
constexpr Sig kSigs[]={
    {0x4B7AC0,{0x48,0x83,0xEC,0x38,0xF6,0x81,0xB0,0x08,0x00,0x00,0x04,0x0F,0x84,0xA1,0x00,0x00}},
    {0x4BE500,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x4BEC00,{0x40,0x56,0x48,0x83,0xEC,0x30,0x48,0x8B,0xF1,0x85,0xD2,0x0F,0x85,0x83,0x00,0x00}},
    {0x3A6710,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x3A6ED0,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x85,0xD2,0x0F,0x85,0x9F,0x00,0x00}},
    {0x52F50,{0x48,0x83,0xEC,0x28,0x4C,0x8B,0xD1,0xE8,0x54,0xFF,0xFF,0xFF,0x84,0xC0,0x74,0x11}},
    {0x4BB900,{0x4C,0x8B,0x51,0x18,0x48,0x8B,0xC1,0x4D,0x85,0xD2,0x74,0x0B,0x48,0x63,0x49,0x20}},
    {0x3A4460,{0x4C,0x8B,0x51,0x18,0x48,0x8B,0xC1,0x4D,0x85,0xD2,0x74,0x0B,0x48,0x63,0x49,0x20}},
    {0x475BE0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x57,0x48,0x83,0xEC,0x30,0x80,0xB9,0xE8,0x02}},
    {0x47D940,{0x40,0x53,0x48,0x83,0xEC,0x30,0xC7,0x44,0x24,0x28,0x00,0x00,0x00,0x00,0x48,0x8D}},
    {0x444C10,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x55}},
    {0x4448E0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x446D90,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x20,0x4C,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55}},
    {0x4471A0,{0x48,0x89,0x5C,0x24,0x08,0x4C,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55}},
    {0x46D5E0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x444770,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x447DA0,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x85,0xD2,0x0F,0x85,0xC0,0x00,0x00}},
    {0x447F00,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x85,0xD2,0x75,0x49,0x48,0x8B,0x89}},
    {0x447FD0,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x85,0xD2,0x75,0x49,0x48,0x8B,0x89}},
    {0x447A50,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xDA,0x48,0x8B,0xF9,0xE8}},
    {0x466E7C,{0x48,0x8B,0x01,0xFF,0xA0,0xE8,0x01,0x00,0x00,0xCC,0xCC,0xCC,0x48,0x8B,0x01,0xFF}},
    {0x4F6FB0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48}},
    {0x11664F0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x7C,0x24,0x10,0x8B,0xC2,0x45,0x33,0xC9,0x48}},
    {0x3A6410,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x12B48A0,{0x48,0x8B,0x41,0x08,0x41,0xB8,0xFF,0xFF,0xFF,0xFF,0x49,0x3B,0xC0,0x75,0x07,0x48}},
    {0x12B4BB0,{0xE9,0xAB,0xFA,0xFF,0xFF,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {0x12B4FD0,{0xB8,0xFF,0xFF,0xFF,0xFF,0x48,0x39,0x41,0x08,0x74,0x05,0x48,0x89,0x51,0x08,0xC3}},
    {0x4775E0,{0x80,0xFA,0xFF,0x0F,0x84,0x7D,0x01,0x00,0x00,0x48,0x89,0x5C,0x24,0x08,0x48,0x89}},
    {0x3A5DA0,{0x40,0x53,0x56,0x57,0x48,0x81,0xEC,0x30,0x06,0x00,0x00,0x48,0x8B,0x05,0xA6,0x92}},
    {0x7748F0,{0x48,0x8B,0x05,0x99,0xDF,0x93,0x01,0x8B,0x48,0x38,0x83,0xF9,0xFF,0x75,0x03,0x32}},
};
Stream streams[kRoots]{};unsigned char* admittedImage=nullptr;
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_COMMAND_ACTIONS_TEST
    return reinterpret_cast<T>(CommandActionsTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
std::uint64_t HashBytes(std::uint64_t value,const void* bytes,std::size_t size) noexcept {
    const auto* p=static_cast<const unsigned char*>(bytes);for(std::size_t i=0;i<size;++i){value^=p[i];value*=1099511628211ull;}return value;
}
std::uint64_t Number(std::uint64_t value,std::uint64_t n,unsigned size=8) noexcept {
    for(unsigned i=0;i<size;++i){const auto b=static_cast<unsigned char>(n>>(i*8));value=HashBytes(value,&b,1);}return value;
}
bool NameHash(const wchar_t* name,std::uint64_t* digest) noexcept {
    if(!name || !digest)return false;
    for(unsigned i=0;i<256;++i) {
        if(!Readable(name+i,sizeof(wchar_t)))return false;const auto c=static_cast<std::uint16_t>(name[i]);
        *digest=Number(*digest,c,2);if(!c)return i!=0;
    }
    return false;
}
bool Family(const NativeLease& l) noexcept {
    if(!l.profile || !Readable(l.profile,sizeof(*l.profile)))return false;
    std::size_t count=0;const auto* profiles=Profiles(&count);bool known=false;
    for(std::size_t i=0;i<count;++i)if(l.profile==profiles+i){known=true;break;}
    return known && (l.profile->kind==Kind::monster || l.profile->vtableRva==0x17B5370 ||
                    l.profile->vtableRva==0x17BEC68 || (l.profile->name &&
                    (!std::strcmp(l.profile->name,"UfoCarrier508") || !std::strcmp(l.profile->name,"UfoCarrier"))));
}
Ref Reference(const NativeLease& l) noexcept {return {l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))};}
bool Current(Ref r,bool alive=true) noexcept {
    return r.object && Readable(r.object,0x700,true) && Readable(r.control,16,true) &&
        At<LONG>(r.control,8)>0 && At<const void*>(r.object,0x30)==r.control &&
        (!alive || (!(r.object[0x18]&4) && !r.object[0x2E8]));
}
bool Pin(Ref r) noexcept {
    if(!Current(r,false))return false;auto* uses=reinterpret_cast<volatile LONG*>(r.control+8);LONG before=*uses;
    while(before>0 && before<(std::numeric_limits<LONG>::max)()) {
        const LONG actual=InterlockedCompareExchange(uses,before+1,before);if(actual==before)return true;before=actual;
    }
    return false;
}
bool Unpin(Ref r,bool* consumed) noexcept {
    if(!consumed)return false;*consumed=false;
    __try {
        if(!Readable(r.control,16,true) || At<LONG>(r.control,8)<=0)return false;
        const auto vt=At<void* const*>(r.control,0);if(!Readable(vt,16) || !vt[0] || !vt[1])return false;
        *consumed=true; // never retry a decrement once native cleanup has entered
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+8))==0) {
            reinterpret_cast<void(__fastcall*)(void*)>(vt[0])(r.control);
            if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12))==0)reinterpret_cast<void(__fastcall*)(void*)>(vt[1])(r.control);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
Stream* Find(const NativeLease& l) noexcept {
    for(auto& s:streams)if(s.held && s.root.object==l.object && s.root.control==l.control && s.profile==l.profile)return &s;return nullptr;
}
bool Hooks() noexcept;
bool Ready() noexcept {
    if(!image)return false;
    if(admittedImage)return admittedImage==image && Hooks();
    for(const auto& sig:kSigs)if(!Readable(image+sig.rva,16) || std::memcmp(image+sig.rva,sig.bytes,16))return false;
    if(!Hooks())return false;admittedImage=image;return true;
}
bool Needs(const NativeLease& l) noexcept {return Ready() && Family(l);}
bool Before(const NativeLease& l,bool authority,Input*) noexcept {
    if(!Needs(l) || !Current(Reference(l),false))return false;
    if(auto* s=Find(l))return s->rootHeld && !s->cleanupEntered && !s->cleanupUncertain && !s->nativeCalls && s->authority==authority && (!s->faulted || s->quiescing);
    for(auto& s:streams)if(!s.held) {
        if(!Pin(Reference(l)))return false;s.root=Reference(l);s.profile=l.profile;s.held=true;s.rootHeld=true;s.authority=authority;return true;
    }
    return false;
}
bool OperationFor(const NativeLease& l,const NativeAbility& a,Operation* operation) noexcept {
    if(!Family(l) || !operation || a.held || a.pressed || a.argument<0)return false;
    if(a.kind==AbilityKind::monsterAction && l.profile->kind==Kind::monster)*operation=Operation::monster;
    else if(a.kind==AbilityKind::mermanAction && l.profile->vtableRva==0x17B5370)*operation=Operation::merman;
    else if(a.kind==AbilityKind::namedAction && l.profile->vtableRva==0x17BEC68)*operation=Operation::bossNamed;
    else if(a.kind==AbilityKind::objectAction && l.profile->vtableRva==0x17BEC68)*operation=Operation::bossHand;
    else if(a.kind==AbilityKind::objectAction && a.argument==64)*operation=Operation::carrier;
    else return false;return true;
}
bool KeyFor(const NativeAbility& a,Operation operation,unsigned* key) noexcept {
    if(!key)return false;
    if(operation==Operation::bossNamed) {
        if(!a.action)return false;
        for(unsigned i=0;i<5;++i)if(!std::wcscmp(a.action,kBossNames[i])){*key=i;return true;}
        return false;
    }
    *key=static_cast<unsigned>(a.argument);return true;
}
bool Carrier(const NativeLease& l) noexcept {return l.profile && l.profile->name &&
    (!std::strcmp(l.profile->name,"UfoCarrier508") || !std::strcmp(l.profile->name,"UfoCarrier"));}
bool BossState(const NativeLease& l,const wchar_t* name) noexcept {
    if(!Readable(l.object,0x26CC,true))return false;const auto* model=At<const unsigned char*>(l.object,0xA70);const int casPlayer=At<int>(l.object,0xEB0);
    return model && casPlayer>=0 && casPlayer<=128 && Readable(model+0x330,0x80) &&
        Fn<int(__fastcall*)(const void*,int,const wchar_t*,bool)>(0x11664F0)(model+0x330,casPlayer,name,true)>=0;
}
bool Scale(float value) noexcept {return std::isfinite(value) && value>0.0f && value<1000000.0f;}
bool Row(const NativeLease& l,Operation operation,unsigned key,Definition* out) noexcept {
    if(!out || !Current(Reference(l)))return false;
    *out={};const unsigned char* rows=nullptr;std::size_t count=0;
    if(operation==Operation::monster && l.profile->kind==Kind::monster && Readable(l.object,0x14E1,true)) {
        rows=At<const unsigned char*>(l.object,0x14A8);count=At<std::size_t>(l.object,0x14B8);
        if(!count || count>kMaxRows || key>=count || !Readable(rows,count*0x38))return false;
        out->row=rows+key*0x38;out->name=At<const wchar_t*>(out->row,0);out->scale=At<float>(l.object,0x698);
        if(!Scale(out->scale) || !Scale(At<float>(out->row,8)))return false;
        out->digest=Number(1469598103934665603ull,1,1);out->digest=Number(out->digest,key,4);
        out->digest=HashBytes(out->digest,out->row+8,4);
    } else if(operation==Operation::merman && l.profile->vtableRva==0x17B5370 && Readable(l.object,0x1C80,true)) {
        rows=At<const unsigned char*>(l.object,0x16F8);count=At<std::size_t>(l.object,0x1708);
        if(!count || count>kMaxRows || key>=count || !Readable(rows,count*0x88))return false;
        out->row=rows+key*0x88;out->name=At<const wchar_t*>(out->row,0);out->mode=At<unsigned>(out->row,8);
        out->flags=out->row[0x18] ? 1u : 0u;
        if(out->mode>4 || At<int>(out->row,0x1C)<0)return false;
        out->digest=Number(1469598103934665603ull,2,1);out->digest=Number(out->digest,key,4);
        out->digest=Number(out->digest,out->mode,4);out->digest=Number(out->digest,At<unsigned>(out->row,0xC),4);
        out->digest=Number(out->digest,out->flags,1);out->digest=Number(out->digest,At<unsigned>(out->row,0x1C),4);
        const double rate=At<const void*>(out->row,0x28) ? Fn<double(__fastcall*)(const void*)>(0x52F50)(out->row+0x28) : 1.0;
        if(!std::isfinite(rate) || rate<=0 || rate>=1000000.0)return false;out->scale=static_cast<float>(rate);
        out->digest=HashBytes(out->digest,&out->scale,4);
    } else if((operation==Operation::bossNamed || operation==Operation::bossHand) && l.profile->vtableRva==0x17BEC68 && Readable(l.object,0x26CC,true)) {
        if(operation==Operation::bossNamed) {if(key>=5 || !BossState(l,kBossNames[key]))return false;out->name=kBossNames[key];}
        else {if(key<4 || key>6)return false;out->mode=At<unsigned>(l.object,0x26C8);if(out->mode>0x7FFFFFFEu)return false;}
        out->digest=Number(Number(1469598103934665603ull,static_cast<unsigned>(operation),1),key,4);
        if(operation==Operation::bossNamed)return NameHash(out->name,&out->digest) && out->digest;
        return out->digest!=0;
    } else if(operation==Operation::carrier && Carrier(l) && key==64 && Readable(l.object,0xD8C,true)) {
        out->digest=Number(Number(1469598103934665603ull,5,1),64,4);return out->digest!=0;
    } else return false;
    return NameHash(out->name,&out->digest) && out->digest;
}
bool PoolReady(const NativeLease& l,Operation operation) noexcept {
    const unsigned machine=operation==Operation::bossNamed ? 0xE50u : 0x17B0u;
    const unsigned context=operation==Operation::bossNamed ? 0xD90u : 0x16F0u;
    const unsigned vtable=operation==Operation::bossNamed ? 0x17C29A8u : 0x17BEC50u;
    if(!Readable(l.object+machine,0x30,true) || !Readable(l.object+context,0xB2,true) ||
       At<const void*>(l.object+machine,0)!=image+vtable || At<const void*>(l.object+machine,8)!=l.object+context ||
       At<const void*>(l.object+machine,0x28)!=l.object)return false;
    const auto* rows=At<const unsigned char*>(l.object+context,0x10);const auto count=At<std::size_t>(l.object+context,0x20);
    if(!count || count>512 || !Readable(rows,count*16))return false;
    for(std::size_t i=0;i<count;++i) {
        const auto* node=At<const unsigned char*>(rows+i*16,0);const auto* control=At<const unsigned char*>(rows+i*16,8);
        if(!Readable(node,0x20) || !Readable(control,12) || At<LONG>(control,8)<=0 || At<const void*>(node,0x18)!=l.object)return false;
    }
    return true;
}
unsigned HandCallback(unsigned key) noexcept {return key==4 ? 0x447DA0u : key==5 ? 0x447F00u : 0x447FD0u;}
bool RoleLease(void* actor,unsigned vtable,NativeLease* out=nullptr) noexcept {
    bool authority=false;if(!actor || !Readable(actor,0x38) || !NativeActionRole(actor,&authority))return false;
    const auto* table=At<const void* const*>(actor,0);
    if(!table || !Readable(image+vtable-sizeof(void*),sizeof(void*)) || !Readable(table-1,sizeof(void*)) ||
       table[-1]!=At<const void*>(image+vtable-sizeof(void*),0))return false;
    const NativeProfile* profile=nullptr;std::size_t count=0;const auto* profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(profiles[i].vtableRva==vtable){profile=profiles+i;break;}
    if(!profile)return false;
    if(out){out->object=static_cast<unsigned char*>(actor);out->profile=profile;out->control=At<const void*>(actor,0x30);}
    return true;
}
Stream* Controlled(void* actor,Operation operation) noexcept {
    const unsigned vtable=operation==Operation::merman ? 0x17B5370u : 0x17BEC68u;
    if(!RoleLease(actor,vtable))return nullptr;
    for(auto& s:streams)if(s.held && s.root.object==actor && Current(s.root,false))return &s;
    return nullptr;
}
void __fastcall MermanReceiver(void* receiver,void* packet) {
    auto* actor=static_cast<unsigned char*>(receiver)-0x120;auto* stream=Controlled(actor,Operation::merman);
    if(RoleLease(actor,0x17B5370)) {
        if(!Readable(packet,0x10)){if(stream)stream->faulted=true;return;}
        const auto cursor=At<std::uint64_t>(packet,8);const auto base=reinterpret_cast<std::uintptr_t>(packet);
        if(cursor==0xFFFFFFFFull || cursor>(std::numeric_limits<std::uintptr_t>::max)()-base-0x19 ||
           !Readable(reinterpret_cast<const void*>(base+0x10+cursor),9)){if(stream)stream->faulted=true;return;}
        std::uint64_t bookmark=0;bool saved=false,valid=false;unsigned char tag=0;
        __try {
            __try {
                Fn<void(__fastcall*)(void*,std::uint64_t*)>(0x12B48A0)(packet,&bookmark);saved=true;
                tag=Fn<unsigned char(__fastcall*)(void*)>(0x12B4BB0)(packet);valid=true;
            } __finally {if(saved)Fn<void(__fastcall*)(void*,std::uint64_t)>(0x12B4FD0)(packet,bookmark);}
        } __except(EXCEPTION_EXECUTE_HANDLER){if(stream)stream->faulted=true;return;}
        if(valid && tag==6)return; // reliable accepted entry owns the entire controlled lease
    }
    Fn<void(__fastcall*)(void*,void*)>(0x3A6410)(receiver,packet);
}
void __fastcall StockNamedEntry(void* machine,signed char ordinal,void* parameter) {
    if(Readable(machine,0x30)) {
        void* actor=At<void*>(machine,0x28);NativeLease l{};
        if(RoleLease(actor,0x17BEC68,&l) && static_cast<unsigned char*>(actor)+0xE50==machine && ordinal>=0) {
            if(PoolReady(l,Operation::bossNamed)) {
                const auto* context=l.object+0xD90;const auto count=At<std::size_t>(context,0x20);
                if(static_cast<unsigned>(ordinal)<count) {
                    const auto* rows=At<const unsigned char*>(context,0x10);const auto* node=At<const unsigned char*>(rows+static_cast<unsigned>(ordinal)*16,0);
                    if(At<const void*>(node,8)==image+0x466E7C && At<int>(node,0x10)==0 && At<const void*>(node,0x18)==l.object)return;
                } else {if(auto* s=Find(l))s->faulted=true;return;}
            } else {if(auto* s=Find(l))s->faulted=true;return;}
        }
    }
    Fn<void(__fastcall*)(void*,signed char,void*)>(0x4775E0)(machine,ordinal,parameter);
}
void __fastcall MermanSender(void* actor,int ordinal,bool selected) {
    if(RoleLease(actor,0x17B5370))return;
    Fn<void(__fastcall*)(void*,int,bool)>(0x3A5DA0)(actor,ordinal,selected);
}
bool EntryRelay(unsigned site,void* expected) noexcept {
    if(!Readable(image+site,5) || (image[site]!=0xE8 && image[site]!=0xE9))return false;
    std::int32_t relative=0;std::memcpy(&relative,image+site+1,4);const auto* thunk=image+site+5+relative;
    if(!Readable(thunk,12) || thunk[0]!=0x48 || thunk[1]!=0xB8 || thunk[10]!=0xFF || thunk[11]!=0xE0)return false;
    void* actual=nullptr;std::memcpy(&actual,thunk+2,sizeof(actual));return actual==expected;
}
bool Hooks() noexcept {
    static unsigned char* hookedImage=nullptr;static bool receiver=false,entries[5]{};static unsigned char entryBytes[5][5]{};
    constexpr unsigned sites[]={0x476530,0x3A3A29,0x3A6235,0x3A6587,0x3A67F0};
    if(hookedImage && hookedImage!=image)return false;
    auto** slot=reinterpret_cast<void**>(image+0x17B55A8);
    if(!Readable(slot,sizeof(void*)))return false;
    if(receiver && *slot!=reinterpret_cast<void*>(&MermanReceiver))return false;
    for(unsigned i=0;i<5;++i)if(entries[i] && (std::memcmp(image+sites[i],entryBytes[i],5) ||
        !EntryRelay(sites[i],i ? reinterpret_cast<void*>(&MermanSender) : reinterpret_cast<void*>(&StockNamedEntry))))return false;
    if(!receiver) {
#ifdef ENEMY_CONTROL_COMMAND_ACTIONS_TEST
        if(*slot!=image+0x3A6410)return false;*slot=reinterpret_cast<void*>(&MermanReceiver);
#else
        if(!edf::PatchVtableSlot(slot,image+0x3A6410,reinterpret_cast<void*>(&MermanReceiver)))return false;
#endif
        receiver=true;hookedImage=image;
    }
    for(unsigned i=0;i<5;++i)if(!entries[i]) {
        const unsigned target=i ? 0x3A5DA0u : 0x4775E0u;
        void* replacement=i ? reinterpret_cast<void*>(&MermanSender) : reinterpret_cast<void*>(&StockNamedEntry);
#ifdef ENEMY_CONTROL_COMMAND_ACTIONS_TEST
        const auto opcode=image[sites[i]];std::int32_t displacement=0;std::memcpy(&displacement,image+sites[i]+1,4);
        if((opcode!=0xE8 && opcode!=0xE9) || static_cast<std::int64_t>(sites[i])+5+displacement!=target)return false;
        // Model the real AllocateNearThunk format, so ownership tests exercise
        // both the caller bytes and the callback address stored in its relay.
        auto* thunk=image+0x20000+i*16;thunk[0]=0x48;thunk[1]=0xB8;thunk[10]=0xFF;thunk[11]=0xE0;
        std::memcpy(thunk+2,&replacement,sizeof(replacement));displacement=static_cast<std::int32_t>(thunk-(image+sites[i]+5));
        std::memcpy(image+sites[i]+1,&displacement,4);
#else
        bool changed=false;if(!RedirectCall(image+sites[i],image+target,replacement,changed) || !changed)return false;
#endif
        if(!EntryRelay(sites[i],replacement))return false;
        entries[i]=true;hookedImage=image;std::memcpy(entryBytes[i],image+sites[i],5);
    }
    return true;
}
bool NamedString(const NativeLease& l,const wchar_t* wanted) noexcept {
    const auto* value=l.object+0x1190;const auto size=std::wcslen(wanted);
    if(At<std::size_t>(value,0x10)!=size)return false;const auto* text=At<std::size_t>(value,0x18)<8 ? reinterpret_cast<const wchar_t*>(value) : At<const wchar_t*>(value,0);
    return Readable(text,(size+1)*2) && !std::memcmp(text,wanted,(size+1)*2);
}
bool MachineReady(const NativeLease& l,Operation operation) noexcept {
    if(operation==Operation::carrier)return Readable(l.object,0xD8C,true);
    if(operation==Operation::bossNamed || operation==Operation::bossHand)return PoolReady(l,operation);
    const auto machine=operation==Operation::monster ? 0x1020u : 0x1B90u;
    const auto context=operation==Operation::monster ? 0xF80u : 0x1AF0u;
    const auto vtable=operation==Operation::monster ? 0x17C5F90u : 0x17B5358u;
    return Readable(l.object+machine,0x28,true) && Readable(l.object+context,0x92,true) &&
        At<const void*>(l.object+machine,0)==image+vtable && At<const void*>(l.object+machine,8)==l.object+context &&
        At<const void*>(l.object+machine,0x10)==l.object;
}
bool Context(const NativeLease& l,std::size_t machine,unsigned callback) noexcept {
    if(!Readable(l.object+machine,0x28,true))return false;const auto* m=l.object+machine;
    const auto* context=At<const unsigned char*>(m,8);
    return Readable(context,0x92,true) && At<const void*>(context,8)==m &&
           At<const void*>(m,0x10)==l.object && At<const void*>(m,0x18)==image+callback && At<int>(m,0x20)==0;
}
bool Accepted(const NativeLease& l,const Event& e,const Definition& d) noexcept {
    if(e.operation==Operation::monster)return Context(l,0x1020,0x4BEC00) && At<int>(l.object,0x14D0)==static_cast<int>(e.key);
    if(e.operation==Operation::merman)return Context(l,0x1B90,0x3A6ED0) && At<const void*>(l.object,0x1710)==d.row &&
        At<unsigned>(l.object,0x17F0)==d.mode && At<unsigned>(l.object,0x17F4)==(d.flags ? 3u : 7u) && l.object[0x1718]==1;
    if(e.operation==Operation::bossNamed) {
        const Descriptor desc{image+0x466E7C};return Fn<bool(__fastcall*)(void*,const Descriptor*)>(0x46D5E0)(l.object+0xE50,&desc) &&
            At<const void*>(l.object+0xD90,0x28)==l.object+0xE50 && NamedString(l,d.name) && At<int>(l.object,0x11B0)==1 &&
            At<float>(l.object,0x11B4)==0.1f && At<float>(l.object,0x11B8)==1.0f && !l.object[0x11BC];
    }
    if(e.operation==Operation::bossHand) {
        const Descriptor desc{image+HandCallback(e.key)};return At<const void*>(l.object+0x16F0,0x28)==l.object+0x17B0 &&
            Fn<bool(__fastcall*)(void*,const Descriptor*)>(0x444770)(l.object+0x17B0,&desc);
    }
    return e.operation==Operation::carrier && At<int>(l.object,0xD88)==1;
}
bool HostGate(const NativeLease& l,const Event& e,const Definition& d) noexcept {
    if(e.operation==Operation::monster)return (At<unsigned>(l.object,0x8B0)&4u)!=0;
    if(e.operation==Operation::merman)return !(l.object[0x17F4]&1) && At<unsigned>(l.object,0x17F0)==d.mode;
    if(e.operation==Operation::bossNamed || e.operation==Operation::bossHand) {
        if(Fn<bool(__fastcall*)(void*)>(0x47D940)(l.object))return false;
        if(e.operation==Operation::bossHand)return At<float>(l.object,0x2F8)>0 && !(l.object[0xEAC]&0x20) && !Fn<bool(__fastcall*)(void*)>(0x4448E0)(l.object);
        if(At<unsigned>(l.object,0x10F0) || l.object[0x11F2] || (!l.object[0x1090] && At<unsigned>(l.object,0x9C0)<At<unsigned>(l.object,0xD6C)))return false;
        const Descriptor desc{image+0x466E7C};if(!Fn<bool(__fastcall*)(void*,const Descriptor*)>(0x46D5E0)(l.object+0xE50,&desc))return true;
        return At<int>(l.object,0x11B0)<1 || (At<int>(l.object,0x11B0)==1 && At<float>(l.object,0x11B4)<=0.1f);
    }
    return e.operation==Operation::carrier;
}
bool Invoke(const NativeLease& l,const Event& e,bool replay) noexcept {
    Definition d{};if(!Row(l,e.operation,e.key,&d) || d.digest!=e.definition || !MachineReady(l,e.operation) ||
        (e.operation==Operation::merman && e.scale!=d.scale))return false;
    if(!replay && !HostGate(l,e,d))return false;
    const float oldScale=At<float>(l.object,0x698);
    const unsigned oldMode=(e.operation==Operation::bossHand ? At<unsigned>(l.object,0x26C8) : 0);
    const unsigned machine=e.operation==Operation::bossNamed ? 0xE50u : 0x17B0u;
    const void* oldNode=nullptr;unsigned oldCounter=0;
    if(e.operation==Operation::bossNamed || e.operation==Operation::bossHand){oldNode=At<const void*>(l.object+machine,0x10);oldCounter=At<unsigned>(l.object+machine,0x20);}
    bool result=false;
    __try {
        // The separate sender hooks suppress Merman tag6 without changing
        // native authority flags while CAS/MAB entry commands execute.
        Put<std::uint64_t>(l.object,0x3E8,e.seed3);Put<std::uint64_t>(l.object,0x490,e.seed4);
        if(e.operation==Operation::monster) {
            Put<float>(l.object,0x698,e.scale);
            if(replay) {
                const Descriptor desc{image+0x4BEC00};const int key=static_cast<int>(e.key);
                Fn<void(__fastcall*)(void*,const Descriptor*,const void*)>(0x4BE500)(l.object+0x1020,&desc,&key);
            } else Fn<void(__fastcall*)(void*,int)>(0x4B7AC0)(l.object,static_cast<int>(e.key));
        } else if(e.operation==Operation::merman)Fn<void(__fastcall*)(void*,int,bool)>(0x3A6710)(l.object,static_cast<int>(e.key),true);
        else if(e.operation==Operation::bossNamed) {
            NamedAction action{};action.name.text=d.name;action.name.length=std::wcslen(d.name);action.name.capacity=action.name.length;
            if(replay){const Descriptor desc{image+0x466E7C};Fn<void(__fastcall*)(void*,const Descriptor*,const NamedAction*)>(0x446D90)(l.object+0xE50,&desc,&action);}
            else Fn<void(__fastcall*)(void*,const NamedAction*)>(0x475BE0)(l.object,&action);
        } else if(e.operation==Operation::bossHand) {
            Put<unsigned>(l.object,0x26C8,e.mode);
            if(replay){const Descriptor desc{image+HandCallback(e.key)};Fn<void(__fastcall*)(void*,const Descriptor*,const void*)>(0x4471A0)(l.object+0x17B0,&desc,nullptr);}
            else {int code=static_cast<int>(e.key);Fn<bool(__fastcall*)(void*,unsigned,void*)>(0x444C10)(l.object,0x1000000D,&code);}
        } else if(e.operation==Operation::carrier){int code=64;result=Fn<bool(__fastcall*)(void*,unsigned,void*)>(0x4F6FB0)(l.object,0x1000000D,&code);}
        if(e.operation!=Operation::carrier)result=true;
        result=result && Current(Reference(l)) && Accepted(l,e,d);
        if(result && (e.operation==Operation::bossNamed || e.operation==Operation::bossHand))
            result=oldNode!=At<const void*>(l.object+machine,0x10) || oldCounter!=At<unsigned>(l.object+machine,0x20);
    } __finally {
        if(e.operation==Operation::monster)Put<float>(l.object,0x698,oldScale);
        if(e.operation==Operation::bossHand)Put<unsigned>(l.object,0x26C8,oldMode);
    }
    return result;
}
void PutWire(unsigned char*& p,std::uint64_t n,unsigned size) noexcept {for(unsigned i=0;i<size;++i)*p++=static_cast<unsigned char>(n>>(8*i));}
std::uint64_t GetWire(const unsigned char*& p,unsigned size) noexcept {std::uint64_t n=0;for(unsigned i=0;i<size;++i)n|=std::uint64_t(*p++)<<(8*i);return n;}
bool Valid(const Event& e) noexcept {
    if(!e.serial || !e.input || !e.definition || !Scale(e.scale))return false;
    if(e.operation==Operation::monster)return e.key<kMaxRows && !e.mode && !e.flags;
    if(e.operation==Operation::merman)return e.key<kMaxRows && e.mode<=4 && e.flags<=1;
    if(e.operation==Operation::bossNamed)return e.key<5 && !e.mode && !e.flags && e.scale==1;
    if(e.operation==Operation::bossHand)return e.key>=4 && e.key<=6 && e.mode<=0x7FFFFFFEu && !e.flags && e.scale==1;
    return e.operation==Operation::carrier && e.key==64 && !e.mode && !e.flags && e.scale==1;
}
void Write(unsigned char*& p,const Event& e) noexcept {
    *p++=static_cast<unsigned char>(e.operation);PutWire(p,e.key,4);PutWire(p,e.serial,8);PutWire(p,e.input,8);PutWire(p,e.definition,8);
    PutWire(p,e.seed3,8);PutWire(p,e.seed4,8);std::uint32_t bits=0;std::memcpy(&bits,&e.scale,4);PutWire(p,bits,4);PutWire(p,e.mode,4);PutWire(p,e.flags,4);
}
bool Encode(const Event* events,unsigned count,void* out,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;if(!events || !out || !used || !count || count>kBatch)return false;
    const auto size=kHeader+count*kEventBytes;if(size>kMaximum || size>capacity || !Readable(out,size,true))return false;
    std::uint64_t previous=0;for(unsigned i=0;i<count;++i){if(!Valid(events[i]) || events[i].serial<=previous)return false;previous=events[i].serial;}
    auto* p=static_cast<unsigned char*>(out);PutWire(p,kTag,4);*p++=1;*p++=static_cast<unsigned char>(count);PutWire(p,0,2);
    for(unsigned i=0;i<count;++i)Write(p,events[i]);*used=size;return true;
}
bool Owns(const void* bytes,std::size_t size) noexcept {
    if(!bytes || size<4 || !Readable(bytes,4))return false;const auto* p=static_cast<const unsigned char*>(bytes);return GetWire(p,4)==kTag;
}
bool Decode(const void* bytes,std::size_t size,Event* events,unsigned* count) noexcept {
    if(!bytes || !events || !count || size<kHeader || size>kMaximum || !Readable(bytes,size))return false;
    const auto* p=static_cast<const unsigned char*>(bytes);if(GetWire(p,4)!=kTag || *p++!=1)return false;
    *count=*p++;if(!*count || *count>kBatch || GetWire(p,2) || size!=kHeader+*count*kEventBytes)return false;
    std::uint64_t previous=0;
    for(unsigned i=0;i<*count;++i) {
        auto& e=events[i];e={};e.operation=static_cast<Operation>(*p++);e.key=static_cast<unsigned>(GetWire(p,4));e.serial=GetWire(p,8);e.input=GetWire(p,8);
        e.definition=GetWire(p,8);e.seed3=GetWire(p,8);e.seed4=GetWire(p,8);const auto bits=static_cast<std::uint32_t>(GetWire(p,4));std::memcpy(&e.scale,&bits,4);
        e.mode=static_cast<unsigned>(GetWire(p,4));e.flags=static_cast<unsigned>(GetWire(p,4));
        if(!Valid(e) || e.serial<=previous)return false;previous=e.serial;
    }
    return true;
}
std::uint64_t Hash(const Event& e) noexcept {unsigned char bytes[kEventBytes]{};auto* p=bytes;Write(p,e);return HashBytes(1469598103934665603ull,bytes,sizeof(bytes));}
bool Capture(const NativeLease& l,void* out,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;auto* s=Find(l);if(!s || !used || !s->authority || !Current(s->root,false) || (s->faulted && !s->quiescing))return false;
    if(!s->count)return true;Event events[kBatch]{};unsigned count=0;const auto cap=(capacity<kMaximum ? capacity : kMaximum);
    while(count<s->count && count<kBatch && kHeader+(count+1)*kEventBytes<=cap){events[count]=s->queue[(s->head+count)%kQueued];++count;}
    if(!count || !Encode(events,count,out,cap,used))return false;s->head=(s->head+count)%kQueued;s->count-=count;return true;
}
Result Apply(const NativeLease& l,const void* bytes,std::size_t size) noexcept {
    auto* s=Find(l);Event events[kBatch]{};unsigned count=0;
    if(!s || s->authority || s->faulted || !s->rootHeld || s->cleanupEntered || s->cleanupUncertain || !Current(s->root) || !Decode(bytes,size,events,&count))return Result::failed;
    if(s->nativeCalls)return Result::pending;
    std::uint64_t next=s->applied+1;
    // Preflight the entire batch before its first native exit/entry. Retryable
    // sequence gaps and definition mismatches have no partially applied prefix.
    for(unsigned i=0;i<count;++i) {
        const auto& e=events[i];if(e.serial<=s->applied) {
            const auto& receipt=s->receipts[e.serial%kQueued];if(receipt.serial!=e.serial || receipt.hash!=Hash(e))return Result::failed;continue;
        }
        if(e.serial!=next)return Result::pending;++next;Definition d{};
        if(!Row(l,e.operation,e.key,&d) || d.digest!=e.definition || (e.operation!=Operation::bossHand && d.mode!=e.mode) || d.flags!=e.flags ||
           !MachineReady(l,e.operation) || (e.operation==Operation::merman && e.scale!=d.scale))return Result::failed;
    }
    for(unsigned i=0;i<count;++i) {
        const auto& e=events[i];if(e.serial<=s->applied)continue;
        bool accepted=false;++s->nativeCalls;
        __try {__try {accepted=Invoke(l,e,true);}__finally {--s->nativeCalls;}}
        __except(EXCEPTION_EXECUTE_HANDLER){accepted=false;}
        if(!accepted){s->faulted=true;return Result::failed;}
        s->applied=e.serial;s->receipts[e.serial%kQueued]={e.serial,Hash(e)};
    }
    return Result::applied;
}
bool Quiesce(const NativeLease& l) noexcept {if(auto* s=Find(l))s->quiescing=true;return true;}
bool Empty(const NativeLease& l) noexcept {const auto* s=Find(l);return !s || (!s->count && !s->nativeCalls && !s->cleanupEntered && !s->cleanupUncertain && s->rootHeld);}
bool Restore(const NativeLease& l) noexcept {
    auto* s=Find(l);if(!s)return true;
    if(s->count || s->nativeCalls || s->cleanupEntered || s->cleanupUncertain || !s->rootHeld)return false;
    s->cleanupEntered=true;bool consumed=false;const bool okay=Unpin(s->root,&consumed);
    if(consumed)s->rootHeld=false;s->cleanupEntered=false;
    if(!okay){if(consumed)s->cleanupUncertain=true;s->faulted=true;return false;}
    *s={};return true;
}
bool Abort(const NativeLease& l) noexcept {
    if(auto* s=Find(l)){s->quiescing=true;if(s->nativeCalls || s->cleanupEntered)return false;s->count=0;}
    return Restore(l);
}
bool Execute(const NativeLease& l,const NativeAbility& a,std::uint64_t sequence,std::uint64_t seed,bool* handled) noexcept {
    if(!handled)return false;*handled=false;Operation operation{};if(!OperationFor(l,a,&operation))return true;
    bool authority=false;const bool bound=NativeActionRole(l.object,&authority);bool online=false;
    if(!bound && !EnemyControlNetworkContext(l.object,&online)){*handled=true;return false;}if(!bound && !online)return true;
    *handled=true;auto* s=Find(l);
    if(!bound || !s || s->authority!=authority || s->quiescing || s->faulted || !s->rootHeld || s->cleanupEntered || s->cleanupUncertain || s->nativeCalls || !sequence || !Current(s->root))return false;
    if(!authority)return true; // only the separately authenticated accepted stream may enter a guest action
    unsigned key=0;if(!KeyFor(a,operation,&key))return false;
    const auto requestId=Number(Number(1469598103934665603ull,static_cast<unsigned>(operation),1),key,4);const auto request=Number(requestId,seed);
    for(const auto& prior:s->requests)if(prior.sequence==sequence && prior.id==requestId)return prior.hash==request && prior.result;
    if(sequence<s->lastRequest || s->count==kQueued || s->next==(std::numeric_limits<std::uint64_t>::max)())return false;
    Definition d{};if(!Row(l,operation,key,&d) || ((operation==Operation::monster || operation==Operation::merman) && a.action!=d.name))return false;
    Event e{};e.operation=operation;e.key=key;e.serial=s->next;e.input=sequence;e.definition=d.digest;e.scale=d.scale;e.mode=d.mode;e.flags=d.flags;
    auto& prior=s->requests[s->requestCursor++%kQueued];prior={sequence,requestId,request,false};s->lastRequest=sequence;if(!HostGate(l,e,d) || !SetSeed(l,seed))return false;
    e.seed3=At<std::uint64_t>(l.object,0x3E8);e.seed4=At<std::uint64_t>(l.object,0x490);
    bool accepted=false;++s->nativeCalls;
    __try {__try {accepted=Invoke(l,e,false);}__finally {--s->nativeCalls;}}
    __except(EXCEPTION_EXECUTE_HANDLER){s->faulted=true;return false;}
    if(!accepted){s->faulted=true;return false;}s->queue[(s->head+s->count)%kQueued]=e;++s->count;++s->next;prior.result=true;return true;
}
} // namespace command_actions
bool InstallCommandAcceptedActions() noexcept {
    using namespace command_actions;
    __try {
        if(!Ready() || !InstallMonsterAcceptedProjectiles() || !InstallMermanAcceptedProjectiles() || !InstallBossGunAcceptedProjectiles())return false;
        const NativeActionExtension extension{kTag,Needs,Before,nullptr,Capture,Owns,Apply,Quiesce,Empty,Abort,Restore};
        return RegisterNativeActionExtension(extension);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ExecuteAcceptedCommand(const NativeLease& l,const NativeAbility& a,std::uint64_t sequence,std::uint64_t seed,bool* handled) noexcept {
    __try {return command_actions::Execute(l,a,sequence,seed,handled);}
    __except(EXCEPTION_EXECUTE_HANDLER){if(handled)*handled=true;return false;}
}
} // namespace crew::enemy_native
