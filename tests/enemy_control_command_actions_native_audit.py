"""Read-only supported-image evidence for accepted Monster/Merman command entry.

EDF.dll is parsed, never loaded or executed. This proves complete native state
entry/exit and distinguishes entry from later MAB/slot5 shot outcomes; it does
not claim that replaying a command proves canonical projectile outcomes.
"""
from pathlib import Path
import argparse
import re
import struct
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--dll', default='')
parser.add_argument('--game', default='')
args = parser.parse_args()
dll = Path(args.dll) if args.dll else Path(args.game) / 'EDF.dll' if args.game else None
if dll is None or not dll.is_file():
    print('SKIP: pass --dll for the supported EDF.dll')
    raise SystemExit(77)

import capstone
import pefile
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

pe = pefile.PE(str(dll), fast_load=True)
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 0


def check(value, why):
    global checks
    assert value, why
    checks += 1


def ins(at):
    return next(md.disasm(img[at:at + 16], at, count=1))


def exact(at, mnemonic, operands):
    x = ins(at)
    check((x.mnemonic, x.op_str) == (mnemonic, operands),
          f'{at:#x}: {x.mnemonic} {x.op_str}')


def branch(at, mnemonic, target):
    x = ins(at)
    check(x.mnemonic == mnemonic and x.operands[0].type == X86_OP_IMM
          and x.operands[0].imm == target, f'{at:#x}: {mnemonic} {target:#x}')


def address(at, target):
    x = ins(at)
    check(x.mnemonic == 'lea' and x.operands[1].type == X86_OP_MEM
          and x.operands[1].mem.base == X86_REG_RIP
          and x.address + x.size + x.operands[1].mem.disp == target,
          f'{at:#x}: local callback {target:#x}')


check(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
      and pe.FILE_HEADER.Machine == 0x8664, 'supported x64 image')
# The runtime's admission table is the source of truth for every command family,
# including additions beyond the independently expanded Monster/Merman contracts.
root = Path(__file__).resolve().parents[1]
source = (root / 'src/enemy_control_command_actions.cpp').read_text(encoding='utf-8-sig')
check('!EntryRelay(sites[i]' in source and 'return actual==expected' in source and 'thunk[10]!=0xFF' in source, 'actual near relay callback ownership is checked, not caller bytes alone')
check('if(consumed)s->cleanupUncertain=true' in source and 's->nativeCalls || s->cleanupEntered' in source, 'consumed native cleanup and in-flight entry keep their persistent lease journal')
section = re.search(r'constexpr Sig kSigs\[\]=\{(.*?)\n\};', source, re.S)
check(section is not None, 'runtime accepted-command signatures are declared')
fingerprints = re.findall(r'\{(0x[0-9A-Fa-f]+)(?:u)?,\{([^}]+)\}\}', section.group(1))
check(bool(fingerprints), 'runtime admission table is nonempty')
for at, values in fingerprints:
    raw = bytes(int(value.strip(), 16) for value in values.split(','))
    address_rva = int(at, 16)
    check(len(raw) == 16 and img[address_rva:address_rva + 16] == raw,
          f'runtime fingerprint {at}')

for at, raw in (
    (0x4B7AC0, '4883ec38f681b0080000040f84a10000'),
    (0x4BE500, '48895c240848896c2410488974241857'),
    (0x4BEC00, '40564883ec30488bf185d20f85830000'),
    (0x4BB900, '4c8b5118488bc14d85d2740b48634920'),
    (0x3A6710, '48895c240848896c2410488974241857'),
    (0x3A6ED0, '40534883ec30488bd985d20f859f0000'),
    (0x3A5DA0, '405356574881ec30060000488b05a692'),
):
    check(img[at:at + 16] == bytes.fromhex(raw), f'fingerprint {at:#x}')

