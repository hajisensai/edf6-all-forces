// Production control lifecycle plus optional execution of the real EDF weapon callbacks in a private mapping.
#define main PreviousProteusFrameChecks
#include "proteus_frame_test.cpp"
#undef main
namespace {
using namespace crew;
alignas(16) unsigned char left[0x1600]{},right[0x1600]{},launcher[0x1600]{},holders[3][0x48]{},weaponCtrl[16]{};
alignas(16) unsigned char muzzles[2*edf::kMuzzleStride]{},bone[0x100]{},oldRider[0x400]{},oldCtrl[16]{};
unsigned char* lists[3][1]={{holders[0]},{holders[1]},{holders[2]}};
int posts=0,activated=0,deactivated=0,axesApplied=0;
void __fastcall AxisApplied(void*,bool){++axesApplied;}
void __fastcall OnActive(void*){++activated;}
void __fastcall OnEmpty(void*){++deactivated;}
bool __fastcall Ready(void*){return true;}
void __fastcall FakeEnable(void* w){static_cast<unsigned char*>(w)[0x13E]=1;OnActive(w);}
void __fastcall FakeDisable(void* w){static_cast<unsigned char*>(w)[0x13E]=0;OnEmpty(w);}
void __fastcall FakePull(void* h){At<unsigned char*>(h,kHolderWeapon)[kPull]=1;}
const void* __fastcall FakeUser(void*,const void* w){return w==left ? human+0x120 : oldRider+0x120;}
void __fastcall FakePost(void*,const float*){++posts;Put<float>(bone,edf::kBoneRows+48,100);}
void Jump(unsigned rva,void* target){unsigned char b[12]={0x48,0xB8};b[10]=0xFF;b[11]=0xE0;std::memcpy(b+2,&target,8);std::memcpy(image+rva,b,sizeof(b));}
void InitWeapon(unsigned char* w,unsigned i) {
    Put<void*>(holders[i],kHolderCtrl,weaponCtrl);Put<void*>(holders[i],kHolderWeapon,w);
    auto seat=SeatAt(vehicle,i+1);Put<void*>(seat,kSeatWeapons,lists[i]);Put<std::uint64_t>(seat,kSeatWeaponCount,1);
    Put<void*>(w,edf::kMuzzles,muzzles);Put<std::uint64_t>(w,edf::kMuzzleCount,2);
    proteus::pose::Identity(reinterpret_cast<float*>(w+edf::kWeaponMatrix));
    Put<float>(w,kRate,1);Put<float>(w,kSpread,1);
}
void Setup(bool native){
    ok=true;config.proteus=true;config.proteusDriverGun=true;config.proteusFieldRadius=0;roundsReady=true;
    Put<void*>(vehicle,0,image+kVtBig);Put<void*>(vehicle,kSelfCtrl,ctrl);Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,4);
    Put<float>(vehicle,kHpMax,7500);Put<float>(vehicle,kHp,7500);Put<int>(ctrl,8,1);Put<int>(ctrl,12,2);Put<int>(weaponCtrl,8,1);
    Put<void*>(human,0,image+kVtRanger);Put<void*>(human,kSelfCtrl,ctrl);Put<float>(human,kHp,100);
    human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);Put<std::uint16_t>(human,0x128,2);
    Put<void*>(seats,kSeatRider,human);Put<void*>(seats,kSeatRiderCtrl,ctrl);
    InitWeapon(left,0);InitWeapon(right,1);InitWeapon(launcher,2);
    proteus::pose::Identity(reinterpret_cast<float*>(bone+edf::kBoneRows));
    for(int i=0;i<2;++i){
        auto m=muzzles+i*edf::kMuzzleStride;Put<void*>(m,0,bone);Put<int>(m,edf::kMuzzleMode,1);
        proteus::pose::Identity(reinterpret_cast<float*>(m+edf::kMuzzleLocal));
        Put<float>(m,edf::kMuzzleLocal+48,i ? 3.0f : -3.0f);Put<float>(m,edf::kMuzzleLocal+52,10);Put<float>(m,edf::kMuzzleLocal+56,9);
    }
    nextWeaponPost=reinterpret_cast<void*>(&FakePost);
    nativeEmpty=native ? reinterpret_cast<WeaponEnableFn>(image+0x690230) : &FakeDisable;
    nativeActive=native ? reinterpret_cast<WeaponEnableFn>(image+0x68F8E0) : &FakeEnable;
    nextUser=native ? reinterpret_cast<UserFn>(image+kUserFn) : &FakeUser;
    buildMuzzle=native ? reinterpret_cast<BuildMuzzleFn>(image+kNativeMuzzle) : &FakeMuzzle;
    if(!native)Jump(kPullFn,reinterpret_cast<void*>(&FakePull));
    auto u=UnitOf(vehicle,true);u->active=u->closed=true;u->st.mode=proteus::Mode::deployed;RefreshWeapons(*u,vehicle);
}
void Lifecycle(){
    auto& u=*UnitOf(vehicle,false);
    Jump(kAxisApply,reinterpret_cast<void*>(&AxisApplied));
    for(unsigned i=0;i<4;++i)for(int axis=0;axis<2;++axis){
        auto a=seataim::Object(SeatAt(vehicle,i))+edf::kAimAxes+axis*edf::kAxisStride;
        Put<float>(a,edf::kAxisMin,-.2f);Put<float>(a,edf::kAxisMax,.2f);Put<float>(a,edf::kAxisAngle,i==0 ? .3f : -.1f);
    }
    FollowCannon(seataim::Object(SeatAt(vehicle,kRightSeat)));
    Check(At<float>(seataim::Object(SeatAt(vehicle,kRightSeat)),edf::kAimAxes+edf::kAxisAngle)==.2f && axesApplied==2,
          "driver control updates both actual cannon axes and obeys destination stops");
    Put<void*>(seats+kSeatStride,kSeatRider,human);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,ctrl);
    FollowCannon(seataim::Object(SeatAt(vehicle,kRightSeat)));
    FollowCannon(seataim::Object(SeatAt(vehicle,kLauncherSeat)));
    Check(At<float>(seataim::Object(SeatAt(vehicle,kRightSeat)),edf::kAimAxes+edf::kAxisAngle)==-.1f &&
          At<float>(seataim::Object(SeatAt(vehicle,kLauncherSeat)),edf::kAimAxes+edf::kAxisAngle)==.2f,
          "gunner owns paired cannons while driver independently aims the salvo launcher");
    Put<void*>(seats+2*kSeatStride,kSeatRider,human);Put<void*>(seats+2*kSeatStride,kSeatRiderCtrl,ctrl);
    const int applied=axesApplied;FollowCannon(seataim::Object(SeatAt(vehicle,kRightSeat)));
    Check(axesApplied==applied,"actual right occupant prevents paired or driver aim override");
    Put<void*>(seats+kSeatStride,kSeatRider,nullptr);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,nullptr);
    Put<void*>(seats+2*kSeatStride,kSeatRider,nullptr);Put<void*>(seats+2*kSeatStride,kSeatRiderCtrl,nullptr);
