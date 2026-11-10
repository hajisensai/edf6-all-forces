// 测试站 #41 (mission_crew_radar.h): a mission AI vehicle's seated crew is no radar dot of its own. Runs the production
// filter on a synthetic MSVC std::set laid out as the stock team manager's (0x82B220) and a native float4 row vector.
// No EDF.dll, no game.
#include "../src/mission_crew_radar.cpp"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
}
namespace {
using namespace crew::crewradar;
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::printf("FAIL: %s\n",what);std::exit(1);}
}
// _Tree_node<void*>: +0 left, +8 parent, +0x10 right, +0x18 color, +0x19 isnil, +0x20 value.
struct Node { Node* left; Node* parent; Node* right; unsigned char color,isnil,pad[6]; const void* value; };
static_assert(offsetof(Node,right)==kRight && offsetof(Node,isnil)==kIsNil &&
              offsetof(Node,value)==kValue,"the MSVC node layout");
// A balanced search tree over `objects` (their order is the set's order), its head as MSVC keeps it.
struct Tree {
    Node head{};std::vector<Node> nodes;
    Node* Build(int lo,int hi,Node* parent) {
        if(lo>hi)return &head;
        const int mid=(lo+hi)/2;Node* n=&nodes[static_cast<std::size_t>(mid)];
        n->parent=parent;n->left=Build(lo,mid-1,n);n->right=Build(mid+1,hi,n);return n;
    }
    explicit Tree(const std::vector<const void*>& objects):nodes(objects.size()) {
        for(std::size_t i=0;i<objects.size();++i)nodes[i]=Node{nullptr,nullptr,nullptr,0,0,{},objects[i]};
        head.isnil=1;
        if(objects.empty()){head.left=head.parent=head.right=&head;return;}
        head.parent=Build(0,static_cast<int>(objects.size())-1,&head);
        head.left=&nodes.front();head.right=&nodes.back();
    }
};
// The native vector the radar fills: +8 data, +0x10 capacity, +0x18 size; the rows are float4 positions.
struct List { void* vtable; float* data; std::uint64_t capacity,size; };
static_assert(offsetof(List,data)==kData && offsetof(List,size)==kSize,"the native row vector");
// The rows 0x82B220 makes: one per node in order, here the node's index as x.
std::vector<float> Fill(List& list,std::size_t n) {
    std::vector<float> rows(n*4);
    for(std::size_t i=0;i<n;++i){rows[i*4]=static_cast<float>(i);rows[i*4+3]=1;}
    list={nullptr,nullptr,n,n};
    return rows;
}
std::vector<int> Xs(const List& list) {
    std::vector<int> out;
    for(std::uint64_t i=0;i<list.size;++i)out.push_back(static_cast<int>(list.data[i*4]));
    return out;
}
}

