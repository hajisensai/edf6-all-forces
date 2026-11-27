"""The HQ's mission pack switch (src/pack_switch.cpp, docs/pack-switch-re.md) against the real game, read as files
(EDF.dll never loaded, the game never touched):

  - every signature InstallPackSwitch compares is the bytes at its address, each array checked once;
  - kHqSlot is slot 1 of ui::HUiHQMain's vtable (RTTI) and holds kHqUpdate; asCModule's vtable (RTTI) holds
    CompileFunction / GetFunctionByDecl at slots 6 / 13; createCoRoutine finds routines through
    ctx->GetFunction(0)->GetModule()->GetFunctionByDecl (the offsets the plugin calls);
  - 0x7E3180 writes the exit code GetUiExitCode reads (0x70EA90), and ResultCallback's set_close_exit calls it;
  - MAINSCRIPT.AS (Root.cpk): main() starts "string " + routine + "()", ApplyModeNo(int) is the script's own switch of
    mode, HQMain saves on Hui_HQMain_Return_GotoTitle (UI_COMMON_CONST.H: 10, LYT_HUIHQMAIN.SGO's Yes) and then
    returns "StartToTitle".

An EDF.dll path as the argument, else the game gamedir finds; exit 77 (skipped) without one."""
from __future__ import annotations

import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

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
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def qword(rva: int) -> int:
    return struct.unpack_from('<Q', img, rva)[0]


def rip(at: int, size: int) -> int:
    return at + size + struct.unpack_from('<i', img, at + size - 4)[0]


def rtti_name(vtable: int) -> str:
    col = qword(vtable - 8) - base
    check(struct.unpack_from('<I', img, col)[0] == 1, f'{vtable:#x}: an x64 CompleteObjectLocator')
    td = struct.unpack_from('<I', img, col + 0xC)[0]
    end = img.index(b'\0', td + 0x10)
    return img[td + 0x10:end].decode('ascii')


with open(os.path.join(ROOT, 'src', 'pack_switch.cpp'), encoding='utf-8') as f:
    text = f.read()
k = {m[0]: int(m[1], 16) for m in re.findall(r'\b(k\w+)=(0x[0-9A-Fa-f]+)\b', text)}
arrays = {name: bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
          for name, body in re.findall(r'const unsigned char (k\w+Sig)\[\]=\{([^}]*)\};', text, re.S)}
uses = re.findall(r'\{(k\w+|0x[0-9A-Fa-f]+),(k\w+Sig),sizeof\(\2\)\}', text)
checked = sorted(n for _, n in uses)
check(sorted(arrays) == sorted(checked + ['kCoRoutineSig']) and len(set(checked)) == len(checked),
      f'every signature array is in kSigs once (kCoRoutineSig: HookCoRoutine\'s own): {sorted(arrays)} {uses}')
for rva_text, name in uses:
    rva = int(rva_text, 16) if rva_text.startswith('0x') else k[rva_text]
    want = arrays[name]
    check(img[rva:rva + len(want)] == want, f'{name} at {rva:#x}: {img[rva:rva + len(want)].hex(" ")} != {want.hex(" ")}')
co = k['kCreateCoRoutine']
check(img[co:co + len(arrays['kCoRoutineSig'])] == arrays['kCoRoutineSig'], 'createCoRoutine\'s head')
check(img[co:co + 5] == bytes.fromhex('48895c2418'), 'its first instruction: the 5 bytes the trampoline runs')

# HUiHQMain's OnUpdate slot.
check(k['kHqSlot'] == 0x1804DD8 + 8 and rtti_name(0x1804DD8) == '.?AVHUiHQMain@ui@@', 'kHqSlot: ui::HUiHQMain slot 1')
check(qword(k['kHqSlot']) - base == k['kHqUpdate'], f'the slot holds {qword(k["kHqSlot"]) - base:#x}')

# AngelScript: the module's vtable, and createCoRoutine's lookup through the active context.
mv = k['kModuleVtable']
check(rtti_name(mv) == '.?AVasCModule@@', f'{mv:#x} is asCModule\'s vtable')
slots = {int(m[1]): m[0] for m in re.findall(r'\b(kMod\w+)=(\d+)\b', text)}
check(slots == {6: 'kModCompile', 13: 'kModByDecl'}, f'the module slots used: {slots}')
check(qword(mv + 6 * 8) - base == k['kCompileFunction'] and qword(mv + 13 * 8) - base == k['kFunctionByDecl'],
      'slot 6 CompileFunction, slot 13 GetFunctionByDecl')
