"""Read-only DLL proof for the Boss final-projectile provider boundary.

Disassembly only: no game DLL loading, execution, installation or asset writes.
"""
from pathlib import Path
import argparse
import re
import struct
import sys

parser = argparse.ArgumentParser()
parser.add_argument('native_dll', nargs='?', default='')
parser.add_argument('--dll', default='')
args = parser.parse_args()
path = Path(args.dll or args.native_dll)
if not (args.dll or args.native_dll) or not path.is_file():
    print('SKIP: pass supported EDF.dll path')
    raise SystemExit(77)
import capstone
import pefile
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
pe = pefile.PE(str(path), fast_load=True)
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
    value = ins(at)
    check((value.mnemonic, value.op_str) == (mnemonic, operands),
          f'{at:#x}: {value.mnemonic} {value.op_str}')

def call(at, target):
    value = ins(at)
    check(value.mnemonic == 'call' and value.operands[0].type == X86_OP_IMM
          and value.operands[0].imm == target, f'{at:#x}: call {target:#x}')

def address(at, target):
    value = ins(at)
    check(value.mnemonic == 'lea' and value.operands[1].type == X86_OP_MEM
          and value.operands[1].mem.base == X86_REG_RIP
          and value.address + value.size + value.operands[1].mem.disp == target,
          f'{at:#x}: address {target:#x}')

check(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664,
      'supported x64 EDF image')
root = Path(__file__).resolve().parents[1]
source = (root / 'src/enemy_control_boss_gun_net.cpp').read_text(encoding='utf-8-sig')
section = re.search(r'constexpr Sig kSigs\[\]=\{(.*?)\n\};', source, re.S)
check(section is not None, 'runtime signatures')
prints = re.findall(r'\{(0x[0-9A-F]+),\{([^}]+)\}\}', section.group(1))
check(len(prints) == 4, 'all independent gun/maker/factory/descriptor entries admitted')
for at, raw in prints:
    wanted = bytes(int(value, 16) for value in raw.split(','))
    check(len(wanted) == 16 and img[int(at, 16):int(at, 16) + 16] == wanted,
          f'fingerprint {at}')

# Stable part ordinals use the real map key, not a guessed string layout.
exact(0x440BF5, 'mov', 'r8, qword ptr [r8 + 0x10]')
exact(0x440BFC, 'cmp', 'qword ptr [rdi + 0x18], 8')
exact(0x440C03, 'mov', 'rdx, qword ptr [rdi]')
exact(0x440C16, 'add', 'r8, r8')
exact(0x440C8E, 'mov', 'ecx, 0x50')
exact(0x440CAB, 'movups', 'xmmword ptr [rax + 0x10], xmm0')
exact(0x440CB3, 'movups', 'xmmword ptr [rax + 0x20], xmm1')
call(0x442715, 0x440BD0)
exact(0x442722, 'movups', 'xmmword ptr [rdx + 0x30], xmm0')
exact(0x442746, 'mov', 'qword ptr [rdx + 0x40], rcx')
exact(0x44274E, 'mov', 'qword ptr [rdx + 0x48], rax')

# The admitted stock topology is complete before root construction returns.
# Only constructors insert/create; destruction owns removal. Hand destruction
# and native MAB actions therefore cannot legally reorder these stable ordinals.
text_section = next(section for section in pe.sections if section.Name.startswith(b'.text'))
text_begin = text_section.VirtualAddress
text_end = text_begin + text_section.Misc_VirtualSize

def callers(target):
    found = set()
    for opcode in (b'\xe8', b'\xe9'):
        at = img.find(opcode, text_begin, text_end)
        while at >= 0:
            if at + 5 <= text_end and at + 5 + struct.unpack_from('<i', img, at + 1)[0] == target:
                found.add(at)
            at = img.find(opcode, at + 1, text_end)
    return found

check(callers(0x440BD0) == {0x442715}, 'only root constructor inserts parts')
check(callers(0x44CDC0) == {0x44C853}, 'only HandMuzzle constructor creates its complete gun vector')
for target in (0x440BD0, 0x44CDC0, 0x44DD80):
    check(struct.pack('<Q', base + target) not in img, f'no indirect writer pointer {target:#x}')
call(0x44259B, 0x11945E0)
call(0x449AD9, 0x44C4C0)
call(0x44C853, 0x44CDC0)
call(0x449B2F, 0x449260)

# Private GunBattery RNG and temporary speed are not the root action RNG.
exact(0x44D2CA, 'mov', 'rax, qword ptr [rip + 0x1b06e57]')
exact(0x44D2EE, 'mov', 'qword ptr [rsi + 0x1f8], rax')
exact(0x44E476, 'movss', 'dword ptr [rax + 0x94], xmm8')
exact(0x44E489, 'mov', 'rax, qword ptr [rdi + 0x1f8]')
exact(0x44E49A, 'mov', 'qword ptr [rdi + 0x1f8], rax')
exact(0x44E4AD, 'mov', 'r8d, eax')
exact(0x44E4B0, 'lea', 'rdx, [rbp - 0x29]')
exact(0x44E4B4, 'lea', 'rcx, [rdi + 0x20]')
call(0x44E4B8, 0x6CFC60)
exact(0x44E4C7, 'movss', 'dword ptr [rax + 0x94], xmm9')

