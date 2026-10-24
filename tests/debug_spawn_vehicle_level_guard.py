"""Source guard (and, with EDF6_NATIVE_DLL, a native audit): the M map's 「摆放」 and the debug spawn tool make an empty
vehicle the way the script's CreateVehicle2 does, never through SetLevel.

The user, 2026-10-19: 「火神炮和bmpt没办法移动和开火，不知道是不是用m地图召唤的原因」. SetLevel (EDF.dll 0x54E740) always ends
in 0x118A4B0(obj, 8) (0x54E8F7): +0x1A |= 8, the native AI list. A vehicle on it gets the AI pass every frame, which
clears every seat's input block before its AI action (603: slot 7 0x673300 -> 0x62C120(seat+0x2C0)), and the crew team
pass skips it (0x63049E): the player who boards it neither drives nor fires. CreateVehicle2 (0x1B28C0) clears its
descriptor's SetLevel flag (0x1B2A2E) and calls 0x633280(vehicle, max(0xD7AE0(GameStatus, level) / 10, 1)) instead
(0x1B2BD9..0x1B2C09), which reads and applies the mission_setup itself. So in src/debug_spawn.cpp:
  - SpawnVehicle calls kVehicleLevelSetup with the level computed through kLevelScale, sets team 5, and neither calls
    SetLevel nor ApplyMissionSetup;
  - the profile (kSigs) pins 0x633280, 0xD7AE0 and CreateVehicle2's own call site, so a different EDF.dll turns the
    tool off instead of guessing;
  - the loading on demand (RequestLoad / LoadTick) uses the manager's completion query (0x7A3600 via IsCompleteResource
    0x70ED20), checked by its own signatures.
With EDF6_NATIVE_DLL (the game's EDF.dll) each of those signatures is compared with the DLL's bytes at its RVA.
python tests/debug_spawn_vehicle_level_guard.py [--root DIR]; exit 1 on a failure."""
from __future__ import annotations

import argparse
import os
import re
import struct
import sys


def code_only(text: str) -> str:
    return '\n'.join(line.split('//', 1)[0] for line in text.splitlines())


def body(cpp: str, head: str) -> str:
    parts = cpp.split(head, 1)
    return parts[1].split('\n}\n', 1)[0] if len(parts) == 2 else ''


def native_bytes(dll: str, rva: int, size: int) -> bytes:
    with open(dll, 'rb') as f:
        data = f.read()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    for i in range(count):
        at = pe + 24 + optional + i * 40
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, at + 8)
        if va <= rva < va + max(vsize, rsize):
            return data[raw + rva - va: raw + rva - va + size]
    return b''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    root = parser.parse_args().root
    raw = open(os.path.join(root, 'src', 'debug_spawn.cpp'), encoding='utf-8').read()
    cpp = code_only(raw)
    failures: list[str] = []
    spawn = body(cpp, 'unsigned char* SpawnVehicle(')
    if not spawn:
        failures.append('debug_spawn.cpp: no SpawnVehicle')
    for banned in ('kSetLevel', 'SetLevelFn', 'ApplyMissionSetup('):
        if banned in spawn:
            failures.append(f'SpawnVehicle uses {banned}: SetLevel puts the vehicle on the native AI list (its seats\' input cleared)')
    for wanted in ('kVehicleLevelSetup', 'kLevelScale', 'kTeamVehicle'):
        if wanted not in spawn:
            failures.append(f'SpawnVehicle does not use {wanted} (CreateVehicle2\'s own steps)')
    if not re.search(r'kTeamVehicle\s*=\s*5\b', cpp):
        failures.append('kTeamVehicle is not 5 (CreateVehicle2 0x1B2A27)')
    sigs = cpp.split('const Sig kSigs[]={', 1)
    table = sigs[1].split('};', 1)[0] if len(sigs) == 2 else ''
    for name in ('kVehicleLevelSetup', 'kLevelScale', 'kCreateVehicleLevelSite'):
        if '{' + name + ',' not in table:
            failures.append(f'kSigs does not pin {name}: another EDF.dll must turn the tool off')
    for fn, wanted in (('bool RequestLoad(', 'kPreload'), ('void LoadTick(', 'kLoadDone')):
        if wanted not in body(cpp, fn):
            failures.append(f'debug_spawn.cpp {fn.split("(")[0]}: does not use {wanted}')
    if 'kLoadDoneSig' not in body(cpp, 'bool CheckProfile('):
        failures.append('CheckProfile does not check the completion query (loadOk)')
    # Every constexpr RVA and its signature bytes, for the native audit.
    rvas = {name: int(value, 16) for name, value in re.findall(r'\b(k\w+)=0x([0-9A-Fa-f]+)', cpp)}
    pinned = {}
    for name, values in re.findall(r'const unsigned char (k\w+)Sig\[\]=\{([^}]*)\}', cpp, re.S):
        if name in rvas:
            pinned[name] = bytes(int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', values))
    dll = os.environ.get('EDF6_NATIVE_DLL')
    audited = 0
    if dll and os.path.isfile(dll):
        for name in ('kVehicleLevelSetup', 'kLevelScale', 'kCreateVehicleLevelSite', 'kLoadDone', 'kLoadDoneScript', 'kPreload', 'kSetLevel'):
            sig = pinned.get(name)
            if not sig:
                failures.append(f'no signature for {name}')
                continue
            if native_bytes(dll, rvas[name], len(sig)) != sig:
                failures.append(f'{name} at {rvas[name]:#x}: EDF.dll differs from its signature')
            audited += 1
        # SetLevel's tail always enters the AI list: call 0x118A4B0 at 0x54E902 (the reason for all of the above).
        tail = native_bytes(dll, 0x54E8F7, 0x10)
        call = tail.find(b'\xE8')
        if call < 0 or 0x54E8F7 + call + 5 + struct.unpack_from('<i', tail, call + 1)[0] != 0x118A4B0:
            failures.append('SetLevel 0x54E8F7: no call to 0x118A4B0 (the AI list) where the guard expects it')
    for f in failures:
        print('FAIL', f)
    print(f'debug_spawn_vehicle_level_guard: {"native audit " + str(audited) + " signatures, " if audited else ""}'
          f'{"FAILED" if failures else "passed"}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
