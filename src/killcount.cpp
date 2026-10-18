// The EDF4.1 port's kill counters in the battle record (戦績 / 战斗记录; docs/kill-counter-re.md, pylib/edf41kills.py).
//
// The counters themselves need no code: they are variables of APP:/ETC/Achievement.sgo (the installer adds
// edf41_*KillCount there, tools/make_edf5_campaign.py), the 4.1 objects name them in game_object_kill_counter, and a
// player's kill adds 1 to the one its SGO names (EDF+0x5499F0 -> 0xCF640); the save keeps them (TROPHY.DAT, by the names'
// order: ours sort after every stock name, so the stock slots stay where they were).
//
// What does need code is the page: the battle record's constructor (EDF+0x893C40, from its one allocation 0x893BD0 at
// 0x893BF3) fills a 90-row table on its stack (rbp+0x270 .. +0x810; +0x810 is the frame's security cookie, so the table
// cannot grow in place), counts the rows to show (loop 0x8947E0..0x8949A6: a *KillCount row at 0 is not shown), makes
// that many cells, and fills them (loop 0x894D50..0x895164: 'StatusInfo_' + name from the text table, the value from the
// counter store). Its only reads of the table:
//   0x8947C9  lea r14,[rbp+0x27C]               loop 1's row pointer ([r14-0xC] name, [r14] scale)
//   0x8949A2  cmp r12d,0x5A                     loop 1's bound
//   0x894D56  mov rdx,[rbp+r13*8+0x270]         loop 2: name        (r13 = 2 x row)
//   0x894F72, 0x894F90  mulss xmm0,[rbp+r13*8+0x27C]   scale (int and float values)
//   0x895161  cmp eax,0x5A                      loop 2's bound
//   0x895263  movzx ecx,byte [rbp+r13*8+0x278]  format
// Those are pointed at the plugin's own table, in the low 2 GB so that the same instructions address it without a base
// (mov r14d,imm32; [r13*8+disp32]: the same lengths): the stock 90 rows, read out of the constructor's fill (0x893CD7 ..
// 0x8947C3, killcount_logic.h ParseRows), then a row for each 4.1 counter the store holds (a name it lacks would be read
// from the map's head node: garbage), the bounds set to the rows there are. The table and the bounds are set before each
// construction (the call at 0x893BF3 redirected), on the thread that runs it. Every byte the patch relies on is checked
// first; until the call is redirected the table holds the stock rows and the bounds are 90: the stock page.
// EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "edf41_kill_counters.h"
#include "killcount_logic.h"
#include "memory.h"

#include <cstdint>
#include <cstring>
#include <iterator>

