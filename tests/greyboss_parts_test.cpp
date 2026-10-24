// src/greyboss_parts.cpp on a private image: the three event handlers' calls of 0x4444B0 are real rel32 calls in
// stand-in functions, 0x4444B0 a jump to a stand-in that counts. A boss's parts map is laid out as the native one
// (docs/greyboss-parts-re.md): a name the map lacks gives null without the native lookup (the crash of
// 2026-10-24 read past the map's end there); a name it has, or a list that cannot be walked, goes to the native call.
#include "../src/greyboss_parts.cpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>
namespace crew {
unsigned char* image=nullptr;
int logged=0;
void Log(const char*,...) noexcept { ++logged; }
}
namespace {
using namespace crew;
int checks=0,failed=0,nativeCalls=0;
void* nativeResult=reinterpret_cast<void*>(0x5150);
void Check(bool yes,const char* label,int line) { ++checks;if(!yes){++failed;std::printf("%d: %s\n",line,label);} }
#define CHECK(x) Check((x),#x,__LINE__)
void Jump(unsigned char* at,const void* fn) { at[0]=0x48;at[1]=0xB8;std::memcpy(at+2,&fn,8);at[10]=0xFF;at[11]=0xE0; }
void* __fastcall FakeLookup(void*,const wchar_t*) { ++nativeCalls;return nativeResult; }
using HandlerFn=void*(__fastcall*)(void*,const wchar_t*);
// Each site: sub rsp,0x28 / call 0x4444B0 / add rsp,0x28 / ret, entered 4 bytes before the call.
void Image() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x500000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image)std::exit(2);
    for(const auto& site:greyboss_parts::sites) {
        const unsigned char before[]={0x48,0x83,0xEC,0x28},after[]={0x48,0x83,0xC4,0x28,0xC3};
        std::memcpy(image+site.rva-4,before,4);std::memcpy(image+site.rva+5,after,5);image[site.rva]=0xE8;
        const auto relative=static_cast<std::int32_t>(greyboss_parts::kLookup-site.rva-5);std::memcpy(image+site.rva+1,&relative,4);
    }
    Jump(image+greyboss_parts::kLookup,reinterpret_cast<const void*>(&FakeLookup));
    FlushInstructionCache(GetCurrentProcess(),image,0x500000);
}
void* Call(unsigned i,void* root,const wchar_t* name) {
    return reinterpret_cast<HandlerFn>(image+greyboss_parts::sites[i].rva-4)(root,name);
}

// The boss's unordered_map<wstring, weak_ptr<part>> as the native lays it out: the head node at +0x17E8, the size at
// +0x17F0; each node {next, prev, wstring key at +0x10 (size +0x20, capacity +0x28), weak_ptr at +0x40}.
struct Boss {
    std::vector<unsigned char> object=std::vector<unsigned char>(0x1900);
    std::vector<std::vector<unsigned char>> nodes;std::vector<std::wstring> heap;
    unsigned char head[0x50]{};
    explicit Boss(std::initializer_list<const wchar_t*> names) {
        nodes.reserve(names.size());heap.reserve(names.size());
        for(const auto* n:names) {
            nodes.emplace_back(0x50);auto& node=nodes.back();
            const std::size_t length=std::wcslen(n);
            Put<std::size_t>(node.data(),0x20,length);
            if(length<8) { Put<std::size_t>(node.data(),0x28,7);std::memcpy(node.data()+0x10,n,(length+1)*sizeof(wchar_t)); }
            else { heap.emplace_back(n);Put<std::size_t>(node.data(),0x28,length);Put<const wchar_t*>(node.data(),0x10,heap.back().c_str()); }
            Put<std::uintptr_t>(node.data(),0x40,0x1234);
        }
        // The head's own value is never constructed: stale bytes, as the crash found (0x7200000067 at +0x48 + 8).
        std::memset(head,0x5F,sizeof(head));
        Link();
    }
    void Link() {
        unsigned char* previous=head;
        for(auto& node:nodes) { Put<unsigned char*>(previous,0,node.data());Put<unsigned char*>(node.data(),8,previous);previous=node.data(); }
        Put<unsigned char*>(previous,0,head);Put<unsigned char*>(head,8,previous);
        Put<unsigned char*>(object.data(),0x17E8,head);Put<std::size_t>(object.data(),0x17F0,nodes.size());
    }
    void* root() { return object.data(); }
};
}

