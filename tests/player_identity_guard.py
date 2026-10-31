"""Source guard of the objects' identity on the wire: python tests/player_identity_guard.py [--root DIR], exit 1 on a
failure. A stock (AngelScript) mission registers every object, the players too, under its script thread's own count
(1D94C0): the enemies' and the NPCs' NetIds agree between the room's machines, a player's does not (2026-10-31: the
guests dropped the host's vehicle syncs, barrages, plans). What it holds in place:
  - every channel names a player by its reference (net_object_ref.h: its mission slot, net_object_resolve.h
    EncodeNetRef / command_identity.h PlayerRequesterOf), never by ReadNativeObjectId of the player;
  - a channel's targets (a Q mark's, a grab's, a map command's focus, a controlled enemy's attack) are references too
    (EncodeNetRef: a player by its slot), never a bare ReadNativeObjectId, and are found by ResolveNetRef(s);
  - the resolvers find a player by that slot alone (net_object_resolve.cpp), the older ones are thin over them;
  - no protocol checks a caller / target / requester as a native ID (CanonicalId, wire Id): a player's reference has its
    tag at bytes 20..23, where a NetId is zero;
  - the ultimate's and the barrage's caller is the reference taken when it started (never a live-only PlayerRequesterOf
    of the caller), checked on receipt by SenderOwns (found here or not);
  - every file that reads a NetId (ReadNativeObjectId) is listed with what it names (never a player: an NPC, a vehicle,
    the native registry's record), and a 32-byte wire field is never checked as a NetId's form (bytes 20..23 zero) in the
    protocols and the controlled enemies' code, where a player's reference has its tag;
  - a reference names an object, whether it is found is the receiver's (EncodeNetRef asks no "not deleted"), a player is
    matched by its slot alone (never by NetId in the walk), and an absent player is an empty actor to the projectile
    streams, not a pending one;
  - the room protocol and the channels' versions moved with it, so an older build is left out (CLAUDE.md: no
    compatibility with older builds).
"""
from __future__ import annotations

import argparse
import os
import re
import sys

failures: list[str] = []


def fail(message: str) -> None:
    failures.append(message)


def code_only(text: str) -> str:
    return '\n'.join(line.split('//', 1)[0] for line in text.splitlines())


def read(root: str, rel: str) -> str:
    with open(os.path.join(root, rel), encoding='utf-8') as f:
        return f.read()


# The senders: each sends its own player by its reference.
SENDERS = [
    'src/player_vehicle.cpp', 'src/barrage.cpp', 'src/ultimate.cpp', 'src/qmark.cpp', 'src/floatgun.cpp',
    'src/gravity_net.cpp', 'src/command_net.cpp', 'src/enemy_control.cpp', 'src/support_dispatch.cpp',
    'src/support_net.cpp', 'src/pvp_support.cpp',
]
# The channels that send a target (any object, a player too): no ReadNativeObjectId in them, every object by EncodeNetRef.
TARGET_SENDERS = [
    'src/qmark.cpp', 'src/gravity_net.cpp', 'src/command_net.cpp', 'src/ultimate.cpp', 'src/barrage.cpp',
    'src/player_vehicle.cpp', 'src/enemy_control_action_native.cpp', 'src/enemy_control_target.cpp',
]
# Every file that reads a NetId (file: what it names). None is a player: a player is named by EncodeNetRef alone.
NETID_READERS = {
    'src/support_soldier.cpp': 'the reader itself (ReadNativeObjectId)',
    'src/support_soldier.h': 'the reader itself (ReadNativeObjectId)',
    'src/net_object_resolve.cpp': "the resolver: an NPC by its NetId; a player soldier never (PlayerSoldier before it)",
    'src/floatgun.cpp': "a drop's colonist (an enemy NPC) by its NetId: FGUN's drop `who` (ValidNative on the wire)",
    'src/enemy_control_projectile_native.cpp': "ReadNativeActorId checks the native registry's (75C910) record of a NetId",
    'src/enemy_control.cpp': 'the controlled enemy (an NPC: the lease target, its candidate ID); the owner by PlayerRequesterOf',
    'src/enemy_control_net.cpp': "the lease's controlled enemy (an NPC) against its NetId",
    'src/enemy_control_timeship_pods_net.cpp': "the TimeShip's pod bodies (enemy NPCs)",
    'src/jet_carrier.cpp': "the plugin's carrier jets (NPC-driven vehicles) and their drones' parent",
    'src/jet_spawn.cpp': "a support drone's parent carrier (an NPC-driven vehicle)",
    'src/npcai.cpp': "a remote NPC squad's top NPC (IsAnyPlayer excluded before it)",
    'src/subcarrier.cpp': 'the submarine carrier and its children (NPC vehicles)',
    'src/support_dispatch.cpp': "a carrier support's parent vehicle",
    'src/support_source.cpp': "a support source's vehicle (its key)",
    'src/mission_crew_support.cpp': "a crew vehicle's NetId; the host's own player only as a local anchor (LOCAL_ONLY)",
}
# A NetId's form (bytes 20..23 zero) checked on a 32-byte field: a player's reference fails it. Scanned in the protocols
# and every controlled enemy's file; allowed only where the field is never a player (file: why).
NETID_FORM = re.compile(r'for\s*\(\s*(?:unsigned|int|std::size_t|auto)?\s*(\w+)\s*=\s*20\s*;\s*\1\s*<\s*24|'
                        r'\b(\w+)\s*>=\s*20\s*&&\s*\2\s*<\s*24')