namespace crew {
namespace {
using killcount::Row;

constexpr std::size_t kCtor=0x893C40,kCtorCall=0x893BF3;
constexpr std::size_t kFillBegin=0x893CD7,kFillEnd=0x8947C3;
constexpr std::size_t kStats=0x20B2890,kStore=0x14B50;   // the counter store: [EDF+0x20B2890] + 0x14B50 (0x549B49)
constexpr std::size_t kRowPointer=0x8947C9,kBound1=0x8949A2,kRowName=0x894D56,kRowScale[]={0x894F72,0x894F90},
                      kBound2=0x895161,kRowFormat=0x895263;
const unsigned char kRowPointerSig[]={0x4C,0x8D,0xB5,0x7C,0x02,0x00,0x00};             // lea r14,[rbp+0x27C]
const unsigned char kBound1Sig[]={0x41,0x83,0xFC,0x5A};                                // cmp r12d,0x5A
const unsigned char kRowNameSig[]={0x4A,0x8B,0x94,0xED,0x70,0x02,0x00,0x00};           // mov rdx,[rbp+r13*8+0x270]
const unsigned char kRowScaleSig[]={0xF3,0x42,0x0F,0x59,0x84,0xED,0x7C,0x02,0x00,0x00};  // mulss xmm0,[rbp+r13*8+0x27C]
const unsigned char kBound2Sig[]={0x83,0xF8,0x5A};                                     // cmp eax,0x5A
const unsigned char kRowFormatSig[]={0x42,0x0F,0xB6,0x8C,0xED,0x78,0x02,0x00,0x00};    // movzx ecx,byte [rbp+r13*8+0x278]
// The constructor's head (its frame: rbp = rsp-0x820 under a 0x920 frame, the cookie at rbp+0x810).
const unsigned char kCtorSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
                                0x48,0x8D,0xAC,0x24,0xE0,0xF7,0xFF,0xFF,0x48,0x81,0xEC,0x20,0x09,0x00,0x00};
// The store reached as the kill does (0x549B49: lea rcx,[rbx+0x14B50]).
const unsigned char kStoreSig[]={0x48,0x8D,0x8B,0x50,0x4B,0x01,0x00};
constexpr std::size_t kStoreUse=0x549B49;

constexpr int kRows=killcount::kStockRows+static_cast<int>(std::size(killcount::kEdf41Counters));
static_assert(kRows<=killcount::kMostRows,"the bounds are imm8 compares");

using CtorFn=void*(__fastcall*)(void*,void*);
CtorFn ctorNext=nullptr;
Row* table=nullptr;                   // kRows rows, below 2 GB
std::uint8_t bound=killcount::kStockRows;
int shown41=-1;                       // the 4.1 rows of the last construction (logged when it changes)

// A page for the table whose address fits a sign-extended disp32 (below 2 GB).
Row* AllocateLow(std::size_t bytes) noexcept {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::uintptr_t grain=info.dwAllocationGranularity ? info.dwAllocationGranularity : 0x10000;
    for(std::uintptr_t at=0x10000000;at+bytes<0x7FFF0000;at+=grain) {
        void* page=VirtualAlloc(reinterpret_cast<void*>(at),bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(!page)continue;
        if(reinterpret_cast<std::uintptr_t>(page)+bytes<0x7FFF0000)return static_cast<Row*>(page);
        VirtualFree(page,0,MEM_RELEASE);
    }
    return nullptr;
}

bool SetBound(std::uint8_t rows) noexcept {
    if(rows==bound)return true;
    const unsigned char old1[]={0x41,0x83,0xFC,bound},new1[]={0x41,0x83,0xFC,rows};
    const unsigned char old2[]={0x83,0xF8,bound},new2[]={0x83,0xF8,rows};
    if(!edf::PatchCode(image+kBound1,old1,new1,sizeof(new1)))return false;
    if(!edf::PatchCode(image+kBound2,old2,new2,sizeof(new2))) {
        edf::PatchCode(image+kBound1,new1,old1,sizeof(old1));
        return false;
    }
    bound=rows;
    return true;
}

const unsigned char* StoreHead() noexcept {
    const auto stats=At<const unsigned char*>(image,kStats);
    if(!stats || !Readable(stats+kStore,8))return nullptr;
    return At<const unsigned char*>(stats,kStore);
}

// Before each construction: the 4.1 rows the store holds after the stock ones, the bounds to match.
void* __fastcall CtorHook(void* self,void* arg) {
    __try {
        const unsigned char* head=StoreHead();
        auto readable=[](const void* p,std::size_t n){return Readable(p,n);};
        int rows=killcount::kStockRows;
        for(const wchar_t* name:killcount::kEdf41Counters)
            if(head && killcount::FindInt(head,name,readable))table[rows++]={name,0,{0,0,0},1.0f};
        if(SetBound(static_cast<std::uint8_t>(rows))) {
            if(rows-killcount::kStockRows!=shown41) {
                shown41=rows-killcount::kStockRows;
                Log("KILLCOUNT battle record: %d stock rows + %d of the EDF4.1 port's %zu counters (the others not in "
                    "the store: Mods/ETC/ACHIEVEMENT.SGO without them)",killcount::kStockRows,shown41,
                    std::size(killcount::kEdf41Counters));
            }
        } else if(shown41!=-2) {
            shown41=-2;
            Log("KILLCOUNT battle record: the row bounds are not as left: the stock rows only");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return ctorNext(self,arg);
}

bool AsRead() noexcept {
    __try {
        bool ok=Matches(kCtor,kCtorSig,sizeof(kCtorSig)) && Matches(kRowPointer,kRowPointerSig,sizeof(kRowPointerSig)) &&
                Matches(kBound1,kBound1Sig,sizeof(kBound1Sig)) && Matches(kRowName,kRowNameSig,sizeof(kRowNameSig)) &&
                Matches(kBound2,kBound2Sig,sizeof(kBound2Sig)) && Matches(kRowFormat,kRowFormatSig,sizeof(kRowFormatSig)) &&
                Matches(kStoreUse,kStoreSig,sizeof(kStoreSig));
        for(const std::size_t at:kRowScale)ok=ok && Matches(at,kRowScaleSig,sizeof(kRowScaleSig));
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// One table read moved: the stock bytes and what they become.
struct Site { std::size_t at; const unsigned char* sig; std::size_t size; unsigned char patched[16]; };
// `sig`'s [rbp+r13*8+d] instruction addressing the table's field `field` instead: ModRM (at `modrm`) mod 10 -> 00
// (rm 100, SIB base 101: no base, disp32 = the field's address), the same length.
Site TableRead(std::size_t at,const unsigned char* sig,std::size_t size,std::size_t modrm,std::size_t field) noexcept {
    Site s{at,sig,size,{}};
    std::memcpy(s.patched,sig,size);
    s.patched[modrm]=static_cast<unsigned char>(sig[modrm]&0x3F);
    const auto disp=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table)+field);
    std::memcpy(s.patched+size-4,&disp,4);
    return s;
}
}  // namespace

bool InstallKillCounters() noexcept {
    if(!AsRead()){Log("KILLCOUNT the battle record is not as read: its stock rows only (no EDF4.1 counters shown)");return false;}
    table=AllocateLow(0x1000);
    if(!table){Log("KILLCOUNT no page below 2 GB for the battle record's rows: its stock rows only");return false;}
    int stock=-1;
    __try {stock=killcount::ParseRows(image,kFillBegin,kFillEnd,table,kRows);} __except(EXCEPTION_EXECUTE_HANDLER) {stock=-1;}
    if(stock!=killcount::kStockRows) {
        Log("KILLCOUNT the battle record's fill reads as %d rows, not %d: its stock rows only",stock,killcount::kStockRows);
        VirtualFree(table,0,MEM_RELEASE);table=nullptr;
        return false;
    }
    // The stock rows in the table and the bounds at 90: every read moved is the stock page as it was (even one half
    // done), until the construction is redirected. The table is never freed once a read points at it.
    Site sites[]={TableRead(kRowName,kRowNameSig,sizeof(kRowNameSig),2,offsetof(Row,name)),
                  TableRead(kRowScale[0],kRowScaleSig,sizeof(kRowScaleSig),4,offsetof(Row,scale)),
                  TableRead(kRowScale[1],kRowScaleSig,sizeof(kRowScaleSig),4,offsetof(Row,scale)),
                  TableRead(kRowFormat,kRowFormatSig,sizeof(kRowFormatSig),3,offsetof(Row,format)),
                  {kRowPointer,kRowPointerSig,sizeof(kRowPointerSig),{0x41,0xBE,0,0,0,0,0x90}}};   // mov r14d,imm32; nop
    const auto first=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table)+offsetof(Row,scale));
    std::memcpy(sites[4].patched+2,&first,4);
    std::size_t done=0;
    while(done<std::size(sites) && edf::PatchCode(image+sites[done].at,sites[done].sig,sites[done].patched,sites[done].size))++done;
    if(done<std::size(sites)) {
        const std::size_t failed=sites[done].at;
        while(done--)edf::PatchCode(image+sites[done].at,sites[done].patched,sites[done].sig,sites[done].size);
        Log("KILLCOUNT the battle record's table reads could not all be moved (EDF+%#zx): put back, its stock rows only",
            failed);
        return false;
    }
    bool changed=false;
    ctorNext=reinterpret_cast<CtorFn>(image+kCtor);
    if(!RedirectCall(image+kCtorCall,image+kCtor,reinterpret_cast<void*>(&CtorHook),changed)) {
        Log("KILLCOUNT the battle record's construction (EDF+%#zx) is not the stock call: its stock rows only",kCtorCall);
        return false;
    }
    Log("KILLCOUNT installed: the battle record reads its %d rows from %p (+ up to %zu EDF4.1 counters)",stock,
        static_cast<void*>(table),std::size(killcount::kEdf41Counters));
    return true;
}
}  // namespace crew
