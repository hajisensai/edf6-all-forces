// The new classes' pure part (src/soldier_type_logic.h): the script boundary mapping, the nameplate and damage colours, the damage
// colour stub's bytes and the site list (src/soldier_sites.inc). The sites against EDF.dll: tests/soldier_native_audit.py.
#include "soldier_type_logic.h"

#include <cstdio>
#include <cstring>

namespace st=crew::soldier_type;
namespace {
int checks=0,failed=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { ++failed; std::printf("FAIL %s\n",what); }
}

void ScriptMapping() {
    // Plan §1: both new classes are 0 to the scripts; every stock type (and anything else) passes.
    Check(st::ScriptType(4)==0 && st::ScriptType(5)==0,"types 4 / 5 are 0 at the script boundary");
    for(int t=0;t<4;++t)Check(st::ScriptType(t)==t,"a stock type passes the script boundary");
    Check(st::ScriptType(6)==6 && st::ScriptType(-1)==-1,"other values pass as they are");
    Check(st::IsNewType(4) && st::IsNewType(5) && !st::IsNewType(3) && !st::IsNewType(6),"only 4 and 5 are new");
}

void Plates() {
    const st::Plate* v=st::PlateOf(4);
    const st::Plate* s=st::PlateOf(5);
    Check(v && v->letter==L'V' && v->r==1.0f && v->g==0.55f && v->b==0.0f,"type 4 plate: V, orange (1, 0.55, 0)");
    Check(s && s->letter==L'S' && s->r==0.6f && s->g==0.4f && s->b==1.0f,"type 5 plate: S, blue-purple (0.6, 0.4, 1)");
    Check(!st::PlateOf(0) && !st::PlateOf(3) && !st::PlateOf(6),"no plugin plate for stock types");
    // The reserve / resize patches make a 6-entry table: capacity 6, 6 * 0x30 bytes.
    unsigned char to[st::kMostSiteBytes];
    bool sawAlloc=false,sawResize=false;
    for(const st::Patch& p:st::kPlatePatches) {
        unsigned char from[st::kMostSiteBytes];
        const std::size_t n=st::Decode(p.from,from,sizeof(from));
        Check(n>0 && st::Decode(p.to,to,sizeof(to))==n,"a patch writes as many bytes as it expects");
        if(p.rva==0x8071F7) {
            std::uint32_t size;
            std::memcpy(&size,to+1,4);
            sawAlloc=size==st::kPlateStride*st::kTypes;
        }
        if(p.rva==0x8072B8)sawResize=to[1]==st::kTypes;
    }
    Check(sawAlloc,"the reserve allocates kTypes entries");
    Check(sawResize,"the resize asks for kTypes entries");
    Check(sizeof(st::kPlatePatches)/sizeof(st::kPlatePatches[0])==6,"six reserve / resize immediates");
}

void DamageRows() {
    constexpr st::DamageRows d=st::MakeDamageRows();
    for(int k=0;k<2;++k)
        for(int c=0;c<st::kDamageColumns;++c)
            Check(d.rgba[k][c][0]==st::kPlates[k].r && d.rgba[k][c][1]==st::kPlates[k].g && d.rgba[k][c][2]==st::kPlates[k].b
                  && d.rgba[k][c][3]==1.0f,"a damage row is the plate colour, opaque");
    Check(sizeof(d)==2*3*16,"two rows of three 16-byte colours");
}

