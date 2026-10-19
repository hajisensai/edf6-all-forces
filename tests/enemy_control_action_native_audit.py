"""Read-only, independent supported-image audit for the accepted root action adapter.

Never loads or executes EDF.dll, opens the game, or writes game assets.
"""
from pathlib import Path
import re
import struct
import sys

if len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass supported EDF.dll path')
    raise SystemExit(77)
import pefile
import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
pe = pefile.PE(sys.argv[1], fast_load=True)
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 0

def check(value, why):
    global checks
    assert value, why
    checks += 1

def instruction(at):
    return next(md.disasm(img[at:at + 16], at, count=1))

def exact(at, mnemonic, operands):
    i = instruction(at)
    check((i.mnemonic, i.op_str) == (mnemonic, operands), f'{at:#x}: {i.mnemonic} {i.op_str}')

def call(at, target):
    i = instruction(at)
    check(i.mnemonic == 'call' and i.operands[0].type == X86_OP_IMM and i.operands[0].imm == target,
          f'{at:#x}: call {target:#x}')

def address(at, target):
    i = instruction(at)
    check(i.mnemonic == 'lea' and i.operands[1].type == X86_OP_MEM and i.operands[1].mem.base == X86_REG_RIP
          and i.address + i.size + i.operands[1].mem.disp == target, f'{at:#x}: local descriptor {target:#x}')

check(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664, 'supported x64 image')
root = Path(__file__).resolve().parents[1]
source = (root / 'src/enemy_control_action_native.cpp').read_text(encoding='utf-8-sig')
section = re.search(r'constexpr Sig kSigs\[\]=\{(.*?)\n\};', source, re.S)
check(section is not None, 'runtime action signatures')
prints = re.findall(r'\{(0x[0-9A-F]+)u,\{([^}]+)\}\}', section.group(1))
check(len(prints) >= 25, 'all setter, dispatch and semantic entries admitted')
for at, values in prints:
    wanted = bytes(int(value, 16) for value in values.split(','))
    check(len(wanted) == 16 and img[int(at, 16):int(at, 16) + 16] == wanted, f'fingerprint {at}')

# The target is the actual main callback, never the render matrix. Each native
# frame calls operation1, then increments the per-action seconds/frame counters.
for current, current_ops, invoke, clock, clock_ops, frame, frame_ops in (
    (0x31F14D, 'rcx, qword ptr [rdi + 0xe48]', 0x31F16D, 0x31F184, 'dword ptr [rdi + 0xe20], xmm0', 0x31F18C, 'dword ptr [rdi + 0xe24]'),
    (0x368531, 'rcx, qword ptr [rsi + 0x1318]', 0x368551, 0x36856E, 'dword ptr [rsi + 0x1310], xmm0', 0x368568, 'dword ptr [rsi + 0x1314]'),
    (0x3EE7B9, 'rcx, qword ptr [rsi + 0x1018]', 0x3EE7D2, 0x3EE7E9, 'dword ptr [rsi + 0x1010], xmm0', 0x3EE7F1, 'dword ptr [rsi + 0x1014]'),
):
    exact(current, 'mov', current_ops)
    exact(invoke, 'call', 'qword ptr [rax + 8]')
    exact(clock, 'movss', clock_ops)
    exact(frame, 'inc', frame_ops)

# Two-method local vtables are cloned, preserving destructor and COL. The root
# registry dispatcher delegates the selected registered callback. Direct member
# dispatchers add their local adjustor to their local actor pointer.
for table, dispatcher in ((0x17AA600, 0x31E790), (0x17AFEB0, 0x367930), (0x17B93A8, 0x3EDF30)):
    check(struct.unpack_from('<Q', img, table + 8)[0] - base == dispatcher, f'vtable dispatch {table:#x}')
