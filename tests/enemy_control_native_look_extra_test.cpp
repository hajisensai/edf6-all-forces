// Execute the production family adapters with native target-state fixtures.
#include "../src/enemy_control_native_look_extra.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::enemy_native;
int checks=0,failed=0;
void Check(bool ok,const char* what,int line){++checks;if(!ok){++failed;std::printf("%d: %s\n",line,what);}}
#define CHECK(x) Check((x),#x,__LINE__)
bool Near(double x,double y,double e=0.00003){return std::fabs(x-y)<e;}
constexpr NativeProfile profiles[]={
#include "../src/enemy_control_native_profiles.inc"
};
struct Fixture {
    unsigned char* object=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* control=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Fixture(){if(!object || !control)std::exit(2);}
    ~Fixture(){VirtualFree(object,0,MEM_RELEASE);VirtualFree(control,0,MEM_RELEASE);}
    NativeLease Reset(const NativeProfile& p){
        std::memset(object,0,0x4000);std::memset(control,0,0x1000);
        Put<const void*>(object,0x30,control);Put<LONG>(control,8,1);
        object[0x1000]=2;object[0xF30]=2;object[0x10E0]=2;object[0x1168]=0;object[0x8B0]=2;object[0x1260]=2;
        Put<float>(object,0x14CC,0.04f);Put<float>(object,0x10C8,0.02f);
        for(auto at:{0xE30,0xE10,0xFC0})Put<float>(object,static_cast<std::size_t>(at)+12,1);
        NativeLease lease{};lease.object=object;lease.control=control;lease.profile=&p;return lease;
    }
};
void BuildImage(){
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)std::exit(2);
#define COPY(name) for(const auto& p:k##name##Look)std::memcpy(image+p.rva,p.bytes,sizeof(p.bytes))
    COPY(Deiroi);COPY(Insect);COPY(Nephila);COPY(LargeDango);COPY(FlyingDyno);COPY(ShellFish);
    COPY(Spinner);COPY(Squid);COPY(TimeShip);COPY(Monster);COPY(Carrier);COPY(Ufo507);COPY(Ufo515);
    COPY(AlienTrailer);COPY(DropBoat);COPY(Merman);
