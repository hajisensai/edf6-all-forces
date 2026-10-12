// src/ifc_model.h CarryModel against a fake image whose variant visitor tables (find 0x179EBA8, get 0x179EAF0) hold
// fakes that behave as the stock node-reference ones do (0x239060 / 0x2390D0): the shield round's animation_model reaches
// its IFC's InitParam +0x1B0 before the IFC fires, and every case that would leave it valueless (the crash of test hub
// report #6: BarrierBullet01's ctor throws on it) is reported, never passed on as done.
#include "../src/ifc_model.h"
#include <windows.h>
#include <cstdio>
#include <cwchar>

namespace {
using namespace crew::ifcmodel;
int failures=0,checks=0;
void Check(bool pass,const char* what){++checks;if(!pass){++failures;std::printf("FAIL %s\n",what);}}

constexpr std::uint16_t kNodeRef=2;
std::int32_t memberIndex=7;    // where the fake SGO has animation_model (-1: it has none)
int finds=0,gets=0;
void* const kDocument=reinterpret_cast<void*>(0x1234500);

struct alignas(16) Variant { unsigned char value[16]; std::uint16_t alternative; unsigned char pad[14]; };
static_assert(offsetof(Variant,alternative)==kVariantAt);

void __fastcall FakeFind(const void* variant,void* request) {
    ++finds;
    auto r=static_cast<Find*>(request);
    (void)variant;
    r->index=std::wcscmp(r->name,L"animation_model")==0 ? memberIndex : -1;
}
void __fastcall FakeGet(const void* variant,void* request) {
    ++gets;
    auto r=static_cast<Get*>(request);
    auto out=static_cast<Variant*>(r->out);
    void* doc;std::memcpy(&doc,variant,8);
    std::memcpy(out->value,&doc,8);std::memcpy(out->value+8,&r->index,4);
    out->alternative=kNodeRef;
}

unsigned char* image=nullptr;
void Table(unsigned table,std::uint16_t alternative,VisitFn f){std::memcpy(image+table+alternative*8,&f,sizeof(f));}

struct Round {   // the DemoIndirectFire as EmcFire sees it after ShellMake: its SGO root, its IFC's model slot
    alignas(16) unsigned char obj[0x800]{};
    Round(std::uint16_t root,std::uint16_t slot) {
        Variant v{};std::memcpy(v.value,&kDocument,8);v.alternative=root;std::memcpy(obj+kDemoSgo,&v,sizeof(v));
        Variant s{};s.alternative=slot;std::memcpy(obj+0x170+kIfcModel,&s,sizeof(s));
    }
    unsigned char* Sgo(){return obj+kDemoSgo;}
    unsigned char* Slot(){return obj+0x170+kIfcModel;}
};

void Carried() {
    memberIndex=7;finds=gets=0;
    Round r(kNodeRef,kValueless);
    Check(CarryModel(image,r.Sgo(),r.Slot())==Carry::done,"the SGO's animation_model is carried");
    Check(AlternativeOf(r.Slot())==kNodeRef,"the IFC's InitParam +0x1B0 holds a node reference: the ctor's 0x6BB890 has its model");
    void* doc;std::int32_t idx;std::memcpy(&doc,r.Slot(),8);std::memcpy(&idx,r.Slot()+8,4);
    Check(doc==kDocument && idx==7,"...of the round's own SGO, its animation_model member");
    Check(finds==1 && gets==1,"one lookup by name, one copy, through the root's own alternative's visitors");
}

void NoMember() {
    memberIndex=-1;finds=gets=0;
    Round r(kNodeRef,kValueless);
    Check(CarryModel(image,r.Sgo(),r.Slot())==Carry::noMember,"an SGO without animation_model (an older install) is reported");
    Check(AlternativeOf(r.Slot())==kValueless && gets==0,"...and nothing is copied: EmcFire deletes the round before it fires");
}

void SlotTaken() {
    memberIndex=7;finds=gets=0;
    Round r(kNodeRef,kNodeRef);
    Check(CarryModel(image,r.Sgo(),r.Slot())==Carry::slotTaken && finds==0 && gets==0,"an IFC that already holds a model is left alone");
}

void NoSgo() {
    memberIndex=7;finds=gets=0;
    Round valueless(kValueless,kValueless),strange(5,kValueless);
    Check(CarryModel(image,valueless.Sgo(),valueless.Slot())==Carry::noSgo,"a valueless SGO root is refused");
    Check(CarryModel(image,strange.Sgo(),strange.Slot())==Carry::noSgo,"an alternative past the tables' 3 is refused (no stray call)");
    Check(finds==0 && gets==0,"...without calling any visitor");
}

void Layout() {
    Check(kIfcModel==0x1F0,"IFC +0x40 (the InitParam the factory gets, 0x2B9F55) + 0x1B0");
    Check(kVariantFind==0x179EBA8 && kVariantGet==0x179EAF0,"the DemoIndirectFire ctor's find / get tables (0x5B5723 / 0x5B5767)");
    Check(kDemoSgo==0x100,"the DemoIndirectFire's SGO root (0x5B56F9 lea rbx,[rdi+0x100])");
}
}  // namespace

int main() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image){std::printf("no memory\n");return 1;}
    Table(kVariantFind,kNodeRef,&FakeFind);Table(kVariantGet,kNodeRef,&FakeGet);
    Carried();NoMember();SlotTaken();NoSgo();Layout();
    std::printf("ifc_model: %d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