exact(0x31E790, 'mov', 'rcx, qword ptr [rcx + 0x10]')
exact(0x31E79C, 'jmp', 'qword ptr [rax + 8]')
for start in (0x367930, 0x3EDF30):
    exact(start, 'mov', 'r10, qword ptr [rcx + 0x18]')
    exact(start + 12, 'movsxd', 'rcx, dword ptr [rcx + 0x20]')
    exact(start + 16, 'add', 'rcx, qword ptr [rax + 0x10]')
    exact(start + 20, 'jmp', 'r10')

# Deiroi's wire values are the exact original per-instance registration ordinals.
for lea, callback, registration in (
    (0x320CC7, 0x321FA0, 0x320CF1), (0x320CF9, 0x322520, 0x320D14),
    (0x320D1C, 0x322590, 0x320D37), (0x320D3F, 0x3226E0, 0x320D5A),
    (0x320D62, 0x321DE0, 0x320D7D), (0x320D85, 0x322290, 0x320DA0),
):
    address(lea, callback)
    call(registration, 0x320AF0)
exact(0x320B50, 'movups', 'xmmword ptr [r8 + 0x18], xmm0')
exact(0x320B55, 'mov', 'qword ptr [r8 + 0x28], rax')
# SetOrdinal performs real old-state exit, resets clock and enters operation0.
call(0x32136B, 0x30C080)
exact(0x321416, 'mov', 'qword ptr [rbx + 0x28], rsi')
exact(0x32141A, 'mov', 'qword ptr [rbx], 0')
exact(0x321431, 'xor', 'edx, edx')
exact(0x321436, 'call', 'qword ptr [rax + 8]')
# Stomp initializes leg=-1, then writes the selected valid native leg index.
# Native tag6 can enter that attack, but its local AI target must still agree.
exact(0x3222CF, 'mov', 'qword ptr [rdi + 0x1a60], 0xffffffffffffffff')
exact(0x3223E4, 'mov', 'qword ptr [rdi + 0x1a60], rbx')
exact(0x3225FC, 'cmp', 'qword ptr [rbx + 0x1a60], -1')
exact(0x322604, 'mov', 'dword ptr [rbx + 0x1a54], eax')
exact(0x32260A, 'je', '0x3225d1')
exact(0x322B0D, 'mov', 'rax, qword ptr [rcx + 0x1a60]')
exact(0x322B14, 'lea', 'rdx, [rcx + 0x1a70]')

# Direct setters are lifecycle operations, not input/cooldown entry points.
# Their identical supported-image bodies call exit2, clear seconds+frame and
# current, copy the local descriptor, establish current and call enter0.
for start in (0x369F20, 0x3F00D0):
    exact(start + 0x34, 'xor', 'r9d, r9d')
    exact(start + 0x3A, 'lea', 'edx, [r9 + 2]')
    exact(start + 0x3E, 'call', 'qword ptr [rax + 8]')
    exact(start + 0x4A, 'mov', 'qword ptr [rbx + 8], rax')
    exact(start + 0x51, 'mov', 'qword ptr [rbx], rax')
    exact(start + 0x64, 'movups', 'xmmword ptr [rdi + 0x18], xmm0')
    exact(start + 0x68, 'mov', 'qword ptr [rbx + 8], rdi')
    exact(start + 0x79, 'call', 'qword ptr [rax + 8]')
# Dango normal roll/air states use no damageData; damage/death are separate.
address(0x36CA0A, 0x36CA40)
address(0x36CA8D, 0x36C9E0)
address(0x368270, 0x36CE90)
address(0x363FAE, 0x36C2D0)
# Ground rolling update's accepted quaternion and contact normal feed native
# effect/attack maintenance outside this main callback; pose alone omitted them.
exact(0x36CAD5, 'movss', 'dword ptr [rcx + 0x1480], xmm1')
exact(0x36CDF2, 'lea', 'rcx, [rbx + 0x1430]')
call(0x36CE10, 0x63DA0)
exact(0x36858A, 'lea', 'r8, [rsi + 0x1430]')
exact(0x368636, 'movups', 'xmm3, xmmword ptr [rsi + 0x1480]')
check('std::memcpy(actor+0x1430,s.data+1,16)' in source and 'std::memcpy(actor+0x1480,s.data+5,16)' in source,
      'accepted rolling quaternion and normal are replayed as typed native maintenance inputs')
