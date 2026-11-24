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
                     ('kUpdateCall', 'kUpdate'), ('kDrawCall', 'kDraw'), ('kResolveCall', 'kResolve')):
    call(value(site), value(target))
    checked += 1
for sites, target in (('kSubmitCalls', 'kSubmit'), ('kMeshCalls', 'kMesh')):
    for at in values(sites):
        call(at, value(target))
        checked += 1
# The Present command's rcx: lea rcx, [rip+disp] at 0x11A308E, the renderer.
assert binary[0x11A308E:0x11A3091] == bytes.fromhex('488d0d')
assert 0x11A3095 + struct.unpack_from('<i', binary, 0x11A3091)[0] == value('kRenderer')
# The render thread's Present: call qword ptr [rax+0x40] (slot 8) at 0x1118B45, rax the swap chain's vtable.
assert binary[0x1118B45:0x1118B48] == bytes.fromhex('ff5040')
# System::update's steps a frame: the whole ticks due clamped (cmp ebx, ebp; then cmova) right after the tick wait.
assert binary[0x11A2F76:0x11A2F78] == bytes.fromhex('3bdd')
print(f'perf probe anchors ok ({checked} call sites)')
