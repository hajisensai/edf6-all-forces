// Real production selector on synthetic native boundaries; no game DLL execution.
#include "../src/enemy_control_walking.cpp"
#include <cstdio>
#include <string>
#include <vector>
namespace crew {
unsigned char* image=nullptr;bool networked=false,contextKnown=true,contextFault=false;
bool EnemyControlNetworkContext(const void*,bool* online) noexcept {
    if(contextFault)RaiseException(0xE0422003,0,0,nullptr);
    if(!contextKnown)return false;*online=networked;return true;
}
}
namespace crew::enemy_native {
constexpr NativeProfile fixtureProfiles[]={
    {"EDF6_Berserker_Large",0x17ADA78,0x343DD0,0x34ED80,1,79,0x31,Kind::walking,7,4},
    {"EDF6_Berserker_Middle",0x17AE088,0x343DD0,0x350BE0,2,79,0x31,Kind::walking,7,4},
    {"EDF6_Berserker_TypeA",0x17AE968,0x343DD0,0x3561D0,3,79,0x31,Kind::walking,7,4},
    {"EDF6_Berserker_TypeBommer",0x17AED50,0x343DD0,0x3586D0,4,79,0x31,Kind::walking,7,4},
    {"EDF6_Berserker_TypeGrapple",0x17AF200,0x343DD0,0x35B7E0,5,79,0x31,Kind::walking,7,4},
    {"EDF6_Berserker_TypeShooter",0x17AF9F8,0x343DD0,0x350BE0,6,79,0x31,Kind::walking,7,4}
};
const NativeProfile* Profiles(std::size_t* count) noexcept {*count=6;return fixtureProfiles;}
bool fixturePartsReady=true;
bool WalkingPartsControlReady() noexcept {return fixturePartsReady;}
bool fixtureRole=false,fixtureAuthority=true,fixtureQuiescing=false,fixtureCapacity=true,fixtureRestore=true;
unsigned queued=0,seeds=0,began=0,canceled=0,restoreCalls=0;bool reenterSeed=false,reenterResult=false;std::uint64_t queued3=0,queued4=0;
bool NativeActionRole(const void*,bool* authority) noexcept {*authority=fixtureAuthority;return fixtureRole;}
bool NativeActionQuiescing(const void*) noexcept {return fixtureQuiescing;}
bool InstallWalkingAcceptedActions() noexcept {return true;}
bool WalkingAcceptedCanQueue(const NativeLease&) noexcept {return fixtureCapacity;}
bool BeginWalkingAcceptedAction(const NativeLease&,unsigned,std::uint64_t,std::uint64_t,std::uint64_t) noexcept {++began;return fixtureCapacity;}
bool CancelWalkingAcceptedAction(const NativeLease&) noexcept {++canceled;return true;}
bool QueueWalkingAcceptedAction(const NativeLease&,unsigned,std::uint64_t,std::uint64_t a,std::uint64_t b) noexcept {++queued;queued3=a;queued4=b;return true;}
bool RestoreWalkingAcceptedActions(const NativeLease&) noexcept {++restoreCalls;return fixtureRestore;}
bool SetSeed(const NativeLease& lease,std::uint64_t seed) noexcept {
    ++seeds;
    if(reenterSeed){reenterSeed=false;reenterResult=ExecuteWalkingAction(lease,2,9999,9999);}
    Put<std::uint64_t>(lease.object,0x3E8,seed);Put<std::uint64_t>(lease.object,0x490,seed^0x11223344ull);return true;
}
}
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::walking_control;
struct Node {unsigned type=3;std::vector<unsigned> children;std::wstring text;};
std::vector<Node> nodes;std::vector<std::wstring> missingStates;alignas(16) unsigned char tree[0x60]{},body[0x3000]{},control[32]{},foreign[0x3000]{},casPlayers[0x160]{},casStates[64]{};
unsigned checks=0,failures=0,enters=0,lookups=0,consumed=999;bool fault=false,clearLease=false,foreignNested=false,restoreDuringEnter=false,restoreDuringResult=false;
NativeLease* mutableLease=nullptr;
void Check(bool value,const char* text,int line){++checks;if(!value){++failures;std::printf("FAIL %d %s\n",line,text);}}
#define CHECK(x) Check((x),#x,__LINE__)
void Jump(unsigned at,const void* target){auto* p=image+at;p[0]=0x48;p[1]=0xB8;std::memcpy(p+2,&target,8);p[10]=0xFF;p[11]=0xE0;}
unsigned __fastcall FakeType(const Handle* h){return h->tree==tree && h->index<nodes.size() ? nodes[h->index].type : 5;}
int __fastcall FakeCount(const Handle* h){return h->index<nodes.size() ? static_cast<int>(nodes[h->index].children.size()) : -1;}
unsigned __fastcall FakeChild(const Handle* h,unsigned i){return h->index<nodes.size() && i<nodes[h->index].children.size() ? nodes[h->index].children[i] : UINT32_MAX;}
const wchar_t* __fastcall FakeText(const Handle* h){return h->index<nodes.size() ? nodes[h->index].text.c_str() : nullptr;}
int __fastcall FakeState(void*,int,const wchar_t* name,bool){for(const auto& missing:missingStates)if(missing==name)return -1;return 1;}
Handle* __fastcall FakeLookup(const Handle*,Handle* out,const wchar_t*) {++lookups;*out=Handle{tree,0,0};return out;}
void __fastcall FakeEnter(void* object) {
    ++enters;auto* root=static_cast<unsigned char*>(object);root[0x12D0]=2;
    if(restoreDuringEnter && mutableLease)restoreDuringResult=RestoreWalkingControl(*mutableLease);
    const Handle settings=At<Handle>(root,0x17C8);
    Handle attacks{tree,FakeChild(&settings,1),0};const auto count=static_cast<unsigned>(FakeCount(&attacks));
    const auto next=At<std::uint64_t>(root,0x3E8)*0x5D588B656C078965ull+0x269EC3ull;Put<std::uint64_t>(root,0x3E8,next);
    const auto random=static_cast<unsigned>(((next>>32)*count)>>32);
    const Handle row{tree,FakeChild(&attacks,random),0};Put<Handle>(root,0x17D8,row);
    if(clearLease && mutableLease)*mutableLease={};
    Handle speed{};
    if(foreignNested)Choose(reinterpret_cast<const Handle*>(foreign+0x17C8),&speed,L"animation_speed");
    Choose(reinterpret_cast<const Handle*>(root+0x17C8),&speed,L"animation_speed");
    if(fault){fault=false;RaiseException(0xE0422002,0,0,nullptr);return;}
    const auto chosen=At<Handle>(root,0x17D8);consumed=chosen.index;
    Put<const void*>(root,0x18A8,image+0x33E124);Put<int>(root,0x18B0,0);Put<int>(casPlayers,0x104,1);
}
unsigned AddText(const wchar_t* text) {nodes.push_back(Node{1,{},text});return static_cast<unsigned>(nodes.size()-1);}
unsigned AddArray(const std::vector<unsigned>& children) {nodes.push_back(Node{3,children,{}});return static_cast<unsigned>(nodes.size()-1);}
NativeLease Lease(unsigned family=0,unsigned count=3) {
    std::memset(body,0,sizeof(body));std::memset(control,0,sizeof(control));nodes.clear();missingStates.clear();
    Put<const void*>(body,0xC30,casPlayers);Put<std::size_t>(body,0xC40,1);Put<int>(body,0xFD0,0);
    Put<const void*>(casPlayers,0xE0,casStates);Put<unsigned>(casStates,4,1);Put<int>(casStates,8,12);
    const unsigned zero=AddArray({});std::vector<unsigned> rows;
    for(unsigned i=0;i<count;++i) {
        std::wstring name=i==0 ? L"attack_shoot_both" : i==1 ? L"attack_shoot_lr" : i==2 ? L"attack_shoot_rl" : L"fixture_attack_"+std::to_wstring(i);
        rows.push_back(AddArray({AddText(name.c_str()),AddText((name+L"_end").c_str())}));
    }
    const unsigned actions=AddArray(rows);const unsigned settings=AddArray({zero,actions});
    Put<Handle>(body,0x17C8,Handle{tree,settings,0});Put<const void*>(body,0x30,control);
    Put<LONG>(control,8,1);Put<LONG>(control,12,1);body[0x12D0]=1;
    NativeLease lease{};lease.object=body;lease.control=control;lease.profile=fixtureProfiles+family;return lease;
}
void ReadyBody(){body[0x12D0]=1;Put<int>(body,0x12DC,0);Put<const void*>(body,0x18A8,nullptr);}
}
int main() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    for(const auto& print:prints)std::memcpy(image+print.at,print.bytes,16);
    const unsigned char call[]={0xE8,0x30,0x20,0xD9,0xFF};std::memcpy(image+kSelectionCall,call,5);
    const unsigned char rowStore[]={0x0F,0x11,0x87,0xD8,0x17,0x00,0x00};std::memcpy(image+0x345444,rowStore,7);
    image[0x53060]=0;CHECK(!InstallWalkingControl());CHECK(!redirected);
    for(const auto& print:prints)std::memcpy(image+print.at,print.bytes,16);
    CHECK(InstallWalkingControl());
    Jump(0x52E50,reinterpret_cast<const void*>(&FakeType));Jump(0x53060,reinterpret_cast<const void*>(&FakeCount));
    Jump(0x52DE0,reinterpret_cast<const void*>(&FakeChild));Jump(0x551E0,reinterpret_cast<const void*>(&FakeText));
    Jump(kLookup,reinterpret_cast<const void*>(&FakeLookup));Jump(kEnter,reinterpret_cast<const void*>(&FakeEnter));
    Jump(0x11664F0,reinterpret_cast<const void*>(&FakeState));
    FlushInstructionCache(GetCurrentProcess(),image,0x1800000);
    auto lease=Lease();NativeAbility menu[4]{};
    fixturePartsReady=false;unsigned unreadyCount=99;CHECK(!ReadWalkingActionCount(lease,&unreadyCount) && unreadyCount==0 && ListWalkingActionAbilities(lease,nullptr,0)==0);fixturePartsReady=true;
    CHECK(ListWalkingActionAbilities(lease,nullptr,0)==3);CHECK(ListWalkingActionAbilities(lease,menu,1)==3);
    CHECK(menu[0].kind==AbilityKind::walkingAction && menu[0].held==0 && menu[0].pressed==0 && menu[0].argument==0);
    CHECK(std::wcscmp(menu[0].name,L"双拳齐发")==0 && std::wcscmp(menu[0].action,L"attack_shoot_both")==0);
    std::uint64_t digest=0;CHECK(WalkingActionDefinition(lease,1,&digest) && digest!=0);
    const auto oldDigest=digest;const Handle settings=At<Handle>(body,0x17C8);Handle attacks{};CHECK(Child(settings,1,&attacks));Handle row{};CHECK(Child(attacks,1,&row));
    nodes[nodes[row.index].children[1]].text=L"changed_end";CHECK(WalkingActionDefinition(lease,1,&digest) && digest!=oldDigest);
    CHECK(!WalkingActionDefinition(lease,3,&digest));
    enemy_control::Ability state{};state.id=15;wcscpy_s(state.name,L"preserve");
    CHECK(ReadWalkingActionState(lease,1,&state) && state.state==enemy_control::AbilityState::native && !state.cooldownKnown);
    CHECK(state.id==15 && std::wcscmp(state.name,L"preserve")==0);
    CHECK(ExecuteWalkingAction(lease,1,1,7));CHECK(enters==1 && consumed==row.index && walking_control::selection==nullptr);
    CHECK(At<LONG>(control,8)==1 && At<LONG>(control,12)==2);
    CHECK(WalkingActionCommitted(lease,1));CHECK(ReadWalkingActionState(lease,1,&state) && state.state==enemy_control::AbilityState::active);
    CHECK(CommitWalkingAction(lease,1,1,7,7^0x11223344ull) && enters==1);
    CHECK(!CommitWalkingAction(lease,2,1,7,7^0x11223344ull) && enters==1);
    CHECK(!ExecuteWalkingAction(lease,0,2,8) && enters==1);
    ReadyBody();CHECK(ReadWalkingActionState(lease,1,&state) && state.state==enemy_control::AbilityState::native && !WalkingActionCommitted(lease,1));
    const auto beforeRetry3=At<std::uint64_t>(body,0x3E8),beforeRetry4=At<std::uint64_t>(body,0x490);const auto seedsBeforeRetry=seeds;
    CHECK(ExecuteWalkingAction(lease,1,1,7) && enters==1 && seeds==seedsBeforeRetry && At<std::uint64_t>(body,0x3E8)==beforeRetry3 && At<std::uint64_t>(body,0x490)==beforeRetry4);
    CHECK(!ExecuteWalkingAction(lease,1,1,99) && seeds==seedsBeforeRetry && At<std::uint64_t>(body,0x3E8)==beforeRetry3);
    CHECK(ExecuteWalkingAction(lease,2,2,8) && enters==2);
    Put<int>(casPlayers,0x104,99);CHECK(!WalkingActionCommitted(lease,2) && ReadWalkingActionState(lease,2,&state) && state.state==enemy_control::AbilityState::cooldown);Put<int>(casPlayers,0x104,1);
    Handle lookup{};Choose(reinterpret_cast<const Handle*>(body+0x17C8),&lookup,L"outside");CHECK(lookups==3);
    ReadyBody();foreignNested=true;CHECK(ExecuteWalkingAction(lease,0,3,9) && enters==3 && selection==nullptr);foreignNested=false;
    CHECK(RestoreWalkingControl(lease) && At<LONG>(control,12)==1);
    ReadyBody();CHECK(ExecuteWalkingAction(lease,0,1,9));CHECK(RestoreWalkingControl(lease));
    lease=Lease();CHECK(CommitWalkingAction(lease,1,1,31,41,0xA2) && body[0x1940]==0xA2);body[0x1940]=0xB3;
    CHECK(CommitWalkingAction(lease,1,1,31,41,0xA2) && body[0x1940]==0xB3);CHECK(!CommitWalkingAction(lease,1,1,31,41,0xA3) && body[0x1940]==0xB3);CHECK(RestoreWalkingControl(lease));
    lease=Lease(3,2);CHECK(ExecuteWalkingAction(lease,1,1,12));CHECK((At<unsigned>(body,0x380)&0x10000000u)!=0);CHECK(RestoreWalkingControl(lease));
    lease=Lease(1,1);Put<int>(body,0x12DC,1);CHECK(!WalkingActionReady(lease,0));ReadyBody();CHECK(WalkingActionReady(lease,0));CHECK(RestoreWalkingControl(lease));
    lease=Lease(4,3);Put<int>(body,0x12DC,2);CHECK(!WalkingActionReady(lease,0));ReadyBody();Put<std::size_t>(body,0x1820,1);CHECK(!WalkingActionReady(lease,0));Put<std::size_t>(body,0x1820,0);CHECK(WalkingActionReady(lease,0));CHECK(RestoreWalkingControl(lease));
    lease=Lease(0,128);std::vector<NativeAbility> all(128);CHECK(ListWalkingActionAbilities(lease,all.data(),128)==128 && all[127].argument==127);
    CHECK(ExecuteWalkingAction(lease,127,1,13) && WalkingActionCommitted(lease,127));CHECK(RestoreWalkingControl(lease));
    lease=Lease();crew::networked=true;const auto before=enters;const auto beforeSeeds=seeds;
    fixtureRole=false;CHECK(!ExecuteWalkingAction(lease,0,1,15) && enters==before && seeds==beforeSeeds);
    fixtureRole=true;fixtureAuthority=false;CHECK(ExecuteWalkingAction(lease,0,1,15) && enters==before && seeds==beforeSeeds && queued==0);
    fixtureAuthority=true;fixtureCapacity=false;CHECK(!ExecuteWalkingAction(lease,0,1,15) && enters==before && seeds==beforeSeeds);
    fixtureCapacity=true;CHECK(ExecuteWalkingAction(lease,0,1,15) && enters==before+1 && queued==1 && queued3==15 && queued4==(15^0x11223344ull));
    fixtureRestore=false;CHECK(!RestoreWalkingControl(lease) && At<LONG>(control,12)==2);
    fixtureRestore=true;CHECK(RestoreWalkingControl(lease) && At<LONG>(control,12)==1);crew::networked=false;
    lease=Lease();fixtureAuthority=false;const auto boundGuestBefore=enters;CHECK(ExecuteWalkingAction(lease,0,1,18) && enters==boundGuestBefore);fixtureRole=false;fixtureAuthority=true;
    contextFault=true;CHECK(!ExecuteWalkingAction(lease,0,1,18) && enters==boundGuestBefore);contextFault=false;
    contextKnown=false;CHECK(!ExecuteWalkingAction(lease,0,1,18) && enters==boundGuestBefore);contextKnown=true;
    lease=Lease();reenterSeed=true;const auto beforeReenter=enters;CHECK(ExecuteWalkingAction(lease,0,1,21) && !reenterResult && enters==beforeReenter+1);
    CHECK(!ExecuteWalkingAction(lease,2,9999,9999) && walking_control::Owner(lease,false)->sequence==1);CHECK(RestoreWalkingControl(lease));
    lease=Lease();mutableLease=&lease;restoreDuringEnter=true;const auto beforeNativeRestore=restoreCalls;CHECK(ExecuteWalkingAction(lease,0,1,22) && !restoreDuringResult && restoreCalls==beforeNativeRestore && WalkingActionCommitted(lease,0));restoreDuringEnter=false;
    CHECK(walking_control::Owner(lease,false)->sequence==1 && RestoreWalkingControl(lease));
    lease=Lease();fault=true;CHECK(!ExecuteWalkingAction(lease,1,1,16) && selection==nullptr && At<LONG>(control,8)==1);CHECK(RestoreWalkingControl(lease));
    lease=Lease();mutableLease=&lease;clearLease=true;CHECK(!ExecuteWalkingAction(lease,1,1,17) && selection==nullptr && At<LONG>(control,8)==1);clearLease=false;
    NativeLease original{};original.object=body;original.control=control;original.profile=fixtureProfiles;CHECK(RestoreWalkingControl(original));
    lease=Lease();missingStates={L"attack_shoot_both",L"attack_shoot_lr",L"attack_shoot_rl"};unsigned actualCount=99;
    CHECK(ReadWalkingActionCount(lease,&actualCount) && actualCount==0 && ListWalkingActionAbilities(lease,nullptr,0)==0 && !WalkingNativeMainAttackAvailable(lease));
    CHECK(!ExecuteWalkingAction(lease,0,1,24));missingStates={L"attack_shoot_lr"};
    CHECK(ReadWalkingActionCount(lease,&actualCount) && actualCount==2 && ListWalkingActionAbilities(lease,menu,4)==2 && menu[0].argument==0 && menu[1].argument==2);
    Put<std::size_t>(body,0xC40,0);actualCount=99;CHECK(!ReadWalkingActionCount(lease,&actualCount) && actualCount==0);CHECK(RestoreWalkingControl(lease));
    lease=Lease();const auto selectedRow=At<Handle>(body,0x17C8);nodes[nodes[selectedRow.index].children[1]].children.push_back(UINT32_MAX);
    CHECK(ListWalkingActionAbilities(lease,nullptr,0)==0);
    CHECK(RestoreWalkingControl(lease));
    VirtualFree(image,0,MEM_RELEASE);std::printf("walking selector: %u checks, %u failures\n",checks,failures);return failures ? 1 : 0;
}