# The common launcher invokes one real bullet attempt per original iteration.
# Its guest path must keep running; intercept only the final Unit factory.
call(0x6CFE2A, 0x6D05C0)
exact(0x6D069E, 'mov', 'rcx, qword ptr [rsi]')
exact(0x6D06AC, 'mov', 'qword ptr [rsi], rcx')
exact(0x6D06BE, 'mov', 'dword ptr [r14 + 0x1a0], eax')
call(0x6D06E5, 0x4E820)
call(0x6D06F3, 0x4E220)
exact(0x6D06F8, 'movups', 'xmm7, xmmword ptr [r15 + 0x10]')
exact(0x6D08D5, 'lea', 'r8, [rsp + 0x40]')
exact(0x6D08DE, 'call', 'qword ptr [rax + 8]')
exact(0x6D0935, 'call', 'qword ptr [rax + 8]')
exact(0x6D0938, 'inc', 'dword ptr [r14 + 0x1a4]')
exact(0x6D093F, 'inc', 'dword ptr [rdi + 0x34]')
check(struct.unpack_from('<Q', img, 0x17E6EE0 + 8)[0] - base == 0x6CF7D0,
      'Bullet_TypeBullet native creation method')
check(struct.unpack_from('<Q', img, 0x17E6EE0 + 0x18)[0] - base == 0x6D0460,
      'ordinary bullet maker does not independently publish an entity NetID')
exact(0x6D0460, 'xor', 'al, al')
exact(0x6D0462, 'ret', '')
exact(0x22E9E5, 'mov', 'ebp, 0xffffffff')
exact(0x22E9EA, 'mov', 'dword ptr [rsi + 0x128], ebp')
exact(0x44CEC5, 'mov', 'qword ptr [rbx + 0x10], r15')
exact(0x6CF65D, 'mov', 'qword ptr [rax + 0x10], rbx')

# Holder+20 is the maker; its descriptor+A0 is a complete native 1E0
# BulletCreateInfo, not the maker's unrelated +90/+94 RNG/ordinal fields.
exact(0x6CF7EA, 'lea', 'rax, [rcx + 0xa0]')
exact(0x6CF7F1, 'mov', 'qword ptr [rsp + 0x20], rax')
exact(0x6CF7F6, 'mov', 'r9, qword ptr [rcx + 0x28]')
call(0x6CF806, 0x84C40)
# Stock TypeBullet construction creates its complete descriptor with null +70.
# The runtime additionally refuses a nonnull +70/hasMuzzle before acceptance;
# maker+70 below is only the unrelated short optional muzzle-effect descriptor.
exact(0x6CF5FF, 'mov', 'ecx, 0x280')
call(0x6CF619, 0x6CEDE0)
address(0x6CEE16, 0x17E6EE0)
exact(0x6CEE20, 'lea', 'rcx, [rbx + 0xa0]')
call(0x6CEE27, 0x1001D0)
exact(0x1001F1, 'xor', 'esi, esi')
exact(0x100207, 'add', 'rcx, 0x30')
exact(0x100234, 'mov', 'qword ptr [rcx + 0x40], rsi')
exact(0x6CFEF0, 'lea', 'rax, [rcx + 0xa0]')
address(0x1002EE, 0x1769028)
exact(0x1002F5, 'mov', 'qword ptr [rbx], rax')
exact(0x84C46, 'mov', 'rax, r9')
exact(0x84C49, 'mov', 'r10, r8')
exact(0x84C4C, 'mov', 'r9, qword ptr [rsp + 0x50]')
exact(0x84C54, 'mov', 'r8, rax')
exact(0x84C57, 'mov', 'rdx, r10')
call(0x84C5A, 0x1194280)
exact(0x84C64, 'test', 'rax, rax')
exact(0x84C69, 'mov', 'qword ptr [rsi], rdx')
exact(0x84C6C, 'mov', 'rax, rsi')
exact(0x84C6F, 'mov', 'qword ptr [rsi + 8], rdx')

# Post 6CF83E creates only an optional muzzle effect. It has no returned-bullet
# cache/list mutation. Local native ticks already execute it, even after null.
call(0x6CF83E, 0x6D0220)
exact(0x6D0238, 'mov', 'r8, qword ptr [rcx + 0x30]')
exact(0x6D023C, 'test', 'r8, r8')
exact(0x6D0244, 'mov', 'qword ptr [rcx + 0x68], rax')
exact(0x6D0248, 'mov', 'qword ptr [rcx + 0x70], r10')
exact(0x6D024C, 'lea', 'r9, [rcx + 0x38]')
call(0x6D025A, 0x1194280)
for token in ('RegisterNativeGunFactoryProvider', 'RegisterNativeUnitFactoryProvider',
              'RegisterNativeUnitFactoryCallsite(0x84C5A)', 'InvokeOriginalNativeGunFactory',
              'InvokeOriginalNativeUnitFactory', 'CaptureNativeProjectileSample',
              'ReplayNativeProjectileSampleInPlace', 'NativeProjectileJournalEmpty', 'RestoreNativeProjectileJournal', 'RootRole(root,&authority)',
              'scope.suppress=!valid || !authority', 'if(!result)return nullptr;',
              '*p++=a.resourceMask', 'a.resourceMask=*p++'):
    check(token in source, f'production scope/sample contract: {token}')
for token in ('!At<const void*>(b.descriptor,0x70)', '!e.sample.hasMuzzle',
              'bool DropStrong(Stream& s,Ref r,bool* held)', '*held=false;s.cleanupUncertain=true;',
              'if(s->restoring || s->cleanupUncertain)return false;',
              'b.partHeld=true;', 'b.gunHeld=true;', 'b.makerHeld=true;',
              'DropStrong(*s,s->root,&s->rootHeld)', 'ResetStream(old)'):
    check(token in source, f'persistent native ownership and supported muzzle contract: {token}')
check('RedirectCall(' not in source, 'only central dispatcher owns native call patches')
print(f'Boss final projectile native audit: {checks} checks passed')
