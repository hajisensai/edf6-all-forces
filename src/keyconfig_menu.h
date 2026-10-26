// The pure part of src/keyconfig.cpp: the plugin's pages 11..16 put into the game's own key config (HUiHQKeyConfig,
// docs/keyconfig-re.md §7): the sites it patches, the extended page table and the stubs. No Windows, no game:
// tests/keyconfig_menu_test.cpp builds every stub and checks its jumps; tests/keyconfig_native_audit.py checks every
// site against EDF.dll (TimeDateStamp 0x678CCB46).
//
// The menu decides everything about a page in code: which table row (0x896CF9), how many rows (0x89736F), each row's
// label and element ids (0x897B9D), its texts (0x8980CC), the dialog a row opens (0x898FBD) and the values the frame
// refreshes (0x89AD47). Each of these six is a `cmp page, 10; ja default; jump table` whose default is no page at all
// (an empty page, or registers left stale); each `cmp; ja` is replaced by a jump to a stub that runs the stock jump table
// for pages 0..10, the plugin's code for pages 11..16, the stock default for any other.
#pragma once
#include "edf/keybind.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace crew::keymenu {
namespace kb=edf::keybind;
constexpr int kLastPage=kb::kFirstPage+kb::kPages-1;
static_assert(kLastPage<0x7F,"the pages fit a cmp imm8");

// --- The six page dispatches (site: the `cmp; ja` replaced; back: the stock jump table; def: the stock default;
// join: where the plugin's page continues)
struct Dispatch { const char* name; std::size_t site,size,back,def,join; unsigned char reg; };
// reg: the page's register in the stub's cmp (0: eax, 1: edx (page - 1, A only), 2: r8d)
inline constexpr Dispatch kPageRow{"page row",0x896CFC,5,0x896D01,0x896D63,0x896D63,1};      // cmp edx,9; ja +0x62
inline constexpr Dispatch kRowCount{"row count",0x89736F,9,0x897378,0x89727B,0x897288,0};   // cmp eax,0xA; ja 0x89727B
inline constexpr Dispatch kLabel{"labels",0x897B9D,10,0x897BA7,0x897E7F,0x897E65,2};        // cmp r8d,0xA; ja 0x897E7F
inline constexpr Dispatch kTexts{"texts",0x8980CC,9,0x8980D5,0x897F28,0x897F25,0};          // cmp eax,0xA; ja 0x897F28
inline constexpr Dispatch kDialog{"dialog",0x898FBD,9,0x898FC6,0x8991A2,0x8991AC,0};        // cmp eax,0xA; ja 0x8991A2
inline constexpr Dispatch kRefresh{"refresh",0x89AD47,9,0x89AD50,0x89AF41,0x89AF3E,0};      // cmp eax,0xA; ja 0x89AF41
inline constexpr Dispatch kDispatches[]={kPageRow,kRowCount,kLabel,kTexts,kDialog,kRefresh};
// The stock bytes of each (the .inc's signatures cover them with their surroundings).
inline constexpr unsigned char kPageRowCode[]={0x83,0xFA,0x09,0x77,0x62};
inline constexpr unsigned char kRowCountCode[]={0x83,0xF8,0x0A,0x0F,0x87,0x03,0xFF,0xFF,0xFF};
inline constexpr unsigned char kLabelCode[]={0x41,0x83,0xF8,0x0A,0x0F,0x87,0xD8,0x02,0x00,0x00};
inline constexpr unsigned char kTextsCode[]={0x83,0xF8,0x0A,0x0F,0x87,0x53,0xFE,0xFF,0xFF};
inline constexpr unsigned char kDialogCode[]={0x83,0xF8,0x0A,0x0F,0x87,0xDC,0x01,0x00,0x00};
inline constexpr unsigned char kRefreshCode[]={0x83,0xF8,0x0A,0x0F,0x87,0xF1,0x01,0x00,0x00};
inline constexpr const unsigned char* kDispatchCode[]={kPageRowCode,kRowCountCode,kLabelCode,kTextsCode,kDialogCode,kRefreshCode};
static_assert(sizeof(kPageRowCode)==kPageRow.size && sizeof(kRowCountCode)==kRowCount.size && sizeof(kLabelCode)==kLabel.size &&
              sizeof(kTextsCode)==kTexts.size && sizeof(kDialogCode)==kDialog.size && sizeof(kRefreshCode)==kRefresh.size,
              "each site's size is its stock cmp + ja");

