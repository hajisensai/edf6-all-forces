// EDF6VehicleCrew's EDF.dll fingerprints (src/profile_sigs.h) against the supported EDF.dll, mapped privately
// (LoadLibraryExW DONT_RESOLVE_DLL_REFERENCES: no DllMain, nothing of the game runs; skipped without the DLL).
// Default mode: each table's check (the loop of plugin.cpp CheckProfile, heli.cpp CheckHeliProfile, ground.cpp
// CheckGroundProfile: edf::Matches of every row) passes on the real code; then every byte of every row is changed in
// the mapping's private copy one at a time, and the table's check must refuse, at that row, and pass again once the
// byte is back. A byte next to a row (no row of the table covers it) changes nothing.
// Mode "plugin <EDF6VehicleCrew.dll> <scratch dir>": the built plugin itself, a copy loaded beside the mapped EDF.dll,
// refuses at each kSignatures row with that row's first byte changed: EML6_Load returns false, its log says
// "PROFILE mismatch at <rva>" then "REFUSED: unexpected EDF.dll code". CheckProfile runs before any patch, so a refused
// EML6_Load leaves the mapping as it was; EML6_Load is never called with every row matching (it would patch the
// mapping and start the plugin's threads).
#include "../src/profile_sigs.h"
#pragma warning(push)
#pragma warning(disable:4201)
#include "PluginAPI.h"
#pragma warning(pop)
#include "edf/host.h"
#include "edf/patch.h"
#include "plugin_load_util.h"
#include <cstdio>
#include <iterator>
#include <string>

