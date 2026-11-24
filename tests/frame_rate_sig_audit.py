"""The sites src/frame_rate.cpp (unlimited frame rate, stage 2, docs/unlimited-fps-re.md) patches, checked against the
real EDF.dll read as a file (never loaded): the main loop's two tick counts and the `mov ebx,eax` after each (the gate);
Application::Update's per-call work (SteamAPI_RunCallbacks, 0x788460, EOS_Platform_Tick through 0x12ADB30, the map's
wind time, the hold-off count Application+0x6C) and what it is given ({dt, n} in r14, Application in rsi); System::Update's
call and the wrapper update's call with their arguments; the wrapper table 0x7CDC0 walks (4 players of 0xDD0, 146
wrappers of 0x18 from +8, the menu-key masks after them) and the wrapper update 0x1182BC0's field layout ClearForUi
relies on; the mouse's and the wheel's takes in the input callback and the accumulator's cleared value; the sync interval
`sete dl` passed to 0x111D890, its only call. An EDF.dll path as the argument, else the game gamedir finds; exit 77
(skipped) without one."""
from __future__ import annotations

import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))

path = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else None
if not path:
    try:
        import gamedir
        path = os.path.join(gamedir.find_or_dev(), 'EDF.dll')
    except Exception:
        path = None
if not path or not os.path.isfile(path):
    print('SKIP: no EDF.dll')
    raise SystemExit(77)
import pefile  # noqa: E402

