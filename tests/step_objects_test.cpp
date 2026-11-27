// stepobj (common/step_objects.cpp) against a stand-in System: its two std::lists of {object, control block}, its
// CRITICAL_SECTION, its scene pointer. No game is loaded.
#include <Windows.h>
#include "edf/step_objects.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
int failures=0,checks=0;
void Check(bool ok,const char* what) { ++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);} }

struct Node { Node* next; Node* prev; void* object; void* ctrl; };
static_assert(offsetof(Node,object)==stepobj::kNodeObject && offsetof(Node,ctrl)==stepobj::kNodeCtrl,"node layout");
struct List {
    Node head{};
    std::uint64_t size=0;
    List() { head.next=head.prev=&head; }
    void Add(Node* n) { n->prev=head.prev;n->next=&head;head.prev->next=n;head.prev=n;++size; }
    void Remove(Node* n) { n->prev->next=n->next;n->next->prev=n->prev;--size; }
};
alignas(16) unsigned char sysmem[0x1000];
List objects,added;
void Bind() {
    Node* h=&objects.head;std::memcpy(sysmem+stepobj::kObjects,&h,8);std::memcpy(sysmem+stepobj::kObjects+8,&objects.size,8);
    Node* a=&added.head;std::memcpy(sysmem+stepobj::kAdded,&a,8);std::memcpy(sysmem+stepobj::kAdded+8,&added.size,8);
}
CRITICAL_SECTION& Lock() { return *reinterpret_cast<CRITICAL_SECTION*>(sysmem+stepobj::kLock); }
void* sysPtr=sysmem;
void* scene=reinterpret_cast<void*>(0x5000);
void SetScene(void* s) { std::memcpy(sysmem+stepobj::kScene,&s,8); }

struct Obj { alignas(16) unsigned char bytes[0x40]; std::int32_t ctrl[4]; Node node; };
void Make(Obj& o,List& l) {
    std::memset(o.bytes,0,sizeof(o.bytes));
    o.ctrl[0]=o.ctrl[1]=0;o.ctrl[2]=1;o.ctrl[3]=1;   // +8 use count 1
    o.node={nullptr,nullptr,o.bytes,o.ctrl};
    l.Add(&o.node);Bind();
}
void Frame() { stepobj::Released(true); }
}  // namespace

