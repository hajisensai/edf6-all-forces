"""Read-only native/asset audit for Large and ported Hector individual arm guns.

Reads machine code as data. Does not load or execute EDF.dll, run the game,
install a plugin, or write game files. Native tag 10 carries whole-body attacks;
this audit does not claim that it can represent an individual gun ordinal.
"""
from pathlib import Path
import argparse
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
sys.path.insert(0, str(ROOT / 'tools'))
import dsgo
import edf41_objects
import rootcpk
import sgo

parser = argparse.ArgumentParser()
parser.add_argument('--dll', default='')
parser.add_argument('--game', default='')
args = parser.parse_args()
game = Path(args.game) if args.game else Path(args.dll).parent if args.dll else Path(rootcpk.DEFAULT_GAME)
dll = Path(args.dll) if args.dll else game / 'EDF.dll'
if not dll.is_file() or not (game / 'Root.cpk').is_file():
    print('SKIP: supported local EDF.dll/Root.cpk absent')
    raise SystemExit(77)

import capstone
import pefile
pe = pefile.PE(str(dll), fast_load=True)
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 0


def require(ok, evidence):
    global checks
    assert ok, evidence
    checks += 1


def instruction(at):
    return next(md.disasm(img[at:at + 16], at, count=1))


def exact(at, mnemonic, operands):
    x = instruction(at)
    require((x.mnemonic, x.op_str) == (mnemonic, operands),
            (hex(at), x.mnemonic, x.op_str, mnemonic, operands))


def branch(at, mnemonic, target):
    x = instruction(at)
    require(x.mnemonic == mnemonic and x.operands[0].type == capstone.x86.X86_OP_IMM
            and x.operands[0].imm == target, (hex(at), x.mnemonic, x.op_str, hex(target)))


def address(at, target):
    x = instruction(at)
    require(x.mnemonic == 'lea' and x.operands[1].mem.base == capstone.x86.X86_REG_RIP
            and x.address + x.size + x.operands[1].mem.disp == target,
            (hex(at), x.mnemonic, x.op_str, hex(target)))


require(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, 'unsupported EDF.dll timestamp')
# The production admission fingerprints must agree with the same actual image.
source = (ROOT / 'src/enemy_control_walking_guns.cpp').read_text(encoding='utf8')
fingerprints = re.findall(r'\{(0x[0-9A-F]+),\{([^}]+)\}\}', source)
require(bool(fingerprints), 'production native admission fingerprints absent')
for at, raw in fingerprints:
    data = bytes(int(v, 16) for v in raw.split(','))
    require(len(data) == 16 and pe.get_data(int(at, 16), len(data)) == data,
            ('production admission fingerprint', at))

# Stable configuration digests must read literal raw definitions. The normal
# numeric/string getters evaluate type-4 expression bytecode; neither evaluation
# results nor cached/overridden pointers can stand in for a definition identity.
exact(0x52DCA, 'mov', 'rdx, qword ptr [rax + 0x50]')
exact(0x52DD1, 'movsxd', 'rax, dword ptr [rdx + 0xc]')
exact(0x52DC6, 'shl', 'rcx, 4')
exact(0x52DD5, 'add', 'rax, rcx')
exact(0x52E60, 'cmp', 'qword ptr [rdx + 0x18], r9')
exact(0x52E66, 'mov', 'rax, qword ptr [rdx + 8]')
exact(0x52E6A, 'mov', 'r8d, dword ptr [rax + r9*4]')
exact(0x52E6E, 'cmp', 'r8d, -1')
exact(0x52E74, 'mov', 'rax, qword ptr [rdx + 0x28]')
exact(0x52E78, 'lea', 'rcx, [r8 + r8*4]')
exact(0x52E7C, 'lea', 'r8, [rax + rcx*8]')
exact(0x52E85, 'movzx', 'eax, word ptr [r8 + 0x20]')
exact(0x52E8B, 'mov', 'rdx, qword ptr [rdx + 0x50]')
exact(0x52E99, 'movsxd', 'rcx, dword ptr [rdx + 0xc]')
exact(0x52E9D, 'mov', 'eax, dword ptr [rcx + rax + 8]')
branch(0x52F34, 'call', 0x52F50)
branch(0x52F57, 'call', 0x52EB0)
branch(0x52F63, 'call', 0x55620)
branch(0x55702, 'jmp', 0x6DF20)
# Actual raw expression payload: node-relative block, block-relative data.
exact(0x6DF95, 'mov', 'rcx, qword ptr [r8 + 0x50]')
exact(0x6DFA0, 'movsxd', 'rdx, dword ptr [rcx + 0xc]')
exact(0x6DFAA, 'movsxd', 'rax, dword ptr [rdx]')
exact(0x6DFAD, 'add', 'rax, rdx')
exact(0x6DFB0, 'movsxd', 'rbx, dword ptr [rax + 4]')
exact(0x6DFB4, 'add', 'rbx, rax')
# NodeText has the same expression-evaluation branch. Literal string hashing
# must require type 1, then hash its UTF-16 code units rather than its address.
branch(0x551E7, 'call', 0x52EB0)
branch(0x551F3, 'call', 0x55620)
exact(0x55236, 'mov', 'rcx, qword ptr [r8 + 0x50]')
exact(0x5523E, 'movsxd', 'rax, dword ptr [rcx + 0xc]')
exact(0x55248, 'movsxd', 'rax, dword ptr [rdx]')
exact(0x5524B, 'add', 'rax, rdx')

