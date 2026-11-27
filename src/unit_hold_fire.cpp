// HOLD FIRE's table (mapcmd.h SetUnitHoldFire / UnitHoldsFire; mapcmd_logic.h HoldsFire): the units the map's H switched
// to it, by identity, with their health as last seen and when they were last hurt. Its own unit so every module that
// fires for a unit (npcai.cpp, heli.cpp, jet.cpp, ground.cpp) links it without the map's whole command layer.
#include <windows.h>
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "mapcmd.h"
#include "mapcmd_logic.h"

namespace crew {
namespace {
struct HoldFireEntry { ObjRef ref{}; ULONGLONG hurtAt=0; float hpWas=0.0f; };
constexpr int kHoldFireUnits=96;
HoldFireEntry holdFire[kHoldFireUnits]{};
HoldFireEntry* HoldFireOf(const void* unit) noexcept {
    if(!unit)return nullptr;
    for(auto& h:holdFire)if(h.ref.obj==unit && h.ref.Is(unit))return &h;
    return nullptr;
}
float HpOf(const void* unit) noexcept {
    __try {
        const auto* o=static_cast<const unsigned char*>(unit);
        return Readable(o,kHp+sizeof(float)) ? *reinterpret_cast<const float*>(o+kHp) : 0.0f;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0.0f;}
}
}  // namespace

void SetUnitHoldFire(const void* unit,bool on) noexcept {
    if(!unit)return;
    HoldFireEntry* h=HoldFireOf(unit);
    if(!on){if(h)*h=HoldFireEntry{};return;}
    if(!h)for(auto& e:holdFire)if(!e.ref.obj || !e.ref.Is(e.ref.obj)){h=&e;break;}   // a free one (or one whose unit is gone)
    if(!h){Log("MAPCMD hold fire: the table's %d units full, %p not added",kHoldFireUnits,unit);return;}
    *h=HoldFireEntry{ObjRef::Of(unit),0,HpOf(unit)};
}
bool UnitHoldFire(const void* unit) noexcept { return HoldFireOf(unit)!=nullptr; }
void UnitStruck(const void* unit) noexcept { if(HoldFireEntry* h=HoldFireOf(unit))h->hurtAt=GetTickCount64(); }
bool UnitHoldsFire(const void* unit) noexcept {
    HoldFireEntry* h=HoldFireOf(unit);
    if(!h)return false;
    const float hp=HpOf(unit);
    if(mapcmd::Hurt(h->hpWas,hp))h->hurtAt=GetTickCount64();
    h->hpWas=hp;
    return mapcmd::HoldsFire(true,h->hurtAt ? GetTickCount64()-h->hurtAt : ~0ull);
}
void ClearUnitHoldFire() noexcept { for(auto& h:holdFire)h=HoldFireEntry{}; }
}  // namespace crew