namespace {
using plugintest::Check;
using crew::sigs::Signature;
unsigned char* image=nullptr;

struct Table { const char* name; const Signature* rows; std::size_t count; };
const Table kTables[]={
    {"kSignatures (CheckProfile)",crew::sigs::kSignatures,std::size(crew::sigs::kSignatures)},
    {"kHeliSignatures (CheckHeliProfile)",crew::sigs::kHeliSignatures,std::size(crew::sigs::kHeliSignatures)},
    {"kGroundSignatures (CheckGroundProfile)",crew::sigs::kGroundSignatures,std::size(crew::sigs::kGroundSignatures)},
};

// The plugin's check of a table: the first row that does not match (crew::Matches is edf::Matches on the image), or -1.
long FirstMismatch(const Table& t) {
    for(std::size_t i=0;i<t.count;++i)
        if(!edf::Matches(image,t.rows[i].rva,t.rows[i].bytes,t.rows[i].size))return static_cast<long>(i);
    return -1;
}
bool Covers(const Table& t,std::size_t rva) {
    for(std::size_t i=0;i<t.count;++i)if(rva>=t.rows[i].rva && rva<t.rows[i].rva+t.rows[i].size)return true;
    return false;
}

// One code byte of the private mapping xor'ed with `mask` (copy on write: the file on disk is never touched).
void Flip(std::size_t rva,unsigned char mask) {
    DWORD old=0;
    Check(VirtualProtect(image+rva,1,PAGE_EXECUTE_READWRITE,&old)!=0,"the code byte writable in the private mapping");
    image[rva]^=mask;
    DWORD ignored=0;
    Check(VirtualProtect(image+rva,1,old,&ignored)!=0,"its protection back");
}

int Tables() {
    int flips=0;
    for(const Table& t:kTables) {
        Check(t.count>0,"a table has rows");
        for(std::size_t i=0;i<t.count;++i)Check(t.rows[i].size>0 && t.rows[i].size<=sizeof(t.rows[i].bytes),"a row's size fits its bytes");
        const long before=FirstMismatch(t);
        if(before>=0)std::fprintf(stderr,"FAIL: %s row %ld (rva %#zx) does not match this EDF.dll\n",t.name,before,t.rows[before].rva);
        Check(before<0,"every row matches the real EDF.dll");
        for(std::size_t i=0;i<t.count;++i) {
            const Signature& s=t.rows[i];
            for(std::size_t k=0;k<s.size;++k) {
                Flip(s.rva+k,0xFF);
                const long at=FirstMismatch(t);
                if(at<0 || !(t.rows[at].rva<=s.rva+k && s.rva+k<t.rows[at].rva+t.rows[at].size))
                    std::fprintf(stderr,"FAIL: %s with byte %#zx (row %zu +%zu) changed: check says %ld\n",t.name,s.rva+k,i,k,at);
                Check(at>=0,"a changed byte is refused");
                Check(t.rows[at].rva<=s.rva+k && s.rva+k<t.rows[at].rva+t.rows[at].size,"...by a row that covers it");
                Check(static_cast<std::size_t>(at)<=i,"...no later than its own row");
                Flip(s.rva+k,0xFF);
                Check(FirstMismatch(t)<0,"the byte back: the table passes again");
                ++flips;
            }
            for(const std::size_t edge:{s.rva-1,s.rva+s.size}) {
                if(Covers(t,edge))continue;
                Flip(edge,0xFF);
                const bool still=FirstMismatch(t)<0;
                Flip(edge,0xFF);
                Check(still,"a byte next to a row (no row covers it) changes nothing");
            }
        }
        std::printf("%s: %zu rows match; each of their bytes changed is refused\n",t.name,t.count);
    }
    std::printf("profile_native_test: %d byte flips, %d checks passed\n",flips,plugintest::checks);
    return 0;
}

int Plugin(const char* dll,const char* scratch) {
    using LoadFn=bool(EDFMLAPI*)(PluginInfo*);
    const std::filesystem::path dir=std::filesystem::u8path(scratch);
    plugintest::FreshDir(dir);
    const std::filesystem::path copy=plugintest::CopyInto(std::filesystem::u8path(dll),dir);
    std::filesystem::path log=copy;
    log.replace_extension(L".log");
    const HMODULE plugin=plugintest::LoadPlugin(copy);
    const auto load=reinterpret_cast<LoadFn>(reinterpret_cast<void*>(GetProcAddress(plugin,"EML6_Load")));
    Check(load!=nullptr,"EML6_Load found");
    Check(GetModuleHandleW(L"EDF.dll")==reinterpret_cast<HMODULE>(image),"the plugin finds the mapped EDF.dll by name");
    const Table& t=kTables[0];
    Check(FirstMismatch(t)<0,"kSignatures matches before the plugin is asked");
    for(std::size_t i=0;i<t.count;++i) {
        const Signature& s=t.rows[i];
        Flip(s.rva,0x01);
        PluginInfo info{};
        const bool loaded=load(&info);
        char mismatch[64];
        std::snprintf(mismatch,sizeof(mismatch),"PROFILE mismatch at %#zx",s.rva);
        const std::string text=plugintest::WaitForLog(log,"REFUSED: unexpected EDF.dll code",static_cast<int>(i)+1);
        Flip(s.rva,0x01);
        Check(!loaded,"EML6_Load refuses with a kSignatures row changed");
        Check(plugintest::Count(text,"REFUSED: unexpected EDF.dll code")==static_cast<int>(i)+1,"the log says REFUSED: unexpected EDF.dll code");
        if(plugintest::Count(text,mismatch)!=1)std::fprintf(stderr,"FAIL: no \"%s\" in the log:\n%s\n",mismatch,text.c_str());
        Check(plugintest::Count(text,mismatch)==1,"the log names the changed row's rva");
        Check(plugintest::Count(text,"REFUSED: unsupported EDF.dll")==0,"the image itself was accepted");
        Check(text.rfind(mismatch)<text.rfind("REFUSED"),"the mismatch is said before the refusal");
        Check(FirstMismatch(t)<0,"the mapping as it was after the refusal");
    }
    // Nothing installed: the plugin logs no hook or patch line when it refuses.
    const std::string text=plugintest::ReadShared(log);
    Check(text.find("HELI profile=")==std::string::npos && text.find("HOOK ")==std::string::npos,"no install ran after a refusal");
    std::printf("profile_native_test plugin: %zu kSignatures rows each refused by the built EDF6VehicleCrew.dll, %d checks passed\n",
                t.count,plugintest::checks);
    std::fflush(stdout);
    return 0;
}
}  // namespace

int main(int argc,char** argv) {
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: pass the supported EDF.dll path (private mapping, no DllMain)");
        return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Check(image!=nullptr,"private DLL mapped without its entrypoint");
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    Check(nt->FileHeader.TimeDateStamp==edf::kImageTimeDateStamp && nt->OptionalHeader.SizeOfImage==edf::kImageSize,"supported game profile");
    const std::string mode=argc>2 ? argv[2] : "";
    if(mode=="plugin") {
        if(argc<5){std::fprintf(stderr,"usage: profile_native_test <EDF.dll> plugin <EDF6VehicleCrew.dll> <scratch dir>\n");return 2;}
        return Plugin(argv[3],argv[4]);
    }
    return Tables();
}