# The root's vector is built in SGO gun_control order. Capacity and count differ;
# the row is a shared pointer pair, not a battery pointer or a packed live index.
exact(0x34C669, 'mov', 'qword ptr [rdi + 0x1aa8], rsi')
exact(0x34C670, 'mov', 'qword ptr [rdi + 0x1ab0], rsi')
exact(0x34C677, 'mov', 'qword ptr [rdi + 0x1ab8], rsi')
exact(0x34C99A, 'lea', 'r12, [rdi + 0xa8]')
address(0x34C9A8, 0x17ADDB8)
require(img[0x17ADDB8:0x17ADDD0] == 'gun_control\0'.encode('utf-16le'), 'native gun_control key')
branch(0x34C9B7, 'call', 0xD7480)
exact(0x34CAC0, 'movups', 'xmm0, xmmword ptr [rbp - 0x40]')
exact(0x34CAC4, 'movups', 'xmmword ptr [rbp + 0x48], xmm0')
exact(0x34CADE, 'lea', 'r8, [rdi + 0x8a0]')
exact(0x34CB04, 'lea', 'rcx, [r12 + 0x10]')
exact(0x34CB09, 'lea', 'r9, [rbp + 0x48]')
exact(0x34CB0D, 'mov', 'rdx, rdi')
branch(0x34CB10, 'call', 0x34D1C0)
exact(0x34CB27, 'mov', 'rax, qword ptr [rdi + 0x1ab0]')
exact(0x34CB2E, 'cmp', 'qword ptr [rdi + 0x1ab8], rax')
exact(0x34CCF0, 'mov', 'qword ptr [rdi + 0x1ab0], rax')
exact(0x34CD01, 'mov', 'rax, qword ptr [rdi + 0x1ab8]')
exact(0x34CD0B, 'shl', 'rcx, 4')
exact(0x34CD0F, 'add', 'rcx, qword ptr [rdi + 0x1aa8]')
exact(0x34CD19, 'mov', 'qword ptr [rdi + 0x1ab8], rax')
exact(0x34CD25, 'mov', 'qword ptr [rcx], rax')
exact(0x34CD28, 'mov', 'qword ptr [rcx + 8], r12')
exact(0x34CD34, 'inc', 'esi')
exact(0x34CD89, 'mov', 'edx, esi')
branch(0x34CD8F, 'call', 0x52DE0)
branch(0x34CDA6, 'jne', 0x34CAC0)
# GunControl retains its owner and GunBattery shared ownership; +38=-1 means
# unscheduled. The adapter must restore that value, not leave a queued shot.
exact(0x34D20A, 'mov', 'ebp, 0xffffffff')
exact(0x34D20F, 'mov', 'dword ptr [rcx + 0x38], ebp')
exact(0x34D280, 'mov', 'qword ptr [rdi + 0x10], rdx')
exact(0x34D311, 'mov', 'rdx, rbx')
branch(0x34D314, 'call', 0x6D0D90)
exact(0x34D31E, 'lea', 'rax, [rsi + 0x10]')
exact(0x34D322, 'mov', 'qword ptr [rdi], rax')
exact(0x34D329, 'mov', 'qword ptr [rdi + 8], rsi')
# The battery independently retains the actual GameObject owner weak pair.
exact(0x6D0DBA, 'mov', 'r15, rdx')
exact(0x6D1015, 'mov', 'rdx, qword ptr [r15 + 0x30]')
exact(0x6D1035, 'mov', 'r10, qword ptr [r15 + 0x28]')
exact(0x6D1039, 'mov', 'rdi, qword ptr [r15 + 0x30]')
branch(0x6D1054, 'call', 0x12DA7AA)
exact(0x6D1071, 'lock inc', 'dword ptr [rbx + 0xc]')
exact(0x6D107B, 'mov', 'qword ptr [rsi + 0x2b0], rcx')
exact(0x6D1089, 'mov', 'qword ptr [rsi + 0x2b8], rdx')

