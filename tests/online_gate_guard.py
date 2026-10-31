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


def native_bridge_code(rel: str, code: str) -> str:
    """Remove exact stock ABI replay sites, never a file-wide authority exemption.

    Native retarget and accepted-action hooks must execute the displaced stock
    gate. These are not independent plugin authority decisions. Their original
    bytes/arguments are also covered by the corresponding native audits.
    """
    code = re.sub(r"\{0x7748F0,\{[0-9A-Fa-fxX,\s]+\}\}", "{STOCK_SESSION_SIGNATURE,{}}", code)
    if rel == 'src/enemy_control_deiroi_net.cpp':
        native = body(code, 'bool __fastcall MainNetwork(')
        expected = 'return Fn<NetActiveFn>(0x7748F0)(netObject);'
        if 'NativeActionRole(root,&authority)' in native and expected in native:
            code = code.replace(native, native.replace(expected, 'return STOCK_NETWORK_GATE(netObject);'), 1)
        # The original-call table is data paired with MainNetwork's one detour.
        code = re.sub(r'(constexpr unsigned targets\[\]=\{[^}]*),0x7748F0(\};)', r'\1,STOCK_NETWORK_GATE\2', code)
    elif rel == 'src/enemy_control_target.cpp':
        native = body(code, 'void Notify(')
        expected = 'reinterpret_cast<bool(__fastcall*)(void*)>(image+kOnline)(bytes+0x120)'
        if native.count(expected) == 1 and '(bytes[0x128]&1)' in native and 'network[14]' in native and 'vtable[37]' in native:
            code = code.replace(native, native.replace(expected, 'STOCK_NETWORK_GATE(bytes+0x120)'), 1)
            if 'image+kOnline' not in code:
                code = code.replace('kOnline=0x7748F0;', 'kOnline=STOCK_NETWORK_GATE;', 1)
    elif rel == 'src/enemy_control_weapon_aim.cpp':
        native = body(code, 'bool InstallMeteor(')
        expected = 'Address(b,image+0x7748F0);'
        if native.count(expected) == 1 and 'constexpr unsigned at=0x443A93;' in native and 'Jump(b,image+0x443AA1)' in native and 'BossMeteorPoint' in native:
            code = code.replace(native, native.replace(expected, 'Address(b,STOCK_NETWORK_GATE);'), 1)
    return code


def check_rvas(root: str, files: list[str]) -> None:
    for rel in files:
        code = native_bridge_code(rel, code_only(read(root, rel))).upper()
        for rva in DECISION_RVAS:
            if rva.upper() in code and rel not in ALLOWED_RVA_FILES:
                fail(f'{rel}: calls {rva} itself; ask src/online_authority.h (InSession / OnlineHostOnly / IsOnlineAuthority)')


