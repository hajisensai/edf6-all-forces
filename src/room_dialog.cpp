// The game's own dialog box with the plugin's buttons: room_dialog.h. EDF.dll TimeDateStamp 0x678CCB46, RVAs; the RE is
// docs/room-native-dialog-re.md (every step below is what the soldier screen's outfit popup builder 0x8AB310 does, in
// its order; tools/room_dialog_sigs.py checks the code, tests/room_dialog_native_audit.py the data and the call shapes).
//
// A button is a 0x90-byte item (game_dialog.h): built on the stack as the game builds its own (the tree's head node
// from the game's operator new), copied into the param's vector by the game's copy constructor 0x89950 (its
// std::function copied through _Copy: ours), and the temporary destroyed by the game's item destructor 0x8E170. The
// std::function is ours: {vtable, 8-byte id}, the vtable made at load (game_dialog.h CloneFuncVtable over the stock
// lambda's, 0x1805CF0, whose capture is the same 8 bytes). Picked, it notes the id in the Handle of its dialog and
// closes the dialog as the stock lambdas do (0x7E2EB0 on the ctx's first pointer, 0x8AD5B0).
//
// Closing (2026-10-20 review of #218): the dialog hangs on the game's global UI, not on the room / HQ screen, so it
// outlives a screen that is destroyed or stops updating (a mission's load). It is closed
//   - by room_ui.cpp right in the screen's destructor hook (CloseDialog on the UI thread), and
//   - by its own frame (HUiDialogBox::OnUpdate, vtable 0x17F7260 slot 1, chained) once RequestCloseDialogs asked (a
//     mission starting: MissionSync / the preload, whatever thread): the dialog's frame runs while it is on screen.
#include "room_dialog.h"
#include "code_signatures.h"
#include "crew.h"
#include "edf/patch.h"
#include "edf/wstr.h"
#include "game_dialog.h"
#include "memory.h"
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iterator>

