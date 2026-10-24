"""Read-only supported-EDF.dll contracts for the five enemy host-pose fallbacks.

Checks production fingerprints and the real native body/kinematic paths. This
never maps executable pages, loads EDF.dll, starts a game, or writes game files.
Run: python -B tests/enemy_control_native_pose_audit.py path/to/EDF.dll
Missing explicit DLL returns CTest skip code 77.
"""
from pathlib import Path
import re
import struct
import sys

if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass the supported EDF.dll path')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Run without -O')

import capstone
from capstone.x86 import X86_OP_IMM
import pefile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/enemy_control_native_pose.cpp').read_text(encoding='utf-8-sig')
pe = pefile.PE(sys.argv[1], fast_load=True)
image = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 0


def check(ok, why):
    global checks
    if not ok:
        raise AssertionError(why)
    checks += 1


def raw(rva, expected, why):
    wanted = bytes.fromhex(expected)
    check(image[rva:rva + len(wanted)] == wanted, f'{rva:#x}: {why}')


def branch(rva, target, mnemonic='call'):
    ins = next(md.disasm(image[rva:rva + 16], rva, count=1))
    check(ins.mnemonic == mnemonic and ins.operands[0].type == X86_OP_IMM
          and ins.operands[0].imm == target, f'{rva:#x}: {mnemonic} {target:#x}')


def code_only(text):
    # Reject actual call syntax. A comment, diagnostic string, or kSigs entry
    # mentioning a forbidden update RVA is not an invocation.
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  ' ', text, flags=re.S)


def native_calls(text):
    code = code_only(text)
    return {int(at, 16) for at in re.findall(
        r'\bFn\s*<[^;{}]+?>\s*\(\s*(0x[0-9A-Fa-f]+)[uUlL]*\s*\)\s*\(', code)}


def body_after(signature):
    start = code.index(signature)
    opening = code.index('{', start)
    depth = 1
    for i in range(opening + 1, len(code)):
        depth += (code[i] == '{') - (code[i] == '}')
        if not depth:
            return code[opening + 1:i]
    raise AssertionError('unclosed production function: ' + signature)


check(pe.FILE_HEADER.Machine == 0x8664 and pe.FILE_HEADER.TimeDateStamp == 0x678CCB46,
      'supported x64 EDF.dll timestamp 678CCB46')

# Check every production fingerprint, including newly added ones automatically.
section = re.search(r'constexpr\s+Sig\s+kSigs\s*\[\s*\]\s*=\s*\{(.*?)\n\};', source, re.S)
check(section is not None, 'production kSigs table exists')
fingerprints = re.findall(r'\{\s*(0x[0-9A-Fa-f]+)[uUlL]*\s*,\s*\{([^}]+)\}\s*\}', section.group(1))
check(len(fingerprints) >= 18, 'the complete native pose contract is fingerprinted')
seen = set()
for at, values in fingerprints:
    rva = int(at, 16)
    values = bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})(?![0-9A-Fa-f])', values))
    check(rva not in seen and len(values) == 16, f'{rva:#x}: unique complete 16-byte fingerprint')
    check(image[rva:rva + 16] == values, f'{rva:#x}: production fingerprint matches supported DLL')
    seen.add(rva)

# Exact RTTI proves the native profiles have not been confused across families.
classes = (
    ('Deiroi', 0x17AA648, 0x17AA7B8),
    ('EDF6_GiantDangoLarge', 0x17AFEE0, 0x17B00A0),
    ('EDF6_TimeShip', 0x17B93C0, 0x17B9580),
    ('EDF6_SquidSmall', 0x17B8898, 0x17B8A58),
    ('UfoSmall515', 0x17CC838, 0x17CC9A8),
)
for name, primary, secondary in classes:
    descriptors = []
    for vt, offset in ((primary, 0), (secondary, 0x120)):
        col = struct.unpack_from('<Q', image, vt - 8)[0] - base
        sig, actual, cd, td, hierarchy, self_rva = struct.unpack_from('<6I', image, col)
        check((sig, actual, cd, self_rva) == (1, offset, 0, col), name + ' exact primary/NetworkObject COL')
        end = image.index(b'\0', td + 16)
        check(image[td + 16:end] == ('.?AV' + name + '@@').encode(), name + ' exact native RTTI')
        descriptors.append(td)
    check(descriptors[0] == descriptors[1], name + ' primary and NetworkObject share one concrete type')

