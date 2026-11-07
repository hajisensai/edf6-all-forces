// Production Titan/Blacker network handoff receives the embedded VehicleWeaponAim, never its vtable pointer.
// The fake vtable is read-only just as EDF.dll's .rdata is, so the previous pointer-indirection bug cannot pass.
#include "../autoturret/src/gunner.cpp"
#include <cstdio>
namespace autoturret {
unsigned char* image=nullptr;Config cfg{};
namespace {void* called=nullptr;unsigned char weapon[32]{};}
int logs=0;
void Log(const char*,...) noexcept{++logs;}
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
bool CameraTurret(const unsigned char*,unsigned) noexcept{return false;}
bool CrewAims(const unsigned char*,unsigned) noexcept{return false;}
edf::aimlink::PlayerGun PlayerControlRule(const unsigned char*,unsigned,bool locked) noexcept{return edf::aimlink::PlayerGunRule(false,locked);}
bool Stabilized(const unsigned char*,unsigned,const float*,float*,float*) noexcept{return false;}
float PriorityWeight(const Enemy&) noexcept{return 1;}
bool ActualWeapon(const unsigned char*,unsigned,const unsigned char*,const unsigned char** out) noexcept{if(out)*out=nullptr;return false;}
void PublishAim(const unsigned char*,bool,const void*,const float*,const void*) noexcept{}
bool Same(const void* obj,const void* ctrl) noexcept {   // plugin.cpp's
    if(!ctrl || !Readable(obj,kSelfCtrl+8) || At<const void*>(obj,kSelfCtrl)!=ctrl)return false;
    return Readable(ctrl,edf::kCtrlUses+4) && At<std::int32_t>(ctrl,edf::kCtrlUses)>0;
}
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
    // 2026-10-25 ("坦克的炮管会360度动"): the side gun's barrel is measured in the hull's frame. The main turret slewing at
    // 0.02 rad a frame against the gun's own 0.01 gave the old rule a ratio of -1 and flipped the sign in four frames;
    // the gun then drove away from its target round its whole circle. Frames the main turret turns teach nothing.
    float sign=1.0f;
    for(int i=0;i<120;++i)sign=LearnSign(sign,0.01f,0.01f-0.02f,false);
    bool learn=sign==1.0f;
    float old1=1.0f;
    for(int i=0;i<4;++i)old1=LearnSign(old1,0.01f,0.01f-0.02f,true);   // the same frames counted (the old rule): flipped
    learn=learn && old1<0.0f;
    float back=-1.0f;
    for(int i=0;i<8;++i)back=LearnSign(back,0.01f,0.01f,true);         // the gun turning alone still teaches its sign
    learn=learn && back>0.0f && LearnSign(1.0f,0.0005f,-0.0005f,true)==1.0f;
    if(!learn)std::printf("FAIL side gun sign learned from the main turret's turn\n");
    pass=pass && learn;
    // LogOnce: more vehicle seats asking each frame than its table holds no longer log every one of them every frame
    // (the old 16-entry ring missed every call then). A gone vehicle's entry is reused, a vehicle built at a gone one's
    // address is told about again, and the mission's reset forgets them all.
    constexpr unsigned kCars=300;
    static unsigned char cars[kCars][kSelfCtrl+8]{},ctrls[kCars+1][16]{};
    for(unsigned i=0;i<kCars;++i){Put<void*>(cars[i],kSelfCtrl,ctrls[i]);Put<std::int32_t>(ctrls[i],edf::kCtrlUses,1);}
    const int before=logs;
    for(unsigned i=0;i<kCars;++i)LogOnce(cars[i],1,"first");
    bool once=logs-before==257;                                            // 256 seats and one "table full"
    for(int frame=0;frame<3;++frame)for(unsigned i=0;i<kCars;++i)LogOnce(cars[i],1,"again");
    once=once && logs-before==257;                                         // the frames after: nothing
    Put<std::int32_t>(ctrls[0],edf::kCtrlUses,0);                         // car 0 gone: its entry is reused
    LogOnce(cars[256],1,"freed");LogOnce(cars[256],1,"freed");
    once=once && logs-before==258;
    Put<void*>(cars[1],kSelfCtrl,ctrls[kCars]);Put<std::int32_t>(ctrls[kCars],edf::kCtrlUses,1);   // a new car at car 1's address
    LogOnce(cars[1],1,"new");LogOnce(cars[1],1,"new");
    once=once && logs-before==259;
    ResetGunnerLog();
    LogOnce(cars[2],1,"next mission");LogOnce(cars[2],2,"other seat");LogOnce(cars[2],1,"next mission");
    once=once && logs-before==261;
    if(!once)std::printf("FAIL LogOnce logged %d lines\n",logs-before);
    pass=pass && once;
    std::printf("gunner_aim_object_test: embedded handoff, vtable preservation, full-circle rear-target reach, sign learning and LogOnce %s\n",pass?"passed":"FAILED");
    VirtualFree(vtable,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);return pass?0:1;
}