namespace crew::nativedialog {
namespace {
namespace gd=gamedialog;

// EDF.dll 0x678CCB46 RVAs.
constexpr std::size_t kParamCtor=0x8C320,kParamVtable=0x17621F8,kParamBaseDtor=0x8E540,kItemsDtor=0xA9460;
constexpr std::size_t kParamInit130=0x1765B30;   // the 16 bytes the builder puts at +0x130 (0x8AB5FE)
constexpr std::size_t kShow=0x119C600,kClose=0x7E2EB0,kNew=0x12D85B0,kLockWeak=0x83E4F0;
constexpr std::size_t kSystem=0x20B2960,kSystemAdjust=0x88;   // [0x20B2960] - 0x88, null when it is (0x8ABBE8)
constexpr std::size_t kLayoutName=0x1F77810,kLayoutText=0x17F7190;   // a pointer to L"APP:/UI/LYT_HUIDIALOGBOX.SGO"
constexpr std::size_t kLambdaVtable=0x1805CF0;   // the outfit popup's first item (its capture: 8 bytes after the vtable)
// HUiDialogBox's vtable (RTTI .?AVHUiDialogBox@ui@@): slot 1 its OnUpdate(this, ctx) 0x810D10, as every HUi screen's.
constexpr std::size_t kDialogVtable=0x17F7260,kDialogUpdate=0x810D10;
constexpr int kDialogSlotUpdate=1;
// The description dialog (2026-10-25, docs/feature-2026-10-25-room-menu-review.md §2): HUiDescriptionDialog
// (vtable 0x17F6F70, RTTI .?AVHUiDescriptionDialog@ui@@, layout APP:/UI/LYT_HUIDESCDIALOGBOX.SGO) is an HUiDialogBox
// with a text beside its buttons that names the focused one. Its InitParam (RTTI .?AUInitParam@HUiDescriptionDialog@ui@@,
// vtable 0x1806FD8, 0x1C0 bytes) adds at +0x170 a std::function<std::wstring(int)>: the dialog copies it (ctor 0x80AF6A)
// and, each time the focus moves, calls it with the focused button's index (its +0x110) and SetTexts the literal result
// (0x80E700..0x80E799). The game's own builder 0x8BEE40 fills it the same way (lambda 0x1806B20). Its OnUpdate (slot 1,
// 0x8088C0) runs the focus tracker, then jumps to HUiDialogBox::OnUpdate itself: chained apart from the base slot.
constexpr std::size_t kDescParamVtable=0x1806FD8,kDescParamDtor=0x8C33E0,kDescParamDelete=0x8C3620;
constexpr std::size_t kDescLayoutName=0x1F77808,kDescLayoutText=0x17F7210;   // a pointer to L"APP:/UI/LYT_HUIDESCDIALOGBOX.SGO"
constexpr std::size_t kDescDialogVtable=0x17F6F70,kDescDialogUpdate=0x8088C0;
constexpr std::size_t kParamDesc=0x170,kFuncImplAt=0x38;   // the std::function's storage, its impl pointer at +0x38

// HUiDialogBox::InitParam (the builder's rbp+0xE0): its own fields after the base the ctor makes.
constexpr std::size_t kParamSize=0x1C0;
constexpr std::size_t kParamText0=0xE8,kParamTitle=0x108,kParamFlags=0x128,kParamFocus=0x12C,kParam130=0x130,
    kParam140=0x140,kParamCursor=0x148,kParam14C=0x14C,kParamItems=0x150;
constexpr std::int32_t kListDialog=6;   // the outfit popup's flags (0x8ABB49)
constexpr std::size_t kTreeHeadSize=0x60;

using Fn1=void(__fastcall*)(void*);
using ShowFn=void*(__fastcall*)(void*,void*,const wchar_t*,void*);
using NewFn=void*(__fastcall*)(std::size_t);
using UpdateFn=std::uintptr_t(__fastcall*)(void*,void*);
struct Shared { void* object; void* control; };
using LockWeakFn=Shared*(__fastcall*)(Shared*,const Shared*);

using Signature=CodeSignature;
#include "room_dialog_signatures.inc"

bool ready=false,descReady=false;
gd::FuncVtable ourVtable{},descVtable{};
UpdateFn nextUpdate=nullptr,nextDescUpdate=nullptr;
// The id space: the low 40 bits the caller's id, the high 24 the dialog's serial (a pick finds its dialog by it).
constexpr std::uint64_t kIdMask=(1ull<<40)-1;
std::uint32_t serial=0;

// The dialogs open now (UI thread): where each one's Handle is, and its order (RequestCloseDialogs closes those
// opened up to the request).
constexpr int kMostOpen=8;
struct Open { Handle* handle=nullptr; std::uint64_t order=0; bool closing=false; };
Open open[kMostOpen];
std::atomic<std::uint64_t> opened{0};      // dialogs opened so far (an Open's order)
std::atomic<std::uint64_t> closeUpTo{0};   // RequestCloseDialogs: those with order <= this close

Open* Find(std::uint32_t s) noexcept {
    if(!s)return nullptr;
    for(auto& o:open)if(o.handle && o.handle->serial==s)return &o;
    return nullptr;
}
void Register(Handle* h) noexcept {
    Open* slot=nullptr;
    for(auto& o:open)if(!o.handle){slot=&o;break;}
    if(!slot) {   // more than kMostOpen alive: the oldest one's picks are not routed any more (logged)
        slot=&open[0];
        for(auto& o:open)if(o.order<slot->order)slot=&o;
        Log("ROOMDIALOG more than %d dialogs held: the oldest (serial %u) no longer takes picks",kMostOpen,slot->handle->serial);
    }
    *slot=Open{h,opened.fetch_add(1)+1,false};
}
void Unregister(const Handle& h) noexcept {
    for(auto& o:open)if(o.handle==&h)o=Open{};
}

// The descriptions of the dialogs shown now, by serial (filled before the game shows the dialog: it asks for the first
// focused button's text while it opens), and the index of the button the focus is on (the game's own report).
constexpr int kDescsMost=kMostOpen,kDescButtonsMost=16;
struct Descs { std::uint32_t serial=0; int count=0; const wchar_t* text[kDescButtonsMost]{}; int focused=-1; };
Descs descs[kDescsMost];
Descs* DescsOf(std::uint32_t s) noexcept {
    if(!s)return nullptr;
    for(auto& d:descs)if(d.serial==s)return &d;
    return nullptr;
}
Descs* NewDescs(std::uint32_t s) noexcept {
    Descs* slot=nullptr;
    for(auto& d:descs)if(!d.serial){slot=&d;break;}
    if(!slot)slot=&descs[s%kDescsMost];   // more than kDescsMost alive: the oldest serials' texts are reused
    *slot=Descs{};slot->serial=s;
    return slot;
}
void DropDescs(std::uint32_t s) noexcept { if(Descs* d=DescsOf(s))*d=Descs{}; }

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
    if(Open* o=Find(static_cast<std::uint32_t>(id>>40)); o && !o->closing){o->handle->picked=id&kIdMask;o->handle->hasPicked=true;}
    reinterpret_cast<Fn1>(image+kClose)(arg ? *arg : nullptr);   // as the stock lambdas (0x8AD5AD: rcx = [rdx])
    return 0;
}

// The description function's implementation: {&descVtable[1], serial}; the game calls it with the focused button's index
// and takes the text it returns (a std::wstring built here in the game's own representation: 0x3D450).
struct DescImpl { void* const* vtable; std::uint64_t serial; };
static_assert(sizeof(DescImpl)==16,"the same 16 bytes as the stock lambda the vtable is cloned from");
void* __fastcall DescCopy(DescImpl* self,void* where) {
    auto* made=static_cast<DescImpl*>(where);
    made->vtable=&descVtable[1];made->serial=self->serial;
    return made;
}
void* __fastcall DescMove(DescImpl* self,void* where) { return DescCopy(self,where); }
void* __fastcall DescCall(DescImpl* self,void* out,const int* index) {
    auto* s=static_cast<unsigned char*>(out);
    edf::wstr::Init(s);
    const wchar_t* text=L"";
    if(Descs* d=DescsOf(static_cast<std::uint32_t>(self->serial)); d && index && *index>=0 && *index<d->count) {
        d->focused=*index;
        if(d->text[*index])text=d->text[*index];
    }
    edf::wstr::Assign(image,s,text);
    return out;
}

// One item on the stack as 0x8AB669..0x8AB6F0 builds it; destroyed by the game's 0x8E170.
bool MakeItem(unsigned char* item,const wchar_t* text,std::uint64_t id) noexcept {
    std::memset(item,0,gd::kItemSize);
    edf::wstr::Init(item+gd::kItemText);
    edf::wstr::Assign(image,item+gd::kItemText,text);
    Impl local{&ourVtable[1],id};
    gd::SetField<void*>(item,gd::kItemImpl,FuncCopy(&local,item+gd::kItemFunc));
    gd::SetField<std::uint16_t>(item,gd::kItemEnabled,1);
    void* head=reinterpret_cast<NewFn>(image+kNew)(kTreeHeadSize);
    if(!head){gd::SetField<void*>(item,gd::kItemImpl,nullptr);edf::wstr::Destroy(image,item+gd::kItemText);return false;}
    std::memset(head,0,kTreeHeadSize);
    gd::SetField<void*>(head,0,head);gd::SetField<void*>(head,8,head);gd::SetField<void*>(head,0x10,head);
    gd::SetField<std::uint16_t>(head,0x18,0x101);
    gd::SetField<void*>(item,gd::kItemTree,head);gd::SetField<std::uint64_t>(item,gd::kItemTreeSize,0);
    return true;
}

bool Check() noexcept {
    if(!CheckSignatures(kSignatures,"ROOMDIALOG"))return false;
    // The layout name is a pointer the loader relocates: checked where it points, not by its bytes.
    if(gd::Field<const void*>(image,kLayoutName)!=image+kLayoutText){Log("ROOMDIALOG REFUSED: EDF+0x%zX does not point at the dialog layout's name",kLayoutName);return false;}
    return true;
}
// The description dialog's pieces (its own signatures are in the same table): its layout name's pointer and its
// InitParam's vtable (slot 0 its scalar deleting destructor, which frees 0x1C0 bytes and calls kDescParamDtor).
bool CheckDescription() noexcept {
    if(gd::Field<const void*>(image,kDescLayoutName)!=image+kDescLayoutText) {
        Log("ROOMDIALOG description dialog off: EDF+0x%zX does not point at its layout's name",kDescLayoutName);return false;
    }
    if(gd::Field<const void*>(image,kDescParamVtable)!=image+kDescParamDelete) {
        Log("ROOMDIALOG description dialog off: its InitParam vtable EDF+0x%zX is not as expected",kDescParamVtable);return false;
    }
    return true;
}

// The weak reference the game handed out (the dialog's control block +0xC: weaks) released.
void ReleaseWeak(void* control) noexcept {
    if(!control)return;
    __try {
        auto* weaks=reinterpret_cast<volatile long*>(static_cast<unsigned char*>(control)+0xC);
        if(InterlockedDecrement(weaks)==0)reinterpret_cast<void(__fastcall*)(void*)>((*static_cast<void***>(control))[1])(control);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// The window closed now when the game still holds it (locked through the weak reference, as the room's own list does).
void CloseNow(void* object,void* control) noexcept {
    Shared weak{object,control},strong{};
    __try {
        if(*reinterpret_cast<volatile long*>(static_cast<unsigned char*>(control)+8)<=0)return;
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

// Every HUiDialogBox's frame (the game's own popups too: passed through). One of ours a close was asked for since it
// opened: closed after its frame, as a picked stock item closes it inside the frame. The Handle stays the caller's: it
// sees the dialog end (DialogAlive) and lets it go.
void CloseAsked(void* self) noexcept;
std::uintptr_t __fastcall DialogUpdateHook(void* self,void* ctx) {
    const std::uintptr_t result=nextUpdate(self,ctx);
    CloseAsked(self);
    return result;
}
// The description dialog's frame (its own slot: it jumps to HUiDialogBox::OnUpdate directly, past the base slot's hook).
std::uintptr_t __fastcall DescDialogUpdateHook(void* self,void* ctx) {
    const std::uintptr_t result=nextDescUpdate(self,ctx);
    CloseAsked(self);
    return result;
}
void CloseAsked(void* self) noexcept {
    const std::uint64_t upTo=closeUpTo.load(std::memory_order_acquire);
    if(!upTo)return;
    for(auto& o:open) {
        if(!o.handle || o.closing || o.order>upTo || o.handle->object!=self)continue;
        o.closing=true;   // a pick from it is not taken any more
        __try { reinterpret_cast<Fn1>(image+kClose)(self); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        Log("ROOMDIALOG dialog %u closed by its own frame (a mission is starting)",o.handle->serial);
    }
}
// Chains one OnUpdate slot; the next one, or nullptr (logged).
UpdateFn ChainUpdate(std::size_t vtable,std::size_t stock,void* hook,const char* what) noexcept {
    auto slot=reinterpret_cast<void**>(image+vtable)+kDialogSlotUpdate;
    void* const current=Readable(slot,8) ? *slot : nullptr;
    if(current && current!=image+stock)Log("ROOMDIALOG %s::OnUpdate is %p, not the stock one: chained onto it",what,current);
    void* next=nullptr;
    if(current && edf::ChainVtableSlot(slot,hook,&next))return reinterpret_cast<UpdateFn>(next);
    Log("ROOMDIALOG %s::OnUpdate could not be chained: a dialog open when a mission starts waits for the screen's next frame",what);
    return nullptr;
}
}  // namespace

bool InstallNativeDialog() noexcept {
    if(ready)return true;
    if(!Check())return false;
    gd::CloneFuncVtable(ourVtable,reinterpret_cast<void* const*>(image+kLambdaVtable),reinterpret_cast<void*>(&FuncCopy),
                        reinterpret_cast<void*>(&FuncMove),reinterpret_cast<void*>(&FuncCall));
    // The dialog's own frame closes ours when a mission starts; without it (another hook holds the slot oddly, logged)
    // only the screens' frames and destructors close them, as before.
    nextUpdate=ChainUpdate(kDialogVtable,kDialogUpdate,reinterpret_cast<void*>(&DialogUpdateHook),"HUiDialogBox");
    // The description dialog: every page then names its focused button beside the list. Only with its frame chained
    // too (else a mission start could not close it): without, the plain dialog box as before.
    if(CheckDescription()) {
        gd::CloneFuncVtable(descVtable,reinterpret_cast<void* const*>(image+kLambdaVtable),reinterpret_cast<void*>(&DescCopy),
                            reinterpret_cast<void*>(&DescMove),reinterpret_cast<void*>(&DescCall));
        nextDescUpdate=ChainUpdate(kDescDialogVtable,kDescDialogUpdate,reinterpret_cast<void*>(&DescDialogUpdateHook),
                                   "HUiDescriptionDialog");
        descReady=nextDescUpdate!=nullptr;
    }
    ready=true;
    Log("ROOMDIALOG the game's dialog box ready (HUiDialogBox; its own frame closes ours: %d; the description dialog: %d)",
        nextUpdate!=nullptr,descReady);
    return true;
}
bool NativeDialogReady() noexcept { return ready; }
bool DescriptionDialogReady() noexcept { return descReady; }
int FocusedButton(const Handle& h) noexcept {
    const Descs* d=DescsOf(h.serial);
    return d && h.control ? d->focused : -1;
}

bool OpenDialog(const wchar_t* title,const Button* buttons,int count,int cursor,Handle* out,const wchar_t* layout) noexcept {
    if(out){Unregister(*out);DropDescs(out->serial);*out=Handle{};}
    if(!ready || !buttons || count<=0 || !out)return false;
    void* system=gd::Field<void*>(image,kSystem);
    if(!system)return false;
    system=static_cast<unsigned char*>(system)-kSystemAdjust;
    serial=(serial+1)&0xFFFFFF;
    if(!serial)serial=1;
    const gd::ItemOps ops=gd::GameOps(image);
    // Descriptions given and the description dialog ready: HUiDescriptionDialog with its InitParam, the texts kept by
    // serial before it opens (it asks for the first focused button's at once). Else the plain dialog box, and only the
    // stock layout (the menu's own layout is a description dialog's).
    bool desc=false;
    for(int i=0;descReady && i<count;++i)desc|=buttons[i].desc!=nullptr;
    if(count>kDescButtonsMost)desc=false;
    if(desc) {
        Descs* d=NewDescs(serial);
        d->count=count;
        for(int i=0;i<count;++i)d->text[i]=buttons[i].desc;
    }
    const wchar_t* name=desc ? (layout && *layout ? layout : gd::Field<const wchar_t*>(image,kDescLayoutName))
                             : gd::Field<const wchar_t*>(image,kLayoutName);
    alignas(16) unsigned char param[kParamSize];
    alignas(16) unsigned char item[gd::kItemSize];
    Shared weak{};
    bool shown=false,faulted=false;
    __try {
        std::memset(param,0,sizeof(param));
        reinterpret_cast<Fn1>(image+kParamCtor)(param);
        gd::SetField<const void*>(param,0,image+(desc ? kDescParamVtable : kParamVtable));
        edf::wstr::Init(param+kParamText0);
        edf::wstr::Init(param+kParamTitle);
        gd::SetField<std::uint64_t>(param,kParamFlags,0);
        std::memcpy(param+kParam130,image+kParamInit130,16);
        gd::SetField<std::uint64_t>(param,kParam140,0);
        gd::SetField<std::int32_t>(param,kParamCursor,-1);
        gd::SetField<std::uint8_t>(param,kParam14C,0);
        int made=0;
        for(int i=0;i<count;++i) {
            if(!MakeItem(item,buttons[i].text,(buttons[i].id&kIdMask)|(static_cast<std::uint64_t>(serial)<<40)))continue;
            gd::PushItem(ops,param+kParamItems,item);
            ops.destroy(item);
            ++made;
        }
        gd::SetField<std::int32_t>(param,kParamFlags,kListDialog);
        gd::SetField<std::int32_t>(param,kParamCursor,cursor>=0 && cursor<made ? cursor : 0);
        gd::SetField<std::int32_t>(param,kParamFocus,1);
        if(title)edf::wstr::Assign(image,param+kParamTitle,title);
        if(desc) {   // the std::function in place (16 bytes fit its storage), its impl pointer at +0x38, as 0x8BF900 leaves it
            DescImpl local{&descVtable[1],serial};
            gd::SetField<void*>(param,kParamDesc+kFuncImplAt,DescCopy(&local,param+kParamDesc));
        }
        if(made>0) {
            // The layout by name, as the game passes its own (0x119C600 copies the name: 0x119C690..0x119C6A2).
            reinterpret_cast<ShowFn>(image+kShow)(system,&weak,name,param);
            shown=weak.control!=nullptr;
        }
        if(desc)reinterpret_cast<Fn1>(image+kDescParamDtor)(param);   // the function, the items, the texts, the base
        else {
            reinterpret_cast<Fn1>(image+kItemsDtor)(param+kParamItems);
            edf::wstr::Destroy(image,param+kParamTitle);
            edf::wstr::Destroy(image,param+kParamText0);
            reinterpret_cast<Fn1>(image+kParamBaseDtor)(param);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        faulted=true;
    }
    if(faulted) {
        // A fault after the game showed it (tearing the param down): the dialog is on screen and we hold its weak
        // reference. Closed and released, never left open with no one to drive or close it.
        if(weak.control){CloseNow(weak.object,weak.control);ReleaseWeak(weak.control);}
        Log("ROOMDIALOG opening the dialog faulted: no dialog%s",weak.control ? " (the one shown closed)" : "");
        DropDescs(serial);
        return false;
    }
    if(!shown){Log("ROOMDIALOG the game opened no dialog (%d buttons, layout %ls)",count,name ? name : L"?");DropDescs(serial);return false;}
    *out=Handle{weak.object,weak.control,serial,0,false};
    Register(out);
    return true;
}

bool DialogAlive(const Handle& h) noexcept {
    if(!h.control)return false;
    __try { return *reinterpret_cast<volatile long*>(static_cast<unsigned char*>(h.control)+8)>0; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool TakePicked(Handle& h,std::uint64_t* id) noexcept {
    if(!h.control || !h.hasPicked)return false;
    h.hasPicked=false;
    if(id)*id=h.picked;
    return true;
}

void ForgetDialog(Handle& h) noexcept {
    Unregister(h);
    DropDescs(h.serial);
    void* const control=h.control;
    h=Handle{};
    ReleaseWeak(control);
}

void CloseDialog(Handle& h) noexcept {
    Unregister(h);   // a pick from it is no longer taken
    if(DialogAlive(h))CloseNow(h.object,h.control);
    ForgetDialog(h);
}

void RequestCloseDialogs() noexcept {
    closeUpTo.store(opened.load(std::memory_order_acquire),std::memory_order_release);
}

}  // namespace crew::nativedialog
