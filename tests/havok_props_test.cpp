// hknpEventDispatcher's uint32 index sites (src/havok_props_logic.h) offline. A small decoder of the few instruction
// forms the sites use reads each site's stock and patched bytes; every pair has to be one of the widening rewrites
// (docs/havok-props-re.md): uint16 -> uint32 access of the same register / memory, the moved fields, sentinel and
// element-size immediates, the reserve call target; the rest nop padding. The two reserve functions agree but for
// the element size.
// cmake --build build --target havok_props_test && build\havok_props_test.exe
#include "../src/havok_props_logic.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace crew::havokprops;
int failures=0,checks=0;
bool Expect(bool ok,const char* what,std::uint32_t rva=0) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %#x %s\n",rva,what);}
    return ok;
}

enum class Op { Bad, Movzx16, MovLoad, MovStore, Cmp, Lea, AddImm8, MovImm, Call, Nop };
struct Mem { bool on=false; int base=-1,index=-1,scale=1; std::int32_t disp=0; };
struct Insn {
    Op op=Op::Bad; int len=0,width=32;   // width: the operation's register width
    int reg=-1,rm=-1;                    // rm: a register operand (mod 3), else mem
    Mem mem; std::int64_t imm=0; std::uint32_t target=0;
};

std::int32_t Rd32(const std::uint8_t* b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(b[0])|static_cast<std::uint32_t>(b[1])<<8|
                                     static_cast<std::uint32_t>(b[2])<<16|static_cast<std::uint32_t>(b[3])<<24);
}

// One instruction at b (n bytes available), at rva; Op::Bad when it is no form the sites use.
Insn Decode(const std::uint8_t* b,int n,std::uint32_t rva) {
    Insn in;
    int i=0;
    bool w16=false;
    int rex=0;
    if(i<n && b[i]==0x66){w16=true;++i;}
    if(i<n && (b[i]&0xF0)==0x40){rex=b[i];++i;}
    if(i>=n)return in;
    const bool w64=(rex&8)!=0;
    in.width=w64 ? 64 : w16 ? 16 : 32;
    auto modrm=[&](Insn& x)->bool {
        if(i>=n)return false;
        const int m=b[i++],mod=m>>6,reg=(m>>3)&7,rm=m&7;
        x.reg=reg|((rex&4)<<1);
        if(mod==3){x.rm=rm|((rex&1)<<3);return true;}
        x.mem.on=true;
        if(rm==4) {
            if(i>=n)return false;
            const int s=b[i++],base=s&7,index=((s>>3)&7)|((rex&2)<<2);
            x.mem.scale=1<<(s>>6);
            x.mem.index=index==4 ? -1 : index;
            if(base==5 && mod==0)return false;   // no base: not a form the sites use
            x.mem.base=base|((rex&1)<<3);
        } else {
            if(rm==5 && mod==0)return false;     // rip-relative: not a form the sites use
            x.mem.base=rm|((rex&1)<<3);
        }
        if(mod==1){if(i>=n)return false;x.mem.disp=static_cast<std::int8_t>(b[i++]);}
        else if(mod==2){if(i+4>n)return false;x.mem.disp=Rd32(b+i);i+=4;}
        return true;
    };
    const int op=b[i++];
    if(op==0x90 && !w16 && !rex){in.op=Op::Nop;}
    else if(op==0x0F) {
        if(i>=n || b[i++]!=0xB7 || !modrm(in))return in;
        in.op=Op::Movzx16;
    }
    else if(op==0x8B){if(!modrm(in))return in;in.op=Op::MovLoad;}
    else if(op==0x89){if(!modrm(in))return in;in.op=Op::MovStore;}
    else if(op==0x3B){if(!modrm(in))return in;in.op=Op::Cmp;}
    else if(op==0x8D){if(!modrm(in) || !in.mem.on)return in;in.op=Op::Lea;}
    else if(op==0x83) {
        if(!modrm(in) || in.reg%8!=0 || i>=n)return in;   // /0: add
        in.imm=static_cast<std::int8_t>(b[i++]);
        in.op=Op::AddImm8;
    }
    else if(op>=0xB8 && op<=0xBF) {
        if(i+4>n)return in;
        in.reg=(op-0xB8)|((rex&1)<<3);
        in.imm=static_cast<std::uint32_t>(Rd32(b+i));
        i+=4;
        in.op=Op::MovImm;
    }
    else if(op==0xE8 && !w16 && !rex) {
        if(i+4>n)return in;
        in.target=static_cast<std::uint32_t>(static_cast<std::int64_t>(rva)+i+4+Rd32(b+i));
        i+=4;
        in.op=Op::Call;
    }
    else return in;
    in.len=i;
    return in;
}

