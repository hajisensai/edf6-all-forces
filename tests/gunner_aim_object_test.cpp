// Production Titan/Blacker network handoff receives the embedded VehicleWeaponAim, never its vtable pointer.
// The fake vtable is read-only just as EDF.dll's .rdata is, so the previous pointer-indirection bug cannot pass.
#include "../autoturret/src/gunner.cpp"
#include <cstdio>
namespace autoturret {
unsigned char* image=nullptr;Config cfg{};
namespace {void* called=nullptr;unsigned char weapon[32]{};}
void Log(const char*,...) noexcept{}
void SeeVehicle(const void*) noexcept{}
ULONGLONG Frame() noexcept{return 1;}
void ScanEnemies(const unsigned char*,float,Nearby& out) noexcept{out.count=0;}
Track* TrackFor(const unsigned char*,unsigned,bool) noexcept{return nullptr;}
float Down(const unsigned char*) noexcept{return 9.8f;}
bool Ballistic(const float*,const Shot&,float&,float&) noexcept{return false;}
float AxisInput(Track&,int,float,float,float,bool,float,float) noexcept{return 0;}
void ReloadConfigIfChanged() noexcept{}
void PilotFrame(const unsigned char*,unsigned,const unsigned char*,const float*,const float*,float) noexcept{}
const void* Designated(const unsigned char*,float*) noexcept{return nullptr;}
bool LeadCircle() noexcept{return false;}
bool CameraTurret(const unsigned char*,unsigned) noexcept{return false;}
bool Stabilized(const unsigned char*,unsigned,const float*,float*,float*) noexcept{return false;}
float PriorityWeight(const Enemy&) noexcept{return 1;}
void PublishAim(const unsigned char*,bool,const void*,const float*,const float*,const float*,const Shot*,const float*,float) noexcept{}
const unsigned char* SeatGun(const unsigned char*) noexcept{return weapon;}
void __fastcall LocalRec(void* aim){called=aim;static_cast<unsigned char*>(aim)[kAimNetwork]=0;}
}
int main(){
    using namespace autoturret;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    auto* vtable=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image || !vtable)return 2;
    vtable[kAimNetwork]=1;DWORD old=0;VirtualProtect(vtable,4096,PAGE_READONLY,&old);
    unsigned char seat[edf::kSeatStride]{},vehicle[32]{};
    Put<void*>(seat,kSeatAim,vtable);seat[kSeatAim+kAimNetwork]=1;
    auto* code=image+kAimLocal;code[0]=0x48;code[1]=0xB8;
    const auto target=reinterpret_cast<std::uintptr_t>(&LocalRec);std::memcpy(code+2,&target,8);code[10]=0xFF;code[11]=0xE0;
    FlushInstructionCache(GetCurrentProcess(),code,12);
    TakeFromNetwork(vehicle,1,seat);
    bool pass=called==seat+kSeatAim && !seat[kSeatAim+kAimNetwork] && vtable[kAimNetwork]==1 && GunnerSeat(seat);
    // A rear target across +/-pi is reachable; its correction and feed-forward use the short turn.
    Aim aim{{3.13f,0},{-kPi,-0.5f},{kPi,0.5f},{3.13f,0},{1,-1}};
    const float want[2]={-3.13f,0};float error[2],axis[2];
    pass=pass && AxisTargets(aim,want,error,axis) && std::fabs(Wrap(axis[0]-want[0]))<1e-5f && error[0]>0 && error[0]<0.03f;
    aim.min[0]=-1;aim.max[0]=1;aim.angle[0]=aim.barrel[0]=0;
    pass=pass && !AxisTargets(aim,want,error,axis);
    std::printf("gunner_aim_object_test: embedded handoff, vtable preservation and full-circle rear-target reach %s\n",pass?"passed":"FAILED");
    VirtualFree(vtable,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);return pass?0:1;
}