# Character-controller transform getter: wrapper -> body -> real read service.
raw(0x11AF6F6, '488bda488b11488b4a288b5218488b01ff9088000000488bc8488bd34883c4205be954e17cff',
    'RCX wrapper/RDX out matrix; read-service +28, body id +18, native vf+88')
branch(0x11AF717, 0x97D870, 'jmp')
raw(0x97D877, '0f105930', 'matrix getter takes true body position at transform+30')
raw(0x97D87B, '0f1001', 'matrix getter takes true body basis zero')
raw(0x97D88B, '0f104110', 'matrix getter takes true body basis one')
raw(0x97D87E, '0f104920', 'matrix getter takes true body basis two')
raw(0x11B9870, '488b09e9f86b0200', 'character warp dereferences wrapper and reaches native body setter')
raw(0x11E0490, '0f104a304c8d4d004c8b51304c8bc28b5118498bca',
    'warp passes full float transform and converted double position to body write service')
raw(0x11E04C2, 'ff9088000000', 'character warp commits through native world write vf+88')
branch(0x57B187, 0x11B9870)  # Stock Human dismount proves the exact same wrapper ABI.
branch(0x11AFA9F, 0x11E01C0)
raw(0x11E0259, '488b4d30', 'velocity setter uses body write service rather than render state')
raw(0x11E02D4, 'ff90a0000000', 'velocity setter reaches native world write vf+A0')
raw(0x11AF7E9, '488b11488b4a288b5218488b01ff90a0000000', 'character velocity getter uses actual world read vf+A0')
raw(0x11B1306, '488b8100010000', 'rigid velocity getter uses body world link +100')
raw(0x11B134A, '4883c060', 'rigid velocity getter returns motion linear velocity +60')
raw(0x11B190F, '48ffa0a8000000', 'rigid linear setter reaches world write vf+A8')
raw(0x11B177F, '48ffa0b0000000', 'rigid angular setter reaches world write vf+B0')

# Concrete hknpWorld reader/writer subobjects alias the same actual arrays.
# These are not computed scratch transform/velocity return buffers.
world_tds = []
for vt, offset in ((0x18BC9C0, 0x20), (0x18BC740, 0x18)):
    col = struct.unpack_from('<Q', image, vt - 8)[0] - base
    sig, actual, cd, td, hierarchy, self_rva = struct.unpack_from('<6I', image, col)
    check((sig, actual, cd, self_rva) == (1, offset, 0, col), f'{vt:#x}: concrete hknpWorld interface COL')
    end = image.index(b'\0', td + 16)
    check(image[td + 16:end] == b'.?AVhknpWorld@@', f'{vt:#x}: exact native world type')
    world_tds.append(td)
check(world_tds[0] == world_tds[1], 'reader and writer belong to the same complete hknpWorld')
for vt, byte_offset, target in ((0x18BC9C0, 0x80, 0xDAB170), (0x18BC9C0, 0x88, 0xDAB410),
                              (0x18BC9C0, 0xA0, 0xE4F0C0), (0x18BC9C0, 0xF8, 0xDABA20),
                              (0x18BC740, 0x88, 0xE51570), (0x18BC740, 0xA0, 0xE517B0)):
    check(struct.unpack_from('<Q', image, vt + byte_offset)[0] == base + target,
          f'{vt:#x}: native world virtual {byte_offset:#x} targets {target:#x}')
