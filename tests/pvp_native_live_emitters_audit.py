"""Read-only native contracts for original PVP persistent caches and 314/318 state.

No game load, execution, install or live-process access. Missing input skips with 77.
The complete 29-weapon/held-storage ABI is independently checked by
pvp_controlled_npc_native_audit.py.
"""
from pathlib import Path
import argparse
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('edf_dll', nargs='?', type=Path)
args = parser.parse_args()
if not args.edf_dll or not args.edf_dll.is_file():
    print('SKIP: pass an existing supported EDF.dll')
    raise SystemExit(77)
import pefile
pe = pefile.PE(str(args.edf_dll), fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46 or pe.FILE_HEADER.Machine != 0x8664:
    raise RuntimeError('unsupported native EDF.dll profile')

def expect(rva, encoded, label):
    wanted = bytes.fromhex(encoded)
    if pe.get_data(rva, len(wanted)) != wanted:
        raise RuntimeError(f'{label}: native bytes changed at {rva:#x}')

expect(0x54EE88, '39 91 14 03 00 00 75 14 8B 81 80 03 00 00 C1 E8 06 24 01 41 3A C0 0F 84 A4 00 00 00', 'SetTeam same-team registration early return')
expect(0x54EEE0, '89 BB 14 03 00 00 83 FF FF 74 06 89 BB 18 03 00 00', 'SetTeam closes 314 independently of last-team 318')
expect(0x54FD23, 'C7 83 14 03 00 00 FF FF FF FF 83 A3 80 03 00 00 BF 8B 93 18 03 00 00 41 B0 01 48 8B CB E8', 'native revival reloads remembered 318')
if 0x54FD45 + struct.unpack('<i', pe.get_data(0x54FD41, 4))[0] != 0x54EE70:
    raise RuntimeError('native revival no longer calls SetTeam')
expect(0x6D2A46, '8B 83 18 03 00 00 89 85 10 02 00 00', 'GunBattery2 per-shot owner last team')
expect(0x2B9325, '8B 90 18 03 00 00 48 8B CB E8', 'RadioContact aircraft owner last team')
expect(0x6974F8, '83 B9 30 0C 00 00 01', 'only mode-1 held bullet enters native keepalive')
expect(0x69750E, '48 8B 01 FF 50 60', 'actual native held-bullet keepalive vcall')
expect(0x69759D, 'E9 4F 09 00 00', 'kept-alive bullet bypasses per-shot 214 snapshot')
expect(0x6976CF, '41 8B 87 14 02 00 00 41 89 84 24 90 00 00 00', 'new bullets inherit actual Weapon214')
print('PVP live-emitter audit: 9 native anchors; separate 314/318, native revival and held/new-shot split verified')
