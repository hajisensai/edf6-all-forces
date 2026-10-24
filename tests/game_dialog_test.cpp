// What the room menu's dialogs (src/room_dialog.cpp) and the HQ outfit popup's EDF4.1 item (src/outfit41.cpp) share,
// offline (src/game_dialog.h, common/edf/wstr.h): the items vector grown as the game's loop grows it (reserve
// capacity*2+2 when full, the size first, then the copy in place), a copy of an item already in the vector taken from
// where it is after the growth, the vector cut with the game's destructor on each item past the cut, our std::function
// vtable cloned from a stock one (its RTTI locator and its three stock slots kept), and the game's std::wstring layout.
// The game's functions are fakes here with the game's contracts.
// cmake --build build --target game_dialog_test && build\game_dialog_test.exe
#include "game_dialog.h"
#include "edf/wstr.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
namespace gd=crew::gamedialog;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

// A fake items vector: {?, data, capacity, size} with 0x90-byte items, an item's first 8 bytes its tag.
struct FakeVector { unsigned char head[0x20]{}; };
std::vector<std::vector<unsigned char>> storage;   // every allocation the fake reserve made (kept: old data stays readable)
int reserves=0,copies=0,destroyed=0;
std::size_t lastReserve=0;
void __fastcall FakeReserve(void* vec,std::size_t capacity) {
    ++reserves;lastReserve=capacity;
    auto* v=static_cast<unsigned char*>(vec);
    const std::size_t size=gd::Count(v);
    storage.emplace_back(capacity*gd::kItemSize,static_cast<unsigned char>(0xCD));
    unsigned char* data=storage.back().data();
    if(size)std::memcpy(data,gd::Field<unsigned char*>(v,gd::kVecData),size*gd::kItemSize);
    // the old block poisoned: a copy from it after the growth would show
    if(size)std::memset(gd::Field<unsigned char*>(v,gd::kVecData),0xEE,size*gd::kItemSize);
    gd::SetField<unsigned char*>(v,gd::kVecData,data);
    gd::SetField<std::size_t>(v,gd::kVecCapacity,capacity);
}
void* __fastcall FakeCopy(void* vec,void* dst,const void* src) {
    ++copies;
    // the game's copy runs after the size was raised (0x8ABAE0): the slot is inside the vector
    auto* v=static_cast<unsigned char*>(vec);
    const auto* d=static_cast<unsigned char*>(dst);
    const unsigned char* base=gd::Field<unsigned char*>(v,gd::kVecData);
    Expect(d>=base && d<base+gd::Count(v)*gd::kItemSize,"the item is copied into a slot the size already counts");
    std::memcpy(dst,src,gd::kItemSize);
    return dst;
}
void __fastcall FakeDestroy(void* item) { ++destroyed;std::memset(item,0xDD,gd::kItemSize); }
const gd::ItemOps kOps{&FakeReserve,&FakeCopy,&FakeDestroy};

std::uint64_t Tag(const unsigned char* item) { std::uint64_t t;std::memcpy(&t,item,8);return t; }
void MakeTagged(unsigned char* item,std::uint64_t tag) { std::memset(item,0,gd::kItemSize);std::memcpy(item,&tag,8); }

