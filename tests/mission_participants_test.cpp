#include "../src/mission_participants.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
bool IsSoldierClass(const void* o) noexcept { return At<unsigned>(o,0)==0x534F4C44; }
}
namespace {
int checks=0;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
}
int main() {
    using namespace crew;using namespace mission_participants;
    unsigned char actor[5][0x1EE0]{},user[5][0x50]{},ctrl[5][16]{};
    void* puids[8]{};Visitor visitor{kVisitorVtable,puids,8,0,false,{},{}};
    for(unsigned i=0;i<5;++i) {
        Put<unsigned>(actor[i],0,0x534F4C44);Put<void*>(actor[i],0x1ED0,user[i]);Put<void*>(actor[i],0x1ED8,ctrl[i]);
        Put<LONG>(ctrl[i],8,1);Put<int>(user[i],0x48,static_cast<int>(i));Put<void*>(user[i],0x18,user[i]);
    }
    Put<unsigned char>(actor[1],kDead,1); // a downed, revivable player remains in the current world
    Put<int>(actor[1],kTeam,5);Put<int>(actor[2],kTeam,1);
    Visit(&visitor,actor[0]);Visit(&visitor,actor[1]);Visit(&visitor,actor[2]);
    Check(!visitor.failed && visitor.count==3,"dead players and actors in any team are retained");
    Visit(&visitor,actor[1]);Check(visitor.count==3,"same actor visited twice is counted once");
    Put<void*>(user[3],0x18,user[0]);Visit(&visitor,actor[3]);
    Check(visitor.count==4 && puids[3]==puids[0],"split-screen actor count retains duplicate PUID");
    Put<int>(user[4],0x48,-1);Visit(&visitor,actor[4]);Check(visitor.count==4,"lobby-only user excluded");
    Put<int>(user[4],0x48,4);Put<unsigned char>(actor[4],0x18,4);Visit(&visitor,actor[4]);
    Check(visitor.count==4,"scene-deleted actor excluded independently of death");
    Put<unsigned char>(actor[4],0x18,0);Put<void*>(actor[4],0x1ED0,nullptr);Visit(&visitor,actor[4]);
    Check(visitor.count==4,"NPC without a User excluded");
    Put<void*>(actor[4],0x1ED0,user[4]);Put<LONG>(ctrl[4],8,0);Visit(&visitor,actor[4]);
    Check(visitor.failed,"broken bound User invalidates snapshot");
    Visitor limited{kVisitorVtable,puids,1,0,false,{},{}};Visit(&limited,actor[0]);Visit(&limited,actor[1]);
    Check(limited.failed,"insufficient capacity does not silently truncate quorum");
    Visitor collision{kVisitorVtable,puids,8,0,false,{},{}};Visit(&collision,actor[0]);Put<int>(user[1],0x48,0);Visit(&collision,actor[1]);
    Check(collision.failed,"duplicate mission indices invalidate ambiguous roster");
    Visitor wrongRange{kVisitorVtable,puids,8,0,false,{},{}};
    Put<int>(user[1],0x48,9);Visit(&wrongRange,actor[0]);Visit(&wrongRange,actor[1]);
    Check(wrongRange.count==2 && !IndicesInRange(wrongRange,2),"expected two actors cannot seal indices zero and nine");
    wrongRange.indices[1]=1;
    Check(IndicesInRange(wrongRange,2),"unique in-range indices cover the expected world when counts match");
    Put<int>(user[1],0x48,1);Put<void*>(actor[0],kSelfCtrl,ctrl[0]);Put<void*>(actor[1],kSelfCtrl,ctrl[1]);
    const ObjRef fresh[]={ObjRef::Of(actor[0]),ObjRef::Of(actor[1])};
    Visitor matches{kVisitorVtable,puids,8,0,false,{},{},fresh,2};Visit(&matches,actor[0]);Visit(&matches,actor[1]);
    Check(!matches.failed && matches.count==2,"every current-world index matches its newly constructed actor identity");
    ObjRef stale[]={fresh[0],ObjRef{actor[1],ctrl[2]}};
    Visitor oldScene{kVisitorVtable,puids,8,0,false,{},{},stale,2};Visit(&oldScene,actor[0]);Visit(&oldScene,actor[1]);
    Check(oldScene.failed,"same player pointer with previous-scene control block cannot seal new world");
    stale[1]=ObjRef{};
    Visitor missing{kVisitorVtable,puids,8,0,false,{},{},stale,2};Visit(&missing,actor[0]);Visit(&missing,actor[1]);
    Check(missing.failed,"player not observed successfully created in this epoch cannot seal");
    Check(!MissionParticipantCreationsMatch(nullptr,2),"missing creation ledger rejected");
    unsigned count=9,expected=9;
    Check(!ReadMissionParticipants(nullptr,8,&count,&expected) && !count && !expected,"failure clears output counts");
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x20C0000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(image!=nullptr,"private native-location image");
    std::memcpy(image+kLocationSetter+0x1A,kLocationLoad,sizeof(kLocationLoad));
    std::memcpy(image+kLocationSetter+0x66,kLocationStore,sizeof(kLocationStore));
    unsigned char network[0x1650]{};Put<void*>(image,kNetworkManager,network);
    for(unsigned phase=0;phase<=5;++phase) {
        Put<unsigned>(network,0x1640,phase);unsigned observed=99;
        Check(ReadNativeMissionLocation(&observed) && observed==phase,"native boot/lobby/room/playing/loading state read exactly");
    }
    Put<LONG>(network,0x1644,-1);unsigned observed=99;
    Check(!ReadNativeMissionLocation(&observed) && observed==99,"in-progress native state write is not advertised");
    Put<LONG>(network,0x1644,0);Put<unsigned>(network,0x1640,6);
    Check(!ReadNativeMissionLocation(&observed),"unknown location cannot be treated as lobby");
    image[kLocationSetter+0x66]^=1;
    Check(!ReadNativeMissionLocation(&observed),"unsupported setter layout fails closed");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("mission_participants_test: %d checks passed\n",checks);
}