# Monster host admission owns the native gate. The accepted receiver instead
# calls the complete setter with the local descriptor and an int ordinal.
exact(0x4B7AC4, 'test', 'byte ptr [rcx + 0x8b0], 4')
address(0x4B7AD6, 0x4BEC00)
exact(0x4B7AE4, 'mov', 'dword ptr [rsp + 0x40], edx')
exact(0x4B7AED, 'lea', 'rdi, [rcx + 0x1020]')
exact(0x4B7B2C, 'lea', 'r9, [rsp + 0x40]')
check(not any(x.mnemonic == 'call' and x.operands[0].type == X86_OP_IMM
              for x in md.disasm(img[0x4B7AC0:0x4B7B77], 0x4B7AC0)),
      'Monster wrapper has no separate stock sender call; only native lifecycle dispatch')
exact(0x4BE53E, 'call', 'qword ptr [rax + 8]')
exact(0x4BE53A, 'lea', 'edx, [r9 + 2]')
exact(0x4BE54A, 'mov', 'qword ptr [rbx + 8], rax')
exact(0x4BE551, 'mov', 'qword ptr [rbx], rax')
exact(0x4BE54E, 'mov', 'r9, rsi')
exact(0x4BE564, 'movups', 'xmmword ptr [rdi + 0x18], xmm0')
exact(0x4BE568, 'mov', 'qword ptr [rbx + 8], rdi')
exact(0x4BE579, 'call', 'qword ptr [rax + 8]')
check(not any(o.type == X86_OP_MEM and o.mem.disp == 0x8B0
              for x in md.disasm(img[0x4BE500:0x4BE598], 0x4BE500) for o in x.operands),
      'accepted Monster setter does not recheck the host admission gate')
check(struct.unpack_from('<Q', img, 0x17C5F98)[0] - base == 0x4BB900,
      'Monster local member-machine dispatch')
address(0x4B544C, 0x17C5F90)
exact(0x4B5453, 'mov', 'qword ptr [rsi + 0x1020], rax')
exact(0x4BB900, 'mov', 'r10, qword ptr [rcx + 0x18]')
exact(0x4BB90C, 'movsxd', 'rcx, dword ptr [rcx + 0x20]')
exact(0x4BB910, 'add', 'rcx, qword ptr [rax + 0x10]')
exact(0x4BD2D5, 'mov', 'rcx, qword ptr [rdi + 0xf88]')
exact(0x4BD303, 'inc', 'dword ptr [rdi + 0xf84]')
exact(0x4BD311, 'movss', 'dword ptr [rdi + 0xf80], xmm0')

# State0 consumes only the ordinal as its parameter and resolves the original
# local table's animation and rate. State1/2 retain completion and exit cleanup.
exact(0x4BEC2D, 'mov', 'eax, dword ptr [r9]')
exact(0x4BEC33, 'mov', 'dword ptr [rcx + 0x14d0], eax')
exact(0x4BEC39, 'mov', 'r8, qword ptr [rcx + 0x14a8]')
exact(0x4BEC4C, 'imul', 'rbx, rax, 0x38')
branch(0x4BEC59, 'call', 0x116C2D0)
exact(0x4BEC72, 'movss', 'xmm2, dword ptr [rbx + rax + 8]')
exact(0x4BEC78, 'mulss', 'xmm2, dword ptr [rsi + 0x698]')
branch(0x4BEC8F, 'jmp', 0x116DF20)
exact(0x4BECAD, 'mov', 'dword ptr [rsi + 0x8b0], ecx')
branch(0x4BECBA, 'call', 0x116BBE0)
address(0x4BECCB, 0x4BF3D0)
branch(0x4BECF1, 'call', 0x4BE500)
exact(0x4BED03, 'mov', 'dword ptr [rcx + 0x14d0], 0xffffffff')
exact(0x4BED0D, 'mov', 'byte ptr [rcx + 0x14e0], 0')
branch(0x4BED2E, 'jmp', 0x6C0510)
for x in md.disasm(img[0x4BEC00:0x4BED39], 0x4BEC00):
    check(not any(o.type == X86_OP_MEM and o.mem.disp in (0x3E8, 0x490)
                  for o in x.operands), f'{x.address:#x}: no direct main-callback RNG')

