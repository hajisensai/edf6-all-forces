// The damage numbers (README 伤害数字, docs/damage-numbers-re.md; the user, 2026-11-10: 「屏幕上显示 NPC 和援军打出的伤害数字及
// 弱点、误射颜色。我的意思是想加上弱点，误射的伤害颜色」). The stock writes the number's two colours into its damage notice at
// one place, 0x54820F, inside the damage function 0x547C30 (dmgnum.h); a jump there to a shim calls Hit, which writes
// them itself: the stock white, or the weak spot / friendly fire colour from the damage function's own two facts (its
// friendly fire flag, its hit part multiplier). An NPC's or a support unit's hit, which the stock shows on nobody's
// screen, is handed with the local player as its attacker to the stock player path 0x54AAC0, the way the stock hands
// a vehicle's hit to its rider (0x62F4F0). Everything stays in the stock's own notice and HUD: the number's place,
// size, merging and fading are the game's.
#include "crew.h"
#include "dmgnum.h"
#include "memory.h"

namespace crew {
namespace {
// EDF.dll (TimeDateStamp 0x678CCB46, docs/damage-numbers-re.md).
constexpr unsigned kColorStores=0x54820F,kColorBack=0x548225,kPlayerNotice=0x54AAC0;
// 0x54820F: movaps xmm0,[0x1765B90]; movaps [rbp+20h],xmm0; movaps xmm1,[0x1765B30]; movaps [rbp+30h],xmm1.
const unsigned char kColorBytes[]={0x0F,0x28,0x05,0x7A,0xD9,0x21,0x01,0x0F,0x29,0x45,0x20,
                                   0x0F,0x28,0x0D,0x0F,0xD9,0x21,0x01,0x0F,0x29,0x4D,0x30};
// What the shim relies on, each the stock's: the attacker tested (0x54820A test rcx,rcx; je), the notice stored and
// sent after it (0x548225: [rbp]=rcx attacker, [rbp+8]=rdi victim, [rbp+10h]=r13 GDI, [rbp+18h]=xmm6 change; call
// [rax+0A8h] with rdx=rbp), the friendly fire flag at [rsp+20h] (0x547DE6 sets it, 0x5483CD reads it), the part
// multiplier at [rsp+44h] (0x548148 reads it), and 0x54AAC0's head (the player path, called directly).
struct Sig { unsigned rva; unsigned char bytes[32]; std::size_t size; };
const Sig kSigs[]={
    {0x54820A,{0x48,0x85,0xC9,0x74,0x35},5},
    {0x548225,{0x48,0x89,0x4D,0x00,0x48,0x89,0x7D,0x08,0x4C,0x89,0x6D,0x10,0xF3,0x0F,0x11,0x75,0x18,0x48,0x8B,0x01,
               0x48,0x8D,0x55,0x00,0xFF,0x90,0xA8,0x00,0x00,0x00},30},
    {0x547DE6,{0xC6,0x44,0x24,0x20,0x01},5},
    {0x5483CD,{0x80,0x7C,0x24,0x20,0x00},5},
    {0x548148,{0xF3,0x0F,0x10,0x44,0x24,0x44},6},
    {kPlayerNotice,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x41,0x56,0x48,0x83,0xEC,0x30},21},
};
constexpr std::size_t kFrameFriendly=0x20,kFramePart=0x44;   // in the damage function's frame (its rsp)
constexpr std::size_t kNoticeFill=0x20,kNoticeSecond=0x30,kNoticeSize=0x40,kPlayerInput=0x340;

struct alignas(16) Notice { void* attacker; void* victim; void* gdi; float change; float pad; dmgnum::Rgba fill,second; };
static_assert(sizeof(Notice)==kNoticeSize && offsetof(Notice,fill)==kNoticeFill && offsetof(Notice,second)==kNoticeSecond,
              "the stock notice (0x548206..0x548231)");
using PlayerNoticeFn=void(__fastcall*)(void*,Notice*);
std::uint32_t allyShown=0,tinted=0;   // game thread: counted for the log

dmgnum::Colors ColorsNow() noexcept {
    const Config& c=Cfg();
    dmgnum::Colors k;
    k.tint=c.enabled && c.damageNumberColors;
    k.weak=static_cast<std::uint32_t>(c.damageNumberWeakColor);
    k.friendly=static_cast<std::uint32_t>(c.damageNumberFriendlyColor);
    k.ally=static_cast<std::uint32_t>(c.damageNumberAllyColor);
    return k;
}

void Store(unsigned char* notice,const dmgnum::Rgba& fill) noexcept {
    std::memcpy(notice+kNoticeFill,&fill,sizeof(fill));
    std::memcpy(notice+kNoticeSecond,&dmgnum::kStockSecond,sizeof(dmgnum::kStockSecond));
}

// The shim's call (dmgnum.h kStub), on the damage function's thread, in its frame: write the notice's colours, and
// hand an ally's hit to the player's HUD.
void __fastcall Hit(unsigned char* notice,unsigned char* victim,unsigned char* gdi,const unsigned char* frame,
                    unsigned char* attacker,float change) noexcept {
    Store(notice,dmgnum::kStockFill);   // the stock's, whatever happens below
    __try {
        const dmgnum::Colors colors=ColorsNow();
        const dmgnum::Kind kind=dmgnum::Classify(frame[kFrameFriendly]!=0,At<float>(frame,kFramePart));
        Store(notice,dmgnum::Fill(colors,kind,false));
        if(kind!=dmgnum::Kind::normal && colors.tint)++tinted;
        const Config& c=Cfg();
        if(!c.enabled || !c.damageNumberAllies || !dmgnum::WorthShowing(change))return;
        unsigned char* const me=PlayerHuman();
        if(!me || me==attacker || !Readable(me,kPlayerInput+8) || !At<void*>(me,kPlayerInput))return;
        if(!dmgnum::AllyNumber(AttackerSide(attacker)))return;
        Notice n{me,victim,gdi,change,0.0f,dmgnum::Fill(colors,kind,true),dmgnum::kStockSecond};
        reinterpret_cast<PlayerNoticeFn>(image+kPlayerNotice)(me,&n);
        ++allyShown;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
}  // namespace

bool InstallDamageNumbers() noexcept {
    __try {
        for(const Sig& s:kSigs)
            if(!Matches(s.rva,s.bytes,s.size)) {
                Log("HOOK damageNumbers: EDF.dll+0x%X is not the stock bytes: no weak spot / friendly fire colours, no allies' numbers",s.rva);
                return false;
            }
        unsigned char stub[sizeof(dmgnum::kStub)];
        std::memcpy(stub,dmgnum::kStub,sizeof(stub));
        const auto hit=reinterpret_cast<std::uintptr_t>(&Hit);
        const auto back=reinterpret_cast<std::uintptr_t>(image+kColorBack);
        std::memcpy(stub+dmgnum::kStubHook,&hit,8);
        std::memcpy(stub+dmgnum::kStubBack,&back,8);
        void* const code=edf::AllocateNearCode(image+kColorStores,stub,sizeof(stub));
        if(!code){Log("HOOK damageNumbers: no code page near EDF.dll: off");return false;}
        // jmp rel32 to the shim, the rest of the 22 bytes int3 (never run: the shim returns past them).
        unsigned char patch[sizeof(kColorBytes)];
        std::memset(patch,0xCC,sizeof(patch));
        patch[0]=0xE9;
        const auto rel=static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(code))-
                       static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(image+kColorStores+5));
        if(rel<INT32_MIN || rel>INT32_MAX){VirtualFree(code,0,MEM_RELEASE);Log("HOOK damageNumbers: the shim is out of reach: off");return false;}
        const auto rel32=static_cast<std::int32_t>(rel);
        std::memcpy(patch+1,&rel32,4);
        if(!edf::PatchCode(image+kColorStores,kColorBytes,patch,sizeof(patch))) {
            VirtualFree(code,0,MEM_RELEASE);
            Log("HOOK damageNumbers: EDF.dll+0x%X could not be patched: off",kColorStores);
            return false;
        }
        Log("HOOK damageNumbers colours at EDF.dll+0x%X, allies' numbers through EDF.dll+0x%X",kColorStores,kPlayerNotice);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
