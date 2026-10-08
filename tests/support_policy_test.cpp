#include "../src/support_policy.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;
}
int main() {
    using namespace crew;using namespace support;
    int checks=0;const auto check=[&](bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}};
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x20B4000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    check(image!=nullptr,"allocate inert image storage");
    constexpr unsigned char signature[]={0x8B,0x15,0xA2,0x28,0xA2,0x01,0x83,0xFA,0x02,0x74,0x1E,
        0x48,0x8B,0x05,0x96,0x1B,0xA2,0x01,0x83,0xB8,0x00,0x0B,0x00,0x00,0x00,0x74,0x0E,0x80,0xB9,0x28,0x02,0x00,
        0x00,0x00,0xB8,0x01,0x00,0x00,0x00,0x74,0x02,0x8B,0xC2,0xC3};
    std::memcpy(image+0x690DB0,signature,sizeof(signature));
    unsigned char world[0xB10]{};Put<void*>(image,0x20B2958,world);
    for(int condition=0;condition<3;++condition)for(int underground=0;underground<2;++underground) {
        Put<int>(image,0x20B3658,condition);Put<int>(world,0xB00,underground);
        const auto policy=SupportMissionPolicy();
        check(policy.underground==(underground!=0),"native underground state read separately from mission restrictions");
        check(Allowed(policy,Capability::air)==(condition!=1 && !underground),"air requires mission permission and open environment");
        check(Allowed(policy,Capability::militaryGround)==(condition!=1),"heavy external reinforcements honor NO_SUPPORT");
        check(Allowed(policy,Capability::infantry) && Allowed(policy,Capability::civilianGround),"ordinary infantry and civilian truck use their own physical entry capability");
    }
    Put<int>(image,0x20B3658,7);check(SupportMissionPolicy().environment==Environment::unknown,"unknown condition not treated as permission");
    Put<int>(image,0x20B3658,0);Put<int>(world,0xB00,7);
    check(SupportMissionPolicy().environment==Environment::unknown,"unknown scene flag fails closed");
    Put<int>(world,0xB00,0);image[0x690DB0]^=1;
    check(!Allowed(SupportMissionPolicy(),Capability::air),"profile mismatch cannot authorize airborne resource access");
    image[0x690DB0]^=1;Put<void*>(image,0x20B2958,nullptr);
    check(SupportMissionPolicy().environment==Environment::unknown,"missing mission object manager refuses military support");
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("support_policy_test: %d checks passed\n",checks);
}
