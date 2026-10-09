// The production Proteus weapons and shield against the real EDF.dll (argv[1]), mapped privately with
// DONT_RESOLVE_DLL_REFERENCES: no DllMain, no game initialiser; only the CRT math imports the native code reaches are
// bound. The real functions run on fixture objects: the weapon user lookup 0x62D950, activation 0x68F8E0, the holder
// pull 0x62C000 and the ready gate 0x6912F0, the patched empty-seat callback, the BarrierBullet01 hit handler
// 0x2913B0; the installers patch / chain the real slots. Without argv[1] it runs the fake-image fixture's checks.
#define main PreviousProteusFrameChecks
#include "proteus_frame_test.cpp"
#undef main
namespace {
using namespace crew;
alignas(16) unsigned char oldRider[0x400]{},oldCtrl[16]{};
int onActive=0,onEmpty=0;
void __fastcall OnActive(void*){++onActive;}
void __fastcall OnEmpty(void*){++onEmpty;}
bool __fastcall Ready(void*){return true;}

void Native() {
    Setup();
    nativeEmpty=reinterpret_cast<WeaponEnableFn>(image+kNativeEmpty);
    nativeActive=reinterpret_cast<WeaponEnableFn>(image+kNativeActive);
    nextUser=reinterpret_cast<UserFn>(image+kUserFn);
    Tick();
    Unit& u=*UnitOf(vehicle,false);
    u.st.mode=proteus::Mode::deployed;
    // A weapon object the real activation / ready gate accept (their vtable slots and view).
    static void* vt[30]{};vt[14]=reinterpret_cast<void*>(&OnActive);vt[15]=reinterpret_cast<void*>(&OnEmpty);vt[26]=reinterpret_cast<void*>(&Ready);
    static unsigned char view[0x58]{},slot[0x28]{},prop[0x60]{};static int index=0;
    unsigned char* const mounts[]={cannonL,cannonR,launcherW};
    for(unsigned char* w:mounts) {
        Put<void*>(w,0,vt);w[0x7F0]=1;Put<void*>(w,0x120,vehicle);
        Put<void*>(w,0x1528,view);
    }
    Put<std::uint64_t>(view,0x18,1);Put<void*>(view,8,&index);Put<void*>(view,0x28,slot);Put<void*>(slot,0,prop);
    // The real user lookup names the empty right seat's stale LAST rider; the borrowing driver overrides it.
    Put<void*>(seats+2*kSeatStride,0x300,oldRider);Put<void*>(seats+2*kSeatStride,0x308,oldCtrl);Put<int>(oldCtrl,8,1);Put<int>(oldCtrl,12,2);
    Check(nextUser(vehicle+kUserIface,cannonR)==oldRider+0x120,"real 62D950 returns the stale last rider of the empty right seat");
    Check(UserHook(vehicle+kUserIface,cannonR)==human+0x120,"the production user hook names the borrowing driver instead");
    Check(At<int>(oldCtrl,8)==1 && At<int>(oldCtrl,12)==2,"the native lookup balanced its weak references");
    // Real activation, pull and ready gate for a driver-borrowed cannon.
    const auto ready=reinterpret_cast<bool(__fastcall*)(void*)>(image+0x6912F0);
    Put<float>(seats,kSeatFire,1);ProteusEmptyWeapon(cannonR);
    Check(cannonR[0x13E]==1 && cannonR[kPull]==1 && onActive==1 && ready(cannonR),
          "real activation plus holder pull pass the real ready gate for the driver's cannon");
    Put<float>(seats,kSeatFire,0);cannonR[kPull]=0;
    Put<float>(seats,kSeatFire2,1);ProteusEmptyWeapon(launcherW);
    Check(launcherW[0x13E]==1 && launcherW[kPull]==1 && ready(launcherW),"the driver's second trigger readies and pulls the real launcher (deployed)");
    Put<float>(seats,kSeatFire2,0);launcherW[kPull]=0;
    u.st.mode=proteus::Mode::walk;ProteusEmptyWeapon(launcherW);
    Check(!launcherW[0x13E] && !ready(launcherW) && onEmpty>=1,"walking: the real empty callback idles the launcher");
    u.st.mode=proteus::Mode::deployed;
    human[kDead]=1;ProteusEmptyWeapon(cannonR);
    Check(!cannonR[0x13E] && !ready(cannonR) && !UserHook(vehicle+kUserIface,cannonR),"a dead driver: real stock deactivation, no operator");
    human[kDead]=0;
    // The installers on the real slots and code.
    std::int32_t rel=At<std::int32_t>(image,0x6302B3);
    Check(image+0x6302B7+rel==image+kNativeEmpty && At<const void*>(image,kWeaponPostSlot)==image+0x644910 &&
          image[0x644910]==0xE9 && image+0x644915+At<std::int32_t>(image,0x644911)==image+0x630250,
          "the real BigBegaruta slot 5 tail-jumps to 0x630250 whose empty callback LEA names 0x690230");
    Check(InstallProteusWeapons() && nextWeaponPost==image+0x644910 &&
          At<const void*>(image,kWeaponPostSlot)==reinterpret_cast<void*>(&ProteusWeaponPost),"the weapon phase chains the real slot 5");
    const auto patchedEmpty=reinterpret_cast<WeaponEnableFn>(image+0x6302B7+At<std::int32_t>(image,0x6302B3));
    config.enabled=false;cannonR[0x13E]=1;patchedEmpty(cannonR);
    Check(!cannonR[0x13E],"the real patched callback delegates the stock deactivation when the plugin is off");
    config.enabled=true;Put<float>(seats,kSeatFire,1);cannonR[kPull]=0;patchedEmpty(cannonR);
    Check(cannonR[0x13E] && cannonR[kPull],"the real patched callback activates and pulls a borrowed cannon");
    Put<float>(seats,kSeatFire,0);
    // A failed user hook leaves every mount to the stock callback.
    auto userSlot=reinterpret_cast<void**>(image+kUserSlotRva);
    DWORD protection=0;VirtualProtect(image+kUserFn,1,PAGE_EXECUTE_READWRITE,&protection);
    const auto saved=image[kUserFn];image[kUserFn]=0xCC;
    Check(!InstallProteusUser() && !userOk,"a user lookup not as expected disables borrowing");
    cannonR[0x13E]=1;Put<float>(seats,kSeatFire,1);cannonR[kPull]=0;patchedEmpty(cannonR);
    Check(!cannonR[0x13E] && !cannonR[kPull],"without the user hook the stock empty callback stays in charge");
    image[kUserFn]=saved;VirtualProtect(image+kUserFn,1,protection,&protection);
    Check(InstallProteusUser() && userOk && At<const void*>(image,kUserSlotRva)==reinterpret_cast<void*>(&UserHook) &&
          reinterpret_cast<const unsigned char*>(nextUser)==image+kUserFn,"the user hook chains the real BigBegaruta slot");
    (void)userSlot;
    Put<float>(seats,kSeatFire,0);
    // The shield: the real BarrierBullet01 code it rests on, and its real hit handler draining the HP the shield reads.
    Check(InstallProteusBarrier() && barrierOk && At<const void*>(image,kBarrierUpdateSlot)==reinterpret_cast<void*>(&BarrierHook) &&
          nextBarrierUpdate==image+kBarrierUpdate,"the real barrier's code matches; its update slot is chained");
    Check(At<float>(image,0x17A46C8)==0.0872f && static_cast<int>(kBarrierArcDeg*kPi/180.0f/At<float>(image,0x17A46C8))==kBarrierSegmentCount,
          "the claim's segment count is the real ctor's (arc / its 0x17A46C8 step)");
    u.st.shieldOn=true;u.st.shield=1;u.st.broken=false;u.st.mode=proteus::Mode::walk;Tick();
    alignas(16) static unsigned char barrier[0x1600];
    FreshBarrier(barrier,vehicle,kBarrierSegmentCount);BarrierStep(barrier);
    const float full=config.proteusBarrier*7500;
    Check(u.barrier.obj==barrier && At<float>(barrier,kBarrierHp)==full,"(the raised barrier is claimed)");
    alignas(16) unsigned char msg[0x80]{};Put<float>(msg,0x50,900.0f);
    const auto hit=reinterpret_cast<bool(__fastcall*)(void*,std::uint32_t,void*)>(image+0x2913B0);
    Check(hit(barrier,0x10000000,msg) && At<float>(barrier,kBarrierHp)==full-900,"the real hit handler takes a hit off the barrier's HP");
    BarrierStep(barrier);
    Check(std::fabs(u.st.shield-(full-900)/full)<1e-5f && u.st.quiet==0,"the shield reads the HP the game left");
    msg[0x60]=0x40;Put<float>(msg,0x50,5000.0f);hit(barrier,0x10000000,msg);
    Check(At<float>(barrier,kBarrierHp)==full-900,"a replayed (0x40) hit is not taken twice by the real handler");
    msg[0x60]=0;hit(barrier,0x10000000,msg);BarrierStep(barrier);
    Check(u.st.broken && At<float>(barrier,kBarrierHp)<=0 && !u.barrier.obj,"the real handler breaks it; the shield is broken");
}
LONG CALLBACK NativeFault(EXCEPTION_POINTERS* p){
    std::printf("native fault code=%lx rva=%llx access=%llx\n",p->ExceptionRecord->ExceptionCode,
        static_cast<unsigned long long>(p->ContextRecord->Rip-reinterpret_cast<std::uintptr_t>(crew::image)),
        static_cast<unsigned long long>(p->ExceptionRecord->ExceptionInformation[1]));return EXCEPTION_CONTINUE_SEARCH;
}
bool BindMath() {
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46)return false;
    auto imp=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;imp->Name;++imp) {
        const auto name=reinterpret_cast<const char*>(image+imp->Name);
        if(std::strcmp(name,"api-ms-win-crt-math-l1-1-0.dll"))continue;
        const auto lib=LoadLibraryA(name);if(!lib)return false;
        auto src=reinterpret_cast<const IMAGE_THUNK_DATA64*>(image+imp->OriginalFirstThunk);
        auto dst=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+imp->FirstThunk);
        for(;src->u1.AddressOfData;++src,++dst) {
            if(IMAGE_SNAP_BY_ORDINAL64(src->u1.Ordinal))return false;
            const auto entry=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(image+src->u1.AddressOfData);
            const auto proc=GetProcAddress(lib,reinterpret_cast<const char*>(entry->Name));if(!proc)return false;
            DWORD old=0;VirtualProtect(dst,sizeof(*dst),PAGE_READWRITE,&old);dst->u1.Function=reinterpret_cast<ULONGLONG>(proc);VirtualProtect(dst,sizeof(*dst),old,&old);
        }
    }
    return true;
}
}  // namespace
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,&NativeFault);
    if(argc<=1)return PreviousProteusFrameChecks();
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    if(!image){std::puts("EDF.dll unavailable");return 77;}
    if(!BindMath())return 77;
    Native();
    std::printf("proteus_weapon_test native: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