raw(0xDAB170, '8bc225ffffff004869c0b000000048034118c3', 'world body query uses readThis+18 and masked body-ID stride B0')
raw(0xDAB410, '8bc225ffffff004869c0b000000048034118c3', 'world transform query returns the same native body, not a copy')
raw(0xE5158F, '4869f8b0000000', 'world transform setter uses the same B0 body stride')
raw(0xE5159B, '48037920', 'writer body base writeThis+20 aliases readerThis+18 at complete-world+38')
check(0x20 + 0x18 == 0x18 + 0x20, 'reader/writer body-array fields have the same complete-object address')
raw(0xDABA20, '4863c248c1e00748038158010000c3', 'world motion query uses motion-ID stride 80 and readThis+158')
raw(0xE4F0C6, '488b01488bd9ff9080000000488b13488bcb4c8b82f80000008b908000000041ffd04883c060',
    'character raw velocity query goes body+80 motion-ID through vfF8 and returns motion+60')
raw(0xE5184C, '8bb88000000048c1e7074903be60010000', 'writer velocity uses same motion-ID stride and writeThis+160')
check(0x20 + 0x158 == 0x18 + 0x160, 'reader/writer motion-array fields alias complete-world+178')
raw(0xE519BA, '0f116760', 'native velocity setter writes the exact queried linear velocity at motion+60')
raw(0xE51A0C, '0f115760', 'native speed-limited branch also writes queried motion+60')
raw(0xDF68A4, '0f10110f106170', 'angular getter reads motion quaternion and local angular velocity +70')
branch(0x11B10C8, 0xDF68A0)
raw(0x11B10D5, '660f7f07', 'native angular getter uses MOVDQA and requires aligned output')
branch(0xE5165F, 0xDE96C0)
raw(0xDE9727, '0f135940', 'native transform setter also writes position precision residual at body+40')
raw(0xDE973A, 'f30f115948', 'native transform setter touches through body+4B beyond the 64-byte float matrix')

# Rag pose target is binding * bone world; it is never merely the actor root.
raw(0x6E913B, '4c8b4108', 'rag record references its binding at +8')
raw(0x6E9179, '498b4040', 'binding references the bone handle at +40')
raw(0x6E918C, '488b08', 'binding dereferences the real bone node')
raw(0x6E9197, '0f10a9c00000000f10b1b00000000f10b9d0000000440f1081e0000000',
    'rag target reads all four bone-world rows +B0/+C0/+D0/+E0')
branch(0x6E928E, 0x511C0)
branch(0x6E929B, 0x97D9D0)
raw(0x97D9FD, '0f1169300f11010f1161100f115920', 'rag target writes full basis and position in physical transform format')
raw(0x6ED630, '448b819c000000418d40fea9fdffffff', 'rag dispatcher reads native body mode +9C')
raw(0x6ED648, '4c8bc2488b91b0000000488b89b8000000', 'mode one forwards manager/node/transform to deferred kinematics')
branch(0x6ED659, 0x109B50, 'jmp')
raw(0x6ED65E, '488b49504533c0', 'mode two uses physical body +50 with non-dynamic warp flag')
branch(0x6ED665, 0x6CE850, 'jmp')
raw(0x109B50, '410f28000f294260410f2848100f294a70410f2840200f298280000000410f2848300f298a90000000',
    'mode one queues a complete transform in node+60..90')
raw(0x6CE878, '488b8100010000', 'direct rigid transform setter follows body+100 world')
raw(0x6CE88D, '8b91f0000000', 'direct rigid transform setter uses native body ID +F0')
raw(0x6CE897, '4d8b5318498d4b18', 'direct rigid transform setter selects true world write service +18')
raw(0x6CE8AE, '41ff9288000000', 'direct rigid transform setter commits through world vf+88')
raw(0x109BF0, '80bba000000000', 'deferred flush checks node dirty state +A0')
raw(0x109C9D, '488b53100f108b90000000', 'deferred flush obtains real body +10 and queued physical position +90')
raw(0x109CAB, '488b82000100008b92f0000000', 'deferred flush follows true world/body-ID, not model nodes alone')
raw(0x109CBC, '488b41184883c118', 'deferred flush selects world write service +18')
raw(0x109CD3, 'ff9088000000', 'deferred kinematic flush actually commits world vf+88')