# Merman's starter has no caller cooldown/mode gate. It always exits the old
# state, sets the accepted row+bool and enters the complete native action.
exact(0x3A6728, 'lea', 'rsi, [rcx + 0x1b90]')
exact(0x3A6732, 'imul', 'r9, r14, 0x88')
address(0x3A673C, 0x3A6ED0)
exact(0x3A6743, 'add', 'r9, qword ptr [rcx + 0x16f8]')
exact(0x3A674D, 'mov', 'qword ptr [rcx + 0x1710], r9')
exact(0x3A6758, 'mov', 'byte ptr [rcx + 0x1718], r8b')
exact(0x3A678A, 'call', 'qword ptr [rax + 8]')
exact(0x3A6780, 'lea', 'edx, [r15 + 2]')
exact(0x3A67AF, 'movups', 'xmmword ptr [rsi + 0x18], xmm0')
exact(0x3A67C4, 'call', 'qword ptr [rax + 8]')
branch(0x3A67F0, 'jmp', 0x3A5DA0)
check(not any(o.type == X86_OP_MEM and o.mem.disp in (0x17F0, 0x17F4)
              for x in md.disasm(img[0x3A6710:0x3A67F5], 0x3A6710) for o in x.operands),
      'Merman starter does not recheck local mode/cooldown admission')
exact(0x3A6EE1, 'mov', 'r8, qword ptr [rcx + 0x1710]')
exact(0x3A6F17, 'mov', 'r8d, dword ptr [r8 + 0x1c]')
branch(0x3A6F2C, 'call', 0x116BFC0)
branch(0x3A6F65, 'call', 0x116DF20)
exact(0x3A701E, 'mov', 'qword ptr [rcx + 0x1710], 0')
branch(0x3A703C, 'jmp', 0x6C8760)

# Stock tag6 carries three bytes: tag, signed row ordinal, bool. It carries no
# seed or canonical target. The custom stream must therefore suppress this
# duplicate sender while using the full native setter.
exact(0x3A5DC3, 'test', 'byte ptr [rcx + 0x128], 1')
branch(0x3A5DCA, 'jne', 0x3A5E3F)
exact(0x3A5DEF, 'mov', 'dl, 6')
for at in (0x3A5DF6, 0x3A5E04, 0x3A5E12):
    branch(at, 'call', 0x12B5790)
exact(0x3A5E25, 'call', 'qword ptr [rax + 0x70]')
check(struct.unpack_from('<Q', img, 0x17B5530 + 15 * 8)[0] - base == 0x3A6410,
      'Merman stock tag receiver')
for at in (0x3A6444, 0x3A645B, 0x3A6467):
    branch(at, 'call', 0x12B4BB0)
exact(0x3A6460, 'movsx', 'r14, al')
exact(0x3A64F9, 'mov', 'qword ptr [rsi + 0x1710], rcx')
exact(0x3A6500, 'mov', 'byte ptr [rsi + 0x1718], bpl')

# A late pre-control stock tag6 has no custom epoch/serial and would enter the
# same native state again. A receiver hook may suppress ONLY that tag while a
# current Merman control role exists. Peeking must use the native bookmark API
# and restore it before either swallowing tag6 or forwarding every other tag.
branch(0x3A643B, 'call', 0x12B48A0)
branch(0x3A65BD, 'call', 0x12B4FD0)
exact(0x12B48A0, 'mov', 'rax, qword ptr [rcx + 8]')
exact(0x12B48A4, 'mov', 'r8d, 0xffffffff')
branch(0x12B48AD, 'jne', 0x12B48B6)
exact(0x12B48AF, 'movsxd', 'rax, dword ptr [rcx + 0x5f0]')
exact(0x12B48B6, 'mov', 'qword ptr [rdx], rax')
branch(0x12B4BB0, 'jmp', 0x12B4660)
exact(0x12B4674, 'mov', 'qword ptr [rcx + 8], rbx')
branch(0x12B4FD9, 'je', 0x12B4FE0)
exact(0x12B4FD0, 'mov', 'eax, 0xffffffff')
exact(0x12B4FDB, 'mov', 'qword ptr [rcx + 8], rdx')
exact(0x12B4FE0, 'mov', 'qword ptr [rcx + 0x5f0], rdx')
# Stock 3A6410 forwards non-tag6 messages only after restoring the same bookmark.
# This is why faking its first tag value is not a safe substitute for the hook.
branch(0x3A659D, 'call', 0x12B4FD0)
branch(0x3A65A8, 'call', 0x318350)