#undef COPY
}
Input Sentinel(){Input i{};i.right=.23f;i.forward=-.31f;i.vertical=.45f;i.held=0x5555;i.pressed=0xAAAA;i.seed=0x12345678;i.yaw=.71f;i.pitch=-.61f;return i;}
void UnchangedOther(const Input& i){CHECK(i.right==.23f && i.forward==-.31f && i.vertical==.45f);CHECK(i.held==0x5555 && i.pressed==0xAAAA && i.seed==0x12345678);}
struct EulerCase{std::uint32_t rva;std::size_t at;float step;bool pitch;};
constexpr EulerCase cases[]={
    {0x31ED90,0xF44,.01f,false},{0x368310,0xFB0,.04f,true},
    {0x3B3ED0,0x10B0,.2f,true},{0x3C0850,0x1000,.1f,true},
    {0x3D60F0,0xFC0,.1f,true},{0x3E2140,0x12C0,.1f,true},
    {0x4ACD70,0x14F4,.01f,false},{0x4BC8B0,0x14F4,.01f,false},
    {0x4F9540,0x604,.01f,false},{0x52EF70,0x6D4,.1f,false},{0x5371C0,0x10B4,.02f,false},
};
const NativeProfile& Find(std::uint32_t rva){for(const auto& p:profiles)if(p.inputRva==rva)return p;std::exit(2);}
void EulerCases(Fixture& f){
    for(const auto& c:cases){
        const auto lease=f.Reset(Find(c.rva));Input i=Sentinel();
        Put<float>(f.object,c.at,0);if(c.pitch)Put<float>(f.object,c.at+4,0);
        CHECK(AimToPadExtra(lease,c.step*.25f,c.step*.5f,&i));
        CHECK(Near(i.yaw,-.25));CHECK(Near(i.pitch,c.pitch ? -.5 : 0));UnchangedOther(i);
        const auto yawAt=c.at+(c.pitch ? 4u : 0u);
        for(unsigned frame=0;frame<160;++frame){
            CHECK(AimToPadExtra(lease,.7f,.3f,&i));
            Put<float>(f.object,yawAt,At<float>(f.object,yawAt)-i.yaw*c.step);
            if(c.pitch)Put<float>(f.object,c.at,At<float>(f.object,c.at)+i.pitch*c.step);
        }
        CHECK(Near(At<float>(f.object,yawAt),.7));if(c.pitch)CHECK(Near(At<float>(f.object,c.at),-.3));
        CHECK(Near(i.yaw,0));CHECK(Near(i.pitch,0));
        Put<float>(f.object,yawAt,static_cast<float>(kExtraPi)-.002f);
        CHECK(AimToPadExtra(lease,static_cast<float>(-kExtraPi)+.002f,.3f,&i));CHECK(i.yaw<0);
        Put<float>(f.object,yawAt,static_cast<float>(-kExtraPi)+.002f);
        CHECK(AimToPadExtra(lease,static_cast<float>(kExtraPi)-.002f,.3f,&i));CHECK(i.yaw>0);
    }
}
struct Q{double x,y,z,w;};
Q Mul(Q a,Q b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
Q Rx(double a){return {std::sin(a/2),0,0,std::cos(a/2)};}
Q Ry(double a){return {0,std::sin(a/2),0,std::cos(a/2)};}
void StoreQ(unsigned char* o,std::size_t at,Q q){const double k=1/std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);Put<float>(o,at,static_cast<float>(q.x*k));Put<float>(o,at+4,static_cast<float>(q.y*k));Put<float>(o,at+8,static_cast<float>(q.z*k));Put<float>(o,at+12,static_cast<float>(q.w*k));}
void QuaternionCases(Fixture& f){
    for(const auto rva:{0x4A8FD0u,0x42D560u,0x4E0010u,0x3EE300u}){
        const auto lease=f.Reset(Find(rva));const bool time=rva==0x3EE300,insect=rva==0x4A8FD0 || rva==0x42D560;
        const std::size_t at=time ? 0xFC0 : insect ? 0xE30 : 0xE10;
        Q target=Ry(-.3);StoreQ(f.object,at,target);Input i=Sentinel();double velocity=.02;
        if(insect)Put<float>(f.object,0x1070,static_cast<float>(velocity));
        for(unsigned frame=0;frame<280;++frame){
            CHECK(AimToPadExtra(lease,.8f,time ? .4f : 0,&i));
            if(time)target=Mul(target,Mul(Ry(-i.yaw*.1),Rx(i.pitch*.1)));
            else {velocity=insect ? velocity*.9-i.yaw*.005 : -i.yaw*.05;target=Mul(target,Ry(velocity));if(insect)Put<float>(f.object,0x1070,static_cast<float>(velocity));CHECK(i.pitch==0);}
            StoreQ(f.object,at,target);
        }
        double x=0,y=0,z=0;CHECK(LocalExtra(lease,at,.8f,time ? .4f : 0,&x,&y,&z));CHECK(Near(x,0));CHECK(Near(y,0));CHECK(Near(z,1));CHECK(Near(i.yaw,0));CHECK(Near(i.pitch,0));UnchangedOther(i);
        // Rendering may lag arbitrarily: only the target quaternion is sampled.
        StoreQ(f.object,at+16,Ry(-1.5));CHECK(AimToPadExtra(lease,.8f,time ? .4f : 0,&i));CHECK(Near(i.yaw,0));
    }
    const auto lease=f.Reset(Find(0x4A8FD0));Input i=Sentinel();
    Put<float>(f.object,0x1070,.02f);CHECK(AimToPadExtra(lease,0,0,&i));CHECK(Near(i.yaw,1)); // brake native inertia
    Put<float>(f.object,0x1070,.001f);CHECK(AimToPadExtra(lease,0,0,&i));CHECK(Near(i.yaw,.18));
    StoreQ(f.object,0xE30,Rx(kExtraPi/2));Put<float>(f.object,0x1070,0);
    CHECK(AimToPadExtra(lease,static_cast<float>(kExtraPi/2),0,&i));CHECK(i.yaw==-1);CHECK(i.pitch==0);
}
void GatesAndDiscrete(Fixture& f){
    struct Gate{std::uint32_t rva;std::size_t at;};
    for(const auto& g:{Gate{0x4A8FD0,0x1000},Gate{0x42D560,0x1000},Gate{0x4E0010,0xF30},Gate{0x3B3ED0,0x10E0},Gate{0x3EE300,0x1168},Gate{0x3C0850,0x1570},Gate{0x3E2140,0x1260},Gate{0x4F9540,0xDF0},Gate{0x4ACD70,0x8B0},Gate{0x4BC8B0,0x8B0}}){
        const auto lease=f.Reset(Find(g.rva));f.object[g.at]=g.rva==0x3EE300 || g.rva==0x3C0850 ? 2u : g.rva==0x4F9540 ? 1u : 0u;Input i=Sentinel();CHECK(AimToPadExtra(lease,.7f,.3f,&i));CHECK(i.yaw==0 && i.pitch==0);UnchangedOther(i);
    }
    for(const auto rva:{0x368310u,0x5371C0u}){
        const auto lease=f.Reset(Find(rva));Put<float>(f.object,rva==0x368310 ? 0x14CC : 0x10C8,0);
        Input i=Sentinel();CHECK(AimToPadExtra(lease,.7f,.3f,&i));CHECK(i.yaw==0 && i.pitch==0);UnchangedOther(i);
    }
    auto lease=f.Reset(Find(0x30E300));Input i=Sentinel();
    CHECK(AimToPadExtra(lease,.31f,.4f,&i));CHECK(i.yaw==-1 && i.pitch==0);
    CHECK(AimToPadExtra(lease,.29f,.4f,&i));CHECK(i.yaw==0);
    Put<float>(f.object,0xCC0,1);
    CHECK(AimToPadExtra(lease,.11f,.4f,&i));CHECK(i.yaw==-1);
    CHECK(AimToPadExtra(lease,.09f,.4f,&i));CHECK(i.yaw==0);
    CHECK(AimToPadExtra(lease,-.11f,.4f,&i));CHECK(i.yaw==1);
    lease=f.Reset(Find(0x4F3810));CHECK(AimToPadExtra(lease,.7f,.4f,&i));CHECK(i.yaw==0 && i.pitch==0);
    // Flight-mode step cap is applied after input scaling in the native body.
    lease=f.Reset(Find(0x3B3ED0));f.object[0x10E0]=0x82;
    Put<const unsigned char*>(f.object,0x1410,f.control);Put<float>(f.control,0x98,.01f);
    CHECK(AimToPadExtra(lease,.005f,0,&i));CHECK(Near(-i.yaw*.2,.005));
}
void MermanRaw(Fixture& f) {
    auto* const tree=f.object+0x2200;auto* const blob=f.object+0x2300;
    Put<const unsigned char*>(f.object,0x1740,tree);Put<std::uint32_t>(f.object,0x1748,0);
    Put<const unsigned char*>(tree,0x50,blob);
    Put<std::uint32_t>(blob,0,0x4F475344);Put<std::uint32_t>(blob,8,5);Put<std::int32_t>(blob,12,16);
    Put<std::int32_t>(blob,0x10,0x60);Put<std::uint32_t>(blob,0x18,3);
    const double values[]={.6,.4,0,1};
    for(unsigned n=0;n<4;++n){Put<double>(blob,0x20+n*16,values[n]);Put<std::uint32_t>(blob,0x28+n*16,0);Put<std::uint32_t>(blob,0x80+n*4,n+1);}
    Put<std::int32_t>(blob,0x78,16);Put<std::uint32_t>(blob,0x7C,4);
}
void MermanRuntime(Fixture& f) {
    auto* const tree=f.object+0x2200;auto* const mapping=f.object+0x2600;
    auto* const nodes=f.object+0x2700;auto* const container=f.object+0x2A00;auto* const children=f.object+0x2B00;
    Put<const unsigned char*>(tree,8,mapping);Put<std::uint64_t>(tree,0x18,5);Put<const unsigned char*>(tree,0x28,nodes);
    const double values[]={.6,.4,0,1};
    for(unsigned n=0;n<5;++n){Put<std::uint32_t>(mapping,n*4,n);Put<std::uint16_t>(nodes,n*0x28+0x20,n ? 0 : 3);if(n)Put<double>(nodes,n*0x28,values[n-1]);}
    Put<const unsigned char*>(nodes,0,container);Put<const unsigned char*>(container,0x48,children);Put<std::uint64_t>(container,0x58,4);
    for(unsigned n=0;n<4;++n)Put<std::uint32_t>(children,n*4,n+1);
}
void MermanCases(Fixture& f) {
    const float rates[]={.6f,.4f,0,1};
    for(unsigned mode=0;mode<3;++mode){
        const auto lease=f.Reset(Find(0x3A4AF0));MermanRaw(f);if(mode)MermanRuntime(f);
        if(mode==2)for(unsigned n=1;n<5;++n)Put<std::uint32_t>(f.object,0x2600+n*4,UINT32_MAX); // runtime parent, serialized numeric children
        for(unsigned state=0;state<4;++state){
            Put<std::uint32_t>(f.object,0x17F0,state);Put<float>(f.object,0x1764,0);Input i=Sentinel();
            const float step=rates[state]*.02f;
            CHECK(AimToPadExtra(lease,step*.5f,.4f,&i));CHECK(Near(i.yaw,state==2 ? 0 : -.5));CHECK(i.pitch==0);UnchangedOther(i);
            for(unsigned frame=0;frame<150;++frame){
                CHECK(AimToPadExtra(lease,.7f,.3f,&i));Put<float>(f.object,0x1764,At<float>(f.object,0x1764)-i.yaw*step);
            }
            CHECK(Near(At<float>(f.object,0x1764),state==2 ? 0 : .7));CHECK(Near(i.yaw,0));
        }
        Put<std::uint32_t>(f.object,0x17F0,4);Input i=Sentinel();CHECK(AimToPadExtra(lease,.7f,.3f,&i));CHECK(i.yaw==0 && i.pitch==0);
        Put<std::uint32_t>(f.object,0x17F0,0);f.object[0x17F4]=4;CHECK(AimToPadExtra(lease,.7f,.3f,&i));CHECK(i.yaw==0 && i.pitch==0);
    }
    const auto lease=f.Reset(Find(0x3A4AF0));MermanRaw(f);Input i=Sentinel(),before=i;
    Put<std::uint32_t>(f.object,0x2328,4); // expression numeric leaf must never execute its VM
    CHECK(!AimToPadExtra(lease,0,0,&i));CHECK(std::memcmp(&i,&before,sizeof(i))==0);
    Put<std::uint32_t>(f.object,0x2328,0);Put<double>(f.object,0x2320,-1);CHECK(!AimToPadExtra(lease,0,0,&i));
    Put<double>(f.object,0x2320,std::numeric_limits<double>::infinity());CHECK(!AimToPadExtra(lease,0,0,&i));
    Put<double>(f.object,0x2320,.6);Put<std::uint32_t>(f.object,0x2380,5);CHECK(!AimToPadExtra(lease,0,0,&i));
    Put<std::uint32_t>(f.object,0x2380,1);Put<std::uint32_t>(f.object,0x237C,0);CHECK(!AimToPadExtra(lease,0,0,&i));
}
void AllExtraProfiles(Fixture& f){
    unsigned count=0;
    for(const auto& p:profiles){
        if(p.kind==Kind::walking || p.kind==Kind::humanoid)continue;
        const auto lease=f.Reset(p);if(p.inputRva==0x3A4AF0)MermanRaw(f);
        Input i=Sentinel();CHECK(AimToPadExtra(lease,0,0,&i));UnchangedOther(i);++count;
    }
    CHECK(count==24);
}
void Failures(Fixture& f){
    auto lease=f.Reset(Find(0x31ED90));Input i=Sentinel(),before=i;
    CHECK(!AimToPadExtra(lease,0,0,nullptr));
    CHECK(!AimToPadExtra(lease,std::numeric_limits<float>::infinity(),0,&i));CHECK(std::memcmp(&i,&before,sizeof(i))==0);
    CHECK(!AimToPadExtra(lease,0,std::numeric_limits<float>::quiet_NaN(),&i));CHECK(std::memcmp(&i,&before,sizeof(i))==0);
    CHECK(!AimToPadExtra(lease,0,2,&i));CHECK(std::memcmp(&i,&before,sizeof(i))==0);
    Put<LONG>(f.control,8,0);CHECK(!AimToPadExtra(lease,0,0,&i));CHECK(std::memcmp(&i,&before,sizeof(i))==0);
    Put<LONG>(f.control,8,1);f.object[0x2E8]=1;CHECK(!AimToPadExtra(lease,0,0,&i));CHECK(std::memcmp(&i,&before,sizeof(i))==0);
    f.object[0x2E8]=0;image[kDeiroiLook[0].rva]^=1;CHECK(!AimToPadExtra(lease,0,0,&i));CHECK(std::memcmp(&i,&before,sizeof(i))==0);image[kDeiroiLook[0].rva]^=1;
    lease=f.Reset(Find(0x5371C0));Put<float>(f.object,0x10C8,-1);CHECK(!AimToPadExtra(lease,0,0,&i));CHECK(std::memcmp(&i,&before,sizeof(i))==0);
    lease=f.Reset(Find(0x3EE300));Put<float>(f.object,0xFCC,0);CHECK(!AimToPadExtra(lease,0,0,&i));CHECK(std::memcmp(&i,&before,sizeof(i))==0);
}
}
int main(){BuildImage();Fixture fixture;EulerCases(fixture);QuaternionCases(fixture);GatesAndDiscrete(fixture);MermanCases(fixture);AllExtraProfiles(fixture);Failures(fixture);VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;std::printf("extra native look %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;}
