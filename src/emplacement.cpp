// The fixed gun emplacements (pylib/emplacement_model.py; the user, 2026-10-24: 「再做一类在地图上固定的炮台。这个炮台
// 使用无畏舰上面那几个」): the Kepler's class (Vehicle603_Flak) with the marker bone kMarker in its model. Its turret and
// guns are the native ones (the gunner lays them, the NPC gunners' AI too); it never drives: its driver seat's drive
// stick (seat +0x2C0 steer, +0x2C4 throttle: npcpost.cpp kSeatSteer / kSeatThrottle) is cleared every frame just before
// the stock input reads it (crew.cpp InputHook, after every plugin step that writes a stick), whoever sits there: a
// player, an NPC, the stock AI. Online the driver's machine runs that input: every machine clears it alike.
#include "crew.h"
#include "body506.h"
#include "layout.h"
#include "memory.h"

namespace crew {
namespace {
constexpr unsigned kVt603=0x17DC620;          // Vehicle603_Flak's vtable (vulcan_rotor.cpp kVt603)
constexpr std::size_t kSeatSteer=0x2C0;       // the left stick: steer, then throttle (+4)
constexpr wchar_t kMarker[]=L"edf6vc_emplacement";   // pylib/emplacement_model.py MARKER
}  // namespace

bool IsEmplacement(const unsigned char* v) noexcept {
    if(!v || !Readable(v,kModelInst506+kInstBones506+8) || At<const void*>(v,0)!=image+kVt603)return false;
    return BoneRecord506(v+kModelInst506,kMarker)!=nullptr;
}

void EmplacementInput(unsigned char* v) noexcept {
    if(v[kDead] || !IsEmplacement(v) || SeatCount(v)<1)return;
    unsigned char* const driver=SeatAt(v,0);
    if(!Readable(driver+kSeatSteer,8,true))return;
    Put<float>(driver,kSeatSteer,0.0f);
    Put<float>(driver,kSeatSteer+4,0.0f);
}
}  // namespace crew