std::vector<Insn> DecodeAll(const std::uint8_t* b,int n,std::uint32_t rva) {
    std::vector<Insn> out;
    for(int at=0;at<n;) {
        Insn in=Decode(b+at,n-at,rva+static_cast<std::uint32_t>(at));
        if(in.op==Op::Bad){out.push_back(in);return out;}
        at+=in.len;
        out.push_back(in);
    }
    return out;
}

bool SameMem(const Mem& a,const Mem& b) {
    return a.on==b.on && a.base==b.base && a.index==b.index && a.scale==b.scale && a.disp==b.disp;
}

// How a stock memory operand became the patched one.
enum class MemRel { None, Same, Field, Next, Scale4, Swap, Plus2, Bad };
MemRel Relate(const Mem& s,const Mem& p) {
    if(!s.on && !p.on)return MemRel::None;
    if(!s.on || !p.on)return MemRel::Bad;
    if(SameMem(s,p))return MemRel::Same;
    if(s.base==p.base && s.index==p.index && s.scale==p.scale) {
        // the moved heads: free list +0x38 -> +0x3C, global list +0x60 -> +0x64
        if(s.index<0 && ((s.disp==0x38 && p.disp==0x3C) || (s.disp==0x60 && p.disp==0x64)))return MemRel::Field;
        // a node's next: +0 -> +4 (the node itself, or nodes[i] at index*8 with index = 2*node)
        if(s.disp==0 && p.disp==4 && (s.index<0 || s.scale==8))return MemRel::Next;
        // lea's +2 -> +4: an element size or a stride (CheckPair allows it for lea only)
        if(s.index<0 && s.disp==2 && p.disp==4)return MemRel::Plus2;
    }
    // uint16 arrays indexed *2 -> uint32 *4
    if(s.base==p.base && s.index==p.index && s.index>=0 && s.disp==p.disp && s.scale==2 && p.scale==4)return MemRel::Scale4;
    // [r8+rcx] with r8 = 2*id -> [rcx+r8*2] = 4*id (0xE75CE0 / 0xE75CED)
    if(s.base==p.index && s.index==p.base && s.scale==1 && p.scale==2 && s.disp==0 && p.disp==0)return MemRel::Swap;
    return MemRel::Bad;
}

// Counts of each kind of rewrite, checked against the design at the end.
struct Counts { int load=0,store=0,cmp=0,movzxReg=0,sentinel=0,elemSize=0,stride=0,lea=0,call=0,copy=0,nop=0; };

