"""Source guard of the online gates (src/online_authority.h): python tests/online_gate_guard.py [--root DIR], exit 1 on
a failure. What it holds in place:
  - the session / host / operator functions (0x7748F0, 0x784210, 0x630F90, 0x630DF0) are called for a decision only from
    src/online_authority.cpp (and read by the NET probe, src/netprobe.cpp): no module asks them on its own;
  - the stock RideAi (VehicleBase slot 50) is called only from SeatNpcRider, behind OnlineMaySeatNpc;
  - AutoCrew (crew.cpp Crew) asks OnlineMaySeatNpc before it seats a driver;
  - every plugin damage round (jet_bay.cpp ShellMake: the ram, the drill, the EMC, the gunship's guns, the Proteus) is
    made with no damage except on the one machine where it counts (OnlineShotCounts; the player's own rounds marked,
    every copy's owner recorded, a call's copies its caller's);
  - the NPC heli pilot flies only where the heli is run (heli.cpp HeliFrame's Replica before Fly), puts its input on
    seat 0's stick (Fly -> MirrorStick), and a replica copies that stick back with the same signs;
  - the shield's push, the crawler's NPC driver, the jets' NPC pilot run where the vehicle is run, and a teleportation
    ship's portal laser starts only where the ship is run;
  - the Air Raider's calls: online along the call's own heading (offline as before), the pick sent in the seed and
    decoded only from a call another machine sent;
  - the shield's push, the crawler's NPC driver, the jets' NPC pilot run where the vehicle is run, and a teleportation
    ship's portal laser starts only where the ship is run;
  - the Air Raider's calls: online along the call's own heading (offline as before), the pick sent in the seed and
    decoded only from a call another machine sent;
  - IsPlayer (common/seat.cpp) leaves out another machine's player; the tests of "a player aboard" ask AnyPlayerIn.
"""
from __future__ import annotations

import argparse
import os
import re
import sys

DECISION_RVAS = ('0x7748F0', '0x784210', '0x630F90', '0x630DF0')
ALLOWED_RVA_FILES = {'src/online_authority.cpp', 'src/netprobe.cpp'}
SOURCE_DIRS = ('src', 'common', 'autoturret/src')
failures: list[str] = []


def fail(message: str) -> None:
    failures.append(message)


def code_only(text: str) -> str:
    """The text with // comments cut (enough for this code base: no // inside its string literals the checks read)."""
    return '\n'.join(re.sub(r'//.*', '', line) for line in text.splitlines())


def read(root: str, rel: str) -> str:
    with open(os.path.join(root, rel), encoding='utf-8') as f:
        return f.read()


def body(text: str, signature: str) -> str:
    """The braces-balanced body of the first definition starting with `signature` ('' when not found): a
    declaration (a ; before its {) is passed over."""
    at = text.find(signature)
    while at >= 0:
        open_at = text.find('{', at)
        if open_at < 0:
            return ''
        if ';' not in text[at:open_at]:
            break
        at = text.find(signature, at + 1)
    if at < 0:
        return ''
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[open_at:i + 1]
    return ''


def before(text: str, first: str, then: str) -> bool:
    a, b = text.find(first), text.find(then)
    return a >= 0 and b >= 0 and a < b


def sources(root: str) -> list[str]:
    out: list[str] = []
    for d in SOURCE_DIRS:
        for base, _, files in os.walk(os.path.join(root, d)):
            for name in files:
                if name.endswith(('.cpp', '.h', '.inc')):
                    out.append(os.path.relpath(os.path.join(base, name), root).replace(os.sep, '/'))
    return sorted(out)


def check_rvas(root: str, files: list[str]) -> None:
    for rel in files:
        code = code_only(read(root, rel)).upper()
        for rva in DECISION_RVAS:
            if rva.upper() in code and rel not in ALLOWED_RVA_FILES:
                fail(f'{rel}: calls {rva} itself; ask src/online_authority.h (InSession / OnlineHostOnly / IsOnlineAuthority)')


def check_ride_ai(root: str, files: list[str]) -> None:
    for rel in files:
        if '[kSlotRideAi]' in code_only(read(root, rel)) and rel != 'src/online_authority.cpp':
            fail(f'{rel}: calls the stock RideAi itself; seat NPC riders through SeatNpcRider (online_authority.h)')
    seat = body(code_only(read(root, 'src/online_authority.cpp')), 'bool SeatNpcRider(')
    if not before(seat, 'OnlineMaySeatNpc(', '[kSlotRideAi]'):
        fail('src/online_authority.cpp SeatNpcRider: RideAi is not behind OnlineMaySeatNpc')
    crew = body(code_only(read(root, 'src/crew.cpp')), 'void Crew(unsigned char* vehicle,int cls)')
    if not before(crew, 'OnlineMaySeatNpc(vehicle)', 'SeatNpcRider('):
        fail('src/crew.cpp Crew: AutoCrew seats a driver without asking OnlineMaySeatNpc first')


