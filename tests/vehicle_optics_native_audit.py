"""Native SetWorld and partial descendant updates on real generated optic trees.

Private DONT_RESOLVE mapping, no DllMain/game process. Runtime bone records are
populated from the serialized MDB's parent/child/DFS contracts; the engine's two
matrix propagation functions execute unchanged. This does not run the renderer.
"""
import ctypes as C
from pathlib import Path
import struct
import sys

if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8 or len(sys.argv) != 2:
    print('SKIP: supported EDF.dll and Root.cpk required on Windows x64')
    raise SystemExit(77)
path = Path(sys.argv[1])
if not path.is_file() or not (path.parent/'Root.cpk').is_file():
    print('SKIP: supported EDF.dll and Root.cpk required')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Native safety assertions must be enabled')
import pefile
pe = pefile.PE(str(path), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664
inherit = bytes.fromhex('807a80010f852e01000048634288')
assert pe.get_data(0x1100080, len(inherit)) == inherit
set_world = bytes.fromhex(
    'c681b0000000010f2802488b41100f2980b00000000f284a100f2988c0000000'
    '0f2842200f2980d00000000f284a3033d20f2988e0000000e943f4ffff')
assert pe.get_data(0x1100B90, len(set_world)) == set_world
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'pylib'))
import rootcpk
import vehicle_optics as optics
from mdb import bind_world, ident, mdb_read, mdb_write, mmul, rab_read
game = rootcpk.Game(str(path.parent))
kernel = C.WinDLL('kernel32', use_last_error=True)
kernel.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
kernel.LoadLibraryExW.restype = C.c_void_p
kernel.FreeLibrary.argtypes = [C.c_void_p]
base = kernel.LoadLibraryExW(str(path.resolve()), None, 1)
assert base
storage = []


def aligned(size):
    buf = C.create_string_buffer(size+15)
    storage.append(buf)
    return (C.addressof(buf)+15) & ~15


def put(at, fmt, *values):
    b = struct.pack(fmt, *values)
    C.memmove(at, b, len(b))


def matrix(at):
    return struct.unpack('<16f', C.string_at(at, 64))


compose = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p)(base+0x1100B90)
partial = C.WINFUNCTYPE(None, C.c_void_p, C.c_int)(base+0x1100010)
try:
    checked = 0
    for spec in optics.MODELS:
        stock = mdb_read(next(f for f in rab_read(game.read('OBJECT', spec.stem+'.MRAB')).files
                              if f.name.lower() == spec.stem.lower()+'.mdb').data)
        md = mdb_read(mdb_write(optics.mark_model(stock, spec, game.read('OBJECT', spec.stem+'.SGO'))))
        inst, records, pose = aligned(0xC0), aligned(len(md.bones)*0x110), aligned(64)
        put(inst+0x10, '<Q', records)
        put(inst+0x20, '<Q', len(md.bones))
        for b in md.bones:
            rec = records+b.index*0x110
            depth = 0
            p = b.parent
            while p >= 0:
                depth += 1
                p = md.bones[p].parent
            # The native loader derives the contiguous subtree endpoint from
            # the MDB depth deltas. Do the same, not a search for our marker.
            end = b.index+1
            next_depth = depth + b.depth_delta
            while end < len(md.bones) and next_depth > depth:
                next_depth += md.bones[end].depth_delta
                end += 1
            put(rec+8, '<B', 1)
            put(rec+0xC, '<4i', b.index, b.parent, b.child, b.sibling)
            put(rec+0x20, '<i', end if end < len(md.bones) else -1)
            put(rec+0x70, '<16f', *b.local)
        root = ident()
        root[12:15] = [10.,20.,30.]
        put(pose, '<16f', *root)
        compose(inst, pose)
        expected = bind_world(md)
        for lens in spec.lenses:
            i = md.bone_index(lens.marker)
            result = matrix(records+i*0x110+0xB0)
            want = mmul(expected[i], root)
            assert max(abs(a-b) for a,b in zip(result,want)) < 1e-5, (spec.stem,lens.marker,'full tree')
            checked += 1
            # Simulate the real turret updater writing its parent's world,
            # then only refreshing that parent's contiguous descendants.
            compose(inst, pose)
            parent = md.bones[i].parent
            turn = [0.,0.,-1.,0., 0.,1.,0.,0., 1.,0.,0.,0., 0.,0.,0.,1.]
            moved = mmul(turn, matrix(records+parent*0x110+0xB0))
            put(records+parent*0x110+0xB0, '<16f', *moved)
            partial(inst, parent)
            result = matrix(records+i*0x110+0xB0)
            want = mmul(md.bones[i].local, moved)
            assert max(abs(a-b) for a,b in zip(result,want)) < 1e-5, (spec.stem,lens.marker,'partial tree')
            checked += 1
            print('PASS',spec.stem,lens.marker,'native full and parent-only updates')
        storage.clear()
    print(checked, 'native optic pose checks passed')
finally:
    kernel.FreeLibrary(base)
