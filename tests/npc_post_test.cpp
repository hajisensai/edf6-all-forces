// Production return-to-post adapter, synthetic object/seat memory, real seat classification. No game calls.
#include "../src/npcpost.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace crew {
bool NpcDriver(const unsigned char* v) noexcept { return v && SeatCount(v)>0 && SeatRider(SeatAt(const_cast<unsigned char*>(v),0))==Rider::dummy; }
bool JetFliesItself(const void*) noexcept { return false; }   // no plugin jet in this world (command_unit.cpp)
bool missionAi=false;   // mission_crew.cpp's answer: the stock RideAi was called on the vehicle
bool MissionAiVehicle(const unsigned char*) noexcept { return missionAi; }

unsigned char* image=nullptr;
Config config{};
ULONGLONG now=1000;
bool sessionOn=false,host=true;
const Config& Cfg() noexcept { return config; }
ULONGLONG GameMs() noexcept { return now; }
bool InSession() noexcept { return sessionOn; }
bool OnlineHostOnly() noexcept { return !sessionOn || host; }
bool IsOnlineAuthority(const void*) noexcept { return !sessionOn || host; }
bool AiGunner(const unsigned char* v,const unsigned char* seat) noexcept {
    if(!config.enabled || !config.npcGunners || AnyPlayerIn(seat))return false;
    const auto who=SeatRider(seat);
    if(who==Rider::dummy)return IsOnlineAuthority(v);
    const auto rider=At<const unsigned char*>(seat,kSeatRider);
    return who==Rider::other && config.customNpcAi && config.npcBoarding &&
        edf::LivingSoldierInSeat(image,seat) && !edf::RemoteRider(rider) && IsOnlineAuthority(v);
}
void Log(const char*,...) noexcept {}
const void* focusEnemy=nullptr;
bool VisitEnemies(const unsigned char*,EnemyVisitor visit,void* context) noexcept {if(focusEnemy)visit(context,focusEnemy,nullptr);return true;}
}
namespace {
int checks=0;
void Check(bool b,const char* why) { ++checks;if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);} }
struct ShapeValue { unsigned char type=1;const wchar_t* name=nullptr;ShapeValue* entries=nullptr;int count=0;std::uint16_t tag=1; };
struct ShapeLocator { const wchar_t* name;unsigned bone;unsigned padding=0;alignas(16) unsigned char definition[0x20]{};float local[16]{}; };
ShapeValue shapeRoot{};
ShapeLocator shapeLocators[4]{};
alignas(16) crew::MechShape shapeStorage[edf::kMaxSeats]{};
unsigned shapeFinds=0,shapeResizes=0,shapeReleases[3]{};
int shapeFailAt=-1;
bool shapeResizeFails=false,shapeVisitorFails=false;
const ShapeValue* Value(const void* variant) { return crew::At<const ShapeValue*>(variant,0); }
void __fastcall ShapeFind(const void* variant,void* request) {
    if(shapeVisitorFails){RaiseException(0xE0001235,0,0,nullptr);return;}
    auto& find=*static_cast<crew::ifcmodel::Find*>(request);
    const auto* root=Value(variant);
    find.index=root && root->count && std::wcscmp(find.name,L"begaruta_aiming_shape")==0 ? 0 : -1;
}
void __fastcall ShapeGet(const void* variant,void* request) {
    auto& pick=*static_cast<crew::ifcmodel::Get*>(request);auto& out=*static_cast<crew::MechVariant*>(pick.out);
    const auto* parent=Value(variant);
    if(!parent || pick.index<0 || pick.index>=parent->count){out=crew::MechVariant{};return;}
    const auto* entry=parent->entries+pick.index;out=crew::MechVariant{};
    crew::Put<const ShapeValue*>(&out,0,entry);out.tag=entry->tag;
}
void __fastcall ShapeType(const void* variant,void* out) { const auto* v=Value(variant);*static_cast<unsigned char*>(out)=v ? v->type : 255; }
void __fastcall ShapeCount(const void* variant,void* out) { const auto* v=Value(variant);*static_cast<std::int32_t*>(out)=v ? v->count : -1; }
void __fastcall ShapeString(const void* variant,void* out) { const auto* v=Value(variant);*static_cast<const wchar_t**>(out)=v ? v->name : nullptr; }
void __fastcall ShapeRelease(const void* variant,void*) {const auto tag=static_cast<const crew::MechVariant*>(variant)->tag;if(tag<3)++shapeReleases[tag];}
bool __fastcall ShapeLookup(void* model,crew::MechShape* out,int mode,const wchar_t* name) {
    using namespace crew;
    const auto call=shapeFinds++;*out={};if(mode || static_cast<int>(call)==shapeFailAt)return false;
    const auto* vehicle=static_cast<unsigned char*>(model)-0xE40;
    auto* bones=At<unsigned char*>(vehicle+kModelInst506,kInstBones506);
    for(auto& loc:shapeLocators)if(loc.name && std::wcscmp(loc.name,name)==0){*out={loc.definition,bones+loc.bone*kBoneStride};return true;}
    return false;
}
void __fastcall ShapeResize(void* vector,std::size_t count) {
    using namespace crew;++shapeResizes;
    if(shapeResizeFails){RaiseException(0xE0001234,0,0,nullptr);return;}
    const auto* old=At<const MechShape*>(vector,0);
    const auto oldEnd=At<std::uintptr_t>(vector,8),oldBegin=reinterpret_cast<std::uintptr_t>(old);
    const auto previous=old ? (oldEnd-oldBegin)/sizeof(MechShape) : 0;
    if(old && old!=shapeStorage && previous)std::memcpy(shapeStorage,old,previous*sizeof(MechShape));
    if(count>previous)std::memset(shapeStorage+previous,0,(count-previous)*sizeof(MechShape));
    Put<void*>(vector,0,shapeStorage);Put<void*>(vector,8,shapeStorage+count);Put<void*>(vector,16,shapeStorage+edf::kMaxSeats);
}
void Jump(unsigned char* at,const void* function) {
    at[0]=0x48;at[1]=0xB8;std::memcpy(at+2,&function,8);at[10]=0xFF;at[11]=0xE0;
}
void ShapeStubs() {
    using namespace crew;
    const void* callbacks[]={reinterpret_cast<const void*>(&ShapeFind),reinterpret_cast<const void*>(&ShapeGet),
        reinterpret_cast<const void*>(&ShapeType),reinterpret_cast<const void*>(&ShapeCount),
        reinterpret_cast<const void*>(&ShapeString),reinterpret_cast<const void*>(&ShapeRelease)};
    for(unsigned k=0;k<6;++k)for(unsigned tag=0;tag<3;++tag) {
        const auto& table=kMechVariantTables[k];Put<void*>(image+table.table,tag*8,image+table.functions[tag]);
        Jump(image+table.functions[tag],callbacks[k]);
    }
    // Keep the real helper's checked prologue, undo it, then invoke the synthetic native operation.
    auto* lookup=image+kMechFindLocator;std::memcpy(lookup,kMechFindLocatorSig,sizeof(kMechFindLocatorSig));
    const unsigned char pops[]={0x41,0x5F,0x41,0x5E,0x5F,0x5E,0x5D,0x5B};
    std::memcpy(lookup+sizeof(kMechFindLocatorSig),pops,sizeof(pops));
    Jump(lookup+sizeof(kMechFindLocatorSig)+sizeof(pops),reinterpret_cast<const void*>(&ShapeLookup));
    auto* resize=image+kMechResizeShapes;std::memcpy(resize,kMechResizeShapesSig,sizeof(kMechResizeShapesSig));
    const unsigned char stackBack[]={0x48,0x83,0xC4,0x28};
    std::memcpy(resize+sizeof(kMechResizeShapesSig),stackBack,sizeof(stackBack));
    Jump(resize+sizeof(kMechResizeShapesSig)+sizeof(stackBack),reinterpret_cast<const void*>(&ShapeResize));
    FlushInstructionCache(GetCurrentProcess(),image,0x1800000);
}
void SetShapeDeclaration(unsigned char* vehicle,ShapeValue* field) {
    shapeRoot=ShapeValue{};shapeRoot.type=3;shapeRoot.entries=field;shapeRoot.count=field ? 1 : 0;
    crew::MechVariant root{};root.tag=0;crew::Put<const ShapeValue*>(&root,0,&shapeRoot);
    std::memcpy(vehicle+0x100,&root,sizeof(root));shapeFinds=shapeResizes=0;shapeFailAt=-1;shapeResizeFails=shapeVisitorFails=false;
}
}
int main() {
    using namespace crew;
    // A real allocated image span makes every image+RVA pointer valid C++ pointer arithmetic; no code is executed.
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"fixture image allocated");
    std::memcpy(image+kAttackRead,kAttackReadSig,sizeof(kAttackReadSig));
    std::memcpy(image+kAttackRelease,kAttackReleaseSig,sizeof(kAttackReleaseSig));
    ShapeStubs();
    alignas(16) unsigned char v[0x2700]{},seat[kSeatStride]{},rider[0x400]{},ctrl[0x20]{},otherCtrl[0x20]{};
    void* vt[73]{};vt[72]=image+0x661440;
    Put<void**>(v,0,vt);Put<void*>(v,kSelfCtrl,ctrl);
    Put<void*>(v,kSeats,seat);Put<std::uint64_t>(v,kSeatCount,1);
    Put<void*>(seat,kSeatRider,rider);Put<void*>(seat,kSeatRiderCtrl,ctrl);Put<int>(ctrl,8,1);
    Put<void*>(rider,0,image+edf::kDummyRiderVtable);
    Put<float>(v,kMatrix,1);Put<float>(v,kMatrix+40,1);Put<unsigned>(v,0x1AD0,1);
    auto neutral=[&](){Put<float>(seat,0x2C0,0);Put<float>(seat,0x2C4,0);++now;};
    auto listed=[&](){CommandUnit u[1]{};return TankCommandUnits(u,1);};
    NpcPostInput(v);Check(listed()==1,"NPC tank listed");
    Put<float>(v,kPosition+8,15);neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)>0,"recoil behind post causes reverse drive");
    neutral();Put<float>(seat,0x2C0,0.7f);NpcPostInput(v);
    Check(At<float>(seat,0x2C0)==0.7f && At<float>(seat,0x2C4)==0,"stock turret-alignment turn owns this frame");
    neutral();Put<float>(seat,0x2C4,-0.5f);NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==-0.5f,"stock throttle owns this frame");
    // An enemy far beyond a gun's reach, but inside the turret arc, must not prevent returning.
    unsigned char target[0x100]{},holder[0x20]{},weapon[0x230]{};void* holders[]={holder};
    Put<float>(target,kPosition+8,1000);Put<void*>(v,0x518,target);Put<void*>(v,0x520,ctrl);
    Put<void*>(seat,0xC8,holders);Put<std::uint64_t>(seat,0xD8,1);Put<void*>(holder,0x10,weapon);Put<float>(weapon,0x224,50);
    neutral();NpcPostInput(v);Check(At<float>(seat,0x2C4)>0,"far enemy does not suppress neutral-frame return");
    Command guard{Order::guard,{100,0,100}};
    Check(TankCommand(v,guard),"live tank accepts guard");
    Put<void*>(v,0x4A8,target);neutral();NpcPostInput(v);
    Check(listed()==0 && !TankCommand(v,guard),"script-controlled tank immediately loses command availability");
    Put<void*>(v,0x4A8,nullptr);neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0 && posts[0].at[2]==15 && posts[0].cmd.order==Order::none,
          "short route clears old order and adopts current position, not nearby old spawn");
    Put<void*>(seat,kSeatRiderCtrl,nullptr);neutral();NpcPostInput(v);
    Check(listed()==0 && !TankCommand(v,guard),"empty/player-owned driver cannot be commanded");
    Put<void*>(seat,kSeatRiderCtrl,ctrl);Put<float>(v,kPosition+8,25);neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0 && posts[0].at[2]==25,"new NPC driver starts at current position");
    v[kDead]=1;Check(listed()==0 && !TankCommand(v,guard),"dead tank rejected before next input");v[kDead]=0;
    Put<void*>(v,kSelfCtrl,otherCtrl);Check(listed()==0 && !TankCommand(v,guard),"reused object address rejected");Put<void*>(v,kSelfCtrl,ctrl);
    now+=501;Check(listed()==0 && !TankCommand(v,guard),"stale tank rejected");
    neutral();NpcPostInput(v);sessionOn=true;host=false;neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0 && !TankCommand(v,guard),"client cannot drive or command");
    Check(listed()==1,"client can select the remote NPC vehicle without writing its input");
    host=true;Put<float>(v,kPosition+8,45);neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)>0 && listed()==1,"AI authority can return and command its tank online");
    sessionOn=false;config.customNpcAi=false;Check(listed()==0 && !TankCommand(v,guard),"disabled AI cannot be commanded");
    ResetNpcPosts();Check(!posts[0].ref,"mission reset drops identity and orders");
    // A mech of the Begaruta family (slot 4 0x644350, no slot 72): no CarBase drive bit, and frames with no AI pass
    // clear nothing, so the plugin's own last stick must not read as the stock AI driving (2026-10-07).
    config.customNpcAi=true;
    alignas(16) unsigned char m[0x2700]{},mseat[kSeatStride]{},mrider[0x400]{},mctrl[0x20]{};
    alignas(16) unsigned char bones[2*kBoneStride]{},replacement[2*kBoneStride]{};
    const wchar_t spine[16]=L"spine",body[16]=L"body";
    Put<const wchar_t*>(bones,0,spine);Put<const wchar_t*>(bones,kBoneStride,body);
    Put<void*>(m+kModelInst506,kInstBones506,bones);Put<int>(m+kModelInst506,kInstBoneCount,2);
    Put<std::uint64_t>(m,kAttackSize,1); // navigation fixtures have a complete pre-existing native AI setup
    Put<void*>(m,kSelfCtrl,mctrl);
    Put<void*>(m,kSeats,mseat);Put<std::uint64_t>(m,kSeatCount,1);
    Put<void*>(mseat,kSeatRider,mrider);Put<void*>(mseat,kSeatRiderCtrl,mctrl);Put<int>(mctrl,8,1);
    Put<void*>(mrider,0,image+edf::kDummyRiderVtable);
    Put<float>(m,kMatrix,1);Put<float>(m,kMatrix+40,1);
    // InstallInputs replaces slot 4 before any NpcPostInput/SeatSwitchFrame runs. Use each real family
    // vtable and replace that slot just as the installed hook does; its stock function is no longer there.
    const unsigned mechVtables[]={0x17DA960,0x17DD440,0x17DE0A8,0x17DEC40};
    for(const unsigned rva:mechVtables) {
        auto mvt=reinterpret_cast<void**>(image+rva);
        Put<void**>(m,0,mvt);
        mvt[4]=image+0x644350;
        Check(NpcDrivable(m),"each stock Begaruta-family mech is NPC-drivable");
        mvt[4]=image+0x100;   // stand-in InputHook, never called
        Check(NpcDrivable(m),"a hooked Begaruta-family mech remains NPC-drivable");
        ResetNpcPosts();++now;NpcPostInput(m);
        CommandUnit units[1]{};
        Check(TankCommandUnits(units,1)==1 && std::strcmp(units[0].name,"MECH")==0,
              "each hooked mech is still listed as MECH");
        Check(TankCommand(m,guard),"each hooked mech accepts map orders");
    }
    ResetNpcPosts();
    ++now;NpcPostInput(m);
    {CommandUnit u[1]{};Check(TankCommandUnits(u,1)==1 && std::strcmp(u[0].name,"MECH")==0,"the mech listed on the map as MECH");}
    Put<float>(m,kPosition+8,-40);++now;NpcPostInput(m);
    const float steer=At<float>(mseat,0x2C0),throttle=At<float>(mseat,0x2C4);
    Check(throttle<0,"a mech behind its post drives forward (no drive bit needed)");
    ++now;NpcPostInput(m);   // no AI pass: nothing cleared the stick
    Check(At<float>(mseat,0x2C0)==steer && At<float>(mseat,0x2C4)==throttle && posts[0].active,
          "its own last stick is not the stock AI driving: it keeps driving");
    Put<float>(m,kPosition+8,0);++now;NpcPostInput(m);
    Check(At<float>(mseat,0x2C0)==0 && At<float>(mseat,0x2C4)==0 && !posts[0].active,"back on its post: the stick taken back");
    Put<float>(m,kPosition+8,-40);Put<float>(mseat,0x2C0,0.7f);++now;NpcPostInput(m);
    Check(At<float>(mseat,0x2C0)==0.7f && At<float>(mseat,0x2C4)==0,"the stock AI's own turn owns the frame");
    ResetNpcPosts();Put<void*>(mseat,kSeatRiderCtrl,nullptr);
    const float arrival[3]={0,0,120};
    Check(NpcPrepareVehiclePost(m,arrival),"support arrival accepted before its real driver boards");
    ++now;NpcPostInput(m);
    Check(pendingPosts[0].ref.Is(m),"empty vehicle keeps its pending arrival without moving");
    Put<void*>(mseat,kSeatRiderCtrl,mctrl);Put<float>(mseat,0x2C0,0);Put<float>(mseat,0x2C4,0);
    ++now;NpcPostInput(m);
    Check(!pendingPosts[0].ref && posts[0].at[2]==120 && posts[0].commanded,
          "arrival transfers to vehicle controller only once crew is seated");
    ResetNpcPosts();Check(!pendingPosts[0].ref,"new mission drops pending support destinations");
    // Production reproduction: a 4 m grid waypoint falls inside the ordinary 6 m guard hold.
    ResetNpcPosts();config.tankPostHold=6.0f;sessionOn=false;host=true;
    Put<float>(v,kPosition,0);Put<float>(v,kPosition+8,0);Put<void*>(v,0x4A8,nullptr);
    const float gridPoint[3]={0,0,4};
    Check(NpcPrepareVehiclePost(v,gridPoint),"ordinary post request accepted");neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0,"ordinary six metre guard radius is preserved");
    Check(NpcPrepareVehicleRoutePost(v,gridPoint,1.0f),"route supplies its own one metre arrival radius");neutral();NpcPostInput(v);
    const float routeThrottle=At<float>(seat,0x2C4);
    Check(routeThrottle<0 && routeThrottle>=-0.5f,"four metre route edge produces slow forward throttle");
    Put<float>(v,kPosition+8,2.6f);neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)<0,"driver continues inside planner 1.5m advancement radius until next waypoint arrives");
    Put<float>(v,kPosition+8,3.1f);++now;NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0 && !posts[0].active,"route stops and releases its throttle within explicit radius");
    Put<float>(v,kPosition+8,0);Check(NpcPrepareVehicleRoutePost(v,gridPoint,0.25f),"narrow route arrival is also explicit");neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)<0,"legacy 0.45m planner can use smaller 0.25m driver radius");
    const float stationary[3]={0,0,0};Check(NpcPrepareVehicleRoutePost(v,stationary,1.0f),"route waiting request uses current point");
    ++now;NpcPostInput(v);Check(At<float>(seat,0x2C4)==0,"route wait removes prior throttle before any new path is accepted");
    Check(!NpcPrepareVehicleRoutePost(v,gridPoint,0) && !NpcPrepareVehicleRoutePost(v,gridPoint,-1) &&
          !NpcPrepareVehicleRoutePost(v,gridPoint,std::numeric_limits<float>::quiet_NaN()),"invalid route radius rejected");
    Check(NpcPrepareVehicleRoutePost(v,gridPoint,1.0f),"pending route queued before user guard");
    Check(NpcPostCommand(v,gridPoint),"explicit user guard overrides route mode");neutral();NpcPostInput(v);
    Check(posts[0].routeArrival==0 && At<float>(seat,0x2C4)==0 && !pendingPosts[0].ref,
          "user guard restores six metre hold and removes a queued old route");
    Check(NpcPrepareVehicleRoutePost(v,gridPoint,1.0f),"route queued before driver changes");
    Put<void*>(rider,0,nullptr);rider[edf::kHumanPlayer]=1;Put<std::uint16_t>(rider,0x128,1);
    Put<float>(seat,0x2C4,0.73f);++now;NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0.73f && !pendingPosts[0].ref,"player takeover preserves human input and cancels pending route");
    Check(!NpcPrepareVehicleRoutePost(v,gridPoint,1.0f),"route producer cannot enqueue more waypoints under player control");
    rider[edf::kHumanPlayer]=0;Put<std::uint16_t>(rider,0x128,0);Put<void*>(rider,0,image+edf::kDummyRiderVtable);
    neutral();NpcPostInput(v);
    Check(posts[0].routeArrival==0 && At<float>(seat,0x2C4)==0,"new NPC after takeover does not resume stale route");
    // Native firing must never trust a non-null origin outside the current model's bone array.
    auto* mv=reinterpret_cast<void**>(image+0x17DA960);Put<void**>(m,0,mv);
    std::memcpy(image+kAttackRead,kAttackReadSig,sizeof(kAttackReadSig));
    std::memcpy(image+kAttackRelease,kAttackReleaseSig,sizeof(kAttackReleaseSig));
    Put<const wchar_t*>(bones,0,spine);Put<const wchar_t*>(bones,kBoneStride,body);
    Put<const wchar_t*>(replacement,0,spine);Put<const wchar_t*>(replacement,kBoneStride,body);
    Put<void*>(m+kModelInst506,kInstBones506,bones);Put<int>(m+kModelInst506,kInstBoneCount,2);
    Put<std::uint64_t>(m,kAttackSize,1);Put<std::uintptr_t>(m,kAimOrigin,0x70000481DULL);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==bones,"non-null corrupted firing origin restored from actual spine");
    Put<void*>(m+kModelInst506,kInstBones506,replacement);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==replacement,"model replacement refreshes stale firing bone");
    Put<void*>(m,kAimOrigin,replacement+1);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==replacement,"misaligned pointer is not a bone record");
    Put<void*>(m,kAimOrigin,replacement+kBoneStride);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==replacement+kBoneStride,"native valid custom origin is preserved");
    const wchar_t neck[16]=L"neck",arm[16]=L"arm";
    Put<void*>(m,kAimOrigin,nullptr);Put<const wchar_t*>(replacement,0,arm);Put<const wchar_t*>(replacement,kBoneStride,arm);
    Check(!PrepareNpcVehicleAi(m) && !At<void*>(m,kAimOrigin),"missing origin prevents native fire instead of dereferencing garbage");
    // V614_PROTEUS_MK2 has no spine: its seat-0 aim bone of begaruta_pilot_class (neck) is the origin (report #12).
    Put<const wchar_t*>(replacement,kBoneStride,neck);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==replacement+kBoneStride,"a model with no spine fires from its neck");
    Put<void*>(m,kAimOrigin,nullptr);Put<const wchar_t*>(replacement,0,spine);Put<const wchar_t*>(replacement,kBoneStride,body);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==replacement,"valid model becomes fire-ready again (spine first)");
    // The real Nix's spine axes are (up, back, left). Its MAB aiming locator rotates those axes back
    // into a forward/up aim frame. Exercise production SGO resolution and publication, not a new AI solver.
    {
        const wchar_t driverName[]=L"driver aim",leftName[]=L"left aim",rightName[]=L"right aim",podName[]=L"pod aim";
        const wchar_t* names[]={driverName,leftName,rightName,podName};
        for(unsigned i=0;i<4;++i) {shapeLocators[i].name=names[i];shapeLocators[i].bone=i%2;}
        const float boneFrame[16]={0,1,0,0, 0,0,-1,0, -1,0,0,0, 0,6.233363f,0,1};
        const float locatorLocal[16]={0,0,-1,0, 1,0,0,0, 0,-1,0,0, 0,0,0,1}; // native Rx(-pi/2) Ry(pi/2) Rz(pi)
        std::memcpy(replacement+kBoneWorld506,boneFrame,64);
        std::memcpy(replacement+kBoneStride+kBoneWorld506,boneFrame,64);
        for(auto& locator:shapeLocators)std::memcpy(locator.local,locatorLocal,64);
        ShapeValue scalar{};scalar.name=driverName;scalar.tag=1;
        SetShapeDeclaration(m,&scalar);
        for(const unsigned rva:mechVtables) {
            Put<void*>(m,0,image+rva);
            Check(PrepareNpcVehicleAi(m),"each Begaruta/Nix/Proteus family resolves its declared native aiming locator");
        }
        auto* table=At<MechShape*>(m,kMechAimShapes);
        Check(table==shapeStorage && At<void*>(m,kMechAimShapes+8)==table+1 &&
              table[0].definition==shapeLocators[0].definition && table[0].bone==replacement,
              "scalar locator is stored as one native definition/current-bone pair");
        Check(shapeResizes==1,"unchanged valid native table is not resized repeatedly");
        MechShape saved[4]{};std::memcpy(saved,table,sizeof(MechShape));
        const unsigned beforeResize=shapeResizes,beforeFind=shapeFinds;
        Check(PrepareNpcVehicleAi(m) && shapeResizes==beforeResize && shapeFinds==beforeFind+1 &&
              !std::memcmp(saved,table,sizeof(MechShape)),"an existing native table is read-only when resolved pairs match");
        float calibrated[16]{};
        const auto* world=reinterpret_cast<const float*>(table[0].bone+kBoneWorld506);
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)
            for(unsigned k=0;k<4;++k)calibrated[row*4+col]+=locatorLocal[row*4+k]*world[k*4+col];
        const float forward[3]={0,0,100};float oldLocal[3]{},newLocal[3]{};
        for(unsigned row=0;row<3;++row)for(unsigned k=0;k<3;++k) {
            oldLocal[row]+=forward[k]*world[row*4+k];newLocal[row]+=forward[k]*calibrated[row*4+k];
        }
        Check(oldLocal[0]==0 && oldLocal[1]==-100 && oldLocal[2]==0,
              "real Nix spine fallback interprets a level forward target as vertically down");
        Check(std::fabs(newLocal[0])<0.0001f && std::fabs(newLocal[1])<0.0001f && std::fabs(newLocal[2]-100)<0.0001f,
              "native locator calibration restores a level forward aim direction");
        ShapeValue entries[4]{};for(unsigned i=0;i<4;++i){entries[i].name=names[i];entries[i].tag=static_cast<std::uint16_t>(1+i%2);}
        ShapeValue array{};array.type=3;array.entries=entries;array.count=4;array.tag=2;
        SetShapeDeclaration(m,&array);
        Check(PrepareNpcVehicleAi(m) && At<void*>(m,kMechAimShapes+8)==table+4,"array config allocates one locator row per original seat index");
        Check(table[0].definition==shapeLocators[0].definition && table[1].definition==shapeLocators[1].definition &&
              table[2].definition==shapeLocators[2].definition && table[3].definition==shapeLocators[3].definition,
              "driver, shoulder/arm gunner and pod locators retain their SGO seat order");
        ShapeValue opaque{};opaque.type=3;opaque.count=-1;opaque.tag=0;SetShapeDeclaration(m,&opaque);
        Check(!PrepareNpcVehicleAi(m) && shapeResizes==0,"an unsupported document-valued array is refused");
        Check(shapeReleases[0]>0 && shapeReleases[1]>0 && shapeReleases[2]>0,"all three native temporary variant alternatives are released");
        SetShapeDeclaration(m,&array);
        std::memcpy(saved,table,sizeof(saved));
        Put<void*>(m+kModelInst506,kInstBones506,bones);
        Check(PrepareNpcVehicleAi(m) && table[0].bone==bones && table[1].bone==bones+kBoneStride &&
              shapeResizes==0,"model replacement refreshes every borrowed bone without resizing unchanged seat count");
        std::memcpy(saved,table,sizeof(saved));
        entries[2].name=L"missing declared locator";SetShapeDeclaration(m,&array);
        Check(!PrepareNpcVehicleAi(m) && shapeResizes==0 && !std::memcmp(saved,table,sizeof(saved)),
              "one unresolved declared locator refuses native AI without publishing a partial table");
        entries[2].name=rightName;SetShapeDeclaration(m,&array);shapeFailAt=2;
        Check(!PrepareNpcVehicleAi(m) && shapeResizes==0 && !std::memcmp(saved,table,sizeof(saved)),
              "native lookup failure after earlier valid rows leaves the live table untouched");
        SetShapeDeclaration(m,&array);entries[1].type=0;
        Check(!PrepareNpcVehicleAi(m) && shapeResizes==0 && !std::memcmp(saved,table,sizeof(saved)),
              "malformed array element is refused without compressing the later seat indexes");
        entries[1].type=1;array.count=17;SetShapeDeclaration(m,&array);
        Check(!PrepareNpcVehicleAi(m) && shapeResizes==0,"oversized locator array is refused before native resize");
        array.count=4;SetShapeDeclaration(m,&scalar);shapeResizeFails=true;
        Check(!PrepareNpcVehicleAi(m) && shapeResizes==1 && !std::memcmp(saved,table,sizeof(saved)) &&
              At<void*>(m,kMechAimShapes+8)==table+4,"native vector allocation exception is caught without exposing uninitialized rows");
        SetShapeDeclaration(m,&scalar);
        auto* savedEnd=At<void*>(m,kMechAimShapes+8);Put<void*>(m,kMechAimShapes+8,reinterpret_cast<unsigned char*>(table)+1);
        Check(!PrepareNpcVehicleAi(m) && shapeResizes==0,"malformed old vector is refused rather than reset by native resize");
        Put<void*>(m,kMechAimShapes+8,savedEnd);
        SetShapeDeclaration(m,nullptr);
        Check(!PrepareNpcVehicleAi(m) && shapeResizes==0 && !std::memcmp(saved,table,sizeof(saved)),
              "missing declaration cannot authenticate an old nonempty table after a resource change");
        std::memset(m+kMechAimShapes,0,24);SetShapeDeclaration(m,nullptr);
        Put<std::uint16_t>(m+0x100,0x10,ifcmodel::kValueless);
        Check(!PrepareNpcVehicleAi(m) && shapeFinds==0 && shapeResizes==0,
              "unknown valueless SGO is not mistaken for a confirmed missing locator declaration");
        Put<std::uint16_t>(m+0x100,0x10,77);
        Check(!PrepareNpcVehicleAi(m) && shapeFinds==0 && shapeResizes==0,"unknown SGO alternative refuses native AI");
        SetShapeDeclaration(m,nullptr);
        shapeVisitorFails=true;
        Check(!PrepareNpcVehicleAi(m) && shapeFinds==0 && shapeResizes==0,
              "visitor exception with an empty table remains unknown instead of admitting the fallback");
        shapeVisitorFails=false;
        Check(PrepareNpcVehicleAi(m) && shapeFinds==0 && shapeResizes==0 && !At<void*>(m,kMechAimShapes),
              "an older SGO without an aiming declaration keeps its empty native spine fallback");
        SetShapeDeclaration(m,&scalar);Put<void*>(mrider,0,nullptr);mrider[edf::kHumanPlayer]=1;
        Check(!PrepareNpcVehicleAi(m) && shapeFinds==0 && shapeResizes==0,"a player driver never enters locator initialization");
        mrider[edf::kHumanPlayer]=0;Put<void*>(mrider,0,image+edf::kDummyRiderVtable);sessionOn=true;host=false;
        Check(!PrepareNpcVehicleAi(m) && shapeFinds==0 && shapeResizes==0,"a remote NPC authority never initializes borrowed model pointers");
        sessionOn=false;host=true;
        alignas(16) unsigned char failSeats[5*kSeatStride]{},gunner[0x400]{},human[0x400]{},remote[0x400]{},riderCtrl[3][0x20]{};
        std::memcpy(failSeats,mseat,kSeatStride);
        unsigned char* occupants[]={gunner,human,remote};
        for(unsigned i=0;i<3;++i) {
            auto* s=failSeats+(i+1)*kSeatStride;Put<void*>(s,kSeatRider,occupants[i]);Put<void*>(s,kSeatRiderCtrl,riderCtrl[i]);
            Put<int>(riderCtrl[i],8,1);Put<void*>(occupants[i],0,image+0x17CDF28);Put<float>(occupants[i],0x2F8,100.0f);
        }
        human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);
        remote[edf::kHumanPlayer]=1;remote[edf::kRiderNet+edf::kNetFlags]=1;
        Put<void*>(m,kSeats,failSeats);Put<std::uint64_t>(m,kSeatCount,5);
        auto fillInputs=[&]() {
            for(unsigned i=0;i<5;++i) {
                auto* s=failSeats+i*kSeatStride;std::memset(s+0x2C0,0x5A,0x2A);s[0x2BF]=0xA6;s[0x2EA]=0xB7;
            }
        };
        unsigned char neutralInputs[0x2A]{};Put<float>(neutralInputs,0xC,1.0f);Put<float>(neutralInputs,0x1C,1.0f);
        fillInputs();unsigned char otherSeats[3*kSeatStride]{};std::memcpy(otherSeats,failSeats+2*kSeatStride,sizeof(otherSeats));
        scalar.name=L"unresolved while last frame held fire";SetShapeDeclaration(m,&scalar);
        Check(!PrepareNpcVehicleAi(m) && !std::memcmp(failSeats+0x2C0,neutralInputs,sizeof(neutralInputs)) &&
              !std::memcmp(failSeats+kSeatStride+0x2C0,neutralInputs,sizeof(neutralInputs)),
              "failed native preparation neutralizes the NPC driver's and owned real gunner's old aiming and triggers");
        Check(!std::memcmp(otherSeats,failSeats+2*kSeatStride,sizeof(otherSeats)),
              "failure preserves local-player, remote-player and empty-seat input and metadata byte for byte");
        Check(failSeats[0x2BF]==0xA6 && failSeats[0x2EA]==0xB7 &&
              failSeats[kSeatStride+0x2BF]==0xA6 && failSeats[kSeatStride+0x2EA]==0xB7,
              "neutralization changes only the native input block, preserving adjacent seat fields");
        scalar.name=driverName;SetShapeDeclaration(m,&scalar);Put<float>(failSeats,0x2D0,0.37f);Put<float>(failSeats,0x2E4,0.8f);
        Check(PrepareNpcVehicleAi(m) && At<float>(failSeats,0x2D0)==0.37f && At<float>(failSeats,0x2E4)==0.8f,
              "metadata recovery permits normal preparation and preserves current native AI intent");
        scalar.name=L"unresolved again";SetShapeDeclaration(m,&scalar);fillInputs();ResetNpcPosts();++now;NpcPostInput(m);
        Check(!std::memcmp(failSeats+0x2D0,neutralInputs+0x10,0x1A) &&
              !std::memcmp(failSeats+kSeatStride+0x2C0,neutralInputs,sizeof(neutralInputs)),
              "input-hook preparation failure also releases held aiming and fire before the stock input reads them");
        Check(posts[0].ref.Is(m),"failed aiming preparation keeps ordinary map/post navigation available");
        fillInputs();sessionOn=true;host=false;
        unsigned char unchanged[2*kSeatStride]{};std::memcpy(unchanged,failSeats,sizeof(unchanged));
        Check(!PrepareNpcVehicleAi(m) && !std::memcmp(unchanged,failSeats,sizeof(unchanged)),
              "a peer without NPC authority never neutralizes another machine's input");
        sessionOn=false;host=true;config.enabled=false;
        Check(!PrepareNpcVehicleAi(m) && !std::memcmp(unchanged,failSeats,sizeof(unchanged)),
              "disabled plugin never reclaims native NPC seat intent");
        config.enabled=true;Put<void*>(failSeats,kSeatRider,human);Put<void*>(failSeats,kSeatRiderCtrl,riderCtrl[1]);
        std::memcpy(unchanged,failSeats,sizeof(unchanged));
        Check(!PrepareNpcVehicleAi(m) && !std::memcmp(unchanged,failSeats,sizeof(unchanged)),
              "player takeover prevents both driver and passenger input reclamation");
        Put<void*>(m,kSeats,mseat);Put<std::uint64_t>(m,kSeatCount,1);ResetNpcPosts();
        std::memset(m+kMechAimShapes,0,24);SetShapeDeclaration(m,nullptr);
        Put<void*>(m+kModelInst506,kInstBones506,replacement);Put<void*>(m,kAimOrigin,replacement);
    }
    // The Proteus's weapons are on seats 1-3 and none on seat 0 (vehicle_weapon_setting): its NPC driver's table holds a
    // row per gunner seat's weapon (its AI think runs the fire entry for seats 1..n-1), not an empty seat-0 table.
    {
        alignas(16) static unsigned char pseats[4*kSeatStride]{},pholder[3][0x20]{},pweapon[3][0x230]{};
        static void* pholders[3][1]{};
        alignas(16) static unsigned char rowsBuf[4*16]{};
        std::memcpy(pseats,mseat,kSeatStride);   // seat 0: the same NPC driver, no weapon
        for(int k=1;k<=3;++k) {
            unsigned char* const s=pseats+k*kSeatStride;
            pholders[k-1][0]=pholder[k-1];Put<void*>(pholder[k-1],kHolderWeapon,pweapon[k-1]);Put<float>(pweapon[k-1],kWeaponReach,500.0f);
            Put<void*>(s,kSeatWeapons,pholders[k-1]);Put<std::uint64_t>(s,kSeatWeaponCount,1);
        }
        Put<void*>(m,kSeats,pseats);Put<std::uint64_t>(m,kSeatCount,4);
        Put<std::uint64_t>(m,kAttackSize,0);Put<void*>(m,kAttackData,rowsBuf);Put<std::uint64_t>(m,kAttackCap,4);
        Check(PrepareNpcVehicleAi(m) && At<std::uint64_t>(m,kAttackSize)==3,"a Proteus-like mech gets one row per gunner seat weapon");
        const auto* rows=reinterpret_cast<const AttackRow*>(rowsBuf);
        Check(rows[0].seat==1 && rows[1].seat==2 && rows[2].seat==3 && rows[0].weapon==0 && rows[2].maxRange==500.0f,
              "the rows name their own seats");
        Put<void*>(m,kSeats,mseat);Put<std::uint64_t>(m,kSeatCount,1);Put<std::uint64_t>(m,kAttackSize,1);
    }
    // Turning off automatic return must not remove a crewed vehicle from the command map.
    config.tankReturnToPost=false;ResetNpcPosts();neutral();NpcPostInput(v);
    Check(listed()==1 && TankCommand(v,guard),"explicit map orders remain available with automatic return off");
    Put<void*>(target,kSelfCtrl,otherCtrl);Put<int>(otherCtrl,8,1);Put<int>(otherCtrl,12,1);focusEnemy=target;
    const ObjRef focused=ObjRef::Of(target);Command focus{Order::focus,{0,0,0}};
    Check(TankCommand(v,focus,focused),"NPC vehicle accepts a live enemy focus");
    Check(At<int>(otherCtrl,12)==2,"map focus owns one weak reference");
    Check(PrepareNpcVehicleAi(v) && At<void*>(v,kTarget)==target,"map focus reaches the native AI target");
    focusEnemy=nullptr;
    Check(!TankCommand(v,focus,focused),"friendly or absent target cannot be focused");
    Check(TankCommand(v,Command{}),"release clears focus");
    Check(At<int>(otherCtrl,12)==2,"clearing map focus releases its reference while native AI retains its own");
    // EDF4.1 MP004 (2026-10-17, docs/feedback-2026-10-17-balam-npc.md): a mission's AI Balam (Vehicle501_FortressRobo,
    // RideAi'd by its transporter formation) walks after the Erginus far past NpcLeash from where its crew first sat.
    // The stock walk 0x604400 is never reached: the fixture image is not executable, so reaching it would fault here.
    {
        config=Config{};sessionOn=false;host=true;focusEnemy=nullptr;ResetNpcPosts();
        std::memcpy(image+kBargaWalk,kBargaWalkSig,sizeof(kBargaWalkSig));
        alignas(16) static unsigned char b[0x2700]{},bseat[kSeatStride]{},brider[0x400]{},bctrl[0x20]{};
        Put<void*>(b,0,image+kVtBarga);Put<void*>(b,kSelfCtrl,bctrl);
        Put<void*>(b,kSeats,bseat);Put<std::uint64_t>(b,kSeatCount,1);
        Put<void*>(bseat,kSeatRider,brider);Put<void*>(bseat,kSeatRiderCtrl,bctrl);Put<int>(bctrl,8,1);
        Put<void*>(brider,0,image+edf::kDummyRiderVtable);
        Put<float>(b,kMatrix,1);Put<float>(b,kMatrix+40,1);
        missionAi=true;
        ++now;NpcPostInput(b);
        Check(posts[0].ref.Is(b),"the mission Balam is seen (listed on the map, commandable)");
        // Landed and walking at the Erginus 200 m on: the stock AI's walk (turn, forward) is in its block.
        Put<float>(b,kPosition+8,200);
        Put<float>(b,kBargaBlock+kBargaTurn,0.25f);Put<float>(b,kBargaBlock+kBargaForward,1.0f);
        ++now;NpcPostInput(b);
        Check(At<float>(b,kBargaBlock+kBargaTurn)==0.25f && At<float>(b,kBargaBlock+kBargaForward)==1.0f && !posts[0].active,
              "a mission's AI Balam past the leash keeps its stock walk at the enemy (not walked back to its post)");
        Check(At<float>(bseat,kSeatThrottle)==0 && At<float>(bseat,kSeatRightX)==0,"nor is its seat 0 stick written");
        // Idle (no stock walk, no target) far from where it was first seen: still not walked back.
        Put<float>(b,kBargaBlock+kBargaTurn,0);Put<float>(b,kBargaBlock+kBargaForward,0);
        ++now;NpcPostInput(b);
        Check(At<float>(b,kBargaBlock+kBargaTurn)==0 && At<float>(b,kBargaBlock+kBargaForward)==0 && !posts[0].active,
              "an idle mission Balam is not walked back to where its crew first sat either");
        CommandUnit u[1]{};
        Check(TankCommandUnits(u,1)==1 && std::strcmp(u[0].name,"MECH")==0,"it stays on the map");
        Check(TankCommand(b,guard) && posts[0].commanded,"a map guard order gives it a post (then the leash is the player's)");
        missionAi=false;ResetNpcPosts();
    }
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("npc_post_test: %d checks passed\n",checks);
}
