"""Read-only supported-image audit of TimeShip capsule creation, throwing and hatch ownership.

No DLL is loaded or game process touched. Optional local EDF.dll/Root.cpk absence is skip 77.
"""
from pathlib import Path
import argparse
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('dll', type=Path)
args = parser.parse_args()
if not args.dll.is_file():
    print('SKIP: supported EDF.dll not available')
    raise SystemExit(77)
try:
    import pefile
    import rootcpk
    import sgo
except ImportError:
    print('SKIP: read-only PE/archive dependencies not available')
    raise SystemExit(77)
pe = pefile.PE(str(args.dll), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
checks = 1

def raw(at, expected, meaning):
    global checks
    expected = bytes.fromhex(expected)
    assert pe.get_data(at, len(expected)) == expected, (hex(at), meaning)
    checks += 1

def rel(at, length, target, meaning):
    global checks
    disp = struct.unpack('<i', pe.get_data(at + length - 4, 4))[0]
    assert at + length + disp == target, (hex(at), meaning)
    checks += 1

source = (ROOT / 'src/enemy_control_timeship_pods.cpp').read_text(encoding='utf-8')
for at, words in re.findall(r'\{(0x[0-9A-F]+),\{([^}]+)\}\}', source):
    expected = bytes(int(x.strip(), 16) for x in words.split(','))
    assert pe.get_data(int(at, 16), len(expected)) == expected, at
    checks += 1
# CreatePods AS wrapper's real uint32 argument reaches native root creation.
rel(0x1CB0CB, 5, 0x3EC680, 'CreatePods native root entry')
raw(0x3EC69D, '8bda488bf1', 'requested count in edx, root in rcx')
raw(0x3EC73B, '413bdd0f42c3', 'requested count capped to native SGO list length')
rel(0x3EC6B8, 7, 0x17B9350, 'actual EDF6_TimeShip SGO key')
rel(0x3EC6CC, 7, 0x17B9718, 'actual Capsule SGO key')
rel(0x3EC863, 5, 0x3ED3C0, 'creation reuses native attachment/factory initializer')
rel(0x3ED493, 5, 0x11945E0, 'native scene factory call')
rel(0x3ED5D1, 5, 0x118AF20, 'native parent-child registration')
raw(0x3EC875, '488b4218488bc848c1e10448034a0848ffc0', 'append index and increment, not a total-count cap')
raw(0x3EC887, '4889421848891948897108', 'native shared link appended')
raw(0x3EC8A2, '488b8e50110000', 'new root-owned link count')
rel(0x3EC8D6, 5, 0x54EE70, 'created actual capsule inherits native team')
raw(0x3EB047, '4889b740110000', 'constructor empty capsule array')
raw(0x3EB055, '4889b750110000', 'constructor zero count, not hardcoded eight')
# Actual root storage and child local ownership used for live membership admission.
raw(0x3ED197, '483b9150110000', 'native child query checks root count')
raw(0x3ED1A7, '488b8140110000', 'root capsule link array')
raw(0x3ED3E8, '48895118', 'each native link records owning root')
raw(0x3ED574, '4d896748498b4f5049897750', 'factory child weak object/control retained')
# Single drop and all-drop are lifecycle methods, not direct teleport writes.
raw(0x3ECC10, '488b8140110000448bd24d03d2', 'single-drop direct index lacks its own bounds gate')
rel(0x3ECC29, 5, 0x3F3780, 'single-drop immediate native release')
raw(0x3ECC2E, '410f10010f11416044894158', 'single-drop delayed target and countdown')
rel(0x1CB20F, 5, 0x3ECC40, 'AS DropAllPods actual root entry')
rel(0x3ECD83, 5, 0x3ED9C0, 'native activation before throwing')
rel(0x3ECF11, 5, 0x11A7EE0, 'native ground intersection, no guessed ground height')
rel(0x3ECF6A, 5, 0x3F3780, 'all-drop first immediate native release')
raw(0x3ECF71, '0f28442420410f11406041897858', 'remaining native delayed throw records')
raw(0x3ECF7F, '83c71e', 'native staggering is 30 frames')
rel(0x3F3814, 5, 0x118AEA0, 'real child detaches from parent registration')
raw(0x3F3819, '33c048894348488b4b5048894350', 'released native link clears its weak reference')
# AS command guards patch the actual dedicated call sites. The Crue detour moves
# exactly seven pushes and the initial stack save; no RIP-relative instruction.
rel(0x1CB48B, 5, 0x3ECC10, 'AS DropPod actual indexed native entry')
raw(0x1CABB0, '48895c24185556574154415541564157', 'whole relocated Crue prologue ends at 1CABC0')
raw(0x1CABC0, '488dac2440ffffff', 'Crue native frame begins after seven relocated pushes')
raw(0x1CAE06, '0fb68528010000', 'sixth AS bool at original entry rsp+30')
raw(0x1CAE10, 'f30f108520010000', 'fifth AS float at original entry rsp+28')
# ForceHatchOpen controls a persistent override; it is not a naked CAS play.
raw(0x1CB594, '488b8010110000889823010000', 'AS ForceHatchOpen reaches exact controller bool')
rel(0x3EACD6, 7, 0x17B9340, 'anchor controller vtable')
raw(0x3EACE0, '48895120', 'controller owner root')
# AS CreatePodCrue first resolves an actual capsule, creates real scene objects,
# then adds each child through the native capsule passenger API.
rel(0x1CACDB, 5, 0x3ED180, 'indexed capsule resolution')
rel(0x1CAF7D, 5, 0x1D8900, 'passenger uses actual enemy factory')
rel(0x1CAFAF, 5, 0x3FAE90, 'native capsule passenger attachment')
# Verify original immutable assets actually carry link paths, but no default crue.
archive = args.dll.parent / 'Root.cpk'
if archive.is_file():
    game = rootcpk.Game(str(args.dll.parent))
    for name in ('E611_TIMESHIP.SGO', 'E611_TIMESHIP_LIGHT.SGO', 'E611_TIMESHIP_FULL_LIGHT.SGO'):
        content = sgo.load(data=game.read('OBJECT', name))
        block = content['EDF6_TimeShip']
        pods = block['Capsule']
        assert len(pods) == 8, name
        assert all(set(pod) == {'link_node', 'object'} for pod in pods), name
        assert all(pod['object'].lower() == 'app:/object/e613_capsule.sgo' for pod in pods), name
        assert len({pod['link_node'] for pod in pods}) == 8, name
        checks += 4
    content = sgo.load(data=game.read('OBJECT', 'E613_CAPSULE.SGO'))
    assert content['xgs_scene_object_class'] == 'EDF6_TimeShip_Capsule'
    checks += 1
print(f'TimeShip pods/hatch native audit: {checks} checks')