# Desired/smoothed rolling velocity is also a live native maintenance input;
# Havok position/velocity correction alone cannot substitute these caches.
exact(0x36CB6F, 'movups', 'xmmword ptr [rbx + 0xf90], xmm0')
exact(0x368583, 'movups', 'xmm1, xmmword ptr [rsi + 0xfa0]')
exact(0x368591, 'movups', 'xmm2, xmmword ptr [rsi + 0xf90]')
exact(0x3685C8, 'movups', 'xmmword ptr [rsi + 0xfa0], xmm2')
exact(0x3686DC, 'movups', 'xmmword ptr [rsi + 0x1010], xmm0')
check('std::memcpy(actor+0xF90,s.data+9,32)' in source, 'rolling target and accumulator velocities replayed as typed maintenance inputs')
# TimeShip's actual accepted target, frame delay and typed main-skill curves.
exact(0x3EE557, 'movups', 'xmmword ptr [rsi + 0x1420], xmm0')
exact(0x3EE54B, 'mov', 'byte ptr [rsi + 0x1410], 1')
exact(0x3EE568, 'mov', 'dword ptr [rsi + 0x1474], 1')
exact(0x3F1325, 'mov', 'eax, dword ptr [rbx + 0x1470]')
exact(0x3F132B, 'cmp', 'dword ptr [rcx + 4], eax')
exact(0x3F2156, 'mov', 'dword ptr [rcx + 0x146c], 0x3f800000')
address(0x3EE6A2, 0x3F1430)
address(0x3F16E7, 0x3F1FB0)
address(0x3EE73C, 0x3F02C0)
# Deferred skill requests are consumed in slot5, after the slot4 bracket.
# Guest outcome replay must never leave these raw requests armed.
exact(0x3EF651, 'cmp', 'byte ptr [rcx + 0x1410], r14b')
exact(0x3EF6EC, 'mov', 'eax, dword ptr [r15 + 0x1474]')
check('Put<std::uint32_t>(actor,0x1474,0)' in source and 'actor[0x1410]=0' in source,
      'accepted semantic replay clears deferred local slot5 requests')
# Opaque damage callback dereferences R9: it is deliberately never synthesized.
exact(0x3F2375, 'mov', 'r14, r9')
exact(0x3F23D3, 'movups', 'xmm0, xmmword ptr [r14 + 0x10]')

