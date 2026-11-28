"""The Vehicler's start seat's natives (src/ride_native.h, src/vehicler_start.cpp) against the game's EDF.dll.

Read only. Without the supported EDF.dll only the source checks run (exit 77). With it:
- the four signatures of ride_native.h are the code at their addresses (RideVehicle 0x5765E0, its release 0x57690D, the
  announce 0x5763E0, the peer's replay lea + call at 0x57765A);
- the replay's call at 0x577661 lands on RideVehicle, the board button's call at 0x56D799 on the announce;
- the board button's tail 0x56D7D1 sets human +0x3F0 when seat +0x2B4 is the key row SetsSeatFlag takes (3);
- RideVehicle writes the human's +0x1540 / +0x1548 / +0x1550 the header names.
python tests/vehicler_start_native_audit.py [EDF.dll]"""
from __future__ import annotations

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import gamedir  # noqa: E402

checks = 0
failed = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


def read(*parts: str) -> str:
    with open(os.path.join(ROOT, *parts), encoding='utf-8') as f:
        return f.read()


NATIVE = read('src', 'ride_native.h')
LOGIC = read('src', 'vehicler_start_logic.h')
START = read('src', 'vehicler_start.cpp')
NPCAI = read('src', 'npcai.cpp')


def constant(text: str, name: str) -> int:
    m = re.search(r'\b' + name + r'=(0x[0-9A-Fa-f]+|\d+)', text)
    return int(m.group(1), 0) if m else -1


def sig(name: str) -> bytes:
    m = re.search(r'\b' + name + r'\[\]=\{([^}]*)\}', NATIVE)
    return bytes(int(x, 16) for x in m.group(1).split(',')) if m else b''


RIDE = constant(NATIVE, 'kRideVehicle')
RELEASE = constant(NATIVE, 'kRideRelease')
ANNOUNCE = constant(NATIVE, 'kAnnounceRide')
REMOTE = constant(NATIVE, 'kRemoteRideCall')
SEAT, RIDING, CTRL = (constant(NATIVE, n) for n in ('kHumanSeat', 'kHumanRiding', 'kHumanRidingCtrl'))
FLAG, KEYROW = constant(NATIVE, 'kHumanSeatFlag'), constant(NATIVE, 'kSeatKeyRow')
SIGS = {n: sig(n) for n in ('kRideSig', 'kRideReleaseSig', 'kAnnounceRideSig', 'kRemoteRideSig')}

# ---- source ----
check(all(len(b) >= 8 for b in SIGS.values()), 'every signature parsed')
check((RIDE, RELEASE, ANNOUNCE, REMOTE) == (0x5765E0, 0x57690D, 0x5763E0, 0x577661), 'the ride addresses')
check((SEAT, RIDING, CTRL, FLAG, KEYROW) == (0x1540, 0x1548, 0x1550, 0x3F0, 0x2B4), 'the human / seat offsets')
check(SIGS['kRemoteRideSig'][-1] == 0xE8, 'the replay signature ends on its call')
check(constant(LOGIC, 'kKeyRowSpecial') == 3, 'the key row SetsSeatFlag takes')
check(constant(LOGIC, 'kVehiclerType') == 4 and constant(LOGIC, 'kDriverSeat') == 0, 'type 4, seat 0')
check('#include "ride_native.h"' in NPCAI and 'kRideSig[]' not in NPCAI and 'kRemoteRideSig[]' not in NPCAI,
      'npcai.cpp takes the ride natives from ride_native.h, no copy')
check('Matches(kRideRelease' in NPCAI and 'Matches(0x57690D' not in NPCAI, 'npcai.cpp checks the release by name')
check(re.search(r'DriverSeatBarred\(i,ZwVehicleOwner\(v\),h\)', NPCAI) is not None, 'SeatTakes bars the owned driver seat')
check(all(s in START for s in ('Matches(kRideVehicle', 'Matches(kRideRelease', 'Matches(kAnnounceRide',
                                'Matches(kRemoteRideCall-7')), 'the install checks all four signatures')
check('_InterlockedIncrement' in START and 'kAnnounceRide)(h)' in START, 'one reference taken, the ride announced')

path = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else None
if not path:
    game = gamedir.find()
    path = os.path.join(game, 'EDF.dll') if game else None
if not path or not os.path.isfile(path):
    print(f'vehicler_start_native: {checks} source checks, {failed} failed; no EDF.dll, its part skipped')
    sys.exit(1 if failed else 77)
import pefile  # noqa: E402

pe = pefile.PE(path, fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'vehicler_start_native: {checks} source checks, {failed} failed; EDF.dll is not the supported build, skipped')
    sys.exit(1 if failed else 77)


def at(rva: int, n: int) -> bytes:
    return pe.get_data(rva, n)


def call_target(rva: int) -> int:
    return rva + 5 + struct.unpack_from('<i', at(rva + 1, 4))[0] if at(rva, 1) == b'\xE8' else -1


# ---- EDF.dll ----
check(at(RIDE, len(SIGS['kRideSig'])) == SIGS['kRideSig'], 'RideVehicle 0x5765E0')
check(at(RELEASE, len(SIGS['kRideReleaseSig'])) == SIGS['kRideReleaseSig'], 'its release 0x57690D')
check(at(ANNOUNCE, len(SIGS['kAnnounceRideSig'])) == SIGS['kAnnounceRideSig'], 'the announce 0x5763E0')
check(at(REMOTE - 7, len(SIGS['kRemoteRideSig'])) == SIGS['kRemoteRideSig'], 'the replay 0x57765A')
check(call_target(REMOTE) == RIDE, 'the replay calls RideVehicle')
check(call_target(0x56D799) == ANNOUNCE, 'the board button announces by 0x5763E0')
# 0x56D7D1: mov rax,[rbx+0x1540]; cmp dword [rax+0x2B4],3; jne +7; mov byte [rbx+0x3F0],1
tail = (b'\x48\x8B\x83' + struct.pack('<I', SEAT) + b'\x83\xB8' + struct.pack('<I', KEYROW) + b'\x03\x75\x07' +
        b'\xC6\x83' + struct.pack('<I', FLAG) + b'\x01')
check(at(0x56D7D1, len(tail)) == tail, 'the board button tail: +0x3F0 = (seat +0x2B4 == 3)')
body = at(RIDE, RELEASE - RIDE)
for off, what in ((SEAT, '+0x1540'), (RIDING, '+0x1548'), (CTRL, '+0x1550')):
    check(struct.pack('<I', off) in body, f'RideVehicle touches the human {what}')

print(f'vehicler_start_native: {checks} checks, {failed} failed')
sys.exit(1 if failed else 0)