# TimeShip pose-only rebuild, then model bones, then physical rag flush. Never
# call its complete Update again: that also advances phase/gameplay/damage.
raw(0x3F2CDC, 'f30f1089d00f0000', 'TimeShip pose-only helper reads current native phase +FD0')
raw(0x3F2D03, '4c8d83b00f0000', 'TimeShip helper reads current native orientation +FB0')
branch(0x3F2D14, 0x643D0)
branch(0x3F2D5C, 0x63F90)
raw(0x3F2D78, 'f30f108330140000', 'TimeShip helper reads native deformation +1430')
raw(0x3F2E85, '440f118b60080000', 'TimeShip helper rebuilds model world basis at +860')
raw(0x3F2EAC, '0f119390080000', 'TimeShip helper rebuilds model world position at +890')
raw(0x1100B90, 'c681b0000000010f2802488b41100f2980b0000000', 'model compose marks dirty and stores the actual bone-world transform')
branch(0x1100BC8, 0x1100010, 'jmp')
branch(0x6F15C3, 0x543920)  # Damage processing, explicitly forbidden as a pose helper.

# Large physical fields and quaternion semantics are independent of its visible
# rolling matrix. All three native quaternion slots are true XYZW rotations.
raw(0x360AC7, '488d8ff00f0000', 'Large constructs physical wrapper at actor+FF0')
branch(0x360ACE, 0x11B8B90)
raw(0x368857, '488d5660488d8ef00f0000', 'Large native frame gets real physical transform before visual rolling')
branch(0x368862, 0x11AF6F0)
raw(0x3687BC, '0f118eb00f0000', 'Large persists target Euler +FB0')
raw(0x3687DC, '0f108ec00f00000f1096b00f0000', 'Large interpolates current +FC0 toward target +FB0')
raw(0x368817, '0f1196c00f0000', 'Large persists current Euler +FC0')
raw(0x36155D, '488d8f70140000488d5760', 'Large rolling quaternion +1470 initializes from authored matrix')
branch(0x361568, 0x63DA0)
raw(0x36156D, '4c89b74414000048c7873c1400000000803fc7874c1400000000803f', 'Large delta quaternion +1440 initializes to identity')
raw(0x361597, '4c89b7641400004489b760140000c7876c1400000000803f', 'Large step quaternion +1460 initializes to identity')
branch(0x3685DB, 0x640D0)
branch(0x3685EE, 0x64500)
branch(0x362812, 0x64B90)
branch(0x36282B, 0x640D0)
branch(0x368838, 0x643D0)
branch(0x368852, 0x643D0)
raw(0x64552, 'f30f51c3', 'native quaternion normalization computes full length')
raw(0x64595, 'f30f59530cf30f11570c', 'native quaternion normalization includes W')
raw(0x11B994A, 'f30f587b34', 'character wrapper has a persistent vertical-velocity accumulator')
raw(0x11B9956, 'f30f117b34', 'character wrapper stores its persistent vertical accumulator')
raw(0x11B9AA1, '0f105330', 'character wrapper consumes separate velocity cache +30')

# Deiroi has indexed current/history control points and separate live-part
# caches. The original part updater refreshes bones/rag, then calls damage;
# the fallback intentionally replaces only the former pose/physics work.
raw(0x31F245, '488b97080b0000488b87180b00004c8d044049c1e0044c03c2', 'Deiroi current control-point pointer/count at B08/B18 use 0x30-byte records')
raw(0x31F26E, '48038f280b00000f10090f5c4a10', 'Deiroi reads history point B28 to compare current and previous geometry')
raw(0x31DD38, '488b86080b00008b4c1820488d14494803d2488b86280b0000', 'Deiroi current point record+20 indexes history points with 0x30 stride')
raw(0x327F9C, '0f118610060000488d8e20060000e8f1bdd3ff', 'Deiroi persists live part XYZ at 610 and quaternion at 620')
raw(0x32843F, '488d9620060000488d4e60e841bbd3ff0f1086100600000f118690000000', 'Deiroi part frame rebuilds orientation from 620 and translation from 610')
branch(0x32844A, 0x63F90)
raw(0x328469, '488b8ea00700004881c1a0000000488d5660', 'Deiroi part model 7A0+A0 receives its real world matrix')
branch(0x32847B, 0x1100B90)
raw(0x328480, '488b8e90070000', 'Deiroi part frame selects its own rag at 790')
branch(0x328487, 0x6ED800)
raw(0x32848C, '488b8eb0070000', 'Deiroi part collision object is separate from rag at 7B0')
branch(0x328493, 0x6F15B0)

