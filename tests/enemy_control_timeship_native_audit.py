"""Independent read-only TimeShip AS/native/SGO skill audit; never executes EDF.dll."""
from pathlib import Path
import argparse
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import rootcpk
import sgo

parser = argparse.ArgumentParser()
parser.add_argument('--dll', default='')
parser.add_argument('--game', default='')
args = parser.parse_args()
game = Path(args.game) if args.game else Path(args.dll).parent if args.dll else Path(rootcpk.DEFAULT_GAME)
dll = Path(args.dll) if args.dll else game / 'EDF.dll'
if not dll.is_file() or not (game / 'Root.cpk').is_file():
    print('SKIP: supported EDF.dll and original Root.cpk required')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O')
import capstone
import pefile

pe = pefile.PE(str(dll), fast_load=True)
assert pe.FILE_HEADER.Machine == 0x8664 and pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 1


def check(yes, label):
    global checks
    assert yes, label
    checks += 1


def ins(at):
    return next(md.disasm(img[at:at + 16], at, count=1))


def operand(at, expected):
    check(ins(at).op_str == expected, (hex(at), ins(at).op_str, expected))


def call(at, target):
    item = ins(at)
    check(item.mnemonic == 'call' and item.operands[0].type == capstone.x86.X86_OP_IMM and item.operands[0].imm == target,
          ('call', hex(at), item.op_str, hex(target)))


def lea(at, target):
    item = ins(at)
    check(item.mnemonic == 'lea' and item.operands[1].mem.base == capstone.x86.X86_REG_RIP and
          item.address + item.size + item.operands[1].mem.disp == target,
          ('lea', hex(at), item.op_str, hex(target)))


# Independent AS registrations prove wrapper signatures rather than inferring a
# native function ABI from a matching prologue.
lea(0x1F2AB4, 0x1CB6E0)
lea(0x1F2AD9, 0x1799638)
check(img[0x1799638:].startswith(b'::Object& EDF6_TimeShip_Warp(float )\0'), 'original Warp AS declaration')
lea(0x1F2B2A, 0x1CB610)
lea(0x1F2B4F, 0x1799688)
check(img[0x1799688:].startswith(b'::Object& EDF6_TimeShip_Warp2(float )\0'), 'original Warp2 AS declaration')
call(0x1CB74E, 0x3F3040)
call(0x1CB67E, 0x3F3160)
check(struct.unpack_from('<f', img, 0x1765A58)[0] == 60, 'AS seconds converted to native frames')
for at, target in [(0x3F3059, 'xmmword ptr [rcx + 0x60], xmm0'),
                   (0x3F307D, 'xmmword ptr [rcx + 0x90], xmm1'),
                   (0x3F30D6, 'dword ptr [rcx + 0x1470], r8d'),
                   (0x3F3179, 'xmmword ptr [rcx + 0x60], xmm0'),
                   (0x3F319D, 'xmmword ptr [rcx + 0x90], xmm1')]:
    operand(at, target)