# Suppress the sender at its actual callers instead of borrowing remote-owner
# flags around state0: CAS/MAB entry commands must retain the original authority.
# Both call and tail-jump sites use (root, int ordinal, bool selected) unchanged.
for at, kind in ((0x3A3A29, 'call'), (0x3A6235, 'jmp'),
                 (0x3A6587, 'call'), (0x3A67F0, 'jmp')):
    branch(at, kind, 0x3A5DA0)
code = next(section for section in pe.sections if section.Name.rstrip(b'\0') == b'.text')
start, stop = code.VirtualAddress, code.VirtualAddress + code.Misc_VirtualSize
senders = set()
for opcode in (b'\xe8', b'\xe9'):
    at = img.find(opcode, start, stop)
    while at >= 0:
        if at + 5 <= stop and at + 5 + struct.unpack_from('<i', img, at + 1)[0] == 0x3A5DA0:
            senders.add(at)
        at = img.find(opcode, at + 1, stop)
check(senders == {0x3A3A29, 0x3A6235, 0x3A6587, 0x3A67F0},
      'all supported-image direct/tail Merman tag6 sender paths are enumerated')
# The fifth hook belongs to the native Humanoid named-state registry receiver;
# it uses the original signed ordinal ABI, not a forged named-action structure.
branch(0x476530, 'call', 0x4775E0)
exact(0x3A3A18, 'movzx', 'r8d, bpl')
exact(0x3A3A23, 'mov', 'edx, r14d')
exact(0x3A3A26, 'mov', 'rcx, rsi')
exact(0x3A657D, 'movzx', 'r8d, bpl')
exact(0x3A6581, 'mov', 'edx, r14d')
exact(0x3A6584, 'mov', 'rcx, rsi')
exact(0x3A67C7, 'movzx', 'r8d, bpl')
exact(0x3A67D2, 'mov', 'edx, r14d')
exact(0x3A67D5, 'mov', 'rcx, rdi')


def function_body(marker):
    match = re.search(re.escape(marker) + r'[^;{]*\{', source)
    check(match is not None, f'source function definition {marker}')
    brace = match.end() - 1
    level = 1
    end = brace + 1
    while level:
        if source[end] == '{':
            level += 1
        elif source[end] == '}':
            level -= 1
        end += 1
    return source[brace + 1:end - 1]


# Local source contracts complement the native instruction evidence above.
# In particular, extension Restore can clear its Stream before the root role is
# finally released; late stock messages must remain suppressed throughout that
# retained native restoration journal, even when no command Stream remains.
role = function_body('bool RoleLease(')
check('NativeActionRole(actor,&authority)' in role and 'table[-1]' in role
      and 'Profiles(&count)' in role,
      'hook role verifies the current native root identity and original primary COL')
receiver = function_body('void __fastcall MermanReceiver(')
check('if(RoleLease(actor,0x17B5370))' in receiver,
      'late stock tag6 isolation depends on the retained root role, not Stream lifetime')
check('if(stream)stream->faulted=true' in receiver,
      'command Stream is optional error bookkeeping after extension restoration')
check('0x12B48A0)(packet,&bookmark)' in receiver
      and '0x12B4BB0)(packet)' in receiver
      and '__finally' in receiver and '0x12B4FD0)(packet,bookmark)' in receiver,
      'receiver peeks and restores only the native logical cursor')