Put<float>(seats,kSeatFire,1);
    QueueWeapons(u,vehicle,true,seats);Check(gunShots==0,"slot4 only queues, never fires from old pose");
    ProteusWeaponPost(vehicle,nullptr);
    Check(posts==1 && gunShots==1 && lastFrom[0]==97,"post wrapper fires once after the original refreshed physical muzzle");
    ProteusWeaponPost(vehicle,nullptr);Check(posts==2 && gunShots==1,"original post still chains but duplicate callback cannot double fire");
    StockArm sight{};Check(ProteusSightWeapon(vehicle,0)==right && ProteusDriverSight(vehicle,&sight),"driver without a stock holder exposes actual right-cannon sight");
    Check(sight.aimed && sight.physicalOnly && sight.paths==1 && sight.bore[2]==1 && std::fabs(sight.flight-2000.0f/960)<1e-4f && sight.at[0]==lastAt[0],
        "driver bore/impact matches shot and uses custom round speed, not stock cannon speed");
    auto arm=&u;
    for(int reason=0;reason<7;++reason){
        ++frame;now+=1000;u.salvoLeft=2;u.mark=human;u.markAt[2]=200;QueueWeapons(u,vehicle,true,seats);
        if(reason==0)Put<void*>(seats,kSeatRider,nullptr);
        if(reason==1)human[kDead]=1;
        if(reason==2)config.enabled=false;
        if(reason==3)vehicle[kDead]=1;
        if(reason==4)u.net.remote=true;
        if(reason==5)++frame;
        if(reason==6)Put<void*>(human,kSelfCtrl,oldCtrl);
        FlushWeapons(vehicle);
        Check(gunShots==1 && salvoShots==0 && !u.queuedDriver && u.salvoLeft==0,"departure/death/disabled/remote/stale frame/reused rider cancels pending fire and salvo");
        Put<void*>(seats,kSeatRider,human);human[kDead]=0;vehicle[kDead]=0;config.enabled=true;u.net.remote=false;Put<void*>(human,kSelfCtrl,ctrl);
    }
    u.st.mode=proteus::Mode::deployed;u.closed=true;u.active=true;
    ++frame;QueueWeapons(u,vehicle,true,seats);ResetProteus();FlushWeapons(vehicle);
    Check(!arm->queuedDriver && gunShots==1,"mission reset retires queued fire");
}
void Native(){
    auto& u=*UnitOf(vehicle,false);
    static void* vt[30]{};vt[14]=reinterpret_cast<void*>(&OnActive);vt[15]=reinterpret_cast<void*>(&OnEmpty);vt[26]=reinterpret_cast<void*>(&Ready);
    unsigned char view[0x58]{},slot[0x28]{},prop[0x60]{};int index=0;
    Put<void*>(right,0,vt);right[0x7F0]=1;Put<void*>(right,0x120,vehicle);
    Put<void*>(right,0x1528,view);Put<std::uint64_t>(view,0x18,1);Put<void*>(view,8,&index);Put<void*>(view,0x28,slot);Put<void*>(slot,0,prop);
    Put<void*>(seats+kSeatStride,kSeatRider,human);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,ctrl);
    Put<void*>(seats+2*kSeatStride,0x300,oldRider);Put<void*>(seats+2*kSeatStride,0x308,oldCtrl);Put<int>(oldCtrl,8,1);Put<int>(oldCtrl,12,2);
    Check(nextUser(vehicle+kUserIface,right)==oldRider+0x120,"real 62D950 returns stale last rider for empty right seat");
    Check(UserHook(vehicle+kUserIface,right)==human+0x120,"production operator overrides stale native result with current left soldier");
    Check(At<int>(ctrl,8)==1 && At<int>(ctrl,12)==2 && At<int>(oldCtrl,8)==1 && At<int>(oldCtrl,12)==2,"operator lookup balances real native weak references");
    left[kPull]=1;ProteusEmptyWeapon(right);
    const auto ready=reinterpret_cast<bool(__fastcall*)(void*)>(image+0x6912F0);
    Check(right[0x13E]==1 && right[kPull]==1 && activated==1 && ready(right),"native activate plus holder pull passes real ready gate for empty paired cannon");
    right[kPull]=0;netSession=true;Put<std::uint16_t>(human,0x128,1);ProteusEmptyWeapon(right);
    Check(!right[kPull],"remote left gunner never creates second native trigger");
    netSession=false;Put<std::uint16_t>(human,0x128,2);
    Put<void*>(seats+2*kSeatStride,kSeatRider,oldRider);Put<void*>(seats+2*kSeatStride,kSeatRiderCtrl,oldCtrl);
    Check(UserHook(vehicle+kUserIface,right)==oldRider+0x120,"occupied right seat keeps its genuine native operator");
    Put<void*>(seats+2*kSeatStride,kSeatRider,nullptr);Put<void*>(seats+2*kSeatStride,kSeatRiderCtrl,nullptr);
    human[kDead]=1;ProteusEmptyWeapon(right);
    Check(!right[0x13E] && !ready(right) && !UserHook(vehicle+kUserIface,right),"dead left soldier stops both native activation and operator");human[kDead]=0;
    ProteusEmptyWeapon(right);config.enabled=false;ProteusEmptyWeapon(right);
    Check(!right[0x13E] && deactivated==2,"disabled rework delegates actual native empty-seat deactivation");config.enabled=true;
    Put<void*>(seats+kSeatStride,kSeatRider,nullptr);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,nullptr);
    float p[3],d[3];
    for(int mode=0;mode<3;++mode){
        Put<int>(muzzles,edf::kMuzzleMode,mode);Put<float>(muzzles,0xD0,1);Put<float>(muzzles,0xD4,0);Put<float>(muzzles,0xD8,0);Put<float>(muzzles,0xDC,0);
        unsigned char saved[edf::kMuzzleStride];std::memcpy(saved,muzzles,sizeof(saved));
        Check(RoundFrom(u,kRightSeat,0,p,d) && p[0]==-3 && p[1]==10 && p[2]==9,"production muzzle reads native firing matrix for all 3 orientation modes");
        Check(std::memcmp(saved,muzzles,sizeof(saved))==0,"muzzle getter does not mutate native shot cache");
        if(mode==2)Check(std::fabs(d[0]-1)<1e-5f && std::fabs(d[2])<1e-5f,"native mode2 FireVector is honored instead of incorrect bone-forward fallback");
    }
    Put<int>(muzzles,edf::kMuzzleMode,1);
    // The LEA's original target and native phase order, not a copied C++ implementation.
    std::int32_t rel=At<std::int32_t>(image,0x6302B3);
    Check(image+0x6302B7+rel==image+0x690230 && At<const void*>(image,kWeaponPostSlot)==image+0x644910 &&
          image[0x644910]==0xE9 && image+0x644915+At<std::int32_t>(image,0x644911)==image+0x630250,
          "verified actual Big slot5 tail-jump and native empty callback target");
    Check(InstallProteusWeapons() && nextWeaponPost==image+0x644910 &&
          At<const void*>(image,kWeaponPostSlot)==reinterpret_cast<void*>(&ProteusWeaponPost),
          "production installer chains genuine Big slot5 in private DLL mapping");
    const auto patchedEmpty=reinterpret_cast<WeaponEnableFn>(image+0x6302B7+At<std::int32_t>(image,0x6302B3));
    config.enabled=false;right[0x13E]=1;patchedEmpty(right);
    Check(!right[0x13E],"real patched callback thunk delegates stock disable when plugin is off");

}
}
LONG CALLBACK NativeFault(EXCEPTION_POINTERS* p){
    std::printf("native fault code=%lx rva=%llx access=%llx\n",p->ExceptionRecord->ExceptionCode,
        static_cast<unsigned long long>(p->ContextRecord->Rip-reinterpret_cast<std::uintptr_t>(crew::image)),
        static_cast<unsigned long long>(p->ExceptionRecord->ExceptionInformation[1]));return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,&NativeFault);
    const bool native=argc>1;
    if(native){image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));if(!image){std::puts("EDF.dll unavailable");return 77;}}
    else image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    if(native){
        // Only resolve the native muzzle builder's CRT math imports; no game initializer or DllMain runs.
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
        if(nt->FileHeader.TimeDateStamp!=0x678CCB46)return 77;
        auto imp=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        for(;imp->Name;++imp) {
            const auto name=reinterpret_cast<const char*>(image+imp->Name);
            if(std::strcmp(name,"api-ms-win-crt-math-l1-1-0.dll"))continue;
            const auto lib=LoadLibraryA(name);if(!lib)return 2;
            auto src=reinterpret_cast<const IMAGE_THUNK_DATA64*>(image+imp->OriginalFirstThunk);
            auto dst=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+imp->FirstThunk);
            for(;src->u1.AddressOfData;++src,++dst) {
                if(IMAGE_SNAP_BY_ORDINAL64(src->u1.Ordinal))return 2;
                const auto entry=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(image+src->u1.AddressOfData);
                const auto proc=GetProcAddress(lib,reinterpret_cast<const char*>(entry->Name));if(!proc)return 2;
                DWORD old=0;VirtualProtect(dst,sizeof(*dst),PAGE_READWRITE,&old);dst->u1.Function=reinterpret_cast<ULONGLONG>(proc);VirtualProtect(dst,sizeof(*dst),old,&old);
            }
        }
    }
    Setup(native);
    if(native)Native();else Lifecycle();
    std::printf("proteus_weapon_test %s: %d checks, %d failures\n",native ? "native" : "production",checks,failures);
    return failures ? 1 : 0;
}