lea(0x3F305D, 0x3F1250)
lea(0x3F317D, 0x3F1430)
operand(0x3EE691, 'dword ptr [rsi + 0x1470], 0x258')
lea(0x3EE6A2, 0x3F1430)
operand(0x3F1261, 'dword ptr [rcx + 0x1168], 7')
operand(0x3F14A0, 'dword ptr [rsi + 0x1168], 7')
call(0x3F13CA, 0x3F01F0)
call(0x3F17EE, 0x3F01F0)
lea(0x3F134A, 0x3F1C30)
lea(0x3F16E7, 0x3F1FB0)
lea(0x3F1EA5, 0x3F1170)
lea(0x3F225B, 0x3F1170)
check(struct.unpack_from('<f', img, 0x176615C)[0] == 120, 'both reappearance curves use native120 frames')
operand(0x3F1189, 'dword ptr [rcx + 0x1168], eax')
operand(0x3F2D70, 'xmm8, xmmword ptr [rbx + 0x90]')
operand(0x3F2EAC, 'xmmword ptr [rbx + 0x890], xmm2')
# The live render pipeline multiplies local effect translation with the world
# position. Resetting the effect matrix is not world teleportation.
operand(0x3F2E7D, 'xmm0, dword ptr [rbx + 0x146c]')
operand(0x3F2E9B, 'xmm0, xmm8')
# Canonical world target API, native admission and real release lifecycle.
call(0x1CB397, 0x3ECA80)
call(0x1CB2A7, 0x3ECAB0)
operand(0x3ECA87, 'byte ptr [rax + 0x120], 0')
operand(0x3ECA90, 'byte ptr [rcx + 0x1410], 1')
operand(0x3ECA9A, 'xmmword ptr [rcx + 0x1420], xmm0')
operand(0x3EF644, 'byte ptr [rcx + 0x1168], 4')
operand(0x3EF65E, 'byte ptr [rcx + 0x1410], r14b')
lea(0x3EF66C, 0x3F04D0)
operand(0x3F037B, 'xmmword ptr [rcx + 0x110], xmm0')
lea(0x3F036A, 0x3F0B00)
call(0x3F0A57, 0x3F3780) if ins(0x3F0A57).mnemonic == 'call' else check(ins(0x3F0A57).mnemonic == 'jmp' and ins(0x3F0A57).operands[0].imm == 0x3F3780, 'real anchor release tailcall')
operand(0x3EA59A, 'dword ptr [rcx + 0x58], 0xffffffff')
operand(0x3F37B0, 'byte ptr [rbx + 0x70], 1')
call(0x3F3814, 0x118AEA0)
operand(0x3F381B, 'qword ptr [rbx + 0x48], rax')
operand(0x3F3823, 'qword ptr [rbx + 0x50], rax')
operand(0x3F0BF3, 'byte ptr [rbx + 0x121], 1')
call(0x3F0A15, 0x118A1B0)
# Pods are native AS commands with explicit creation, not automatic SGO count.
call(0x1CB0CB, 0x3EC680)
for at, value in [(0x3EB047, 'qword ptr [rdi + 0x1140], rsi'), (0x3EB04E, 'qword ptr [rdi + 0x1148], rsi'),
                  (0x3EB055, 'qword ptr [rdi + 0x1150], rsi')]:
    operand(at, value)
operand(0x3EC73B, 'ebx, r13d')
operand(0x3EC73E, 'eax, ebx')
check(ins(0x3EC73E).mnemonic == 'cmovb', 'per-call min(requested, configured slots)')
operand(0x3EC875, 'rax, qword ptr [rdx + 0x18]')
operand(0x3EC884, 'rax')
call(0x3EC8D6, 0x54EE70)
call(0x1CB20F, 0x3ECC40)
call(0x3ECD83, 0x3ED9C0)
call(0x3ECF6A, 0x3F3780)
operand(0x3ECF7F, 'edi, 0x1e')
check(struct.unpack_from('<f', img, 0x1765A68)[0] == 100, 'AS deterministic target/radius seed scale')
# Source/runtime front door safety and the restore hook are additional regression
# checks; code-flow and original resource assertions above are independent.
source = (ROOT / 'src/enemy_control_timeship.cpp').read_text(encoding='utf-8-sig')
for phrase in ['EnemyControlNetworkContext(lease.object,&online) && !online', 'NativeActionRole(lease.object,&authority)', '!NativeActionQuiescing(lease.object)',
               'input.sequence==sequence', 'EnemyControlWeaponAim(lease.object,target)',
               'reinterpret_cast<WarpFn>', 'std::memcpy(out->values,lease.object+0x60',
               'RestoreTimeShipPodControl(lease)', 'extra==4']:
    check(phrase in source, 'production front door ' + phrase)
# The real AS wrappers also invoke these native entries outside slot7 AI. Gate
# their call sites, keeping accepted native replay callable on guests.
for at, target in [(0x1CB74E, 0x3F3040), (0x1CB67E, 0x3F3160),
                   (0x1CB397, 0x3ECA80), (0x1CB2A7, 0x3ECAB0),
                   (0x1CB857, 0x3F3280), (0x1CB7D7, 0x3F3290)]:
    call(at, target)
