"""Read the supported EDF.dll and prove the lens view's native anchors (src/scope_lens.cpp, docs/scope-pip-re.md §4-§5).

Every guard scope_lens.cpp checks at install is compared with the DLL, every redirected call site is shown to call the
callee scope_lens.cpp chains to, and the facts the design rests on are read from the code itself: the slot layout, the
camera queue's join, the TraceCamera factory and its stereo_camera caller, the command ring's one-unit command, the
post's block copied whole when recorded, the recording before the screen HUD. No binary is loaded or executed. Missing
--dll skips with 77.
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
import pefile

pe = pefile.PE(args.dll, fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
binary = pe.get_memory_mapped_image()
source = (Path(__file__).resolve().parents[1] / 'src/scope_lens.cpp').read_text(encoding='utf-8')


def value(name):
    return int(re.search(r'\b' + name + r'=0x([0-9A-Fa-f]+)', source).group(1), 16)


def signature(name):
    body = re.search(r'\b' + name + r'\[\]=\{([^}]+)\}', source).group(1)
    return bytes(int(token.strip(), 0) for token in body.split(','))


def evaluate(expression):
    expression = expression.strip()
    match = re.fullmatch(r'(k\w+)-(\d+)', expression)
    if match:
        return value(match.group(1)) - int(match.group(2))
    if expression.startswith('k'):
        return value(expression)
    return int(expression, 0)


def target(at):
    assert binary[at] == 0xE8, hex(at)
    return at + 5 + struct.unpack_from('<i', binary, at + 1)[0]


def call(at, to):
    assert target(at) == to, (hex(at), hex(target(at)), hex(to))


def rip(at, size):
    """The address a RIP-relative operand ending at at+size points to (its disp32 the instruction's last 4 bytes)."""
    return at + size + struct.unpack_from('<i', binary, at + size - 4)[0]


# 1. Every guard of kGuards, byte for byte.
table = re.search(r'const Guard kGuards\[\]=\{(.*?)\n\};', source, re.S).group(1)
guards = re.findall(r'\{([^,]+),(k\w+),sizeof\(\2\),"([^"]+)"\}', table)
assert len(guards) == 22, len(guards)
for expression, name, what in guards:
    at = evaluate(expression)
    expected = signature(name)
    assert binary[at:at + len(expected)] == expected, (what, hex(at))

# 2. The eight redirected calls: each guard ends in the call, which calls the callee scope_lens.cpp chains to.
for site, callee in (('kProjectCall', 'kProject'), ('kGbufferCall', 'kGbuffer'), ('kPostCall', 'kPost'),
                     ('kPlainCall', 'kPlain'), ('kCopyCall', 'kCopy'), ('kHistoryCall', 'kHistory'),
                     ('kModeCall', 'kModeTail'), ('kSubmitCall', 'kSubmit')):
    call(value(site), value(callee))
    assert any(evaluate(e) + len(signature(n)) == value(site) + 5 for e, n, _ in guards), site
# All inside the recording 0x11978D0, which System::update calls after the camera queue's join.
for site in ('kProjectCall', 'kGbufferCall', 'kPostCall', 'kPlainCall', 'kCopyCall', 'kHistoryCall', 'kModeCall',
             'kSubmitCall'):
    assert 0x11978D0 < value(site) < 0x11988D0, site
call(0x1198F61, 0x1195A80)
call(0x1198FA4, 0x11978D0)
assert 0x1198F61 < 0x1198FA4
# The screen HUD stage reads the 4 slots' current cameras later in the main loop (0x7058B7 -> 0x11977F0 -> 0x118E500);
# the lens camera is current only inside the recording.
call(0x7058B7, 0x11977F0)
call(0x1197854, 0x118E500)

# 3. The slot layout scope_lens.cpp reads (system+0x4D0, 0x188 apart).
assert value('kSlots') == 0x4D0 and value('kSlot') == 0x188 and value('kSystemRef') == 0x20B2958
assert binary[0x1197A80:0x1197A87] == bytes.fromhex('4869db88010000')          # imul rbx,rbx,188h (the slot index)
assert binary[0x1197A90:0x1197A98] == bytes.fromhex('488b943be0040000')        # mov rdx,[rbx+rdi+4E0h]: +0x10 the current control
assert binary[0x1197B37:0x1197B3E] == bytes.fromhex('4c69f088010000')          # imul r14,rax,188h
assert binary[0x1197B43:0x1197B4A] == bytes.fromhex('488d9704050000')          # lea rdx,[rdi+504h]: +0x34 the rect
assert binary[0x1198681:0x1198689] == bytes.fromhex('83bc3bd004000000')        # cmp [rbx+rdi+4D0h],0: +0 the mode
assert binary[0x11987A4:0x11987AC] == bytes.fromhex('83bc3bd004000003')        # cmp [rbx+rdi+4D0h],3
assert binary[0x11986F4:0x11986FC] == bytes.fromhex('80bc3b9805000000')        # cmp [rbx+rdi+598h],0: +0xC8 the history
assert binary[0x11986E8:0x11986EF] == bytes.fromhex('4c8db1c0050000')          # lea r14,[rcx+5C0h]: +0xF0 a colour
# The post's block: system+0x2A0 written for the slot, then system+0x280 passed in r9.
assert binary[0x119872A:0x1198731] == bytes.fromhex('48898fa0020000')
assert binary[0x1198731:0x1198738] == bytes.fromhex('4c8d8f80020000')
# 0x112C230 copies its r9 block (0x48 bytes) when it records: movups [rsi], [rsi+10h], [rsi+20h], [rsi+30h], movsd [rsi+40h].
assert binary[0x112C253:0x112C256] == bytes.fromhex('498bf1')                  # mov rsi,r9
for at, pattern in ((0x112C368, '0f1006'), (0x112C36F, '0f104e10'), (0x112C377, '0f104620'), (0x112C37F, '0f104e30'),
                    (0x112C387, 'f20f104640')):
    assert binary[at:at + len(pattern) // 2] == bytes.fromhex(pattern), hex(at)
# 0x112B230's argument is three 0x20-byte handles copied with 0x8F000 (+0, +0x20, +0x40).
call(0x112B2B2, 0x8F000)
call(0x112B2BF, 0x8F000)
call(0x112B2CC, 0x8F000)
assert binary[0x112B2B7:0x112B2BB] == bytes.fromhex('488d5720') and binary[0x112B2C4:0x112B2C8] == bytes.fromhex('488d5740')
# The recording's locals: constructed 0x1120500, destroyed 0x11207A0 at the slot's end.
call(0x11987DE, 0x11207A0)
call(0x11987E7, 0x11207A0)
call(0x11987F0, 0x11207A0)
assert value('kHandleNew') == 0x1120500 and value('kHandleAssign') == 0x8F000 and value('kHandleFree') == 0x11207A0

# 4. Viewport set (0x1198C40): rect -> +0x34, target size -> +0x44 / +0x48, then the targets (0x11997A0).
assert binary[0x1198C6A:0x1198C6E] == bytes.fromhex('0f114134')
assert binary[0x1198C7E:0x1198C86] == bytes.fromhex('44895144') + bytes.fromhex('44894948')
call(0x1198C86, 0x11997A0)
# The targets: none without cameras (+0x20), +0x28 built.
assert binary[0x11997D1:0x11997D6] == bytes.fromhex('48837920' + '00')
assert binary[0x11997D8:0x11997DD] == bytes.fromhex('48837928' + '00')
# The queue's join (0x1195A80): the slot's list (+0x18) takes it, set camera, the targets.
call(0x1195B5C, 0x119B410)
call(0x1195B7C, 0x1193FA0)
call(0x1195B8B, 0x11997A0)
assert binary[0x1195B44:0x1195B47] == bytes.fromhex('8b4860')                  # the camera's +0x60: its viewport
# The list node: next, prev, then the shared_ptr at +0x10 / +0x18; the count at +8 of the list (+0x20 of the slot).
assert binary[0x119B473:0x119B477] == bytes.fromhex('48894a10')
assert binary[0x119B47B:0x119B47F] == bytes.fromhex('48894218')
assert binary[0x119B47F:0x119B483] == bytes.fromhex('48ff4308')
# Set camera (0x1193FA0): the old camera's vtbl[7], the new one's vtbl[6], the argument's weak reference released.
assert binary[0x1193FFC:0x1193FFF] == bytes.fromhex('ff5038')
assert binary[0x11940CD:0x11940D0] == bytes.fromhex('ff5030')
assert binary[0x1194100:0x1194104] == bytes.fromhex('498b4f08') and binary[0x1194109:0x119410E] == bytes.fromhex('f00fc1710c')

# 5. The camera: the factory makes a TraceCamera that follows desc+0x80 and returns a weak_ptr (weaks +1, the lock's use
# released); the engine's own stereo_camera makes exactly this second view of viewport 0's camera.
assert binary[0x82F84:0x82F89] == bytes.fromhex('488b5c2470')                  # desc: the 5th argument
assert binary[0x82F89:0x82F8D] == bytes.fromhex('410f2801')                    # movaps: the matrix 16-aligned
assert binary[0x82FB7:0x82FBB] == bytes.fromhex('44894358')                    # desc+0x58 the viewport
assert binary[0x83090:0x83094] == bytes.fromhex('f0ff430c')                    # out: weaks +1
assert binary[0x1763E28:0x1763E28 + 28] == 'stereo_camera\0'.encode('utf-16-le')  # the command's name
assert rip(0xA3C9A, 7) == 0xACC80                                               # registered with its implementation
call(0xACCB6, 0x1195BE0)                                                        # viewport 0's camera
call(0xACDEF, 0x82F60)
assert rip(0xACD16, 7) == value('kDescVtable')                                 # the description's vtable
assert binary[0xACD43:0xACD4B] == bytes.fromhex('48895510' + '488b4d18')       # desc+0x80 / +0x88 at rbp+0x10 / +0x18
assert rip(0x105207, 7) == 0x1769218                                           # TraceCamera's vtable
base = pe.OPTIONAL_HEADER.ImageBase
slots = [struct.unpack_from('<Q', binary, 0x1769218 + 8 * i)[0] - base for i in range(8)]
assert slots[4] == 0x1052F0 and slots[3] == 0x118DE80 and slots[5] == 0x118DE60, [hex(s) for s in slots]
assert binary[0x118DE80] == 0xC2 and binary[0x118DE60] == 0xC2                 # vtbl[3] / vtbl[5]: ret 0 (no HUD)
assert slots[6] == 0x1041F0 and slots[7] == 0x104250

# 6. The command ring: a one-unit command is alloc 1, the function pointer, commit 1 (the engine's own Flush 0x111D810),
# run by the dispatcher as fn(context+0x140, reader) with the D3D context at +8 of the first.
assert binary[0x111D816:0x111D81D] == bytes.fromhex('488d99a8280000')          # ring = context+0x28A8
call(0x111D825, value('kAlloc'))
assert binary[0x111D836:0x111D839] == bytes.fromhex('488908')
assert binary[0x111D841] == 0xE9 and 0x111D846 + struct.unpack_from('<i', binary, 0x111D842)[0] == value('kCommit')
assert value('kRing') == 0x28A8 and value('kCommands') == 0x2136FF0
assert rip(0x1198894, 7) == value('kCommands')                                 # the loop's submit is of this context
assert binary[0x111B8FE:0x111B905] == bytes.fromhex('488db340010000')          # rsi = context+0x140
assert binary[0x1118AC0:0x1118AC4] == bytes.fromhex('488b4908')                # a command's D3D context: +8

print('scope lens native anchors passed:', len(guards), 'guards, 8 redirected calls, slots, camera, ring')
