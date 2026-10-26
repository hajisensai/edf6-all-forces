// The map's support pt (support_points.h): the local player's team score read as the stock pt reload reads it
// (support_points_logic.h: EDF.dll 0x693EAC..0x693F1E), one pool for this machine.
#include "support_points.h"
#include "support_points_logic.h"
#include "crew.h"
#include "memory.h"

namespace crew {
namespace {
// The team manager (*(image+0x20B2978)): its rows (+0x38, 0x38 bytes a row), a row's score (+0x30); the soldier's team
// (+0x314). The stock reload's read (0x693EAC..0x693ED3) checked against these at the first read.
constexpr unsigned kTeamManager=0x20B2978,kReloadRead=0x693EAC;
constexpr std::size_t kTeamRows=0x38,kTeamStride=0x38,kTeamScore=0x30,kTeamField=0x314;
// mov rax,[rsi+120h]; movsxd rcx,dword [rax+314h]; cmp ecx,-1; je; cmp ecx,7; jge; mov rdx,[rip+..]; imul rcx,rcx,38h;
// mov rax,[rdx+38h]; movss xmm6,[rcx+rax+30h]
const unsigned char kReloadReadSig[]={0x48,0x8B,0x86,0x20,0x01,0x00,0x00,0x48,0x63,0x88,0x14,0x03,0x00,0x00,0x83,0xF9,0xFF,
                                      0x74,0x1A,0x83,0xF9,0x07,0x7D,0x15,0x48,0x8B,0x15};
const unsigned char kReloadScaleSig[]={0x48,0x6B,0xC9,0x38,0x48,0x8B,0x42,0x38,0xF3,0x0F,0x10,0x74,0x01,0x30};

support_points::Pool pool;
support_points::Ledger ledger;
int checked=0;              // 0 not yet, 1 the stock read is as expected, -1 it is not (no pt are earned)
std::int32_t lastTeam=-1;   // the player's team when last seen (a dead player keeps earning for it)
ULONGLONG loggedAt=0;
std::int32_t loggedPoints=-1;

support_points::Settings Settings() noexcept {
    const Config& c=Cfg();
    support_points::Settings s;
    s.start=c.supportPtStart;s.most=c.supportPtMost;s.scale=c.supportPtScale;
    return s;
}
bool Checked() noexcept {
    if(!checked) {
        checked=Matches(kReloadRead,kReloadReadSig,sizeof(kReloadReadSig)) &&
                Matches(kReloadRead+0x1F,kReloadScaleSig,sizeof(kReloadScaleSig)) ? 1 : -1;
        Log("SUPPORT PT the stock pt reload's team score read: %s",checked>0 ? "as expected" :
            "profile mismatch (no pt are earned; the pool keeps its start)");
    }
    return checked>0;
}
// The score of `team` (its row's +0x30), false when the manager or the row is not readable.
bool TeamScore(std::int32_t team,float* out) noexcept {
    if(team<0 || team>=support_points::kTeams)return false;
    const auto manager=At<const unsigned char*>(image,kTeamManager);
    if(!manager || !Readable(manager+kTeamRows,8))return false;
    const auto rows=At<const unsigned char*>(manager,kTeamRows);
    const unsigned char* const row=rows ? rows+static_cast<std::size_t>(team)*kTeamStride : nullptr;
    if(!row || !Readable(row+kTeamScore,4))return false;
    *out=At<float>(row,kTeamScore);
    return true;
}
}  // namespace

void SupportPointsReset() noexcept {
    pool.Start(Settings());
    ledger.Clear();
    lastTeam=-1;loggedPoints=-1;
    Log("SUPPORT PT a new mission: %d pt (most %d, prices x%.2f)",pool.points,Cfg().supportPtMost,Cfg().supportPtScale);
}

void SupportPointsTick() noexcept {
    if(!Checked())return;
    __try {
        if(const unsigned char* const human=PlayerHuman(); human && Readable(human+kTeamField,4))lastTeam=At<std::int32_t>(human,kTeamField);
        float score=0.0f;
        if(!TeamScore(lastTeam,&score))return;
        pool.Take(lastTeam,score,Settings());
    } __except(EXCEPTION_EXECUTE_HANDLER){return;}
    const ULONGLONG now=GetTickCount64();
    if(Cfg().debug && pool.points!=loggedPoints && now-loggedAt>=5000) {
        loggedAt=now;loggedPoints=pool.points;
        Log("SUPPORT PT %d pt (team %d score %.0f; this mission earned %lld, spent %lld, back %lld)",pool.points,pool.team,pool.seen,
            static_cast<long long>(pool.earned),static_cast<long long>(pool.spent),static_cast<long long>(pool.refunded));
    }
}

std::int32_t SupportPoints() noexcept { return pool.points; }
std::int32_t SupportPointsMost() noexcept { return Cfg().supportPtMost; }
std::int32_t SupportPointsPrice(std::uint32_t pt) noexcept { return support_points::Price(pt,Cfg().supportPtScale); }

bool SupportPointsSpend(std::int32_t price) noexcept {
    if(!pool.Spend(price))return false;
    if(price>0)Log("SUPPORT PT spent %d (%d left)",price,pool.points);
    return true;
}
void SupportPointsRefund(std::int32_t price) noexcept {
    if(price<=0)return;
    pool.Refund(price,Settings());
    Log("SUPPORT PT the call came to nothing: %d back (%d now)",price,pool.points);
}
bool SupportPointsOwe(std::int32_t price,std::uint32_t request) noexcept {
    if(!SupportPointsSpend(price))return false;
    if(price>0 && !ledger.Keep(request,price))Log("SUPPORT PT more than %d calls owed at once: the oldest one's pt stay spent",
                                                  support_points::kOwedMost);
    return true;
}
void SupportPointsSettle(std::uint32_t request,bool back) noexcept {
    const std::int32_t price=ledger.Settle(request);
    if(price>0 && back)SupportPointsRefund(price);
}
}  // namespace crew
