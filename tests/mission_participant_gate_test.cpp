#include "../src/mission_participant_gate.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;
bool session=true;
bool InSession() noexcept { return session; }
// edf5online.cpp: the EDF5 loop's online pad/split (on while bvmOnline) and the created player reported back.
bool bvmOnline=false;
int madeCalls=0,madeIndex=-2;
const void* madeObj=reinterpret_cast<const void*>(1);
bool Edf5BvmOnlinePlayerArgs(int,int* pad,int* split) noexcept { if(!bvmOnline)return false;*pad=-1;*split=1;return true; }
void Edf5BvmOnlinePlayerMade(int index,const void* made) noexcept { ++madeCalls;madeIndex=index;madeObj=made; }
}
namespace {
int checks=0,calls=0,admissions=0;
bool admitValue=false;
bool emptyResult=false;
unsigned char actor[0x100]{},actorCtrl[16]{};
int creations=0;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
bool Admit(int index) noexcept {++admissions;Check(index==7,"mission index forwarded to frozen-cohort check");return admitValue;}
void Created(int index,const crew::ObjRef& ref) noexcept {
    ++creations;Check(index==7 && ref.obj==actor && ref.ctrl==actorCtrl,"successful native actor identity reported after construction");
}
crew::ObjRef* __fastcall Original(void* context,crew::ObjRef* out,const float* matrix,int index,int a5,int a6,int a7,bool a8,int a9,void* a10) {
    ++calls;Check(context==reinterpret_cast<void*>(1) && matrix==reinterpret_cast<float*>(2) && index==7 &&
        (crew::bvmOnline ? a5==-1 && a6==1 : a5==-5 && a6==6) && a7==-7 && a8 && a9==9 && a10==reinterpret_cast<void*>(10),"all ten Win64 ABI arguments preserved");
    *out=emptyResult ? crew::ObjRef{} : crew::ObjRef{actor,actorCtrl};return out;
}
}
int main() {
    using namespace crew;using namespace mission_admission;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x230000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private code image");
    Check(!InstallMissionParticipantGate(&Admit) && !MissionParticipantGateReady(),"unknown profile fails closed");
    std::memcpy(image+kUpperCall,kCallerBytes,sizeof(kCallerBytes));std::memcpy(image+kUpperCreate,kUpperBytes,sizeof(kUpperBytes));
    Check(InstallMissionParticipantGate(&Admit,&Created) && MissionParticipantGateReady(),"safe upper call installed");
    Check(InstallMissionParticipantGate(&Admit,&Created),"idempotent install");
    original=&Original;
    Put<void*>(actor,kSelfCtrl,actorCtrl);Put<LONG>(actorCtrl,8,1);
    const auto hook=reinterpret_cast<UpperFn>(image+kUpperCall+5+At<std::int32_t>(image+kUpperCall,1));
    ObjRef out{reinterpret_cast<void*>(99),reinterpret_cast<void*>(98)};
    auto invoke=[&](){return hook(reinterpret_cast<void*>(1),&out,reinterpret_cast<float*>(2),7,-5,6,-7,true,9,reinterpret_cast<void*>(10));};
    Check(invoke()==&out && !out.obj && !out.ctrl && calls==0 && creations==0,"denial returns valid empty weak output without creation observation");
    admitValue=true;Check(invoke()==&out && calls==1 && out.obj==actor && creations==1,"admission preserves native result and observes actual actor");
    session=false;admitValue=false;const int before=admissions;invoke();
    Check(calls==2 && admissions==before && creations==1,"offline creation unaffected and not part of online cohort");
    session=true;admitValue=true;emptyResult=true;invoke();
    Check(creations==1,"allowed but failed creation does not produce a world identity");
    emptyResult=false;Put<LONG>(actorCtrl,8,0);invoke();
    Check(creations==1,"expired creation weak result is not observed");
    Check(madeCalls==0,"outside an online EDF5 loop nothing is reported to it");
    // An online EDF5 creation (edf5online.cpp): the pad and split it gives reach the native call, the player made is
    // reported back; a refused one is reported as none.
    bvmOnline=true;Put<LONG>(actorCtrl,8,1);admitValue=true;const int made=calls;invoke();
    Check(calls==made+1 && madeCalls==1 && madeIndex==7 && madeObj==actor,"online EDF5 pad/split passed, the player reported");
    admitValue=false;invoke();
    Check(calls==made+1 && madeCalls==2 && madeObj==nullptr,"a refused online EDF5 player reported as none, not created");
    bvmOnline=false;
    Check(std::memcmp(image+kUpperCall+5,kCallerBytes+5,sizeof(kCallerBytes)-5)==0,"consumer instructions remain unchanged");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("mission_participant_gate_test: %d checks passed\n",checks);
}
