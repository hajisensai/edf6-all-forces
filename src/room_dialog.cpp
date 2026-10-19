// The game's own dialog box with the plugin's buttons: room_dialog.h. EDF.dll TimeDateStamp 0x678CCB46, RVAs; the RE is
// docs/room-native-dialog-re.md (every step below is what the soldier screen's outfit popup builder 0x8AB310 does, in
// its order; tools/room_dialog_sigs.py checks the code, tests/room_dialog_native_audit.py the data and the call shapes).
//
// A button is a 0x90-byte item {std::wstring text, std::function<void(ctx*)> picked, +0x60 enabled 1, +0x80 an empty
// tree}: built on the stack as the game builds its own (the tree's head node from the game's operator new), copied into
// the param's vector by the game's copy constructor 0x89950 (its std::function copied through _Copy: ours), and the
// temporary destroyed by the game's item destructor 0x8E170. The std::function is ours: {vtable, 8-byte id}, the vtable
// made at load (MSVC _Func_impl: _Copy / _Move / _Do_call ours; _Target_type / _Delete_this / _Get the stock lambda's,
// 0x1805CF0, whose capture is the same 8 bytes). Picked, it notes the id and closes the dialog as the stock lambdas do
// (0x7E2EB0 on the ctx's first pointer, 0x8AD5B0).
#include "room_dialog.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
#include <cwchar>
#include <iterator>

