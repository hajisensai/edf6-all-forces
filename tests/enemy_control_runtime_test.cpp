// Executes the production controller ownership, queue and restoration code.
#include "../src/enemy_control.cpp"
#include <cstdio>
#include <limits>
namespace crew {
unsigned char* image=nullptr;
Config fixtureConfig{};
ULONGLONG fixtureTime=1000,fixtureFrame=1;
bool fixtureOnline=false,fixtureRestoreFails=false;
bool fixtureCapacityReady=true;
bool fixtureGuideReady=true,fixtureGuideRestoreFails=false;unsigned fixtureRayMode=0;const unsigned char* fixtureMapOwner=nullptr;HWND fixtureInputWindow=nullptr;
bool fixturePendingVisible=false,fixtureNetworkIds=false,fixtureNetFailed=false;
bool fixtureActions=false,fixtureActionQuiesced=false,fixtureActionRestoreFails=false,fixtureTargetRestoreFails=false,fixtureFocusClearFails=false,fixtureRunFails=false;
unsigned fixtureActionPending=0,fixtureBefore=0,fixtureAfter=0,fixtureActionApplied=0;
std::vector<enemy_radar::View> fixtureRadar;
enemy_target::Selection fixtureFocus{};
Snapshot fixturePending{};unsigned fixturePendingCancels=0;
enemy_control_protocol::Phase fixturePendingPhase=enemy_control_protocol::Phase::preparing;
unsigned fixturePressed=0,fixtureHeld=0,fixtureCommands=0,fixtureUpdates=0;
std::vector<std::uint64_t> commandSequences;
const Config& Cfg() noexcept {return fixtureConfig;}
ULONGLONG GameMs() noexcept {return fixtureTime;}
ULONGLONG GameFrame() noexcept {return fixtureFrame;}
bool GamePaused() noexcept {return false;}
bool EnemyControlLocalCapacityReady() noexcept {return fixtureCapacityReady;}
void Log(const char*,...) noexcept {}
bool IsSoldierClass(const void*) noexcept {return true;}
bool HumanOnFoot(const unsigned char*) noexcept {return true;}
bool MapHoldsKeys() noexcept {return fixtureMapOwner!=nullptr;}
bool MapHoldsHumanKeys(const unsigned char* human) noexcept {return fixtureMapOwner && fixtureMapOwner==human;}
bool MapPadState(std::uint16_t*,std::uint8_t*,std::uint8_t*) noexcept {return false;}
float MapRay(const float* from,const float* to,float* out) noexcept {
    if(!fixtureRayMode)return -1;
    const float fraction=fixtureRayMode==1 ? 0.0f : 0.025f;
    for(unsigned i=0;i<3;++i)out[i]=from[i]+(to[i]-from[i])*fraction;
    return fraction;
}
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out) noexcept {
    if(!fixtureNetworkIds)return false;std::memset(out,0,32);out[0]=object[0x20];return true;
}
bool PlayerRequesterOf(const ObjRef& who,unsigned char out[32]) noexcept {   // the owner's (player_requester.h)
    return ReadNativeObjectId(static_cast<const unsigned char*>(who.obj),out);
}
bool EnemyControlNativeNetworkPoseReady(const enemy_native::NativeLease&) noexcept {return true;}
bool EnemyControlWeaponAimReady(const enemy_native::NativeProfile*) noexcept {return true;}
bool InstallEnemyControlUnitGuide() noexcept {return true;}
bool EnemyControlUnitGuideReady(const enemy_native::NativeProfile*) noexcept {return fixtureGuideReady;}
bool RestoreEnemyControlUnitGuide(const void*,const void*) noexcept {return !fixtureGuideRestoreFails;}
bool EnemyControlNativePvpHooks(const enemy_native::NativeLease&,controlled_pvp::Hooks*) noexcept {return false;}
bool ReadPvpStatus(PvpStatus* out) noexcept {if(out)*out={};return true;}
int PvpSupportTeam(const char*) noexcept {return 0;}
namespace controlled_pvp {
bool Probe(const ObjRef&,const Hooks&) noexcept {return true;}
bool Acquire(const ObjRef&,const char*,const Hooks&,Lease* out) noexcept {if(out)*out={};return true;}
bool Frame(const Lease&) noexcept {return true;}
bool Restore(Lease* value) noexcept {if(value)*value={};return true;}
bool ReadStatus(const Lease&,Status*) noexcept {return false;}
}
bool ReadEnemyControlNetworkStatus(const unsigned char* source,EnemyControlNetworkStatus* out) noexcept {
    if(!fixtureOnline)return false;
    if(fixturePendingVisible && !std::memcmp(source,fixturePending.owner,32)) {
        *out={};out->online=true;out->ready=true;out->request=42;out->profileHash=fixturePending.profileHash;
        out->phase=fixturePendingPhase;out->result=fixturePendingPhase==enemy_control_protocol::Phase::closed ?
            (fixtureNetFailed ? enemy_control_protocol::Result::TimedOut : enemy_control_protocol::Result::Complete) : enemy_control_protocol::Result::Pending;
        std::memcpy(out->source,fixturePending.owner,32);std::memcpy(out->target,fixturePending.target,32);return true;
    }
    for(const auto& session:sessions)if(session.snapshot.lease) {
        *out={};out->online=true;out->ready=true;out->request=1;out->lease=session.snapshot.lease;
        out->profileHash=session.snapshot.profileHash;out->phase=enemy_control_protocol::Phase::beginning;
        std::memcpy(out->source,session.snapshot.owner,32);std::memcpy(out->target,session.snapshot.target,32);return true;
    }
    return false;
}
bool IsOnlineAuthority(const void*) noexcept {return true;}
void EnemyControlRadarPublish(const enemy_radar::View* views,unsigned count) noexcept {fixtureRadar.assign(views,views+count);}
namespace enemy_target {
bool Install() noexcept {return true;}
bool Pick(const ObjRef&,const ObjRef&,const Ray&,Selection* out) noexcept {if(out)*out=fixtureFocus;return true;}
bool Resolve(const ObjRef&,const ObjRef&,const unsigned char* id,const Ray&,Selection* out) noexcept {
    if(out)*out={};if(std::memcmp(id,fixtureFocus.nativeId,32))return false;if(out)*out=fixtureFocus;return true;
}
bool Normalize(const ObjRef&,const ObjRef&,unsigned char* id,const Ray&,Selection* out) noexcept {
    if(out)*out={};if(std::memcmp(id,fixtureFocus.nativeId,32))std::memset(id,0,32);else if(out)*out=fixtureFocus;return true;
}
bool TryAssign(Selection& next,const Selection& source) noexcept {next=source;return true;}
bool TryClear(Selection& next) noexcept {if(fixtureFocusClearFails)return false;next={};return true;}
void Assign(Selection& next,const Selection& source) noexcept {TryAssign(next,source);}
void Clear(Selection& next) noexcept {TryClear(next);}
bool CopyLock(const Selection& value,void* out) noexcept {
    if(!value.lock.obj || !value.lock.ctrl)return false;std::memcpy(out,&value.lock,16);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(const_cast<void*>(value.lock.ctrl))+3);return true;
}
bool Begin(const ObjRef& root,NativeLease* out) noexcept {out->root=root;out->active=true;return true;}
bool Apply(NativeLease& lease,const Selection* selection) noexcept {lease.applied=selection ? selection->actor : ObjRef{};return lease.active;}
bool Restore(NativeLease& lease) noexcept {if(fixtureTargetRestoreFails)return false;lease={};return true;}
}
namespace edf41c {bool AimPoint(const void*,float*) noexcept {return false;}}
namespace enemy_native {
const NativeProfile fixtureProfile{"FixtureNative",1,1,1,123,45,0x10,Kind::insect,7,4};
// A soldier family (enemy_control_binding_order.h): its catalogue lists 跳跃 / 掩体 before its attacks.
const NativeProfile fixtureSoldier{"Humanoid_Basic",2,2,2,321,45,0x1F9,Kind::humanoid,7,4};
const NativeProfile* fixtureActive=&fixtureProfile;
const NativeProfile* Profile(const void*) noexcept {return fixtureActive;}
bool InstallDeiroiControl() noexcept {return true;}
bool InstallHumanoidControl() noexcept {return true;}
bool InstallWalkingControl() noexcept {return true;}
bool InstallWalkingGunControl() noexcept {return true;}
bool InstallTimeShipControl() noexcept {return true;}
bool RestoreWalkingControl(const NativeLease&) noexcept {return true;}
bool RestoreWalkingGunControl(const NativeLease&) noexcept {return true;}
bool RestoreTimeShipControl(const NativeLease&) noexcept {return true;}
bool NeedsNativeActions(const NativeLease&) noexcept {return fixtureActions;}
bool BeforeNativeActionInput(const NativeLease&,bool,Input*) noexcept {++fixtureBefore;return true;}
bool AfterNativeActionInput(const NativeLease&) noexcept {++fixtureAfter;return true;}
bool CaptureNativeAction(const NativeLease&,void* bytes,std::size_t cap,std::size_t* used) noexcept {
    *used=0;if(fixtureActionPending){if(cap<4)return false;std::memcpy(bytes,&fixtureActionPending,4);*used=4;fixtureActionPending=0;}return true;
}
enemy_control::ActionResult ApplyNativeAction(const NativeLease&,const void*,std::size_t) noexcept {++fixtureActionApplied;return enemy_control::ActionResult::applied;}
bool QuiesceNativeActions(const NativeLease&) noexcept {fixtureActionQuiesced=true;return true;}
bool NativeActionsEmpty(const NativeLease&) noexcept {return !fixtureActionPending;}
bool AbortNativeActions(const NativeLease&) noexcept {fixtureActionPending=0;fixtureActionQuiesced=true;return true;}
bool RestoreNativeActions(const NativeLease&) noexcept {return !fixtureActionPending && !fixtureActionRestoreFails;}
bool UpdateDeiroiAim(const NativeLease&,float,float) noexcept {return true;}
bool RestoreDeiroiControl(const NativeLease&) noexcept {return true;}
bool PoseRequired(const NativeLease&) noexcept {return false;}
bool PoseReady(const NativeLease&) noexcept {return false;}
bool CapturePose(const NativeLease&,void*,std::size_t,std::size_t*) noexcept {return false;}
bool ApplyPose(NativeLease&,const void*,std::size_t) noexcept {return false;}
bool RestorePose(NativeLease&) noexcept {return true;}
bool WorldMoveToPad(const NativeLease&,const float*,float,Input*) noexcept {return true;}
bool AimToPad(const NativeLease&,float,float,Input*) noexcept {return true;}
bool RunControlledAiExtension(const NativeLease&,void*,std::uint64_t*) noexcept {return false;}
bool Capture(void* object,NativeLease* out) noexcept {
    *out={};out->object=static_cast<unsigned char*>(object);out->profile=fixtureActive;out->control=At<const void*>(object,0x30);return true;
}
bool Restore(NativeLease* lease) noexcept {if(fixtureRestoreFails)return false;*lease={};return true;}
bool RunInput(NativeLease& lease,const Input& input,InputFn original,void* dt,std::uint64_t* out) noexcept {
    fixtureHeld=input.held;fixturePressed+=input.pressed ? 1u : 0u;
    if(fixtureRunFails)return false;
    const auto result=original(lease.object,dt);if(out)*out=result;return true;
}
bool RunHumanoidInput(NativeLease& lease,const Input& input,InputFn original,void* dt,std::uint64_t,std::uint64_t* out) noexcept {
    return RunInput(lease,input,original,dt,out);
}
bool ReadAbilityState(const NativeLease&,const NativeAbility&,enemy_control::Ability*) noexcept {return true;}
unsigned Abilities(const NativeLease&,NativeAbility* out,unsigned cap) noexcept {
    if(fixtureActive==&fixtureSoldier) {   // the native order: pad buttons 0, 3, 4, 5, 6, then a weapon
        const NativeAbility soldier[]={
            {"native_action_0",L"跳跃",0x1,0x1},{"native_action_3",L"进入／退出掩体",0x8,0x8},
            {"native_action_4",L"举枪瞄准",0x10,0x10},{"native_action_5",L"装备武器齐射",0x30,0x20},
            {"native_action_6",L"翻滚回避",0x40,0x40},{"fixture_gun",L"武器",0x30,0x30,AbilityKind::humanoidWeapon,nullptr,0}
        };
        for(unsigned i=0;out && i<cap && i<6;++i)out[i]=soldier[i];return 6;
    }
    const NativeAbility list[]={
        {"fixture_pad",L"原生持续攻击",0x10,0x10},
        {"fixture_command",L"原生独立技能",0,0,AbilityKind::objectAction,nullptr,7}
    };
    for(unsigned i=0;out && i<cap && i<2;++i)out[i]=list[i];return 2;
}
bool ExecuteAbility(const NativeLease&,const NativeAbility&,std::uint64_t sequence,std::uint64_t) noexcept {
    ++fixtureCommands;commandSequences.push_back(sequence);return true;
}
}
}
namespace {
int checks=0,failures=0;
void Check(bool pass,const char* label){++checks;if(!pass){++failures;std::printf("FAIL %s\n",label);}}
void* originalTable[46]{};
unsigned char owner1[0x3000]{},owner2[0x3000]{},ownerCtrl1[32]{},ownerCtrl2[32]{};
// Controller objects (human+0x340): four 0xA80 records, the pad-path flag at +0x2D68 (enemy_control_pad.h).
alignas(16) unsigned char pad[0x2E00]{},pad2[0x2E00]{};
std::uint64_t __fastcall NativeUpdate(void*,void*) {++crew::fixtureUpdates;return 77;}
// `localPad` the pad bound to the player (+0x338: P1 0, P2 1), `record` its class's key record (+0xD40: Ranger 0 .. Fencer 3).
void Human(unsigned char* object,unsigned char* control,int localPad,int record,unsigned char* controller=pad) {
    std::memset(object,0,0x3000);std::memset(control,0,32);
    crew::Put<const void*>(object,0x30,control);crew::Put<LONG>(control,8,1);crew::Put<LONG>(control,12,1);
    object[0x20]=static_cast<unsigned char>(localPad+1);
    object[crew::kHumanPlayer]=1;crew::Put<const void*>(object,crew::kHumanPad,controller);crew::Put<int>(object,0xD40,record);crew::Put<int>(object,0x338,localPad);
}
void Enemy(unsigned char* object,unsigned char* control) {
    std::memset(object,0,0x3000);std::memset(control,0,32);
    object[0x20]=3;
    crew::Put<const void*>(object,0,originalTable+1);crew::Put<const void*>(object,0x30,control);
    crew::Put<LONG>(control,8,1);crew::Put<LONG>(control,12,1);
    crew::Put<float>(object,0x2F4,100.0f);crew::Put<float>(object,0x2F8,75.0f);
    float* matrix=reinterpret_cast<float*>(object+0x60);matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
}
void NativeStep(unsigned char* object) {
    const auto table=crew::At<void* const*>(object,0);
    reinterpret_cast<crew::enemy_native::InputFn>(table[4])(object,nullptr);
}
int fixtureHeldKey=0;
SHORT WINAPI FixtureKeys(int key) {return fixtureHeldKey && key==fixtureHeldKey ? static_cast<SHORT>(0x8000) : 0;}
HWND WINAPI FixtureWindow() {return crew::fixtureInputWindow;}
bool Online() noexcept {return crew::fixtureOnline;}
bool PendingRequest(const crew::enemy_control::Snapshot& snapshot) noexcept {
    crew::fixturePending=snapshot;crew::fixturePendingVisible=true;
    crew::fixturePendingPhase=enemy_control_protocol::Phase::preparing;return true;
}
bool PendingStop(const crew::enemy_control::Snapshot& snapshot) noexcept {
    if(snapshot.revision!=crew::fixturePending.revision || snapshot.ownerHumanKey!=crew::fixturePending.ownerHumanKey)return false;
    ++crew::fixturePendingCancels;crew::fixturePendingPhase=enemy_control_protocol::Phase::restoring;return true;
}
}
// The user (2026-10-21): 「我怎么左键往右走，右键往后走」. The left button was the catalogue's first entry, a
// soldier's 跳跃; the right one 进入／退出掩体. The keys now follow the player's: fire, then aim, then the
// other attacks, the movement actions last.
void SoldierKeys() {
    using namespace crew;using namespace crew::enemy_control;
    auto* owner=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    auto* target=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    unsigned char ownerControl[32]{},targetControl[32]{};Human(owner,ownerControl,0,0);Enemy(target,targetControl);
    enemy_native::fixtureActive=&enemy_native::fixtureSoldier;
    Catalogue soldier{};Check(ReadCatalogue(ObjRef::Of(target),soldier) && soldier.abilities.size()==6,"soldier catalogue read");
    const wchar_t* order[]={L"装备武器齐射",L"举枪瞄准",L"武器",L"跳跃",L"进入／退出掩体",L"翻滚回避"};
    bool ordered=soldier.abilities.size()==6;
    for(unsigned i=0;ordered && i<6;++i)ordered=!std::wcscmp(soldier.abilities[i].name,order[i]) &&
        !std::wcscmp(soldier.native[soldier.abilities[i].channel].name,order[i]);
    Check(ordered,"keys: fire, aim, the weapon, then jump, cover, roll; each keeps its native channel");
    Check(EnemyControlBegin(ObjRef::Of(owner),ObjRef::Of(target),soldier.hash,95001,true) && EnemyControlActivate(95001),"soldier lease");
    HWND window=CreateWindowExW(0,L"STATIC",L"enemy-control soldier keys",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    fixtureInputWindow=window;edf::SetInputSource(&FixtureKeys,&FixtureWindow);
    fixtureHeldKey=VK_LBUTTON;Input lmb{};Sample(*ByLease(95001),lmb);
    Check(lmb.active[0].id==soldier.abilities[0].id && !std::wcscmp(soldier.abilities[0].name,L"装备武器齐射") && !lmb.active[1].id,
          "the left mouse button fires (not 跳跃)");
    fixtureHeldKey=VK_RBUTTON;Input rmb{};Sample(*ByLease(95001),rmb);
    Check(rmb.active[0].id==soldier.abilities[1].id && !std::wcscmp(soldier.abilities[1].name,L"举枪瞄准"),"the right mouse button aims (not 掩体)");
    fixtureHeldKey=0;edf::SetInputSource(nullptr,nullptr);DestroyWindow(window);fixtureInputWindow=nullptr;std::memset(pad,0,sizeof(pad));
    Check(EnemyControlRestore(95001),"soldier lease returned");
    enemy_native::fixtureActive=&enemy_native::fixtureProfile;
    VirtualFree(owner,0,MEM_RELEASE);VirtualFree(target,0,MEM_RELEASE);
}
void UnreadableSourceRestoration() {
    using namespace crew;using namespace crew::enemy_control;
    auto* owner=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    auto* target=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    auto* ownerControl=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    unsigned char targetControl[32]{};Human(owner,ownerControl,0,0);Enemy(target,targetControl);
    Put<unsigned>(owner,0x380,0x2840u);
    Catalogue catalogue{};Check(ReadCatalogue(ObjRef::Of(target),catalogue),"unreadable source fixture reads its native catalogue");
    Check(EnemyControlBegin(ObjRef::Of(owner),ObjRef::Of(target),catalogue.hash,90001,true),"unreadable source fixture acquires a real controller lease");
    DWORD protection=0,ignored=0;
    Check(VirtualProtect(owner,0x1000,PAGE_NOACCESS,&protection)!=0,"source page becomes temporarily inaccessible");Sleep(20);
    Check(!EnemyControlRestore(90001) && ByLease(90001) && ByLease(90001)->sourceProtected,"inaccessible source retains its owned protection and restoration baseline");
    Check(VirtualProtect(owner,0x1000,protection,&ignored)!=0,"source page is readable again");Sleep(20);
    Check((At<unsigned>(owner,0x380)&0x81u)==0x81u,"pending source protection remains on the same native actor");
    Check(VirtualProtect(ownerControl,0x1000,PAGE_NOACCESS,&protection)!=0,"source control becomes temporarily inaccessible");Sleep(20);
    Check(!EnemyControlRestore(90001) && ByLease(90001) && ByLease(90001)->sourceProtected,"inaccessible source control cannot prove destruction");
    Check(VirtualProtect(ownerControl,0x1000,protection,&ignored)!=0,"source control is readable again");Sleep(20);
    Check(EnemyControlRestore(90001) && !ByLease(90001) && At<unsigned>(owner,0x380)==0x2840u,"source retry restores its exact protection baseline before retiring the lease");
    Check(At<LONG>(ownerControl,12)==1 && At<LONG>(targetControl,12)==1,"inaccessible identity retries release source and target weak references once");
    // A known zero strong count permits retirement without reading expired storage.
    Check(EnemyControlBegin(ObjRef::Of(owner),ObjRef::Of(target),catalogue.hash,90002,true),"destroyed source fixture acquires a fresh controller lease");
    Put<LONG>(ownerControl,8,0);
    Check(VirtualProtect(owner,0x1000,PAGE_NOACCESS,&protection)!=0,"expired source storage is inaccessible");Sleep(20);
    Check(EnemyControlRestore(90002) && !ByLease(90002),"strong zero is explicit destruction even when old storage is unreadable");
    Check(VirtualProtect(owner,0x1000,protection,&ignored)!=0,"expired source storage restored for fixture cleanup");Sleep(20);
    VirtualFree(owner,0,MEM_RELEASE);VirtualFree(target,0,MEM_RELEASE);VirtualFree(ownerControl,0,MEM_RELEASE);
}
int main() {
    using namespace crew;using namespace crew::enemy_control;
    Input relative{};relative.move[0]=1;
    Check(!ValidInput(relative),"moving semantic input requires an authenticated absolute view");
    relative={};relative.fullFocusId[20]=1;
    Check(!ValidInput(relative),"core rejects noncanonical NativeID padding");
    relative={};relative.active[0]={42,1};
    Check(!ValidInput(relative),"skill input without an absolute view cannot bypass the wire validator");
    originalTable[5]=originalTable[6]=reinterpret_cast<void*>(&NativeUpdate);
    originalTable[8]=reinterpret_cast<void*>(&NativeUpdate);
    Human(owner1,ownerCtrl1,0,0);Human(owner2,ownerCtrl2,1,1,pad2);
    auto* target1=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    auto* target2=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    unsigned char targetCtrl1[32]{},targetCtrl2[32]{};
    Enemy(target1,targetCtrl1);Enemy(target2,targetCtrl2);
    ConfigureEnemyControlNetwork({nullptr,nullptr,nullptr,&Online});InstallEnemyControl();
    Snapshot candidate{};Check(EnemyControlDescribe(ObjRef::Of(owner1),ObjRef::Of(target1),&candidate),"local player can describe the real native target");
    Check(candidate.reason==Reason::none && candidate.abilityCount==2,"complete native catalogue is published");
    Ability abilities[2]{};Check(EnemyControlAbilities(candidate,0,abilities,2)==2,"page reads use the exact candidate identity");
    Check(EnemyControlRequest(candidate),"offline request captures and activates a native lease");
    Snapshot active{};Check(EnemyControlReadSnapshot(ObjRef::Of(owner1),&active) && active.phase==Phase::active,"owner reads its active lease");
    Check(active.healthKnown && active.health==75.0f && active.maxHealth==100.0f,"HUD reads the controlled body health instead of source player health");
    Check(EnemyControlBodyForMap(owner1)==target1 && EnemyControlBodyForMap(owner2)==nullptr,"map follows only the matching source player controlled body");
    Check((At<unsigned>(owner1,0x380)&0x81u)==0x81u,"source character receives only temporary native protection bits");
    Check(EnemyControlSuppressHuman(owner1) && !EnemyControlSuppressHuman(owner2),"a second local player keeps its own input");
    Check(At<const void*>(target1,0)!=originalTable+1,"native input and AI are intercepted per object");
    bool knownOnline=true;Check(EnemyControlNetworkContext(target1,&knownOnline) && !knownOnline && !EnemyControlNetworkContext(target2,&knownOnline),"exact offline control context cannot be confused with an unknown object");
    ByLease(active.lease)->yaw=0.73f;ByLease(active.lease)->pitch=0.4f;
    float eye[3]{},look[3]{};
    Check(EnemyControlCamera(owner1,nullptr,nullptr,eye,look),"controlled camera uses the source player's own lease");
    // over its shoulder (2026-10-25): the centre looks at the skill's own aim ray (from the lock point at its yaw and
    // pitch; no map hit: 300 m along it), the eye over the body rather than straight behind its lock point
    const float aimDir[3]={std::sin(0.73f)*std::cos(0.4f),std::sin(0.4f),std::cos(0.73f)*std::cos(0.4f)};
    bool onAim=true;for(unsigned i=0;i<3;++i)onAim=onAim && std::fabs(look[i]-((i==1 ? 2.0f : 0.0f)+aimDir[i]*300.0f))<0.001f;
    Check(onAim,"camera centre looks at the absolute skill aim ray");
    Check(eye[1]>2.0f-aimDir[1]*8.0f+1.0f,"camera eye rides over the controlled body, not behind its lock point");
    fixtureRayMode=1;Check(EnemyControlCamera(owner1,nullptr,nullptr,eye,look),"inclusive origin hit retains a usable enemy camera");
    const float pivot[3]={0,2,0};float retreat=0;for(unsigned i=0;i<3;++i)retreat+=(eye[i]-pivot[i])*(eye[i]-pivot[i]);
    Check(retreat>4,"zero-distance collision cannot collapse eye onto the controlled body");
    fixtureRayMode=2;EnemyControlCamera(owner1,nullptr,nullptr,eye,look);retreat=0;
    for(unsigned i=0;i<3;++i)retreat+=(eye[i]-pivot[i])*(eye[i]-pivot[i]);
    Check(retreat>0 && retreat<1,"a genuine positive-distance near wall still shortens the camera");fixtureRayMode=0;
    Input press{};press.absoluteAim=true;press.aimYaw=0.73f;press.aimPitch=0.4f;press.sequence=1;press.seed=9;press.active[0]={abilities[1].id,2};
    Input release{};release.sequence=2;release.seed=10;
    Check(EnemyControlApply(active.lease,press) && EnemyControlApply(active.lease,release),"same-poll press and release are both accepted in order");
    NativeStep(target1);float weaponPoint[3]{};
    Check(EnemyControlWeaponAim(target1,weaponPoint) && std::fabs((weaponPoint[1]-2.0f)/2000.0f-std::sin(0.4f))<0.00001f,"native weapon target uses exactly the canonical absolute skill elevation");
    Check(fixtureCommands==1 && commandSequences[0]==1,"first native update executes the queued skill press");
    NativeStep(target1);NativeStep(target1);Check(fixtureCommands==1,"release and repeated native update cannot replay the skill edge");
    Check(EnemyControlWeaponAim(target1,weaponPoint),"neutral release preserves the final canonical target for an already accepted native burst");
    Input hold{};hold.absoluteAim=true;hold.sequence=3;hold.seed=11;hold.active[0]={abilities[0].id,1};
    Check(EnemyControlApply(active.lease,hold),"held native skill is admitted");
    const unsigned before=fixturePressed;NativeStep(target1);NativeStep(target1);
    Check(fixtureHeld==0x10 && fixturePressed==before+1,"one rising held skill produces exactly one native pressed record");
    hold.sequence=4;hold.seed=12;Check(EnemyControlApply(active.lease,hold),"next heartbeat preserves held skill");NativeStep(target1);
    Check(fixturePressed==before+1,"held heartbeat cannot reset or repress an ongoing skill");
    release.sequence=5;Check(EnemyControlApply(active.lease,release),"release is accepted");NativeStep(target1);Check(fixtureHeld==0,"native held intent is released");
    Input bad{};bad.absoluteAim=true;bad.sequence=6;bad.active[0]={0xBAD,1};Check(!EnemyControlApply(active.lease,bad),"unknown ability cannot enter the native queue");
    Check(!EnemyControlApply(active.lease,release),"old or duplicate sequence cannot be replayed");
    for(unsigned i=0;i<64;++i){press.sequence=6+i;Check(EnemyControlApply(active.lease,press),"bounded edge queue accepts each semantic press");}
    press.sequence=70;Check(!EnemyControlApply(active.lease,press),"a full event queue explicitly refuses input instead of dropping an acknowledged edge");
    Put<unsigned>(owner1,0x380,At<unsigned>(owner1,0x380)|0x2840u);
    DWORD old=0;VirtualProtect(target1,0x1000,PAGE_READONLY,&old);
    Check(!EnemyControlRestore(active.lease) && ByLease(active.lease)!=nullptr,"failed live-vptr restoration retains session and baseline");
    Check(At<LONG>(targetCtrl1,12)>1,"restoration failure retains the native weak reference");
    VirtualProtect(target1,0x1000,old,&old);fixtureRestoreFails=true;
    Check(!EnemyControlRestore(active.lease) && ByLease(active.lease)!=nullptr,"failed native intent restoration remains retryable after vptr is returned");
    fixtureRestoreFails=false;Check(EnemyControlRestore(active.lease),"restoration retry succeeds");
    Check(EnemyControlBodyForMap(owner1)==nullptr,"map returns to the source player immediately after control exit");
    Check(At<const void*>(target1,0)==originalTable+1 && !EnemyControlSuppressHuman(owner1),"original enemy AI and source input resume");
    Check(At<unsigned>(owner1,0x380)==0x2840u,"protection release preserves unrelated PVP and mission flag changes");
    Check(EnemyControlBegin(ObjRef::Of(owner1),ObjRef::Of(target1),candidate.profileHash,123,false),"network begin can capture without activating");
    fixtureOnline=true;Check(EnemyControlActivate(123),"core activation succeeds before transport published phase catches up");
    Input input{};input.sequence=1;input.seed=1;
    Check(ByLease(123)->snapshot.phase==Phase::active && EnemyControlApply(123,input),"UI beginning status cannot mutate core active state or reject immediate ordered input");
    fixtureOnline=false;target1[kDead]=1;
    Check(EnemyControlRestore(123) && At<const void*>(target1,0)==originalTable+1,"dead but existing object returns its cloned vtable before session reuse");
    target1[kDead]=0;
    Check(EnemyControlBegin(ObjRef::Of(owner1),ObjRef::Of(target1),candidate.profileHash,124,true) && EnemyControlActivate(124),"first split-screen source begins independently");
    Snapshot second{};Check(EnemyControlDescribe(ObjRef::Of(owner2),ObjRef::Of(target2),&second) && EnemyControlRequest(second),"second split-screen source has an independent native body");
    Snapshot secondActive{};EnemyControlReadSnapshot(ObjRef::Of(owner2),&secondActive);
    fixtureInputWindow=CreateWindowExW(0,L"STATIC",L"enemy-control input fixture",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(fixtureInputWindow!=nullptr,"input fixture owns a hidden message-only window without taking desktop focus");
    edf::SetInputSource(&FixtureKeys,&FixtureWindow);
    // The pad path (enemy_control_pad.h): each controller's own sticks at its base, whatever the record index.
    Put<int>(pad,0x2D68,1);Put<int>(pad2,0x2D68,1);
    Put<float>(pad,0x0C,0.75f);Put<float>(pad,0x24,0.5f);pad[0xD0]=1;
    Put<float>(pad2,0x0C,-0.5f);Put<float>(pad2,0x24,0.25f);Put<float>(pad2,0x3C,0.5f);pad2[0xA80+0xD0]=1;
    fixtureMapOwner=owner1;Input p1Map{},p2Pad{};Sample(*ByLease(124),p1Map);Sample(*ByLease(secondActive.lease),p2Pad);
    Check(p1Map.move[0]==0 && p1Map.move[2]==0 && !p1Map.active[0].id,"the map owner consumes its own movement and physical trigger");
    Check(p2Pad.move[0]==-0.5f && p2Pad.move[2]==-0.25f && p2Pad.look[0]==0.5f && p2Pad.active[0].id,"P1 map and closing drain cannot swallow P2 native movement look or fire");
    fixtureMapOwner=owner2;Input p1Pad{},p2Map{};Sample(*ByLease(124),p1Pad);Sample(*ByLease(secondActive.lease),p2Map);
    Check(p1Pad.move[0]==0.75f && p1Pad.active[0].id && p2Map.move[0]==0 && !p2Map.active[0].id,"owner-aware input holds also isolate P2 map from P1 controls");
    {
        // The keyboard path (the user, 2026-10-21: 「我怎么左键往右走，右键往后走，wasd没反应」): the soldier walks with its
        // record's movement keys and looks with its mouse motion; the pad floats are not its sticks there.
        fixtureMapOwner=nullptr;Put<int>(pad,0x2D68,0);Put<int>(pad2,0x2D68,0);pad[0xD0]=0;pad2[0xA80+0xD0]=0;
        Put<float>(pad,0x0C,1.0f);Put<float>(pad,0x24,1.0f);Put<float>(pad,0x3C,1.0f);
        Input idle{};Sample(*ByLease(124),idle);
        Check(idle.move[0]==0 && idle.move[2]==0 && idle.look[0]==0,"keyboard path: the pad floats a mouse button sets move and turn nothing");
        pad[0x610]=1;Input forward{};Sample(*ByLease(124),forward);
        Check(forward.move[2]==1.0f && forward.move[0]==0,"keyboard path: the forward key (W) walks forward");
        pad[0x610]=0;pad[0x640]=1;pad[0x658]=1;Input backRight{};Sample(*ByLease(124),backRight);
        Check(backRight.move[2]==-1.0f && backRight.move[0]==1.0f,"keyboard path: back (S) and right (D) as the soldier reads them");
        pad[0x640]=pad[0x658]=0;pad[0x628]=1;Put<float>(pad,0x66C,12.0f);Put<float>(pad,0x684,-6.0f);Input left{};Sample(*ByLease(124),left);
        Check(left.move[0]==-1.0f && left.look[0]==0.5f && left.look[1]==-0.25f,"keyboard path: left (A), and the mouse motion over 24 turns the camera");
        pad[0x628]=0;Put<float>(pad,0x66C,0.0f);Put<float>(pad,0x684,0.0f);
        pad2[0xA80+0x628]=1;Input p1Keys{},p2Keys{};Sample(*ByLease(124),p1Keys);Sample(*ByLease(secondActive.lease),p2Keys);
        Check(p2Keys.move[0]==-1.0f && p1Keys.move[0]==0,"keyboard path: each local player's own record, P1's keys never P2's");
        pad2[0xA80+0x628]=0;std::memset(pad,0,sizeof(pad));
        // The game's own flag (0x1183D70: movzx eax, byte [rip+rel]; ret) is found from its code.
        unsigned char code[16]{0x0F,0xB6,0x05,0,0,0,0,0xC3};const int rel=8-7;std::memcpy(code+3,&rel,4);
        Check(PadFlagAddress(code)==code+8,"the pad-path flag is the byte the function returns");
        code[7]=0x90;Check(!PadFlagAddress(code),"other code there yields no flag");
    }
    {
        // Each attack has its own key: the second attack's key (right mouse button) uses it
        // directly, with no selection and without the first attack.
        fixtureMapOwner=nullptr;pad[0xD0]=0;pad2[0xA80+0xD0]=0;fixtureHeldKey=VK_RBUTTON;
        Snapshot p1{};EnemyControlReadSnapshot(ObjRef::Of(owner1),&p1);
        Ability own[2]{};Check(EnemyControlAbilities(p1,0,own,2)==2,"controlled catalogue readable");
        Input first{},again{};Sample(*ByLease(124),first);Sample(*ByLease(124),again);
        const bool held=(own[1].flags&AbilityFlag::held)!=0;
        Check(first.active[0].id==own[1].id && first.active[0].phase==(held ? 1u : 2u) && !first.active[1].id,"the second attack's own key uses that attack and nothing else");
        Check(held ? again.active[0].id==own[1].id : !again.active[0].id,"an edge attack is pressed once per key press, a held one stays held");
        fixtureHeldKey=0;Input up{};Sample(*ByLease(124),up);Check(!up.active[0].id,"releasing the key releases the attack");
        // +0xD40 is the soldier class's key record, not the player (enemy_control_pad.h): P1 as an Air Raider
        // (record 2) walks with that record's keys and keeps the attack keys; P2 with the Ranger's record 0
        // (pad 1) still never reads P1's keys.
        Put<int>(owner1,0xD40,2);Put<int>(owner2,0xD40,0);pad[2*0xA80+0x610]=1;fixtureHeldKey=VK_RBUTTON;
        Input raider{},p2Ranger{};Sample(*ByLease(124),raider);Sample(*ByLease(secondActive.lease),p2Ranger);
        Check(raider.move[2]==1.0f && raider.active[0].id==own[1].id,"P1 as an Air Raider (record 2): its record's W and the attack keys");
        Check(!p2Ranger.active[0].id && p2Ranger.move[2]==0,"P2 with the Ranger's record 0 still never reads P1's keys");
        fixtureHeldKey=0;pad[2*0xA80+0x610]=0;Input released{};Sample(*ByLease(124),released);Sample(*ByLease(secondActive.lease),p2Ranger);
        Put<int>(owner1,0xD40,0);Put<int>(owner2,0xD40,1);
        Check(EnemyControlHoldAbility(p1,own[0].id),"the M card holds an attack directly");
        Input card{};Sample(*ByLease(124),card);Check(card.active[0].id==own[0].id,"the card's held row uses its attack");
        Check(EnemyControlHoldAbility(p1,0),"the card lets go");Input none{};Sample(*ByLease(124),none);Check(!none.active[0].id,"no attack after the card lets go");
    }
    fixtureMapOwner=nullptr;edf::SetInputSource(nullptr,nullptr);DestroyWindow(fixtureInputWindow);fixtureInputWindow=nullptr;std::memset(pad,0,sizeof(pad));std::memset(pad2,0,sizeof(pad2));
    Snapshot wrong=secondActive;wrong.ownerHumanKey=reinterpret_cast<std::uintptr_t>(owner1);
    Check(!EnemyControlExit(wrong),"P1 cannot exit P2's lease with a changed UI owner key");
    Check(!EnemyControlSetAbilityHeld(wrong,false),"P1 cannot release P2's UI skill with a mismatched owner card");
    Check(EnemyControlSelectAbility(secondActive,abilities[1].id),"a local owner can select another real skill");
    Snapshot selected{};EnemyControlReadSnapshot(ObjRef::Of(owner2),&selected);
    Check(selected.revision!=secondActive.revision && !EnemyControlSetAbilityHeld(secondActive,true),"skill selection changes its semantic revision and rejects a stale press");
    Check(EnemyControlSetAbilityHeld(secondActive,false),"old skill revision can still release only its exact owner body lease");
    fixtureConfig.enemyControl=false;EnemyControlFrame();
    Check(!ByLease(124) && !ByLease(secondActive.lease) && At<const void*>(target2,0)==originalTable+1,"disabling feature releases all local native leases");
    ResetEnemyControl();
    for(unsigned flags:{1u,0x80u,0x81u}) {
        fixtureConfig.enemyControl=true;Put<unsigned>(owner1,0x380,flags);
        Check(EnemyControlBegin(ObjRef::Of(owner1),ObjRef::Of(target1),candidate.profileHash,1000+flags,true),"pre-existing source protection can be leased");
        Check(EnemyControlRestore(1000+flags) && At<unsigned>(owner1,0x380)==flags,"lease exit preserves protection bits that it never owned");
    }
    fixtureConfig.enemyControl=true;fixtureOnline=false;fixtureNetworkIds=false;
    ConfigureEnemyControlNetwork({&PendingRequest,nullptr,&PendingStop,&Online});
    Snapshot offlineCandidate{};Check(EnemyControlDescribe(ObjRef::Of(owner2),ObjRef::Of(target2),&offlineCandidate),"pre-registration candidate can be inspected offline");
    fixtureOnline=fixtureNetworkIds=true;
    Snapshot pendingCandidate{};Check(EnemyControlDescribe(ObjRef::Of(owner2),ObjRef::Of(target2),&pendingCandidate),"pending network request keeps a local candidate identity");
    Check(pendingCandidate.revision!=offlineCandidate.revision && pendingCandidate.owner[0]==2 && pendingCandidate.target[0]==3,"registered native identities replace stale offline candidate tokens without requiring hover expiry");
    Check(EnemyControlRequest(pendingCandidate),"network callback accepts a preparing request without beginning native control");
    Snapshot pending{};Check(EnemyControlReadSnapshot(ObjRef::Of(owner2),&pending) && pending.phase==Phase::pending,"pending request is visible by its exact source");
    fixtureTime+=4000;++fixtureFrame;EnemyControlFrame();Snapshot delayed{};
    Check(EnemyControlReadSnapshot(ObjRef::Of(owner2),&delayed) && delayed.revision==pending.revision,"pending request survives the ordinary three-second hover expiry");
    Check(EnemyControlExit(delayed) && fixturePendingCancels==1,"four-second preparing request can still be cancelled by its owner");
    Check(EnemyControlReadSnapshot(ObjRef::Of(owner2),&delayed) && delayed.phase==Phase::restoring,"pending cancellation remains visible while network restoration completes");
    fixturePendingPhase=enemy_control_protocol::Phase::closed;++fixtureFrame;EnemyControlFrame();
    fixtureNetFailed=true;Snapshot failedRetry{},retryStable{};
    Check(EnemyControlDescribe(ObjRef::Of(owner2),ObjRef::Of(target2),&failedRetry),"a terminal network rejection can be inspected again");
    Check(EnemyControlDescribe(ObjRef::Of(owner2),ObjRef::Of(target2),&retryStable) && failedRetry.revision==retryStable.revision && retryStable.phase==Phase::idle && retryStable.reason==Reason::none,"old request failure cannot contaminate a fresh candidate or churn its revision every frame");
    fixtureNetFailed=false;fixturePendingVisible=false;fixtureOnline=false;fixtureNetworkIds=false;ResetEnemyControl();
    // Core target selection publishes only real retained identities. Target helper
    // visibility/hostility/native weak setters have their own production fixture.
    fixtureFocus.actor=ObjRef::Of(target2);fixtureFocus.lock={target2+0x1000,targetCtrl2};fixtureFocus.nativeId[0]=3;fixtureFocus.lockIndex=0;
    Put<const void*>(target2,0x368,target2+0x1100);Put<std::uint64_t>(target2,0x378,1);
    Put<const void*>(target2+0x1100,0,fixtureFocus.lock.obj);Put<const void*>(target2+0x1100,8,fixtureFocus.lock.ctrl);
    Put<const void*>(target2+0x1000,8,target2);target2[0x1029]=target2[0x102A]=1;
    Check(EnemyControlBegin(ObjRef::Of(owner1),ObjRef::Of(target1),candidate.profileHash,2001,true) && EnemyControlActivate(2001),"real focus fixture owns a local native body");
    Input focused{};focused.absoluteAim=true;focused.sequence=1;focused.seed=81;
    Check(EnemyControlApply(2001,focused),"focused absolute input is accepted");NativeStep(target1);
    ObjRef actual{};Check(EnemyControlAimTarget(target1,&actual) && Same(actual,ObjRef::Of(target2)),"weapon actor query returns the cached real target identity");
    const auto selector=At<void* const*>(target1,0)[14];
    Check(EnemyControlOwnsTargetSelector(target1,selector) && !EnemyControlOwnsTargetSelector(target1,reinterpret_cast<void*>(&NativeUpdate)),"selector proof admits only this private slot14 hook");
    Check(reinterpret_cast<void*(__fastcall*)(void*)>(selector)(target1)==target2,"stock AI selector returns the cached actor");
    alignas(16) ObjRef nativeLock{};const LONG lockBefore=At<LONG>(targetCtrl2,12);
    Check(EnemyControlWeaponLock(target1,&nativeLock) && Same(nativeLock,fixtureFocus.lock) && At<LONG>(targetCtrl2,12)==lockBefore+1,"native lock copies exactly one retained real weak pair");
    Drop(nativeLock);Check(At<LONG>(targetCtrl2,12)==lockBefore,"the real native weak recipient releases its copied lock once");
    unsigned char lockId[32]{};int lockOrdinal=99;fixtureNetworkIds=true;
    Check(EnemyControlWeaponLockIdentity(target1,lockId,&lockOrdinal) && lockId[0]==3 && lockOrdinal==0 && At<LONG>(targetCtrl2,12)==lockBefore,"borrowed canonical homing identity preserves the actual lock ordinal without adding a weak reference");
    target2[0x102A]=0;Check(EnemyControlWeaponLockIdentity(target1,lockId,&lockOrdinal) && !lockId[0] && lockOrdinal==-1,"an invalidated native lock becomes legal empty homing instead of old AI target");
    target2[0x102A]=1;fixtureNetworkIds=false;
    fixtureFocus={};focused.sequence=2;Check(EnemyControlApply(2001,focused),"an empty reticle is a valid controlled sample");NativeStep(target1);
    Check(EnemyControlAimTarget(target1,&actual) && !actual && reinterpret_cast<void*(__fastcall*)(void*)>(selector)(target1)==nullptr,"empty controlled focus cannot fall through to autonomous AI selection");
    Check(EnemyControlWeaponLock(target1,&nativeLock) && !nativeLock,"no target returns a legal empty native lock");
    Check(EnemyControlWeaponLockIdentity(target1,lockId,&lockOrdinal) && !lockId[0] && lockOrdinal==-1,"a real actor without a selected LockPoint has no fabricated homing ordinal");
    Put<int>(target1,0x314,-1);Put<int>(target1,0x318,2);Publish();
    Check(fixtureRadar.size()==1 && fixtureRadar[0].ownerObject==reinterpret_cast<std::uintptr_t>(owner1) && fixtureRadar[0].team==2,"controlled radar uses exact local owner and native closed last-team");
    fixtureTargetRestoreFails=true;Check(!EnemyControlRestore(2001) && ByLease(2001) && (At<unsigned>(owner1,0x380)&0x81u)==0x81u,"target restoration failure retains ownership and source protection");
    fixtureTargetRestoreFails=false;fixtureFocusClearFails=true;
    Check(!EnemyControlRestore(2001) && At<const void*>(target1,0)!=originalTable+1 && EnemyControlSuppressHuman(owner1) && ByLease(2001)->sourceProtected,"focus weak cleanup failure retains the input table and source protection before returning ownership");
    fixtureFocusClearFails=false;Check(EnemyControlRestore(2001) && fixtureRadar.empty(),"complete exit clears the radar set and returns target fields");
    Put<int>(target1,0x314,1);
    // Network action queue and full focus identity normalization are core seams;
    // typed native acceptance/replay and authenticated wire have separate fixtures.
    fixtureOnline=fixtureNetworkIds=fixtureActions=true;fixtureActionQuiesced=false;
    Check(EnemyControlBegin(ObjRef::Of(owner1),ObjRef::Of(target1),candidate.profileHash,2002,true,"FixturePuid") && EnemyControlActivate(2002),"online begin binds the native action role before activation");
    bool networkKnown=false;Check(EnemyControlNetworkContext(target1,&networkKnown) && networkKnown,"authenticated online context is immutable for the exact body lease");
    const unsigned beforeScope=fixtureBefore,afterScope=fixtureAfter;
    Input networkInput{};networkInput.absoluteAim=true;networkInput.sequence=1;networkInput.seed=91;networkInput.fullFocusId[0]=88;
    Check(EnemyControlNormalizeNetworkInput(2002,&networkInput) && !networkInput.fullFocusId[0],"host clears an invalid full focus ID without revoking the input lease");
    fixtureFocus.actor=ObjRef::Of(target2);fixtureFocus.nativeId[0]=77;networkInput.fullFocusId[0]=77;
    Check(EnemyControlNormalizeNetworkInput(2002,&networkInput) && networkInput.fullFocusId[0]==77,"host preserves only the helper-validated full target identity");
    networkInput.active[0]={abilities[1].id,2};Check(EnemyControlApply(2002,networkInput),"online named ability enters the ordered input queue");NativeStep(target1);
    Check(fixtureBefore==beforeScope+1 && fixtureAfter==afterScope+1,"accepted action scope brackets native input and named command once");
    Check(EnemyControlNeedsHostActions(2002),"network core exposes its real bound native action provider");
    fixtureActionPending=3;Check(EnemyControlQuiesceNetworkActions(2002) && fixtureActionQuiesced && ByLease(2002)->closing,"normal exit quiesces without dropping accepted outcomes");
    Check(!EnemyControlLeaseLive(2002) && !EnemyControlNetworkActionsEmpty(2002),"quiescing blocks new input while its accepted outbox remains nonempty");
    Check(!EnemyControlApply(2002,networkInput),"closing lease rejects new semantic input");
    Check(EnemyControlWeaponAim(target1,weaponPoint),"finishing accepted actions retain their canonical weapon aim during drain");
    Check(!EnemyControlRestore(2002) && ByLease(2002) && (At<unsigned>(owner1,0x380)&0x81u)==0x81u,"normal restore cannot discard an unconsumed accepted outbox");
    unsigned char action[512]{};std::size_t actionBytes=0;
    Check(EnemyControlCaptureNetworkAction(2002,action,sizeof(action),&actionBytes) && actionBytes==4 && EnemyControlNetworkActionsEmpty(2002),"closing native cache can be captured before restoration");
    const unsigned appliedBefore=fixtureActionApplied;
    Check(EnemyControlApplyNetworkAction(2002,1,action,actionBytes)==ActionResult::applied && EnemyControlApplyNetworkAction(2002,1,action,actionBytes)==ActionResult::applied && fixtureActionApplied==appliedBefore+1,"duplicate reliable action sequence is acknowledged without a second native replay");
    action[0]^=1;Check(EnemyControlApplyNetworkAction(2002,1,action,actionBytes)==ActionResult::failed,"same reliable sequence cannot acknowledge a different native action payload");
    Check(EnemyControlRestore(2002),"drained native session finally restores and releases source protection");
    Check(EnemyControlBegin(ObjRef::Of(owner1),ObjRef::Of(target1),candidate.profileHash,2003,true,"FixturePuid") && EnemyControlActivate(2003),"trusted epoch retirement fixture begins a new lease");
    fixtureActionPending=5;fixtureActionRestoreFails=true;
    Check(EnemyControlAbortNetworkActions(2003) && !fixtureActionPending && !EnemyControlRestore(2003),"trusted epoch abort retires old outcomes but retains failed native restoration");
    fixtureActionRestoreFails=false;++fixtureFrame;EnemyControlFrame();
    Check(!ByLease(2003),"aborted restoration failure is retried on the next game frame");
    Check(EnemyControlBegin(ObjRef::Of(owner1),ObjRef::Of(target1),candidate.profileHash,2004,true,"FixturePuid") && EnemyControlActivate(2004),"input failure cleanup fixture begins");
    networkInput.sequence=1;Check(EnemyControlApply(2004,networkInput),"input failure fixture queues one real sample");
    const unsigned afterFailure=fixtureAfter;fixtureRunFails=true;NativeStep(target1);fixtureRunFails=false;
    Check(fixtureAfter==afterFailure+1 && ByLease(2004)->faulted,"native input failure still releases the accepted-action temporary scope");
    fixtureOnline=fixtureNetworkIds=fixtureActions=false;fixtureFocus={};ResetEnemyControl();
    Check(EnemyControlBegin(ObjRef::Of(owner1),ObjRef::Of(target1),candidate.profileHash,2005,true) && EnemyControlActivate(2005),"guide cleanup fixture begins an ordinary native lease");
    fixtureGuideReady=false;fixtureGuideRestoreFails=true;++fixtureFrame;EnemyControlFrame();
    Check(ByLease(2005) && ByLease(2005)->faulted && EnemyControlSuppressHuman(owner1),"a live guide fault stops new control while its weak cleanup journal remains owned");
    fixtureGuideReady=true;fixtureGuideRestoreFails=false;++fixtureFrame;EnemyControlFrame();
    Check(!ByLease(2005) && !EnemyControlSuppressHuman(owner1),"successful guide journal retry returns the body and source before releasing their refs");
    fixtureOnline=true;fixtureCapacityReady=false;Catalogue capacityCatalogue{};
    Check(CanPrepare(ObjRef::Of(owner1),ObjRef::Of(target1),&capacityCatalogue)==Reason::networkCapacity && !EnemyControlSuppressHuman(owner1),"old Coop capacity is refused with an explicit update reason before native ownership changes");
    fixtureCapacityReady=true;capacityCatalogue={};
    Check(CanPrepare(ObjRef::Of(owner1),ObjRef::Of(target1),&capacityCatalogue)!=Reason::networkCapacity,"compatible Coop capacity continues normal native admission checks");
    fixtureOnline=false;
    auto* protectedControl=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Put<LONG>(protectedControl,8,1);Put<LONG>(protectedControl,12,2);
    ObjRef protectedRef{target1,protectedControl};DWORD controlProtection=0;
    VirtualProtect(protectedControl,0x1000,PAGE_READONLY,&controlProtection);
    Check(!Drop(protectedRef) && protectedRef.ctrl==protectedControl && At<LONG>(protectedControl,12)==2,"readonly native weak release retains its identity and exact count");
    VirtualProtect(protectedControl,0x1000,controlProtection,&controlProtection);
    // Readable intentionally caches VirtualQuery regions for one system tick.
    const auto cacheTick=GetTickCount64();while(GetTickCount64()==cacheTick)Sleep(1);
    Check(Drop(protectedRef) && !protectedRef && At<LONG>(protectedControl,12)==1,"weak restoration retry releases the retained identity exactly once");
    VirtualFree(protectedControl,0,MEM_RELEASE);
    Check(At<LONG>(ownerCtrl1,12)==1 && At<LONG>(ownerCtrl2,12)==1 && At<LONG>(targetCtrl1,12)==1 && At<LONG>(targetCtrl2,12)==1,"all source/target weak references are released exactly once");
    VirtualFree(target1,0,MEM_RELEASE);VirtualFree(target2,0,MEM_RELEASE);
    UnreadableSourceRestoration();
    SoldierKeys();
    std::printf("enemy control runtime: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
