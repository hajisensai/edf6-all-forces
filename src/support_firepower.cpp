// See support_firepower.h. EDF.dll TimeDateStamp 0x678CCB46, RVAs.
#include "support_firepower.h"
#include "crew.h"
#include "memory.h"

namespace crew {
namespace {
// GameStatus *(image+kGameStatus): +0x4C the difficulty (0xD8850 mov ebx,[rcx+4Ch], the cap's argument), and 0xD7B60(GS)
// the mission's place in its list, 0..1 (+0x48 / (rows - 1), 0 with one row: docs/weapon-level-cap-re.md §1).
constexpr std::size_t kGameStatus=0x20B2890,kDifficulty=0x4C;
constexpr unsigned kProgress=0xD7B60;
const unsigned char kProgressSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x48,0x81,0xC1,0x30,0x01,0x00,0x00};
using ProgressFn=float(__fastcall*)(void*);
// The vehicle (support_firepower.h): +0x678 its weapons' damage multiplier, +0x67C its unlevelled base; its weapon holders
// +0x638 (count +0x648), 0x48 apart, each's damage multiplier at +0x40 (0x633936 movss [rsi+rdi*8+40h],xmm6, rdi = i * 9).
constexpr std::size_t kDamage=0x678,kBase=0x67C,kHolders=0x638,kHolderCount=0x648,kHolderStride=0x48,kHolderDamage=0x40;
constexpr std::size_t kMaxHolders=256;

struct Mark { ObjRef ref; supportlevel::Curve curve; };
constexpr unsigned kMarks=128;
Mark marks[kMarks]{};
bool ready=false;

bool Live(const ObjRef& r) noexcept {
    __try {
        return r.obj && r.ctrl && Readable(r.ctrl,0x10) && At<long>(r.ctrl,8)>0 && r.Is(r.obj);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
const Mark* Find(const unsigned char* v) noexcept {
    for(const auto& m:marks) {
        if(!m.ref || m.ref.obj!=v)continue;
        __try { if(m.ref.Is(v))return &m; } __except(EXCEPTION_EXECUTE_HANDLER){}
    }
    return nullptr;
}
}  // namespace

bool InstallSupportFirepower() noexcept {
    ready=image && Matches(kProgress,kProgressSig,sizeof(kProgressSig));
    if(!ready)Log("SUPPORT firepower: unexpected EDF.dll progress code: support vehicles and aircraft are not deployed");
    return ready;
}

void ResetSupportFirepower() noexcept {
    for(auto& m:marks)m=Mark{};
}

bool MarkSupportFirepower(unsigned char* v,supportlevel::Curve curve) noexcept {
    if(!ready || !v)return false;
    const ObjRef ref=ObjRef::Of(v);
    if(!Live(ref))return false;
    Mark* slot=nullptr;
    for(auto& m:marks) {
        if(m.ref.obj==v){slot=&m;break;}   // the same address again: a new object there replaces the old mark
        if(!slot && (!m.ref || !Live(m.ref)))slot=&m;
    }
    if(!slot){Log("SUPPORT firepower: %u vehicles marked already: %p refused",kMarks,v);return false;}
    *slot=Mark{ref,curve};
    return true;
}

bool ApplySupportFirepower(unsigned char* v) noexcept {
    const Mark* const m=v ? Find(v) : nullptr;
    if(!m)return true;
    __try {
        unsigned char* const gs=At<unsigned char*>(image,kGameStatus);
        if(!gs || !Readable(gs,kDifficulty+4)){Log("SUPPORT firepower v=%p: no game status",v);return false;}
        const std::int32_t difficulty=At<std::int32_t>(gs,kDifficulty);
        const float progress=reinterpret_cast<ProgressFn>(image+kProgress)(gs);
        const int level=supportlevel::SupportLevel(difficulty,progress);
        if(level<0){Log("SUPPORT firepower v=%p: difficulty %d progress %.3f has no level",v,difficulty,progress);return false;}
        const float multiplier=supportlevel::LevelDamage(m->curve,level);
        const float was=At<float>(v,kDamage);
        const float damage=multiplier*At<float>(v,kBase);
        auto* const holders=At<unsigned char*>(v,kHolders);
        const auto count=At<std::size_t>(v,kHolderCount);
        if(count && (!holders || count>kMaxHolders || !Readable(holders,count*kHolderStride))) {
            Log("SUPPORT firepower v=%p: %zu weapon holders at %p unreadable",v,count,holders);return false;
        }
        Put<float>(v,kDamage,damage);
        for(std::size_t i=0;i<count;++i)Put<float>(holders+i*kHolderStride,kHolderDamage,damage);
        Log("SUPPORT firepower v=%p curve=%s difficulty=%d progress=%.3f level=%d: damage x%.3f (the game's x%.3f), %zu weapons",
            v,supportlevel::CurveName(m->curve),difficulty,progress,level,damage,was,count);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("SUPPORT firepower v=%p: fault",v);return false;
    }
}
}  // namespace crew
