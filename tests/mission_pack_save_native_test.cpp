// The mission packs' save marker (src/mission_packs.h: seen[508..511] of a mode's save, GS+0x57A4+0x169C) through the
// game's own code on a private EDF.dll mapping (no DllMain, imports unresolved: only memcpy / memset / memmove /
// memcmp / wcsncpy_s are pointed at this process's CRT). Never starts the game. What it runs, all EDF.dll 0x678CCB46:
//   0xDBA00   the mode-save reset (0xDF790's template memcpy'd over GS+0x57A4, 0x16C4 bytes; DB580 bit 4 is the same
//             code inline, which SetMode 0xDC460 runs through DB580(0x1C))
//   0xDF790   the template itself (clear and seen memset 0)
//   0xDFA90 / 0xDFAB0 / 0xDFA20   the game's seen / clear writers and the seen reader (row-indexed, no bound)
//   0x7C1270  the save file's checksum (header +0xC): the write job 0x7C1CB0 computes it over the live block
//             (rcx = job+0x90 = GS+0x57A4, rdx = job+0x98 = 0x16C4), the read job 0x7C1670 compares it
//   0x7BE970  the load's apply (HUiSaveLoad completion 0x945680 -> 0x945741): status >= 0 and the read size == job+0x98
//             -> memcpy(job+0x90, data, size), the whole block; else nothing
//   0xDBCE0   the mode-save post-load step (flag 0x40): current row / difficulty to GS+0x48 / +0x4C, nothing else
// The file write and read themselves (0x7B9990 / 0x7B8DE0: platform file API) are not run; the signatures checked
// below pin that the write job sends exactly {header 0x14, GS+0x57A4 .. +0x16C4} and the read job accepts only
// size == 0x16C4 with a matching checksum. So the block goes to the file and back byte for byte.
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <vector>
#include "../src/mission_packs.h"
namespace {
unsigned char* image=nullptr;
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(GetEnvironmentVariableA("MPS_TRACE",nullptr,0))std::fprintf(stderr,"check %d: %s\n",checks,why);
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
bool Matches(unsigned rva,std::initializer_list<unsigned char> bytes) {
    return std::memcmp(image+rva,bytes.begin(),bytes.size())==0;
}
constexpr std::size_t kGsSize=0x150A0,kModeSave=0x57A4,kModeSaveSize=0x16C4,kSeen=0x14A0,kMarker=kSeen+508;
constexpr std::uint32_t kMarkerValue=0xA1B2C3D4u;
static_assert(kModeSave==crew::packs::kModeSave && kModeSaveSize==crew::packs::kModeSaveSize && kSeen==crew::packs::kRowBytes
              && kMarker==crew::packs::kMarker,"the plugin's save layout (src/mission_packs.h) is the one tested here");

// Point the mapping's CRT imports this test's code paths call at this process's own.
void* __cdecl MemCpy(void* d,const void* s,std::size_t n){return std::memcpy(d,s,n);}
void* __cdecl MemSet(void* d,int c,std::size_t n){return std::memset(d,c,n);}
void* __cdecl MemMove(void* d,const void* s,std::size_t n){return std::memmove(d,s,n);}
int __cdecl MemCmp(const void* a,const void* b,std::size_t n){return std::memcmp(a,b,n);}
errno_t __cdecl WcsNCpyS(wchar_t* d,rsize_t n,const wchar_t* s,rsize_t c){return wcsncpy_s(d,n,s,c);}   // 0xDFDD0 (the template)
int ResolveCrt() {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    const auto rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    int done=0;
    for(auto p=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image+rva);p->Name;++p) {
        if(!p->OriginalFirstThunk)continue;
        auto names=reinterpret_cast<const IMAGE_THUNK_DATA64*>(image+p->OriginalFirstThunk);
        auto slots=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+p->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
            const char* name=reinterpret_cast<const char*>(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(image+names->u1.AddressOfData)->Name);
            void* to=!std::strcmp(name,"memcpy") ? reinterpret_cast<void*>(&MemCpy) : !std::strcmp(name,"memset") ? reinterpret_cast<void*>(&MemSet)
                   : !std::strcmp(name,"memmove") ? reinterpret_cast<void*>(&MemMove) : !std::strcmp(name,"memcmp") ? reinterpret_cast<void*>(&MemCmp)
                   : !std::strcmp(name,"wcsncpy_s") ? reinterpret_cast<void*>(&WcsNCpyS) : nullptr;
            if(!to)continue;
            DWORD old=0;
            VirtualProtect(&slots->u1.Function,8,PAGE_READWRITE,&old);
            slots->u1.Function=reinterpret_cast<ULONGLONG>(to);
            VirtualProtect(&slots->u1.Function,8,old,&old);
            ++done;
        }
    }
    return done;
}