pe = pefile.PE(path, fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: EDF.dll {pe.FILE_HEADER.TimeDateStamp:#x} is not the one the signatures are for')
    raise SystemExit(77)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
imports = {i.address - base: (d.dll.decode().upper(), (i.name or b'').decode())
           for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports}
src = open(os.path.join(ROOT, 'src', 'frame_rate.cpp'), encoding='utf-8').read()
pacing = open(os.path.join(ROOT, 'src', 'frame_pacing.cpp'), encoding='utf-8').read()
checks = 0


def check(ok: object, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


consts = {m[0]: int(m[1], 0) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+|\d+)\b', src)}
arrays = {name: bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
          for name, body in re.findall(r'\b(k\w+)\[\]=\{([^}]*)\}', src)}
pconsts = {m[0]: int(m[1], 16) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)\b', pacing)}


def rel_target(at: int, length: int) -> int:
    return at + length + struct.unpack_from('<i', img, at + length - 4)[0]


rip = rel_target
text = pe.sections[0]
tb, te = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize


def direct_callers(target: int) -> list[int]:
    return [tb + m.start() for m in re.finditer(b'\xe8', img[tb:te]) if rel_target(tb + m.start(), 5) == target]


# The gate: both tick counts, each `call kTickCount; mov ebx,eax; test eax,eax; j..`, the wait between them the frame
# step's (frame_pacing.cpp kTickSleep), and the loop's n = min(n, 3) after them.
for name in ('kCountFirst', 'kCountAgain'):
    at = consts[name]
    sig = arrays[name + 'Sig']
    check(img[at:at + len(sig)] == sig, f'{name} {at:#x}: {img[at:at + len(sig)].hex(" ")} != {sig.hex(" ")}')
    check(rel_target(at, 5) == consts['kTickCount'], f'{name} calls kTickCount')
    check(img[at + 5:at + 7] == bytes.fromhex('8bd8'), f'{name}: mov ebx,eax after the call')
check(arrays['kMovEbxEdx'] == bytes.fromhex('8bda'), 'kMovEbxEdx is mov ebx,edx')
check(consts['kTickCount'] == pconsts['kTickCount'], 'the gate counts with the clock frame_pacing.cpp checks byte for byte')
first, again = consts['kCountFirst'], consts['kCountAgain']
check(first + 9 + 2 + img[first + 10] == 0x11A2F76, 'the first count goes on to the frame when it is not 0')
check(again + 9 + 2 + struct.unpack_from('<b', img, again + 10)[0] == pconsts['kTickSleep'] - 5,
      'the count after the wait waits again when it is 0 (to frame_pacing.cpp kTickSleep)')
check(img[0x11A2F76:0x11A2F78] == b'\x3b\xdd' and img[0x11A2F80:0x11A2F83] == b'\x0f\x47\xdd' and
      img[0x11A2EDA:0x11A2EDD] == b'\x8d\x6e\x03', 'n = min(n, 3) after the counts (ebp = rsi + 3)')
check(direct_callers(consts['kTickCount']) == [first, again], 'kTickCount is called only by the two counts')
# The final n: after the clamp, zeroed with dt when [rdi+9] (mov ebx,esi), then stored for Application::Update. The
# guards read the copy the store's stub keeps, so they see the n the step runs with.
fin = consts['kFinalStore']
check(img[fin:fin + 7] == arrays['kFinalStoreSig'], 'kFinalStore: mov [rsp+0BCh],ebx')
check(img[0x11A2FAC:0x11A2FB0] == bytes.fromhex('40387709') and img[0x11A2FB6:0x11A2FBB] == bytes.fromhex('0f57f68bde'),
      'the reset between the counts and kFinalStore: [rdi+9] zeroes dt and n')
check(re.search(r'Site sites\[\]=\{\{kCountFirst,.*\{kCountAgain,.*\{kFinalStore,', src), 'the gate writes its three sites together')
check(src.index('SetFramePacingGate(&GateWaitCounts)') > src.index('int written=0'), 'the wait goes to the gate only after the writes')
check('frameTicks' not in src and 'bool Steps() noexcept { return !gateOn || finalTicks; }' in src, 'the guards read the final n')
check(re.search(r'RtlAddFunctionTable', src) and re.search(r'0x01,0x04,0x01,0x00,0x04,0x42', src),
      'the gate stub registers its unwind (UWOP_ALLOC_SMALL 0x28 at offset 4)')
check(re.search(r'if\(gateWait\)', pacing) and re.search(r'tickWaitInstalled=tick;', pacing),
      'TickWait waits for the gate\'s slot when it is on')

# Application::Update 0x705530: rsi = Application, r14 = {dt, n}.
check(img[0x705556:0x70555C] == b'\x4c\x8b\xf2\x48\x8b\xf1', 'Application::Update keeps {dt, n} in r14 and itself in rsi')
check(img[0x11A3006:0x11A3009] == b'\xff\x50\x08', 'the main loop calls Application::Update through its vtable slot 1')
steam = consts['kSteamCall']
check(img[steam:steam + 6] == arrays['kSteamSig'] and
      imports.get(rel_target(steam, 6)) == ('STEAM_API64.DLL', 'SteamAPI_RunCallbacks'), 'kSteamCall is SteamAPI_RunCallbacks')
for call, target in (('kNetCall', 'kNet'), ('kEosCall', 'kEos'), ('kWindCall', 'kWind')):
    check(img[consts[call]] == 0xE8 and rel_target(consts[call], 5) == consts[target], f'{call} calls {target}')
eos = consts['kEos']
check(img[eos + 0x1D:eos + 0x1F] == b'\xff\x15' and imports.get(rel_target(eos + 0x1D, 6)) == ('EOSSDK-WIN64-SHIPPING.DLL', 'EOS_Platform_Tick'),
      f'kEos calls EOS_Platform_Tick ({imports.get(rel_target(eos + 0x1D, 6))})')
check(sorted(direct_callers(eos)) == sorted([consts['kEosCall'], 0xCD468]), 'kEos: Application::Update and the 1 s loop 0xCD468 only')
check(img[0x12341F:0x123428] == b'\xf3\x41\x0f\x58\x86\xc8\x00\x00\x00', 'kWind adds MapMgr+0xC8 (the wind step) each call')
hold = consts['kHoldOff']
check(img[hold:hold + len(arrays['kHoldOffSig'])] == arrays['kHoldOffSig'], 'kHoldOff: the +0x6C count and its dec')
check(re.search(r'0x8B,0x46,0x6C,0x41,0x83,0x7E,0x04,0x00,0x75,0x02,0x33,0xC0,0x85,0xC0,0xC3', src),
      'the hold-off leaf reads [rsi+6Ch] and n = [r14+4], 0 on a 0-tick frame')
rnd = consts['kRandomStep']
check(img[rnd:rnd + len(arrays['kRandomStepSig'])] == arrays['kRandomStepSig'], 'kRandomStep: the 34-byte seed step')
check(rel_target(rnd + 10, 7) == consts['kRandomSeed'] and rel_target(rnd + 27, 7) == consts['kRandomSeed'],
      'kRandomStep reads and writes kRandomSeed')
check(img[rnd + 34:rnd + 39] == bytes.fromhex('f30f104668'), 'after the step: movss xmm0,[rsi+68h] (rax and rcx dead)')

# The input: System::Update(rcx, xmm1 = dt, r8d = n) and the wrapper update (rcx = [kInputManager], edx = n).
sysc, wrapc = consts['kSystemCall'], consts['kWrapCall']
check(img[sysc] == 0xE8 and rel_target(sysc, 5) == consts['kSystemUpdate'], 'kSystemCall calls kSystemUpdate')
check(img[sysc - 16:sysc - 7] == b'\x45\x8b\x46\x04\xf3\x41\x0f\x10\x0e', 'System::Update gets r8d = n, xmm1 = dt')
check(img[wrapc] == 0xE8 and rel_target(wrapc, 5) == consts['kWrapUpdate'], 'kWrapCall calls kWrapUpdate')
check(img[wrapc - 11:wrapc - 7] == b'\x41\x8b\x56\x04' and img[wrapc - 7:wrapc - 4] == b'\x48\x8b\x0d' and
      rip(wrapc - 7, 7) == consts['kInputManager'], 'the wrapper update gets rcx = [kInputManager], edx = n')
check(sysc < wrapc, 'System::Update comes before the wrapper update in Application::Update')
wu = consts['kWrapUpdate']
check(img[wu:wu + 7] == b'\x48\x81\xc1' + struct.pack('<I', consts['kWrapTable']) and img[wu + 7] == 0xE9 and
      rel_target(wu + 7, 5) == 0x7CDC0, 'kWrapUpdate is `add rcx,kWrapTable; jmp 0x7CDC0`')
check(direct_callers(wu) == [wrapc], 'the wrapper update is called only from Application::Update')
# 0x7CDC0: rbx = the table; per player rsi = p * 0xDD0; wrappers at rbx+8.. updated by 0x1182BC0(edx = n).
check(img[0x7CDFD:0x7CE00] == b'\x48\x8b\xd9' and img[0x7CE37:0x7CE3E] == b'\x48\x69\xf0' + struct.pack('<I', consts['kPlayerSize']),
      'the table in rbx, players kPlayerSize apart')
check(img[0x7CE33:0x7CE37] == b'\x48\x8d\x4b' + bytes([consts['kWrapFirst']]) and rel_target(0x7CE44, 5) == 0x1182BC0,
      'the first wrapper at kWrapFirst')
check(img[0x7D042:0x7D049] == b'\x48\x8d\xa8\x50\x01\x00\x00' and img[0x7D064:0x7D06B] == b'\x48\x8d\x83\x10\x04\x00\x00' and
      img[0x7D071:0x7D078] == b'\x48\x8d\xa8\xa8\x09\x00\x00' and 0x410 + 0x9A8 == consts['kWrapEnd'] and
      img[0x7D05B:0x7D05F] == b'\x48\x83\xc7' + bytes([consts['kWrapSize']]), 'the wrappers run on to kWrapEnd, kWrapSize apart')
offs = [struct.unpack_from('<I', img, a + 3)[0] if img[a + 2] in (0x8b, 0x83) and img[a + 1] == 0x8d else struct.unpack_from('<b', img, a + 3)[0]
        for a in [0x7CE33] + [m for m in range(0x7CE49, 0x7D035) if img[m:m + 3] in (b'\x48\x8d\x4b', b'\x48\x8d\x8b')]]
check(offs == list(range(consts['kWrapFirst'], 0x2C0, consts['kWrapSize'])), f'the first 29 wrappers one after another: {offs}')
check((consts['kWrapEnd'] - consts['kWrapFirst']) // consts['kWrapSize'] == 146 and img[0x7D9C6:0x7D9CA] == b'\x41\x83\xfd' + bytes([consts['kPlayers']]),
      'kPlayers players of 146 wrappers')
# The masks: {&+0xDB8, held}, {&+0xDBC, pressed}, {&+0xDC0, repeat}, {&+0xDC4, released}.
for lea_at, fn_at, off, field in ((0x7D09F, 0x7D0B6, 0xDB8, 0x10), (0x7D0C2, 0x7D0DD, consts['kMaskPressed'], 0x11),
                                  (0x7D0E9, 0x7D107, consts['kMaskRepeat'], 0x13), (0x7D116, 0x7D134, consts['kMaskReleased'], 0x12)):
    fn = rip(fn_at, 7)
    check(img[lea_at:lea_at + 3] == b'\x48\x8d\x83' and struct.unpack_from('<I', img, lea_at + 3)[0] == off and
          img[fn:fn + 5] == b'\x0f\xb6\x41' + bytes([field]) + b'\xc3', f'the mask at {off:#x} is of wrapper byte {field:#x}')
# 0x1182BC0: the source at +0 (value +0x48, held +0x4C, off +0x4D); value +0xC, held +0x10, edges +0x11/+0x12/+0x13.
bc = bytes.fromhex('4c8b01448bd24d85c0746d4180784d007566418b4048440fb64910'
                   '89410c4180784d007507410fb6404ceb0233c0884110' '66c741110000c6411300')
check(img[0x1182BC0:0x1182BC0 + len(bc)] == bc, 'the wrapper update 0x1182BC0 reads and clears the fields ClearForUi does')

# The takes: 0x11831F0(rcx = out, dl = 1) and 0x11832D0(cl = 1) in the input callback; the accumulator's cleared value.
mouse, wheel = consts['kMouseCall'], consts['kWheelCall']
check(img[mouse - 7:mouse - 5] == b'\xb2\x01' and img[mouse] == 0xE8 and rel_target(mouse, 5) == consts['kMouseTake'], 'kMouseCall: dl = 1, the mouse take')
check(img[wheel - 2:wheel] == b'\xb1\x01' and img[wheel] == 0xE8 and rel_target(wheel, 5) == consts['kWheelTake'], 'kWheelCall: cl = 1, the wheel take')
mt = consts['kMouseTake']
check(img[mt + 0x12:mt + 0x15] == b'\x0f\x28\x05' and rip(mt + 0x12, 7) == consts['kMouseCleared'] and
      img[consts['kMouseCleared']:consts['kMouseCleared'] + 16] == bytes(12) + struct.pack('<f', 1.0),
      'the mouse take clears its sum to kMouseCleared {0, 0, 0, 1}')
wt = consts['kWheelTake']
check(img[wt:wt + 0x18] == bytes.fromhex('8b05ea2dfb004533c084c98bd0410f45d08915d92dfb00c3'), 'the wheel take returns the sum, cleared when cl')

# The sync interval: `mov edx,esi; lea rcx,[renderer]; cmp [rdi+0Ch],edx; sete dl; call 0x111D890`, its only call.
sync = consts['kSyncCall']
check(img[sync - len(arrays['kSyncSig']):sync] == arrays['kSyncSig'] and rel_target(sync, 5) == consts['kSyncSubmit'], 'kSyncCall')
check(direct_callers(consts['kSyncSubmit']) == [sync], 'the sync interval is passed only at kSyncCall')

for site, target, hook in (('kNetCall', 'kNet', 'NetGuard'), ('kEosCall', 'kEos', 'EosGuard'), ('kWindCall', 'kWind', 'WindGuard'),
                           ('kSystemCall', 'kSystemUpdate', 'SystemUpdateHook'), ('kWrapCall', 'kWrapUpdate', 'WrapUpdateHook'),
                           ('kMouseCall', 'kMouseTake', 'MouseTakeHook'), ('kWheelCall', 'kWheelTake', 'WheelTakeHook'),
                           ('kSyncCall', 'kSyncSubmit', 'SyncHook'), ('kVolumeCall', 'kVolume', 'VolumeGuard')):
    check(re.search(rf'RedirectCall\(image\+{site},image\+{target},reinterpret_cast<void\*>\(&{hook}\)', src), f'{hook} at {site}')
check(re.search(r'RedirectImportCall\(image\+kSteamCall,kSteamSig,reinterpret_cast<void\*>\(&SteamGuard\)', src), 'SteamGuard at kSteamCall')

# Stage 3 (frame_rate_view.cpp): the camera step is called once, at kCameraStepCall, the last call of the tick loop (after
# every object phase, so its hook samples the tick's result); the projection 0x1197B50 -> 0x118DFF0 is shared with scope_lens.
view = open(os.path.join(ROOT, 'src', 'frame_rate_view.cpp'), encoding='utf-8').read()
vconsts = {m[0]: int(m[1], 0) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+|\d+)\b', view)}
varrays = {name: bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
           for name, body in re.findall(r'\b(k\w+)\[\]=\{([^}]*)\}', view)}
step_call, step = vconsts['kCameraStepCall'], vconsts['kCameraStep']
check(img[step_call - 5:step_call + 5] == varrays['kCameraStepSig'] and rel_target(step_call, 5) == step, 'kCameraStepCall')
check(direct_callers(step) == [step_call], 'the camera step is called only at kCameraStepCall')
check(img[step_call + 0xE0] == 0xE9 and rel_target(step_call + 0xE0, 5) == 0x119959C, 'the camera step is in the per-viewport loop')
check(re.search(r'RedirectCall\(image\+kCameraStepCall,image\+kCameraStep,reinterpret_cast<void\*>\(&CameraStepHook\)', view), 'CameraStepHook')
for vt in (0x17CDF28, 0x17D0FF8, 0x17CF5B8, 0x17CF100):
    check(f'{vt:#X}'.replace('0X', '0x') in view, f'soldier vtable {vt:#x} in frame_rate_view.cpp')

# The swap chain (swap_flip.cpp): CreateSwapChain's call in 0x111B220 and the Present's flags.
flip = open(os.path.join(ROOT, 'src', 'swap_flip.cpp'), encoding='utf-8').read()
fconsts = {m[0]: int(m[1], 0) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+|\d+)\b', flip)}
farrays = {name: bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
           for name, body in re.findall(r'\b(k\w+)\[\]=\{([^}]*)\}', flip)}
create, present = fconsts['kCreateCall'], fconsts['kPresentFlags']
check(img[create:create + 6] == farrays['kCreateSig'], 'kCreateCall: mov rax,[rcx]; call [rax+50h] (IDXGIFactory::CreateSwapChain)')
check(img[create - 14:create - 9] == b'\x4c\x8d\x44\x24\x30', 'kCreateCall: r8 = the description on the stack')
check(sorted(direct_callers(0x111B220)) == [0x111B77B, 0x111C8EC], 'the swap chain is built only by its two callers')
check(img[present:present + 6] == farrays['kPresentSig'], 'kPresentFlags: mov rax,[rcx]; xor r8d,r8d')
check(img[present + 0xF:present + 0x12] == b'\xff\x50\x40', 'kPresentFlags: IDXGISwapChain::Present follows')
# The volume smoothing (rcx = the sound, xmm1 = dt) and the load meter's store, both guarded to frames with ticks.
vol = consts['kVolumeCall']
check(img[vol] == 0xE8 and rel_target(vol, 5) == consts['kVolume'] and img[vol - 12:vol - 7] == bytes.fromhex('f3410f100e'),
      'kVolumeCall: 0x7B3010 with xmm1 = dt')
check(direct_callers(consts['kVolume']) == [vol], 'the volume smoothing is called only at kVolumeCall')
ls = consts['kLoadStore']
check(img[ls:ls + 8] == arrays['kLoadStoreSig'] and rip(ls, 8) == consts['kLoadMeter'], 'kLoadStore stores to kLoadMeter')
check(img[ls + 8] == 0xE8, 'a call follows the load store (r11 and the flags are dead in the stub)')
check(re.search(r'constexpr int kGuards=8;', src) and 'guards!=kGuards' in src, 'the gate needs all 8 guards')
print(f'frame rate signatures: {checks} checks ok')
