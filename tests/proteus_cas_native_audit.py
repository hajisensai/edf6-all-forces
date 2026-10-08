"""Execute EDF.dll's full CANM track evaluator on actual generated CAS files.

Private DONT_RESOLVE mapping, no game entrypoint or installed-file writes.
The negative control keeps every pointer valid but misaligns the channel table,
reproducing the 2026-10-08 load crash at the evaluator's MOVAPS.
"""
import ctypes as C
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import cas_pose
import gamedir
import proteus_model
import rootcpk

path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(gamedir.find_or_dev() or '.') / 'EDF.dll'
if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8 or not path.is_file() or not (path.parent / 'Root.cpk').is_file():
    print('SKIP: native CANM audit needs Windows x64, supported EDF.dll and Root.cpk')
    raise SystemExit(77)
try:
    import pefile
except ImportError:
    print('SKIP: native CANM audit requires optional pefile dependency')
    raise SystemExit(77)
pe = pefile.PE(str(path), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
assert pe.get_data(0x1160620, 9) == bytes.fromhex('0f280366410f7f4510')
kernel = C.WinDLL('kernel32', use_last_error=True)
kernel.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint32]
kernel.LoadLibraryExW.restype = C.c_void_p
module = kernel.LoadLibraryExW(str(path), None, 1)
assert module
evaluate = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p)(module + 0x1160500)
sample_time = C.WINFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_float)(module + 0x11602D0)


def aligned(data):
    backing = C.create_string_buffer(len(data) + 15)
    address = (C.addressof(backing) + 15) & ~15
    C.memmove(address, data, len(data))
    return backing, address


def evaluate_rotation(data):
    pose = cas_pose.CasPose(data)
    raw, base = aligned(data)
    out, output = aligned(bytes(64))
    # The dump identifies wake_up[0], globalSRT, constant quaternion channel 7.
    for clip in sorted(pose.clips, key=lambda c: c.name != 'wake_up'):
        for index, track in enumerate(clip.tracks):
            if track.rotation < 0:
                continue
            channel = pose.points + track.rotation * 48
            if pose.unpack('<I', channel + 36)[0] != 2:
                continue
            runtime = C.create_string_buffer(struct.pack('<QQ', base + pose.canm, base + clip.at))
            mapping = C.create_string_buffer(bytes((index, 2)))
            frame = C.create_string_buffer(struct.pack('<If', 0, 0.))
            evaluate(C.addressof(runtime), output, C.addressof(mapping), C.addressof(frame))
            assert C.string_at(output + 16, 16) == data[channel:channel + 16]
            return pose.points % 16
    raise AssertionError('real stock CAS lacks a constant quaternion track')


def evaluate_channel(data, clip, index, mask, frame=0, blend=0.):
    raw, base = aligned(data)
    out, output = aligned(bytes(64))
    pose = cas_pose.CasPose(data)
    runtime = C.create_string_buffer(struct.pack('<QQ', base + pose.canm, base + clip.at))
    mapping = C.create_string_buffer(bytes((index, mask)))
    sample = C.create_string_buffer(struct.pack('<If', frame, blend))
    evaluate(C.addressof(runtime), output, C.addressof(mapping), C.addressof(sample))
    offset = {1: 0, 2: 16, 4: 32}[mask]
    return C.string_at(output + offset, 16)


def sample(data, clip, time):
    raw, base = aligned(data)
    runtime = C.create_string_buffer(struct.pack('<QQ', base + cas_pose.CasPose(data).canm, base + clip.at))
    out = C.create_string_buffer(8)
    sample_time(C.addressof(runtime), C.addressof(out), time)
    return bytes(out)


def check_clips(stock, generated):
    before, after = cas_pose.CasPose(stock), cas_pose.CasPose(generated)
    checks = 0
    for old, new in zip(before.clips, after.clips):
        duration, step, frames = before.unpack('<ffI', old.at + 8)
        assert frames >= 2 and step > 0
        for time in (0., duration * .5, duration, duration + step):
            assert sample(stock, old, time) == sample(generated, new, time)
            checks += 1
        # At least one original animated translation per clip: beginning,
        # interpolation and final sample, using the actual stored key count.
        for index, track in enumerate(old.tracks):
            if track.translation < 0:
                continue
            at = before.points + track.translation * 48
            kind, count = before.unpack('<II', at + 36)
            if kind != 1 or count < 2:
                continue
            for frame, blend in ((0, 0.), (0, .5), (count-2, 1.)):
                assert evaluate_channel(stock, old, index, 1, frame, blend) == evaluate_channel(generated, new, index, 1, frame, blend)
                checks += 1
            break
        for index in range(len(old.tracks), len(new.tracks)):
            track = new.tracks[index]
            assert track.name in proteus_model.SHIELD_BONES
            assert evaluate_channel(generated, new, index, 4) == bytes(16)
            checks += 1
    return checks


def misalign(data):
    pose = cas_pose.CasPose(data)
    out = bytearray(data)
    out.extend(bytes((-len(out)) % 16 + 4))
    new_points = len(out)
    for index in range(pose.channel_count):
        old = pose.points + index * 48
        new = len(out)
        row = bytearray(data[old:old + 48])
        relative = struct.unpack_from('<i', row, 32)[0]
        if relative:
            struct.pack_into('<i', row, 32, old + relative - new)
        out.extend(row)
    struct.pack_into('<i', out, pose.canm + 20, new_points - pose.canm)
    return bytes(out)


game = rootcpk.Game(str(path.parent))
for host in proteus_model.HOSTS:
    stock = game.read('OBJECT', host + '.CAS')
    generated = proteus_model.animation(stock)
    assert evaluate_rotation(stock) == 0
    try:
        evaluate_rotation(misalign(generated))
    except OSError as failure:
        print(f'PASS {host}: misaligned native negative control rejected ({failure})')
    else:
        raise AssertionError('misaligned MOVAPS negative control unexpectedly passed')
    assert evaluate_rotation(generated) == 0
    print(f'PASS {host}: full native track evaluator reads generated quaternion; {check_clips(stock, generated)} native clip/key/hidden-scale checks')
    installed = path.parent / 'Mods' / 'OBJECT' / f'EDF6VC_{host}.CAS'
    if installed.is_file() and cas_pose.CasPose(installed.read_bytes()).points % 16:
        try:
            evaluate_rotation(installed.read_bytes())
        except OSError as failure:
            print(f'CONFIRMED installed {host}: same native fault ({failure}); file not modified')
        else:
            raise AssertionError('installed unaligned CAS did not reproduce')
