// Exercises the production pose gate against synthetic PE/RTTI/vtables and weak actor identities. It never loads or
// executes EDF.dll. Compile this source alone (it includes the production .cpp), include dirs src/common/
// third_party/EDFModLoader/build/generated, link edf6common; C++17 /MT /W4 /WX /utf-8 /permissive-.
#include "../src/enemy_control_native_net.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace crew { unsigned char* image=nullptr; }
namespace {
using namespace crew;
using namespace crew::enemy_native;
int checks=0,failures=0;
void Check(bool ok,const char* what,int line){++checks;if(!ok){++failures;std::fprintf(stderr,"line %d: %s\n",line,what);}}
#define CHECK(value) Check((value),#value,__LINE__)
constexpr std::size_t kRows=sizeof(kPoseRows)/sizeof(kPoseRows[0]);
bool Expected(const char* name){
    const char* const names[]={"AlienTrailer","EDF6_Berserker_Large","EDF6_Berserker_Middle","EDF6_Berserker_TypeA",
        "EDF6_Berserker_TypeBommer","EDF6_Berserker_TypeGrapple","EDF6_Berserker_TypeShooter","EDF6_Merman",
        "EDF6_FlyingDyno","EDF6_ShellFish","EDF6_SpinnerUfo","Humanoid_BigGreyBoss","EDF6_Martian","EDF6_Venus",
        "Humanoid_Basic","Monster501","Monster504","DropBoat512","UfoCarrier508","UfoSmall507",
        "DragonSmall","GiantAnt","GiantAntEX","GiantBee","GiantBeeEX","GiantDango","GiantSpider","GiantSpiderEX","Nephila"};
    for(const auto* candidate:names)if(!std::strcmp(name,candidate))return true;return false;
}
struct Fixture {
    static constexpr std::size_t kObjectSize=0x3000;
    unsigned char* object=static_cast<unsigned char*>(VirtualAlloc(nullptr,kObjectSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* control=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* otherControl=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Fixture(){if(!object || !control || !otherControl)std::exit(2);}
    ~Fixture(){VirtualFree(object,0,MEM_RELEASE);VirtualFree(control,0,MEM_RELEASE);VirtualFree(otherControl,0,MEM_RELEASE);}
    NativeLease Lease(std::size_t index=0){
        std::memset(object,0,kObjectSize);std::memset(control,0,0x1000);std::memset(otherControl,0,0x1000);
        Put<const void*>(object,0,image+kPoseRows[index].primary);Put<const void*>(object,0x120,image+kPoseRows[index].secondary);
        Put<const void*>(object,0x30,control);Put<LONG>(control,8,1);Put<LONG>(otherControl,8,1);
        NativeLease lease{};lease.object=object;lease.control=control;lease.profile=&kPoseInputProfiles[index];return lease;
    }
};
void BuildCol(unsigned char* col,const PoseRow& row,std::uint32_t rva,std::uint32_t offset){
    Put<std::uint32_t>(col,0,1);Put<std::uint32_t>(col,4,offset);Put<std::uint32_t>(col,8,0);
    Put<std::uint32_t>(col,12,row.typeDescriptor);Put<std::uint32_t>(col,16,row.hierarchy);Put<std::uint32_t>(col,20,rva);
}
void BuildImage(){
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,kPoseImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)std::exit(2);
    Put<std::uint16_t>(image,0,0x5A4D);Put<std::uint32_t>(image,0x3C,0x100);
    Put<std::uint32_t>(image,0x100,0x4550);Put<std::uint16_t>(image,0x104,0x8664);
    Put<std::uint32_t>(image,0x108,kPoseTimestamp);Put<std::uint16_t>(image,0x118,0x20B);Put<std::uint32_t>(image,0x150,kPoseImageSize);
    for(const auto& f:kPoseFingerprints)std::memcpy(image+f.rva,f.bytes,sizeof(f.bytes));
    for(const auto& f:kPoseSharedFingerprints)std::memcpy(image+f.rva,f.bytes,sizeof(f.bytes));
    Put<const void*>(image+kPoseSharedTable,0,image+kPoseSharedWriter);Put<const void*>(image+kPoseSharedTable,8,image+kPoseSharedReader);
    for(std::size_t i=0;i<kRows;++i){const auto& row=kPoseRows[i];const auto primaryCol=static_cast<std::uint32_t>(0x10000+i*0x100);
        Put<const void*>(image+row.primary-8,0,image+primaryCol);BuildCol(image+primaryCol,row,primaryCol,0);
        Put<const void*>(image+row.secondary-8,0,image+row.col);BuildCol(image+row.col,row,row.col,0x120);
        std::snprintf(reinterpret_cast<char*>(image+row.typeDescriptor+16),128,".?AV%s@@",row.name);
        for(std::size_t slot=0;slot<4;++slot)Put<const void*>(image+row.secondary,(slot+5)*8,image+row.slot[slot]);
    }
}
void CatalogCases(Fixture& fixture){
    CHECK(kRows==34);unsigned accepted=0;
    for(std::size_t i=0;i<kRows;++i){const auto& row=kPoseRows[i];const auto lease=fixture.Lease(i);const bool expected=Expected(row.name);
        CHECK(row.verified==expected);CHECK(EnemyControlNativeNetworkPoseReady(lease)==expected);accepted+=expected;
        std::array<unsigned char,Fixture::kObjectSize> before{};std::memcpy(before.data(),fixture.object,before.size());
        EnemyControlNativeNetworkPoseReady(lease);CHECK(!std::memcmp(before.data(),fixture.object,before.size()));
        for(std::size_t slot=0;slot<4;++slot){
            const auto offset=(slot+5)*8;const void* original=At<const void*>(image+row.secondary,offset);
            Put<const void*>(image+row.secondary,offset,image+row.slot[slot]+1);CHECK(!EnemyControlNativeNetworkPoseReady(lease));
            Put<const void*>(image+row.secondary,offset,original);
            for(unsigned byte=0;byte<16;++byte){image[row.slot[slot]+byte]^=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));image[row.slot[slot]+byte]^=1;}
            CHECK(EnemyControlNativeNetworkPoseReady(lease)==expected);
        }
        for(unsigned field=0;field<6;++field){const auto previous=At<std::uint32_t>(image+row.col,field*4);
            Put<std::uint32_t>(image+row.col,field*4,previous^1u);CHECK(!EnemyControlNativeNetworkPoseReady(lease));Put<std::uint32_t>(image+row.col,field*4,previous);}
        Put<const void*>(fixture.object,0x120,image+row.secondary+8);CHECK(!EnemyControlNativeNetworkPoseReady(lease));
        Put<const void*>(fixture.object,0x120,image+row.secondary);
        Put<const void*>(image+row.secondary-8,0,image+row.col+4);CHECK(!EnemyControlNativeNetworkPoseReady(lease));Put<const void*>(image+row.secondary-8,0,image+row.col);
        const auto td=row.typeDescriptor+16;image[td+4]^=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));image[td+4]^=1;
        const auto tail=td+4+std::strlen(row.name);image[tail+2]=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));image[tail+2]=0;
        CHECK(EnemyControlNativeNetworkPoseReady(lease)==expected);
    }
    CHECK(accepted==29);
    // Non-empty but unproven or partial serializers must stay denied; empty forwarded writers cannot be upgraded by
    // borrowing a known serializer pair. These are admission decisions, not guesses based on writer length.
    for(std::size_t i=0;i<kRows;++i)if(!Expected(kPoseRows[i].name)){
        auto lease=fixture.Lease(i);const auto& row=kPoseRows[i];std::array<const void*,4> originals{};
        for(std::size_t slot=0;slot<4;++slot){originals[slot]=At<const void*>(image+row.secondary,(slot+5)*8);Put<const void*>(image+row.secondary,(slot+5)*8,image+kPoseRows[0].slot[slot]);}
        CHECK(!EnemyControlNativeNetworkPoseReady(lease));for(std::size_t slot=0;slot<4;++slot)Put<const void*>(image+row.secondary,(slot+5)*8,originals[slot]);
    }
}
void SharedCases(Fixture& fixture){
    const auto lease=fixture.Lease(2);CHECK(!std::strcmp(lease.profile->name,"DragonSmall"));CHECK(EnemyControlNativeNetworkPoseReady(lease));
    for(unsigned slot=0;slot<2;++slot){const auto saved=At<const void*>(image+kPoseSharedTable,slot*8);
        Put<const void*>(image+kPoseSharedTable,slot*8,image+1);CHECK(!EnemyControlNativeNetworkPoseReady(lease));Put<const void*>(image+kPoseSharedTable,slot*8,saved);}
    for(const auto& signature:kPoseSharedFingerprints)for(unsigned byte=0;byte<16;++byte){image[signature.rva+byte]^=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));image[signature.rva+byte]^=1;}
    CHECK(EnemyControlNativeNetworkPoseReady(lease));
}
void IdentityCases(Fixture& fixture){
    auto lease=fixture.Lease();CHECK(EnemyControlNativeNetworkPoseReady(lease));const auto baseline=lease;
    lease.object=nullptr;CHECK(!EnemyControlNativeNetworkPoseReady(lease));lease=baseline;
    lease.profile=nullptr;CHECK(!EnemyControlNativeNetworkPoseReady(lease));lease=baseline;
    lease.control=nullptr;CHECK(!EnemyControlNativeNetworkPoseReady(lease));lease=baseline;
    Put<LONG>(fixture.control,8,0);CHECK(!EnemyControlNativeNetworkPoseReady(lease));Put<LONG>(fixture.control,8,-1);CHECK(!EnemyControlNativeNetworkPoseReady(lease));Put<LONG>(fixture.control,8,1);
    Put<const void*>(fixture.object,0x30,fixture.otherControl);CHECK(!EnemyControlNativeNetworkPoseReady(lease));Put<const void*>(fixture.object,0x30,fixture.control);
    fixture.object[0x18]=4;CHECK(!EnemyControlNativeNetworkPoseReady(lease));fixture.object[0x18]=0;
    fixture.object[0x2E8]=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));fixture.object[0x2E8]=0;
    Put<const void*>(fixture.object,0,image+kPoseRows[0].primary+8);CHECK(!EnemyControlNativeNetworkPoseReady(lease));Put<const void*>(fixture.object,0,image+kPoseRows[0].primary);
    NativeProfile profile=*lease.profile;lease.profile=&profile;profile.vtableRva=0x1777777;CHECK(!EnemyControlNativeNetworkPoseReady(lease));profile=*baseline.profile;
    profile.profileHash^=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));profile=*baseline.profile;
    profile.inputRva^=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));profile=*baseline.profile;
    profile.aiRva^=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));profile=*baseline.profile;
    ++profile.slots;CHECK(!EnemyControlNativeNetworkPoseReady(lease));profile=*baseline.profile;
    profile.buttonMask^=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));profile=*baseline.profile;
    profile.axes^=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));profile=*baseline.profile;
    profile.inputSlot^=1;CHECK(!EnemyControlNativeNetworkPoseReady(lease));profile=*baseline.profile;
    profile.name=nullptr;CHECK(!EnemyControlNativeNetworkPoseReady(lease));profile=*baseline.profile;
    profile.kind=Kind::deiroi;CHECK(!EnemyControlNativeNetworkPoseReady(lease));lease=baseline;
    auto* primaryCol=const_cast<unsigned char*>(At<const unsigned char*>(image+kPoseRows[0].primary-8,0));
    for(unsigned field=0;field<6;++field){const auto value=At<std::uint32_t>(primaryCol,field*4);Put<std::uint32_t>(primaryCol,field*4,value^1u);
        CHECK(!EnemyControlNativeNetworkPoseReady(lease));Put<std::uint32_t>(primaryCol,field*4,value);}
    Put<const void*>(image+kPoseRows[0].primary-8,0,nullptr);CHECK(!EnemyControlNativeNetworkPoseReady(lease));
    Put<const void*>(image+kPoseRows[0].primary-8,0,fixture.control);CHECK(!EnemyControlNativeNetworkPoseReady(lease));
    Put<const void*>(image+kPoseRows[0].primary-8,0,image+kPoseImageSize+0x1000);CHECK(!EnemyControlNativeNetworkPoseReady(lease));
    Put<const void*>(image+kPoseRows[0].primary-8,0,primaryCol);CHECK(EnemyControlNativeNetworkPoseReady(lease));
}
void ImageCases(Fixture& fixture){
    auto lease=fixture.Lease();const std::size_t fields[]={0,0x100,0x104,0x108,0x118,0x150};
    for(const auto field:fields){const auto value=At<std::uint32_t>(image,field);Put<std::uint32_t>(image,field,value^1u);CHECK(!EnemyControlNativeNetworkPoseReady(lease));Put<std::uint32_t>(image,field,value);}
    Put<std::uint32_t>(image,0x3C,0x1001);CHECK(!EnemyControlNativeNetworkPoseReady(lease));Put<std::uint32_t>(image,0x3C,0x100);
    auto* saved=image;image=nullptr;CHECK(!EnemyControlNativeNetworkPoseReady(lease));image=reinterpret_cast<unsigned char*>(1);CHECK(!EnemyControlNativeNetworkPoseReady(lease));image=saved;
    DWORD old=0;CHECK(VirtualProtect(image,kPoseImageSize,PAGE_READONLY,&old)!=0);Sleep(2);
    CHECK(EnemyControlNativeNetworkPoseReady(lease));CHECK(VirtualProtect(image,kPoseImageSize,old,&old)!=0);Sleep(2);
}
void ReadBoundaries(Fixture& fixture){
    auto lease=fixture.Lease();DWORD previous=0;
    CHECK(VirtualProtect(fixture.control,0x1000,PAGE_NOACCESS,&previous)!=0);Sleep(2);CHECK(!EnemyControlNativeNetworkPoseReady(lease));
    CHECK(VirtualProtect(fixture.control,0x1000,previous,&previous)!=0);Sleep(2);
    auto* primaryCol=const_cast<unsigned char*>(At<const unsigned char*>(image+kPoseRows[0].primary-8,0));
    CHECK(VirtualProtect(primaryCol,0x1000,PAGE_NOACCESS,&previous)!=0);Sleep(2);CHECK(!EnemyControlNativeNetworkPoseReady(lease));
    CHECK(VirtualProtect(primaryCol,0x1000,previous,&previous)!=0);Sleep(2);
    const auto codePage=kPoseRows[0].slot[2]&~0xFFFu;
    CHECK(VirtualProtect(image+codePage,0x1000,PAGE_NOACCESS,&previous)!=0);Sleep(2);CHECK(!EnemyControlNativeNetworkPoseReady(lease));
    CHECK(VirtualProtect(image+codePage,0x1000,previous,&previous)!=0);Sleep(2);
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(pages!=nullptr);if(!pages)std::exit(2);
    CHECK(VirtualProtect(pages+0x1000,0x1000,PAGE_NOACCESS,&previous)!=0);Sleep(2);
    lease.object=pages+0x1000-0x128;CHECK(!EnemyControlNativeNetworkPoseReady(lease));lease=fixture.Lease();
    lease.control=pages+0x1000-10;Put<const void*>(fixture.object,0x30,lease.control);CHECK(!EnemyControlNativeNetworkPoseReady(lease));
    lease=fixture.Lease();lease.profile=reinterpret_cast<const NativeProfile*>(pages+0x1000-sizeof(NativeProfile)+1);CHECK(!EnemyControlNativeNetworkPoseReady(lease));
    CHECK(VirtualFree(pages,0,MEM_RELEASE)!=0);
    lease=fixture.Lease();CHECK(VirtualProtect(fixture.object,Fixture::kObjectSize,PAGE_READONLY,&previous)!=0);Sleep(2);
    CHECK(EnemyControlNativeNetworkPoseReady(lease));CHECK(VirtualProtect(fixture.object,Fixture::kObjectSize,previous,&previous)!=0);Sleep(2);
    CHECK(EnemyControlNativeNetworkPoseReady(lease));
}
}
int main(){BuildImage();{Fixture fixture;CatalogCases(fixture);SharedCases(fixture);IdentityCases(fixture);ImageCases(fixture);ReadBoundaries(fixture);}VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("enemy native net pose: %d checks, %d failures; 34 decisions, 29 verified; no game execution\n",checks,failures);return failures ? 1 : 0;}