# Squid's body wrapper is F90 and all current/target Euler components are
# gameplay-native caches. Its old native packet sends only one target angle.
raw(0x3E28F4, '4881c1900f0000', 'Squid native frame selects physical controller F90')
branch(0x3E28FB, 0x11B8DD0)
raw(0x3E2907, '0f118790000000', 'Squid native frame writes true physical position after readback')
raw(0x3E24F6, '0f1087d01200000f108fc01200000f5cc8', 'Squid interpolates current Euler 12D0 toward target Euler 12C0')
raw(0x3E2525, '0f118fd0120000', 'Squid persists interpolated current Euler 12D0')
raw(0x3E2582, 'f30f1087d0120000e82983ef00', 'Squid visible/native heading uses current pitch 12D0')
raw(0x3E25D7, 'f30f1087d4120000', 'Squid visible/native heading also uses current yaw 12D4')
raw(0x3E281A, '0f119fb00f0000', 'Squid controller desired velocity is actor FB0')
raw(0x3E286B, '488d8f900f0000', 'Squid steps the same real controller F90')
branch(0x3E2872, 0x11B9890)
raw(0x3E3A3C, 'f30f108bc0120000', 'old Squid writer only samples target angle X, not full yaw')
branch(0x3E3A47, 0x12B5350)
raw(0x3DEE31, 'f30f1186c0120000', 'old Squid reader only stores target angle X')

# Ufo515 has two explicit heading angles plus a bank state: physical pose alone
# cannot replace them because the native frame reconstructs the displayed basis.
raw(0x53767B, 'f30f1087a0100000f30f108f10060000', 'Ufo515 bank 10A0 depends on current native turn input 610')
raw(0x5376B3, 'f30f118fa0100000', 'Ufo515 frame persists native bank 10A0')
raw(0x5376C4, 'f30f108fb0100000488d4db0e85bd0b2ff', 'Ufo515 heading angle 10B0 produces a native quaternion')
branch(0x5376D0, 0x64730)
raw(0x5376D5, 'f30f108fb4100000488d4db0e81ad1b2ff', 'Ufo515 heading angle 10B4 produces the other native rotation')
branch(0x5376E1, 0x64800)
raw(0x5376E6, '488d55b0488d4f60e89dc8b2ff', 'Ufo515 converts native heading quaternion into its world basis')
branch(0x5376EE, 0x63F90)

# Source contract: read current native state, preflight before touching it, retain
# rollback on partial failure, and physically flush both rag modes. Comments and
# fingerprint constants must not satisfy or trip executable call guards.
code = code_only(source)
calls = native_calls(source)
for forbidden in (0x6F15B0, 0x543920, 0x31F6F0, 0x3EF610, 0x368490):
    check(forbidden not in calls, f'pose implementation never invokes damage/full Update {forbidden:#x}')
check(0x6F15B0 not in native_calls('// Fn<void(*)()>(0x6F15B0)();\nconst char* p="Fn<void(*)()>(0x6F15B0)();";'),
      'call guard ignores comments and diagnostic strings')
check(0x6F15B0 in native_calls('Fn<void(__fastcall*)(void*)>(0x6F15B0)(object);'), 'call guard recognizes actual forbidden Fn invocation')
for required in (0x11AF6F0, 0x11B9870, 0x11AFA70, 0x6E9130, 0x6ED630, 0x109B80, 0x3F2CB0, 0x1100B90):
    check(required in calls, f'production retains real native pose primitive {required:#x}')