using ResetFn=void(__fastcall*)(unsigned char* gs);
using TemplateFn=unsigned char*(__fastcall*)(unsigned char* out);
using SeenSetFn=void(__fastcall*)(unsigned char* save,unsigned row,unsigned char flag);
using SeenGetFn=bool(__fastcall*)(unsigned char* save,unsigned row,unsigned char flag);
using ClearSetFn=void(__fastcall*)(unsigned char* save,unsigned cls,unsigned row,unsigned diff);
using ChecksumFn=std::uint32_t(__fastcall*)(const unsigned char* data,std::size_t size);
using ApplyFn=void(__fastcall*)(unsigned char* job);
using PostLoadFn=void(__fastcall*)(unsigned char* gs);

std::uint32_t Marker(const unsigned char* save){std::uint32_t m;std::memcpy(&m,save+kMarker,4);return m;}
// The load job's state as 0x7BE970 reads it: +0x30 status, +0x90 the destination, +0x98 its size, +0xB0 the read
// result {+8 data, +0x18 size} (0x7C1A9E resizes it to the header's size and copies the payload in).
struct Result { unsigned char pad[8]; unsigned char* data; std::size_t capacity; std::size_t size; unsigned char rest[0x10]; };
void Load(ApplyFn apply,unsigned char* gs,std::vector<unsigned char>& file,int status,std::size_t size=kModeSaveSize) {
    alignas(16) unsigned char job[0xB8]{};
    Result result{};
    result.data=file.data();result.capacity=file.size();result.size=size;
    std::int32_t s=status;
    std::memcpy(job+0x30,&s,4);
    unsigned char* dest=gs+kModeSave;
    std::memcpy(job+0x90,&dest,8);
    const std::size_t want=kModeSaveSize;
    std::memcpy(job+0x98,&want,8);
    Result* r=&result;
    std::memcpy(job+0xB0,&r,8);
    apply(job);
}
}