int main() {
    Image();
    // A site that no longer calls 0x4444B0: nothing is installed, nothing written.
    auto& last=greyboss_parts::sites[2];
    const auto saved=At<std::int32_t>(image,last.rva+1);
    Put<std::int32_t>(image,last.rva+1,saved+1);
    CHECK(!InstallGreyBossParts());
    for(const auto& site:greyboss_parts::sites)CHECK(!site.changed && !site.thunk);
    Put<std::int32_t>(image,last.rva+1,saved);

    CHECK(InstallGreyBossParts());CHECK(InstallGreyBossParts());
    for(const auto& site:greyboss_parts::sites)CHECK(site.changed && site.thunk && std::memcmp(image+site.rva,site.patched,5)==0);

    // E516's parts (E516_BIGGREYBOSS.SGO `parts`): no 中心砲台.
    Boss boss{L"hand_muzzle_l",L"hand_muzzle_r",L"ジェノサイド砲",L"リング小",L"リング中",L"リング大",L"エフェクト中",L"エフェクト大",
              L"エフェクトキック",L"エフェクトダイブ"};
    for(unsigned i=0;i<3;++i) {
        nativeCalls=0;
        CHECK(Call(i,boss.root(),L"中心砲台")==nullptr);CHECK(nativeCalls==0);
        CHECK(Call(i,boss.root(),L"リング大")==nativeResult);CHECK(nativeCalls==1);           // short key, in the node
        CHECK(Call(i,boss.root(),L"hand_muzzle_r")==nativeResult);CHECK(nativeCalls==2);      // long key, on the heap
        CHECK(Call(i,boss.root(),L"hand_muzzle_")==nullptr);CHECK(nativeCalls==2);            // a prefix is not the key
        CHECK(Call(i,boss.root(),L"")==nullptr);CHECK(nativeCalls==2);
    }
    CHECK(crew::logged>0 && crew::logged<=8);

    // An empty map: every name is missing.
    Boss none{};nativeCalls=0;
    CHECK(Call(0,none.root(),L"hand_muzzle_l")==nullptr);CHECK(nativeCalls==0);

    // A list that cannot be walked is the native call's, as before.
    Boss broken{L"hand_muzzle_l",L"リング大"};
    Put<std::size_t>(broken.object.data(),0x17F0,1);                  // the size disagrees with the links
    nativeCalls=0;CHECK(Call(0,broken.root(),L"中心砲台")==nativeResult);CHECK(nativeCalls==1);
    broken.Link();Put<unsigned char*>(broken.nodes[1].data(),8,broken.head);   // a back link that is not the walk's
    CHECK(Call(1,broken.root(),L"中心砲台")==nativeResult);CHECK(nativeCalls==2);
    broken.Link();Put<unsigned char*>(broken.object.data(),0x17E8,nullptr);    // no head
    CHECK(Call(2,broken.root(),L"中心砲台")==nativeResult);CHECK(nativeCalls==3);

    // Restore: the original calls back, the native lookup reached directly again.
    image[last.rva]=0xCC;CHECK(!RestoreGreyBossParts());CHECK(last.changed && last.thunk);
    CHECK(!InstallGreyBossParts());CHECK(last.changed && last.thunk);   // a foreign write is not taken over
    std::memcpy(image+last.rva,last.patched,5);CHECK(RestoreGreyBossParts());
    for(const auto& site:greyboss_parts::sites)CHECK(!site.changed && !site.thunk && std::memcmp(image+site.rva,site.original,5)==0);
    nativeCalls=0;CHECK(Call(0,boss.root(),L"中心砲台")==nativeResult);CHECK(nativeCalls==1);
    CHECK(RestoreGreyBossParts());
    CHECK(InstallGreyBossParts());CHECK(RestoreGreyBossParts());
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    CHECK(!InstallGreyBossParts());
    std::printf("GreyBoss part events: %d checks, %d failures\n",checks,failed);
    return failed ? 1 : 0;
}