script = (ROOT / 'src/enemy_control_timeship_script.cpp').read_text(encoding='utf-8-sig')
for phrase in ['return !EnemyControlTargetActive(root);', 'NativeActionRole(root,&authority)',
               '!NativeActionQuiescing(root)', 'EnemyControlNetworkContext(root,&online)']:
    check(phrase in script, 'AS authority gate ' + phrase)
role = source[source.index('bool Role('):source.index('bool Fresh(')]
check(role.index('NativeActionRole(') < role.index('EnemyControlNetworkContext('), 'bound guest authority survives failed session probe')
allowed = script[script.index('bool Allowed('):script.index('void __fastcall Warp(')]
check(allowed.index('NativeActionRole(') < allowed.index('EnemyControlNetworkContext('), 'AS gate keeps bound role before fallback session probe')
check(allowed.index('NativeActionRole(') < allowed.index('EnemyControlTargetActive('), 'bound action drain cannot be bypassed by root-vtable restoration')
check('InSession(' not in role + allowed, 'unknown global probe cannot authorize a manual or controlled-script skill')
G = rootcpk.Game(str(game))
for name, anchor in [('E611_TIMESHIP.SGO', 'e612_anchor.sgo'),
                     ('E611_TIMESHIP_FULL_LIGHT.SGO', 'e612_anchor_full_light.sgo'),
                     ('E611_TIMESHIP_LIGHT.SGO', 'e612_anchor_light.sgo')]:
    settings = sgo.load(data=G.read('OBJECT', name))['EDF6_TimeShip']
    check(settings['Anchor']['link_node'] == 'anchorTarget', (name, 'true anchor mount'))
    check(settings['Anchor']['object'].lower() == 'app:/object/' + anchor, (name, 'true anchor resource'))
    pods = settings['Capsule']
    check(len(pods) == 8, (name, 'eight resource mounts'))
    for ordinal, item in enumerate(pods):
        check(item['link_node'] == 'capsuleTarget_' + chr(ord('A') + ordinal), (name, ordinal))
        check(item['object'].lower() == 'app:/object/e613_capsule.sgo', (name, 'true capsule'))
        check(sgo.load(data=G.read('OBJECT', 'E613_CAPSULE.SGO'))['xgs_scene_object_class'] == 'EDF6_TimeShip_Capsule', 'capsule factory')
    check(sgo.load(data=G.read('OBJECT', anchor.upper()))['xgs_scene_object_class'] == 'EDF6_TimeShip_Anchor', 'anchor factory')
capsule = sgo.load(data=G.read('OBJECT', 'E613_CAPSULE.SGO'))['EDF6_TimeShip_Capsule']
check(set(capsule) == {'sheets', 'se_list', 'destroy_object'}, 'original capsule has no invented occupant table')
for directory, method, expected in [('EDF6_TIMESHIPWARP', 'Warp', [0, 1, 2, 10]), ('EDF6_TIMESHIPWARP2', 'Warp2', [2, 4, 5, 10])]:
    text = G.read('MISSION/SAMPLE/' + directory, 'MISSION.AC').decode('utf-8-sig')
    values = [int(item) for item in re.findall(r'\.EDF6_TimeShip_' + method + r'\((\d+)\s*/\*', text)]
    check(values == expected, (directory, 'actual original script delay parameters'))
text = G.read('MISSION/SAMPLE/EDF6_TIMESHIP_DROPCAPSULE', 'MISSION.AC').decode('utf-8-sig')
check('EDF6_TimeShip_CreatePods(4' in text, 'original explicit four-pod preparation')
check('e503_frog_af.sgo' in text and 'e503_frog_sg.sgo' in text, 'original explicit pod occupants')
check(bool(re.search(r'EDF6_TimeShip_DropAllPods\([\s\S]*?50\.0,[\s\S]*?100\.0', text)), 'original50/100 radius example')
print(f'TimeShip native/AS/resources audit: {checks} checks')