# A single native Start, with its own rejection gates, schedules the complete
# original gun lifecycle. Busy reports scheduling/count; it is not cooldown time.
exact(0x34E192, 'cmp', 'dword ptr [rcx + 0x38], 0')
branch(0x34E199, 'jg', 0x34E453)
exact(0x34E1A2, 'test', 'byte ptr [rax + 0x2c0], 1')
exact(0x34E1AB, 'cmp', 'dword ptr [rax + 0x2e0], 0')
exact(0x34E1B4, 'mov', 'ecx, dword ptr [rax + 0x30]')
exact(0x34E1B9, 'mov', 'ecx, dword ptr [rax + 0x2e0]')
branch(0x34E1C1, 'jne', 0x34E453)
exact(0x34E22E, 'mov', 'dword ptr [rbx + 0x38], eax')
branch(0x34E231, 'call', 0x34E470)
exact(0x34E480, 'mov', 'rcx, qword ptr [rcx + 0x10]')
branch(0x34E484, 'call', 0x30C0F0)
exact(0x34E640, 'cmp', 'dword ptr [rcx + 0x38], 0')
branch(0x34E644, 'jg', 0x34E66D)
exact(0x34E649, 'test', 'byte ptr [rax + 0x2c0], 1')
exact(0x34E652, 'cmp', 'dword ptr [rax + 0x2e0], 0')
exact(0x34E65B, 'mov', 'ecx, dword ptr [rax + 0x30]')
exact(0x34E660, 'mov', 'ecx, dword ptr [rax + 0x2e0]')
exact(0x34E66A, 'xor', 'al, al')
exact(0x34E66D, 'mov', 'al, 1')

# Root tick continues updating every configured gun: delay -> pending -> charge
# -> original burst -> real factory. Both initial and per-tick targets use the
# same native target provider that the shared canonical-aim hook intercepts.
exact(0x34F2A5, 'mov', 'rdi, qword ptr [rsi + 0x1aa8]')
exact(0x34F2AC, 'mov', 'rbp, qword ptr [rsi + 0x1ab8]')
exact(0x34F2B3, 'shl', 'rbp, 4')
branch(0x34F2E1, 'call', 0x34F900)
exact(0x34F913, 'mov', 'ecx, dword ptr [rcx + 0x38]')
exact(0x34F91D, 'mov', 'dword ptr [rbx + 0x38], eax')
exact(0x34F925, 'or', 'dword ptr [rax + 0x2c0], 1')
branch(0x34F978, 'call', 0x34E470)
branch(0x34FB98, 'jmp', 0x6D51C0)
exact(0x6D5463, 'test', 'al, 1')
exact(0x6D5474, 'mov', 'ecx, dword ptr [rdi + 0x140]')
exact(0x6D547A, 'mov', 'dword ptr [rdi + 0x2e8], ecx')
exact(0x6D5483, 'mov', 'dword ptr [rdi + 0x2e0], eax')
branch(0x6D54A2, 'call', 0x6D3A50)
exact(0x6D556A, 'mov', 'dword ptr [rdi + 0x2e8], eax')
exact(0x6D56A4, 'cmp', 'dword ptr [rdi + 0x2e0], 0')
exact(0x6D56BD, 'mov', 'dword ptr [rdi + 0x2e4], eax')
branch(0x6D5734, 'call', 0x6D2640)
exact(0x6D573C, 'mov', 'dword ptr [rdi + 0x2e4], eax')
exact(0x6D5742, 'dec', 'dword ptr [rdi + 0x2e0]')
exact(0x6D3308, 'mov', 'r8, qword ptr [rsi + 0x2f0]')
branch(0x6D331D, 'call', 0x1194280)
# Native interruption releases live bullet controls and the sound handle using
# its own stop/fade path, while clearing charge/count/pending. No CAS-only shot.
branch(0x34F5E6, 'call', 0x6D3EC0)
exact(0x6D3EC6, 'and', 'dword ptr [rcx + 0x2c0], 0xfffffffe')
branch(0x6D3ED0, 'call', 0x6D3D80)
branch(0x6D3E31, 'call', 0x2EDDE0)
exact(0x6D3EDA, 'mov', 'dword ptr [rbx + 0x2e8], eax')
exact(0x6D3EE0, 'mov', 'dword ptr [rbx + 0x2e0], eax')
exact(0x6D3EE6, 'lea', 'edx, [rax + 0x10]')
branch(0x6D3EEE, 'jmp', 0x7A8CD0)
exact(0x7A8D08, 'mov', 'dword ptr [rax + 0x30], 2')
exact(0x7A8D17, 'movss', 'dword ptr [rax + 0x34], xmm0')
branch(0x7A8D24, 'jmp', 0x7A8B00)
exact(0x7A8B1A, 'mov', 'qword ptr [rcx + 8], rbp')

