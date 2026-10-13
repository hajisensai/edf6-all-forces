// Production Ride / Gather / Rebatch on synthetic native layouts: boarding must return a
// surviving on-foot weapon's batch, without writing a weapon removed in this frame.
#include "../src/firstperson.cpp"
#include <cstdio>

namespace edf {
bool ChainVtableSlot(void**,void*,void**) noexcept { return false; }
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept { return false; }
bool IsPlayer(const unsigned char*) noexcept { return false; }
bool RedirectCall(unsigned char*,void*,void*,bool&) noexcept { return false; }
}
namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
bool IsSazabi(const void*) noexcept { return false; }
bool HighCamOn(const void*) noexcept { return false; }
bool TurretCamHighTransition(const void*) noexcept { return false; }
float SightZoomNow(const void*) noexcept { return 1.0f; }
bool SightZoomMounted(const void*) noexcept { return false; }
bool HumanOnFoot(const unsigned char*) noexcept { return false; }
bool MapHoldsKeys() noexcept { return false; }
bool haveEye=true;
bool VehicleEye(unsigned char*,float* eye,RideView* out) noexcept {
    if(!haveEye)return false;
    eye[0]=0;eye[1]=2;eye[2]=0;*out=RideView{};return true;
}
int VehicleScreens(const unsigned char*,int,const float*,CockpitScreens* out) noexcept { out->n=0;return 0; }
}

int main() {
    using namespace crew;
    unsigned char human[0x2100]{},cam[0x400]{},weapon[0x1400]{};
    void* vt[2]{};
    unsigned char* list[]={weapon};
    auto model=weapon+kWeaponModel;
    int batch=1,replacement=2;
    int failed=0;
    const auto check=[&](bool ok,const char* what){std::printf("%s %s\n",ok ? "ok" : "FAIL",what);if(!ok)++failed;};
    config.debug=false;
    Put<void*>(model,0,vt);
    Put<void*>(human,kWeapons,list);
    Put<std::uint64_t>(human,kWeaponCount,1);
    const auto reset=[&](){
        st=State{};st.on=true;st.human=human;st.cam=cam;
        st.weapons[0]=model;st.weaponCount=1;st.inHand[0]=model;st.inHandCount=1;
        for(auto& u:unbatched)u=Unbatched{};
        unbatched[0]=Unbatched{model,&batch};Put<void*>(model,kBatch,nullptr);
        Put<std::uint64_t>(human,kWeaponCount,1);haveEye=true;
    };
    reset();
    check(Ride(cam,human),"a fitted vehicle accepts the ride");
    check(At<void*>(model,kBatch)==&batch,"boarding returns the surviving weapon's original batch");
    check(!unbatched[0].model && st.weaponCount==0 && st.inHandCount==0,"boarding retires the batch record and viewmodel lists");
    reset();Put<std::uint64_t>(human,kWeaponCount,0);
    check(Ride(cam,human),"boarding after the weapon was removed");
    check(!At<void*>(model,kBatch) && !unbatched[0].model,"a removed weapon is never written and its stale record is dropped");
    reset();Put<void*>(model,kBatch,&replacement);
    check(Ride(cam,human) && At<void*>(model,kBatch)==&replacement,"boarding preserves a replacement batch owned by the game");
    reset();haveEye=false;
    check(!Ride(cam,human) && !At<void*>(model,kBatch) && unbatched[0].model==model,"no cockpit leaves the on-foot state for Leave to restore");
    return failed ? 1 : 0;
}