void DamageStub() {
    unsigned char stock[st::kMostSiteBytes];
    const st::Site* site=st::FindPrint(st::kDamageSite);
    Check(site && st::Decode(site->hex,stock,sizeof(stock))==st::kDamageSiteSize,"the damage site print is the 22 bytes replaced");
    unsigned char code[96];
    std::memset(code,0xCC,sizeof(code));
    const std::uint64_t table=0x1122334455667788ull,back=0x0102030405060708ull;
    Check(st::BuildDamageStub(code,st::kDamageStubSize-1,stock,table,back)==0,"a short buffer builds nothing");
    const std::size_t n=st::BuildDamageStub(code,sizeof(code),stock,table,back);
    Check(n==st::kDamageStubSize,"the stub is kDamageStubSize bytes");
    // The test: lea ecx,[rax-4] / cmp ecx,1 / ja stock, the jump landing on the stock bytes.
    const unsigned char head[]={0x8D,0x48,0xFC,0x83,0xF9,0x01,0x77};
    Check(std::memcmp(code,head,sizeof(head))==0,"the stub tests type-4 <= 1 unsigned");
    const std::size_t stockAt=8+code[7];
    Check(stockAt==st::kDamageStubSize-st::kDamageSiteSize-14 && std::memcmp(code+stockAt,stock,st::kDamageSiteSize)==0,
          "ja lands on the stock bytes");
    // The plugin path: offset (3k + column) * 16 into the table.
    const unsigned char path[]={0x8D,0x14,0x49,0x48,0x01,0xDA,0x48,0xC1,0xE2,0x04,0x48,0xB8};
    Check(std::memcmp(code+8,path,sizeof(path))==0,"the plugin path computes (3k + rbx) << 4");
    std::uint64_t got;
    std::memcpy(&got,code+20,8);
    Check(got==table,"the table's address");
    const unsigned char load[]={0x0F,0x10,0x04,0x10,0xFF,0x25,0,0,0,0};
    Check(std::memcmp(code+28,load,sizeof(load))==0,"movups xmm0,[rax+rdx] then jmp [rip]");
    std::memcpy(&got,code+38,8);
    Check(got==back,"the plugin path jumps back");
    Check(stockAt==46,"the stock path follows the plugin path");
    const unsigned char jmp[]={0xFF,0x25,0,0,0,0};
    Check(std::memcmp(code+stockAt+st::kDamageSiteSize,jmp,sizeof(jmp))==0,"the stock path jumps back too");
    std::memcpy(&got,code+stockAt+st::kDamageSiteSize+6,8);
    Check(got==back,"to the same address");
    Check(code[st::kDamageStubSize]==0xCC,"nothing written past the stub");
}

void Sites() {
    for(const st::Site& s:st::kPrints) {
        unsigned char b[st::kMostSiteBytes];
        Check(st::Decode(s.hex,b,sizeof(b))>0,s.what);
    }
    unsigned char b[4];
    Check(st::Decode("4883 ec",b,sizeof(b))==3 && b[0]==0x48 && b[1]==0x83 && b[2]==0xEC,"spaces between bytes are skipped");
    Check(st::Decode("488",b,sizeof(b))==0,"an odd digit is malformed");
    Check(st::Decode("48XY",b,sizeof(b))==0,"a non-hex digit is malformed");
    Check(st::Decode("4848484848",b,sizeof(b))==0,"too long for the buffer");
    Check(st::FindPrint(0x550A20) && !st::FindPrint(0x1234),"FindPrint finds the listed only");
    // Each print the code relies on is there.
    for(std::size_t rva:{0x54FE50,0x54FF10,0x54FF39,0x5505B0,0x550654,0x550414,0x550420,0x550A20,0x12D85EC,0x804875,0x806FE0,
                         0x8073A4,0x3D450,0x81F837,0x81F844,0x81F85A,0x1B5092,0x222810})
        Check(st::FindPrint(rva)!=nullptr,"a print the code checks is listed");
    // The script sites are the slot 83 call inside their prints (3 bytes in: after mov rax,[rcx]).
    for(std::size_t i=0;i<2;++i) {
        const st::Site* p=st::FindPrint(st::kScriptSites[i]-3);
        unsigned char code[st::kMostSiteBytes];
        Check(p && st::Decode(p->hex,code,sizeof(code))>=9 && std::memcmp(code+3,st::kTypeCall,st::kScriptSiteSize)==0,
              "a script site is the slot 83 call");
    }
    Check(st::kMainVtable[st::kSlotType]==0x550A20 && st::kMainVtable[1]==st::kDtor,"slot 83 the type, slot 1 the destructor");
    Check(st::kStockSize%16==0,"the plugin's part starts 16-aligned");
}
}  // namespace

int main() {
    ScriptMapping();
    Plates();
    DamageRows();
    DamageStub();
    Sites();
    std::printf("soldier_type_test: %d/%d checks passed\n",checks-failed,checks);
    return failed ? 1 : 0;
}