int main() {
    InitializeCriticalSection(&Lock());
    SetScene(scene);Bind();
    Obj a{},b{},c{},d{};
    Make(a,objects);Make(b,objects);Make(c,added);

    Check(!stepobj::Has(a.bytes) && !stepobj::Period(),"not installed: nothing");
    stepobj::Install(const_cast<const void* const*>(&sysPtr));
    Check(!stepobj::Has(a.bytes) && !stepobj::Period(),"installed, no frame's release yet: nothing");
    Frame();
    Check(stepobj::Period()!=0,"a frame's release opens a period on its thread");
    Check(stepobj::Has(a.bytes) && stepobj::Has(b.bytes) && stepobj::Trusted(a.bytes),"the scene list's objects");
    Check(stepobj::Has(c.bytes),"the added list's objects");
    Check(!stepobj::Has(d.bytes) && !stepobj::Has(nullptr) && !stepobj::Trusted(a.bytes+8),"anything else: no (an inner pointer too)");
    const auto p1=stepobj::Period();
    Check(stepobj::Period()==p1,"the period holds between releases");

    // Added after the copy: not in it until the next period.
    Make(d,objects);
    Check(!stepobj::Has(d.bytes),"added after the period's copy: no");
    Frame();
    Check(stepobj::Period()!=p1,"the next release: a new period");
    Check(stepobj::Has(d.bytes),"the next period's copy has it");

    // Released (gone from the list): only the release removes from it, and the period moves with it.
    objects.Remove(&b.node);Bind();
    Frame();
    Check(!stepobj::Has(b.bytes) && !stepobj::Trusted(b.bytes),"the period after its release: no");

    // Deleted (+0x18 bit 2): trusted (still allocated), not in the scene.
    a.bytes[stepobj::kObjectFlags]|=stepobj::kRemoved;
    Check(!stepobj::Has(a.bytes) && stepobj::Trusted(a.bytes),"Deleted: Trusted yes, Has no");
    a.bytes[stepobj::kObjectFlags]=0;
    // Use count 0: not in the scene.
    a.ctrl[2]=0;
    Check(!stepobj::Has(a.bytes),"use count 0: no");
    a.ctrl[2]=1;
    Check(stepobj::Has(a.bytes),"use count back: yes");

    // Another thread: no.
    bool other=true;std::uint64_t otherPeriod=1;
    std::thread t([&]{ other=stepobj::Has(a.bytes) || stepobj::Trusted(a.bytes);otherPeriod=stepobj::Period(); });
    t.join();
    Check(!other && otherPeriod==0,"not the game thread: no");

    // Another System or scene: no.
    alignas(16) static unsigned char otherSys[0x1000];
    std::memcpy(otherSys,sysmem,sizeof(sysmem));
    InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(otherSys+stepobj::kLock));
    sysPtr=otherSys;
    Check(!stepobj::Has(a.bytes),"the System pointer changed within the period: no");
    sysPtr=sysmem;
    Check(stepobj::Has(a.bytes),"the same System again: yes");
    SetScene(reinterpret_cast<void*>(0x6000));
    Check(!stepobj::Has(a.bytes),"the scene changed within the period: no");
    SetScene(scene);
    // The list shrank under the copy (a teardown that bypassed the release): no.
    objects.Remove(&a.node);Bind();
    Check(!stepobj::Has(d.bytes),"the scene list shorter than the copy's: no");
    objects.Add(&a.node);Bind();
    Frame();
    Check(stepobj::Has(a.bytes) && stepobj::Has(d.bytes),"a new period: yes again");

    // The scene ends: nothing until the next frame's release.
    stepobj::Close();
    Check(!stepobj::Has(a.bytes) && !stepobj::Period(),"closed: nothing");
    stepobj::Released(false);
    Check(!stepobj::Has(a.bytes),"a flush's release does not reopen");
    Frame();
    Check(stepobj::Has(a.bytes),"the next frame's release reopens");

    // While a release or a teardown runs: nothing (its objects' destructors may ask).
    stepobj::Releasing();
    Check(!stepobj::Has(a.bytes) && !stepobj::Period(),"inside a release: nothing");
    Frame();
    Check(stepobj::Has(a.bytes),"after it: yes");
    stepobj::Releasing();stepobj::Released(false);stepobj::Close();   // the teardown hook's order
    Check(!stepobj::Has(a.bytes),"after a teardown: nothing");
    Frame();
    Check(stepobj::Has(a.bytes),"the next frame's release reopens after a teardown");

    // A flush's release ends the period too.
    const auto p2=stepobj::Period();
    stepobj::Released(false);
    Check(stepobj::Period()!=p2,"a flush's release: a new period");

    // Another thread holds the lists: no copy this period (never waits), the next period copies.
    {
        HANDLE held=CreateEventW(nullptr,TRUE,FALSE,nullptr),go=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::thread holder([&]{ EnterCriticalSection(&Lock());SetEvent(held);WaitForSingleObject(go,INFINITE);LeaveCriticalSection(&Lock()); });
        WaitForSingleObject(held,INFINITE);
        Frame();
        Check(!stepobj::Has(a.bytes),"the lists' lock held elsewhere: no copy, no");
        SetEvent(go);holder.join();
        Check(!stepobj::Has(a.bytes),"still this period: no copy is made later in it");
        Frame();
        Check(stepobj::Has(a.bytes),"the next period copies");
        CloseHandle(held);CloseHandle(go);
    }

    // A battle's objects.
    static Obj many[5000];
    for(Obj& o:many)Make(o,objects);
    Frame();
    bool all=true;
    for(Obj& o:many)all=all && stepobj::Has(o.bytes);
    Check(all,"5,000 objects, all found");
    const stepobj::Stats s=stepobj::TakeStats();
    Check(s.copies>0 && s.copied>=5000,"the copies are counted");

    stepobj::Install(nullptr);
    Check(!stepobj::Has(a.bytes),"uninstalled: nothing");
    std::printf("step_objects_test: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
