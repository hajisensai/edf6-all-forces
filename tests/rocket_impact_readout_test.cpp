// Production ReadRound + RoundLands, fed the same CP layout the constructor reads. Uses real map-hit traversal
// on a recording stand-in terrain ray; no game process. Guided stores must keep lock marks instead of fake impacts.
#include "../src/rounds.cpp"
#include <cstdio>
namespace crew {
unsigned char* image=nullptr;
namespace {
Config config{};bool terrain=true,marked=true;int type=1,rays=0,checks=0,failed=0;
void Check(bool ok,const char* what){++checks;if(!ok){++failed;std::printf("FAIL %s\n",what);}}
void Jump(unsigned rva,const void* to){auto* p=image+rva;p[0]=0x48;p[1]=0xB8;std::memcpy(p+2,&to,8);p[10]=0xFF;p[11]=0xE0;}
void __fastcall Count(const Variant* v,int* n){const int first=At<int>(v,0);*n=first<0?11:(first==3||first==7)?3:0;}
void __fastcall ChildRec(const Variant* v,Pick* pick){*pick->out=Variant{};pick->out->tag=kTagNode;
    const int first=At<int>(v,0);Put<int>(pick->out,0,first<0?pick->index:first);Put<int>(pick->out,4,first<0?-1:pick->index);}
void __fastcall NumberRec(const Variant* v,double* n){const int first=At<int>(v,0),second=At<int>(v,4);
    if(first==0)*n=type;else if(first==3 && second==0)*n=66;else if(first==4)*n=650.0/3600.0;
    else if(first==6)*n=740.0/60.0;else if(first==7)*n=second==2?1:0;else if(first==8)*n=1000000;else if(first==9)*n=marked?4242:0;else *n=0;}
}
const Config& Cfg() noexcept{return config;}
void Log(const char*,...) noexcept{}
float MapRay(const float* a,const float* b,float* hit) noexcept{
    ++rays;if(!terrain || a[1]<0 || b[1]>0 || a[1]==b[1])return -1;
    const float t=a[1]/(a[1]-b[1]);for(int i=0;i<3;++i)hit[i]=a[i]+t*(b[i]-a[i]);
    return vec::Dist(a,hit);
}
}
namespace edf {bool WorldGravity(const unsigned char*,float* g) noexcept{g[0]=g[2]=0;g[1]=-14.7f;return true;}}
int main(){
    using namespace crew;
    config.enabled=true;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    Jump(kChild,reinterpret_cast<const void*>(&ChildRec));Jump(kCount,reinterpret_cast<const void*>(&Count));Jump(kNumber,reinterpret_cast<const void*>(&NumberRec));
    unsigned char weapon[0x1000]{},factory[16]{};
    Put<const void*>(factory,0,image+0x17A1BD0);Put<void*>(weapon,kAmmoFactory,factory);
    Put<float>(weapon,edf::kWeaponAmmoSpeed,0.6f);Put<float>(weapon,edf::kWeaponAmmoGravity,0);Put<int>(weapon,edf::kWeaponAmmoAlive,1200);
    Put<float>(weapon,edf::kWeaponAmmoOwnerMove,1);Put<float>(weapon,edf::kWeaponOwnerVel,180);
    auto* v=reinterpret_cast<Variant*>(weapon+kWeaponCustom);v->tag=kTagNode;Put<int>(v,0,-1);
    customOk=true;for(bool& c:classOk)c=true;
    RoundModel m{};
    Check(ReadRound(weapon,&m) && m.kind==RoundKind::rocket && m.pluginMotor && m.burn==66,"Hydra marked missile with no lock is an unguided plugin rocket");
    const float start[3]={0,60,0};float dir[3]={0,-0.15f,1};vec::Normalize(dir);
    float at[3],sec=0;rays=0;
    Check(RoundLands(weapon,m,start,dir,3000,at,&sec) && std::fabs(at[1])<1e-4f && rays>1 && sec>0,"rocket impact follows actual motor segments to first map contact");
    Check(at[0]>100,"rocket prediction preserves launcher sideways velocity");
    const float savedX=at[0];
    config.enabled=false;
    Check(RoundLands(weapon,m,start,dir,3000,at,&sec) && at[0]<savedX-50,"disabled plugin uses the stock motor and inherited-velocity decay");
    config.enabled=true;Put<float>(weapon,edf::kWeaponAmmoOwnerMove,0);
    Check(RoundLands(weapon,m,start,dir,3000,at,&sec) && std::fabs(at[0])<1e-4f && savedX>at[0]+100,"AmmoOwnerMove is read from the real weapon");
    terrain=false;
    Check(!RoundLands(weapon,m,start,dir,3000,at,&sec),"sky or unreachable terrain yields no fabricated impact");
    Put<int>(weapon,kWeaponLockon,kHoming);
    Check(ReadRound(weapon,&m) && m.kind==RoundKind::homing,"guided plugin stores stay guided");rays=0;
    Check(!RoundLands(weapon,m,start,dir,3000,at,&sec) && rays==0,"guided store has no unguided impact traversal");
    Put<int>(weapon,kWeaponLockon,0);type=2;
    Check(ReadRound(weapon,&m) && m.kind==RoundKind::homing,"unsupported nose-thrust type 2 is not approximated as a Hydra");
    type=1;marked=false;
    Check(ReadRound(weapon,&m) && m.kind==RoundKind::homing,"a non-plugin type 1 retains the guidance exclusion");
    type=0;
    Check(ReadRound(weapon,&m) && m.kind==RoundKind::rocket && !m.pluginMotor,"stock type 0 retains its existing motor model");
    std::printf("rocket_impact_readout_test: %d checks, %d failed\n",checks,failed);
    VirtualFree(image,0,MEM_RELEASE);return failed?1:0;
}
