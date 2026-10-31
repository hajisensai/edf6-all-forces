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
    'src/player_vehicle.cpp', 'src/enemy_control_action_native.cpp',
]
# A ReadNativeObjectId that stays in a channel (file: why): never a player.
NATIVE_ONLY = {
    'src/floatgun.cpp': "a drop's colonist (an enemy NPC) by its NetId: FGUN's drop `who` (ValidNative on the wire)",
    'src/enemy_control_projectile_native.cpp': "ReadNativeActorId checks the native registry's (75C910) record of a NetId",
}
# The receivers: what they are sent is found by ResolveNetRef(s).
RESOLVERS = [
    'src/qmark.cpp', 'src/gravity_net.cpp', 'src/ultimate.cpp', 'src/barrage.cpp', 'src/player_vehicle.cpp',
    'src/enemy_control_projectile_native.cpp', 'src/command_identity.cpp',
]
# Protocols / codecs: no player-bearing field read or checked as a native ID.
PROTOCOLS = [
    'src/qmark_protocol.cpp', 'src/gravity_net_protocol.cpp', 'src/ultimate_net_protocol.h', 'src/barrage_net_protocol.h',
    'src/player_vehicle_logic.h', 'src/command_protocol.cpp', 'src/enemy_control_protocol.h', 'src/support_protocol.cpp',
    'src/enemy_control_deiroi_net_codec.h',
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
    ('src/barrage_net_protocol.h', r'kVersion=(\d+)', 3),
    ('src/ultimate_net_protocol.h', r'kVersion=(\d+)', 3),
    ('src/qmark_protocol.h', r'kVersion=(\d+)', 3),
    ('src/gravity_net_protocol.h', r'kVersion=(\d+)', 4),
    ('src/enemy_control_protocol.h', r'kVersion=(\d+)', 6),
    ('src/enemy_control_deiroi_net_codec.h', r'kVersion=(\d+)', 2),
    ('src/enemy_control_monster_net_codec.h', r'kVersion=(\d+)', 2),
    ('src/enemy_control_action_native.cpp', r'kRootVersion=(\d+)', 2),
]


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
    for rel, why in NATIVE_ONLY.items():
        if not why or 'ReadNativeObjectId(' not in code_only(read(root, rel)):
            fail(f'{rel}: listed as a NetId reader ({why}) but reads none: drop it from NATIVE_ONLY')
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
    if 'q.kind!=netref::Kind::player || q.slot!=slot' not in walk:
        fail('src/net_object_resolve.cpp: a player is not found by its slot')

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