namespace crew::nativedialog {
namespace {

// EDF.dll 0x678CCB46 RVAs.
constexpr std::size_t kParamCtor=0x8C320,kParamVtable=0x17621F8,kParamBaseDtor=0x8E540,kItemsDtor=0xA9460;
constexpr std::size_t kParamInit130=0x1765B30;   // the 16 bytes the builder puts at +0x130 (0x8AB5FE)
constexpr std::size_t kStringDtor=0x3D3C0,kAssign=0x3D450,kItemCopy=0x89950,kReserve=0xB91D0,kItemDtor=0x8E170;
constexpr std::size_t kShow=0x119C600,kClose=0x7E2EB0,kNew=0x12D85B0,kLockWeak=0x83E4F0;
constexpr std::size_t kSystem=0x20B2960,kSystemAdjust=0x88;   // [0x20B2960] - 0x88, null when it is (0x8ABBE8)
constexpr std::size_t kLayoutName=0x1F77810,kLayoutText=0x17F7190;   // a pointer to L"APP:/UI/LYT_HUIDIALOGBOX.SGO"
constexpr std::size_t kLambdaVtable=0x1805CF0;   // the outfit popup's first item (its capture: 8 bytes after the vtable)

// HUiDialogBox::InitParam (the builder's rbp+0xE0): its own fields after the base the ctor makes.
constexpr std::size_t kParamSize=0x1C0;
constexpr std::size_t kParamText0=0xE8,kParamTitle=0x108,kParamFlags=0x128,kParamFocus=0x12C,kParam130=0x130,
    kParam140=0x140,kParamCursor=0x148,kParam14C=0x14C,kParamItems=0x150;
constexpr std::size_t kVecData=0x8,kVecCapacity=0x10,kVecSize=0x18;
constexpr std::int32_t kListDialog=6;   // the outfit popup's flags (0x8ABB49)
// An item (0x8AB669..0x8AB6F0).
constexpr std::size_t kItemSize=0x90,kItemFunc=0x20,kItemImpl=0x58,kItemEnabled=0x60,kItemTree=0x80,kItemTreeSize=0x88;
constexpr std::size_t kTreeHeadSize=0x60;
// MSVC std::wstring: buffer / pointer, size +0x10, capacity +0x18 (7: in place).
constexpr std::size_t kStrSize=0x10,kStrCapacity=0x18;
// std::_Func_impl's vtable: _Copy, _Move, _Do_call, _Target_type, _Delete_this, _Get.
constexpr int kFuncSlots=6,kSlotCopy=0,kSlotMove=1,kSlotCall=2;

using Fn1=void(__fastcall*)(void*);
using AssignFn=void*(__fastcall*)(void*,const wchar_t*,std::size_t);
using ItemCopyFn=void*(__fastcall*)(void*,void*,const void*);
using ReserveFn=void(__fastcall*)(void*,std::size_t);
using ShowFn=void*(__fastcall*)(void*,void*,const wchar_t*,void*);
using NewFn=void*(__fastcall*)(std::size_t);
struct Shared { void* object; void* control; };
using LockWeakFn=Shared*(__fastcall*)(Shared*,const Shared*);

struct Signature { std::size_t rva; const unsigned char* bytes; std::size_t size; };
#include "room_dialog_signatures.inc"

bool ready=false;
void* ourVtable[1+kFuncSlots]{};   // [0] the stock lambda vtable's RTTI locator, then the six slots
// The id space: the low 40 bits the caller's id, the high 24 the dialog's serial (a pick of a dialog no longer ours is
// not taken).
constexpr std::uint64_t kIdMask=(1ull<<40)-1;
std::uint32_t serial=0;
std::uint64_t picked=0;
bool hasPicked=false;

template<class T> T Get(const void* p,std::size_t at) noexcept { T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+at,sizeof(T));return v; }
template<class T> void Set(void* p,std::size_t at,T v) noexcept { std::memcpy(static_cast<unsigned char*>(p)+at,&v,sizeof(T)); }

// --- Our std::function implementation: {&ourVtable[1], id}
struct Impl { void* const* vtable; std::uint64_t id; };
static_assert(sizeof(Impl)==16,"the stock lambda's size: its _Delete_this / _Get treat ours alike");
void* __fastcall FuncCopy(Impl* self,void* where) {
    auto* made=static_cast<Impl*>(where);   // the std::function's own storage (16 bytes fit it: no allocation)
    made->vtable=&ourVtable[1];made->id=self->id;
    return made;
}
void* __fastcall FuncMove(Impl* self,void* where) { return FuncCopy(self,where); }
std::uintptr_t __fastcall FuncCall(Impl* self,void** arg) {
    const std::uint64_t id=self->id;
    if(static_cast<std::uint32_t>(id>>40)==(serial&0xFFFFFF)){picked=id&kIdMask;hasPicked=true;}
    reinterpret_cast<Fn1>(image+kClose)(arg ? *arg : nullptr);   // as the stock lambdas (0x8AD5AD: rcx = [rdx])
    return 0;
}

void InitString(unsigned char* s) noexcept {
    std::memset(s,0,0x20);
    Set<std::uint64_t>(s,kStrCapacity,7);
}
// One item on the stack as 0x8AB669..0x8AB6F0 builds it; destroyed by DestroyItem (the game's 0x8E170).
bool MakeItem(unsigned char* item,const wchar_t* text,std::uint64_t id) noexcept {
    std::memset(item,0,kItemSize);
    InitString(item);
    reinterpret_cast<AssignFn>(image+kAssign)(item,text ? text : L"",text ? std::wcslen(text) : 0);
    Impl local{&ourVtable[1],id};
    Set<void*>(item,kItemImpl,FuncCopy(&local,item+kItemFunc));
    Set<std::uint16_t>(item,kItemEnabled,1);
    void* head=reinterpret_cast<NewFn>(image+kNew)(kTreeHeadSize);
    if(!head){Set<void*>(item,kItemImpl,nullptr);reinterpret_cast<Fn1>(image+kStringDtor)(item);return false;}
    std::memset(head,0,kTreeHeadSize);
    Set<void*>(head,0,head);Set<void*>(head,8,head);Set<void*>(head,0x10,head);Set<std::uint16_t>(head,0x18,0x101);
    Set<void*>(item,kItemTree,head);Set<std::uint64_t>(item,kItemTreeSize,0);
    return true;
}
// The items vector's push_back as the game's loop (0x8ABAE0): grown to capacity*2+2 when full, the size first, then the
// item copy-constructed in place.
void PushItem(unsigned char* vec,const unsigned char* item) noexcept {
    std::size_t size=Get<std::size_t>(vec,kVecSize);
    if(size==Get<std::size_t>(vec,kVecCapacity))reinterpret_cast<ReserveFn>(image+kReserve)(vec,size*2+2);
    size=Get<std::size_t>(vec,kVecSize);
    unsigned char* data=Get<unsigned char*>(vec,kVecData);
    Set<std::size_t>(vec,kVecSize,size+1);
    reinterpret_cast<ItemCopyFn>(image+kItemCopy)(vec,data+size*kItemSize,item);
}

bool Check() noexcept {
    for(const auto& s:kSignatures) {
        bool ok=false;
        __try { ok=Matches(s.rva,s.bytes,s.size); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if(!ok){Log("ROOMDIALOG REFUSED: code at EDF+0x%zX is not the expected (%zu bytes)",s.rva,s.size);return false;}
    }
    // The layout name is a pointer the loader relocates: checked where it points, not by its bytes.
    if(Get<const void*>(image,kLayoutName)!=image+kLayoutText){Log("ROOMDIALOG REFUSED: EDF+0x%zX does not point at the dialog layout's name",kLayoutName);return false;}
    return true;
}
}  // namespace

bool InstallNativeDialog() noexcept {
    if(ready)return true;
    if(!Check())return false;
    auto stock=reinterpret_cast<void* const*>(image+kLambdaVtable);
    ourVtable[0]=stock[-1];
    for(int i=0;i<kFuncSlots;++i)ourVtable[1+i]=stock[i];
    ourVtable[1+kSlotCopy]=reinterpret_cast<void*>(&FuncCopy);
    ourVtable[1+kSlotMove]=reinterpret_cast<void*>(&FuncMove);
    ourVtable[1+kSlotCall]=reinterpret_cast<void*>(&FuncCall);
    ready=true;
    Log("ROOMDIALOG the game's dialog box ready (HUiDialogBox, LYT_HUIDIALOGBOX)");
    return true;
}
bool NativeDialogReady() noexcept { return ready; }

bool OpenDialog(const wchar_t* title,const Button* buttons,int count,int cursor,Handle* out) noexcept {
    if(out)*out=Handle{};
    if(!ready || !buttons || count<=0 || !out)return false;
    void* system=Get<void*>(image,kSystem);
    if(!system)return false;
    system=static_cast<unsigned char*>(system)-kSystemAdjust;
    serial=(serial+1)&0xFFFFFF;
    if(!serial)serial=1;
    hasPicked=false;
    alignas(16) unsigned char param[kParamSize];
    alignas(16) unsigned char item[kItemSize];
    Shared weak{};
    bool shown=false;
    __try {
        std::memset(param,0,sizeof(param));
        reinterpret_cast<Fn1>(image+kParamCtor)(param);
        Set<const void*>(param,0,image+kParamVtable);
        InitString(param+kParamText0);
        InitString(param+kParamTitle);
        Set<std::uint64_t>(param,kParamFlags,0);
        std::memcpy(param+kParam130,image+kParamInit130,16);
        Set<std::uint64_t>(param,kParam140,0);
        Set<std::int32_t>(param,kParamCursor,-1);
        Set<std::uint8_t>(param,kParam14C,0);
        int made=0;
        for(int i=0;i<count;++i) {
            if(!MakeItem(item,buttons[i].text,(buttons[i].id&kIdMask)|(static_cast<std::uint64_t>(serial)<<40)))continue;
            PushItem(param+kParamItems,item);
            reinterpret_cast<Fn1>(image+kItemDtor)(item);
            ++made;
        }
        Set<std::int32_t>(param,kParamFlags,kListDialog);
        Set<std::int32_t>(param,kParamCursor,cursor>=0 && cursor<made ? cursor : 0);
        Set<std::int32_t>(param,kParamFocus,1);
        if(title)reinterpret_cast<AssignFn>(image+kAssign)(param+kParamTitle,title,std::wcslen(title));
        if(made>0) {
            reinterpret_cast<ShowFn>(image+kShow)(system,&weak,Get<const wchar_t*>(image,kLayoutName),param);
            shown=weak.control!=nullptr;
        }
        reinterpret_cast<Fn1>(image+kItemsDtor)(param+kParamItems);
        reinterpret_cast<Fn1>(image+kStringDtor)(param+kParamTitle);
        reinterpret_cast<Fn1>(image+kStringDtor)(param+kParamText0);
        reinterpret_cast<Fn1>(image+kParamBaseDtor)(param);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("ROOMDIALOG opening the dialog faulted: no dialog");
        return false;
    }
    if(!shown){Log("ROOMDIALOG the game opened no dialog (%d buttons)",count);return false;}
    *out=Handle{weak.object,weak.control,serial};
    return true;
}

bool DialogAlive(const Handle& h) noexcept {
    if(!h.control)return false;
    __try { return *reinterpret_cast<volatile long*>(static_cast<unsigned char*>(h.control)+8)>0; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool TakePicked(const Handle& h,std::uint64_t* id) noexcept {
    if(!h.control || h.serial!=serial || !hasPicked)return false;
    hasPicked=false;
    if(id)*id=picked;
    return true;
}

void ForgetDialog(Handle& h) noexcept {
    void* const control=h.control;
    h=Handle{};
    if(!control)return;
    __try {
        auto* weaks=reinterpret_cast<volatile long*>(static_cast<unsigned char*>(control)+0xC);
        if(InterlockedDecrement(weaks)==0)reinterpret_cast<void(__fastcall*)(void*)>((*static_cast<void***>(control))[1])(control);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void CloseDialog(Handle& h) noexcept {
    if(h.control && h.serial==serial)serial=(serial+1)&0xFFFFFF;   // a pick from it is no longer taken
    if(DialogAlive(h)) {
        Shared weak{h.object,h.control},strong{};
        __try {
            reinterpret_cast<LockWeakFn>(image+kLockWeak)(&strong,&weak);
            if(strong.object)reinterpret_cast<Fn1>(image+kClose)(strong.object);
            if(strong.control) {
                auto* c=static_cast<unsigned char*>(strong.control);
                if(InterlockedDecrement(reinterpret_cast<volatile long*>(c+8))==0) {
                    reinterpret_cast<void(__fastcall*)(void*)>((*reinterpret_cast<void***>(c))[0])(c);
                    if(InterlockedDecrement(reinterpret_cast<volatile long*>(c+0xC))==0)
                        reinterpret_cast<void(__fastcall*)(void*)>((*reinterpret_cast<void***>(c))[1])(c);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    ForgetDialog(h);
}

}  // namespace crew::nativedialog
