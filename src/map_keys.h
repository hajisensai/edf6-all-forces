// The map's order keys (mapcmd.cpp ReadKeys; the user, 2026-11-27: 「把指挥改成断箭的操作方式吧，全照抄」): Broken Arrow's
// set, each a Windows virtual-key code in the ini (Map<name>Key, kIni; 0: none). The camera keeps WASD and the wheel
// (map.cpp Steer); the selection keeps Shift, Ctrl, the digits and Ctrl+A as they are (not rebound here).
// Included by crew.h (Config::mapKeys).
#pragma once

namespace mapkeys {
enum Key : int {
    attackMove,   // Q: attack-move, its point the next left click
    stop,         // E: every order off, the Shift queue too (STOP)
    reverse,      // R: reverse move (the tanks back onto the point), its point the next left click
    fireMission,  // T: the support picked ([ ]) armed, its point the next left click
    unload,       // U: unload at the next left click's point; U twice: unload where they are
    fastMove,     // F: fast move (no stop to fight on the way), its point the next left click
    holdFire,     // H: hold fire on / off (fire back once hurt)
    sprint,       // Z: the soldiers' dashes unrested, the jets' afterburner
    smoke,        // X: smoke (a unit's own; none has one now: the card shows it unavailable)
    decoy,        // C: decoys (the jets' flares)
    altitude,     // V: the aircraft's flight height high / low
    back,         // B: back to base (a support withdraws, a recruited squad is dismissed)
    subgroup,     // Tab: the next kind of the selection the orders go to (back to all)
    ping1,ping2,ping3,ping4,   // F1-F4: a ping on the map, seen by the room
    count
};
inline constexpr const char* kIni[count]={"MapAttackMoveKey","MapStopKey","MapReverseMoveKey","MapFireMissionKey","MapUnloadKey",
    "MapFastMoveKey","MapHoldFireKey","MapSprintKey","MapSmokeKey","MapDecoyKey","MapAltitudeKey","MapBackKey","MapSubgroupKey",
    "MapPing1Key","MapPing2Key","MapPing3Key","MapPing4Key"};
inline constexpr const wchar_t* kIniW[count]={L"MapAttackMoveKey",L"MapStopKey",L"MapReverseMoveKey",L"MapFireMissionKey",
    L"MapUnloadKey",L"MapFastMoveKey",L"MapHoldFireKey",L"MapSprintKey",L"MapSmokeKey",L"MapDecoyKey",L"MapAltitudeKey",
    L"MapBackKey",L"MapSubgroupKey",L"MapPing1Key",L"MapPing2Key",L"MapPing3Key",L"MapPing4Key"};
struct Keys {
    int vk[count]={'Q','E','R','T','U','F','H','Z','X','C','V','B',0x09,0x70,0x71,0x72,0x73};
    int operator[](Key k) const noexcept { return vk[k]; }
};
}  // namespace mapkeys
