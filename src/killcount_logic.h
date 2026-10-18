// The battle record's rows and the counter store, as src/killcount.cpp reads them (docs/kill-counter-re.md); no game
// state here, so tests/killcount_test.cpp runs it on made-up bytes. EDF.dll TimeDateStamp 0x678CCB46.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace killcount {
// One row of the battle record's table (EDF+0x893C40 fills 90 of them at rbp+0x270): its counter's name, how the value
// is printed (0 "%d", 1 h:mm:ss, 2 "%d" + StatusInfo_Count, 3 "%d%%", 4 "%dm": the switch at 0x895263) and the factor
// the value is scaled by before (1.0; 0.01 for the clear ratios).
struct Row {
    const wchar_t* name;
    std::uint8_t format;
    std::uint8_t pad[3];
    float scale;
};
static_assert(sizeof(Row)==16 && offsetof(Row,format)==8 && offsetof(Row,scale)==12,"the stack table's row");

constexpr std::size_t kFrameRows=0x270;   // the table's first row in the constructor's frame (rbp+0x270)
constexpr int kStockRows=90;              // 0x5A: both loops' bound (0x8949A2, 0x895161)
constexpr int kMostRows=127;              // the bound is an imm8 compare: signed

// The rows the constructor's straight-line fill (rva `begin` .. `end`) writes, read from its instructions: each row is
//   lea rax,[rip+name]            48 8D 05 d32
//   mov [rbp+0x270+16i],rax       48 89 85 o32
//   mov byte [rbp+0x278+16i],f    C6 85 o32 imm8
//   mov dword [rbp+0x27C+16i],s   C7 85 o32 imm32   (WeaponGetRatio: movss xmm0,[rip+s] F3 0F 10 05 d32;
//                                                     movss [rbp+0x27C+16i],xmm0 F3 0F 11 85 o32)
// `image` is the module (rip targets are image offsets). The row count, or -1 for any other instruction, a store out of
// the table, a row left incomplete or more than `most` rows.
inline int ParseRows(const unsigned char* image,std::size_t begin,std::size_t end,Row* rows,int most) noexcept {
    enum : unsigned { kName=1,kFormat=2,kScale=4,kAll=7 };
    unsigned filled[kMostRows]{};
    if(most>kMostRows)most=kMostRows;
    int count=0;
    std::uintptr_t rax=0;
    float xmm0=0.0f;
    bool haveRax=false,haveXmm=false;
    auto d32=[&](std::size_t at){std::int32_t v;std::memcpy(&v,image+at,4);return v;};
    auto slot=[&](std::int32_t off,std::int32_t field,int* row)->bool {
        const std::int64_t rel=static_cast<std::int64_t>(off)-static_cast<std::int64_t>(kFrameRows);
        if(rel<0 || rel%16!=field)return false;
        *row=static_cast<int>(rel/16);
        if(*row>=most)return false;
        if(*row+1>count)count=*row+1;
        return true;
    };
    for(std::size_t at=begin;at<end;) {
        const unsigned char* p=image+at;
        int row=0;
        if(p[0]==0x48 && p[1]==0x8D && p[2]==0x05) {
            rax=reinterpret_cast<std::uintptr_t>(image)+at+7+d32(at+3);haveRax=true;at+=7;
        } else if(p[0]==0x48 && p[1]==0x89 && p[2]==0x85) {
            if(!haveRax || !slot(d32(at+3),0,&row))return -1;
            rows[row].name=reinterpret_cast<const wchar_t*>(rax);filled[row]|=kName;at+=7;
        } else if(p[0]==0xC6 && p[1]==0x85) {
            if(!slot(d32(at+2),8,&row))return -1;
            rows[row].format=p[6];filled[row]|=kFormat;at+=7;
        } else if(p[0]==0xC7 && p[1]==0x85) {
            if(!slot(d32(at+2),12,&row))return -1;
            std::memcpy(&rows[row].scale,p+6,4);filled[row]|=kScale;at+=10;
        } else if(p[0]==0xF3 && p[1]==0x0F && p[2]==0x10 && p[3]==0x05) {
            std::memcpy(&xmm0,image+at+8+d32(at+4),4);haveXmm=true;at+=8;
        } else if(p[0]==0xF3 && p[1]==0x0F && p[2]==0x11 && p[3]==0x85) {
            if(!haveXmm || !slot(d32(at+4),12,&row))return -1;
            rows[row].scale=xmm0;filled[row]|=kScale;at+=8;
        } else return -1;
    }
    for(int i=0;i<count;++i) {
        if(filled[i]!=kAll)return -1;
        rows[i].pad[0]=rows[i].pad[1]=rows[i].pad[2]=0;
    }
    return count;
}

// The counter store (EDF+0x20B2890's +0x14B50): a std::map<std::wstring, variable> (MSVC: node +0 left, +8 parent,
// +0x10 right, +0x19 is-nil; the key's wstring at +0x20: its characters inline when its capacity (+0x38) is under 8,
// else a pointer, its length at +0x30; the variable's type at +0x40, 0 int / 1 float; the head's parent is the root),
// ordered by the names' UTF-16 code units (0xCFAB0's compare). `readable(p, n)` guards every read.
constexpr std::size_t kNodeLeft=0x00,kNodeParent=0x08,kNodeRight=0x10,kNodeNil=0x19,kNodeKey=0x20,kKeySize=0x30,
                      kKeyCapacity=0x38,kNodeType=0x40,kNodeSize=0x48;

template<class Readable>
int Compare(const unsigned char* node,const wchar_t* name,Readable readable) noexcept {   // name <=> key, or 2
    std::uint64_t size=0,capacity=0;
    std::memcpy(&size,node+kKeySize,8);
    std::memcpy(&capacity,node+kKeyCapacity,8);
    if(size>256)return 2;
    const wchar_t* key=reinterpret_cast<const wchar_t*>(node+kNodeKey);
    if(capacity>=8)std::memcpy(&key,node+kNodeKey,sizeof(key));
    if(!readable(key,size*sizeof(wchar_t)+sizeof(wchar_t)))return 2;
    for(std::uint64_t i=0;;++i) {
        const unsigned a=static_cast<std::uint16_t>(name[i]);
        if(i==size)return a ? 1 : 0;
        const unsigned b=static_cast<std::uint16_t>(key[i]);
        if(!a || a<b)return -1;
        if(a>b)return 1;
    }
}

// The store's int variable named `name` (its node), or nullptr: none of that name, a variable of another type, a tree
// that does not read as one.
template<class Readable>
const unsigned char* FindInt(const unsigned char* head,const wchar_t* name,Readable readable) noexcept {
    if(!head || !readable(head,kNodeSize))return nullptr;
    const unsigned char* node=nullptr;
    std::memcpy(&node,head+kNodeParent,sizeof(node));
    for(int depth=0;depth<64;++depth) {
        if(!node || !readable(node,kNodeSize) || node[kNodeNil])return nullptr;
        const int c=Compare(node,name,readable);
        if(c==2)return nullptr;
        if(c==0) {
            std::int32_t type=-1;
            std::memcpy(&type,node+kNodeType,4);
            return type==0 ? node : nullptr;
        }
        std::memcpy(&node,node+(c<0 ? kNodeLeft : kNodeRight),sizeof(node));
    }
    return nullptr;
}
}  // namespace killcount