// --- The page table (0x17F26B0: 13 bytes a row, 0x4E ends a row's actions): read through six lea's (7 bytes each, a
// rip disp32 at +3), which are pointed at a copy with rows 11..16 added. 0x7CD9E0's lea of row 2 alone stays.
inline constexpr std::size_t kTable=0x17F26B0,kRowBytes=13,kStockRows=11,kRows=kStockRows+kb::kPages;
inline constexpr std::size_t kTableLeas[]={0x7CCE8B,0x7CCF13,0x7CD2EE,0x7CDA96,0x7CDB34,0x7CDC51};
inline constexpr std::size_t kLeaSize=7,kLeaDisp=3;
// The model's all-sets flag (0x7CDE10: rows 2 and 5..10 write the four class sets, 0xD5470): `cmp eax,5` of
// `lea eax,[r9-5]` widened to rows 5..16.
inline constexpr std::size_t kSyncedRows=0x7CDE40;
inline constexpr unsigned char kSyncedRowsCode[]={0x83,0xF8,0x05};
inline constexpr unsigned char kSyncedRowsOurs[]={0x83,0xF8,static_cast<unsigned char>(kLastPage-5)};

// --- The stock common page's defaults (plan §3.3, the user 2026-10-26: Chat to Enter, VoiceChat to a key nobody uses)
// 0xD48B0 writes the common page's defaults into sets 0..3, one `mov dword [rcx+disp32], imm32` (10 bytes, the imm at +6)
// per action per set; it runs when the store is built without a COMMON.CFG (0xD3F90 -> 0xD4530 flag 8, and the full
// reset 0xDB8C0 copies such a store) and on the common page's 「恢复默认」 (0xD4BB0 page 0). Only these immediates change:
// a player's COMMON.CFG keeps whatever it holds (keyconfig-re.md §5.2).
struct StockDefault { std::size_t rva; int action,set; std::int32_t from,to; };
inline constexpr std::size_t kDefaultImm=6,kDefaultSize=10;
inline constexpr std::int32_t kChatFrom=0x58,kChatTo=0x62,kVoiceFrom=0x27,kVoiceTo=0x1C;   // ControlL -> Return, V -> K
inline constexpr StockDefault kStockDefaults[]={
    {0xD48EC,0x1D,0,kChatFrom,kChatTo},{0xD491E,0x21,0,kVoiceFrom,kVoiceTo},
    {0xD4964,0x1D,1,kChatFrom,kChatTo},{0xD4996,0x21,1,kVoiceFrom,kVoiceTo},
    {0xD49DC,0x1D,2,kChatFrom,kChatTo},{0xD4A0E,0x21,2,kVoiceFrom,kVoiceTo},
    {0xD4A54,0x1D,3,kChatFrom,kChatTo},{0xD4A86,0x21,3,kVoiceFrom,kVoiceTo},
};
// The instruction as the stock has it (c7 81 disp32 imm32) and as it is made.
inline void StockDefaultCode(const StockDefault& d,bool ours,unsigned char* out) noexcept {
    const std::uint32_t disp=static_cast<std::uint32_t>(kb::kSlotsAt+static_cast<std::size_t>(d.set)*kb::kSetStride+static_cast<std::size_t>(d.action)*4);
    const std::int32_t imm=ours ? d.to : d.from;
    out[0]=0xC7;out[1]=0x81;
    std::memcpy(out+2,&disp,4);
    std::memcpy(out+kDefaultImm,&imm,4);
}