check('cursor==0xFFFFFFFFull' in receiver and 'tag==6)return' in receiver
      and '0x3A6410)(receiver,packet)' in receiver,
      'receiver rejects writer mode, suppresses only stock tag6, and forwards unowned messages')
sender = function_body('void __fastcall MermanSender(')
check('if(RoleLease(actor,0x17B5370))return' in sender
      and '0x3A5DA0)(actor,ordinal,selected)' in sender,
      'sender keeps stock arguments unchanged when no matching root role exists')
named = function_body('void __fastcall StockNamedEntry(')
check('At<void*>(machine,0x28)' in named and 'RoleLease(actor,0x17BEC68,&l)' in named
      and '0x4775E0)(machine,ordinal,parameter)' in named,
      'named stock entry resolves current owner independently of command Stream lifetime')
hooks = function_body('bool Hooks(')
check('0x476530,0x3A3A29,0x3A6235,0x3A6587,0x3A67F0' in hooks
      and '0x17B55A8' in hooks,
      'runtime hook sites are the five proven sender/receiver call boundaries')
invoke = function_body('bool Invoke(')
check('Put<unsigned>(l.object,0x128' not in invoke,
      'native CAS/MAB entry retains its real authority flags')

# Entry alone is not a shot outcome. These are actual later MAB/slot5 consumers
# which must remain native, or gain separate accepted outcomes if suppressed.
exact(0x4AC08E, 'mov', 'dword ptr [rbx + 0x1d10], edx')
exact(0x4AC094, 'mov', 'dword ptr [rbx + 0x1d14], 2')
branch(0x4ACDB9, 'call', 0x4BD100)
exact(0x4ACDD0, 'mov', 'dword ptr [rbx + 0x1d10], eax')
exact(0x4ACE35, 'mov', 'rax, qword ptr [rbx + 0x3e8]')
exact(0x4ACE50, 'mov', 'qword ptr [rbx + 0x3e8], rax')
branch(0x4ACE69, 'call', 0x6BB5A0)
branch(0x4ACE88, 'call', 0x1194280)
branch(0x4B39BB, 'call', 0x4B24D0)
exact(0x4B2832, 'mov', 'rcx, qword ptr [r15 + 0x3e8]')
exact(0x4B286F, 'mov', 'qword ptr [r15 + 0x3e8], rcx')

# When local game assets are present, prove delayed attack events in the actual
# source MABs without extracting or modifying any archive member.
game = Path(args.game) if args.game else dll.parent
if (game / 'Root.cpk').is_file():
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
    import rootcpk
    import sgo
    archive = rootcpk.Game(str(game))

    def text_at(data, at):
        end = at
        while data[end:end + 2] != b'\0\0':
            end += 2
        return data[at:end].decode('utf-16le')

    for name, attacks, fires in (('MONSTER501.SGO', 18, 2),
                                  ('MONSTER501_KING.SGO', 15, 2),
                                  ('E504_MONSTERA.SGO', 38, 10),
                                  ('E504_MONSTERA_M034_3.SGO', 38, 10)):
        values = sgo.read(archive.read('OBJECT', name))[1]
        check(len(values['attack_setting']) == attacks, f'{name}: exact attack ordinals')
        data = values['animation_model'][2]
        tracks = struct.unpack_from('<H', data, 0xE)[0]
        first = struct.unpack_from('<I', data, 0x18)[0]
        fire = []
        for track in range(first, first + tracks * 16, 16):
            keys, _end, _name, count = struct.unpack_from('<4i', data, track)
            for key in range(track + keys, track + keys + count * 16, 16):
                rel, time = struct.unpack_from('<if', data, key)
                if text_at(data, key + rel) == 'fire':
                    fire.append(time)
        check(len(fire) == fires and all(time > 0 for time in fire),
              f'{name}: real fire events occur after entry, not in command acceptance')

print('enemy accepted command native audit:', checks, 'checks passed')
