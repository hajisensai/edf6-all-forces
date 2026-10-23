// The stock crosshair's draw (infantryscope.cpp CrossDrawHook, the production hook) under the plugin's own scopes: held
// back for the soldier whose vehicle sight field hud.cpp drew (HudScopeHoldsCrosshair, scope_hold.h; the user,
// 2026-10-21: "进入瞄具状态的时候没隐藏准星"), as for a soldier's own open scope; drawn for everyone else.
#include "../src/infantryscope.cpp"
#include "../src/scope_hold.h"
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
namespace {
Config config{};
int checks=0,failures=0,drawn=0;
const void* scopeSoldier=nullptr;   // HudScopeHoldsCrosshair's answer: the soldier HudDraw drew the field for
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
void __fastcall Original(void*,void*,void*){++drawn;}
alignas(16) unsigned char crosshair[kCrossHuman+0x40]{};
unsigned char soldier[0x100]{},other[0x100]{};
// One draw of the crosshair for `who`: whether the stock draw ran.
bool Draws(const void* who) {
    Put<const void*>(crosshair,kCrossHuman,who);
    const int before=drawn;
    CrossDrawHook(crosshair,nullptr,nullptr);
    return drawn==before+1;
}
}
const Config& Cfg() noexcept{return config;}
void Log(const char*,...) noexcept{}
unsigned char* PlayerHuman() noexcept{return soldier;}
bool HumanOnFoot(const unsigned char*) noexcept{return true;}
bool ReadRound(const unsigned char*,RoundModel* r) noexcept{*r=RoundModel{};return false;}
bool HudScopeHoldsCrosshair(const void* who) noexcept{return who && who==scopeSoldier;}
}
bool Matches(unsigned,const unsigned char*,std::size_t) noexcept{return false;}

int wmain() {
    using namespace crew;
    nextCrossDraw=&Original;
    Check(Draws(soldier) && Draws(other),"no scope: the crosshair for everyone");
    scopeSoldier=soldier;
    Check(!Draws(soldier),"the vehicle sight's field drawn for this soldier: its crosshair held back");
    Check(Draws(other),"another soldier's crosshair (a split screen's 2P) stays");
    Check(Draws(nullptr),"a crosshair for no soldier is the stock draw's own business");
    scopeSoldier=nullptr;
    Check(Draws(soldier),"the field gone: the crosshair is back");
    // A soldier's own open scope (InfantryScopeNow) still holds it back, as before.
    config.enabled=config.infantryScope=true;
    Publish(Published{ObjRef::Of(soldier),InfantryScopeCue{soldier,4.0f,scope::Look::marksman},GetTickCount64()});
    Check(!Draws(soldier) && Draws(other),"an infantry scope holds back its own soldier's crosshair only");
    Publish(Published{});
    Check(Draws(soldier),"the infantry scope closed: the crosshair is back");
    // scope_hold.h: the published field holds the crosshair back while fresh and for its own soldier only.
    const scopehold::Drawn d{soldier,1000};
    Check(scopehold::Holds(d,soldier,1000) && scopehold::Holds(d,soldier,1000+scopehold::kHeldMs),"fresh: held");
    Check(!scopehold::Holds(d,soldier,1001+scopehold::kHeldMs),"stale (the HUD stopped drawing the field): drawn again");
    Check(!scopehold::Holds(d,other,1000) && !scopehold::Holds(scopehold::Drawn{},soldier,0),"another soldier, or none drawn: drawn");
    std::printf("scope_crosshair: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
