// Ordinary GameObjects: deliberately no Vehicle fields, seats or vehicle vtables.
#include "../src/enemy_control_pvp_tree.h"
#include <cstdio>

namespace tree_fixture {
struct Node { unsigned char bytes[0x18]{}; };
struct Object {
    alignas(16) unsigned char bytes[0x560]{};
    alignas(16) unsigned char control[0x20]{};
    Node head{};
    unsigned char padding[8]{};
};
Object objects[8]{};
Node nodes[16]{};
struct Region { const void* pointer=nullptr;std::size_t bytes=0; };
Region regions[48]{};
unsigned regionCount=0,nodeCount=0;
int checks=0,failures=0;
const void* denied=nullptr;
const void* watched=nullptr;
unsigned watchedReads=0;
const void* mutateObject=nullptr;
unsigned mutateReads=0;
const void* replacementControl=nullptr;
void Check(bool value,const char* why,int line) {
    ++checks;if(!value){++failures;std::fprintf(stderr,"line %d: %s\n",line,why);}
}
#define CHECK(value) tree_fixture::Check(!!(value),#value,__LINE__)
void RegionOf(const void* p,std::size_t bytes) {regions[regionCount++]={p,bytes};}
void Reset() {
    regionCount=nodeCount=watchedReads=mutateReads=0;denied=watched=mutateObject=replacementControl=nullptr;
    for(auto& o:objects) {
        o=Object{};
        edf::Put<void*>(o.bytes,edf::kSelf,o.bytes);edf::Put<void*>(o.bytes,edf::kSelfCtrl,o.control);
        edf::Put<LONG>(o.control,8,1);edf::Put<LONG>(o.control,12,1);
        edf::Put<void*>(o.bytes,0x550,o.head.bytes);
        edf::Put<void*>(o.head.bytes,0,o.head.bytes);edf::Put<void*>(o.head.bytes,8,o.head.bytes);
        RegionOf(o.bytes,sizeof(o.bytes));RegionOf(o.control,sizeof(o.control));RegionOf(o.head.bytes,sizeof(o.head.bytes));
    }
    for(auto& node:nodes){node=Node{};RegionOf(node.bytes,sizeof(node.bytes));}
}
crew::ObjRef Ref(unsigned i) {return {objects[i].bytes,objects[i].control};}
Node& Link(unsigned parent,unsigned child) {
    auto& node=nodes[nodeCount++];auto* head=objects[parent].head.bytes;
    auto* tail=edf::At<unsigned char*>(head,8);
    edf::Put<void*>(node.bytes,0,head);edf::Put<void*>(node.bytes,8,tail);
    edf::Put<void*>(node.bytes,0x10,objects[child].bytes);
    edf::Put<std::size_t>(objects[parent].bytes,0x558,edf::At<std::size_t>(objects[parent].bytes,0x558)+1);
    edf::Put<void*>(tail,0,node.bytes);edf::Put<void*>(head,8,node.bytes);return node;
}
bool Collect(unsigned capacity,unsigned* count,crew::ObjRef* out) {
    return crew::enemy_pvp_native::CollectTree(Ref(0),out,capacity,count);
}
void Reject(unsigned capacity=8) {
    crew::ObjRef out[8]{};unsigned count=99;CHECK(!Collect(capacity,&count,out));CHECK(count==0);
}
void TestArgumentsAndRoot() {
    Reset();crew::ObjRef out[8]{};unsigned count=99;
    CHECK(!crew::enemy_pvp_native::CollectTree(Ref(0),out,8,nullptr));
    CHECK(!crew::enemy_pvp_native::CollectTree(Ref(0),nullptr,8,&count));CHECK(count==0);
    count=99;CHECK(!Collect(0,&count,out));CHECK(count==0);
    count=99;CHECK(!crew::enemy_pvp_native::CollectTree({},out,8,&count));CHECK(count==0);
    count=99;CHECK(!crew::enemy_pvp_native::CollectTree({objects[0].bytes,nullptr},out,8,&count));CHECK(count==0);
    CHECK(Collect(1,&count,out));CHECK(count==1 && out[0].obj==Ref(0).obj && out[0].ctrl==Ref(0).ctrl);
    CHECK(edf::At<LONG>(objects[0].control,8)==1 && edf::At<LONG>(objects[0].control,12)==1);
    // Dead control must reject before even querying the old object's readable range.
    edf::Put<LONG>(objects[0].control,8,0);watched=objects[0].bytes;
    Reject();CHECK(watchedReads==0);
    Reset();denied=objects[0].control;Reject();
    Reset();edf::Put<void*>(objects[0].bytes,edf::kSelfCtrl,objects[7].control);Reject();
    Reset();objects[0].bytes[0x18]=4;Reject();
    Reset();denied=objects[0].bytes;Reject();
}
void TestWholeTreeAndCapacity() {
    Reset();Link(0,1);Link(0,2);Link(1,3);Link(1,4);Link(2,5);
    crew::ObjRef out[8]{};unsigned count=0;CHECK(Collect(6,&count,out));CHECK(count==6);
    for(unsigned i=0;i<count;++i) {
        CHECK(out[i].obj==objects[i].bytes && out[i].ctrl==objects[i].control);
        CHECK(edf::At<LONG>(objects[i].control,8)==1 && edf::At<LONG>(objects[i].control,12)==1);
    }
    Reject(5);Reject(1);
    Reset();for(unsigned i=1;i<8;++i)Link(i-1,i);
    CHECK(Collect(8,&count,out));CHECK(count==8);Reject(7);
    // A valid GameObject may be dead but retain native child ownership; restoring
    // its team's tree must not rely on a vehicle's health or seat layout.
    objects[3].bytes[edf::kDead]=1;CHECK(Collect(8,&count,out));CHECK(count==8);
}
void TestChildIdentity() {
    Reset();auto& node=Link(0,1);edf::Put<LONG>(objects[1].control,8,0);watched=objects[1].bytes;
    Reject();CHECK(watchedReads==0);
    Reset();Link(0,1);denied=objects[1].control;Reject();
    Reset();Link(0,1);denied=objects[1].bytes;Reject();
    Reset();Link(0,1);objects[1].bytes[0x18]=4;Reject();
    Reset();auto& nullObject=Link(0,1);edf::Put<void*>(nullObject.bytes,0x10,nullptr);Reject();
    Reset();Link(0,1);edf::Put<void*>(objects[1].bytes,edf::kSelfCtrl,nullptr);Reject();
    Reset();Link(0,1);edf::Put<LONG>(objects[1].control,8,-1);Reject();
    Reset();Link(0,1);Link(0,1);mutateObject=objects[1].bytes;replacementControl=objects[2].control;Reject();CHECK(mutateReads>=3);
    (void)node;
}
void TestDuplicateMembersAndCycles() {
    Reset();Link(0,1);Link(0,1);Reject();
    Reset();Link(0,1);Link(0,2);Link(1,3);Link(2,3);Reject();
    Reset();Link(0,0);Reject();
    Reset();Link(0,1);Link(1,2);Link(2,0);Reject();
    Reset();auto& node=Link(0,1);edf::Put<void*>(node.bytes,0,node.bytes);Reject();
    Reset();auto& first=Link(0,1);auto& last=Link(0,2);edf::Put<void*>(last.bytes,0,first.bytes);Reject();
}
void TestMalformedLists() {
    Reset();edf::Put<void*>(objects[0].bytes,0x550,nullptr);Reject();
    Reset();denied=objects[0].head.bytes;Reject();
    Reset();edf::Put<void*>(objects[0].head.bytes,0,nullptr);Reject();
    Reset();auto& node=Link(0,1);denied=node.bytes;Reject();
    Reset();auto& noNext=Link(0,1);edf::Put<void*>(noNext.bytes,0,nullptr);Reject();
    Reset();auto& wrongPrevious=Link(0,1);edf::Put<void*>(wrongPrevious.bytes,8,objects[7].head.bytes);Reject();
    Reset();Link(0,1);edf::Put<void*>(objects[0].head.bytes,8,objects[0].head.bytes);Reject();
    Reset();edf::Put<void*>(objects[0].head.bytes,8,nodes[15].bytes);Reject();
    Reset();Link(0,1);Link(1,2);edf::Put<void*>(objects[2].bytes,0x550,nullptr);Reject();
    Reset();edf::Put<std::size_t>(objects[0].bytes,0x558,1);Reject();
    Reset();Link(0,1);edf::Put<std::size_t>(objects[0].bytes,0x558,0);Reject();
    Reset();Link(0,1);edf::Put<std::size_t>(objects[0].bytes,0x558,2);Reject();
    Reset();edf::Put<std::size_t>(objects[0].bytes,0x558,static_cast<std::size_t>(-1));Reject();
}
}
namespace edf {
bool Readable(const void* pointer,std::size_t bytes,bool) noexcept {
    using namespace tree_fixture;
    if(pointer==watched && bytes>edf::kSelfCtrl+sizeof(void*))++watchedReads;
    if(pointer==mutateObject && ++mutateReads==3)
        edf::Put<const void*>(const_cast<void*>(pointer),edf::kSelfCtrl,replacementControl);
    if(!pointer || pointer==denied)return false;
    const auto start=reinterpret_cast<std::uintptr_t>(pointer);
    for(unsigned i=0;i<regionCount;++i) {
        const auto base=reinterpret_cast<std::uintptr_t>(regions[i].pointer);
        if(start>=base && start-base<=regions[i].bytes && bytes<=regions[i].bytes-(start-base))return true;
    }
    return false;
}
}
int main() {
    tree_fixture::TestArgumentsAndRoot();tree_fixture::TestWholeTreeAndCapacity();tree_fixture::TestChildIdentity();
    tree_fixture::TestDuplicateMembersAndCycles();tree_fixture::TestMalformedLists();
    std::printf("enemy_control_pvp_tree_test: %d checks, %d failed\n",tree_fixture::checks,tree_fixture::failures);
    return tree_fixture::failures ? 1 : 0;
}