# Native event tag 10 contains only signed-byte attack id, root seed, and action.
# Receiver delegates id to 34FE90: 0 melee, 1 ALL guns. An ordinal >=2 exits;
# treating a per-gun ordinal as this field would be an incorrect network adapter.
exact(0x34F414, 'mov', 'dl, 0xa')
branch(0x34F41B, 'call', 0x12B5790)
exact(0x34F420, 'movzx', 'edx, bl')
branch(0x34F428, 'call', 0x12B5790)
exact(0x34F42D, 'mov', 'rdx, qword ptr [rdi + 0x3e8]')
branch(0x34F439, 'call', 0x12B5690)
exact(0x34F43E, 'movsx', 'edx, byte ptr [rdi + 0x1940]')
branch(0x34F44A, 'call', 0x12B5580)
exact(0x34F6A0, 'lea', 'r8d, [rax - 9]')
exact(0x34F6B2, 'cmp', 'ecx, 1')
branch(0x34F6D1, 'call', 0x12B4BB0)
exact(0x34F6D6, 'movsx', 'ebx, al')
branch(0x34F6DC, 'call', 0x12B4BA0)
exact(0x34F6E1, 'mov', 'qword ptr [rsi + 0x3e8], rax')
branch(0x34F6EB, 'call', 0x12B4B90)
exact(0x34F6F0, 'mov', 'byte ptr [rsi + 0x1940], al')
branch(0x34F6FB, 'call', 0x34FE90)
exact(0x34FEA2, 'test', 'edx, edx')
branch(0x34FEA4, 'je', 0x34FF51)
exact(0x34FEAA, 'cmp', 'edx, 1')
branch(0x34FEAD, 'jne', 0x34FF56)
branch(0x34FEB3, 'call', 0x34E550)
exact(0x34FEC0, 'mov', 'rdi, qword ptr [rbx + 0x1aa8]')
exact(0x34FEC7, 'mov', 'rsi, qword ptr [rbx + 0x1ab8]')
exact(0x34FECE, 'shl', 'rsi, 4')
branch(0x34FEFE, 'call', 0x34E180)
exact(0x34FF38, 'add', 'rdi, 0x10')
branch(0x34FF51, 'call', 0x33EDA0)

# Every original Large variant has four actual configured gun batteries. Counts
# and slots come from the original Root.cpk rather than from the plugin adapter.
G = rootcpk.Game(str(game))
stock_expected = {'E602_BERSERKER_LARGE.SGO', 'E602_BERSERKER_LARGE_ACE.SGO',
                  'E602_BERSERKER_LARGE_LITE.SGO', 'E602_BERSERKER_LARGE_PUNCH.SGO'}
