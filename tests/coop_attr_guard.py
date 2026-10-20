"""Source guard (2026-10-20 review): EDF6Coop's member attributes are taken in one place (src/coop_attr.h / .cpp), the
host-authoritative switches are read through host_settings.h, and the mission's sync point does what it promises.

    python tests/coop_attr_guard.py
"""
from __future__ import annotations

import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
checks = failed = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


def src(rel: str) -> str:
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


# The table looked up only by coop_attr.cpp (mod_room.cpp's dependency check goes through its test seam resolveCoop and
# coop_attr.h Usable: the same validity rule).
for path in sorted(glob.glob(os.path.join(ROOT, 'src', '*.cpp'))):
    rel = os.path.relpath(path, ROOT).replace('\\', '/')
    text = src(rel)
    if rel in ('src/coop_attr.cpp', 'src/mod_room.cpp'):   # mod_room: through its seam, checked below
        continue
    if 'GetProcAddress' in text and '"EDF6CoopGetMemberAttributeApi"' in text:
        check(False, f'{rel} looks EDF6CoopGetMemberAttributeApi up itself (use coop_attr.h Api)')
    if rel != 'src/mod_room.cpp':
        check(not re.search(r'EDF6COOP_MEMBER_ATTRIBUTE_VERSION\s*\)', text), f'{rel} asks for the table itself')
mod_room = src('src/mod_room.cpp')
check('coopattr::Usable(attributes(EDF6COOP_MEMBER_ATTRIBUTE_VERSION))' in mod_room, 'the room check uses the same validity rule')
for rel in ('src/outfit41.cpp', 'src/edf5online_room.cpp', 'src/pvp.cpp', 'src/room_sync.cpp', 'src/host_settings.cpp'):
    check('coopattr::' in src(rel), f'{rel} reads the member attributes through coop_attr.h')
for rel in ('src/outfit41.cpp', 'src/edf5online_room.cpp', 'src/host_settings.cpp'):
    check('coopattr::Beat' in src(rel) and 'GetTickCount64()-' not in src(rel).replace(' ', ''),
          f'{rel} ticks on coop_attr.h Beat')

# The host's switches: never this machine's ini directly where the shared objects are decided.
npcai = src('src/npcai.cpp')
check('Cfg().vanillaRecruit' not in npcai and 'VanillaRecruitInEffect()' in npcai, 'npcai: VanillaRecruit is the host\'s online')
check('Cfg().npcFreeSquadCover' not in npcai and 'NpcFreeSquadCoverInEffect()' in npcai, 'npcai: NpcFreeSquadCover is the host\'s online')
bike = src('src/edf41bike.cpp')
check(bike.count('Edf41BikeGunnerAllowed()') == 3, 'edf41bike: the seat build, the frame and the pose all gated per mission')

# MissionSync_Begin: colours, host settings, the dialog, then the record.
mission = src('src/mission.cpp')
hook = mission.split('std::uintptr_t __fastcall MissionSyncHook(', 1)[1].split('\n}', 1)[0]
order = [hook.find(x) for x in ('Outfit41MissionSync();', 'HostSettingsMissionSync();', 'ResetRoomUi();', 'image+kSyncRecord')]
check(all(i >= 0 for i in order) and order == sorted(order), 'MissionSyncHook: colours, host settings, dialog, then the record')
check('RedirectCall(image+kSyncCall,image+kSyncRecord' in mission, 'the call at 0x7920EA redirected only while it targets 0x78E9A0')

print(f'coop_attr_guard: {checks - failed}/{checks} checks passed')
sys.exit(1 if failed else 0)