def check_damage(root: str) -> None:
    make = body(code_only(read(root, 'src/jet_bay.cpp')), 'unsigned char* ShellMake(')
    if not re.search(r'if\(damage>0\.0f && !OnlineShotCounts\(owner,by\)\)damage=0\.0f;', make) or \
            not before(make, 'OnlineShotCounts(owner,by)', 'ShellCreate('):
        fail('src/jet_bay.cpp ShellMake: a damage round is made without the exactly-once gate (OnlineShotCounts)')
    if not before(make, 'OnlineAttacker(owner,by)', 'image+kIfcOwner'):
        fail("src/jet_bay.cpp ShellMake: the round's attacker is not OnlineAttacker's (coop's hit authority would drop it)")
    hook = body(code_only(read(root, 'src/jet_hooks.cpp')), 'void __fastcall AddBodyHook(')
    if not before(hook, 'SparesOwnRide(collector,body)', 'nextAddBody(collector,body)'):
        fail("src/jet_hooks.cpp AddBodyHook: a round named the player is not kept off the vehicle they ride (self-hit)")
    if 'online::SparesRide(InSession()' not in body(code_only(read(root, 'src/jet_hooks.cpp')), 'bool SparesOwnRide('):
        fail('src/jet_hooks.cpp SparesOwnRide: not the online-only rule (offline must stay stock)')
    bay = code_only(read(root, 'src/jet_bay.cpp'))
    for fn in ('bool PlayerShell(', 'bool PlayerCannon('):
        if 'online::Shooter::localPlayer' not in body(bay, fn):
            fail(f'src/jet_bay.cpp {fn[5:-1]}: the player\'s own round is not marked as theirs (nobody would count it)')
    # Every copy the plugin makes is recorded with its owner, and a call's copies are its caller's.
    for rel, fn in (('src/jet_spawn.cpp', 'Jet* Launch('), ('src/jet_spawn.cpp', 'unsigned char* HeliLaunch('),
                    ('src/subcarrier.cpp', 'unsigned char* SubLaunch(')):
        if 'NoteLocalCopy(v,' not in body(code_only(read(root, rel)), fn):
            fail(f'{rel} {fn}: a copy is made without its owner recorded (its damage would count on the host only)')
    radio = body(code_only(read(root, 'src/airstrike.cpp')), 'std::uintptr_t __fastcall RadioStartHook(')
    if not before(radio, 'SetSpawnOwner(CopyOwnerOfCaller(owner))', 'LaunchCall('):
        fail("src/airstrike.cpp RadioStartHook: the call's copies are not made as its caller's")
    for rel, call in (('src/vehicleram.cpp', 'ImpactDamage('), ('src/drill.cpp', 'DrillCharge('), ('src/emc.cpp', 'EmcFire(')):
        if call not in code_only(read(root, rel)):
            fail(f'{rel}: no longer deals its damage through {call} (ShellMake\'s gate): gate the new path too')


def check_heli(root: str) -> None:
    code = code_only(read(root, 'src/heli.cpp'))
    frame = body(code, 'void HeliFrame(unsigned char* vehicle)')
    if not before(frame, 'Replica(vehicle)', 'Fly('):
        fail('src/heli.cpp HeliFrame: the NPC pilot flies before the replica test')
    if 'MirrorStick(v,c);' not in body(code, 'void Fly(Heli& h,unsigned char* v,bool playerAboard)'):
        fail('src/heli.cpp Fly: the pilot\'s input is not put on seat 0\'s stick (MirrorStick)')
    if 'OnlineRunsHere(v)' not in body(code, 'bool Replica(unsigned char* v)'):
        fail('src/heli.cpp Replica: does not ask OnlineRunsHere')
    if not before(body(code, 'void Replay(unsigned char* v)'), 'StickLive(', 'ReplicaOf('):
        fail('src/heli.cpp Replay: a heli with no stick coming in takes a replica record (the stock frame count +0x1D7C cycles)')
    mirror = body(code, 'void MirrorStick(unsigned char* v,const Control& c)')
    replay = body(code, 'void Replay(unsigned char* v)')
    pairs = (('kSeatLX,-c.stickL', 'kInLateral,-SeatAxis(seat,kSeatLX)'), ('kSeatLY,-c.stickF', 'kInForward,-SeatAxis(seat,kSeatLY)'),
             ('kSeatRX,-c.yaw', 'kInYaw,-SeatAxis(seat,kSeatRX)'), ('kSeatAscend,Clamp(c.throttle', 'kInThrottle,Clamp(At<float>(seat,kSeatAscend)'))
    for put, back in pairs:
        if put not in mirror or back not in replay:
            fail(f'src/heli.cpp: MirrorStick ({put}) and Replay ({back}) no longer the stock copy\'s signs')