NETID_FORM_OK = {
    'src/command_protocol.cpp': "CanonicalId: a map command's unit (an NPC; ValidNative for the request's units)",
    'src/support_protocol.cpp': "an existing vehicle unit's NetId (a vehicle, never a player)",
}
# The receivers: what they are sent is found by ResolveNetRef(s).
RESOLVERS = [
    'src/qmark.cpp', 'src/gravity_net.cpp', 'src/ultimate.cpp', 'src/barrage.cpp', 'src/player_vehicle.cpp',
    'src/enemy_control_projectile_native.cpp', 'src/command_identity.cpp', 'src/enemy_control_target.cpp',
]
# Protocols / codecs: no player-bearing field read or checked as a native ID.
PROTOCOLS = [
    'src/qmark_protocol.cpp', 'src/gravity_net_protocol.cpp', 'src/ultimate_net_protocol.h', 'src/barrage_net_protocol.h',
    'src/player_vehicle_logic.h', 'src/command_protocol.cpp', 'src/enemy_control_protocol.h', 'src/support_protocol.cpp',
    'src/enemy_control_deiroi_net_codec.h', 'src/enemy_control_net_input.h', 'src/enemy_control_logic.cpp',
]
NATIVE_FIELD = re.compile(
    r'(CanonicalId\(\s*(m|r|g|s)\.(caller|target|marker|grabber|requester|who|focus|spec\.requesterPlayer)\b|'
    r'\.Id\(\s*(m|g|s)\.(caller|target|marker|grabber|requester|who)\b|'
    r'\bIdOk\(\s*s\.(marker|target)\b)')
# A player's NetId read where it never leaves the machine (file: why).
LOCAL_ONLY = {
    'src/mission_crew_support.cpp': 'the host checks its own player is registered before a crew transaction',
}
PLAYER_NETID = re.compile(r'ReadNativeObjectId\(\s*(PlayerHuman\(\)|me|human|who|by|caller|s\.by|'
                          r'static_cast<const unsigned char\*>\(\s*(requester|actor|owner|ownerPlayer)\.obj\s*\))\s*,')
VERSIONS = [
    ('src/mod_room.h', r'kAllForcesRoomProtocol=(\d+)', 7),
    ('src/command_protocol.h', r'kMinVersion=(\d+)', 9),
    ('src/player_vehicle_logic.h', r'kVersion=(\d+)', 3),
    ('src/barrage_net_protocol.h', r'kVersion=(\d+)', 4),
    ('src/ultimate_net_protocol.h', r'kVersion=(\d+)', 3),
    ('src/qmark_protocol.h', r'kVersion=(\d+)', 3),
    ('src/gravity_net_protocol.h', r'kVersion=(\d+)', 4),
    ('src/enemy_control_protocol.h', r'kVersion=(\d+)', 7),
    ('src/enemy_control_net_input.h', r'kInputVersion=(\d+)', 4),
    ('src/enemy_control_deiroi_net_codec.h', r'kVersion=(\d+)', 2),
    ('src/enemy_control_monster_net_codec.h', r'kVersion=(\d+)', 2),
    ('src/enemy_control_action_native.cpp', r'kRootVersion=(\d+)', 2),
]