// --- Calls redirected
inline constexpr std::size_t kResetCall=0x89A286,kResetPage=0xD4BB0;      // 「恢复默认」: the page's defaults (mgr, player, page)
inline constexpr std::size_t kAllowedCall=0x89D1F1,kKeyAllowed=0x7CD940;  // the dialog: may this key go to this action
inline constexpr std::size_t kTopDialogCall=0x893337,kShowDialog=0x119C600;   // the key config's top list shown
// The top list's request (HUiHQGameSetting::cbCallKeyConfigSelect 0x892FF0, at rbp+0xA0): type 5, its six keyboard items
// (common, the four classes: lambda_3 0x18030C0 capturing (screen, page) at impl +8 / +0x10, whose call opens the key
// config on that page by 0x88DD00; the vehicles: lambda_2 0x1803088).
inline constexpr std::int32_t kTopType=5;
inline constexpr std::size_t kTopItems=6,kPageItemVtable=0x18030C0,kVehicleItemVtable=0x1803088,kItemPage=0x10;
inline constexpr std::size_t kOpenPage=0x88DD00;
// What the plugin's pages ask of the game: the key name of (model, action) 0x7CD2B0 ([model+4] player, [model+0x14]
// set), the text table [0x20B29A8] and its lookup 0x7A3500; the menu's model +0x798, its player +0x520; the unassigned
// name (the pad name table 0x1F56050's first pointer).
inline constexpr std::size_t kKeyName=0x7CD2B0,kTextLookup=0x7A3500,kTextTable=0x20B29A8,kNameTable=0x1F56050;
inline constexpr std::size_t kMenuModel=0x798,kMenuPlayer=0x520;

// The page table with the plugin's rows: the stock rows as read, then each page's actions and 0x4E.
inline void BuildTable(const unsigned char* stock,unsigned char* out) noexcept {
    std::memcpy(out,stock,kStockRows*kRowBytes);
    std::memset(out+kStockRows*kRowBytes,0,kb::kPages*kRowBytes);
    for(int p=0;p<kb::kPages;++p) {
        unsigned char* row=out+(kStockRows+p)*kRowBytes;
        int n=0;
        for(const kb::Slot& s:kb::kSlots)if(s.page==kb::kFirstPage+p)row[n++]=s.action;
        row[n]=static_cast<unsigned char>(kb::kListEnd);
    }
}

// --- A small emitter
struct Code {
    unsigned char b[160];
    std::size_t n=0;
    std::uint64_t at=0;   // where the bytes will run
    bool ok=true;
    void Raw(std::initializer_list<unsigned char> bytes) noexcept { for(unsigned char x:bytes){ if(n<sizeof(b))b[n++]=x; else ok=false; } }
    void Imm32(std::uint32_t v) noexcept { Raw({static_cast<unsigned char>(v),static_cast<unsigned char>(v>>8),static_cast<unsigned char>(v>>16),static_cast<unsigned char>(v>>24)}); }
    void Imm64(std::uint64_t v) noexcept { Imm32(static_cast<std::uint32_t>(v));Imm32(static_cast<std::uint32_t>(v>>32)); }
    void Rel32(std::uint64_t target) noexcept {   // the rel32 of the instruction ending right after it
        const std::int64_t rel=static_cast<std::int64_t>(target)-static_cast<std::int64_t>(at+n+4);
        if(rel<INT32_MIN || rel>INT32_MAX)ok=false;
        Imm32(static_cast<std::uint32_t>(static_cast<std::int32_t>(rel)));
    }
    void Jmp(std::uint64_t t) noexcept { Raw({0xE9});Rel32(t); }
    void Jcc(unsigned char cc,std::uint64_t t) noexcept { Raw({0x0F,cc});Rel32(t); }   // 0x82 jb, 0x83 jae, 0x86 jbe
    void Call(std::uint64_t fn) noexcept { Raw({0x48,0xB8});Imm64(fn);Raw({0xFF,0xD0}); }   // mov rax,fn; call rax
};
inline constexpr unsigned char kJb=0x82,kJae=0x83,kJbe=0x86;

// The common head: the stock cmp and its jump table for 0..10, the default outside 11..16.
inline void Head(Code& c,const Dispatch& d,std::uint64_t base) noexcept {
    const unsigned char limit=d.reg==1 ? 9 : 10;
    switch(d.reg) {
    case 1: c.Raw({0x83,0xFA,limit});break;          // cmp edx,9 (page - 1)
    case 2: c.Raw({0x41,0x83,0xF8,limit});break;     // cmp r8d,10
    default: c.Raw({0x83,0xF8,limit});break;         // cmp eax,10
    }
    c.Jcc(kJbe,base+d.back);
    const auto cmpPage=[&](unsigned char v) noexcept {
        if(d.reg==2)c.Raw({0x41,0x83,0xF8,v}); else c.Raw({0x83,0xF8,v});   // the page itself (A: eax holds it too)
    };
    cmpPage(static_cast<unsigned char>(kb::kFirstPage));c.Jcc(kJb,base+d.def);
    cmpPage(static_cast<unsigned char>(kLastPage+1));c.Jcc(kJae,base+d.def);
}