# Per-frame plugin work on another machine's object: each must ask the gate first (file, function, the gate's call).
FRAME_GATES = (
    ('src/shield.cpp', 'void ShieldVehicle(unsigned char* v)', 'OnlineRunsHere(v)', 'kSetLinVel'),
    ('src/ground.cpp', 'void GroundFrame(unsigned char* vehicle)', 'OnlineRunsHere(vehicle)', 'Drive('),
    ('src/carrierlaser.cpp', 'bool Start(Ship& s,const Carriers& live,ULONGLONG ms)', 'IsOnlineAuthority(s.ship)', 's.carrier='),
    ('src/heli.cpp', 'void HeliFrame(unsigned char* vehicle)', 'OnlineRunsHere(vehicle))JetFrame(', 'JetFrame('),
)


def check_frames(root: str) -> None:
    for rel, signature, gate, work in FRAME_GATES:
        if not before(body(code_only(read(root, rel)), signature), gate, work):
            fail(f'{rel} {signature.split("(")[0]}: {work} runs without {gate} before it')


def check_calls(root: str) -> None:
    code = code_only(read(root, 'src/airstrike.cpp'))
    launch = body(code, 'int LaunchCall(')
    if 'player.' in launch or 'CallDirection(' not in launch:
        fail("src/airstrike.cpp LaunchCall: its direction is not CallDirection's (online: the call's own heading)")
    call_of = body(code, 'const Call* CallOf(')
    remote = re.search(r'if\(InSession\(\) && edf::RemoteRider\(owner\)\) \{(.*?)\}', call_of, re.S)
    if not remote or 'callnet::Decode(' not in remote.group(1) or call_of.count('callnet::Decode(') != 1:
        fail('src/airstrike.cpp CallOf: a pick is decoded outside a call received from another machine')
    direction = body(code, 'void CallDirection(')
    if not before(direction, 'if(InSession())', 'player.'):
        fail("src/airstrike.cpp CallDirection: offline the call no longer comes from behind as the player sees it")
    if 'Put<std::uint64_t>(weapon,kWeaponSeed,sent)' not in body(code, 'bool __fastcall SeedSendHook('):
        fail("src/airstrike.cpp SeedSendHook: the caller does not keep the seed it sent")
    if 'InstallPickSend()' not in body(code, 'bool InstallAirstrikes()'):
        fail("src/airstrike.cpp: the call's pick is no longer sent (InstallPickSend)")


def check_session(root: str) -> None:
    session = body(code_only(read(root, 'src/netprobe.cpp')), 'bool InSession()')
    if 'return sig && ' not in session:
        fail('src/netprobe.cpp InSession: unknown session code counts as online (offline play would lose the gated features)')


# "A player is aboard, leave it to them" asks for a player of any machine (AnyPlayerIn); "this machine's player" (its
# fix, its keys) for Rider::player. Each: (file, function, what must be in it).
ANY_PLAYER_SITES = (
    ('src/crew.cpp', 'void Crew(unsigned char* vehicle,int cls)', 'anyPlayer=anyPlayer || AnyPlayerIn('),
    ('src/crew.cpp', 'void Crew(unsigned char* vehicle,int cls)', 'if(localPlayer)SeePlayer('),
    ('src/heli.cpp', 'void DoorGun(Heli& h,unsigned char* v,int i,bool hold,float dt,ULONGLONG ms)', 'if(AnyPlayerIn(seat))'),
    ('src/heli.cpp', 'void HeliFrame(unsigned char* vehicle)', 'playerAboard=playerAboard || AnyPlayerIn('),
    ('src/jet.cpp', 'Rider Aboard(unsigned char* v)', 'if(AnyPlayerIn('),
)


def check_any_player(root: str) -> None:
    for rel, signature, need in ANY_PLAYER_SITES:
        if need not in body(code_only(read(root, rel)), signature):
            fail(f'{rel} {signature.split("(")[0]}: lost "{need}" (another machine\'s player aboard is a player too)')


def check_player(root: str) -> None:
    if 'RemoteRider(human)' not in body(code_only(read(root, 'common/seat.cpp')), 'bool IsAnyPlayer('):
        fail("common/seat.cpp IsAnyPlayer: another machine's player copied here is no longer a player")
    if '!RemoteRider(human)' not in body(code_only(read(root, 'common/seat.cpp')), 'bool IsPlayer('):
        fail('common/seat.cpp IsPlayer: another machine\'s player counts as this machine\'s')
    if re.search(r'\bkOnline\b', code_only(read(root, 'src/seatswitch.cpp'))):
        fail('src/seatswitch.cpp: its own session check is back; ask InSession')


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')))
    root = parser.parse_args().root
    files = sources(root)
    checks = (lambda: check_rvas(root, files), lambda: check_ride_ai(root, files), lambda: check_damage(root),
              lambda: check_heli(root), lambda: check_frames(root), lambda: check_calls(root), lambda: check_session(root),
              lambda: check_any_player(root), lambda: check_player(root))
    for c in checks:
        c()
    for f in failures:
        print('FAIL:', f)
    print(f'online_gate_guard: {len(files)} files, {len(checks)} checks, {len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
