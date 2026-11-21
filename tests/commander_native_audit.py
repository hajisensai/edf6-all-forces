"""Read-only native contract of the commander mode's stock death (src/commander.cpp KillLocalPlayer,
docs/feature-2026-11-21-commander-mode.md 2.1): SendMessage, GameObjectBase slot 9's switch and damage case, ApplyDamage,
and the soldiers' message slots that pass 0x10000000 on to it."""
from pathlib import Path
import argparse
import re

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
SITES = {
    0x1DA480: '48895c241857 4883ec20 488b01 488bf9',          # SendMessage(obj, msg, data): slots 10, 9, 11
    0x54A530: '48895c2410 56 57 4156 4881ec80000000',          # GameObjectBase slot 9
    0x54A555: '81c2000000f0 83fa0f',                           # add edx,0xF0000000; cmp edx,0xF: 0x10000000 is case 0
    0x54A579: '0fb69fe8020000 488bd6 488bcf e8a5d6ffff',       # case 0: +0x2E8 kept, call ApplyDamage
    0x547C30: '488bc4 48895818 55 56 57 4154 4155 4156',       # ApplyDamage(obj, gdi)
    0x598FD0: '48895c2420 55 56 57 4156 4157 488d6c24',        # AssultSoldier / PaleWing / Engineer slot 9
    0x567250: '48895c2418 4889742420 55 488bec 4883',          # HeavyArmor slot 9
    0x572810: '488bc4 55 53 56 57 4154 4156 4157 488d68',      # HumanBase slot 9
    0x5728D4: '8bc2 2d10000010 0f84a9010000 83e805 0f8493010000 83e801 7424 83f80f 740a e8377cfdff',
}
checks = 0
for rva, text in SITES.items():
    want = bytes.fromhex(text.replace(' ', ''))
    assert image[rva:rva + len(want)] == want, hex(rva)
    checks += 1
# The call at 0x54A586 reaches ApplyDamage; HumanBase's default reaches GameObjectBase slot 9 (0x5728F4).
rel = int.from_bytes(image[0x54A587:0x54A58B], 'little', signed=True)
assert 0x54A58B + rel == 0x547C30
rel = int.from_bytes(image[0x5728F5:0x5728F9], 'little', signed=True)
assert 0x5728F9 + rel == 0x54A530
checks += 2
source = (Path(__file__).resolve().parents[1] / 'src' / 'commander.cpp').read_text(encoding='utf-8-sig')
assert re.search(r'kDamageMessage=0x10000000u', source)
assert 'kSendMessage=0x1DA480' in source and 'kApplyDamage=0x547C30' in source
assert 'Put<std::uint16_t>(gdi,0x60,0x40)' in source and 'Put<void*>(gdi,0x78,hit)' in source
checks += 3
print(f'commander native audit: {checks} checks passed')