stock = set()
for name in G.names('OBJECT'):
    if not name.upper().endswith('.SGO') or 'BERSERKER_LARGE' not in name.upper():
        continue
    values = sgo.load(data=G.read('OBJECT', name))
    if values.get('xgs_scene_object_class') != 'EDF6_Berserker_Large':
        continue
    guns = values['gun_control']
    require(len(guns) == 4, ('stock gun count', name, len(guns)))
    for ordinal, gun in enumerate(guns):
        b = gun['gun_battery']
        require(bool(b['muzzle']['shape']) and bool(b['bullet']['factory']), (name, ordinal, 'muzzle/factory'))
        require(b['charge']['time'] > 0 and b['fire']['count'] > 0 and b['fire']['interval'] > 0,
                (name, ordinal, 'real native lifecycle'))
    stock.add(name)
    print(name, 'configured gun ordinals', ','.join(str(i) for i in range(len(guns))))
require(stock == stock_expected, ('stock Large variants', stock))

# Explicit original arm order, including the stock name exception 401SG = G/S.
# Verify the actual generated assets independently of the generator's code.
hector_arms = {
    'HECTOR301CC.SGO': 'CC', 'HECTOR301CG.SGO': 'CG', 'HECTOR301CL.SGO': 'CL',
    'HECTOR301GC.SGO': 'GC', 'HECTOR301GG.SGO': 'GG', 'HECTOR301GL.SGO': 'GL',
    'HECTOR301LC.SGO': 'LC', 'HECTOR301LG.SGO': 'LG', 'HECTOR301LL.SGO': 'LL',
    'HECTOR401CC.SGO': 'CC', 'HECTOR401CG.SGO': 'CG', 'HECTOR401CS.SGO': 'CS',
    'HECTOR401GG.SGO': 'GG', 'HECTOR401GL.SGO': 'GL', 'HECTOR401LC.SGO': 'LC',
    'HECTOR401LG.SGO': 'LG', 'HECTOR401LL.SGO': 'LL', 'HECTOR401LS.SGO': 'LS',
    'HECTOR401SC.SGO': 'SC', 'HECTOR401SG.SGO': 'GS', 'HECTOR401SS.SGO': 'SS',
}
rows = [r for r in edf41_objects.registry()['objects'] if r.get('class') == 'EDF6_Berserker_Large']
require(len(rows) == 21 and {Path(r['object']).name for r in rows} == set(hector_arms), 'all 21 ported Hectors')
shield_slots = 0
for row in rows:
    name = Path(row['object']).name
    raw = edf41_objects.read(row['object'])
    values = sgo.load(data=raw)
    tree = dsgo.parse(raw).root
    guns = values['gun_control']
    node_guns = tree.get('gun_control').items
    ranges = values['edf41_hector']['ranges']
    require(values['xgs_scene_object_class'] == 'EDF6_Berserker_Large', (name, 'native class'))
    require(len(guns) == len(node_guns) == len(ranges) == 2, (name, 'two stable gun ordinals'))
    for ordinal, (gun, node, arm, reach) in enumerate(zip(guns, node_guns, hector_arms[name], ranges)):
        b = gun['gun_battery']
        require(b['muzzle']['shape'] == '\u7832\u53f0' + chr(0xFF11 + ordinal), (name, ordinal, 'original muzzle ordinal'))
        damage = node.get('gun_battery').get('bullet').get('damage')
        if isinstance(damage, dsgo.Node):
            damage = damage.items[0]
        require(isinstance(damage, dsgo.Blob) and damage.kind == 4 and len(damage.data) == 28,
                (name, ordinal, 'damage expression'))
        opcode, coefficient, variable, level, multiply, end = struct.unpack('<IdIIII', damage.data)
        require((opcode, variable, level, multiply, end) == (1, 3, 11, 7, 0), (name, ordinal, 'coefficient times V11'))
        if arm == 'S':
            require(reach == 0 and coefficient == 0 and b['bullet']['factory'] == 'SolidBullet02',
                    (name, ordinal, 'non-damaging shield placeholder'))
            require(b['fire']['count'] == b['fire']['interval'] == 1, (name, ordinal, 'shield placeholder timing'))
            shield_slots += 1
        else:
            require(reach > 0 and coefficient > 0, (name, ordinal, 'real damaging arm'))
            require(b['fire']['count'] > 0 and b['fire']['interval'] > 0, (name, ordinal, 'real burst'))
    print(name, 'configured gun ordinals 0,1; actual arm kinds', hector_arms[name])