int main() {
    using namespace crew;using namespace crew::crewradar;
    unsigned char soldiers[9][16]{};
    std::vector<const void*> objects;
    for(auto& s:soldiers)objects.push_back(s);
    Tree tree(objects);
    List list{};
    // The successor walk visits every node once, in order (both branches of 0x82B2A2: down the right, up the parents).
    {
        std::vector<const void*> seen;
        for(const void* n=tree.head.left;!Nil(n);n=Next(n))seen.push_back(Field<const void*>(n,kValue));
        Check(seen==objects,"the walk is the set's order, every node once");
    }
    // Nothing published: the row is left as the radar made it.
    auto rows=Fill(list,9);list.data=rows.data();
    MissionCrewRadarFilterRows(&tree.head,&list);
    Check(list.size==9 && Xs(list)==std::vector<int>({0,1,2,3,4,5,6,7,8}),"no crew aboard: every dot kept");
    // Three soldiers seated (the first, a middle one, the last): their rows dropped, the rest in order.
    const void* seated[]={soldiers[0],soldiers[4],soldiers[8]};
    MissionCrewRadarPublish(seated,3);
    Check(MissionCrewRadarHidden(soldiers[4]) && !MissionCrewRadarHidden(soldiers[3]) && !MissionCrewRadarHidden(nullptr),
          "the published set");
    rows=Fill(list,9);list.data=rows.data();
    MissionCrewRadarFilterRows(&tree.head,&list);
    Check(list.size==6 && Xs(list)==std::vector<int>({1,2,3,5,6,7}),"seated crew: no dot of their own, every other dot kept");
    Check(list.capacity==9,"the vector's capacity untouched");
    // A set changed since the walk (one row fewer than nodes): not matched row for row, left as it is.
    rows=Fill(list,8);list.data=rows.data();
    MissionCrewRadarFilterRows(&tree.head,&list);
    Check(list.size==8,"node count is not the row count: the row untouched");
    rows=Fill(list,10);list.data=rows.data();
    MissionCrewRadarFilterRows(&tree.head,&list);
    Check(list.size==10,"more rows than nodes: untouched");
    // Every soldier of the team seated: an empty row.
    MissionCrewRadarPublish(objects.data(),static_cast<unsigned>(objects.size()));
    rows=Fill(list,9);list.data=rows.data();
    MissionCrewRadarFilterRows(&tree.head,&list);
    Check(list.size==0,"all seated: no soldier dot");
    // An empty set (head only) with an empty row.
    {
        Tree none({});List empty{};
        MissionCrewRadarFilterRows(&none.head,&empty);
        Check(empty.size==0,"an empty team");
    }
    // Dismounted / dead / vehicle gone: the next frame publishes the set without them, their dots back.
    const void* one[]={soldiers[2]};
    MissionCrewRadarPublish(one,1);
    rows=Fill(list,9);list.data=rows.data();
    MissionCrewRadarFilterRows(&tree.head,&list);
    Check(list.size==8 && Xs(list)==std::vector<int>({0,1,3,4,5,6,7,8}),"the others off their vehicle: dots back");
    MissionCrewRadarPublish(nullptr,0);
    Check(!MissionCrewRadarHidden(soldiers[2]),"cleared");
    rows=Fill(list,9);list.data=rows.data();
    MissionCrewRadarFilterRows(&tree.head,&list);
    Check(list.size==9,"cleared: every dot back");

    // Through the team manager as 0x82B23C..0x82B24B reads it, on a private image.
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,kTeamManager+0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(image!=nullptr,"private image");
    MissionCrewRadarPublish(seated,3);
    unsigned char manager[0x40]{};
    struct TeamRow { Node* head; unsigned char rest[0x30]; } teams[7]{};
    static_assert(sizeof(TeamRow)==kTeamRow,"a team row");
    teams[2].head=&tree.head;
    void* teamsAt=teams;std::memcpy(manager+kTeams,&teamsAt,8);
    void* managerAt=manager;std::memcpy(image+kTeamManager,&managerAt,8);
    rows=Fill(list,9);list.data=rows.data();
    MissionCrewRadarFilter(&list,2);
    Check(list.size==9,"the walk's bytes not checked yet: the radar left as it is");
    Check(!InstallMissionCrewRadar(),"other bytes at 0x82B220: refused");
    std::memcpy(image+kWalk,kWalkCode,sizeof(kWalkCode));
    Check(InstallMissionCrewRadar() && InstallMissionCrewRadar(),"the stock walk: installed (once)");
    MissionCrewRadarFilter(&list,2);
    Check(list.size==6 && Xs(list)==std::vector<int>({1,2,3,5,6,7}),"team 2's row through the manager");
    rows=Fill(list,9);list.data=rows.data();
    MissionCrewRadarFilter(&list,-1);
    Check(list.size==9,"no team: untouched");
    managerAt=nullptr;std::memcpy(image+kTeamManager,&managerAt,8);
    MissionCrewRadarFilter(&list,2);
    Check(list.size==9,"no team manager (between missions): untouched");
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("mission_crew_radar: %d checks passed\n",checks);
    return 0;
}
