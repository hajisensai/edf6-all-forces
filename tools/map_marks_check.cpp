// What the map marks of the friendly side (src/map_marks.h) checked offline: team 5 (nobody's vehicles: the parked and
// delivered aircraft, every empty vehicle) is walked besides the friends' walk, which never visits it (the stock board
// prompt walks both the same way: 0x56D768, 0x56D77F); its vehicles and aircraft are marked, flagged empty, its
// non-vehicles not; the plugin's fixed-wing aircraft are aircraft (not vehicles), helicopters aircraft with the rotor's
// flag; an aircraft on the ground stands as a ground unit's pin.
//   cmake --build build --target map_marks_check && build\map_marks_check.exe      (exit code 1 on a failure)
#include "../src/map_marks.h"
#include <cstdio>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s\n",what);
}
using crew::MapKind;
using crew::mapmarks::Seen;
bool Mark(const Seen& o,MapKind* kind,std::uint8_t* flags) { return crew::mapmarks::FriendlyMark(o,kind,flags); }
}  // namespace

int main() {
    namespace mm=crew::mapmarks;
    // The walks: the player's team 0 (and a friend's 2) walk team 5 too; a player on team 5 has it in the friends' walk.
    Check(mm::WalksFor(0).friends && mm::WalksFor(0).nobodys,"team 0: the friends and team 5 walked");
    Check(mm::WalksFor(2).nobodys,"team 2: team 5 walked");
    Check(mm::WalksFor(mm::kNobodysTeam).friends && !mm::WalksFor(mm::kNobodysTeam).nobodys,"team 5's own player: walked once");

    MapKind kind{};
    std::uint8_t flags=0xFF;
    // A parked plugin jet (team 5, nobody in it): an aircraft, flagged empty, no rotor.
    Check(Mark(Seen{true,false,true,false,false,true},&kind,&flags) && kind==MapKind::air && flags==crew::kMapEmpty,
          "parked jet: aircraft, empty");
    // An NPC jet in flight (team 2): an aircraft, not empty.
    Check(Mark(Seen{true,false,true,false,false,false},&kind,&flags) && kind==MapKind::air && flags==0,"NPC jet: aircraft");
    // A plugin aircraft the heli code knows too (IsJet and IsHelicopter both): the plugin's own body decides, a plane.
    Check(Mark(Seen{true,false,true,true,false,false},&kind,&flags) && kind==MapKind::air && !(flags&crew::kMapRotor),
          "plugin aircraft: the plane icon");
    // A stock helicopter: an aircraft with the rotor's flag.
    Check(Mark(Seen{true,false,false,true,false,false},&kind,&flags) && kind==MapKind::air && flags==crew::kMapRotor,
          "stock heli: aircraft, rotor");
    // The carrier first (it is a 506 body like the jets).
    Check(Mark(Seen{true,true,false,true,false,false},&kind,&flags) && kind==MapKind::carrier,"carrier");
    // An empty stock tank (team 5): a vehicle, flagged empty.
    Check(Mark(Seen{true,false,false,false,false,true},&kind,&flags) && kind==MapKind::vehicle && flags==crew::kMapEmpty,
          "empty tank: vehicle, empty");
    // A crewed friendly tank: a vehicle.
    Check(Mark(Seen{true,false,false,false,false,false},&kind,&flags) && kind==MapKind::vehicle && flags==0,"crewed tank");
    // Soldiers: the squad on the player's team, other friends else; none on team 5.
    Check(Mark(Seen{false,false,false,false,true,false},&kind,&flags) && kind==MapKind::squad,"squad");
    Check(Mark(Seen{false,false,false,false,false,false},&kind,&flags) && kind==MapKind::ally,"friendly soldier");
    Check(!Mark(Seen{false,false,false,false,false,true},&kind,&flags),"team 5 non-vehicle: no mark");
    // On the ground or flying.
    Check(mm::Landed(100.0f,98.5f) && mm::Landed(100.0f,100.0f),"parked: landed");
    Check(!mm::Landed(140.0f,100.0f),"40 m up: flying");

    std::printf("map_marks_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
