// Real room_ui.cpp native button path; the native ABI is replaced by executable RVA trampolines.
// No game, layouts, DLLs or input devices are loaded. Link with /Gy and /OPT:REF.
#include "../src/room_ui.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace crew {
unsigned char* image=nullptr;
Config nativeEntryConfig{};
const Config& Cfg() noexcept { return nativeEntryConfig; }
void Log(const char*,...) noexcept {}
volatile bool nativeEntryUnexpected=true;
int nativePreviewHides=0,nativeSyncLeaves=0;
__declspec(noinline) void UnexpectedRoomDependency() noexcept {
    std::puts("FAIL: unused room dependency was called by the native entry fixture");
    if(nativeEntryUnexpected)std::abort();
}
void PvpRoomTick() noexcept {UnexpectedRoomDependency();}
void Outfit41RoomTick() noexcept {UnexpectedRoomDependency();}
void Edf5OnlineRoomTick() noexcept {UnexpectedRoomDependency();}
const wchar_t* ConfigPath() noexcept {UnexpectedRoomDependency();return nullptr;}
int GameTextLanguage() noexcept {UnexpectedRoomDependency();return 0;}
bool MapHoldsKeys() noexcept {UnexpectedRoomDependency();return false;}
int SupportCallCount() noexcept {UnexpectedRoomDependency();return 0;}
const wchar_t* SupportCallName(int) noexcept {UnexpectedRoomDependency();return nullptr;}
int SupportCallSeats(int) noexcept {UnexpectedRoomDependency();return 0;}
const wchar_t* SupportCallKey(int) noexcept {UnexpectedRoomDependency();return nullptr;}
bool SaveSupportLoadouts(const LoadoutSave*,int,int*,wchar_t*,std::size_t) noexcept {UnexpectedRoomDependency();return false;}
bool StartVariantGeneration(wchar_t*,std::size_t) noexcept {UnexpectedRoomDependency();return false;}
GenerateStatus VariantGenerationStatus() noexcept {UnexpectedRoomDependency();return GenerateStatus{};}
bool OnlineHostOnly() noexcept {UnexpectedRoomDependency();return false;}
const SupportConfig& SupportCfg() noexcept {UnexpectedRoomDependency();static SupportConfig value{};return value;}
namespace roompreview {
bool InstallPreviewHooks() noexcept {UnexpectedRoomDependency();return false;}
void ShowPreview(const PreviewSpec&) noexcept {UnexpectedRoomDependency();}
void HidePreview() noexcept {++nativePreviewHides;}
void TickPreview(bool) noexcept {UnexpectedRoomDependency();}
}
// The native dialogs (room_dialog.cpp): not ready here, so the menu keeps its overlay path this fixture drives; with no
// dialog open, closing / forgetting one is a no-op.
namespace nativedialog {
bool InstallNativeDialog() noexcept {UnexpectedRoomDependency();return false;}
bool NativeDialogReady() noexcept {return false;}
bool OpenDialog(const wchar_t*,const Button*,int,int,Handle*) noexcept {UnexpectedRoomDependency();return false;}
bool DialogAlive(const Handle&) noexcept {UnexpectedRoomDependency();return false;}
bool TakePicked(const Handle&,std::uint64_t*) noexcept {UnexpectedRoomDependency();return false;}
void CloseDialog(Handle& h) noexcept {h=Handle{};}
void ForgetDialog(Handle& h) noexcept {h=Handle{};}
}
namespace roomsync {
void TickRoomSync() noexcept {UnexpectedRoomDependency();}
void LeaveRoomSync() noexcept {++nativeSyncLeaves;}
int RoomSyncLines(SyncLine*,int) noexcept {UnexpectedRoomDependency();return 0;}
void ForgetRoomHostList() noexcept {UnexpectedRoomDependency();}
}
}

