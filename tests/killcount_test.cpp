// src/killcount_logic.h on made-up bytes: the battle record's row fill as the constructor encodes it, and the counter
// store's tree lookup (docs/kill-counter-re.md). The real bytes: tests/killcount_native_audit.py.
#include "killcount_logic.h"
#include "edf41_kill_counters.h"

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
int failures=0;
void Check(bool ok,const char* what) {
    if(!ok){std::printf("FAIL %s\n",what);++failures;}
}

struct Image {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x400,0xCC);
    std::size_t code=0x100;
    void Put(std::size_t at,const void* p,std::size_t n){std::memcpy(bytes.data()+at,p,n);}
    void Op(std::initializer_list<unsigned char> b){for(unsigned char x:b)bytes[code++]=x;}
    void D32(std::int32_t v){std::memcpy(bytes.data()+code,&v,4);code+=4;}
    // lea rax,[rip+target]
    void Lea(std::size_t target){Op({0x48,0x8D,0x05});D32(static_cast<std::int32_t>(target-(code+4)));}
    void StoreName(int row){Op({0x48,0x89,0x85});D32(0x270+16*row);}
    void StoreFormat(int row,unsigned char f){Op({0xC6,0x85});D32(0x278+16*row);Op({f});}
    void StoreScale(int row,float s){Op({0xC7,0x85});D32(0x27C+16*row);std::memcpy(bytes.data()+code,&s,4);code+=4;}
    void LoadXmm(std::size_t target){Op({0xF3,0x0F,0x10,0x05});D32(static_cast<std::int32_t>(target-(code+4)));}
    void StoreXmm(int row){Op({0xF3,0x0F,0x11,0x85});D32(0x27C+16*row);}
};

void Rows() {
    Image im;
    const wchar_t a[]=L"FrameCount",b[]=L"WeaponGetRatio",c[]=L"GiantAntKillCount";
    im.Put(0x20,a,sizeof(a));im.Put(0x60,b,sizeof(b));im.Put(0xA0,c,sizeof(c));
    const float ratio=100.0f;
    im.Put(0xF0,&ratio,4);
    const std::size_t begin=im.code;
    im.Lea(0x20);im.StoreName(0);im.StoreFormat(0,1);im.StoreScale(0,1.0f);
    im.Lea(0x60);im.StoreName(1);im.StoreFormat(1,3);im.LoadXmm(0xF0);im.StoreXmm(1);
    im.Lea(0xA0);im.StoreName(2);im.StoreFormat(2,0);im.StoreScale(2,1.0f);
    const std::size_t end=im.code;
    killcount::Row rows[8]{};
    const unsigned char* base=im.bytes.data();
    Check(killcount::ParseRows(base,begin,end,rows,8)==3,"three rows read");
    Check(rows[0].name==reinterpret_cast<const wchar_t*>(base+0x20) && rows[0].format==1 && rows[0].scale==1.0f,"row 0");
    Check(rows[1].name==reinterpret_cast<const wchar_t*>(base+0x60) && rows[1].format==3 && rows[1].scale==100.0f,
          "row 1: the scale from movss");
    Check(std::wcscmp(rows[2].name,L"GiantAntKillCount")==0 && rows[2].format==0,"row 2");
    Check(killcount::ParseRows(base,begin,end,rows,2)==-1,"more rows than room: refused");
    Check(killcount::ParseRows(base,begin,end-10,rows,8)==-1,"a row left without its scale: refused");
    Check(killcount::ParseRows(base,begin,end+1,rows,8)==-1,"an instruction of another kind: refused");
    Image off;   // a store outside the row's field
    const std::size_t b2=off.code;
    off.Lea(0x20);off.Op({0x48,0x89,0x85});off.D32(0x274);
    Check(killcount::ParseRows(off.bytes.data(),b2,off.code,rows,8)==-1,"a name stored off its field: refused");
}

// A node of the store's tree (MSVC std::map<std::wstring, variable>).
struct Node {
    unsigned char b[killcount::kNodeSize]{};
    std::wstring key;
    Node(const wchar_t* k,std::int32_t type) : key(k) {
        const std::uint64_t size=key.size(),capacity=size<8 ? 7 : size;
        if(size<8)std::memcpy(b+killcount::kNodeKey,key.c_str(),(size+1)*sizeof(wchar_t));
        else {const wchar_t* p=key.c_str();std::memcpy(b+killcount::kNodeKey,&p,sizeof(p));}
        std::memcpy(b+killcount::kKeySize,&size,8);
        std::memcpy(b+killcount::kKeyCapacity,&capacity,8);
        std::memcpy(b+killcount::kNodeType,&type,4);
    }
    void Link(std::size_t at,const void* to){std::memcpy(b+at,&to,sizeof(to));}
};

void Store() {
    // head <- root "GiantAntKillCount"; left "Frame" (inline key); right "edf41_HectorKillCount" (left: a float "WeaponGetRatio")
    Node head(L"",0),root(L"GiantAntKillCount",0),left(L"Frame",0),right(L"edf41_HectorKillCount",0),fl(L"WeaponGetRatio",1);
    head.b[killcount::kNodeNil]=1;
    for(Node* n:{&root,&left,&right,&fl}) {n->Link(killcount::kNodeLeft,head.b);n->Link(killcount::kNodeRight,head.b);}
    head.Link(killcount::kNodeParent,root.b);
    root.Link(killcount::kNodeLeft,left.b);root.Link(killcount::kNodeRight,right.b);
    right.Link(killcount::kNodeLeft,fl.b);
    auto any=[](const void* p,std::size_t){return p!=nullptr;};
    Check(killcount::FindInt(head.b,L"GiantAntKillCount",any)==root.b,"the root");
    Check(killcount::FindInt(head.b,L"Frame",any)==left.b,"an inline key");
    Check(killcount::FindInt(head.b,L"edf41_HectorKillCount",any)==right.b,"a 4.1 counter");
    Check(killcount::FindInt(head.b,L"WeaponGetRatio",any)==nullptr,"a float variable is not a counter");
    Check(killcount::FindInt(head.b,L"edf41_DragonKillCount",any)==nullptr,"a name the store lacks");
    Check(killcount::FindInt(head.b,L"Fram",any)==nullptr && killcount::FindInt(head.b,L"Frames",any)==nullptr,
          "a prefix and a longer name are other names");
    auto none=[](const void*,std::size_t){return false;};
    Check(killcount::FindInt(head.b,L"GiantAntKillCount",none)==nullptr,"an unreadable store");
}

void Names() {
    // every name sorts after every stock name (the save's slots: docs/kill-counter-re.md), is a *KillCount row
    for(const wchar_t* n:killcount::kEdf41Counters) {
        Check(std::wcsncmp(n,L"edf41_",6)==0,"the 4.1 prefix");
        const std::size_t len=std::wcslen(n);
        Check(len>9 && std::wcscmp(n+len-9,L"KillCount")==0,"a KillCount row (shown only when not 0)");
    }
    Check(killcount::kStockRows+static_cast<int>(sizeof(killcount::kEdf41Counters)/sizeof(*killcount::kEdf41Counters))<=
          killcount::kMostRows,"the rows fit the imm8 bounds");
}
}  // namespace

int main() {
    Rows();
    Store();
    Names();
    if(failures){std::printf("%d failure(s)\n",failures);return 1;}
    std::printf("killcount_test ok\n");
    return 0;
}