// The plugin's callbacks the stubs call (Microsoft x64: page, row, the menu, out).
struct Callbacks { std::uint64_t label=0,value=0,dialog=0; };
// The dialog's row: label and value texts, the action byte (KcDialog fills it; E reads it).
struct DialogRow { const wchar_t* label; const wchar_t* value; std::uint64_t action; };
static_assert(sizeof(DialogRow)==0x18,"the stub reads +0, +8, +0x10");

// A: the page's table row is the page itself (ebx), then the stock store of it (0x896D63).
inline void PageRowStub(Code& c,std::uint64_t base) noexcept {
    Head(c,kPageRow,base);
    c.Raw({0x8B,0xD8});                               // mov ebx,eax
    c.Jmp(base+kPageRow.join);
}
// B: the row count from the stub's table (rows[page - 11]) into this +0x844 and the layout's count [rsp+0x30].
inline void RowCountStub(Code& c,std::uint64_t base) noexcept {
    Head(c,kRowCount,base);
    const std::size_t lea=c.n;
    c.Raw({0x48,0x8D,0x0D,0,0,0,0});                  // lea rcx,[rip+rows]
    c.Raw({0x8B,0x4C,0x81,static_cast<unsigned char>(-4*kb::kFirstPage)});   // mov ecx,[rcx+rax*4-44]
    c.Raw({0x89,0x8E,0x44,0x08,0x00,0x00});           // mov [rsi+0x844],ecx
    c.Raw({0x89,0x4C,0x24,0x30});                     // mov [rsp+0x30],ecx
    c.Jmp(base+kRowCount.join);
    const std::int32_t disp=static_cast<std::int32_t>(c.n-(lea+7));
    std::memcpy(c.b+lea+3,&disp,4);
    for(int p=0;p<kb::kPages;++p)c.Imm32(static_cast<std::uint32_t>(kb::RowsOf(kb::kFirstPage+p)));
}
// C: a row's label (r9d) or none: rax for the stock join (0x897E65: mov rdx,rax; ids set when not null).
inline void LabelStub(Code& c,std::uint64_t base,const Callbacks& cb) noexcept {
    Head(c,kLabel,base);
    c.Raw({0x44,0x89,0xC1});                          // mov ecx,r8d
    c.Raw({0x44,0x89,0xCA});                          // mov edx,r9d
    c.Raw({0x45,0x31,0xC0});                          // xor r8d,r8d
    c.Raw({0x48,0x83,0xEC,0x20});                     // sub rsp,0x20
    c.Call(cb.label);
    c.Raw({0x48,0x83,0xC4,0x20});                     // add rsp,0x20
    c.Jmp(base+kLabel.join);
}
// D: the row's value text in r12, its label in rax for the stock join (0x897F25: mov r14,rax).
inline void TextsStub(Code& c,std::uint64_t base,const Callbacks& cb) noexcept {
    Head(c,kTexts,base);
    c.Raw({0x48,0x83,0xEC,0x30});                     // sub rsp,0x30
    c.Raw({0x89,0x44,0x24,0x20});                     // mov [rsp+0x20],eax
    c.Raw({0x89,0xC1});                               // mov ecx,eax
    c.Raw({0x41,0x8B,0x17});                          // mov edx,[r15]   (the item's row)
    c.Raw({0x49,0x89,0xF0});                          // mov r8,rsi      (the menu)
    c.Call(cb.value);
    c.Raw({0x49,0x89,0xC4});                          // mov r12,rax
    c.Raw({0x8B,0x4C,0x24,0x20});                     // mov ecx,[rsp+0x20]
    c.Raw({0x41,0x8B,0x17});                          // mov edx,[r15]
    c.Raw({0x45,0x31,0xC0});                          // xor r8d,r8d
    c.Call(cb.label);
    c.Raw({0x48,0x83,0xC4,0x30});                     // add rsp,0x30
    c.Jmp(base+kTexts.join);
}
// E: the dialog's label (r14), value (r15) and action (bl) for the row under the cursor (item r14).
inline void DialogStub(Code& c,std::uint64_t base,const Callbacks& cb) noexcept {
    Head(c,kDialog,base);
    c.Raw({0x48,0x83,0xEC,0x40});                     // sub rsp,0x40
    c.Raw({0x89,0xC1});                               // mov ecx,eax
    c.Raw({0x41,0x8B,0x16});                          // mov edx,[r14]   (the item's row)
    c.Raw({0x49,0x89,0xF8});                          // mov r8,rdi      (the menu)
    c.Raw({0x4C,0x8D,0x4C,0x24,0x20});                // lea r9,[rsp+0x20]
    c.Call(cb.dialog);
    c.Raw({0x4C,0x8B,0x74,0x24,0x20});                // mov r14,[rsp+0x20]
    c.Raw({0x4C,0x8B,0x7C,0x24,0x28});                // mov r15,[rsp+0x28]
    c.Raw({0x8A,0x5C,0x24,0x30});                     // mov bl,[rsp+0x30]
    c.Raw({0x48,0x83,0xC4,0x40});                     // add rsp,0x40
    c.Jmp(base+kDialog.join);
}
// F: the row's value text in rax for the stock join (0x89AF3E: mov rbx,rax), the item [[rdi]].
inline void RefreshStub(Code& c,std::uint64_t base,const Callbacks& cb) noexcept {
    Head(c,kRefresh,base);
    c.Raw({0x48,0x83,0xEC,0x20});                     // sub rsp,0x20
    c.Raw({0x89,0xC1});                               // mov ecx,eax
    c.Raw({0x48,0x8B,0x17});                          // mov rdx,[rdi]
    c.Raw({0x8B,0x12});                               // mov edx,[rdx]
    c.Raw({0x49,0x89,0xF0});                          // mov r8,rsi      (the menu)
    c.Call(cb.value);
    c.Raw({0x48,0x83,0xC4,0x20});                     // add rsp,0x20
    c.Jmp(base+kRefresh.join);
}
// Stub i of kDispatches, assembled to run at `c.at`.
inline void BuildStub(int i,Code& c,std::uint64_t base,const Callbacks& cb) noexcept {
    switch(i) {
    case 0: PageRowStub(c,base);break;
    case 1: RowCountStub(c,base);break;
    case 2: LabelStub(c,base,cb);break;
    case 3: TextsStub(c,base,cb);break;
    case 4: DialogStub(c,base,cb);break;
    default: RefreshStub(c,base,cb);break;
    }
}
inline constexpr std::size_t kStubSlot=128;   // each stub's room in the one page