def body(text: str, signature: str) -> str:
    """The braces-balanced body of the first definition starting with `signature` ('' when not found)."""
    at = text.find(signature)
    while at >= 0:
        open_at = text.find('{', at)
        if open_at < 0:
            return ''
        if ';' not in text[at:open_at]:
            depth = 0
            for i in range(open_at, len(text)):
                depth += {'{': 1, '}': -1}.get(text[i], 0)
                if depth == 0:
                    return text[open_at:i + 1]
            return ''
        at = text.find(signature, at + 1)
    return ''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    root = parser.parse_args().root

    for rel in SENDERS:
        code = code_only(read(root, rel))
        if 'PlayerRequesterOf(' not in code and 'EncodeNetRef(' not in code:
            fail(f'{rel}: no player sent by its reference (EncodeNetRef / PlayerRequesterOf)')
    if 'netref::MakePlayer(' not in code_only(read(root, 'src/support_net.cpp')):
        fail("src/support_net.cpp: a plan's requester is not made from the player's mission slot")
    if os.path.exists(os.path.join(root, 'src/player_requester.h')):
        fail('src/player_requester.h: the old requester layout is back (net_object_ref.h replaced it)')
    ref = code_only(read(root, 'src/net_object_ref.h'))
    if 'kTagAt=20' not in ref or 'kPlayerTag=0x52504641u' not in ref:
        fail("src/net_object_ref.h: a player's tag is not at bytes 20..23 (where a native NetId is always zero)")

    for folder in ('src', 'autoturret'):
        for name in sorted(os.listdir(os.path.join(root, folder))):
            if not name.endswith(('.cpp', '.h')):
                continue
            rel = f'{folder}/{name}'
            for match in PLAYER_NETID.finditer(code_only(read(root, rel))):
                if rel not in LOCAL_ONLY:
                    fail(f"{rel}: a player's NetId read ({match.group(0)}...): name it by EncodeNetRef")

    for rel in TARGET_SENDERS:
        code = code_only(read(root, rel))
        if 'ReadNativeObjectId(' in code:
            fail(f'{rel}: a target read by ReadNativeObjectId: send it by EncodeNetRef (a player is its slot)')
        if 'EncodeNetRef(' not in code:
            fail(f'{rel}: no object sent by EncodeNetRef')
    readers = set()
    for folder in ('src', 'autoturret'):
        for name in sorted(os.listdir(os.path.join(root, folder))):
            if not name.endswith(('.cpp', '.h')):
                continue
            rel = f'{folder}/{name}'
            code = code_only(read(root, rel))
            if 'ReadNativeObjectId(' in code:
                readers.add(rel)
                if not NETID_READERS.get(rel):
                    fail(f'{rel}: reads a NetId (ReadNativeObjectId) unlisted: a player or a target goes by EncodeNetRef; '
                         'an NPC-only read is listed in NETID_READERS with what it names')
            if rel in PROTOCOLS or name.startswith('enemy_control'):
                for match in NETID_FORM.finditer(code):
                    if not NETID_FORM_OK.get(rel):
                        fail(f"{rel}: a field checked as a NetId's form ({match.group(0)}): a player's reference has its tag "
                             'there; check it with netref::Valid')
    for rel in NETID_READERS:
        if rel not in readers:
            fail(f'{rel}: listed as a NetId reader ({NETID_READERS[rel]}) but reads none: drop it from NETID_READERS')
    for rel in NETID_FORM_OK:
        if not NETID_FORM.search(code_only(read(root, rel))):
            fail(f"{rel}: listed in NETID_FORM_OK but checks no NetId's form: drop it")
    for rel in RESOLVERS:
        if not re.search(r'\bResolveNetRefs?\(', code_only(read(root, rel))):
            fail(f'{rel}: what it is sent is not found by ResolveNetRef(s)')
    for rel in PROTOCOLS:
        for match in NATIVE_FIELD.finditer(code_only(read(root, rel))):
            fail(f'{rel}: a player-bearing field read or checked as a native ID ({match.group(0)}): wire Ref / netref')
    for rel in ('src/ultimate.cpp', 'src/barrage.cpp'):
        code = code_only(read(root, rel))
        if re.search(r'PlayerRequesterOf\(\s*(s\.by|caller)\b', code):
            fail(f'{rel}: the caller sent only while alive (PlayerRequesterOf): send the reference taken at its start')
        if 'SenderOwns(' not in code:
            fail(f"{rel}: the caller not checked by SenderOwns (a fallen or absent caller's messages dropped)")
    if 's.callerRef' not in code_only(read(root, 'src/ultimate.cpp')):
        fail("src/ultimate.cpp: the show does not keep its caller's reference from its start")

    resolver = code_only(read(root, 'src/command_identity.cpp'))
    if 'ReadNativeObjectId(' in resolver:
        fail('src/command_identity.cpp: the older resolvers read NetIds themselves (they are thin over ResolveNetRefs)')
    if 'netref::IsPlayer(requester)' not in resolver:
        fail("src/command_identity.cpp: the command resolver does not take its requester as a player's reference")
    if 'netref::IsPlayer(marker)' not in resolver:
        fail("src/command_identity.cpp: the mark resolver does not take its marker as a player's reference")
    walk = code_only(read(root, 'src/net_object_resolve.cpp'))
    if 's.slot!=q.slot' not in body(walk, 'NetResolved Result('):
        fail('src/net_object_resolve.cpp: a player is not found by its slot')
    visit = body(walk, 'void __fastcall Visit(')
    if not 0 <= visit.find('PlayerSoldier(object)') < visit.find('ReadNativeObjectId(object,id)'):
        fail('src/net_object_resolve.cpp: the walk matches a player soldier by its NetId (a player only by its slot)')
    encode = body(walk, 'bool EncodeNetRef(const ObjRef& who')
    if 'Named(object,&ref)' not in encode or 'Present(' in encode or '0x18' in encode:
        fail('src/net_object_resolve.cpp: EncodeNetRef asks more than what the object is called (deleted / dead is the '
             "receiver's NetLife)")
    actor = body(code_only(read(root, 'src/enemy_control_projectile_native.cpp')), 'bool ResolveActor(')
    if 'found.ambiguous' not in actor or 'if(!found.found)return true;' not in actor:
        fail('src/enemy_control_projectile_native.cpp ResolveActor: a player not here must be an empty actor (only two of '
             'its slot fail)')
    lock = body(code_only(read(root, 'src/enemy_control.cpp')), 'bool EnemyControlWeaponLockIdentity(')
    if 'EncodeNetRef(focus.actor' not in lock or 'ReadNativeObjectId(' in lock:
        fail("src/enemy_control.cpp EnemyControlWeaponLockIdentity: the Deiroi entity shot's target is not its reference")
    target = code_only(read(root, 'src/enemy_control_target.cpp'))
    if 'ResolveNetRef(id,NetLife::live' not in target or 'StillNetRef(id,NetLife::live' not in target:
        fail('src/enemy_control_target.cpp: a focus reference is not found live and rechecked (ResolveNetRef / StillNetRef)')
    receive = body(code_only(read(root, 'src/barrage.cpp')), 'bool ReceiveBarrageNetwork(')
    if 'm.team' not in receive or 'PlayerHuman()' not in receive:
        fail('src/barrage.cpp: a barrage whose caller is not here neither warns nor fires from a stand-in of its side')

    for rel, pattern, least in VERSIONS:
        found = re.search(pattern, code_only(read(root, rel)))
        if not found or int(found.group(1)) < least:
            fail(f'{rel}: {pattern} below {least} (an older build would still be spoken to)')

    for message in failures:
        print(f'FAIL {message}')
    if failures:
        return 1
    print('player_identity_guard: OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
