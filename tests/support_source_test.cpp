// Production complete-ID source resolver with the native enumeration ABI on an inert private image.
#include "../src/support_source.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace fixture {
std::array<std::array<unsigned char,0x500>,3> objects{};
std::array<std::array<unsigned char,16>,3> controls{};
std::array<std::array<unsigned char,32>,3> ids{};
unsigned count=0;bool swapManager=false,changeAfterWalk=false;
unsigned char manager[0x80]{},replacement[0x80]{},rows[7*0x38]{};
int checks=0;
void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL source: %s\n",why);std::exit(1);}}
void __fastcall Walk(void*,void* raw) {
    auto* visitor=static_cast<crew::Visitor*>(raw);
    for(unsigned i=0;i<count;++i)crew::Visit(visitor,objects[i].data());
    if(swapManager)crew::Put<void*>(crew::image,0x20B2978,replacement);
    if(changeAfterWalk)ids[0][31]^=1;
}
void Reset() {
    count=1;swapManager=changeAfterWalk=false;crew::Put<void*>(crew::image,0x20B2978,manager);
    crew::Put<void*>(manager,0x38,rows);crew::Put<std::uint64_t>(manager,0x48,7);
    for(unsigned i=0;i<3;++i) {
        objects[i].fill(0);controls[i].fill(0);ids[i].fill(0);
        crew::Put<void*>(objects[i].data(),crew::kSelf,objects[i].data());
        crew::Put<void*>(objects[i].data(),crew::kSelfCtrl,controls[i].data());
        crew::Put<LONG>(controls[i].data(),8,1);crew::Put<LONG>(controls[i].data(),12,7);
        ids[i][0]=0x44;ids[i][4]=static_cast<unsigned char>(i+1);ids[i][12]=5;
    }
}
}
namespace crew {
unsigned char* image=nullptr;
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out) noexcept {
    for(unsigned i=0;i<fixture::count;++i)if(object==fixture::objects[i].data()) {
        std::memcpy(out,fixture::ids[i].data(),32);return true;
    }
    return false;
}
}
int main() {
    using namespace crew;using namespace fixture;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private image allocated");
    // Exact verified native prologue, then the fixture's ABI-compatible enumeration and matching epilogue.
    std::memcpy(image+0x5E0C80,kEnumSignature,sizeof(kEnumSignature));
    unsigned char tail[]={0xEC,0x20,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x83,0xC4,0x20,0x41,0x5E,0x5F,0x5E,0xC3};
    const auto target=reinterpret_cast<std::uintptr_t>(&Walk);std::memcpy(tail+4,&target,8);
    std::memcpy(image+0x5E0C80+sizeof(kEnumSignature),tail,sizeof(tail));
    FlushInstructionCache(GetCurrentProcess(),image+0x5E0C80,sizeof(kEnumSignature)+sizeof(tail));
    const unsigned char bound[]={0x48,0x81,0xFE,0x88,0x01,0x00,0x00};std::memcpy(image+0x5E0D28,bound,sizeof(bound)); // the walk's 7 rows
    Reset();ObjRef ref{objects[0].data(),controls[0].data()},out{};
    const auto key=SupportSourceKey(ref);Check(key!=0,"live canonical registered source has a stable descriptor");
    Check(ResolveSupportSource(key,&out) && out.obj==ref.obj && out.ctrl==ref.ctrl,"full native registry resolves exact weak identity");
    count=3;Check(SupportSourceKey(ref)==key,"unrelated registered objects do not change identity");
    ids[2]=ids[0];Check(!ResolveSupportSource(key,&out) && !out,"ambiguous complete native identity is refused");
    Reset();ids[0][20]=1;Check(!SupportSourceKey(ref),"noncanonical native padding is never hashed");
    Reset();ids[0].fill(0);Check(!SupportSourceKey(ref),"zero native identity is absent");
    Reset();Put<LONG>(controls[0].data(),8,0);Check(!SupportSourceKey(ref),"expired control cannot authorize a current source");
    Reset();Put<void*>(objects[0].data(),kSelfCtrl,controls[1].data());Check(!SupportSourceKey(ref),"address-reused source cannot inherit old weak identity");
    Reset();objects[0][kDead]=1;Check(!SupportSourceKey(ref),"dead source is refused");
    Reset();objects[0][0x18]=4;Check(!SupportSourceKey(ref),"deleted source is refused");
    Reset();Put<void*>(objects[0].data(),kSelf,nullptr);Check(!SupportSourceKey(ref),"forged alias is not a native self identity");
    Reset();Put<std::uint64_t>(manager,0x48,16);Check(!ResolveSupportSource(key,&out) && !out,"a walk bound that is not the manager's row count is refused");
    Reset();swapManager=true;Check(!ResolveSupportSource(key,&out) && !out,"registry replacement during native walk invalidates the entire result");
    Reset();changeAfterWalk=true;Check(!ResolveSupportSource(key,&out) && !out,"native identity is rechecked after enumeration");
    Reset();const auto before=controls[0];Check(SupportSourceKey(ref)==key && controls[0]==before,"read-only lookup changes no native reference counts");
    Check(!ResolveSupportSource(0,&out) && !ResolveSupportSource(key,nullptr),"zero key and null output are refused");
    Check(VirtualFree(image,0,MEM_RELEASE)!=0,"private image released");image=nullptr;
    std::printf("support source: %d checks passed\n",checks);return 0;
}