// The patch over a site: jmp rel32 to its stub, the rest int3 (never reached: every stub leaves elsewhere).
inline bool SiteJump(std::uint64_t at,std::uint64_t stub,std::size_t size,unsigned char* out) noexcept {
    const std::int64_t rel=static_cast<std::int64_t>(stub)-static_cast<std::int64_t>(at+5);
    if(size<5 || rel<INT32_MIN || rel>INT32_MAX)return false;
    const std::int32_t rel32=static_cast<std::int32_t>(rel);
    out[0]=0xE9;
    std::memcpy(out+1,&rel32,4);
    std::memset(out+5,0xCC,size-5);
    return true;
}
// A lea's new disp32 to `target` (false: out of reach).
inline bool LeaTo(std::uint64_t at,const unsigned char* stock,std::uint64_t target,unsigned char* out) noexcept {
    const std::int64_t rel=static_cast<std::int64_t>(target)-static_cast<std::int64_t>(at+kLeaSize);
    if(rel<INT32_MIN || rel>INT32_MAX)return false;
    std::memcpy(out,stock,kLeaSize);
    const std::int32_t rel32=static_cast<std::int32_t>(rel);
    std::memcpy(out+kLeaDisp,&rel32,4);
    return true;
}
}  // namespace crew::keymenu
