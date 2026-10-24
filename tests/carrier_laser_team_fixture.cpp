// Execute the production carrier-laser state machine with a small native ABI.
// No EDF.dll, game resources, hooks or live session are required.
#include "../src/carrierlaser.cpp"
#include <cstdio>
#include <algorithm>
#include <limits>

namespace laser_fixture {
struct Object {
    alignas(16) unsigned char bytes[0x1200]{};
    alignas(16) unsigned char control[0x20]{};
};
Object hulls[3]{},objects[4]{},human{},beams[8]{};
alignas(16) unsigned char manager[0x50]{},rows[64*0x38]{};
int relations[64][64]{};
crew::Config config{};
unsigned carrierCount=0,objectCount=0,beamCount=0,laserCount=0,deletes=0,flies=0,deckCalls=0;
int visitedTeams[8]{};
unsigned visits=0;
bool authority=true;
bool patchAllowed=false,registrationAllowed=true;
unsigned registrations=0,nativeAiCalls=0,seenFrames=0;
crew::enemy_native::OwnsAiFn registeredOwns=nullptr;
crew::enemy_native::InputFn registeredControlled=nullptr;

unsigned char* player=nullptr;
ULONGLONG milliseconds=100000,frame=1;
int checks=0,failures=0;
void Check(bool value,const char* why,int line) {
    ++checks;if(!value){++failures;std::fprintf(stderr,"line %d: %s\n",line,why);}
}
#define CHECK(value) laser_fixture::Check(!!(value),#value,__LINE__)
void Position(Object& o,float x,float y,float z) {
    edf::Put<float>(o.bytes,edf::kPosition,x);edf::Put<float>(o.bytes,edf::kPosition+4,y);edf::Put<float>(o.bytes,edf::kPosition+8,z);
}
void Make(Object& o,int team,unsigned vtable,float x=0.0f,float y=0.0f,float z=0.0f) {
    o=Object{};
    edf::Put<const void*>(o.bytes,0,crew::image+vtable);
    edf::Put<void*>(o.bytes,edf::kSelf,o.bytes);edf::Put<void*>(o.bytes,edf::kSelfCtrl,o.control);
    edf::Put<int>(o.bytes,edf::kTeam,team);
    edf::Put<float>(o.bytes,edf::kMatrix+0x14,1.0f);
    edf::Put<float>(o.bytes,crew::kHp,1000.0f);edf::Put<float>(o.bytes,crew::kHpMax,1000.0f);
    Position(o,x,y,z);
}
Object& Carrier(int team,float x,float z=0.0f) {
    CHECK(carrierCount<3);Object& o=hulls[carrierCount++];Make(o,team,0,x,0.0f,z);return o;
}
Object& Ship(int team,float x=0.0f,float y=0.0f,float z=0.0f) {
    CHECK(objectCount<4);Object& o=objects[objectCount++];Make(o,team,crew::kShipVtable,x,y,z);return o;
}
void Hostile(int from,int to,bool enemy=true) {relations[from][to]=enemy ? crew::kEnemyRelation : 1;}
void Reset() {
    crew::ResetLaser();
    config=crew::Config{};config.enabled=config.carrierLaser=true;config.carrierLaserBreak=0.5f;
    carrierCount=objectCount=beamCount=laserCount=deletes=flies=deckCalls=visits=0;
    authority=true;player=nullptr;milliseconds=100000;++frame;
    patchAllowed=false;registrationAllowed=true;registrations=nativeAiCalls=seenFrames=0;registeredOwns=nullptr;registeredControlled=nullptr;
    std::memset(relations,0,sizeof(relations));std::memset(rows,0,sizeof(rows));
    for(unsigned i=0;i<64;++i)edf::Put<void*>(rows,i*crew::kTeamStride+crew::kTeamRelation,relations[i]);
    edf::Put<void*>(manager,crew::kTeamRows,rows);edf::Put<void*>(crew::image,crew::kTeams,manager);
    edf::Put<void*>(crew::image,crew::kObjectMgr,manager);
    crew::sigOk=crew::preloaded=crew::flyOk=true;crew::broken=crew::aiDead=false;
}
void Jump(unsigned rva,const void* target) {
    unsigned char code[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto address=reinterpret_cast<std::uintptr_t>(target);std::memcpy(code+2,&address,8);
    std::memcpy(crew::image+rva,code,sizeof(code));
}
unsigned char* __fastcall Create(void*,const float*,const wchar_t* sgo,void*) noexcept {
    CHECK(beamCount<8);if(beamCount>=8)return nullptr;
    Object& beam=beams[beamCount++];Make(beam,0,crew::kDemoVtable);
    if(std::wcscmp(sgo,crew::kLaserSgo)==0)++laserCount;
    return beam.bytes;
}
void __fastcall Delete(void* object) noexcept {
    ++deletes;static_cast<unsigned char*>(object)[crew::kObjFlags]|=crew::kObjDeleted;
}
void __fastcall Owner(void*,const crew::Weak* weak) noexcept {CHECK(weak && weak->object && weak->ctrl);}
void __fastcall Damage(void*,float) noexcept {}
void __fastcall NativeShipAi(void*,const void*) {++nativeAiCalls;}
void __fastcall FlyTo(void*,const float*,float,float speed) noexcept {++flies;CHECK(speed>=0.0f && speed<=crew::kFlySpeed);}
int CarrierIndex(const crew::ObjRef& ref) {
    for(unsigned i=0;i<carrierCount;++i)if(ref.Is(hulls[i].bytes))return static_cast<int>(i);
    return -1;
}
crew::Ship* ActiveShip() {
    auto* state=crew::Slot(objects[0].bytes,milliseconds);
    CHECK(state);if(!state)return nullptr;
    state->seen=milliseconds;CHECK(crew::Start(*state,crew::Live(),milliseconds));
    CHECK(state->phase==crew::Phase::moving && state->steer);return state;
}
void SetupActive() {
    Reset();Carrier(1,100.0f);Ship(2);Hostile(1,2);Hostile(2,1);
}
// A relation can change through either object's native team or through the directed matrix.
void ChangeSide(unsigned change) {
    if(change==0)edf::Put<int>(hulls[0].bytes,edf::kTeam,2);
    else if(change==1)edf::Put<int>(objects[0].bytes,edf::kTeam,1);
    else Hostile(2,1,false);
}
}
namespace edf {
bool Readable(const void* pointer,std::size_t,bool) noexcept {return pointer!=nullptr;}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept {return true;}
bool PatchVtableSlot(void** slot,void* expected,void* next) noexcept {
    if(!laser_fixture::patchAllowed || !slot || *slot!=expected)return false;
    *slot=next;return true;
}
}
namespace crew::enemy_native {
bool RegisterControlledAiExtension(std::uint32_t vt,std::uint32_t original,OwnsAiFn owns,InputFn controlled,SkillSeedFn skillSeed) noexcept {
    CHECK(vt==crew::kShipVtable && original==crew::kShipAi && owns && controlled && !skillSeed);
    ++laser_fixture::registrations;laser_fixture::registeredOwns=owns;laser_fixture::registeredControlled=controlled;
    return laser_fixture::registrationAllowed;
}
}
namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept {return laser_fixture::config;}
ULONGLONG GameMs() noexcept {return laser_fixture::milliseconds;}
ULONGLONG GameFrame() noexcept {return laser_fixture::frame;}
void SeeFrame(const void*) noexcept {++laser_fixture::seenFrames;}
void Log(const char*,...) noexcept {}
bool IsOnlineAuthority(const void*) noexcept {return laser_fixture::authority;}
unsigned char* PlayerHuman() noexcept {return laser_fixture::player;}
int SubCarriers(SubView* out,int max) noexcept {
    const unsigned count=(std::min)(laser_fixture::carrierCount,static_cast<unsigned>(max));
    for(unsigned i=0;i<count;++i) {
        const auto& object=laser_fixture::hulls[i];out[i]=SubView{};out[i].ref=ObjRef::Of(object.bytes);
        std::memcpy(out[i].pos,object.bytes+kPosition,12);out[i].team=At<int>(object.bytes,kTeam);
    }
    return static_cast<int>(count);
}
bool SubSpot(const ObjRef& sub,float along,float over,float* out) noexcept {
    const int i=laser_fixture::CarrierIndex(sub);if(i<0)return false;
    const auto* o=laser_fixture::hulls[i].bytes;
    out[0]=At<float>(o,kPosition);out[1]=At<float>(o,kPosition+4)+over;out[2]=At<float>(o,kPosition+8)+along;return true;
}
bool SubDeckUnder(const ObjRef& sub,const float* from,float* out) noexcept {
    const int i=laser_fixture::CarrierIndex(sub);if(i<0)return false;
    out[0]=from[0];out[1]=At<float>(laser_fixture::hulls[i].bytes,kPosition+4)+10.0f;out[2]=from[2];return true;
}
bool SubDeckFor(const ObjRef& sub,const float*,float* out) noexcept {
    ++laser_fixture::deckCalls;
    const int i=laser_fixture::CarrierIndex(sub);if(i<0)return false;
    std::memcpy(out,laser_fixture::hulls[i].bytes+kPosition,12);out[1]+=10.0f;return true;
}
bool VisitEnemiesOf(std::int32_t team,EnemyVisitor visit,void* ctx) noexcept {
    CHECK(laser_fixture::visits<8);if(laser_fixture::visits<8)laser_fixture::visitedTeams[laser_fixture::visits]=team;
    ++laser_fixture::visits;
    if(team<0 || team>=64)return false;
    for(unsigned i=0;i<laser_fixture::objectCount;++i) {
        const auto* o=laser_fixture::objects[i].bytes;const int other=At<int>(o,kTeam);
        if(other<0 || other>=64 || laser_fixture::relations[team][other]!=kEnemyRelation)continue;
        const float* p=reinterpret_cast<const float*>(o+kPosition);
        visit(ctx,o,p);visit(ctx,o,p); // Two lock points must still produce one ship slot.
    }
    return true;
}
}
namespace laser_fixture {
void TestDirectedRelation() {
    Reset();auto& ship=Ship(7);Hostile(7,2);
    CHECK(crew::ShipEnemy(ship.bytes,2));CHECK(!crew::ShipEnemy(ship.bytes,7));
    Hostile(7,2,false);Hostile(2,7);CHECK(!crew::ShipEnemy(ship.bytes,2));
    Hostile(7,7);CHECK(!crew::ShipEnemy(ship.bytes,7));
    CHECK(!crew::ShipEnemy(ship.bytes,-1));CHECK(!crew::ShipEnemy(ship.bytes,64));
    edf::Put<int>(ship.bytes,edf::kTeam,-1);CHECK(!crew::ShipEnemy(ship.bytes,2));
    edf::Put<int>(ship.bytes,edf::kTeam,64);CHECK(!crew::ShipEnemy(ship.bytes,2));
    edf::Put<int>(ship.bytes,edf::kTeam,7);Hostile(7,2);
    edf::Put<void*>(crew::image,crew::kTeams,nullptr);CHECK(!crew::ShipEnemy(ship.bytes,2));
}
void TestMixedTeamDiscovery() {
    Reset();Carrier(1,0);Carrier(1,200);Carrier(2,400);
    Ship(0,100);Ship(1,110);Ship(2,120);auto& decoy=Ship(0,130);edf::Put<const void*>(decoy.bytes,0,crew::image+1);
    Hostile(1,0);Hostile(2,0);Hostile(1,2);Hostile(2,1);
    crew::Tick();
    CHECK(visits==2 && visitedTeams[0]==1 && visitedTeams[1]==2);
    unsigned slots=0;for(const auto& s:crew::ships)if(s.ship)++slots;
    CHECK(slots==3);
    for(unsigned i=0;i<3;++i) {
        unsigned matches=0;for(const auto& s:crew::ships)if(s.ship==objects[i].bytes){++matches;CHECK(s.seen==milliseconds);}
        CHECK(matches==1);
    }
    CHECK(beamCount==0 && flies==0);
}
void TestStartHostileOnly() {
    Reset();auto& ship=Ship(7);Carrier(7,5);Carrier(8,10);auto& enemy=Carrier(2,100);Hostile(7,2);Hostile(7,7);
    crew::Ship state{};state.ship=ship.bytes;state.ctrl=ship.control;
    CHECK(crew::Start(state,crew::Live(),milliseconds));
    CHECK(state.carrier.Is(enemy.bytes) && state.phase==crew::Phase::moving && state.steer);
    CHECK(beamCount==0);
    crew::Ship refused{};refused.ship=ship.bytes;refused.ctrl=ship.control;
    authority=false;CHECK(!crew::Start(refused,crew::Live(),milliseconds));CHECK(refused.phase==crew::Phase::idle);
    authority=true;Hostile(7,2,false);Hostile(2,7);CHECK(!crew::Start(refused,crew::Live(),milliseconds));
    Hostile(7,2);Position(enemy,1600.0f,0,0);CHECK(!crew::Start(refused,crew::Live(),milliseconds));
    CHECK(!refused.carrier && beamCount==0);
}
void TestTargetHostileOnly() {
    Reset();auto& ship=Ship(7);Carrier(7,2);Carrier(8,5);auto& enemy=Carrier(2,100);Hostile(7,2);
    Make(human,7,0,1);player=human.bytes;
    const float from[3]={0,0,0};float aim[3]{};bool atPlayer=true;
    CHECK(crew::Target(ship.bytes,from,aim,&atPlayer));CHECK(!atPlayer && aim[0]==100 && aim[1]==10 && deckCalls==1);
    Make(human,2,0,20);deckCalls=0;
    CHECK(crew::Target(ship.bytes,from,aim,&atPlayer));CHECK(atPlayer && aim[0]==20 && aim[1]==1.2f && deckCalls==0);
    human.bytes[edf::kDead]=1;CHECK(crew::Target(ship.bytes,from,aim,&atPlayer));CHECK(!atPlayer && aim[0]==100);
    human.bytes[edf::kDead]=0;Position(human,401,0,0);
    CHECK(crew::Target(ship.bytes,from,aim,&atPlayer));CHECK(!atPlayer && aim[0]==100);
    Position(human,std::numeric_limits<float>::quiet_NaN(),0,0);
    CHECK(crew::Target(ship.bytes,from,aim,&atPlayer));CHECK(!atPlayer && aim[0]==100);
    player=nullptr;edf::Put<int>(hulls[0].bytes,edf::kTeam,2);Position(hulls[0],250,0,0);
    CHECK(crew::Target(ship.bytes,from,aim,&atPlayer));CHECK(!atPlayer && aim[0]==100);
    Position(enemy,300,0,0);CHECK(crew::Target(ship.bytes,from,aim,&atPlayer));CHECK(!atPlayer && aim[0]==250);
    Hostile(7,2,false);Hostile(2,7);deckCalls=0;
    CHECK(!crew::Target(ship.bytes,from,aim,&atPlayer));CHECK(!atPlayer && deckCalls==0);
}
void TestMovingInterruptedOnSideChange() {
    for(unsigned change=0;change<3;++change) {
        SetupActive();auto* state=ActiveShip();if(!state)continue;
        ChangeSide(change);++milliseconds;crew::Tick();
        CHECK(state->ship==objects[0].bytes && state->phase==crew::Phase::idle);
        CHECK(!state->steer && !state->down && state->nextAt>milliseconds);
        CHECK(beamCount==0 && flies==0);
    }
}
void TestChargingInterruptedOnSideChange() {
    for(unsigned direct=0;direct<2;++direct)for(unsigned change=0;change<3;++change) {
        SetupActive();auto* state=ActiveShip();if(!state)continue;
        state->down=true;crew::Charge(*state,milliseconds);
        CHECK(state->phase==crew::Phase::charging && state->sight && beamCount==1 && deletes==0);
        // The next step would fire without revalidating the carrier's current relation.
        state->phaseAt=milliseconds-crew::kChargeMs;
        ChangeSide(change);++milliseconds;
        if(direct)crew::Charging(*state,milliseconds);else crew::Tick();
        CHECK(state->phase==crew::Phase::idle && !state->steer && !state->down && !state->sight);
        CHECK(deletes==1 && beamCount==1 && laserCount==0);
    }
}
void TestControlledAiCompatibility() {
    Reset();patchAllowed=true;
    auto** slot=reinterpret_cast<void**>(crew::image+crew::kShipVtable+crew::kShipAiSlot*8);
    *slot=crew::image+crew::kShipAi;
    Jump(crew::kShipAi,reinterpret_cast<const void*>(&NativeShipAi));
    CHECK(crew::InstallFlight() && registrations==1);
    CHECK(*slot==reinterpret_cast<void*>(&crew::ShipAiHook));
    CHECK(registeredOwns && registeredOwns(*slot));
    CHECK(registeredOwns && !registeredOwns(reinterpret_cast<const void*>(&NativeShipAi)));
    CHECK(registeredControlled && registeredControlled(objects[0].bytes,nullptr)==0);
    CHECK(seenFrames==1 && nativeAiCalls==0);
    crew::ShipAiHook(objects[0].bytes,nullptr);
    CHECK(nativeAiCalls==1 && seenFrames==2);
    SetupActive();auto* oldCharge=ActiveShip();
    if(oldCharge) {
        oldCharge->down=true;crew::Charge(*oldCharge,milliseconds);
        CHECK(oldCharge->sight && beamCount==1);
        edf::Put<const void*>(objects[0].bytes,0,crew::image+0x2000000); // real control owns a per-object clone
        ++frame;++milliseconds;
        CHECK(crew::ControlledShipAi(objects[0].bytes,nullptr)==0);
        CHECK(!oldCharge->ship && deletes==1 && !laserCount && !nativeAiCalls && !flies);
    }
    // A foreign chain remains available to the laser, but is never certified
    // as the original native AI for enemy-control admission.
    Reset();patchAllowed=true;*slot=reinterpret_cast<void*>(&NativeShipAi);
    CHECK(crew::InstallFlight() && registrations==0);
    CHECK(!crew::OwnsControlledShipAi(*slot));
    Reset();patchAllowed=false;*slot=crew::image+crew::kShipAi;
    CHECK(!crew::InstallFlight() && registrations==0);
}
void TestFlyRechecksCurrentRelation() {
    for(unsigned change=0;change<3;++change) {
        SetupActive();auto* state=ActiveShip();if(!state)continue;
        crew::Fly(objects[0].bytes,*state,milliseconds);CHECK(flies==1 && state->flownAt==milliseconds);
        ChangeSide(change);++milliseconds;
        crew::Fly(objects[0].bytes,*state,milliseconds);CHECK(flies==1 && state->flownAt==milliseconds-1);
    }
}
}
int main() {
    using namespace laser_fixture;
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    CHECK(crew::image);if(!crew::image)return 1;
    Jump(crew::kCreateObject,reinterpret_cast<const void*>(&Create));Jump(crew::kDelete,reinterpret_cast<const void*>(&Delete));
    Jump(crew::kIfcOwner,reinterpret_cast<const void*>(&Owner));Jump(crew::kIfcDamage,reinterpret_cast<const void*>(&Damage));
    Jump(crew::kFlyTo,reinterpret_cast<const void*>(&FlyTo));FlushInstructionCache(GetCurrentProcess(),crew::image,0x2100000);
    TestDirectedRelation();TestMixedTeamDiscovery();TestStartHostileOnly();TestTargetHostileOnly();
    TestMovingInterruptedOnSideChange();TestChargingInterruptedOnSideChange();TestFlyRechecksCurrentRelation();
    TestControlledAiCompatibility();
    crew::ResetLaser();VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("carrier_laser_team_fixture: %d checks, %d failed\n",checks,failures);return failures ? 1 : 0;
}