require(shield_slots == 6, ('shield placeholder slots', shield_slots))
# Rebuild each real file with its text pool reversed. This moves relative
# offsets and pool entries; a semantic identity must remain equal. A separate raw
# traversal follows the exact native node/block layouts above, and is compared
# with the repository's lossless DSGO value parser. No expression is evaluated.
def _slice(data, at, size):
    assert 0 <= at <= len(data) and 0 <= size <= len(data) - at, (at, size, len(data))
    return data[at:at + size]


def _u32(data, at):
    return struct.unpack('<I', _slice(data, at, 4))[0]


def _relative(data, at, field=0):
    return at + struct.unpack('<i', _slice(data, at + field, 4))[0]


def _text_bytes(data, at):
    out = bytearray()
    for i in range(1024):
        pair = _slice(data, at + 2 * i, 2)
        if pair == b'\0\0':
            return bytes(out)
        out += pair
    raise AssertionError('unterminated or excessive DSGO text')


def _node_at(data, index):
    assert data[:4] == b'DSGO' and _u32(data, 4) == _u32(data, 12) == 16
    assert 0 <= index < _u32(data, 8)
    at = 16 + 16 * index
    _slice(data, at, 16)
    assert _u32(data, at + 12) == 0
    return at


def _container(data, index):
    at = _node_at(data, index)
    assert _u32(data, at + 8) == 3
    block = _relative(data, at)
    _slice(data, block, 16)
    count, name_count = _u32(data, block + 12), _u32(data, block + 4)
    assert name_count <= count <= 1024
    indices_at = _relative(data, block, 8)
    kids = [_u32(data, indices_at + 4 * i) for i in range(count)]
    names = {}
    labels = set()
    names_at = _relative(data, block)
    for i in range(name_count):
        entry = names_at + 8 * i
        member = _u32(data, entry + 4)
        label = _text_bytes(data, _relative(data, entry))
        assert member < count and member not in names and label not in labels
        names[member] = label
        labels.add(label)
    return kids, names