void CheckPair(const Site& s,const Insn& a,const Insn& b,Counts& c) {
    const std::uint32_t r=s.rva;
    const MemRel rel=Relate(a.mem,b.mem);
    if(!Expect(rel!=MemRel::Bad,"memory operand not one of the widening rewrites",r))return;
    if(rel==MemRel::Plus2 && !Expect(a.op==Op::Lea,"+2 -> +4 only in an lea",r))return;
    if(r==0xE758EF) {   // the realloc's node copy: word +0 -> qword +0..+7, carrying the new next at +4
        Expect((a.width==32 || a.width==16) && b.width==64 && rel==MemRel::Same && a.reg==b.reg,"realloc copy: 16-bit -> qword, same operand",r);
        if(a.op==Op::Movzx16)Expect(b.op==Op::MovLoad && a.mem.disp==-8,"realloc copy load [rcx-8]",r);
        else Expect(a.op==Op::MovStore && b.op==Op::MovStore,"realloc copy store",r);
        ++c.copy;
        return;
    }
    switch(a.op) {
    case Op::Movzx16:
        Expect(b.op==Op::MovLoad && b.width==32 && a.reg==b.reg,"movzx r32,r/m16 -> mov r32,r/m32",r);
        if(a.mem.on) {
            Expect(rel==MemRel::Field || rel==MemRel::Next || rel==MemRel::Scale4 || rel==MemRel::Swap ||
                   (rel==MemRel::Same && (a.mem.disp==0x48 || a.mem.index>=0)),
                   "load: a moved head, a node's next, a widened array element, or the node count",r);
            ++c.load;
        } else {
            Expect(a.rm==b.rm,"movzx r32,r16 -> mov r32,r32 of the same register",r);
            ++c.movzxReg;
        }
        break;
    case Op::MovStore:
        Expect(b.op==Op::MovStore && a.width==16 && b.width==32 && a.reg==b.reg && a.mem.on,"16-bit store -> 32-bit store",r);
        Expect(rel==MemRel::Field || rel==MemRel::Next || rel==MemRel::Scale4 || rel==MemRel::Swap || rel==MemRel::Same,
               "store: a moved head, a node's next, a widened array element",r);
        ++c.store;
        break;
    case Op::Cmp:
        Expect(b.op==Op::Cmp && a.width==16 && b.width==32 && a.reg==b.reg && a.rm==b.rm && !a.mem.on,
               "cmp r16,r16 -> cmp r32,r32",r);
        ++c.cmp;
        break;
    case Op::MovImm:
        Expect(b.op==Op::MovImm && a.reg==b.reg && a.width==32 && b.width==32,"mov r32,imm32 of the same register",r);
        if(a.imm==0xFFFF)Expect(b.imm==0xFFFFFFFF,"sentinel 0xFFFF -> 0xFFFFFFFF",r),++c.sentinel;
        else Expect(a.imm==2 && b.imm==4 && a.reg==8,"free element size r8d 2 -> 4",r),++c.elemSize;
        break;
    case Op::Lea:
        Expect(b.op==Op::Lea && a.reg==b.reg && a.width==b.width,"lea of the same register",r);
        if(rel==MemRel::Scale4)++c.lea;
        else if(rel==MemRel::Plus2) {
            // lea r8d,[r14/r15+2]: the free's element size (r14/r15 zero there); lea rsi/rdi,[x+2]: the pending stride
            if(a.reg==8 && a.width==32)++c.elemSize;
            else Expect(a.reg==a.mem.base && a.width==64,"lea r,[r+2] -> [r+4]: the pending list stride",r),++c.stride;
        } else Expect(false,"lea: *2 -> *4 or +2 -> +4",r);
        break;
    case Op::AddImm8:
        Expect(b.op==Op::AddImm8 && a.rm==b.rm && a.rm>=0 && a.width==64 && a.imm==2 && b.imm==4,"add r64,2 -> 4",r);
        ++c.stride;
        break;
    case Op::Call:
        Expect(b.op==Op::Call && a.target==kReserveU16 && b.target==kReserveU32,"call 0xA55860 -> 0x978600",r);
        ++c.call;
        break;
    default: Expect(false,"stock instruction not one the sites rewrite",r);
    }
}
}  // namespace