def check_ride_ai(root: str, files: list[str]) -> None:
    for rel in files:
        if '[kSlotRideAi]' in code_only(read(root, rel)) :
            fail(f'{rel}: calls the stock RideAi itself; seat NPC riders through SeatNpcRider (online_authority.h)')
    seat = body(code_only(read(root, 'src/online_authority.cpp')), 'bool SeatNpcRider(')
    if not before(seat, 'OnlineMaySeatNpc(', 'NpcRequestCrew('):
        fail('src/online_authority.cpp SeatNpcRider: real crew recruitment is not behind OnlineMaySeatNpc')
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
    spare = body(code_only(read(root, 'src/jet_hooks.cpp')), 'bool SparesOwnRide(')
    if not before(spare, 'if(!InSession())return false;', 'Candidate(') or \
            'online::SparesRide(true,IsPlayer(human),target,target)' not in spare:
        fail('src/jet_hooks.cpp SparesOwnRide: not the online-only rule (offline must stay stock)')
    bay = code_only(read(root, 'src/jet_bay.cpp'))
    for fn in ('bool PlayerShell(', 'bool PlayerSideGun('):
        if 'online::Shooter::localPlayer' not in body(bay, fn):
            fail(f'src/jet_bay.cpp {fn[5:-1]}: the player\'s own round is not marked as theirs (nobody would count it)')
    if 'damage,true,gun.name,by)' not in body(bay, 'bool GunShot('):
        fail('src/jet_bay.cpp GunShot: side guns discard the player shot authority')
    # Every copy the plugin makes is recorded with its owner, and a call's copies are its caller's.
    for rel, fn in (('src/jet_spawn.cpp', 'Jet* Launch('), ('src/jet_spawn.cpp', 'unsigned char* PrepareSupportAircraft('),
                    ('src/subcarrier.cpp', 'unsigned char* SubLaunch(')):
        if 'NoteLocalCopy(' not in body(code_only(read(root, rel)), fn):
            fail(f'{rel} {fn}: a copy is made without its owner recorded (its damage would count on the host only)')
    strike = body(code_only(read(root, 'src/airstrike.cpp')), 'bool SupportStrikeAt(')
    human_frame = body(code_only(read(root, 'src/map.cpp')), 'bool __fastcall MapHumanFrame(')
    if human_frame.find('SupportActorFrame(human)') < 0 or human_frame.find('SupportActorFrame(human)') > human_frame.find('IsPlayer(human)'):
        fail('mission support service must advance before filtering local player input')
    # A strike from the map's bar is this machine's player's (its peers play it from its own channel, never call it again).
    if 'unsigned char* const caller=PlayerHuman();' not in strike:
        fail("src/airstrike.cpp SupportStrikeAt: a strike must be called by this machine's player")
    barrage = code_only(read(root, 'src/barrage.cpp'))
    receive = body(barrage, 'bool ReceiveBarrageNetwork(')
    if 'IsPlayer(caller));' not in body(barrage, 'BarrageResult BarrageStart(') or             'if(tell)Tell(' not in body(barrage, 'BarrageResult Start(') or             'SenderOwns(who,puid)' not in receive or ',false);' not in receive:
        fail("src/barrage.cpp: only this machine's player's barrage is told, and a peer's only for its own player")
    support = code_only(read(root, 'src/support_dispatch.cpp'))
    dispatch = body(support, 'bool SpawnDeployment(')
    if 'RegisterSupportObject(vehicle,unit.netId)' not in dispatch or 'HoldSupportSoldier(object,true)' not in dispatch or \
            'NpcBoardCrew(' in dispatch or 'FollowSupportSoldier(' in dispatch:
        fail('support spawn must register native IDs and hold new crew without emitting cross-channel boarding or follow work')
    tick = body(support, 'void SupportDispatchTick(')
    if not before(tick, 'SupportTransactionActive(deployed.id)', 'Assign(deployed)'):
        fail('crew assignment must wait for all-peer spawn acknowledgment')
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
    dispatch = code_only(read(root, 'src/support_dispatch.cpp'))
    # Every map / radio call goes through SupportCallComposedAt (a composed load or none; SupportCallAt only delegates),
    # which refuses the sea rescue's entry and hands the rest to the one gated request path (Request).
    request = body(dispatch, 'bool Request(int index,')
    if 'return SupportCallComposedAt(index,target,nullptr,note,capacity);' not in body(dispatch, 'bool SupportCallAt('):
        fail('SupportCallAt must only delegate to SupportCallComposedAt (one gated path)')
    # (a flown call's sortie second wave rides on it: queued only once the first wave is out, Request's `followUp`)
    composed = body(dispatch, 'bool SupportCallComposedAt(')
    if 'return Request(index,target,load,SupportCallPrice(index),index>=0 && index<SupportAirCallCount(),note,capacity);' not in composed:
        fail('src/support_dispatch.cpp SupportCallComposedAt: not through the request path (Request) for its price')
    # A strike is never planned or deployed by the host: it is started here (SupportStrikeAt), before the online branch.
    if not before(request, 'StrikeCatalog(static_cast<std::uint32_t>(index),strike)', 'else if(InSession())'):
        fail('src/support_dispatch.cpp Request: a strike must start here, never be sent to the host as a deployment')
    # The sea rescue: its own path (no map queue / cooldown), the same host transaction for any non-local machine.
    rescue = body(dispatch, 'bool RescueRequest(')
    if 'RescueRequest(' not in body(dispatch, 'bool SupportRescueAt(') or             not before(rescue, 'InSession() && !LocalAuthority()', 'SubmitSupportRequest('):
        fail('src/support_dispatch.cpp SupportRescueAt: an online rescue does not go through the host-planned protocol')
    if not before(request, 'else if(InSession())', 'SubmitSupportRequest(') or             not before(request, 'SubmitSupportRequest(', 'else if(offlinePending)'):
        fail('all online support must go through the reliable host-planned deployment protocol')
    # Even a one-participant online world uses Session tokens/native IDs. A
    # separate local counter can collide when a dynamic child is created later.
    if 'bool LocalAuthority() noexcept {return !InSession();}' not in dispatch:
        fail('local support deployment must remain strictly offline, including one-participant online worlds')
    spawn = body(dispatch, 'bool SpawnDeployment(')
    if 'unit.matrix' not in spawn or 'PlanAirSupport(' in spawn or 'player.pos' in spawn:
        fail('peers must apply explicit support matrices, never recalculate entry against their own player')
    # No support of the plugin's is a radio call any more (2026-10-26): the stock calls are left wholly native.
    if 'RedirectCall(' in body(code, 'bool InstallAirstrikes()') or 'RadioStartHook' in code or 'SeedSendHook' in code:
        fail("src/airstrike.cpp: a stock radio call is hooked again (the supports are the map bar's)")