int main(int argc,char** argv) {
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: pass the supported EDF.dll path (private mapping, no DllMain)");return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Check(image!=nullptr,"private DLL mapped without its entrypoint");
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    Check(nt->FileHeader.TimeDateStamp==0x678CCB46 && nt->OptionalHeader.SizeOfImage==0x22CE000,"supported game profile");
    Check(ResolveCrt()>=4,"memcpy / memmove / memset / wcsncpy_s imports pointed at this process");

    // The code this test runs and the code it does not run but relies on, as read.
    Check(Matches(0xDBA00,{0x40,0x53,0xB8,0x00,0x17,0x00,0x00,0xE8}) && Matches(0xDBA31,{0x48,0x8D,0x8B,0xA4,0x57,0x00,0x00,0x41,0xB8,0xC4,0x16,0x00,0x00}),
          "0xDBA00: the template over GS+0x57A4, 0x16C4 bytes");
    Check(Matches(0xDB72B,{0x40,0xF6,0xC5,0x04,0x74,0x2A,0x48,0x8D,0x4C,0x24,0x50,0xE8,0x55,0x40,0x00,0x00,0x48,0x8B,0xD0,0x48,0x8D,0x8F,0xA4,0x57,0x00,0x00,0x41,0xB8,0xC4,0x16,0x00,0x00}),
          "0xDB580 bit 4: the same reset inline (0xDF790 template, 0x16C4 bytes)");
    Check(Matches(0xDC4B8,{0xBA,0x1C,0x00,0x00,0x00,0x48,0x8B,0xCD,0xE8,0xBB,0xF0,0xFF,0xFF,0x89,0x75,0x38}),
          "SetMode 0xDC460: DB580(0x1C) (mode save reset) and only then GS+0x38 = the new mode");
    Check(Matches(0xDF790,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x33,0xD2,0x48,0x83,0xC1,0x08,0x41}),"0xDF790 template");
    Check(Matches(0xDFA90,{0x8B,0xC2,0x41,0x80,0xF0,0x01,0x41,0xFE,0xC0,0x44,0x08,0x84,0x08,0xA0,0x14,0x00,0x00}),"0xDFA90 seen |= 1 or 2");
    Check(Matches(0xDFA20,{0x8B,0xC2,0x0F,0xB6,0x94,0x08,0xA0,0x14,0x00,0x00}),"0xDFA20 seen read");
    Check(Matches(0xDFAB0,{0x8B,0xC2,0x48,0xC1,0xE0,0x09,0x48,0x03,0xC8,0x41,0x8B,0xD0,0x0F,0xB6,0x44,0x11,0x08,0x44,0x0F,0xAB,0xC8,0x88,0x44,0x11,0x08}),
          "0xDFAB0 clear[class][row] |= 1 << diff");
    Check(Matches(0x7BE970,{0x83,0x79,0x30,0x00,0x7C,0x2C,0x48,0x8B,0x81,0x90,0x00,0x00,0x00,0x48,0x85,0xC0,0x74,0x20,0x48,0x8B,0x91,0xB0,0x00,0x00,0x00,
          0x4C,0x8B,0x42,0x18,0x4C,0x39,0x81,0x98,0x00,0x00,0x00,0x75,0x0C,0x48,0x8B,0x52,0x08,0x48,0x8B,0xC8,0xE9}),
          "0x7BE970: the load's apply is one memcpy of the whole read payload");
    Check(Matches(0x945741,{0xE8,0x2A,0x92,0xE7,0xFF}),"0x945741: the save/load UI's completion calls it");
    Check(Matches(0xDBCE0,{0x8B,0x81,0xA4,0x57,0x00,0x00,0x89,0x41,0x48,0x8B,0x81,0xA8,0x57,0x00,0x00,0x89,0x41,0x4C,0xC3}),"0xDBCE0 post-load");
    Check(Matches(0x7C220E,{0xC7,0x44,0x24,0x58,0x4D,0x44,0x42,0x30,0xC7,0x44,0x24,0x5C,0x03,0x00,0x00,0x00,0x8B,0x86,0xB0,0x00,0x00,0x00,0x89,0x44,0x24,0x68,
          0x8B,0x86,0x98,0x00,0x00,0x00,0x89,0x44,0x24,0x60,0x48,0x8B,0x96,0x98,0x00,0x00,0x00,0x48,0x8B,0x8E,0x90,0x00,0x00,0x00,0xE8,0x2B,0xF0,0xFF,0xFF}),
          "write job 0x7C1CB0: header MDB0 v3, size job+0x98, checksum 0x7C1270 over job+0x90");
    Check(Matches(0x7C225F,{0x48,0x8B,0x8E,0x90,0x00,0x00,0x00,0x48,0x8B,0x44,0x24,0x40,0x48,0x89,0x48,0x10,0x48,0x8B,0x8E,0x98,0x00,0x00,0x00}),
          "write job: the second buffer it writes is job+0x90 for job+0x98 bytes");
    Check(Matches(0x7C18C7,{0x4C,0x8B,0x4D,0x87,0x49,0x83,0xC1,0xEC,0x48,0x8B,0x93,0x98,0x00,0x00,0x00,0x4C,0x3B,0xCA,0x0F,0x84}),
          "read job 0x7C1670: file size - 0x14 must be job+0x98");
    Check(Matches(0x7C1A53,{0x4D,0x63,0x77,0x08,0x49,0x8D,0x4F,0x14,0xE8,0x10,0xF8,0xFF,0xFF,0x41,0x8B,0x4F,0x0C,0x85,0xC9,0x74,0x0D,0x3B,0xC1}),
          "read job: the header checksum checked with 0x7C1270 over the payload");

    const auto reset=reinterpret_cast<ResetFn>(image+0xDBA00);
    const auto tmpl=reinterpret_cast<TemplateFn>(image+0xDF790);
    const auto seenSet=reinterpret_cast<SeenSetFn>(image+0xDFA90);
    const auto seenGet=reinterpret_cast<SeenGetFn>(image+0xDFA20);
    const auto clearSet=reinterpret_cast<ClearSetFn>(image+0xDFAB0);
    const auto checksum=reinterpret_cast<ChecksumFn>(image+0x7C1270);
    const auto apply=reinterpret_cast<ApplyFn>(image+0x7BE970);
    const auto postLoad=reinterpret_cast<PostLoadFn>(image+0xDBCE0);

    auto* const gs=static_cast<unsigned char*>(VirtualAlloc(nullptr,kGsSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(gs!=nullptr,"a stand-in game status");
    unsigned char* const save=gs+kModeSave;

    // The template: everything the reset leaves, the marker 0.
    std::vector<unsigned char> blank(kModeSaveSize+0x40,0xCC);
    tmpl(blank.data());
    Check(Marker(blank.data())==0,"0xDF790: seen[508..511] zero in a fresh save");
    std::memset(save,0xCC,kModeSaveSize);
    reset(gs);
    // (The template leaves a few bytes past +0x1408 unwritten: the reset copies its stack there. Compared: the row,
    // the difficulty, clear[10][512] and seen[512], the parts the template writes.)
    const auto sameRows=[&](const unsigned char* a,const unsigned char* b){
        return std::memcmp(a,b,0x1408)==0 && std::memcmp(a+kSeen,b+kSeen,0x200)==0;
    };
    Check(sameRows(save,blank.data()),"0xDBA00: GS+0x57A4's row, clears and seen are the template's (all 0)");
    Check(Marker(save)==0 && *reinterpret_cast<std::uint32_t*>(gs+0x48)==0 && *reinterpret_cast<std::uint32_t*>(gs+0x4C)==1,"0xDBA00: row 0, difficulty 1");

    // A save with the marker, played through the game's own writers on every row a pack of ours has (< 508).
    std::memcpy(save+kMarker,&kMarkerValue,4);
    for(unsigned row=0;row<508;++row) {
        seenSet(save,row,static_cast<unsigned char>(row&1));
        for(unsigned cls=0;cls<4;++cls)clearSet(save,cls,row,row%5);
    }
    Check(Marker(save)==kMarkerValue,"the game's seen / clear writers on rows 0..507 leave the marker");
    Check(seenGet(save,7,1) && !seenGet(save,7,0) && seenGet(save,8,0) && !seenGet(save,8,1),"the seen reader agrees with the writer (flag 1: bit 0, flag 0: bit 1)");
    // seen[row] for rows 508..511 reads the marker's bytes: any non-zero byte is "before.bvm played" for a row no pack has.
    Check(seenGet(save,508,1)==((kMarkerValue&1u)!=0) && seenGet(save,508,0)==((kMarkerValue&2u)!=0),"row 508's seen is the marker's low byte (a row the list never has)");

    // Write: the job writes {header, GS+0x57A4 .. +0x16C4}; the header's checksum is over the live block, marker included.
    std::vector<unsigned char> file(save,save+kModeSaveSize);
    const std::uint32_t crc=checksum(save,kModeSaveSize);
    save[kMarker]^=1;
    Check(checksum(save,kModeSaveSize)!=crc,"the file checksum covers seen[508..511]");
    save[kMarker]^=1;

    // Next session: SetMode resets the block (marker gone from memory), then the load puts the file back whole.
    reset(gs);
    Check(Marker(save)==0,"SetMode's reset clears the marker in memory (the file still has it)");
    Check(checksum(file.data(),kModeSaveSize)==crc,"the read job's checksum check passes on the file as written");
    Load(apply,gs,file,0);
    Check(std::memcmp(save,file.data(),kModeSaveSize)==0 && Marker(save)==kMarkerValue,"load: the block is the file, marker kept");
    std::uint32_t row=17;
    std::memcpy(save,&row,4);
    postLoad(gs);
    Check(*reinterpret_cast<std::uint32_t*>(gs+0x48)==17 && Marker(save)==kMarkerValue,"0xDBCE0 touches the row only");

    // Second round: more play, written, reset, read: still there.
    for(unsigned r=0;r<300;++r)seenSet(save,r,0);
    std::vector<unsigned char> file2(save,save+kModeSaveSize);
    reset(gs);
    Load(apply,gs,file2,0);
    Check(Marker(save)==kMarkerValue && std::memcmp(save,file2.data(),kModeSaveSize)==0,"read -> save -> read: marker kept");

    // No file / a failed read (status < 0) or a payload of another size: nothing copied, the reset template stays.
    reset(gs);
    Load(apply,gs,file2,-5);
    Check(Marker(save)==0 && sameRows(save,blank.data()),"a failed load leaves the template (marker 0)");
    Load(apply,gs,file2,0,kModeSaveSize-4);
    Check(Marker(save)==0,"a payload of another size is not applied");

    std::printf("mission_pack_save_native_test: %d checks passed (marker crc %08x)\n",checks,crc);
    return 0;
}