def _raw_semantic(data, index, active=None):
    if active is None:
        active = set()
    assert index not in active and len(active) < 32
    active.add(index)
    at = _node_at(data, index)
    typ = _u32(data, at + 8)
    if typ == 0:
        out = struct.pack('<I', typ) + _slice(data, at, 8)
    elif typ == 1:
        text = _text_bytes(data, _relative(data, at))
        out = struct.pack('<II', typ, len(text) // 2) + text
    elif typ in (2, 4):
        block = _relative(data, at)
        size = _u32(data, block)
        assert size <= 1024 * 1024
        out = struct.pack('<II', typ, size) + _slice(data, _relative(data, block, 4), size)
    elif typ == 3:
        kids, names = _container(data, index)
        out = struct.pack('<II', typ, len(kids))
        for ordinal, child in enumerate(kids):
            out += struct.pack('<I', int(ordinal in names))
            if ordinal in names:
                text = names[ordinal]
                out += struct.pack('<I', len(text) // 2) + text
            out += _raw_semantic(data, child, active)
    else:
        raise AssertionError(('unsupported raw DSGO type', typ))
    active.remove(index)
    return out


def _value_semantic(value):
    if isinstance(value, float):
        return struct.pack('<Id', 0, value)
    if isinstance(value, str):
        text = value.encode('utf-16le')
        return struct.pack('<II', 1, len(text) // 2) + text
    if isinstance(value, dsgo.Blob):
        return struct.pack('<II', value.kind, len(value.data)) + value.data
    assert isinstance(value, dsgo.Node)
    out = struct.pack('<II', 3, len(value.items))
    for ordinal, child in enumerate(value.items):
        out += struct.pack('<I', int(ordinal in value.names))
        if ordinal in value.names:
            text = value.names[ordinal].encode('utf-16le')
            out += struct.pack('<I', len(text) // 2) + text
        out += _value_semantic(child)
    return out


def _gun_nodes(data):
    kids, names = _container(data, 0)
    member = [i for i, label in names.items() if label == 'gun_control'.encode('utf-16le')]
    assert len(member) == 1
    return _container(data, kids[member[0]])[0]


asset_rows = [(name, G.read('OBJECT', name)) for name in sorted(stock_expected)]
asset_rows += [(row['object'], edf41_objects.read(row['object'])) for row in rows]
semantic_guns = 0
for name, data in asset_rows:
    document = dsgo.parse(data)
    native_values = [_raw_semantic(data, i) for i in _gun_nodes(data)]
    parsed_values = [_value_semantic(gun) for gun in document.root.get('gun_control').items]
    require(native_values == parsed_values, (name, 'native raw layout matches lossless parser'))
    permuted = dsgo.write(dsgo.Document(document.root, list(reversed(document.pool))))
    require(data != permuted, (name, 'pool permutation actually moves storage'))
    require(native_values == [_raw_semantic(permuted, i) for i in _gun_nodes(permuted)],
            (name, 'stable definition independent of pool/relative offsets'))
    semantic_guns += len(native_values)
require(len(asset_rows) == 25 and semantic_guns == 58, ('all semantic gun definitions', len(asset_rows), semantic_guns))
print('Stable raw definitions:', len(asset_rows), 'SGOs /', semantic_guns, 'guns; pool permutation passed')
# Middle owns one GunBattery2; Shooter invokes that exact base constructor.
# Their actual emitter definition is root gun_battery_settings, not a made-up gun table.
address(0x3500F9, 0x17AE088)
branch(0x35021C, 'call', 0x6D0D90)
address(0x350187, 0x17AE3B8)
branch(0x350197, 'call', 0xD7480)
exact(0x350226, 'lea', 'rax, [rsi + 0x10]')
exact(0x35022A, 'mov', 'qword ptr [rdi + 0x1a60], rax')
exact(0x350238, 'mov', 'qword ptr [rdi + 0x1a68], rsi')
branch(0x35F175, 'call', 0x3500C0)
address(0x35F17B, 0x17AF9F8)
single_batteries = {
    'EDF6_Berserker_Middle': [
        'E602_BERSERKER_MIDDLE.SGO', 'E602_BERSERKER_MIDDLEUG.SGO',
        'E602_BERSERKER_MIDDLEUG_MG.SGO', 'E602_BERSERKER_MIDDLEUG_PUNCH.SGO',
        'E602_BERSERKER_MIDDLE_MG.SGO', 'E602_BERSERKER_MIDDLE_PUNCH.SGO',
        'E602_BERSERKER_MIDDLE_SPREAD.SGO'],
    'EDF6_Berserker_TypeShooter': [
        'E602_BERSERKERUG_BEAMA.SGO', 'E602_BERSERKERUG_BEAMB.SGO',
        'E602_BERSERKER_BEAMA.SGO', 'E602_BERSERKER_BEAMB.SGO'],
}
for family, files in single_batteries.items():
    for name in files:
        data = G.read('OBJECT', name)
        values = sgo.load(data=data)
        require(values['xgs_scene_object_class'] == family, (name, 'actual shared-battery family'))
        document = dsgo.parse(data)
        battery = document.root.get('gun_battery_settings')
        require(isinstance(battery, dsgo.Node) and len(battery.items) > 0, (name, 'actual complete root battery'))
        kids, names = _container(data, 0)
        members = [i for i, key in names.items() if key == 'gun_battery_settings'.encode('utf-16le')]
        require(len(members) == 1 and _raw_semantic(data, kids[members[0]]) == _value_semantic(battery),
                (name, 'raw single-battery subtree agrees with lossless parser'))
        permuted = dsgo.write(dsgo.Document(document.root, list(reversed(document.pool))))
        permuted_kids, permuted_names = _container(permuted, 0)
        permuted_member = next(i for i, key in permuted_names.items() if key == 'gun_battery_settings'.encode('utf-16le'))
        require(_raw_semantic(data, kids[members[0]]) == _raw_semantic(permuted, permuted_kids[permuted_member]),
                (name, 'single-battery semantic identity ignores text-pool layout'))
print('Shared native battery definitions:', sum(map(len, single_batteries.values())), 'Middle/Shooter SGOs verified')
print('Walking arm guns native/asset audit:', checks, 'checks passed')
