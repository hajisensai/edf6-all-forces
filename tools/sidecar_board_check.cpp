// Run the production SidecarBoard against stand-in humans and bikes, without EDF.dll. The warp entry records
// where the human went. In particular, FindSeat is visited for ALL friendly objects, including those far away,
// and returning nullptr after a sidecar take does not stop that visit. No game is started.
#include "../src/sidecar.cpp"
#include <cstdio>
#include <initializer_list>

namespace crew {
unsigned char* image=nullptr;
PlayerFix player{};
namespace {
Config config{};
unsigned char human[0x1600]{},bike[0x3000]{},second[0x3000]{},ordinary[0x3000]{},seat[kSeatStride]{};
unsigned char riderCtrl[0x10]{};
const void* boardingOnly=nullptr;
bool doorReadable=true;
float doorReach=2.3f;
int warps=0,failures=0;
void __fastcall WarpRec(void* ctrl,const float* matrix) {
    auto h=static_cast<unsigned char*>(ctrl)-kHumanCtrl;
    std::memcpy(h+kPosition,matrix+12,12);
    ++warps;
}
std::uintptr_t __fastcall MoveRec(void*) { return 0; }
void Jump(unsigned rva,const void* to) {
    unsigned char* p=image+rva;
    p[0]=0x48;p[1]=0xB8;std::memcpy(p+2,&to,8);p[10]=0xFF;p[11]=0xE0;
}
void Expect(bool pass,const char* what) {
    std::printf("%s: %s\n",pass ? "PASS" : "FAIL",what);
    if(!pass)++failures;
}
void HumanAt(float x,float y,float z) {
    const float p[3]={x,y,z};std::memcpy(human+kPosition,p,12);
}
void Reset() {
    ResetSidecars();warps=0;doorReadable=true;doorReach=2.3f;boardingOnly=nullptr;
    std::memset(human,0,sizeof(human));std::memset(bike,0,sizeof(bike));std::memset(second,0,sizeof(second));
    std::memset(seat,0,sizeof(seat));
    Put<void*>(human,kHumanPad,human);Put<unsigned char>(human,kHumanPlayer,1);
    for(auto v:{bike,second}) {
        Put<unsigned char*>(v,kSeats,seat);Put<std::uint64_t>(v,kSeatCount,1);
        Put<float>(v,kMatrix,1.0f);Put<float>(v,kMatrix+20,1.0f);Put<float>(v,kMatrix+40,1.0f);
    }
    sidecars[0].ref=ObjRef::Of(bike);sidecars[0].marked=true;sidecars[0].seen=GameMs();
    sidecars[1].ref=ObjRef::Of(second);sidecars[1].marked=true;sidecars[1].seen=GameMs();
}
}  // namespace
const Config& Cfg() noexcept { return config; }
const void* BoardingOnly() noexcept { return boardingOnly; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return 3600000; }
ULONGLONG GameFrame() noexcept { return 1; }
bool CameraRay(float*,float*) noexcept { return false; }
bool SidecarLevelHooked() noexcept { return false; }
unsigned char* BoneRecord506(const unsigned char*,const wchar_t*) noexcept { return nullptr; }
bool SeatPoint(const unsigned char* v,unsigned,float* at,float* reach) noexcept {
    if(!doorReadable)return false;
    FramePoint(v,1.0f,0.0f,kGunnerZ,at);*reach=doorReach;return true;
}
}  // namespace crew

int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    Jump(kWarp,reinterpret_cast<const void*>(&WarpRec));Jump(kMoveIntent,reinterpret_cast<const void*>(&MoveRec));
    ok=true;
    Reset();HumanAt(-1000.0f,0.0f,kGunnerZ);
    Expect(!SidecarBoard(bike,human) && warps==0,"no boarding across the map even when the sidecar is nearer");
    Reset();HumanAt(kGunnerX,20.0f,kGunnerZ);
    Expect(!SidecarBoard(bike,human) && warps==0,"no boarding from another floor directly above the platform");
    Reset();HumanAt(kGunnerX-2.31f,0.0f,kGunnerZ);
    Expect(!SidecarBoard(bike,human) && warps==0,"just outside the bike's stock reach");
    Reset();HumanAt(kGunnerX-2.29f,0.0f,kGunnerZ);
    Expect(SidecarBoard(bike,human) && warps==1,"just inside the bike's stock reach boards");
    Reset();HumanAt(kGunnerX,0.0f,kGunnerZ);doorReadable=false;
    Expect(!SidecarBoard(bike,human) && warps==0,"an unreadable door does not permit an unbounded take");
    Reset();HumanAt(1.0f,0.0f,kGunnerZ);
    Expect(!SidecarBoard(bike,human) && warps==0,"the nearer free saddle keeps the original boarding path");
    Reset();HumanAt(1.0f,0.0f,kGunnerZ);  // boarding gun's temporary position at the native saddle door
    Put<std::int32_t>(riderCtrl,edf::kCtrlUses,1);
    Put<unsigned char*>(seat,kSeatRiderCtrl,riderCtrl);Put<unsigned char*>(seat,kSeatRider,ordinary);
    boardingOnly=bike;
    Expect(SeatRider(seat)==Rider::other && !SidecarBoard(bike,human) && warps==0 && !sidecars[0].gunner && !boardHeld,
           "boarding gun with an occupied saddle cannot take the reachable virtual sidecar");
    boardingOnly=nullptr;
    Expect(SidecarBoard(bike,human) && warps==1,"ordinary boarding still takes the sidecar beside an occupied saddle");
    Reset();HumanAt(kGunnerX,0.0f,kGunnerZ);
    Expect(SidecarBoard(bike,human) && warps==1,"nearby sidecar takes the player");
    Expect(SidecarBoard(second,human) && warps==1 && sidecars[0].gunner.Is(human) && !sidecars[1].gunner,
           "the remaining team walk cannot transfer the player to another sidecar");
    Expect(SidecarBoard(ordinary,human) && warps==1,"the remaining team walk cannot invoke an ordinary vehicle's FindSeat");
    MoveIntent(human);  // released: the press guard clears, but occupying the sidecar still blocks boarding
    Expect(!boardHeld && SidecarBoard(ordinary,human),"sidecar occupancy still prevents a second boarding after release");
    human[kHumanBoard]=1;MoveIntent(human);
    Expect(!sidecars[0].gunner && boardHeld.Is(human) && SidecarBoard(second,human),"the step-off press cannot board another vehicle");
    human[kHumanBoard]=0;MoveIntent(human);
    Expect(!SidecarBoard(ordinary,human),"release after step-off restores ordinary boarding");
    VirtualFree(image,0,MEM_RELEASE);
    return failures ? 1 : 0;
}
