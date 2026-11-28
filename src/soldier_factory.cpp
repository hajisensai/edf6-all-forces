// The new classes' player objects (src/soldier_factory.h): factories ZwVehicler / ZwRuneBlade on AssultSoldier's constructor with
// cloned vtables. The sites are src/soldier_sites.inc (tests/soldier_native_audit.py checks them against EDF.dll).
//
// The clones follow AssultSoldier's live tables. Other code patches slots of the live 0x17CDF28 / 0x17CE220 / 0x17CE2D8 for
// every AssultSoldier: EDF6Coop's player sync (slots 52 / 53 / 55 / 92 of the main table, its netplayer_game.cpp), this
// plugin's own hooks, at whatever time each installs (another plugin may load after this one). A clone of the file's stock
// functions would skip all of them for classes 4 / 5 (online: their players not synced). So right before each object is made,
// every slot of the three clones this module does not replace (Owned: main 1 and 83, slot 0 of both secondary tables) is
// copied from the live table; a hook that patches the live table is then inherited by the new classes with no list of
// hooks here. The EDF4.1 ports' clones (edf41classes) keep their own rule (the file's functions, counted).
#include "soldier_factory.h"
#include "soldier_type_logic.h"
#include "edf_layout.h"
#include "edf41classes.h"
#include "plugin_log.h"
#include "memory.h"

#include <atomic>
#include <cstring>
#include <new>