apply = body_after('bool Apply(')
case_parts = re.split(r'case\s+PoseKind::(\w+)\s*:', apply)
case_bodies = {case_parts[i]: case_parts[i + 1] for i in range(1, len(case_parts), 2)}
for kind in ('large', 'squid', 'ufo515', 'timeShip', 'deiroi'):
    check(kind in case_bodies, 'production implements physical fallback for ' + kind)
for kind, offset in (('large', '0xFF0'), ('squid', '0xF90')):
    check(0x11B9870 in native_calls(case_bodies[kind]) and offset.lower() in case_bodies[kind].lower(), kind + ' uses true character warp at its own wrapper offset')
check(0x6CE820 in native_calls(case_bodies['ufo515']), 'Ufo515 calls real rigid-body wrapper setter')
skeleton_move = body_after('bool MoveSkeleton(')
check(0x1100B90 in native_calls(skeleton_move) and 'SameMatrix(' in skeleton_move and 'nodes+0xB0' in skeleton_move,
      'bone helper invokes native compose and verifies the resulting native root-bone world matrix')
check(0x3F2CB0 in native_calls(case_bodies['timeShip']) and 'MoveSkeleton(' in case_bodies['timeShip']
      and 'FlushRag(' in case_bodies['timeShip'], 'TimeShip rebuilds pose-only, verifies bone-world then commits physical rag')
check('d.points+i*0x30' in case_bodies['deiroi'] and 'd.previous+i*0x30' in case_bodies['deiroi']
      and 'p.object+0x610' in case_bodies['deiroi'] and 'p.object+0x620' in case_bodies['deiroi'],
      'Deiroi moves both current/history control points and each live part pose cache')
check('MoveSkeleton(' in case_bodies['deiroi'] and 'FlushRag(' in case_bodies['deiroi'], 'Deiroi verifies refreshed root/part bones and commits real rag physics')
flush = body_after('bool FlushRag(')
check({0x6E9130, 0x6ED630, 0x109B80} <= native_calls(flush), 'rag fallback derives bone targets, dispatches physical modes and flushes deferred kinematics')
check('mode!=1 && mode!=2' in flush and '0xA4' in flush, 'rag fallback ignores detached/dead debris and honors native active record state')
check('SameMatrix(got,m)' in apply and 'RigidMatrix(' in apply, 'fallback verifies physical readback rather than only a render matrix')
check('0x12C0,s.targetAngles' in case_bodies['squid'] and '0x12D0,s.currentAngles' in case_bodies['squid'], 'Squid fallback corrects both full native Euler caches')
check('0x10B0,s.currentAngles' in case_bodies['ufo515'] and '0x10A0,s.bank' in case_bodies['ufo515'], 'Ufo515 fallback corrects both heading angles and bank')
parts = body_after('bool Parts(')
for field in ('0xB08', '0xB18', '0xB28', '0xB38', '0xF60', '0xF70', '0x790', '0x7A0'):
    check(field in parts, 'Deiroi preflight retains native point/part field ' + field)

# Writable preflight reaches real world backing before the first undo/write,
# rather than probing only the game's small wrapper or recording descriptor.
compact = re.sub(r'\s+', '', code)
motion_storage = re.sub(r'\s+', '', body_after('bool MotionStorage('))
check('table[0x80/8]' in motion_storage and 'table[0xF8/8]' in motion_storage and 'raw,0x80' in motion_storage,
      'motion preflight obtains the real native motion via current body-ID and motion-ID')
motion_probe = re.search(r'Readable\(motion,(0x[0-9A-Fa-f]+|[0-9]+),true\)', motion_storage)
check(motion_probe is not None and int(motion_probe.group(1), 0) >= 0x80,
      'motion preflight checks complete writable native motion record including quaternion/linear/angular backing')
character_storage = re.sub(r'\s+', '', body_after('bool CharacterStorage('))
check('table[0x88/8]' in character_storage and 'table[0xA0/8]' in character_storage
      and 'MotionStorage(readService,id)' in character_storage,
      'character preflight queries real transform/velocity and motion backing')
