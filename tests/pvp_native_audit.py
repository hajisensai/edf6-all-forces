"""Read-only EDF.dll contracts for PVP radar/lock/weapon ownership.

Pass the supported native file; missing input returns 77. This reads PE bytes,
never loads or executes the game DLL. It proves source-level native contracts,
not an in-game radar/lock-on or multiplayer acceptance result.
"""
from pathlib import Path
import argparse
import struct
import sys

pvp_parser=argparse.ArgumentParser(description=__doc__)
pvp_parser.add_argument('edf_dll',nargs='?',type=Path)
pvp_args=pvp_parser.parse_args()
if not pvp_args.edf_dll or not pvp_args.edf_dll.is_file():
    print('SKIP: pass an existing supported EDF.dll');raise SystemExit(77)
import pefile
pvp_pe=pefile.PE(str(pvp_args.edf_dll),fast_load=True)
if pvp_pe.FILE_HEADER.TimeDateStamp!=0x678CCB46 or pvp_pe.FILE_HEADER.Machine!=0x8664:
    raise RuntimeError('unsupported native EDF.dll profile')

def expect(rva,hex_bytes,label):
    expected=bytes.fromhex(hex_bytes)
    if pvp_pe.get_data(rva,len(expected))!=expected:
        raise RuntimeError(f'{label}: native bytes changed at {rva:#x}')

def rel_target(rva,prefix,label):
    expect(rva,prefix,label)
    prefix_bytes=bytes.fromhex(prefix)
    displacement=struct.unpack('<i',pvp_pe.get_data(rva+len(prefix_bytes),4))[0]
    return rva+len(prefix_bytes)+4+displacement

expect(0x82B541,'48 8B 91 80 07 00 00 48 85 D2 74 CF 83 7A 08 00','radar owner weak control')
expect(0x82B553,'4C 8D B9 78 07 00 00','radar owner weak object')
source_type=rel_target(0x82B58C,'4C 8D 05','radar dynamic-cast source')
target_type=rel_target(0x82B585,'4C 8D 0D','radar dynamic-cast target')
for rva,name in [(source_type,b'.?AVSceneObject@game@xgs@@\0'),(target_type,b'.?AVSoldierBase@@\0')]:
    if pvp_pe.get_data(rva+16,len(name))!=name:raise RuntimeError('radar owner RTTI changed')
expect(0x82B59D,'48 8B F0 48 89 85 70 01 00 00','fresh SoldierBase pointer in RSI and local')
expect(0x82B89A,'8B 96 14 03 00 00','first hostile pass reads current soldier team')
if rel_target(0x82B8CA,'E8','first hostile walk')!=0x5E0F20:raise RuntimeError('first hostile walk changed')
# RSI is used as temporary allocator storage in the intervening list build, then
# restored from this update's saved SoldierBase pointer before its second walk.
expect(0x82BB09,'48 8B B5 70 01 00 00','second pass restores this frame soldier pointer')
expect(0x82BB80,'8B 96 14 03 00 00','second hostile pass reads current soldier team')
if rel_target(0x82BBB0,'E8','second hostile walk')!=0x5E0F20:raise RuntimeError('second hostile walk changed')
expect(0x82BB42,'41 B8 02 00 00 00 48 8D 54 24 30 49 8B CE','friendly pass literal two and HUD context')
if rel_target(0x82BB50,'E8','friendly one-team walk')!=0x82B220:raise RuntimeError('friendly one-team walk changed')
expect(0x82B220,'48 89 5C 24 10 57 48 83 EC 20 48 C7 42 18 00 00','friend walk entry')
expect(0x82B235,'49 63 C0 48 6B D8 38','friend walk indexes requested native team row')
expect(0x689E75,'48 8B 53 08','lock filter reads owner')
expect(0x689E80,'44 8B 82 14 03 00 00 8B 90 14 03 00 00','lock filter reads live owner and target teams')
if rel_target(0x689E92,'E9','lock enemy predicate')!=0x5E1540:raise RuntimeError('lock enemy predicate changed')
expect(0x6304C7,'BD 05 00 00 00','empty native vehicle fallback team five')
expect(0x630589,'41 B0 01 8B D5 48 8B CF','native vehicle team recompute arguments')
if rel_target(0x630591,'E8','native vehicle SetTeam')!=0x54EE70:raise RuntimeError('vehicle team recompute changed')
expect(0x6976CF,'41 8B 87 14 02 00 00','fire reads weapon emitter team')
print('PVP native audit: current owner radar teams, isolated friendly row, lock hostility and weapon emitter team verified')
