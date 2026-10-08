#include "../src/mission_participant_gate.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;
bool session=true;
bool InSession() noexcept { return session; }
}
namespace {
int checks=0,calls=0,admissions=0;
bool admitValue=false;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
bool Admit(int index) noexcept {++admissions;Check(index==7,"mission index forwarded to frozen-cohort check");return admitValue;}
crew::ObjRef* __fastcall Original(void* context,crew::ObjRef* out,const float* matrix,int index,int a5,int a6,int a7,bool a8,int a9,void* a10) {
    ++calls;Check(context==reinterpret_cast<void*>(1) && matrix==reinterpret_cast<float*>(2) && index==7 &&
        a5==-5 && a6==6 && a7==-7 && a8 && a9==9 && a10==reinterpret_cast<void*>(10),"all ten Win64 ABI arguments preserved");
    *out=crew::ObjRef{context,a10};return out;
}
}
int main() {
    using namespace crew;using namespace mission_admission;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x230000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private code image");
    Check(!InstallMissionParticipantGate(&Admit) && !MissionParticipantGateReady(),"unknown profile fails closed");
    std::memcpy(image+kUpperCall,kCallerBytes,sizeof(kCallerBytes));std::memcpy(image+kUpperCreate,kUpperBytes,sizeof(kUpperBytes));
    Check(InstallMissionParticipantGate(&Admit) && MissionParticipantGateReady(),"safe upper call installed");
    Check(InstallMissionParticipantGate(&Admit),"idempotent install");
    original=&Original;
    const auto hook=reinterpret_cast<UpperFn>(image+kUpperCall+5+At<std::int32_t>(image+kUpperCall,1));
    ObjRef out{reinterpret_cast<void*>(99),reinterpret_cast<void*>(98)};
    auto invoke=[&](){return hook(reinterpret_cast<void*>(1),&out,reinterpret_cast<float*>(2),7,-5,6,-7,true,9,reinterpret_cast<void*>(10));};
    Check(invoke()==&out && !out.obj && !out.ctrl && calls==0,"denial returns valid empty weak output before any native spawn");
    admitValue=true;Check(invoke()==&out && calls==1 && out.obj==reinterpret_cast<void*>(1),"admission preserves native result");
    session=false;admitValue=false;const int before=admissions;invoke();
    Check(calls==2 && admissions==before,"offline creation unaffected");
    Check(std::memcmp(image+kUpperCall+5,kCallerBytes+5,sizeof(kCallerBytes)-5)==0,"consumer instructions remain unchanged");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("mission_participant_gate_test: %d checks passed\n",checks);
}