void Growth() {
    Expect(gd::Grown(0)==2 && gd::Grown(2)==6 && gd::Grown(5)==12,"reserve(capacity * 2 + 2), as 0x8ABAF3");
    FakeVector vec;
    unsigned char item[gd::kItemSize];
    for(std::uint64_t i=0;i<9;++i) {
        MakeTagged(item,100+i);
        unsigned char* made=gd::PushItem(kOps,vec.head,item);
        Expect(Tag(made)==100+i,"the pushed item is the copy");
    }
    Expect(gd::Count(vec.head)==9,"nine items");
    Expect(reserves==3 && lastReserve==14,"grown 0 -> 2 -> 6 -> 14, only when full");
    bool order=true;
    for(std::size_t i=0;i<9;++i)order=order && Tag(gd::ItemAt(vec.head,i))==100+i;
    Expect(order,"every item kept in order across the growths");
    // a copy of item 0 while full: taken from its place after the growth (the old block is poisoned by the fake)
    while(gd::Count(vec.head)<gd::Field<std::size_t>(vec.head,gd::kVecCapacity)){MakeTagged(item,7);gd::PushItem(kOps,vec.head,item);}
    const int before=reserves;
    unsigned char* copy=gd::PushCopyOf(kOps,vec.head,0);
    Expect(reserves==before+1,"a full vector grows before the copy");
    Expect(Tag(copy)==100,"the copy of item 0 is read where item 0 is after the growth, not from the freed block");
    // cut to one: every item past it destroyed by the game's destructor, the size set
    const std::size_t had=gd::Count(vec.head);
    destroyed=0;
    gd::Truncate(kOps,vec.head,1);
    Expect(gd::Count(vec.head)==1 && destroyed==static_cast<int>(had-1),"cut to one: the others destroyed in place");
    Expect(Tag(gd::ItemAt(vec.head,0))==100,"the kept item untouched");
    destroyed=0;
    gd::Truncate(kOps,vec.head,5);
    Expect(gd::Count(vec.head)==1 && destroyed==0,"a cut past the size changes nothing");
}

int a,b,c,d,e,f,g,rtti;
void FuncVtable() {
    void* stock[1+gd::kFuncSlots]={&rtti,&a,&b,&c,&d,&e,&f};
    gd::FuncVtable ours{};
    gd::CloneFuncVtable(ours,stock+1,&g,&g,&g);
    Expect(ours[0]==&rtti,"the RTTI locator ([-1]) is the stock vtable's (dynamic_cast / typeid still work)");
    Expect(ours[1+gd::kSlotCopy]==&g && ours[1+gd::kSlotMove]==&g && ours[1+gd::kSlotCall]==&g,"_Copy / _Move / _Do_call ours");
    Expect(ours[4]==&d && ours[5]==&e && ours[6]==&f,"_Target_type / _Delete_this / _Get the stock lambda's");
    Expect(gd::kItemSize==0x90 && gd::kItemImpl==0x58 && gd::kItemFunc==0x20 && gd::kItemTree==0x80,"an item's layout (0x8AB669..0x8AB6F0)");
    Expect(gd::kVecData==0x8 && gd::kVecCapacity==0x10 && gd::kVecSize==0x18,"the vector's layout (0x8ABAE0)");
    Expect(gd::kReserveRva==0xB91D0 && gd::kItemCopyRva==0x89950 && gd::kItemDestroyRva==0x8E170,"the game's item functions");
}

void Wstr() {
    unsigned char s[edf::wstr::kBytes];
    std::memset(s,0xAB,sizeof(s));
    edf::wstr::Init(s);
    Expect(edf::wstr::Size(s)==0 && edf::wstr::Capacity(s)==7,"empty: size 0, capacity 7 (in place)");
    Expect(edf::wstr::Data(s)==reinterpret_cast<const wchar_t*>(s) && edf::wstr::Data(s)[0]==0,"in place: the characters at +0");
    static const wchar_t heap[]=L"a longer text than seven";
    const wchar_t* p=heap;
    std::memcpy(s,&p,sizeof(p));
    const std::uint64_t size=24,cap=31;
    std::memcpy(s+edf::wstr::kSize,&size,8);std::memcpy(s+edf::wstr::kCapacity,&cap,8);
    Expect(edf::wstr::Data(s)==heap && edf::wstr::Size(s)==24,"on the heap: the pointer at +0");
    Expect(edf::wstr::kAssignRva==0x3D450 && edf::wstr::kDestroyRva==0x3D3C0,"std::wstring::assign 0x3D450, ~wstring 0x3D3C0");
}
}  // namespace

int main() {
    Growth();
    FuncVtable();
    Wstr();
    std::printf("game_dialog_test: %d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
