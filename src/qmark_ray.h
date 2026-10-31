// The ray the Q mark is cast along (qmark.cpp; the user, 2026-10-10: "这个q应该在鼠标位置，如果是鼠标位置和视角不同的情况").
// From the eye of the camera drawn for the player, through the mouse's aim point where the player's mode keeps one apart
// from the view: the mouse-aim flight of a stock heli and of the plugin's rotor craft (the cyan FLY square, heli.cpp
// AimFly / playerjet.cpp AimSteer) and the fighters' mouse aim (the square AimMarks draws). Each is a world point the HUD
// draws its square on (800 m out along the aim), so the ray through it crosses the screen where the square is.
// Everywhere else (on foot, a gunner's seat, the turret camera: there the mouse turns the view itself) the screen's
// centre. Pure arithmetic; tools-free check in tests/qmark_test.cpp.
#pragma once
#include <cmath>

namespace crew::qmark {
// `dir`: the unit direction from `eye` through `aim` when there is one in front of the camera (within 89 degrees of
// `centre`, more than a metre off), else `centre` normalized. False: neither is a direction.
inline bool AimRay(const float* eye,const float* centre,const float* aim,float* dir) noexcept {
    const float cl=std::sqrt(centre[0]*centre[0]+centre[1]*centre[1]+centre[2]*centre[2]);
    if(!(cl>1e-6f) || !std::isfinite(cl))return false;
    const float c[3]={centre[0]/cl,centre[1]/cl,centre[2]/cl};
    if(aim && std::isfinite(aim[0]+aim[1]+aim[2])) {
        const float d[3]={aim[0]-eye[0],aim[1]-eye[1],aim[2]-eye[2]};
        const float l=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(l>1.0f && (d[0]*c[0]+d[1]*c[1]+d[2]*c[2])/l>0.0175f) {
            for(int i=0;i<3;++i)dir[i]=d[i]/l;
            return true;
        }
    }
    for(int i=0;i<3;++i)dir[i]=c[i];
    return true;
}

// The custom Q in a turret seat of EDF6AutoTurret's (aimlink.h TurretLockV1; the user, 2026-10-25: "载具上按q……提示对准
// 敌人才能。但是我明明对准了"): one pick, the lock's rule (the first enemy in sight within LockCone), auto-aim on or off.
// The mark's own pick (the screen's centre, NpcMarkCone 8 deg to the lock point, no line of sight) said "aim at the enemy"
// over an enemy the lock had just boxed. The press is the turret lock's when that plugin's auto-aim is on, the player on
// the keyboard and its lock key the very key the mark is on (`lockOwns`); a pad's lock is its button, Q stays the mark's.
inline bool KeyIsTurretLock(int markKey,bool lockOwns,int lockKey) noexcept { return markKey>0 && lockOwns && lockKey==markKey; }
// What a press of the mark key does in such a seat: with an enemy in sight (`pick`) the lock's press takes it (the mark
// follows: MarkLock) or the mark takes it (the same one again: let go); with none, the ground point and the selected
// squads sent there, as on foot. Never the "aim at the enemy" word.
enum class TurretPress { none, toggle, point };
inline TurretPress TurretMarkPress(bool lockOwns,bool pick) noexcept {
    return !pick ? TurretPress::point : lockOwns ? TurretPress::none : TurretPress::toggle;
}
// ...and the mark follows the lock: a lock new since the one last followed (`followed`), not already the mark, is marked.
inline bool MarkLock(const void* lock,const void* followed,bool marked) noexcept { return lock && lock!=followed && !marked; }
// A press of the mark key by the seat the player holds (plan-2026-10-25-keybinds.md §3.3 B / C):
//   - a Dreadnought's bridge on the keyboard whose strafe keys include the mark key (`bridgeClaims`): the bridge holds
//     the key (Q / E strafe, as the map's Q / E turn), the mark yields: nothing;
//   - at the stick of one of the plugin's aircraft (`atStick`): the same key is its target key (the next target), so the
//     press waits for the lock (markLock: JetMarkDue), never lets a mark go and never sends to a ground point (a miss at
//     its speed is nearly always a slip);
//   - anywhere else: the press as ever (an enemy toggled, else the ground point).
enum class SeatPress { normal, none, markLock };
inline SeatPress SeatMarkPress(bool bridgeClaims,bool atStick) noexcept {
    return bridgeClaims ? SeatPress::none : atStick ? SeatPress::markLock : SeatPress::normal;
}
// A press at an aircraft's stick waiting (kJetMarkWaitMs at most): what to mark now. `from` the target locked at the
// press (the one the target key cycles away from), `locked` the lock now, `expired` the wait over: the new lock once it
// is there; at the wait's end the lock it kept (one target in the cone: the cycle came back to it); nothing while there
// is no lock or it is already the mark (`marked`).
constexpr unsigned kJetMarkWaitMs=2500;

// The mark key held (the user, 2026-10-31: "长按q应该能取消标记"), as EDF6AutoTurret's lock key (designate.cpp,
// LockClearMs 600): a tap acts on its release; held kMarkClearMs it clears this machine's player's marks at that moment
// and its release does nothing. `start`: a press may begin now (the map shut and not holding the keys, the mark on, the
// seat not one that keeps the key: a Dreadnought's bridge); `keep`: a press held may go on (the map opened or the mark
// turned off mid-press: it is dropped, neither a tap nor a hold). `now` in ms.
constexpr unsigned kMarkClearMs=600;
enum class MarkPress { none, tap, hold };
struct MarkKey { bool down,armed; unsigned long long at; };
inline MarkPress StepMarkKey(MarkKey& k,bool down,bool start,bool keep,unsigned long long now) noexcept {
    MarkPress r=MarkPress::none;
    if(down && !k.down){k.armed=start;k.at=now;}
    else if(down && k.armed && !keep)k.armed=false;
    else if(down && k.armed && now-k.at>=kMarkClearMs){k.armed=false;r=MarkPress::hold;}
    else if(!down && k.down && k.armed){k.armed=false;r=keep ? MarkPress::tap : MarkPress::none;}
    k.down=down;
    return r;
}
inline const void* JetMarkDue(const void* from,const void* locked,bool expired,bool marked) noexcept {
    if(!locked || marked)return nullptr;
    return locked!=from || expired ? locked : nullptr;
}
}  // namespace crew::qmark