namespace {
using namespace crew;
using namespace crew::roomui;
int checks=0,failures=0;
void Check(bool good,const char* why) {
    ++checks;
    if(!good){++failures;std::printf("FAIL: %s\n",why);}
}

struct FakeControl {
    ControlBlock value{};
    int destroyed=0,deleted=0;
};
void __fastcall Destroy(ControlBlock* control) {
    ++reinterpret_cast<FakeControl*>(control)->destroyed;
}
void __fastcall DeleteControl(ControlBlock* control) {
    ++reinterpret_cast<FakeControl*>(control)->deleted;
}
void* controlVtable[]={reinterpret_cast<void*>(&Destroy),reinterpret_cast<void*>(&DeleteControl)};
void Init(FakeControl& control,long uses=1,long weaks=1) {
    control=FakeControl{};
    control.value={controlVtable,uses,weaks};
}
void DropWeak(Shared& weak) {
    if(weak.control && InterlockedDecrement(&weak.control->weaks)==0)
        reinterpret_cast<void(__fastcall*)(ControlBlock*)>(weak.control->vtable[1])(weak.control);
    weak=Shared{};
}

struct FakeAction {
    FakeControl control{};
    Shared screenWeak{};
    MemberClick member{};
    std::wstring event;
};
struct NativeFixture {
    alignas(void*) unsigned char screen[0x100]{};
    alignas(void*) unsigned char otherScreen[0x100]{};
    alignas(void*) unsigned char root[0x240]{};
    alignas(void*) unsigned char parent[0x240]{};
    alignas(void*) unsigned char button[0x240]{};
    alignas(void*) unsigned char label[0x100]{};
    alignas(void*) unsigned char mission[0x240]{},exit[0x240]{};
    FakeControl screenControl{},rootControl{},parentControl{},buttonControl{},labelControl{},missionControl{},exitControl{};
    FakeAction actions[3]{};
    int createCalls=0,weakCalls=0,makeCalls=0,addCalls=0,labelLocks=0,labelWrites=0;
    int enableCalls=0,hasFocusCalls=0,focusCalls=0;
    void* focused=nullptr;
    std::wstring focusedName;
    bool missionPresent=true,exitPresent=true;
    long parentUsesAtCreate=0,screenUsesAtMake=0;
    bool installed=true,parentPresent=true,parentOwned=true,createWorks=true,weakWorks=true,makeWorks=true,labelAlive=true;
    bool buttonPresent=false,abiGood=true,makeWithoutControl=false;
    std::wstring parentName,caption;
    void Reset(bool room=false) {
        // Tear down the fake native action list before resetting its controls.
        for(auto& action:actions) {
            if(action.control.value.uses) {
                ReleaseAction(action);
                DropWeak(action.screenWeak);
            }
            action=FakeAction{};
        }
        Init(screenControl);Init(rootControl);Init(parentControl);Init(buttonControl);Init(labelControl,1,2);
        Init(missionControl);Init(exitControl);
        createCalls=weakCalls=makeCalls=addCalls=labelLocks=labelWrites=0;
        parentUsesAtCreate=screenUsesAtMake=0;
        enableCalls=hasFocusCalls=focusCalls=0;focused=nullptr;focusedName.clear();
        missionPresent=exitPresent=true;
        installed=parentPresent=parentOwned=createWorks=weakWorks=makeWorks=labelAlive=abiGood=true;
        buttonPresent=makeWithoutControl=false;parentName.clear();caption.clear();
        std::memset(button,0,sizeof(button));
        std::memset(mission,0,sizeof(mission));std::memset(exit,0,sizeof(exit));
        mission[kComponentEnabled]=exit[kComponentEnabled]=1;
        const Shared weakLabel{label,&labelControl.value};
        std::memcpy(button+kButtonLabel,&weakLabel,sizeof(weakLabel));
        st=State{};
        st.self=screen;st.room=room;
        followed.store(screen);followedGone.store(false);nativeEntryOk=true;
        nativeEntryConfig=Config{};nativePreviewHides=nativeSyncLeaves=0;
        hudtext::Use(hudtext::Lang::en);
    }
    static void ReleaseAction(FakeAction& action) {
        // The fixture's array is the native Button::actions owner.
        Shared owned{&action,&action.control.value};
        Release(owned);
    }
    void Dispatch(const wchar_t* event) {
        for(auto& action:actions) {
            if(action.event!=event || !action.control.value.uses || !action.screenWeak.control)continue;
            ControlBlock* control=action.screenWeak.control;
            // Native callback locks weak_ptr for this invocation only.
            if(control->uses<=0)continue;
            InterlockedIncrement(&control->uses);
            auto* target=static_cast<unsigned char*>(action.screenWeak.object)+action.member.adjust;
            action.member.call(target,nullptr);
            Shared invocation{action.screenWeak.object,control};
            Release(invocation);
        }
    }
} fixture;

Shared Own(void* object,FakeControl& control) {
    InterlockedIncrement(&control.value.uses);
    return Shared{object,&control.value};
}
Shared* __fastcall FindStub(unsigned char* self,Shared* out,const WideString* name) {
    fixture.abiGood=fixture.abiGood && self==fixture.screen;
    const wchar_t* text=name->capacity<=7 ? name->text : name->ptr;
    fixture.abiGood=fixture.abiGood && name->size==std::wcslen(text);
    *out=Shared{};
    if(!std::wcscmp(text,kRootName) && fixture.installed)*out=Own(fixture.root,fixture.rootControl);
    else if(!std::wcscmp(text,L"ButtonsGroup") || !std::wcscmp(text,L"LeftButtonsBase2")) {
        fixture.parentName=text;
        if(fixture.parentPresent) {
            if(fixture.parentOwned)*out=Own(fixture.parent,fixture.parentControl);
            else *out=Shared{fixture.parent,nullptr};
        }
        } else if(!std::wcscmp(text,kNativeEntryName) && fixture.buttonPresent)*out=Own(fixture.button,fixture.buttonControl);
    else if(!std::wcscmp(text,L"Button_Mission") || !std::wcscmp(text,L"ButtonGotoSelectMission")) {
        fixture.abiGood=fixture.abiGood && !std::wcscmp(text,st.room ? L"Button_Mission" : L"ButtonGotoSelectMission");
        if(fixture.missionPresent)*out=Own(fixture.mission,fixture.missionControl);
    } else if(!std::wcscmp(text,L"Button_Exit") || !std::wcscmp(text,L"ButtonGotoTitle")) {
        fixture.abiGood=fixture.abiGood && !std::wcscmp(text,st.room ? L"Button_Exit" : L"ButtonGotoTitle");
        if(fixture.exitPresent)*out=Own(fixture.exit,fixture.exitControl);
    }
    return out;
}
Shared* __fastcall CreateButtonStub(unsigned char* self,Shared* out,Shared* parent,const wchar_t* name) {
    ++fixture.createCalls;
    fixture.abiGood=fixture.abiGood && self==fixture.screen && parent->object==fixture.parent &&
        parent->control==&fixture.parentControl.value && !std::wcscmp(name,kNativeEntryName);
    fixture.parentUsesAtCreate=parent->control ? parent->control->uses : -1;
    Release(*parent); // AddByName consumes its by-value parent shared_ptr.
    *out=Shared{};
    if(fixture.createWorks) {
        fixture.buttonPresent=true;
        *out=Own(fixture.button,fixture.buttonControl); // layout owns the baseline reference.
        fixture.button[kShown]=1;
        fixture.button[kComponentEnabled]=fixture.button[kComponentEnabled+1]=1;
    }
    return out;
}
Shared* __fastcall ScreenWeakStub(unsigned char* self,Shared* out) {
    ++fixture.weakCalls;
    fixture.abiGood=fixture.abiGood && self==fixture.screen;
    *out=Shared{};
    if(fixture.weakWorks) {
        InterlockedIncrement(&fixture.screenControl.value.weaks);
        *out=Shared{self,&fixture.screenControl.value};
    }
    return out;
}
Shared* __fastcall MakeClickStub(Shared* out,Shared* weak,const MemberClick* member) {
    const int slot=fixture.makeCalls++;
    fixture.abiGood=fixture.abiGood && slot<3 && weak->object==fixture.screen &&
        weak->control==&fixture.screenControl.value && member->call && member->adjust==0 && member->padding==0;
    fixture.screenUsesAtMake=weak->control ? weak->control->uses : -1;
    *out=Shared{};
    if(!fixture.makeWorks || slot>=3){DropWeak(*weak);return out;}
    if(fixture.makeWithoutControl){DropWeak(*weak);*out=Shared{reinterpret_cast<void*>(0x10),nullptr};return out;}
    FakeAction& action=fixture.actions[slot];
    Init(action.control);
    action.screenWeak=*weak;*weak=Shared{};
    action.member=*member;
    *out=Shared{&action,&action.control.value}; // caller owns one; AddAction copies.
    return out;
}
void __fastcall AddActionStub(void* button,const wchar_t* event,const Shared* shared) {
    ++fixture.addCalls;
    fixture.abiGood=fixture.abiGood && button==fixture.button && shared->object && shared->control;
    auto& action=*static_cast<FakeAction*>(shared->object);
    action.event=event;
    InterlockedIncrement(&shared->control->uses);
}
Shared* __fastcall LockWeakStub(Shared* out,const Shared* weak) {
    ++fixture.labelLocks;
    fixture.abiGood=fixture.abiGood && weak->object==fixture.label && weak->control==&fixture.labelControl.value;
    *out=fixture.labelAlive && weak->control->uses>0 ? Own(weak->object,fixture.labelControl) : Shared{};
    return out;
}
void __fastcall SetButtonTextStub(void* text,const wchar_t* value,const void* font) {
    ++fixture.labelWrites;
    fixture.abiGood=fixture.abiGood && text==fixture.label && value && !font;
    fixture.caption=value ? value : L"";
}
void __fastcall EnableComponentStub(void* component,bool enabled) {
    ++fixture.enableCalls;
    fixture.abiGood=fixture.abiGood && component==fixture.button;
    static_cast<unsigned char*>(component)[kComponentEnabled+1]=enabled ? 1 : 0;
}
bool __fastcall HasFocusStub(void* component) {
    ++fixture.hasFocusCalls;
    fixture.abiGood=fixture.abiGood && component==fixture.button;
    return fixture.focused==component;
}
void __fastcall FocusComponentStub(void* component,bool focus) {
    ++fixture.focusCalls;
    fixture.abiGood=fixture.abiGood && focus &&
        (component==fixture.mission || component==fixture.exit) &&
        static_cast<unsigned char*>(component)[kComponentEnabled]!=0 &&
        fixture.button[kComponentEnabled]==0 && fixture.button[kComponentEnabled+1]==0;
    if(focus) {
        fixture.focused=component;
        fixture.focusedName=component==fixture.mission ? L"mission" : L"exit";
        fixture.Dispatch(L"kill_focus");
    }
}
template<class Fn> void Trampoline(unsigned rva,Fn fn) {
    unsigned char bytes[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0}; // mov rax,imm64; jmp rax
    const auto target=reinterpret_cast<std::uintptr_t>(fn);
    std::memcpy(bytes+2,&target,sizeof(target));
    std::memcpy(image+rva,bytes,sizeof(bytes));
}
void InstallNativeStubs() {
    Trampoline(kFind,&FindStub);Trampoline(kCreateButton,&CreateButtonStub);
    Trampoline(kScreenWeak,&ScreenWeakStub);Trampoline(kMakeClick,&MakeClickStub);
    Trampoline(kAddAction,&AddActionStub);Trampoline(kLockWeak,&LockWeakStub);
    Trampoline(kSetButtonText,&SetButtonTextStub);
    Trampoline(kEnableComponent,&EnableComponentStub);Trampoline(kHasFocus,&HasFocusStub);
    Trampoline(kFocusComponent,&FocusComponentStub);
    FlushInstructionCache(GetCurrentProcess(),image,0x900000);
}

void Construction() {
    for(bool room:{false,true}) {
        fixture.Reset(room);
        EnsureNativeEntry(fixture.screen);
        Check(st.nativeEntry.attempted && st.nativeEntry.ready,"production creation binds a native Click action");
        Check(fixture.createCalls==1 && fixture.parentName==(room ? L"ButtonsGroup" : L"LeftButtonsBase2"),
              "HQ and room use their own stock focus parent group");
        Check(fixture.parentUsesAtCreate==3 && fixture.parentControl.value.uses==1 &&
              fixture.rootControl.value.uses==1 && fixture.buttonControl.value.uses==1,
              "Find reference and consumed parent copy each release exactly once");
        Check(fixture.makeCalls==3 && fixture.addCalls==3 && fixture.weakCalls==3,
              "Click, focus and kill_focus each get an independent native callback");
        Check(fixture.screenUsesAtMake==1 && fixture.screenControl.value.uses==1 &&
              fixture.screenControl.value.weaks==4,"native actions retain weak screens without retaining their lifetime");
        bool owned=true;
        for(const auto& action:fixture.actions)owned=owned && action.control.value.uses==1;
        Check(owned,"AddAction copies each shared action and production releases its temporary");
        Check(!fixture.button[kShown],"a newly created button is hidden until input and label are ready");
        EnsureNativeEntry(fixture.screen);EnsureNativeEntry(fixture.screen);
        Check(fixture.createCalls==1 && fixture.addCalls==3,"successive frames never append duplicate buttons or actions");
        Check(fixture.abiGood,"real native ABI receives expected object, string and member-pointer contracts");
    }
}
void RefusalPaths() {
    fixture.Reset();nativeEntryOk=false;
    EnsureNativeEntry(fixture.screen);
    Check(!st.nativeEntry.attempted && !fixture.createCalls,"unvalidated native code never creates a focus entry");
    nativeEntryOk=true;EnsureNativeEntry(fixture.screen);
    Check(st.nativeEntry.ready && fixture.createCalls==1,"later validated native availability still allows first creation");
    fixture.Reset();fixture.installed=false;
    EnsureNativeEntry(fixture.screen);fixture.installed=true;EnsureNativeEntry(fixture.screen);
    Check(st.nativeEntry.attempted && !st.nativeEntry.ready && !fixture.createCalls && fixture.parentName.empty(),
          "legacy or foreign layout without vcRoot never adds an empty focus entry");
    fixture.Reset();fixture.parentPresent=false;
    EnsureNativeEntry(fixture.screen);
    Check(!st.nativeEntry.ready && !fixture.createCalls && fixture.rootControl.value.uses==1,
          "missing native focus group cannot orphan a dynamic button");
    fixture.Reset();fixture.parentOwned=false;
    EnsureNativeEntry(fixture.screen);
    Check(!st.nativeEntry.ready && !fixture.createCalls,"unowned parent cannot be passed as a native shared pointer");
    fixture.Reset();fixture.createWorks=false;
    EnsureNativeEntry(fixture.screen);EnsureNativeEntry(fixture.screen);
    Check(!st.nativeEntry.ready && fixture.createCalls==1 && !fixture.buttonPresent &&
          fixture.parentControl.value.uses==1,"missing definition releases consumed parent once and never adds a focus slot");
    fixture.Reset();fixture.weakWorks=false;
    EnsureNativeEntry(fixture.screen);
    Check(!st.nativeEntry.ready && !fixture.addCalls && !fixture.button[kShown] &&
          fixture.buttonControl.value.uses==1 && fixture.screenControl.value.weaks==1,
          "missing native weak-screen identity leaves the button hidden and unbound");
    fixture.Reset();fixture.makeWorks=false;
    EnsureNativeEntry(fixture.screen);
    Check(!st.nativeEntry.ready && !fixture.addCalls && !fixture.button[kShown] &&
          fixture.screenControl.value.weaks==1,"failed native action consumes its weak temporary without retaining screen");
}
void InvalidActionControl() {
    fixture.Reset();fixture.makeWithoutControl=true;
    EnsureNativeEntry(fixture.screen);
    Check(!st.nativeEntry.ready && !fixture.addCalls && !fixture.button[kShown] &&
          fixture.screenControl.value.weaks==1,
          "native allocator failure sentinel with a non-null object and no control never becomes a Click action");
}
void DisplayAndFocus() {
    fixture.Reset();EnsureNativeEntry(fixture.screen);
    NativeEntryInput(st.nativeEntry,true);
    NativeEntryDisplay(fixture.screen,st.nativeEntry.enabled);
    Check(fixture.button[kShown] && fixture.caption==Title(false) && fixture.labelWrites==1,
          "native ui TextField gets the current translated title before joining visible focus");
    Check(fixture.labelControl.value.uses==1 && fixture.labelControl.value.weaks==2,
          "native label lock's strong temporary is released and button's weak label is preserved");
    NativeEntryDisplay(fixture.screen,true);NativeEntryDisplay(fixture.screen,false);
    Check(fixture.labelWrites==1 && fixture.labelLocks==1 && !fixture.button[kShown],
          "unchanged title avoids native text work and a covered screen hides its entry");
    hudtext::Use(hudtext::Lang::zhCN);NativeEntryDisplay(fixture.screen,true);
    Check(fixture.labelWrites==2 && fixture.caption==Title(false) && fixture.button[kShown],
          "language change rewrites the live native button caption");
    fixture.Dispatch(L"focus");
    Check(st.nativeFocused && !st.menu.open,"native focus only requests description text and never opens the menu");
    fixture.Dispatch(L"kill_focus");
    Check(!st.nativeFocused,"native kill_focus withdraws the extra description");
    fixture.Reset();EnsureNativeEntry(fixture.screen);fixture.labelAlive=false;
    NativeEntryDisplay(fixture.screen,true);
    Check(!fixture.button[kShown] && !fixture.labelWrites && !st.nativeLabel,
          "unready label never leaves a blank visible focus entry");
    fixture.labelAlive=true;NativeEntryDisplay(fixture.screen,true);
    Check(fixture.button[kShown] && fixture.labelWrites==1,"label weak-lock readiness is retryable without rebuilding the button");
    Check(fixture.abiGood,"native label path uses ui TextField SetText with a null font override");
}
void FocusNavigation() {
    for(bool room:{false,true}) {
        fixture.Reset(room);EnsureNativeEntry(fixture.screen);
        Check(!fixture.button[kShown] && !fixture.button[kComponentEnabled] && !fixture.button[kComponentEnabled+1],
              "new native entry is immediately disabled in current and pending navigation until display is ready");
        NativeEntryDisplay(fixture.screen,true);
        Check(fixture.button[kShown] && !fixture.button[kComponentEnabled] && fixture.button[kComponentEnabled+1],
              "showing enables pending native state without skipping native skin transition");
        // Emulate the native child update that commits the pending enable.
        fixture.button[kComponentEnabled]=fixture.button[kComponentEnabled+1];
        fixture.focused=fixture.button;fixture.Dispatch(L"focus");
        NativeEntryDisplay(fixture.screen,false);
        Check(!fixture.button[kShown] && !fixture.button[kComponentEnabled] && !fixture.button[kComponentEnabled+1],
              "opening overlay, disabling loadouts, or covering screen immediately removes hidden button from navigation");
        Check(fixture.focusCalls==1 && fixture.focused==fixture.mission && !st.nativeFocused,
              "hiding a focused entry restores each screen's enabled native mission button");
        NativeEntryDisplay(fixture.screen,false);
        Check(fixture.focusCalls==1 && fixture.missionControl.value.uses==1 && fixture.exitControl.value.uses==1,
              "repeated hiding does not steal native focus or retain fallback shared references");
        Check(fixture.abiGood,"focus transfer uses exact per-screen native names and commits disable before native Focus");
    }
    fixture.Reset();EnsureNativeEntry(fixture.screen);NativeEntryDisplay(fixture.screen,true);
    fixture.focused=fixture.mission;NativeEntryDisplay(fixture.screen,false);
    Check(!fixture.focusCalls && fixture.focused==fixture.mission,"hiding an unfocused entry never moves the user's current focus");
    fixture.Reset();EnsureNativeEntry(fixture.screen);fixture.focused=fixture.button;
    fixture.mission[kComponentEnabled]=0;fixture.mission[kComponentEnabled+1]=1;
    NativeEntryDisplay(fixture.screen,false);
    Check(fixture.focusCalls==1 && fixture.focused==fixture.exit,
          "default target pending-enable alone is insufficient; fallback selects current-enabled Exit or Title");
    fixture.Reset(true);EnsureNativeEntry(fixture.screen);fixture.focused=fixture.button;fixture.missionPresent=false;
    NativeEntryDisplay(fixture.screen,false);
    Check(fixture.focusCalls==1 && fixture.focused==fixture.exit,"missing default target falls back to the room's Exit button");
    fixture.Reset();EnsureNativeEntry(fixture.screen);fixture.focused=fixture.button;
    fixture.mission[kComponentEnabled]=fixture.exit[kComponentEnabled]=0;
    NativeEntryDisplay(fixture.screen,false);
    Check(!fixture.focusCalls && !fixture.button[kComponentEnabled] && !fixture.button[kComponentEnabled+1] &&
          fixture.missionControl.value.uses==1 && fixture.exitControl.value.uses==1,
          "all-disabled stock targets are released without forcing focus into another disabled item");
    fixture.Reset();EnsureNativeEntry(fixture.screen);fixture.labelAlive=false;
    NativeEntryDisplay(fixture.screen,true);
    Check(!fixture.button[kShown] && !fixture.button[kComponentEnabled] && !fixture.button[kComponentEnabled+1],
          "label weak-lock failure disables native navigation as well as drawing");
    for(int fail=0;fail<3;++fail) {
        fixture.Reset();
        if(fail==0)fixture.weakWorks=false;
        if(fail==1)fixture.makeWorks=false;
        if(fail==2)fixture.makeWithoutControl=true;
        EnsureNativeEntry(fixture.screen);NativeEntryDisplay(fixture.screen,true);
        Check(!st.nativeEntry.ready && !fixture.button[kShown] && !fixture.button[kComponentEnabled] &&
              !fixture.button[kComponentEnabled+1],"failed callback binding never leaves an invisible enabled focus slot");
    }
}
void ScreenLifecycle() {
    fixture.Reset();EnsureNativeEntry(fixture.screen);NativeEntryDisplay(fixture.screen,true);
    const wchar_t* caption=st.nativeLabel;
    NativeEntryDisplay(fixture.screen,false);
    st.menu.open=true;st.previewShown=true;st.switches.syncing=true;
    st.gate.latched=true;st.written.valid=true;st.drew=true;
    st.nativeEntry.requested=true;
    ForgetMenu();
    Check(st.self==fixture.screen && followed.load()==fixture.screen && st.nativeEntry.attempted &&
          st.nativeEntry.ready && st.nativeLabel==caption,
          "mission reset preserves a retained native room's button and weak action identity");
    Check(!st.menu.open && !st.previewShown && !st.nativeEntry.enabled && !st.nativeEntry.requested &&
          !st.gate.latched && !st.written.valid && !st.drew,
          "mission reset drops menu, preview, input and queued requests without extending native state");
    Check(nativePreviewHides==1 && nativeSyncLeaves==1,"mission reset releases active preview and room synchronization once");
    EnsureNativeEntry(fixture.screen);ForgetMenu();EnsureNativeEntry(fixture.screen);
    Check(fixture.createCalls==1 && fixture.makeCalls==3 && fixture.addCalls==3 &&
          fixture.screenControl.value.weaks==4 && nativePreviewHides==1 && nativeSyncLeaves==1,
          "repeated mission reset on one retained screen never duplicates native entries or callbacks");
    NativeEntryInput(st.nativeEntry,true);fixture.Dispatch(L"Click");
    Check(TakeNativeEntry(st.nativeEntry,true),"retained screen's original weak Click callback remains usable after returning from mission");
    Follow(fixture.screen,false);EnsureNativeEntry(fixture.screen);
    Check(fixture.createCalls==1 && st.nativeEntry.ready,"following the same live native screen retains its existing button");
    NoteGone(fixture.otherScreen);
    Check(!followedGone.load(),"another native screen's destructor cannot discard the followed screen's identity");
    NoteGone(fixture.screen);Follow(fixture.screen,false);
    Check(st.self==fixture.screen && followed.load()==fixture.screen && !followedGone.load() &&
          !st.nativeEntry.attempted && !st.nativeEntry.ready && !st.nativeLabel && !st.nativeFocused,
          "destructor plus reused object address starts a fresh native-entry generation");
    // Native old layout teardown owns its action list, independently of Forget's plugin-only state.
    for(auto& action:fixture.actions) {
        NativeFixture::ReleaseAction(action);DropWeak(action.screenWeak);action=FakeAction{};
    }
    fixture.makeCalls=fixture.addCalls=0;fixture.buttonPresent=false;
    EnsureNativeEntry(fixture.screen);
    Check(fixture.createCalls==2 && fixture.makeCalls==3 && fixture.addCalls==3 && st.nativeEntry.ready &&
          fixture.screenControl.value.weaks==4,"new object at the reused address receives exactly one fresh native entry");
}
void ClickLifetime() {
    fixture.Reset();EnsureNativeEntry(fixture.screen);
    fixture.Dispatch(L"Click");
    Check(!st.nativeEntry.requested,"input-disabled native activation cannot queue an open");
    NativeEntryInput(st.nativeEntry,true);
    fixture.Dispatch(L"Click");fixture.Dispatch(L"Click");
    Check(st.nativeEntry.requested && !st.menu.open && !st.previewShown && !st.gate.latched,
          "real native Click callback only queues and does not mutate menu or preview during action dispatch");
    Check(fixture.screenControl.value.uses==1,"native dispatch releases its temporary strong screen lock");
    Check(TakeNativeEntry(st.nativeEntry,true) && !TakeNativeEntry(st.nativeEntry,true),
          "post-native-update queue consumption opens at most once");
    fixture.Dispatch(L"Click");NativeEntryInput(st.nativeEntry,false);
    Check(!st.nativeEntry.requested && !TakeNativeEntry(st.nativeEntry,true),
          "screen input loss discards a click before deferred opening");
    NativeEntryInput(st.nativeEntry,true);st.menu.open=true;fixture.Dispatch(L"Click");
    Check(!st.nativeEntry.requested,"already-open menu refuses native reentry");
    st.menu.open=false;nativeEntryConfig.enabled=false;fixture.Dispatch(L"Click");
    Check(!st.nativeEntry.requested,"global disable prevents delayed native menu opens");
    nativeEntryConfig.enabled=true;nativeEntryConfig.roomLoadout=false;fixture.Dispatch(L"Click");
    Check(!st.nativeEntry.requested,"RoomLoadout disable prevents delayed native menu opens");
    nativeEntryConfig.roomLoadout=true;
    NativeEntryClicked(fixture.otherScreen,nullptr);
    Check(!st.nativeEntry.requested,"callback carrying another native screen cannot act on the followed screen");
    followed.store(fixture.otherScreen);fixture.Dispatch(L"Click");
    Check(!st.nativeEntry.requested,"a replaced followed screen cannot act on stale State identity");
    followed.store(fixture.screen);NoteGone(fixture.screen);fixture.Dispatch(L"Click");fixture.Dispatch(L"focus");
    Check(!st.nativeEntry.requested && !st.nativeFocused,"screen destructor invalidates click and focus before native weak object deletion");
    followedGone.store(false);
    Shared screenOwner{fixture.screen,&fixture.screenControl.value};Release(screenOwner);
    Check(fixture.screenControl.value.uses==0 && fixture.screenControl.destroyed==1 &&
          fixture.screenControl.value.weaks==3 && !fixture.screenControl.deleted,
          "native callbacks keep only weak control storage after the actual screen is destroyed");
    fixture.Dispatch(L"Click");fixture.Dispatch(L"focus");
    Check(!st.nativeEntry.requested && !st.nativeFocused && fixture.screenControl.value.uses==0,
          "expired native weak screen never dispatches or resurrects its object");
    for(auto& action:fixture.actions) {
        NativeFixture::ReleaseAction(action);DropWeak(action.screenWeak);
    }
    Check(fixture.screenControl.value.weaks==0 && fixture.screenControl.deleted==1,
          "destroying the native action list releases the last weak screen reference exactly once");
}
}

int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x900000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image){std::puts("FAIL: native fixture image allocation");return 2;}
    InstallNativeStubs();
    Construction();RefusalPaths();InvalidActionControl();DisplayAndFocus();FocusNavigation();ScreenLifecycle();ClickLifetime();
    fixture.Reset();
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("room native entry runtime: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