int main() {
    Counts c;
    std::size_t bytes=0;
    for(std::size_t k=0;k<kSiteCount;++k) {
        const Site& s=kSites[k];
        bytes+=s.size;
        Expect(s.rva>=kFamilyBegin && s.rva+s.size<=kFamilyEnd,"inside the dispatcher's functions",s.rva);
        if(k)Expect(kSites[k-1].rva+kSites[k-1].size<=s.rva,"sorted, not overlapping",s.rva);
        Expect(std::memcmp(s.stock,s.patched,s.size)!=0,"changes something",s.rva);
        const std::vector<Insn> a=DecodeAll(s.stock,s.size,s.rva),b=DecodeAll(s.patched,s.size,s.rva);
        if(!Expect(!a.empty() && a.back().op!=Op::Bad,"stock bytes decode as whole instructions",s.rva))continue;
        if(!Expect(!b.empty() && b.back().op!=Op::Bad,"patched bytes decode as whole instructions",s.rva))continue;
        std::size_t real=0;
        while(real<b.size() && b[real].op!=Op::Nop)++real;
        for(std::size_t j=real;j<b.size();++j)Expect(b[j].op==Op::Nop,"only trailing nops pad",s.rva),++c.nop;
        Expect(real==a.size(),"one patched instruction per stock instruction",s.rva);
        for(const Insn& x:a)Expect(x.op!=Op::Nop,"no stock nop",s.rva);
        if(real==a.size())for(std::size_t j=0;j<a.size();++j)CheckPair(s,a[j],b[j],c);
    }
    Expect(kSiteCount==137 && bytes==548,"137 sites, 548 bytes");
    std::printf("loads %d stores %d cmps %d movzx-reg %d sentinels %d elem-sizes %d strides %d lea*4 %d calls %d copy %d nops %d\n",
                c.load,c.store,c.cmp,c.movzxReg,c.sentinel,c.elemSize,c.stride,c.lea,c.call,c.copy,c.nop);
    Expect(c.call==3,"three reserves of the uint16 arrays: ctor, getBodySignal (+0x50), onBodyRemoved (+0x68)");
    Expect(c.elemSize==4,"two destructors, each freeing +0x68 and +0x50");
    Expect(c.load==40 && c.store==19 && c.cmp==29 && c.movzxReg==22 && c.stride==5 && c.lea==4 && c.nop==87,
           "the rewrite counts of docs/havok-props-re.md");
    Expect(c.load+c.store+c.cmp+c.movzxReg+c.sentinel+c.elemSize+c.stride+c.lea+c.call+c.copy==138,
           "138 instructions: one per site, two at the realloc's copy");
    Expect(c.copy==2,"the realloc's one node copy (load and store)");
    Expect(c.sentinel==10,"ten 0xFFFF sentinels: ctor, allocEntry, three dispatches, freeChain, unlink, two getSignals, onBodyRemoved");

    // The reserves: the same code but for the element size and two calls to the same targets.
    int diffs=0;
    for(std::uint32_t i=0;i<kReserveSize;++i)
        if(kReserveU16Bytes[i]!=kReserveU32Bytes[i]) {
            ++diffs;
            Expect((i>=0x31 && i<0x35) || i==0x44 || (i>=0x63 && i<0x67),"reserves differ only at the calls and the element size",i);
        }
    Expect(diffs<=9,"at most 9 differing bytes");
    Expect(kReserveU16Bytes[0x44]==2 && kReserveU32Bytes[0x44]==4 && kReserveU16Bytes[0x42]==0x41 && kReserveU16Bytes[0x43]==0xB8,
           "mov r8d,2 / mov r8d,4: the element size");
    for(std::uint32_t at:{0x30u,0x62u}) {
        Expect(kReserveU16Bytes[at]==0xE8 && kReserveU32Bytes[at]==0xE8,"a call at +0x30 / +0x62",at);
        const auto t16=static_cast<std::uint32_t>(static_cast<std::int64_t>(kReserveU16)+at+5+Rd32(kReserveU16Bytes+at+1));
        const auto t32=static_cast<std::uint32_t>(static_cast<std::int64_t>(kReserveU32)+at+5+Rd32(kReserveU32Bytes+at+1));
        Expect(t16==t32 && (t16==0x97C750 || t16==0x97C890),"both reserves call the same function",at);
    }
    std::printf("havok_props: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