# .pdata proves that the collision GDI emitter is not the adjacent AI7 body.
# This boundary check prevents restoring the entire AI decision path under a
# mistaken belief that suppressing AI7 disabled native rolling collision damage.
full = pefile.PE(sys.argv[1])
ranges = {(e.struct.BeginAddress, e.struct.EndAddress) for e in full.DIRECTORY_ENTRY_EXCEPTION}
check((0x367800, 0x367926) in ranges, 'AI7 is exactly 367800..367926')
check(any(a == 0x3679A0 and b > 0x367E19 for a, b in ranges), 'GDI belongs to collision callback 3679A0')
exact(0x367A0B, 'mov', 'r15, r8')
exact(0x367A0E, 'mov', 'rsi, rdx')
exact(0x367A88, 'cmp', 'dword ptr [rsi + 0x20], 0')
exact(0x367E19, 'mov', 'eax, dword ptr [r14 + 0x318]')
address(0x36975A, 0x3679A0)
call(0x369803, 0x360980)
# Arrival's operation1 contains a real one-shot visibility+collision event.
# It is explicitly carried as a typed event, never omitted as a pose correction.
exact(0x3F16BD, 'cmp', 'dword ptr [rcx + 4], edx')
call(0x3F16C5, 0x3F01F0)
call(0x3F0202, 0x6C04B0)
call(0x3F0210, 0x6ECCF0)
call(0x3F0222, 0x3E9780)
exact(0x6C04D4, 'mov', 'byte ptr [rbx + 0x480], dil')
exact(0x6ECD29, 'mov', 'byte ptr [rbx + 0xa4], dil')
# Stomp captures an actual target weak identity in the selected leg controller.
# Remote entry is redirected to that resolved native ID, not its own AI target.
call(0x322458, 0x30C0F0)
exact(0x32536F, 'mov', 'rcx, qword ptr [rdx + 0x28]')
exact(0x32537A, 'mov', 'qword ptr [rbx + 0x140], rcx')
exact(0x325388, 'mov', 'qword ptr [rbx + 0x148], r8')
check('ReadNativeObjectId(target,reinterpret_cast<unsigned char*>(s.data+15))' in source, 'real selected target ID captured')
check('ResolveNativeProjectileActor(id,&actor,&control)' in source and 'b->stompTarget=stomp[i].obj' in source, 'full native target ID retained before replay, including dead allocated actors')
check('if(!actor || !control)return Result::failed' in source and 'ReleaseNativeProjectileLock(const_cast<void*>(r.obj),r.ctrl,&consumed)' in source and 'if(consumed)b.targetsUncertain=true' in source, 'expired target cannot ACK a no-op; strong target release retains uncertain cleanup')
check('if(enable)b.nativeEffects|=1u' in source and 'Fn<void(__fastcall*)(void*)>(0x3F01F0)' in source, 'actual enabled event observed and replayed locally')

# Active main callbacks have no direct object RNG consumption (the excluded
# Deiroi death callback does). Future accepted entries nevertheless carry the
# exact host pre-entry 3E8/490 RNG values, observed before native dispatch0.
for start, end in ((0x321FA0, 0x322BF0), (0x36C9E0, 0x36CE90), (0x3F02C0, 0x3F2BA0)):
    for entry in full.DIRECTORY_ENTRY_EXCEPTION:
        a, b = entry.struct.BeginAddress, entry.struct.EndAddress
        if not start <= a < end:
            continue
        for i in md.disasm(img[a:b], a):
            check(not any(o.type == X86_OP_MEM and o.mem.disp in (0x3E8, 0x490) for o in i.operands),
                  f'{i.address:#x}: no hidden direct object RNG read in an active main callback')
check('operation==0 && !(b.quiescing && fromDamage) && ControlledSemantic(b,&entered)' in source and 'b.entrySeed=At<std::uint64_t>(b.object,0x3E8)' in source,
      'true pre-entry object RNG captured')
check('Put<std::uint64_t>(actor,0x3E8,s.entrySeed)' in source and 'Put<std::uint64_t>(actor,0x490,s.variantSeed)' in source,
      'true pre-entry RNG restored before the native setter')

# Source contract guards complement instruction evidence; a physics writer,
# render transform, received pointer or same-input retry is not accepted action.
check('operation==1 && ControlledSemantic(b)' in source, 'guest main update1 gate')
check('s.cycle>b->remote.cycle' in source and 'if(enter && !Enter(*b,s))' in source, 'only new accepted cycles enter')
check('Parameters(*b,s);Phase(*b,s);if(!Effect(*b,s))' in source and 'b->remote=s;b->applied=true' in source, 'typed outcomes, native effect and phase commit before applied')
check('return StockOnly(b)' in source, 'known stock opaque damage/death preserved')
check('image+Callback(s.family,s.semantic)' in source, 'wire semantic resolved through local descriptor table')
check('s.semantic,nullptr' in source, 'Deiroi native registry ordinal entry')
check('RunInput(' not in source and 'BuildPad(' not in source, 'guest never recreates an accepted action by retrying input')
check('std::memcpy(actor+0x60,s.data+24,64)' in source and '__finally {if(context){std::memcpy(actor+0x60,frame,64)' in source, 'host entry frame is a temporary state0 argument context, restored in finally')
print('enemy main accepted action native audit:', checks, 'checks passed')
