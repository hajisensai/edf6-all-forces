"""Source guard of the players' identity on the wire: python tests/player_identity_guard.py [--root DIR], exit 1 on a
failure. A stock (AngelScript) mission registers every object, the players too, under its script thread's own count
(1D94C0), so a player's native NetId differs between the room's machines (2026-10-31: the guests dropped the host's
vehicle syncs, barrages, plans). What it holds in place:
  - every channel names a player by its requester ID (player_requester.h: its mission slot, command_identity.h
    PlayerRequesterOf), never by ReadNativeObjectId of the player;
  - the resolvers find a player by that slot alone: the mark resolver compares no marker with a NetId, the command
    resolver takes its requester as a slot;
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


# The senders: each sends its own player by its requester ID.
SENDERS = [
    'src/player_vehicle.cpp', 'src/barrage.cpp', 'src/ultimate.cpp', 'src/qmark.cpp', 'src/floatgun.cpp',
    'src/gravity_net.cpp', 'src/command_net.cpp', 'src/enemy_control.cpp', 'src/support_dispatch.cpp',
    'src/support_net.cpp', 'src/pvp_support.cpp',
]
# A player's NetId read where it never leaves the machine (file: why).
LOCAL_ONLY = {
    'src/mission_crew_support.cpp': 'the host checks its own player is registered before a crew transaction',
}
PLAYER_NETID = re.compile(r'ReadNativeObjectId\(\s*(PlayerHuman\(\)|me|human|who|by|caller|s\.by|'
                          r'static_cast<const unsigned char\*>\(\s*(requester|actor|owner|ownerPlayer)\.obj\s*\))\s*,')
VERSIONS = [
    ('src/mod_room.h', r'kAllForcesRoomProtocol=(\d+)', 6),
    ('src/command_protocol.h', r'kMinVersion=(\d+)', 8),
    ('src/player_vehicle_logic.h', r'kVersion=(\d+)', 2),
    ('src/barrage_net_protocol.h', r'kVersion=(\d+)', 2),
    ('src/ultimate_net_protocol.h', r'kVersion=(\d+)', 2),
    ('src/qmark_protocol.h', r'kVersion=(\d+)', 2),
    ('src/gravity_net_protocol.h', r'kVersion=(\d+)', 3),
    ('src/enemy_control_protocol.h', r'kVersion=(\d+)', 5),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    root = parser.parse_args().root

    for rel in SENDERS:
        if 'PlayerRequesterOf(' not in code_only(read(root, rel)):
            fail(f'{rel}: no player sent by its requester ID (PlayerRequesterOf)')
    support_net = code_only(read(root, 'src/support_net.cpp'))
    if 'MakePlayerRequester(' not in support_net:
        fail('src/support_net.cpp: a plan\'s requester is not made from the player\'s mission slot')

    for folder in ('src', 'autoturret'):
        for name in sorted(os.listdir(os.path.join(root, folder))):
            if not name.endswith(('.cpp', '.h')):
                continue
            rel = f'{folder}/{name}'
            for match in PLAYER_NETID.finditer(code_only(read(root, rel))):
                if rel not in LOCAL_ONLY:
                    fail(f'{rel}: a player\'s NetId read ({match.group(0)}...): name it by PlayerRequesterOf')

    resolver = code_only(read(root, 'src/command_identity.cpp'))
    if re.search(r'v->marker\b', resolver):
        fail('src/command_identity.cpp: the mark resolver compares a marker with a NetId')
    if 'PlayerRequesterSlot(requester,&visitor.requesterSlot)' not in resolver:
        fail('src/command_identity.cpp: the command resolver does not take its requester as a mission slot')
    if 'PlayerRequesterSlot(marker,&visitor.markerSlot)' not in resolver:
        fail('src/command_identity.cpp: the mark resolver does not take its marker as a mission slot')

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
