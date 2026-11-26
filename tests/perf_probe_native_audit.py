"""Read the supported EDF.dll and prove the measurement probes' anchors (src/perf_probe.cpp): every call site it chains
is a near call to the stock function its constant names, the Present command's rcx is the renderer it reads the swap
chain from, and the render thread's Present is the vtable call at +0x40 (IDXGISwapChain::Present, slot 8).

No binary is loaded or executed. Missing --dll skips with 77.
"""
from pathlib import Path
import argparse
import re
import struct

parser = argparse.ArgumentParser()
parser.add_argument('--dll', default='')
args = parser.parse_args()
if not args.dll or not Path(args.dll).is_file():
    print('SKIP: EDF6_NATIVE_DLL required')
    raise SystemExit(77)
import pefile  # noqa: E402

pe = pefile.PE(args.dll, fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
binary = pe.get_memory_mapped_image()
source = (Path(__file__).resolve().parents[1] / 'src' / 'perf_probe.cpp').read_text(encoding='utf-8')


def value(name: str) -> int:
    return int(re.search(r'\b' + name + r'=0x([0-9A-Fa-f]+)', source).group(1), 16)


def values(name: str) -> list[int]:
    body = re.search(r'\b' + name + r'\[\]=\{([^}]+)\}', source).group(1)
    return [int(t.strip(), 0) for t in body.split(',')]


def call(at: int, target: int) -> None:
    assert binary[at] == 0xE8 and at + 5 + struct.unpack_from('<i', binary, at + 1)[0] == target, (hex(at), hex(target))


checked = 0
for site, target in (('kInputCall', 'kInput'), ('kFenceCall', 'kFence'), ('kPresentCall', 'kPresentCmd'),
                     ('kUpdateCall', 'kUpdate'), ('kDrawCall', 'kDraw'), ('kResolveCall', 'kResolve'),
                     ('kQueryCall', 'kQuery'), ('kPaletteCall', 'kPalette'),
                     ('kHavokBeforeCall', 'kHavokStep'), ('kHavokAfterCall', 'kHavokStep'),
                     ('kHavokBuildCall', 'kHavokBuild'), ('kHavokFinishCall', 'kHavokFinish'),
                     ('kCollideConstructCall', 'kCollideConstruct'), ('kCollideSleepCall', 'kCollideSleep')):
    call(value(site), value(target))
    checked += 1
for sites, target in (('kSubmitCalls', 'kSubmit'), ('kMeshCalls', 'kMesh')):
    for at in values(sites):
        call(at, value(target))
        checked += 1
# Both dispatch calls use the same argument setup and secondary queue vtable slot.
sig = bytes.fromhex('488b06488d5424204533c0488bceff90a8000000')
for at in values('kCollideDispatchCalls'):
    assert binary[at - 14:at + 6] == sig
    checked += 1
vt = value('kDefaultQueueInterface')
assert struct.unpack_from('<Q', binary, vt + 0xA8)[0] - pe.OPTIONAL_HEADER.ImageBase == 0x9EB5D0
# The mode check selects the runner containing the signed sleep call. The sleep thunk loads a handle, then tail-calls WaitForSingleObject.
assert value('kDefaultQueueMode') == 0x36C
assert binary[0xA15AFE:0xA15B05] == bytes.fromhex('458b806c030000')
jump = value('kCollideSleep') + 8
assert binary[jump:jump+3] == bytes.fromhex('48ff25')
slot = jump + 7 + struct.unpack_from('<i', binary, jump+3)[0]
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
assert any(entry.address - pe.OPTIONAL_HEADER.ImageBase == slot and entry.name == b'WaitForSingleObject'
           for library in pe.DIRECTORY_ENTRY_IMPORT for entry in library.imports)
# The mode-0 call uses the same +0x20 task interface that the graph passes to queue slot 4.
at = value('kModeZeroExecute')
assert binary[at-16:at+6] == bytes.fromhex('488d431048894588488d55a0488b4b58488b01ff5010')
assert struct.unpack_from('<Q', binary, vt + 0x20)[0] - pe.OPTIONAL_HEADER.ImageBase == 0xA15830
assert binary[0xA1583C:0xA15840] == bytes.fromhex('4c894258')
assert binary[0x9EDC05:0x9EDC09] == bytes.fromhex('488b5310')
assert binary[0x9EC4AB:0x9EC4AF] == bytes.fromhex('4c8d4320')
assert binary[0x9EC6BB:0x9EC6BF] == bytes.fromhex('4c8934c8')
for raw, name in re.findall(r'{(0x[0-9A-Fa-f]+),"([A-Za-z]+)"}', source):
    vtable = int(raw, 16)
    if not (0x18C0000 <= vtable < 0x18D0000):
        continue
    col = struct.unpack_from('<Q', binary, vtable - 8)[0] - pe.OPTIONAL_HEADER.ImageBase
    assert struct.unpack_from('<I', binary, col + 4)[0] == 0x20
    td = struct.unpack_from('<I', binary, col + 12)[0]
    native_name = binary[td + 16:td + 128].partition(b'\0')[0]
    assert native_name == f'.?AVhknp{name}Task@@'.encode(), native_name
    assert struct.unpack_from('<Q', binary, vtable + 0x10)[0] == struct.unpack_from('<Q', binary, vtable - 0x20 + 0x30)[0]
checked += 1
# PostCollide is split at the stock calls. The listener bridge recreates both volatile arguments.
for probe, callee in [('kContactDispatchCall','kContactDispatch'),('kPostCollideSignalCall','kPostCollideSignal'),
                      ('kEventCommandCall','kEventCommand')]:
    call(value(probe),value(callee))
at = value('kEventListenerCall')
assert binary[at:at+10] == bytes.fromhex('4c8bc5498bd741ff5118')
assert binary[0xE75F98:0xE75F9B] == bytes.fromhex('4c8b09')   # r9 is the listener vtable
assert binary[0xE76970:0xE76974] == bytes.fromhex('66394704') # event kind at event+4
assert binary[0x12FE960:0x12FE964] == bytes.fromhex('0fb64203') # command kind at record+3
checked += 4
# The Present command's rcx: lea rcx, [rip+disp] at 0x11A308E, the renderer.
assert binary[0x11A308E:0x11A3091] == bytes.fromhex('488d0d')
assert 0x11A3095 + struct.unpack_from('<i', binary, 0x11A3091)[0] == value('kRenderer')
# The render thread's Present: call qword ptr [rax+0x40] (slot 8) at 0x1118B45, rax the swap chain's vtable.
assert binary[0x1118B45:0x1118B48] == bytes.fromhex('ff5040')
# The occlusion query read: its wait flag is the caller's byte at [rsp+0x20] (mov [rsp+0x20], al at 0x11E4230), and it
# reads the query with GetData (call qword ptr [rax+0xE8]) at 0x1631860, flags 0 (mov dword ptr [rsp+0x20], 0).
assert binary[0x11E4230:0x11E4234] == bytes.fromhex('88442420')
assert binary[0x1631860:0x1631866] == bytes.fromhex('ff90e8000000')
assert binary[0x163184C:0x1631854] == bytes.fromhex('c744242000000000')
# System::update's steps a frame: the whole ticks due clamped (cmp ebx, ebp; then cmova) right after the tick wait.
assert binary[0x11A2F76:0x11A2F78] == bytes.fromhex('3bdd')
print(f'perf probe anchors ok ({checked} call sites)')
