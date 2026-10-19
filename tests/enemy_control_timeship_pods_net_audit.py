"""Read-only supported-image evidence for accepted TimeShip capsule transactions.

Never loads EDF.dll or writes a game directory. Missing image/dependency skips 77.
"""
from pathlib import Path
import argparse
import re
import struct

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('dll', type=Path)
args = p.parse_args()
if not args.dll.is_file():
    print('SKIP: supported EDF.dll absent')
    raise SystemExit(77)
try:
    import pefile
except ImportError:
    print('SKIP: read-only PE dependency absent')
    raise SystemExit(77)
pe = pefile.PE(str(args.dll), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
checks = 1
source = (root / 'src/enemy_control_timeship_pods_net.cpp').read_text(encoding='utf-8-sig')
for at, body in re.findall(r'\{(0x[0-9A-F]+),\{([^}]+)\}\}', source):
    expected = bytes(int(x.strip(), 16) for x in body.split(','))
    assert pe.get_data(int(at, 16), len(expected)) == expected, at
    checks += 1


def raw(at, value, meaning):
    global checks
    expected = bytes.fromhex(value)
    assert pe.get_data(at, len(expected)) == expected, (hex(at), meaning)
    checks += 1


def relative(at, size, target, meaning):
    global checks
    delta = struct.unpack('<i', pe.get_data(at + size - 4, 4))[0]
    assert at + size + delta == target, (hex(at), meaning)
    checks += 1


relative(0x3ED44A, 7, 0x1762068, 'fresh native InitParamBase')
raw(0x3ED455, '0f57c0f30f7f45ff33f64889750f', 'zero InitParam data through +20')
raw(0x5456E0, '66448966084c8966104c896618', 'fresh network flags/control/entry are zero')
relative(0x3ED493, 5, 0x11945E0, 'capsule scene factory')
relative(0x3ED5D1, 5, 0x118AF20, 'parent attachment, not native net registration')
raw(0x118AF30, 'e86bffffff48895f38', 'detach old parent then set actual child owner')
raw(0x774511, '49837d18007512', 'unregistered child cannot emit init messages')
relative(0x3ECD83, 5, 0x3ED9C0, 'DropAll activates each original child')
relative(0x3ECF11, 5, 0x11A7EE0, 'host original ground ray')
relative(0x3ECF6A, 5, 0x3F3780, 'only observer callsite is DropAll immediate release')
raw(0x3ECF71, '0f28442420410f11406041897858', 'actual delayed destination and delay')
raw(0x3ECF7F, '83c71e', 'actual native thirty-frame index staggering')
relative(0x3ECC29, 5, 0x3F3780, 'DropOne calls real release with supplied exact target')
raw(0x3ECC2E, '410f10010f11416044894158', 'DropOne sets supplied target/delay')
relative(0x3F3814, 5, 0x118AEA0, 'release performs actual native parent detachment')
raw(0x3F3819, '33c048894348488b4b5048894350', 'native release clears only actual weak link')
# Registered capsules do NOT broadcast their dynamic fall message. Accepted
# state entry therefore occurs once; HP/death/generic pose remain native.
raw(0x1FEEEC8, '00000080', 'dynamic message counter starts80000000')
relative(0xBB74, 5, 0x11BAF30, 'fall message dynamic allocator')
relative(0xBB79, 6, 0x20B33DC, 'dynamic fall message initializer')
relative(0x3F37C7, 6, 0x20B33DC, 'native release reads same dynamic fall message')
relative(0x54AA97, 5, 0x774260, 'pre-message native network filter')
raw(0x7742CE, '418b08bfffffffff81e9040102000f84a700000083f9010f8526020000', '20104/20105 special sends')
raw(0x77452A, '418b0f8d81000000fc83f8047746', 'send remap limited4000000..4')
raw(0x77457E, '81f9000000100f85c7000000', 'remaining send special requiresdamage10000000')
relative(0x54A9CC, 5, 0x7746C0, 'post-message native network filter')
raw(0x774725, '4181fa000000100f857201000041f64160400f8567010000', 'post-message only sendsdamage10000000')
relative(0x3FB67E, 7, 0x20B33DC, 'capsule real fall handler recognizes dynamic message')
relative(0x3FB690, 7, 0x3FD4A0, 'actual falling state callback')
relative(0x3FB6DA, 5, 0x3FC340, 'native local state transition')
for slot, rva in ((9, 0x3FB4D0), (10, 0x54AA50), (11, 0x54A970), (53, 0x318320)):
    assert struct.unpack('<Q', pe.get_data(0x17BA800 + slot * 8, 8))[0] - pe.OPTIONAL_HEADER.ImageBase == rva
    checks += 1
assert struct.unpack('<Q', pe.get_data(0x17BA9C0 + 15 * 8, 8))[0] - pe.OPTIONAL_HEADER.ImageBase == 0x318350
checks += 1
raw(0x318383, '83ea04744c83fa017421', 'native receive type4/type5 dispatch')
relative(0x3183DE, 5, 0x318110, 'type4 generic walking pose decoder')
raw(0x3183B5, '488b07488b98a8010000', 'type5 invokes owner slot53')
raw(0x318320, 'c20000', 'capsule custom event receive stub cannot replayfall')
raw(0x54C171, 'f68728010000017577', 'remote flag1 suppresses authoritative generic sends')
# A retained native strong root keeps the real prototype and link roster alive;
# logical HP/death flags do not justify treating an accepted child drop as gone.
raw(0x3EC077, '488d8b38110000', 'deleting destructor owns real capsule roster1138')
relative(0x3EC07E, 5, 0x3EFF40, 'root destructor releases actual roster')
raw(0x3EC083, '488d8b28110000', 'root destructor owns capsule prototype1128')
relative(0x3EC08A, 5, 0x3EBCA0, 'prototype cleanup is inside the deleting destructor')
raw(0x3EFF93, 'f00fc14308', 'roster destructor drops real child holder strong refs')
raw(0x3EF96B, '83e8018941587509', 'native slot5 decrements delayed capsule release clock')
relative(0x3EF977, 5, 0x3F3780, 'delayed releases use the same genuine native fall/detach function')
# Required transaction properties, alongside independent production-spy runtime tests.
for token in ('sizeof(Event)==488', 'NativeActionRole(l.object,&authority)',
              'RegisterChild(l,body,event.rows[i].id,authority)', 'MatchesId(body,event.rows[i].id)',
              'Fn<RegisterFn>(0x781950)', 'std::memcmp(expected,row.id,32)',
              '__finally {dropping=nullptr;}', 'ReleaseObserved', 'Rollback(l,event.before)',
              'if(event.kind==2)Fn<VoidFn>(0x3ED9C0)', 'Fn<SingleFn>(0x3ECC10)',
              'ExecuteTimeShipPodsNetworkCreate', 'ExecuteTimeShipPodsNetworkDropOne',
              'sourceSeed=seed', 'r->faulted=true', 'r->count==kQueued',
              'ReleasePinned', 'r.strong=true', 'Ready(l,Find(l)==nullptr)',
              'event.kind=4', 'Settled(l,*r,event)', 'r.strongDtorFailed'):
    assert token in source, token
    checks += 1
assert 'InSession()' not in source, 'bound authority must survive temporary session-status probe changes'
checks += 1
print(f'TimeShip accepted capsule native audit: {checks} checks')
