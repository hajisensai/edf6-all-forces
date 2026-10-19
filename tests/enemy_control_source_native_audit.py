"""Read-only native source-protection contract for enemy-control leases."""
from pathlib import Path
import argparse
import re
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--dll', type=Path)
args = parser.parse_args()
if args.dll is None or not args.dll.is_file():
    print('SKIP: supported EDF.dll is required')
    raise SystemExit(77)
try:
    import pefile
except ImportError:
    print('SKIP: pefile is required')
    raise SystemExit(77)
pe = pefile.PE(str(args.dll), fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
image = pe.get_memory_mapped_image()
# Native ApplyDamage keeps forced script damage (GDI 0x40) meaningful but skips
# ordinary HP changes under GameObjectBase flag1. It retains normal hit events.
assert image[0x5480F8:0x548109] == bytes.fromhex('f68780030000017408a8400f84b7020000')
# The real hostile-team enumeration never proposes a notTarget (0x80) object.
assert image[0x5E0FB2:0x5E0FC8] == bytes.fromhex('488b532080bae8020000007516f6828003000080750d')
source = (Path(__file__).resolve().parents[1] / 'src' / 'enemy_control.cpp').read_text(encoding='utf-8-sig')
assert 'session.sourceOwnedFlags=0x81u&~flags;' in source
assert 'flags|0x81u' in source
assert 'flags&~session.sourceOwnedFlags' in source
assert '!RestoreSource(*s)' in source
assert source.index('enemy_native::Restore(&s->native)') < source.index('const bool returned=RestoreTable(s)')
print('enemy control source protection native audit: 10 checks passed')