namespace crew {
namespace {
namespace st=soldier_type;
using edf41c::Clone;

// The plugin's part, at +0x2090 (16-aligned; the game's new 0x12D85B0 is malloc).
struct State {
    std::uint32_t magic;
    int type;
    void* data;
    soldier::ReleaseFn release;
};
constexpr std::size_t kStateAt=st::kStockSize;
static_assert(kStateAt%16==0,"the state starts 16-aligned");
constexpr std::size_t kObjectSize=kStateAt+sizeof(State);

Clone vehiclerClass{"ZwVehicler",st::kVtable,st::kMainSlots,st::kMainVtable,{},{},{}};
Clone runeBladeClass{"ZwRuneBlade",st::kVtable,st::kMainSlots,st::kMainVtable,{},{},{}};
// The secondary tables, shared by both classes: [0] the stock complete object locator, slot 0 the adjustor to the object's own
// (main table) destructor, the rest the stock functions as the file has them.
void* table120[1+st::kSlots120];
void* table1940[1+st::kSlots1940];
// The slots this module replaces (the rest follow the live tables: Inherit).
constexpr bool OwnedMain(int slot) noexcept { return slot==edf41c::kSlotDtor || slot==st::kSlotType; }
constexpr bool OwnedSecondary(int slot) noexcept { return slot==0; }
std::atomic<bool> ready{false};
std::atomic<bool> factories{false};   // both AddFactory calls succeeded
std::atomic<std::uint32_t> made[2],destroyed[2];

State* StateOf(void* object) noexcept { return reinterpret_cast<State*>(static_cast<char*>(object)+kStateAt); }
const State* StateOf(const void* object) noexcept { return reinterpret_cast<const State*>(static_cast<const char*>(object)+kStateAt); }

int TypeOfVtable(const void* vptr) noexcept {
    if(!ready.load(std::memory_order_acquire) || !vptr)return -1;
    if(vptr==&vehiclerClass.block[1])return st::kVehicler;
    if(vptr==&runeBladeClass.block[1])return st::kRuneBlade;
    return -1;
}

void ReleaseData(State& s) noexcept {
    if(s.data && s.release)s.release(s.data);
    s.data=nullptr;
    s.release=nullptr;
}

using DeleteFn=void(__fastcall*)(void*,std::size_t);
// The clones' deleting destructor (slot 1; rcx this, edx flags bit 0 = delete). The plugin's part first (the most derived),
// then AssultSoldier's destructor without its delete (0x550654: `test bpl,1` skips it), then the delete of the whole object.
std::uint64_t __fastcall Dtor(void* self,unsigned flags) noexcept {
    State* s=StateOf(self);
    const int k=s->type==st::kRuneBlade ? 1 : 0;
    if(s->magic==st::kStateMagic) {
        ReleaseData(*s);
        s->magic=0;
        destroyed[k].fetch_add(1,std::memory_order_relaxed);
    }
    const std::uint64_t result=reinterpret_cast<edf41c::DtorFn>(image+st::kDtor)(self,flags&~1u);
    if(flags&1u)reinterpret_cast<DeleteFn>(image+st::kDelete)(self,kObjectSize);
    return result;
}
// Slot 0 of the secondary tables: the stock ones are `sub rcx,0x120 / 0x1940; jmp 0x5505B0`; these go through the object's
// main table, so whichever class it is gets its own destructor.
std::uint64_t __fastcall Dtor120(char* self,unsigned flags) noexcept {
    void* object=self-st::kAt120;
    return reinterpret_cast<edf41c::DtorFn>((*static_cast<void***>(object))[edf41c::kSlotDtor])(object,flags);
}
std::uint64_t __fastcall Dtor1940(char* self,unsigned flags) noexcept {
    void* object=self-st::kAt1940;
    return reinterpret_cast<edf41c::DtorFn>((*static_cast<void***>(object))[edf41c::kSlotDtor])(object,flags);
}
int __fastcall VehiclerType(void*) noexcept { return st::kVehicler; }
int __fastcall RuneBladeType(void*) noexcept { return st::kRuneBlade; }

// Every slot not owned, from the live table (readable: checked by the install). Aligned pointer stores: an object of the
// class already alive reads either the old or the new function, both the live table's at some time.
void InheritTable(void** slots,std::size_t vtable,int count,bool (*owned)(int) noexcept) noexcept {
    const void* const* live=reinterpret_cast<const void* const*>(image+vtable);
    for(int i=0;i<count;++i)
        if(!owned(i))slots[i]=const_cast<void*>(live[i]);
}
void Inherit(Clone& c) noexcept {
    InheritTable(&c.block[1],st::kVtable,st::kMainSlots,&OwnedMain);
    InheritTable(&table120[1],st::kTable120,st::kSlots120,&OwnedSecondary);
    InheritTable(&table1940[1],st::kTable1940,st::kSlots1940,&OwnedSecondary);
}

void Create(Clone& c,int type,void* info) noexcept {
    Inherit(c);
    void* obj=edf41c::CloneObject(c,st::kCtor,kObjectSize,info);
    if(!obj)return;   // logged; left an AssultSoldier (its own destructor frees the whole block)
    char* o=static_cast<char*>(obj);
    new(StateOf(obj)) State{st::kStateMagic,type,nullptr,nullptr};
    void** at120=reinterpret_cast<void**>(o+st::kAt120);
    void** at1940=reinterpret_cast<void**>(o+st::kAt1940);
    if(*at120==image+st::kTable120 && *at1940==image+st::kTable1940) {
        *at120=&table120[1];
        *at1940=&table1940[1];
    } else {
        Log("ZWSOLDIER %s %p: the +0x120 / +0x1940 tables are %p / %p, not AssultSoldier's: left (their destructor skips the plugin's part)",
            c.tag,obj,*at120,*at1940);
    }
    const std::uint32_t n=made[type==st::kRuneBlade ? 1 : 0].fetch_add(1,std::memory_order_relaxed)+1;
    if(n<=8)Log("ZWSOLDIER create #%u: %s %p (type %d) on AssultSoldier's constructor",n,c.tag,obj,type);
}
void CreateVehicler(void* info) noexcept { Create(vehiclerClass,st::kVehicler,info); }
void CreateRuneBlade(void* info) noexcept { Create(runeBladeClass,st::kRuneBlade,info); }

bool PrintsMatch() noexcept {
    constexpr std::size_t rvas[]={0x54FE50,0x54FE6D,0x54FF10,0x54FF39,0x5505B0,0x550654,0x550414,0x550420,0x550A20,0x12D85EC};
    for(const std::size_t rva:rvas) {
        const st::Site* site=st::FindPrint(rva);
        unsigned char bytes[st::kMostSiteBytes];
        const std::size_t n=site ? st::Decode(site->hex,bytes,sizeof(bytes)) : 0;
        if(!n || !Readable(image+rva,n) || !Matches(rva,bytes,n)) {
            Log("ZWSOLDIER the code at EDF.dll+0x%zX (%s) is not what this was written for: the new classes' player objects are off",rva,
                site ? site->what : "unlisted");
            return false;
        }
    }
    return true;
}

bool BuildTable(void** block,std::size_t vtable,const std::uint32_t* rvas,int slots,void* slot0) noexcept {
    const void* const* vt=reinterpret_cast<const void* const*>(image+vtable);
    if(!Readable(vt-1,sizeof(void*)*static_cast<std::size_t>(slots+1)))return false;
    block[0]=const_cast<void*>(vt[-1]);
    block[1]=slot0;
    for(int i=1;i<slots;++i) {
        if(vt[i]!=image+rvas[i])Log("ZWSOLDIER table 0x%zX slot %d is patched already (%p, stock EDF.dll+0x%X): the clones follow it",vtable,i,vt[i],rvas[i]);
        block[1+i]=const_cast<void*>(vt[i]);   // refreshed from the live table before each object (Inherit)
    }
    return true;
}
}  // namespace

bool InstallSoldierFactory() noexcept {
    if(!PrintsMatch())return false;
    if(!BuildTable(table120,st::kTable120,st::kVtable120,st::kSlots120,reinterpret_cast<void*>(&Dtor120))
       || !BuildTable(table1940,st::kTable1940,st::kVtable1940,st::kSlots1940,reinterpret_cast<void*>(&Dtor1940))) {
        Log("ZWSOLDIER AssultSoldier's secondary tables are not readable: the new classes' player objects are off");
        return false;
    }
    for(Clone* c:{&vehiclerClass,&runeBladeClass}) {
        if(!edf41c::BuildClone(*c,nullptr,nullptr,nullptr)) {
            Log("ZWSOLDIER the %s clone was not built: the new classes' player objects are off",c->tag);
            return false;
        }
        edf41c::SetSlot(*c,edf41c::kSlotDtor,reinterpret_cast<void*>(&Dtor));
    }
    edf41c::SetSlot(vehiclerClass,st::kSlotType,reinterpret_cast<void*>(&VehiclerType));
    edf41c::SetSlot(runeBladeClass,st::kSlotType,reinterpret_cast<void*>(&RuneBladeType));
    static_assert(OwnedMain(edf41c::kSlotDtor) && OwnedMain(st::kSlotType) && OwnedSecondary(0),"the slots set here are the owned ones");
    if(!Readable(image+st::kVtable,sizeof(void*)*st::kMainSlots)) {
        Log("ZWSOLDIER AssultSoldier's main table is not readable: the new classes' player objects are off");
        return false;
    }
    for(Clone* c:{&vehiclerClass,&runeBladeClass})Inherit(*c);
    soldier::zwVtables[0]=&vehiclerClass.block[1];
    soldier::zwVtables[1]=&runeBladeClass.block[1];
    ready.store(true,std::memory_order_release);
    const bool v=edf41c::AddFactory(L"ZwVehicler",&CreateVehicler);
    const bool r=edf41c::AddFactory(L"ZwRuneBlade",&CreateRuneBlade);
    Log("ZWSOLDIER factories ZwVehicler=%d ZwRuneBlade=%d (AssultSoldier 0x%zX, object 0x%zX bytes)",v,r,st::kCtor,kObjectSize);
    factories.store(v && r,std::memory_order_release);
    return v && r;
}

namespace soldier {
bool FactoriesReady() noexcept { return factories.load(std::memory_order_acquire); }
int ZwType(const void* object) noexcept {
    if(!object || !Readable(object,sizeof(void*)))return -1;
    return TypeOfVtable(*static_cast<const void* const*>(object));
}
bool SetExtension(void* object,void* data,ReleaseFn release) noexcept {
    if(ZwType(object)<0)return false;
    State* s=StateOf(object);
    if(s->magic!=st::kStateMagic)return false;
    if(s->data!=data)ReleaseData(*s);
    s->data=data;
    s->release=release;
    return true;
}
void* Extension(const void* object) noexcept {
    if(ZwType(object)<0)return nullptr;
    const State* s=StateOf(object);
    return s->magic==st::kStateMagic ? s->data : nullptr;
}
Counts Made() noexcept {
    return {{made[0].load(),made[1].load()},{destroyed[0].load(),destroyed[1].load()}};
}
}  // namespace soldier
}  // namespace crew