character_probe = re.search(r'Readable\(matrix\(readService,id\),(0x[0-9A-Fa-f]+|[0-9]+),true\)', character_storage)
check(character_probe is not None and int(character_probe.group(1), 0) >= 0xB0,
      'character preflight checks complete writable B0 body record including position precision residual')
rigid_storage = re.sub(r'\s+', '', body_after('bool RigidStorage('))
rigid_probe = re.search(r'Readable\(query\(service,id\),(0x[0-9A-Fa-f]+|[0-9]+),true\)', rigid_storage)
check(rigid_probe is not None and int(rigid_probe.group(1), 0) >= 0xB0 and 'MotionStorage(service,id)' in rigid_storage,
      'rigid preflight checks complete writable B0 body record and native motion backing')
add_rigid = re.sub(r'\s+', '', body_after('bool AddRigid('))
check('alignas(16)floatangular[4]' in add_rigid and '0x11B1060)(body,angular)' in add_rigid,
      'native angular getter receives a 16-byte-aligned local before copying into journal')
rollback = body_after('bool Rollback(')
body_probe_end = max(rollback.find('CharacterStorage('), rollback.find('RigidStorage('))
first_cache_restore = rollback.find('std::memcpy(w.at,w.before,w.size)')
check(body_probe_end >= 0 and first_cache_restore > body_probe_end,
      'rollback validates all current native physical backing before undoing any cache byte')

# Preserve the low XYZ position residual too. A float-only wrapper warp loses
# this native precision, so rollback uses the real typed world-writer overload.
raw(0xE515AF, '0f1057300f104f40', 'native warp recovers original double position from float high and residual')
raw(0xE515C1, '660f58f0', 'native warp adds double-converted high and residual XY')
raw(0xE515CF, '660f58f8', 'native warp adds double-converted high and residual Z')
raw(0xDE9702, '0f136130', 'native writer stores float high position XY')
raw(0xDE9722, 'f30f116138', 'native writer stores float high position Z')
raw(0xDE9727, '0f135940', 'native writer stores low position residual XY')
raw(0xDE973A, 'f30f115948', 'native writer stores low position residual Z')
physics_position = re.sub(r'\s+', '', body_after('bool PhysicsPosition('))
check('static_cast<double>(At<float>(raw,0x30+i*4))+At<float>(raw,0x40+i*4)' in physics_position,
      'rollback snapshot recomposes native double XYZ using both high and residual components')
body_storage = re.sub(r'\s+', '', body_after('const unsigned char* BodyStorage('))
check('table[0x80/8]' in body_storage and 'query(readService,b.id)' in body_storage,
      'precise position snapshot resolves actual current native body storage')
restore_transform = re.sub(r'\s+', '', body_after('bool RestoreTransform('))
check('At<void*>(b.object,0x30)' in restore_transform and 'world,0x58)+0x18' in restore_transform,
      'precise rollback resolves the native writer interface for character and rigid bodies')
check('alignas(32)doubleposition[4]' in restore_transform and 'std::memcpy(position,b.position' in restore_transform,
      'precise rollback passes aligned typed double position, not opaque raw memory')
check('reinterpret_cast<void(__fastcall*)(void*,std::uint32_t,constfloat*,constdouble*,int)>(table[0x88/8])(writer,b.id,matrix,position,0)' in restore_transform,
      'rollback invokes the true world-writer vf88 with native body ID, float basis, double XYZ and zero flag')
check('PhysicsPosition(b,restored)' in restore_transform and 'restored[i]-b.position[i]' in restore_transform,
      'precise rollback verifies double position through the actual native body backing')
add_character = re.sub(r'\s+', '', body_after('bool AddCharacter('))
check('PhysicsPosition(b,b.position)' in add_character and 'PhysicsPosition(b,b.position)' in add_rigid,
      'both character and rigid journals capture native high-plus-residual position before mutation')
check(rollback.count('RestoreTransform(b,before)') == 2,
      'both rollback branches use precise world-writer restoration instead of float-only wrapper warp')

print(f'{checks} enemy-control native pose checks passed; {len(classes)} concrete families, {len(fingerprints)} production fingerprints; static PE/source audit only, no game or DLL execution')
