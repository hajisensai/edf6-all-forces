// The map's order keys (mapcmd.cpp ReadKeys; the user, 2026-11-27: 「把指挥改成断箭的操作方式吧，全照抄」): Broken Arrow's
// set, each a Windows virtual-key code in the ini (Map<name>Key, tools/config_keys.py; 0: none). The camera keeps WASD and the wheel
// (map.cpp Steer); the selection keeps Shift, Ctrl, the digits and Ctrl+A as they are (not rebound here).
// Included by config.h (inside namespace crew); the values are Config members (Of).
#pragma once

namespace mapkeys {
enum Key : int {
    attackMove,   // Q: attack-move, its point the next left click
    stop,         // E: every order off, the Shift queue too (STOP)
    reverse,      // R: reverse move (the tanks back onto the point), its point the next left click
    fireMission,  // T: the support picked ([ ]) armed, its point the next left click
    unload,       // U: unload at the next left click's point; U twice: unload where they are
    // (F fast move and X smoke retired 2026-11-27: a move judges its own pace, mapcmd_logic.h MoveRushes; no unit has smoke)
    holdFire,     // H: hold fire on / off (fire back once hurt)
    sprint,       // Z: the soldiers' dashes unrested, the jets' afterburner
    decoy,        // C: decoys (the jets' flares)
    altitude,     // V: the aircraft's flight height high / low
    back,         // B: back to base (a support withdraws, a recruited squad is dismissed)
    subgroup,     // Tab: the next kind of the selection the orders go to (back to all)
    ping1,ping2,ping3,ping4,   // F1-F4: a ping on the map, seen by the room
    count
};
// The keys as the ini has them (tools/config_keys.py Map*Key, Config::map<Name>Key): Of(Cfg()).
struct Keys {
    int vk[count]{};
    int operator[](Key k) const noexcept { return vk[k]; }
};
template<class C> Keys Of(const C& c) noexcept {
    return Keys{{c.mapAttackMoveKey,c.mapStopKey,c.mapReverseMoveKey,c.mapFireMissionKey,c.mapUnloadKey,
                 c.mapHoldFireKey,c.mapSprintKey,c.mapDecoyKey,c.mapAltitudeKey,c.mapBackKey,c.mapSubgroupKey,
                 c.mapPing1Key,c.mapPing2Key,c.mapPing3Key,c.mapPing4Key}};
}
}  // namespace mapkeys