check(rip(0x1294724, 5) == k['kActiveContext'], 'createCoRoutine calls asGetActiveContext first')
check(struct.unpack_from('<I', img, 0x129474B + 2)[0] == k['kCtxGetFunction'], 'ctx->GetFunction(0): vtable +0x148')
check(img[0x1294757 + 3] == k['kFuncGetModule'], '->GetModule(): vtable +0x30')
check(img[0x129477C + 5] == 13 * 8, '->GetFunctionByDecl: vtable +0x68 (slot 13)')
# The declaration is narrowed by 0x40890 between the module and the lookup (the plugin compares the wide string).
narrow = [at for at in range(0x129475B, 0x129477C) if img[at] == 0xE8 and rip(at, 5) == 0x40890]
check(len(narrow) == 1, f'one call to 0x40890 before GetFunctionByDecl: {[hex(a) for a in narrow]}')

# The exit code: 0x7E3180 stores it where GetUiExitCode reads it; ResultCallback's set_close_exit calls it.
check(rip(k['kCloseExit'] + 4, 6) == 0x20B39D8, '0x7E3180 writes 0x20B39D8')
check(img[0x70EA90:0x70EA92] == bytes.fromhex('8b05') and rip(0x70EA90, 6) == 0x20B39D8, 'GetUiExitCode reads it')
check(img[0x84FA62] == 0xE8, 'ResultCallback (0x84F780) set_close_exit: a call at 0x84FA62')
check(rip(0x84FA62, 5) == k['kCloseExit'], 'the call at 0x84FA62')

# The script and the layout (Root.cpk): skipped without the archive.
try:
    import rootcpk
    import sgo
    game = rootcpk.Game(os.path.dirname(os.path.abspath(path)))
    script = game.read('MAINSCRIPT', 'MAINSCRIPT.AS').decode('utf-8', 'replace').replace('\r', '')
    const = game.read('MAINSCRIPT', 'UI_COMMON_CONST.H').decode('utf-8', 'replace')
    layout = sgo.load(data=game.read('UI', 'LYT_HUIHQMAIN.SGO'))
except (OSError, KeyError) as e:
    print(f'pack_switch_native: {checks} checks on EDF.dll; MAINSCRIPT not read ({e}): skipped')
    raise SystemExit(0)
check('string exec = "string " + current_routine + "()";' in script and 'createCoRoutine(exec)' in script,
      'main() starts each routine by "string <name>()"')
check(re.search(r'\nbool ApplyModeNo\(int mode_index\)\n\{', script) is not None, 'bool ApplyModeNo(int) is the script\'s')
apply = script[script.index('\nbool ApplyModeNo(int mode_index)'):]
apply = apply[:apply.index('\n}\n')]
for need in ('SetMode(mode_index)', 'LoadSaveData_ModeSelect()', 'UnlockDownloadContents();', 'ModeChangeFixup'):
    check(need in apply, f'ApplyModeNo: {need}')
hq = script[script.index('\nstring HQMain()'):]
hq = hq[:hq.index('\n}\n')]
check(re.search(r'GetUiExitCode\(\) ==\s+Hui_HQMain_Return_GotoTitle\)\s*\{\s*SaveAllData_ReturnTitle\(true\);', hq)
      is not None, 'HQMain saves on Hui_HQMain_Return_GotoTitle')
check(hq.rstrip().endswith('return "StartToTitle";	//todo:') or hq.rstrip().split('\n')[-1].strip().startswith('return "StartToTitle";'),
      'and its last way out is StartToTitle')
check(re.search(r'Hui_HQMain_Return_GotoTitle\s*=\s*10\s*,', const) is not None and 'kGotoTitle=10;' in text,
      'Hui_HQMain_Return_GotoTitle = 10')
yes = None
for item in layout['ButtonGotoTitle'][1]:
    if item[0] == 'callback':
        for arg in item[1][1]:
            if arg[0] == 'init_args':
                yes = [a for a in arg[1] if a[0] == 'HUiMessageBoxButtonYes']
check(yes == [['HUiMessageBoxButtonYes', 'set_close_exit', 10]], f'the stock 回到标题 Yes: set_close_exit 10 ({yes})')
print(f'pack_switch_native: {checks} checks OK')