def check_session(root: str) -> None:
    session = body(code_only(read(root, 'src/netprobe.cpp')), 'bool InSession()')
    if not before(session, 'if(!SessionCodeKnown(base))return false;', 'At<const unsigned char*>(base,kGameStatus)'):
        fail('src/netprobe.cpp InSession: reject an unknown native profile before reading session state')


# "A player is aboard, leave it to them" asks for a player of any machine (AnyPlayerIn); "this machine's player" (its
# fix, its keys) for Rider::player. Each: (file, function, what must be in it).
ANY_PLAYER_SITES = (
    ('src/crew.cpp', 'void Crew(unsigned char* vehicle,int cls)', 'anyPlayer=anyPlayer || AnyPlayerIn('),
    ('src/crew.cpp', 'void Crew(unsigned char* vehicle,int cls)', 'if(localPlayer)SeePlayer('),
    ('src/heli.cpp', 'void DoorGun(Door& g,const ObjRef& share,bool refill,unsigned char* v,int i,bool hold,float dt,ULONGLONG ms)', 'if(AnyPlayerIn(seat) && !theirs)'),
    ('src/heli.cpp', 'void HeliFrame(unsigned char* vehicle)', 'playerAboard=playerAboard || AnyPlayerIn('),
    ('src/jet.cpp', 'Rider Aboard(unsigned char* v)', 'if(AnyPlayerIn('),
)


def check_any_player(root: str) -> None:
    for rel, signature, need in ANY_PLAYER_SITES:
        if need not in body(code_only(read(root, rel)), signature):
            fail(f'{rel} {signature.split("(")[0]}: lost "{need}" (another machine\'s player aboard is a player too)')


def check_npc_ai(root: str, files: list[str]) -> None:
    for rel in files:
        if re.search(r'\bIsRoomHost\b', code_only(read(root, rel))):
            fail(f'{rel}: IsRoomHost is back; ask OnlineHostOnly (online_authority.h)')
    ai = code_only(read(root, 'src/npcai.cpp'))
    if re.search(r'kNet\)&1|0x128\)&1', ai):
        fail('src/npcai.cpp: a soldier\'s own network bit read for a decision; ask IsOnlineAuthority')
    if re.search(r'!IsPlayer\(|\|\| IsPlayer\(', ai):
        fail('src/npcai.cpp: IsPlayer used to tell an NPC from a player; ask IsAnyPlayer (IsPlayer is this machine\'s only)')
    if not before(body(ai, 'bool DismountSquad('), 'OnlineMaySeatNpc(v)', 'kSeatKick'):
        fail('src/npcai.cpp DismountSquad: riders kicked where NPC riders may not be seated')
    gunner = body(ai, 'void NpcGunnersInput(')
    role = body(ai, 'bool AiGunner(')
    if '|| InSession() ||' in gunner or '|| InSession())return' in role:
        fail('src/npcai.cpp: the offline-only NPC gunner gate is back')
    if not before(gunner, 'ReleaseGunnerInputs(v)', 'if(!ok') or 'if(!AiGunner(v,seat))continue;' not in gunner:
        fail('src/npcai.cpp: NPC gunner inputs bypass their ownership/lifecycle checks')
    if 'OnlineHostOnly()' not in role or 'IsOnlineAuthority(rider)' not in role or '!IsAnyPlayer(rider)' not in role:
        fail('src/npcai.cpp: Dummy/local NPC authority or human-seat exclusion is missing')
    heli = code_only(read(root, 'src/heli.cpp'))
    frame = body(heli, 'void HeliFrame(')
    if not before(frame, 'CrewDoorGuns(vehicle)', 'Replica(vehicle)'):
        fail('src/heli.cpp: remote pilot suppresses local NPC door gunners')
    if 'if(InSession() && !NpcGunnerAimReady())return;' not in body(heli, 'void CrewDoorGuns('):
        fail('src/heli.cpp: online door gunners lack the verified native aim bridge')
    if 'IsOnlineAuthority(vehicle)' not in body(code_only(read(root, 'src/online_authority.cpp')), 'bool VehicleAuthority('):
        fail("src/online_authority.cpp VehicleAuthority: the gunner recoil's authority is not the one rule")


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
              lambda: check_any_player(root), lambda: check_npc_ai(root, files),
              lambda: check_player(root))
    for c in checks:
        c()
    for f in failures:
        print('FAIL:', f)
    print(f'online_gate_guard: {len(files)} files, {len(checks)} checks, {len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
